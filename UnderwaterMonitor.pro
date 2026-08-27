QT += core gui widgets webengine webenginewidgets webchannel

CONFIG += c++11
TEMPLATE = app
TARGET = UnderwaterMonitor

OPENCV_ROOT = $$PWD/third_party/opencv
INCLUDEPATH += $$OPENCV_ROOT/include

SOURCES += \
    main.cpp \
    Theme.cpp \
    GeoCoordinateUtils.cpp \
    LocationController.cpp \
    CameraController.cpp \
    EffectController.cpp \
    ParticleSystem.cpp \
    Widgets.cpp \
    DemoDataModel.cpp \
    MainWindow.cpp \
    Pages.cpp

HEADERS += \
    Theme.h \
    GeoCoordinateUtils.h \
    LocationTypes.h \
    LocationController.h \
    CameraTypes.h \
    CameraController.h \
    Actions.h \
    RouteTypes.h \
    TelemetryTypes.h \
    EffectController.h \
    ParticleSystem.h \
    Widgets.h \
    DemoDataModel.h \
    MainWindow.h \
    Pages.h

win32:CONFIG += windows
win32-g++:QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
win32-msvc {
    LIBS += -ldwmapi
    QMAKE_CXXFLAGS += /utf-8
    # Qt 5.15.2 qmake can emit invalid relative dependencies for external
    # Qt headers when used with jom. The compiler include paths stay intact.
    CONFIG -= depend_includepath

    CONFIG(debug, debug|release) {
        LIBS += -L$$OPENCV_ROOT/x64/vc15/lib -lopencv_world453d
        OPENCV_RUNTIME_DIR = $$shell_path($$OUT_PWD/debug)
        OPENCV_WORLD_DLL = $$shell_path($$OPENCV_ROOT/x64/vc15/bin/opencv_world453d.dll)
        OPENCV_MSMF_DLL = $$shell_path($$OPENCV_ROOT/x64/vc15/bin/opencv_videoio_msmf453_64d.dll)
    } else {
        LIBS += -L$$OPENCV_ROOT/x64/vc15/lib -lopencv_world453
        OPENCV_RUNTIME_DIR = $$shell_path($$OUT_PWD/release)
        OPENCV_WORLD_DLL = $$shell_path($$OPENCV_ROOT/x64/vc15/bin/opencv_world453.dll)
        OPENCV_MSMF_DLL = $$shell_path($$OPENCV_ROOT/x64/vc15/bin/opencv_videoio_msmf453_64.dll)
    }

    QMAKE_POST_LINK += $$QMAKE_COPY_FILE $$quote($$OPENCV_WORLD_DLL) $$quote($$OPENCV_RUNTIME_DIR) $$escape_expand(\n\t)
    QMAKE_POST_LINK += $$QMAKE_COPY_FILE $$quote($$OPENCV_MSMF_DLL) $$quote($$OPENCV_RUNTIME_DIR)
}
