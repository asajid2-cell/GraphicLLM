#pragma once
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
#include "Graphics/HyperGeometry/HyperGeometryEngine.h"
#else
namespace Cortex { namespace Graphics { namespace HyperGeometry {
class HyperGeometryEngine;
}}}
#endif
