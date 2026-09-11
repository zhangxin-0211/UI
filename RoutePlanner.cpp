#include "RoutePlanner.h"
#include "SuppliedHybridAstarBridge.h"

RoutePlanningResult HybridAstarRoutePlanner::plan(const RoutePlanningRequest &request)
{
    // The supplied main(1).cpp implementation is kept unmodified and called
    // through the project-side bridge. The bridge converts the selected
    // waterway boundary into the original algorithm's obstacle points.
    return runSuppliedHybridAstar(request);
}

void PlannerWorker::planExternal(const RoutePlanningRequest &request)
{
    if (!m_externalPlanner) {
        RoutePlanningResult result;
        result.missionId = request.missionId;
        result.revision = request.waterway.revision;
        result.errorMessage = QStringLiteral("正式路径算法待接入");
        emit planningFinished(result);
        return;
    }
    RoutePlanningResult result = m_externalPlanner->plan(request);
    if (result.missionId.isEmpty()) result.missionId = request.missionId;
    result.revision = request.waterway.revision;
    emit planningFinished(result);
}

PlannerController::PlannerController(QObject *parent) : QObject(parent)
{
    m_worker = new PlannerWorker;
    m_worker->moveToThread(&m_workerThread);
    connect(this, &PlannerController::externalPlanningRequested,
            m_worker, &PlannerWorker::planExternal, Qt::QueuedConnection);
    connect(m_worker, &PlannerWorker::planningFinished,
            this, &PlannerController::planningFinished, Qt::QueuedConnection);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_workerThread.start();
}

void PlannerController::setExternalPlanner(const std::shared_ptr<IRoutePlanner> &planner)
{
    m_externalPlanner = planner;
    PlannerWorker *worker = m_worker;
    const std::shared_ptr<IRoutePlanner> plannerCopy = planner;
    QMetaObject::invokeMethod(m_worker, [worker, plannerCopy] {
        worker->setExternalPlanner(plannerCopy);
    }, Qt::QueuedConnection);
}

PlannerController::~PlannerController()
{
    m_workerThread.quit();
    m_workerThread.wait();
}

void PlannerController::planWithExternal(const RoutePlanningRequest &request)
{
    emit externalPlanningRequested(request);
}
