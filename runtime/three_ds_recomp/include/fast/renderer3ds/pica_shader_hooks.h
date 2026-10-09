#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fast::Renderer3ds {

inline constexpr uint32_t kPicaShaderHookSchemaVersion = 10U;
inline constexpr uint32_t kPicaFragmentOutputContractSchemaVersion = 1U;

enum class PicaFragmentDepthOutput : uint8_t {
    Unavailable = 0,
    FixedFunction,
    ExplicitNative,
};

// Structural output ownership emitted by the canonical PICA frontend. This
// is intentionally independent from the attachment plan: canonical shaders
// own only native color/depth, while typed instrumentation may add MRTs later.
struct PicaFragmentOutputContract {
    static constexpr uint8_t UnavailableLocation = 0xffU;

    uint32_t SchemaVersion = 0U;
    uint32_t ColorLocationMask = 0U;
    uint8_t NativeColorLocation = UnavailableLocation;
    PicaFragmentDepthOutput Depth =
        PicaFragmentDepthOutput::Unavailable;
    bool UnsupportedColorLocation = false;
    bool DuplicateColorLocation = false;

    [[nodiscard]] bool Valid() const noexcept {
        return SchemaVersion ==
                   kPicaFragmentOutputContractSchemaVersion &&
               ColorLocationMask != 0U &&
               NativeColorLocation < 32U &&
               (ColorLocationMask &
                (1U << NativeColorLocation)) != 0U &&
               Depth != PicaFragmentDepthOutput::Unavailable &&
               !UnsupportedColorLocation && !DuplicateColorLocation;
    }

    [[nodiscard]] bool CanonicalNative() const noexcept {
        return Valid() && NativeColorLocation == 0U &&
               ColorLocationMask == 1U;
    }

    bool operator==(const PicaFragmentOutputContract&) const = default;
};

enum class PicaShaderHook : uint8_t {
    GlobalDeclarations,
    MainPrologue,
    PicaLighting,
    BeforeDepth,
    BeforeNativeColor,
    MainEpilogue,
    AfterNativeTev,
    Count,
};

enum class PicaShaderSemantic : uint32_t {
    None = 0,
    NormalQuaternion = 1U << 0U,
    NativeColorOutput = 1U << 1U,
    NormalGuideOutput = 1U << 2U,
    MaterialGuideOutput = 1U << 3U,
    AmbientGuideOutput = 1U << 4U,
    AmbientOcclusionResponse = 1U << 5U,
    SecondaryFragmentColor = 1U << 6U,
    MaterialLightingPoint = 1U << 7U,
    PrimaryColorInput = 1U << 8U,
    ViewVector = 1U << 9U,
    CombinerOutput = 1U << 10U,
    PrimaryColorConsumed = 1U << 11U,
    MaterialNormal = 1U << 12U,
    VertexLightingPoint = 1U << 13U,
    SecondaryFragmentColorConsumed = 1U << 14U,
    NativeFogFactor = 1U << 15U,
};

enum class PicaTextureCoordinateOperation : uint8_t {
    Unavailable,
    NativeVFlip,
    ProjectedNativeVFlip,
    Shadow2DNative,
};

struct PicaTextureSampleLayout {
    static constexpr uint8_t UnavailableCoordinate = 0xffU;

    uint8_t Coordinate = UnavailableCoordinate;
    PicaTextureCoordinateOperation Operation =
        PicaTextureCoordinateOperation::Unavailable;

    [[nodiscard]] bool Available() const noexcept {
        return Coordinate != UnavailableCoordinate &&
               Operation != PicaTextureCoordinateOperation::Unavailable;
    }
    bool operator==(const PicaTextureSampleLayout&) const = default;
};

constexpr PicaShaderSemantic operator|(PicaShaderSemantic left, PicaShaderSemantic right) noexcept {
    return static_cast<PicaShaderSemantic>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr PicaShaderSemantic& operator|=(PicaShaderSemantic& left, PicaShaderSemantic right) noexcept {
    left = left | right;
    return left;
}

// Versioned optional capture sites owned by the native lighting emitter.
// Offsets name statement boundaries in the unchanged canonical source.
// This extension is not part of the legacy schema/key; absent on old packs.
struct PicaNativeLightingCaptureLayout {
    uint32_t Version = 0;
    size_t Begin = 0, Diffuse = 0, Specular = 0, End = 0;
    bool Valid(size_t sourceSize, size_t lightingEnd) const noexcept {
        return Version == 1 && Begin < Diffuse && Diffuse < Specular &&
               Specular < End && End <= lightingEnd && lightingEnd <= sourceSize;
    }
    void Rebase(size_t offset) noexcept {
        if (Version == 1) { Begin += offset; Diffuse += offset; Specular += offset; End += offset; }
    }
    bool operator==(const PicaNativeLightingCaptureLayout&) const = default;
};

struct PicaShaderHookLayout {
    static constexpr size_t Unavailable = static_cast<size_t>(-1);

    uint32_t SchemaVersion = 0U;
    size_t SourceSize = 0U;
    std::array<size_t, static_cast<size_t>(PicaShaderHook::Count)> Offsets{};
    PicaShaderSemantic Semantics = PicaShaderSemantic::None;
    uint8_t SampledTextureMask = 0U;
    std::array<PicaTextureSampleLayout, 3U> TextureSamples{};
    PicaFragmentOutputContract Outputs;
    PicaNativeLightingCaptureLayout NativeLightingCapture;

    PicaShaderHookLayout() noexcept {
        Offsets.fill(Unavailable);
    }

    [[nodiscard]] bool Supports(PicaShaderHook hook) const noexcept {
        return Offsets[static_cast<size_t>(hook)] != Unavailable;
    }

    [[nodiscard]] size_t Offset(PicaShaderHook hook) const noexcept {
        return Offsets[static_cast<size_t>(hook)];
    }

    [[nodiscard]] bool Has(PicaShaderSemantic semantic) const noexcept {
        return (static_cast<uint32_t>(Semantics) & static_cast<uint32_t>(semantic)) != 0U;
    }

    [[nodiscard]] bool SamplesTexture(uint8_t texture) const noexcept {
        return texture < 8U &&
               (SampledTextureMask & (1U << texture)) != 0U;
    }

    [[nodiscard]] const PicaTextureSampleLayout* TextureSample(
        uint8_t texture) const noexcept {
        return texture < TextureSamples.size() &&
                       TextureSamples[texture].Available()
                   ? &TextureSamples[texture]
                   : nullptr;
    }

    [[nodiscard]] bool Valid() const noexcept {
        return SchemaVersion == kPicaShaderHookSchemaVersion && Outputs.Valid() &&
               Supports(PicaShaderHook::GlobalDeclarations) &&
               Supports(PicaShaderHook::MainEpilogue) &&
               Offset(PicaShaderHook::GlobalDeclarations) <= Offset(PicaShaderHook::MainEpilogue) &&
               Offset(PicaShaderHook::MainEpilogue) <= SourceSize;
    }

    [[nodiscard]] bool ValidFor(std::string_view source) const noexcept {
        return Valid() && SourceSize == source.size();
    }
    bool operator==(const PicaShaderHookLayout&) const = default;
};

enum class PicaVertexShaderHook : uint8_t {
    GlobalDeclarations,
    RegisterStateBegin,
    RegisterStateEnd,
    MainBodyBegin,
    MainBodyEnd,
    Count,
};

enum class PicaVertexShaderSemantic : uint32_t {
    None = 0,
    PicaRegisterState = 1U << 0U,
    VertexUniformState = 1U << 1U,
    ClipPositionOutput = 1U << 2U,
    TextureCoordinateProgram = 1U << 3U,
    TransformProgram = 1U << 4U,
    SkeletonProgram = 1U << 5U,
    ViewPositionOutput = 1U << 6U,
    SurfaceProgram = 1U << 7U,
};

constexpr PicaVertexShaderSemantic operator|(
    PicaVertexShaderSemantic left,
    PicaVertexShaderSemantic right) noexcept {
    return static_cast<PicaVertexShaderSemantic>(
        static_cast<uint32_t>(left) |
        static_cast<uint32_t>(right));
}

constexpr PicaVertexShaderSemantic& operator|=(
    PicaVertexShaderSemantic& left,
    PicaVertexShaderSemantic right) noexcept {
    left = left | right;
    return left;
}

enum class PicaVertexTextureCoordinateOperation : uint8_t {
    Unavailable,
    CmbAffine,
};

struct PicaVertexTextureCoordinateSourceLayout {
    static constexpr uint8_t Unavailable = 0xffU;

    uint8_t InputRegister = Unavailable;
    uint8_t ScaleUniform = Unavailable;
    uint8_t ScaleComponent = Unavailable;
    uint8_t EnableBooleanUniform = Unavailable;

    bool operator==(
        const PicaVertexTextureCoordinateSourceLayout&) const = default;

    [[nodiscard]] bool Available() const noexcept {
        return InputRegister < 16U && ScaleUniform < 96U &&
               ScaleComponent < 4U && EnableBooleanUniform < 16U;
    }
};

struct PicaVertexTextureCoordinateLayout {
    static constexpr uint8_t Unavailable = 0xffU;

    PicaVertexTextureCoordinateOperation Operation =
        PicaVertexTextureCoordinateOperation::Unavailable;
    uint8_t EnableBooleanUniform = Unavailable;
    uint8_t SourceSelectorUniform = Unavailable;
    uint8_t SourceSelectorComponent = Unavailable;
    uint8_t SourceSelectorConstantUniform = Unavailable;
    std::array<uint8_t, 2U> SourceSelectorConstantComponents{
        Unavailable, Unavailable};
    uint8_t CoordinateModeUniform = Unavailable;
    uint8_t CoordinateModeComponent = Unavailable;
    uint8_t CoordinateModeConstantUniform = Unavailable;
    std::array<uint8_t, 2U> CoordinateModeConstantComponents{
        Unavailable, Unavailable};
    uint8_t MatrixRowUUniform = Unavailable;
    uint8_t MatrixRowVUniform = Unavailable;
    uint8_t HomogeneousUniform = Unavailable;
    uint8_t HomogeneousComponent = Unavailable;
    std::array<PicaVertexTextureCoordinateSourceLayout, 3U> Sources{};
    uint8_t SourceCount = 0U;

    bool operator==(
        const PicaVertexTextureCoordinateLayout&) const = default;

    [[nodiscard]] bool Available() const noexcept {
        if (Operation ==
                PicaVertexTextureCoordinateOperation::Unavailable ||
            EnableBooleanUniform >= 16U ||
            SourceSelectorUniform >= 96U ||
            SourceSelectorComponent >= 4U ||
            SourceSelectorConstantUniform >= 96U ||
            SourceSelectorConstantComponents[0] >= 4U ||
            SourceSelectorConstantComponents[1] >= 4U ||
            CoordinateModeUniform >= 96U ||
            CoordinateModeComponent >= 4U ||
            CoordinateModeConstantUniform >= 96U ||
            CoordinateModeConstantComponents[0] >= 4U ||
            CoordinateModeConstantComponents[1] >= 4U ||
            MatrixRowUUniform >= 96U || MatrixRowVUniform >= 96U ||
            HomogeneousUniform >= 96U || HomogeneousComponent >= 4U ||
            SourceCount == 0U || SourceCount > Sources.size()) {
            return false;
        }
        for (size_t index = 0U; index < SourceCount; ++index) {
            if (!Sources[index].Available()) {
                return false;
            }
        }
        return true;
    }
};

enum class PicaVertexTransformOperation : uint8_t {
    Unavailable,
    ModelViewProjection3x4,
};

struct PicaVertexTransformLayout {
    static constexpr uint8_t Unavailable = 0xffU;

    PicaVertexTransformOperation Operation = PicaVertexTransformOperation::Unavailable;
    uint8_t ProjectionFirstUniform = Unavailable;
    uint8_t ProjectionRowCount = 0U;
    uint8_t ViewFirstUniform = Unavailable;
    uint8_t ViewRowCount = 0U;
    uint8_t ModelFirstUniform = Unavailable;
    uint8_t ModelRowCount = 0U;
    uint8_t PositionInputRegister = Unavailable;
    uint8_t NormalInputRegister = Unavailable;

    bool operator==(const PicaVertexTransformLayout&) const = default;

    [[nodiscard]] bool Available() const noexcept {
        const auto rowsAvailable = [](uint8_t first, uint8_t count) {
            return first < 96U && count != 0U && static_cast<uint16_t>(first) + count <= 96U;
        };
        return Operation == PicaVertexTransformOperation::ModelViewProjection3x4 && ProjectionRowCount == 4U &&
               ViewRowCount == 3U && ModelRowCount == 3U && rowsAvailable(ProjectionFirstUniform, ProjectionRowCount) &&
               rowsAvailable(ViewFirstUniform, ViewRowCount) && rowsAvailable(ModelFirstUniform, ModelRowCount) &&
               PositionInputRegister < 16U && NormalInputRegister < 16U;
    }
};

enum class PicaVertexSkeletonOperation : uint8_t {
    Unavailable,
    MatrixPalette3x4,
};

struct PicaVertexSkeletonLayout {
    static constexpr uint8_t Unavailable = 0xffU;

    PicaVertexSkeletonOperation Operation = PicaVertexSkeletonOperation::Unavailable;
    uint8_t EnableBooleanUniform = Unavailable;
    uint8_t MultipleInfluenceBooleanUniform = Unavailable;
    uint8_t PaletteFirstUniform = Unavailable;
    uint8_t RowsPerMatrix = 0U;
    uint8_t BoneIndexInputRegister = Unavailable;
    uint8_t BoneWeightInputRegister = Unavailable;
    uint8_t MaximumInfluences = 0U;

    bool operator==(const PicaVertexSkeletonLayout&) const = default;

    [[nodiscard]] bool Available() const noexcept {
        return Operation == PicaVertexSkeletonOperation::MatrixPalette3x4 && EnableBooleanUniform < 16U &&
               (MultipleInfluenceBooleanUniform < 16U || MultipleInfluenceBooleanUniform == Unavailable) &&
               PaletteFirstUniform < 96U && RowsPerMatrix == 3U &&
               static_cast<uint16_t>(PaletteFirstUniform) + RowsPerMatrix <= 96U && BoneIndexInputRegister < 16U &&
               BoneWeightInputRegister < 16U && MaximumInfluences != 0U && MaximumInfluences <= 4U;
    }
};

struct PicaVertexUniformComponent {
    uint8_t Uniform = 0xffU;
    uint8_t Component = 0xffU;

    bool operator==(const PicaVertexUniformComponent&) const = default;
    [[nodiscard]] bool Available() const noexcept {
        return Uniform < 96U && Component < 4U;
    }
};

// Structurally recovered inputs to native diffuse lighting, before lighting is
// folded into primary color. Register names are never inferred from GLSL.
struct PicaVertexSurfaceLayout {
    uint8_t ViewNormalTemporary = 0xffU;
    uint8_t LightingBooleanUniform = 0xffU;
    uint8_t DiffuseUniform = 0xffU;
    uint8_t AmbientUniform = 0xffU;
    uint8_t TintBooleanUniform = 0xffU;
    uint8_t TintInputRegister = 0xffU;
    PicaVertexUniformComponent TintScale;
    PicaVertexUniformComponent LightEnabledValue;
    std::array<uint8_t, 3> LightDirectionUniforms{0xffU, 0xffU, 0xffU};
    PicaVertexUniformComponent BoneIndexScale;
    PicaVertexUniformComponent BoneWeightScale;
    PicaVertexUniformComponent SkinnedNormalScale;
    PicaVertexUniformComponent SkinnedPositionScale;
    PicaVertexUniformComponent InfluenceCount;
    std::array<PicaVertexUniformComponent, 2> AdditionalInfluenceThresholds{};
    // False for a vertex-lighting block the surface rows cannot hold (more
    // than three lights, or other enable semantics): such a program supplies
    // the normal, material and tint, and a draw that lights per vertex keeps
    // its native lighting.
    bool VertexLightsReconstructed = true;

    bool operator==(const PicaVertexSurfaceLayout&) const = default;
    [[nodiscard]] bool Available() const noexcept {
        if (ViewNormalTemporary >= 16U || LightingBooleanUniform >= 16U ||
            DiffuseUniform >= 96U || AmbientUniform >= 96U ||
            TintBooleanUniform >= 16U || TintInputRegister >= 16U ||
            !TintScale.Available()) return false;
        if (!VertexLightsReconstructed) return true;
        if (!LightEnabledValue.Available()) return false;
        for (auto uniform : LightDirectionUniforms)
            if (uniform >= 94U) return false;
        return true;
    }
    [[nodiscard]] bool SkinnedNormalsAvailable() const noexcept {
        return BoneIndexScale.Available() && BoneWeightScale.Available() &&
               SkinnedNormalScale.Available() && InfluenceCount.Available() &&
               AdditionalInfluenceThresholds[0].Available() &&
               AdditionalInfluenceThresholds[1].Available();
    }
};

struct PicaVertexShaderHookLayout {
    static constexpr size_t Unavailable = static_cast<size_t>(-1);

    uint32_t SchemaVersion = 0U;
    size_t SourceSize = 0U;
    std::array<size_t,
               static_cast<size_t>(PicaVertexShaderHook::Count)>
        Offsets{};
    PicaVertexShaderSemantic Semantics =
        PicaVertexShaderSemantic::None;
    std::array<PicaVertexTextureCoordinateLayout, 3U>
        TextureCoordinates{};
    PicaVertexTransformLayout Transform;
    PicaVertexSkeletonLayout Skeleton;
    PicaVertexSurfaceLayout Surface;

    bool operator==(const PicaVertexShaderHookLayout&) const = default;

    PicaVertexShaderHookLayout() noexcept {
        Offsets.fill(Unavailable);
    }

    [[nodiscard]] bool Supports(
        PicaVertexShaderHook hook) const noexcept {
        return Offsets[static_cast<size_t>(hook)] != Unavailable;
    }

    [[nodiscard]] size_t Offset(
        PicaVertexShaderHook hook) const noexcept {
        return Offsets[static_cast<size_t>(hook)];
    }

    [[nodiscard]] bool Has(
        PicaVertexShaderSemantic semantic) const noexcept {
        return (static_cast<uint32_t>(Semantics) &
                static_cast<uint32_t>(semantic)) != 0U;
    }

    [[nodiscard]] const PicaVertexTextureCoordinateLayout*
    TextureCoordinate(uint8_t coordinate) const noexcept {
        return coordinate < TextureCoordinates.size() &&
                       TextureCoordinates[coordinate].Available()
                   ? &TextureCoordinates[coordinate]
                   : nullptr;
    }

    [[nodiscard]] const PicaVertexTransformLayout* TransformProgram() const noexcept {
        return Transform.Available() ? &Transform : nullptr;
    }

    [[nodiscard]] const PicaVertexSkeletonLayout* SkeletonProgram() const noexcept {
        return Skeleton.Available() ? &Skeleton : nullptr;
    }

    [[nodiscard]] const PicaVertexSurfaceLayout* SurfaceProgram() const noexcept {
        return Surface.Available() ? &Surface : nullptr;
    }

    [[nodiscard]] bool Valid() const noexcept {
        if (SchemaVersion != kPicaShaderHookSchemaVersion) {
            return false;
        }
        for (size_t index = 0U;
             index < static_cast<size_t>(PicaVertexShaderHook::Count);
             ++index) {
            if (Offsets[index] == Unavailable ||
                Offsets[index] > SourceSize) {
                return false;
            }
        }
        return Offset(PicaVertexShaderHook::GlobalDeclarations) ==
                   Offset(PicaVertexShaderHook::RegisterStateBegin) &&
               Offset(PicaVertexShaderHook::RegisterStateBegin) <=
                   Offset(PicaVertexShaderHook::RegisterStateEnd) &&
               Offset(PicaVertexShaderHook::RegisterStateEnd) <=
                   Offset(PicaVertexShaderHook::MainBodyBegin) &&
               Offset(PicaVertexShaderHook::MainBodyBegin) <=
                   Offset(PicaVertexShaderHook::MainBodyEnd);
    }

    [[nodiscard]] bool ValidFor(
        std::string_view source) const noexcept {
        return Valid() && SourceSize == source.size();
    }
};

struct PicaTemporalVertexProgramView {
    PicaVertexShaderHookLayout Hooks;
    std::string_view PreviousRegisterState;
    std::string_view PreviousMainBody;

    [[nodiscard]] bool ValidFor(
        std::string_view source) const noexcept {
        return Hooks.ValidFor(source) &&
               Hooks.Has(
                   PicaVertexShaderSemantic::PicaRegisterState) &&
               Hooks.Has(
                   PicaVertexShaderSemantic::VertexUniformState) &&
               Hooks.Has(
                   PicaVertexShaderSemantic::ClipPositionOutput) &&
               !PreviousRegisterState.empty() &&
               !PreviousMainBody.empty();
    }
};

} // namespace Fast::Renderer3ds
