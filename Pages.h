#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

#include <memory>

#include "Actions.h"
#include "CameraTypes.h"
#include "LocationTypes.h"
#include "MissionTypes.h"
#include "RouteTypes.h"
#include "TelemetryTypes.h"

class DemoDataModel;
class GaugeWidget;
class CircularProgress;
class LineChart;
class MapPlanningWidget;
class CompassWidget;
class VideoPlaceholder;
class QTableWidget;
class QLabel;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QTimer;
class QImage;
class WaterwayRecognizer;
class CameraController;
class PlannerController;
class UdpRobotController;
class IRoutePlanner;
class IRobotProtocolCodec;

class DashboardPage : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardPage(QWidget *parent = nullptr) : QWidget(parent) {}

signals:
    void actionRequested(ActionId action);
    void monitoringSettingsChanged(const MonitoringSettings &settings);
    void deviceControlRequested(const DeviceControlParameters &parameters);
    void telemetryExported(const QString &filePath, int rowCount);

protected:
    void requestAction(ActionId action) { emit actionRequested(action); }
};

class HealthPage : public DashboardPage
{
public:
    explicit HealthPage(DemoDataModel *model, QWidget *parent = nullptr);

private:
    QList<GaugeWidget *> m_thrusters;
    GaugeWidget *m_pitch = nullptr;
    GaugeWidget *m_roll = nullptr;
    CompassWidget *m_compass = nullptr;
    CircularProgress *m_battery = nullptr;
    LineChart *m_chart = nullptr;
    QLabel *m_depthValue = nullptr;
};

class RoutePage : public DashboardPage
{
    Q_OBJECT
public:
    explicit RoutePage(DemoDataModel *model, QWidget *parent = nullptr);
    void warmUpMap();
    void setCurrentLocation(const LocationFix &fix, LocationSource source);
    void setLocationStatus(const QString &status);
    void setPageActive(bool active);
    void setMapHost(QWidget *host);
    void syncMapHostGeometry();
    void setExternalRoutePlanner(const std::shared_ptr<IRoutePlanner> &planner);
    void setRobotProtocolCodec(const std::shared_ptr<IRobotProtocolCodec> &codec);
    PreparedMission preparedMission() const { return m_preparedMission; }

signals:
    void amapLocationReceived(const LocationFix &fix);
    void amapLocationFailed(const QString &message);
    void missionPrepared(const PreparedMission &mission);

private:
    void setMissionState(MissionState state, const QString &message = QString());
    void updateControls();
    void confirmWaterway();
    void requestPlanning();
    void acceptSnapCandidate();
    void schedulePreviewWaterwayRecognition();
    void requestPreviewWaterwayCapture();
    void requestPlanningWaterwayCapture();
    void processWaterwayCapture(const QImage &image, const GeoReference &geoReference);
    void configureAndStartUdp();
    QPoint gridCellForGcj02(const QPointF &gcj02) const;
    QPointF gcj02ForGridPoint(const QPointF &cell) const;
    bool cellIsNavigable(const QPoint &cell) const;
    bool cellIsSafe(const QPoint &cell) const;
    QPoint nearestSafeCell(const QPoint &origin, int requiredRegion = 0,
                           bool requirePlannerClearance = false) const;
    RoutePoint safeDevicePositionForDisplay(const RoutePoint &actual,
                                            bool *wasAdjusted = nullptr) const;
    bool usingManualTestDevice() const;
    bool hasUsableDeviceOrigin() const;
    QString preparedMissionAvailabilityMessage() const;
    double activeDeviceHeadingDegrees() const;
    QDateTime activeDeviceTimestamp() const;
    QPointF actualDeviceGcj02() const;
    void invalidateRouteForOriginChange(const QString &message);
    void restoreLatestUdpDevicePosition();
    void restoreManualTestDevicePosition();
    bool updatePlanningStartFromDevice();
    void clearMissionOriginSnapshot();
    RoutePath routeFromPlanningResult(const RoutePlanningResult &result) const;
    QString coordinateText(const QPointF &position) const;
    MapPlanningWidget *m_mapPlanning = nullptr;
    PlannerController *m_plannerController = nullptr;
    UdpRobotController *m_udpController = nullptr;
    QLabel *m_devicePositionLabel = nullptr;
    QLabel *m_targetPositionLabel = nullptr;
    QLabel *m_recognitionStatusLabel = nullptr;
    QLabel *m_missionStatusLabel = nullptr;
    QLabel *m_algorithmStatusLabel = nullptr;
    QLabel *m_planningDiagnosticsLabel = nullptr;
    QPushButton *m_acceptSnapButton = nullptr;
    QPushButton *m_planButton = nullptr;
    QCheckBox *m_manualDeviceCheck = nullptr;
    QPushButton *m_markTestDeviceButton = nullptr;
    QDoubleSpinBox *m_testHeadingSpin = nullptr;
    QPushButton *m_uploadButton = nullptr;
    QPushButton *m_returnMissionButton = nullptr;
    QPushButton *m_stopMissionButton = nullptr;
    QCheckBox *m_waterwayOnlyCheck = nullptr;
    QTimer *m_missionTimer = nullptr;
    QTimer *m_autoRecognitionTimer = nullptr;
    MissionState m_missionState = MissionState::NotReady;
    WaterwayGrid m_waterwayGrid;
    RoutePoint m_devicePosition;
    RoutePoint m_planningStartPosition;
    RoutePoint m_targetPosition;
    RoutePoint m_snapCandidate;
    RoutePath m_plannedRoute;
    RoutePath m_returnRoute;
    PreparedMission m_preparedMission;
    RobotTelemetry m_lastTelemetry;
    RoutePoint m_manualTestDevicePosition;
    QDateTime m_manualTestDeviceTimestamp;
    struct MissionOriginSnapshot {
        RoutePoint safePose;
        QPointF actualGcj02;
        QDateTime telemetryTimestamp;
        bool valid = false;
    } m_missionOrigin;
    RoutePoint m_frozenTargetPosition;
    QString m_missionId;
    quint64 m_currentMapRevision = 0;
    double m_recognitionConfidence = 0.0;
    bool m_hasTarget = false;
    bool m_hasPlanningStart = false;
    bool m_hasManualTestDevice = false;
    bool m_hasSnapCandidate = false;
    bool m_waterwayConfirmed = false;
    bool m_routeUploaded = false;
    bool m_missionStarted = false;
    bool m_returning = false;
    bool m_pageActive = false;
    bool m_planningCaptureRequested = false;
    bool m_planningRecognitionActive = false;
    bool m_recognitionBusy = false;
    bool m_currentGridIsPlanning = false;
    class WaterwayRecognizer *m_waterwayRecognizer = nullptr;
};

class VideoPage : public DashboardPage
{
public:
    explicit VideoPage(DemoDataModel *model, QWidget *parent = nullptr);

private:
    VideoPlaceholder *m_video = nullptr;
    QLabel *m_coordinates = nullptr;
    QLabel *m_cameraInfo = nullptr;
    QPushButton *m_playButton = nullptr;
    QTimer *m_frameTimer = nullptr;
    CameraController *m_cameraController = nullptr;
};

class SonarPage : public DashboardPage
{
public:
    explicit SonarPage(QWidget *parent = nullptr);
};

class DataPage : public DashboardPage
{
public:
    explicit DataPage(DemoDataModel *model, QWidget *parent = nullptr);

private:
    void addSample(double phase, int sequence);
    TelemetrySample createSample(double phase);
    void appendSampleToTable(const TelemetrySample &sample, int cacheIndex);
    void rebuildTable();
    void applyColumnVisibility();
    void updateCountLabel();
    void loadSettings();
    void applyMonitoringSettings();
    void applyControlParameters();
    void clearData();
    void exportData();
    MonitoringSettings settingsFromControls() const;
    DeviceControlParameters controlParametersFromControls() const;
    bool writeCsv(const QString &filePath, const QVector<TelemetrySample> &samples, QString *errorMessage) const;

    QTableWidget *m_table = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_controlFeedback = nullptr;
    QPushButton *m_collectButton = nullptr;
    QSpinBox *m_recordIntervalSpin = nullptr;
    QSpinBox *m_visibleRowsSpin = nullptr;
    QSpinBox *m_decimalsSpin = nullptr;
    QList<QCheckBox *> m_fieldChecks;
    QDoubleSpinBox *m_targetDepthSpin = nullptr;
    QDoubleSpinBox *m_targetHeadingSpin = nullptr;
    QDoubleSpinBox *m_cruiseSpeedSpin = nullptr;
    QDoubleSpinBox *m_powerLimitSpin = nullptr;
    QComboBox *m_controlModeCombo = nullptr;
    QTimer *m_recordTimer = nullptr;
    QVector<TelemetrySample> m_sessionSamples;
    MonitoringSettings m_settings;
    quint64 m_nextSequence = 0;
    double m_latestPhase = 0.0;
    bool m_hasLatestSample = false;
    bool m_collecting = true;
    bool m_limitReported = false;
};
