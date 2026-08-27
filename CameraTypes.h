#pragma once

#include <QMetaType>
#include <QString>

enum class CameraState {
    Stopped,
    Opening,
    Streaming,
    Error
};

struct CameraStreamInfo {
    int deviceIndex = 0;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    QString backend;
};

Q_DECLARE_METATYPE(CameraState)
Q_DECLARE_METATYPE(CameraStreamInfo)
