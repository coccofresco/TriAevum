// Generated from maintained PICA equations, not captured game shaders.
// Vulkan 1.1, shaderc performance optimization; canonical and NRI interfaces.
// Optional effect family, isolated from canonical programs. Regenerate
// pica_temporal_fragment.spvpack with
// tools/renderer/tev_program/build_fragment_artifacts --temporal.
#include "fast/extension/pica_temporal_fragment_binaries.h"
#include "fast/renderer/spirv_pack.h"

namespace Fast::Extension {
namespace {
TRIAEVUM_SPIRV_PACK_EMBED_BEGIN
alignas(16) const unsigned char kPack[] = {
#if TRIAEVUM_SPIRV_PACK_EMBED
#embed "pica_temporal_fragment.spvpack"
#else
#include "pica_temporal_fragment.spvpack.inc"
#endif
};
TRIAEVUM_SPIRV_PACK_EMBED_END
} // namespace

std::span<const Renderer3ds::PicaFragmentArtifact> TemporalFragmentArtifacts() {
    static const auto entries = Renderer::ReadSpirvPack(kPack);
    static const auto artifacts = Renderer3ds::PicaFragmentArtifactsFromPack(entries);
    return artifacts;
}
} // namespace Fast::Extension
