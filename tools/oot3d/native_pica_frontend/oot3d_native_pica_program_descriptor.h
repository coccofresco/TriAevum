#pragma once

#include "oot3d_native_pica_draw_state.h"
#include "oot3d/renderer/pica_render_backend.h"

#include <cstdint>
#include <span>
#include <string>

namespace Oot3dNativeGame {

// Increment when the canonical byte layout changes. These identities are used
// by traces and offline shader packs, so host object layout is never hashed.
inline constexpr uint32_t kOot3dPicaProgramDescriptorSchemaVersion = 3U;

struct Oot3dPicaCanonicalDrawIdentity {
    uint32_t SchemaVersion = kOot3dPicaProgramDescriptorSchemaVersion;
    uint64_t VertexProgramId = 0;
    uint64_t FragmentProgramId = 0;
    uint64_t RasterStateId = 0;
    uint64_t PipelineId = 0;
    uint64_t DynamicStateId = 0;
    uint64_t FullRegisterStateId = 0;

    bool operator==(const Oot3dPicaCanonicalDrawIdentity&) const = default;
};

enum class Oot3dPicaFragmentUnsupportedFeature : uint32_t {
    None = 0,
    FragmentLighting = 1U << 0U,
    ProceduralTexture = 1U << 1U,
    TextureCube = 1U << 2U,
    Shadow2D = 1U << 3U,
    ShadowCube = 1U << 4U,
    Gas = 1U << 5U,
    InvalidFogMode = 1U << 6U,
    TevEncoding = 1U << 7U,
};

struct Oot3dPicaFragmentFeatureSet {
    bool FragmentLightingEnabled = false;
    bool ProceduralTextureEnabled = false;
    bool ProceduralTextureReferenced = false;
    bool FogEnabled = false;
    bool FogFlip = false;
    bool GasEnabled = false;
    uint8_t FogMode = 0;
    uint8_t Texture0Type = 0;
    uint8_t EnabledTextureMask = 0;
    uint8_t ReferencedTextureMask = 0;
    uint32_t UnsupportedFeatureMask = 0;
    Oot3d::Renderer::PicaFragmentLightingLayout FragmentLighting;

    bool FullySupported() const noexcept {
        return UnsupportedFeatureMask == 0U;
    }

    bool operator==(const Oot3dPicaFragmentFeatureSet&) const = default;
};

// Stable FNV-1a over bytes. Zero is reserved for "identity unavailable".
uint64_t HashOot3dPicaCanonicalBytes(std::span<const uint8_t> bytes);

std::string FormatOot3dPicaCanonicalId(uint64_t id);

Oot3dPicaFragmentFeatureSet AnalyzeOot3dPicaFragmentFeatures(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state);

uint64_t BuildOot3dPicaCanonicalVertexProgramId(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state);

Oot3dPicaCanonicalDrawIdentity BuildOot3dPicaCanonicalDrawIdentity(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state);

Oot3dPicaCanonicalDrawIdentity BuildOot3dPicaCanonicalDrawIdentity(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state,
    uint64_t vertexProgramId);

} // namespace Oot3dNativeGame
