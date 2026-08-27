#pragma once

#include <QObject>

#include "LocationTypes.h"

class QTimer;

class LocationController : public QObject
{
    Q_OBJECT

public:
    explicit LocationController(QObject *parent = nullptr);

    LocationFix currentLocation() const { return m_currentFix; }
    LocationSource currentSource() const { return m_currentSource; }
    bool hasCurrentLocation() const { return m_currentFix.valid; }

public slots:
    void updateGpsLocation(const LocationFix &fix);
    void setGpsAvailable(bool available);
    void updateAmapLocation(const LocationFix &fix);
    void reportAmapFailure(const QString &message);

signals:
    void currentLocationChanged(const LocationFix &fix, LocationSource source);
    void statusChanged(const QString &status);

private:
    bool isFresh(const LocationFix &fix, int maximumAgeMs) const;
    void evaluate();
    QString formatStatus(const LocationFix &fix, LocationSource source) const;

    QTimer *m_evaluationTimer = nullptr;
    LocationFix m_gpsFix;
    LocationFix m_amapFix;
    LocationFix m_lastValidFix;
    LocationFix m_currentFix;
    LocationSource m_currentSource = LocationSource::None;
    bool m_gpsAvailable = false;
    QString m_lastAmapError;
    QString m_lastStatus;
    int m_gpsFreshnessMs = 10000;
    int m_amapFreshnessMs = 60000;
};
