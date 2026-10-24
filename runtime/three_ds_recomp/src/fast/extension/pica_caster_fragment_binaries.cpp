// Generated from maintained PICA equations (parametric bases), not captured game shaders.
// Regenerate pica_caster_fragment.spvpack with
// tools/renderer/tev_program/build_fragment_artifacts --casters.
#include "fast/extension/pica_caster_fragment_binaries.h"
#include "fast/renderer/spirv_pack.h"

namespace Fast::Extension {
namespace {
TRIAEVUM_SPIRV_PACK_EMBED_BEGIN
alignas(16) const unsigned char kPack[] = {
#if TRIAEVUM_SPIRV_PACK_EMBED
#embed "pica_caster_fragment.spvpack"
#else
#include "pica_caster_fragment.spvpack.inc"
#endif
};
TRIAEVUM_SPIRV_PACK_EMBED_END
} // namespace

std::span<const Renderer::PassShaderArtifact> CasterFragmentArtifacts() {
    static const auto entries = Renderer::ReadSpirvPack(kPack);
    static const auto artifacts = Renderer::PassShaderArtifactsFromPack(entries);
    return artifacts;
}
} // namespace Fast::Extension
