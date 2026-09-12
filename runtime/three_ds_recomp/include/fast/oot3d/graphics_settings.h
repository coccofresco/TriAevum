#pragma once

#include "fast/oot3d/directional_shadows.h"
#include "fast/oot3d/graphics_capabilities.h"
#include "fast/oot3d/grass_types.h"
#include "fast/oot3d/reflection_material_profile.h"
#include "fast/oot3d/upscaler_contract.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Fast::Oot3d {

enum class GraphicsPreset : uint8_t { Authentic, Enhanced, Toon, Custom };
enum class WindowMode : uint8_t { Windowed, Borderless, ExclusiveFullscreen };
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
enum class AmbientOcclusionMode : uint8_t { Off, Cacao };
enum class ReflectionMode : uint8_t { Off, HiZ, FidelityFxSssr };
enum class ToonMode : uint8_t { Off, PostProcessPreview, PicaMaterial };
enum class GrassQuality : uint8_t { Off, Low, Medium, High, Custom };

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
};

void ApplyGrassQualityPreset(
    InteractiveGrassSettings& settings,
    GrassQuality quality) noexcept;

struct EffectsSettings {
    DirectionalShadowSettings DirectionalShadows;
    AmbientOcclusionMode AmbientOcclusion = AmbientOcclusionMode::Off;
    uint8_t AoQuality = 0;
    float AoRadius = 1.0F;
    float AoStrength = 1.0F;
    float AoShadowPower = 1.5F;
    float AoShadowClamp = 0.98F;
    float AoHorizonAngleThreshold = 0.06F;
    float AoFadeOutFrom = 50.0F;
    float AoFadeOutTo = 300.0F;
    uint8_t AoBlurPassCount = 2;
    float AoSharpness = 0.98F;
    float AoDetailStrength = 0.5F;
    ReflectionMode Reflections = ReflectionMode::Off;
    float ReflectionStrength = 0.65F;
    float ReflectionMaxDistance = 1500.0F;
    float ReflectionThickness = 8.0F;
    float ReflectionEdgeFade = 0.08F;
    uint8_t ReflectionMaxSteps = 40;
    float ReflectionRoughnessBias = 0.0F;
    uint8_t ReflectionDebugView = 0;
    std::vector<ReflectionMaterialRule> ReflectionMaterials;
    ToonMode Toon = ToonMode::Off;
    ToonStyleSettings ToonStyle;
};

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
    float InternalResolutionScale = 1.0F;
    AntiAliasingMode AntiAliasing = AntiAliasingMode::Off;
    uint8_t MsaaSamples = 1;
    float TaaHistoryWeight = 0.90F;
    float TaaClampExpansion = 0.05F;
    float TaaSharpness = 0.10F;
    UpscalerProvider Upscaler = UpscalerProvider::Nis;
    UpscalerQuality UpscalerMode = UpscalerQuality::Quality;
    float UpscalerSharpness = 0.35F;
    FrameRateMode FrameRate = FrameRateMode::Original30;
    bool VSync = true;
    float FovMultiplier = 1.0F;
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
