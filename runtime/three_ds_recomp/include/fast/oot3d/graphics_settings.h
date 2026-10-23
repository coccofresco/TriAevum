#pragma once

#include "fast/oot3d/directional_shadows.h"
#include "fast/oot3d/graphics_capabilities.h"
#include "fast/oot3d/point_shadows.h"
#include "fast/oot3d/grass_types.h"
#include "fast/oot3d/reflection_material_profile.h"
#include "fast/oot3d/upscaler_contract.h"
#include "fast/renderer3ds/pica_pbr_material.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Fast::Oot3d {

enum class GraphicsPreset : uint8_t { Authentic, Enhanced, Toon, Custom };
enum class WindowMode : uint8_t { Windowed, Borderless, ExclusiveFullscreen };
// The picture's aspect ratio: the original top screen (5:3), 16:9, 21:9 (7:3)
// or Free (the output's own ratio: the window as the user shapes it, the
// whole screen in a fullscreen mode).
enum class AspectRatioMode : uint8_t { Native3ds, Wide16x9, Ultrawide21x9, Free };
enum class AntiAliasingMode : uint8_t { Off, Fxaa, Smaa1x, Msaa, Taa, Upscaler };
enum class FrameRateMode : uint8_t {
    Original30,
    Interpolated2x,
    Interpolated3x,
    Uncapped,
    // Source compatibility for integrations that still name the former
    // fixed-60 presentation mode. Persistence emits Interpolated2x.
    Fixed60 = Interpolated2x,
};
enum class AmbientOcclusionMode : uint8_t { Off, Gtao, Cacao, Mxao };
// iMMERSE MXAO estimators: ground-truth horizons, solid angle, and the
// visibility bitmask with a cosine-weighted or solid-angle integral.
enum class MxaoEstimator : uint8_t { GroundTruth, SolidAngle, VisibilityBitmask, VisibilityBitmaskSolidAngle };
// The surface normal MXAO integrates against: reconstructed from depth, as the
// original computes it, or the renderer's normal guide (interpolated vertex
// normals, PBR normal maps and texture relief), like the original's Launchpad
// normals. Only the depth normal is the plane of the depth samples it tests.
// Geometry: the face normal of the visible surface from the geometry guide,
// the exact plane of the rasterized triangle (no reconstruction across
// silhouettes); from depth where the guide has none.
enum class MxaoNormalSource : uint8_t { Depth, Renderer, Geometry };
// MXAO debug views, shown by the scene composite in place of the image. Not
// saved. Normals are shown in view space (X right, Y up, Z toward the viewer).
enum class MxaoDebugView : uint8_t {
    Off,
    AmbientOcclusion,    // the original's "Show Raw AO": the published visibility
    UnfilteredOcclusion, // the estimate before the filter (accumulated when temporal)
    Normals,             // the normals in use
    RendererNormals,     // the renderer's normal guide
    DepthNormals,        // normals reconstructed from depth
    NormalDisagreement,  // angle between the two, 0 to 90 degrees
    Depth,               // depth over the fade range
    EdgeWeight,          // the filter's silhouette / grazing weight
    Fade,                // distance fade
    HistoryLength,       // temporal accumulation length
    FaceNormals,         // the geometry guide's face normals
    FaceDisagreement,    // angle between face and depth normals, 0 to 10 degrees
};
// Relief the AO providers read from each world material's colour texture: its
// luminance slope bends the scene's normal guide (and so every guide reader).
enum class AoTextureRelief : uint8_t { Off, Subtle, Medium, Strong };
// Normal tilt per unit change of log2 luminance across one texel.
[[nodiscard]] constexpr float AoTextureReliefGain(AoTextureRelief relief) noexcept {
    switch (relief) {
    case AoTextureRelief::Subtle: return 1.0F;
    case AoTextureRelief::Medium: return 2.0F;
    case AoTextureRelief::Strong: return 3.0F;
    case AoTextureRelief::Off: break;
    }
    return 0.0F;
}
enum class ReflectionMode : uint8_t { Off, HiZ, FidelityFxSssr };
enum class ToonMode : uint8_t { Off, PostProcessPreview, PicaMaterial };
enum class SurfaceLightingMode : uint8_t { Native, PerPixel, PerPixelPbr, NativePbr };

[[nodiscard]] constexpr bool UsesFullSceneLighting(SurfaceLightingMode mode) noexcept {
    return mode == SurfaceLightingMode::PerPixel || mode == SurfaceLightingMode::PerPixelPbr;
}
[[nodiscard]] constexpr bool UsesPbrMaterials(SurfaceLightingMode mode) noexcept {
    return mode == SurfaceLightingMode::PerPixelPbr || mode == SurfaceLightingMode::NativePbr;
}
enum class GrassQuality : uint8_t { Off, Low, Medium, High, Custom };

// Dynamic diffuse global illumination. Per-pixel lighting modes only.
// BrixelizerGi: from the scene's distance field (FidelityFX Brixelizer GI).
// ScreenSpace: the screen-space diffuse GI of the native depth and guides
// (docs/TRIAEVUM_SCREEN_SPACE_GI.md).
enum class GlobalIlluminationMode : uint8_t { Off, BrixelizerGi, ScreenSpace };
// Internal resolution of the GI solve relative to the scene guides.
enum class GlobalIlluminationResolution : uint8_t { Half, Quarter };

// Screen-space GI transport and quality (docs/TRIAEVUM_SCREEN_SPACE_GI.md,
// "M5"): per pixel and frame Sections stratified sections of Sectors visibility
// sectors, RadialCandidates per side on the metric radial schedule; radius R and
// thickness tau in OoT scene units (provisional until open point 2 measures
// the scale); temporal accumulation and the a-trous filter levels; the work
// scale of the guide extent (this mode's own; GlobalIllumination.Resolution is
// Brixelizer GI's) and the highlight clamp (largest J per channel, 0 off).
// Defaults chosen by test on the game's buffers (docs, "M5 Stage C").
// Validation composes the pass's J without the artistic gain
// (DiffuseIntensity) and without the highlight clamp.
struct ScreenSpaceGiSettings {
    uint8_t Sections = 1;            // 1-4
    uint16_t RadialCandidates = 20;  // 8, 16, 20, 24 or 32
    uint16_t Sectors = 64;           // 32, 64 or 128
    float Radius = 150.0F;           // R, scene units, [10, 1000]
    float Thickness = 30.0F;         // tau, scene units, [1, 200]
    bool Temporal = true;
    uint8_t FilterLevels = 2;        // 0-4
    bool Validation = false;
    GlobalIlluminationResolution WorkScale = GlobalIlluminationResolution::Quarter;
    float HighlightClamp = 0.0F;     // 0 off, else [0.05, 16]
    bool operator==(const ScreenSpaceGiSettings&) const = default;
};

struct GlobalIlluminationSettings {
    GlobalIlluminationMode Mode = GlobalIlluminationMode::Off;
    GlobalIlluminationResolution Resolution = GlobalIlluminationResolution::Half;
    float DiffuseIntensity = 1.0F;
    // Sources of the bounced light (gains on their direct light): the native
    // scene directionals, the added key light, and the dynamic lights (native
    // local lights, Navi and fairies). Ambient and fill light are never
    // sources: OoT bakes them into the native colour.
    float SceneLight = 1.0F;
    float KeyLight = 1.0F;
    float DynamicLights = 1.0F;
    // Sky light: traced rays that leave the scene receive the native sky
    // colour, so open surfaces are lit and occluded ones are not. At 1 every
    // open surface receives the full sky colour on top of its native colour.
    float SkyLight = 0.25F;
    // Specular GI is a reflection source: composed through the material
    // guide like screen-space reflections, which own the term when enabled.
    bool Specular = false;
    float SpecularIntensity = 1.0F;
    // Mode ScreenSpace only (sky light and specular GI are Brixelizer terms:
    // a screen-space gather has no ray that leaves the scene).
    ScreenSpaceGiSettings ScreenSpace;
};

// How the GI sources reach the trace. The surface guide carries each source's
// weight relative to the largest gain (so its 8-bit radiance never clips) and
// the traced light is scaled by that gain when it is composed, at once. The
// GI's radiance cache follows a change of the weights only over seconds, so
// the pass restarts its history when they change.
struct GlobalIlluminationSourceMix {
    std::array<float, 4> Weights{}; // native scene light, key light, dynamic lights, sky light
    float Gain = 0.0F;
    bool operator==(const GlobalIlluminationSourceMix&) const = default;
};
// The screen-space mode has no sky source: its sky weight is zero and the
// sky gain never raises the composition gain.
[[nodiscard]] GlobalIlluminationSourceMix ResolveGlobalIlluminationSources(
    const GlobalIlluminationSettings& settings) noexcept;
// Whether the GI mode reads the renderer's face normal (geometry guide G):
// the screen-space mode takes its geometric normal from it.
[[nodiscard]] constexpr bool GlobalIlluminationReadsFaceNormal(const GlobalIlluminationSettings& settings) noexcept {
    return settings.Mode == GlobalIlluminationMode::ScreenSpace;
}
// Whether a change of the source weights restarts the GI history instead of
// blending in over the radiance cache's averaging window (seconds): light
// returning to a history that held none, or a source switched off (its light
// would linger). A restarted history fills at once; for other changes a
// restart would only add a dark dip.
[[nodiscard]] bool RestartsGlobalIlluminationHistory(const std::array<float, 4>& previous,
                                                     const std::array<float, 4>& next) noexcept;

inline constexpr uint8_t kMaximumToonLightBands = 6U;

struct ToonStyleSettings {
    uint8_t LightBands = 4;
    bool CustomLightBands = false;
    std::array<float, kMaximumToonLightBands> LightBandLevels{
        0.0F, 1.0F / 3.0F, 2.0F / 3.0F, 1.0F, 1.0F, 1.0F };
    std::array<float, kMaximumToonLightBands - 1U> LightBandThresholds{
        1.0F / 6.0F, 0.5F, 5.0F / 6.0F, 1.0F, 1.0F };
    float BandSoftness = 0.228F;
    float Saturation = 1.09F;
    std::array<float, 3> ShadowTint{ 0.48F, 0.56F, 0.72F };
    float ShadowStrength = 1.0F;
    float RimStrength = 0.34F;
    float RimWidth = 3.17F;
    std::array<float, 3> RimTint{ 1.0F, 0.93F, 0.72F };
    bool OutlineEnabled = false;
    float OutlineWidth = 1.25F;
    float OutlineSoftness = 1.0F;
    float OutlineDepthSensitivity = 1.0F;
    float OutlineNormalSensitivity = 1.0F;
    std::array<float, 3> OutlineTint{ 0.03F, 0.04F, 0.06F };
    float OutlineOpacity = 0.85F;
};

void ResetToonLightBandProfile(ToonStyleSettings& settings) noexcept;
void ResizeToonLightBandProfile(ToonStyleSettings& settings, uint8_t bands) noexcept;

struct GrassAppearanceSettings {
    std::array<float, 3> RootColor{0.055F, 0.19F, 0.035F};
    std::array<float, 3> TipColor{0.34F, 0.72F, 0.12F};
    float TextureColorInfluence = 0.0F;
    float TextureRootBrightness = 0.65F;
    float TextureTipBrightness = 1.15F;
    float HeightScale = 1.0F;
    float BladeCurvature = 0.0F;
    float BladeDroop = 0.0F;
    float ShapeVariation = 0.0F;
    float BladeTwistDegrees = 0.0F;
    uint8_t BladeSegments = 2U;
    // Near blades as two crossed planes; otherwise one ribbon that turns its
    // width toward the camera (half the vertices and fill, the default).
    bool CrossedBlades = false;
    bool ReceiveLighting = true;
    bool ReceiveFog = true;
    bool ToonRimEnabled = true;
    float ToonRimFadeStart = 200.0F;
    float ToonRimFadeEnd = 800.0F;
};

struct InteractiveGrassSettings {
    GrassQuality Quality = GrassQuality::Off;
    uint32_t MaxInstancesPerRoom = 0;
    float DrawDistance = 0.0F;
    // Zero means a legacy profile: resolve once from its draw distance.
    float LodReferenceDistance = 0.0F;
    bool FrustumCulling = true;
    float CullingClusterSize = 300.0F;
    float LodStartFraction = 0.45F;
    float LodEndFraction = 0.80F;
    float SegmentLodStartDistance = 200.0F;
    float SegmentLodEndDistance = 800.0F;
    float FarDensity = 0.30F;
    uint8_t FarBladeSegments = 1U;
    bool FarTuftsEnabled = true;
    bool MidrangeClustersEnabled = false;
    bool MidrangeAdaptiveEnabled = false;
    uint32_t MidrangeAdaptiveCapacity = 128;
    float MidrangeClusterCellExtent = 22.0F;
    float MidrangeFarBladeFraction = 0.25F;
    uint8_t FarTuftBladeCount = 5U;
    float DrawFadeFraction = 0.15F;
    float DensityFadeFraction = 0.10F;
    float TuftTransitionFraction = 0.20F;
    float FarTuftDensity = 1.0F;
    float FarTuftSpread = 1.0F;
    float SegmentLodSoftness = 0.5F;
    // Geometric LOD beyond the blades (grass_card_lod.h): cards per cluster,
    // per group of clusters and per patch of clusters, the last without end.
    // The blades end at the clump distance while it is enabled.
    bool CardLodEnabled = false;
    float CardLodBladeDistance = 1000.0F;
    float CardLodClumpDistance = 4000.0F;
    float CardLodGroupDistance = 15000.0F;
    float CardLodBlendFraction = 0.25F;
    GrassGenerationSettings Generation;
    GrassAppearanceSettings Appearance;
    float WindDirectionDegrees = 0.0F;
    float WindStrength = 0.0F;
    float WindSpeed = 0.0F;
    float WindSpatialScale = 1.0F;
    float WindGustStrength = 0.35F;
    float WindGustFrequency = 0.35F;
    float WindTurbulence = 0.25F;
    float WindRandomness = 1.0F;
    float CollisionPush = 1.0F;
    float CollisionVelocityResponse = 0.65F;
    float ColliderRadiusMultiplier = 1.0F;
    float ColliderHeightMultiplier = 1.0F;
    float RecoverySeconds = 1.0F;
    float InteractionDamping = 1.0F;
    float MaximumBend = 0.85F;
    float InteractionFieldRadius = 600.0F;
    uint16_t InteractionFieldResolution = 128;
    float InteractionVerticalMargin = 12.0F;
    std::vector<GrassPlacementRule> Rules;
    std::vector<GrassMaskRule> Masks;
};

void ApplyGrassQualityPreset(
    InteractiveGrassSettings& settings,
    GrassQuality quality) noexcept;

struct EffectsSettings {
    SurfaceLightingMode SurfaceLighting = SurfaceLightingMode::Native;
    DirectionalShadowSettings DirectionalShadows;
    PointShadowSettings PointShadows;
    GlobalIlluminationSettings GlobalIllumination;
    AmbientOcclusionMode AmbientOcclusion = AmbientOcclusionMode::Off;
    uint8_t AoQuality = 0;
    float AoRadius = 1.0F;
    float AoDetailRadius = 0.25F;
    float AoStrength = 1.0F;
    float AoShadowPower = 1.5F;
    float AoShadowClamp = 0.98F;
    float AoHorizonAngleThreshold = 0.06F;
    uint32_t AoBlurPassCount = 2U;
    float AoFadeOutFrom = 50.0F;
    float AoFadeOutTo = 300.0F;
    float AoSharpness = 0.98F;
    float AoDetailStrength = 0.5F;
    // MXAO's own controls; radius, strength and fade are the shared AO values.
    MxaoEstimator MxaoMethod = MxaoEstimator::VisibilityBitmask;
    // Low through IDGAF (0-6).
    uint8_t MxaoQuality = 1;
    uint8_t MxaoFilter = 1;
    // Accumulates MXAO over frames with the scene's motion vectors.
    bool MxaoTemporal = false;
    // Face normals by default: the exact depth plane (the original
    // reconstructs it from depth; still selectable).
    MxaoNormalSource MxaoNormals = MxaoNormalSource::Geometry;
    // The original's Shading Rate: 0 full, 1 half, 2 quarter of the pixels
    // estimated per frame; the others keep their previous estimate.
    uint8_t MxaoShadingRate = 0;
    // The original's "Increase Radius with Distance": a constant screen radius
    // (MxaoScreenRadius, the original's Sample Radius) instead of AoRadius.
    bool MxaoRadiusGrowsWithDistance = false;
    float MxaoScreenRadius = 2.5F;
    // The original's exponential Fade Out Distance (depth over the far plane)
    // instead of the native fade start/end.
    bool MxaoOriginalFade = false;
    float MxaoFadeDistance = 0.25F;
    MxaoDebugView MxaoDebug = MxaoDebugView::Off;
    AoTextureRelief AoRelief = AoTextureRelief::Off;
    ReflectionMode Reflections = ReflectionMode::Off;
    float ReflectionStrength = 0.65F;
    float ReflectionMaxDistance = 1500.0F;
    float ReflectionThickness = 8.0F;
    float ReflectionEdgeFade = 0.08F;
    uint8_t ReflectionMaxSteps = 40;
    float ReflectionRoughnessBias = 0.0F;
    uint8_t ReflectionDebugView = 0;
    // With Reflections Off, the per-pixel models still trace Hi-Z
    // reflections on gameplay water only; the trace finds the water surfaces
    // from the title's water bodies.
    bool WaterReflections = true;
    std::vector<ReflectionMaterialRule> ReflectionMaterials;
    ToonMode Toon = ToonMode::Off;
    ToonStyleSettings ToonStyle;
};

// Whether an effect reads the face normal of the geometry guide: the guide is
// then published and declared as that effect's input.
[[nodiscard]] constexpr bool EffectsReadFaceNormal(const EffectsSettings& effects) noexcept {
    return effects.AmbientOcclusion == AmbientOcclusionMode::Mxao &&
           (effects.MxaoNormals == MxaoNormalSource::Geometry || effects.MxaoDebug == MxaoDebugView::FaceNormals ||
            effects.MxaoDebug == MxaoDebugView::FaceDisagreement);
}

struct AzaharTexturePackSettings {
    bool DumpTextures = false;
    bool LoadCustomTextures = false;
    std::string LoadDirectory;
    std::string DumpDirectory;
};

struct TexturePackSettings {
    AzaharTexturePackSettings Azahar;
};

struct GraphicsSettings {
    GraphicsPreset Preset = GraphicsPreset::Authentic;
    WindowMode Window = WindowMode::Windowed;
    uint32_t DisplayIndex = 0;
    uint32_t OutputWidth = 1280;
    uint32_t OutputHeight = 720;
    uint32_t RefreshRate = 60;
    // The picture's aspect ratio inside the output (presentation_picture.h):
    // a fixed ratio is letterboxed and centred, Free fills the window or the
    // screen. Host-owned, like the window and the output size.
    AspectRatioMode Aspect = AspectRatioMode::Wide16x9;
    float InternalResolutionScale = 1.0F;
    AntiAliasingMode AntiAliasing = AntiAliasingMode::Off;
    uint8_t MsaaSamples = 1;
    float TaaHistoryWeight = 0.90F;
    float TaaClampExpansion = 0.05F;
    float TaaSharpness = 0.10F;
    UpscalerProvider Upscaler = UpscalerProvider::Nis;
    UpscalerQuality UpscalerMode = UpscalerQuality::Quality;
    float UpscalerSharpness = 0.35F;
    DlssModel UpscalerDlssModel = DlssModel::Default;
    FrameRateMode FrameRate = FrameRateMode::Original30;
    bool VSync = true;
    float FovMultiplier = 1.0F;
    // Actors at 5x the native distance, native foliage groups spawned
    // farther and the Kokiri fade removed. Off keeps the title's ranges.
    bool ExtendedDrawDistance = false;
    InteractiveGrassSettings Grass;
    InteractiveGrassSettings GrassSavedPreset;
    EffectsSettings Effects;
    TexturePackSettings TexturePacks;
};

struct GraphicsSettingsIssue {
    std::string Field;
    std::string Message;
    bool Corrected = false;
};

struct GraphicsSettingsValidation {
    GraphicsSettings Value;
    std::vector<GraphicsSettingsIssue> Issues;
    [[nodiscard]] bool Accepted() const;
};

class GraphicsSettingsService {
  public:
    explicit GraphicsSettingsService(GraphicsSettings initial = {});

    [[nodiscard]] static GraphicsSettings Preset(GraphicsPreset preset);
    [[nodiscard]] static GraphicsSettings PresetForCurrent(
        GraphicsPreset preset, const GraphicsSettings& current);
    // A preset's settings over the current ones: every preset, built-in or
    // the user's, keeps host presentation and content identities (window,
    // output, VSync, texture packs, grass sources, reflection materials, the
    // saved grass preset) as they are.
    [[nodiscard]] static GraphicsSettings KeepHostOwnedSettings(
        GraphicsSettings preset, const GraphicsSettings& current);
    [[nodiscard]] static GraphicsSettingsValidation Validate(
        GraphicsSettings candidate, const GraphicsCapabilities& capabilities);
    [[nodiscard]] const GraphicsSettings& Current() const;
    [[nodiscard]] const GraphicsSettings& LastKnownGood() const;
    [[nodiscard]] const std::optional<GraphicsSettings>& Pending() const;

    GraphicsSettingsValidation Stage(GraphicsSettings candidate,
                                     const GraphicsCapabilities& capabilities);
    bool ApplyPending();
    void ConfirmCurrent();
    void Rollback();

  private:
    GraphicsSettings mCurrent;
    GraphicsSettings mLastKnownGood;
    std::optional<GraphicsSettings> mPending;
};

} // namespace Fast::Oot3d
