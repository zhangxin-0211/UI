#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QVector>

class QPainter;

class ParticleSystem
{
public:
    enum class Mode { Fog, Orbit };

    explicit ParticleSystem(int maximum = 18, Mode mode = Mode::Fog);
    void setMaximum(int maximum);
    void update(qreal deltaSeconds, const QRectF &bounds, qreal intensity, qreal phase);
    void triggerPulse(const QPointF &center, int count, const QColor &color);
    void paint(QPainter &painter, qreal opacity = 1.0) const;
    void clear();

private:
    struct Particle {
        QPointF position;
        QPointF velocity;
        qreal life = 0.0;
        qreal maximumLife = 1.0;
        qreal size = 2.0;
        qreal angle = 0.0;
        qreal radius = 0.0;
        qreal angularSpeed = 0.0;
        QColor color;
        bool pulse = false;
    };

    void spawnAmbient(const QRectF &bounds, qreal phase);
    qreal random(qreal minimum, qreal maximum) const;

    QVector<Particle> m_particles;
    int m_maximum = 18;
    Mode m_mode = Mode::Fog;
};
