#pragma once

#include <QAbstractButton>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QVector>

#include "ParticleSystem.h"
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
    void setWaypoints(const QVector<RoutePoint> &points);
    void setPlannedPath(const RoutePath &path);
    void setMapReady(bool ready);
    void clearOverlays();
    QSize sizeHint() const override;

signals:
    void waypointCreated(const RoutePoint &point);
    void waypointRemoved(const QString &id);
    void routePlanningRequested(const QVector<RoutePoint> &points);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<RoutePoint> m_waypoints;
    RoutePath m_plannedPath;
    bool m_mapReady = false;
    qreal m_effectPhase = 0.0;
};

class VideoPlaceholder : public QWidget
{
public:
    explicit VideoPlaceholder(QWidget *parent = nullptr);
    void setPlaying(bool playing);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_playing = false;
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
