#pragma once

#include <QObject>

class QTimer;

class DemoDataModel : public QObject
{
    Q_OBJECT

public:
    explicit DemoDataModel(QObject *parent = nullptr);
    void start();
    void stop();

signals:
    void sampleReady(double phase, int sequence);

private:
    QTimer *m_timer = nullptr;
    double m_phase = 0.0;
    int m_sequence = 0;
};
