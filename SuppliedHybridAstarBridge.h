#pragma once

#include "MissionTypes.h"

struct SuppliedHybridAstarParameters {
    double shoreSafetyMeters = 3.0;
    double boundaryPointSpacingMeters = 2.0;
    double maximumPlanningDistanceMeters = 5000.0;
};

// Application-side mirror of the constants in the supplied implementation.
// Keeping this in the adapter lets the UI and grid code use the same contract
// without editing the third-party source.
const SuppliedHybridAstarParameters &suppliedHybridAstarParameters();

// Calls the supplied, unmodified geographic Hybrid A* implementation through
// a project-side adapter.  The supplied source keeps its own GeoPoint/GeoPose
// types; callers only see the project's request/result types.
RoutePlanningResult runSuppliedHybridAstar(const RoutePlanningRequest &request);

// Builds the obstacle point cloud from the connected water region containing
// both the start and target. Boundaries are closed and sampled at roughly
// four-metre intervals, matching the input passed to the supplied algorithm.
QVector<QPointF> buildWaterwayObstaclePoints(const RoutePlanningRequest &request,
                                             QString *errorMessage = nullptr);
