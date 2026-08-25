#pragma once

#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

enum class CoordinateSystem {
    Unspecified,
    Wgs84,
    Gcj02,
    LocalCartesian
};

struct RoutePoint {
    QString id;
    CoordinateSystem coordinateSystem = CoordinateSystem::Unspecified;
    QPointF position;
    double depth = 0.0;
};

struct RoutePath {
    QVector<RoutePoint> points;
    QString algorithmId;
    bool valid = false;
};

Q_DECLARE_METATYPE(CoordinateSystem)
Q_DECLARE_METATYPE(RoutePoint)
Q_DECLARE_METATYPE(QVector<RoutePoint>)
Q_DECLARE_METATYPE(RoutePath)
