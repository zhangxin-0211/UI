#include "LocationController.h"

#include "GeoCoordinateUtils.h"

#include <QTimer>
#include <QtMath>

namespace {

bool sameFix(const LocationFix &first, const LocationFix &second)
{
    if (first.valid != second.valid) return false;
    if (!first.valid) return true;
    return qAbs(first.gcj02Position.x() - second.gcj02Position.x()) < 1e-9
        && qAbs(first.gcj02Position.y() - second.gcj02Position.y()) < 1e-9
        && qAbs(first.horizontalAccuracyMeters - second.horizontalAccuracyMeters) < 0.01
        && first.timestamp == second.timestamp
        && first.providerDetail == second.providerDetail;
}

QString accuracyText(double accuracyMeters)
{
    if (!qIsFinite(accuracyMeters) || accuracyMeters < 0.0)
        return QStringLiteral("精度未知");
    const int decimals = accuracyMeters < 10.0 ? 1 : 0;
    return QStringLiteral("±%1 m").arg(accuracyMeters, 0, 'f', decimals);
}

}

LocationController::LocationController(QObject *parent) : QObject(parent)
{
    m_evaluationTimer = new QTimer(this);
    m_evaluationTimer->setInterval(1000);
    connect(m_evaluationTimer, &QTimer::timeout, this, &LocationController::evaluate);
    m_evaluationTimer->start();
}

void LocationController::updateGpsLocation(const LocationFix &fix)
{
    m_gpsFix = fix;
    if (!m_gpsFix.timestamp.isValid())
        m_gpsFix.timestamp = QDateTime::currentDateTimeUtc();
    m_gpsFix.valid = m_gpsFix.valid
        && GeoCoordinateUtils::isValidLongitudeLatitude(m_gpsFix.gcj02Position);
    if (m_gpsFix.valid) m_gpsAvailable = true;
    evaluate();
}

void LocationController::setGpsAvailable(bool available)
{
    m_gpsAvailable = available;
    evaluate();
}

void LocationController::updateAmapLocation(const LocationFix &fix)
{
    m_amapFix = fix;
    if (!m_amapFix.timestamp.isValid())
        m_amapFix.timestamp = QDateTime::currentDateTimeUtc();
    m_amapFix.valid = m_amapFix.valid
        && GeoCoordinateUtils::isValidLongitudeLatitude(m_amapFix.gcj02Position);
    if (m_amapFix.valid) m_lastAmapError.clear();
    evaluate();
}

void LocationController::reportAmapFailure(const QString &message)
{
    m_lastAmapError = message.trimmed();
    m_amapFix.valid = false;
    evaluate();
}

bool LocationController::isFresh(const LocationFix &fix, int maximumAgeMs) const
{
    if (!fix.valid || !fix.timestamp.isValid()) return false;
    const qint64 age = fix.timestamp.msecsTo(QDateTime::currentDateTimeUtc());
    return age >= -5000 && age <= maximumAgeMs;
}

QString LocationController::formatStatus(const LocationFix &fix, LocationSource source) const
{
    switch (source) {
    case LocationSource::Gps:
        return QStringLiteral("GPS · %1").arg(accuracyText(fix.horizontalAccuracyMeters));
    case LocationSource::Amap: {
        const QString detail = fix.providerDetail.trimmed().isEmpty()
            ? QStringLiteral("AMAP") : QStringLiteral("AMAP/%1").arg(fix.providerDetail.trimmed().toUpper());
        return QStringLiteral("%1 · %2").arg(detail, accuracyText(fix.horizontalAccuracyMeters));
    }
    case LocationSource::LastKnown: {
        const qint64 seconds = fix.timestamp.isValid()
            ? qMax<qint64>(0, fix.timestamp.secsTo(QDateTime::currentDateTimeUtc())) : 0;
        QString status = QStringLiteral("LAST KNOWN · %1 s AGO").arg(seconds);
        if (!m_lastAmapError.isEmpty()) status += QStringLiteral(" · AMAP ERROR");
        return status;
    }
    case LocationSource::None:
        break;
    }
    return m_lastAmapError.isEmpty() ? QStringLiteral("LOCATION · WAITING")
                                     : QStringLiteral("LOCATION · AMAP FAILED");
}

void LocationController::evaluate()
{
    LocationFix selected;
    LocationSource source = LocationSource::None;

    if (m_gpsAvailable && isFresh(m_gpsFix, m_gpsFreshnessMs)) {
        selected = m_gpsFix;
        source = LocationSource::Gps;
    } else if (isFresh(m_amapFix, m_amapFreshnessMs)) {
        selected = m_amapFix;
        source = LocationSource::Amap;
    } else if (m_lastValidFix.valid) {
        selected = m_lastValidFix;
        source = LocationSource::LastKnown;
    }

    if (selected.valid && source != LocationSource::LastKnown)
        m_lastValidFix = selected;

    const bool changed = source != m_currentSource || !sameFix(selected, m_currentFix);
    m_currentFix = selected;
    m_currentSource = source;
    if (changed) emit currentLocationChanged(m_currentFix, m_currentSource);
    const QString status = formatStatus(m_currentFix, m_currentSource);
    if (status != m_lastStatus) {
        m_lastStatus = status;
        emit statusChanged(status);
    }
}
