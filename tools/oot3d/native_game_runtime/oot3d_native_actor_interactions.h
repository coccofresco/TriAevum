#pragma once

#include <cstdint>

namespace Oot3dNativeGame {
class NativeA32Memory;

// Read-only title adapter. The renderer receives world-space colliders, never
// guest addresses/layouts.
void PublishNativeActorInteractions(const NativeA32Memory &memory,
                                    uint32_t playState, uint64_t sourceFrame);
} // namespace Oot3dNativeGame
