#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

class NativeA32Process;

struct NativeScenarioRuntimeContract {
  uint32_t RequestTransitionFunction = 0;
  uint32_t PrepareTransitionEffectFunction = 0;
  uint32_t DirectCallReturnAddress = 0;
  uint32_t CurrentEntranceAddress = 0;
  uint32_t PlaySceneIdOffset = 0;
  uint32_t PendingEntranceOffset = 0;
  uint32_t PendingTriggerOffset = 0;
  uint32_t TransitionEffectOffset = 0;
  uint8_t TransitionTrigger = 0;
  std::string AcceptedTransitionIdentity;
  std::string NativeDepartureCompletion;
};

struct NativeScenarioCoverage {
  uint32_t SetupCount = 0;
  uint32_t GameplaySetupCount = 0;
  uint32_t CutsceneSetupCount = 0;
  uint32_t SpawnCount = 0;
  uint32_t TransitionActorCount = 0;
  uint32_t LightSettingCount = 0;
  uint32_t CutsceneReferenceCount = 0;
  uint32_t ActorPlacementCount = 0;
  uint32_t DecodedLightRecordCount = 0;
  uint32_t CutsceneSourceCount = 0;
  uint32_t UnresolvedCommandCount = 0;
};

struct NativeScenarioRecipe {
  std::string Id;
  std::string Category;
  uint16_t EntranceIndex = 0;
  uint8_t SceneId = 0;
  uint8_t LocalEntranceIndex = 0;
  uint16_t Field = 0;
  std::string ScenePath;
  std::string SceneSourceBasename;
  std::string SceneIndexSymbol;
  std::string SemanticScene;
  std::string ValidationStatus;
  std::string ReferenceKinds;
  uint32_t ReferenceUseCount = 0;
  NativeScenarioCoverage Coverage;
  uint32_t SettleFrames = 0;
  uint32_t CaptureFrames = 0;
  uint32_t ReadyTimeoutGuestFrames = 0;
};

struct NativeScenarioCatalog {
  std::filesystem::path SourcePath;
  std::string SourcePolicy;
  NativeScenarioRuntimeContract Runtime;
  std::vector<NativeScenarioRecipe> Recipes;
  uint32_t DeclaredRecipeCount = 0;
  uint32_t DeclaredUniqueSceneCount = 0;
};

NativeScenarioCatalog
LoadNativeScenarioCatalog(const std::filesystem::path &path);

const NativeScenarioRecipe *
FindNativeScenarioRecipe(const NativeScenarioCatalog &catalog,
                         const std::string &id) noexcept;

enum class NativeScenarioBootstrapStatus : uint8_t {
  WaitingForPlayState,
  WaitingForTransitionSlot,
  Requested,
  Settling,
  Capturing,
  Complete,
  Failed,
};

const char *NativeScenarioBootstrapStatusName(
    NativeScenarioBootstrapStatus status) noexcept;

struct NativeScenarioBootstrapStats {
  NativeScenarioBootstrapStatus Status =
      NativeScenarioBootstrapStatus::WaitingForPlayState;
  uint64_t FirstObservedGuestFrame = 0;
  uint64_t RequestGuestFrame = 0;
  uint64_t ReadyGuestFrame = 0;
  uint64_t CompleteGuestFrame = 0;
  uint32_t PlayStateAddress = 0;
  uint32_t CurrentEntrance = 0;
  uint16_t PendingEntrance = 0xFFFFU;
  uint16_t ObservedSceneId = 0xFFFFU;
  uint8_t TransitionEffect = 0;
  uint32_t SettleFramesObserved = 0;
  uint32_t CaptureFramesObserved = 0;
  uint64_t PlayStateWaitFrames = 0;
  uint64_t TransitionSlotWaitFrames = 0;
  uint64_t TransitionHelperCalls = 0;
  uint64_t TransitionEffectPreparationCalls = 0;
  uint64_t TransitionHelperRejections = 0;
  bool TransitionRequestAccepted = false;
  uint64_t RequestedEntranceFirstObservedGuestFrame = 0;
  uint64_t TargetSceneAfterEntranceGuestFrame = 0;
  bool RequestedEntranceObserved = false;
  bool TargetSceneObservedAfterEntrance = false;
  uint32_t ResolvedEntrance = 0;
  uint16_t NativeDepartureEntrance = 0xFFFFU;
  bool TargetIdentityValidated = false;
  bool EntranceNormalizedByNativeLoader = false;
  bool AlreadyAtTarget = false;
  std::string IdentityValidation;
  std::string CompletionReason;
  std::string Error;
};

struct NativeScenarioStateObservation {
  uint64_t GuestFrame = 0;
  uint32_t CurrentEntrance = 0;
  uint16_t PendingEntrance = 0xFFFFU;
  uint16_t SceneId = 0xFFFFU;
};

// Requests only a native code.bin scene transition. The original game remains
// responsible for loading scene/room data and constructing every subsystem.
class NativeScenarioBootstrap {
public:
  NativeScenarioBootstrap(NativeScenarioRuntimeContract runtime,
                          NativeScenarioRecipe recipe);

  bool Advance(NativeA32Process &process, uint32_t playStateAddress,
               uint64_t guestFrame, std::string *error = nullptr);
  bool Finished() const noexcept;
  bool Ready() const noexcept;
  bool Complete() const noexcept;

  const NativeScenarioRecipe &Recipe() const noexcept;
  const NativeScenarioBootstrapStats &Stats() const noexcept;
  nlohmann::json ToJson() const;

private:
  bool ReadState(NativeA32Process &process, uint32_t playStateAddress,
                 uint32_t *currentEntrance, uint16_t *pendingEntrance,
                 uint16_t *sceneId, std::string *error);
  bool Fail(std::string message, std::string *error);

  NativeScenarioRuntimeContract mRuntime;
  NativeScenarioRecipe mRecipe;
  NativeScenarioBootstrapStats mStats;
  std::vector<NativeScenarioStateObservation> mStateTrace;
  bool mObservedAnyFrame = false;
  bool mReachedTarget = false;
};

} // namespace Oot3dNativeGame
