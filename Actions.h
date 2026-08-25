#pragma once

#include <QMetaType>

enum class ActionId {
    AddWaypoint,
    RemoveWaypoint,
    ClearWaypoints,
    LoadDefaults,
    DrawRoute,
    SendMission,
    StartVideo,
    SendStationKeeping,
    StartCollection,
    ClearData,
    ExportData,
    ApplyMonitoringSettings,
    ApplyControlParameters
};

Q_DECLARE_METATYPE(ActionId)
