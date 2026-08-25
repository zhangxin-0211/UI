#include "EffectController.h"

#include <QCoreApplication>
#include <QTimer>
#include <QtGlobal>

EffectController *EffectController::instance()
{
    static EffectController *controller = new EffectController(qApp);
    return controller;
}

EffectController::EffectController(QObject *parent)
    : QObject(parent), m_timer(new QTimer(this))
{
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, [this] {
        const qreal delta = m_timer->interval() / 1000.0 * m_speedScale;
        m_phase += delta;
        if (m_phase > 10000.0)
            m_phase = 0.0;
        emit frameAdvanced(m_phase, delta);
    });
}

void EffectController::setSpeedScale(qreal scale)
{
    m_speedScale = qBound<qreal>(0.1, scale, 2.0);
}

bool EffectController::isRunning() const { return m_timer->isActive(); }
void EffectController::start() { if (!m_timer->isActive()) m_timer->start(); }
void EffectController::stop() { m_timer->stop(); }
