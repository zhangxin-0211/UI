QT += core gui widgets

CONFIG += c++11
TEMPLATE = app
TARGET = UnderwaterMonitor

SOURCES += \
    main.cpp \
    Theme.cpp \
    EffectController.cpp \
    ParticleSystem.cpp \
    Widgets.cpp \
    DemoDataModel.cpp \
    MainWindow.cpp \
    Pages.cpp

HEADERS += \
    Theme.h \
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
