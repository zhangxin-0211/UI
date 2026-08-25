#pragma once

#include <QColor>
#include <QFont>
#include <QLinearGradient>
#include <QRectF>
#include <QString>

namespace Theme {

QColor background();
QColor backgroundDeep();
QColor panel();
QColor panelAlt();
QColor panelRaised();
QColor glow();
QColor accent();
QColor border();
QColor titleStart();
QColor titleEnd();
QColor text();
QColor textMuted();
QColor value();
QColor warning();
QColor fault();
QColor grid();
QColor curveMint();
QColor curveCoral();
QColor plasmaViolet();
QColor plasmaLight();
QColor iceCyan();
QLinearGradient backgroundGradient(const QRectF &rect);
QLinearGradient panelGradient(const QRectF &rect);
QLinearGradient activeGradient(const QRectF &rect);
QLinearGradient energyGradient(const QRectF &rect);
QFont font(int pointSize, bool bold = false);
QString commonStyle();

}
