#include "MainWindow.h"
#include "LocationController.h"
#include "DemoDataModel.h"
#include "EffectController.h"
#include "Pages.h"
#include "Theme.h"
#include "Widgets.h"
#include "MissionTypes.h"
#include "RoutePlanner.h"

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
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#endif

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

class RouteTransitionOverlay : public QWidget
{
public:
    explicit RouteTransitionOverlay(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setOverlayOpacity(qreal opacity)
    {
        m_opacity = qBound<qreal>(0.0, opacity, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QColor cover = Theme::backgroundDeep();
        cover.setAlphaF(m_opacity);
        painter.fillRect(rect(), cover);
    }

private:
    qreal m_opacity = 1.0;
};

class MapPrewarmCover : public QWidget
{
public:
    explicit MapPrewarmCover(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Theme::backgroundGradient(rect()));
        QColor grid = Theme::accent(); grid.setAlpha(18);
        painter.setPen(QPen(grid, 1));
        const int spacing = qMax(36, width() / 42);
        for (int x = 0; x < width(); x += spacing) painter.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += spacing) painter.drawLine(0, y, width(), y);
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
    qRegisterMetaType<LocationSource>("LocationSource");
    qRegisterMetaType<LocationFix>("LocationFix");
    qRegisterMetaType<GeoReference>("GeoReference");
    qRegisterMetaType<WaterwayGrid>("WaterwayGrid");
    qRegisterMetaType<WaterwayRecognitionRequest>("WaterwayRecognitionRequest");
    qRegisterMetaType<WaterwayRecognitionResult>("WaterwayRecognitionResult");
    qRegisterMetaType<RoutePlanningRequest>("RoutePlanningRequest");
    qRegisterMetaType<RoutePlanningResult>("RoutePlanningResult");
    qRegisterMetaType<PreparedMission>("PreparedMission");
    qRegisterMetaType<MissionState>("MissionState");
    qRegisterMetaType<RobotTelemetry>("RobotTelemetry");
    qRegisterMetaType<RobotLinkState>("RobotLinkState");
    qRegisterMetaType<RobotEndpoint>("RobotEndpoint");
    qRegisterMetaType<QVector<QVector<QPointF>>>("QVector<QVector<QPointF>>");
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
    m_routePage = new RoutePage(m_model);
    m_locationController = new LocationController(this);
    // Register the supplied Hybrid A* adapter for formal planning. The UI
    // still gates sending until a real protocol codec is registered.
    m_routePage->setExternalRoutePlanner(std::make_shared<HybridAstarRoutePlanner>());
    connect(m_routePage, &RoutePage::amapLocationReceived,
            m_locationController, &LocationController::updateAmapLocation);
    connect(m_routePage, &RoutePage::amapLocationFailed,
            m_locationController, &LocationController::reportAmapFailure);
    connect(m_locationController, &LocationController::currentLocationChanged,
            m_routePage, &RoutePage::setCurrentLocation);
    connect(m_locationController, &LocationController::statusChanged,
            m_routePage, &RoutePage::setLocationStatus);
    pageList << new HealthPage(m_model)
             << m_routePage
             << new VideoPage(m_model)
             << new SonarPage
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

    // 独立的地图宿主位于页面堆栈之后，WebEngine 可在后台持续渲染；
    // 路径页激活时仅提升该宿主层，不重新创建或显示整页 WebView。
    m_mapHost = new QWidget(central);
    m_mapHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_mapHost->lower();
    m_mapHost->show();
    m_mapPrewarmCover = new MapPrewarmCover(central);
    m_mapPrewarmCover->setGeometry(m_pages->geometry());
    m_mapPrewarmCover->show();
    m_mapPrewarmCover->raise();
    m_pages->raise();
    m_routePage->setMapHost(m_mapHost);
    QTimer::singleShot(0, this, [this] {
        if (m_routePage) m_routePage->setMapHost(m_mapHost);
        if (m_mapPrewarmCover && m_pages)
            m_mapPrewarmCover->setGeometry(m_pages->geometry());
    });

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
    const QStringList labels = {QStringLiteral("健康监控"), QStringLiteral("路径规划"), QStringLiteral("视频监控"),
                                QStringLiteral("三维声纳"), QStringLiteral("数据管理")};
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
    if (page == PageId::RoutePlanning && m_routePage)
        m_routePage->warmUpMap();
    if (previousIndex == index) {
        if (index < m_navButtons.size()) m_navButtons.at(index)->setChecked(true);
        if (isVisible() && !isMinimized())
            EffectController::instance()->start();
        return;
    }
    QWidget *incoming = m_pages->widget(index);
    m_pages->setCurrentIndex(index);
    if (m_routePage) m_routePage->setPageActive(page == PageId::RoutePlanning);
    if (index < m_navButtons.size()) m_navButtons.at(index)->setChecked(true);
    emit currentPageChanged(page);
    if (isVisible() && !isMinimized())
        EffectController::instance()->start();

    if (m_mapHost && m_mapPrewarmCover) {
        if (page == PageId::RoutePlanning) {
            m_mapPrewarmCover->hide();
            m_mapHost->raise();
        } else {
            m_mapHost->lower();
            m_mapPrewarmCover->show();
            m_mapPrewarmCover->raise();
            m_pages->raise();
        }
    }

    if (!isVisible() || !incoming) return;
    m_transitioning = true;
    for (NavButton *button : m_navButtons)
        button->setEnabled(false);

    // WebEngine remains in its independent host; this animation only affects
    // the route page's Qt chrome and content, matching every other page
    // without forcing the map through an off-screen opacity composition.
    const int direction = index > previousIndex ? 1 : -1;
    const QPoint finalPosition = incoming->pos();
    incoming->move(finalPosition + QPoint(direction * 24, 0));

    // The map WebView is hosted outside QStackedWidget so it can stay warm in
    // the background.  When the route page slides in, move that host in sync
    // with the page; otherwise the page chrome and map briefly have different
    // horizontal origins (the map appears shifted depending on slide direction).
    const bool animateMapHost = page == PageId::RoutePlanning && m_mapHost
        && m_mapHost->isVisible() && !m_mapPrewarmCover->isVisible();
    const QPoint mapHostFinalPosition = animateMapHost ? m_mapHost->pos() : QPoint();
    if (animateMapHost)
        m_mapHost->move(mapHostFinalPosition + QPoint(direction * 24, 0));

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
    QPropertyAnimation *mapSlide = nullptr;
    if (animateMapHost) {
        mapSlide = new QPropertyAnimation(m_mapHost, "pos", group);
        mapSlide->setDuration(250);
        mapSlide->setStartValue(mapHostFinalPosition + QPoint(direction * 24, 0));
        mapSlide->setEndValue(mapHostFinalPosition);
        mapSlide->setEasingCurve(QEasingCurve::OutCubic);
    }
    connect(group, &QParallelAnimationGroup::finished, this, [this, incoming, finalPosition, group] {
        incoming->move(finalPosition);
        if (m_mapHost && currentPage() == PageId::RoutePlanning)
            syncMapOverlayGeometry();
        incoming->setGraphicsEffect(nullptr);
        m_transitioning = false;
        for (NavButton *button : m_navButtons)
            button->setEnabled(true);
        group->deleteLater();
    });
    group->start();
}

void MainWindow::updateGpsLocation(const LocationFix &fix)
{
    if (m_locationController) m_locationController->updateGpsLocation(fix);
}

void MainWindow::setGpsAvailable(bool available)
{
    if (m_locationController) m_locationController->setGpsAvailable(available);
}

void MainWindow::setExternalRoutePlanner(const std::shared_ptr<IRoutePlanner> &planner)
{
    if (m_routePage) m_routePage->setExternalRoutePlanner(planner);
}

void MainWindow::setRobotProtocolCodec(const std::shared_ptr<IRobotProtocolCodec> &codec)
{
    if (m_routePage) m_routePage->setRobotProtocolCodec(codec);
}

void MainWindow::showActionHint(ActionId action)
{
    QString name;
    QString state = QStringLiteral("功能接口已预留");
    switch (action) {
    case ActionId::AddWaypoint: name = QStringLiteral("设置任务目标"); break;
    case ActionId::DrawRoute: name = QStringLiteral("规划任务路线"); break;
    case ActionId::SendMission: name = QStringLiteral("上传任务路线"); break;
    case ActionId::StartVideo: name = QStringLiteral("摄像头控制"); state = QStringLiteral("状态已切换"); break;
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

void MainWindow::applyWindowChrome()
{
#ifdef Q_OS_WIN
    // 保留系统标题栏的拖动、最小化和关闭按钮，仅将其颜色统一到深海蓝主题。
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;
    // 使用可辨识的深海墨蓝，而不是接近黑色的背景色；系统标题栏本身
    // 只能接受纯色，因此取主题渐变中部的蓝色作为统一基色。
    const COLORREF caption = RGB(16, 42, 80);   // #102A50
    const COLORREF text = RGB(184, 243, 255);   // #B8F3FF
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
#endif
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
    const int count = m_pages ? m_pages->count() : 0;
    if (count <= 0) {
        QMainWindow::keyPressEvent(event);
        return;
    }
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

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    syncMapOverlayGeometry();
}

void MainWindow::syncMapOverlayGeometry()
{
    if (!m_pages) return;
    // The map host and cover are siblings of the stacked widget. Keep both
    // aligned to the post-layout page rectangle after fullscreen/windowed
    // transitions, where a single resize event may precede layout activation.
    const QRect pageRect = m_pages->geometry();
    const bool routeActive = currentPage() == PageId::RoutePlanning;
    QRect coverRect = pageRect;
    // While leaving fullscreen, the hidden route page can still report its old
    // full-screen map rectangle for one layout turn. Cover that stale rectangle
    // too, so no WebEngine pixels can flash below the new windowed page bounds.
    if (!routeActive && m_mapHost)
        coverRect = coverRect.united(m_mapHost->geometry());
    if (m_mapPrewarmCover)
        m_mapPrewarmCover->setGeometry(coverRect);
    if (m_mapHost)
        m_mapHost->setGeometry(pageRect);
    if (m_routePage)
        m_routePage->syncMapHostGeometry();
    if (!routeActive && m_mapHost && m_mapPrewarmCover) {
        m_mapHost->lower();
        m_mapPrewarmCover->show();
        m_mapPrewarmCover->raise();
        m_pages->raise();
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    applyWindowChrome();
    EffectController::instance()->start();
    if (m_firstShow) {
        m_firstShow = false;
        QTimer::singleShot(0, this, [this] {
            if (m_routePage) m_routePage->warmUpMap();
        });
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
        QTimer::singleShot(0, this, [this] { syncMapOverlayGeometry(); });
        QTimer::singleShot(50, this, [this] { syncMapOverlayGeometry(); });
    }
    QMainWindow::changeEvent(event);
}
