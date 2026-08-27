#include "CameraController.h"

#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cmath>

struct CameraFrameBuffer
{
    QMutex mutex;
    QImage latestFrame;
};

namespace {

QImage imageFromMat(const cv::Mat &frame)
{
    if (frame.empty())
        return QImage();

    cv::Mat converted;
    QImage::Format format = QImage::Format_Invalid;

    switch (frame.channels()) {
    case 1:
        converted = frame;
        format = QImage::Format_Grayscale8;
        break;
    case 3:
        cv::cvtColor(frame, converted, cv::COLOR_BGR2RGB);
        format = QImage::Format_RGB888;
        break;
    case 4:
        cv::cvtColor(frame, converted, cv::COLOR_BGRA2RGBA);
        format = QImage::Format_RGBA8888;
        break;
    default:
        return QImage();
    }

    const QImage wrapped(converted.data, converted.cols, converted.rows,
                         static_cast<int>(converted.step), format);
    return wrapped.copy();
}

bool openCamera(cv::VideoCapture &capture, int deviceIndex, int backend)
{
    capture.release();
    try {
        return capture.open(deviceIndex, backend);
    } catch (const cv::Exception &) {
        return false;
    }
}

QString backendName(int backend)
{
    return backend == cv::CAP_DSHOW ? QStringLiteral("DirectShow")
                                    : QStringLiteral("Media Foundation");
}

} // namespace

CameraWorker::CameraWorker(int deviceIndex, CameraFrameBuffer *frameBuffer, QObject *parent)
    : QObject(parent), m_deviceIndex(deviceIndex), m_frameBuffer(frameBuffer)
{
}

void CameraWorker::requestStop()
{
    m_stopRequested.store(true, std::memory_order_release);
}

void CameraWorker::capture()
{
    cv::VideoCapture capture;
    int activeBackend = cv::CAP_DSHOW;

    bool opened = openCamera(capture, m_deviceIndex, activeBackend);
    if (!opened && !m_stopRequested.load(std::memory_order_acquire)) {
        activeBackend = cv::CAP_MSMF;
        opened = openCamera(capture, m_deviceIndex, activeBackend);
    }

    if (!opened) {
        if (m_stopRequested.load(std::memory_order_acquire))
            emit stateChanged(CameraState::Stopped, QStringLiteral("摄像头已停止"));
        else
            emit stateChanged(CameraState::Error,
                              QStringLiteral("无法打开摄像头 %1，请检查设备连接、占用状态和 Windows 相机权限")
                                  .arg(m_deviceIndex));
        emit finished();
        return;
    }

    try {
        capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        capture.set(cv::CAP_PROP_FRAME_WIDTH, 1920.0);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, 1080.0);
        capture.set(cv::CAP_PROP_FPS, 30.0);
        capture.set(cv::CAP_PROP_BUFFERSIZE, 1.0);
    } catch (const cv::Exception &) {
        // 部分驱动不支持全部请求属性；继续使用设备实际规格。
    }

    CameraStreamInfo info;
    info.deviceIndex = m_deviceIndex;
    info.width = qMax(0, qRound(capture.get(cv::CAP_PROP_FRAME_WIDTH)));
    info.height = qMax(0, qRound(capture.get(cv::CAP_PROP_FRAME_HEIGHT)));
    info.fps = capture.get(cv::CAP_PROP_FPS);
    if (!std::isfinite(info.fps) || info.fps < 0.0)
        info.fps = 0.0;
    info.backend = backendName(activeBackend);

    emit streamOpened(info);
    emit stateChanged(CameraState::Streaming, QStringLiteral("摄像头画面已连接"));

    int consecutiveFailures = 0;
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        cv::Mat frame;
        bool readOk = false;
        try {
            readOk = capture.read(frame);
        } catch (const cv::Exception &) {
            readOk = false;
        }

        if (!readOk || frame.empty()) {
            ++consecutiveFailures;
            if (consecutiveFailures >= 45) {
                emit stateChanged(CameraState::Error,
                                  QStringLiteral("摄像头连续读取失败，设备可能已断开或被其他程序占用"));
                break;
            }
            QThread::msleep(20);
            continue;
        }

        consecutiveFailures = 0;
        QImage image = imageFromMat(frame);
        if (image.isNull())
            continue;

        QMutexLocker locker(&m_frameBuffer->mutex);
        m_frameBuffer->latestFrame = image;
    }

    capture.release();
    if (m_stopRequested.load(std::memory_order_acquire))
        emit stateChanged(CameraState::Stopped, QStringLiteral("摄像头已停止"));
    emit finished();
}

CameraController::CameraController(QObject *parent)
    : QObject(parent), m_frameBuffer(new CameraFrameBuffer)
{
    qRegisterMetaType<CameraState>("CameraState");
    qRegisterMetaType<CameraStreamInfo>("CameraStreamInfo");
}

CameraController::~CameraController()
{
    if (m_worker)
        m_worker->requestStop();
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    delete m_frameBuffer;
}

void CameraController::startCamera(int deviceIndex)
{
    if (m_thread)
        return;

    {
        QMutexLocker locker(&m_frameBuffer->mutex);
        m_frameBuffer->latestFrame = QImage();
    }

    m_state.store(static_cast<int>(CameraState::Opening), std::memory_order_release);
    emit stateChanged(CameraState::Opening, QStringLiteral("正在连接摄像头 %1…").arg(deviceIndex));

    QThread *thread = new QThread(this);
    CameraWorker *worker = new CameraWorker(deviceIndex, m_frameBuffer);
    worker->moveToThread(thread);
    m_thread = thread;
    m_worker = worker;

    connect(thread, &QThread::started, worker, &CameraWorker::capture);
    connect(worker, &CameraWorker::stateChanged, this,
            [this](CameraState state, const QString &message) {
        m_state.store(static_cast<int>(state), std::memory_order_release);
        emit stateChanged(state, message);
    });
    connect(worker, &CameraWorker::streamOpened,
            this, &CameraController::streamOpened);
    connect(worker, &CameraWorker::finished, thread, &QThread::quit);
    connect(worker, &CameraWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread] {
        const CameraState lastState = static_cast<CameraState>(m_state.load(std::memory_order_acquire));
        if (m_thread == thread) {
            m_thread = nullptr;
            m_worker = nullptr;
        }
        if (lastState == CameraState::Error) {
            QMutexLocker locker(&m_frameBuffer->mutex);
            m_frameBuffer->latestFrame = QImage();
        }
        thread->deleteLater();
    });
    thread->start();
}

void CameraController::stopCamera()
{
    if (!m_worker) {
        m_state.store(static_cast<int>(CameraState::Stopped), std::memory_order_release);
        emit stateChanged(CameraState::Stopped, QStringLiteral("摄像头已停止"));
        return;
    }
    m_state.store(static_cast<int>(CameraState::Stopped), std::memory_order_release);
    m_worker->requestStop();
}

bool CameraController::isRunning() const
{
    const CameraState state = static_cast<CameraState>(m_state.load(std::memory_order_acquire));
    return state == CameraState::Opening || state == CameraState::Streaming;
}

QImage CameraController::takeLatestFrame()
{
    QMutexLocker locker(&m_frameBuffer->mutex);
    QImage frame;
    frame.swap(m_frameBuffer->latestFrame);
    return frame;
}
