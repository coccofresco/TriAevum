#include "fast/oot3d/graphics_settings_runtime.h"

#include "fast/oot3d/graphics_settings_persistence.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Fast::Oot3d {
namespace {

std::mutex& PersistencePortMutex() {
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<GraphicsSettingsPersistencePort>& PersistencePort() {
    static std::shared_ptr<GraphicsSettingsPersistencePort> persistence;
    return persistence;
}

// Set once the runtime instance has taken its persistence port snapshot: a
// port installed later would never be read, so that order is reported.
bool& PersistencePortSnapshotTaken() {
    static bool taken = false;
    return taken;
}

std::shared_ptr<GraphicsSettingsPersistencePort>
SnapshotPersistencePort() {
    std::scoped_lock lock(PersistencePortMutex());
    PersistencePortSnapshotTaken() = true;
    return PersistencePort();
}

uint64_t PresentationClockMilliseconds() {
    const auto elapsed =
        std::chrono::steady_clock::now().time_since_epoch();
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            elapsed).count();
    return milliseconds <= 0
        ? 0U
        : static_cast<uint64_t>(milliseconds);
}

bool HasGraphicsEnvironmentOverride() {
    constexpr std::array names{
        "OOT3D_GRAPHICS_FXAA",
        "OOT3D_GRAPHICS_SMAA",
        "OOT3D_GRAPHICS_MSAA",
        "OOT3D_GRAPHICS_TAA",
        "OOT3D_GRAPHICS_NIS",
        "OOT3D_GRAPHICS_FSR",
        "OOT3D_GRAPHICS_DLSS",
        "OOT3D_GRAPHICS_FRAME_RATE",
        "OOT3D_GRAPHICS_RENDER_SCALE",
        "OOT3D_GRAPHICS_FOV_MULTIPLIER",
        "OOT3D_GRAPHICS_GRASS_AUTO",
        "OOT3D_GRAPHICS_GRASS_HASH",
        "OOT3D_GRAPHICS_AO",
        "OOT3D_GRAPHICS_AO_QUALITY",
        "OOT3D_GRAPHICS_AO_STRESS",
        "OOT3D_GRAPHICS_HIZ",
        "OOT3D_GRAPHICS_SSSR",
        "OOT3D_GRAPHICS_REFLECTION_TEXTURE_HASH",
        "OOT3D_GRAPHICS_REFLECTION_TEXTURE_PROFILE",
        "OOT3D_GRAPHICS_HIZ_DEBUG",
        "OOT3D_GRAPHICS_TOON",
        "OOT3D_GRAPHICS_TOON_STRESS",
        "OOT3D_GRAPHICS_TOON_MATERIAL",
        "OOT3D_GRAPHICS_TOON_OUTLINE",
        "OOT3D_GRAPHICS_TOON_OUTLINE_STRESS",
        "OOT3D_AZAHAR_DUMP_TEXTURES",
        "OOT3D_AZAHAR_CUSTOM_TEXTURES",
        "OOT3D_AZAHAR_LOAD_DIRECTORY",
        "OOT3D_AZAHAR_DUMP_DIRECTORY",
        "OOT3D_AZAHAR_USER_DIRECTORY",
        "OOT3D_AZAHAR_TITLE_ID",
        "OOT3D_GRAPHICS_TEST_BORDERLESS_AFTER_FRAMES",
        "OOT3D_GRAPHICS_TEST_PRESENTATION_ROLLBACK_AFTER_FRAMES",
        "OOT3D_GRAPHICS_TEST_FAIL_PRESENTATION_APPLY",
        "OOT3D_GRAPHICS_TEST_AO_AFTER_FRAMES",
        "OOT3D_GRAPHICS_TEST_AO_OFF_AFTER_FRAMES",
        "OOT3D_GRAPHICS_TEST_RENDER_SCALE_AFTER_FRAMES",
        "OOT3D_GRAPHICS_TEST_UPSCALER_QUALITY_AFTER_FRAMES",
    };
    return std::any_of(names.begin(), names.end(),
                       [](const char* name) {
                           return std::getenv(name) != nullptr;
                       });
}

} // namespace

void InstallGraphicsSettingsPersistencePort(
    std::shared_ptr<GraphicsSettingsPersistencePort> persistence) {
    std::scoped_lock lock(PersistencePortMutex());
    if (PersistencePortSnapshotTaken()) {
        // The runtime already started without a port: the settings file is
        // neither loaded nor saved this launch. An initialization-order
        // defect in the host, not a user condition.
        SPDLOG_ERROR("OOT3D graphics settings persistence installed after the "
                     "settings runtime started: the settings file is ignored "
                     "this launch");
        std::fprintf(stderr,
                     "TRIAEVUM_GRAPHICS_SETTINGS_PERSISTENCE_LATE installed after "
                     "the runtime started; settings file ignored\n");
    }
    PersistencePort() = std::move(persistence);
}

GraphicsSettingsRuntime::GraphicsSettingsRuntime() {
    auto initial = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    bool selectedInitial = false;
    bool rewriteLoadedSettings = false;
    mPersistence = SnapshotPersistencePort();
    mPersistenceSuppressed = HasGraphicsEnvironmentOverride();
    if (!mPersistenceSuppressed && mPersistence != nullptr) {
        try {
            nlohmann::json root;
            if (mPersistence->LoadRoot(root)) {
                const auto loaded = LoadGraphicsSettingsConfig(
                    root, mService.Current());
                for (const auto& issue : loaded.Issues) {
                    if (issue.Corrected) {
                        SPDLOG_WARN("OOT3D graphics config {}: {}",
                                    issue.Field, issue.Message);
                    } else {
                        SPDLOG_ERROR("OOT3D graphics config {}: {}",
                                     issue.Field, issue.Message);
                    }
                }
                if (loaded.Found &&
                    !loaded.UnsupportedFutureVersion) {
                    initial = loaded.Value;
                    selectedInitial = true;
                    rewriteLoadedSettings = loaded.NeedsRewrite;
                }
                if (const auto presets = root.find("GraphicsPresets"); presets != root.end()) {
                    std::vector<std::string> issues;
                    mUserPresets = GraphicsPresetLibrary::Deserialize(*presets, &issues);
                    for (const auto& issue : issues) SPDLOG_WARN("OOT3D graphics presets: {}", issue);
                }
            }
        } catch (const std::exception& exception) {
            SPDLOG_ERROR("Unable to load OOT3D graphics settings: {}",
                         exception.what());
        }
    }
    bool customized = false;
    if (const char* renderScale =
            std::getenv("OOT3D_GRAPHICS_RENDER_SCALE")) {
        char* end = nullptr;
        const float parsed = std::strtof(renderScale, &end);
        if (end != renderScale && *end == '\0' &&
            std::isfinite(parsed) && parsed >= 0.5F &&
            parsed <= 2.0F) {
            initial.InternalResolutionScale = parsed;
            customized = true;
        }
    }
    if (const char* frameRate = std::getenv("OOT3D_GRAPHICS_FRAME_RATE")) {
        const std::string_view mode(frameRate);
        if (mode == "Original30" || mode == "30") {
            initial.FrameRate = FrameRateMode::Original30;
            customized = true;
        } else if (mode == "Interpolated2x" || mode == "Fixed60" ||
                   mode == "2x" || mode == "60") {
            initial.FrameRate = FrameRateMode::Interpolated2x;
            customized = true;
        } else if (mode == "Interpolated3x" || mode == "3x" ||
                   mode == "90") {
            initial.FrameRate = FrameRateMode::Interpolated3x;
            customized = true;
        } else if (mode == "Uncapped" || mode == "0") {
            initial.FrameRate = FrameRateMode::Uncapped;
            customized = true;
        }
    }
    const char* fxaa = std::getenv("OOT3D_GRAPHICS_FXAA");
    if (fxaa != nullptr && std::string_view(fxaa) == "1") {
        initial.AntiAliasing = AntiAliasingMode::Fxaa;
        customized = true;
    }
    const char* smaa = std::getenv("OOT3D_GRAPHICS_SMAA");
    if (smaa != nullptr && std::string_view(smaa) == "1") {
        initial.AntiAliasing = AntiAliasingMode::Smaa1x;
        customized = true;
    }
    if (const char* msaa = std::getenv("OOT3D_GRAPHICS_MSAA")) {
        char* end = nullptr;
        const long parsed = std::strtol(msaa, &end, 10);
        if (end != msaa && *end == '\0' &&
            (parsed == 2 || parsed == 4 || parsed == 8)) {
            initial.AntiAliasing = AntiAliasingMode::Msaa;
            initial.MsaaSamples = static_cast<uint8_t>(parsed);
            customized = true;
        }
    }
    if (const char* taa = std::getenv("OOT3D_GRAPHICS_TAA");
        taa != nullptr && std::string_view(taa) == "1") {
        initial.AntiAliasing = AntiAliasingMode::Taa;
        initial.MsaaSamples = 1;
        customized = true;
    }
    if (const char* nis = std::getenv("OOT3D_GRAPHICS_NIS");
        nis != nullptr && std::string_view(nis) == "1") {
        initial.AntiAliasing = AntiAliasingMode::Upscaler;
        initial.Upscaler = UpscalerProvider::Nis;
        initial.UpscalerMode = UpscalerQuality::Quality;
        initial.MsaaSamples = 1;
        customized = true;
    }
    if (const char* fsr = std::getenv("OOT3D_GRAPHICS_FSR");
        fsr != nullptr && std::string_view(fsr) == "1") {
        initial.AntiAliasing = AntiAliasingMode::Upscaler;
        initial.Upscaler = UpscalerProvider::Fsr;
        initial.UpscalerMode = UpscalerQuality::Quality;
        initial.MsaaSamples = 1;
        customized = true;
    }
    if (const char* dlss = std::getenv("OOT3D_GRAPHICS_DLSS");
        dlss != nullptr && std::string_view(dlss) == "1") {
        initial.AntiAliasing = AntiAliasingMode::Upscaler;
        initial.Upscaler = UpscalerProvider::Dlss;
        initial.UpscalerMode = UpscalerQuality::Quality;
        initial.MsaaSamples = 1;
        customized = true;
    }
    if (const char* fov = std::getenv("OOT3D_GRAPHICS_FOV_MULTIPLIER")) {
        char* end = nullptr;
        const float parsed = std::strtof(fov, &end);
        if (end != fov && *end == '\0') {
            initial.FovMultiplier = std::clamp(parsed, 1.0F, 1.5F);
            customized = true;
        }
    }
    if (const char* dumpTextures =
            std::getenv("OOT3D_AZAHAR_DUMP_TEXTURES");
        dumpTextures != nullptr) {
        const std::string_view value(dumpTextures);
        if (value == "0" || value == "1") {
            initial.TexturePacks.Azahar.DumpTextures =
                value == "1";
            customized = true;
        }
    }
    if (const char* customTextures =
            std::getenv("OOT3D_AZAHAR_CUSTOM_TEXTURES");
        customTextures != nullptr) {
        const std::string_view value(customTextures);
        if (value == "0" || value == "1") {
            initial.TexturePacks.Azahar.LoadCustomTextures =
                value == "1";
            customized = true;
        }
    }
    if (const char* loadDirectory =
            std::getenv("OOT3D_AZAHAR_LOAD_DIRECTORY");
        loadDirectory != nullptr && *loadDirectory != '\0') {
        initial.TexturePacks.Azahar.LoadDirectory =
            loadDirectory;
        customized = true;
    }
    if (const char* dumpDirectory =
            std::getenv("OOT3D_AZAHAR_DUMP_DIRECTORY");
        dumpDirectory != nullptr && *dumpDirectory != '\0') {
        initial.TexturePacks.Azahar.DumpDirectory =
            dumpDirectory;
        customized = true;
    }
    if (const char* ao = std::getenv("OOT3D_GRAPHICS_AO");
        ao != nullptr && std::string_view(ao) == "1") {
        initial.Effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
        customized = true;
    }
    if (const char* aoQuality =
            std::getenv("OOT3D_GRAPHICS_AO_QUALITY");
        aoQuality != nullptr) {
        const std::string_view quality(aoQuality);
        if (quality == "0" || quality == "1") {
            initial.Effects.AoQuality =
                quality == "1" ? 1U : 0U;
            customized = true;
        }
    }
    if (const char* stress =
            std::getenv("OOT3D_GRAPHICS_AO_STRESS");
        stress != nullptr && std::string_view(stress) == "1") {
        initial.Effects.AmbientOcclusion =
            AmbientOcclusionMode::Gtao;
        initial.Effects.AoQuality = 1U;
        initial.Effects.AoRadius = 12.0F;
        initial.Effects.AoStrength = 4.0F;
        initial.Effects.AoShadowPower = 4.0F;
        initial.Effects.AoShadowClamp = 0.72F;
        initial.Effects.AoDetailRadius = 3.0F;
        initial.Effects.AoFadeOutFrom = 0.0F;
        initial.Effects.AoFadeOutTo = 5000.0F;
        initial.Effects.AoSharpness = 0.25F;
        initial.Effects.AoDetailStrength = 2.0F;
        customized = true;
    }
    if (const char* reflections = std::getenv("OOT3D_GRAPHICS_HIZ");
        reflections != nullptr && std::string_view(reflections) == "1") {
        initial.Effects.Reflections = ReflectionMode::HiZ;
        customized = true;
    }
    if (const char* reflections = std::getenv("OOT3D_GRAPHICS_SSSR");
        reflections != nullptr && std::string_view(reflections) == "1") {
        initial.Effects.Reflections = ReflectionMode::FidelityFxSssr;
        customized = true;
    }
    if (const char* textureHash =
            std::getenv("OOT3D_GRAPHICS_REFLECTION_TEXTURE_HASH")) {
        char* end = nullptr;
        const uint64_t parsed =
            std::strtoull(textureHash, &end, 16);
        if (end != textureHash && *end == '\0' && parsed != 0U) {
            ReflectionMaterialProfile profile =
                ReflectionMaterialProfile::Polished;
            if (const char* profileName = std::getenv(
                    "OOT3D_GRAPHICS_REFLECTION_TEXTURE_PROFILE")) {
                const std::string_view name(profileName);
                if (name == "water") {
                    profile = ReflectionMaterialProfile::Water;
                } else if (name == "metal") {
                    profile = ReflectionMaterialProfile::Metal;
                } else if (name == "custom") {
                    profile = ReflectionMaterialProfile::Custom;
                }
            }
            ReflectionMaterialRule rule;
            rule.RuleId = 1U;
            rule.Target.ContentHash = parsed;
            ApplyReflectionMaterialProfileDefaults(rule, profile);
            initial.Effects.ReflectionMaterials.push_back(rule);
            customized = true;
        }
    }
    if (const char* debug = std::getenv("OOT3D_GRAPHICS_HIZ_DEBUG")) {
        char* end = nullptr;
        const long parsed = std::strtol(debug, &end, 10);
        if (end != debug && *end == '\0' && parsed >= 0 && parsed <= 4) {
            initial.Effects.Reflections = ReflectionMode::HiZ;
            initial.Effects.ReflectionDebugView = static_cast<uint8_t>(parsed);
            customized = true;
        }
    }
    // Developer inspection of MXAO (MxaoDebugView 1-12); never saved.
    if (const char* debug = std::getenv("OOT3D_GRAPHICS_MXAO_DEBUG")) {
        char* end = nullptr;
        const long parsed = std::strtol(debug, &end, 10);
        if (end != debug && *end == '\0' && parsed >= 0 &&
            parsed <= static_cast<long>(MxaoDebugView::FaceDisagreement)) {
            initial.Effects.MxaoDebug = static_cast<MxaoDebugView>(parsed);
            customized = true;
        }
    }
    if (const char* toon = std::getenv("OOT3D_GRAPHICS_TOON");
        toon != nullptr && std::string_view(toon) == "1") {
        initial.Effects.Toon = ToonMode::PostProcessPreview;
        customized = true;
    }
    if (const char* toonStress = std::getenv("OOT3D_GRAPHICS_TOON_STRESS");
        toonStress != nullptr && std::string_view(toonStress) == "1") {
        initial.Effects.Toon = ToonMode::PostProcessPreview;
        initial.Effects.ToonStyle.LightBands = 2;
        initial.Effects.ToonStyle.BandSoftness = 0.0F;
        initial.Effects.ToonStyle.Saturation = 0.0F;
        initial.Effects.ToonStyle.ShadowTint = { 0.0F, 0.0F, 0.0F };
        initial.Effects.ToonStyle.ShadowStrength = 1.0F;
        initial.Effects.ToonStyle.RimTint = { 1.0F, 0.0F, 1.0F };
        initial.Effects.ToonStyle.RimStrength = 2.0F;
        initial.Effects.ToonStyle.RimWidth = 0.25F;
        customized = true;
    }
    if (const char* materialToon =
            std::getenv("OOT3D_GRAPHICS_TOON_MATERIAL");
        materialToon != nullptr && std::string_view(materialToon) == "1") {
        initial.Preset = GraphicsPreset::Custom;
        initial.Effects.Toon = ToonMode::PicaMaterial;
        customized = true;
    }
    if (const char* outline = std::getenv("OOT3D_GRAPHICS_TOON_OUTLINE");
        outline != nullptr && std::string_view(outline) == "1") {
        initial.Effects.Toon = ToonMode::PostProcessPreview;
        initial.Effects.ToonStyle.OutlineEnabled = true;
        customized = true;
    }
    if (const char* outlineStress =
            std::getenv("OOT3D_GRAPHICS_TOON_OUTLINE_STRESS");
        outlineStress != nullptr && std::string_view(outlineStress) == "1") {
        initial.Effects.Toon = ToonMode::PostProcessPreview;
        initial.Effects.ToonStyle.OutlineEnabled = true;
        initial.Effects.ToonStyle.OutlineWidth = 3.0F;
        initial.Effects.ToonStyle.OutlineDepthSensitivity = 8.0F;
        initial.Effects.ToonStyle.OutlineTint = { 1.0F, 0.0F, 1.0F };
        initial.Effects.ToonStyle.OutlineOpacity = 1.0F;
        customized = true;
    }
    if (customized) {
        initial.Preset = GraphicsPreset::Custom;
        selectedInitial = true;
    }
    // The host found that the previous launch with these settings ended in
    // its first minute (TRIAEVUM_SAFE_PRESENTATION carries why): the stored
    // presentation (resolutions, window mode, anti-aliasing and upscaler) may
    // be what ends it, so it restarts from the defaults and is stored so,
    // instead of failing at every launch. Everything else is kept.
    if (const char* safe = std::getenv("TRIAEVUM_SAFE_PRESENTATION");
        selectedInitial && safe != nullptr && *safe != '\0') {
        const GraphicsSettings defaults{};
        initial.Window = defaults.Window;
        initial.OutputWidth = defaults.OutputWidth;
        initial.OutputHeight = defaults.OutputHeight;
        initial.InternalResolutionScale = defaults.InternalResolutionScale;
        initial.AntiAliasing = defaults.AntiAliasing;
        initial.MsaaSamples = defaults.MsaaSamples;
        initial.Upscaler = defaults.Upscaler;
        initial.UpscalerMode = defaults.UpscalerMode;
        rewriteLoadedSettings = true;
        SPDLOG_WARN("OOT3D graphics: {}; display, resolution, anti-aliasing and upscaler settings were reset "
                    "to their defaults", safe);
        std::fprintf(stderr, "TRIAEVUM_SAFE_PRESENTATION reason=\"%s\"\n", safe);
    }
    if (selectedInitial) {
        mService = GraphicsSettingsService(std::move(initial));
    }
    if (rewriteLoadedSettings && !mPersistenceSuppressed) {
        PersistCurrentLocked();
    }
}
GraphicsSettingsRuntime& GraphicsSettingsRuntime::Instance() {
    static GraphicsSettingsRuntime runtime;
    return runtime;
}
GraphicsSettings GraphicsSettingsRuntime::Snapshot() const {
    std::scoped_lock lock(mMutex);
    return mService.Current();
}
VersionedGraphicsSettings
GraphicsSettingsRuntime::SnapshotWithRevision() const {
    std::scoped_lock lock(mMutex);
    return {mService.Current(), mRevision};
}
VersionedGraphicsSettings GraphicsSettingsRuntime::SnapshotForRendering() const {
    std::scoped_lock lock(mMutex);
    VersionedGraphicsSettings result{mService.Current(), mRevision};
    mNativePresentation.Apply(result.Value);
    if (!UsesFullSceneLighting(result.Value.Effects.SurfaceLighting)) {
        result.Value.Effects.DirectionalShadows.Mode = DirectionalShadowMode::Off;
        result.Value.Effects.PointShadows.Mode = PointShadowMode::Off;
        result.Value.Effects.GlobalIllumination.Mode = GlobalIlluminationMode::Off;
    }
    // A GI mode this build or device does not offer (the experimental modes
    // outside builds that opt in) renders Off, whatever the loaded
    // configuration asks for; F1 shows the capability's reason.
    auto& globalIllumination = result.Value.Effects.GlobalIllumination;
    if ((globalIllumination.Mode == GlobalIlluminationMode::BrixelizerGi &&
         !mCapabilities.Has(GraphicsCapability::BrixelizerGlobalIllumination)) ||
        (globalIllumination.Mode == GlobalIlluminationMode::ScreenSpace &&
         !mCapabilities.Has(GraphicsCapability::ScreenSpaceGlobalIllumination)))
        globalIllumination.Mode = GlobalIlluminationMode::Off;
    // Likewise an AO provider that is not available (MXAO without the
    // separately installed iMMERSE module, CACAO without its SDK) renders Off;
    // the preference stays saved for when it is.
    auto& ambientOcclusion = result.Value.Effects.AmbientOcclusion;
    if ((ambientOcclusion == AmbientOcclusionMode::Mxao && !mCapabilities.Has(GraphicsCapability::ImmerseMxao)) ||
        (ambientOcclusion == AmbientOcclusionMode::Cacao && !mCapabilities.Has(GraphicsCapability::FidelityFxCacao)))
        ambientOcclusion = AmbientOcclusionMode::Off;
    // Inactive directional/ambient edits must not invalidate histories.
    // Point-light groups remain independently controllable in per-pixel modes.
    if (!mNativePresentation.Active() && result.Value.Preset != GraphicsPreset::Authentic &&
        UsesFullSceneLighting(result.Value.Effects.SurfaceLighting)) {
        if (mSceneEnvironment.Enabled) result.Environment = mSceneEnvironment;
        else {
            result.Environment.ScopeIdentity = mSceneEnvironment.ScopeIdentity;
            result.Environment.PointLights = mSceneEnvironment.PointLights;
            // Caster policy is independent of the authored light override.
            result.Environment.CasterFaces = mSceneEnvironment.CasterFaces;
        }
    }
    // Inactive rim edits neither change renderer state nor invalidate histories.
    if (!result.Environment.Rim.Enabled) result.Environment.Rim = {};
    return result;
}
void GraphicsSettingsRuntime::PublishSceneEnvironment(
    const Renderer3ds::PicaEnvironmentLighting& environment) {
    std::scoped_lock lock(mMutex);
    mSceneEnvironment = environment;
}
bool GraphicsSettingsRuntime::NativePresentationOverrideActive() const {
    std::scoped_lock lock(mMutex);
    return mNativePresentation.Active();
}
bool GraphicsSettingsRuntime::NativePresentationOverrideRequired() const {
    std::scoped_lock lock(mMutex);
    return mNativePresentation.Required();
}
void GraphicsSettingsRuntime::ToggleNativePresentationOverride() {
    std::scoped_lock lock(mMutex);
    if (!mNativePresentation.Toggle()) return;
    ++mRevision;
    SPDLOG_INFO("F2 native presentation override: {}", mNativePresentation.Active() ? "on" : "off");
}
GraphicsCapabilities GraphicsSettingsRuntime::Capabilities() const {
    std::scoped_lock lock(mMutex);
    return mCapabilities;
}
PresentationTransactionStatus
GraphicsSettingsRuntime::PresentationStatus() const {
    std::scoped_lock lock(mMutex);
    return mPresentationTransaction.Status(
        PresentationClockMilliseconds());
}
GraphicsSettingsValidation GraphicsSettingsRuntime::Apply(GraphicsSettings candidate, bool persist) {
    std::scoped_lock lock(mMutex);
    return ApplyLocked(std::move(candidate), mCapabilities, persist);
}
GraphicsSettingsValidation GraphicsSettingsRuntime::RestorePreferences(GraphicsSettings stored,
                                                                       bool persist) {
    std::scoped_lock lock(mMutex);
    // Structural validation only: every capability counts as present.
    GraphicsCapabilities unrestricted;
    for (uint32_t index = 0; index <= static_cast<uint32_t>(kLastGraphicsCapability); ++index) {
        unrestricted.Set(static_cast<GraphicsCapability>(index), true);
    }
    return ApplyLocked(std::move(stored), unrestricted, persist);
}
GraphicsSettingsValidation GraphicsSettingsRuntime::ApplyLocked(
    GraphicsSettings candidate, const GraphicsCapabilities& capabilities, bool persist) {
    const auto previous =
        GetPresentationSettings(mService.Current());
    auto result = mService.Stage(std::move(candidate), capabilities);
    if (mService.ApplyPending()) {
        ++mRevision;
        const auto current =
            GetPresentationSettings(mService.Current());
        if (current != previous) {
            mPresentationTransaction.Begin(previous, current);
        }
        mService.ConfirmCurrent();
        mSaveState = GraphicsSettingsSaveState::Pending;
        if (persist) PersistCurrentLocked();
    }
    return result;
}
GraphicsDisplayMetrics GraphicsSettingsRuntime::DisplayMetrics() const {
    std::scoped_lock lock(mMutex);
    return mDisplayMetrics;
}
void GraphicsSettingsRuntime::PublishDisplayMetrics(GraphicsDisplayMetrics metrics) {
    std::scoped_lock lock(mMutex);
    if (metrics.OutputWidth == mDisplayMetrics.OutputWidth &&
        metrics.OutputHeight == mDisplayMetrics.OutputHeight &&
        metrics.InternalScale == mDisplayMetrics.InternalScale) {
        metrics.SceneWidth = mDisplayMetrics.SceneWidth;
        metrics.SceneHeight = mDisplayMetrics.SceneHeight;
    }
    mDisplayMetrics = metrics;
}
void GraphicsSettingsRuntime::PublishSceneExtent(uint32_t width, uint32_t height) {
    std::scoped_lock lock(mMutex);
    mDisplayMetrics.SceneWidth = width;
    mDisplayMetrics.SceneHeight = height;
}
GlobalIlluminationProviderStatus GraphicsSettingsRuntime::GlobalIlluminationStatus() const {
    std::scoped_lock lock(mMutex);
    return mGlobalIlluminationStatus;
}
void GraphicsSettingsRuntime::PublishGlobalIlluminationStatus(GlobalIlluminationProviderStatus status) {
    std::scoped_lock lock(mMutex);
    mGlobalIlluminationStatus = std::move(status);
}
GraphicsSettingsSaveState GraphicsSettingsRuntime::SaveState() const {
    std::scoped_lock lock(mMutex);
    if (mPersistenceSuppressed || mPersistence == nullptr)
        return GraphicsSettingsSaveState::SessionOnly;
    return mSaveState;
}
void GraphicsSettingsRuntime::SavePending() {
    std::scoped_lock lock(mMutex);
    if (mSaveState == GraphicsSettingsSaveState::Pending)
        PersistCurrentLocked();
}
bool GraphicsSettingsRuntime::RetrySave() {
    std::scoped_lock lock(mMutex);
    return PersistCurrentLocked();
}
bool GraphicsSettingsRuntime::AcknowledgePresentationApplied(
    const GraphicsSettings& applied) {
    std::scoped_lock lock(mMutex);
    const auto phase = mPresentationTransaction.Status(
        PresentationClockMilliseconds()).Phase;
    const bool acknowledged = mPresentationTransaction.MarkApplied(
        GetPresentationSettings(applied),
        PresentationClockMilliseconds(), true);
    // Only a successfully applied new candidate supersedes the previous failure.
    // A rollback acknowledgement is recovery, not a successful user request.
    if (acknowledged && phase == PresentationTransactionPhase::ApplyRequested) {
        mLastPresentationRejection.clear();
    }
    if (acknowledged &&
        (phase == PresentationTransactionPhase::RollbackRequested ||
         mPresentationTransaction.Status(PresentationClockMilliseconds())
                 .Phase == PresentationTransactionPhase::Idle)) {
        PersistCurrentLocked();
    }
    return acknowledged;
}
bool GraphicsSettingsRuntime::RejectPresentationApply(
    const GraphicsSettings& rejected, std::string reason) {
    std::scoped_lock lock(mMutex);
    const auto status = mPresentationTransaction.Status(
        PresentationClockMilliseconds());
    if (GetPresentationSettings(rejected) !=
        GetPresentationSettings(mService.Current())) {
        return false;
    }
    if (status.Phase ==
            PresentationTransactionPhase::ApplyRequested ||
        status.Phase ==
            PresentationTransactionPhase::AwaitingConfirmation) {
        mPresentationTransaction.RequestRollback();
        if (!RestoreLastKnownPresentationLocked()) {
            return false;
        }
        mLastPresentationRejection = std::move(reason);
        mPresentationTransaction.MarkApplied(
            status.LastKnownGood,
            PresentationClockMilliseconds());
        PersistCurrentLocked();
        return true;
    }
    return status.Phase ==
        PresentationTransactionPhase::RollbackRequested;
}
std::string GraphicsSettingsRuntime::LastPresentationRejection() const {
    std::scoped_lock lock(mMutex);
    return mLastPresentationRejection;
}
bool GraphicsSettingsRuntime::ConfirmPresentation() {
    std::scoped_lock lock(mMutex);
    if (!mPresentationTransaction.Confirm()) {
        return false;
    }
    PersistCurrentLocked();
    return true;
}
void GraphicsSettingsRuntime::PresentationConfirmationVisible() {
    std::scoped_lock lock(mMutex);
    mPresentationTransaction.ConfirmationVisible(PresentationClockMilliseconds());
}
bool GraphicsSettingsRuntime::RollbackPresentation() {
    std::scoped_lock lock(mMutex);
    if (!mPresentationTransaction.RequestRollback()) {
        return false;
    }
    return RestoreLastKnownPresentationLocked();
}
bool GraphicsSettingsRuntime::TickPresentation() {
    std::scoped_lock lock(mMutex);
    if (!mPresentationTransaction.Advance(
            PresentationClockMilliseconds())) {
        return false;
    }
    return RestoreLastKnownPresentationLocked();
}
void GraphicsSettingsRuntime::SetCapability(GraphicsCapability capability, bool available,
                                             std::string reason) {
    std::scoped_lock lock(mMutex);
    mCapabilities.Set(capability, available, std::move(reason));
}
bool GraphicsSettingsRuntime::RestoreLastKnownPresentationLocked() {
    auto restored = mService.Current();
    SetPresentationSettings(
        restored, mPresentationTransaction.LastKnownGood());
    auto validation =
        mService.Stage(std::move(restored), mCapabilities);
    if (!validation.Accepted() || !mService.ApplyPending()) {
        return false;
    }
    ++mRevision;
    mService.ConfirmCurrent();
    return true;
}
bool GraphicsSettingsRuntime::PersistCurrentLocked() {
    const bool presentationActive =
        mPresentationTransaction.Status(
            PresentationClockMilliseconds()).Active();
    if (mPersistenceSuppressed || mPersistence == nullptr) {
        mSaveState = GraphicsSettingsSaveState::SessionOnly;
        return true;
    }
    if (presentationActive) {
        mSaveState = GraphicsSettingsSaveState::WaitingForDisplay;
        return true;
    }
    try {
        const bool saved = mPersistence->StoreGraphics(
            SerializeGraphicsSettings(mService.Current()));
        mSaveState = saved ? GraphicsSettingsSaveState::Saved
                           : GraphicsSettingsSaveState::Failed;
        return saved;
    } catch (const std::exception& exception) {
        mSaveState = GraphicsSettingsSaveState::Failed;
        SPDLOG_ERROR("Unable to persist OOT3D graphics settings: {}",
                     exception.what());
        return false;
    }
}
std::vector<std::string> GraphicsSettingsRuntime::UserPresetNames() const {
    std::scoped_lock lock(mMutex);
    std::vector<std::string> names;
    for (const auto& preset : mUserPresets.Presets()) names.push_back(preset.Name);
    return names;
}
bool GraphicsSettingsRuntime::HasUserPreset(std::string_view name) const {
    std::scoped_lock lock(mMutex);
    return mUserPresets.Find(name) != nullptr;
}
std::string GraphicsSettingsRuntime::ActiveUserPreset() const {
    std::scoped_lock lock(mMutex);
    if (mActiveUserPresetRevision != mRevision || mActiveUserPresetGeneration != mUserPresetGeneration) {
        const auto* match = mUserPresets.Match(mService.Current());
        mActiveUserPreset = match != nullptr ? match->Name : std::string{};
        mActiveUserPresetRevision = mRevision;
        mActiveUserPresetGeneration = mUserPresetGeneration;
    }
    return mActiveUserPreset;
}
std::string GraphicsSettingsRuntime::UserPresetsReadOnlyReason() const {
    std::scoped_lock lock(mMutex);
    return mUserPresets.ReadOnlyReason();
}
GraphicsPresetChange GraphicsSettingsRuntime::SaveUserPreset(std::string_view name) {
    std::scoped_lock lock(mMutex);
    if (!mUserPresets.ReadOnlyReason().empty()) return {false, mUserPresets.ReadOnlyReason()};
    if (const auto error = GraphicsPresetLibrary::ValidateName(name); error != GraphicsPresetNameError::None)
        return {false, GraphicsPresetLibrary::DescribeNameError(error)};
    auto previous = mUserPresets;
    const bool replaced = mUserPresets.Find(name) != nullptr;
    mUserPresets.Save(name, mService.Current());
    ++mUserPresetGeneration;
    const auto stored = GraphicsPresetLibrary::NormalizeName(name);
    return PersistUserPresetsLocked(std::move(previous),
                                    (replaced ? "Replaced preset \"" : "Saved preset \"") + stored + "\"");
}
GraphicsPresetChange GraphicsSettingsRuntime::LoadUserPreset(std::string_view name) {
    std::scoped_lock lock(mMutex);
    const auto* preset = mUserPresets.Find(name);
    if (preset == nullptr) return {false, "No user preset is named \"" + std::string(name) + "\""};
    auto candidate = GraphicsSettingsService::KeepHostOwnedSettings(preset->Settings, mService.Current());
    candidate.Preset = GraphicsPreset::Custom;
    const auto loadedName = preset->Name;
    const auto validation = ApplyLocked(std::move(candidate), mCapabilities, true);
    GraphicsPresetChange change{validation.Accepted(),
                                (validation.Accepted() ? "Loaded preset \"" : "Rejected preset \"") + loadedName + "\""};
    for (const auto& issue : validation.Issues) change.Message += "\n" + issue.Field + ": " + issue.Message;
    return change;
}
GraphicsPresetChange GraphicsSettingsRuntime::DeleteUserPreset(std::string_view name) {
    std::scoped_lock lock(mMutex);
    if (!mUserPresets.ReadOnlyReason().empty()) return {false, mUserPresets.ReadOnlyReason()};
    const auto* preset = mUserPresets.Find(name);
    if (preset == nullptr) return {false, "No user preset is named \"" + std::string(name) + "\""};
    const auto deletedName = preset->Name;
    auto previous = mUserPresets;
    mUserPresets.Remove(name);
    ++mUserPresetGeneration;
    return PersistUserPresetsLocked(std::move(previous), "Deleted preset \"" + deletedName + "\"");
}
GraphicsPresetChange GraphicsSettingsRuntime::PersistUserPresetsLocked(
    GraphicsPresetLibrary previous, std::string done) {
    if (mPersistenceSuppressed || mPersistence == nullptr)
        return {true, done + " for this session only (settings are not being saved)."};
    std::string failure;
    try {
        if (mPersistence->StoreGraphicsPresets(mUserPresets.Serialize())) return {true, done + "."};
        failure = "the configuration file could not be written";
    } catch (const std::exception& exception) {
        failure = exception.what();
    }
    SPDLOG_ERROR("Unable to persist OOT3D graphics presets: {}", failure);
    mUserPresets = std::move(previous);
    ++mUserPresetGeneration;
    return {false, "Not saved: " + failure + ". The presets are unchanged."};
}
} // namespace Fast::Oot3d
