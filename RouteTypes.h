#pragma once

#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

enum class CoordinateSystem {
    Unspecified,
    Gcj02
};

struct RoutePoint {
    QString id;
    CoordinateSystem coordinateSystem = CoordinateSystem::Gcj02;
    QPointF position;
    double headingRadians = 0.0;
    bool headingValid = false;
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
