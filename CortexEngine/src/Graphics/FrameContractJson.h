#pragma once

#include "Graphics/FrameContract.h"

#include <nlohmann/json_fwd.hpp>

namespace Cortex::Graphics {

[[nodiscard]] nlohmann::json FrameContractToJson(const FrameContract& contract);

} // namespace Cortex::Graphics
