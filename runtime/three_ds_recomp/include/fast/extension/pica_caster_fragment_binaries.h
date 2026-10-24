#pragma once
// Generated from maintained PICA equations (parametric bases), not captured game shaders.
// Regenerate pica_caster_fragment.spvpack with
// tools/renderer/tev_program/build_fragment_artifacts --casters.
#include "fast/renderer/pass_shader_artifact.h"

namespace Fast::Extension {
std::span<const Renderer::PassShaderArtifact> CasterFragmentArtifacts();
} // namespace Fast::Extension
