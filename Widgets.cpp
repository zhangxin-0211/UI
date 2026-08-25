#include "Widgets.h"
#include "EffectController.h"
#include "Theme.h"

#include <QEvent>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

QColor alpha(QColor color, int value)
{
    color.setAlpha(value);
    return color;
}

void drawGlowLine(QPainter &p, const QLineF &line, const QColor &color, qreal width = 1.0)
{
    QColor glow = color;
    glow.setAlpha(35);
    p.setPen(QPen(glow, width + 7.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(line);
    glow.setAlpha(90);
    p.setPen(QPen(glow, width + 3.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(line);
    p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(line);
}

void drawCornerMarks(QPainter &p, const QRectF &r, qreal length)
{
    const QColor c = Theme::border();
    p.setPen(QPen(c, 2.0));
    p.drawLine(QPointF(r.left(), r.top() + length), r.topLeft());
    p.drawLine(r.topLeft(), QPointF(r.left() + length, r.top()));
    p.drawLine(QPointF(r.right() - length, r.top()), r.topRight());
    p.drawLine(r.topRight(), QPointF(r.right(), r.top() + length));
    p.drawLine(QPointF(r.left(), r.bottom() - length), r.bottomLeft());
    p.drawLine(r.bottomLeft(), QPointF(r.left() + length, r.bottom()));
    p.drawLine(QPointF(r.right() - length, r.bottom()), r.bottomRight());
    p.drawLine(r.bottomRight(), QPointF(r.right(), r.bottom() - length));
}

QPainterPath roundedPanel(const QRectF &r, qreal radius)
{
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    return path;
}

}

NeonPanel::NeonPanel(const QString &title, QWidget *parent)
    : QFrame(parent), m_title(title)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setContentsMargins(14, title.isEmpty() ? 14 : 48, 14, 14);
    connect(EffectController::instance(), &EffectController::frameAdvanced, this, [this](qreal phase, qreal) {
        if (!isVisible()) return;
        m_effectPhase = phase;
        update();
    });
}

void NeonPanel::setTitle(const QString &title)
{
    m_title = title;
    setContentsMargins(14, title.isEmpty() ? 14 : 48, 14, 14);
    update();
}

void NeonPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(2, 2, -2, -2);
    QLinearGradient fill = Theme::panelGradient(r);
    p.setPen(QPen(alpha(Theme::accent(), 150), 1.0));
    p.setBrush(fill);
    p.drawRoundedRect(r, 10, 10);

    if (!m_title.isEmpty()) {
        QRectF titleRect(r.left(), r.top(), r.width(), 39);
        QLinearGradient titleFill(titleRect.topLeft(), titleRect.topRight());
        titleFill.setColorAt(0.0, alpha(Theme::panel(), 220));
        titleFill.setColorAt(0.5, alpha(Theme::panelRaised(), 245));
        titleFill.setColorAt(1.0, alpha(Theme::panel(), 220));
        QPainterPath titlePath;
        titlePath.addRoundedRect(titleRect, 9, 9);
        p.fillPath(titlePath, titleFill);
        p.fillRect(QRectF(titleRect.left(), titleRect.bottom() - 9, titleRect.width(), 9), titleFill);
        p.setFont(Theme::font(13, true));
        p.setPen(Theme::titleStart());
        p.drawText(titleRect, Qt::AlignCenter, m_title);
    }
    const qreal travel = std::fmod(m_effectPhase * 0.10, 1.0);
    const qreal highlightX = r.left() + travel * r.width();
    QLinearGradient sweep(highlightX - 70, 0, highlightX + 70, 0);
    sweep.setColorAt(0.0, Qt::transparent);
    QColor cyan = Theme::iceCyan(); cyan.setAlpha(120);
    QColor violet = Theme::plasmaViolet(); violet.setAlpha(90);
    sweep.setColorAt(0.46, cyan);
    sweep.setColorAt(0.54, violet);
    sweep.setColorAt(1.0, Qt::transparent);
    p.setPen(QPen(QBrush(sweep), 1.4));
    p.drawLine(QPointF(r.left() + 14, r.top() + 1), QPointF(r.right() - 14, r.top() + 1));
    drawCornerMarks(p, r, 16);
}

NavButton::NavButton(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(62);
    connect(EffectController::instance(), &EffectController::frameAdvanced, this, [this](qreal phase, qreal) {
        if (!isVisible() || !isChecked()) return;
        m_effectPhase = phase;
        update();
    });
}

QSize NavButton::sizeHint() const { return QSize(260, 64); }

void NavButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(4, 6, -4, -7);
    QPainterPath shape;
    shape.moveTo(r.left(), r.top());
    shape.lineTo(r.right(), r.top());
    shape.cubicTo(r.right() - 3, r.bottom() - 6, r.right() - 30, r.bottom(), r.right() - 64, r.bottom());
    shape.lineTo(r.left() + 64, r.bottom());
    shape.cubicTo(r.left() + 30, r.bottom(), r.left() + 3, r.bottom() - 6, r.left(), r.top());
    shape.closeSubpath();

    QLinearGradient g(r.topLeft(), r.bottomLeft());
    if (isChecked()) {
        g = Theme::activeGradient(r);
    } else if (m_hovered) {
        g.setColorAt(0.0, Theme::panelRaised());
        g.setColorAt(1.0, Theme::panel());
    } else {
        g.setColorAt(0.0, Theme::panelAlt());
        g.setColorAt(1.0, Theme::background());
    }
    p.fillPath(shape, g);
    QColor outline = isChecked() ? Theme::border() : alpha(Theme::accent(), 100);
    p.setPen(QPen(outline, isChecked() ? 1.8 : 1.0));
    p.drawPath(shape);
    if (isChecked())
        drawGlowLine(p, QLineF(r.left() + 55, r.bottom() - 1, r.right() - 55, r.bottom() - 1), Theme::glow(), 2.0);
    if (isChecked()) {
        const qreal x = r.left() + std::fmod(m_effectPhase * 0.32, 1.0) * r.width();
        QLinearGradient scan(x - 45, 0, x + 45, 0);
        QColor navShimmer = Theme::titleStart();
        navShimmer.setAlpha(130);
        scan.setColorAt(0.0, Qt::transparent);
        scan.setColorAt(0.5, navShimmer);
        scan.setColorAt(1.0, Qt::transparent);
        p.setPen(QPen(QBrush(scan), 2.0));
        p.drawLine(QPointF(r.left() + 45, r.top() + 2), QPointF(r.right() - 45, r.top() + 2));
    }

    p.setFont(Theme::font(isChecked() ? 15 : 13, isChecked()));
    p.setPen(isChecked() ? Theme::titleStart() : Theme::text());
    p.drawText(r.adjusted(0, -2, 0, 0), Qt::AlignCenter, text());
}

void NavButton::enterEvent(QEvent *event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void NavButton::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

GaugeWidget::GaugeWidget(const QString &caption, QWidget *parent)
    : QWidget(parent), m_caption(caption), m_unit(QStringLiteral("rad/min"))
{
    setMinimumSize(88, 88);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    connect(EffectController::instance(), &EffectController::frameAdvanced, this, [this](qreal phase, qreal delta) {
        advanceEffects(phase, delta);
    });
}

void GaugeWidget::setRange(double minimum, double maximum) { m_minimum = minimum; m_maximum = maximum; update(); }
void GaugeWidget::setValue(double value)
{
    const double next = qBound(m_minimum, value, m_maximum);
    const double range = qMax(0.001, m_maximum - m_minimum);
    const double speed = qAbs(next - m_value) / range;
    m_previousValue = m_value;
    m_value = next;
    m_activity = qBound<qreal>(0.22, 0.22 + speed * 12.0, 1.0);
    if (speed > 0.055)
        m_orbitParticles.triggerPulse(rect().center(), 7, Theme::plasmaViolet());
    update();
}
void GaugeWidget::setUnit(const QString &unit) { m_unit = unit; update(); }
void GaugeWidget::setCaption(const QString &caption) { m_caption = caption; update(); }
void GaugeWidget::setResetParticlesOnResize(bool enabled)
{
    m_resetParticlesOnResize = enabled;
}
QSize GaugeWidget::sizeHint() const { return QSize(150, 150); }

void GaugeWidget::advanceEffects(qreal phase, qreal deltaSeconds)
{
    if (!isVisible()) return;
    m_effectPhase = phase;
    m_flowRotation = std::fmod(m_flowRotation + deltaSeconds * 38.0, 360.0);
    m_activity = qMax<qreal>(0.22, m_activity * 0.955);
    const QRectF bounds = rect().adjusted(width() * 0.10, height() * 0.08, -width() * 0.10, -height() * 0.15);
    m_fogParticles.update(deltaSeconds, bounds, 0.34 + m_activity * 0.38, phase);
    m_orbitParticles.update(deltaSeconds, bounds, 0.30 + m_activity * 0.58, phase);
    update();
}

void GaugeWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Re-seed particles after layout/scale changes so their first positions
    // are generated from the current 1920x1080 design-space mapping.
    if (m_resetParticlesOnResize) {
        m_fogParticles.clear();
        m_orbitParticles.clear();
    }
    rebuildStaticLayer();
}

void GaugeWidget::mousePressEvent(QMouseEvent *event)
{
    m_fogParticles.triggerPulse(event->pos(), 8, Theme::iceCyan());
    m_orbitParticles.triggerPulse(event->pos(), 8, Theme::plasmaViolet());
    QWidget::mousePressEvent(event);
}

void GaugeWidget::rebuildStaticLayer()
{
    if (size().isEmpty()) return;
    m_staticLayer = QPixmap(size() * devicePixelRatioF());
    m_staticLayer.setDevicePixelRatio(devicePixelRatioF());
    m_staticLayer.fill(Qt::transparent);
    QPainter p(&m_staticLayer);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(width(), height() - 20);
    p.translate(width() / 2.0, (height() - 12) / 2.0);
    p.scale(side / 200.0, side / 200.0);
    QRectF arc(-78, -78, 156, 156);
    p.setPen(QPen(alpha(Theme::glow(), 30), 12, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arc.adjusted(-5, -5, 5, 5), 225 * 16, -270 * 16);
    p.setPen(QPen(alpha(Theme::accent(), 155), 4, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arc, 225 * 16, -270 * 16);
    p.setPen(QPen(alpha(Theme::border(), 125), 1));
    p.drawArc(arc.adjusted(7, 7, -7, -7), 225 * 16, -270 * 16);
    for (int i = 0; i <= 30; ++i) {
        p.save();
        p.rotate(-135.0 + i * 9.0);
        p.setPen(QPen(i % 5 == 0 ? Theme::value() : Theme::text(), i % 5 == 0 ? 2.0 : 1.0));
        p.drawLine(QPointF(0, -67), QPointF(0, i % 5 == 0 ? -56 : -61));
        p.restore();
    }
}

void GaugeWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_staticLayer.isNull()) rebuildStaticLayer();
    p.drawPixmap(0, 0, m_staticLayer);
    m_fogParticles.paint(p, 0.85);

    const qreal side = qMin(width(), height() - 20);
    p.translate(width() / 2.0, (height() - 12) / 2.0);
    p.scale(side / 200.0, side / 200.0);
    QRectF arc(-78, -78, 156, 156);
    p.save();
    p.rotate(m_flowRotation);
    QConicalGradient flow(QPointF(0, 0), 0);
    QColor transparentCyan = Theme::iceCyan();
    transparentCyan.setAlpha(0);
    QColor transparentViolet = Theme::plasmaViolet();
    transparentViolet.setAlpha(0);
    flow.setColorAt(0.0, transparentCyan);
    flow.setColorAt(0.13, Theme::iceCyan());
    flow.setColorAt(0.34, transparentCyan);
    flow.setColorAt(0.66, Theme::plasmaViolet());
    flow.setColorAt(0.82, transparentViolet);
    flow.setColorAt(1.0, transparentCyan);
    p.setPen(QPen(QBrush(flow), 4.2, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arc.adjusted(-3, -3, 3, 3), 225 * 16, -270 * 16);
    p.restore();

    const double ratio = (m_value - m_minimum) / qMax(0.0001, m_maximum - m_minimum);
    p.save();
    const double needleAngle = -135.0 + ratio * 270.0;
    const double previousRatio = (m_previousValue - m_minimum) / qMax(0.0001, m_maximum - m_minimum);
    const double previousAngle = -135.0 + previousRatio * 270.0;
    p.rotate(previousAngle);
    QColor trail = Theme::plasmaViolet(); trail.setAlpha(60);
    p.setPen(QPen(trail, 5.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(0, 6), QPointF(0, -48));
    p.restore();
    p.save();
    p.rotate(needleAngle);
    QLinearGradient needle(0, 0, 0, -55);
    needle.setColorAt(0, Theme::warning());
    needle.setColorAt(1, Theme::value());
    p.setPen(QPen(QBrush(needle), 2.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(0, 8), QPointF(0, -55));
    p.restore();
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::value());
    p.drawEllipse(QPointF(0, 0), 5, 5);

    p.setFont(Theme::font(9));
    p.setPen(Theme::text());
    p.drawText(QRectF(-55, 18, 110, 20), Qt::AlignCenter, m_unit);
    p.setFont(Theme::font(13, true));
    p.setPen(Theme::value());
    p.drawText(QRectF(-70, 37, 140, 24), Qt::AlignCenter, QString::number(m_value, 'f', 0));

    p.resetTransform();
    m_orbitParticles.paint(p, 0.95);
    p.setFont(Theme::font(10));
    p.setPen(Theme::textMuted());
    p.drawText(QRect(0, height() - 24, width(), 20), Qt::AlignCenter, m_caption);
}

CircularProgress::CircularProgress(QWidget *parent) : QWidget(parent) { setMinimumSize(80, 80); }
void CircularProgress::setValue(int value) { m_value = qBound(0, value, 100); update(); }
void CircularProgress::setSubtitle(const QString &subtitle) { m_subtitle = subtitle; update(); }
QSize CircularProgress::sizeHint() const { return QSize(180, 180); }

void CircularProgress::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int side = qMin(width(), height() - 22);
    QRectF r((width() - side) / 2.0 + 14, 12, side - 28, side - 28);
    p.setPen(QPen(alpha(Theme::glow(), 45), 15, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(r);
    p.setPen(QPen(Theme::glow(), 7, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(r, 90 * 16, -m_value * 3.6 * 16);
    p.setFont(Theme::font(18, true));
    p.setPen(Theme::titleStart());
    p.drawText(r, Qt::AlignCenter, QString::number(m_value) + QStringLiteral("%"));
    p.setFont(Theme::font(10));
    p.setPen(Theme::textMuted());
    p.drawText(QRect(0, height() - 28, width(), 22), Qt::AlignCenter, m_subtitle);
}

StatusLamp::StatusLamp(const QString &text, State state, QWidget *parent)
    : QWidget(parent), m_text(text), m_state(state) { setMinimumHeight(28); }
void StatusLamp::setState(State state) { m_state = state; update(); }
QSize StatusLamp::sizeHint() const { return QSize(130, 38); }

void StatusLamp::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c = Theme::textMuted();
    if (m_state == Online) c = Theme::glow();
    else if (m_state == Warning) c = Theme::warning();
    else if (m_state == Fault) c = Theme::fault();
    QRadialGradient g(QPointF(18, height() / 2.0), 13);
    g.setColorAt(0.0, c.lighter(160));
    QColor fade = c; fade.setAlpha(40);
    g.setColorAt(1.0, fade);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(QPointF(18, height() / 2.0), 13, 13);
    p.setBrush(c);
    p.drawEllipse(QPointF(18, height() / 2.0), 5.5, 5.5);
    p.setFont(Theme::font(10));
    p.setPen(Theme::text());
    p.drawText(QRect(37, 0, width() - 37, height()), Qt::AlignVCenter | Qt::AlignLeft, m_text);
}

LineChart::LineChart(const QString &title, QWidget *parent) : QWidget(parent), m_title(title)
{
    setMinimumSize(220, 140);
    for (int i = 0; i < 48; ++i) {
        m_first << qSin(i * 0.18) * 0.5 + qCos(i * 0.07) * 0.35;
        m_second << qCos(i * 0.14) * 0.6 - qSin(i * 0.08) * 0.3;
    }
}
void LineChart::setTitle(const QString &title) { m_title = title; update(); }
void LineChart::setSeries(const QVector<double> &first, const QVector<double> &second) { m_first = first; m_second = second; update(); }
void LineChart::setCompact(bool compact)
{
    if (m_compact == compact) return;
    m_compact = compact;
    setMinimumSize(compact ? QSize(180, 76) : QSize(220, 140));
    updateGeometry();
    update();
}
void LineChart::append(double first, double second, int limit)
{
    m_first << first; m_second << second;
    while (m_first.size() > limit) m_first.remove(0);
    while (m_second.size() > limit) m_second.remove(0);
    update();
}
QSize LineChart::sizeHint() const { return m_compact ? QSize(320, 96) : QSize(420, 235); }

void LineChart::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF outer = rect().adjusted(1, 1, -1, -1);
    QLinearGradient bg(outer.topLeft(), outer.bottomRight());
    bg.setColorAt(0, alpha(Theme::panelAlt(), 220));
    bg.setColorAt(1, alpha(Theme::background(), 235));
    p.setPen(QPen(alpha(Theme::accent(), 90), 1));
    p.setBrush(bg);
    p.drawRoundedRect(outer, 9, 9);
    const qreal leftInset = m_compact ? 28.0 : 42.0;
    const qreal titleInset = m_title.isEmpty() ? (m_compact ? 8.0 : 15.0) : (m_compact ? 25.0 : 38.0);
    const qreal bottomInset = m_compact ? 18.0 : 28.0;
    QRectF graph = outer.adjusted(leftInset, titleInset, -10, -bottomInset);
    p.setPen(QPen(Theme::grid(), 1));
    const int horizontalDivisions = m_compact ? 3 : 5;
    const int verticalDivisions = m_compact ? 6 : 8;
    for (int i = 0; i <= horizontalDivisions; ++i) {
        const qreal y = graph.top() + graph.height() * i / horizontalDivisions;
        p.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
    }
    for (int i = 0; i <= verticalDivisions; ++i) {
        const qreal x = graph.left() + graph.width() * i / verticalDivisions;
        p.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
    }
    if (!m_title.isEmpty()) {
        p.setFont(Theme::font(m_compact ? 9 : 11, true));
        p.setPen(Theme::text());
        p.drawText(QRectF(m_compact ? 10 : 14, m_compact ? 3 : 8,
                          width() - (m_compact ? 20 : 28), m_compact ? 19 : 24),
                   Qt::AlignLeft | Qt::AlignVCenter, m_title);
    }
    auto drawSeries = [&](const QVector<double> &values, const QColor &color) {
        if (values.size() < 2) return;
        double lo = -1.8, hi = 1.8;
        QPainterPath path;
        for (int i = 0; i < values.size(); ++i) {
            qreal x = graph.left() + graph.width() * i / qMax(1, values.size() - 1);
            qreal y = graph.bottom() - graph.height() * ((values.at(i) - lo) / (hi - lo));
            y = qBound(graph.top(), y, graph.bottom());
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        QColor glow = color; glow.setAlpha(35);
        p.setPen(QPen(glow, m_compact ? 5.5 : 8.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(QPen(color, m_compact ? 1.8 : 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    };
    p.save();
    p.setClipRect(graph.adjusted(-6, -6, 6, 6));
    drawSeries(m_first, Theme::curveMint());
    drawSeries(m_second, Theme::curveCoral());
    p.restore();
    p.setFont(Theme::font(m_compact ? 7 : 8));
    p.setPen(Theme::textMuted());
    p.drawText(QRectF(graph.left(), graph.bottom() + (m_compact ? 1 : 5), graph.width(), m_compact ? 14 : 18),
               Qt::AlignCenter, QStringLiteral("实时数据 · TIME"));
}

MapPlanningWidget::MapPlanningWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(520, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(EffectController::instance(), &EffectController::frameAdvanced, this, [this](qreal phase, qreal) {
        if (!isVisible()) return;
        m_effectPhase = phase;
        update();
    });
}

void MapPlanningWidget::setWaypoints(const QVector<RoutePoint> &points)
{
    m_waypoints = points;
    update();
}

void MapPlanningWidget::setPlannedPath(const RoutePath &path)
{
    m_plannedPath = path;
    update();
}

void MapPlanningWidget::setMapReady(bool ready)
{
    m_mapReady = ready;
    update();
}

void MapPlanningWidget::clearOverlays()
{
    m_waypoints.clear();
    m_plannedPath = RoutePath();
    update();
}

QSize MapPlanningWidget::sizeHint() const { return QSize(980, 690); }

void MapPlanningWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF outer = rect().adjusted(2, 2, -2, -2);
    QLinearGradient background(outer.topLeft(), outer.bottomRight());
    background.setColorAt(0.0, Theme::backgroundDeep());
    background.setColorAt(0.52, Theme::panel());
    background.setColorAt(1.0, Theme::panelAlt());
    p.setPen(QPen(alpha(Theme::border(), 125), 1.3));
    p.setBrush(background);
    p.drawRoundedRect(outer, 12, 12);

    QPainterPath clip;
    clip.addRoundedRect(outer.adjusted(1, 1, -1, -1), 11, 11);
    p.setClipPath(clip);
    const QRectF mapArea = outer.adjusted(34, 54, -34, -36);

    const int fineGrid = qMax(28, qRound(qMin(mapArea.width(), mapArea.height()) / 14.0));
    QColor fine = Theme::accent();
    fine.setAlpha(22);
    p.setPen(QPen(fine, 1));
    for (qreal x = mapArea.left(); x <= mapArea.right(); x += fineGrid)
        p.drawLine(QPointF(x, mapArea.top()), QPointF(x, mapArea.bottom()));
    for (qreal y = mapArea.top(); y <= mapArea.bottom(); y += fineGrid)
        p.drawLine(QPointF(mapArea.left(), y), QPointF(mapArea.right(), y));

    QColor major = Theme::glow();
    major.setAlpha(36);
    p.setPen(QPen(major, 1));
    for (qreal x = mapArea.left(); x <= mapArea.right(); x += fineGrid * 4)
        p.drawLine(QPointF(x, mapArea.top()), QPointF(x, mapArea.bottom()));
    for (qreal y = mapArea.top(); y <= mapArea.bottom(); y += fineGrid * 4)
        p.drawLine(QPointF(mapArea.left(), y), QPointF(mapArea.right(), y));

    p.setFont(Theme::font(8));
    p.setPen(alpha(Theme::textMuted(), 145));
    int xIndex = 0;
    for (qreal x = mapArea.left(); x <= mapArea.right(); x += fineGrid * 4, ++xIndex)
        p.drawText(QRectF(x + 4, mapArea.bottom() - 18, 72, 16), QStringLiteral("GRID %1").arg(xIndex, 2, 10, QLatin1Char('0')));
    int yIndex = 0;
    for (qreal y = mapArea.top(); y <= mapArea.bottom(); y += fineGrid * 4, ++yIndex)
        p.drawText(QRectF(mapArea.left() + 5, y + 3, 58, 16), QStringLiteral("%1").arg(yIndex, 2, 10, QLatin1Char('0')));

    const qreal scanY = mapArea.top() + std::fmod(m_effectPhase * 21.0, qMax<qreal>(1.0, mapArea.height()));
    QLinearGradient scan(0, scanY - 32, 0, scanY + 32);
    QColor transparentCyan = Theme::glow();
    transparentCyan.setAlpha(0);
    QColor scanPeak = Theme::iceCyan();
    scanPeak.setAlpha(34);
    scan.setColorAt(0.0, transparentCyan);
    scan.setColorAt(0.5, scanPeak);
    scan.setColorAt(1.0, transparentCyan);
    p.fillRect(QRectF(mapArea.left(), scanY - 32, mapArea.width(), 64), scan);

    const QPointF center = mapArea.center();
    QColor crosshair = Theme::iceCyan();
    crosshair.setAlpha(85);
    p.setPen(QPen(crosshair, 1));
    p.drawEllipse(center, 32, 32);
    p.drawEllipse(center, 7, 7);
    p.drawLine(QPointF(center.x() - 58, center.y()), QPointF(center.x() - 12, center.y()));
    p.drawLine(QPointF(center.x() + 12, center.y()), QPointF(center.x() + 58, center.y()));
    p.drawLine(QPointF(center.x(), center.y() - 58), QPointF(center.x(), center.y() - 12));
    p.drawLine(QPointF(center.x(), center.y() + 12), QPointF(center.x(), center.y() + 58));

    p.setClipping(false);
    QLinearGradient header(outer.topLeft(), outer.topRight());
    header.setColorAt(0.0, alpha(Theme::panel(), 230));
    header.setColorAt(0.52, alpha(Theme::panelRaised(), 245));
    header.setColorAt(1.0, alpha(Theme::panel(), 230));
    p.fillRect(QRectF(outer.left() + 1, outer.top() + 1, outer.width() - 2, 42), header);
    p.setFont(Theme::font(13, true));
    p.setPen(Theme::titleStart());
    p.drawText(QRectF(18, 7, width() - 36, 28), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("地图与路径规划 / MAP & ROUTE PLANNING"));

    const QString connection = m_mapReady ? QStringLiteral("MAP PROVIDER · ONLINE")
                                           : QStringLiteral("MAP PROVIDER · STANDBY");
    p.setFont(Theme::font(8, true));
    p.setPen(m_mapReady ? Theme::glow() : Theme::warning());
    p.drawText(QRectF(width() - 245, 9, 220, 24), Qt::AlignRight | Qt::AlignVCenter, connection);

    QRectF messageRect(center.x() - qMin<qreal>(330, mapArea.width() * 0.38),
                       center.y() - 48,
                       qMin<qreal>(660, mapArea.width() * 0.76), 96);
    QLinearGradient messageFill(messageRect.topLeft(), messageRect.bottomRight());
    messageFill.setColorAt(0.0, alpha(Theme::backgroundDeep(), 220));
    messageFill.setColorAt(1.0, alpha(Theme::panelAlt(), 210));
    p.setPen(QPen(alpha(Theme::border(), 100), 1));
    p.setBrush(messageFill);
    p.drawRoundedRect(messageRect, 8, 8);
    p.setFont(Theme::font(15, true));
    p.setPen(Theme::value());
    p.drawText(messageRect.adjusted(12, 9, -12, -36), Qt::AlignCenter,
               QStringLiteral("高德地图与路径规划接口待接入"));
    p.setFont(Theme::font(9));
    p.setPen(Theme::textMuted());
    p.drawText(messageRect.adjusted(12, 44, -12, -8), Qt::AlignCenter,
               QStringLiteral("AMAP ADAPTER  ·  WAYPOINT LAYER  ·  ALGORITHM BRIDGE"));

    p.setFont(Theme::font(8));
    p.setPen(Theme::textMuted());
    p.drawText(QRectF(20, height() - 29, width() - 40, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("坐标系统：未指定  ·  路径点缓存：%1  ·  规划结果：%2")
                   .arg(m_waypoints.size())
                   .arg(m_plannedPath.valid ? QStringLiteral("READY") : QStringLiteral("STANDBY")));
    drawCornerMarks(p, outer.adjusted(7, 7, -7, -7), 24);
}

VideoPlaceholder::VideoPlaceholder(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(480, 300);
    connect(EffectController::instance(), &EffectController::frameAdvanced, this, [this](qreal phase, qreal delta) {
        if (!isVisible()) return;
        m_effectPhase = phase;
        const QRectF bounds = rect().adjusted(30, 30, -30, -30);
        m_signalParticles.update(delta, bounds, m_playing ? 0.72 : 0.28, phase);
        update();
    });
}
void VideoPlaceholder::setPlaying(bool playing) { m_playing = playing; update(); }

void VideoPlaceholder::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(2, 2, -2, -2);
    QLinearGradient bg(r.topLeft(), r.bottomRight());
    bg.setColorAt(0, Theme::panelAlt());
    bg.setColorAt(1, Theme::backgroundDeep());
    p.setPen(QPen(alpha(Theme::border(), 110), 1.2));
    p.setBrush(bg);
    p.drawRoundedRect(r, 26, 26);
    p.setClipPath(roundedPanel(r, 26));
    p.setPen(QPen(alpha(Theme::accent(), 28), 1));
    const int spacing = 34;
    for (int x = 0; x < width(); x += spacing) p.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += spacing) p.drawLine(0, y, width(), y);
    const qreal scanY = std::fmod(m_effectPhase * (m_playing ? 70.0 : 28.0), qMax(1, height()));
    QLinearGradient scanGradient(0, scanY - 18, 0, scanY + 18);
    QColor transparentGlow = Theme::glow();
    transparentGlow.setAlpha(0);
    QColor scanHighlight = Theme::iceCyan();
    scanHighlight.setAlpha(m_playing ? 120 : 55);
    QColor transparentViolet = Theme::plasmaViolet();
    transparentViolet.setAlpha(0);
    scanGradient.setColorAt(0.0, transparentGlow);
    scanGradient.setColorAt(0.5, scanHighlight);
    scanGradient.setColorAt(1.0, transparentViolet);
    p.fillRect(QRectF(0, scanY - 18, width(), 36), scanGradient);
    p.setPen(QPen(alpha(Theme::plasmaViolet(), m_playing ? 60 : 25), 1));
    for (int i = 0; i < 3; ++i) {
        QPainterPath wave;
        for (int x = 0; x <= width(); x += 18) {
            const qreal y = height() * (0.27 + i * 0.18) + qSin(x * 0.018 + m_effectPhase * (0.7 + i * 0.18)) * (7 + i * 3);
            if (x == 0) wave.moveTo(x, y); else wave.lineTo(x, y);
        }
        p.drawPath(wave);
    }
    p.setClipping(false);
    m_signalParticles.paint(p, m_playing ? 0.9 : 0.42);
    p.setFont(Theme::font(16, true));
    p.setPen(m_playing ? Theme::glow() : Theme::textMuted());
    p.drawText(r, Qt::AlignCenter, m_playing ? QStringLiteral("VIDEO STREAM · ONLINE") : QStringLiteral("视频信号等待接入"));
    p.setFont(Theme::font(9));
    p.drawText(r.adjusted(0, 52, 0, 0), Qt::AlignCenter, m_playing ? QStringLiteral("CAP 0  |  1920 × 1080  |  30 FPS") : QStringLiteral("SELECT CAMERA AND START"));
    drawCornerMarks(p, r.adjusted(12, 12, -12, -12), 22);
}

GamepadWidget::GamepadWidget(QWidget *parent) : QWidget(parent) { setMinimumSize(240, 132); }
QSize GamepadWidget::sizeHint() const { return QSize(390, 230); }

void GamepadWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(width() / 2.0, height() / 2.0);
    const qreal s = qMin(width() / 400.0, height() / 270.0);
    p.scale(s, s);
    QPainterPath body;
    body.moveTo(-150, -75);
    body.cubicTo(-205, -60, -210, 105, -155, 115);
    body.cubicTo(-120, 92, -92, 50, -55, 50);
    body.lineTo(55, 50);
    body.cubicTo(92, 50, 120, 92, 155, 115);
    body.cubicTo(210, 105, 205, -60, 150, -75);
    body.cubicTo(100, -93, 80, -70, 55, -73);
    body.lineTo(-55, -73);
    body.cubicTo(-80, -70, -100, -93, -150, -75);
    body.closeSubpath();
    QLinearGradient g(0, -90, 0, 120);
    g.setColorAt(0, Theme::panelRaised().lighter(115));
    g.setColorAt(1, Theme::panel());
    p.setBrush(g);
    p.setPen(QPen(alpha(Theme::border(), 130), 2));
    p.drawPath(body);
    p.setPen(QPen(Theme::glow(), 4));
    p.drawLine(-112, -16, -58, -16);
    p.drawLine(-85, -43, -85, 11);
    auto button = [&](QPointF c, const QString &label) {
        p.setPen(QPen(alpha(Theme::border(), 150), 2));
        p.setBrush(Theme::background());
        p.drawEllipse(c, 16, 16);
        p.setFont(Theme::font(8, true));
        p.setPen(Theme::text());
        p.drawText(QRectF(c.x()-14, c.y()-14, 28, 28), Qt::AlignCenter, label);
    };
    button(QPointF(98, -37), QStringLiteral("上"));
    button(QPointF(128, -12), QStringLiteral("右"));
    button(QPointF(68, -12), QStringLiteral("左"));
    button(QPointF(98, 13), QStringLiteral("下"));
    button(QPointF(-138, -53), QStringLiteral("开"));
    button(QPointF(34, 18), QStringLiteral("关"));
}

CompassWidget::CompassWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(280, 280);
    connect(EffectController::instance(), &EffectController::frameAdvanced, this, [this](qreal phase, qreal delta) {
        advanceEffects(phase, delta);
    });
}
void CompassWidget::setHeading(double heading)
{
    const qreal delta = qAbs(heading - m_heading);
    m_previousHeading = m_heading;
    m_heading = heading;
    m_activity = qBound<qreal>(0.22, 0.22 + delta * 0.08, 1.0);
    if (delta > 5.5)
        m_orbitParticles.triggerPulse(rect().center(), 20, Theme::plasmaViolet());
    update();
}
QSize CompassWidget::sizeHint() const { return QSize(430, 430); }

void CompassWidget::advanceEffects(qreal phase, qreal deltaSeconds)
{
    if (!isVisible()) return;
    m_effectPhase = phase;
    m_flowRotation = std::fmod(m_flowRotation + deltaSeconds * 25.0, 360.0);
    m_activity = qMax<qreal>(0.24, m_activity * 0.965);
    const QRectF bounds = rect().adjusted(width() * 0.07, height() * 0.07, -width() * 0.07, -height() * 0.07);
    m_fogParticles.update(deltaSeconds, bounds, 0.52 + m_activity * 0.35, phase);
    m_orbitParticles.update(deltaSeconds, bounds, 0.48 + m_activity * 0.52, phase);
    update();
}

void CompassWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Do not retain particles created before the final window scale/layout.
    m_fogParticles.clear();
    m_orbitParticles.clear();
    rebuildStaticLayer();
}

void CompassWidget::mousePressEvent(QMouseEvent *event)
{
    m_fogParticles.triggerPulse(event->pos(), 24, Theme::iceCyan());
    m_orbitParticles.triggerPulse(event->pos(), 26, Theme::plasmaViolet());
    QWidget::mousePressEvent(event);
}

void CompassWidget::rebuildStaticLayer()
{
    if (size().isEmpty()) return;
    m_staticLayer = QPixmap(size() * devicePixelRatioF());
    m_staticLayer.setDevicePixelRatio(devicePixelRatioF());
    m_staticLayer.fill(Qt::transparent);
    QPainter p(&m_staticLayer);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(width(), height());
    p.translate(width()/2.0, height()/2.0);
    p.scale(side/460.0, side/460.0);
    for (int ring = 0; ring < 4; ++ring) {
        QColor c = ring == 2 ? Theme::plasmaViolet() : Theme::glow();
        c.setAlpha(155 - ring * 24);
        p.setPen(QPen(c, ring == 1 ? 5 : 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(0,0), 170 + ring*12, 170 + ring*12);
    }
    for (int i = 0; i < 72; ++i) {
        p.save();
        p.rotate(i * 5.0);
        p.setPen(QPen(i % 9 == 0 ? Theme::value() : Theme::text(), i % 9 == 0 ? 2.2 : 1.0));
        p.drawLine(QPointF(0,-166), QPointF(0, i % 9 == 0 ? -145 : -157));
        p.restore();
    }
}

void CompassWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_staticLayer.isNull()) rebuildStaticLayer();
    p.drawPixmap(0, 0, m_staticLayer);
    m_fogParticles.paint(p, 0.78);
    const qreal side = qMin(width(), height());
    p.translate(width()/2.0, height()/2.0);
    p.scale(side/460.0, side/460.0);
    p.save();
    p.rotate(m_flowRotation);
    QConicalGradient energy(QPointF(0, 0), 0);
    QColor transparentGlow = Theme::glow();
    transparentGlow.setAlpha(0);
    QColor transparentCyan = Theme::iceCyan();
    transparentCyan.setAlpha(0);
    QColor transparentPlasma = Theme::plasmaLight();
    transparentPlasma.setAlpha(0);
    energy.setColorAt(0.0, transparentGlow);
    energy.setColorAt(0.12, Theme::iceCyan());
    energy.setColorAt(0.30, transparentCyan);
    energy.setColorAt(0.54, Theme::plasmaViolet());
    energy.setColorAt(0.72, transparentPlasma);
    energy.setColorAt(1.0, transparentGlow);
    p.setPen(QPen(QBrush(energy), 6, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(QPointF(0, 0), 190, 190);
    p.setPen(QPen(QBrush(energy), 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(QPointF(0, 0), 207, 207);
    p.restore();
    const qreal pulse = 0.5 + qSin(m_effectPhase * 2.1) * 0.5;
    QColor plasma = Theme::plasmaViolet(); plasma.setAlpha(38 + qRound(pulse * 45));
    p.setPen(QPen(plasma, 12));
    p.drawArc(QRectF(-183, -183, 366, 366), 198 * 16, 72 * 16);
    p.save();
    p.rotate(m_previousHeading);
    QColor trail = Theme::plasmaViolet(); trail.setAlpha(55);
    p.setPen(QPen(trail, 8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(0, 18), QPointF(0, -112));
    p.restore();
    p.save();
    p.rotate(m_heading);
    QPainterPath needle;
    needle.moveTo(0,-125); needle.lineTo(-11,25); needle.lineTo(0,13); needle.lineTo(11,25); needle.closeSubpath();
    QLinearGradient needleGradient(0, 28, 0, -125);
    needleGradient.setColorAt(0.0, Theme::plasmaViolet());
    needleGradient.setColorAt(0.58, Theme::iceCyan());
    needleGradient.setColorAt(1.0, Theme::value());
    p.setBrush(needleGradient); p.setPen(Qt::NoPen); p.drawPath(needle);
    p.restore();
    p.setBrush(Theme::glow()); p.drawEllipse(QPointF(0,0), 8, 8);
    p.setFont(Theme::font(15, true)); p.setPen(Theme::titleStart());
    p.drawText(QRectF(-90, 48, 180, 30), Qt::AlignCenter, QString::number(m_heading, 'f', 1) + QStringLiteral("°"));
    p.setFont(Theme::font(9)); p.setPen(Theme::textMuted());
    p.drawText(QRectF(-90, 78, 180, 24), Qt::AlignCenter, QStringLiteral("航向角 / HEADING"));
    p.resetTransform();
    m_orbitParticles.paint(p, 1.0);
}
