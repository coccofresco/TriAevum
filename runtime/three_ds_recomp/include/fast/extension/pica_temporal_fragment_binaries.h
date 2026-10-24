#pragma once
// Generated from maintained PICA equations, not captured game shaders.
// Vulkan 1.1, shaderc performance optimization; canonical and NRI interfaces.
// Optional effect family, isolated from canonical programs. Regenerate
// pica_temporal_fragment.spvpack with
// tools/renderer/tev_program/build_fragment_artifacts --temporal.
#include "fast/renderer3ds/pica_fragment_artifact.h"

namespace Fast::Extension {
std::span<const Renderer3ds::PicaFragmentArtifact> TemporalFragmentArtifacts();
} // namespace Fast::Extension
