#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QtWebEngine/QtWebEngine>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (qEnvironmentVariableIsEmpty("QT_OPENGL"))
        qputenv("QT_OPENGL", QByteArrayLiteral("angle"));
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#endif

    QtWebEngine::initialize();
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Underwater Robotics Lab"));
    QCoreApplication::setApplicationName(QStringLiteral("UnderwaterMonitor"));

    MainWindow window;
    window.show();
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test")))
        QTimer::singleShot(1200, &app, &QCoreApplication::quit);
    return app.exec();
}
