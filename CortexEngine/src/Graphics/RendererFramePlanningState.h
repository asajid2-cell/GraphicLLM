#pragma once

#include "Graphics/BudgetPlanner.h"
#include "Graphics/RendererSceneSnapshot.h"
#include "Graphics/RTScheduler.h"

namespace Cortex::Graphics {

struct RendererFramePlanningState {
    RendererSceneSnapshot sceneSnapshot;
    RTFramePlan rtPlan;
    RendererBudgetPlan budgetPlan;
};

} // namespace Cortex::Graphics
