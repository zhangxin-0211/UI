#pragma once

#include <QList>
#include <QWidget>

#include "Actions.h"
#include "CameraTypes.h"
#include "LocationTypes.h"
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
class CameraController;

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
    void setVehiclePosition(const RoutePoint &position);
    void setCurrentLocation(const LocationFix &fix, LocationSource source);
    void setLocationStatus(const QString &status);
    void setPlannedPath(const RoutePath &path);
    void setPageActive(bool active);
    void setMapHost(QWidget *host);
    void syncMapHostGeometry();

signals:
    void amapLocationReceived(const LocationFix &fix);
    void amapLocationFailed(const QString &message);

private:
    QVector<RoutePoint> routePointsFromTable() const;
    void appendWaypointRow(const RoutePoint &point);
    void updateWaypointRow(const RoutePoint &point);
    void renumberWaypointRows();
    void syncMapWaypoints();
    MapPlanningWidget *m_mapPlanning = nullptr;
    LineChart *m_liveTrack = nullptr;
    QTableWidget *m_waypoints = nullptr;
    int m_liveTrackUpdateDivider = 0;
    bool m_pageActive = false;
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
