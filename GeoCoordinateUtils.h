#pragma once

#include <QPointF>

namespace GeoCoordinateUtils {

bool isValidLongitudeLatitude(const QPointF &coordinate);
bool isInsideAmapCoverage(const QPointF &wgs84);
QPointF wgs84ToGcj02(const QPointF &wgs84);
QPointF gcj02ToWgs84(const QPointF &gcj02);

}
