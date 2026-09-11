#include "WaterwayRecognizer.h"

#include <opencv2/imgproc.hpp>

#include <QtMath>

#include <algorithm>
#include <cstring>
#include <cmath>

namespace {

QPointF gridPointToGcj02(const GeoReference &reference, const QSize &gridSize, const QPointF &cell)
{
    if (gridSize.width() <= 1 || gridSize.height() <= 1)
        return reference.centerGcj02;
    constexpr double pi = 3.14159265358979323846;
    const double worldSize = 256.0 * qPow(2.0, reference.zoom);
    auto world = [worldSize, pi](const QPointF &coordinate) {
        const double latitude = qBound(-85.05112878, coordinate.y(), 85.05112878);
        const double radians = qDegreesToRadians(latitude);
        return QPointF((coordinate.x() + 180.0) / 360.0 * worldSize,
            (1.0 - qLn(qTan(radians) + 1.0 / qCos(radians)) / pi) * 0.5 * worldSize);
    };
    auto inverse = [worldSize, pi](const QPointF &pixel) {
        double x = std::fmod(pixel.x(), worldSize);
        if (x < 0.0) x += worldSize;
        const double n = pi - 2.0 * pi * qBound(0.0, pixel.y(), worldSize) / worldSize;
        return QPointF(x / worldSize * 360.0 - 180.0,
                       qRadiansToDegrees(qAtan(std::sinh(n))));
    };
    const QPointF topLeft = world(reference.topLeftGcj02);
    const QPointF bottomRight = world(reference.bottomRightGcj02);
    const QPointF pixel(topLeft.x() + (bottomRight.x() - topLeft.x())
                            * cell.x() / double(gridSize.width() - 1),
                        topLeft.y() + (bottomRight.y() - topLeft.y())
                            * cell.y() / double(gridSize.height() - 1));
    return inverse(pixel);
}

QPointF gcj02ToGridPoint(const GeoReference &reference, const QSize &gridSize,
                         const QPointF &coordinate)
{
    if (gridSize.width() <= 1 || gridSize.height() <= 1)
        return QPointF();
    constexpr double pi = 3.14159265358979323846;
    const double worldSize = 256.0 * qPow(2.0, reference.zoom);
    const auto world = [worldSize, pi](const QPointF &value) {
        const double latitude = qBound(-85.05112878, value.y(), 85.05112878);
        const double radians = qDegreesToRadians(latitude);
        return QPointF((value.x() + 180.0) / 360.0 * worldSize,
            (1.0 - qLn(qTan(radians) + 1.0 / qCos(radians)) / pi) * 0.5 * worldSize);
    };
    const QPointF topLeft = world(reference.topLeftGcj02);
    const QPointF bottomRight = world(reference.bottomRightGcj02);
    const QPointF point = world(coordinate);
    const double width = bottomRight.x() - topLeft.x();
    const double height = bottomRight.y() - topLeft.y();
    if (qFuzzyIsNull(width) || qFuzzyIsNull(height)) return QPointF();
    return QPointF((point.x() - topLeft.x()) / width * (gridSize.width() - 1),
                   (point.y() - topLeft.y()) / height * (gridSize.height() - 1));
}

QPointF mercatorMeters(const QPointF &coordinate)
{
    constexpr double earthRadiusMeters = 6378137.0;
    constexpr double pi = 3.14159265358979323846;
    const double longitude = qBound(-180.0, coordinate.x(), 180.0);
    const double latitude = qBound(-85.05112878, coordinate.y(), 85.05112878);
    const double radians = qDegreesToRadians(latitude);
    return QPointF(earthRadiusMeters * qDegreesToRadians(longitude),
                   earthRadiusMeters * qLn(qTan(pi * 0.25 + radians * 0.5)));
}

QImage overlayFromExactMask(const cv::Mat &mask)
{
    QImage overlay(mask.cols, mask.rows, QImage::Format_ARGB32);
    overlay.fill(Qt::transparent);
    for (int y = 0; y < mask.rows; ++y) {
        const uchar *maskLine = mask.ptr<uchar>(y);
        QRgb *line = reinterpret_cast<QRgb *>(overlay.scanLine(y));
        for (int x = 0; x < mask.cols; ++x) {
            if (maskLine[x] != 0)
                line[x] = qRgba(91, 92, 255, 112);
        }
    }
    return overlay;
}

bool rebuildSafeNavigableMask(WaterwayGrid *waterway)
{
    if (!waterway || !waterway->isValid() || waterway->shoreSafetyMeters <= 0.0
        || waterway->mercatorCellMeters <= 0.0) {
        if (waterway) waterway->safeNavigableMask.clear();
        return true;
    }

    const int width = waterway->gridSize.width();
    const int height = waterway->gridSize.height();
    // Calculate shore distance from the final binary water mask itself.  A
    // simplified display/planning outline can move a few cells at a curved
    // bank, which is unacceptable for the 5 m safety margin.
    cv::Mat distanceSource(height, width, CV_8UC1,
                           const_cast<char *>(waterway->navigableMask.constData()));
    cv::Mat distances;
    cv::distanceTransform(distanceSource, distances, cv::DIST_L2, cv::DIST_MASK_PRECISE);

    waterway->safeNavigableMask.resize(width * height);
    waterway->safeNavigableMask.fill(0);
    for (int y = 0; y < height; ++y) {
        const double latitude = gridPointToGcj02(waterway->geoReference,
                                                  waterway->gridSize,
                                                  QPointF(width * 0.5, y)).y();
        const double groundMetersPerCell = waterway->mercatorCellMeters
            * qCos(qDegreesToRadians(latitude));
        const float *distanceRow = distances.ptr<float>(y);
        for (int x = 0; x < width; ++x) {
            const int index = y * width + x;
            if (static_cast<uchar>(waterway->navigableMask.at(index)) != 0
                && distanceRow[x] * groundMetersPerCell + 1e-6
                    >= waterway->shoreSafetyMeters) {
                waterway->safeNavigableMask[index] = char(255);
            }
        }
    }
    return true;
}

void rebuildOverlayAndBoundaries(WaterwayRecognitionResult *result);

bool rebuildConnectedRegionIds(WaterwayGrid *waterway)
{
    if (!waterway || !waterway->isValid())
        return false;

    const int width = waterway->gridSize.width();
    const int height = waterway->gridSize.height();
    cv::Mat mask(height, width, CV_8UC1,
                 reinterpret_cast<uchar *>(waterway->navigableMask.data()));
    cv::Mat componentLabels;
    cv::connectedComponents(mask, componentLabels, 8, CV_32S);
    waterway->connectedRegionIds.resize(width * height);
    for (int y = 0; y < height; ++y) {
        const int *row = componentLabels.ptr<int>(y);
        for (int x = 0; x < width; ++x)
            waterway->connectedRegionIds[y * width + x] = row[x];
    }
    return true;
}

cv::Mat qImageToBgr(const QImage &input)
{
    const QImage rgba = input.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat wrapped(rgba.height(), rgba.width(), CV_8UC4,
                    const_cast<uchar *>(rgba.constBits()), rgba.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(wrapped, bgr, cv::COLOR_RGBA2BGR);
    return bgr.clone();
}


void rebuildOverlayAndBoundaries(WaterwayRecognitionResult *result)
{
    if (!result || !result->waterway.gridSize.isValid()) return;
    const int width = result->waterway.gridSize.width();
    const int height = result->waterway.gridSize.height();
    cv::Mat mask(height, width, CV_8UC1,
                 reinterpret_cast<uchar *>(result->waterway.navigableMask.data()));
    // Preserve connected-region outlines as geographic polygons for external
    // planners and the device protocol.  The outline is simplified to roughly
    // one recognition cell so it remains compact on UDP.
    result->waterway.boundaryPolygons.clear();
    result->waterway.boundaryRegionIds.clear();
    cv::Mat contourMask(height, width, CV_8UC1,
                        reinterpret_cast<uchar *>(result->waterway.navigableMask.data()));
    contourMask = contourMask.clone();
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    // CCOMP keeps both the outside shoreline and land holes (islands, piers,
    // complex terrain enclosed by water).  Every contour is passed through
    // the project-side adapter as obstacle points; the third-party planner is
    // intentionally left untouched.
    cv::findContours(contourMask, contours, hierarchy, cv::RETR_CCOMP,
                     cv::CHAIN_APPROX_SIMPLE);
    // Keep the contour and its source sample together.  A contour may be
    // discarded after simplification; indexing the original contour vector
    // afterwards would then associate the following polygon with the wrong
    // connected component.
    struct ExtractedBoundary {
        QVector<QPointF> polygon;
        cv::Point sample;
    };
    std::vector<ExtractedBoundary> extracted;
    for (const std::vector<cv::Point> &contour : contours) {
        if (contour.size() < 3) continue;
        std::vector<cv::Point> simplified;
        cv::approxPolyDP(contour, simplified, 1.25, true);
        QVector<QPointF> polygon;
        for (const cv::Point &point : simplified)
            polygon.append(gridPointToGcj02(result->waterway.geoReference,
                                             result->waterway.gridSize,
                                             QPointF(point.x, point.y)));
        if (polygon.size() >= 3) {
            extracted.push_back({polygon, contour.front()});
        }
    }
    // Map every extracted shoreline to the adjacent connected water region.
    // Contour points are water-side pixels for both outer shores and holes;
    // the small neighbourhood probe also covers simplification/rounding.
    cv::Mat regionLabels;
    cv::connectedComponents(mask, regionLabels, 8, CV_32S);
    for (const ExtractedBoundary &boundary : extracted) {
        result->waterway.boundaryPolygons.append(boundary.polygon);
        const cv::Point p = boundary.sample;
        int regionId = 0;
        // findContours reports the outline pixel, which can be the first
        // background pixel just outside a one-cell shoreline. Probe a small
        // neighbourhood so the exported polygon still receives the correct
        // connected-component id.
        for (int radius = 0; radius <= 2 && regionId == 0; ++radius) {
            for (int dy = -radius; dy <= radius && regionId == 0; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int x = qBound(0, p.x + dx, width - 1);
                    const int y = qBound(0, p.y + dy, height - 1);
                    regionId = regionLabels.at<int>(y, x);
                    if (regionId != 0) break;
                }
            }
        }
        result->waterway.boundaryRegionIds.append(regionId);
    }
    result->success = true;
}

WaterwayRecognitionResult recognizeImage(const WaterwayRecognitionRequest &request)
{
    WaterwayRecognitionResult result;
    if (request.mapImage.isNull() || request.mapImage.width() < 64 || request.mapImage.height() < 64) {
        result.errorMessage = QStringLiteral("地图截图无效，请等待地图完成显示后重试");
        return result;
    }
    if (!request.geoReference.viewportPixels.isValid() || request.geoReference.zoom <= 0) {
        result.errorMessage = QStringLiteral("地图视口地理信息不可用");
        return result;
    }

    cv::Mat bgr = qImageToBgr(request.mapImage);
    const double scale = qMin(1.0, qMin(request.maximumGridSize.width() / double(bgr.cols),
                                       request.maximumGridSize.height() / double(bgr.rows)));
    const int width = qMax(32, qRound(bgr.cols * scale));
    const int height = qMax(24, qRound(bgr.rows * scale));
    // The only water source is the exact AMap fill RGB(178, 206, 254).
    // OpenCV stores BGR, so no hue, brightness, terrain or tolerance rule is
    // involved in this mask.
    const cv::Vec3b waterBgr(254, 206, 178);
    cv::Mat exactWaterMask(bgr.rows, bgr.cols, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < bgr.rows; ++y) {
        const cv::Vec3b *source = bgr.ptr<cv::Vec3b>(y);
        uchar *destination = exactWaterMask.ptr<uchar>(y);
        for (int x = 0; x < bgr.cols; ++x) {
            if (source[x] == waterBgr)
                destination[x] = 255;
        }
    }
    const int exactWaterPixels = cv::countNonZero(exactWaterMask);
    if (exactWaterPixels == 0) {
        result.errorMessage = QStringLiteral("当前视口未找到 RGB(178,206,254) 水域像素");
        return result;
    }
    result.overlayImage = overlayFromExactMask(exactWaterMask);

    result.waterway.gridSize = QSize(width, height);
    result.waterway.navigableMask.resize(width * height);
    cv::Mat planningMask;
    if (width == exactWaterMask.cols && height == exactWaterMask.rows)
        planningMask = exactWaterMask;
    else
        cv::resize(exactWaterMask, planningMask, cv::Size(width, height), 0.0, 0.0,
                   cv::INTER_NEAREST);
    for (int y = 0; y < height; ++y)
        memcpy(result.waterway.navigableMask.data() + y * width, planningMask.ptr(y), size_t(width));
    result.waterway.geoReference = request.geoReference;
    result.waterway.revision = request.geoReference.revision;
    result.waterway.shoreSafetyMeters = request.buildPlanningTopology
        ? qMax(0.0, request.shoreSafetyMeters) : 0.0;
    const QPointF topLeftMeters = mercatorMeters(request.geoReference.topLeftGcj02);
    const QPointF bottomRightMeters = mercatorMeters(request.geoReference.bottomRightGcj02);
    const double cellWidth = qAbs(bottomRightMeters.x() - topLeftMeters.x())
        / qMax(1, width - 1);
    const double cellHeight = qAbs(bottomRightMeters.y() - topLeftMeters.y())
        / qMax(1, height - 1);
    result.waterway.mercatorCellMeters = (cellWidth + cellHeight) * 0.5;
    result.confidence = 1.0;
    if (request.buildPlanningTopology) {
        if (!rebuildConnectedRegionIds(&result.waterway)) {
            result.errorMessage = QStringLiteral("当前视口水域拓扑重建失败");
            return result;
        }
        rebuildOverlayAndBoundaries(&result);
        if (!rebuildSafeNavigableMask(&result.waterway)) {
            result.errorMessage = QStringLiteral("当前视口安全水域重建失败");
            result.success = false;
            return result;
        }
    }
    result.success = true;
    return result;
}

} // namespace

WaterwayRecognitionWorker::WaterwayRecognitionWorker(QObject *parent) : QObject(parent) {}

WaterwayRecognitionWorker::~WaterwayRecognitionWorker()
{}

void WaterwayRecognitionWorker::recognize(const WaterwayRecognitionRequest &request)
{
    emit recognitionFinished(recognizeImage(request));
}

WaterwayRecognizer::WaterwayRecognizer(QObject *parent) : QObject(parent)
{
    m_worker = new WaterwayRecognitionWorker;
    m_worker->moveToThread(&m_workerThread);
    connect(this, &WaterwayRecognizer::recognitionRequested,
            m_worker, &WaterwayRecognitionWorker::recognize, Qt::QueuedConnection);
    connect(m_worker, &WaterwayRecognitionWorker::recognitionFinished,
            this, &WaterwayRecognizer::recognitionFinished, Qt::QueuedConnection);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_workerThread.start();
}

WaterwayRecognizer::~WaterwayRecognizer()
{
    m_workerThread.quit();
    m_workerThread.wait();
}

void WaterwayRecognizer::recognize(const WaterwayRecognitionRequest &request)
{
    emit recognitionRequested(request);
}
