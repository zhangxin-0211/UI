#pragma once

#include <QAbstractButton>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QVariantList>
#include <QObject>

#include "ParticleSystem.h"
#include "CameraTypes.h"
#include "LocationTypes.h"
#include "MissionTypes.h"
#include "RouteTypes.h"

class NeonPanel : public QFrame
{
public:
    explicit NeonPanel(const QString &title = QString(), QWidget *parent = nullptr);
    void setTitle(const QString &title);
    QString title() const { return m_title; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_title;
    qreal m_effectPhase = 0.0;
};

class NavButton : public QAbstractButton
{
public:
    explicit NavButton(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hovered = false;
    qreal m_effectPhase = 0.0;
};

class GaugeWidget : public QWidget
{
public:
    explicit GaugeWidget(const QString &caption = QString(), QWidget *parent = nullptr);
    void setRange(double minimum, double maximum);
    void setValue(double value);
    void setUnit(const QString &unit);
    void setCaption(const QString &caption);
    void setResetParticlesOnResize(bool enabled);
    double value() const { return m_value; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildStaticLayer();
    void advanceEffects(qreal phase, qreal deltaSeconds);
    QString m_caption;
    QString m_unit;
    double m_minimum = -3000.0;
    double m_maximum = 3000.0;
    double m_value = 0.0;
    double m_previousValue = 0.0;
    qreal m_effectPhase = 0.0;
    qreal m_flowRotation = 0.0;
    qreal m_activity = 0.25;
    bool m_resetParticlesOnResize = true;
    QPixmap m_staticLayer;
    ParticleSystem m_fogParticles{8, ParticleSystem::Mode::Fog};
    ParticleSystem m_orbitParticles{10, ParticleSystem::Mode::Orbit};
};

class CircularProgress : public QWidget
{
public:
    explicit CircularProgress(QWidget *parent = nullptr);
    void setValue(int value);
    void setSubtitle(const QString &subtitle);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_value = 50;
    QString m_subtitle = QStringLiteral("48V");
};

class StatusLamp : public QWidget
{
public:
    enum State { Offline, Online, Warning, Fault };
    explicit StatusLamp(const QString &text, State state = Offline, QWidget *parent = nullptr);
    void setState(State state);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    State m_state;
};

class LineChart : public QWidget
{
public:
    explicit LineChart(const QString &title = QString(), QWidget *parent = nullptr);
    void setTitle(const QString &title);
    void setSeries(const QVector<double> &first, const QVector<double> &second);
    void append(double first, double second, int limit = 80);
    void setCompact(bool compact);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_title;
    QVector<double> m_first;
    QVector<double> m_second;
    bool m_compact = false;
};

class MapPlanningWidget : public QWidget
{
    Q_OBJECT

public:
    enum class TargetState {
        Pending,
        Safe,
        Invalid
    };

    explicit MapPlanningWidget(QWidget *parent = nullptr);
    void warmUp();
    bool isMapReady() const { return m_mapReady; }
    QPointF currentMapCenter() const { return m_centerCoordinate; }
    quint64 currentMapRevision() const { return m_mapRevision; }
    void setVehiclePosition(const RoutePoint &position);
    void setVehicleTelemetry(const RoutePoint &position, double headingDegrees);
    // Shows the unmodified telemetry/manual point separately from the safe
    // planning point used by the orange vehicle marker.
    void setActualVehiclePosition(const RoutePoint &position);
    void setTargetSelectionEnabled(bool enabled);
    void setTestDeviceSelectionEnabled(bool enabled);
    void setMissionTarget(const RoutePoint &target, TargetState state = TargetState::Pending);
    void setSnapCandidate(const RoutePoint &candidate, bool visible);
    void setPlanningStart(const RoutePoint &position, bool visible);
    void requestWaterwayCapture();
    bool isWaterwayCapturePending() const { return m_capturePending; }
    void setWaterwayOverlay(const QImage &overlay);
    void clearWaterwayOverlay();
    void setWaterwayOnlyMode(bool enabled);
    void setCurrentLocation(const LocationFix &fix, LocationSource source);
    void setLocationStatus(const QString &status);
    void setPlannedPath(const RoutePath &path);
    void setPageActive(bool active);
    void setWebViewHost(QWidget *host);
    void syncWebViewGeometry();
    QSize sizeHint() const override;

signals:
    void amapLocationReceived(const LocationFix &fix);
    void amapLocationFailed(const QString &message);
    void missionTargetSelected(const RoutePoint &target);
    void testDevicePositionSelected(const RoutePoint &position);
    void waterwayCaptureReady(const QImage &image, const GeoReference &geoReference);
    void mapRevisionChanged(quint64 revision);
    void mapViewportResized(quint64 revision);
    void mapReadyChanged(bool ready);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum class DisplayMode { Amap, GlobalGrid };

    QRectF mapArea() const;
    QPointF waypointGcj02(const RoutePoint &point) const;
    QPointF displayCoordinate(const RoutePoint &point) const;
    QPointF gridProject(const QPointF &gcj02) const;
    QPointF gridUnproject(const QPointF &screenPosition) const;
    void drawGlobalGrid(QPainter &p, const QRectF &area);
    void updateEffectiveMode();
    void updateMapVisibility();
    void centerOnCurrentLocation();
    void centerMapOnCurrentLocation();
    void finishManualLocationRequest(bool succeeded, const QString &message = QString());
    void scheduleWebSync();
    void flushWebSync();
    void syncBootstrapLocation();
    void startMapSession();
    void performWaterwayCapture(quint64 revision,
                                double centerLongitude, double centerLatitude, int zoom,
                                int width, int height,
                                double topLeftLongitude, double topLeftLatitude,
                                double bottomRightLongitude, double bottomRightLatitude);
    void refreshWaterwayOverlayView();
    void refreshScaleRulerView();

    RoutePath m_plannedPath;
    RoutePoint m_vehiclePosition;
    RoutePoint m_actualVehiclePosition;
    LocationFix m_currentLocation;
    LocationSource m_locationSource = LocationSource::None;
    bool m_mapReady = false;
    bool m_amapUnavailable = false;
    bool m_hasVehiclePosition = false;
    bool m_hasActualVehiclePosition = false;
    bool m_warmupStarted = false;
    // Loading the WebEngine document is safe to do in the background.  Starting a
    // real AMap instance is deliberately deferred until the route page is visible.
    bool m_webPageLoaded = false;
    bool m_mapSessionRequested = false;
    bool m_mapSessionStarted = false;
    bool m_plannedPathDirty = true;
    bool m_vehicleDirty = false;
    bool m_actualVehicleDirty = false;
    bool m_locationDirty = false;
    bool m_bootstrapLocationDirty = false;
    bool m_targetDirty = false;
    bool m_snapCandidateDirty = false;
    bool m_planningStartDirty = false;
    bool m_pageActive = false;
    bool m_manualLocationRequest = false;
    bool m_gridDragging = false;
    bool m_amapLocationPaused = false;
    bool m_targetSelectionEnabled = false;
    bool m_testDeviceSelectionEnabled = false;
    bool m_waterwayOnlyMode = false;
    QImage m_waterwayOverlay;
    class QLabel *m_waterwayOverlayView = nullptr;
    class QLabel *m_captureCoverView = nullptr;
    class QLabel *m_scaleRulerView = nullptr;
    QSize m_waterwayOverlayViewSize;
    bool m_capturePending = false;
    QTimer *m_captureTimeoutTimer = nullptr;
    bool m_gridMoved = false;
    QPoint m_gridPressPosition;
    QPoint m_gridLastMousePosition;
    QPointF m_cursorCoordinate;
    QPointF m_centerCoordinate{116.397, 39.908};
    QPointF m_gridCenterCoordinate{116.397, 39.908};
    int m_zoom = 5;
    int m_gridZoom = 3;
    double m_vehicleHeadingDegrees = 0.0;
    quint64 m_mapRevision = 0;
    // JavaScript keeps its own lightweight view counter for pan/zoom events.
    // Keep the last value separately so a Qt resize revision can never be
    // overwritten by an older JS report.
    quint64 m_lastJsViewRevision = 0;
    QSize m_mapViewportPixels;
    QPointF m_viewTopLeftGcj02;
    QPointF m_viewBottomRightGcj02;
    RoutePoint m_missionTarget;
    RoutePoint m_snapCandidate;
    RoutePoint m_planningStart;
    bool m_hasMissionTarget = false;
    TargetState m_targetState = TargetState::Pending;
    bool m_hasSnapCandidate = false;
    bool m_hasPlanningStart = false;
    class QWebEngineView *m_webView = nullptr;
    QWidget *m_webViewHost = nullptr;
    class AmapWebBridge *m_bridge = nullptr;
    class QTimer *m_webSyncTimer = nullptr;
    class QPushButton *m_recenterButton = nullptr;
    QString m_jsApiKey;
    QString m_securityJsCode;
    QString m_mapConfigSource = QStringLiteral("CONFIG MISSING");
    QString m_lastPlannedPathJson;
    QString m_mapStatus = QStringLiteral("AMAP JS API · LOADING");
    QString m_locationStatus = QStringLiteral("LOCATION · WAITING");
    DisplayMode m_effectiveMode = DisplayMode::Amap;
};

class AmapWebBridge : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
public slots:
    void mapClicked(double longitude, double latitude) { emit waypointClicked(longitude, latitude); }
    void mapStateChanged(bool ready, const QString &message) { emit mapStateReported(ready, message); }
    void mapViewChanged(double longitude, double latitude, int zoom) { emit mapViewReported(longitude, latitude, zoom); }
    void locationChanged(double longitude, double latitude, double accuracy, double altitude,
                         double timestampMs, const QString &detail)
    { emit amapLocationReported(longitude, latitude, accuracy, altitude, timestampMs, detail); }
    void locationFailed(const QString &message) { emit amapLocationFailureReported(message); }
    void useDefaultMap() { emit defaultMapRequested(); }
    void mapGeometryChanged(double longitude, double latitude, int zoom, int width, int height,
                            double topLeftLongitude, double topLeftLatitude,
                            double bottomRightLongitude, double bottomRightLatitude,
                            double revision)
    { emit mapGeometryReported(longitude, latitude, zoom, width, height,
                               topLeftLongitude, topLeftLatitude,
                               bottomRightLongitude, bottomRightLatitude, quint64(revision)); }
    void capturePrepared(double revision, double centerLongitude, double centerLatitude, int zoom,
                         int width, int height,
                         double topLeftLongitude, double topLeftLatitude,
                         double bottomRightLongitude, double bottomRightLatitude)
    {
        emit capturePreparationReported(quint64(revision), centerLongitude, centerLatitude, zoom,
                                        width, height, topLeftLongitude, topLeftLatitude,
                                        bottomRightLongitude, bottomRightLatitude);
    }
signals:
    void waypointClicked(double longitude, double latitude);
    void mapStateReported(bool ready, const QString &message);
    void mapViewReported(double longitude, double latitude, int zoom);
    void amapLocationReported(double longitude, double latitude, double accuracy, double altitude,
                              double timestampMs, const QString &detail);
    void amapLocationFailureReported(const QString &message);
    void defaultMapRequested();
    void mapGeometryReported(double longitude, double latitude, int zoom, int width, int height,
                             double topLeftLongitude, double topLeftLatitude,
                             double bottomRightLongitude, double bottomRightLatitude,
                             quint64 revision);
    void capturePreparationReported(quint64 revision,
                                    double centerLongitude, double centerLatitude, int zoom,
                                    int width, int height,
                                    double topLeftLongitude, double topLeftLatitude,
                                    double bottomRightLongitude, double bottomRightLatitude);
};

class VideoPlaceholder : public QWidget
{
public:
    explicit VideoPlaceholder(QWidget *parent = nullptr);
    void setFrame(const QImage &frame);
    void clearFrame();
    void setStreamState(CameraState state, const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_frame;
    CameraState m_state = CameraState::Stopped;
    QString m_stateMessage = QStringLiteral("视频信号等待接入");
    qreal m_effectPhase = 0.0;
    ParticleSystem m_signalParticles{34, ParticleSystem::Mode::Orbit};
};

class GamepadWidget : public QWidget
{
public:
    explicit GamepadWidget(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

class CompassWidget : public QWidget
{
public:
    explicit CompassWidget(QWidget *parent = nullptr);
    void setHeading(double heading);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildStaticLayer();
    void advanceEffects(qreal phase, qreal deltaSeconds);
    double m_heading = 15.0;
    double m_previousHeading = 15.0;
    qreal m_effectPhase = 0.0;
    qreal m_flowRotation = 0.0;
    qreal m_activity = 0.25;
    QPixmap m_staticLayer;
    ParticleSystem m_fogParticles{58, ParticleSystem::Mode::Fog};
    ParticleSystem m_orbitParticles{58, ParticleSystem::Mode::Orbit};
};
