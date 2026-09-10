#include "fast/oot3d/graphics_settings_runtime.h"

#include "fast/oot3d/graphics_settings_persistence.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string_view>

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

std::shared_ptr<GraphicsSettingsPersistencePort>
SnapshotPersistencePort() {
    std::scoped_lock lock(PersistencePortMutex());
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
        "OOT3D_GRAPHICS_CACAO",
        "OOT3D_GRAPHICS_CACAO_QUALITY",
        "OOT3D_GRAPHICS_CACAO_STRESS",
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
        "OOT3D_GRAPHICS_TEST_CACAO_AFTER_FRAMES",
        "OOT3D_GRAPHICS_TEST_CACAO_OFF_AFTER_FRAMES",
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
    if (const char* cacao = std::getenv("OOT3D_GRAPHICS_CACAO");
        cacao != nullptr && std::string_view(cacao) == "1") {
        initial.Effects.AmbientOcclusion = AmbientOcclusionMode::Cacao;
        customized = true;
    }
    if (const char* cacaoQuality =
            std::getenv("OOT3D_GRAPHICS_CACAO_QUALITY");
        cacaoQuality != nullptr) {
        const std::string_view quality(cacaoQuality);
        if (quality == "0" || quality == "1") {
            initial.Effects.AoQuality =
                quality == "1" ? 1U : 0U;
            customized = true;
        }
    }
    if (const char* stress =
            std::getenv("OOT3D_GRAPHICS_CACAO_STRESS");
        stress != nullptr && std::string_view(stress) == "1") {
        initial.Effects.AmbientOcclusion =
            AmbientOcclusionMode::Cacao;
        initial.Effects.AoQuality = 1U;
        initial.Effects.AoRadius = 12.0F;
        initial.Effects.AoStrength = 4.0F;
        initial.Effects.AoShadowPower = 4.0F;
        initial.Effects.AoShadowClamp = 0.72F;
        initial.Effects.AoHorizonAngleThreshold = 0.20F;
        initial.Effects.AoFadeOutFrom = 0.0F;
        initial.Effects.AoFadeOutTo = 5000.0F;
        initial.Effects.AoBlurPassCount = 0U;
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
    if (mNativePresentationOverride) {
        result.Value.Grass.Quality = GrassQuality::Off;
        result.Value.Effects.Toon = ToonMode::Off;
        result.Value.Effects.ToonStyle.OutlineEnabled = false;
        result.Value.Effects.AmbientOcclusion = AmbientOcclusionMode::Off;
        result.Value.Effects.Reflections = ReflectionMode::Off;
    }
    return result;
}
bool GraphicsSettingsRuntime::NativePresentationOverrideActive() const {
    std::scoped_lock lock(mMutex);
    return mNativePresentationOverride;
}
void GraphicsSettingsRuntime::ToggleNativePresentationOverride() {
    std::scoped_lock lock(mMutex);
    mNativePresentationOverride = !mNativePresentationOverride;
    ++mRevision;
    SPDLOG_INFO("F2 native presentation override: {}", mNativePresentationOverride ? "on" : "off");
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
    const auto previous =
        GetPresentationSettings(mService.Current());
    auto result = mService.Stage(std::move(candidate), mCapabilities);
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
std::string GraphicsSettingsRuntime::LastPresentationRejection() const {
    std::scoped_lock lock(mMutex);
    return mLastPresentationRejection;
}
bool GraphicsSettingsRuntime::AcknowledgePresentationApplied(
    const GraphicsSettings& applied) {
    std::scoped_lock lock(mMutex);
    const auto phase = mPresentationTransaction.Status(
        PresentationClockMilliseconds()).Phase;
    const bool acknowledged = mPresentationTransaction.MarkApplied(
        GetPresentationSettings(applied),
        PresentationClockMilliseconds());
    if (acknowledged) {
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
    mLastPresentationRejection = std::move(reason);
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
        mPresentationTransaction.MarkApplied(
            status.LastKnownGood,
            PresentationClockMilliseconds());
        PersistCurrentLocked();
        return true;
    }
    return status.Phase ==
        PresentationTransactionPhase::RollbackRequested;
}
bool GraphicsSettingsRuntime::ConfirmPresentation() {
    std::scoped_lock lock(mMutex);
    if (!mPresentationTransaction.Confirm()) {
        return false;
    }
    PersistCurrentLocked();
    return true;
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
} // namespace Fast::Oot3d
