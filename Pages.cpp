#include "Pages.h"
#include "DemoDataModel.h"
#include "Theme.h"
#include "Widgets.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>

namespace {

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
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(16, 4, 16, 16);
    root->setSpacing(12);

    m_mapPlanning = new MapPlanningWidget;

    QVBoxLayout *right = new QVBoxLayout;
    right->setSpacing(10);
    NeonPanel *waypointPanel = new NeonPanel(QStringLiteral("期望路径点设置 / WAYPOINTS"));
    QVBoxLayout *waypointLayout = new QVBoxLayout(waypointPanel);
    waypointLayout->setContentsMargins(12, 49, 12, 12);
    m_waypoints = new QTableWidget(8, 3);
    configureTable(m_waypoints);
    m_waypoints->setHorizontalHeaderLabels({QStringLiteral("序号"), QStringLiteral("经度 / X"), QStringLiteral("纬度 / Y")});
    m_waypoints->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int i = 0; i < m_waypoints->rowCount(); ++i) {
        m_waypoints->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_waypoints->setItem(i, 1, new QTableWidgetItem(QString::number(120.102 + i * 0.0013, 'f', 4)));
        m_waypoints->setItem(i, 2, new QTableWidgetItem(QString::number(30.206 + i * 0.0008, 'f', 4)));
    }
    waypointLayout->addWidget(m_waypoints, 1);
    QGridLayout *actions = new QGridLayout;
    QPushButton *addRow = actionButton(QStringLiteral("添加行"));
    QPushButton *removeRow = actionButton(QStringLiteral("删除行"));
    QPushButton *clearRows = actionButton(QStringLiteral("清空"));
    QPushButton *defaults = actionButton(QStringLiteral("默认参数"));
    QPushButton *drawRoute = actionButton(QStringLiteral("绘制路径"));
    QPushButton *sendTask = actionButton(QStringLiteral("发送任务"));
    const QList<QPushButton *> actionButtons = {addRow, removeRow, clearRows, defaults, drawRoute, sendTask};
    for (int i = 0; i < actionButtons.size(); ++i) actions->addWidget(actionButtons.at(i), i / 3, i % 3);
    waypointLayout->addLayout(actions);
    right->addWidget(waypointPanel, 7);
    m_liveTrack = new LineChart(QStringLiteral("船舶实时轨迹 / LIVE TRACK"));
    right->addWidget(m_liveTrack, 3);

    root->addWidget(m_mapPlanning, 73);
    root->addLayout(right, 27);

    syncMapWaypoints();

    connect(addRow, &QPushButton::clicked, this, [this] {
        requestAction(ActionId::AddWaypoint);
        const int row = m_waypoints->rowCount();
        m_waypoints->insertRow(row);
        m_waypoints->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        m_waypoints->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0.0000")));
        m_waypoints->setItem(row, 2, new QTableWidgetItem(QStringLiteral("0.0000")));
        m_waypoints->setCurrentCell(row, 1);
        syncMapWaypoints();
    });
    connect(removeRow, &QPushButton::clicked, this, [this] {
        requestAction(ActionId::RemoveWaypoint);
        int row = m_waypoints->currentRow();
        if (row < 0) row = m_waypoints->rowCount() - 1;
        if (row >= 0) m_waypoints->removeRow(row);
        for (int i = 0; i < m_waypoints->rowCount(); ++i)
            m_waypoints->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        syncMapWaypoints();
    });
    connect(clearRows, &QPushButton::clicked, this, [this] {
        requestAction(ActionId::ClearWaypoints);
        m_waypoints->setRowCount(0);
        m_mapPlanning->clearOverlays();
    });
    connect(defaults, &QPushButton::clicked, this, [this] {
        requestAction(ActionId::LoadDefaults);
        m_waypoints->setRowCount(6);
        for (int i = 0; i < 6; ++i) {
            m_waypoints->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
            m_waypoints->setItem(i, 1, new QTableWidgetItem(QString::number(120.102 + i * 0.0013, 'f', 4)));
            m_waypoints->setItem(i, 2, new QTableWidgetItem(QString::number(30.206 + i * 0.0008, 'f', 4)));
        }
        syncMapWaypoints();
    });
    connect(drawRoute, &QPushButton::clicked, this, [this, drawRoute] {
        requestAction(ActionId::DrawRoute);
        drawRoute->setText(QStringLiteral("规划算法接口已预留"));
        QTimer::singleShot(900, drawRoute, [drawRoute] { drawRoute->setText(QStringLiteral("绘制路径")); });
    });
    connect(sendTask, &QPushButton::clicked, this, [this, sendTask] {
        requestAction(ActionId::SendMission);
        sendTask->setText(QStringLiteral("接口已预留"));
        QTimer::singleShot(900, sendTask, [sendTask] { sendTask->setText(QStringLiteral("发送任务")); });
    });
    connect(m_waypoints, &QTableWidget::cellChanged, this, [this](int, int) {
        syncMapWaypoints();
    });

    connect(model, &DemoDataModel::sampleReady, this, [this](double phase, int) {
        m_liveTrack->append(qSin(phase * 0.55) * 0.6, qCos(phase * 0.38) * 0.7);
    });
}

QVector<RoutePoint> RoutePage::routePointsFromTable() const
{
    QVector<RoutePoint> points;
    if (!m_waypoints) return points;
    points.reserve(m_waypoints->rowCount());
    for (int row = 0; row < m_waypoints->rowCount(); ++row) {
        RoutePoint point;
        point.id = m_waypoints->item(row, 0) ? m_waypoints->item(row, 0)->text()
                                             : QString::number(row + 1);
        const double first = m_waypoints->item(row, 1) ? m_waypoints->item(row, 1)->text().toDouble() : 0.0;
        const double second = m_waypoints->item(row, 2) ? m_waypoints->item(row, 2)->text().toDouble() : 0.0;
        point.position = QPointF(first, second);
        point.coordinateSystem = CoordinateSystem::Unspecified;
        points.append(point);
    }
    return points;
}

void RoutePage::syncMapWaypoints()
{
    if (m_mapPlanning)
        m_mapPlanning->setWaypoints(routePointsFromTable());
}

VideoPage::VideoPage(DemoDataModel *model, QWidget *parent) : DashboardPage(parent)
{
    Q_UNUSED(model)
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(16, 4, 16, 16);
    root->setSpacing(12);

    QVBoxLayout *left = new QVBoxLayout;
    QHBoxLayout *controls = new QHBoxLayout;
    QLabel *camera = label(QStringLiteral("CAP 0 · 水下作业摄像头 · SINGLE CAMERA"), 10, true);
    camera->setMinimumHeight(42);
    camera->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    camera->setStyleSheet(QStringLiteral("color:%1;background:%2;border:1px solid %3;border-radius:6px;padding:8px 14px;")
                              .arg(Theme::text().name(), Theme::background().name(), Theme::accent().darker(150).name()));
    QPushButton *play = actionButton(QStringLiteral("开始播放"));
    play->setMinimumWidth(220);
    controls->addWidget(camera, 1);
    controls->addWidget(play);
    left->addLayout(controls);
    m_video = new VideoPlaceholder;
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

    connect(play, &QPushButton::clicked, this, [this, play] {
        requestAction(ActionId::StartVideo);
        const bool nowPlaying = play->text() == QStringLiteral("开始播放");
        m_video->setPlaying(nowPlaying);
        play->setText(nowPlaying ? QStringLiteral("停止播放") : QStringLiteral("开始播放"));
        m_coordinates->setText(nowPlaying ? QStringLiteral("视觉链路：ONLINE") : QStringLiteral("目标锁定：待命"));
    });
    connect(stationKeep, &QPushButton::clicked, this, [this, stationKeep] {
        requestAction(ActionId::SendStationKeeping);
        stationKeep->setText(QStringLiteral("功能接口已预留"));
        QTimer::singleShot(1000, stationKeep, [stationKeep] { stationKeep->setText(QStringLiteral("发送悬停指令")); });
    });
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
        QStringLiteral("采集时间"), QStringLiteral("序号"), QStringLiteral("xpos"),
        QStringLiteral("ypos"), QStringLiteral("depth"), QStringLiteral("yaw"),
        QStringLiteral("pitch"), QStringLiteral("roll"), QStringLiteral("forceX"),
        QStringLiteral("forceY"), QStringLiteral("forceZ"), QStringLiteral("forceYAW")
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
