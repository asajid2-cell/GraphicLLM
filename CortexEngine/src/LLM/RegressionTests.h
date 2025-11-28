#pragma once

namespace Cortex {
namespace Scene { class ECS_Registry; }
namespace Graphics { class Renderer; }
}

namespace Cortex::LLM {

// Lightweight, headless-style regression tests for the command pipeline.
// They operate on a temporary registry and renderer and log results via spdlog.
void RunRegressionTests();

} // namespace Cortex::LLM

