#include "DemoDataModel.h"

#include <QTimer>

DemoDataModel::DemoDataModel(QObject *parent)
    : QObject(parent), m_timer(new QTimer(this))
{
    m_timer->setInterval(120);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_phase += 0.085;
        ++m_sequence;
        emit sampleReady(m_phase, m_sequence);
    });
}

void DemoDataModel::start() { m_timer->start(); }
void DemoDataModel::stop() { m_timer->stop(); }
