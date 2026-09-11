#include "Pages.h"
#include "CameraController.h"
#include "DeviceTelemetryProtocol.h"
#include "DemoDataModel.h"
#include "EffectController.h"
#include "GeoCoordinateUtils.h"
#include "RobotCommunication.h"
#include "RoutePlanner.h"
#include "SuppliedHybridAstarBridge.h"
#include "Theme.h"
#include "WaterwayRecognizer.h"
#include "Widgets.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLinearGradient>
#include <QLineF>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTextStream>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QPainter>

#include <algorithm>
#include <QPainterPath>
#include <QtMath>
#include <cmath>
#include <limits>

namespace {

double geographicDistanceMeters(const QPointF &from, const QPointF &to)
{
    constexpr double earthRadiusMeters = 6378137.0;
    const double latitude1 = qDegreesToRadians(from.y());
    const double latitude2 = qDegreesToRadians(to.y());
    const double deltaLatitude = latitude2 - latitude1;
    const double deltaLongitude = qDegreesToRadians(to.x() - from.x());
    const double a = qPow(qSin(deltaLatitude * 0.5), 2.0)
        + qCos(latitude1) * qCos(latitude2) * qPow(qSin(deltaLongitude * 0.5), 2.0);
    return 2.0 * earthRadiusMeters * qAtan2(qSqrt(a), qSqrt(qMax(0.0, 1.0 - a)));
}

double geographicHeadingRadians(const QPointF &from, const QPointF &to)
{
    const double meanLatitude = qDegreesToRadians((from.y() + to.y()) * 0.5);
    const double east = qDegreesToRadians(to.x() - from.x()) * qCos(meanLatitude);
    const double north = qDegreesToRadians(to.y() - from.y());
    double heading = qAtan2(east, north);
    if (heading < 0.0) heading += 2.0 * qAcos(-1.0);
    return heading;
}

QLabel *label(const QString &text, int pointSize = 10, bool bold = false)
{
    QLabel *result = new QLabel(text);
    result->setFont(Theme::font(pointSize, bold));
    result->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::text().name()));
    return result;
}

QLabel *createMetricLabel(const QString &name, const QString &value)
{
    QLabel *result = new QLabel(QStringLiteral("<span style='color:%1'>%2</span><br><span style='color:%3;font-size:18px;font-weight:600'>%4</span>")
                                    .arg(Theme::textMuted().name(), name, Theme::value().name(), value));
    result->setTextFormat(Qt::RichText);
    result->setMinimumHeight(45);
    return result;
}

QPushButton *actionButton(const QString &text)
{
    QPushButton *button = new QPushButton(text);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(38);
    return button;
}

void configureTable(QTableWidget *table)
{
    table->setAlternatingRowColors(true);
    table->setShowGrid(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setHighlightSections(false);
}

const QStringList &telemetryFieldIds()
{
    static const QStringList fields = {
        QStringLiteral("xpos"), QStringLiteral("ypos"), QStringLiteral("depth"),
        QStringLiteral("yaw"), QStringLiteral("pitch"), QStringLiteral("roll"),
        QStringLiteral("forceX"), QStringLiteral("forceY"), QStringLiteral("forceZ"),
        QStringLiteral("forceYaw")
    };
    return fields;
}

const QStringList &telemetryFieldLabels()
{
    static const QStringList labels = {
        QStringLiteral("横向位置"), QStringLiteral("纵向位置"), QStringLiteral("深度"),
        QStringLiteral("艏向角"), QStringLiteral("纵倾角"), QStringLiteral("横倾角"),
        QStringLiteral("X 控制力"), QStringLiteral("Y 控制力"), QStringLiteral("Z 控制力"),
        QStringLiteral("艏向控制力")
    };
    return labels;
}

QString csvCell(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(qBound(0, alpha, 255));
    return color;
}

class SonarVolumePlaceholder final : public QWidget
{
public:
    explicit SonarVolumePlaceholder(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(600, 380);
        connect(EffectController::instance(), &EffectController::frameAdvanced, this,
                [this](qreal phase, qreal) {
            if (!isVisible()) return;
            m_phase = phase;
            update();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = rect().adjusted(2, 2, -2, -2);
        QLinearGradient fill(frame.topLeft(), frame.bottomRight());
        fill.setColorAt(0.0, Theme::panelAlt());
        fill.setColorAt(1.0, Theme::backgroundDeep());
        painter.setPen(QPen(withAlpha(Theme::border(), 145), 1.2));
        painter.setBrush(fill);
        painter.drawRoundedRect(frame, 22, 22);

        const QPainterPath clip = [] (const QRectF &area) {
            QPainterPath path;
            path.addRoundedRect(area, 22, 22);
            return path;
        }(frame);
        painter.setClipPath(clip);

        const QRectF volumeArea = frame.adjusted(frame.width() * 0.10, 50,
                                                  -frame.width() * 0.10, -34);
        const qreal width = volumeArea.width();
        const qreal height = volumeArea.height();
        const QPointF frontTop(volumeArea.left() + width * 0.28, volumeArea.top() + height * 0.21);
        const QPointF frontBottom(volumeArea.left() + width * 0.28, volumeArea.bottom() - height * 0.12);
        const QPointF rearTop(volumeArea.right() - width * 0.18, volumeArea.top() + height * 0.34);
        const QPointF rearBottom(volumeArea.right() - width * 0.18, volumeArea.bottom());

        QPolygonF front;
        front << frontTop << QPointF(frontTop.x(), frontBottom.y()) << frontBottom
              << QPointF(frontBottom.x() + width * 0.31, frontBottom.y() - height * 0.12)
              << QPointF(frontTop.x() + width * 0.31, frontTop.y() - height * 0.12);
        QPolygonF rear;
        rear << rearTop << QPointF(rearTop.x(), rearBottom.y()) << rearBottom
             << QPointF(rearBottom.x() + width * 0.22, rearBottom.y() - height * 0.09)
             << QPointF(rearTop.x() + width * 0.22, rearTop.y() - height * 0.09);

        QLinearGradient glowGradient(frame.topLeft(), frame.bottomRight());
        glowGradient.setColorAt(0.0, withAlpha(Theme::accent(), 14));
        glowGradient.setColorAt(0.55, withAlpha(Theme::iceCyan(), 28));
        glowGradient.setColorAt(1.0, withAlpha(Theme::plasmaViolet(), 11));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glowGradient);
        QPolygonF volumeFace;
        volumeFace << frontTop << rearTop << rearBottom << frontBottom;
        painter.drawPolygon(volumeFace);

        const QColor gridLine = withAlpha(Theme::accent(), 42);
        painter.setPen(QPen(gridLine, 1));
        constexpr int slices = 7;
        for (int i = 0; i <= slices; ++i) {
            const qreal t = static_cast<qreal>(i) / slices;
            const QPointF left = frontTop * (1.0 - t) + frontBottom * t;
            const QPointF right = rearTop * (1.0 - t) + rearBottom * t;
            painter.drawLine(left, right);
            const QPointF leftDepth = frontTop * (1.0 - t) + rearTop * t;
            const QPointF rightDepth = frontBottom * (1.0 - t) + rearBottom * t;
            painter.drawLine(leftDepth, rightDepth);
        }

        painter.setPen(QPen(withAlpha(Theme::iceCyan(), 145), 1.5));
        painter.drawPolyline(front);
        painter.drawPolyline(rear);
        painter.drawLine(frontTop, rearTop);
        painter.drawLine(frontBottom, rearBottom);

        const qreal scanX = volumeArea.left() + std::fmod(m_phase * 20.0, qMax<qreal>(1.0, width));
        QLinearGradient scan(scanX - 24, 0, scanX + 24, 0);
        scan.setColorAt(0.0, withAlpha(Theme::iceCyan(), 0));
        scan.setColorAt(0.5, withAlpha(Theme::iceCyan(), 55));
        scan.setColorAt(1.0, withAlpha(Theme::plasmaViolet(), 0));
        painter.fillRect(QRectF(scanX - 24, volumeArea.top(), 48, volumeArea.height()), scan);

        const QPointF origin(frontBottom.x() - 18, frontBottom.y() + 5);
        painter.setPen(QPen(Theme::iceCyan(), 2));
        painter.drawLine(origin, origin + QPointF(50, 0));
        painter.setPen(QPen(withAlpha(Theme::glow(), 180), 2));
        painter.drawLine(origin, origin + QPointF(0, -44));
        painter.setPen(QPen(withAlpha(Theme::plasmaLight(), 170), 2));
        painter.drawLine(origin, origin + QPointF(30, -24));
        painter.setFont(Theme::font(8, true));
        painter.setPen(Theme::textMuted());
        painter.drawText(QRectF(origin + QPointF(54, -10), QSizeF(30, 18)), QStringLiteral("X"));
        painter.drawText(QRectF(origin + QPointF(-10, -63), QSizeF(30, 18)), QStringLiteral("Z"));
        painter.drawText(QRectF(origin + QPointF(32, -39), QSizeF(30, 18)), QStringLiteral("Y"));

        const QRectF message(frame.center().x() - qMin<qreal>(275, frame.width() * 0.35),
                             frame.center().y() - 45,
                             qMin<qreal>(550, frame.width() * 0.70), 90);
        painter.setPen(QPen(withAlpha(Theme::border(), 140), 1));
        painter.setBrush(withAlpha(Theme::backgroundDeep(), 218));
        painter.drawRoundedRect(message, 9, 9);
        painter.setFont(Theme::font(15, true));
        painter.setPen(Theme::value());
        painter.drawText(message.adjusted(12, 10, -12, -36), Qt::AlignCenter,
                         QStringLiteral("声纳三维点云接口待接入"));
        painter.setFont(Theme::font(9));
        painter.setPen(Theme::textMuted());
        painter.drawText(message.adjusted(12, 44, -12, -7), Qt::AlignCenter,
                         QStringLiteral("当前无有效声纳数据 · 等待设备与算法服务接入"));

        painter.setClipping(false);
        painter.setFont(Theme::font(8));
        painter.setPen(Theme::textMuted());
        painter.drawText(frame.adjusted(18, frame.height() - 28, -18, -7),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("SONAR VOLUME · STANDBY   |   POINT CLOUD · NO DATA   |   RENDERER · RESERVED"));
        painter.setPen(QPen(Theme::iceCyan(), 2));
        const qreal mark = 22;
        painter.drawLine(frame.topLeft() + QPointF(12, 12), frame.topLeft() + QPointF(12 + mark, 12));
        painter.drawLine(frame.topLeft() + QPointF(12, 12), frame.topLeft() + QPointF(12, 12 + mark));
        painter.drawLine(frame.bottomRight() - QPointF(12 + mark, 12), frame.bottomRight() - QPointF(12, 12));
        painter.drawLine(frame.bottomRight() - QPointF(12, 12 + mark), frame.bottomRight() - QPointF(12, 12));
    }

private:
    qreal m_phase = 0.0;
};

}

HealthPage::HealthPage(DemoDataModel *model, QWidget *parent) : DashboardPage(parent)
{
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(16, 4, 16, 16);
    root->setSpacing(12);

    NeonPanel *thrusterPanel = new NeonPanel(QStringLiteral("推进器状态 / THRUSTERS"));
    QGridLayout *thrusterLayout = new QGridLayout(thrusterPanel);
    thrusterLayout->setContentsMargins(12, 48, 12, 12);
    thrusterLayout->setSpacing(4);
    for (int i = 0; i < 8; ++i) {
        GaugeWidget *gauge = new GaugeWidget(QStringLiteral("推进器 %1").arg(i + 1));
        gauge->setValue(-2500 + i * 120);
        m_thrusters << gauge;
        thrusterLayout->addWidget(gauge, i / 2, i % 2);
    }
    m_chart = new LineChart(QStringLiteral("状态曲线简图"));
    m_chart->setCompact(true);
    thrusterLayout->addWidget(m_chart, 4, 0, 1, 2);
    for (int row = 0; row <= 4; ++row)
        thrusterLayout->setRowStretch(row, 1);

    NeonPanel *center = new NeonPanel(QStringLiteral("姿态与航向 / ATTITUDE & HEADING"));
    QVBoxLayout *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(16, 50, 16, 14);
    centerLayout->setSpacing(8);
    QLabel *steer = label(QStringLiteral("转艏角设定   −60     −30      0      30      60"), 10);
    steer->setAlignment(Qt::AlignCenter);
    steer->setStyleSheet(QStringLiteral("color:%1;background:%2;border:1px solid %3;border-radius:5px;padding:8px;")
                             .arg(Theme::text().name(), Theme::background().name(), Theme::accent().darker(150).name()));
    centerLayout->addWidget(steer);
    m_compass = new CompassWidget;
    centerLayout->addWidget(m_compass, 1);
    QHBoxLayout *attitude = new QHBoxLayout;
    m_pitch = new GaugeWidget(QStringLiteral("纵倾角 / PITCH"));
    m_pitch->setResetParticlesOnResize(false);
    m_pitch->setRange(-30, 30); m_pitch->setUnit(QStringLiteral("deg"));
    m_roll = new GaugeWidget(QStringLiteral("横倾角 / ROLL"));
    m_roll->setResetParticlesOnResize(false);
    m_roll->setRange(-30, 30); m_roll->setUnit(QStringLiteral("deg"));
    attitude->addWidget(m_pitch);
    QLabel *force = createMetricLabel(QStringLiteral("转艏控制力"), QStringLiteral("30 Nm"));
    force->setAlignment(Qt::AlignCenter);
    attitude->addWidget(force, 1);
    attitude->addWidget(m_roll);
    centerLayout->addLayout(attitude);

    QVBoxLayout *rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(10);
    NeonPanel *environment = new NeonPanel(QStringLiteral("环境信息 / ENVIRONMENT"));
    QGridLayout *env = new QGridLayout(environment);
    env->setContentsMargins(16, 49, 16, 12);
    env->addWidget(createMetricLabel(QStringLiteral("绝对流速"), QStringLiteral("0.5 m/s")), 0, 0);
    env->addWidget(createMetricLabel(QStringLiteral("相对流速"), QStringLiteral("0.8 m/s")), 0, 1);
    env->addWidget(createMetricLabel(QStringLiteral("水温"), QStringLiteral("18.6 ℃")), 1, 0);
    m_depthValue = createMetricLabel(QStringLiteral("当前深度"), QStringLiteral("12.4 m"));
    env->addWidget(m_depthValue, 1, 1);
    env->addWidget(createMetricLabel(QStringLiteral("湿度"), QStringLiteral("45 %")), 2, 0);
    env->addWidget(createMetricLabel(QStringLiteral("舱压"), QStringLiteral("102.4 kPa")), 2, 1);
    rightColumn->addWidget(environment, 2);

    NeonPanel *batteryPanel = new NeonPanel(QStringLiteral("电池信息 / BATTERY"));
    QVBoxLayout *batteryLayout = new QVBoxLayout(batteryPanel);
    batteryLayout->setContentsMargins(12, 45, 12, 8);
    m_battery = new CircularProgress;
    batteryLayout->addWidget(m_battery);
    rightColumn->addWidget(batteryPanel, 2);

    NeonPanel *systemStatusPanel = new NeonPanel(QStringLiteral("系统状态 / SYSTEM STATUS"));
    QGridLayout *signalLayout = new QGridLayout(systemStatusPanel);
    signalLayout->setContentsMargins(14, 47, 14, 10);
    signalLayout->addWidget(new StatusLamp(QStringLiteral("定位"), StatusLamp::Online), 0, 0);
    signalLayout->addWidget(new StatusLamp(QStringLiteral("摄像头"), StatusLamp::Online), 0, 1);
    signalLayout->addWidget(new StatusLamp(QStringLiteral("通讯状态"), StatusLamp::Online), 1, 0);
    signalLayout->addWidget(new StatusLamp(QStringLiteral("惯导"), StatusLamp::Warning), 1, 1);
    signalLayout->addWidget(new StatusLamp(QStringLiteral("推进器"), StatusLamp::Online), 2, 0);
    signalLayout->addWidget(new StatusLamp(QStringLiteral("深度计"), StatusLamp::Online), 2, 1);
    rightColumn->addWidget(systemStatusPanel, 2);

    root->addWidget(thrusterPanel, 28);
    root->addWidget(center, 47);
    root->addLayout(rightColumn, 25);

    connect(model, &DemoDataModel::sampleReady, this, [this](double phase, int) {
        for (int i = 0; i < m_thrusters.size(); ++i)
            m_thrusters.at(i)->setValue(-2250 + qSin(phase * 0.55 + i * 0.72) * 480);
        m_pitch->setValue(qSin(phase * 0.48) * 7.2);
        m_roll->setValue(qCos(phase * 0.42) * 9.5);
        m_compass->setHeading(15.0 + qSin(phase * 0.2) * 22.0);
        m_battery->setValue(73 + qRound(qSin(phase * 0.06) * 2));
        m_chart->append(qSin(phase) * 0.8, qCos(phase * 0.72) * 0.75);
        m_depthValue->setText(QStringLiteral("<span style='color:%1'>当前深度</span><br><span style='color:%2;font-size:18px;font-weight:600'>%3 m</span>")
                                  .arg(Theme::textMuted().name(), Theme::value().name())
                                  .arg(12.4 + qSin(phase * 0.22) * 0.7, 0, 'f', 1));
    });
}

RoutePage::RoutePage(DemoDataModel *model, QWidget *parent) : DashboardPage(parent)
{
    Q_UNUSED(model)
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(16, 4, 16, 16);
    root->setSpacing(12);

    m_mapPlanning = new MapPlanningWidget;
    m_plannerController = new PlannerController(this);
    m_udpController = new UdpRobotController(this);
    m_udpController->setProtocolCodec(std::make_shared<DeviceTelemetryProtocol>());
    m_waterwayRecognizer = new WaterwayRecognizer(this);

    QScrollArea *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #07162E; border: 0; }"));
    scroll->viewport()->setAutoFillBackground(false);
    QWidget *controlContainer = new QWidget;
    controlContainer->setObjectName(QStringLiteral("routeControlContainer"));
    controlContainer->setStyleSheet(QStringLiteral(
        "#routeControlContainer { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #07162E, stop:0.48 #0A2344, stop:1 #102F58); "
        "border: 1px solid rgba(0,210,255,0.28); border-radius: 12px; }"));
    QVBoxLayout *right = new QVBoxLayout(controlContainer);
    right->setContentsMargins(0, 0, 4, 0);
    right->setSpacing(10);

    NeonPanel *taskPanel = new NeonPanel(QStringLiteral("任务与河道识别 / MISSION"));
    QVBoxLayout *taskLayout = new QVBoxLayout(taskPanel);
    taskLayout->setContentsMargins(12, 47, 12, 14);
    taskLayout->setSpacing(6);
    m_devicePositionLabel = label(QStringLiteral("设备位置：等待 UDP 遥测"), 9, true);
    m_targetPositionLabel = label(QStringLiteral("目标位置：尚未设置（单击地图即可设置）"), 9, true);
    m_recognitionStatusLabel = label(QStringLiteral("河道识别：尚未识别"), 9);
    m_missionStatusLabel = label(QStringLiteral("任务状态：未就绪"), 9, true);
    m_algorithmStatusLabel = label(QStringLiteral("算法：原始 Hybrid A* · 离岸 5 m · 障碍点间距 4 m"), 8);
    m_planningDiagnosticsLabel = label(QStringLiteral("规划诊断：等待规划"), 8);
    const QList<QLabel *> taskLabels = {m_devicePositionLabel, m_targetPositionLabel,
                                        m_recognitionStatusLabel, m_missionStatusLabel,
                                        m_algorithmStatusLabel, m_planningDiagnosticsLabel};
    for (QLabel *item : taskLabels) {
        item->setWordWrap(true);
        item->setMinimumHeight(31);
        item->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        item->setStyleSheet(QStringLiteral("color:%1;background:%2;border:1px solid %3;border-radius:5px;padding:6px;")
            .arg(Theme::text().name(), Theme::background().name(), Theme::accent().darker(165).name()));
        taskLayout->addWidget(item);
    }
    QGridLayout *missionActions = new QGridLayout;
    // The full map stays visible for presentation; this switch toggles the
    // purple film generated from the temporary road-free capture of the map.
    m_waterwayOnlyCheck = new QCheckBox(QStringLiteral("显示可用水域"));
    m_waterwayOnlyCheck->setChecked(false);
    m_manualDeviceCheck = new QCheckBox(QStringLiteral("使用手动测试设备位置"));
    m_manualDeviceCheck->setChecked(false);
    m_markTestDeviceButton = actionButton(QStringLiteral("在地图标记测试设备"));
    m_markTestDeviceButton->setObjectName(QStringLiteral("markTestDeviceButton"));
    m_markTestDeviceButton->setCheckable(true);
    m_testHeadingSpin = new QDoubleSpinBox;
    m_testHeadingSpin->setRange(0.0, 359.9);
    m_testHeadingSpin->setDecimals(1);
    m_testHeadingSpin->setSingleStep(5.0);
    m_testHeadingSpin->setValue(0.0);
    m_testHeadingSpin->setPrefix(QStringLiteral("测试艏向 "));
    m_testHeadingSpin->setSuffix(QStringLiteral("°"));
    m_testHeadingSpin->setKeyboardTracking(false);
    m_testHeadingSpin->setToolTip(QStringLiteral("北为 0°，顺时针增加；仅用于手动测试起点"));
    m_acceptSnapButton = actionButton(QStringLiteral("采用黄色建议点"));
    m_acceptSnapButton->setObjectName(QStringLiteral("acceptSnapButton"));
    m_planButton = actionButton(QStringLiteral("规划路线"));
    m_planButton->setObjectName(QStringLiteral("planRouteButton"));
    missionActions->setHorizontalSpacing(7);
    missionActions->setVerticalSpacing(6);
    missionActions->addWidget(m_manualDeviceCheck, 0, 0, 1, 2);
    missionActions->addWidget(m_markTestDeviceButton, 1, 0);
    missionActions->addWidget(m_testHeadingSpin, 1, 1);
    missionActions->addWidget(m_acceptSnapButton, 2, 0, 1, 2);
    missionActions->addWidget(m_planButton, 3, 0, 1, 2);
    missionActions->setColumnStretch(0, 1);
    missionActions->setColumnStretch(1, 1);
    taskLayout->addLayout(missionActions);
    taskLayout->addWidget(m_waterwayOnlyCheck);
    QFrame *missionDivider = new QFrame;
    missionDivider->setFrameShape(QFrame::HLine);
    missionDivider->setStyleSheet(QStringLiteral("color:%1; background:%1; max-height:1px;")
                                  .arg(Theme::accent().darker(135).name()));
    taskLayout->addWidget(missionDivider);
    QLabel *communicationHint = label(QStringLiteral("设备通信 · 自动连接"), 8, true);
    communicationHint->setStyleSheet(QStringLiteral("color:%1; padding:2px 4px;")
                                     .arg(Theme::textMuted().name()));
    taskLayout->addWidget(communicationHint);
    m_uploadButton = actionButton(QStringLiteral("上传路线"));
    m_uploadButton->setObjectName(QStringLiteral("uploadRouteButton"));
    m_uploadButton->setMinimumHeight(42);
    taskLayout->addWidget(m_uploadButton);
    m_returnMissionButton = actionButton(QStringLiteral("原路返航"));
    m_stopMissionButton = actionButton(QStringLiteral("停止任务"));
    taskLayout->addWidget(m_returnMissionButton);
    taskLayout->addWidget(m_stopMissionButton);
    right->addWidget(taskPanel);
    right->addStretch(1);
    scroll->setWidget(controlContainer);

    root->addWidget(m_mapPlanning, 68);
    root->addWidget(scroll, 32);

    connect(m_mapPlanning, &MapPlanningWidget::amapLocationReceived,
            this, &RoutePage::amapLocationReceived);
    connect(m_mapPlanning, &MapPlanningWidget::amapLocationFailed,
            this, &RoutePage::amapLocationFailed);
    connect(m_mapPlanning, &MapPlanningWidget::missionTargetSelected, this,
            [this](const RoutePoint &target) {
        requestAction(ActionId::AddWaypoint);
        m_hasTarget = true;
        m_targetPosition = target;
        m_targetPositionLabel->setText(QStringLiteral("目标位置：%1 · 待确认（单击地图可替换）")
            .arg(coordinateText(target.position)));
        m_routeUploaded = false;
        m_udpController->cancelUpload();
        m_missionId.clear();
        m_plannedRoute = RoutePath();
        m_returnRoute = RoutePath();
        m_waterwayConfirmed = false;
        m_missionStarted = false;
        m_returning = false;
        m_mapPlanning->setPlannedPath(m_plannedRoute);
        m_hasSnapCandidate = false;
        m_mapPlanning->setSnapCandidate(RoutePoint(), false);
        m_hasPlanningStart = false;
        m_mapPlanning->setPlanningStart(RoutePoint(), false);
        clearMissionOriginSnapshot();
        const bool currentSafetyGrid = m_waterwayGrid.isValid()
            && m_currentGridIsPlanning
            && m_currentMapRevision == m_mapPlanning->currentMapRevision();
        if (currentSafetyGrid) {
            // A target click does not change the viewport. Reuse the safety
            // topology already recognised for these exact map pixels.
            confirmWaterway();
        } else {
            setMissionState(MissionState::NotReady,
                            QStringLiteral("正在识别目标所在位置是否安全"));
            schedulePreviewWaterwayRecognition();
        }
    });
    connect(m_mapPlanning, &MapPlanningWidget::testDevicePositionSelected, this,
            [this](const RoutePoint &position) {
        if (!usingManualTestDevice()) return;
        m_manualTestDevicePosition = position;
        m_manualTestDevicePosition.id = QStringLiteral("manual-test-device");
        m_manualTestDevicePosition.coordinateSystem = CoordinateSystem::Gcj02;
        m_manualTestDevicePosition.headingRadians =
            qDegreesToRadians(m_testHeadingSpin->value());
        m_manualTestDevicePosition.headingValid = true;
        m_manualTestDeviceTimestamp = QDateTime::currentDateTimeUtc();
        m_hasManualTestDevice = true;
        m_markTestDeviceButton->setChecked(false);
        invalidateRouteForOriginChange(
            QStringLiteral("测试设备位置已更新，请设置目标后点击规划路线"));
        // The viewport did not change, so reuse its current waterway grid and
        // immediately calculate the safe display/planning position. If there
        // is no current grid yet, request recognition now; map movement must
        // never be required to obtain the recommendation.
        if (m_currentGridIsPlanning && m_waterwayGrid.isValid())
            restoreManualTestDevicePosition();
        else
            schedulePreviewWaterwayRecognition();
    });
    m_autoRecognitionTimer = new QTimer(this);
    m_autoRecognitionTimer->setSingleShot(true);
    m_autoRecognitionTimer->setInterval(280);
    connect(m_autoRecognitionTimer, &QTimer::timeout,
            this, &RoutePage::requestPreviewWaterwayCapture);
    connect(m_mapPlanning, &MapPlanningWidget::waterwayCaptureReady,
            this, &RoutePage::processWaterwayCapture);
    connect(m_mapPlanning, &MapPlanningWidget::mapReadyChanged, this, [this](bool ready) {
        if (ready) schedulePreviewWaterwayRecognition();
    });
    connect(m_mapPlanning, &MapPlanningWidget::mapRevisionChanged, this, [this](quint64) {
        m_currentGridIsPlanning = false;
        if (m_planningCaptureRequested || m_planningRecognitionActive
            || m_missionState == MissionState::Planning) {
            m_planningCaptureRequested = false;
            clearMissionOriginSnapshot();
            setMissionState(MissionState::NotReady,
                            QStringLiteral("地图视图已变化，请在当前视口重新规划"));
        }
        schedulePreviewWaterwayRecognition();
    });
    connect(m_waterwayRecognizer, &WaterwayRecognizer::recognitionFinished, this,
            [this](const WaterwayRecognitionResult &result) {
        const bool planning = m_planningRecognitionActive;
        m_planningRecognitionActive = false;
        m_recognitionBusy = false;
        if (!m_mapPlanning || result.waterway.revision != m_mapPlanning->currentMapRevision()) {
            if (planning)
                setMissionState(MissionState::NotReady,
                                QStringLiteral("地图视图已变化，请在当前视口重新规划"));
            schedulePreviewWaterwayRecognition();
            return;
        }
        if (!planning) {
            if (result.success) {
                m_waterwayGrid = result.waterway;
                m_currentMapRevision = result.waterway.revision;
                m_recognitionConfidence = result.confidence;
                m_currentGridIsPlanning = true;
                m_mapPlanning->setWaterwayOverlay(result.overlayImage);
                if (usingManualTestDevice()) restoreManualTestDevicePosition();
                else restoreLatestUdpDevicePosition();
                m_recognitionStatusLabel->setText(
                    QStringLiteral("河道识别：当前视口颜色识别 · 薄膜已更新 · 置信度 %1%")
                        .arg(qRound(result.confidence * 100.0)));
                if (m_hasTarget) confirmWaterway();
            } else {
                m_waterwayGrid = WaterwayGrid();
                m_currentGridIsPlanning = false;
                m_recognitionStatusLabel->setText(
                    QStringLiteral("河道识别：当前视口未识别可靠水域"));
            }
            if (m_planningCaptureRequested) requestPlanningWaterwayCapture();
            return;
        }
        m_planningCaptureRequested = false;
        if (!result.success) {
            m_currentGridIsPlanning = false;
            setMissionState(MissionState::NotReady,
                            QStringLiteral("当前视口水域识别失败：%1")
                                .arg(result.errorMessage));
            return;
        }
        m_waterwayGrid = result.waterway;
        m_currentMapRevision = result.waterway.revision;
        m_recognitionConfidence = result.confidence;
        m_currentGridIsPlanning = true;
        // From this point onward the visible film must be the exact mask used
        // to extract Hybrid A* obstacles. MapPlanningWidget scales it smoothly
        // for display without changing the binary planning topology.
        m_mapPlanning->setWaterwayOverlay(result.overlayImage);
        if (usingManualTestDevice()) restoreManualTestDevicePosition();
        else restoreLatestUdpDevicePosition();
        confirmWaterway();
        if (m_missionState == MissionState::ReadyToPlan) requestPlanning();
    });
    connect(m_plannerController, &PlannerController::planningFinished, this,
            [this](const RoutePlanningResult &result) {
        if (result.missionId != m_missionId) return;
        if (!m_mapPlanning || result.revision != m_currentMapRevision
            || result.revision != m_mapPlanning->currentMapRevision()) {
            clearMissionOriginSnapshot();
            setMissionState(MissionState::NotReady,
                            QStringLiteral("地图视图已变化，已丢弃过期规划结果"));
            return;
        }
        if (!result.success) {
            if (m_planningDiagnosticsLabel) {
                m_planningDiagnosticsLabel->setText(result.diagnosticMessage.isEmpty()
                    ? QStringLiteral("规划诊断：原始算法未返回细分诊断")
                    : QStringLiteral("规划诊断：%1").arg(result.diagnosticMessage));
            }
            setMissionState(MissionState::Failed, QStringLiteral("规划失败：%1").arg(result.errorMessage));
            return;
        }
        if (m_planningDiagnosticsLabel) {
            m_planningDiagnosticsLabel->setText(result.diagnosticMessage.isEmpty()
                ? QStringLiteral("规划诊断：原始 Hybrid A* 已返回有效路线")
                : QStringLiteral("规划诊断：%1").arg(result.diagnosticMessage));
        }
        const RoutePath route = routeFromPlanningResult(result);
        if (!route.valid) {
            setMissionState(MissionState::Failed, QStringLiteral("规划结果复核失败：路线越出确认水域"));
            return;
        }
        m_plannedRoute = route;
        m_returnRoute = route;
        std::reverse(m_returnRoute.points.begin(), m_returnRoute.points.end());
        // A reverse mission has the same geographic samples in the opposite
        // order, but its pose metadata must describe the reverse direction.
        // Recompute each heading from the next point instead of merely
        // reversing the outbound heading array (which would leave every
        // vehicle pose pointing along the old route).
        for (int i = 0; i < m_returnRoute.points.size(); ++i) {
            RoutePoint &point = m_returnRoute.points[i];
            if (i + 1 < m_returnRoute.points.size()) {
                int nextIndex = i + 1;
                while (nextIndex < m_returnRoute.points.size()
                       && geographicDistanceMeters(point.position,
                                                   m_returnRoute.points.at(nextIndex).position)
                              <= 1e-6) {
                    ++nextIndex;
                }
                point.headingRadians = nextIndex < m_returnRoute.points.size()
                    ? geographicHeadingRadians(point.position,
                                                m_returnRoute.points.at(nextIndex).position)
                    : m_missionOrigin.safePose.headingRadians;
                point.headingValid = true;
            } else if (m_missionOrigin.valid) {
                point.position = m_missionOrigin.safePose.position;
                point.headingRadians = m_missionOrigin.safePose.headingRadians;
                point.headingValid = m_missionOrigin.safePose.headingValid;
            }
        }
        m_routeUploaded = false;
        m_preparedMission = PreparedMission();
        m_preparedMission.missionId = m_missionId;
        m_preparedMission.actualStartGcj02 = m_missionOrigin.actualGcj02;
        m_preparedMission.safeStart = m_missionOrigin.safePose;
        m_preparedMission.safeTarget = m_frozenTargetPosition;
        m_preparedMission.outboundRoute = m_plannedRoute;
        m_preparedMission.returnRoute = m_returnRoute;
        m_preparedMission.boundaryObstaclePointsGcj02 = result.obstaclePointsGcj02;
        m_preparedMission.mapRevision = m_currentMapRevision;
        const auto routeReadyForHandoff = [](const RoutePath &path) {
            if (!path.valid || path.points.size() < 2) return false;
            for (const RoutePoint &point : path.points) {
                if (!GeoCoordinateUtils::isValidLongitudeLatitude(point.position)
                    || !point.headingValid || !qIsFinite(point.headingRadians)) {
                    return false;
                }
            }
            return true;
        };
        m_preparedMission.valid = m_missionOrigin.valid
            && GeoCoordinateUtils::isValidLongitudeLatitude(
                m_preparedMission.actualStartGcj02)
            && GeoCoordinateUtils::isValidLongitudeLatitude(
                m_preparedMission.safeStart.position)
            && m_preparedMission.safeStart.headingValid
            && qIsFinite(m_preparedMission.safeStart.headingRadians)
            && GeoCoordinateUtils::isValidLongitudeLatitude(
                m_preparedMission.safeTarget.position)
            && routeReadyForHandoff(m_preparedMission.outboundRoute)
            && routeReadyForHandoff(m_preparedMission.returnRoute)
            && !m_preparedMission.boundaryObstaclePointsGcj02.isEmpty();
        if (!m_preparedMission.valid) {
            m_preparedMission = PreparedMission();
            setMissionState(MissionState::Failed,
                            QStringLiteral("路线已生成，但待下发任务数据校验失败"));
            return;
        }
        m_mapPlanning->setPlannedPath(route);
        // Count the exact outbound route retained in PreparedMission, rather
        // than the planner's intermediate grid cells.  This is the sequence
        // that will be handed to the device protocol once it is connected.
        const int outboundPointCount = m_preparedMission.outboundRoute.points.size();
        m_algorithmStatusLabel->setText(
            QStringLiteral("算法：%1 · 离岸 5 m · 障碍点间距 4 m · 待发送路径点 %2 个")
                .arg(result.algorithmId).arg(outboundPointCount));
        emit missionPrepared(m_preparedMission);
        setMissionState(MissionState::Planned,
                        QStringLiteral("任务数据已准备，等待 UDP 协议接入"));
    });

    connect(m_udpController, &UdpRobotController::linkStateChanged, this,
            [this](RobotLinkState state, const QString &message) {
        Q_UNUSED(state);
        Q_UNUSED(message);
        updateControls();
    });
    connect(m_udpController, &UdpRobotController::telemetryReceived, this,
            [this](const RobotTelemetry &telemetry) {
        m_lastTelemetry = telemetry;
        if (!usingManualTestDevice()) restoreLatestUdpDevicePosition();
        if (telemetry.state == QStringLiteral("running") && !m_missionStarted) {
            m_missionStarted = true;
        }
        if (telemetry.state == QStringLiteral("returning")) {
            m_returning = true;
            m_missionStarted = true;
            if (m_missionState != MissionState::Returning)
                setMissionState(MissionState::Returning, QStringLiteral("设备正在执行预置返航路线"));
        } else if (telemetry.state == QStringLiteral("running")
                   && m_missionState == MissionState::Executing) {
            setMissionState(MissionState::Executing, QStringLiteral("设备正在执行任务路线"));
        }
        if (telemetry.state == QStringLiteral("completed")) {
            m_missionStarted = false;
            m_returning = false;
            setMissionState(MissionState::Completed, QStringLiteral("设备已完成任务"));
        }
        if (telemetry.state == QStringLiteral("failed")) {
            m_missionStarted = false;
            setMissionState(MissionState::Failed, QStringLiteral("设备报告任务失败"));
        }
        if (!usingManualTestDevice() && m_waterwayConfirmed && m_hasTarget
                   && m_waterwayGrid.isValid()
                   && m_missionState == MissionState::WaterwayPendingConfirmation) {
            // The target can be confirmed before the first device fix arrives.
            // Re-run only the start-cell check when telemetry becomes fresh;
            // do not require the user to press “确认水域” a second time.
            confirmWaterway();
        } else
            updateControls();
    });
    connect(m_udpController, &UdpRobotController::routeUploaded, this,
            [this](const QString &) {
        m_routeUploaded = true;
        setMissionState(m_returning ? MissionState::Returning : MissionState::Executing,
                        m_returning
                            ? QStringLiteral("返航路线、真实起点和目标已上传，正在自动启动返航")
                            : QStringLiteral("路线、真实起点和目标已上传，正在自动启动设备"));
    });
    connect(m_udpController, &UdpRobotController::commandAcknowledged, this,
            [this](const QString &command, const QString &) {
        if (command == QStringLiteral("启动"))
            setMissionState(MissionState::Executing, QStringLiteral("设备已确认启动，等待运行遥测"));
        else if (command == QStringLiteral("返航"))
            setMissionState(MissionState::Returning, QStringLiteral("设备已确认返航，等待返航遥测"));
        else if (command == QStringLiteral("停止"))
            setMissionState(MissionState::Uploaded, QStringLiteral("设备已确认停止"));
    });
    connect(m_udpController, &UdpRobotController::uploadFailed, this,
            [this](const QString &message) {
        m_routeUploaded = false;
        setMissionState(MissionState::Failed, message);
    });
    connect(m_udpController, &UdpRobotController::commandFailed, this,
            [this](const QString &, const QString &, const QString &message) {
        setMissionState(MissionState::Failed, message);
    });

    m_missionTimer = new QTimer(this);
    m_missionTimer->setInterval(1000);
    connect(m_missionTimer, &QTimer::timeout, this, [this] {
        if (!usingManualTestDevice() && m_lastTelemetry.timestamp.isValid()) {
            const qint64 age = m_lastTelemetry.timestamp.msecsTo(QDateTime::currentDateTimeUtc());
            if (age > 5000)
                m_devicePositionLabel->setText(QStringLiteral("设备位置：遥测已过期 %1 秒，任务操作已锁定")
                    .arg(age / 1000));
        }
        updateControls();
    });
    m_missionTimer->start();

    connect(m_manualDeviceCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        m_markTestDeviceButton->setChecked(false);
        m_mapPlanning->setTestDeviceSelectionEnabled(false);
        if (enabled) {
            if (!m_hasManualTestDevice) {
                m_devicePosition = RoutePoint();
                m_devicePositionLabel->setText(
                    QStringLiteral("测试设备位置：请点击“在地图标记测试设备”后单击地图"));
            }
            invalidateRouteForOriginChange(
                m_hasManualTestDevice
                    ? QStringLiteral("已切换到手动测试设备位置，请点击规划路线")
                    : QStringLiteral("请先在地图标记测试设备位置"));
            if (m_hasManualTestDevice)
                restoreManualTestDevicePosition();
        } else {
            invalidateRouteForOriginChange(
                QStringLiteral("已恢复 UDP 设备位置，请等待有效遥测后规划"));
            restoreLatestUdpDevicePosition();
        }
    });
    connect(m_markTestDeviceButton, &QPushButton::toggled, this, [this](bool armed) {
        const bool selecting = armed && usingManualTestDevice();
        m_mapPlanning->setTestDeviceSelectionEnabled(selecting);
        m_markTestDeviceButton->setText(selecting
            ? QStringLiteral("请在地图单击设备位置")
            : QStringLiteral("在地图标记测试设备"));
        updateControls();
    });
    connect(m_testHeadingSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double headingDegrees) {
        if (!m_hasManualTestDevice) return;
        m_manualTestDevicePosition.headingRadians = qDegreesToRadians(headingDegrees);
        m_manualTestDevicePosition.headingValid = true;
        if (!usingManualTestDevice()) return;
        m_manualTestDeviceTimestamp = QDateTime::currentDateTimeUtc();
        invalidateRouteForOriginChange(
            QStringLiteral("测试艏向已更新，请重新规划路线"));
        restoreManualTestDevicePosition();
    });
    connect(m_waterwayOnlyCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!m_mapPlanning) return;
        m_mapPlanning->setWaterwayOnlyMode(enabled);
        if (enabled && !m_currentGridIsPlanning)
            schedulePreviewWaterwayRecognition();
    });
    m_mapPlanning->setWaterwayOnlyMode(m_waterwayOnlyCheck->isChecked());
    connect(m_acceptSnapButton, &QPushButton::clicked, this, &RoutePage::acceptSnapCandidate);
    connect(m_planButton, &QPushButton::clicked, this, &RoutePage::requestPlanning);
    connect(m_uploadButton, &QPushButton::clicked, this, [this] {
        requestAction(ActionId::SendMission);
        if (!m_preparedMission.valid) return;
        m_returning = false;
        setMissionState(MissionState::Uploading,
                        QStringLiteral("正在上传真实起点、目标和路线；完成后自动启动设备"));
        m_udpController->uploadAndStartMission(m_preparedMission);
    });
    connect(m_returnMissionButton, &QPushButton::clicked, this, [this] {
        if (!m_preparedMission.valid) return;
        m_returning = true;
        setMissionState(MissionState::Uploading,
                        QStringLiteral("正在上传真实起点、目标和原路返航路径"));
        m_udpController->uploadAndReturnMission(m_preparedMission);
    });
    connect(m_stopMissionButton, &QPushButton::clicked, this, [this] {
        m_udpController->stopMission(m_missionId);
        m_missionStarted = false;
        setMissionState(MissionState::Uploaded, QStringLiteral("已发送停止指令"));
    });
    configureAndStartUdp();
    setMissionState(MissionState::NotReady,
                    QStringLiteral("设备通信已自动启动，请单击地图设置目标；点击规划后采集当前水域"));
}

void RoutePage::warmUpMap()
{
    if (m_mapPlanning) m_mapPlanning->warmUp();
}

void RoutePage::setCurrentLocation(const LocationFix &fix, LocationSource source)
{
    if (m_mapPlanning) m_mapPlanning->setCurrentLocation(fix, source);
}

void RoutePage::setLocationStatus(const QString &status)
{
    if (m_mapPlanning) m_mapPlanning->setLocationStatus(status);
}

void RoutePage::setPageActive(bool active)
{
    m_pageActive = active;
    if (active) warmUpMap();
    if (m_mapPlanning) m_mapPlanning->setPageActive(active);
    if (active) schedulePreviewWaterwayRecognition();
}

void RoutePage::schedulePreviewWaterwayRecognition()
{
    if (m_pageActive && m_autoRecognitionTimer)
        m_autoRecognitionTimer->start();
}

void RoutePage::requestPreviewWaterwayCapture()
{
    if (!m_mapPlanning || !m_mapPlanning->isMapReady()
        || m_mapPlanning->isWaterwayCapturePending() || m_recognitionBusy) return;
    m_mapPlanning->requestWaterwayCapture();
}

void RoutePage::requestPlanningWaterwayCapture()
{
    if (!m_mapPlanning || !m_mapPlanning->isMapReady()) return;
    m_planningCaptureRequested = true;
    if (m_recognitionBusy || m_mapPlanning->isWaterwayCapturePending()) return;
    m_recognitionStatusLabel->setText(QStringLiteral("河道识别：正在采集当前视口边界用于规划…"));
    m_mapPlanning->requestWaterwayCapture();
}

void RoutePage::processWaterwayCapture(const QImage &image, const GeoReference &geoReference)
{
    if (m_recognitionBusy) return;
    if (!m_waterwayRecognizer || image.isNull()) {
        if (m_planningCaptureRequested) {
            m_planningCaptureRequested = false;
            setMissionState(MissionState::NotReady, QStringLiteral("当前视口截图失败，请等待地图稳定后重试"));
        }
        return;
    }
    WaterwayRecognitionRequest request;
    request.mapImage = image;
    request.geoReference = geoReference;
    // Preview recognition also prepares the safety topology. A later click
    // can therefore be checked without capturing a road-less map again.
    request.buildPlanningTopology = true;
    // The visual thin film is a pixel mask, not a planning grid.  Preserve the
    // captured map resolution for preview so shorelines do not become visible
    // square cells.  Route planning still receives a
    // bounded internal grid, where that representation is useful for A*.
    request.maximumGridSize = m_planningCaptureRequested
        ? QSize(640, 480) : QSize(1920, 1080);
    request.shoreSafetyMeters = suppliedHybridAstarParameters().shoreSafetyMeters;
    m_planningRecognitionActive = m_planningCaptureRequested;
    m_recognitionBusy = true;
    m_waterwayRecognizer->recognize(request);
}

void RoutePage::setMapHost(QWidget *host)
{
    if (m_mapPlanning) m_mapPlanning->setWebViewHost(host);
}

void RoutePage::syncMapHostGeometry()
{
    if (m_mapPlanning) m_mapPlanning->syncWebViewGeometry();
}

void RoutePage::setMissionState(MissionState state, const QString &message)
{
    m_missionState = state;
    QString name;
    switch (state) {
    case MissionState::NotReady: name = QStringLiteral("未就绪"); break;
    case MissionState::WaterwayPendingConfirmation: name = QStringLiteral("水域待确认"); break;
    case MissionState::ReadyToPlan: name = QStringLiteral("可规划"); break;
    case MissionState::Planning: name = QStringLiteral("规划中"); break;
    case MissionState::Planned: name = QStringLiteral("已规划"); break;
    case MissionState::Uploading: name = QStringLiteral("上传中"); break;
    case MissionState::Uploaded: name = QStringLiteral("已上传"); break;
    case MissionState::Executing: name = QStringLiteral("执行中"); break;
    case MissionState::Returning: name = QStringLiteral("返航中"); break;
    case MissionState::Completed: name = QStringLiteral("完成"); break;
    case MissionState::Failed: name = QStringLiteral("失败"); break;
    }
    m_missionStatusLabel->setText(QStringLiteral("任务状态：%1%2")
        .arg(name, message.isEmpty() ? QString() : QStringLiteral(" · ") + message));
    updateControls();
}

void RoutePage::setExternalRoutePlanner(const std::shared_ptr<IRoutePlanner> &planner)
{
    if (m_plannerController) m_plannerController->setExternalPlanner(planner);
    if (m_algorithmStatusLabel) {
        m_algorithmStatusLabel->setText(planner
            ? QStringLiteral("算法：原始 Hybrid A* · 离岸 5 m · 障碍点间距 4 m")
            : QStringLiteral("算法：原始 Hybrid A* 未接入，无法规划"));
    }
    updateControls();
}

void RoutePage::setRobotProtocolCodec(const std::shared_ptr<IRobotProtocolCodec> &codec)
{
    if (m_udpController) m_udpController->setProtocolCodec(codec);
    updateControls();
}

void RoutePage::updateControls()
{
    if (!m_mapPlanning) return;
    const bool originReady = hasUsableDeviceOrigin();
    // Planning and transport are independent capabilities. The supplied
    // Hybrid A* planning is independent of transport; upload/return stay
    // locked until the real device protocol codec is registered.
    const bool plannerAvailable = m_plannerController->hasExternalPlanner();
    const bool taskEditingAllowed = m_missionState != MissionState::Planning
        && m_missionState != MissionState::Uploading;
    // A normal click on an otherwise interactive map sets or replaces the
    // mission target. AMap suppresses its click event after a drag.
    const bool selectingTestDevice = m_markTestDeviceButton
        && m_markTestDeviceButton->isChecked() && usingManualTestDevice();
    m_mapPlanning->setTargetSelectionEnabled(taskEditingAllowed && !selectingTestDevice);
    m_mapPlanning->setTestDeviceSelectionEnabled(taskEditingAllowed && selectingTestDevice);
    // The thin-film display is independent of the planning grid. It remains
    // available while a new viewport is being recognized or after a zoom.
    m_waterwayOnlyCheck->setEnabled(taskEditingAllowed);
    m_manualDeviceCheck->setEnabled(taskEditingAllowed);
    m_markTestDeviceButton->setEnabled(taskEditingAllowed && usingManualTestDevice());
    m_testHeadingSpin->setEnabled(taskEditingAllowed && usingManualTestDevice());
    // Keep this slot visible even without a candidate so the layout does not
    // jump when automatic recognition changes the target state.
    m_acceptSnapButton->setVisible(true);
    m_acceptSnapButton->setEnabled(taskEditingAllowed && m_hasSnapCandidate);
    m_acceptSnapButton->setText(m_hasSnapCandidate ? QStringLiteral("采用黄色建议点")
                                                   : QStringLiteral("等待黄色建议点"));
    m_planButton->setEnabled(plannerAvailable && originReady && m_hasTarget
                             && m_missionState != MissionState::Planning
                             && m_missionState != MissionState::Uploading);
    const bool missionProtocolReady = m_udpController && m_udpController->hasMissionProtocolCodec();
    const bool telemetryReady = m_udpController && m_udpController->isTelemetryFresh();
    const bool linkReady = m_udpController && m_udpController->isLinkEstablished();
    const bool missionReady = m_preparedMission.valid && missionProtocolReady
        && telemetryReady && linkReady;
    m_uploadButton->setEnabled(missionReady && taskEditingAllowed);
    m_uploadButton->setToolTip(missionProtocolReady
        ? (linkReady
            ? (telemetryReady ? QStringLiteral("上传路线、真实起点和目标后自动启动设备")
                              : QStringLiteral("等待设备位置与艏向遥测"))
            : QStringLiteral("等待设备双向心跳连接"))
        : QStringLiteral("等待设备 UDP 协议接入"));
    m_returnMissionButton->setEnabled(missionReady && taskEditingAllowed);
    m_stopMissionButton->setEnabled(missionProtocolReady && m_routeUploaded);
}

bool RoutePage::usingManualTestDevice() const
{
    return m_manualDeviceCheck && m_manualDeviceCheck->isChecked();
}

bool RoutePage::hasUsableDeviceOrigin() const
{
    if (usingManualTestDevice()) {
        return m_hasManualTestDevice
            && GeoCoordinateUtils::isValidLongitudeLatitude(
                m_manualTestDevicePosition.position)
            && m_manualTestDevicePosition.headingValid
            && qIsFinite(m_manualTestDevicePosition.headingRadians);
    }
    return m_udpController && m_udpController->isTelemetryFresh()
        && m_lastTelemetry.valid && m_lastTelemetry.headingValid
        && qIsFinite(m_lastTelemetry.headingDegrees)
        && GeoCoordinateUtils::isValidLongitudeLatitude(m_lastTelemetry.gcj02Position);
}

double RoutePage::activeDeviceHeadingDegrees() const
{
    return usingManualTestDevice() ? m_testHeadingSpin->value()
                                   : m_lastTelemetry.headingDegrees;
}

QDateTime RoutePage::activeDeviceTimestamp() const
{
    return usingManualTestDevice() ? m_manualTestDeviceTimestamp
                                   : m_lastTelemetry.timestamp;
}

QPointF RoutePage::actualDeviceGcj02() const
{
    return usingManualTestDevice() ? m_manualTestDevicePosition.position
                                   : m_lastTelemetry.gcj02Position;
}

void RoutePage::invalidateRouteForOriginChange(const QString &message)
{
    m_routeUploaded = false;
    if (m_udpController) m_udpController->cancelUpload();
    m_missionId.clear();
    m_plannedRoute = RoutePath();
    m_returnRoute = RoutePath();
    const bool canReuseCurrentGrid = m_waterwayGrid.isValid()
        && m_currentGridIsPlanning && m_mapPlanning
        && m_currentMapRevision == m_mapPlanning->currentMapRevision();
    if (!canReuseCurrentGrid) {
        m_waterwayGrid = WaterwayGrid();
        m_currentGridIsPlanning = false;
    }
    m_waterwayConfirmed = false;
    m_hasPlanningStart = false;
    m_hasSnapCandidate = false;
    m_mapPlanning->setPlannedPath(RoutePath());
    m_mapPlanning->setPlanningStart(RoutePoint(), false);
    m_mapPlanning->setSnapCandidate(RoutePoint(), false);
    clearMissionOriginSnapshot();
    setMissionState(MissionState::NotReady, message);
}

void RoutePage::restoreLatestUdpDevicePosition()
{
    if (!m_lastTelemetry.valid
        || !GeoCoordinateUtils::isValidLongitudeLatitude(m_lastTelemetry.gcj02Position)) {
        m_devicePosition = RoutePoint();
        m_mapPlanning->setActualVehiclePosition(RoutePoint());
        m_devicePositionLabel->setText(QStringLiteral("设备位置：等待 UDP 遥测"));
        return;
    }
    RoutePoint actual;
    actual.id = QStringLiteral("udp-device-actual");
    actual.coordinateSystem = CoordinateSystem::Gcj02;
    actual.position = m_lastTelemetry.gcj02Position;
    actual.headingRadians = qDegreesToRadians(m_lastTelemetry.headingDegrees);
    actual.headingValid = m_lastTelemetry.headingValid
        && qIsFinite(m_lastTelemetry.headingDegrees);
    m_mapPlanning->setActualVehiclePosition(actual);
    bool adjusted = false;
    m_devicePosition = safeDevicePositionForDisplay(actual, &adjusted);
    const double adjustment = geographicDistanceMeters(actual.position, m_devicePosition.position);
    if (adjusted) {
        m_devicePositionLabel->setText(
            QStringLiteral("设备真实位置（GCJ-02）%1 · 安全规划位置 %2 · 已自动吸附 · 调整 %3 m · 艏向 %4° · %5")
                .arg(coordinateText(actual.position), coordinateText(m_devicePosition.position))
                .arg(adjustment, 0, 'f', 1)
                .arg(m_lastTelemetry.headingDegrees, 0, 'f', 1)
                .arg(m_lastTelemetry.timestamp.toLocalTime().toString(QStringLiteral("HH:mm:ss"))));
    } else {
        m_devicePositionLabel->setText(QStringLiteral("设备真实位置（GCJ-02）：%1 · 位于安全水域 · 艏向 %2° · 进度 %3% · %4")
            .arg(coordinateText(actual.position))
            .arg(m_lastTelemetry.headingDegrees, 0, 'f', 1)
            .arg(qRound(m_lastTelemetry.missionProgress * 100.0))
            .arg(m_lastTelemetry.timestamp.toLocalTime().toString(QStringLiteral("HH:mm:ss"))));
    }
    m_mapPlanning->setVehicleTelemetry(m_devicePosition, m_lastTelemetry.headingDegrees);
}

void RoutePage::restoreManualTestDevicePosition()
{
    if (!m_hasManualTestDevice) return;
    m_mapPlanning->setActualVehiclePosition(m_manualTestDevicePosition);
    bool adjusted = false;
    m_devicePosition = safeDevicePositionForDisplay(m_manualTestDevicePosition, &adjusted);
    const double headingDegrees = m_testHeadingSpin->value();
    const double adjustment = geographicDistanceMeters(m_manualTestDevicePosition.position,
                                                        m_devicePosition.position);
    m_mapPlanning->setVehicleTelemetry(m_devicePosition, headingDegrees);
    if (adjusted) {
        m_devicePositionLabel->setText(
            QStringLiteral("测试设备真实位置 %1 · 已自动吸附安全水域 %2 · 调整 %3 m · 测试艏向 %4°")
                .arg(coordinateText(m_manualTestDevicePosition.position),
                     coordinateText(m_devicePosition.position))
                .arg(adjustment, 0, 'f', 1)
                .arg(headingDegrees, 0, 'f', 1));
    } else {
        m_devicePositionLabel->setText(
            QStringLiteral("测试设备真实位置：%1 · 测试艏向 %2°")
                .arg(coordinateText(m_manualTestDevicePosition.position))
                .arg(headingDegrees, 0, 'f', 1));
    }
}

QString RoutePage::coordinateText(const QPointF &position) const
{
    return QStringLiteral("%1, %2 (GCJ-02)")
        .arg(position.x(), 0, 'f', 7).arg(position.y(), 0, 'f', 7);
}

QPoint RoutePage::gridCellForGcj02(const QPointF &gcj02) const
{
    if (!m_waterwayGrid.gridSize.isValid()) return QPoint(-1, -1);
    constexpr double pi = 3.14159265358979323846;
    auto world = [pi](const QPointF &coordinate, int zoom) {
        const double size = 256.0 * std::pow(2.0, zoom);
        const double latitude = qBound(-85.05112878, coordinate.y(), 85.05112878);
        const double radians = qDegreesToRadians(latitude);
        return QPointF((coordinate.x() + 180.0) / 360.0 * size,
            (1.0 - std::log(std::tan(radians) + 1.0 / std::cos(radians)) / pi) * 0.5 * size);
    };
    const GeoReference &reference = m_waterwayGrid.geoReference;
    if (GeoCoordinateUtils::isValidLongitudeLatitude(reference.topLeftGcj02)
        && GeoCoordinateUtils::isValidLongitudeLatitude(reference.bottomRightGcj02)
        && QLineF(reference.topLeftGcj02, reference.bottomRightGcj02).length() > 1e-9) {
        const QPointF topLeft = world(reference.topLeftGcj02, reference.zoom);
        const QPointF bottomRight = world(reference.bottomRightGcj02, reference.zoom);
        const QPointF point = world(gcj02, reference.zoom);
        const double dx = bottomRight.x() - topLeft.x();
        const double dy = bottomRight.y() - topLeft.y();
        if (!qFuzzyIsNull(dx) && !qFuzzyIsNull(dy))
            return QPoint(qRound((point.x() - topLeft.x()) / dx * (m_waterwayGrid.gridSize.width() - 1)),
                          qRound((point.y() - topLeft.y()) / dy * (m_waterwayGrid.gridSize.height() - 1)));
    }
    const QPointF center = world(reference.centerGcj02, reference.zoom);
    const QPointF point = world(gcj02, reference.zoom);
    const QPointF pixel = QPointF(reference.viewportPixels.width() * 0.5,
                                  reference.viewportPixels.height() * 0.5) + point - center;
    return QPoint(qRound(pixel.x() * (m_waterwayGrid.gridSize.width() - 1)
                         / qMax(1, reference.viewportPixels.width() - 1)),
                  qRound(pixel.y() * (m_waterwayGrid.gridSize.height() - 1)
                         / qMax(1, reference.viewportPixels.height() - 1)));
}

QPointF RoutePage::gcj02ForGridPoint(const QPointF &cell) const
{
    constexpr double pi = 3.14159265358979323846;
    const GeoReference &reference = m_waterwayGrid.geoReference;
    const double size = 256.0 * std::pow(2.0, reference.zoom);
    auto world = [size, pi](const QPointF &coordinate) {
        const double latitude = qBound(-85.05112878, coordinate.y(), 85.05112878);
        const double radians = qDegreesToRadians(latitude);
        return QPointF((coordinate.x() + 180.0) / 360.0 * size,
            (1.0 - std::log(std::tan(radians) + 1.0 / std::cos(radians)) / pi) * 0.5 * size);
    };
    if (GeoCoordinateUtils::isValidLongitudeLatitude(reference.topLeftGcj02)
        && GeoCoordinateUtils::isValidLongitudeLatitude(reference.bottomRightGcj02)
        && QLineF(reference.topLeftGcj02, reference.bottomRightGcj02).length() > 1e-9) {
        const QPointF topLeft = world(reference.topLeftGcj02);
        const QPointF bottomRight = world(reference.bottomRightGcj02);
        const QPointF projected(topLeft.x() + (bottomRight.x() - topLeft.x())
                                * cell.x() / qMax(1, m_waterwayGrid.gridSize.width() - 1),
                                topLeft.y() + (bottomRight.y() - topLeft.y())
                                * cell.y() / qMax(1, m_waterwayGrid.gridSize.height() - 1));
        double x = std::fmod(projected.x(), size);
        if (x < 0.0) x += size;
        const double longitude = x / size * 360.0 - 180.0;
        const double n = pi - 2.0 * pi * qBound(0.0, projected.y(), size) / size;
        return QPointF(longitude, qRadiansToDegrees(std::atan(std::sinh(n))));
    }
    const QPointF center = world(reference.centerGcj02);
    const QPointF pixel(cell.x() * qMax(1, reference.viewportPixels.width() - 1)
                        / qMax(1, m_waterwayGrid.gridSize.width() - 1),
                        cell.y() * qMax(1, reference.viewportPixels.height() - 1)
                        / qMax(1, m_waterwayGrid.gridSize.height() - 1));
    QPointF projected = center + pixel - QPointF(reference.viewportPixels.width() * 0.5,
                                                  reference.viewportPixels.height() * 0.5);
    double x = std::fmod(projected.x(), size);
    if (x < 0.0) x += size;
    const double longitude = x / size * 360.0 - 180.0;
    const double n = pi - 2.0 * pi * qBound(0.0, projected.y(), size) / size;
    return QPointF(longitude, qRadiansToDegrees(std::atan(std::sinh(n))));
}

bool RoutePage::cellIsNavigable(const QPoint &cell) const
{
    if (!m_waterwayGrid.isValid() || cell.x() < 0 || cell.y() < 0
        || cell.x() >= m_waterwayGrid.gridSize.width()
        || cell.y() >= m_waterwayGrid.gridSize.height()) return false;
    const int index = cell.y() * m_waterwayGrid.gridSize.width() + cell.x();
    return static_cast<uchar>(m_waterwayGrid.navigableMask.at(index)) != 0;
}

bool RoutePage::cellIsSafe(const QPoint &cell) const
{
    if (!m_waterwayGrid.isValid() || cell.x() < 0 || cell.y() < 0
        || cell.x() >= m_waterwayGrid.gridSize.width()
        || cell.y() >= m_waterwayGrid.gridSize.height()) return false;
    const int index = cell.y() * m_waterwayGrid.gridSize.width() + cell.x();
    const QByteArray &mask = m_waterwayGrid.hasSafeNavigableMask()
        ? m_waterwayGrid.safeNavigableMask : m_waterwayGrid.navigableMask;
    return static_cast<uchar>(mask.at(index)) != 0;
}

QPoint RoutePage::nearestSafeCell(const QPoint &origin, int requiredRegion,
                                  bool requirePlannerClearance) const
{
    if (!m_waterwayGrid.isValid()) return QPoint(-1, -1);
    const int width = m_waterwayGrid.gridSize.width();
    const int height = m_waterwayGrid.gridSize.height();
    const bool hasRegions = m_waterwayGrid.connectedRegionIds.size() == width * height;
    int plannerRegion = requiredRegion;
    // A device that needs snapping must join the same raw water component as
    // the selected mission target.  That gives the clearance test precisely
    // the shoreline point cloud that will later be passed to Hybrid A*.
    if (requirePlannerClearance && plannerRegion <= 0 && m_hasTarget) {
        const QPoint targetCell = gridCellForGcj02(m_targetPosition.position);
        if (cellIsNavigable(targetCell) && hasRegions)
            plannerRegion = m_waterwayGrid.connectedRegionIds.at(
                targetCell.y() * width + targetCell.x());
    }

    struct Candidate {
        QPoint cell;
        qint64 distanceSquared = 0;
    };
    QVector<Candidate> candidates;
    const auto consider = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const QPoint candidate(x, y);
        if (!cellIsSafe(candidate)) return;
        if (plannerRegion > 0
            && (!hasRegions
                || m_waterwayGrid.connectedRegionIds.at(y * width + x) != plannerRegion))
            return;
        const qint64 dx = qint64(x) - origin.x(), dy = qint64(y) - origin.y();
        candidates.append({candidate, dx * dx + dy * dy});
    };
    // Scan the complete recognised view rather than expanding from origin.
    // A target may be on land or outside the view, so an expanding ring can
    // otherwise terminate before it reaches any valid water cell.
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            consider(x, y);
    if (candidates.isEmpty()) return QPoint(-1, -1);
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &left,
                                                        const Candidate &right) {
        return left.distanceSquared != right.distanceSquared
            ? left.distanceSquared < right.distanceSquared
            : (left.cell.y() != right.cell.y()
                ? left.cell.y() < right.cell.y() : left.cell.x() < right.cell.x());
    });
    if (!requirePlannerClearance) return candidates.first().cell;

    // Do not let the pixel distance transform and the planner's simplified
    // shoreline disagree. Rebuild the exact selected-region obstacle cloud
    // once, then accept only a candidate that is also clear of those points.
    if (!hasRegions) return QPoint(-1, -1);
    const QPoint seedCell = candidates.first().cell;
    if (plannerRegion <= 0)
        plannerRegion = m_waterwayGrid.connectedRegionIds.at(
            seedCell.y() * width + seedCell.x());
    if (plannerRegion <= 0) return QPoint(-1, -1);
    RoutePlanningRequest obstacleRequest;
    obstacleRequest.waterway = m_waterwayGrid;
    obstacleRequest.startCell = seedCell;
    obstacleRequest.targetCell = seedCell;
    obstacleRequest.startGcj02 = gcj02ForGridPoint(seedCell);
    obstacleRequest.targetGcj02 = obstacleRequest.startGcj02;
    const QVector<QPointF> obstaclePoints = buildWaterwayObstaclePoints(obstacleRequest);
    if (obstaclePoints.isEmpty()) return QPoint(-1, -1);
    const double minimumClearance = qMax(suppliedHybridAstarParameters().shoreSafetyMeters + 1.0,
                                         6.0);
    for (const Candidate &candidate : candidates) {
        if (m_waterwayGrid.connectedRegionIds.at(candidate.cell.y() * width + candidate.cell.x())
                != plannerRegion)
            continue;
        const QPointF position = gcj02ForGridPoint(candidate.cell);
        bool clear = true;
        for (const QPointF &obstacle : obstaclePoints) {
            if (geographicDistanceMeters(position, obstacle) + 1e-6 < minimumClearance) {
                clear = false;
                break;
            }
        }
        if (clear) return candidate.cell;
    }
    return QPoint(-1, -1);
}

RoutePoint RoutePage::safeDevicePositionForDisplay(const RoutePoint &actual,
                                                    bool *wasAdjusted) const
{
    if (wasAdjusted) *wasAdjusted = false;
    if (!m_waterwayGrid.isValid()
        || !GeoCoordinateUtils::isValidLongitudeLatitude(actual.position)) return actual;

    const QPoint originalCell = gridCellForGcj02(actual.position);
    if (cellIsSafe(originalCell)) return actual;

    // Unlike a user-selected goal, a device GPS fix is corrected immediately.
    // Land, shoreline and out-of-view fixes all use the closest safe cell in
    // the currently recognised water mask, without a confirmation step.
    const QPoint safeCell = nearestSafeCell(originalCell, 0, true);
    if (safeCell.x() < 0) return actual;

    RoutePoint safe = actual;
    safe.id = QStringLiteral("safe-device-display");
    safe.position = gcj02ForGridPoint(safeCell);
    safe.coordinateSystem = CoordinateSystem::Gcj02;
    if (wasAdjusted) *wasAdjusted = true;
    return safe;
}

bool RoutePage::updatePlanningStartFromDevice()
{
    m_hasPlanningStart = false;
    m_mapPlanning->setPlanningStart(RoutePoint(), false);
    if (!hasUsableDeviceOrigin() || !m_waterwayGrid.isValid()
        || !GeoCoordinateUtils::isValidLongitudeLatitude(m_devicePosition.position))
        return false;

    const QPoint originalCell = gridCellForGcj02(m_devicePosition.position);
    QPoint planningCell = originalCell;
    if (!cellIsSafe(originalCell)) {
        planningCell = nearestSafeCell(originalCell, 0, true);
        if (planningCell.x() < 0) return false;
    }

    m_planningStartPosition = m_devicePosition;
    m_planningStartPosition.id = QStringLiteral("safe-planning-start");
    m_planningStartPosition.position = planningCell == originalCell
        ? m_devicePosition.position : gcj02ForGridPoint(planningCell);
    m_planningStartPosition.coordinateSystem = CoordinateSystem::Gcj02;
    const double headingDegrees = activeDeviceHeadingDegrees();
    m_planningStartPosition.headingRadians = qDegreesToRadians(headingDegrees);
    m_planningStartPosition.headingValid = qIsFinite(headingDegrees);
    m_hasPlanningStart = true;

    const double adjustment = geographicDistanceMeters(m_devicePosition.position,
                                                        m_planningStartPosition.position);
    const bool adjusted = planningCell != originalCell;
    if (adjusted) {
        m_mapPlanning->setPlanningStart(m_planningStartPosition, true);
        m_devicePositionLabel->setText(
            QStringLiteral("%1真实位置 %2 · 安全规划起点 %3 · 调整 %4 m · 艏向 %5°")
                .arg(usingManualTestDevice() ? QStringLiteral("测试设备")
                                             : QStringLiteral("设备 GPS"))
                .arg(coordinateText(m_devicePosition.position),
                     coordinateText(m_planningStartPosition.position))
                .arg(adjustment, 0, 'f', 1)
                .arg(headingDegrees, 0, 'f', 1));
    }
    return true;
}

void RoutePage::clearMissionOriginSnapshot()
{
    m_missionOrigin = MissionOriginSnapshot();
    m_frozenTargetPosition = RoutePoint();
    m_preparedMission = PreparedMission();
}

void RoutePage::confirmWaterway()
{
    if (!m_waterwayGrid.isValid()) return;
    const auto insideGrid = [this](const QPoint &cell) {
        return cell.x() >= 0 && cell.y() >= 0
            && cell.x() < m_waterwayGrid.gridSize.width()
            && cell.y() < m_waterwayGrid.gridSize.height();
    };
    if (!m_hasTarget) {
        setMissionState(MissionState::WaterwayPendingConfirmation, QStringLiteral("请先设置单个任务目标"));
        return;
    }
    const QPoint target = gridCellForGcj02(m_targetPosition.position);
    const bool targetInside = insideGrid(target);
    if (!targetInside || !cellIsSafe(target)) {
        m_waterwayConfirmed = false;
        m_hasSnapCandidate = false;
        // Water targets retain their connected-region constraint. A land or
        // out-of-view target has no source region, so offer the closest safe
        // cell in the recognised view as an explicit yellow recommendation.
        int targetRegion = 0;
        if (targetInside && cellIsNavigable(target)
            && m_waterwayGrid.connectedRegionIds.size()
                == m_waterwayGrid.gridSize.width() * m_waterwayGrid.gridSize.height()) {
            targetRegion = m_waterwayGrid.connectedRegionIds.at(
                target.y() * m_waterwayGrid.gridSize.width() + target.x());
        }
        const QPoint candidate = nearestSafeCell(target, targetRegion);
        if (candidate.x() >= 0) {
            m_snapCandidate = m_targetPosition;
            m_snapCandidate.id = QStringLiteral("safe-target-suggestion");
            m_snapCandidate.position = gcj02ForGridPoint(candidate);
            m_snapCandidate.coordinateSystem = CoordinateSystem::Gcj02;
            m_hasSnapCandidate = true;
            const double adjustment = geographicDistanceMeters(m_targetPosition.position,
                                                                m_snapCandidate.position);
            m_mapPlanning->setMissionTarget(m_targetPosition,
                                             MapPlanningWidget::TargetState::Invalid);
            m_mapPlanning->setSnapCandidate(m_snapCandidate, true);
            const QString targetState = !targetInside ? QStringLiteral("不在当前视口")
                : cellIsNavigable(target) ? QStringLiteral("离岸距离不足")
                                          : QStringLiteral("非水域");
            m_targetPositionLabel->setText(
                QStringLiteral("目标位置：%1 · %2；黄色建议点 %3 · 距离 %4 m")
                    .arg(coordinateText(m_targetPosition.position),
                         targetState,
                         coordinateText(m_snapCandidate.position))
                    .arg(adjustment, 0, 'f', 1));
            setMissionState(MissionState::WaterwayPendingConfirmation,
                            QStringLiteral("请确认是否采用黄色建议点"));
            return;
        } else {
            m_mapPlanning->setSnapCandidate(RoutePoint(), false);
            m_mapPlanning->setMissionTarget(m_targetPosition,
                                             MapPlanningWidget::TargetState::Invalid);
            m_targetPositionLabel->setText(QStringLiteral("目标位置：%1 · 非法（陆地或无安全水域）")
                .arg(coordinateText(m_targetPosition.position)));
            setMissionState(MissionState::WaterwayPendingConfirmation,
                            targetInside && cellIsNavigable(target)
                                ? QStringLiteral("同一连通水域内没有满足 %1 m 距岸要求的位置")
                                      .arg(m_waterwayGrid.shoreSafetyMeters, 0, 'f', 1)
                                : QStringLiteral("当前视口未识别到可自动吸附的安全水域"));
            return;
        }
    }
    m_hasSnapCandidate = false;
    m_mapPlanning->setSnapCandidate(RoutePoint(), false);
    m_mapPlanning->setMissionTarget(m_targetPosition, MapPlanningWidget::TargetState::Safe);
    if (m_targetPositionLabel->text().contains(QStringLiteral("自动调整")) == false)
        m_targetPositionLabel->setText(QStringLiteral("目标位置：%1 · 离岸安全距离 ≥ %2 m")
            .arg(coordinateText(m_targetPosition.position))
            .arg(m_waterwayGrid.shoreSafetyMeters, 0, 'f', 1));
    if (!hasUsableDeviceOrigin()) {
        // The waterway/target confirmation is already valid.  Keep it latched
        // while waiting for the independent device-origin check; this lets the
        // first fresh telemetry promote the mission automatically.
        m_waterwayConfirmed = true;
        setMissionState(MissionState::WaterwayPendingConfirmation,
                        usingManualTestDevice()
                            ? QStringLiteral("目标已确认在安全水域，请先标记测试设备位置")
                            : QStringLiteral("目标已确认在安全水域，等待有效 UDP 设备遥测"));
        return;
    }
    if (!updatePlanningStartFromDevice()) {
        m_waterwayConfirmed = false;
        setMissionState(MissionState::WaterwayPendingConfirmation,
                        QStringLiteral("当前视口未识别到可自动吸附的设备安全水域"));
        return;
    }
    const QPoint start = gridCellForGcj02(m_planningStartPosition.position);
    if (m_waterwayGrid.connectedRegionIds.size() == m_waterwayGrid.gridSize.width()
            * m_waterwayGrid.gridSize.height()) {
        const int startRegion = m_waterwayGrid.connectedRegionIds.at(
            start.y() * m_waterwayGrid.gridSize.width() + start.x());
        const int targetRegion = m_waterwayGrid.connectedRegionIds.at(
            target.y() * m_waterwayGrid.gridSize.width() + target.x());
        if (startRegion <= 0 || targetRegion <= 0 || startRegion != targetRegion) {
            m_waterwayConfirmed = false;
            setMissionState(MissionState::WaterwayPendingConfirmation,
                            QStringLiteral("设备起点和目标不在同一连通水域"));
            return;
        }
    }
    m_waterwayConfirmed = true;
    m_recognitionStatusLabel->setText(
        QStringLiteral("河道识别：当前视口颜色识别已确认 · 安全距离 %1 m · 置信度 %2%")
            .arg(m_waterwayGrid.shoreSafetyMeters, 0, 'f', 1)
            .arg(qRound(m_recognitionConfidence * 100.0)));
    setMissionState(MissionState::ReadyToPlan,
                    QStringLiteral("起点和目标均满足 %1 m 离岸安全距离")
                        .arg(m_waterwayGrid.shoreSafetyMeters, 0, 'f', 1));
}

void RoutePage::acceptSnapCandidate()
{
    if (!m_hasSnapCandidate) return;
    m_targetPosition = m_snapCandidate;
    m_targetPosition.id = QStringLiteral("mission-target");
    m_targetPositionLabel->setText(QStringLiteral("目标位置：%1 · 已人工确认吸附")
        .arg(coordinateText(m_targetPosition.position)));
    m_hasSnapCandidate = false;
    m_mapPlanning->setMissionTarget(m_targetPosition, MapPlanningWidget::TargetState::Pending);
    m_mapPlanning->setSnapCandidate(RoutePoint(), false);
    confirmWaterway();
}

void RoutePage::requestPlanning()
{
    requestAction(ActionId::DrawRoute);
    if (!m_plannerController->hasExternalPlanner()) {
        setMissionState(MissionState::Failed, QStringLiteral("原始 Hybrid A* 未接入，无法规划"));
        return;
    }
    if (!hasUsableDeviceOrigin()) {
        setMissionState(MissionState::Failed,
                        usingManualTestDevice()
                            ? QStringLiteral("请先在地图标记测试设备位置")
                            : QStringLiteral("等待有效 UDP 设备遥测和艏向"));
        return;
    }
    if (!m_currentGridIsPlanning
        || !m_waterwayGrid.isValid()
        || m_currentMapRevision != m_mapPlanning->currentMapRevision()) {
        requestPlanningWaterwayCapture();
        return;
    }
    if (!m_waterwayConfirmed || !updatePlanningStartFromDevice()
        || !m_planningStartPosition.headingValid) {
        setMissionState(MissionState::Failed,
                        QStringLiteral("设备起点或艏向不满足规划条件"));
        return;
    }
    m_routeUploaded = false;
    m_plannedRoute = RoutePath();
    m_returnRoute = RoutePath();
    m_preparedMission = PreparedMission();
    m_mapPlanning->setPlannedPath(RoutePath());
    RoutePlanningRequest request;
    m_missionId = QStringLiteral("mission-%1").arg(QDateTime::currentMSecsSinceEpoch());
    m_missionOrigin.safePose = m_planningStartPosition;
    m_missionOrigin.actualGcj02 = actualDeviceGcj02();
    m_missionOrigin.telemetryTimestamp = activeDeviceTimestamp();
    m_missionOrigin.valid = true;
    m_frozenTargetPosition = m_targetPosition;
    request.missionId = m_missionId;
    request.waterway = m_waterwayGrid;
    request.startCell = gridCellForGcj02(m_missionOrigin.safePose.position);
    request.targetCell = gridCellForGcj02(m_frozenTargetPosition.position);
    request.startGcj02 = m_missionOrigin.safePose.position;
    request.targetGcj02 = m_frozenTargetPosition.position;
    request.startHeadingRadians = m_missionOrigin.safePose.headingRadians;
    request.startHeadingValid = m_missionOrigin.safePose.headingValid;
    request.mapRevision = m_currentMapRevision;
    setMissionState(MissionState::Planning,
                    QStringLiteral("原始 Hybrid A* 正在后台计算"));
    if (m_planningDiagnosticsLabel)
        m_planningDiagnosticsLabel->setText(QStringLiteral("规划诊断：正在检查安全连通域、粗路径与起始动作"));
    m_plannerController->planWithExternal(request);
}

RoutePath RoutePage::routeFromPlanningResult(const RoutePlanningResult &result) const
{
    RoutePath route;
    route.algorithmId = result.algorithmId;
    if (!result.success || result.gridPath.size() < 2 || !m_missionOrigin.valid)
        return route;
    // Validate every half-cell along each simplified segment, not only its
    // vertices, so smoothing cannot cut across a bank or a disconnected gap.
    for (int i = 1; i < result.gridPath.size(); ++i) {
        const QPointF from = result.gridPath.at(i - 1);
        const QPointF to = result.gridPath.at(i);
        const int samples = qMax(1, qCeil(QLineF(from, to).length() * 2.0));
        for (int sample = 0; sample <= samples; ++sample) {
            const QPointF point = from + (to - from) * (sample / double(samples));
            if (!cellIsSafe(QPoint(qRound(point.x()), qRound(point.y()))))
                return RoutePath();
        }
    }
    const bool hasGeoPath = !result.gcj02Path.isEmpty()
        && result.gcj02Path.size() == result.gridPath.size();
    for (int i = 0; i < result.gridPath.size(); ++i) {
        RoutePoint point;
        point.id = QStringLiteral("route-%1").arg(i + 1);
        point.coordinateSystem = CoordinateSystem::Gcj02;
        point.position = hasGeoPath ? result.gcj02Path.at(i)
                                    : gcj02ForGridPoint(result.gridPath.at(i));
        if (i < result.headingRadians.size()) {
            point.headingRadians = result.headingRadians.at(i);
            point.headingValid = qIsFinite(point.headingRadians);
        }
        if (!GeoCoordinateUtils::isValidLongitudeLatitude(point.position)) return RoutePath();
        route.points.append(point);
    }
    if (gridCellForGcj02(route.points.first().position)
            != gridCellForGcj02(m_missionOrigin.safePose.position)
        || gridCellForGcj02(route.points.last().position)
            != gridCellForGcj02(m_frozenTargetPosition.position))
        return RoutePath();
    // Preserve the frozen task pose even if live telemetry changes while the
    // background planner is running.
    route.points.first() = m_missionOrigin.safePose;
    route.points.first().id = QStringLiteral("route-1");
    route.points.last().position = m_frozenTargetPosition.position;
    route.valid = true;
    return route;
}

void RoutePage::configureAndStartUdp()
{
    RobotEndpoint endpoint;
    m_udpController->configure(endpoint);
    if (!m_udpController->start())
        m_algorithmStatusLabel->setText(QStringLiteral("算法：原始 Hybrid A* · 控制报文待接入"));
    updateControls();
}

VideoPage::VideoPage(DemoDataModel *model, QWidget *parent) : DashboardPage(parent)
{
    Q_UNUSED(model)
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(16, 4, 16, 16);
    root->setSpacing(12);

    QVBoxLayout *left = new QVBoxLayout;
    QHBoxLayout *controls = new QHBoxLayout;
    m_cameraInfo = label(QStringLiteral("摄像头 0 · USB 摄像头 · 等待连接"), 10, true);
    m_cameraInfo->setMinimumHeight(42);
    m_cameraInfo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_cameraInfo->setStyleSheet(QStringLiteral("color:%1;background:%2;border:1px solid %3;border-radius:6px;padding:8px 14px;")
                              .arg(Theme::text().name(), Theme::background().name(), Theme::accent().darker(150).name()));
    m_playButton = actionButton(QStringLiteral("开始播放"));
    m_playButton->setMinimumWidth(220);
    controls->addWidget(m_cameraInfo, 1);
    controls->addWidget(m_playButton);
    left->addLayout(controls);
    m_video = new VideoPlaceholder;
    // 摄像头连接时播放按钮会短暂禁用；让焦点落到不可编辑的视频区域，
    // 避免 Qt 自动把焦点转交给右侧的 X 位置输入框。
    m_video->setFocusPolicy(Qt::StrongFocus);
    left->addWidget(m_video, 1);

    NeonPanel *controlPanel = new NeonPanel(QStringLiteral("悬停控制 / STATION KEEPING"));
    QVBoxLayout *controlLayout = new QVBoxLayout(controlPanel);
    controlLayout->setContentsMargins(18, 53, 18, 14);
    QFormLayout *form = new QFormLayout;
    form->setSpacing(13);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const QStringList fields = {QStringLiteral("X 位置"), QStringLiteral("Y 位置"), QStringLiteral("深度"), QStringLiteral("艏向角")};
    for (const QString &field : fields) {
        QDoubleSpinBox *input = new QDoubleSpinBox;
        input->setRange(-9999, 9999); input->setDecimals(2); input->setMinimumHeight(38);
        form->addRow(field, input);
    }
    controlLayout->addLayout(form);
    m_coordinates = label(QStringLiteral("目标锁定：待命"), 11, true);
    m_coordinates->setAlignment(Qt::AlignCenter);
    m_coordinates->setStyleSheet(QStringLiteral("color:%1;background:%2;border:1px solid %3;border-radius:6px;padding:10px;")
                                     .arg(Theme::value().name(), Theme::background().name(), Theme::accent().darker(150).name()));
    controlLayout->addWidget(m_coordinates);
    GamepadWidget *gamepad = new GamepadWidget;
    controlLayout->addWidget(gamepad, 1);
    QPushButton *stationKeep = actionButton(QStringLiteral("发送悬停指令"));
    controlLayout->addWidget(stationKeep);

    root->addLayout(left, 73);
    root->addWidget(controlPanel, 27);

    m_cameraController = new CameraController(this);
    m_frameTimer = new QTimer(this);
    m_frameTimer->setInterval(33);
    m_frameTimer->setTimerType(Qt::PreciseTimer);
    connect(m_frameTimer, &QTimer::timeout, this, [this] {
        if (!isVisible()) return;
        const QImage frame = m_cameraController->takeLatestFrame();
        if (!frame.isNull())
            m_video->setFrame(frame);
    });
    m_frameTimer->start();

    connect(m_cameraController, &CameraController::streamOpened, this,
            [this](const CameraStreamInfo &info) {
        const QString fps = info.fps > 0.0 ? QString::number(info.fps, 'f', info.fps < 10.0 ? 1 : 0)
                                           : QStringLiteral("未知");
        m_cameraInfo->setText(QStringLiteral("摄像头 %1 · %2×%3 · %4 FPS · %5")
                                  .arg(info.deviceIndex).arg(info.width).arg(info.height).arg(fps, info.backend));
    });
    connect(m_cameraController, &CameraController::stateChanged, this,
            [this](CameraState state, const QString &message) {
        m_video->setStreamState(state, message);
        switch (state) {
        case CameraState::Opening:
            m_video->setFocus(Qt::OtherFocusReason);
            m_playButton->setText(QStringLiteral("正在连接…"));
            m_playButton->setEnabled(false);
            m_cameraInfo->setText(QStringLiteral("摄像头 0 · 正在尝试连接"));
            break;
        case CameraState::Streaming:
            m_playButton->setText(QStringLiteral("停止播放"));
            m_playButton->setEnabled(true);
            break;
        case CameraState::Error:
            m_playButton->setText(QStringLiteral("开始播放"));
            m_playButton->setEnabled(true);
            m_cameraInfo->setText(QStringLiteral("摄像头 0 · 连接失败"));
            break;
        case CameraState::Stopped:
            m_playButton->setText(QStringLiteral("开始播放"));
            m_playButton->setEnabled(true);
            m_cameraInfo->setText(QStringLiteral("摄像头 0 · USB 摄像头 · 等待连接"));
            break;
        }
    });

    connect(m_playButton, &QPushButton::clicked, this, [this] {
        requestAction(ActionId::StartVideo);
        if (m_cameraController->isRunning()) {
            m_video->setFocus(Qt::OtherFocusReason);
            m_playButton->setText(QStringLiteral("正在停止…"));
            m_playButton->setEnabled(false);
            m_cameraController->stopCamera();
        } else {
            m_video->setFocus(Qt::OtherFocusReason);
            m_video->clearFrame();
            m_cameraController->startCamera(0);
        }
    });
    connect(stationKeep, &QPushButton::clicked, this, [this, stationKeep] {
        requestAction(ActionId::SendStationKeeping);
        stationKeep->setText(QStringLiteral("功能接口已预留"));
        QTimer::singleShot(1000, stationKeep, [stationKeep] { stationKeep->setText(QStringLiteral("发送悬停指令")); });
    });
}

SonarPage::SonarPage(QWidget *parent) : DashboardPage(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 4, 16, 16);
    layout->setSpacing(12);

    NeonPanel *volumePanel = new NeonPanel(QStringLiteral("三维声纳显示 / SONAR 3D VISUALIZATION"));
    QVBoxLayout *volumeLayout = new QVBoxLayout(volumePanel);
    volumeLayout->setContentsMargins(14, 49, 14, 14);
    volumeLayout->addWidget(new SonarVolumePlaceholder, 1);
    layout->addWidget(volumePanel, 1);

    QHBoxLayout *statusRow = new QHBoxLayout;
    statusRow->setSpacing(12);
    QLabel *source = createMetricLabel(QStringLiteral("数据源状态"), QStringLiteral("待接入"));
    QLabel *cloud = createMetricLabel(QStringLiteral("点云状态"), QStringLiteral("无有效数据"));
    QLabel *renderer = createMetricLabel(QStringLiteral("渲染接口"), QStringLiteral("已预留"));
    for (QLabel *item : {source, cloud, renderer}) {
        item->setAlignment(Qt::AlignCenter);
        item->setStyleSheet(QStringLiteral("color:%1;background:%2;border:1px solid %3;border-radius:6px;padding:8px;")
                                .arg(Theme::text().name(), Theme::background().name(),
                                     Theme::accent().darker(150).name()));
        statusRow->addWidget(item, 1);
    }
    layout->addLayout(statusRow);
}

DataPage::DataPage(DemoDataModel *model, QWidget *parent) : DashboardPage(parent)
{
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(16, 4, 16, 16);
    root->setSpacing(12);

    NeonPanel *tablePanel = new NeonPanel(QStringLiteral("实时遥测数据 / TELEMETRY DATA"));
    QVBoxLayout *tableLayout = new QVBoxLayout(tablePanel);
    tableLayout->setContentsMargins(12, 49, 12, 12);
    m_table = new QTableWidget(0, 12);
    configureTable(m_table);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("采集时间"), QStringLiteral("序号"), QStringLiteral("横向位置"),
        QStringLiteral("纵向位置"), QStringLiteral("深度"), QStringLiteral("艏向角"),
        QStringLiteral("纵倾角"), QStringLiteral("横倾角"), QStringLiteral("X轴推力"),
        QStringLiteral("Y轴推力"), QStringLiteral("Z轴推力"), QStringLiteral("偏航力矩")
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tableLayout->addWidget(m_table);

    NeonPanel *controlPanel = new NeonPanel(QStringLiteral("数据中心 / DATA CENTER"));
    QVBoxLayout *control = new QVBoxLayout(controlPanel);
    control->setContentsMargins(12, 50, 12, 12);
    QTabWidget *tabs = new QTabWidget;
    control->addWidget(tabs);

    QWidget *operationTab = new QWidget;
    QVBoxLayout *operations = new QVBoxLayout(operationTab);
    operations->setContentsMargins(12, 14, 12, 14);
    operations->setSpacing(10);
    QLabel *icon = new QLabel(QStringLiteral("◉"));
    icon->setAlignment(Qt::AlignCenter);
    icon->setFont(Theme::font(42, true));
    icon->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::glow().name()));
    operations->addWidget(icon);
    m_statusLabel = label(QStringLiteral("遥测数据链路已连接\nTELEMETRY LINK ONLINE"), 10, true);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    operations->addWidget(m_statusLabel);
    m_countLabel = createMetricLabel(QStringLiteral("会话数据 / 当前显示"), QStringLiteral("0 / 0 条"));
    m_countLabel->setAlignment(Qt::AlignCenter);
    operations->addWidget(m_countLabel);
    operations->addStretch();
    m_collectButton = actionButton(QStringLiteral("暂停数据采集"));
    QPushButton *clearButton = actionButton(QStringLiteral("清空数据"));
    QPushButton *exportButton = actionButton(QStringLiteral("导出 CSV"));
    m_collectButton->setMinimumHeight(45);
    clearButton->setMinimumHeight(45);
    exportButton->setMinimumHeight(45);
    operations->addWidget(m_collectButton);
    operations->addWidget(clearButton);
    operations->addWidget(exportButton);
    tabs->addTab(operationTab, QStringLiteral("数据操作"));

    QWidget *monitoringTab = new QWidget;
    QVBoxLayout *monitoring = new QVBoxLayout(monitoringTab);
    monitoring->setContentsMargins(12, 14, 12, 14);
    monitoring->setSpacing(10);
    QFormLayout *monitorForm = new QFormLayout;
    monitorForm->setSpacing(8);
    m_recordIntervalSpin = new QSpinBox;
    m_recordIntervalSpin->setRange(100, 10000);
    m_recordIntervalSpin->setSuffix(QStringLiteral(" ms"));
    m_visibleRowsSpin = new QSpinBox;
    m_visibleRowsSpin->setRange(20, 2000);
    m_visibleRowsSpin->setSuffix(QStringLiteral(" 行"));
    m_decimalsSpin = new QSpinBox;
    m_decimalsSpin->setRange(0, 6);
    m_decimalsSpin->setSuffix(QStringLiteral(" 位"));
    monitorForm->addRow(QStringLiteral("记录间隔"), m_recordIntervalSpin);
    monitorForm->addRow(QStringLiteral("最大显示"), m_visibleRowsSpin);
    monitorForm->addRow(QStringLiteral("小数位数"), m_decimalsSpin);
    monitoring->addLayout(monitorForm);
    QLabel *fieldTitle = label(QStringLiteral("表格显示字段"), 9, true);
    monitoring->addWidget(fieldTitle);
    QGridLayout *fieldGrid = new QGridLayout;
    fieldGrid->setHorizontalSpacing(8);
    fieldGrid->setVerticalSpacing(5);
    for (int i = 0; i < telemetryFieldIds().size(); ++i) {
        QCheckBox *check = new QCheckBox(telemetryFieldLabels().at(i));
        check->setProperty("fieldId", telemetryFieldIds().at(i));
        m_fieldChecks << check;
        fieldGrid->addWidget(check, i / 2, i % 2);
    }
    monitoring->addLayout(fieldGrid);
    monitoring->addStretch();
    QPushButton *applyMonitoring = actionButton(QStringLiteral("应用采集与显示设置"));
    applyMonitoring->setMinimumHeight(44);
    monitoring->addWidget(applyMonitoring);
    tabs->addTab(monitoringTab, QStringLiteral("采集设置"));

    QWidget *controlTab = new QWidget;
    QVBoxLayout *controlParameters = new QVBoxLayout(controlTab);
    controlParameters->setContentsMargins(12, 14, 12, 14);
    controlParameters->setSpacing(10);
    QLabel *warning = label(QStringLiteral("参数仅保存并发出请求，不直接控制设备"), 8, true);
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::warning().name()));
    controlParameters->addWidget(warning);
    QFormLayout *deviceForm = new QFormLayout;
    deviceForm->setSpacing(9);
    m_targetDepthSpin = new QDoubleSpinBox;
    m_targetDepthSpin->setRange(0.0, 1000.0);
    m_targetDepthSpin->setDecimals(2);
    m_targetDepthSpin->setSuffix(QStringLiteral(" m"));
    m_targetHeadingSpin = new QDoubleSpinBox;
    m_targetHeadingSpin->setRange(0.0, 359.9);
    m_targetHeadingSpin->setDecimals(1);
    m_targetHeadingSpin->setSuffix(QStringLiteral(" °"));
    m_cruiseSpeedSpin = new QDoubleSpinBox;
    m_cruiseSpeedSpin->setRange(0.0, 3.0);
    m_cruiseSpeedSpin->setDecimals(2);
    m_cruiseSpeedSpin->setSuffix(QStringLiteral(" m/s"));
    m_powerLimitSpin = new QDoubleSpinBox;
    m_powerLimitSpin->setRange(0.0, 100.0);
    m_powerLimitSpin->setDecimals(1);
    m_powerLimitSpin->setSuffix(QStringLiteral(" %"));
    m_controlModeCombo = new QComboBox;
    m_controlModeCombo->addItem(QStringLiteral("手动控制"), static_cast<int>(ControlMode::Manual));
    m_controlModeCombo->addItem(QStringLiteral("航向保持"), static_cast<int>(ControlMode::HeadingHold));
    m_controlModeCombo->addItem(QStringLiteral("深度保持"), static_cast<int>(ControlMode::DepthHold));
    m_controlModeCombo->addItem(QStringLiteral("定点悬停"), static_cast<int>(ControlMode::StationKeeping));
    deviceForm->addRow(QStringLiteral("目标深度"), m_targetDepthSpin);
    deviceForm->addRow(QStringLiteral("目标艏向"), m_targetHeadingSpin);
    deviceForm->addRow(QStringLiteral("航行速度"), m_cruiseSpeedSpin);
    deviceForm->addRow(QStringLiteral("功率上限"), m_powerLimitSpin);
    deviceForm->addRow(QStringLiteral("控制模式"), m_controlModeCombo);
    controlParameters->addLayout(deviceForm);
    m_controlFeedback = label(QStringLiteral("设备控制接口待命"), 9, true);
    m_controlFeedback->setAlignment(Qt::AlignCenter);
    m_controlFeedback->setWordWrap(true);
    controlParameters->addWidget(m_controlFeedback);
    controlParameters->addStretch();
    QPushButton *applyControl = actionButton(QStringLiteral("应用控制参数"));
    applyControl->setMinimumHeight(44);
    controlParameters->addWidget(applyControl);
    tabs->addTab(controlTab, QStringLiteral("控制参数"));

    root->addWidget(tablePanel, 75);
    root->addWidget(controlPanel, 25);

    loadSettings();
    rebuildTable();
    m_recordTimer = new QTimer(this);
    m_recordTimer->setTimerType(Qt::PreciseTimer);
    m_recordTimer->setInterval(m_settings.recordIntervalMs);
    m_recordTimer->start();

    connect(m_collectButton, &QPushButton::clicked, this, [this] {
        requestAction(ActionId::StartCollection);
        m_collecting = !m_collecting;
        m_collectButton->setText(m_collecting ? QStringLiteral("暂停数据采集") : QStringLiteral("继续数据采集"));
        m_statusLabel->setText(m_collecting ? QStringLiteral("遥测数据链路已连接\nTELEMETRY LINK ONLINE")
                                            : QStringLiteral("数据记录已暂停\nRECORDING PAUSED"));
    });
    connect(clearButton, &QPushButton::clicked, this, &DataPage::clearData);
    connect(exportButton, &QPushButton::clicked, this, &DataPage::exportData);
    connect(applyMonitoring, &QPushButton::clicked, this, &DataPage::applyMonitoringSettings);
    connect(applyControl, &QPushButton::clicked, this, &DataPage::applyControlParameters);
    connect(model, &DemoDataModel::sampleReady, this, [this](double phase, int) {
        m_latestPhase = phase;
        m_hasLatestSample = true;
    });
    connect(m_recordTimer, &QTimer::timeout, this, [this] {
        if (m_collecting && m_hasLatestSample)
            addSample(m_latestPhase, 0);
    });
}

TelemetrySample DataPage::createSample(double phase)
{
    TelemetrySample sample;
    sample.timestamp = QDateTime::currentDateTime();
    sample.sequence = ++m_nextSequence;
    sample.xpos = 24.0 + qSin(phase * 0.23) * 4.0;
    sample.ypos = 16.0 + qCos(phase * 0.19) * 3.0;
    sample.depth = 12.4 + qSin(phase * 0.13) * 0.8;
    sample.yaw = 15.0 + qSin(phase * 0.11) * 22.0;
    sample.pitch = qSin(phase * 0.47) * 7.0;
    sample.roll = qCos(phase * 0.41) * 9.0;
    sample.forceX = qSin(phase) * 18.0;
    sample.forceY = qCos(phase * 0.91) * 16.0;
    sample.forceZ = qSin(phase * 0.73) * 12.0;
    sample.forceYaw = qCos(phase * 0.62) * 30.0;
    return sample;
}

void DataPage::addSample(double phase, int)
{
    constexpr int maximumSessionSamples = 100000;
    if (m_sessionSamples.size() >= maximumSessionSamples) {
        m_collecting = false;
        m_collectButton->setText(QStringLiteral("继续数据采集"));
        m_statusLabel->setText(QStringLiteral("已达到 100000 条缓存上限\nRECORDING STOPPED"));
        if (!m_limitReported) {
            m_limitReported = true;
            QMessageBox::warning(this, QStringLiteral("会话缓存已满"),
                                 QStringLiteral("已达到 100000 条会话数据上限，采集已自动暂停。请先导出或清空数据。"));
        }
        return;
    }
    const TelemetrySample sample = createSample(phase);
    m_sessionSamples.append(sample);
    appendSampleToTable(sample, m_sessionSamples.size() - 1);
    while (m_table->rowCount() > m_settings.maximumVisibleRows)
        m_table->removeRow(0);
    m_table->scrollToBottom();
    updateCountLabel();
}

void DataPage::appendSampleToTable(const TelemetrySample &sample, int cacheIndex)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    const QList<QString> values = {
        sample.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
        QString::number(sample.sequence),
        QString::number(sample.xpos, 'f', m_settings.decimalPlaces),
        QString::number(sample.ypos, 'f', m_settings.decimalPlaces),
        QString::number(sample.depth, 'f', m_settings.decimalPlaces),
        QString::number(sample.yaw, 'f', m_settings.decimalPlaces),
        QString::number(sample.pitch, 'f', m_settings.decimalPlaces),
        QString::number(sample.roll, 'f', m_settings.decimalPlaces),
        QString::number(sample.forceX, 'f', m_settings.decimalPlaces),
        QString::number(sample.forceY, 'f', m_settings.decimalPlaces),
        QString::number(sample.forceZ, 'f', m_settings.decimalPlaces),
        QString::number(sample.forceYaw, 'f', m_settings.decimalPlaces)
    };
    for (int column = 0; column < values.size(); ++column) {
        QTableWidgetItem *item = new QTableWidgetItem(values.at(column));
        item->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, column, item);
    }
    m_table->item(row, 1)->setData(Qt::UserRole, cacheIndex);
}

void DataPage::rebuildTable()
{
    m_table->setRowCount(0);
    const int first = qMax(0, m_sessionSamples.size() - m_settings.maximumVisibleRows);
    for (int index = first; index < m_sessionSamples.size(); ++index)
        appendSampleToTable(m_sessionSamples.at(index), index);
    applyColumnVisibility();
    updateCountLabel();
}

void DataPage::applyColumnVisibility()
{
    m_table->setColumnHidden(0, false);
    m_table->setColumnHidden(1, false);
    for (int i = 0; i < telemetryFieldIds().size(); ++i)
        m_table->setColumnHidden(i + 2, !m_settings.visibleFields.contains(telemetryFieldIds().at(i)));
}

void DataPage::updateCountLabel()
{
    m_countLabel->setText(QStringLiteral("<span style='color:%1'>会话数据 / 当前显示</span><br><span style='color:%2;font-size:18px;font-weight:600'>%3 / %4 条</span>")
                              .arg(Theme::textMuted().name(), Theme::value().name())
                              .arg(m_sessionSamples.size())
                              .arg(m_table->rowCount()));
}

void DataPage::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("DataManagement"));
    m_settings.recordIntervalMs = settings.value(QStringLiteral("recordIntervalMs"), 500).toInt();
    m_settings.maximumVisibleRows = settings.value(QStringLiteral("maximumVisibleRows"), 120).toInt();
    m_settings.decimalPlaces = settings.value(QStringLiteral("decimalPlaces"), 2).toInt();
    m_settings.visibleFields = settings.value(QStringLiteral("visibleFields"), telemetryFieldIds()).toStringList();
    m_recordIntervalSpin->setValue(qBound(100, m_settings.recordIntervalMs, 10000));
    m_visibleRowsSpin->setValue(qBound(20, m_settings.maximumVisibleRows, 2000));
    m_decimalsSpin->setValue(qBound(0, m_settings.decimalPlaces, 6));
    for (QCheckBox *check : m_fieldChecks)
        check->setChecked(m_settings.visibleFields.contains(check->property("fieldId").toString()));
    m_settings = settingsFromControls();

    m_targetDepthSpin->setValue(settings.value(QStringLiteral("targetDepth"), 0.0).toDouble());
    m_targetHeadingSpin->setValue(settings.value(QStringLiteral("targetHeading"), 0.0).toDouble());
    m_cruiseSpeedSpin->setValue(settings.value(QStringLiteral("cruiseSpeed"), 0.0).toDouble());
    m_powerLimitSpin->setValue(settings.value(QStringLiteral("thrusterPowerLimit"), 100.0).toDouble());
    const int mode = settings.value(QStringLiteral("controlMode"), static_cast<int>(ControlMode::Manual)).toInt();
    const int modeIndex = m_controlModeCombo->findData(mode);
    m_controlModeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    settings.endGroup();
}

MonitoringSettings DataPage::settingsFromControls() const
{
    MonitoringSettings settings;
    settings.recordIntervalMs = m_recordIntervalSpin->value();
    settings.maximumVisibleRows = m_visibleRowsSpin->value();
    settings.decimalPlaces = m_decimalsSpin->value();
    for (QCheckBox *check : m_fieldChecks)
        if (check->isChecked()) settings.visibleFields << check->property("fieldId").toString();
    return settings;
}

DeviceControlParameters DataPage::controlParametersFromControls() const
{
    DeviceControlParameters parameters;
    parameters.targetDepth = m_targetDepthSpin->value();
    parameters.targetHeading = m_targetHeadingSpin->value();
    parameters.cruiseSpeed = m_cruiseSpeedSpin->value();
    parameters.thrusterPowerLimit = m_powerLimitSpin->value();
    parameters.mode = static_cast<ControlMode>(m_controlModeCombo->currentData().toInt());
    return parameters;
}

void DataPage::applyMonitoringSettings()
{
    m_settings = settingsFromControls();
    QSettings settings;
    settings.beginGroup(QStringLiteral("DataManagement"));
    settings.setValue(QStringLiteral("recordIntervalMs"), m_settings.recordIntervalMs);
    settings.setValue(QStringLiteral("maximumVisibleRows"), m_settings.maximumVisibleRows);
    settings.setValue(QStringLiteral("decimalPlaces"), m_settings.decimalPlaces);
    settings.setValue(QStringLiteral("visibleFields"), m_settings.visibleFields);
    settings.endGroup();
    if (m_recordTimer)
        m_recordTimer->setInterval(m_settings.recordIntervalMs);
    rebuildTable();
    requestAction(ActionId::ApplyMonitoringSettings);
    emit monitoringSettingsChanged(m_settings);
    m_statusLabel->setText(QStringLiteral("采集与显示设置已应用\nSETTINGS APPLIED"));
}

void DataPage::applyControlParameters()
{
    const DeviceControlParameters parameters = controlParametersFromControls();
    const QString summary = QStringLiteral("目标深度：%1 m\n目标艏向：%2°\n航行速度：%3 m/s\n推进器功率上限：%4%\n\n确认保存并发出控制参数请求吗？")
                                .arg(parameters.targetDepth, 0, 'f', 2)
                                .arg(parameters.targetHeading, 0, 'f', 1)
                                .arg(parameters.cruiseSpeed, 0, 'f', 2)
                                .arg(parameters.thrusterPowerLimit, 0, 'f', 1);
    if (QMessageBox::question(this, QStringLiteral("确认控制参数"), summary,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QSettings settings;
    settings.beginGroup(QStringLiteral("DataManagement"));
    settings.setValue(QStringLiteral("targetDepth"), parameters.targetDepth);
    settings.setValue(QStringLiteral("targetHeading"), parameters.targetHeading);
    settings.setValue(QStringLiteral("cruiseSpeed"), parameters.cruiseSpeed);
    settings.setValue(QStringLiteral("thrusterPowerLimit"), parameters.thrusterPowerLimit);
    settings.setValue(QStringLiteral("controlMode"), static_cast<int>(parameters.mode));
    settings.endGroup();
    requestAction(ActionId::ApplyControlParameters);
    emit deviceControlRequested(parameters);
    m_controlFeedback->setText(QStringLiteral("设备控制接口已预留 · REQUEST EMITTED"));
}

void DataPage::clearData()
{
    if (QMessageBox::question(this, QStringLiteral("清空遥测数据"),
                              QStringLiteral("确认清空当前表格和本次会话缓存中的全部遥测数据吗？"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const bool resumeAfterLimit = m_limitReported;
    m_sessionSamples.clear();
    m_table->setRowCount(0);
    m_nextSequence = 0;
    m_limitReported = false;
    if (resumeAfterLimit) {
        m_collecting = true;
        m_collectButton->setText(QStringLiteral("暂停数据采集"));
    }
    m_statusLabel->setText(m_collecting ? QStringLiteral("数据已清空，采集继续\nCACHE CLEARED")
                                        : QStringLiteral("数据已清空，记录仍暂停\nCACHE CLEARED"));
    updateCountLabel();
    requestAction(ActionId::ClearData);
}

void DataPage::exportData()
{
    if (m_sessionSamples.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("没有可导出的数据"),
                                 QStringLiteral("当前会话尚未采集遥测数据。"));
        return;
    }

    const QModelIndexList selectedRows = m_table->selectionModel()->selectedRows();
    QMessageBox scopeBox(this);
    scopeBox.setWindowTitle(QStringLiteral("选择导出范围"));
    scopeBox.setText(QStringLiteral("请选择需要导出的遥测数据范围。"));
    QPushButton *selectedButton = scopeBox.addButton(QStringLiteral("导出选中行 (%1)").arg(selectedRows.size()), QMessageBox::AcceptRole);
    QPushButton *allButton = scopeBox.addButton(QStringLiteral("导出全部会话数据 (%1)").arg(m_sessionSamples.size()), QMessageBox::ActionRole);
    scopeBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    selectedButton->setEnabled(!selectedRows.isEmpty());
    scopeBox.exec();
    if (scopeBox.clickedButton() != selectedButton && scopeBox.clickedButton() != allButton)
        return;

    QVector<TelemetrySample> samples;
    if (scopeBox.clickedButton() == allButton) {
        samples = m_sessionSamples;
    } else {
        QList<int> indexes;
        for (const QModelIndex &rowIndex : selectedRows) {
            const QTableWidgetItem *item = m_table->item(rowIndex.row(), 1);
            if (item) indexes << item->data(Qt::UserRole).toInt();
        }
        std::sort(indexes.begin(), indexes.end());
        for (int index : indexes)
            if (index >= 0 && index < m_sessionSamples.size()) samples.append(m_sessionSamples.at(index));
    }
    if (samples.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("没有找到有效的选中数据。"));
        return;
    }

    const QString defaultName = QStringLiteral("telemetry_%1.csv").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QString filePath = QFileDialog::getSaveFileName(this, QStringLiteral("导出遥测数据"), defaultName,
                                                     QStringLiteral("CSV 文件 (*.csv)"));
    if (filePath.isEmpty()) return;
    if (!filePath.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        filePath += QStringLiteral(".csv");
    QString errorMessage;
    if (!writeCsv(filePath, samples, &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("导出失败"), errorMessage);
        return;
    }
    requestAction(ActionId::ExportData);
    emit telemetryExported(filePath, samples.size());
    QMessageBox::information(this, QStringLiteral("导出完成"),
                             QStringLiteral("已导出 %1 条遥测数据。\n%2").arg(samples.size()).arg(filePath));
}

bool DataPage::writeCsv(const QString &filePath, const QVector<TelemetrySample> &samples, QString *errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法写入文件：%1").arg(file.errorString());
        return false;
    }
    file.write("\xEF\xBB\xBF");
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QStringLiteral("采集时间,序号,xpos,ypos,depth,yaw,pitch,roll,forceX,forceY,forceZ,forceYAW\r\n");
    for (const TelemetrySample &sample : samples) {
        const QStringList row = {
            csvCell(sample.timestamp.toString(Qt::ISODateWithMs)),
            QString::number(sample.sequence),
            QString::number(sample.xpos, 'f', 6), QString::number(sample.ypos, 'f', 6),
            QString::number(sample.depth, 'f', 6), QString::number(sample.yaw, 'f', 6),
            QString::number(sample.pitch, 'f', 6), QString::number(sample.roll, 'f', 6),
            QString::number(sample.forceX, 'f', 6), QString::number(sample.forceY, 'f', 6),
            QString::number(sample.forceZ, 'f', 6), QString::number(sample.forceYaw, 'f', 6)
        };
        stream << row.join(QLatin1Char(',')) << QStringLiteral("\r\n");
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok || file.error() != QFile::NoError) {
        if (errorMessage) *errorMessage = QStringLiteral("写入文件时发生错误：%1").arg(file.errorString());
        return false;
    }
    return true;
}
