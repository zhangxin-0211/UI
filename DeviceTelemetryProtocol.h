#pragma once

#include "RobotCommunication.h"

// Receive-only codec for the protocol currently supplied by the device.
// Route/control encoders deliberately remain empty until their byte protocol
// is defined by the device side.
class DeviceTelemetryProtocol final : public IRobotProtocolCodec
{
public:
    QVector<QByteArray> encodeRoute(const QString &missionId, const RoutePath &route,
                                    int maximumDatagramBytes) const override;
    QVector<QByteArray> encodeObstacles(const QString &missionId,
                                        const QVector<QPointF> &obstaclePoints,
                                        quint64 revision,
                                        int maximumDatagramBytes) const override;
    QByteArray encodeCommit(const QString &missionId, int chunkCount) const override;
    QByteArray encodeCommand(const QString &missionId, const QString &command) const override;
    QByteArray encodePeriodicHeartbeat() const override;
    bool decodeMessage(const QByteArray &datagram, QString *type,
                       QString *missionId, int *sequence,
                       RobotTelemetry *telemetry) const override;
};
