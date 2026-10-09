#pragma once

#include <cstdint>

namespace Fast::Oot3d {

// Every branch BuildPicaFragmentInstrumentationVariant took for one draw. The
// specialized composition is emitted from these choices; the always-available
// (uber) program receives the same record as data, so one decision owner
// governs both programs.
struct PicaEffectDecisions {
    enum Bit : uint32_t {
        SurfaceLighting = 1U << 0U,
        Pbr = 1U << 1U,
        // Surface lighting replays the native TEV under unit lighting for the
        // GI albedo (no PBR material).
        AlbedoReplay = 1U << 2U,
        FogGuide = 1U << 3U,
        // The native fog factor feeds the fog guide (else the neutral value).
        FogGuideFactor = 1U << 4U,
        NormalGuide = 1U << 5U,
        Relief = 1U << 6U,
        Outline = 1U << 7U,
        // A depth-compare-always outline draw clears the raster canvas.
        OutlineClear = 1U << 8U,
        AlbedoGuide = 1U << 9U,
        DirectionalShadow = 1U << 10U,
        PointShadow = 1U << 11U,
        Toon = 1U << 12U,
        ToonMaterialPath = 1U << 13U,
        ToonVertexLighting = 1U << 14U,
        AmbientGuide = 1U << 15U,
        // Scene domain: geometry resets the transparent-depth guide.
        SceneReset = 1U << 16U,
        SceneOverlay = 1U << 17U,
        SceneTransparentDepth = 1U << 18U,
        // The overlay's transparent depth shares the rigid motion output.
        SceneTransparentRigid = 1U << 19U,
        SceneBlended = 1U << 20U,
        SceneOutline = 1U << 21U,
        SceneAmbient = 1U << 22U,
        SceneAlbedo = 1U << 23U,
        Reflection = 1U << 24U,
        ReflectionExplicit = 1U << 25U,
        ReflectionPbr = 1U << 26U,
        Reactive = 1U << 27U,
        RigidMotion = 1U << 28U,
        // Surface lighting mixes the authored environment before fog.
        EnvironmentMix = 1U << 29U,
        // The fragment depth is written explicitly (else gl_FragCoord.z).
        ExplicitDepth = 1U << 30U,
        // The face normal of the visible surface (geometry guide G/B).
        FaceNormal = 1U << 31U,
    };
    enum class NormalPath : uint8_t { Pbr, Surface, Material, Typed, NativeView, NativeRotate };
    // The first block that needed a guide output writes its base value there.
    enum class NormalWriter : uint8_t { None, NormalGuide, Outline };
    enum class MaterialWriter : uint8_t { None, Reflection, Reactive, Baseline };
    enum class Coverage : uint8_t { One, Zero, Alpha };
    enum class AlbedoPath : uint8_t { Zero, Pbr, Surface };
    // None: an isolated draw publishes no ambient occlusion response.
    enum class AmbientPath : uint8_t { Pbr, Exact, Fallback, None };
    enum class ReactiveCoverage : uint8_t { None, SourceAlpha, SourceColor, Full };

    uint32_t Bits = 0;
    NormalPath Normal = NormalPath::NativeRotate;
    NormalWriter NormalAt = NormalWriter::None;
    MaterialWriter MaterialAt = MaterialWriter::None;
    Coverage AlbedoCoverage = Coverage::One;
    AlbedoPath Albedo = AlbedoPath::Zero;
    AmbientPath Ambient = AmbientPath::Fallback;
    ReactiveCoverage ReactiveKind = ReactiveCoverage::None;
    uint8_t ReliefUnit = 0;
    uint8_t ReliefCoordinate = 0;
    // Continuous values exactly as the specialized program's literals parse.
    float ReliefGain = 0.0F;
    float ReflectionReflectivity = 0.0F;
    float ReflectionRoughness = 0.0F;
    // PBR material sampling: the coordinate of its texture unit, projection.
    uint8_t PbrCoordinate = 0;
    bool PbrProjected = false;
    bool PbrNativeLighting = false;
    bool PbrAttenuationLuts = false;
    bool PbrSeams = false;
    // Guide locations (bit N = location N) the specialized program declares.
    // An undeclared output leaves its attachment untouched; the superset
    // program declares them all, so its pipeline masks the others.
    uint8_t Outputs = 0;
    // This variant is not in the legacy built-in effect superset.
    bool FragmentLightingOperand = false;
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
    bool ShadowProbe = false;
#endif

    [[nodiscard]] constexpr bool Has(Bit bit) const noexcept { return (Bits & bit) != 0U; }
    constexpr void Set(Bit bit) noexcept { Bits |= bit; }
};

} // namespace Fast::Oot3d
