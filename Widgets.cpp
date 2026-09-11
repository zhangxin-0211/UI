#include "Widgets.h"
#include "EffectController.h"
#include "GeoCoordinateUtils.h"
#include "Theme.h"
#include <QSettings>
#include <QStandardPaths>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWebEngineSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <QEvent>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

constexpr double kWebMercatorMaximumLatitude = 85.05112878;
constexpr double kMapPi = 3.14159265358979323846;
constexpr double kEquatorialMetersPerPixel = 156543.03392804097;

struct ScaleRulerSpec {
    double meters = 0.0;
    int pixels = 0;
    QString text;
};

ScaleRulerSpec scaleRulerFor(double latitudeDegrees, int zoom)
{
    const double metersPerPixel = kEquatorialMetersPerPixel
        * std::cos(qDegreesToRadians(qBound(-kWebMercatorMaximumLatitude,
                                             latitudeDegrees,
                                             kWebMercatorMaximumLatitude)))
        / std::pow(2.0, qMax(0, zoom));
    if (!qIsFinite(metersPerPixel) || metersPerPixel <= 0.0) return {};

    constexpr double desiredPixels = 132.0;
    const double desiredMeters = desiredPixels * metersPerPixel;
    const double magnitude = std::pow(10.0, std::floor(std::log10(desiredMeters)));
    double meters = magnitude;
    for (const double multiplier : {5.0, 2.0, 1.0}) {
        const double candidate = multiplier * magnitude;
        if (candidate <= desiredMeters) {
            meters = candidate;
            break;
        }
    }
    const int pixels = qMax(36, qRound(meters / metersPerPixel));
    QString text;
    if (meters >= 1000.0) {
        const double kilometers = meters / 1000.0;
        text = QStringLiteral("%1 公里").arg(kilometers, 0, 'f',
            std::abs(kilometers - std::round(kilometers)) < 1e-8 ? 0 : 1);
    } else {
        text = QStringLiteral("%1 米").arg(meters, 0, 'f', 0);
    }
    return {meters, pixels, text};
}

QPointF mercatorWorldPixel(const QPointF &coordinate, int zoom)
{
    const double longitude = qBound(-180.0, coordinate.x(), 180.0);
    const double latitude = qBound(-kWebMercatorMaximumLatitude, coordinate.y(), kWebMercatorMaximumLatitude);
    const double worldSize = 256.0 * std::pow(2.0, zoom);
    const double radians = qDegreesToRadians(latitude);
    return QPointF((longitude + 180.0) / 360.0 * worldSize,
                   (1.0 - std::log(std::tan(radians) + 1.0 / std::cos(radians)) / kMapPi) * 0.5 * worldSize);
}

QPointF coordinateFromMercatorPixel(const QPointF &pixel, int zoom)
{
    const double worldSize = 256.0 * std::pow(2.0, zoom);
    double x = std::fmod(pixel.x(), worldSize);
    if (x < 0.0) x += worldSize;
    const double y = qBound(0.0, pixel.y(), worldSize);
    const double longitude = x / worldSize * 360.0 - 180.0;
    const double n = kMapPi - 2.0 * kMapPi * y / worldSize;
    const double latitude = qRadiansToDegrees(std::atan(std::sinh(n)));
    return QPointF(longitude, latitude);
}

QVector<QPolygonF> worldCoastlines()
{
    return {
        QPolygonF() << QPointF(-168, 72) << QPointF(-140, 70) << QPointF(-125, 55) << QPointF(-124, 40)
                    << QPointF(-112, 28) << QPointF(-97, 20) << QPointF(-82, 25) << QPointF(-66, 45)
                    << QPointF(-55, 52) << QPointF(-75, 62) << QPointF(-105, 72) << QPointF(-168, 72),
        QPolygonF() << QPointF(-82, 12) << QPointF(-72, -5) << QPointF(-66, -20) << QPointF(-58, -36)
                    << QPointF(-68, -55) << QPointF(-75, -40) << QPointF(-80, -15) << QPointF(-82, 12),
        QPolygonF() << QPointF(-18, 36) << QPointF(5, 37) << QPointF(20, 32) << QPointF(34, 12)
                    << QPointF(42, -12) << QPointF(30, -34) << QPointF(18, -35) << QPointF(5, -20)
                    << QPointF(-8, 5) << QPointF(-18, 36),
        QPolygonF() << QPointF(-10, 36) << QPointF(5, 55) << QPointF(28, 70) << QPointF(60, 72)
                    << QPointF(95, 76) << QPointF(135, 58) << QPointF(165, 55) << QPointF(150, 40)
                    << QPointF(122, 22) << QPointF(105, 5) << QPointF(80, 8) << QPointF(60, 25)
                    << QPointF(35, 35) << QPointF(15, 42) << QPointF(-10, 36),
        QPolygonF() << QPointF(112, -11) << QPointF(153, -10) << QPointF(154, -38) << QPointF(132, -44)
                    << QPointF(114, -34) << QPointF(112, -11),
        QPolygonF() << QPointF(-52, 60) << QPointF(-20, 77) << QPointF(-32, 84) << QPointF(-60, 82)
                    << QPointF(-73, 70) << QPointF(-52, 60),
        QPolygonF() << QPointF(43, -12) << QPointF(50, -14) << QPointF(49, -26) << QPointF(44, -25)
                    << QPointF(43, -12)
    };
}

QString amapConfigurationPath()
{
    const QString fileName = QStringLiteral("amap.local.ini");
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        applicationDirectory.filePath(fileName),
        QDir::current().filePath(fileName),
        applicationDirectory.filePath(QStringLiteral("../") + fileName),
        applicationDirectory.filePath(QStringLiteral("../../") + fileName)
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) return QDir::cleanPath(candidate);
    }
    return candidates.first();
}

QString decodeEmbeddedCredential(const char *hex)
{
    const QByteArray encoded = QByteArray::fromHex(QByteArray(hex));
    QByteArray decoded;
    decoded.reserve(encoded.size());
    for (const char byte : encoded)
        decoded.append(char(static_cast<unsigned char>(byte) ^ 0x5A));
    return QString::fromLatin1(decoded);
}

struct AmapConfigData
{
    QString key;
    QString securityCode;
    QPointF center{116.397, 39.908};
    int zoom = 5;
    QString source = QStringLiteral("CONFIG MISSING");
};

AmapConfigData loadAmapConfig()
{
    AmapConfigData config;
    QSettings settings(amapConfigurationPath(), QSettings::IniFormat);
    const QString externalKey = settings.value(QStringLiteral("AMap/JsApiKey"),
                                                settings.value(QStringLiteral("AMap/Key"))).toString().trimmed();
    const QString externalSecurity = settings.value(QStringLiteral("AMap/SecurityJsCode")).toString().trimmed();
    config.center = QPointF(settings.value(QStringLiteral("AMap/InitialLongitude"), config.center.x()).toDouble(),
                            settings.value(QStringLiteral("AMap/InitialLatitude"), config.center.y()).toDouble());
    config.zoom = settings.value(QStringLiteral("AMap/InitialZoom"), config.zoom).toInt();
    if (!externalKey.isEmpty() && !externalSecurity.isEmpty()) {
        config.key = externalKey;
        config.securityCode = externalSecurity;
        config.source = QStringLiteral("EXTERNAL CONFIG");
        return config;
    }
    config.key = decodeEmbeddedCredential("6A6269636D3E63623F633F693C6338396A3C6A68626B3B696A63383F6B626A6E");
    config.securityCode = decodeEmbeddedCredential("62686D3F6F686A6F386F3F3B3E686A626F6C626F686B6D38383E633B62623C38");
    if (!config.key.isEmpty() && !config.securityCode.isEmpty())
        config.source = QStringLiteral("BUILT-IN FALLBACK");
    return config;
}

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
        // A repeating energy band travels at a constant rate from left to right.
        // Two neighboring bands make the wrap at each edge visually continuous.
        const qreal cycle = std::fmod(m_effectPhase / 4.5, 1.0);
        QLinearGradient moving(r.left(), r.top(), r.right(), r.top());
        const QColor electricBlue(QStringLiteral("#163C86"));
        const QColor blue(QStringLiteral("#2563EB"));
        const QColor cyan = Theme::iceCyan();
        const QColor violet = Theme::plasmaViolet();
        moving.setSpread(QGradient::RepeatSpread);
        moving.setCoordinateMode(QGradient::ObjectBoundingMode);
        moving.setStart(cycle - 1.0, 0.0);
        moving.setFinalStop(cycle, 0.0);
        moving.setColorAt(0.00, electricBlue);
        moving.setColorAt(0.27, blue);
        moving.setColorAt(0.48, cyan);
        moving.setColorAt(0.70, violet);
        moving.setColorAt(1.00, electricBlue);
        g = moving;
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
        const qreal cycle = std::fmod(m_effectPhase / 4.5, 1.0);
        const qreal x = r.left() + cycle * r.width();
        QLinearGradient scan(x - 52, 0, x + 52, 0);
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
    setMouseTracking(true);
    const AmapConfigData amapConfig = loadAmapConfig();
    m_jsApiKey = amapConfig.key;
    m_securityJsCode = amapConfig.securityCode;
    m_mapConfigSource = amapConfig.source;
    m_centerCoordinate = amapConfig.center;
    m_cursorCoordinate = m_centerCoordinate;
    m_zoom = amapConfig.zoom;
    m_gridCenterCoordinate = m_centerCoordinate;
    m_gridZoom = qBound(1, m_zoom, 12);
    m_mapReady = false;
    m_mapStatus = m_jsApiKey.isEmpty() ? QStringLiteral("AMAP JS API · KEY REQUIRED")
                                      : QStringLiteral("AMAP JS API · %1").arg(m_mapConfigSource);

    m_webSyncTimer = new QTimer(this);
    m_webSyncTimer->setSingleShot(true);
    m_webSyncTimer->setInterval(80);
    connect(m_webSyncTimer, &QTimer::timeout, this, &MapPlanningWidget::flushWebSync);
    m_captureTimeoutTimer = new QTimer(this);
    m_captureTimeoutTimer->setSingleShot(true);
    m_captureTimeoutTimer->setInterval(2500);
    connect(m_captureTimeoutTimer, &QTimer::timeout, this, [this] {
        if (!m_capturePending) return;
        m_capturePending = false;
        if (m_webView) m_webView->page()->runJavaScript(QStringLiteral("restoreAfterWaterwayCapture();"));
        refreshWaterwayOverlayView();
        if (m_captureCoverView) m_captureCoverView->hide();
        GeoReference reference;
        reference.revision = m_mapRevision;
        emit waterwayCaptureReady(QImage(), reference);
    });

    m_recenterButton = new QPushButton(QStringLiteral("定位"), this);
    // 地图按钮使用独立尺寸和内边距，避免被全局 QPushButton 样式挤压中文。
    m_recenterButton->setFixedSize(104, 32);
    m_recenterButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #102A50, stop:0.62 #2563EB, stop:1 #1647A0);"
        "border: 1px solid #00D2FF; border-radius: 6px;"
        "padding: 0px; color: #DDF8FF; font-weight: bold;"
        "}"
        "QPushButton:hover { border-color: #00F2FE; color: #FFFFFF; }"
        "QPushButton:pressed { background: #00D2FF; color: #050916; }"));
    m_recenterButton->setToolTip(QStringLiteral("回到当前位置"));
    connect(m_recenterButton, &QPushButton::clicked, this, [this] { centerOnCurrentLocation(); });
    m_scaleRulerView = new QLabel(this);
    m_scaleRulerView->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_scaleRulerView->setAttribute(Qt::WA_TranslucentBackground, true);
    m_scaleRulerView->setAutoFillBackground(false);
    m_scaleRulerView->setFixedSize(210, 56);
    m_scaleRulerView->setToolTip(QStringLiteral("地图比例尺"));
    m_scaleRulerView->hide();
    updateMapVisibility();
    refreshScaleRulerView();
}

void MapPlanningWidget::warmUp()
{
    if (m_warmupStarted) return;
    m_warmupStarted = true;

    if (m_jsApiKey.trimmed().isEmpty()) {
        m_mapStatus = QStringLiteral("AMAP JS API · KEY REQUIRED · CONFIG MISSING");
        m_amapUnavailable = true;
        updateEffectiveMode();
        update();
        return;
    }

    m_mapStatus = QStringLiteral("AMAP JS API · WARMING UP");
    m_webView = new QWebEngineView(m_webViewHost ? m_webViewHost : this);
    m_webView->setFocusPolicy(Qt::WheelFocus);
    m_webView->setAttribute(Qt::WA_Hover, true);
    m_webView->installEventFilter(this);
    m_webView->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    m_webView->settings()->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    m_webView->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    m_webView->settings()->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, false);
    QWebEngineProfile *profile = m_webView->page()->profile();
    const QString webEngineData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                + QStringLiteral("/webengine");
    const QString webEngineCache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                 + QStringLiteral("/webengine");
    QDir().mkpath(webEngineData);
    QDir().mkpath(webEngineCache);
    profile->setPersistentStoragePath(webEngineData);
    profile->setCachePath(webEngineCache);
    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    profile->setHttpCacheMaximumSize(128 * 1024 * 1024);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    connect(m_webView->page(), &QWebEnginePage::featurePermissionRequested, this,
            [this](const QUrl &origin, QWebEnginePage::Feature feature) {
        if (feature == QWebEnginePage::Geolocation)
            m_webView->page()->setFeaturePermission(origin, feature,
                                                     QWebEnginePage::PermissionDeniedByUser);
    });
    m_webView->setGeometry(mapArea().toRect());
    m_webView->show();
    // Keep the purple water film as a lightweight Qt overlay over the full map.
    // Recognition temporarily switches this same map to its road-free feature set.
    m_waterwayOverlayView = new QLabel(m_webViewHost ? m_webViewHost : this);
    m_waterwayOverlayView->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_waterwayOverlayView->setAttribute(Qt::WA_TranslucentBackground, true);
    m_waterwayOverlayView->setAutoFillBackground(false);
    m_waterwayOverlayView->setStyleSheet(QStringLiteral("background: transparent;"));
    m_waterwayOverlayView->setScaledContents(true);
    m_waterwayOverlayView->hide();
    // Hold the last fully rendered map frame over WebEngine while the temporary
    // road-free frame is captured for recognition. This keeps the feature switch
    // invisible to the user.
    m_captureCoverView = new QLabel(m_webViewHost ? m_webViewHost : this);
    m_captureCoverView->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_captureCoverView->setScaledContents(true);
    m_captureCoverView->hide();
    if (m_scaleRulerView) {
        m_scaleRulerView->setParent(m_webViewHost ? m_webViewHost : this);
        refreshScaleRulerView();
    }

    m_bridge = new AmapWebBridge(m_webView);
    QWebChannel *channel = new QWebChannel(m_webView);
    channel->registerObject(QStringLiteral("bridge"), m_bridge);
    m_webView->page()->setWebChannel(channel);
    connect(m_bridge, &AmapWebBridge::waypointClicked, this, [this](double lon, double lat) {
        if (!m_testDeviceSelectionEnabled && !m_targetSelectionEnabled) return;
        RoutePoint point;
        point.coordinateSystem = CoordinateSystem::Gcj02;
        point.position = QPointF(lon, lat);
        if (m_testDeviceSelectionEnabled) {
            point.id = QStringLiteral("manual-test-device");
            emit testDevicePositionSelected(point);
            return;
        }
        point.id = QStringLiteral("mission-target");
        setMissionTarget(point, TargetState::Pending);
        emit missionTargetSelected(point);
    });
    connect(m_bridge, &AmapWebBridge::mapStateReported, this, [this](bool ready, const QString &message) {
        m_mapReady = ready;
        m_amapUnavailable = !ready;
        m_mapStatus = message;
        if (ready) {
            m_plannedPathDirty = true;
            m_vehicleDirty = m_hasVehiclePosition;
            m_actualVehicleDirty = m_hasActualVehiclePosition;
            m_targetDirty = m_hasMissionTarget;
            m_snapCandidateDirty = true;
            m_planningStartDirty = true;
            m_locationDirty = m_currentLocation.valid;
            scheduleWebSync();
            if (m_pageActive && m_webView)
                m_webView->page()->runJavaScript(QStringLiteral("activateMap();"));
            m_amapLocationPaused = !m_pageActive;
            if (m_webView)
                m_webView->page()->runJavaScript(QStringLiteral("setLocationActive(%1);")
                    .arg(m_pageActive ? QStringLiteral("true") : QStringLiteral("false")));
            refreshWaterwayOverlayView();
        }
        updateEffectiveMode();
        update();
        emit mapReadyChanged(ready);
    });
    connect(m_bridge, &AmapWebBridge::mapViewReported, this,
            [this](double longitude, double latitude, int zoom) {
        m_centerCoordinate = QPointF(longitude, latitude);
        m_cursorCoordinate = m_centerCoordinate;
        m_zoom = zoom;
        refreshScaleRulerView();
        update();
    });
    connect(m_bridge, &AmapWebBridge::mapGeometryReported, this,
            [this](double longitude, double latitude, int zoom, int width, int height,
                   double topLeftLongitude, double topLeftLatitude,
                   double bottomRightLongitude, double bottomRightLatitude,
                   quint64 revision) {
        m_centerCoordinate = QPointF(longitude, latitude);
        m_viewTopLeftGcj02 = QPointF(topLeftLongitude, topLeftLatitude);
        m_viewBottomRightGcj02 = QPointF(bottomRightLongitude, bottomRightLatitude);
        m_zoom = zoom;
        m_mapViewportPixels = QSize(width, height);
        refreshScaleRulerView();
        // JS revision values only describe map movements; Qt owns the public
        // revision so resize and movement changes form one monotonic sequence.
        if (revision != m_lastJsViewRevision) {
            m_lastJsViewRevision = revision;
            ++m_mapRevision;
            emit mapRevisionChanged(m_mapRevision);
        }
    });
    connect(m_bridge, &AmapWebBridge::capturePreparationReported, this,
            [this](quint64 revision, double centerLongitude, double centerLatitude, int zoom,
                   int width, int height, double topLeftLongitude, double topLeftLatitude,
                   double bottomRightLongitude, double bottomRightLatitude) {
        performWaterwayCapture(revision, centerLongitude, centerLatitude, zoom, width, height,
                               topLeftLongitude, topLeftLatitude,
                               bottomRightLongitude, bottomRightLatitude);
    });
    connect(m_bridge, &AmapWebBridge::amapLocationReported, this,
            [this](double longitude, double latitude, double accuracy, double altitude,
                   double timestampMs, const QString &detail) {
        LocationFix fix;
        fix.gcj02Position = QPointF(longitude, latitude);
        fix.horizontalAccuracyMeters = accuracy;
        fix.altitudeMeters = altitude;
        fix.timestamp = QDateTime::fromMSecsSinceEpoch(qRound64(timestampMs), Qt::UTC);
        if (!fix.timestamp.isValid()) fix.timestamp = QDateTime::currentDateTimeUtc();
        fix.providerDetail = detail;
        fix.valid = GeoCoordinateUtils::isValidLongitudeLatitude(fix.gcj02Position);
        emit amapLocationReceived(fix);
        if (m_manualLocationRequest) {
            // Use the just-returned AMap fix immediately.  The controller also
            // receives it through the signal above, but that signal is queued
            // through the page hierarchy and should not delay manual recenter.
            m_currentLocation = fix;
            m_locationSource = LocationSource::Amap;
            centerMapOnCurrentLocation();
            finishManualLocationRequest(true);
        }
    });
    connect(m_bridge, &AmapWebBridge::amapLocationFailureReported, this,
            [this](const QString &message) {
        if (m_manualLocationRequest)
            finishManualLocationRequest(false, message);
        emit amapLocationFailed(message);
    });
    connect(m_bridge, &AmapWebBridge::defaultMapRequested, this, [this] {
        if (m_webView)
            m_webView->page()->runJavaScript(QStringLiteral("startInitialMap(null);"));
    });
    connect(m_webView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) {
            m_webPageLoaded = false;
            m_mapReady = false;
            m_amapUnavailable = true;
            m_mapStatus = QStringLiteral("AMAP JS API · PAGE LOAD FAILED");
            updateEffectiveMode();
        } else {
            m_webPageLoaded = true;
            syncBootstrapLocation();
            // A user may have opened the route page while the preload document was
            // still loading.  Start its session only after that page has painted.
            if (m_pageActive && m_mapSessionRequested)
                QTimer::singleShot(0, this, [this] { startMapSession(); });
        }
        update();
    });

    const QString key = m_jsApiKey;
    const QString security = m_securityJsCode;
    const QPointF initialAmapCenter = m_centerCoordinate;
    // Split the embedded document into two literals. MSVC has a roughly 16K
    // character limit for a single string literal; keeping each half below
    // that limit also makes future HTML additions safe.
    QString html = QStringLiteral(R"HTML(
<!doctype html><html><head><meta charset="utf-8">
 <style>html,body,#map-full{margin:0;width:100%;height:100%;overflow:hidden;background:#071b38}.map-layer{position:absolute;inset:0}.map-layer .amap-marker,.map-layer .amap-icon{cursor:move!important}#hud{display:none;position:absolute;left:16px;top:14px;z-index:25;padding:5px 9px;color:#d9f7ff;background:rgba(4,20,42,.86);border:1px solid rgba(0,210,255,.6);border-radius:4px;font:12px Arial;pointer-events:none}#startup{position:absolute;inset:0;z-index:30;display:flex;align-items:center;justify-content:center;background:linear-gradient(135deg,#06152b,#0a2850)}#startup-card{min-width:280px;padding:24px 32px;text-align:center;color:#d9f7ff;background:rgba(4,20,42,.9);border:1px solid #00d2ff;border-radius:9px;box-shadow:0 0 28px rgba(0,210,255,.2);font:14px Arial}#startup-title{font-weight:bold;letter-spacing:1px;color:#00f2fe;margin-bottom:10px}#startup-detail{font-size:12px;color:#8ec9e9;margin-bottom:16px}#skip-location{display:none;border:1px solid #00d2ff;border-radius:5px;padding:7px 16px;background:#123a70;color:#e4fbff;cursor:pointer;font-weight:bold}.location-dot{width:24px;height:24px;border:1px solid rgba(255,255,255,.88);border-radius:50%;background:rgba(52,120,246,.22);display:flex;align-items:center;justify-content:center;box-sizing:border-box;pointer-events:none}.location-dot-core{width:12px;height:12px;border:3px solid #fff;border-radius:50%;background:#3478f6;box-shadow:0 1px 4px rgba(0,0,0,.38);box-sizing:border-box}</style>
<script>window._AMapSecurityConfig={securityJsCode:'%2'};</script>
<script src="qrc:///qtwebchannel/qwebchannel.js"></script></head>
 <body><div id="map-full" class="map-layer"></div><div id="hud">AMAP JS API · LOADING</div><div id="startup"><div id="startup-card"><div id="startup-title">正在初始化地图</div><div id="startup-detail">高德地图服务准备中…</div><button id="skip-location" onclick="skipInitialLocation()">跳过定位，显示默认地图</button></div></div><script>
 var bridge=null,map=null,mapComplete=false,apiLoaded=false,apiLoading=false,apiAttempts=0,planned=null,vehicle=null,actualVehicle=null,userLocation=null,targetMarker=null,snapMarker=null,planningStartMarker=null,ipLocator=null,ipLocationApplied=false;
 var hasVehicle=false,locationTimer=null,viewReportTimer=null;
 var viewRevision=0,captureRevision=0;
 var initialMapStarted=false,initialViewLocked=false,initialLocationTimer=null,pendingInitialLocation=null,bootstrapLocation=null,locationPaused=false,mapSessionStarted=false;
 var stateReady=false,stateText='AMAP JS API · LOADING';
 function report(ok,msg){stateReady=ok;stateText=msg;var hud=document.getElementById('hud');hud.textContent=msg;hud.style.display=ok?'none':'block';if(bridge)bridge.mapStateChanged(ok,msg);}
 function showStartup(title,detail,allowSkip){var box=document.getElementById('startup');if(!box)return;box.style.display='flex';document.getElementById('startup-title').textContent=title;document.getElementById('startup-detail').textContent=detail;document.getElementById('skip-location').style.display=allowSkip?'inline-block':'none';}
 function hideStartup(){var box=document.getElementById('startup');if(box)box.style.display='none';}
 function skipInitialLocation(){if(bridge)bridge.useDefaultMap();else startInitialMap(null);}
 window.onerror=function(msg){report(false,'JS ERROR · '+msg);};
 function setPlanned(points,valid){if(!map)return;if(planned){map.remove(planned);planned=null;}if(valid&&points.length>1){planned=new AMap.Polyline({path:points.map(function(p){return[p.lon,p.lat];}),strokeColor:'#a855f7',strokeWeight:4,strokeOpacity:.9});planned.setMap(map);}}
 function setVehicle(lon,lat,heading){if(!map)return;var pos=[Number(lon),Number(lat)];if(!Number.isFinite(pos[0])||!Number.isFinite(pos[1]))return;hasVehicle=true;if(vehicle){vehicle.setPosition(pos);vehicle.setAngle(Number(heading)||0);}else{vehicle=new AMap.Marker({position:pos,title:'设备当前位置',anchor:'center',angle:Number(heading)||0,zIndex:520,content:'<div style="width:0;height:0;border-left:9px solid transparent;border-right:9px solid transparent;border-bottom:24px solid #ffb020;filter:drop-shadow(0 0 5px #ffb020);transform-origin:center"></div>'});vehicle.setMap(map);}}
 function setActualVehicle(lon,lat,visible){if(!map)return;if(!visible){if(actualVehicle)actualVehicle.hide();return;}var pos=[Number(lon),Number(lat)];if(!Number.isFinite(pos[0])||!Number.isFinite(pos[1]))return;if(actualVehicle){actualVehicle.setPosition(pos);actualVehicle.show();return;}actualVehicle=new AMap.Marker({position:pos,title:'设备真实位置',anchor:'center',zIndex:521,content:'<div style="width:24px;height:24px;box-sizing:border-box;border-radius:50%;background:transparent;border:3px solid #ff334f;box-shadow:0 0 12px #ff334f"></div>'});actualVehicle.setMap(map);}
 function setTarget(x,y,s){if(!map)return;var c={pending:'#7667ff',safe:'#20e3a2',invalid:'#ff334f'}[s]||'#7667ff',content='<div style="width:20px;height:20px;border-radius:50%;box-sizing:border-box;background:'+c+';border:3px solid #fff;box-shadow:0 0 13px '+c+'"></div>',pos=[+x,+y];if(targetMarker){targetMarker.setPosition(pos);targetMarker.setContent(content);targetMarker.show();return;}targetMarker=new AMap.Marker({position:pos,title:'任务目标',anchor:'center',zIndex:510,content:content});targetMarker.setMap(map);}
 function clearTarget(){if(map&&targetMarker)map.remove(targetMarker);targetMarker=null;}
 function setSnapCandidate(lon,lat,visible){if(!map)return;if(!visible){if(snapMarker)snapMarker.hide();return;}var pos=[Number(lon),Number(lat)];if(snapMarker){snapMarker.setPosition(pos);snapMarker.show();return;}snapMarker=new AMap.Marker({position:pos,title:'建议安全水域',anchor:'center',zIndex:509,content:'<div style="width:18px;height:18px;border-radius:50%;box-sizing:border-box;background:#ffd54a;border:3px solid #fff;box-shadow:0 0 12px #ffd54a"></div>'});snapMarker.setMap(map);}
 function setPlanningStart(lon,lat,visible){if(map&&planningStartMarker){map.remove(planningStartMarker);planningStartMarker=null;}}
 function mapGeometry(){if(!map)return null;var c=map.getCenter(),s=map.getSize();if(!c||!s||!Number.isFinite(Number(c.lng))||!Number.isFinite(Number(c.lat))||!Number.isFinite(Number(s.width))||!Number.isFinite(Number(s.height)))return null;var tl=map.containerToLngLat(new AMap.Pixel(0,0)),br=map.containerToLngLat(new AMap.Pixel(s.width,s.height));if(!tl||!br||!Number.isFinite(Number(tl.lng))||!Number.isFinite(Number(tl.lat))||!Number.isFinite(Number(br.lng))||!Number.isFinite(Number(br.lat)))return null;return{center:c,size:s,topLeft:tl,bottomRight:br};}
 function reportView(changed){if(!map||!bridge)return;if(changed){viewRevision++;}if(viewReportTimer)clearTimeout(viewReportTimer);viewReportTimer=setTimeout(function(){if(!map||!bridge)return;var g=mapGeometry();if(!g)return;bridge.mapViewChanged(g.center.lng,g.center.lat,map.getZoom());bridge.mapGeometryChanged(g.center.lng,g.center.lat,map.getZoom(),g.size.width,g.size.height,g.topLeft.lng,g.topLeft.lat,g.bottomRight.lng,g.bottomRight.lat,viewRevision);},100);}
 function showOperationalOverlays(show){[planned,vehicle,actualVehicle,userLocation,targetMarker,snapMarker,planningStartMarker].forEach(function(o){if(o){show?o.show():o.hide();}});}
 function prepareWaterwayCapture(revision){if(!map||!mapComplete||!bridge)return;captureRevision=Number(revision)||viewRevision;showOperationalOverlays(false);map.setFeatures(['bg']);map.setStatus({showLabel:false,scrollWheel:false,zoomEnable:false,dragEnable:false,doubleClickZoom:false,keyboardEnable:false});map.resize();var notify=function(){requestAnimationFrame(function(){requestAnimationFrame(function(){if(!map||!bridge)return;var g=mapGeometry();if(!g)return;bridge.capturePrepared(captureRevision,g.center.lng,g.center.lat,map.getZoom(),g.size.width,g.size.height,g.topLeft.lng,g.topLeft.lat,g.bottomRight.lng,g.bottomRight.lat);});});};setTimeout(notify,120);}
 function restoreAfterWaterwayCapture(){if(!map)return;map.setFeatures(['bg','road','point']);map.setStatus({showLabel:true,scrollWheel:true,zoomEnable:true,dragEnable:true,doubleClickZoom:true,keyboardEnable:true});showOperationalOverlays(true);}
 function reportLocation(result){if(!result||!result.position){if(bridge)bridge.locationFailed(result&&result.message?result.message:'POSITION UNAVAILABLE');return;}var pos=[Number(result.position.lng),Number(result.position.lat)];if(bridge)bridge.locationChanged(pos[0],pos[1],Number(result.accuracy||-1),Number(result.position.altitude||0),Date.now(),result.location_type||'AMAP');}
 function locationZoom(accuracy){return Number(accuracy)>5000?10:(Number(accuracy)>1000?12:(Number(accuracy)>200?14:16));}
 function setCurrentLocation(lon,lat,accuracy,centerOnce,sourceLabel){if(!map)return;var pos=[Number(lon),Number(lat)];if(!Number.isFinite(pos[0])||!Number.isFinite(pos[1]))return;if(userLocation){userLocation.setPosition(pos);}else{userLocation=new AMap.Marker({position:pos,title:'当前位置',anchor:'center',offset:new AMap.Pixel(0,0),zIndex:500,content:'<div class="location-dot"><div class="location-dot-core"></div></div>'});userLocation.setMap(map);}}
 function setBootstrapLocation(lon,lat,accuracy){var pos=[Number(lon),Number(lat)];if(!Number.isFinite(pos[0])||!Number.isFinite(pos[1]))return;bootstrapLocation={pos:pos,accuracy:Number(accuracy)||-1};if(mapSessionStarted&&apiLoaded&&!initialMapStarted)startInitialMap(bootstrapLocation);}
 function setLocationActive(active){locationPaused=!active;if(active&&map)requestAmapLocation(false);}
 function reportIpLocation(result,force){if(!result||!result.bounds||typeof result.bounds.getCenter!=='function'){if(bridge)bridge.locationFailed('AMAP IP LOCATION INVALID');return;}var center=result.bounds.getCenter();if(!center||!Number.isFinite(Number(center.lng))||!Number.isFinite(Number(center.lat))){if(bridge)bridge.locationFailed('AMAP IP LOCATION INVALID');return;}if(map&&(force||(!bootstrapLocation&&!ipLocationApplied))){map.setBounds(result.bounds,false);ipLocationApplied=true;}if(bridge)bridge.locationChanged(Number(center.lng),Number(center.lat),10000,0,Date.now(),'IP/'+String(result.city||result.province||'CITY'));}
 function requestAmapLocation(force){if(!ipLocator||locationPaused)return;ipLocator.getLocalCity(function(status,result){if(status==='complete'&&result&&result.info==='OK')reportIpLocation(result,!!force);else if(bridge)bridge.locationFailed(result&&result.info?result.info:'AMAP IP LOCATION ERROR');});}
 function ensureGeolocation(callback){if(ipLocator){callback(true);return;}AMap.plugin('AMap.CitySearch',function(){try{ipLocator=new AMap.CitySearch();callback(true);}catch(e){if(bridge)bridge.locationFailed(e.message||'AMAP IP LOCATION ERROR');callback(false);}});}
 function startLocationPolling(){if(locationTimer)return;locationTimer=setInterval(function(){requestAmapLocation(false);},300000);}
 function startInitialMap(fix){if(initialMapStarted)return;initialMapStarted=true;if(initialLocationTimer){clearTimeout(initialLocationTimer);initialLocationTimer=null;}pendingInitialLocation=fix||null;var center=fix?fix.pos:[%3,%5];var zoom=fix?locationZoom(fix.accuracy):%4;createMap(center,zoom);}
 function beginInitialLocation(){startInitialMap(bootstrapLocation||null);ensureGeolocation(function(available){if(!available||!ipLocator){if(bridge)bridge.locationFailed('AMAP IP LOCATION UNAVAILABLE');return;}requestAmapLocation(false);});}
 function enableMapInteraction(){if(!map)return;map.setStatus({scrollWheel:true,zoomEnable:true,dragEnable:true,doubleClickZoom:true,keyboardEnable:true});}
 function startMapSession(){if(mapSessionStarted)return;mapSessionStarted=true;if(typeof AMap!=='undefined'&&apiLoaded){beginInitialLocation();return;}loadAmapApi();}
 function activateMap(){if(!map){startMapSession();return;}enableMapInteraction();requestAnimationFrame(function(){if(!map)return;map.resize();reportView(false);});}
)HTML") + QStringLiteral(R"HTML(
function createMap(center,zoom){if(map)return;showStartup('正在初始化地图','地图服务正在后台准备…',false);try{map=new AMap.Map('map-full',{zoom:zoom,center:center,viewMode:'2D',animateEnable:false,resizeEnable:false,features:['bg','road','point'],pitchEnable:false,rotateEnable:false,jogEnable:false,buildingAnimation:false,scrollWheel:true,zoomEnable:true,dragEnable:true,doubleClickZoom:true,keyboardEnable:true});map.on('complete',function(){mapComplete=true;initialViewLocked=true;enableMapInteraction();if(pendingInitialLocation)setCurrentLocation(pendingInitialLocation.pos[0],pendingInitialLocation.pos[1],pendingInitialLocation.accuracy,false,'INITIAL');startLocationPolling();hideStartup();report(true,'AMAP JS API · ONLINE · MAP READY');reportView(false);});map.on('click',function(e){var point=e&&e.lnglat;if(bridge&&point&&Number.isFinite(Number(point.lng))&&Number.isFinite(Number(point.lat)))bridge.mapClicked(point.lng,point.lat);});map.on('moveend',function(){reportView(true);});map.on('zoomend',function(){reportView(true);});setTimeout(function(){if(!mapComplete){report(false,'AMAP JS API · MAP LOAD SLOW');showStartup('地图加载较慢','请检查网络后重试',false);}},15000);}catch(e){report(false,'AMAP INIT FAILED · '+(e.message||e));showStartup('地图初始化失败',e.message||'AMAP INIT FAILED',false);}}
function initMap(){apiLoaded=true;apiLoading=false;try{if(typeof AMap==='undefined')throw new Error('AMap object unavailable');if(mapSessionStarted)beginInitialLocation();else showStartup('地图组件已就绪','打开路径规划后开始定位与加载地图',false);}catch(e){report(false,'AMAP INIT FAILED · '+(e.message||e));}}
function loadAmapApi(){if(typeof AMap!=='undefined'){initMap();return;}if(apiLoading)return;apiLoading=true;apiAttempts++;var api=document.createElement('script');api.async=true;api.src='https://webapi.amap.com/maps?v=2.0&key=%1&_attempt='+apiAttempts;api.onload=initMap;api.onerror=function(){apiLoading=false;report(false,'AMAP JS API · SCRIPT LOAD FAILED · RETRYING');if(apiAttempts<4)setTimeout(loadAmapApi,1200*apiAttempts);};document.head.appendChild(api);}
if(typeof qt!=='undefined'&&typeof QWebChannel!=='undefined'){new QWebChannel(qt.webChannelTransport,function(c){bridge=c.objects.bridge;bridge.mapStateChanged(stateReady,stateText);});}
loadAmapApi();
setTimeout(function(){if(!apiLoaded){apiLoading=false;report(false,'AMAP JS API · NETWORK SLOW · RETRYING');loadAmapApi();}},12000);
</script></body></html>)HTML");
    html = html.arg(key.toHtmlEscaped()).arg(security.toHtmlEscaped())
        .arg(initialAmapCenter.x(),0,'f',6).arg(m_zoom,0)
        .arg(initialAmapCenter.y(),0,'f',6);
    m_webView->setHtml(html, QUrl(QStringLiteral("https://webapi.amap.com/")));
    updateMapVisibility();
    update();
}

bool MapPlanningWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_webView
        && (event->type() == QEvent::Enter
            || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::Wheel)) {
        if (m_webView && !m_webView->hasFocus())
            m_webView->setFocus(Qt::MouseFocusReason);
    }
    return QWidget::eventFilter(watched, event);
}

void MapPlanningWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const QSize previousViewport = m_mapViewportPixels;
    if (m_webViewHost) {
        QWidget *hostParent = m_webViewHost->parentWidget();
        if (hostParent) {
            const QPoint topLeft = mapTo(hostParent, mapArea().topLeft().toPoint());
            m_webViewHost->setGeometry(QRect(topLeft, mapArea().size().toSize()));
        }
        if (m_webView) m_webView->setGeometry(m_webViewHost->rect());
    } else if (m_webView) {
        m_webView->setGeometry(mapArea().toRect());
    }
    if (m_recenterButton) m_recenterButton->setGeometry(width() - 140, 7, 104, 32);
    if (m_webViewHost) updateMapVisibility();
    refreshWaterwayOverlayView();
    refreshScaleRulerView();
    const QSize currentViewport = mapArea().size().toSize();
    // The first resize can happen before the asynchronous map-geometry report
    // arrives.  Treat a newly observed valid viewport as a real change too;
    // otherwise the automatic recognizer would remain pending until the user
    // automatic recognition owns this capture; no manual recognition action is
    // required from the user.
    if (m_webView && currentViewport.isValid() && currentViewport != previousViewport) {
        m_mapViewportPixels = currentViewport;
        if (m_mapReady) {
            ++m_mapRevision;
            emit mapRevisionChanged(m_mapRevision);
        }
        // Keep this signal independent of map readiness.  A window can be
        // resized while WebEngine is still loading; RoutePage will retain the
        // pending request and run it as soon as the map reports ready.
        emit mapViewportResized(m_mapRevision);
    }
}

QRectF MapPlanningWidget::mapArea() const
{
    return rect().adjusted(36, 54, -36, -36);
}

QPointF MapPlanningWidget::displayCoordinate(const RoutePoint &point) const
{
    return waypointGcj02(point);
}

QPointF MapPlanningWidget::waypointGcj02(const RoutePoint &point) const
{
    return point.position;
}

void MapPlanningWidget::setVehiclePosition(const RoutePoint &position)
{
    RoutePoint acceptedPosition = position;
    if (acceptedPosition.coordinateSystem == CoordinateSystem::Unspecified)
        acceptedPosition.coordinateSystem = CoordinateSystem::Gcj02;
    if (!GeoCoordinateUtils::isValidLongitudeLatitude(acceptedPosition.position)) {
        m_mapStatus = QStringLiteral("AMAP JS API · INVALID ROBOT POSITION · VIEW UNCHANGED");
        update();
        return;
    }
    acceptedPosition.position = waypointGcj02(acceptedPosition);
    acceptedPosition.coordinateSystem = CoordinateSystem::Gcj02;
    m_vehiclePosition = acceptedPosition;
    m_hasVehiclePosition = true;
    m_vehicleDirty = true;
    update();
    scheduleWebSync();
}

void MapPlanningWidget::setVehicleTelemetry(const RoutePoint &position, double headingDegrees)
{
    m_vehicleHeadingDegrees = headingDegrees;
    setVehiclePosition(position);
}

void MapPlanningWidget::setActualVehiclePosition(const RoutePoint &position)
{
    RoutePoint acceptedPosition = position;
    if (acceptedPosition.coordinateSystem == CoordinateSystem::Unspecified)
        acceptedPosition.coordinateSystem = CoordinateSystem::Gcj02;
    m_hasActualVehiclePosition = GeoCoordinateUtils::isValidLongitudeLatitude(
        acceptedPosition.position);
    if (m_hasActualVehiclePosition) {
        acceptedPosition.position = waypointGcj02(acceptedPosition);
        acceptedPosition.coordinateSystem = CoordinateSystem::Gcj02;
        m_actualVehiclePosition = acceptedPosition;
    } else {
        m_actualVehiclePosition = RoutePoint();
    }
    m_actualVehicleDirty = true;
    update();
    scheduleWebSync();
}

void MapPlanningWidget::setTargetSelectionEnabled(bool enabled)
{
    if (m_targetSelectionEnabled == enabled) return;
    m_targetSelectionEnabled = enabled;
    if (m_webView && m_mapReady)
        m_webView->page()->runJavaScript(QStringLiteral(
            "var mapElement=document.getElementById('map-full');"
            "if(mapElement)mapElement.style.cursor='%1';")
            .arg(m_testDeviceSelectionEnabled ? QStringLiteral("crosshair")
                                              : QStringLiteral("default")));
}

void MapPlanningWidget::setTestDeviceSelectionEnabled(bool enabled)
{
    if (m_testDeviceSelectionEnabled == enabled) return;
    m_testDeviceSelectionEnabled = enabled;
    if (m_webView && m_mapReady)
        m_webView->page()->runJavaScript(QStringLiteral(
            "var mapElement=document.getElementById('map-full');"
            "if(mapElement)mapElement.style.cursor='%1';")
            .arg(enabled ? QStringLiteral("crosshair") : QStringLiteral("default")));
}

void MapPlanningWidget::setMissionTarget(const RoutePoint &target, TargetState state)
{
    m_missionTarget = target;
    if (m_missionTarget.coordinateSystem == CoordinateSystem::Unspecified)
        m_missionTarget.coordinateSystem = CoordinateSystem::Gcj02;
    m_hasMissionTarget = GeoCoordinateUtils::isValidLongitudeLatitude(waypointGcj02(m_missionTarget));
    m_targetState = state;
    m_targetDirty = true;
    scheduleWebSync();
    update();
}

void MapPlanningWidget::setSnapCandidate(const RoutePoint &candidate, bool visible)
{
    m_snapCandidate = candidate;
    if (m_snapCandidate.coordinateSystem == CoordinateSystem::Unspecified)
        m_snapCandidate.coordinateSystem = CoordinateSystem::Gcj02;
    m_hasSnapCandidate = visible
        && GeoCoordinateUtils::isValidLongitudeLatitude(waypointGcj02(m_snapCandidate));
    m_snapCandidateDirty = true;
    scheduleWebSync();
}

void MapPlanningWidget::setPlanningStart(const RoutePoint &position, bool visible)
{
    m_planningStart = position;
    if (m_planningStart.coordinateSystem == CoordinateSystem::Unspecified)
        m_planningStart.coordinateSystem = CoordinateSystem::Gcj02;
    m_hasPlanningStart = visible
        && GeoCoordinateUtils::isValidLongitudeLatitude(waypointGcj02(m_planningStart));
    m_planningStartDirty = true;
    // The vehicle icon itself shows the effective start used by planning.
    // m_vehiclePosition still retains the real telemetry coordinate.
    if (m_hasVehiclePosition) m_vehicleDirty = true;
    scheduleWebSync();
    update();
}

void MapPlanningWidget::requestWaterwayCapture()
{
    // Capture a temporary road-free frame from the same map instance. The full
    // presentation frame is covered while the feature set is switched.
    if (!m_webView || !m_mapReady || m_capturePending) return;
    m_capturePending = true;
    if (m_captureCoverView) {
        m_captureCoverView->setGeometry(m_webViewHost ? m_webViewHost->rect()
                                                       : m_webView->geometry());
        m_captureCoverView->setPixmap(m_webViewHost ? m_webViewHost->grab()
                                                     : m_webView->grab());
        m_captureCoverView->raise();
        m_captureCoverView->show();
    }
    if (m_waterwayOverlayView) m_waterwayOverlayView->hide();
    m_captureTimeoutTimer->start();
    m_webView->page()->runJavaScript(QStringLiteral("prepareWaterwayCapture(%1);")
        .arg(m_mapRevision));
}

void MapPlanningWidget::performWaterwayCapture(quint64 revision,
                                                double centerLongitude,
                                                double centerLatitude,
                                                int zoom,
                                                int width,
                                                int height,
                                                double topLeftLongitude,
                                                double topLeftLatitude,
                                                double bottomRightLongitude,
                                                double bottomRightLatitude)
{
    if (!m_capturePending || !m_webView) return;
    QTimer::singleShot(80, this, [this, revision, centerLongitude, centerLatitude, zoom,
                                  width, height, topLeftLongitude, topLeftLatitude,
                                  bottomRightLongitude, bottomRightLatitude] {
        if (!m_webView) return;
        m_captureTimeoutTimer->stop();
        QImage image = m_webView->grab().toImage();
        if (!image.isNull() && image.size() != m_webView->size())
            image = image.scaled(m_webView->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_webView->page()->runJavaScript(QStringLiteral("restoreAfterWaterwayCapture();"));
        m_capturePending = false;
        refreshWaterwayOverlayView();
        QTimer::singleShot(120, this, [this] {
            if (m_captureCoverView) m_captureCoverView->hide();
        });
        GeoReference reference;
        reference.centerGcj02 = QPointF(centerLongitude, centerLatitude);
        reference.topLeftGcj02 = QPointF(topLeftLongitude, topLeftLatitude);
        reference.bottomRightGcj02 = QPointF(bottomRightLongitude, bottomRightLatitude);
        reference.zoom = zoom;
        // The bridge reports the exact map viewport used for containerToLngLat.
        // Keep the actual grab size as a final guard for device-pixel-ratio scaling.
        reference.viewportPixels = QSize(width, height).isValid()
            ? QSize(width, height) : image.size();
        if (reference.viewportPixels != image.size())
            reference.viewportPixels = image.size();
        // Use the revision captured before JavaScript hid operational markers.
        // If the map moves before grab() completes, RoutePage will reject this
        // image instead of assigning stale pixels to the new map geometry.
        reference.revision = revision;
        emit waterwayCaptureReady(image, reference);
    });
}

void MapPlanningWidget::setWaterwayOverlay(const QImage &overlay)
{
    if (overlay.isNull()) return;
    m_waterwayOverlay = overlay;
    refreshWaterwayOverlayView();
}

void MapPlanningWidget::setWaterwayOnlyMode(bool enabled)
{
    m_waterwayOnlyMode = enabled;
    refreshWaterwayOverlayView();
    update();
}

void MapPlanningWidget::clearWaterwayOverlay()
{
    m_waterwayOverlay = QImage();
    refreshWaterwayOverlayView();
}

void MapPlanningWidget::refreshWaterwayOverlayView()
{
    if (!m_waterwayOverlayView) return;
    const QImage &overlay = m_waterwayOverlay;
    const bool visible = m_waterwayOnlyMode && !overlay.isNull()
        && m_pageActive && m_webViewHost && m_mapReady;
    if (!visible) {
        m_waterwayOverlayView->hide();
        return;
    }
    const QSize targetSize = m_webViewHost->size();
    if (!targetSize.isValid()) {
        m_waterwayOverlayView->hide();
        return;
    }
    m_waterwayOverlayView->setGeometry(m_webViewHost->rect());
    m_waterwayOverlayView->setPixmap(QPixmap::fromImage(
        // The recognizer works on a bounded grid, but this is only the visual
        // Keep the displayed film aligned to the exact recognition mask. The
        // overlay is enlarged only for presentation and never used for math.
        overlay.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::FastTransformation)));
    m_waterwayOverlayView->raise();
    m_waterwayOverlayView->show();
    if (m_scaleRulerView) m_scaleRulerView->raise();
}

void MapPlanningWidget::refreshScaleRulerView()
{
    if (!m_scaleRulerView) return;
    QWidget *parent = m_webViewHost ? m_webViewHost : this;
    const QRect area = m_webViewHost ? m_webViewHost->rect() : mapArea().toRect();
    if (!m_pageActive || !parent || !area.isValid() || area.width() < 140
        || area.height() < 100) {
        m_scaleRulerView->hide();
        return;
    }
    if (m_scaleRulerView->parentWidget() != parent)
        m_scaleRulerView->setParent(parent);

    const bool gridMode = m_effectiveMode == DisplayMode::GlobalGrid;
    const ScaleRulerSpec ruler = scaleRulerFor(
        (gridMode ? m_gridCenterCoordinate : m_centerCoordinate).y(),
        gridMode ? m_gridZoom : m_zoom);
    if (ruler.pixels <= 0 || ruler.text.isEmpty()) {
        m_scaleRulerView->hide();
        return;
    }

    QPixmap pixmap(m_scaleRulerView->size());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect textRect(0, 0, pixmap.width(), 28);
    painter.setFont(Theme::font(11, true));
    painter.setPen(QColor(255, 255, 255, 215));
    painter.drawText(textRect.translated(1, 1), Qt::AlignCenter, ruler.text);
    painter.setPen(QColor(5, 25, 50, 235));
    painter.drawText(textRect, Qt::AlignCenter, ruler.text);

    const int left = qBound(12, (pixmap.width() - ruler.pixels) / 2,
                            pixmap.width() - 12);
    const int right = qBound(left + 1, left + ruler.pixels, pixmap.width() - 12);
    constexpr int baseline = 41;
    painter.setPen(QPen(QColor(255, 255, 255, 210), 3.6));
    painter.drawLine(left, baseline, right, baseline);
    painter.drawLine(left, baseline - 4, left, baseline + 4);
    painter.drawLine(right, baseline - 4, right, baseline + 4);
    painter.setPen(QPen(QColor(5, 25, 50, 240), 1.3));
    painter.drawLine(left, baseline, right, baseline);
    painter.drawLine(left, baseline - 4, left, baseline + 4);
    painter.drawLine(right, baseline - 4, right, baseline + 4);
    painter.end();

    m_scaleRulerView->setPixmap(pixmap);
    m_scaleRulerView->move(area.left() + 12, area.bottom() - m_scaleRulerView->height() - 12);
    m_scaleRulerView->raise();
    m_scaleRulerView->show();
}

void MapPlanningWidget::setCurrentLocation(const LocationFix &fix, LocationSource source)
{
    m_currentLocation = fix;
    m_locationSource = source;
    if (fix.valid && GeoCoordinateUtils::isValidLongitudeLatitude(fix.gcj02Position)) {
        m_cursorCoordinate = fix.gcj02Position;
        m_locationDirty = true;
        if (!m_mapReady) {
            m_bootstrapLocationDirty = true;
            syncBootstrapLocation();
        }
    }
    updateEffectiveMode();
    scheduleWebSync();
    update();
}

void MapPlanningWidget::syncBootstrapLocation()
{
    // A GPS fix may arrive while the WebEngine document is navigating.  Hold it
    // until the JavaScript bridge is ready; otherwise runJavaScript can silently
    // discard it and the first route-page session would unnecessarily use AMap.
    if (!m_bootstrapLocationDirty || !m_webView || !m_webPageLoaded || !m_currentLocation.valid)
        return;
    if (!GeoCoordinateUtils::isValidLongitudeLatitude(m_currentLocation.gcj02Position))
        return;
    const QPointF gcj = m_currentLocation.gcj02Position;
    m_webView->page()->runJavaScript(QStringLiteral("setBootstrapLocation(%1,%2,%3);")
        .arg(gcj.x(), 0, 'f', 8)
        .arg(gcj.y(), 0, 'f', 8)
        .arg(m_currentLocation.horizontalAccuracyMeters, 0, 'f', 2));
    m_bootstrapLocationDirty = false;
}

void MapPlanningWidget::startMapSession()
{
    if (!m_pageActive || m_mapSessionStarted)
        return;

    m_mapSessionRequested = true;
    // Do not issue JavaScript while the preload document is still navigating.
    // loadFinished will retry this once the document is ready.
    if (!m_webView || !m_webPageLoaded)
        return;

    m_mapSessionStarted = true;
    QTimer::singleShot(0, this, [this] {
        if (m_webView && m_pageActive)
            m_webView->page()->runJavaScript(QStringLiteral("startMapSession();"));
    });
}

void MapPlanningWidget::setLocationStatus(const QString &status)
{
    if (m_locationStatus == status) return;
    m_locationStatus = status;
    update();
}

void MapPlanningWidget::setPlannedPath(const RoutePath &path)
{
    m_plannedPath = path;
    m_plannedPathDirty = true;
    update();
    scheduleWebSync();
}

void MapPlanningWidget::setPageActive(bool active)
{
    m_pageActive = active;
    updateMapVisibility();
    refreshWaterwayOverlayView();
    refreshScaleRulerView();
    if (m_webView && m_mapReady && m_amapLocationPaused == active) {
        m_amapLocationPaused = !active;
        m_webView->page()->runJavaScript(QStringLiteral("setLocationActive(%1);")
            .arg(active ? QStringLiteral("true") : QStringLiteral("false")));
    }
    if (active && m_webView)
        QTimer::singleShot(0, this, [this] {
            if (!m_webView || !m_pageActive) return;
            if (m_mapReady)
                m_webView->page()->runJavaScript(QStringLiteral("activateMap();"));
            else
                startMapSession();
        });
}

void MapPlanningWidget::setWebViewHost(QWidget *host)
{
    if (m_webViewHost == host) {
        updateMapVisibility();
        refreshWaterwayOverlayView();
        return;
    }
    m_webViewHost = host;
    if (m_webView && m_webViewHost) {
        m_webView->setParent(m_webViewHost);
        m_webView->setGeometry(m_webViewHost->rect());
        m_webView->show();
    }
    if (m_waterwayOverlayView && m_webViewHost) {
        m_waterwayOverlayView->setParent(m_webViewHost);
        m_waterwayOverlayView->setGeometry(m_webViewHost->rect());
    }
    if (m_captureCoverView && m_webViewHost) {
        m_captureCoverView->setParent(m_webViewHost);
        m_captureCoverView->setGeometry(m_webViewHost->rect());
    }
    if (m_scaleRulerView && m_webViewHost)
        m_scaleRulerView->setParent(m_webViewHost);
    updateMapVisibility();
    refreshWaterwayOverlayView();
    refreshScaleRulerView();
}

void MapPlanningWidget::syncWebViewGeometry()
{
    if (m_webViewHost)
        updateMapVisibility();
    refreshWaterwayOverlayView();
    refreshScaleRulerView();
}

void MapPlanningWidget::updateEffectiveMode()
{
    // The product uses a single AMap view. Transient script or tile timeouts must
    // never replace it with the old global-grid placeholder.
    m_effectiveMode = DisplayMode::Amap;
    updateMapVisibility();
}

void MapPlanningWidget::updateMapVisibility()
{
    if (m_webViewHost) {
        const QWidget *hostParent = m_webViewHost->parentWidget();
        if (hostParent) {
            const QPoint topLeft = mapTo(const_cast<QWidget *>(hostParent), mapArea().topLeft().toPoint());
            m_webViewHost->setGeometry(QRect(topLeft, mapArea().size().toSize()));
        }
        // WebEngine 在后台保持实际尺寸与可见渲染，首次定位和建图不会等到
        // 用户切入路径页才开始。非路径页的遮挡由 MainWindow 的不透明封面承担。
        m_webViewHost->show();
        if (m_pageActive)
            m_webViewHost->raise();
        refreshWaterwayOverlayView();
    }
    if (m_webView && !m_webViewHost) {
        // Keep the WebEngine child alive at its real size while the stacked page is
        // hidden. The parent page controls visibility, so this lets Chromium finish
        // its first layout and tile preparation before the user opens the map.
        const bool showAmap = m_effectiveMode == DisplayMode::Amap;
        if (showAmap) {
            const bool wasHidden = !m_webView->isVisible();
            m_webView->setGeometry(mapArea().toRect());
            m_webView->show();
            m_webView->raise();
            if (wasHidden && m_mapReady)
                QTimer::singleShot(0, this, [this] {
                    if (m_webView && m_pageActive && m_effectiveMode == DisplayMode::Amap)
                        m_webView->page()->runJavaScript(QStringLiteral("activateMap();"));
                });
        } else {
            m_webView->hide();
        }
    }
    if (m_recenterButton) m_recenterButton->raise();
    refreshScaleRulerView();
}

void MapPlanningWidget::centerOnCurrentLocation()
{
    if (m_manualLocationRequest) return;
    if (!m_webView || !m_mapReady) {
        m_locationStatus = QStringLiteral("LOCATION · MAP NOT READY");
        update();
        return;
    }
    if (m_currentLocation.valid
        && GeoCoordinateUtils::isValidLongitudeLatitude(m_currentLocation.gcj02Position)) {
        centerMapOnCurrentLocation();
        return;
    }
    m_manualLocationRequest = true;
    m_recenterButton->setText(QStringLiteral("定位中…"));
    m_recenterButton->setEnabled(false);
    m_locationStatus = QStringLiteral("LOCATION · REQUESTING IP FIX");
    m_webView->page()->runJavaScript(
        QStringLiteral("if(typeof requestAmapLocation==='function')requestAmapLocation(true);"));
    QTimer::singleShot(9000, this, [this] {
        if (m_manualLocationRequest)
            finishManualLocationRequest(false, QStringLiteral("定位请求超时"));
    });
    update();
}

void MapPlanningWidget::centerMapOnCurrentLocation()
{
    if (!m_currentLocation.valid || !m_webView || !m_mapReady) return;
    m_gridCenterCoordinate = m_currentLocation.gcj02Position;
    if (m_effectiveMode == DisplayMode::Amap) {
        int zoom = 16;
        const double accuracy = m_currentLocation.horizontalAccuracyMeters;
        if (qIsFinite(accuracy) && accuracy > 5000.0) zoom = 10;
        else if (qIsFinite(accuracy) && accuracy > 1000.0) zoom = 12;
        else if (qIsFinite(accuracy) && accuracy > 200.0) zoom = 14;
        const QPointF gcj = m_currentLocation.gcj02Position;
        const QString script = QStringLiteral(
            "if(typeof activateMap==='function')activateMap();"
            "if(typeof map!=='undefined'&&map)map.setZoomAndCenter(%3,[%1,%2],false);")
            .arg(gcj.x(), 0, 'f', 8)
            .arg(gcj.y(), 0, 'f', 8)
            .arg(zoom);
        m_webView->page()->runJavaScript(script);
    }
}

void MapPlanningWidget::finishManualLocationRequest(bool succeeded, const QString &message)
{
    if (!m_manualLocationRequest) return;
    m_manualLocationRequest = false;
    if (m_recenterButton) {
        m_recenterButton->setText(QStringLiteral("定位"));
        m_recenterButton->setEnabled(true);
    }
    if (!succeeded)
        m_locationStatus = QStringLiteral("LOCATION · %1")
            .arg(message.isEmpty() ? QStringLiteral("AMAP FAILED") : message);
    update();
}

void MapPlanningWidget::scheduleWebSync()
{
    if (!m_mapReady || !m_webView || !m_webSyncTimer) return;
    if (!m_webSyncTimer->isActive()) m_webSyncTimer->start();
}

void MapPlanningWidget::flushWebSync()
{
    if (!m_mapReady || !m_webView) return;

    QString script;
    if (m_plannedPathDirty) {
        QJsonArray array;
        for (const RoutePoint &point : m_plannedPath.points) {
            const QPointF pos = displayCoordinate(point);
            QJsonObject object;
            object[QStringLiteral("lon")] = pos.x();
            object[QStringLiteral("lat")] = pos.y();
            array.append(object);
        }
        const QString json = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
        const QString stateKey = json + (m_plannedPath.valid ? QStringLiteral("1") : QStringLiteral("0"));
        if (stateKey != m_lastPlannedPathJson) {
            script += QStringLiteral("setPlanned(%1,%2);")
                .arg(json, m_plannedPath.valid ? QStringLiteral("true") : QStringLiteral("false"));
            m_lastPlannedPathJson = stateKey;
        }
        m_plannedPathDirty = false;
    }

    if (m_vehicleDirty && m_hasVehiclePosition) {
        const RoutePoint &displayedVehicle = m_hasPlanningStart
            ? m_planningStart : m_vehiclePosition;
        const QPointF pos = waypointGcj02(displayedVehicle);
        if (GeoCoordinateUtils::isValidLongitudeLatitude(pos)) {
            script += QStringLiteral("setVehicle(%1,%2,%3);")
                .arg(pos.x(), 0, 'f', 8)
                .arg(pos.y(), 0, 'f', 8)
                .arg(m_vehicleHeadingDegrees, 0, 'f', 2);
        }
        m_vehicleDirty = false;
    }

    if (m_actualVehicleDirty) {
        if (m_hasActualVehiclePosition) {
            const QPointF pos = waypointGcj02(m_actualVehiclePosition);
            script += QStringLiteral("setActualVehicle(%1,%2,true);")
                .arg(pos.x(), 0, 'f', 8).arg(pos.y(), 0, 'f', 8);
        } else {
            script += QStringLiteral("setActualVehicle(0,0,false);");
        }
        m_actualVehicleDirty = false;
    }

    if (m_targetDirty) {
        if (m_hasMissionTarget) {
            const QPointF pos = displayCoordinate(m_missionTarget);
            QString stateName = QStringLiteral("pending");
            if (m_targetState == TargetState::Safe) stateName = QStringLiteral("safe");
            else if (m_targetState == TargetState::Invalid) stateName = QStringLiteral("invalid");
            script += QStringLiteral("setTarget(%1,%2,'%3');")
                .arg(pos.x(), 0, 'f', 8).arg(pos.y(), 0, 'f', 8)
                .arg(stateName);
        } else {
            script += QStringLiteral("clearTarget();");
        }
        m_targetDirty = false;
    }

    if (m_snapCandidateDirty) {
        if (m_hasSnapCandidate) {
            const QPointF pos = displayCoordinate(m_snapCandidate);
            script += QStringLiteral("setSnapCandidate(%1,%2,true);")
                .arg(pos.x(), 0, 'f', 8).arg(pos.y(), 0, 'f', 8);
        } else {
            script += QStringLiteral("setSnapCandidate(0,0,false);");
        }
        m_snapCandidateDirty = false;
    }

    if (m_planningStartDirty) {
        if (m_hasPlanningStart) {
            const QPointF pos = displayCoordinate(m_planningStart);
            script += QStringLiteral("setPlanningStart(%1,%2,true);")
                .arg(pos.x(), 0, 'f', 8).arg(pos.y(), 0, 'f', 8);
        } else {
            script += QStringLiteral("setPlanningStart(0,0,false);");
        }
        m_planningStartDirty = false;
    }

    if (m_locationDirty && m_currentLocation.valid) {
        if (GeoCoordinateUtils::isValidLongitudeLatitude(m_currentLocation.gcj02Position)) {
            const QPointF pos = m_currentLocation.gcj02Position;
            script += QStringLiteral("setCurrentLocation(%1,%2,%3,%4,'%5');")
                .arg(pos.x(), 0, 'f', 8)
                .arg(pos.y(), 0, 'f', 8)
                .arg(m_currentLocation.horizontalAccuracyMeters, 0, 'f', 2)
                .arg(QStringLiteral("false"))
                .arg(m_locationSource == LocationSource::Gps ? QStringLiteral("GPS") : QStringLiteral("AMAP"));
        }
        m_locationDirty = false;
    }

    if (!script.isEmpty()) m_webView->page()->runJavaScript(script);
}

QSize MapPlanningWidget::sizeHint() const { return QSize(980, 690); }

QPointF MapPlanningWidget::gridProject(const QPointF &gcj02) const
{
    const QRectF area = mapArea();
    const QPointF centerPixel = mercatorWorldPixel(m_gridCenterCoordinate, m_gridZoom);
    QPointF pointPixel = mercatorWorldPixel(gcj02, m_gridZoom);
    const double worldSize = 256.0 * std::pow(2.0, m_gridZoom);
    double deltaX = pointPixel.x() - centerPixel.x();
    if (deltaX > worldSize * 0.5) deltaX -= worldSize;
    if (deltaX < -worldSize * 0.5) deltaX += worldSize;
    return area.center() + QPointF(deltaX, pointPixel.y() - centerPixel.y());
}

QPointF MapPlanningWidget::gridUnproject(const QPointF &screenPosition) const
{
    const QRectF area = mapArea();
    const QPointF centerPixel = mercatorWorldPixel(m_gridCenterCoordinate, m_gridZoom);
    return coordinateFromMercatorPixel(centerPixel + screenPosition - area.center(), m_gridZoom);
}

void MapPlanningWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_effectiveMode != DisplayMode::GlobalGrid || !mapArea().contains(event->pos())) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    m_gridPressPosition = event->pos();
    m_gridLastMousePosition = event->pos();
    m_gridMoved = false;
    m_gridDragging = true;
    setCursor(Qt::SizeAllCursor);
    event->accept();
}

void MapPlanningWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_effectiveMode != DisplayMode::GlobalGrid) return QWidget::mouseMoveEvent(event);
    if (!m_gridDragging) {
        m_cursorCoordinate = gridUnproject(event->pos());
        setCursor(Qt::ArrowCursor);
        update();
        return;
    }
    if (QLineF(m_gridPressPosition, event->pos()).length() > 3.0) m_gridMoved = true;
    if (m_gridDragging) {
        const QPointF centerPixel = mercatorWorldPixel(m_gridCenterCoordinate, m_gridZoom);
        const QPoint delta = event->pos() - m_gridLastMousePosition;
        m_gridCenterCoordinate = coordinateFromMercatorPixel(centerPixel - QPointF(delta), m_gridZoom);
    }
    m_gridLastMousePosition = event->pos();
    m_cursorCoordinate = gridUnproject(event->pos());
    update();
    event->accept();
}

void MapPlanningWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_effectiveMode != DisplayMode::GlobalGrid || event->button() != Qt::LeftButton)
        return QWidget::mouseReleaseEvent(event);
    const bool selectPoint = (m_testDeviceSelectionEnabled || m_targetSelectionEnabled)
        && m_gridDragging && !m_gridMoved && mapArea().contains(event->pos());
    m_gridDragging = false;
    unsetCursor();
    if (selectPoint) {
        RoutePoint point;
        point.coordinateSystem = CoordinateSystem::Gcj02;
        point.position = gridUnproject(event->pos());
        if (m_testDeviceSelectionEnabled) {
            point.id = QStringLiteral("manual-test-device");
            emit testDevicePositionSelected(point);
            event->accept();
            return;
        }
        point.id = QStringLiteral("mission-target");
        setMissionTarget(point, TargetState::Pending);
        emit missionTargetSelected(point);
    }
    event->accept();
}

void MapPlanningWidget::wheelEvent(QWheelEvent *event)
{
    if (m_effectiveMode != DisplayMode::GlobalGrid || !mapArea().contains(event->position()))
        return QWidget::wheelEvent(event);
    const QPointF before = gridUnproject(event->position());
    m_gridZoom = qBound(1, m_gridZoom + (event->angleDelta().y() > 0 ? 1 : -1), 18);
    const QPointF after = gridUnproject(event->position());
    m_gridCenterCoordinate += before - after;
    m_cursorCoordinate = before;
    refreshScaleRulerView();
    update();
    event->accept();
}

void MapPlanningWidget::drawGlobalGrid(QPainter &p, const QRectF &area)
{
    p.save();
    p.setClipRect(area);
    QLinearGradient ocean(area.topLeft(), area.bottomRight());
    ocean.setColorAt(0.0, QColor(3, 12, 31));
    ocean.setColorAt(0.55, QColor(5, 31, 66));
    ocean.setColorAt(1.0, QColor(3, 17, 42));
    p.fillRect(area, ocean);

    const int coordinateStep = m_gridZoom >= 10 ? 1 : m_gridZoom >= 7 ? 5 : m_gridZoom >= 4 ? 15 : 30;
    p.setPen(QPen(alpha(Theme::grid(), 80), 1));
    p.setFont(Theme::font(7));
    for (int lon = -180; lon <= 180; lon += coordinateStep) {
        const QPointF top = gridProject(QPointF(lon, kWebMercatorMaximumLatitude));
        const QPointF bottom = gridProject(QPointF(lon, -kWebMercatorMaximumLatitude));
        if (top.x() < area.left() - 2 || top.x() > area.right() + 2) continue;
        p.drawLine(QPointF(top.x(), area.top()), QPointF(top.x(), area.bottom()));
        p.setPen(Theme::textMuted());
        p.drawText(QPointF(top.x() + 3, area.bottom() - 6), QStringLiteral("%1°").arg(lon));
        p.setPen(QPen(alpha(Theme::grid(), 80), 1));
    }
    for (int lat = -75; lat <= 75; lat += coordinateStep) {
        const QPointF point = gridProject(QPointF(m_gridCenterCoordinate.x(), lat));
        if (point.y() < area.top() - 2 || point.y() > area.bottom() + 2) continue;
        p.drawLine(QPointF(area.left(), point.y()), QPointF(area.right(), point.y()));
        p.setPen(Theme::textMuted());
        p.drawText(QPointF(area.left() + 5, point.y() - 3), QStringLiteral("%1°").arg(lat));
        p.setPen(QPen(alpha(Theme::grid(), 80), 1));
    }

    p.setPen(QPen(alpha(Theme::iceCyan(), 125), 1.2));
    p.setBrush(alpha(Theme::panelAlt(), 150));
    for (const QPolygonF &coast : worldCoastlines()) {
        QPolygonF screen;
        for (const QPointF &coordinate : coast) screen << gridProject(coordinate);
        p.drawPolygon(screen);
    }

    if (m_plannedPath.valid && m_plannedPath.points.size() > 1) {
        QPainterPath path;
        for (int i = 0; i < m_plannedPath.points.size(); ++i) {
            const QPointF pos = gridProject(waypointGcj02(m_plannedPath.points.at(i)));
            if (i == 0) path.moveTo(pos); else path.lineTo(pos);
        }
        p.setPen(QPen(Theme::plasmaViolet(), 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }

    if (m_hasMissionTarget) {
        const QPointF pos = gridProject(waypointGcj02(m_missionTarget));
        QColor color(118, 103, 255);
        if (m_targetState == TargetState::Safe) color = QColor(32, 227, 162);
        else if (m_targetState == TargetState::Invalid) color = QColor(255, 51, 79);
        p.setPen(QPen(Qt::white, 3));
        p.setBrush(color);
        p.drawEllipse(pos, 10, 10);
    }

    if (m_hasSnapCandidate) {
        const QPointF pos = gridProject(waypointGcj02(m_snapCandidate));
        p.setPen(QPen(Qt::white, 3));
        p.setBrush(QColor(255, 213, 74));
        p.drawEllipse(pos, 9, 9);
    }

    if (m_hasVehiclePosition) {
        const RoutePoint &displayedVehicle = m_hasPlanningStart
            ? m_planningStart : m_vehiclePosition;
        const QPointF pos = gridProject(waypointGcj02(displayedVehicle));
        p.setPen(QPen(Theme::warning(), 2)); p.setBrush(alpha(Theme::warning(), 90));
        QPolygonF vehicleMark;
        vehicleMark << QPointF(pos.x(), pos.y() - 11) << QPointF(pos.x() + 8, pos.y() + 8)
                    << QPointF(pos.x(), pos.y() + 4) << QPointF(pos.x() - 8, pos.y() + 8);
        p.drawPolygon(vehicleMark);
    }

    if (m_hasActualVehiclePosition) {
        const QPointF pos = gridProject(waypointGcj02(m_actualVehiclePosition));
        p.setPen(QPen(Qt::white, 2.5));
        p.setBrush(QColor(255, 51, 79));
        p.drawEllipse(pos, 8, 8);
    }

    if (m_currentLocation.valid) {
        const QPointF pos = gridProject(m_currentLocation.gcj02Position);
        p.setPen(QPen(Qt::white, 3));
        p.setBrush(m_locationSource == LocationSource::Gps ? Theme::value() : Theme::accent());
        p.drawEllipse(pos, 7, 7);
    }

    p.setPen(Theme::iceCyan());
    p.setFont(Theme::font(9, true));
    p.drawText(area.adjusted(14, 12, -14, -12), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("GLOBAL GRID · GCJ-02 · Z%1").arg(m_gridZoom));
    p.restore();
}

void MapPlanningWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF outer = rect().adjusted(2, 2, -2, -2);
    p.setPen(QPen(alpha(Theme::border(), 125), 1.3));
    p.setBrush(Theme::panelGradient(outer));
    p.drawRoundedRect(outer, 12, 12);

    const QRectF area = mapArea();
    p.fillRect(area, Theme::backgroundDeep());
    if (m_effectiveMode == DisplayMode::GlobalGrid)
        drawGlobalGrid(p, area);

    QLinearGradient header(outer.topLeft(), outer.topRight());
    header.setColorAt(0.0, alpha(Theme::panel(), 235));
    header.setColorAt(0.52, alpha(Theme::panelRaised(), 245));
    header.setColorAt(1.0, alpha(Theme::panel(), 235));
    p.fillRect(QRectF(outer.left() + 1, outer.top() + 1, outer.width() - 2, 42), header);
    p.setFont(Theme::font(13, true)); p.setPen(Theme::titleStart());
    p.drawText(QRectF(18, 7, qMax(220, width() - 640), 28), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("地图与路径规划 / MAP & ROUTE PLANNING"));
    const bool ready = m_mapReady;
    p.setFont(Theme::font(8, true)); p.setPen(ready ? Theme::glow() : Theme::warning());
    p.drawText(QRectF(qMax(280, width() - 610), 9, 305, 24), Qt::AlignRight | Qt::AlignVCenter,
               m_effectiveMode == DisplayMode::GlobalGrid ? QStringLiteral("GLOBAL GRID · ONLINE") : m_mapStatus);

    if (!ready && m_effectiveMode == DisplayMode::Amap) {
        const QRectF message(area.center().x() - qMin<qreal>(250, area.width() * 0.36), area.center().y() - 43,
                             qMin<qreal>(500, area.width() * 0.72), 86);
        p.setPen(QPen(alpha(Theme::border(), 110), 1)); p.setBrush(alpha(Theme::backgroundDeep(), 220));
        p.drawRoundedRect(message, 8, 8);
        p.setFont(Theme::font(12, true)); p.setPen(Theme::value());
        p.drawText(message.adjusted(12, 8, -12, -34), Qt::AlignCenter,
                   m_jsApiKey.isEmpty() ? QStringLiteral("高德 JS API 等待授权配置")
                                        : QStringLiteral("高德地图后台加载中"));
        p.setFont(Theme::font(8)); p.setPen(Theme::textMuted());
        p.drawText(message.adjusted(12, 42, -12, -7), Qt::AlignCenter,
                   QStringLiteral("amap.local.ini · Key + SecurityJsCode · %1").arg(m_mapConfigSource));
    }
    p.setFont(Theme::font(8)); p.setPen(Theme::textMuted());
    p.drawText(QRectF(20, height() - 29, width() - 40, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1  ·  Z%2  ·  %3  ·  %4  ·  单目标河道任务  ·  %5")
                   .arg(m_effectiveMode == DisplayMode::GlobalGrid ? QStringLiteral("GLOBAL GRID")
                                                                    : (m_mapReady ? QStringLiteral("AMAP JS API") : QStringLiteral("地图加载中")))
                   .arg(m_effectiveMode == DisplayMode::GlobalGrid ? m_gridZoom : m_zoom)
                   .arg(QStringLiteral("%1, %2").arg(m_cursorCoordinate.x(), 0, 'f', 6).arg(m_cursorCoordinate.y(), 0, 'f', 6))
                   .arg(m_locationStatus)
                   .arg(m_plannedPath.valid ? QStringLiteral("规划结果：READY") : QStringLiteral("规划结果：STANDBY")));
    drawCornerMarks(p, outer.adjusted(7, 7, -7, -7), 24);
}

VideoPlaceholder::VideoPlaceholder(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(480, 300);
    connect(EffectController::instance(), &EffectController::frameAdvanced, this, [this](qreal phase, qreal delta) {
        if (!isVisible()) return;
        m_effectPhase = phase;
        const QRectF bounds = rect().adjusted(30, 30, -30, -30);
        m_signalParticles.update(delta, bounds, m_frame.isNull() ? 0.28 : 0.20, phase);
        update();
    });
}

void VideoPlaceholder::setFrame(const QImage &frame)
{
    if (frame.isNull()) return;
    m_frame = frame;
    update();
}

void VideoPlaceholder::clearFrame()
{
    m_frame = QImage();
    update();
}

void VideoPlaceholder::setStreamState(CameraState state, const QString &message)
{
    m_state = state;
    m_stateMessage = message;
    if (state == CameraState::Stopped || state == CameraState::Error)
        m_frame = QImage();
    update();
}

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

    QRectF videoRect;
    if (!m_frame.isNull()) {
        const QSizeF fitted = m_frame.size().scaled(r.size().toSize(), Qt::KeepAspectRatio);
        videoRect = QRectF(QPointF(r.center().x() - fitted.width() * 0.5,
                                  r.center().y() - fitted.height() * 0.5), fitted);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(videoRect, m_frame);
        p.fillRect(r, alpha(Theme::backgroundDeep(), 16));
    }

    p.setPen(QPen(alpha(Theme::accent(), m_frame.isNull() ? 28 : 11), 1));
    const int spacing = 34;
    for (int x = 0; x < width(); x += spacing) p.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += spacing) p.drawLine(0, y, width(), y);
    if (m_frame.isNull()) {
        const qreal scanY = std::fmod(m_effectPhase * 28.0, qMax(1, height()));
        QLinearGradient scanGradient(0, scanY - 18, 0, scanY + 18);
        QColor transparentGlow = Theme::glow();
        transparentGlow.setAlpha(0);
        QColor scanHighlight = Theme::iceCyan();
        scanHighlight.setAlpha(55);
        QColor transparentViolet = Theme::plasmaViolet();
        transparentViolet.setAlpha(0);
        scanGradient.setColorAt(0.0, transparentGlow);
        scanGradient.setColorAt(0.5, scanHighlight);
        scanGradient.setColorAt(1.0, transparentViolet);
        p.fillRect(QRectF(0, scanY - 18, width(), 36), scanGradient);

        p.setPen(QPen(alpha(Theme::plasmaViolet(), 25), 1));
        for (int i = 0; i < 3; ++i) {
            QPainterPath wave;
            for (int x = 0; x <= width(); x += 18) {
                const qreal y = height() * (0.27 + i * 0.18) + qSin(x * 0.018 + m_effectPhase * (0.7 + i * 0.18)) * (7 + i * 3);
                if (x == 0) wave.moveTo(x, y); else wave.lineTo(x, y);
            }
            p.drawPath(wave);
        }
    }
    p.setClipping(false);
    m_signalParticles.paint(p, m_frame.isNull() ? 0.42 : 0.18);

    if (m_frame.isNull()) {
        p.setFont(Theme::font(16, true));
        p.setPen(m_state == CameraState::Error ? QColor(255, 112, 112)
                                               : (m_state == CameraState::Opening ? Theme::iceCyan() : Theme::textMuted()));
        p.drawText(r.adjusted(32, 0, -32, 0), Qt::AlignCenter | Qt::TextWordWrap,
                   m_stateMessage.isEmpty() ? QStringLiteral("视频信号等待接入") : m_stateMessage);
        p.setFont(Theme::font(9));
        p.setPen(Theme::textMuted());
        p.drawText(r.adjusted(0, 62, 0, 0), Qt::AlignCenter,
                   m_state == CameraState::Opening ? QStringLiteral("正在尝试 DirectShow / Media Foundation")
                                                   : QStringLiteral("摄像头 0 · 点击开始播放"));
    } else {
        const QRectF status(videoRect.left() + 12, videoRect.top() + 12, 130, 28);
        p.setPen(QPen(alpha(Theme::iceCyan(), 150), 1));
        p.setBrush(alpha(Theme::backgroundDeep(), 190));
        p.drawRoundedRect(status, 5, 5);
        p.setFont(Theme::font(8, true));
        p.setPen(Theme::iceCyan());
        p.drawText(status, Qt::AlignCenter, QStringLiteral("摄像头 0 · 实时"));
    }
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
