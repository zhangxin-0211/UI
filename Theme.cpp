#include "Theme.h"

namespace Theme {

QColor background() { return QColor(QStringLiteral("#0B1120")); }
QColor backgroundDeep() { return QColor(QStringLiteral("#050916")); }
QColor panel() { return QColor(QStringLiteral("#09172C")); }
QColor panelAlt() { return QColor(QStringLiteral("#102A50")); }
QColor panelRaised() { return QColor(QStringLiteral("#15376A")); }
QColor glow() { return QColor(QStringLiteral("#00D2FF")); }
QColor accent() { return QColor(QStringLiteral("#2563EB")); }
QColor border() { return QColor(QStringLiteral("#00F2FE")); }
QColor titleStart() { return QColor(QStringLiteral("#DDF8FF")); }
QColor titleEnd() { return QColor(QStringLiteral("#60A5FA")); }
QColor text() { return QColor(QStringLiteral("#C8EEFF")); }
QColor textMuted() { return QColor(QStringLiteral("#789DBD")); }
QColor value() { return QColor(QStringLiteral("#B8F4FF")); }
QColor warning() { return QColor(QStringLiteral("#FFD45A")); }
QColor fault() { return QColor(QStringLiteral("#FF6B6B")); }
QColor grid() { return QColor(37, 99, 235, 62); }
QColor curveMint() { return QColor(QStringLiteral("#00F2FE")); }
QColor curveCoral() { return QColor(QStringLiteral("#FF746C")); }
QColor plasmaViolet() { return QColor(QStringLiteral("#A855F7")); }
QColor plasmaLight() { return QColor(QStringLiteral("#C084FC")); }
QColor iceCyan() { return QColor(QStringLiteral("#00F2FE")); }

QLinearGradient backgroundGradient(const QRectF &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, backgroundDeep());
    gradient.setColorAt(0.48, background());
    gradient.setColorAt(1.0, QColor(QStringLiteral("#071B38")));
    return gradient;
}

QLinearGradient panelGradient(const QRectF &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, panel());
    gradient.setColorAt(0.58, QColor(QStringLiteral("#0C203D")));
    gradient.setColorAt(1.0, panelAlt());
    return gradient;
}

QLinearGradient activeGradient(const QRectF &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.topRight());
    gradient.setColorAt(0.0, QColor(QStringLiteral("#163C86")));
    gradient.setColorAt(0.48, QColor(QStringLiteral("#2563EB")));
    gradient.setColorAt(0.72, iceCyan());
    gradient.setColorAt(1.0, plasmaViolet());
    return gradient;
}

QLinearGradient energyGradient(const QRectF &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, QColor(QStringLiteral("#0066FF")));
    gradient.setColorAt(0.42, iceCyan());
    gradient.setColorAt(0.74, plasmaViolet());
    gradient.setColorAt(1.0, plasmaLight());
    return gradient;
}

QFont font(int pointSize, bool bold)
{
    QFont f(QStringLiteral("Microsoft YaHei UI"), pointSize);
    if (!f.exactMatch())
        f = QFont(QStringLiteral("Microsoft YaHei"), pointSize);
    f.setBold(bold);
    return f;
}

QString commonStyle()
{
    return QStringLiteral(
        "QWidget { color: #C8EEFF; font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', Arial; }"
        "QToolTip { color: #DDF8FF; background: #09172C; border: 1px solid #00D2FF; padding: 5px 8px; }"
        "QMessageBox { background: #F5FAFF; }"
        "QMessageBox QLabel { color: #102A50; font-weight: 600; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { background: #0B1D37; border: 1px solid #285AA5; border-radius: 5px; padding: 7px 10px; color: #DDF8FF; selection-background-color: #2563EB; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #00F2FE; }"
        "QComboBox::drop-down { border: 0px; width: 26px; }"
        "QComboBox QAbstractItemView { background: #09172C; color: #DDF8FF; selection-background-color: #2563EB; border: 1px solid #00D2FF; }"
        "QTabWidget::pane { background: #071327; border: 1px solid #285AA5; border-radius: 7px; top: -1px; }"
        "QTabBar::tab { background: #0B1D37; color: #789DBD; border: 1px solid #285AA5; border-bottom: 0px; padding: 8px 10px; min-width: 58px; }"
        "QTabBar::tab:selected { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #1647A0,stop:0.65 #2563EB,stop:1 #7C3AED); color: #FFFFFF; border-color: #00F2FE; }"
        "QTabBar::tab:hover:!selected { color: #DDF8FF; border-color: #00D2FF; }"
        "QCheckBox { color: #C8EEFF; spacing: 7px; }"
        "QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #285AA5; border-radius: 3px; background: #071327; }"
        "QCheckBox::indicator:checked { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #00D2FF,stop:1 #2563EB); border-color: #00F2FE; }"
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #102A50,stop:0.55 #15376A,stop:1 #172650); border: 1px solid #285AA5; border-radius: 7px; padding: 8px 16px; color: #C8EEFF; }"
        "QPushButton:disabled { background: #0A172C; border: 1px solid #193457; color: #52708F; }"
        "QPushButton#acceptSnapButton:enabled { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #8B5A00,stop:0.5 #D49A19,stop:1 #6E4A0A); border-color: #FFD45A; color: #FFF4C2; }"
        "QPushButton#acceptSnapButton:enabled:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #C77D00,stop:0.5 #FFD45A,stop:1 #A855F7); color: #FFFFFF; }"
        "QPushButton#planRouteButton:enabled { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #006B82,stop:0.5 #1677D2,stop:1 #5B35A8); border-color: #00F2FE; color: #FFFFFF; }"
        "QPushButton#uploadRouteButton:enabled { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #006B82,stop:0.5 #00A6C7,stop:1 #2563EB); border-color: #00F2FE; color: #FFFFFF; }"
        "#routeControlContainer { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #07162E,stop:0.5 #0A2344,stop:1 #102F58); border: 1px solid #163E70; border-radius: 12px; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #1647A0,stop:0.55 #2563EB,stop:1 #7C3AED); border-color: #00F2FE; color: #FFFFFF; }"
        "QPushButton:pressed { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #00D2FF,stop:1 #A855F7); color: #050916; }"
        "QTableWidget { background: #071327; alternate-background-color: #0B1D37; gridline-color: #173E70; border: 1px solid #285AA5; color: #C8EEFF; }"
        "QHeaderView::section { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #102A50,stop:0.55 #15376A,stop:1 #172650); color: #DDF8FF; border: 0px; border-right: 1px solid #285AA5; border-bottom: 1px solid #285AA5; padding: 8px; }"
        "QScrollBar:vertical { background: #071327; width: 10px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: #2563EB; min-height: 24px; border-radius: 5px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );
}

}
