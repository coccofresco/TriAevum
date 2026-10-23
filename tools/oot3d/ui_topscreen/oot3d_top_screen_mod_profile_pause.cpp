// TopScreen mod profile: pause draw inputs, routes and redraws, renderer visibility and fades,
// the pause controller and alternate renderers, child suppression.
#include "oot3d_top_screen_mod_profile_internal.h"

namespace Oot3dNativeGame {

namespace {
constexpr std::array<std::uint32_t, 6> kTopScreenPauseChildStateAddresses{
    0x00504484U, 0x0050672CU, 0x00506CE8U,
    0x0050A530U, 0x005043F4U, 0x0050AF68U};
constexpr std::uint8_t kTopScreenPauseTailSuppressionMask = 0x2FU;
constexpr std::uint8_t kTopScreenPauseAuxiliarySuppressionMask = 0x3FU;
constexpr std::uint8_t kTopScreenPausePageRedrawSuppressionMask = 0x20U;
constexpr std::array<std::uint32_t, 4> kTopScreenAlternateRendererPointers{
    0x005C4CD4U, 0x005C4CD8U, 0x005C4CDCU, 0x005C4CE0U};

bool ReadRequired8(NativeA32Memory &memory, std::uint32_t address,
                   std::uint8_t *value, std::string *error) {
  if (memory.Read8(address, value))
    return true;
  if (error != nullptr)
    *error = "cannot read TopScreen pause byte state";
  return false;
}
} // namespace

bool ReadTopScreenPauseDrawInputs(NativeA32Memory &memory,
                                  TopScreenPauseDrawInputs *inputs,
                                  std::string *error) {
  if (inputs == nullptr) {
    if (error != nullptr)
      *error = "missing TopScreen pause input output";
    return false;
  }
  constexpr std::uint32_t kSceneOwnerRoot = 0x005043E0U;
  constexpr std::uint32_t kRuntimeMode = 0x00588E3CU;
  constexpr std::uint32_t kEntranceIndex = 0x00587960U;
  constexpr std::uint32_t kNativeTransitionState = 0x005C4344U;
  constexpr std::uint32_t kAlternatePathReady = 0x005C3F61U;
  constexpr std::uint32_t kAlternatePathVisible = 0x005C3F62U;
  constexpr std::uint32_t kSuppressionOverride = 0x005C3F65U;

  TopScreenPauseDrawInputs result;
  std::uint32_t scene = 0U;
  std::uint32_t transitionState = 0U;
  std::uint8_t alternatePathReady = 0U;
  std::uint8_t alternatePathVisible = 0U;
  std::uint8_t suppressionOverride = 0U;
  if (!memory.Read32(kSceneOwnerRoot, &scene) ||
      !memory.Read32(kRuntimeMode, &result.RuntimeMode) ||
      !memory.Read32(kEntranceIndex, &result.EntranceIndex) ||
      !memory.Read32(kNativeTransitionState, &transitionState) ||
      !ReadRequired8(memory, kAlternatePathReady, &alternatePathReady, error) ||
      !ReadRequired8(memory, kAlternatePathVisible, &alternatePathVisible,
                     error) ||
      !ReadRequired8(memory, kSuppressionOverride, &suppressionOverride,
                     error)) {
    if (error != nullptr && error->empty())
      *error = "cannot read TopScreen pause routing state";
    return false;
  }
  result.AlternatePathReady = alternatePathReady != 0U;
  result.AlternatePathVisible = alternatePathVisible != 0U;
  result.SuppressionOverride = suppressionOverride != 0U;
  result.NativeTransitionActive =
      static_cast<std::int32_t>(transitionState) >= 0;
  result.HasScene = scene != 0U;
  if (result.HasScene) {
    if (!memory.Read8(scene + 0x100U, &result.SceneMode) ||
        !memory.Read8(scene + 0x101U, &result.SceneVariant) ||
        !memory.Read16(scene + 0x318CU, &result.SceneSequence)) {
      if (error != nullptr)
        *error = "cannot read TopScreen pause scene state";
      return false;
    }
  }
  for (std::size_t index = 0; index < result.ChildStates.size(); ++index) {
    if (!memory.Read32(kTopScreenPauseChildStateAddresses[index],
                       &result.ChildStates[index])) {
      if (error != nullptr)
        *error = "cannot read TopScreen pause page gates";
      return false;
    }
    if (index < 4U)
      result.AnyPageGateActive |= result.ChildStates[index] != 0U;
  }
  *inputs = result;
  return true;
}

TopScreenPauseClosePlan
ResolveTopScreenPauseStartClose(const TopScreenPauseDrawInputs &inputs,
                                bool startPressed) noexcept {
  if (!startPressed || !inputs.AnyPageGateActive)
    return {};

  // 0x005C9D6C checks Items before Gear, Dungeon Map and System Menu.
  if (inputs.ChildStates[1] != 0U)
    return {TopScreenPauseClosePage::Items, 0x002EB628U};
  if (inputs.ChildStates[0] != 0U)
    return {TopScreenPauseClosePage::Gear, 0x002F8AF4U};
  if (inputs.ChildStates[2] != 0U)
    return {TopScreenPauseClosePage::DungeonMap, 0x002F0618U};
  if (inputs.ChildStates[3] != 0U)
    return {TopScreenPauseClosePage::SystemMenu, 0x002E9920U};
  return {};
}

TopScreenPauseSystemOpenPlan ResolveTopScreenPauseBSystemOpen(
    const TopScreenPauseDrawInputs &inputs, bool bPressed,
    TopScreenPauseSystemOpenState &state) noexcept {
  const bool ordinaryPageActive = inputs.ChildStates[0] != 0U ||
                                  inputs.ChildStates[1] != 0U ||
                                  inputs.ChildStates[2] != 0U;
  if (bPressed && ordinaryPageActive)
    state.RemainingTicks = 5U;
  if (state.RemainingTicks == 0U)
    return {};

  TopScreenPauseSystemOpenPlan plan;
  std::uint32_t pageState = 0U;
  // 0x005C9F14 checks Items, Gear, then Dungeon Map.
  if (inputs.ChildStates[1] != 0U) {
    plan = {TopScreenPauseClosePage::Items, 0x002EB628U};
    pageState = inputs.ChildStates[1];
  } else if (inputs.ChildStates[0] != 0U) {
    plan = {TopScreenPauseClosePage::Gear, 0x002F8AF4U};
    pageState = inputs.ChildStates[0];
  } else if (inputs.ChildStates[2] != 0U) {
    plan = {TopScreenPauseClosePage::DungeonMap, 0x002F0618U};
    pageState = inputs.ChildStates[2];
  } else {
    --state.RemainingTicks;
    return {};
  }

  --state.RemainingTicks;
  bool ready = pageState == 3U;
  switch (plan.Page) {
  case TopScreenPauseClosePage::Items:
    ready |= pageState == 0xEU;
    break;
  case TopScreenPauseClosePage::Gear:
    ready |= pageState == 0xAU;
    break;
  case TopScreenPauseClosePage::DungeonMap:
    ready |= (pageState & ~2U) == 8U;
    break;
  case TopScreenPauseClosePage::None:
  case TopScreenPauseClosePage::SystemMenu:
    break;
  }
  if (!ready)
    return {};

  state.RemainingTicks = 0U;
  return plan;
}

bool ReadTopScreenPausePageRedrawInputs(NativeA32Memory &memory,
                                        bool runtimeSceneLatch,
                                        TopScreenPausePageRedrawInputs *inputs,
                                        std::string *error) {
  if (inputs == nullptr) {
    if (error != nullptr)
      *error = "missing TopScreen pause page-redraw output";
    return false;
  }
  constexpr std::uint32_t kNativeHealthGate = 0x0058799CU;
  constexpr std::uint32_t kWorldMapControllerState = 0x005093F8U;
  TopScreenPausePageRedrawInputs result;
  std::uint16_t healthGate = 0U;
  if (!ReadTopScreenPauseDrawInputs(memory, &result.Pause, error) ||
      !memory.Read16(kNativeHealthGate, &healthGate) ||
      !memory.Read32(kWorldMapControllerState,
                     &result.WorldMapControllerState)) {
    if (error != nullptr && error->empty())
      *error = "cannot read TopScreen pause page-redraw state";
    return false;
  }
  result.NativeHealthGate = static_cast<std::int16_t>(healthGate);
  result.RuntimeSceneLatch = runtimeSceneLatch;
  *inputs = result;
  return true;
}

bool ResolveTopScreenPausePageRedraw(
    const TopScreenPausePageRedrawInputs &inputs,
    TopScreenPausePageRedrawState *state) noexcept {
  if (state == nullptr)
    return false;

  const auto &pause = inputs.Pause;
  if (IsTopScreenTitleDemoRuntime(pause) || inputs.RuntimeSceneLatch)
    return false;

  const std::uint16_t sceneSequence =
      pause.HasScene && pause.SceneMode == 3U ? pause.SceneSequence : 0U;
  if (sceneSequence != 0U || inputs.NativeHealthGate <= 0) {
    state->DelayCalls = 90U;
    return false;
  }
  if (state->DelayCalls != 0U && --state->DelayCalls != 0U)
    return false;

  // FUN_005C8EEC blocks this path while the native process entry or alternate
  // viewport is active, unless byte +0x65 explicitly bypasses that gate.
  const bool transitionBlocked =
      !pause.SuppressionOverride && (pause.NativeTransitionActive ||
                                     UseTopScreenAlternatePauseViewport(pause));
  return pause.AnyPageGateActive && !transitionBlocked &&
         inputs.WorldMapControllerState == 0U;
}

bool UseTopScreenAlternatePauseViewport(
    const TopScreenPauseDrawInputs &inputs) noexcept {
  return inputs.HasScene && inputs.RuntimeMode == 4U &&
         inputs.AlternatePathReady && inputs.AlternatePathVisible;
}

bool IsTopScreenGameOverPresentation(
    const TopScreenPauseDrawInputs &inputs) noexcept {
  return !inputs.NativeTransitionActive && inputs.HasScene &&
         inputs.SceneMode == 3U && inputs.SceneVariant == 2U &&
         inputs.SceneSequence >= 3U && inputs.SceneSequence <= 6U;
}

bool IsTopScreenTitleDemoRuntime(
    const TopScreenPauseDrawInputs &inputs) noexcept {
  // TopScreen 1.2 helper 0x005C8FA0 adds the SaveContext entrance-index gate
  // that was absent in 1.1. This prevents ordinary mode-3 scenes from being
  // routed as the title-screen demo.
  return inputs.HasScene && inputs.SceneMode == 3U &&
         inputs.RuntimeMode >= 1U && inputs.RuntimeMode <= 2U &&
         inputs.EntranceIndex == 0xFFF3U;
}

bool UseTopScreenSceneFiveViewport(
    const TopScreenPauseDrawInputs &inputs) noexcept {
  return inputs.HasScene && inputs.SceneMode == 5U;
}

bool ResolveTopScreenPauseRouteActive(
    const TopScreenPauseDrawInputs &inputs,
    TopScreenPauseRouteState *state) noexcept {
  if (state == nullptr)
    return false;

  const bool startsTransition =
      inputs.RuntimeMode == 0U &&
      (state->PreviousRuntimeMode == 1U || state->PreviousRuntimeMode == 2U ||
       state->PreviousRuntimeMode == 4U);
  if (startsTransition) {
    state->TransitionPhase = 1U;
    state->RemainingCalls = 900U;
  }
  state->PreviousRuntimeMode = inputs.RuntimeMode;

  if (state->TransitionPhase != 0U) {
    if (inputs.HasScene) {
      if (state->TransitionPhase == 1U && inputs.SceneVariant != 2U) {
        state->TransitionPhase = 2U;
      } else if (state->TransitionPhase == 2U && inputs.SceneVariant == 2U) {
        state->TransitionPhase = 0U;
      }
    }
    if (state->TransitionPhase != 0U) {
      if (state->RemainingCalls != 0U)
        --state->RemainingCalls;
      if (state->RemainingCalls != 0U)
        return true;
      state->TransitionPhase = 0U;
    }
  }

  return UseTopScreenSceneFiveViewport(inputs) ||
         UseTopScreenAlternatePauseViewport(inputs);
}

std::uint32_t ResolveTopScreenPauseRoutedCommand(std::uint32_t nativeCommand,
                                                 bool routeActive) noexcept {
  if (!routeActive)
    return nativeCommand;
  if (nativeCommand == 0x400U)
    return 0x401U;
  if (nativeCommand == 0x401U)
    return 0x400U;
  return nativeCommand;
}

bool ResolveTopScreenTouchCoordinateSuppression(
    const TopScreenPauseDrawInputs &inputs, bool routeActive,
    bool pauseCameraControlActive, bool secondaryLayerActive,
    TopScreenTouchCoordinateRouteState *state) noexcept {
  if (state == nullptr)
    return false;

  // Exact source equivalent of payload helper 0x005C8E0C. Mode 3 preserves
  // the previous value; every other inactive scene clears it.
  if (inputs.NativeTransitionActive) {
    state->RuntimeSceneLatch = true;
  } else if (!inputs.HasScene || inputs.SceneMode != 3U) {
    state->RuntimeSceneLatch = false;
  }

  const bool sceneRuntimeRoute = IsTopScreenTitleDemoRuntime(inputs);
  const bool sequenceRoute =
      !state->RuntimeSceneLatch && !inputs.NativeTransitionActive &&
      inputs.HasScene && inputs.SceneMode == 3U && inputs.SceneVariant == 2U &&
      inputs.SceneSequence >= 3U && inputs.SceneSequence <= 6U;
  return routeActive || sceneRuntimeRoute || sequenceRoute ||
         (pauseCameraControlActive && secondaryLayerActive);
}

bool ApplyTopScreenTouchCoordinateSuppression(NativeA32Memory &memory,
                                              std::string *error) {
  constexpr std::uint32_t kTouchState = 0x0050BB00U;
  constexpr std::array<std::pair<std::uint32_t, std::uint16_t>, 6> kWrites{
      {{0x3AU, 0x7FFFU},
       {0x3CU, 0x7FFFU},
       {0x3EU, 0U},
       {0x40U, 0U},
       {0x42U, 0U},
       {0x48U, 0U}}};
  for (const auto &[offset, value] : kWrites) {
    (void)value;
    if (!memory.IsWritable(kTouchState + offset, sizeof(std::uint16_t))) {
      if (error != nullptr)
        *error = "cannot preflight TopScreen touch-coordinate state";
      return false;
    }
  }
  for (const auto &[offset, value] : kWrites) {
    if (!memory.WriteHost<std::uint16_t>(kTouchState + offset, value)) {
      if (error != nullptr)
        *error = "cannot suppress TopScreen touch-coordinate state";
      return false;
    }
  }
  return true;
}

namespace {
bool ReconcileAndHideTopScreenRenderer(NativeA32Memory &memory,
                                       std::uint32_t controller,
                                       bool reconcileTables,
                                       bool *rendererConflict,
                                       std::string *error) {
  if (controller == 0U || rendererConflict == nullptr) {
    SetError(error, "missing TopScreen renderer controller state");
    return false;
  }
  bool conflict = false;
  std::array<std::pair<std::uint32_t, std::uint32_t>, 256> ownerWrites{};
  std::size_t ownerWriteCount = 0U;
  if (reconcileTables) {
    for (std::uint32_t offset = 0xAF8U; offset <= 0xEF4U; offset += 4U) {
      std::uint32_t renderer = 0U;
      if (!memory.Read32(controller + offset, &renderer)) {
        SetError(error, "cannot read TopScreen renderer table");
        return false;
      }
      if (renderer == 0U)
        continue;
      std::uint8_t visible = 0U;
      if (!memory.Read8(renderer + 0x6CU, &visible)) {
        SetError(error, "cannot read TopScreen renderer visibility");
        return false;
      }
      if (visible != 1U)
        continue;
      const std::uint32_t ownerSlot = controller + offset - 0x400U;
      std::uint32_t owner = 0U;
      if (!memory.Read32(ownerSlot, &owner) ||
          !memory.IsWritable(ownerSlot, sizeof(std::uint32_t))) {
        SetError(error, "cannot preflight TopScreen renderer owner table");
        return false;
      }
      if (owner == 0U || owner == renderer) {
        ownerWrites[ownerWriteCount++] = {ownerSlot, renderer};
      } else {
        conflict = true;
      }
    }
  }
  std::uint32_t renderer = 0U;
  if (!memory.Read32(controller + 0x6FCU, &renderer)) {
    SetError(error, "cannot read TopScreen terminal renderer");
    return false;
  }
  if (renderer != 0U && !memory.IsWritable(renderer + 0x6CU, 1U)) {
    SetError(error, "cannot preflight TopScreen terminal renderer");
    return false;
  }
  for (std::size_t index = 0U; index < ownerWriteCount; ++index) {
    const auto [ownerSlot, owner] = ownerWrites[index];
    if (!memory.WriteHost<std::uint32_t>(ownerSlot, owner)) {
      SetError(error, "cannot reconcile TopScreen renderer owner");
      return false;
    }
  }
  if (renderer != 0U && !memory.WriteHost<std::uint8_t>(renderer + 0x6CU, 0U)) {
    SetError(error, "cannot hide TopScreen terminal renderer");
    return false;
  }
  *rendererConflict = conflict;
  return true;
}

bool ApplyTopScreenRendererFade(NativeA32Memory &memory, std::int32_t fadeStep,
                                std::string *error) {
  constexpr std::uint32_t kRendererRoot = 0x005C588CU;
  std::uint32_t owner = 0U;
  std::uint32_t renderer = 0U;
  if (!memory.Read32(kRendererRoot, &owner) || owner == 0U ||
      !memory.Read32(owner + 0x6FCU, &renderer) || renderer == 0U) {
    return true;
  }
  const float alpha = 1.0F - static_cast<float>(fadeStep) / 24.0F;
  const std::uint8_t alphaByte =
      static_cast<std::uint8_t>(static_cast<std::uint32_t>(alpha * 255.0F));
  constexpr std::array<std::uint32_t, 5> kAlphaByteOffsets{0xA0U, 0xA1U, 0xA2U,
                                                           0xA3U, 0xAFU};
  for (const auto offset : kAlphaByteOffsets) {
    if (!memory.IsWritable(renderer + offset, 1U)) {
      SetError(error, "cannot preflight TopScreen renderer byte fade");
      return false;
    }
  }
  std::array<std::uint32_t, 2> colorBlocks{};
  if (!memory.Read32(renderer + 0x64U, &colorBlocks[0]) ||
      !memory.Read32(renderer + 0x68U, &colorBlocks[1])) {
    SetError(error, "cannot read TopScreen renderer color blocks");
    return false;
  }
  for (const auto block : colorBlocks) {
    if (block != 0U && !memory.IsWritable(block + 0xF0U, 16U)) {
      SetError(error, "cannot preflight TopScreen renderer float fade");
      return false;
    }
  }
  for (const auto offset : kAlphaByteOffsets) {
    if (!memory.WriteHost<std::uint8_t>(renderer + offset, alphaByte)) {
      SetError(error, "cannot write TopScreen renderer byte fade");
      return false;
    }
  }
  const auto alphaBits = std::bit_cast<std::uint32_t>(alpha);
  for (const auto block : colorBlocks) {
    if (block == 0U)
      continue;
    for (std::uint32_t component = 0U; component < 4U; ++component) {
      if (!memory.WriteHost<std::uint32_t>(block + 0xF0U + component * 4U,
                                           alphaBits)) {
        SetError(error, "cannot write TopScreen renderer float fade");
        return false;
      }
    }
  }
  return true;
}
} // namespace

bool ResolveTopScreenRendererVisibilityRoute(
    NativeA32Memory &memory, const TopScreenRendererVisibilityInputs &inputs,
    TopScreenRendererVisibilityRouteState *state,
    TopScreenRendererVisibilityAction *action, std::string *error) {
  if (state == nullptr || action == nullptr) {
    SetError(error, "missing TopScreen renderer visibility output");
    return false;
  }
  *action = TopScreenRendererVisibilityAction::NativeDraw;
  if (inputs.ControllerAddress == 0U || !inputs.HasCallScene)
    return true;

  if (inputs.CallSceneMode == 7U) {
    if (inputs.CallSceneSubmode != 4U) {
      state->RendererConflict = false;
      state->FadeStep = 0;
      state->DelayCalls = 0;
    } else if (!ReconcileAndHideTopScreenRenderer(
                   memory, inputs.ControllerAddress, true,
                   &state->RendererConflict, error)) {
      return false;
    }
    *action = TopScreenRendererVisibilityAction::SuppressNativeDraw;
    return true;
  }
  if (inputs.CallSceneMode != 3U)
    return true;

  if (inputs.GlobalSceneRuntimeRoute || inputs.RuntimeSceneLatch ||
      inputs.NativeRuntimeActive ||
      (inputs.CallSceneSequence == 0U && inputs.NativeFadeGate > 0)) {
    state->FadeStep = 0;
    state->DelayCalls = 0;
    return true;
  }
  state->RendererConflict = false;
  if (inputs.CallSceneSequence <= 3U) {
    state->FadeStep = 0;
    state->DelayCalls = 0;
    return true;
  }
  if (state->FadeStep <= 23) {
    if (state->DelayCalls <= 59) {
      ++state->DelayCalls;
      return true;
    }
    ++state->FadeStep;
    return ApplyTopScreenRendererFade(memory, state->FadeStep, error);
  }
  const bool reconcile =
      inputs.CallSceneSequence >= 4U && inputs.CallSceneSequence <= 6U;
  if (!ReconcileAndHideTopScreenRenderer(memory, inputs.ControllerAddress,
                                         reconcile, &state->RendererConflict,
                                         error)) {
    return false;
  }
  *action = TopScreenRendererVisibilityAction::SuppressNativeDraw;
  return true;
}

bool PrepareTopScreenGameOverDraw(
    NativeA32Memory &memory, std::uint32_t controller,
    const TopScreenPauseDrawInputs &inputs, bool runtimeSceneLatch,
    TopScreenRendererVisibilityRouteState *state, std::string *error) {
  if (state == nullptr) {
    SetError(error, "missing TopScreen GameOver presentation state");
    return false;
  }
  if (!IsTopScreenGameOverPresentation(inputs) || runtimeSceneLatch ||
      IsTopScreenTitleDemoRuntime(inputs)) {
    *state = {};
    return true;
  }
  // Savestates retain the native migrated tables, not host fade counters.
  // Resume their already-visible choice screen immediately on the first draw.
  if (controller != 0U && inputs.SceneSequence > 3U &&
      state->DelayCalls == 0 && state->FadeStep == 0) {
    std::uint32_t terminal = 0U;
    std::uint8_t terminalVisible = 1U;
    if (!memory.Read32(controller + 0x6FCU, &terminal) ||
        (terminal != 0U && !memory.Read8(terminal + 0x6CU, &terminalVisible))) {
      SetError(error, "cannot read GameOver resume state");
      return false;
    }
    if (terminal != 0U && terminalVisible == 0U) {
      for (std::uint32_t offset = 0xAF8U; offset <= 0xEF4U; offset += 4U) {
        std::uint32_t source = 0U, destination = 0U;
        std::uint8_t visible = 0U;
        if (!memory.Read32(controller + offset, &source) ||
            !memory.Read32(controller + offset - 0x400U, &destination) ||
            (source != 0U && !memory.Read8(source + 0x6CU, &visible))) {
          SetError(error, "cannot read GameOver resumed choices");
          return false;
        }
        if (source != 0U && source == destination && visible == 1U) {
          state->DelayCalls = 60;
          state->FadeStep = 24;
          break;
        }
      }
    }
  }
  TopScreenRendererVisibilityInputs visibility;
  visibility.ControllerAddress = controller;
  visibility.HasCallScene = true;
  visibility.CallSceneMode = inputs.SceneMode;
  visibility.CallSceneSequence = inputs.SceneSequence;
  TopScreenRendererVisibilityAction action;
  // Reuse the mod's native renderer-table migration and fade. The product
  // observes the upper native draw itself, not the mod's suppressed callsite.
  return ResolveTopScreenRendererVisibilityRoute(memory, visibility, state,
                                                  &action, error);
}

bool ReadTopScreenPauseControllerInputs(NativeA32Memory &memory,
                                        TopScreenPauseControllerInputs *inputs,
                                        std::string *error) {
  if (inputs == nullptr) {
    SetError(error, "missing TopScreen pause-controller inputs");
    return false;
  }
  TopScreenPauseControllerInputs result;
  if (!ReadTopScreenPauseDrawInputs(memory, &result.Pause, error) ||
      !memory.Read32(0x0050BB50U, &result.RendererAddress)) {
    SetError(error, "cannot read TopScreen pause-controller state");
    return false;
  }
  if (result.RendererAddress != 0U &&
      !memory.Read8(result.RendererAddress + 4U, &result.RendererPhase)) {
    SetError(error, "cannot read TopScreen pause renderer phase");
    return false;
  }
  *inputs = result;
  return true;
}

namespace {
bool ApplyTopScreenPauseControllerFade(NativeA32Memory &memory,
                                       std::uint32_t renderer,
                                       std::int32_t fadeStep,
                                       std::string *error) {
  constexpr std::array<std::uint16_t, 14> kAlphaOffsets{
      0x0054U, 0x0090U, 0x00CCU, 0x0108U, 0x0234U, 0x0270U, 0x02ACU,
      0x02E8U, 0x0324U, 0x0360U, 0x039CU, 0x03D8U, 0x0414U, 0x0450U};
  constexpr std::array<std::uint32_t, 2> kColorBlockPointers{0x05C4U, 0x05C8U};
  if (renderer == 0U)
    return true;
  for (const auto offset : kAlphaOffsets) {
    if (!memory.IsWritable(renderer + offset, sizeof(std::uint32_t))) {
      SetError(error, "cannot preflight TopScreen controller fade field");
      return false;
    }
  }
  std::array<std::uint32_t, 2> colorBlocks{};
  for (std::size_t index = 0U; index < kColorBlockPointers.size(); ++index) {
    if (!memory.Read32(renderer + kColorBlockPointers[index],
                       &colorBlocks[index]) ||
        (colorBlocks[index] != 0U &&
         !memory.IsWritable(colorBlocks[index] + 0xFCU,
                            sizeof(std::uint32_t)))) {
      SetError(error, "cannot preflight TopScreen controller color fade");
      return false;
    }
  }
  const auto alphaBits =
      std::bit_cast<std::uint32_t>(static_cast<float>(fadeStep) * 0.05F);
  for (const auto offset : kAlphaOffsets) {
    if (!memory.WriteHost<std::uint32_t>(renderer + offset, alphaBits)) {
      SetError(error, "cannot write TopScreen controller fade field");
      return false;
    }
  }
  for (const auto colorBlock : colorBlocks) {
    if (colorBlock != 0U &&
        !memory.WriteHost<std::uint32_t>(colorBlock + 0xFCU, alphaBits)) {
      SetError(error, "cannot write TopScreen controller color fade");
      return false;
    }
  }
  return true;
}
} // namespace

bool ResolveTopScreenPauseController(
    NativeA32Memory &memory, const TopScreenPauseControllerInputs &inputs,
    bool routeActive, TopScreenPauseControllerState *state,
    TopScreenPauseControllerAction *action, std::string *error) {
  if (state == nullptr || action == nullptr) {
    SetError(error, "missing TopScreen pause-controller output");
    return false;
  }
  if (!routeActive) {
    state->RoutedRendererLatched = false;
    state->RoutedFadeStep = 0;
  } else if (!state->RoutedRendererLatched && inputs.RendererAddress != 0U &&
             inputs.RendererPhase == 8U) {
    // Producer 0x005CE57C owns this transition in the binary mod. Observing
    // the same native phase here avoids recreating its payload-global latch.
    state->RoutedRendererLatched = true;
  }

  if (inputs.Pause.HasScene && inputs.Pause.SceneMode == 7U) {
    *action = TopScreenPauseControllerAction::SuppressNativeDraw;
    return true;
  }
  if (!routeActive) {
    *action =
        UseTopScreenAlternatePauseViewport(inputs.Pause)
            ? TopScreenPauseControllerAction::NativeDrawThenAlternateOverlay
            : TopScreenPauseControllerAction::NativeDraw;
    return true;
  }
  if (inputs.RendererAddress == 0U) {
    *action = TopScreenPauseControllerAction::SuppressNativeDraw;
    return true;
  }
  if (state->RoutedRendererLatched) {
    if (state->RoutedFadeStep <= 19) {
      ++state->RoutedFadeStep;
      if (!ApplyTopScreenPauseControllerFade(memory, inputs.RendererAddress,
                                             state->RoutedFadeStep, error)) {
        return false;
      }
    }
    *action = TopScreenPauseControllerAction::SuppressNativeDraw;
    return true;
  }
  *action = inputs.RendererPhase >= 1U && inputs.RendererPhase <= 2U
                ? TopScreenPauseControllerAction::NativeDraw
                : TopScreenPauseControllerAction::SuppressNativeDraw;
  return true;
}

bool PrepareTopScreenPauseAlternateRenderers(
    NativeA32Memory &memory, TopScreenPauseControllerState *state,
    std::array<std::uint32_t, 2> *visibleRenderers, std::string *error) {
  if (state == nullptr || visibleRenderers == nullptr) {
    SetError(error, "missing TopScreen alternate-renderer output");
    return false;
  }
  constexpr std::array<std::uint32_t, 2> kRendererSlots{0x005C4CD8U,
                                                        0x005C4CE0U};
  constexpr std::array<float, 2> kXOffsets{45.0F, -25.0F};
  std::array<std::uint32_t, 2> renderers{};
  for (std::size_t index = 0U; index < renderers.size(); ++index) {
    if (!memory.Read32(kRendererSlots[index], &renderers[index])) {
      SetError(error, "cannot read TopScreen alternate renderer");
      return false;
    }
  }
  if (!state->AlternateRendererPositionsCaptured ||
      state->AlternateRendererAddresses != renderers) {
    std::array<float, 2> originalX{};
    std::array<float, 2> originalY{};
    for (std::size_t index = 0U; index < renderers.size(); ++index) {
      if (renderers[index] == 0U)
        continue;
      std::uint32_t xBits = 0U;
      std::uint32_t yBits = 0U;
      if (!memory.Read32(renderers[index] + 0x80U, &xBits) ||
          !memory.Read32(renderers[index] + 0x84U, &yBits)) {
        SetError(error, "cannot capture TopScreen alternate renderer position");
        return false;
      }
      originalX[index] = std::bit_cast<float>(xBits);
      originalY[index] = std::bit_cast<float>(yBits);
    }
    state->AlternateRendererAddresses = renderers;
    state->AlternateRendererOriginalX = originalX;
    state->AlternateRendererOriginalY = originalY;
    state->AlternateRendererPositionsCaptured = true;
  }
  for (const auto renderer : renderers) {
    if (renderer != 0U &&
        (!memory.IsWritable(renderer + 0x80U, sizeof(std::uint32_t)) ||
         !memory.IsWritable(renderer + 0x84U, sizeof(std::uint32_t)))) {
      SetError(error, "cannot preflight TopScreen alternate renderer position");
      return false;
    }
  }
  *visibleRenderers = {};
  for (std::size_t index = 0U; index < renderers.size(); ++index) {
    const auto renderer = renderers[index];
    if (renderer == 0U)
      continue;
    const auto xBits = std::bit_cast<std::uint32_t>(
        state->AlternateRendererOriginalX[index] + kXOffsets[index]);
    const auto yBits = std::bit_cast<std::uint32_t>(
        state->AlternateRendererOriginalY[index] - 6.0F);
    std::uint8_t visible = 0U;
    if (!memory.WriteHost<std::uint32_t>(renderer + 0x80U, xBits) ||
        !memory.WriteHost<std::uint32_t>(renderer + 0x84U, yBits) ||
        !memory.Read8(renderer + 0x6CU, &visible)) {
      SetError(error, "cannot prepare TopScreen alternate renderer");
      return false;
    }
    if (visible != 0U)
      (*visibleRenderers)[index] = renderer;
  }
  return true;
}

bool BeginTopScreenAlternateRendererSuppression(
    NativeA32Memory &memory, TopScreenAlternateRendererState *saved,
    std::string *error) {
  if (saved == nullptr) {
    if (error != nullptr)
      *error = "missing alternate renderer-state output";
    return false;
  }
  TopScreenAlternateRendererState snapshot;
  std::array<std::uint32_t, 4> renderers{};
  for (std::size_t index = 0; index < renderers.size(); ++index) {
    if (!memory.Read32(kTopScreenAlternateRendererPointers[index],
                       &renderers[index])) {
      if (error != nullptr)
        *error = "cannot read alternate renderer pointer";
      return false;
    }
    if (renderers[index] != 0U &&
        (!memory.Read8(renderers[index] + 0x6CU, &snapshot.Visibility[index]) ||
         !memory.IsWritable(renderers[index] + 0x6CU, 1U))) {
      if (error != nullptr)
        *error = "cannot preflight alternate renderer";
      return false;
    }
  }
  for (const auto renderer : renderers) {
    if (renderer != 0U && !memory.Write8(renderer + 0x6CU, 0U)) {
      if (error != nullptr)
        *error = "cannot suppress alternate renderer";
      return false;
    }
  }
  *saved = snapshot;
  return true;
}

bool EndTopScreenAlternateRendererSuppression(
    NativeA32Memory &memory, const TopScreenAlternateRendererState &saved,
    std::string *error) {
  std::array<std::uint32_t, 4> renderers{};
  for (std::size_t index = 0; index < renderers.size(); ++index) {
    if (!memory.Read32(kTopScreenAlternateRendererPointers[index],
                       &renderers[index]) ||
        (renderers[index] != 0U &&
         !memory.IsWritable(renderers[index] + 0x6CU, 1U))) {
      if (error != nullptr)
        *error = "cannot preflight alternate renderer restore";
      return false;
    }
  }
  for (std::size_t index = 0; index < renderers.size(); ++index) {
    if (renderers[index] != 0U &&
        !memory.Write8(renderers[index] + 0x6CU, saved.Visibility[index])) {
      if (error != nullptr)
        *error = "cannot restore alternate renderer";
      return false;
    }
  }
  return true;
}

TopScreenPauseDrawAction ResolveTopScreenPauseDrawAction(
    const TopScreenPauseDrawInputs &inputs,
    TopScreenPauseDrawRoutingState *state) noexcept {
  if (state == nullptr)
    return TopScreenPauseDrawAction::NativeDraw;
  if (inputs.NativeTransitionActive) {
    state->NativeTransitionLatched = true;
  } else if (!inputs.HasScene || inputs.SceneMode != 3U) {
    state->NativeTransitionLatched = false;
  }
  const bool alternateRuntime = IsTopScreenTitleDemoRuntime(inputs);
  const bool alternateSequence =
      !state->NativeTransitionLatched && inputs.HasScene &&
      inputs.SceneMode == 3U && inputs.SceneVariant == 2U &&
      inputs.SceneSequence >= 3U && inputs.SceneSequence <= 6U;
  const bool alternateReady = inputs.HasScene && inputs.RuntimeMode == 4U &&
                              inputs.AlternatePathReady &&
                              inputs.AlternatePathVisible;
  if (alternateRuntime || alternateSequence || alternateReady ||
      state->NativeTransitionLatched || !inputs.AnyPageGateActive ||
      state->NativeSceneRequested) {
    return TopScreenPauseDrawAction::NativeDraw;
  }
  const bool suppressionBlocked =
      state->SuppressionDelayCommands != 0U ||
      (!inputs.SuppressionOverride && inputs.NativeTransitionActive);
  return suppressionBlocked ? TopScreenPauseDrawAction::NativeDraw
                            : TopScreenPauseDrawAction::SuppressNativeChildren;
}

void ObserveTopScreenPauseTargetCommand(
    const TopScreenPauseDrawInputs &inputs, std::uint32_t nativeCommand,
    TopScreenPauseDrawRoutingState *state) noexcept {
  if (state == nullptr || nativeCommand != 0x400U)
    return;
  if (inputs.NativeTransitionActive) {
    if (inputs.SuppressionOverride) {
      state->SuppressionDelayArmed = true;
    } else if (state->SuppressionDelayArmed) {
      state->SuppressionDelayCommands = 180U;
    }
  } else {
    state->SuppressionDelayArmed = false;
  }
  if (state->SuppressionDelayCommands != 0U) {
    if (inputs.HasScene && inputs.RuntimeMode == 4U &&
        inputs.AlternatePathReady && inputs.AlternatePathVisible) {
      state->SuppressionDelayCommands = 0U;
    } else {
      --state->SuppressionDelayCommands;
    }
  }
}

bool ResolveTopScreenPauseIconBuild(TopScreenPauseIconBuild *build,
                                    const TopScreenUiConfig &config) noexcept {
  if (build == nullptr || build->Argument3 != 0xC5U)
    return false;

  const bool restoration =
      config.HudLayout == TopScreenHudLayout::Restoration;
  const float baseX = restoration ? 358.0F : 248.0F;
  const float baseY = restoration ? 68.0F : 24.0F;
  const float scale = config.HudScale;
  const float insetX = static_cast<float>(config.HudMarginX);
  const float insetY = static_cast<float>(config.HudMarginY);
  const auto size = static_cast<std::uint32_t>(std::lround(scale * 28.0F));
  const auto x = static_cast<std::uint32_t>(std::lround(
      400.0F + (baseX - 400.0F) * scale - insetX - scale * 14.0F));
  const auto y = static_cast<std::uint32_t>(
      std::lround(insetY + baseY * scale - scale * 14.0F));
  build->Argument2 = x;
  build->Argument3 = y;
  build->StackArgument0 = size;
  build->StackArgument1 = size;
  return true;
}

bool SuppressTopScreenTouchButtonDraw(std::uint32_t pauseState) noexcept {
  return pauseState >= 12U && pauseState <= 19U;
}

bool ReadTopScreenItemDescriptionInputs(NativeA32Memory &memory,
                                        TopScreenItemDescriptionInputs *inputs) {
  if (inputs == nullptr) return false;
  std::uint32_t row = 0U, gearCursor = 0U;
  if (!memory.Read32(0x0050672CU, &inputs->Items) ||
      !memory.Read32(0x00504484U, &inputs->Gear) ||
      !memory.Read32(0x0050A530U, &inputs->System) ||
      !memory.Read32(0x0050AF68U, &inputs->Controller) ||
      !memory.Read32(0x00506774U, &row) || !memory.Read32(0x0050448CU, &gearCursor))
    return false;
  inputs->ItemsCursorRow = static_cast<std::int32_t>(row);
  inputs->GearCursor = static_cast<std::int32_t>(gearCursor);
  return true;
}

bool UpdateTopScreenItemDescription(const TopScreenItemDescriptionInputs &inputs,
                                    bool selectPressed, bool &visible) noexcept {
  const bool browsing = inputs.Items == 2U || inputs.Gear == 2U;
  if (!browsing || inputs.System != 0U) visible = false;
  if (!selectPressed || !browsing) return false;
  visible = !visible;
  return true;
}

bool TopScreenItemDescriptionOwnsSelect(
    const TopScreenItemDescriptionInputs &inputs) noexcept {
  return inputs.Controller != 2U;
}

bool TopScreenItemDescriptionDrawEligible(const TopScreenItemDescriptionInputs &inputs,
                                          bool visible) noexcept {
  return visible && inputs.System == 0U && (inputs.Items == 2U || inputs.Gear == 2U);
}

std::uint32_t ResolveTopScreenItemDescriptionOffset(
    const TopScreenItemDescriptionInputs &inputs) noexcept {
  if (inputs.Items != 0U) return inputs.ItemsCursorRow >= 2 ? 300U : 0U;
  if (inputs.Gear != 0U)
    return inputs.GearCursor >= 0x13 && inputs.GearCursor <= 0x19 ? 300U : 0U;
  return 0U;
}

bool SuppressTopScreenTouchButtonPageDraw(
    std::uint32_t pauseState, const TopScreenPauseDrawInputs &inputs) noexcept {
  return SuppressTopScreenTouchButtonDraw(pauseState) || inputs.ChildStates[0] != 0U ||
         inputs.ChildStates[1] != 0U || inputs.ChildStates[2] != 0U ||
         inputs.ChildStates[3] != 0U;
}

bool BeginTopScreenPauseChildSuppression(NativeA32Memory &memory,
                                         TopScreenPauseChildSuppressionSet set,
                                         TopScreenPauseChildState *saved,
                                         std::string *error) {
  if (saved == nullptr) {
    if (error != nullptr)
      *error = "missing pause child-state output";
    return false;
  }
  TopScreenPauseChildState snapshot;
  switch (set) {
  case TopScreenPauseChildSuppressionSet::Tail:
    snapshot.SuppressedMask = kTopScreenPauseTailSuppressionMask;
    break;
  case TopScreenPauseChildSuppressionSet::AuxiliaryRedraw:
    snapshot.SuppressedMask = kTopScreenPauseAuxiliarySuppressionMask;
    break;
  case TopScreenPauseChildSuppressionSet::PageRedraw:
    snapshot.SuppressedMask = kTopScreenPausePageRedrawSuppressionMask;
    break;
  case TopScreenPauseChildSuppressionSet::SystemPageRedraw:
    snapshot.SuppressedMask = 0x30U;
    break;
  }
  for (std::size_t index = 0; index < kTopScreenPauseChildStateAddresses.size();
       ++index) {
    const auto address = kTopScreenPauseChildStateAddresses[index];
    if (!memory.Read32(address, &snapshot.Values[index]) ||
        ((snapshot.SuppressedMask & (1U << index)) != 0U &&
         !memory.IsWritable(address, sizeof(std::uint32_t)))) {
      if (error != nullptr)
        *error = "cannot preflight pause child state";
      return false;
    }
  }
  for (std::size_t index = 0; index < kTopScreenPauseChildStateAddresses.size();
       ++index) {
    if ((snapshot.SuppressedMask & (1U << index)) == 0U)
      continue;
    const auto value = set == TopScreenPauseChildSuppressionSet::SystemPageRedraw &&
                               index == 4U ? 1U : 0U;
    if (!memory.Write32(kTopScreenPauseChildStateAddresses[index], value)) {
      (void)EndTopScreenPauseChildSuppression(memory, snapshot);
      if (error != nullptr)
        *error = "cannot suppress pause child state";
      return false;
    }
  }
  *saved = snapshot;
  return true;
}

bool EndTopScreenPauseChildSuppression(NativeA32Memory &memory,
                                       const TopScreenPauseChildState &saved,
                                       std::string *error) {
  for (std::size_t index = 0; index < kTopScreenPauseChildStateAddresses.size();
       ++index) {
    if ((saved.SuppressedMask & (1U << index)) != 0U &&
        !memory.IsWritable(kTopScreenPauseChildStateAddresses[index],
                           sizeof(std::uint32_t))) {
      if (error != nullptr)
        *error = "cannot preflight pause child restore";
      return false;
    }
  }
  for (std::size_t index = 0; index < kTopScreenPauseChildStateAddresses.size();
       ++index) {
    if ((saved.SuppressedMask & (1U << index)) == 0U)
      continue;
    if (!memory.Write32(kTopScreenPauseChildStateAddresses[index],
                        saved.Values[index])) {
      if (error != nullptr)
        *error = "cannot restore pause child state";
      return false;
    }
  }
  return true;
}

} // namespace Oot3dNativeGame
