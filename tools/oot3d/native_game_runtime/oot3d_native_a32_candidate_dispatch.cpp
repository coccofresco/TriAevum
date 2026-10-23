// Per-block native candidates of the A32 window: TopScreen UI source-port blocks and product hooks.
#include <cstring>
#include "oot3d_native_a32_window_shared.h"

namespace Oot3dA32Window {

namespace {

bool ExecuteTopScreenUiSourcePortBlock(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeCandidateDispatchState &dispatch,
    oot3d::recomp::a32::ExecutionResult *result, uint32_t *blocksConsumed) {
  auto &memory = *dispatch.Memory;
  if (Oot3dNativeGame::ExecuteTopScreenDungeonMapIconCoordinate(
          pc, state, result, blocksConsumed))
    return true;
  if (pc == Oot3dNativeGame::kTopScreenInputUpdateBoundary) {
    // One guest continuation owns the boundary at a time; a requested pause
    // transaction runs before the song text of the same update. The product
    // build reaches TopScreen only through this boundary, so 2.1.1's
    // 0x0041EAE0 controller work is delivered here, in the same update.
    const bool textInFlight = dispatch.OcarinaText != nullptr && dispatch.OcarinaText->Pending();
    if (!textInFlight && dispatch.PauseTransaction != nullptr &&
        dispatch.PauseTransaction->Execute(state, result, blocksConsumed))
      return true;
    return dispatch.OcarinaText != nullptr &&
           dispatch.OcarinaText->Execute(state, result, blocksConsumed);
  }
  if (dispatch.Gameplay != nullptr && dispatch.Gameplay->Execute(pc, state, result, blocksConsumed))
    return true;
  if (pc == Oot3dNativeGame::kTopScreenSystemPageBoundary && dispatch.SystemPage != nullptr)
    return dispatch.SystemPage->Execute(state, result, blocksConsumed);
  // R6: the description is redrawn over the page this draw just placed on
  // the upper screen (2.1.1 0x005D3344..0x005D3410).
  if (pc == Oot3dNativeGame::kTopScreenItemDescriptionBoundary &&
      dispatch.ItemDescription != nullptr &&
      dispatch.ItemDescription->Execute(state, result, blocksConsumed))
    return true;
  // Shared wide-page upload (0x005DF428) and Items-only draw/cursor hooks
  // (0x005D19F8, 0x005D1CBC, 0x005D1BE8).
  if (dispatch.WideItems != nullptr) {
    if (pc == Oot3dNativeGame::kTopScreenPageProjectionUploadCall) {
      if (dispatch.SystemPage != nullptr && dispatch.SystemPage->Drawing()) {
        try {
          return dispatch.WideItems->ExecutePageProjection(state, true);
        } catch (...) {
          dispatch.SystemPage->Cancel();
          throw;
        }
      }
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      // The page goes to the upper screen as the target routing decides.
      const bool pageRedraw = dispatch.TopScreenPausePageRedrawActive != nullptr &&
                              *dispatch.TopScreenPausePageRedrawActive;
      return dispatch.WideItems->ExecutePageProjection(
          state,
          Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
              inputs, &dispatch.TopScreenPauseRoute) ||
              pageRedraw);
    }
    if (pc == Oot3dNativeGame::kTopScreenItemsPageDrawCall)
      return dispatch.WideItems->ExecuteItemsDraw(state, result, blocksConsumed);
    if (pc == Oot3dNativeGame::kTopScreenItemsUpdateBoundary)
      return dispatch.WideItems->ExecuteItemsUpdate(state, result, blocksConsumed);
    if (pc == Oot3dNativeGame::kTopScreenItemsShortcutCursorBoundary)
      return dispatch.WideItems->ExecuteShortcutCursor(state, result, blocksConsumed);
    if (pc == Oot3dNativeGame::kTopScreenGearModelDrawCall)
      return dispatch.WideItems->ExecuteGearModelDraw(state, result, blocksConsumed);
    if (pc == Oot3dNativeGame::kTopScreenViewModelTransformBlock)
      return dispatch.WideItems->ExecuteViewModelTransform(state, result, blocksConsumed);
    if (pc == Oot3dNativeGame::kTopScreenItemsCursorDrawCallA ||
        pc == Oot3dNativeGame::kTopScreenItemsCursorDrawCallB)
      return dispatch.WideItems->ExecuteCursorDraw(pc, state, result, blocksConsumed);
  }
  uint32_t nextPc = 0U;
  auto branchFromCallsite = [&](uint32_t target, uint32_t returnAddress) {
    state.r[14] = returnAddress;
    state.r[15] = target;
    *result = {oot3d::recomp::a32::ExitKind::Branch, target,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  };
  if (pc == kTopScreenTitleLogoFadeHoldPrefix) {
    Oot3dNativeGame::TopScreenTitleLogoFadeHoldResult hold;
    if (!Oot3dNativeGame::ApplyTopScreenTitleLogoFadeHold(
            memory, state.r[4], state.r[5], state.vfp[16], &hold)) {
      return false;
    }
    ++dispatch.TopScreenTitleLogoFadeHoldCalls;
    state.r[0] = hold.AnimationOwner;
    state.r[1] = hold.Argument1;
    state.r[15] = hold.NextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, hold.NextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kPauseConditionalDrawCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    if (Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
            inputs, &dispatch.TopScreenPauseRoute)) {
      return branchFromCallsite(kPauseConditionalNativeDraw, pc + 4U);
    }
    return branchFromCallsite(pc + 4U, pc + 4U);
  }
  if (pc == kPauseTouchCoordinateUpdateCall) {
    if (dispatch.TopScreenTouchCoordinateUpdatePhase == 0U) {
      dispatch.TopScreenTouchCoordinateUpdatePhase = 1U;
      return branchFromCallsite(kPauseTouchCoordinateNativeUpdate, pc);
    }
    dispatch.TopScreenTouchCoordinateUpdatePhase = 0U;
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    std::uint32_t pauseCameraControl = 0U;
    std::uint32_t secondaryLayer = 0U;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs) ||
        !memory.Read32(0x00506CE8U, &pauseCameraControl) ||
        !memory.Read32(0x004FDABCU, &secondaryLayer)) {
      return false;
    }
    const bool routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
        inputs, &dispatch.TopScreenPauseRoute);
    if (Oot3dNativeGame::ResolveTopScreenTouchCoordinateSuppression(
            inputs, routeActive, pauseCameraControl != 0U, secondaryLayer == 1U,
            &dispatch.TopScreenTouchCoordinateRoute) &&
        !Oot3dNativeGame::ApplyTopScreenTouchCoordinateSuppression(memory)) {
      return false;
    }
    return branchFromCallsite(pc + 4U, pc + 4U);
  }
  if (pc == kPauseRendererVisibilityCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs globalInputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &globalInputs)) {
      return false;
    }
    Oot3dNativeGame::TopScreenRendererVisibilityInputs inputs;
    inputs.ControllerAddress = state.r[0];
    inputs.HasCallScene = state.r[1] != 0U;
    inputs.GlobalSceneRuntimeRoute =
        globalInputs.HasScene && globalInputs.SceneMode == 3U &&
        (globalInputs.RuntimeMode == 1U || globalInputs.RuntimeMode == 2U);
    inputs.RuntimeSceneLatch =
        dispatch.TopScreenTouchCoordinateRoute.RuntimeSceneLatch;
    inputs.NativeRuntimeActive = globalInputs.NativeTransitionActive;
    std::uint16_t nativeFadeGate = 0U;
    if (!memory.Read16(0x0058799CU, &nativeFadeGate))
      return false;
    inputs.NativeFadeGate = static_cast<std::int16_t>(nativeFadeGate);
    if (inputs.HasCallScene &&
        (!memory.Read8(state.r[1] + 0x100U, &inputs.CallSceneMode) ||
         !memory.Read32(state.r[1] + 0x104U, &inputs.CallSceneSubmode) ||
         !memory.Read16(state.r[1] + 0x318CU, &inputs.CallSceneSequence))) {
      return false;
    }
    Oot3dNativeGame::TopScreenRendererVisibilityAction action;
    if (!Oot3dNativeGame::ResolveTopScreenRendererVisibilityRoute(
            memory, inputs, &dispatch.TopScreenRendererVisibilityRoute,
            &action)) {
      return false;
    }
    return branchFromCallsite(
        action == Oot3dNativeGame::TopScreenRendererVisibilityAction::NativeDraw
            ? kPauseRendererVisibilityNativeDraw
            : pc + 4U,
        pc + 4U);
  }
  if (pc == kPauseControllerDrawCall) {
    if (dispatch.TopScreenPauseControllerDrawPhase == 1U) {
      if (!Oot3dNativeGame::PrepareTopScreenPauseAlternateRenderers(
              memory, &dispatch.TopScreenPauseController,
              &dispatch.TopScreenPauseControllerVisibleRenderers)) {
        return false;
      }
      dispatch.TopScreenPauseControllerDrawPhase = 2U;
      state.r[0] = 0U;
      state.r[1] = 40U;
      state.r[2] = 480U;
      state.r[3] = 320U;
      return branchFromCallsite(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 2U) {
      dispatch.TopScreenPauseControllerDrawPhase = 3U;
      const auto renderer =
          dispatch.TopScreenPauseControllerVisibleRenderers[0];
      if (renderer != 0U) {
        state.r[0] = renderer;
        return branchFromCallsite(kPauseControllerRendererDraw, pc);
      }
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 3U) {
      dispatch.TopScreenPauseControllerDrawPhase = 4U;
      const auto renderer =
          dispatch.TopScreenPauseControllerVisibleRenderers[1];
      if (renderer != 0U) {
        state.r[0] = renderer;
        return branchFromCallsite(kPauseControllerRendererDraw, pc);
      }
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 4U) {
      dispatch.TopScreenPauseControllerDrawPhase = 5U;
      state.r[0] = 0U;
      state.r[1] = 0U;
      state.r[2] = 480U;
      state.r[3] = 400U;
      return branchFromCallsite(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 5U) {
      dispatch.TopScreenPauseControllerDrawPhase = 0U;
      dispatch.TopScreenPauseControllerVisibleRenderers = {};
      Oot3dNativeGame::TopScreenPauseControllerInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseControllerInputs(memory,
                                                               &inputs)) {
        return false;
      }
      if (dispatch.UiLifecycleBridge != nullptr) {
        // 2.1.1 0x005D18F0: scene mode 5 has no pause edges.
        dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
            Oot3dNativeGame::UseTopScreenSceneFiveViewport(inputs.Pause)
                ? std::nullopt
                : std::optional(Oot3dNativeGame::BuildTopScreenPauseEdgeGeometry(
                      inputs.Pause.HasScene, inputs.Pause.SceneMode,
                      inputs.Pause.RuntimeMode, 0U, 0, 1.0F, 1.0F,
                      inputs.Pause.ChildStates[1] != 0U)));
      }
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    Oot3dNativeGame::TopScreenPauseControllerInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseControllerInputs(memory, &inputs)) {
      return false;
    }
    const bool routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
        inputs.Pause, &dispatch.TopScreenPauseRoute);
    Oot3dNativeGame::TopScreenPauseControllerAction action;
    if (!Oot3dNativeGame::ResolveTopScreenPauseController(
            memory, inputs, routeActive, &dispatch.TopScreenPauseController,
            &action)) {
      return false;
    }
    if (action ==
        Oot3dNativeGame::TopScreenPauseControllerAction::SuppressNativeDraw) {
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    if (action == Oot3dNativeGame::TopScreenPauseControllerAction::
                      NativeDrawThenAlternateOverlay) {
      dispatch.TopScreenPauseControllerDrawPhase = 1U;
      return branchFromCallsite(kPauseControllerNativeDraw, pc);
    }
    return branchFromCallsite(kPauseControllerNativeDraw, pc + 4U);
  }
  if (pc == kPauseRoutedCommandCallA || pc == kPauseRoutedCommandCallB) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    const bool routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
        inputs, &dispatch.TopScreenPauseRoute);
    state.r[1] = Oot3dNativeGame::ResolveTopScreenPauseRoutedCommand(
        state.r[1], routeActive);
    return branchFromCallsite(kPauseRoutedCommandNative, pc + 4U);
  }
  if (pc == kPauseOverlayViewportDrawCall) {
    if (dispatch.TopScreenOverlayViewportDrawPhase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (!Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
              inputs, &dispatch.TopScreenPauseRoute)) {
        if (dispatch.UiLifecycleBridge != nullptr) {
          dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
              std::nullopt);
        }
        return branchFromCallsite(kPauseOverlayNativeDraw, pc + 4U);
      }
      dispatch.TopScreenOverlayViewportDrawPhase = 1U;
      state.r[0] = 0U;
      state.r[1] = 40U;
      state.r[2] = 480U;
      state.r[3] = 320U;
      return branchFromCallsite(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenOverlayViewportDrawPhase == 1U) {
      dispatch.TopScreenOverlayViewportDrawPhase = 2U;
      return branchFromCallsite(kPauseOverlayNativeDraw, pc);
    }
    dispatch.TopScreenOverlayViewportDrawPhase = 0U;
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    // No pause edges here: in scene mode 5 (the file select and the other
    // frontend screens) 2.1.1 0x005D2078 leaves through 0x005D18F0 before its
    // edge producer 0x005D0374.
    if (dispatch.UiLifecycleBridge != nullptr) {
      dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
          std::nullopt);
    }
    if (!Oot3dNativeGame::UseTopScreenSceneFiveViewport(inputs)) {
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    state.r[0] = 0U;
    state.r[1] = 0U;
    state.r[2] = 480U;
    state.r[3] = 400U;
    return branchFromCallsite(kGlViewportEntry, pc + 4U);
  }
  if (pc == kPauseSceneSevenDrawCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    if (inputs.HasScene && (inputs.SceneMode == 5U || inputs.SceneMode == 7U)) {
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    return branchFromCallsite(kPauseSceneSevenNativeDraw, pc + 4U);
  }
  // 2.1.1 005DF518 / 005D3CBC: scene type 5 skips these calls.
  // The 1.2 message viewport transaction is not present in 2.1.1.
  if (pc == kPauseSceneFiveDrawCall || pc == kPauseSceneViewportDrawCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    if (inputs.HasScene && inputs.SceneMode == 5U) {
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    // 3.0 0x005D6354 (at 0x00300550) also skips the page title and item
    // description of 0x0042CBCC while the View Model is open.
    if (pc == kPauseSceneViewportDrawCall && dispatch.WideItems != nullptr &&
        dispatch.WideItems->ViewModelActive()) {
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    return branchFromCallsite(pc == kPauseSceneFiveDrawCall
                                  ? kPauseSceneFiveNativeDraw
                                  : kPauseSceneViewportNativeDraw,
                              pc + 4U);
  }
  if (pc == kPauseAlternateRendererUpdateCall) {
    if (!dispatch.TopScreenAlternateRendererRestore.has_value()) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      // 005D1920 tests scene type 5 before saving any renderer fields.
      if (inputs.HasScene && inputs.SceneMode == 5U) {
        return branchFromCallsite(pc + 4U, pc + 4U);
      }
      if (Oot3dNativeGame::UseTopScreenAlternatePauseViewport(inputs)) {
        Oot3dNativeGame::TopScreenAlternateRendererState saved;
        if (!Oot3dNativeGame::BeginTopScreenAlternateRendererSuppression(
                memory, &saved)) {
          return false;
        }
        dispatch.TopScreenAlternateRendererRestore = saved;
        state.r[14] = pc;
      } else {
        state.r[14] = pc + 4U;
      }
      state.r[15] = kPauseAlternateRendererNativeUpdate;
      *result = {oot3d::recomp::a32::ExitKind::Branch,
                 kPauseAlternateRendererNativeUpdate,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    }
    if (!Oot3dNativeGame::EndTopScreenAlternateRendererSuppression(
            memory, *dispatch.TopScreenAlternateRendererRestore)) {
      return false;
    }
    dispatch.TopScreenAlternateRendererRestore.reset();
    nextPc = pc + 4U;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kPauseAlternateViewportDrawCall) {
    auto branch = [&](uint32_t target, uint32_t returnAddress) {
      state.r[14] = returnAddress;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };
    if (dispatch.TopScreenViewportDrawPhase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (!Oot3dNativeGame::UseTopScreenAlternatePauseViewport(inputs)) {
        return branch(kPauseAlternateViewportNativeDraw, pc + 4U);
      }
      dispatch.TopScreenViewportDrawArgument = state.r[0];
      dispatch.TopScreenViewportDrawPhase = 1U;
      state.r[0] = 0U;
      state.r[1] = 40U;
      state.r[2] = 480U;
      state.r[3] = 320U;
      return branch(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenViewportDrawPhase == 1U) {
      dispatch.TopScreenViewportDrawPhase = 2U;
      state.r[0] = dispatch.TopScreenViewportDrawArgument;
      return branch(kPauseAlternateViewportNativeDraw, pc);
    }
    dispatch.TopScreenViewportDrawPhase = 0U;
    dispatch.TopScreenViewportDrawArgument = 0U;
    state.r[0] = 0U;
    state.r[1] = 0U;
    state.r[2] = 480U;
    state.r[3] = 400U;
    return branch(kGlViewportEntry, pc + 4U);
  }
  if (pc == kTopScreenCameraUpdateEntry ||
      pc == kTopScreenCameraUpdatePatchSite) {
    const bool enteredAtFunctionEntry = pc == kTopScreenCameraUpdateEntry;
    // Developer free camera: the main camera's update is replaced by the
    // free view (no Link follow, no collision), and returns to the caller.
    if (enteredAtFunctionEntry &&
        dispatch.TopScreenCamera.Phase == Oot3dNativeGame::TopScreenCameraGuestPhase::Idle &&
        dispatch.DevFreeCamera != nullptr && dispatch.DevFreeCamera->Active()) {
      const uint32_t out = state.r[0];
      const uint32_t camera = state.r[1];
      const auto readVec = [&](uint32_t address, std::array<float, 3> &value) {
        for (uint32_t axis = 0; axis < 3U; ++axis) {
          uint32_t bits = 0U;
          if (!memory.Read32(address + axis * 4U, &bits)) return false;
          std::memcpy(&value[axis], &bits, sizeof(bits));
        }
        return true;
      };
      const auto writeVec = [&](uint32_t address, const std::array<float, 3> &value) {
        for (uint32_t axis = 0; axis < 3U; ++axis) {
          uint32_t bits = 0U;
          std::memcpy(&bits, &value[axis], sizeof(bits));
          if (!memory.Write32(address + axis * 4U, bits)) return false;
        }
        return true;
      };
      uint32_t play = 0U;
      std::array<float, 3> gameEye{}, gameAt{};
      // Camera +0xD4 is its PlayState; the main camera is PlayState +0x364.
      if (memory.Read32(camera + 0xD4U, &play) && play != 0U && camera == play + 0x364U &&
          readVec(camera + 0x8CU, gameEye) && readVec(camera + 0x80U, gameAt)) {
        if (const auto view = dispatch.DevFreeCamera->ResolveView(gameEye, gameAt)) {
          // Camera at/eye (+0x80/+0x8C/+0xA4) and the PlayState view
          // (eye +0x1B8, at +0x1C4, up +0x1D0), as the TopScreen camera writes.
          if (!writeVec(camera + 0x80U, view->At) || !writeVec(camera + 0x8CU, view->Eye) ||
              !writeVec(camera + 0xA4U, view->Eye) || !writeVec(play + 0x1C4U, view->At) ||
              !writeVec(play + 0x1B8U, view->Eye) || !writeVec(play + 0x1D0U, {0.0F, 1.0F, 0.0F}) ||
              !memory.Write16(out, static_cast<uint16_t>(view->Pitch)) ||
              !memory.Write16(out + 2U, static_cast<uint16_t>(view->Yaw)) ||
              !memory.Write16(out + 4U, 0U))
            return false;
          if (!dispatch.DevFreeCameraReported) {
            dispatch.DevFreeCameraReported = true;
            std::fprintf(stderr, "TRIAEVUM_DEV_FREECAM applied camera=0x%08X eye=%.1f,%.1f,%.1f\n", camera,
                         view->Eye[0], view->Eye[1], view->Eye[2]);
          }
          state.r[0] = out;
          state.r[15] = state.r[14];
          *result = {oot3d::recomp::a32::ExitKind::Branch, state.r[14],
                     oot3d::recomp::a32::FallbackReason::None, pc};
          if (blocksConsumed != nullptr)
            *blocksConsumed = 1U;
          return true;
        }
      }
    }
    if (enteredAtFunctionEntry &&
        dispatch.TopScreenCamera.Phase ==
            Oot3dNativeGame::TopScreenCameraGuestPhase::Idle) {
      std::uint16_t cameraSetting = 0U;
      std::uint16_t cameraIndex = 0xFFFFU;
      std::uint32_t play = 0U;
      if (!memory.Read16(state.r[1] + 0x18AU, &cameraSetting) ||
          !memory.Read16(state.r[1] + 0x1ACU, &cameraIndex) ||
          !memory.Read32(state.r[1] + 0xD4U, &play)) {
        return false;
      }
      dispatch.TopScreenCameraFov =
          Oot3dNativeGame::ResolveTopScreenCameraFovOwnership(
              {play, static_cast<std::int16_t>(cameraSetting),
               static_cast<std::int16_t>(cameraIndex)});
    }
    if (!dispatch.TopScreenCamera.Camera.Enabled) {
      dispatch.TopScreenExtendedCameraActive = false;
      return false;
    }
    const uint32_t cameraStackAddress =
        enteredAtFunctionEntry ? state.r[13] - 36U : state.r[13];
    auto branchToNative = [&](uint32_t target, uint32_t argument0,
                              uint32_t argument1 = 0U,
                              uint32_t argument2 = 0U) {
      state.r[0] = argument0;
      state.r[1] = argument1;
      state.r[2] = argument2;
      // Return through the real function entry: 0x002D84C8 is the IPS patch
      // site but not a materialized AOT block boundary. The staged phase
      // distinguishes this callback from the initial function invocation.
      state.r[14] = kTopScreenCameraUpdateEntry;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };
    auto finishUpdate = [&]() {
      const uint32_t stack = dispatch.TopScreenCamera.OriginalStackAddress;
      for (uint32_t index = 0U; index < 8U; ++index) {
        if (!memory.Read32(stack + index * 4U, &state.r[4U + index])) {
          return false;
        }
      }
      if (!memory.Read32(stack + 32U, &nextPc))
        return false;
      state.r[13] = stack + 36U;
      state.r[15] = nextPc;
      Oot3dNativeGame::ResetTopScreenFreeCameraGuestUpdate(
          &dispatch.TopScreenCamera);
      dispatch.TopScreenExtendedCameraActive =
          dispatch.TopScreenCamera.Camera.Active;
      *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };

    switch (dispatch.TopScreenCamera.Phase) {
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Idle: {
      ++dispatch.TopScreenCameraUpdateCalls;
      bool active = false;
      const auto cameraInput = dispatch.TopScreenCameraInput.Consume();
      if (!Oot3dNativeGame::BeginTopScreenFreeCameraGuestUpdate(
              memory, state.r[0], state.r[1], cameraStackAddress,
              cameraInput.X, cameraInput.Y,
              cameraInput.Kind ==
                  Oot3dNativeGame::NativeFreeCameraInputKind::Relative,
              &dispatch.TopScreenCamera, &active)) {
        return false;
      }
      dispatch.TopScreenExtendedCameraActive = active;
      dispatch.TopScreenCameraActiveCalls += active ? 1U : 0U;
      if (!active) {
        // The payload owns Camera_Update only while free-camera
        // ownership is active. Let the compiled native function run
        // from its original entry so it retains its optimized region
        // and executes the original `mov r6, r0` itself.
        return false;
      }
      if (enteredAtFunctionEntry) {
        // Native 0x002D84C4 is `push {r4-r11, lr}`. The IPS record replaces
        // the following `mov r6, r0`, but AOT callbacks can only observe the
        // function's real basic-block entry. Materialize the prologue only
        // after the typed path has accepted ownership; otherwise returning
        // false executes the complete original function unchanged.
        for (uint32_t index = 0U; index < 8U; ++index) {
          if (!memory.Write32(cameraStackAddress + index * 4U,
                              state.r[4U + index])) {
            return false;
          }
        }
        if (!memory.Write32(cameraStackAddress + 32U, state.r[14])) {
          return false;
        }
        state.r[13] = cameraStackAddress;
      }
      state.r[13] = dispatch.TopScreenCamera.ScratchAddress;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraCheckWater,
                            dispatch.TopScreenCamera.CameraAddress);
    }
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Water:
      if (!Oot3dNativeGame::ApplyTopScreenFreeCameraEnvironment(
              memory, &dispatch.TopScreenCamera))
        return false;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraUpdateInterface,
                            0U);
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Interface:
      if (!Oot3dNativeGame::PrepareTopScreenFreeCameraCollision(
              memory, &dispatch.TopScreenCamera))
        return false;
      if (dispatch.TopScreenCamera.PlayerAddress == 0U) {
        return finishUpdate();
      }
      return branchToNative(Oot3dNativeGame::kTopScreenCameraBgCheckInfo,
                            dispatch.TopScreenCamera.CameraAddress,
                            dispatch.TopScreenCamera.ScratchAddress + 0x20U,
                            dispatch.TopScreenCamera.ScratchAddress + 0x30U);
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Collision:
      if (!Oot3dNativeGame::CommitTopScreenFreeCameraCollision(
              memory, &dispatch.TopScreenCamera))
        return false;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraQuakeUpdate,
                            dispatch.TopScreenCamera.CameraAddress,
                            dispatch.TopScreenCamera.ScratchAddress + 0x60U);
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Quake: {
      const bool quakeActive = state.r[0] != 0U;
      if (!Oot3dNativeGame::ApplyTopScreenFreeCameraQuake(
              memory, &dispatch.TopScreenCamera, quakeActive)) {
        return false;
      }
      std::uint32_t floorPoly = 0U;
      std::uint8_t bgId = 0U;
      if (!memory.Read32(dispatch.TopScreenCamera.PlayerAddress + 0x7CU,
                         &floorPoly) ||
          !memory.Read8(dispatch.TopScreenCamera.PlayerAddress + 0x81U, &bgId))
        return false;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraGetDataId,
                            dispatch.TopScreenCamera.GlobalContextAddress +
                                0xA98U,
                            floorPoly, bgId);
    }
    case Oot3dNativeGame::TopScreenCameraGuestPhase::CameraData:
      if (!Oot3dNativeGame::ApplyTopScreenFreeCameraData(
              memory, &dispatch.TopScreenCamera,
              static_cast<int16_t>(state.r[0])))
        return false;
      return finishUpdate();
    }
  }
  if (pc == kTopScreenCameraFovScalar) {
    // 005DF388 scales and clamps to 20..110 degrees at every percentage,
    // 100 included.
    if (!dispatch.TopScreenCameraFov.Active ||
        dispatch.TopScreenCameraFov.ValueAddress != state.r[4] + 0x10U) {
      return false;
    }
    std::uint32_t nativeFovBits = 0U;
    if (!memory.Read32(state.r[4] + 0x10U, &nativeFovBits)) {
      return false;
    }
    const float resolvedFov = Oot3dNativeGame::ResolveTopScreenCameraFovDegrees(
        std::bit_cast<float>(nativeFovBits),
        dispatch.TopScreenCamera.Camera.FovPercent);
    state.vfp[16] = std::bit_cast<std::uint32_t>(resolvedFov);
    nextPc = pc + 4U;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kTopScreenCameraNormal1Scalar) {
    if (!dispatch.TopScreenCamera.Camera.N64StyleZoom) {
      return false;
    }
    ++dispatch.TopScreenCameraNormal1Calls;
    const auto scalar = Oot3dNativeGame::ResolveTopScreenCameraNormal1Scalar(
        state.vfp[0], dispatch.TopScreenCamera.Camera.ZoomPercent);
    if (!memory.Write32(state.r[4] + 0x124U, scalar.NativeValueBits) ||
        !memory.Write32(state.r[13] + 0x60U, scalar.ContinuedValueBits)) {
      return false;
    }
    state.vfp[0] = scalar.ContinuedValueBits;
    // The payload returns at 0x0023A938, inside the original basic block.
    // Preserve that continuation through its next real AOT boundary.
    uint16_t modeBits = 0U;
    if (!memory.Read16(state.r[5] + 0x2AU, &modeBits))
      return false;
    const auto mode = static_cast<int32_t>(static_cast<int16_t>(modeBits));
    state.r[0] = static_cast<uint32_t>(mode);
    auto compareZero = [&](uint32_t value) {
      using namespace oot3d::recomp::a32;
      state.cpsr = (state.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
                   kFlagC | (value & kFlagN) | (value == 0U ? kFlagZ : 0U);
    };
    compareZero(state.r[0]);
    if (mode <= 0) {
      nextPc = kTopScreenCameraNormal1DefaultPath;
    } else {
      compareZero(state.r[9]);
      nextPc = state.r[9] == 0U ? kTopScreenCameraNormal1AlternatePath
                                : kTopScreenCameraNormal1DefaultPath;
    }
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (Oot3dNativeGame::ExecuteTopScreenItemDispatch(
          pc, memory, state, dispatch.TopScreenInput, dispatch.TopScreenItems)) {
    *result = {oot3d::recomp::a32::ExitKind::Branch, state.r[15],
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  }
  if (pc == kTopScreenGameplayCompositionPrefix) {
    uint32_t thresholdBits = 0U;
    uint32_t compositionBits = 0U;
    if (!memory.Read32(0x002E2A6CU, &thresholdBits))
      return false;
    state.vfp[3] = state.vfp[16];
    state.r[8] = state.r[4] + 0x7C00U;
    state.r[1] = thresholdBits;
    if (!memory.Read32(state.r[8] + 0x338U, &compositionBits)) {
      return false;
    }
    state.vfp[0] = compositionBits;
    state.vfp[2] = state.vfp[3];
    state.vfp[1] = state.vfp[3];
    if (!memory.Write32(state.r[13] + 0x1DCU, state.vfp[2]) ||
        !memory.Write32(state.r[13] + 0x1E0U, state.vfp[1]) ||
        !memory.Write32(state.r[13] + 0x1E4U, state.vfp[3]) ||
        !memory.Write32(state.r[13] + 0x1E8U, state.vfp[0])) {
      return false;
    }
    state.r[0] = compositionBits;
    const uint32_t subtraction = state.r[0] - state.r[1];
    using namespace oot3d::recomp::a32;
    state.cpsr = (state.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
                 (subtraction & kFlagN) | (subtraction == 0U ? kFlagZ : 0U) |
                 (state.r[0] >= state.r[1] ? kFlagC : 0U) |
                 ((((state.r[0] ^ state.r[1]) & (state.r[0] ^ subtraction)) &
                   kFlagN) != 0U
                      ? kFlagV
                      : 0U);
    if (!Oot3dNativeGame::ResolveTopScreenControlFlow(
            kTopScreenGameplayCompositionBranch, &nextPc)) {
      return false;
    }
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (Oot3dNativeGame::ResolveTopScreenControlFlow(pc, &nextPc)) {
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  uint8_t vfpLane = 0U;
  uint32_t valueBits = 0U;
  if (Oot3dNativeGame::ResolveTopScreenFloatLoad(pc, &vfpLane, &valueBits)) {
    state.vfp[vfpLane] = valueBits;
    nextPc = pc + 4U;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  switch (pc) {
  case kPauseUiNativeUpdateCall: {
    uint32_t pauseState = 0U;
    if (!memory.Read32(0x0050AF68U, &pauseState))
      return false;
    if (Oot3dNativeGame::SuppressTopScreenTouchButtonDraw(pauseState)) {
      nextPc = pc + 4U;
    } else {
      state.r[14] = pc + 4U;
      nextPc = kPauseUiNativeUpdate;
    }
    break;
  }
  case kPauseProjectionPrepareCall:
    if (dispatch.TopScreenPauseProjection == nullptr ||
        !Oot3dNativeGame::ApplyTopScreenPauseProjection(
            memory, *dispatch.TopScreenPauseProjection, nullptr,
            dispatch.TopScreenConfig)) {
      return false;
    }
    state.r[14] = pc + 4U;
    nextPc = kPauseProjectionPrepare;
    break;
  case kPauseTouchButtonsDrawCall: {
    if (!Oot3dNativeGame::ApplyTopScreenNativeTouchQuad28Layout(memory)) {
      return false;
    }
    uint32_t pauseState = 0U;
    Oot3dNativeGame::TopScreenPauseDrawInputs pageInputs;
    if (!memory.Read32(0x0050AF68U, &pauseState) ||
        !Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &pageInputs))
      return false;
    if (Oot3dNativeGame::SuppressTopScreenTouchButtonPageDraw(pauseState, pageInputs)) {
      nextPc = pc + 4U;
    } else {
      state.r[14] = pc + 4U;
      nextPc = state.r[1];
    }
    break;
  }
  case kPauseTouchButtonsTouchBranch:
    // IPS record 27 changes `tst r0, #8` to `tst r0, #0`; the following
    // native BEQ therefore always takes the no-touch continuation.
    state.cpsr = (state.cpsr & ~((1U << 31U) | (1U << 30U))) | (1U << 30U);
    nextPc = kPauseTouchButtonsNoTouchExit;
    break;
  case kPauseTouchButtonsHeldDownQuery:
    // TopScreen 2.1.1 replaces this PauseInput_GetHeldButtons call with
    // `mov r0, #0`: held D-pad Down (0x80) never reaches the native 30-update
    // counter at 0x0042E654 that calls PauseTouchButtons_TogglePrimaryRestriction
    // (0x002F009C). The mod owns D-pad Down (Ocarina by default).
    state.r[0] = 0U;
    nextPc = kPauseTouchButtonsHeldDownTest;
    break;
  case kPauseLowerScreenBlock:
    nextPc = kPauseLowerScreenBlockEnd;
    break;
  case kPauseDungeonMapCursorInput:
    nextPc = state.r[14];
    break;
  case kPauseSceneDimBranch: {
    // 3.0 always skips the dim: the View Model and the System page (Save)
    // show the scene undimmed. Beside the pages the port keeps the native dim
    // (16:9 side strips; see oot3d_top_screen_mod_profile_internal.h).
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) return false;
    const bool viewModel = dispatch.WideItems != nullptr && dispatch.WideItems->ViewModelActive();
    if (!viewModel && inputs.ChildStates[3] == 0U) return false;
    nextPc = kPauseSceneDimSkip;
    break;
  }
  case Oot3dNativeGame::kTopScreenQuestDrawHook: {
    // 3.0 0x005C91D0 returns at once while the pause owns the screen.
    bool suppressed = false;
    std::string error;
    if (!Oot3dNativeGame::ReadTopScreenQuestPageDrawSuppressed(
            memory, dispatch.TopScreenTouchCoordinateRoute.RuntimeSceneLatch, &suppressed,
            &error))
      throw std::runtime_error("TopScreen Quest draw gate: " + error);
    if (!suppressed) return false;
    nextPc = state.r[14];
    break;
  }
  default:
    return false;
  }
  state.r[15] = nextPc;
  *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
             oot3d::recomp::a32::FallbackReason::None, pc};
  if (blocksConsumed != nullptr)
    *blocksConsumed = 1U;
  return true;
}

} // namespace

bool ExecuteProductTopScreenHook(uint32_t pc, oot3d::recomp::a32::GuestState &state,
                                   oot3d::recomp::a32::MemoryBus &memory,
                                   oot3d::recomp::a32::ExecutionResult *result,
                                   uint32_t blockBudget, uint32_t *blocksConsumed, void *user) {
  auto &dispatch = *static_cast<NativeCandidateDispatchState *>(user);
  const bool observedExit = dispatch.ObservedExitPc == pc;
  dispatch.ObservedExitPc.reset();
  // As the developer interpreter does, the entry's block policy runs before
  // its hook. An entry resumed after its own observable exit already ran it
  // in compiled code; one reached by a hook's branch (0x0041E984 resumes at
  // the input boundary 0x0041E988) or after a block budget has not. Outside
  // compiled execution the policy cannot raise another exit.
  const auto runBlockEntry = [&] {
    if (!observedExit && dispatch.BlockEntry != nullptr &&
        (dispatch.BlockEntryPcs.empty() ||
         std::find(dispatch.BlockEntryPcs.begin(), dispatch.BlockEntryPcs.end(), pc) !=
             dispatch.BlockEntryPcs.end()))
      dispatch.BlockEntry(pc, state, memory, dispatch.BlockEntryUser);
  };
  // Extended draw distance object slots, in every UI profile. Their entries
  // never coincide with TopScreen blocks. If native ownership wins, resume
  // the entry once instead of trapping it again.
  const auto objectBankHooks = Oot3dNativeGame::ObjectBankExtension::HookPcs();
  if (dispatch.ObjectBank != nullptr && dispatch.Memory != nullptr &&
      std::binary_search(objectBankHooks.begin(), objectBankHooks.end(), pc)) {
    runBlockEntry();
    if (dispatch.ObjectBank->Execute(pc, state, result, blocksConsumed)) return true;
    return Oot3dNativeGame::ExecuteOot3dCompiledFunction(
        pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
        dispatch.BlockEntry, dispatch.BlockEntryUser,
        dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true, true, true);
  }
  // SingleScreen 3.0 rain around the native weather draw.
  const auto rainHooks = Oot3dNativeGame::TopScreenRainAdapter::HookPcs();
  if (dispatch.Rain != nullptr && dispatch.Memory != nullptr &&
      std::find(rainHooks.begin(), rainHooks.end(), pc) != rainHooks.end()) {
    runBlockEntry();
    if (dispatch.Rain->Execute(pc, state, result, blocksConsumed)) return true;
    return Oot3dNativeGame::ExecuteOot3dCompiledFunction(
        pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
        dispatch.BlockEntry, dispatch.BlockEntryUser,
        dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true, true, true);
  }
  // Product opt-in UI mod, not the experimental source/gameplay dispatcher:
  // the whole TopScreen source port, and nothing else.
  if (!dispatch.TopScreenUiProfile || dispatch.Memory == nullptr ||
      !std::binary_search(dispatch.ProductTopScreenBlocks.begin(),
                          dispatch.ProductTopScreenBlocks.end(), pc)) return false;
  runBlockEntry();
  if (pc == kPauseRendererVisibilityNativeDraw) {
    auto &phase = dispatch.TopScreenGameOverDrawPhase;
    auto &saved = dispatch.TopScreenGameOverDrawRegisters;
    auto branch = [&](uint32_t target) {
      state.r[14] = pc;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed) *blocksConsumed = 1U;
      return true;
    };
    std::string error;
    if (phase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(*dispatch.Memory, &inputs, &error) ||
          !Oot3dNativeGame::PrepareTopScreenGameOverDraw(
              *dispatch.Memory, state.r[0], inputs,
              dispatch.TopScreenTouchCoordinateRoute.RuntimeSceneLatch,
              &dispatch.TopScreenRendererVisibilityRoute, &error)) {
        throw std::runtime_error("TopScreen GameOver prepare: " + error);
      }
      if (dispatch.TopScreenRendererVisibilityRoute.FadeStep == 24) {
        saved = {state.r[0], state.r[1], state.r[2], state.r[3], state.r[14]};
        // 2.1.1 compositor: native auxiliary redraw establishes the 2D
        // material state before drawing the reconciled native choices.
        if (!Oot3dNativeGame::BeginTopScreenPauseChildSuppression(
                *dispatch.Memory, Oot3dNativeGame::TopScreenPauseChildSuppressionSet::AuxiliaryRedraw,
                &dispatch.TopScreenGameOverChildRestore, &error)) {
          throw std::runtime_error("TopScreen GameOver auxiliary draw: " + error);
        }
        phase = 1U;
        state.r[0] = 0U; state.r[1] = 40U;
        state.r[2] = 480U; state.r[3] = 320U;
        return branch(kGlViewportEntry);
      }
    } else if (phase == 1U) {
      phase = 2U;
      return branch(kPauseUiDraw);
    } else if (phase == 2U) {
      if (!Oot3dNativeGame::EndTopScreenPauseChildSuppression(
              *dispatch.Memory, dispatch.TopScreenGameOverChildRestore, &error)) {
        throw std::runtime_error("TopScreen GameOver restore: " + error);
      }
      phase = 3U;
      std::copy_n(saved.begin(), 4U, state.r.begin());
      state.r[14] = pc;
      // Fall through to native draw, bypassing this entry hook once.
    } else if (phase == 3U) {
      phase = 4U;
      state.r[0] = 0U; state.r[1] = 0U;
      state.r[2] = 480U; state.r[3] = 400U;
      return branch(kGlViewportEntry);
    } else {
      phase = 0U;
      std::copy_n(saved.begin(), 4U, state.r.begin());
      const auto handled = branch(saved[4]);
      state.r[14] = saved[4];
      return handled;
    }
    return Oot3dNativeGame::ExecuteOot3dCompiledFunction(
        pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
        dispatch.BlockEntry, dispatch.BlockEntryUser,
        dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true, true, true);
  }
  const bool handled = ExecuteTopScreenUiSourcePortBlock(pc, state, dispatch, result, blocksConsumed);
  if (handled) {
    ++dispatch.TopScreenSourcePortCalls;
    if (pc == kTopScreenGameplayCompositionPrefix) {
      ++dispatch.TopScreenLowerCompositionSkips;
      if (dispatch.PicaCompositionDomain != nullptr)
        *dispatch.PicaCompositionDomain = Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui;
      if (dispatch.UiLifecycleBridge != nullptr)
        dispatch.UiLifecycleBridge->ObserveGameplayComposition();
    }
    return true;
  }
  // The entry's policy ran above or before its observable exit. If native
  // ownership wins, resume it once instead of trapping the same entry.
  return Oot3dNativeGame::ExecuteOot3dCompiledFunction(
      pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
      dispatch.BlockEntry, dispatch.BlockEntryUser,
      dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true, true, true);
}

bool ExecuteNativeCandidate(uint32_t pc, oot3d::recomp::a32::GuestState &state,
                            oot3d::recomp::a32::MemoryBus &memory,
                            oot3d::recomp::a32::ExecutionResult *result,
                            uint32_t blockBudget, uint32_t *blocksConsumed,
                            void *user) {
  auto &dispatch = *static_cast<NativeCandidateDispatchState *>(user);
  bool sample = false;
  if (dispatch.ProfileRuntime) {
    ++dispatch.Calls;
    dispatch.SampleState = dispatch.SampleState * 1664525U + 1013904223U;
    sample = (dispatch.SampleState & (kA32RuntimeSampleDenominator - 1U)) == 0U;
  }
  const auto sampleStart = sample ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point{};
  // Extended draw distance object slots, in every UI profile.
  if (dispatch.ObjectBank != nullptr &&
      dispatch.ObjectBank->Execute(pc, state, result, blocksConsumed))
    return true;
  if (dispatch.Rain != nullptr && dispatch.Rain->Execute(pc, state, result, blocksConsumed))
    return true;
  const bool topScreenUi =
      dispatch.TopScreenUiProfile && dispatch.Memory != nullptr &&
      ExecuteTopScreenUiSourcePortBlock(pc, state, dispatch, result,
                                        blocksConsumed);
  if (topScreenUi) {
    ++dispatch.TopScreenSourcePortCalls;
    if (pc == kTopScreenGameplayCompositionPrefix) {
      ++dispatch.TopScreenLowerCompositionSkips;
      if (dispatch.PicaCompositionDomain != nullptr) {
        *dispatch.PicaCompositionDomain =
            Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui;
      }
      if (dispatch.UiLifecycleBridge != nullptr) {
        dispatch.UiLifecycleBridge->ObserveGameplayComposition();
      }
    }
  }
  const bool typedGameplay = !topScreenUi &&
                             dispatch.TypedGameplay &&
                             dispatch.Memory != nullptr &&
                             Oot3dNativeGame::ExecuteOot3dTypedGameplay(
                                 pc, state, *dispatch.Memory, result,
                                 dispatch.TypedGameplayContext, blocksConsumed);
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool wholeAotAllowed = dispatch.WholeAot;
#else
  const bool wholeAotAllowed =
      dispatch.WholeAot && !dispatch.TopScreenPauseAotBarrier;
#endif
  const bool compiled =
      !topScreenUi && !typedGameplay &&
      dispatch.CompiledFunctions &&
      dispatch.Memory != nullptr &&
      Oot3dNativeGame::ExecuteOot3dCompiledFunction(
          pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
          dispatch.BlockEntry, dispatch.BlockEntryUser,
          dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true,
          dispatch.ManualCompiledFunctions, wholeAotAllowed);
  const bool massAot =
      !topScreenUi && !typedGameplay &&
      !compiled && dispatch.MassAot &&
      dispatch.Memory != nullptr &&
      Oot3dNativeGame::ExecuteOot3dMassAot(
          pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
          dispatch.BlockEntry, dispatch.BlockEntryUser,
          dispatch.MassAotBlockEntryFilter,
          dispatch.MassAotObservableExitFilter, true);
  const bool trueAot = !topScreenUi &&
                       !typedGameplay && !compiled && !massAot &&
                       dispatch.TrueAotBlocks &&
                       Oot3dNativeGame::ExecuteOot3dTrueAotBlock(
                           pc, state, memory, result, nullptr);
  if (sample) {
    const uint64_t nanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - sampleStart)
            .count());
    ++dispatch.Samples;
    if (typedGameplay) {
      ++dispatch.TypedGameplaySamples;
      dispatch.TypedGameplaySampleNanoseconds += nanoseconds;
    } else if (massAot || compiled) {
      ++dispatch.CompiledSamples;
      dispatch.CompiledSampleNanoseconds += nanoseconds;
    } else if (trueAot) {
      ++dispatch.TrueAotSamples;
      dispatch.TrueAotSampleNanoseconds += nanoseconds;
    } else {
      ++dispatch.UnhandledSamples;
      dispatch.UnhandledSampleNanoseconds += nanoseconds;
    }
  }
  const bool handled =
      topScreenUi || typedGameplay || massAot || compiled || trueAot;
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (!handled) {
    if (result != nullptr) {
      *result = {
          oot3d::recomp::a32::ExitKind::Unsupported,
          pc,
          oot3d::recomp::a32::FallbackReason::Unsupported,
          0x57414F54U,
      };
    }
    if (blocksConsumed != nullptr) {
      *blocksConsumed = 1U;
    }
    return true;
  }
#endif
  return handled;
}

} // namespace Oot3dA32Window
