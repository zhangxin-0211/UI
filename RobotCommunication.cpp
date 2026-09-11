#include "RobotCommunication.h"

#include "GeoCoordinateUtils.h"

#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QtMath>
#include <QDebug>

UdpRobotController::UdpRobotController(QObject *parent) : QObject(parent)
{
    m_socket = new QUdpSocket(this);
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(800);
    m_commandRetryTimer.setSingleShot(true);
    m_commandRetryTimer.setInterval(800);
    m_staleTimer.setInterval(1000);
    m_heartbeatTimer.setInterval(1000);
    connect(m_socket, &QUdpSocket::readyRead,
            this, &UdpRobotController::readPendingDatagrams);
    connect(&m_retryTimer, &QTimer::timeout,
            this, &UdpRobotController::retryPendingPacket);
    connect(&m_commandRetryTimer, &QTimer::timeout, this, [this] {
        if (m_pendingCommand.data.isEmpty()) return;
        if (m_pendingCommand.attempts >= 4) {
            const QString command = m_pendingCommand.expectedType == QStringLiteral("start_ack")
                ? QStringLiteral("启动")
                : m_pendingCommand.expectedType == QStringLiteral("return_ack")
                    ? QStringLiteral("返航") : QStringLiteral("停止");
            const QString missionId = m_pendingCommand.missionId;
            const QString message = QStringLiteral("%1 ACK 超时，已重试 3 次").arg(command);
            m_pendingCommand = PendingPacket();
            emit commandFailed(command, missionId, message);
            return;
        }
        sendPendingCommand();
    });
    connect(&m_staleTimer, &QTimer::timeout,
            this, &UdpRobotController::checkTelemetryFreshness);
    connect(&m_heartbeatTimer, &QTimer::timeout,
            this, &UdpRobotController::sendPeriodicHeartbeat);

}

UdpRobotController::~UdpRobotController()
{
    stop();
}

void UdpRobotController::configure(const RobotEndpoint &endpoint)
{
    const bool wasRunning = m_socket->state() == QAbstractSocket::BoundState;
    if (wasRunning) stop();
    m_endpoint = endpoint;
    if (wasRunning) start();
}

bool UdpRobotController::start()
{

    stop();
    QHostAddress validatedAddress;
    if (!validatedAddress.setAddress(m_endpoint.address)) {
        setLinkState(RobotLinkState::Error, QStringLiteral("设备 IP 地址格式无效"));
        return false;
    }
    QHostAddress localAddress;
    if (!localAddress.setAddress(QStringLiteral(LOCAL_BIND_IP))) {
        setLinkState(RobotLinkState::Error, QStringLiteral("本机 UDP 绑定 IP 地址格式无效"));
        return false;
    }
    if (!m_socket->bind(localAddress, m_endpoint.localPort)) {
        setLinkState(RobotLinkState::Error,
                     QStringLiteral("UDP 监听端口 %1 绑定失败：%2")
                         .arg(m_endpoint.localPort).arg(m_socket->errorString()));
        return false;
    }
    m_lastTelemetryTime = QDateTime();
    m_lastDeviceHeartbeatTime = QDateTime();
    m_localHeartbeatSent = false;
    m_staleTimer.start();
    if (m_externalCodec && !m_externalCodec->encodePeriodicHeartbeat().isEmpty()) {
        m_heartbeatTimer.start();
        sendPeriodicHeartbeat();
    }
    if (!m_externalCodec) {
        setLinkState(RobotLinkState::ProtocolUnavailable,
                     QStringLiteral("UDP 已自动监听，正式设备协议待接入"));
    } else {
        setLinkState(RobotLinkState::Listening,
                     QStringLiteral("UDP 已监听，正在建立双向心跳连接"));
    }
    return true;
}

void UdpRobotController::stop()
{
    m_retryTimer.stop();
    m_commandRetryTimer.stop();
    m_staleTimer.stop();
    m_heartbeatTimer.stop();
    m_pending = PendingPacket();
    m_pendingCommand = PendingPacket();
    m_uploadChunks.clear();
    m_uploadCommitData.clear();
    m_postUploadCommand.clear();
    m_socket->close();
    m_lastTelemetryTime = QDateTime();
    m_lastDeviceHeartbeatTime = QDateTime();
    m_localHeartbeatSent = false;
    setLinkState(RobotLinkState::Stopped, QStringLiteral("UDP 通信已停止"));
}

void UdpRobotController::sendDatagram(const QByteArray &data)
{
    QHostAddress address;
    if (!address.setAddress(m_endpoint.address)) return;
    sendDatagramTo(data, address, m_endpoint.devicePort);
}

void UdpRobotController::sendDatagramTo(const QByteArray &data,
                                        const QHostAddress &address, quint16 port)
{
    if (data.isEmpty() || port == 0) return;
    m_socket->writeDatagram(data, address, port);
}

void UdpRobotController::sendPeriodicHeartbeat()
{
    if (m_socket->state() != QAbstractSocket::BoundState || !m_externalCodec) return;
    const QByteArray heartbeat = m_externalCodec->encodePeriodicHeartbeat();
    if (heartbeat.isEmpty()) return;
    QHostAddress address;
    if (!address.setAddress(m_endpoint.address)) return;
    if (m_socket->writeDatagram(heartbeat, address, m_endpoint.devicePort) == heartbeat.size()) {
        m_localHeartbeatSent = true;
        updateHeartbeatLinkState();
    }
}

void UdpRobotController::sendPendingCommand()
{
    ++m_pendingCommand.attempts;
    sendDatagram(m_pendingCommand.data);
    m_commandRetryTimer.start();
}

void UdpRobotController::uploadAndStartMission(const PreparedMission &mission)
{
    beginMissionTransfer(mission, false);
}

void UdpRobotController::uploadAndReturnMission(const PreparedMission &mission)
{
    beginMissionTransfer(mission, true);
}

void UdpRobotController::beginMissionTransfer(const PreparedMission &mission, bool returning)
{
    const IRobotProtocolCodec *codec = m_externalCodec.get();
    if (!codec || m_socket->state() != QAbstractSocket::BoundState) {
        emit uploadFailed(QStringLiteral("正式协议未接入或 UDP 尚未启动，禁止上传任务"));
        return;
    }
    if (!isLinkEstablished()) {
        emit uploadFailed(QStringLiteral("双向心跳尚未建立，禁止上传任务"));
        return;
    }
    if (!codec->supportsMissionContext()) {
        emit uploadFailed(QStringLiteral("设备协议尚未定义起点、目标和路线任务帧，禁止下发"));
        return;
    }
    if (!codec->supportsWaypointHeadings()) {
        emit uploadFailed(QStringLiteral("设备协议不支持航点艏向，禁止上传返航姿态不完整的任务"));
        return;
    }
    if (!mission.valid || !mission.outboundRoute.valid
        || mission.returnRoute.points.size() < 2
        || !GeoCoordinateUtils::isValidLongitudeLatitude(mission.actualStartGcj02)
        || !GeoCoordinateUtils::isValidLongitudeLatitude(mission.safeTarget.position)
        || !isTelemetryFresh()) {
        emit uploadFailed(QStringLiteral("真实起点、目标、任务路线无效或设备遥测已过期"));
        return;
    }
    const auto headingsValid = [](const RoutePath &path) {
        for (const RoutePoint &point : path.points) {
            if (!point.headingValid || !qIsFinite(point.headingRadians)) return false;
        }
        return true;
    };
    if (!headingsValid(mission.outboundRoute) || !headingsValid(mission.returnRoute)) {
        emit uploadFailed(QStringLiteral("任务路线缺少有效艏向，禁止上传"));
        return;
    }

    constexpr int encodedPayloadBudget = 1100;
    const QVector<QByteArray> context = codec->encodeMissionContext(
        mission, returning, encodedPayloadBudget);
    const QVector<QByteArray> outbound = codec->encodeRoute(
        mission.missionId, mission.outboundRoute, encodedPayloadBudget);
    const QVector<QByteArray> inbound = codec->encodeReturnRoute(
        mission.missionId, mission.returnRoute, encodedPayloadBudget);
    if (context.isEmpty() || outbound.isEmpty() || inbound.isEmpty()) {
        emit uploadFailed(QStringLiteral("任务位置或路线编码失败"));
        return;
    }
    m_uploadChunks = context + outbound + inbound;
    m_uploadCommitData = codec->encodeCommit(mission.missionId, m_uploadChunks.size());
    if (m_uploadCommitData.isEmpty()) {
        emit uploadFailed(QStringLiteral("任务提交帧编码失败"));
        return;
    }
    m_uploadMissionId = mission.missionId;
    m_postUploadCommand = returning ? QStringLiteral("mission_return")
                                    : QStringLiteral("mission_start");
    m_nextUploadChunk = 0;
    m_acknowledgedChunks = 0;
    emit uploadProgress(0, m_uploadChunks.size());
    advanceUpload();
}

void UdpRobotController::cancelUpload()
{
    m_retryTimer.stop();
    m_pending = PendingPacket();
    m_commandRetryTimer.stop();
    m_pendingCommand = PendingPacket();
    m_uploadChunks.clear();
    m_uploadCommitData.clear();
    m_uploadMissionId.clear();
    m_postUploadCommand.clear();
    m_nextUploadChunk = 0;
    m_acknowledgedChunks = 0;
}

void UdpRobotController::advanceUpload()
{
    if (m_nextUploadChunk < m_uploadChunks.size()) {
        m_pending.data = m_uploadChunks.at(m_nextUploadChunk);
        m_pending.expectedType = QStringLiteral("chunk_ack");
        m_pending.missionId = m_uploadMissionId;
        m_pending.sequence = m_nextUploadChunk;
        m_pending.attempts = 0;
        sendCurrentPacket();
        return;
    }
    if (!m_externalCodec) {
        emit uploadFailed(QStringLiteral("设备协议待接入"));
        return;
    }
    m_pending.data = m_uploadCommitData.isEmpty()
        ? m_externalCodec->encodeCommit(m_uploadMissionId, m_uploadChunks.size())
        : m_uploadCommitData;
    m_pending.expectedType = QStringLiteral("commit_ack");
    m_pending.missionId = m_uploadMissionId;
    m_pending.sequence = -1;
    m_pending.attempts = 0;
    sendCurrentPacket();
}

void UdpRobotController::sendCurrentPacket()
{
    ++m_pending.attempts;
    sendDatagram(m_pending.data);
    m_retryTimer.start();
}

void UdpRobotController::retryPendingPacket()
{
    if (m_pending.data.isEmpty()) return;
    if (m_pending.attempts >= 4) {
        const QString message = QStringLiteral("UDP ACK 超时，已重试 3 次");
        m_pending = PendingPacket();
        m_uploadChunks.clear();
        emit uploadFailed(message);
        return;
    }
    sendCurrentPacket();
}

void UdpRobotController::readPendingDatagrams()
{
    while (m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        qDebug() << datagram.data().toHex()
                 << datagram.senderAddress().toString()
                 << datagram.senderPort();

        if (!m_externalCodec) continue;
        QHostAddress configuredAddress;
        if (!configuredAddress.setAddress(m_endpoint.address)
            || datagram.senderAddress() != configuredAddress) {
            continue;
        }

        QString type, missionId;
        int sequence = -1;
        RobotTelemetry telemetry;
        if (!m_externalCodec->decodeMessage(datagram.data(), &type, &missionId,
                                             &sequence, &telemetry)) {
            continue;
        }
        if (type == QStringLiteral("heartbeat")) {
            // Heartbeats are answered at the sender's endpoint, rather than
            // the configured command port: devices often source them from a
            // transient telemetry port.
            sendDatagramTo(m_externalCodec->encodeHeartbeatReply(datagram.data()),
                           datagram.senderAddress(), datagram.senderPort());
            m_lastDeviceHeartbeatTime = QDateTime::currentDateTimeUtc();
            updateHeartbeatLinkState();
            continue;
        }
        if (type == QStringLiteral("telemetry") && telemetry.valid) {
            m_lastTelemetryTime = telemetry.timestamp;
            if (isLinkEstablished())
                setLinkState(RobotLinkState::Online,
                             QStringLiteral("双向心跳已建立 · 真实设备遥测正常"));
            emit telemetryReceived(telemetry);
            continue;
        }
        if (type == m_pendingCommand.expectedType && !m_pendingCommand.data.isEmpty()
            && (m_pendingCommand.missionId.isEmpty()
                || missionId == m_pendingCommand.missionId)) {
            m_commandRetryTimer.stop();
            const QString command = type == QStringLiteral("start_ack")
                ? QStringLiteral("启动")
                : type == QStringLiteral("return_ack")
                    ? QStringLiteral("返航") : QStringLiteral("停止");
            m_pendingCommand = PendingPacket();
            emit commandAcknowledged(command, missionId);
        } else if (type == m_pending.expectedType
                   && (m_pending.missionId.isEmpty() || missionId == m_pending.missionId)
                   && (m_pending.sequence < 0 || sequence == m_pending.sequence)) {
            m_retryTimer.stop();
            m_pending = PendingPacket();
            if (type == QStringLiteral("chunk_ack")) {
                ++m_acknowledgedChunks;
                ++m_nextUploadChunk;
                emit uploadProgress(m_acknowledgedChunks, m_uploadChunks.size());
                advanceUpload();
            } else if (type == QStringLiteral("commit_ack")) {
                const QString completedMission = m_uploadMissionId;
                m_uploadChunks.clear();
                m_uploadCommitData.clear();
                emit routeUploaded(completedMission);
                startPendingPostUploadCommand();
            }
        } else if (type == QStringLiteral("commit_reject")
                   && (m_pending.missionId.isEmpty()
                       || missionId == m_pending.missionId)) {
            m_retryTimer.stop();
            m_pending = PendingPacket();
            emit uploadFailed(QStringLiteral("设备拒绝提交：路线分片不完整"));
        }
    }
}

void UdpRobotController::startPendingPostUploadCommand()
{
    const QString command = m_postUploadCommand;
    m_postUploadCommand.clear();
    if (command == QStringLiteral("mission_start")) {
        startMission(m_uploadMissionId);
    } else if (command == QStringLiteral("mission_return")) {
        returnMission(m_uploadMissionId);
    }
}

void UdpRobotController::startMission(const QString &missionId)
{
    if (!m_externalCodec || m_socket->state() != QAbstractSocket::BoundState) {
        emit commandFailed(QStringLiteral("启动"), missionId, QStringLiteral("设备协议未接入或 UDP 未监听"));
        return;
    }
    if (!isTelemetryFresh()) {
        emit commandFailed(QStringLiteral("启动"), missionId, QStringLiteral("设备遥测已过期，未启动任务"));
        return;
    }
    if (!isLinkEstablished()) {
        emit commandFailed(QStringLiteral("启动"), missionId,
                           QStringLiteral("双向心跳未建立，未启动任务"));
        return;
    }
    if (!m_pendingCommand.data.isEmpty()) {
        emit commandFailed(QStringLiteral("启动"), missionId, QStringLiteral("上一条设备指令仍在等待确认"));
        return;
    }
    const QByteArray command = m_externalCodec->encodeCommand(missionId, QStringLiteral("mission_start"));
    if (command.isEmpty()) {
        emit commandFailed(QStringLiteral("启动"), missionId, QStringLiteral("启动指令编码失败"));
        return;
    }
    m_pendingCommand.data = command;
    m_pendingCommand.expectedType = QStringLiteral("start_ack");
    m_pendingCommand.missionId = missionId;
    m_pendingCommand.attempts = 0;
    sendPendingCommand();
}

void UdpRobotController::returnMission(const QString &missionId)
{
    if (!m_externalCodec || m_socket->state() != QAbstractSocket::BoundState) {
        emit commandFailed(QStringLiteral("返航"), missionId, QStringLiteral("设备协议未接入或 UDP 未监听"));
        return;
    }
    if (!isTelemetryFresh()) {
        emit commandFailed(QStringLiteral("返航"), missionId, QStringLiteral("设备遥测已过期，未启动返航"));
        return;
    }
    if (!isLinkEstablished()) {
        emit commandFailed(QStringLiteral("返航"), missionId,
                           QStringLiteral("双向心跳未建立，未启动返航"));
        return;
    }
    if (!m_pendingCommand.data.isEmpty()) {
        emit commandFailed(QStringLiteral("返航"), missionId, QStringLiteral("上一条设备指令仍在等待确认"));
        return;
    }
    const QByteArray command = m_externalCodec->encodeCommand(missionId, QStringLiteral("mission_return"));
    if (command.isEmpty()) {
        emit commandFailed(QStringLiteral("返航"), missionId, QStringLiteral("返航指令编码失败"));
        return;
    }
    m_pendingCommand.data = command;
    m_pendingCommand.expectedType = QStringLiteral("return_ack");
    m_pendingCommand.missionId = missionId;
    m_pendingCommand.attempts = 0;
    sendPendingCommand();
}

void UdpRobotController::stopMission(const QString &missionId)
{
    if (!m_externalCodec || m_socket->state() != QAbstractSocket::BoundState
        || !m_pendingCommand.data.isEmpty()) {
        return;
    }
    m_pendingCommand.data = m_externalCodec->encodeCommand(
        missionId, QStringLiteral("mission_stop"));
    m_pendingCommand.expectedType = QStringLiteral("stop_ack");
    m_pendingCommand.missionId = missionId;
    m_pendingCommand.attempts = 0;
    sendPendingCommand();
}

bool UdpRobotController::isTelemetryFresh() const
{
    return m_lastTelemetryTime.isValid()
        && m_lastTelemetryTime.msecsTo(QDateTime::currentDateTimeUtc()) <= 5000;
}

bool UdpRobotController::isLinkEstablished() const
{
    return m_localHeartbeatSent && m_lastDeviceHeartbeatTime.isValid()
        && m_lastDeviceHeartbeatTime.msecsTo(QDateTime::currentDateTimeUtc()) <= 3500;
}

void UdpRobotController::checkTelemetryFreshness()
{
    if (m_localHeartbeatSent && m_lastDeviceHeartbeatTime.isValid()
        && !isLinkEstablished()) {
        setLinkState(RobotLinkState::Stale,
                     QStringLiteral("设备心跳超过 3 秒未更新，UDP 连接已断开"));
        return;
    }
    if (m_lastTelemetryTime.isValid() && !isTelemetryFresh()) {
        setLinkState(RobotLinkState::Stale,
                     QStringLiteral("设备遥测超过 5 秒未更新，任务操作已锁定"));
    }
}

void UdpRobotController::updateHeartbeatLinkState()
{
    if (isLinkEstablished()) {
        setLinkState(RobotLinkState::Online,
                     QStringLiteral("双向心跳已建立，等待或接收设备遥测"));
    } else if (m_localHeartbeatSent) {
        setLinkState(RobotLinkState::Listening,
                     QStringLiteral("本机心跳已发送，等待设备心跳"));
    }
}

void UdpRobotController::setLinkState(RobotLinkState state, const QString &message)
{
    if (m_linkState == state && state == RobotLinkState::Online) return;
    m_linkState = state;
    emit linkStateChanged(state, message);
}
