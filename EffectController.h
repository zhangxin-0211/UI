#pragma once

#include <QObject>

class QTimer;

class EffectController : public QObject
{
    Q_OBJECT

public:
    static EffectController *instance();
    qreal phase() const { return m_phase; }
    qreal speedScale() const { return m_speedScale; }
    void setSpeedScale(qreal scale);
    bool isRunning() const;
    void start();
    void stop();

signals:
    void frameAdvanced(qreal phase, qreal deltaSeconds);

private:
    explicit EffectController(QObject *parent = nullptr);
    QTimer *m_timer = nullptr;
    qreal m_phase = 0.0;
    qreal m_speedScale = 0.5;
};
