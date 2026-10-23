#pragma once
#include "fast/renderer3ds/pica_surface_color_response.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fast::Renderer3ds {

enum class NativeBlendEquation : uint8_t {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

enum class NativeBlendFactor : uint8_t {
    Zero,
    One,
    SourceColor,
    OneMinusSourceColor,
    DestColor,
    OneMinusDestColor,
    SourceAlpha,
    OneMinusSourceAlpha,
    DestAlpha,
    OneMinusDestAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha,
    SourceAlphaSaturate,
};

struct NativeBlendState {
    bool Enabled = false;
    NativeBlendEquation EquationRgb = NativeBlendEquation::Add;
    NativeBlendEquation EquationAlpha = NativeBlendEquation::Add;
    NativeBlendFactor SourceRgb = NativeBlendFactor::One;
    NativeBlendFactor DestRgb = NativeBlendFactor::Zero;
    NativeBlendFactor SourceAlpha = NativeBlendFactor::One;
    NativeBlendFactor DestAlpha = NativeBlendFactor::Zero;
    float ConstantColor[4] = { 0.0F, 0.0F, 0.0F, 0.0F };
};

// PICA enables its blend unit even for opaque ONE/ZERO replacement.
[[nodiscard]] constexpr bool PicaBlendReplacesDestination(const NativeBlendState& blend) noexcept {
    return !blend.Enabled ||
           (blend.EquationRgb == NativeBlendEquation::Add && blend.EquationAlpha == NativeBlendEquation::Add &&
            blend.SourceRgb == NativeBlendFactor::One && blend.DestRgb == NativeBlendFactor::Zero &&
            blend.SourceAlpha == NativeBlendFactor::One && blend.DestAlpha == NativeBlendFactor::Zero);
}

enum class NativeCullMode : uint8_t {
    KeepAll,
    KeepClockwise,
    KeepCounterClockwise,
};

enum class NativeTextureFilter : uint8_t {
    Nearest,
    Linear,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear,
};

enum class NativeTextureWrap : uint8_t {
    ClampToEdge,
    Repeat,
    MirroredRepeat,
};

struct NativeSamplerState {
    NativeTextureFilter MinFilter = NativeTextureFilter::Nearest;
    NativeTextureFilter MagFilter = NativeTextureFilter::Nearest;
    NativeTextureWrap WrapS = NativeTextureWrap::Repeat;
    NativeTextureWrap WrapT = NativeTextureWrap::Repeat;
    float LodBias = 0.0F;
    uint32_t MinMipLevel = 0;
    uint32_t MaxMipLevel = 0;
};

enum class PicaTopology : uint8_t {
    TriangleList = 0,
    TriangleStrip = 1,
    TriangleFan = 2,
    GeometryShader = 3,
};

enum class PicaVertexFormat : uint8_t {
    SignedByte = 0,
    UnsignedByte = 1,
    SignedShort = 2,
    Float = 3,
};

enum class PicaCompareFunction : uint8_t {
    Never = 0,
    Always = 1,
    Equal = 2,
    NotEqual = 3,
    Less = 4,
    LessOrEqual = 5,
    Greater = 6,
    GreaterOrEqual = 7,
};

enum class PicaStencilAction : uint8_t {
    Keep = 0,
    Zero = 1,
    Replace = 2,
    Increment = 3,
    Decrement = 4,
    Invert = 5,
    IncrementWrap = 6,
    DecrementWrap = 7,
};

enum class PicaLogicOperation : uint8_t {
    Clear = 0,
    And = 1,
    AndReverse = 2,
    Copy = 3,
    Set = 4,
    CopyInverted = 5,
    NoOp = 6,
    Invert = 7,
    Nand = 8,
    Or = 9,
    Nor = 10,
    Xor = 11,
    Equivalent = 12,
    AndInverted = 13,
    OrReverse = 14,
    OrInverted = 15,
};

struct PicaVertexBindingView {
    uint8_t Binding = 0;
    uint16_t ByteStride = 0;
    bool PerInstance = false;
    std::span<const uint8_t> Bytes;
};

struct PicaVertexAttributeView {
    uint8_t Location = 0;
    uint8_t Binding = 0;
    PicaVertexFormat Format = PicaVertexFormat::Float;
    uint8_t ComponentCount = 4;
    uint16_t ByteOffset = 0;
};

// Explicit producer of the bytes a sampler reads. A raw PICA TextureCopy runs
// on the GPU; until a guest CPU write or a newer copy takes the bytes back, the
// copy's ordered producer owns that physical subrange and the sampled color must
// come from that immutable GPU version rather than a re-uploaded guest payload.
// Identity is the producer's CompletionId inside the draw's render-target
// namespace: never a byte hash, and never inferred from an absent hash.
struct PicaTextureCopyReference {
    uint64_t CompletionId = 0;
    uint64_t AfterDrawSubmissionId = 0;
    uint32_t PhysicalAddress = 0;
    uint32_t ByteCount = 0;
};

struct PicaTextureView {
    uint8_t Slot = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t NativeFormat = 0;
    uint8_t NativeType = 0;
    uint8_t NativeWrapS = 0;
    uint8_t NativeWrapT = 0;
    bool MinLinear = false;
    bool MagLinear = false;
    bool MipLinear = false;
    int16_t LodBiasRaw = 0;
    uint8_t MinMipLevel = 0;
    uint8_t MaxMipLevel = 0;
    uint32_t PhysicalAddress = 0;
    std::span<const uint8_t> NativeBytes;
    uint64_t NativeContentHash = 0;
    bool NativeContentHashAvailable = false;
    uint64_t NativeBaseLevelContentHash = 0;
    bool NativeBaseLevelContentHashAvailable = false;
    // Empty for an ordinary CPU-owned texture. Non-empty only where an ordered
    // raw copy still owns bytes of this immutable draw snapshot; each entry is
    // the intersection of that producer's destination subrange with this
    // texture's physical byte range, so the union is exactly what the producer
    // covers and a partially covered texture stays CPU-owned.
    std::span<const PicaTextureCopyReference> CopyReferences;
};

// Whether a native texture format stores alpha per texel. RGB8, RGB565,
// HILO8, I8, I4 and ETC1 sample alpha 1 everywhere; unknown formats count as
// carrying alpha.
[[nodiscard]] constexpr bool PicaTextureFormatCarriesAlpha(uint8_t nativeFormat) noexcept {
    switch (nativeFormat) {
        case 1U: case 3U: case 6U: case 7U: case 10U: case 12U:
            return false;
        default:
            return true;
    }
}

inline constexpr size_t kPicaLightingLutPackedEntryCount = 24U * 256U;

// Packed native table words remain owned by the title/frontend. The common
// contract carries only an immutable view and its content identity.
struct PicaLightingLutView {
    std::span<const uint32_t> PackedEntries;
    uint64_t ContentHash = 0;
    bool ContentHashAvailable = false;

    [[nodiscard]] bool Valid() const noexcept {
        return PackedEntries.size() == kPicaLightingLutPackedEntryCount &&
               ContentHashAvailable && ContentHash != 0U;
    }
};

struct PicaStencilState {
    bool Enabled = false;
    PicaCompareFunction Compare = PicaCompareFunction::Always;
    uint8_t Reference = 0;
    uint8_t CompareMask = 0;
    uint8_t WriteMask = 0;
    PicaStencilAction Fail = PicaStencilAction::Keep;
    PicaStencilAction DepthFail = PicaStencilAction::Keep;
    PicaStencilAction Pass = PicaStencilAction::Keep;
};

inline constexpr uint32_t kPicaFragmentFeatureLegacySchemaVersion = 1U;
inline constexpr uint32_t kPicaFragmentFeatureSchemaVersion = 2U;
inline constexpr uint32_t kPicaFragmentLightingLayoutSchemaVersion = 1U;
inline constexpr size_t kPicaFragmentLightCount = 8U;
inline constexpr size_t kPicaFragmentLightingLutSamplerCount = 7U;

enum class PicaFragmentLightingBumpMode : uint8_t {
    None = 0,
    NormalMap = 1,
    TangentMap = 2,
};

enum class PicaFragmentLightingLutInput : uint8_t {
    NormalHalf = 0,
    ViewHalf = 1,
    NormalView = 2,
    LightNormal = 3,
    NegatedLightSpot = 4,
    CosinePhi = 5,
};

struct PicaFragmentLightingLutSamplerLayout {
    PicaFragmentLightingLutInput Input =
        PicaFragmentLightingLutInput::NormalHalf;
    float Scale = 1.0F;
    bool AbsoluteInput = true;

    [[nodiscard]] bool Valid() const noexcept {
        return std::isfinite(Scale) &&
               static_cast<uint8_t>(Input) <=
                   static_cast<uint8_t>(
                       PicaFragmentLightingLutInput::CosinePhi);
    }

    bool operator==(
        const PicaFragmentLightingLutSamplerLayout&) const = default;
};

struct PicaFragmentLightLayout {
    bool Directional = false;
    bool TwoSidedDiffuse = false;
    bool GeometricFactor0 = false;
    bool GeometricFactor1 = false;
    bool ShadowEnabled = false;
    bool SpotAttenuationEnabled = false;
    bool DistanceAttenuationEnabled = false;

    bool operator==(const PicaFragmentLightLayout&) const = default;
};

struct PicaFragmentLightingLayout {
    uint32_t SchemaVersion = 0U;
    uint8_t ActiveLightCount = 0U;
    std::array<uint8_t, kPicaFragmentLightCount> LightPermutation{};
    std::array<PicaFragmentLightLayout, kPicaFragmentLightCount> Lights{};
    std::array<PicaFragmentLightingLutSamplerLayout,
               kPicaFragmentLightingLutSamplerCount>
        LutSamplers{};
    uint8_t EnvironmentConfiguration = 0U;
    uint8_t FresnelSelector = 0U;
    uint8_t BumpTextureUnit = 0U;
    uint8_t ShadowTextureUnit = 0U;
    PicaFragmentLightingBumpMode BumpMode =
        PicaFragmentLightingBumpMode::None;
    bool ClampHighlights = false;
    bool RecalculateBumpVectors = true;
    bool ShadowFactorEnabled = false;
    bool ShadowPrimary = false;
    bool ShadowSecondary = false;
    bool ShadowAlpha = false;
    bool InvertShadow = false;

    [[nodiscard]] bool Available() const noexcept {
        return SchemaVersion == kPicaFragmentLightingLayoutSchemaVersion;
    }

    [[nodiscard]] bool Valid() const noexcept {
        if (!Available() || ActiveLightCount > LightPermutation.size() ||
            (EnvironmentConfiguration > 6U &&
             EnvironmentConfiguration != 8U) ||
            FresnelSelector > 3U || BumpTextureUnit > 3U ||
            ShadowTextureUnit > 3U ||
            static_cast<uint8_t>(BumpMode) >
                static_cast<uint8_t>(
                    PicaFragmentLightingBumpMode::TangentMap)) {
            return false;
        }
        for (size_t slot = 0U; slot < ActiveLightCount; ++slot) {
            if (LightPermutation[slot] >= Lights.size()) {
                return false;
            }
        }
        for (const auto& sampler : LutSamplers) {
            if (!sampler.Valid()) {
                return false;
            }
        }
        return true;
    }

    bool operator==(const PicaFragmentLightingLayout&) const = default;
};

// Register-derived fragment semantics shared by all PICA200 titles.
struct PicaFragmentFeatureView {
    uint32_t SchemaVersion = 0U;
    bool FragmentLightingEnabled = false;
    bool FogEnabled = false;
    bool FogFlip = false;
    uint8_t FogMode = 0U;
    PicaFragmentLightingLayout FragmentLighting;
    PicaSurfaceColorResponse SurfaceColorResponse;

    [[nodiscard]] bool Valid() const noexcept {
        const bool knownSchema =
            SchemaVersion == kPicaFragmentFeatureLegacySchemaVersion ||
            SchemaVersion == kPicaFragmentFeatureSchemaVersion;
        if (!knownSchema || FogEnabled != (FogMode == 5U)) {
            return false;
        }
        if (FragmentLighting.Available() &&
            (!FragmentLighting.Valid() ||
             (!FragmentLightingEnabled &&
              FragmentLighting.ActiveLightCount != 0U))) {
            return false;
        }
        return SchemaVersion == kPicaFragmentFeatureLegacySchemaVersion ||
               !FragmentLightingEnabled || FragmentLighting.Valid();
    }
};

} // namespace Fast::Renderer3ds
