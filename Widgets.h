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
    explicit MapPlanningWidget(QWidget *parent = nullptr);
    void warmUp();
    bool isMapReady() const { return m_mapReady; }
    void setWaypoints(const QVector<RoutePoint> &points);
    void setVehiclePosition(const RoutePoint &position);
    void setCurrentLocation(const LocationFix &fix, LocationSource source);
    void setLocationStatus(const QString &status);
    void setPlannedPath(const RoutePath &path);
    void setMapReady(bool ready);
    void setPageActive(bool active);
    void setWebViewHost(QWidget *host);
    void syncWebViewGeometry();
    void clearOverlays();
    void requestRoutePlanning();
    QSize sizeHint() const override;

signals:
    void waypointCreated(const RoutePoint &point);
    void waypointUpdated(const RoutePoint &point);
    void waypointRemoved(const QString &id);
    void routePlanningRequested(const QVector<RoutePoint> &points);
    void amapLocationReceived(const LocationFix &fix);
    void amapLocationFailed(const QString &message);

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
    QPointF waypointWgs84(const RoutePoint &point) const;
    QPointF displayCoordinate(const RoutePoint &point) const;
    QPointF gridProject(const QPointF &wgs84) const;
    QPointF gridUnproject(const QPointF &screenPosition) const;
    void drawGlobalGrid(QPainter &p, const QRectF &area);
    void updateEffectiveMode();
    void updateMapVisibility();
    void centerOnCurrentLocation();
    int waypointAt(const QPointF &position, qreal radius = 14.0) const;
    void scheduleWebSync();
    void flushWebSync();
    void syncBootstrapLocation();
    void startMapSession();

    QVector<RoutePoint> m_waypoints;
    RoutePath m_plannedPath;
    RoutePoint m_vehiclePosition;
    LocationFix m_currentLocation;
    LocationSource m_locationSource = LocationSource::None;
    bool m_mapReady = false;
    bool m_amapUnavailable = false;
    bool m_hasVehiclePosition = false;
    bool m_vehicleAutoCenterApplied = false;
    bool m_warmupStarted = false;
    // Loading the WebEngine document is safe to do in the background.  Starting a
    // real AMap instance is deliberately deferred until the route page is visible.
    bool m_webPageLoaded = false;
    bool m_mapSessionRequested = false;
    bool m_mapSessionStarted = false;
    bool m_waypointsDirty = true;
    bool m_plannedPathDirty = true;
    bool m_vehicleDirty = false;
    bool m_locationDirty = false;
    bool m_bootstrapLocationDirty = false;
    bool m_pageActive = false;
    bool m_locationAutoCenterApplied = false;
    bool m_locationCenterPending = false;
    bool m_gridDragging = false;
    bool m_gridWaypointDragging = false;
    bool m_amapLocationPaused = false;
    bool m_gridMoved = false;
    int m_gridDraggedWaypoint = -1;
    QPoint m_gridPressPosition;
    QPoint m_gridLastMousePosition;
    QPointF m_cursorCoordinate;
    QPointF m_centerCoordinate{116.397, 39.908};
    QPointF m_gridCenterCoordinate{116.397, 39.908};
    int m_zoom = 5;
    int m_gridZoom = 3;
    int m_nextWaypointId = 1;
    class QWebEngineView *m_webView = nullptr;
    QWidget *m_webViewHost = nullptr;
    class AmapWebBridge *m_bridge = nullptr;
    class QTimer *m_webSyncTimer = nullptr;
    class QPushButton *m_recenterButton = nullptr;
    QString m_jsApiKey;
    QString m_securityJsCode;
    QString m_lastWaypointsJson;
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
    void waypointMoved(const QString &id, double longitude, double latitude) { emit waypointDragged(id, longitude, latitude); }
    void waypointDeleted(const QString &id) { emit waypointDeleteRequested(id); }
    void mapStateChanged(bool ready, const QString &message) { emit mapStateReported(ready, message); }
    void mapViewChanged(double longitude, double latitude, int zoom) { emit mapViewReported(longitude, latitude, zoom); }
    void locationChanged(double longitude, double latitude, double accuracy, double altitude,
                         double timestampMs, const QString &detail)
    { emit amapLocationReported(longitude, latitude, accuracy, altitude, timestampMs, detail); }
    void locationFailed(const QString &message) { emit amapLocationFailureReported(message); }
    void useDefaultMap() { emit defaultMapRequested(); }
signals:
    void waypointClicked(double longitude, double latitude);
    void waypointDragged(const QString &id, double longitude, double latitude);
    void waypointDeleteRequested(const QString &id);
    void mapStateReported(bool ready, const QString &message);
    void mapViewReported(double longitude, double latitude, int zoom);
    void amapLocationReported(double longitude, double latitude, double accuracy, double altitude,
                              double timestampMs, const QString &detail);
    void amapLocationFailureReported(const QString &message);
    void defaultMapRequested();
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
