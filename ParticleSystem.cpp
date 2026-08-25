#include "ParticleSystem.h"
#include "Theme.h"

#include <QImage>
#include <QPainter>
#include <QHash>
#include <QRandomGenerator>
#include <QRadialGradient>
#include <QtMath>

namespace {

QImage softParticleTexture(const QColor &sourceColor)
{
    static QHash<QRgb, QImage> cache;
    QColor keyColor = sourceColor;
    keyColor.setAlpha(255);
    const QRgb key = keyColor.rgba();
    const auto found = cache.constFind(key);
    if (found != cache.constEnd())
        return found.value();

    constexpr int textureSize = 64;
    QImage texture(textureSize, textureSize, QImage::Format_ARGB32_Premultiplied);
    texture.fill(Qt::transparent);
    QPainter painter(&texture);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor center = keyColor.lighter(145);
    QColor middle = keyColor;
    QColor edge = keyColor;
    edge.setAlpha(0);
    QRadialGradient gradient(QPointF(textureSize / 2.0, textureSize / 2.0), textureSize / 2.0);
    gradient.setColorAt(0.0, center);
    gradient.setColorAt(0.42, middle);
    gradient.setColorAt(1.0, edge);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(QRectF(0, 0, textureSize, textureSize));
    cache.insert(key, texture);
    return texture;
}

}

ParticleSystem::ParticleSystem(int maximum, Mode mode)
    : m_maximum(qMax(1, maximum)), m_mode(mode)
{
}

void ParticleSystem::setMaximum(int maximum)
{
    m_maximum = qMax(1, maximum);
    while (m_particles.size() > m_maximum)
        m_particles.removeLast();
}

qreal ParticleSystem::random(qreal minimum, qreal maximum) const
{
    return minimum + (maximum - minimum) * QRandomGenerator::global()->generateDouble();
}

void ParticleSystem::spawnAmbient(const QRectF &bounds, qreal phase)
{
    Particle particle;
    particle.life = particle.maximumLife = random(1.4, 3.8);
    particle.color = (QRandomGenerator::global()->bounded(4) == 0) ? Theme::plasmaViolet() : Theme::iceCyan();

    if (m_mode == Mode::Fog) {
        particle.position = QPointF(random(bounds.left() + bounds.width() * 0.12, bounds.right() - bounds.width() * 0.12),
                                    random(bounds.center().y() + bounds.height() * 0.18, bounds.bottom() - 4.0));
        particle.velocity = QPointF(random(-4.5, 4.5) + qSin(phase * 0.8) * 1.2, random(-13.0, -5.0));
        particle.size = random(5.0, 13.0);
    } else {
        particle.angle = random(0.0, M_PI * 2.0);
        const qreal diameter = qMin(bounds.width(), bounds.height());
        particle.radius = random(diameter * 0.37, diameter * 0.49);
        particle.angularSpeed = random(0.18, 0.58) * (QRandomGenerator::global()->bounded(2) ? 1.0 : -1.0);
        particle.size = random(1.2, 3.8);
        particle.position = bounds.center() + QPointF(qCos(particle.angle), qSin(particle.angle)) * particle.radius;
    }
    m_particles.append(particle);
}

void ParticleSystem::update(qreal deltaSeconds, const QRectF &bounds, qreal intensity, qreal phase)
{
    const int ambientTarget = qBound(1, qRound(m_maximum * qBound(0.2, intensity, 1.0)), m_maximum);
    int ambientCount = 0;
    for (const Particle &particle : m_particles)
        if (!particle.pulse) ++ambientCount;
    while (ambientCount < ambientTarget && m_particles.size() < m_maximum) {
        spawnAmbient(bounds, phase);
        ++ambientCount;
    }
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        Particle &particle = m_particles[i];
        particle.life -= deltaSeconds;
        if (particle.life <= 0.0) {
            m_particles.remove(i);
            continue;
        }
        if (particle.pulse || m_mode == Mode::Fog) {
            particle.position += particle.velocity * deltaSeconds;
            particle.velocity *= qPow(0.92, deltaSeconds * 30.0);
            if (!particle.pulse)
                particle.velocity.rx() += qSin(phase * 1.4 + i) * deltaSeconds * 2.0;
        } else {
            particle.angle += particle.angularSpeed * deltaSeconds * (0.6 + intensity);
            particle.position = bounds.center() + QPointF(qCos(particle.angle), qSin(particle.angle)) * particle.radius;
        }
    }
}

void ParticleSystem::triggerPulse(const QPointF &center, int count, const QColor &color)
{
    const int available = qMax(0, m_maximum - m_particles.size());
    const int actual = qMin(count, available);
    for (int i = 0; i < actual; ++i) {
        Particle particle;
        const qreal angle = random(0.0, M_PI * 2.0);
        const qreal speed = random(18.0, 52.0);
        particle.position = center + QPointF(random(-3.0, 3.0), random(-3.0, 3.0));
        particle.velocity = QPointF(qCos(angle), qSin(angle)) * speed;
        particle.life = particle.maximumLife = random(0.45, 1.1);
        particle.size = random(1.5, 4.5);
        particle.color = (i % 3 == 0) ? Theme::iceCyan() : color;
        particle.pulse = true;
        m_particles.append(particle);
    }
}

void ParticleSystem::paint(QPainter &painter, qreal opacity) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    for (const Particle &particle : m_particles) {
        const qreal progress = qBound(0.0, particle.life / qMax(0.001, particle.maximumLife), 1.0);
        const qreal fade = qSin(progress * M_PI) * opacity;
        const qreal size = particle.size * (particle.pulse ? (0.55 + progress) : (1.35 - progress * 0.35));
        const qreal halfWidth = size * 1.8;
        const qreal halfHeight = particle.pulse ? halfWidth : size;
        const QRectF target(particle.position.x() - halfWidth,
                            particle.position.y() - halfHeight,
                            halfWidth * 2.0,
                            halfHeight * 2.0);
        painter.setOpacity(qBound(0.0, fade * (particle.pulse ? 0.85 : 0.34), 1.0));
        const QImage texture = softParticleTexture(particle.color);
        painter.drawImage(target, texture, texture.rect());
    }
    painter.setOpacity(1.0);
    painter.restore();
}

void ParticleSystem::clear() { m_particles.clear(); }
