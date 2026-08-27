#include "GeoCoordinateUtils.h"

#include <QPolygonF>
#include <QtMath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double transformLatitude(double x, double y)
{
    double value = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y
                 + 0.1 * x * y + 0.2 * qSqrt(qAbs(x));
    value += (20.0 * qSin(6.0 * x * kPi) + 20.0 * qSin(2.0 * x * kPi)) * 2.0 / 3.0;
    value += (20.0 * qSin(y * kPi) + 40.0 * qSin(y / 3.0 * kPi)) * 2.0 / 3.0;
    value += (160.0 * qSin(y / 12.0 * kPi) + 320.0 * qSin(y * kPi / 30.0)) * 2.0 / 3.0;
    return value;
}

double transformLongitude(double x, double y)
{
    double value = 300.0 + x + 2.0 * y + 0.1 * x * x
                 + 0.1 * x * y + 0.1 * qSqrt(qAbs(x));
    value += (20.0 * qSin(6.0 * x * kPi) + 20.0 * qSin(2.0 * x * kPi)) * 2.0 / 3.0;
    value += (20.0 * qSin(x * kPi) + 40.0 * qSin(x / 3.0 * kPi)) * 2.0 / 3.0;
    value += (150.0 * qSin(x / 12.0 * kPi) + 300.0 * qSin(x / 30.0 * kPi)) * 2.0 / 3.0;
    return value;
}

}

namespace GeoCoordinateUtils {

bool isValidLongitudeLatitude(const QPointF &coordinate)
{
    return qIsFinite(coordinate.x()) && qIsFinite(coordinate.y())
        && coordinate.x() >= -180.0 && coordinate.x() <= 180.0
        && coordinate.y() >= -90.0 && coordinate.y() <= 90.0;
}

bool isInsideAmapCoverage(const QPointF &wgs84)
{
    if (!isValidLongitudeLatitude(wgs84)) return false;

    // A deliberately low-resolution operational boundary.  It follows the
    // shape of mainland China and nearby coastal waters instead of treating
    // the entire bounding rectangle as covered by the domestic base map.
    static const QPolygonF mainland = QPolygonF()
        << QPointF(73.4, 39.5) << QPointF(76.0, 35.0) << QPointF(78.2, 31.0)
        << QPointF(82.0, 28.0) << QPointF(87.0, 27.2) << QPointF(92.0, 27.0)
        << QPointF(97.0, 25.0) << QPointF(101.0, 22.0) << QPointF(106.0, 20.0)
        << QPointF(111.0, 18.0) << QPointF(116.0, 20.0) << QPointF(121.5, 22.0)
        << QPointF(124.5, 27.0) << QPointF(125.0, 32.0) << QPointF(123.0, 36.0)
        << QPointF(127.0, 40.0) << QPointF(132.0, 44.0) << QPointF(131.0, 48.0)
        << QPointF(126.0, 51.0) << QPointF(120.0, 54.0) << QPointF(113.0, 52.0)
        << QPointF(107.0, 50.0) << QPointF(101.0, 49.0) << QPointF(96.0, 50.0)
        << QPointF(91.0, 47.0) << QPointF(86.0, 49.0) << QPointF(81.0, 46.0)
        << QPointF(76.0, 43.0) << QPointF(73.4, 39.5);
    static const QPolygonF taiwan = QPolygonF()
        << QPointF(119.2, 25.8) << QPointF(122.3, 25.8) << QPointF(122.3, 21.5)
        << QPointF(119.2, 21.5) << QPointF(119.2, 25.8);
    static const QPolygonF hainanAndCoast = QPolygonF()
        << QPointF(107.0, 22.3) << QPointF(112.5, 22.3) << QPointF(112.5, 17.0)
        << QPointF(107.0, 17.0) << QPointF(107.0, 22.3);
    return mainland.containsPoint(wgs84, Qt::OddEvenFill)
        || taiwan.containsPoint(wgs84, Qt::OddEvenFill)
        || hainanAndCoast.containsPoint(wgs84, Qt::OddEvenFill);
}

QPointF wgs84ToGcj02(const QPointF &wgs84)
{
    if (!isInsideAmapCoverage(wgs84)) return wgs84;

    constexpr double earthRadius = 6378245.0;
    constexpr double eccentricity = 0.00669342162296594323;
    const double dLatitude = transformLatitude(wgs84.x() - 105.0, wgs84.y() - 35.0);
    const double dLongitude = transformLongitude(wgs84.x() - 105.0, wgs84.y() - 35.0);
    const double radians = wgs84.y() / 180.0 * kPi;
    const double magic = 1.0 - eccentricity * qSin(radians) * qSin(radians);
    const double sqrtMagic = qSqrt(magic);
    const double adjustedLatitude = dLatitude * 180.0
        / ((earthRadius * (1.0 - eccentricity)) / (magic * sqrtMagic) * kPi);
    const double adjustedLongitude = dLongitude * 180.0
        / (earthRadius / sqrtMagic * qCos(radians) * kPi);
    return QPointF(wgs84.x() + adjustedLongitude, wgs84.y() + adjustedLatitude);
}

QPointF gcj02ToWgs84(const QPointF &gcj02)
{
    if (!isInsideAmapCoverage(gcj02)) return gcj02;
    QPointF estimate = gcj02;
    for (int i = 0; i < 5; ++i) {
        const QPointF converted = wgs84ToGcj02(estimate);
        estimate.rx() -= converted.x() - gcj02.x();
        estimate.ry() -= converted.y() - gcj02.y();
    }
    return estimate;
}

}
