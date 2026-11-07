// Generated from maintained PICA equations and effect compositions.
// Regenerate pica_uber_fragment.spvpack with
// tools/renderer/tev_program/build_fragment_artifacts --uber.
#include "fast/extension/pica_uber_fragment_binaries.h"
#include "fast/renderer/spirv_pack.h"

namespace Fast::Extension {
namespace {
TRIAEVUM_SPIRV_PACK_EMBED_BEGIN
alignas(16) const unsigned char kPack[] = {
#if TRIAEVUM_SPIRV_PACK_EMBED
#embed "pica_uber_fragment.spvpack"
#else
#include "pica_uber_fragment.spvpack.inc"
#endif
};
alignas(16) const unsigned char kNativeLightingPack[] = {
#if TRIAEVUM_SPIRV_PACK_EMBED
#embed "pica_native_lighting_uber.spvpack"
#else
#include "pica_native_lighting_uber.spvpack.inc"
#endif
};
TRIAEVUM_SPIRV_PACK_EMBED_END
} // namespace

std::span<const Renderer3ds::PicaFragmentArtifact> LegacyUberFragmentArtifacts() {
    static const auto artifacts = Renderer3ds::PicaFragmentArtifactsFromPack(Renderer::ReadSpirvPack(kPack));
    return artifacts;
}

std::span<const Renderer3ds::PicaFragmentArtifact> UberFragmentArtifacts() {
    static const auto entries = [] {
        auto legacy = Renderer::ReadSpirvPack(kPack);
        const auto native = Renderer::ReadSpirvPack(kNativeLightingPack);
        legacy.insert(legacy.end(), native.begin(), native.end());
        return legacy;
    }();
    static const auto artifacts = Renderer3ds::PicaFragmentArtifactsFromPack(entries);
    return artifacts;
}
} // namespace Fast::Extension
