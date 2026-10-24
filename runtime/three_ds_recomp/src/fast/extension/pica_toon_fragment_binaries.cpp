// Generated from maintained PICA equations, not captured game shaders.
// Vulkan 1.1, shaderc performance optimization; canonical and NRI interfaces.
// Optional effect family, isolated from canonical programs. Regenerate
// pica_toon_fragment.spvpack with
// tools/renderer/tev_program/build_fragment_artifacts --toon.
#include "fast/extension/pica_toon_fragment_artifact.h"
#include "fast/renderer/spirv_pack.h"

namespace Fast::Extension {
namespace {
TRIAEVUM_SPIRV_PACK_EMBED_BEGIN
alignas(16) const unsigned char kPack[] = {
#if TRIAEVUM_SPIRV_PACK_EMBED
#embed "pica_toon_fragment.spvpack"
#else
#include "pica_toon_fragment.spvpack.inc"
#endif
};
TRIAEVUM_SPIRV_PACK_EMBED_END
} // namespace

std::span<const Renderer3ds::PicaFragmentArtifact> ToonFragmentArtifacts() {
    static const auto entries = Renderer::ReadSpirvPack(kPack);
    static const auto artifacts = Renderer3ds::PicaFragmentArtifactsFromPack(entries);
    return artifacts;
}
} // namespace Fast::Extension
