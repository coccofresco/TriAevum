#pragma once

#include "fast/oot3d/grass_interaction_bridge.h"

#include <cstdint>
#include <vector>

namespace Oot3dNativeGame {
class NativeA32Memory;

// Read-only title adapter. The renderer receives world-space colliders, never
// guest addresses/layouts.
//
// Collect reads guest memory; publishing hands the colliders to the renderer.
// They are separate so the read can happen on the guest side and the publish
// in renderer order.
std::vector<Fast::Oot3d::GrassInteractor>
CollectNativeActorInteractions(const NativeA32Memory &memory,
                               uint32_t playState, uint64_t sourceFrame);
void PublishNativeActorInteractions(const NativeA32Memory &memory,
                                    uint32_t playState, uint64_t sourceFrame);
} // namespace Oot3dNativeGame
