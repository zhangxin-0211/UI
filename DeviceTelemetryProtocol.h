#pragma once

#include "RobotCommunication.h"

// Device telemetry codec plus the device's direct route-command wire format.
// All outbound positions are GCJ-02 longitude/latitude values scaled by 1e7.
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
    bool usesDirectRouteCommands() const override { return true; }
    QVector<QByteArray> encodeReturnRoute(const QString &missionId,
                                          const RoutePath &route,
                                          int maximumDatagramBytes) const override;
    QByteArray encodePeriodicHeartbeat() const override;
    bool decodeMessage(const QByteArray &datagram, QString *type,
                       QString *missionId, int *sequence,
                       RobotTelemetry *telemetry) const override;
};
