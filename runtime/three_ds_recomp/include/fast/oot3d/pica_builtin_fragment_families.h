#pragma once
#include "fast/renderer3ds/pica_shader_source_identity.h"
#include <cstdint>
#include <span>

namespace Fast::Oot3d {
// The built-in SPIR-V of a specialized fragment program: the native, temporal
// and toon families, in that order, consulted before the portable pack. The
// runtime and device preparation both resolve through it, so a prepared
// pipeline is the pipeline a game session creates. Empty when no family has it.
[[nodiscard]] std::span<const uint32_t> FindPicaBuiltInFragmentSpirv(
    const Renderer3ds::PicaShaderSourceIdentity& source, bool separateSamplers);
} // namespace Fast::Oot3d
