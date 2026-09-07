#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <span>

namespace Oot3dNativeGame {

// The packed registry is a compatibility input, not the authority for a
// whole-AOT product. A registry-less native backend is dispatched through the
// NativeBlockCallback path in the pinned runtime.
const oot3d::recomp::a32::Registry& Oot3dNativeA32Registry() noexcept;
void ConfigureOot3dNativeA32Candidates(
    std::span<const uint32_t> entryPoints) noexcept;

} // namespace Oot3dNativeGame
