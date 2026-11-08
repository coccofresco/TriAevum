// Generated translated OOT3D title vertex SPIR-V. Not title-neutral code.
// Source: original SHBIN interfaces + maintained GLSL wrapper and translated body.
// Citra/Azahar translator donor: GPL-2.0-or-later; preserve donor notices.
// Regenerate oot3d_native_vertex.spvpack with
// tools/renderer/tev_program/build_vertex_artifacts (see the provenance JSON).
#include "oot3d_native_vertex_binaries.h"
#include "fast/renderer/spirv_pack.h"
#include "fast/renderer/title_program_pack.h"
#include <cstdlib>
#include <cstdio>
#include <memory>

namespace Oot3dNativeGame {
namespace {
TRIAEVUM_SPIRV_PACK_EMBED_BEGIN
alignas(16) const unsigned char kPack[] = {
#if TRIAEVUM_SPIRV_PACK_EMBED
#embed "oot3d_native_vertex.spvpack"
#else
#include "oot3d_native_vertex.spvpack.inc"
#endif
};
TRIAEVUM_SPIRV_PACK_EMBED_END
const std::vector<Fast::Renderer::SpirvPackEntry>& Entries() {
    // The environment is configured from the launch profile before first use.
    // Exact source identities keep other titles isolated; embedded programs
    // stay first, so an optional pack cannot override legacy shaders.
    static const auto extra = []() -> std::unique_ptr<Fast::Renderer::TitleProgramPack> {
        const char* path = std::getenv("TRIAEVUM_TITLE_PROGRAM_PACK");
        if (!path || !*path) return {};
        auto pack = std::make_unique<Fast::Renderer::TitleProgramPack>(std::filesystem::path(path));
        std::fprintf(stderr, "TRIAEVUM_TITLE_PROGRAM_PACK loaded=%zu path=%s\n",
                     pack->Entries().size(), path);
        return pack;
    }();
    static const auto entries = [] {
        auto all = Fast::Renderer::ReadSpirvPack(kPack);
        if (extra) all.insert(all.end(), extra->Entries().begin(), extra->Entries().end());
        return all;
    }();
    return entries;
}
} // namespace

std::span<const Fast::Renderer3ds::PicaVertexArtifact> TitleVertexArtifacts() {
    static const auto artifacts = Fast::Renderer3ds::PicaVertexArtifactsFromPack(Entries());
    return artifacts;
}
std::span<const Fast::Renderer3ds::PicaVertexArtifact> AdditionalTitleVertexArtifacts() {
    static const auto embeddedCount = Fast::Renderer3ds::PicaVertexArtifactsFromPack(
        Fast::Renderer::ReadSpirvPack(kPack)).size();
    return TitleVertexArtifacts().subspan(embeddedCount);
}
std::span<const Fast::Renderer::PassShaderArtifact> TitlePassArtifacts() {
    static const auto artifacts = Fast::Renderer::PassShaderArtifactsFromPack(Entries());
    return artifacts;
}
} // namespace Oot3dNativeGame
