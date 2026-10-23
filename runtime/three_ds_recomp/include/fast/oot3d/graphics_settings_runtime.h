#pragma once

#include "fast/oot3d/graphics_preset_library.h"
#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/native_presentation_policy.h"
#include "fast/oot3d/presentation_settings_transaction.h"
#include "fast/renderer3ds/pica_environment_lighting.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace Fast::Oot3d {

class GraphicsSettingsPersistencePort {
  public:
    virtual ~GraphicsSettingsPersistencePort() = default;
    [[nodiscard]] virtual bool LoadRoot(nlohmann::json& root) = 0;
    [[nodiscard]] virtual bool StoreGraphics(
        const nlohmann::json& graphics) = 0;
    // The user's graphics presets: the top-level "GraphicsPresets" block.
    [[nodiscard]] virtual bool StoreGraphicsPresets(
        const nlohmann::json& presets) = 0;
};

// The outcome of a user preset action, with the reason when it failed.
struct GraphicsPresetChange {
    bool Succeeded = false;
    std::string Message;
};

// Install before the first call to GraphicsSettingsRuntime::Instance().
// The renderer remains usable without a persistence port.
void InstallGraphicsSettingsPersistencePort(
    std::shared_ptr<GraphicsSettingsPersistencePort> persistence);

struct VersionedGraphicsSettings {
    GraphicsSettings Value;
    uint64_t Revision = 0;
    Renderer3ds::PicaEnvironmentLighting Environment;
};

// Observed renderer state, never persisted as a user request.
struct GraphicsDisplayMetrics {
    uint32_t OutputWidth = 0, OutputHeight = 0;
    uint32_t SceneWidth = 0, SceneHeight = 0;
    float InternalScale = 1.0F;
    WindowMode Window = WindowMode::Windowed;
    // The picture inside the output (the aspect ratio's letterbox); the whole
    // output when it is not known yet.
    uint32_t PictureWidth = 0, PictureHeight = 0;
};

// Observed state of the global illumination provider, never persisted: a
// selected mode whose provider is not ready yet (pipelines created off the
// frame) composes no GI and says why.
struct GlobalIlluminationProviderStatus {
    GlobalIlluminationMode Mode = GlobalIlluminationMode::Off;
    bool Ready = false;
    std::string Reason;
    bool operator==(const GlobalIlluminationProviderStatus&) const = default;
};

enum class GraphicsSettingsSaveState {
    Saved, Pending, WaitingForDisplay, SessionOnly, Failed
};

class GraphicsSettingsRuntime final {
  public:
    static GraphicsSettingsRuntime& Instance();
    [[nodiscard]] GraphicsSettings Snapshot() const;
    [[nodiscard]] VersionedGraphicsSettings SnapshotWithRevision() const;
    // Rendering consumes the session override; editors/persistence consume Snapshot().
    [[nodiscard]] VersionedGraphicsSettings SnapshotForRendering() const;
    [[nodiscard]] bool NativePresentationOverrideActive() const;
    [[nodiscard]] bool NativePresentationOverrideRequired() const;
    void ToggleNativePresentationOverride();
    [[nodiscard]] GraphicsCapabilities Capabilities() const;
    [[nodiscard]] PresentationTransactionStatus PresentationStatus() const;
    [[nodiscard]] GraphicsDisplayMetrics DisplayMetrics() const;
    void PublishDisplayMetrics(GraphicsDisplayMetrics metrics);
    void PublishSceneExtent(uint32_t width, uint32_t height);
    [[nodiscard]] GlobalIlluminationProviderStatus GlobalIlluminationStatus() const;
    void PublishGlobalIlluminationStatus(GlobalIlluminationProviderStatus status);
    // Resolved title-owned profile; never serialized into global graphics settings.
    void PublishSceneEnvironment(const Renderer3ds::PicaEnvironmentLighting& environment);
    GraphicsSettingsValidation Apply(GraphicsSettings candidate, bool persist = true);
    // Restores previously stored preferences the way the startup load does: a
    // capability missing at this moment (for example view metadata outside
    // gameplay) leaves its effect inert instead of erasing the choice.
    GraphicsSettingsValidation RestorePreferences(GraphicsSettings stored, bool persist = true);
    // User graphics presets. The built-in presets are not among them and
    // cannot be overwritten or deleted. Saving stores the current settings'
    // preset part under the name (replacing a user preset of that name);
    // loading applies it like a built-in preset, keeping window, output and
    // content identities; both are written at once, never left pending.
    [[nodiscard]] std::vector<std::string> UserPresetNames() const;
    [[nodiscard]] bool HasUserPreset(std::string_view name) const;
    // The user preset the current settings are, unmodified; empty otherwise.
    [[nodiscard]] std::string ActiveUserPreset() const;
    [[nodiscard]] std::string UserPresetsReadOnlyReason() const;
    GraphicsPresetChange SaveUserPreset(std::string_view name);
    GraphicsPresetChange LoadUserPreset(std::string_view name);
    GraphicsPresetChange DeleteUserPreset(std::string_view name);
    [[nodiscard]] GraphicsSettingsSaveState SaveState() const;
    void SavePending();
    bool RetrySave();
    bool AcknowledgePresentationApplied(
        const GraphicsSettings& applied);
    bool RejectPresentationApply(const GraphicsSettings& rejected, std::string reason = {});
    [[nodiscard]] std::string LastPresentationRejection() const;
    bool ConfirmPresentation();
    void PresentationConfirmationVisible();
    bool RollbackPresentation();
    bool TickPresentation();
    void SetCapability(GraphicsCapability capability, bool available, std::string reason = {});
  private:
    GraphicsSettingsRuntime();
    bool RestoreLastKnownPresentationLocked();
    bool PersistCurrentLocked();
    // Writes the preset library, or restores `previous` and says why not.
    GraphicsPresetChange PersistUserPresetsLocked(GraphicsPresetLibrary previous, std::string done);
    GraphicsSettingsValidation ApplyLocked(GraphicsSettings candidate,
                                           const GraphicsCapabilities& capabilities, bool persist);

    mutable std::mutex mMutex;
    GraphicsSettingsService mService;
    GraphicsCapabilities mCapabilities;
    GraphicsDisplayMetrics mDisplayMetrics;
    GlobalIlluminationProviderStatus mGlobalIlluminationStatus;
    Renderer3ds::PicaEnvironmentLighting mSceneEnvironment;
    PresentationSettingsTransaction mPresentationTransaction;
    std::string mLastPresentationRejection;
    std::shared_ptr<GraphicsSettingsPersistencePort> mPersistence;
    uint64_t mRevision = 1;
    NativePresentationPolicy mNativePresentation;
    bool mPersistenceSuppressed = false;
    GraphicsSettingsSaveState mSaveState = GraphicsSettingsSaveState::Saved;
    GraphicsPresetLibrary mUserPresets;
    uint64_t mUserPresetGeneration = 1;
    // ActiveUserPreset() for (mRevision, mUserPresetGeneration).
    mutable uint64_t mActiveUserPresetRevision = 0;
    mutable uint64_t mActiveUserPresetGeneration = 0;
    mutable std::string mActiveUserPreset;
};
} // namespace Fast::Oot3d
