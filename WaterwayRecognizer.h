#pragma once

#include <QObject>
#include <QThread>

#include "MissionTypes.h"

class WaterwayRecognitionWorker : public QObject
{
    Q_OBJECT
public slots:
    void recognize(const WaterwayRecognitionRequest &request);

public:
    explicit WaterwayRecognitionWorker(QObject *parent = nullptr);
    ~WaterwayRecognitionWorker() override;

signals:
    void recognitionFinished(const WaterwayRecognitionResult &result);

};

class WaterwayRecognizer : public QObject
{
    Q_OBJECT
public:
    explicit WaterwayRecognizer(QObject *parent = nullptr);
    ~WaterwayRecognizer() override;

    void recognize(const WaterwayRecognitionRequest &request);

signals:
    void recognitionRequested(const WaterwayRecognitionRequest &request);
    void recognitionFinished(const WaterwayRecognitionResult &result);

private:
    QThread m_workerThread;
    WaterwayRecognitionWorker *m_worker = nullptr;
};
