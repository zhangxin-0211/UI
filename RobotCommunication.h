#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QVector>

#include <memory>

#include "MissionTypes.h"

class QUdpSocket;

class IRobotProtocolCodec
{
public:
    virtual ~IRobotProtocolCodec() = default;
    virtual QVector<QByteArray> encodeRoute(const QString &missionId,
                                            const RoutePath &route,
                                            int maximumDatagramBytes) const = 0;
    virtual QVector<QByteArray> encodeObstacles(const QString &missionId,
                                                const QVector<QPointF> &obstaclePoints,
                                                quint64 revision,
                                                int maximumDatagramBytes) const = 0;
    virtual QByteArray encodeCommit(const QString &missionId, int chunkCount) const = 0;
    virtual QByteArray encodeCommand(const QString &missionId, const QString &command) const = 0;
    // Every mission transfer includes the immutable map/planning snapshot.
    // The eventual device codec must encode the true device origin, safe
    // target, and route identity before it accepts route-point chunks.
    virtual bool supportsMissionContext() const { return false; }
    virtual QVector<QByteArray> encodeMissionContext(const PreparedMission &mission,
                                                     bool returning,
                                                     int maximumDatagramBytes) const
    {
        Q_UNUSED(mission)
        Q_UNUSED(returning)
        Q_UNUSED(maximumDatagramBytes)
        return {};
    }
    // A production codec must explicitly opt in and encode headingRadians for
    // every outbound and return waypoint. Upload is rejected otherwise.
    virtual bool supportsWaypointHeadings() const { return false; }
    // Some device protocols define a route frame as the execution command
    // itself.  Such a protocol has no separate context/commit/start frames
    // and does not acknowledge UDP datagrams on the wire.
    virtual bool usesDirectRouteCommands() const { return false; }
    virtual QVector<QByteArray> encodeReturnRoute(const QString &missionId,
                                                  const RoutePath &route,
                                                  int maximumDatagramBytes) const
    { return encodeRoute(missionId, route, maximumDatagramBytes); }
    virtual bool decodeMessage(const QByteArray &datagram, QString *type,
                               QString *missionId, int *sequence,
                               RobotTelemetry *telemetry) const = 0;
    // decodeMessage() returns type == "heartbeat" for device heartbeats.
    // The device currently accepts arbitrary content as its response; use a
    // small visible default until its wire protocol specifies otherwise.
    virtual QByteArray encodeHeartbeatReply(const QByteArray &heartbeat) const
    {
        Q_UNUSED(heartbeat)
        return QByteArrayLiteral("ACK");
    }
    // A codec may provide a periodic device-link heartbeat. An empty value
    // means that this protocol has no autonomous heartbeat frame.
    virtual QByteArray encodePeriodicHeartbeat() const { return {}; }
};

class UdpRobotController : public QObject
{
    Q_OBJECT
public:
    explicit UdpRobotController(QObject *parent = nullptr);
    ~UdpRobotController() override;

    void configure(const RobotEndpoint &endpoint);
    void setProtocolCodec(const std::shared_ptr<IRobotProtocolCodec> &codec)
    { m_externalCodec = codec; }
    bool start();
    void stop();
    // Both actions transmit the frozen mission context (true device origin,
    // target and route) before route chunks. Upload automatically starts the
    // outbound task; return automatically starts the reversed route.
    void uploadAndStartMission(const PreparedMission &mission);
    void uploadAndReturnMission(const PreparedMission &mission);
    void cancelUpload();
    void startMission(const QString &missionId);
    void returnMission(const QString &missionId);
    void stopMission(const QString &missionId);
    bool isTelemetryFresh() const;
    // The UDP link is established only after this PC has transmitted a
    // heartbeat and a fresh heartbeat has arrived from the configured device.
    bool isLinkEstablished() const;
    bool hasExternalProtocolCodec() const { return bool(m_externalCodec); }
    bool hasMissionProtocolCodec() const
    {
        return m_externalCodec
            && (m_externalCodec->supportsMissionContext()
                || m_externalCodec->usesDirectRouteCommands());
    }

signals:
    void linkStateChanged(RobotLinkState state, const QString &message);
    void telemetryReceived(const RobotTelemetry &telemetry);
    void uploadProgress(int acknowledged, int total);
    void routeUploaded(const QString &missionId);
    void uploadFailed(const QString &message);
    void commandAcknowledged(const QString &command, const QString &missionId);
    void commandFailed(const QString &command, const QString &missionId,
                       const QString &message);

private slots:
    void readPendingDatagrams();
    void retryPendingPacket();
    void checkTelemetryFreshness();
    void sendPeriodicHeartbeat();

private:
    struct PendingPacket {
        QByteArray data;
        QString expectedType;
        QString missionId;
        int sequence = -1;
        int attempts = 0;
    };
    void sendCurrentPacket();
    void sendPendingCommand();
    void advanceUpload();
    void sendDatagram(const QByteArray &data);
    void sendDatagramTo(const QByteArray &data, const QHostAddress &address, quint16 port);
    void beginMissionTransfer(const PreparedMission &mission, bool returning);
    void startPendingPostUploadCommand();
    void setLinkState(RobotLinkState state, const QString &message);
    void updateHeartbeatLinkState();

    RobotEndpoint m_endpoint;
    QUdpSocket *m_socket = nullptr;
    std::shared_ptr<IRobotProtocolCodec> m_externalCodec;
    QTimer m_retryTimer;
    QTimer m_commandRetryTimer;
    QTimer m_staleTimer;
    QTimer m_heartbeatTimer;
    QVector<QByteArray> m_uploadChunks;
    QByteArray m_uploadCommitData;
    PendingPacket m_pending;
    PendingPacket m_pendingCommand;
    QString m_uploadMissionId;
    QString m_postUploadCommand;
    int m_nextUploadChunk = 0;
    int m_acknowledgedChunks = 0;
    QDateTime m_lastTelemetryTime;
    QDateTime m_lastDeviceHeartbeatTime;
    bool m_localHeartbeatSent = false;
    RobotLinkState m_linkState = RobotLinkState::Stopped;
};
