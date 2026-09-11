#pragma once

#include <QObject>
#include <QThread>

#include <memory>

#include "MissionTypes.h"

class IRoutePlanner
{
public:
    virtual ~IRoutePlanner() = default;
    virtual RoutePlanningResult plan(const RoutePlanningRequest &request) = 0;
};

// Adapter for the supplied geographic Hybrid A* implementation. The bridge
// converts the selected waterway boundary to obstacle points and converts the
// original GCJ-02 planning result back to the project result type.
class HybridAstarRoutePlanner final : public IRoutePlanner
{
public:
    RoutePlanningResult plan(const RoutePlanningRequest &request) override;
};

class PlannerWorker : public QObject
{
    Q_OBJECT
public slots:
    void planExternal(const RoutePlanningRequest &request);

public:
    void setExternalPlanner(const std::shared_ptr<IRoutePlanner> &planner)
    { m_externalPlanner = planner; }

signals:
    void planningFinished(const RoutePlanningResult &result);

private:
    std::shared_ptr<IRoutePlanner> m_externalPlanner;
};

class PlannerController : public QObject
{
    Q_OBJECT
public:
    explicit PlannerController(QObject *parent = nullptr);
    ~PlannerController() override;

    void setExternalPlanner(const std::shared_ptr<IRoutePlanner> &planner);
    void planWithExternal(const RoutePlanningRequest &request);
    bool hasExternalPlanner() const { return bool(m_externalPlanner); }

signals:
    void externalPlanningRequested(const RoutePlanningRequest &request);
    void planningFinished(const RoutePlanningResult &result);

private:
    QThread m_workerThread;
    PlannerWorker *m_worker = nullptr;
    std::shared_ptr<IRoutePlanner> m_externalPlanner;
};
