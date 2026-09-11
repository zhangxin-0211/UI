#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QMetaType>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>

#include "RouteTypes.h"
#include "DeviceNetworkConfig.h"

struct GeoReference {
    QPointF centerGcj02;
    QPointF topLeftGcj02;
    QPointF bottomRightGcj02;
    int zoom = 0;
    QSize viewportPixels;
    quint64 revision = 0;
};

struct WaterwayGrid {
    QSize gridSize;
    QByteArray navigableMask;
    // Cells that remain usable after applying the planner's shoreline
    // clearance. The raw mask above is retained for shoreline extraction.
    QByteArray safeNavigableMask;
    double shoreSafetyMeters = 0.0;
    double mercatorCellMeters = 0.0;
    // 8-connected component id per cell; 0 means not navigable.
    QVector<int> connectedRegionIds;
    GeoReference geoReference;
    // Simplified GCJ-02 shorelines of every connected navigable water region,
    // including inner land holes such as islands. These are geographic
    // boundaries, not obstacle points.
    QVector<QVector<QPointF>> boundaryPolygons;
    // Region ids are kept alongside the polygons so a planner/device can
    // distinguish separate water bodies without treating a shoreline as an
    // obstacle cloud.
    QVector<int> boundaryRegionIds;
    quint64 revision = 0;

    bool isValid() const
    {
        const int cells = gridSize.width() * gridSize.height();
        return gridSize.width() > 0 && gridSize.height() > 0
            && navigableMask.size() == cells;
    }

    bool hasSafeNavigableMask() const
    {
        return isValid() && shoreSafetyMeters > 0.0
            && safeNavigableMask.size() == gridSize.width() * gridSize.height();
    }
};

struct WaterwayRecognitionRequest {
    // The recognizer analyzes the rendered map screenshot.  The geographic
    // reference keeps the resulting mask aligned with the GCJ-02 AMap view.
    QImage mapImage;
    GeoReference geoReference;
    QSize maximumGridSize{240, 180};
    // Preview requests only generate the overlay. Planning requests also
    // build local topology, shoreline contours and a safe navigation mask.
    bool buildPlanningTopology = false;
    double shoreSafetyMeters = 0.0;
};

struct WaterwayRecognitionResult {
    bool success = false;
    WaterwayGrid waterway;
    QImage overlayImage;
    double confidence = 0.0;
    QString errorMessage;
};

struct RoutePlanningRequest {
    QString missionId;
    WaterwayGrid waterway;
    QPoint startCell;
    QPoint targetCell;
    QPointF startGcj02;
    QPointF targetGcj02;
    // Device heading in the planner's ENU convention: north=0, clockwise +.
    double startHeadingRadians = 0.0;
    bool startHeadingValid = false;
    quint64 mapRevision = 0;
};

struct RoutePlanningResult {
    bool success = false;
    QVector<QPointF> gridPath;
    QVector<QPointF> gcj02Path;
    // Exact, closed/densified shoreline samples used by the local Hybrid A*
    // adapter and frozen for the future device-protocol hand-off.
    QVector<QPointF> obstaclePointsGcj02;
    QVector<double> headingRadians;
    QString missionId;
    QString algorithmId;
    QString errorMessage;
    // Project-side diagnostics for the immutable Hybrid A* implementation.
    // Kept separate from errorMessage so the UI can explain a failed plan
    // without making the transport/protocol contract depend on debug text.
    QString diagnosticMessage;
    quint64 revision = 0;
};

// The complete, immutable hand-off from map/route planning to a future device
// protocol encoder.  Keeping this separate from transport guarantees that the
// device's reported position is never replaced by the planner's safe offset.
struct PreparedMission {
    QString missionId;
    QPointF actualStartGcj02;
    RoutePoint safeStart;
    RoutePoint safeTarget;
    RoutePath outboundRoute;
    RoutePath returnRoute;
    QVector<QPointF> boundaryObstaclePointsGcj02;
    quint64 mapRevision = 0;
    bool valid = false;
};

enum class MissionState {
    NotReady,
    WaterwayPendingConfirmation,
    ReadyToPlan,
    Planning,
    Planned,
    Uploading,
    Uploaded,
    Executing,
    Returning,
    Completed,
    Failed
};

struct RobotTelemetry {
    // The device firmware reports longitude/latitude in GCJ-02, which is the
    // project's single geographic coordinate system.
    QPointF gcj02Position;
    double headingDegrees = 0.0;
    double missionProgress = 0.0;
    QString missionId;
    QString state = QStringLiteral("idle");
    QDateTime timestamp;
    bool valid = false;
    bool headingValid = false;
};

Q_DECLARE_METATYPE(QVector<QVector<QPointF>>)

enum class RobotLinkState {
    Stopped,
    Listening,
    Online,
    Stale,
    ProtocolUnavailable,
    Error
};

struct RobotEndpoint {
    QString address = QStringLiteral(DEVICE_IP);
    quint16 devicePort = DEVICE_PORT;
    quint16 localPort = LOCAL_RECEIVE_PORT;
};

Q_DECLARE_METATYPE(GeoReference)
Q_DECLARE_METATYPE(WaterwayGrid)
Q_DECLARE_METATYPE(WaterwayRecognitionRequest)
Q_DECLARE_METATYPE(WaterwayRecognitionResult)
Q_DECLARE_METATYPE(RoutePlanningRequest)
Q_DECLARE_METATYPE(RoutePlanningResult)
Q_DECLARE_METATYPE(PreparedMission)
Q_DECLARE_METATYPE(MissionState)
Q_DECLARE_METATYPE(RobotTelemetry)
Q_DECLARE_METATYPE(RobotLinkState)
Q_DECLARE_METATYPE(RobotEndpoint)
