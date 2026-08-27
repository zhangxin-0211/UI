#pragma once

#include <QObject>
#include <QImage>
#include <QPointer>

#include <atomic>

#include "CameraTypes.h"

class QThread;
struct CameraFrameBuffer;

class CameraWorker : public QObject
{
    Q_OBJECT

public:
    CameraWorker(int deviceIndex, CameraFrameBuffer *frameBuffer, QObject *parent = nullptr);
    void requestStop();

public slots:
    void capture();

signals:
    void stateChanged(CameraState state, const QString &message);
    void streamOpened(const CameraStreamInfo &info);
    void finished();

private:
    int m_deviceIndex = 0;
    CameraFrameBuffer *m_frameBuffer = nullptr;
    std::atomic_bool m_stopRequested{false};
};

class CameraController : public QObject
{
    Q_OBJECT

public:
    explicit CameraController(QObject *parent = nullptr);
    ~CameraController() override;

    void startCamera(int deviceIndex = 0);
    void stopCamera();
    bool isRunning() const;
    QImage takeLatestFrame();

signals:
    void stateChanged(CameraState state, const QString &message);
    void streamOpened(const CameraStreamInfo &info);

private:
    QThread *m_thread = nullptr;
    QPointer<CameraWorker> m_worker;
    CameraFrameBuffer *m_frameBuffer = nullptr;
    std::atomic<int> m_state{static_cast<int>(CameraState::Stopped)};
};
