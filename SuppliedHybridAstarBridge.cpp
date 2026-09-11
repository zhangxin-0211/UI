#include "SuppliedHybridAstarBridge.h"

#include "GeoCoordinateUtils.h"

#include <QLineF>
#include <QQueue>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// The supplied file is intentionally kept byte-for-byte unchanged.  Its demo
// entry point is renamed only by the preprocessor while this translation unit
// is compiled, so it cannot collide with the application's main.cpp and is
// never executed.
#define main supplied_hybrid_astar_demo_main
#include "third_party/hybrid_astar/main(1).cpp"
#undef main

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSuppliedShoreSafetyMeters = 3.0;
constexpr double kBoundarySpacingMeters = 2.0;
constexpr double kSuppliedWorldLimitMeters = 5000.0;
static_assert(kBoundarySpacingMeters < kSuppliedShoreSafetyMeters,
              "Shoreline obstacle samples must be closer than the safety radius");

// The supplied implementation's coarse grid is indexed from (0, 0) and
// cannot represent a goal west or south of its origin.  Keep the supplied
// source untouched by reflecting the local ENU axes for the duration of a
// call.  The path is reflected back before it reaches the application.
struct AxisTransform {
    double east = 1.0;
    double north = 1.0;
};

double wrapTo2PiAdapter(double angle)
{
    angle = std::fmod(angle, 2.0 * kPi);
    return angle < 0.0 ? angle + 2.0 * kPi : angle;
}

double headingBetween(const QPointF &from, const QPointF &to, double fallback)
{
    const double east = to.x() - from.x();
    const double north = to.y() - from.y();
    if (std::hypot(east, north) < 1e-12) return fallback;
    return wrapTo2PiAdapter(std::atan2(east, north));
}

double transformHeading(double heading, const AxisTransform &transform)
{
    return wrapTo2PiAdapter(std::atan2(transform.east * std::sin(heading),
                                       transform.north * std::cos(heading)));
}

GeoPoint syntheticGeoPoint(const GeoPoint &origin, const GeoPoint &original,
                           const AxisTransform &transform)
{
    const EnuPoint local = geo2enu(origin, original);
    return enu2geo(origin, EnuPoint(transform.east * local.e,
                                    transform.north * local.n));
}

std::vector<GeoPose> restorePathFromSynthetic(const GeoPoint &origin,
                                              const std::vector<GeoPose> &path,
                                              const AxisTransform &transform)
{
    std::vector<GeoPose> restored;
    restored.reserve(path.size());
    for (const GeoPose &pose : path) {
        const EnuPoint synthetic = geo2enu(origin, GeoPoint(pose.lon, pose.lat));
        const EnuPoint original(transform.east * synthetic.e,
                                transform.north * synthetic.n);
        const GeoPoint geographic = enu2geo(origin, original);
        restored.emplace_back(geographic.lon, geographic.lat,
                              transformHeading(pose.yaw, transform));
    }
    return restored;
}

bool pointInPolygon(const QVector<QPointF> &polygon, const QPointF &point)
{
    if (polygon.size() < 3) return false;
    bool inside = false;
    for (int i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const QPointF &a = polygon.at(i);
        const QPointF &b = polygon.at(j);
        const double vx = b.x() - a.x();
        const double vy = b.y() - a.y();
        const double length2 = vx * vx + vy * vy;
        if (length2 > 1e-18) {
            const double t = qBound(0.0,
                                    ((point.x() - a.x()) * vx
                                     + (point.y() - a.y()) * vy) / length2,
                                    1.0);
            if (std::hypot(point.x() - (a.x() + t * vx),
                           point.y() - (a.y() + t * vy)) <= 1e-8)
                return true;
        }
        if (((a.y() > point.y()) != (b.y() > point.y()))
            && point.x() < (b.x() - a.x()) * (point.y() - a.y())
                               / (b.y() - a.y()) + a.x()) {
            inside = !inside;
        }
    }
    return inside;
}

int regionForCell(const RoutePlanningRequest &request, const QPoint &cell)
{
    const WaterwayGrid &waterway = request.waterway;
    const int width = waterway.gridSize.width();
    const int height = waterway.gridSize.height();
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= width || cell.y() >= height)
        return 0;
    if (waterway.connectedRegionIds.size() == width * height)
        return waterway.connectedRegionIds.at(cell.y() * width + cell.x());
    return 0;
}

int regionForCoordinate(const RoutePlanningRequest &request, const QPointF &coordinate)
{
    const WaterwayGrid &waterway = request.waterway;
    for (int i = 0; i < waterway.boundaryPolygons.size(); ++i) {
        if (!pointInPolygon(waterway.boundaryPolygons.at(i), coordinate)) continue;
        if (i < waterway.boundaryRegionIds.size()
            && waterway.boundaryRegionIds.at(i) > 0)
            return waterway.boundaryRegionIds.at(i);
        return i + 1;
    }
    return 0;
}

int selectedRegion(const RoutePlanningRequest &request)
{
    int startRegion = regionForCell(request, request.startCell);
    int targetRegion = regionForCell(request, request.targetCell);
    // A contour can be one cell inside the source mask after simplification.
    // Fall back independently for either endpoint instead of rejecting the
    // route merely because one rounded cell has no component label.
    if (startRegion <= 0)
        startRegion = regionForCoordinate(request, request.startGcj02);
    if (targetRegion <= 0)
        targetRegion = regionForCoordinate(request, request.targetGcj02);
    return startRegion > 0 && startRegion == targetRegion ? startRegion : 0;
}

bool validGeo(const QPointF &point)
{
    return GeoCoordinateUtils::isValidLongitudeLatitude(point)
        && qIsFinite(point.x()) && qIsFinite(point.y());
}

bool safeCellsConnected(const RoutePlanningRequest &request, bool *startSafe,
                        bool *targetSafe)
{
    const WaterwayGrid &waterway = request.waterway;
    const int width = waterway.gridSize.width();
    const int height = waterway.gridSize.height();
    const QByteArray &mask = waterway.hasSafeNavigableMask()
        ? waterway.safeNavigableMask : waterway.navigableMask;
    const auto isSafe = [&](const QPoint &cell) {
        return cell.x() >= 0 && cell.y() >= 0 && cell.x() < width && cell.y() < height
            && mask.size() == width * height
            && static_cast<uchar>(mask.at(cell.y() * width + cell.x())) != 0;
    };
    const bool sourceIsSafe = isSafe(request.startCell);
    const bool destinationIsSafe = isSafe(request.targetCell);
    if (startSafe) *startSafe = sourceIsSafe;
    if (targetSafe) *targetSafe = destinationIsSafe;
    if (!sourceIsSafe || !destinationIsSafe) return false;

    QByteArray visited(width * height, char(0));
    QQueue<QPoint> pending;
    pending.enqueue(request.startCell);
    visited[request.startCell.y() * width + request.startCell.x()] = char(1);
    static const QPoint directions[] = {
        QPoint(-1, -1), QPoint(0, -1), QPoint(1, -1), QPoint(-1, 0),
        QPoint(1, 0), QPoint(-1, 1), QPoint(0, 1), QPoint(1, 1)
    };
    while (!pending.isEmpty()) {
        const QPoint current = pending.dequeue();
        if (current == request.targetCell) return true;
        for (const QPoint &direction : directions) {
            const QPoint next = current + direction;
            if (!isSafe(next)) continue;
            const int index = next.y() * width + next.x();
            if (visited.at(index) != 0) continue;
            visited[index] = char(1);
            pending.enqueue(next);
        }
    }
    return false;
}

int safeGridCoarsePathPointCount(const RoutePlanningRequest &request)
{
    const WaterwayGrid &waterway = request.waterway;
    const int width = waterway.gridSize.width();
    const int height = waterway.gridSize.height();
    const QByteArray &mask = waterway.hasSafeNavigableMask()
        ? waterway.safeNavigableMask : waterway.navigableMask;
    const auto isSafe = [&](const QPoint &cell) {
        return cell.x() >= 0 && cell.y() >= 0 && cell.x() < width && cell.y() < height
            && mask.size() == width * height
            && static_cast<uchar>(mask.at(cell.y() * width + cell.x())) != 0;
    };
    if (!isSafe(request.startCell) || !isSafe(request.targetCell)) return 0;

    const int startIndex = request.startCell.y() * width + request.startCell.x();
    const int targetIndex = request.targetCell.y() * width + request.targetCell.x();
    QVector<int> parents(width * height, -1);
    QQueue<QPoint> pending;
    pending.enqueue(request.startCell);
    parents[startIndex] = startIndex;
    static const QPoint directions[] = {
        QPoint(-1, -1), QPoint(0, -1), QPoint(1, -1), QPoint(-1, 0),
        QPoint(1, 0), QPoint(-1, 1), QPoint(0, 1), QPoint(1, 1)
    };
    while (!pending.isEmpty() && parents[targetIndex] < 0) {
        const QPoint current = pending.dequeue();
        for (const QPoint &direction : directions) {
            const QPoint next = current + direction;
            if (!isSafe(next)) continue;
            const int index = next.y() * width + next.x();
            if (parents[index] >= 0) continue;
            parents[index] = current.y() * width + current.x();
            pending.enqueue(next);
            if (index == targetIndex) break;
        }
    }
    if (parents[targetIndex] < 0) return 0;
    int points = 1;
    for (int index = targetIndex; index != startIndex; index = parents[index]) ++points;
    return points;
}

QString pointText(const QPointF &point)
{
    return QStringLiteral("(%1,%2)").arg(point.x(), 0, 'f', 6).arg(point.y(), 0, 'f', 6);
}

QString startActionDiagnostic(const GeoPose &start, const std::vector<EnuPoint> &obstacles,
                              const QVector<QPointF> &obstacleCoordinates)
{
    const std::vector<std::pair<QString, Primitive>> primitives = {
        {QStringLiteral("直行 12m"), Primitive(INFINITY, 12.0, 0.0)},
        {QStringLiteral("右转弧 6m"), Primitive(5.0, 6.0, 2.0)},
        {QStringLiteral("左转弧 6m"), Primitive(-5.0, 6.0, 2.0)},
        {QStringLiteral("右转弧 9m"), Primitive(5.0, 9.0, 5.0)},
        {QStringLiteral("左转弧 9m"), Primitive(-5.0, 9.0, 5.0)}
    };
    const EnuPose from(0.0, 0.0, start.yaw);
    for (const auto &entry : primitives) {
        const EnuPose to = propagate(from, entry.second.R, entry.second.length);
        double nearestDistance = std::numeric_limits<double>::infinity();
        int nearestIndex = -1;
        for (int sample = 0; sample <= 10; ++sample) {
            const double t = sample / 10.0;
            const double east = from.e + (to.e - from.e) * t;
            const double north = from.n + (to.n - from.n) * t;
            for (int index = 0; index < int(obstacles.size()); ++index) {
                const double distance = std::hypot(east - obstacles[size_t(index)].e,
                                                   north - obstacles[size_t(index)].n);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearestIndex = index;
                }
            }
        }
        if (nearestDistance >= kSuppliedShoreSafetyMeters) {
            continue;
        }
        const QPointF shoreline = nearestIndex >= 0 && nearestIndex < obstacleCoordinates.size()
            ? obstacleCoordinates.at(nearestIndex) : QPointF();
        return QStringLiteral("首个碰撞动作：%1，距岸线采样点 %2 m，岸线点 %3")
            .arg(entry.first)
            .arg(nearestDistance, 0, 'f', 1)
            .arg(pointText(shoreline));
    }
    return QStringLiteral("起始动作预检：5/5 可通行");
}

QString failureDiagnostic(const RoutePlanningRequest &request,
                          const QVector<QPointF> &obstacleCoordinates)
{
    bool startSafe = false;
    bool targetSafe = false;
    const bool connected = safeCellsConnected(request, &startSafe, &targetSafe);
    const QString connectivity = QStringLiteral("安全水域连通：%1（起点%2，目标%3）")
        .arg(connected ? QStringLiteral("是") : QStringLiteral("否"))
        .arg(startSafe ? QStringLiteral("安全") : QStringLiteral("不安全"))
        .arg(targetSafe ? QStringLiteral("安全") : QStringLiteral("不安全"));

    // The supplied entry point does not return its private coarse reference.
    // Use the same already-recognised safe water grid for a fast diagnostic
    // reference instead of running the expensive supplied 2-D search twice.
    const int safeGridCoarsePoints = safeGridCoarsePathPointCount(request);
    const QString coarseText = safeGridCoarsePoints > 0
        ? QStringLiteral("安全栅格粗路径：%1 点").arg(safeGridCoarsePoints)
        : QStringLiteral("安全栅格粗路径：未找到");

    const GeoPose start(request.startGcj02.x(), request.startGcj02.y(),
                        request.startHeadingRadians);
    const GeoPoint origin(start.lon, start.lat);
    std::vector<EnuPoint> obstacleEnu;
    obstacleEnu.reserve(obstacleCoordinates.size());
    for (const QPointF &obstacle : obstacleCoordinates)
        obstacleEnu.push_back(geo2enu(origin, GeoPoint(obstacle.x(), obstacle.y())));
    return QStringLiteral("%1 · %2 · %3")
        .arg(connectivity, coarseText,
             startActionDiagnostic(start, obstacleEnu, obstacleCoordinates));
}

QVector<QPointF> boundaryObstacleCoordinates(const RoutePlanningRequest &request,
                                             int regionId,
                                             QString *error)
{
    QVector<QPointF> obstacles;
    const WaterwayGrid &waterway = request.waterway;
    for (int polygonIndex = 0; polygonIndex < waterway.boundaryPolygons.size(); ++polygonIndex) {
        const int polygonRegion = polygonIndex < waterway.boundaryRegionIds.size()
            ? waterway.boundaryRegionIds.at(polygonIndex) : polygonIndex + 1;
        if (regionId > 0 && polygonRegion != regionId) continue;

        const QVector<QPointF> &polygon = waterway.boundaryPolygons.at(polygonIndex);
        if (polygon.size() < 3) continue;
        for (const QPointF &point : polygon) {
            if (!validGeo(point)) {
                if (error) *error = QStringLiteral("河道边界包含无效经纬度");
                return {};
            }
        }
        for (int i = 0; i < polygon.size(); ++i) {
            const QPointF &from = polygon.at(i);
            const QPointF &to = polygon.at((i + 1) % polygon.size());

            const EnuPoint fromEnu = geo2enu(GeoPoint(request.startGcj02.x(),
                                                       request.startGcj02.y()),
                                             GeoPoint(from.x(), from.y()));
            const EnuPoint toEnu = geo2enu(GeoPoint(request.startGcj02.x(),
                                                     request.startGcj02.y()),
                                           GeoPoint(to.x(), to.y()));
            const double distance = std::hypot(toEnu.e - fromEnu.e,
                                               toEnu.n - fromEnu.n);
            const int steps = qMax(1, qCeil(distance / kBoundarySpacingMeters));
            for (int step = 0; step < steps; ++step) {
                const double t = step / double(steps);
                const QPointF sample = from + (to - from) * t;
                if (validGeo(sample))
                    obstacles.append(sample);
            }
        }
    }
    // Remove exact duplicate samples (including the repeated closing point)
    // while preserving the continuous order of each polygon.  The supplied
    // algorithm only needs a dense point cloud, and duplicate points needlessly
    // increase every collision check.
    QVector<QPointF> unique;
    unique.reserve(obstacles.size());
    for (const QPointF &point : obstacles) {
        if (!unique.empty()
            && std::abs(unique.back().x() - point.x()) < 1e-12
            && std::abs(unique.back().y() - point.y()) < 1e-12)
            continue;
        unique.append(point);
    }
    obstacles = unique;
    if (obstacles.empty() && error)
        *error = QStringLiteral("当前水域没有可用于算法的闭合边界");
    return obstacles;
}

QPoint gridCellForGcj02(const WaterwayGrid &waterway, const QPointF &coordinate)
{
    const GeoReference &reference = waterway.geoReference;
    const int width = waterway.gridSize.width();
    const int height = waterway.gridSize.height();
    if (width <= 0 || height <= 0 || reference.zoom <= 0
        || !reference.viewportPixels.isValid())
        return QPoint(-1, -1);

    constexpr double pi = 3.14159265358979323846;
    const double worldSize = 256.0 * std::pow(2.0, reference.zoom);
    const auto world = [worldSize, pi](const QPointF &value) {
        const double latitude = qBound(-85.05112878, value.y(), 85.05112878);
        const double radians = qDegreesToRadians(latitude);
        return QPointF((value.x() + 180.0) / 360.0 * worldSize,
                       (1.0 - std::log(std::tan(radians) + 1.0 / std::cos(radians)) / pi)
                           * 0.5 * worldSize);
    };
    const QPointF projected = world(coordinate);
    QPointF topLeft;
    QPointF bottomRight;
    if (GeoCoordinateUtils::isValidLongitudeLatitude(reference.topLeftGcj02)
        && GeoCoordinateUtils::isValidLongitudeLatitude(reference.bottomRightGcj02)
        && QLineF(reference.topLeftGcj02, reference.bottomRightGcj02).length() > 1e-9) {
        topLeft = world(reference.topLeftGcj02);
        bottomRight = world(reference.bottomRightGcj02);
    } else {
        const QPointF center = world(reference.centerGcj02);
        topLeft = center - QPointF(reference.viewportPixels.width() * 0.5,
                                   reference.viewportPixels.height() * 0.5);
        bottomRight = center + QPointF(reference.viewportPixels.width() * 0.5,
                                       reference.viewportPixels.height() * 0.5);
    }
    const double dx = bottomRight.x() - topLeft.x();
    const double dy = bottomRight.y() - topLeft.y();
    if (std::abs(dx) < 1e-9 || std::abs(dy) < 1e-9) return QPoint(-1, -1);
    return QPoint(qRound((projected.x() - topLeft.x()) / dx * (width - 1)),
                  qRound((projected.y() - topLeft.y()) / dy * (height - 1)));
}

bool pathInsideWaterway(const RoutePlanningRequest &request,
                        const std::vector<GeoPose> &path)
{
    if (path.size() < 2) return false;
    const int width = request.waterway.gridSize.width();
    const int height = request.waterway.gridSize.height();
    const QByteArray &mask = request.waterway.hasSafeNavigableMask()
        ? request.waterway.safeNavigableMask : request.waterway.navigableMask;
    if (width <= 0 || height <= 0 || mask.size() != width * height)
        return false;
    for (const GeoPose &pose : path) {
        const QPoint cell = gridCellForGcj02(request.waterway, QPointF(pose.lon, pose.lat));
        if (cell.x() < 0 || cell.y() < 0 || cell.x() >= width || cell.y() >= height)
            return false;
        const int index = cell.y() * width + cell.x();
        if (index < 0 || index >= mask.size()
            || static_cast<uchar>(mask.at(index)) == 0)
            return false;
    }
    for (size_t i = 1; i < path.size(); ++i) {
        const QPoint a = gridCellForGcj02(request.waterway,
                                           QPointF(path[i - 1].lon, path[i - 1].lat));
        const QPoint b = gridCellForGcj02(request.waterway,
                                           QPointF(path[i].lon, path[i].lat));
        const int samples = qMax(1, qCeil(QLineF(a, b).length() * 2.0));
        for (int sample = 0; sample <= samples; ++sample) {
            const QPointF interpolated = QPointF(a)
                + (QPointF(b) - QPointF(a)) * (sample / double(samples));
            const QPoint cell(qRound(interpolated.x()), qRound(interpolated.y()));
            if (cell.x() < 0 || cell.y() < 0 || cell.x() >= width || cell.y() >= height)
                return false;
            const int index = cell.y() * width + cell.x();
            if (static_cast<uchar>(mask.at(index)) == 0)
                return false;
        }
    }
    return true;
}

RoutePlanningResult convertResult(const RoutePlanningRequest &request,
                                  const std::vector<GeoPose> &path)
{
    RoutePlanningResult result;
    result.missionId = request.missionId;
    result.revision = request.mapRevision ? request.mapRevision : request.waterway.revision;
    result.algorithmId = QStringLiteral("原始 Hybrid A* · 河道边界障碍物");
    if (path.size() < 2) {
        result.errorMessage = QStringLiteral("原始 Hybrid A* 未生成有效轨迹");
        return result;
    }
    std::vector<GeoPose> effectivePath = path;
    effectivePath.front().lon = request.startGcj02.x();
    effectivePath.front().lat = request.startGcj02.y();
    effectivePath.back().lon = request.targetGcj02.x();
    effectivePath.back().lat = request.targetGcj02.y();
    result.gcj02Path.reserve(int(path.size()));
    result.headingRadians.reserve(int(path.size()));
    result.gridPath.reserve(int(path.size()));
    double fallbackHeading = request.startHeadingRadians;
    for (size_t i = 0; i < effectivePath.size(); ++i) {
        const QPointF coordinate(effectivePath[i].lon, effectivePath[i].lat);
        if (!validGeo(coordinate)) {
            result.errorMessage = QStringLiteral("原始算法返回了无效经纬度");
            return result;
        }
        result.gcj02Path.append(coordinate);
        const QPointF previous = i > 0
            ? QPointF(effectivePath[i - 1].lon, effectivePath[i - 1].lat) : coordinate;
        const double heading = i == 0
            ? request.startHeadingRadians
            : headingBetween(previous, coordinate, fallbackHeading);
        fallbackHeading = heading;
        result.headingRadians.append(heading);
        result.gridPath.append(gridCellForGcj02(request.waterway, coordinate));
    }
    result.gridPath[0] = gridCellForGcj02(request.waterway, request.startGcj02);
    result.gridPath.last() = gridCellForGcj02(request.waterway, request.targetGcj02);
    result.success = result.gridPath.size() >= 2
        && pathInsideWaterway(request, effectivePath);
    if (!result.success)
        result.errorMessage = QStringLiteral("原始算法路线越出确认水域");
    return result;
}

} // namespace

const SuppliedHybridAstarParameters &suppliedHybridAstarParameters()
{
    static const SuppliedHybridAstarParameters parameters{
        kSuppliedShoreSafetyMeters,
        kBoundarySpacingMeters,
        kSuppliedWorldLimitMeters
    };
    return parameters;
}

QVector<QPointF> buildWaterwayObstaclePoints(const RoutePlanningRequest &request,
                                             QString *errorMessage)
{
    const int regionId = selectedRegion(request);
    if (regionId <= 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("起点和目标不在同一连通水域");
        return {};
    }
    return boundaryObstacleCoordinates(request, regionId, errorMessage);
}

RoutePlanningResult runSuppliedHybridAstar(const RoutePlanningRequest &request)
{
    RoutePlanningResult result;
    result.missionId = request.missionId;
    result.revision = request.mapRevision ? request.mapRevision : request.waterway.revision;
    result.algorithmId = QStringLiteral("原始 Hybrid A* · 河道边界障碍物");
    if (!request.waterway.isValid()) {
        result.errorMessage = QStringLiteral("水域栅格无效");
        return result;
    }
    if (!validGeo(request.startGcj02) || !validGeo(request.targetGcj02)) {
        result.errorMessage = QStringLiteral("起点或目标坐标无效");
        return result;
    }
    if (!request.startHeadingValid || !qIsFinite(request.startHeadingRadians)) {
        result.errorMessage = QStringLiteral("设备实时艏向无效，无法调用原始算法");
        return result;
    }

    QString obstacleError;
    const QVector<QPointF> obstacleCoordinates = buildWaterwayObstaclePoints(request,
                                                                              &obstacleError);
    if (obstacleCoordinates.isEmpty()) {
        result.errorMessage = obstacleError;
        return result;
    }
    std::vector<GeoPoint> obstacles;
    obstacles.reserve(size_t(obstacleCoordinates.size()));
    for (const QPointF &point : obstacleCoordinates)
        obstacles.emplace_back(point.x(), point.y());

    const GeoPoint origin(request.startGcj02.x(), request.startGcj02.y());
    const GeoPoint originalGoal(request.targetGcj02.x(), request.targetGcj02.y());
    const EnuPoint goalEnu = geo2enu(origin, originalGoal);
    if (std::hypot(goalEnu.e, goalEnu.n) > kSuppliedWorldLimitMeters) {
        result.errorMessage = QStringLiteral("起点到目标超出原始算法 5 km 搜索范围");
        return result;
    }
    const AxisTransform transform{
        goalEnu.e < 0.0 ? -1.0 : 1.0,
        goalEnu.n < 0.0 ? -1.0 : 1.0
    };
    const GeoPose start(request.startGcj02.x(), request.startGcj02.y(),
                        transformHeading(request.startHeadingRadians, transform));
    const double directHeading = headingBetween(request.startGcj02,
                                                request.targetGcj02,
                                                request.startHeadingRadians);
    const GeoPoint syntheticGoal = syntheticGeoPoint(origin, originalGoal, transform);
    GeoPose goal(syntheticGoal.lon, syntheticGoal.lat,
                 transformHeading(directHeading, transform));

    // Reflect every input into the same synthetic ENU quadrant as the goal.
    // This works around the supplied coarse-grid implementation's inability
    // to index negative cells without changing the supplied source file.
    std::vector<GeoPoint> syntheticObstacles;
    syntheticObstacles.reserve(obstacles.size());
    for (const GeoPoint &obstacle : obstacles)
        syntheticObstacles.push_back(syntheticGeoPoint(origin, obstacle, transform));

    std::vector<GeoPose> path = planGeoHybridAstar(start, goal, syntheticObstacles);
    path = restorePathFromSynthetic(origin, path, transform);
    if (path.size() >= 2) {
        const QPointF last(path[path.size() - 2].lon, path[path.size() - 2].lat);
        const QPointF end(path.back().lon, path.back().lat);
        const double routeHeading = headingBetween(last, end, directHeading);
        if (std::abs(std::atan2(std::sin(routeHeading - goal.yaw),
                                std::cos(routeHeading - goal.yaw))) > 0.08) {
            goal.yaw = transformHeading(routeHeading, transform);
            const std::vector<GeoPose> refinedSynthetic = planGeoHybridAstar(start, goal,
                                                                              syntheticObstacles);
            if (refinedSynthetic.size() >= 2)
                path = restorePathFromSynthetic(origin, refinedSynthetic, transform);
        }
    }
    if (path.size() < 2) {
        result.diagnosticMessage = failureDiagnostic(request, obstacleCoordinates);
        result.errorMessage = QStringLiteral("原始 Hybrid A* 未找到可行路线");
        return result;
    }
    result = convertResult(request, path);
    if (result.success)
        result.obstaclePointsGcj02 = obstacleCoordinates;
    return result;
}
