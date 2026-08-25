#include "MainWindow.h"
#include "DemoDataModel.h"
#include "EffectController.h"
#include "Pages.h"
#include "Theme.h"
#include "Widgets.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QDateTime>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QRadialGradient>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

class BackgroundWidget : public QWidget
{
public:
    explicit BackgroundWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        connect(EffectController::instance(), &EffectController::frameAdvanced, this, [this](qreal phase, qreal) {
            if (!isVisible()) return;
            m_phase = phase;
            update();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::backgroundGradient(rect()));

        QColor gridColor = Theme::accent();
        gridColor.setAlpha(20);
        p.setPen(QPen(gridColor, 1));
        const int grid = qMax(30, width() / 48);
        for (int x = 0; x < width(); x += grid) p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += grid) p.drawLine(0, y, width(), y);
        QRadialGradient bloom(QPointF(width() * 0.5, 0), width() * 0.38);
        QColor bloomStart = Theme::glow();
        bloomStart.setAlpha(45);
        QColor bloomEnd = Theme::glow();
        bloomEnd.setAlpha(0);
        bloom.setColorAt(0.0, bloomStart);
        bloom.setColorAt(1.0, bloomEnd);
        p.fillRect(rect(), bloom);

        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < 4; ++i) {
            QPainterPath current;
            const qreal baseY = height() * (0.28 + i * 0.18);
            for (int x = -20; x <= width() + 20; x += 22) {
                const qreal y = baseY + qSin(x * 0.008 + m_phase * (0.22 + i * 0.04) + i) * (8 + i * 3);
                if (x < 0) current.moveTo(x, y); else current.lineTo(x, y);
            }
            QColor water = i % 2 ? Theme::plasmaViolet() : Theme::iceCyan();
            water.setAlpha(12 + i * 2);
            p.setPen(QPen(water, 1.0));
            p.drawPath(current);
        }

        const qreal scanY = std::fmod(m_phase * 23.0, qMax(1, height()));
        QLinearGradient scan(0, scanY - 35, 0, scanY + 35);
        QColor scanCyan = Theme::glow();
        scanCyan.setAlpha(0);
        QColor scanPeak = Theme::glow();
        scanPeak.setAlpha(18);
        QColor scanViolet = Theme::plasmaViolet();
        scanViolet.setAlpha(0);
        scan.setColorAt(0.0, scanCyan);
        scan.setColorAt(0.5, scanPeak);
        scan.setColorAt(1.0, scanViolet);
        p.fillRect(QRectF(0, scanY - 35, width(), 70), scan);
    }

private:
    qreal m_phase = 0.0;
};

class TitleLabel : public QLabel
{
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient gradient(0, 0, width(), 0);
        gradient.setColorAt(0, Theme::titleStart());
        gradient.setColorAt(0.55, Theme::glow());
        gradient.setColorAt(1, Theme::titleEnd());
        QFont f = Theme::font(24, true);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
        p.setFont(f);
        QColor titleGlow = Theme::glow();
        titleGlow.setAlpha(35);
        p.setPen(titleGlow);
        p.drawText(rect().translated(0, 3), alignment(), text());
        p.setPen(QPen(QBrush(gradient), 1));
        p.drawText(rect(), alignment(), text());
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_model(new DemoDataModel(this))
{
    qRegisterMetaType<ActionId>("ActionId");
    qRegisterMetaType<CoordinateSystem>("CoordinateSystem");
    qRegisterMetaType<RoutePoint>("RoutePoint");
    qRegisterMetaType<QVector<RoutePoint>>("QVector<RoutePoint>");
    qRegisterMetaType<RoutePath>("RoutePath");
    qRegisterMetaType<TelemetrySample>("TelemetrySample");
    qRegisterMetaType<MonitoringSettings>("MonitoringSettings");
    qRegisterMetaType<ControlMode>("ControlMode");
    qRegisterMetaType<DeviceControlParameters>("DeviceControlParameters");
    setWindowTitle(QStringLiteral("水下清洁机器人智能监控系统"));
    setMinimumSize(1280, 720);
    resize(1600, 900);
    setStyleSheet(Theme::commonStyle());

    BackgroundWidget *central = new BackgroundWidget;
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(22, 10, 22, 18);
    layout->setSpacing(3);
    layout->addWidget(createHeader());
    layout->addWidget(createNavigation());

    m_pages = new QStackedWidget;
    QList<DashboardPage *> pageList;
    pageList << new RoutePage(m_model)
             << new HealthPage(m_model)
             << new VideoPage(m_model)
             << new DataPage(m_model);
    for (DashboardPage *page : pageList) {
        m_pages->addWidget(page);
        connect(page, &DashboardPage::actionRequested, this, [this](ActionId action) {
            emit actionRequested(action);
            showActionHint(action);
        });
        connect(page, &DashboardPage::monitoringSettingsChanged,
                this, &MainWindow::monitoringSettingsChanged);
        connect(page, &DashboardPage::deviceControlRequested,
                this, &MainWindow::deviceControlRequested);
        connect(page, &DashboardPage::telemetryExported,
                this, &MainWindow::telemetryExported);
    }
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    navigateTo(PageId::HealthMonitoring);
    m_model->start();
    EffectController::instance()->start();

    QTimer *clockTimer = new QTimer(this);
    clockTimer->setInterval(1000);
    connect(clockTimer, &QTimer::timeout, this, [this] {
        m_clock->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd  hh:mm:ss")));
    });
    clockTimer->start();
    m_clock->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd  hh:mm:ss")));
}

QWidget *MainWindow::createHeader()
{
    QWidget *header = new QWidget;
    header->setFixedHeight(86);
    QHBoxLayout *layout = new QHBoxLayout(header);
    layout->setContentsMargins(12, 0, 12, 0);
    QLabel *system = new QLabel(QStringLiteral("SYSTEM 01 · ONLINE"));
    system->setFont(Theme::font(9, true));
    system->setStyleSheet(QStringLiteral("color:%1;letter-spacing:2px;").arg(Theme::accent().name()));
    system->setMinimumWidth(260);
    TitleLabel *title = new TitleLabel(QStringLiteral("水下清洁机器人智能监控系统"));
    title->setAlignment(Qt::AlignCenter);
    m_actionHint = new QLabel(QStringLiteral("接口层待命 · ACTION BUS READY"));
    m_actionHint->setAlignment(Qt::AlignCenter);
    m_actionHint->setFont(Theme::font(8, true));
    m_actionHint->setStyleSheet(QStringLiteral("color:%1;letter-spacing:1px;").arg(Theme::textMuted().name()));
    m_actionHint->setMinimumWidth(230);
    m_clock = new QLabel;
    m_clock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_clock->setFont(Theme::font(9, true));
    m_clock->setStyleSheet(QStringLiteral("color:%1;letter-spacing:1px;").arg(Theme::textMuted().name()));
    m_clock->setMinimumWidth(260);
    layout->addWidget(system);
    layout->addWidget(title, 1);
    layout->addWidget(m_actionHint);
    layout->addWidget(m_clock);
    return header;
}

QWidget *MainWindow::createNavigation()
{
    QWidget *navigation = new QWidget;
    navigation->setFixedHeight(75);
    QHBoxLayout *layout = new QHBoxLayout(navigation);
    layout->setContentsMargins(0, 0, 0, 3);
    layout->setSpacing(26);
    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    const QStringList labels = {QStringLiteral("路径规划"), QStringLiteral("健康监控"), QStringLiteral("视频监控"), QStringLiteral("数据管理")};
    layout->addStretch(1);
    for (int i = 0; i < labels.size(); ++i) {
        NavButton *button = new NavButton(labels.at(i));
        m_navButtons << button;
        m_navGroup->addButton(button, i);
        layout->addWidget(button, 4);
    }
    layout->addStretch(1);
    connect(m_navGroup, QOverload<int>::of(&QButtonGroup::idClicked), this, [this](int id) {
        navigateTo(static_cast<PageId>(id));
    });
    return navigation;
}

void MainWindow::navigateTo(PageId page)
{
    const int index = static_cast<int>(page);
    if (!m_pages || index < 0 || index >= m_pages->count()) return;
    if (m_transitioning) {
        const int active = m_pages->currentIndex();
        if (active >= 0 && active < m_navButtons.size())
            m_navButtons.at(active)->setChecked(true);
        return;
    }
    const int previousIndex = m_pages->currentIndex();
    if (previousIndex == index) {
        if (index < m_navButtons.size()) m_navButtons.at(index)->setChecked(true);
        return;
    }
    QWidget *incoming = m_pages->widget(index);
    m_pages->setCurrentIndex(index);
    if (index < m_navButtons.size()) m_navButtons.at(index)->setChecked(true);
    emit currentPageChanged(page);

    if (!isVisible() || !incoming) return;
    m_transitioning = true;
    for (NavButton *button : m_navButtons)
        button->setEnabled(false);
    const int direction = index > previousIndex ? 1 : -1;
    const QPoint finalPosition = incoming->pos();
    incoming->move(finalPosition + QPoint(direction * 24, 0));
    QGraphicsOpacityEffect *opacityEffect = new QGraphicsOpacityEffect(incoming);
    incoming->setGraphicsEffect(opacityEffect);
    opacityEffect->setOpacity(0.0);

    QParallelAnimationGroup *group = new QParallelAnimationGroup(this);
    QPropertyAnimation *fade = new QPropertyAnimation(opacityEffect, "opacity", group);
    fade->setDuration(250);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    QPropertyAnimation *slide = new QPropertyAnimation(incoming, "pos", group);
    slide->setDuration(250);
    slide->setStartValue(finalPosition + QPoint(direction * 24, 0));
    slide->setEndValue(finalPosition);
    slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(group, &QParallelAnimationGroup::finished, this, [this, incoming, finalPosition, group] {
        incoming->move(finalPosition);
        incoming->setGraphicsEffect(nullptr);
        m_transitioning = false;
        for (NavButton *button : m_navButtons)
            button->setEnabled(true);
        group->deleteLater();
    });
    group->start();
}

void MainWindow::showActionHint(ActionId action)
{
    QString name;
    QString state = QStringLiteral("功能接口已预留");
    switch (action) {
    case ActionId::AddWaypoint: name = QStringLiteral("添加路径点"); break;
    case ActionId::RemoveWaypoint: name = QStringLiteral("删除路径点"); break;
    case ActionId::ClearWaypoints: name = QStringLiteral("清空路径点"); break;
    case ActionId::LoadDefaults: name = QStringLiteral("载入默认参数"); break;
    case ActionId::DrawRoute: name = QStringLiteral("绘制路径"); break;
    case ActionId::SendMission: name = QStringLiteral("发送任务"); break;
    case ActionId::StartVideo: name = QStringLiteral("视频控制"); break;
    case ActionId::SendStationKeeping: name = QStringLiteral("悬停控制"); break;
    case ActionId::StartCollection: name = QStringLiteral("数据采集"); state = QStringLiteral("状态已切换"); break;
    case ActionId::ClearData: name = QStringLiteral("清空数据"); state = QStringLiteral("已完成"); break;
    case ActionId::ExportData: name = QStringLiteral("导出数据"); state = QStringLiteral("已完成"); break;
    case ActionId::ApplyMonitoringSettings: name = QStringLiteral("应用监控设置"); state = QStringLiteral("已应用"); break;
    case ActionId::ApplyControlParameters: name = QStringLiteral("应用控制参数"); break;
    }
    m_actionHint->setText(QStringLiteral("%1 · %2").arg(name, state));
    m_actionHint->setStyleSheet(QStringLiteral("color:%1;letter-spacing:1px;").arg(Theme::iceCyan().name()));
    QTimer::singleShot(1200, m_actionHint, [this] {
        m_actionHint->setText(QStringLiteral("接口层待命 · ACTION BUS READY"));
        m_actionHint->setStyleSheet(QStringLiteral("color:%1;letter-spacing:1px;").arg(Theme::textMuted().name()));
    });
}

MainWindow::PageId MainWindow::currentPage() const
{
    return static_cast<PageId>(m_pages ? m_pages->currentIndex() : 0);
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) showNormal(); else showFullScreen();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F11) {
        toggleFullScreen();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        event->accept();
        return;
    }
    const int count = 4;
    int index = static_cast<int>(currentPage());
    if (event->key() == Qt::Key_Right || event->key() == Qt::Key_PageDown) {
        navigateTo(static_cast<PageId>((index + 1) % count));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_PageUp) {
        navigateTo(static_cast<PageId>((index - 1 + count) % count));
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    EffectController::instance()->start();
    if (m_firstShow) {
        m_firstShow = false;
        const bool windowed = QCoreApplication::arguments().contains(QStringLiteral("--windowed"));
        if (!windowed)
            QTimer::singleShot(0, this, [this] { showFullScreen(); });
    }
}

void MainWindow::hideEvent(QHideEvent *event)
{
    EffectController::instance()->stop();
    QMainWindow::hideEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized())
            EffectController::instance()->stop();
        else if (isVisible())
            EffectController::instance()->start();
    }
    QMainWindow::changeEvent(event);
}
