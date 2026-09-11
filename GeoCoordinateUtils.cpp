#include "GeoCoordinateUtils.h"

#include <QtMath>

namespace GeoCoordinateUtils {

bool isValidLongitudeLatitude(const QPointF &coordinate)
{
    return qIsFinite(coordinate.x()) && qIsFinite(coordinate.y())
        && coordinate.x() >= -180.0 && coordinate.x() <= 180.0
        && coordinate.y() >= -90.0 && coordinate.y() <= 90.0;
}

}
