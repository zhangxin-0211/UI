#include "DeviceTelemetryProtocol.h"

#include "GeoCoordinateUtils.h"

#include <QDateTime>
#include <QtMath>

#include <limits>

namespace {
constexpr uchar kHeader = 0x55;
constexpr uchar kTrailer = 0xFF;
constexpr int kHeartbeatBytes = 4;
constexpr int kTelemetryBytes = 15;
constexpr int kChecksumIndex = 12;
constexpr uchar kStopCommand = 0x00;
constexpr uchar kPatrolCommand = 0x01;
constexpr uchar kReturnCommand = 0x02;
constexpr int kRouteFrameFixedBytes = 8; // header + length + command + checksum + trailer

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

void appendBigEndianU16(QByteArray *data, quint16 value)
{
    data->append(char((value >> 8) & 0xff));
    data->append(char(value & 0xff));
}

void appendBigEndianU32(QByteArray *data, quint32 value)
{
    data->append(char((value >> 24) & 0xff));
    data->append(char((value >> 16) & 0xff));
    data->append(char((value >> 8) & 0xff));
    data->append(char(value & 0xff));
}

QByteArray encodeRouteFrame(uchar command, const RoutePath &route,
                            int maximumDatagramBytes)
{
    if (!route.valid || route.points.isEmpty()) return {};

    const qint64 totalBytes = kRouteFrameFixedBytes + qint64(route.points.size()) * 8;
    if (totalBytes > 0xffff || totalBytes > maximumDatagramBytes) return {};

    QByteArray frame;
    frame.reserve(int(totalBytes));
    frame.append(char(kHeader));
    frame.append(char(kHeader));
    appendBigEndianU16(&frame, quint16(totalBytes));
    frame.append(char(command));
    for (const RoutePoint &point : route.points) {
        const QPointF position = point.position;
        if (!GeoCoordinateUtils::isValidLongitudeLatitude(position)) return {};
        const qint64 longitude = qRound64(position.x() * 10000000.0);
        const qint64 latitude = qRound64(position.y() * 10000000.0);
        if (longitude < 0 || longitude > std::numeric_limits<quint32>::max()
            || latitude < 0 || latitude > std::numeric_limits<quint32>::max()) {
            return {};
        }
        // The command protocol defines longitude first, then latitude.
        appendBigEndianU32(&frame, quint32(longitude));
        appendBigEndianU32(&frame, quint32(latitude));
    }

    uchar sum = 0;
    for (const char byte : frame) sum = uchar(sum + uchar(byte));
    frame.append(char(sum));
    frame.append(char(kTrailer));
    frame.append(char(kTrailer));
    return frame;
}

QByteArray encodeStopFrame()
{
    QByteArray frame;
    frame.reserve(kRouteFrameFixedBytes);
    frame.append(char(kHeader));
    frame.append(char(kHeader));
    appendBigEndianU16(&frame, kRouteFrameFixedBytes);
    frame.append(char(kStopCommand));
    uchar sum = 0;
    for (const char byte : frame) sum = uchar(sum + uchar(byte));
    frame.append(char(sum));
    frame.append(char(kTrailer));
    frame.append(char(kTrailer));
    return frame;
}
}

QVector<QByteArray> DeviceTelemetryProtocol::encodeRoute(
    const QString &missionId, const RoutePath &route, int maximumDatagramBytes) const
{
    Q_UNUSED(missionId)
    const QByteArray frame = encodeRouteFrame(kPatrolCommand, route, maximumDatagramBytes);
    return frame.isEmpty() ? QVector<QByteArray>() : QVector<QByteArray>{frame};
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
    return command == QStringLiteral("mission_stop") ? encodeStopFrame() : QByteArray();
}

QVector<QByteArray> DeviceTelemetryProtocol::encodeReturnRoute(
    const QString &missionId, const RoutePath &route, int maximumDatagramBytes) const
{
    Q_UNUSED(missionId)
    const QByteArray frame = encodeRouteFrame(kReturnCommand, route, maximumDatagramBytes);
    return frame.isEmpty() ? QVector<QByteArray>() : QVector<QByteArray>{frame};
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
