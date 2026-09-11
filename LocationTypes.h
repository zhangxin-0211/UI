#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QPointF>
#include <QString>

enum class LocationSource {
    None,
    Gps,
    Amap,
    LastKnown
};

struct LocationFix {
    QPointF gcj02Position;
    double horizontalAccuracyMeters = -1.0;
    double altitudeMeters = 0.0;
    QDateTime timestamp;
    QString providerDetail;
    bool valid = false;
};

Q_DECLARE_METATYPE(LocationSource)
Q_DECLARE_METATYPE(LocationFix)
