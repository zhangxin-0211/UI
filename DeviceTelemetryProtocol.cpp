#include "DeviceTelemetryProtocol.h"

#include "GeoCoordinateUtils.h"

#include <QDateTime>

namespace {
constexpr uchar kHeader = 0x55;
constexpr uchar kTrailer = 0xFF;
constexpr int kHeartbeatBytes = 4;
constexpr int kTelemetryBytes = 15;
constexpr int kChecksumIndex = 12;

quint32 readBigEndianU32(const QByteArray &data, int offset)
{
    return (quint32(uchar(data.at(offset))) << 24)
        | (quint32(uchar(data.at(offset + 1))) << 16)
        | (quint32(uchar(data.at(offset + 2))) << 8)
        | quint32(uchar(data.at(offset + 3)));
}

quint16 readBigEndianU16(const QByteArray &data, int offset)
{
    return (quint16(uchar(data.at(offset))) << 8) | quint16(uchar(data.at(offset + 1)));
}

uchar checksum(const QByteArray &data)
{
    uchar value = 0;
    for (int i = 0; i < kChecksumIndex; ++i)
        value = uchar(value + uchar(data.at(i)));
    return value;
}
}

QVector<QByteArray> DeviceTelemetryProtocol::encodeRoute(
    const QString &missionId, const RoutePath &route, int maximumDatagramBytes) const
{
    Q_UNUSED(missionId)
    Q_UNUSED(route)
    Q_UNUSED(maximumDatagramBytes)
    return {};
}

QVector<QByteArray> DeviceTelemetryProtocol::encodeObstacles(
    const QString &missionId, const QVector<QPointF> &obstaclePoints,
    quint64 revision, int maximumDatagramBytes) const
{
    Q_UNUSED(missionId)
    Q_UNUSED(obstaclePoints)
    Q_UNUSED(revision)
    Q_UNUSED(maximumDatagramBytes)
    return {};
}

QByteArray DeviceTelemetryProtocol::encodeCommit(const QString &missionId, int chunkCount) const
{
    Q_UNUSED(missionId)
    Q_UNUSED(chunkCount)
    return {};
}

QByteArray DeviceTelemetryProtocol::encodeCommand(const QString &missionId,
                                                   const QString &command) const
{
    Q_UNUSED(missionId)
    Q_UNUSED(command)
    return {};
}

QByteArray DeviceTelemetryProtocol::encodePeriodicHeartbeat() const
{
    return QByteArray::fromHex("5555ffff");
}

bool DeviceTelemetryProtocol::decodeMessage(const QByteArray &datagram, QString *type,
                                            QString *missionId, int *sequence,
                                            RobotTelemetry *telemetry) const
{
    if (type) type->clear();
    if (missionId) missionId->clear();
    if (sequence) *sequence = -1;
    if (telemetry) *telemetry = RobotTelemetry();

    if (datagram.size() == kHeartbeatBytes
        && uchar(datagram.at(0)) == kHeader && uchar(datagram.at(1)) == kHeader
        && uchar(datagram.at(2)) == kTrailer && uchar(datagram.at(3)) == kTrailer) {
        if (type) *type = QStringLiteral("heartbeat");
        return true;
    }

    if (datagram.size() != kTelemetryBytes
        || uchar(datagram.at(0)) != kHeader || uchar(datagram.at(1)) != kHeader
        || uchar(datagram.at(13)) != kTrailer || uchar(datagram.at(14)) != kTrailer
        || uchar(datagram.at(kChecksumIndex)) != checksum(datagram)) {
        return false;
    }

    // Actual device telemetry places GCJ-02 latitude first, then longitude.
    const double latitude = double(readBigEndianU32(datagram, 2)) / 10000000.0;
    const double longitude = double(readBigEndianU32(datagram, 6)) / 10000000.0;
    const double headingDegrees = double(readBigEndianU16(datagram, 10)) / 100.0;
    if (!GeoCoordinateUtils::isValidLongitudeLatitude(QPointF(longitude, latitude))
        || headingDegrees < 0.0 || headingDegrees > 360.0) {
        return false;
    }

    if (type) *type = QStringLiteral("telemetry");
    if (telemetry) {
        telemetry->gcj02Position = QPointF(longitude, latitude);
        telemetry->headingDegrees = headingDegrees;
        telemetry->timestamp = QDateTime::currentDateTimeUtc();
        telemetry->valid = true;
        telemetry->headingValid = true;
    }
    return true;
}
