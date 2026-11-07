#pragma once
// Generated from maintained PICA equations and effect compositions.
// Regenerate pica_uber_fragment.spvpack with
// tools/renderer/tev_program/build_fragment_artifacts --uber.
#include "fast/renderer3ds/pica_fragment_artifact.h"

namespace Fast::Extension {
std::span<const Renderer3ds::PicaFragmentArtifact> UberFragmentArtifacts();
// Preserve the existing startup workload for titles without native-lighting policy.
std::span<const Renderer3ds::PicaFragmentArtifact> LegacyUberFragmentArtifacts();
} // namespace Fast::Extension
