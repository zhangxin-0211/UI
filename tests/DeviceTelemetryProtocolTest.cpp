#include <QtTest>

#include <QNetworkDatagram>
#include <QUdpSocket>

#include <memory>

#include "DeviceTelemetryProtocol.h"

namespace {
void writeBigEndianU32(QByteArray &data, int offset, quint32 value)
{
    data[offset] = char((value >> 24) & 0xff);
    data[offset + 1] = char((value >> 16) & 0xff);
    data[offset + 2] = char((value >> 8) & 0xff);
    data[offset + 3] = char(value & 0xff);
}

void writeBigEndianU16(QByteArray &data, int offset, quint16 value)
{
    data[offset] = char((value >> 8) & 0xff);
    data[offset + 1] = char(value & 0xff);
}

QByteArray telemetryFrame(quint32 longitude, quint32 latitude, quint16 heading)
{
    QByteArray frame(15, '\0');
    frame[0] = char(0x55); frame[1] = char(0x55);
    writeBigEndianU32(frame, 2, longitude);
    writeBigEndianU32(frame, 6, latitude);
    writeBigEndianU16(frame, 10, heading);
    uchar checksum = 0;
    for (int i = 0; i < 12; ++i) checksum = uchar(checksum + uchar(frame.at(i)));
    frame[12] = char(checksum);
    frame[13] = char(0xff); frame[14] = char(0xff);
    return frame;
}
}

class DeviceTelemetryProtocolTest final : public QObject
{
    Q_OBJECT

private slots:
    void acceptsHeartbeat();
    void acceptsTelemetry();
    void rejectsMalformedFrames();
    void sendsPeriodicHeartbeat();
};

void DeviceTelemetryProtocolTest::acceptsHeartbeat()
{
    DeviceTelemetryProtocol codec;
    QString type, missionId;
    int sequence = 0;
    RobotTelemetry telemetry;
    QVERIFY(codec.decodeMessage(QByteArray::fromHex("5555ffff"), &type, &missionId,
                                &sequence, &telemetry));
    QCOMPARE(type, QStringLiteral("heartbeat"));
    QCOMPARE(codec.encodeHeartbeatReply(QByteArray::fromHex("5555ffff")), QByteArray("ACK"));
    QCOMPARE(codec.encodePeriodicHeartbeat(), QByteArray::fromHex("5555ffff"));
}

void DeviceTelemetryProtocolTest::acceptsTelemetry()
{
    DeviceTelemetryProtocol codec;
    QString type, missionId;
    int sequence = 0;
    RobotTelemetry telemetry;
    const QByteArray frame = telemetryFrame(1134903745u, 221500000u, 12345u);
    QVERIFY(codec.decodeMessage(frame, &type, &missionId, &sequence, &telemetry));
    QCOMPARE(type, QStringLiteral("telemetry"));
    QVERIFY(telemetry.valid);
    QVERIFY(telemetry.headingValid);
    QCOMPARE(telemetry.gcj02Position.x(), 113.4903745);
    QCOMPARE(telemetry.gcj02Position.y(), 22.15);
    QCOMPARE(telemetry.headingDegrees, 123.45);
}

void DeviceTelemetryProtocolTest::rejectsMalformedFrames()
{
    DeviceTelemetryProtocol codec;
    QString type, missionId;
    int sequence = -1;
    RobotTelemetry telemetry;
    QByteArray frame = telemetryFrame(1134903745u, 221500000u, 12345u);
    frame[12] = char(uchar(frame.at(12)) + 1);
    QVERIFY(!codec.decodeMessage(frame, &type, &missionId, &sequence, &telemetry));
    frame = telemetryFrame(1134903745u, 221500000u, 12345u);
    frame[13] = '\0';
    QVERIFY(!codec.decodeMessage(frame, &type, &missionId, &sequence, &telemetry));
    QVERIFY(!codec.decodeMessage(QByteArray::fromHex("5555ffff00"), &type, &missionId,
                                 &sequence, &telemetry));
    frame = telemetryFrame(1134903745u, 900000001u, 12345u);
    QVERIFY(!codec.decodeMessage(frame, &type, &missionId, &sequence, &telemetry));
    frame = telemetryFrame(1134903745u, 221500000u, 36001u);
    QVERIFY(!codec.decodeMessage(frame, &type, &missionId, &sequence, &telemetry));
}

void DeviceTelemetryProtocolTest::sendsPeriodicHeartbeat()
{
    QUdpSocket receiver;
    const QHostAddress localhost(QHostAddress::LocalHost);
    QVERIFY(receiver.bind(localhost, quint16(0)));

    QUdpSocket localPortReservation;
    QVERIFY(localPortReservation.bind(localhost, quint16(0)));
    const quint16 controllerPort = localPortReservation.localPort();
    localPortReservation.close();

    RobotEndpoint endpoint;
    endpoint.address = QStringLiteral("127.0.0.1");
    endpoint.devicePort = receiver.localPort();
    endpoint.localPort = controllerPort;

    UdpRobotController controller;
    controller.configure(endpoint);
    controller.setProtocolCodec(std::make_shared<DeviceTelemetryProtocol>());
    QVERIFY(controller.start());

    QTRY_VERIFY_WITH_TIMEOUT(receiver.hasPendingDatagrams(), 1800);
    const QNetworkDatagram datagram = receiver.receiveDatagram();
    QCOMPARE(datagram.data(), QByteArray::fromHex("5555ffff"));
    QCOMPARE(datagram.senderPort(), controllerPort);

    controller.stop();
}

QTEST_APPLESS_MAIN(DeviceTelemetryProtocolTest)
#include "DeviceTelemetryProtocolTest.moc"
