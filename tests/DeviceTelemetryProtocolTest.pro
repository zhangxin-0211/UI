QT += core network testlib
CONFIG += c++11 testcase console
TEMPLATE = app
TARGET = DeviceTelemetryProtocolTest
win32-msvc: QMAKE_CXXFLAGS += /utf-8

INCLUDEPATH += $$PWD/..
SOURCES += \
    DeviceTelemetryProtocolTest.cpp \
    ../DeviceTelemetryProtocol.cpp \
    ../RobotCommunication.cpp \
    ../GeoCoordinateUtils.cpp
HEADERS += \
    ../DeviceTelemetryProtocol.h \
    ../DeviceNetworkConfig.h \
    ../GeoCoordinateUtils.h \
    ../MissionTypes.h \
    ../RobotCommunication.h \
    ../RouteTypes.h
