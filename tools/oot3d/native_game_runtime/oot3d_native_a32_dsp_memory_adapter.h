#pragma once

#include "oot3d_native_a32_dsp_hle.h"

#include <vector>

namespace Oot3dNativeGame {

struct NativeA32ProcessImageManifest;

std::vector<NativeA32DspPhysicalRegion>
BuildNativeA32DspPhysicalRegions(const NativeA32ProcessImageManifest &manifest);

} // namespace Oot3dNativeGame
