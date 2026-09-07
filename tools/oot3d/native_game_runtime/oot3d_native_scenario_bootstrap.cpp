#include "oot3d_native_scenario_bootstrap.h"

#include "oot3d_native_a32_process.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr const char *kCatalogFormat = "oot3d_structural_scenario_catalog_v2";
constexpr const char *kAcceptedTransitionIdentity =
    "requested_entrance_observed_before_target_scene";
constexpr const char *kNativeDepartureCompletion = "complete_after_settle";

template <typename T>
T RequiredUnsigned(const nlohmann::json &object, const char *name,
                   T maximum = std::numeric_limits<T>::max()) {
  const auto iterator = object.find(name);
  if (iterator == object.end() || !iterator->is_number_unsigned()) {
    throw std::runtime_error(
        std::string("scenario catalog field is missing: ") + name);
  }
  const uint64_t value = iterator->get<uint64_t>();
  if (value > static_cast<uint64_t>(maximum)) {
    throw std::runtime_error(
        std::string("scenario catalog field is too large: ") + name);
  }
  return static_cast<T>(value);
}

std::string RequiredString(const nlohmann::json &object, const char *name) {
  const auto iterator = object.find(name);
  if (iterator == object.end() || !iterator->is_string() ||
      iterator->get_ref<const std::string &>().empty()) {
    throw std::runtime_error(
        std::string("scenario catalog field is missing: ") + name);
  }
  return iterator->get<std::string>();
}

NativeScenarioCoverage ParseCoverage(const nlohmann::json &document) {
  return {
      .SetupCount = RequiredUnsigned<uint32_t>(document, "setup_count"),
      .GameplaySetupCount =
          RequiredUnsigned<uint32_t>(document, "gameplay_setup_count"),
      .CutsceneSetupCount =
          RequiredUnsigned<uint32_t>(document, "cutscene_setup_count"),
      .SpawnCount = RequiredUnsigned<uint32_t>(document, "spawn_count"),
      .TransitionActorCount =
          RequiredUnsigned<uint32_t>(document, "transition_actor_count"),
      .LightSettingCount =
          RequiredUnsigned<uint32_t>(document, "light_setting_count"),
      .CutsceneReferenceCount =
          RequiredUnsigned<uint32_t>(document, "cutscene_reference_count"),
      .ActorPlacementCount =
          RequiredUnsigned<uint32_t>(document, "actor_placement_count"),
      .DecodedLightRecordCount =
          RequiredUnsigned<uint32_t>(document, "decoded_light_record_count"),
      .CutsceneSourceCount =
          RequiredUnsigned<uint32_t>(document, "cutscene_source_count"),
      .UnresolvedCommandCount =
          RequiredUnsigned<uint32_t>(document, "unresolved_command_count"),
  };
}

NativeScenarioRecipe ParseRecipe(const nlohmann::json &document) {
  if (!document.is_object()) {
    throw std::runtime_error("scenario recipe is not an object");
  }
  const auto &coverage = document.at("coverage");
  const auto &execution = document.at("execution");
  NativeScenarioRecipe recipe{
      .Id = RequiredString(document, "id"),
      .Category = RequiredString(document, "category"),
      .EntranceIndex = RequiredUnsigned<uint16_t>(document, "entrance_index"),
      .SceneId = RequiredUnsigned<uint8_t>(document, "scene_id"),
      .LocalEntranceIndex =
          RequiredUnsigned<uint8_t>(document, "local_entrance_index"),
      .Field = RequiredUnsigned<uint16_t>(document, "field"),
      .ScenePath = RequiredString(document, "scene_path"),
      .SceneSourceBasename = RequiredString(document, "scene_source_basename"),
      .SceneIndexSymbol = RequiredString(document, "scene_index_symbol"),
      .SemanticScene = RequiredString(document, "semantic_scene"),
      .ValidationStatus = RequiredString(document, "validation_status"),
      .ReferenceKinds = RequiredString(document, "reference_kinds"),
      .ReferenceUseCount =
          RequiredUnsigned<uint32_t>(document, "reference_use_count"),
      .Coverage = ParseCoverage(coverage),
      .SettleFrames = RequiredUnsigned<uint32_t>(execution, "settle_frames"),
      .CaptureFrames = RequiredUnsigned<uint32_t>(execution, "capture_frames"),
      .ReadyTimeoutGuestFrames =
          RequiredUnsigned<uint32_t>(execution, "ready_timeout_guest_frames"),
  };
  if (recipe.ReadyTimeoutGuestFrames == 0U || recipe.SettleFrames == 0U ||
      recipe.CaptureFrames == 0U) {
    throw std::runtime_error("scenario recipe has a zero frame budget");
  }
  return recipe;
}

} // namespace

NativeScenarioCatalog
LoadNativeScenarioCatalog(const std::filesystem::path &path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::runtime_error("cannot open structural scenario catalog: " +
                             path.string());
  }
  nlohmann::json document;
  stream >> document;
  if (RequiredString(document, "format") != kCatalogFormat) {
    throw std::runtime_error("unsupported structural scenario catalog: " +
                             path.string());
  }
  const auto &runtime = document.at("runtime_contract");
  const auto &summary = document.at("summary");
  NativeScenarioCatalog catalog;
  catalog.SourcePath = std::filesystem::absolute(path).lexically_normal();
  catalog.SourcePolicy = RequiredString(document, "source_policy");
  catalog.Runtime = {
      .RequestTransitionFunction =
          RequiredUnsigned<uint32_t>(runtime, "request_transition_function"),
      .PrepareTransitionEffectFunction = RequiredUnsigned<uint32_t>(
          runtime, "prepare_transition_effect_function"),
      .DirectCallReturnAddress =
          RequiredUnsigned<uint32_t>(runtime, "direct_call_return_address"),
      .CurrentEntranceAddress =
          RequiredUnsigned<uint32_t>(runtime, "current_entrance_address"),
      .PlaySceneIdOffset =
          RequiredUnsigned<uint32_t>(runtime, "play_scene_id_offset"),
      .PendingEntranceOffset =
          RequiredUnsigned<uint32_t>(runtime, "pending_entrance_offset"),
      .PendingTriggerOffset =
          RequiredUnsigned<uint32_t>(runtime, "pending_trigger_offset"),
      .TransitionEffectOffset =
          RequiredUnsigned<uint32_t>(runtime, "transition_effect_offset"),
      .TransitionTrigger =
          RequiredUnsigned<uint8_t>(runtime, "transition_trigger"),
      .AcceptedTransitionIdentity =
          RequiredString(runtime, "accepted_transition_identity"),
      .NativeDepartureCompletion =
          RequiredString(runtime, "native_departure_completion"),
  };
  catalog.DeclaredRecipeCount =
      RequiredUnsigned<uint32_t>(summary, "recipe_count");
  catalog.DeclaredUniqueSceneCount =
      RequiredUnsigned<uint32_t>(summary, "unique_scene_count");

  const auto &recipes = document.at("recipes");
  if (!recipes.is_array()) {
    throw std::runtime_error("scenario catalog recipes are not an array");
  }
  std::set<std::string> identifiers;
  std::set<uint8_t> scenes;
  for (const auto &documentRecipe : recipes) {
    auto recipe = ParseRecipe(documentRecipe);
    if (!identifiers.insert(recipe.Id).second) {
      throw std::runtime_error("duplicate structural scenario: " + recipe.Id);
    }
    scenes.insert(recipe.SceneId);
    catalog.Recipes.push_back(std::move(recipe));
  }
  if (catalog.Recipes.size() != catalog.DeclaredRecipeCount ||
      scenes.size() != catalog.DeclaredUniqueSceneCount) {
    throw std::runtime_error("structural scenario catalog summary mismatch");
  }
  if (catalog.Runtime.RequestTransitionFunction == 0U ||
      catalog.Runtime.PrepareTransitionEffectFunction == 0U ||
      catalog.Runtime.DirectCallReturnAddress == 0U ||
      catalog.Runtime.CurrentEntranceAddress == 0U ||
      catalog.Runtime.PlaySceneIdOffset == 0U ||
      catalog.Runtime.TransitionTrigger == 0U) {
    throw std::runtime_error(
        "structural scenario runtime contract is incomplete");
  }
  if (catalog.Runtime.AcceptedTransitionIdentity !=
          kAcceptedTransitionIdentity ||
      catalog.Runtime.NativeDepartureCompletion !=
          kNativeDepartureCompletion) {
    throw std::runtime_error(
        "unsupported structural scenario transition policy");
  }
  return catalog;
}

const NativeScenarioRecipe *
FindNativeScenarioRecipe(const NativeScenarioCatalog &catalog,
                         const std::string &id) noexcept {
  for (const auto &recipe : catalog.Recipes) {
    if (recipe.Id == id) {
      return &recipe;
    }
  }
  return nullptr;
}

const char *NativeScenarioBootstrapStatusName(
    NativeScenarioBootstrapStatus status) noexcept {
  switch (status) {
  case NativeScenarioBootstrapStatus::WaitingForPlayState:
    return "waiting_for_play_state";
  case NativeScenarioBootstrapStatus::WaitingForTransitionSlot:
    return "waiting_for_transition_slot";
  case NativeScenarioBootstrapStatus::Requested:
    return "requested";
  case NativeScenarioBootstrapStatus::Settling:
    return "settling";
  case NativeScenarioBootstrapStatus::Capturing:
    return "capturing";
  case NativeScenarioBootstrapStatus::Complete:
    return "complete";
  case NativeScenarioBootstrapStatus::Failed:
    return "failed";
  }
  return "unknown";
}

NativeScenarioBootstrap::NativeScenarioBootstrap(
    NativeScenarioRuntimeContract runtime, NativeScenarioRecipe recipe)
    : mRuntime(runtime), mRecipe(std::move(recipe)) {}

bool NativeScenarioBootstrap::ReadState(NativeA32Process &process,
                                        uint32_t playStateAddress,
                                        uint32_t *currentEntrance,
                                        uint16_t *pendingEntrance,
                                        uint16_t *sceneId, std::string *error) {
  if (!process.Memory().Read32(mRuntime.CurrentEntranceAddress,
                               currentEntrance) ||
      !process.Memory().Read16(
          playStateAddress + mRuntime.PendingEntranceOffset, pendingEntrance) ||
      !process.Memory().Read16(playStateAddress + mRuntime.PlaySceneIdOffset,
                               sceneId)) {
    return Fail("cannot read the native scene-transition state", error);
  }
  mStats.CurrentEntrance = *currentEntrance;
  mStats.PendingEntrance = *pendingEntrance;
  mStats.ObservedSceneId = *sceneId;
  return true;
}

bool NativeScenarioBootstrap::Fail(std::string message, std::string *error) {
  mStats.Status = NativeScenarioBootstrapStatus::Failed;
  mStats.Error = std::move(message);
  if (error != nullptr) {
    *error = mStats.Error;
  }
  return false;
}

bool NativeScenarioBootstrap::Advance(NativeA32Process &process,
                                      uint32_t playStateAddress,
                                      uint64_t guestFrame, std::string *error) {
  if (Finished()) {
    if (error != nullptr) {
      *error = mStats.Error;
    }
    return mStats.Status != NativeScenarioBootstrapStatus::Failed;
  }
  if (!mObservedAnyFrame) {
    mObservedAnyFrame = true;
    mStats.FirstObservedGuestFrame = guestFrame;
  }
  const uint64_t elapsed = guestFrame - mStats.FirstObservedGuestFrame;
  if (!mReachedTarget && elapsed > mRecipe.ReadyTimeoutGuestFrames) {
    return Fail("native scenario did not become ready within its frame budget",
                error);
  }
  if (playStateAddress == 0U) {
    ++mStats.PlayStateWaitFrames;
    mStats.Status = NativeScenarioBootstrapStatus::WaitingForPlayState;
    return true;
  }
  mStats.PlayStateAddress = playStateAddress;

  uint32_t currentEntrance = 0U;
  uint16_t pendingEntrance = 0U;
  uint16_t sceneId = 0U;
  if (!ReadState(process, playStateAddress, &currentEntrance, &pendingEntrance,
                 &sceneId, error)) {
    return false;
  }
  if (mStateTrace.empty() ||
      mStateTrace.back().CurrentEntrance != currentEntrance ||
      mStateTrace.back().PendingEntrance != pendingEntrance ||
      mStateTrace.back().SceneId != sceneId) {
    mStateTrace.push_back(
        {guestFrame, currentEntrance, pendingEntrance, sceneId});
  }
  if (!mStats.RequestedEntranceObserved &&
      mStats.TransitionRequestAccepted &&
      guestFrame >= mStats.RequestGuestFrame &&
      currentEntrance == mRecipe.EntranceIndex) {
    mStats.RequestedEntranceObserved = true;
    mStats.RequestedEntranceFirstObservedGuestFrame = guestFrame;
  }
  if (!mStats.TargetSceneObservedAfterEntrance &&
      mStats.RequestedEntranceObserved &&
      guestFrame >= mStats.RequestedEntranceFirstObservedGuestFrame &&
      sceneId == mRecipe.SceneId) {
    mStats.TargetSceneObservedAfterEntrance = true;
    mStats.TargetSceneAfterEntranceGuestFrame = guestFrame;
  }
  if (mReachedTarget) {
    const uint64_t readyElapsed = guestFrame - mStats.ReadyGuestFrame;
    mStats.SettleFramesObserved = static_cast<uint32_t>(
        std::min<uint64_t>(readyElapsed, mRecipe.SettleFrames));
    const uint64_t captureElapsed = readyElapsed > mRecipe.SettleFrames
                                        ? readyElapsed - mRecipe.SettleFrames
                                        : 0U;
    mStats.CaptureFramesObserved = static_cast<uint32_t>(
        std::min<uint64_t>(captureElapsed, mRecipe.CaptureFrames));
    if (sceneId != mRecipe.SceneId ||
        currentEntrance != mStats.ResolvedEntrance) {
      return Fail("native scenario changed while collecting coverage", error);
    }
    if (static_cast<int16_t>(pendingEntrance) >= 0) {
      if (readyElapsed < mRecipe.SettleFrames) {
        return Fail("native scenario departed before its settle budget", error);
      }
      mStats.NativeDepartureEntrance = pendingEntrance;
      mStats.CompleteGuestFrame = guestFrame;
      mStats.CompletionReason = "native_transition_after_settle";
      mStats.Status = NativeScenarioBootstrapStatus::Complete;
      std::cout << "oot3d_native_game: scenario " << mRecipe.Id
                << " completed when native gameplay requested entrance "
                << pendingEntrance << " after " << readyElapsed
                << " target frames\n";
      return true;
    }
    if (readyElapsed < mRecipe.SettleFrames) {
      mStats.Status = NativeScenarioBootstrapStatus::Settling;
    } else if (captureElapsed < mRecipe.CaptureFrames) {
      mStats.Status = NativeScenarioBootstrapStatus::Capturing;
    } else {
      mStats.CompleteGuestFrame = guestFrame;
      mStats.CompletionReason = "full_capture_budget";
      mStats.Status = NativeScenarioBootstrapStatus::Complete;
      std::cout << "oot3d_native_game: scenario " << mRecipe.Id
                << " completed structural capture at guest frame " << guestFrame
                << '\n';
    }
    return true;
  }
  const bool exactTarget = currentEntrance == mRecipe.EntranceIndex &&
                           sceneId == mRecipe.SceneId;
  const bool causallyResolvedTarget =
      mStats.TransitionRequestAccepted &&
      mStats.RequestedEntranceObserved &&
      mStats.TargetSceneObservedAfterEntrance && sceneId == mRecipe.SceneId;
  if ((exactTarget || causallyResolvedTarget) &&
      static_cast<int16_t>(pendingEntrance) < 0) {
    mStats.AlreadyAtTarget = mStats.TransitionHelperCalls == 0U;
    mStats.ResolvedEntrance = currentEntrance;
    mStats.TargetIdentityValidated = true;
    mStats.EntranceNormalizedByNativeLoader = !exactTarget;
    mStats.IdentityValidation = exactTarget
                                    ? "exact_entrance_and_scene"
                                    : kAcceptedTransitionIdentity;
    mStats.ReadyGuestFrame = guestFrame;
    mStats.Status = NativeScenarioBootstrapStatus::Settling;
    mReachedTarget = true;
    std::cout << "oot3d_native_game: scenario " << mRecipe.Id
              << " reached native entrance " << mRecipe.EntranceIndex
              << " as resolved entrance " << currentEntrance
              << " at guest frame " << guestFrame << '\n';
    return true;
  }
  if (mStats.Status == NativeScenarioBootstrapStatus::Requested) {
    return true;
  }
  if (static_cast<int16_t>(pendingEntrance) >= 0) {
    ++mStats.TransitionSlotWaitFrames;
    mStats.Status = NativeScenarioBootstrapStatus::WaitingForTransitionSlot;
    return true;
  }

  const std::array<uint32_t, 3> arguments{
      playStateAddress, static_cast<uint32_t>(mRecipe.EntranceIndex),
      static_cast<uint32_t>(mRuntime.TransitionTrigger)};
  uint32_t accepted = 0U;
  std::string invocationError;
  ++mStats.TransitionHelperCalls;
  if (!process.InvokeFunctionWithResult(
          mRuntime.RequestTransitionFunction, arguments,
          mRuntime.DirectCallReturnAddress, &accepted, &invocationError)) {
    return Fail("native transition helper failed: " + invocationError, error);
  }
  if (accepted == 0U) {
    ++mStats.TransitionHelperRejections;
    mStats.Status = NativeScenarioBootstrapStatus::WaitingForTransitionSlot;
    return true;
  }
  mStats.TransitionRequestAccepted = true;
  const std::array<uint32_t, 1> prepareArguments{playStateAddress};
  ++mStats.TransitionEffectPreparationCalls;
  if (!process.InvokeFunction(
          mRuntime.PrepareTransitionEffectFunction, prepareArguments,
          mRuntime.DirectCallReturnAddress, &invocationError)) {
    return Fail("native transition-effect preparation failed: " +
                    invocationError,
                error);
  }
  if (!process.Memory().Read8(playStateAddress +
                                  mRuntime.TransitionEffectOffset,
                              &mStats.TransitionEffect)) {
    return Fail("cannot read the prepared native transition effect", error);
  }
  mStats.RequestGuestFrame = guestFrame;
  mStats.Status = NativeScenarioBootstrapStatus::Requested;
  std::cout << "oot3d_native_game: scenario " << mRecipe.Id
            << " requested native entrance " << mRecipe.EntranceIndex
            << " from PlayState 0x" << std::hex << playStateAddress << std::dec
            << " effect " << static_cast<uint32_t>(mStats.TransitionEffect)
            << '\n';
  return true;
}

bool NativeScenarioBootstrap::Finished() const noexcept {
  return mStats.Status == NativeScenarioBootstrapStatus::Complete ||
         mStats.Status == NativeScenarioBootstrapStatus::Failed;
}

bool NativeScenarioBootstrap::Ready() const noexcept {
  return mReachedTarget &&
         mStats.Status != NativeScenarioBootstrapStatus::Failed;
}

bool NativeScenarioBootstrap::Complete() const noexcept {
  return mStats.Status == NativeScenarioBootstrapStatus::Complete;
}

const NativeScenarioRecipe &NativeScenarioBootstrap::Recipe() const noexcept {
  return mRecipe;
}

const NativeScenarioBootstrapStats &
NativeScenarioBootstrap::Stats() const noexcept {
  return mStats;
}

nlohmann::json NativeScenarioBootstrap::ToJson() const {
  nlohmann::json stateTrace = nlohmann::json::array();
  for (const auto &observation : mStateTrace) {
    stateTrace.push_back({
        {"guest_frame", observation.GuestFrame},
        {"current_entrance", observation.CurrentEntrance},
        {"pending_entrance", observation.PendingEntrance},
        {"scene_id", observation.SceneId},
    });
  }
  return {
      {"catalog_format", kCatalogFormat},
      {"id", mRecipe.Id},
      {"status", NativeScenarioBootstrapStatusName(mStats.Status)},
      {"ready", Ready()},
      {"complete", Complete()},
      {"entrance_index", mRecipe.EntranceIndex},
      {"scene_id", mRecipe.SceneId},
      {"local_entrance_index", mRecipe.LocalEntranceIndex},
      {"field", mRecipe.Field},
      {"scene_path", mRecipe.ScenePath},
      {"semantic_scene", mRecipe.SemanticScene},
      {"validation_status", mRecipe.ValidationStatus},
      {"first_observed_guest_frame", mStats.FirstObservedGuestFrame},
      {"request_guest_frame", mStats.RequestGuestFrame},
      {"ready_guest_frame", mStats.ReadyGuestFrame},
      {"complete_guest_frame", mStats.CompleteGuestFrame},
      {"play_state_address", mStats.PlayStateAddress},
      {"current_entrance", mStats.CurrentEntrance},
      {"pending_entrance", mStats.PendingEntrance},
      {"observed_scene_id", mStats.ObservedSceneId},
      {"transition_effect", mStats.TransitionEffect},
      {"settle_frames_required", mRecipe.SettleFrames},
      {"settle_frames_observed", mStats.SettleFramesObserved},
      {"capture_frames_required", mRecipe.CaptureFrames},
      {"capture_frames_observed", mStats.CaptureFramesObserved},
      {"play_state_wait_frames", mStats.PlayStateWaitFrames},
      {"transition_slot_wait_frames", mStats.TransitionSlotWaitFrames},
      {"transition_helper_calls", mStats.TransitionHelperCalls},
      {"transition_effect_preparation_calls",
       mStats.TransitionEffectPreparationCalls},
      {"transition_helper_rejections", mStats.TransitionHelperRejections},
      {"transition_request_accepted", mStats.TransitionRequestAccepted},
      {"requested_entrance_observed_after_request",
       mStats.RequestedEntranceObserved},
      {"requested_entrance_first_observed_guest_frame",
       mStats.RequestedEntranceFirstObservedGuestFrame},
      {"target_scene_observed_after_entrance",
       mStats.TargetSceneObservedAfterEntrance},
      {"target_scene_after_entrance_guest_frame",
       mStats.TargetSceneAfterEntranceGuestFrame},
      {"resolved_entrance", mStats.ResolvedEntrance},
      {"target_identity_validated", mStats.TargetIdentityValidated},
      {"entrance_normalized_by_native_loader",
       mStats.EntranceNormalizedByNativeLoader},
      {"identity_validation", mStats.IdentityValidation},
      {"completion_reason", mStats.CompletionReason},
      {"native_departure_entrance", mStats.NativeDepartureEntrance},
      {"already_at_target", mStats.AlreadyAtTarget},
      {"error", mStats.Error},
      {"state_trace", std::move(stateTrace)},
      {"coverage",
       {{"setups", mRecipe.Coverage.SetupCount},
        {"gameplay_setups", mRecipe.Coverage.GameplaySetupCount},
        {"cutscene_setups", mRecipe.Coverage.CutsceneSetupCount},
        {"spawns", mRecipe.Coverage.SpawnCount},
        {"transition_actors", mRecipe.Coverage.TransitionActorCount},
        {"light_settings", mRecipe.Coverage.LightSettingCount},
        {"cutscene_references", mRecipe.Coverage.CutsceneReferenceCount},
        {"actor_placements", mRecipe.Coverage.ActorPlacementCount},
        {"decoded_light_records", mRecipe.Coverage.DecodedLightRecordCount},
        {"cutscene_sources", mRecipe.Coverage.CutsceneSourceCount},
        {"unresolved_commands", mRecipe.Coverage.UnresolvedCommandCount}}},
  };
}

} // namespace Oot3dNativeGame
