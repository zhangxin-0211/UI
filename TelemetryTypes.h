#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QStringList>

struct TelemetrySample {
    QDateTime timestamp;
    quint64 sequence = 0;
    double xpos = 0.0;
    double ypos = 0.0;
    double depth = 0.0;
    double yaw = 0.0;
    double pitch = 0.0;
    double roll = 0.0;
    double forceX = 0.0;
    double forceY = 0.0;
    double forceZ = 0.0;
    double forceYaw = 0.0;
};

struct MonitoringSettings {
    int recordIntervalMs = 500;
    int maximumVisibleRows = 120;
    int decimalPlaces = 2;
    QStringList visibleFields;
};

enum class ControlMode {
    Manual,
    HeadingHold,
    DepthHold,
    StationKeeping
};

struct DeviceControlParameters {
    double targetDepth = 0.0;
    double targetHeading = 0.0;
    double cruiseSpeed = 0.0;
    double thrusterPowerLimit = 100.0;
    ControlMode mode = ControlMode::Manual;
};

Q_DECLARE_METATYPE(TelemetrySample)
Q_DECLARE_METATYPE(MonitoringSettings)
Q_DECLARE_METATYPE(ControlMode)
Q_DECLARE_METATYPE(DeviceControlParameters)
