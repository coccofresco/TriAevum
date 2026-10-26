// Block-entry policy of the A32 window: widescreen projection and guest observation hooks.
#include "oot3d_native_a32_window_shared.h"

namespace Oot3dA32Window {

void ApplyNativeWidescreenProjectionPolicy(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::MemoryBus &memory, void *user) {
  auto &runtime = *static_cast<NativeWidescreenProjectionState *>(user);
  (void)Oot3dNativeGame::ApplyNativeActorVisibilityHook(
      pc, state, memory, runtime.ActorVisibility);
  if (pc == Oot3dNativeGame::kNativeQbfA4Blit && runtime.HdFonts && runtime.TraceMemory)
    runtime.HdFonts->ObserveBlit(state, *runtime.TraceMemory);
  if (runtime.TopScreenUiProfile &&
      pc == Oot3dNativeGame::kTopScreenInputUpdateBoundary &&
      (runtime.OcarinaText == nullptr || !runtime.OcarinaText->Pending()) &&
      (runtime.PauseTransaction == nullptr || !runtime.PauseTransaction->Pending()) &&
      runtime.TopScreenInputClock != nullptr && runtime.TopScreenInput != nullptr) {
    *runtime.TopScreenInput = runtime.TopScreenInputClock->Advance();
    if (runtime.UiLifecycleBridge != nullptr)
      runtime.UiLifecycleBridge->SetTopScreenInputFrame(*runtime.TopScreenInput);
    if (runtime.UiLifecycleBridge != nullptr && runtime.TraceMemory != nullptr) {
      // 2.1.1 0x005DF65C, once per native UI update: consume the previous
      // update's B-copy latch, sample the player after the pause
      // transaction (skipped when one runs in this update or while the
      // runtime scene latch holds), and advance the View icon alpha.
      std::uint8_t runtimeGate = 0U, type = 0U;
      std::uint32_t play = 0U;
      auto &guest = *runtime.TraceMemory;
      if (!guest.Read8(0x005C3F71U, &runtimeGate) || !guest.Read32(0x005043E0U, &play) ||
          (play != 0U && !guest.Read8(play + 0x100U, &type)))
        throw std::runtime_error("TopScreen copy latch: cannot read the native scene");
      const bool retainPrevious =
          (runtime.PauseTransaction != nullptr && runtime.PauseTransaction->Requested()) ||
          (play != 0U && type == 3U && runtime.TopScreenTouchCoordinateRoute != nullptr &&
           runtime.TopScreenTouchCoordinateRoute->RuntimeSceneLatch && runtimeGate != 0U);
      if (!runtime.UiLifecycleBridge->BeginTopScreenTouchCopies() ||
          !runtime.UiLifecycleBridge->FinishTopScreenTouchCopies(retainPrevious) ||
          !runtime.UiLifecycleBridge->AdvanceTopScreenViewIcon())
        throw std::runtime_error("TopScreen copy latch: native update failed");
    }
    if (runtime.UiLifecycleBridge != nullptr && runtime.OcarinaText != nullptr) {
      Oot3dNativeGame::TopScreenOcarinaGeometry geometry;
      std::string textError;
      if (!runtime.UiLifecycleBridge->ReadOcarinaGeometry(geometry, &textError) ||
          !runtime.OcarinaText->Prepare(geometry, &textError))
        throw std::runtime_error("TopScreen song text preparation: " + textError);
    }
  }
  // The page draw returned: restore its children before the return can
  // leave the compiled graph, which resumes past this block entry.
  if (pc == kPauseUiDrawReturn && runtime.TopScreenUiProfile &&
      runtime.TraceMemory != nullptr &&
      runtime.PendingTopScreenPauseChildRestore.has_value()) {
    Oot3dNativeGame::EndTopScreenPauseChildSuppression(
        *runtime.TraceMemory, *runtime.PendingTopScreenPauseChildRestore);
    runtime.PendingTopScreenPauseChildRestore.reset();
  }
  // Its injected PauseUi_Draw returns here; close UI attribution before an
  // observable exit skips the remaining block-entry observations. The rain
  // adapter's exit at the weather return likewise must not leave the
  // precipitation atmosphere scope open. Reframed's split title logo returns
  // to its own call site, where the file selection exits again: a UI scope
  // left open there would hold every later draw, gameplay included, as UI.
  const bool compositionBeforeExit =
      pc == Oot3dNativeGame::kTopScreenSystemPageBoundary ||
      pc == Oot3dNativeGame::kTopScreenRainWeatherReturn ||
      pc == Oot3dNativeGame::kReframedFileLogoCall;
  if (compositionBeforeExit && runtime.PicaCompositionTracker != nullptr)
    runtime.PicaCompositionTracker->ObserveBlockEntry(pc, state, memory);
  // The camera update entry resolves the FOV owner of 0x00478EB8, which
  // 2.1.1 scales and clamps at every percentage, so it is observed whenever
  // the TopScreen profile is active, not only for the free camera.
  const bool enabledTopScreenCameraExit =
      (pc != kTopScreenCameraUpdateEntry || runtime.TopScreenUiProfile ||
       (runtime.DevFreeCamera != nullptr && runtime.DevFreeCamera->Active())) &&
      (pc != kTopScreenCameraNormal1Scalar ||
       runtime.TopScreenConfig.CameraZoomPercent != 100U);
  const bool enabledTopScreenItemExit =
      !Oot3dNativeGame::IsTopScreenItemDispatchEntry(pc) ||
      (runtime.TopScreenInput != nullptr && runtime.TopScreenItems != nullptr &&
       runtime.TraceMemory != nullptr &&
       Oot3dNativeGame::ShouldObserveTopScreenItemDispatch(
           pc, *runtime.TraceMemory, state, *runtime.TopScreenInput,
           *runtime.TopScreenItems));
#if defined(OOT3D_NATIVE_DIRECT_AOT_PLUGIN)
  const bool wholeAotExecutionActive =
      Oot3dNativeGame::Oot3dWholeAotPluginExecutionActive();
#else
  const bool wholeAotExecutionActive =
      Oot3dNativeGame::Oot3dWholeAotExecutionActive();
#endif
  const auto gameplayHooks = Oot3dNativeGame::TopScreenGameplayRuntime::HookPcs();
  const bool enabledTopScreenGameplayExit =
      !std::binary_search(gameplayHooks.begin(), gameplayHooks.end(), pc) ||
      (runtime.TopScreenGameplay != nullptr && runtime.TopScreenGameplay->ShouldObserve(pc));
  // Object slots stay observed while any extra slot is resident, even after
  // Extended draw distance is turned off.
  const auto objectBankHooks = Oot3dNativeGame::ObjectBankExtension::HookPcs();
  const bool enabledObjectBankExit =
      !std::binary_search(objectBankHooks.begin(), objectBankHooks.end(), pc) ||
      (runtime.ObjectBank != nullptr && runtime.ObjectBank->ShouldExit(pc, state));
  const auto rainHooks = Oot3dNativeGame::TopScreenRainAdapter::HookPcs();
  const bool enabledRainExit =
      std::find(rainHooks.begin(), rainHooks.end(), pc) == rainHooks.end() ||
      (runtime.Rain != nullptr && runtime.Rain->ShouldObserve(pc));
  // The Quest draw leaves compiled execution only when 3.0 skips it: an exit
  // skips the rest of this policy, the Quest hook (transforms, B copies)
  // among it.
  const auto questDrawExit = [&] {
    if (pc != Oot3dNativeGame::kTopScreenQuestDrawHook) return true;
    bool suppressed = false;
    return runtime.TraceMemory != nullptr && runtime.TopScreenTouchCoordinateRoute != nullptr &&
           Oot3dNativeGame::ReadTopScreenQuestPageDrawSuppressed(
               *runtime.TraceMemory, runtime.TopScreenTouchCoordinateRoute->RuntimeSceneLatch,
               &suppressed) &&
           suppressed;
  };
  // Player_Update is also the Roll Dash entry: an exit there skips the rest
  // of this policy, so the PlayState and player probes observe it first.
  // While A is held, the Roll Dash hook calls FUN_0037577c from this entry
  // and returns to it: that second entry carries the query's registers (r0
  // its answer, r1 player+0x1000), not the title's (player, PlayState). Only
  // the title's call is observed; the probes would otherwise lose the scene
  // view, its environment and camera for as long as A is held (#91).
  if (pc == Oot3dNativeGame::kOot3dPlayerUpdateEntry &&
      (runtime.TopScreenGameplay == nullptr || !runtime.TopScreenGameplay->DashQueryPending())) {
    if (runtime.PlayerTimingProbe != nullptr) {
      runtime.PlayerTimingProbe->ObserveUpdateEntry(state);
    }
    if (runtime.SceneViewProbe != nullptr) {
      runtime.SceneViewProbe->ObservePlayState(state.r[1]);
    }
    if (runtime.CollectExtendedDiagnostics &&
        runtime.PlayerTimingProbe != nullptr) {
      constexpr size_t kMaximumPlayerTimingEvents = 4096U;
      const auto snapshot = runtime.PlayerTimingProbe->Capture(memory);
      if (snapshot.has_value() &&
          runtime.TemporalEventLedger != nullptr &&
          runtime.FrameRatePolicy != nullptr) {
        runtime.TemporalEventLedger->ObservePlayer(
            runtime.FrameRatePolicy->GameplayClock().Context(), *snapshot);
      }
      if (snapshot.has_value() &&
          runtime.PlayerTimingEvents.size() < kMaximumPlayerTimingEvents) {
        runtime.PlayerTimingEvents.push_back({
            {"host_frame", runtime.CurrentHostFrame},
            {"world_position",
             {snapshot->WorldX, snapshot->WorldY, snapshot->WorldZ}},
            {"velocity",
             {snapshot->VelocityX, snapshot->VelocityY, snapshot->VelocityZ}},
            {"actor_speed", snapshot->ActorSpeed},
            {"player_speed", snapshot->PlayerSpeed},
            {"floor_height", snapshot->FloorHeight},
            {"underwater_timer", snapshot->UnderwaterTimer},
            {"melee_weapon_action_timer",
             snapshot->MeleeWeaponActionTimer},
            {"melee_weapon_combo_state",
             snapshot->MeleeWeaponComboState},
            {"item_action_state_or_burn_timer",
             snapshot->ItemActionStateOrBurnTimer},
            {"ledge_climb_type", snapshot->LedgeClimbType},
            {"ledge_climb_delay_timer",
             snapshot->LedgeClimbDelayTimer},
            {"textbox_button_cooldown_timer",
             snapshot->TextboxButtonCooldownTimer},
            {"damage_flicker_animation_counter",
             snapshot->DamageFlickerAnimationCounter},
            {"damage_run_timer", snapshot->DamageRunTimer},
            {"item_action_cooldown_timer",
             snapshot->ItemActionCooldownTimer},
            {"collision_sfx_cooldown_timer",
             snapshot->CollisionSfxCooldownTimer},
            {"invincibility_timer", snapshot->InvincibilityTimer},
            {"floor_type_timer", snapshot->FloorTypeTimer},
            {"previous_floor_type", snapshot->PreviousFloorType},
            {"respawn_damage_state", snapshot->RespawnDamageState},
            {"random_turn_state", snapshot->RandomTurnState},
            {"random_turn_timer", snapshot->RandomTurnTimer},
            {"attention_persistence_counter",
             snapshot->AttentionPersistenceCounter},
            {"fairy_revive_grace_timer",
             snapshot->FairyReviveGraceTimer},
            {"background_check_flags", snapshot->BackgroundCheckFlags},
            {"action_function", snapshot->ActionFunction},
            {"animation_resource", snapshot->AnimationResource},
            {"animation_frame", snapshot->AnimationFrame},
            {"animation_play_speed", snapshot->AnimationPlaySpeed},
            {"animation_mode", snapshot->AnimationMode},
            {"time_state_address", snapshot->TimeStateAddress},
            {"time_state_update_rate", snapshot->TimeStateUpdateRate},
        });
      } else if (snapshot.has_value()) {
        runtime.PlayerTimingEventsTruncated = true;
      }
    }
  }
  if (wholeAotExecutionActive &&
      enabledTopScreenCameraExit && enabledTopScreenItemExit &&
      enabledTopScreenGameplayExit && enabledObjectBankExit && enabledRainExit &&
      (pc != Oot3dNativeGame::kTopScreenInputUpdateBoundary ||
       (runtime.OcarinaText != nullptr && runtime.OcarinaText->NeedsExecution()) ||
       (runtime.PauseTransaction != nullptr && runtime.PauseTransaction->NeedsExecution())) &&
      (pc != kPauseUiDrawReturn ||
       (runtime.ItemDescription != nullptr && runtime.ItemDescription->NeedsExecution())) &&
      std::binary_search(runtime.WholeAotObservableExitPcs.begin(),
                         runtime.WholeAotObservableExitPcs.end(), pc) &&
      questDrawExit()) {
    if (runtime.ObservedExitPc != nullptr)
      *runtime.ObservedExitPc = pc;
#if defined(OOT3D_NATIVE_DIRECT_AOT_PLUGIN)
    Oot3dNativeGame::Oot3dWholeAotPluginObservableExit(pc, state);
#else
    Oot3dNativeGame::Oot3dWholeAotExitAt(pc, state);
#endif
  }
  if (runtime.PlayerTemporalBridge != nullptr &&
      runtime.FrameRatePolicy != nullptr) {
    runtime.PlayerTemporalBridge->ObserveBlockEntry(
        pc, state, memory,
        runtime.FrameRatePolicy->GameplayClock().Context());
  }
  if (runtime.PicaCompositionTracker != nullptr && !compositionBeforeExit) {
    runtime.PicaCompositionTracker->ObserveBlockEntry(pc, state, memory);
  }
  if (runtime.Title != nullptr) {
    Oot3dNativeGame::TitleBlockEntry entry{
        .Pc = pc,
        .State = state,
        .Memory = memory,
        .SceneView = runtime.SceneViewProbe,
        .PendingSceneFrustum = runtime.Projection.PendingSceneFrustum,
        .HostFrame = runtime.CurrentHostFrame};
    runtime.Title->ObserveBlockEntry(entry);
  }
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  if (runtime.ReadProbes != nullptr && runtime.ReadProbes->Watches(pc)) {
    uint32_t registers[16];
    for (size_t index = 0; index < 16U; ++index) registers[index] = state.r[index];
    const auto line = runtime.ReadProbes->Observe(
        pc, runtime.CurrentHostFrame, registers,
        [&](uint32_t address, uint32_t *value) { return memory.Read32(address, value); },
        [&](uint32_t address, uint8_t *bytes, uint32_t size) {
          for (uint32_t i = 0; i < size; ++i)
            if (!memory.Read8(address + i, &bytes[i])) return false;
          return true;
        });
    if (line) {
      std::fprintf(runtime.ReadProbeOut, "%s\n", line->c_str());
      std::fflush(runtime.ReadProbeOut);
    }
  }
#endif
  if (runtime.CallTrace != nullptr &&
      runtime.CurrentHostFrame >= runtime.CallTraceFirstFrame &&
      runtime.CurrentHostFrame <= runtime.CallTraceLastFrame) {
    uint32_t word = 0;
    if (runtime.CallTraceWordAddress != 0U)
      (void)memory.Read32(runtime.CallTraceWordAddress, &word);
    const std::array<uint32_t, 10> record{
        runtime.CurrentHostFrame, pc, state.r[14], state.r[13], state.r[0],
        state.r[1], state.r[2], state.r[3], state.thread_pointer, word};
    std::fwrite(record.data(), sizeof(uint32_t), record.size(), runtime.CallTrace);
  }
  if (!runtime.PcDumpDone && pc == runtime.PcDumpAddress &&
      runtime.CurrentHostFrame >= runtime.PcDumpFrame && runtime.TraceMemory != nullptr) {
    runtime.PcDumpDone = true;
    std::filesystem::create_directories(runtime.PcDumpDirectory);
    if (std::FILE *registers = std::fopen(
            (std::filesystem::path(runtime.PcDumpDirectory) / "registers.txt").string().c_str(), "w")) {
      std::fprintf(registers, "frame %u pc %08x thread_pointer %08x\n", runtime.CurrentHostFrame, pc,
                   state.thread_pointer);
      for (int index = 0; index < 16; ++index)
        std::fprintf(registers, "r%d %08x\n", index, state.r[index]);
      std::fclose(registers);
    }
    for (const auto &[address, size] : runtime.PcDumpRanges) {
      std::vector<uint8_t> bytes(size);
      if (!runtime.TraceMemory->ReadBytes(address, bytes))
        continue;
      char name[32];
      std::snprintf(name, sizeof(name), "%08x.bin", address);
      if (std::FILE *out = std::fopen(
              (std::filesystem::path(runtime.PcDumpDirectory) / name).string().c_str(), "wb")) {
        std::fwrite(bytes.data(), 1, bytes.size(), out);
        std::fclose(out);
      }
    }
  }
  if (runtime.TraceA32Blocks) {
    ++runtime.BlockTraceRecordsObserved;
    if (runtime.BlockTrace.size() == kMaximumA32BlockTraceRecords) {
      runtime.BlockTrace.pop_front();
      runtime.BlockTraceTruncated = true;
    }
    NativeWidescreenProjectionState::A32BlockTraceRecord record;
    record.HostFrame = runtime.CurrentHostFrame;
    record.Pc = pc;
    record.MemoryWriteGeneration =
        runtime.TraceMemory != nullptr
            ? runtime.TraceMemory->WriteGeneration()
            : 0U;
    record.MemoryWriteFingerprint =
        runtime.TraceMemory != nullptr
            ? runtime.TraceMemory->WriteTraceFingerprint()
            : 0U;
    if (runtime.TraceMemory != nullptr) {
      record.FastReadMismatchCount =
          runtime.TraceMemory->FastReadMismatchCount();
      record.LastFastReadMismatchAddress =
          runtime.TraceMemory->LastFastReadMismatchAddress();
      record.LastFastReadMismatchValue =
          runtime.TraceMemory->LastFastReadMismatchValue();
      record.LastCheckedReadMismatchValue =
          runtime.TraceMemory->LastCheckedReadMismatchValue();
    }
    record.Registers = state.r;
    record.Cpsr = state.cpsr;
    record.Fpscr = state.fpscr;
    record.ThreadPointer = state.thread_pointer;
    record.Vfp = state.vfp;
    record.ExclusiveAddress = state.exclusive_address;
    record.ExclusiveToken = state.exclusive_token;
    record.ExclusiveSize = state.exclusive_size;
    record.ExclusiveValid = state.exclusive_valid;
    runtime.BlockTrace.push_back(std::move(record));
  }
  if (runtime.UiLifecycleBridge != nullptr) {
    const auto observation =
        runtime.UiLifecycleBridge->ObserveGuestEntry(pc);
    if (runtime.PicaCompositionDomain != nullptr &&
        Oot3dNativeGame::BeginsNativeGameplayUiPresentation(observation)) {
      *runtime.PicaCompositionDomain =
          Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui;
    }
    if (runtime.TopScreenUiProfile) {
      runtime.UiLifecycleBridge->ApplyTopScreenGuestHook(pc, state.r[0]);
    }
  }
  // The file select's update is the only sign that the title scene on the
  // upper screen is the file select's (the title demo's PlayState keeps
  // updating behind its cards): the scene view takes its identity from it
  // while this keeps running. PauseUi_Update calls it every frame of play as
  // well (the integrated file-select flow of the touch screen), so only a
  // frontend SaveContext.game_mode makes it the file select's screen.
  if (pc == kFileSelectUpdate && runtime.SceneViewProbe != nullptr &&
      runtime.TraceMemory != nullptr) {
    uint32_t nativeGameMode = 0U;
    if (runtime.TraceMemory->Read32(kNativeGameMode, &nativeGameMode) &&
        !Oot3dNativeGame::IsTopScreenGameplayInputMode(nativeGameMode)) {
      runtime.SceneViewProbe->ObserveFileSelect();
    }
  }
  if (runtime.TopScreenUiProfile && runtime.TraceMemory != nullptr &&
      runtime.UiLifecycleBridge != nullptr && pc == kPauseProjectionPrepare &&
      state.r[14] == kPauseProjectionPrepareCall + 4U) {
    const auto captureProjection = [&]() {
      nlohmann::json snapshot = nlohmann::json::object();
      uint32_t owner = 0U;
      uint32_t extents = 0U;
      uint32_t materialized = 0U;
      uint32_t offset = 0U;
      uint32_t extentA = 0U;
      uint32_t extentB = 0U;
      uint32_t materializedA = 0U;
      uint32_t materializedB = 0U;
      uint32_t offsetX = 0U;
      uint32_t offsetY = 0U;
      const bool rootAvailable =
          runtime.TraceMemory->Read32(0x004FDA84U, &owner);
      const bool ownerAvailable =
          rootAvailable && owner != 0U &&
          runtime.TraceMemory->Read32(owner + 0x0CU, &extents) &&
          runtime.TraceMemory->Read32(owner + 0x10U, &materialized) &&
          runtime.TraceMemory->Read32(owner + 0x1CU, &offset);
      const bool extentsAvailable =
          ownerAvailable && extents != 0U &&
          runtime.TraceMemory->Read32(extents, &extentA) &&
          runtime.TraceMemory->Read32(extents + 0x24U, &extentB);
      const bool materializedAvailable =
          ownerAvailable && materialized != 0U &&
          runtime.TraceMemory->Read32(materialized, &materializedA) &&
          runtime.TraceMemory->Read32(materialized + 0x24U, &materializedB);
      const bool offsetAvailable =
          ownerAvailable && offset != 0U &&
          runtime.TraceMemory->Read32(offset, &offsetX) &&
          runtime.TraceMemory->Read32(offset + 4U, &offsetY);
      snapshot = {
          {"owner", owner},
          {"extents", extents},
          {"materialized", materialized},
          {"offset", offset},
          {"extent_a_bits", extentsAvailable ? extentA : 0U},
          {"extent_b_bits", extentsAvailable ? extentB : 0U},
          {"materialized_a_bits", materializedAvailable ? materializedA : 0U},
          {"materialized_b_bits", materializedAvailable ? materializedB : 0U},
          {"offset_x_bits", offsetAvailable ? offsetX : 0U},
          {"offset_y_bits", offsetAvailable ? offsetY : 0U},
      };
      return snapshot;
    };
    nlohmann::json before = nullptr;
    if (runtime.CollectExtendedDiagnostics &&
        runtime.TopScreenPauseProjectionEvents.size() < 64U) {
      before = captureProjection();
    }
    ++runtime.TopScreenPauseProjectionCalls;
    std::string projectionError;
    const bool applied = Oot3dNativeGame::ApplyTopScreenPauseProjection(
        *runtime.TraceMemory,
        runtime.UiLifecycleBridge->TopScreenPauseProjection(),
        &projectionError, &runtime.TopScreenConfig);
    if (!applied) {
      ++runtime.TopScreenPauseProjectionFailures;
    }
    if (runtime.CollectExtendedDiagnostics &&
        runtime.TopScreenPauseProjectionEvents.size() < 64U) {
      runtime.TopScreenPauseProjectionEvents.push_back(
          {{"host_frame", runtime.CurrentHostFrame},
           {"return_address", state.r[14]},
           {"applied", applied},
           {"error", projectionError},
           {"before", std::move(before)},
           {"after", captureProjection()}});
    }
  }
  constexpr uint32_t kPauseRendererSetTarget = 0x00300588U;
  constexpr uint32_t kGlViewport = 0x002FEABCU;
  constexpr uint32_t kGlBindFramebuffer = 0x00311364U;
  if (runtime.TopScreenUiProfile && runtime.TraceMemory != nullptr) {
    if (pc == kPauseIconBuild) {
      Oot3dNativeGame::TopScreenPauseIconBuild build{state.r[2], state.r[3],
                                                     0U, 0U};
      if (runtime.TraceMemory->IsWritable(state.r[13], 8U) &&
          runtime.TraceMemory->Read32(state.r[13], &build.StackArgument0) &&
          runtime.TraceMemory->Read32(state.r[13] + 4U,
                                      &build.StackArgument1) &&
          Oot3dNativeGame::ResolveTopScreenPauseIconBuild(
              &build, runtime.TopScreenConfig)) {
        state.r[2] = build.Argument2;
        state.r[3] = build.Argument3;
        runtime.TraceMemory->Write32(state.r[13], build.StackArgument0);
        runtime.TraceMemory->Write32(state.r[13] + 4U, build.StackArgument1);
      }
    } else if (pc == kPauseRendererSetTarget) {
      runtime.PendingTopScreenPauseTarget.reset();
      runtime.PendingTopScreenPauseRenderer = 0U;
      uint8_t enabled = 0U;
      uint8_t halfHeight = 0U;
      if (state.r[0] != 0U &&
          runtime.TraceMemory->Read8(state.r[0] + 4U, &enabled) &&
          runtime.TraceMemory->Read8(state.r[0] + 0x75U, &halfHeight)) {
        Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
        bool routeActive = false;
        if (state.r[1] == 0x400U && runtime.SystemPage != nullptr)
          runtime.SystemPage->Prepare(false);
        if (Oot3dNativeGame::ReadTopScreenPauseDrawInputs(*runtime.TraceMemory,
                                                          &inputs) &&
            runtime.TopScreenPauseTargetRoute != nullptr) {
          ++runtime.TopScreenPauseTargetObservations;
          routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
              inputs, runtime.TopScreenPauseTargetRoute);
          runtime.HasTopScreenPauseDrawInputs = true;
          runtime.LastTopScreenPauseDrawInputs = inputs;
          runtime.LastTopScreenPauseRouteActive = routeActive;
          Oot3dNativeGame::ObserveTopScreenPauseTargetCommand(
              inputs, state.r[1], &runtime.TopScreenPauseDrawRouting);
          if (state.r[1] == 0x400U && runtime.SystemPage != nullptr) {
            bool redraw = false;
            if (enabled != 0U && inputs.ChildStates[3] != 0U && !routeActive) {
              Oot3dNativeGame::TopScreenPausePageRedrawInputs pageInputs;
              const bool sceneLatch = runtime.TopScreenTouchCoordinateRoute != nullptr &&
                                     runtime.TopScreenTouchCoordinateRoute->RuntimeSceneLatch;
              auto routing = runtime.TopScreenPauseDrawRouting;
              if (!Oot3dNativeGame::ReadTopScreenPausePageRedrawInputs(
                      *runtime.TraceMemory, sceneLatch, &pageInputs))
                throw std::runtime_error("TopScreen System cannot read redraw inputs");
              redraw = Oot3dNativeGame::ResolveTopScreenPauseDrawAction(inputs, &routing) ==
                           Oot3dNativeGame::TopScreenPauseDrawAction::SuppressNativeChildren &&
                       Oot3dNativeGame::ResolveTopScreenPausePageRedraw(
                           pageInputs, &runtime.TopScreenPausePageRedraw);
            }
            runtime.SystemPage->Prepare(redraw);
          }
        }
        if (state.r[1] == 0x400U && runtime.UiLifecycleBridge != nullptr)
          runtime.UiLifecycleBridge->SetTopScreenSystemPagePresentation(inputs.ChildStates[3] != 0U);
        const Oot3dNativeGame::TopScreenPauseTargetCommand command{
            enabled != 0U, state.r[1], halfHeight != 0U};
        auto plan = Oot3dNativeGame::ResolveTopScreenPauseCompositionTargetCommand(
            command, routeActive,
            runtime.TopScreenPausePageRedrawActive && inputs.AnyPageGateActive,
            inputs.ChildStates[3] != 0U);
        const auto frontendPlan =
            Oot3dNativeGame::ResolveTopScreenFrontendTargetCommand(
                command,
                runtime.UiLifecycleBridge != nullptr &&
                    runtime.UiLifecycleBridge->NativeFrontendPresentationActive() &&
                    // Reframed draws the file interface into the upper pass
                    // itself; the lower targets stay native.
                    !Oot3dNativeGame::TopScreenReframedFileSelect::Active(*runtime.TraceMemory));
        if (frontendPlan.Handled) {
          plan = frontendPlan;
        } else {
          Oot3dNativeGame::TopScreenWidePageState page;
          Oot3dNativeGame::WidenTopScreenPauseTargetForPage(
              plan, Oot3dNativeGame::ReadTopScreenWidePageState(
                        *runtime.TraceMemory, &page) && page.Wide());
        }
        if (plan.Handled && plan.BindTopTarget &&
            !Oot3dNativeGame::TopScreenReframedFileSelect::Active(*runtime.TraceMemory)) {
          if (state.r[1] == 0x401U)
            runtime.TopScreenLowerMenuTouchSurface = Oot3dNativeGame::TopScreenTouchSurfaceForViewport(
                static_cast<int32_t>(plan.ViewportX), static_cast<int32_t>(plan.ViewportY),
                static_cast<int32_t>(plan.ViewportWidth), static_cast<int32_t>(plan.ViewportHeight),
                halfHeight != 0U ? 240 : 480);
          runtime.PendingTopScreenPauseTarget = plan;
          runtime.PendingTopScreenPauseRenderer = state.r[0];
          ++runtime.TopScreenPauseTargetCommands;
        }
        // 3.1 draws the Visions lists (0x005D5698) and the Game Over choices
        // (compositor 0x005D3D30) centred at (0,40,480,320) on the upper
        // target: touches on them map through that rectangle.
        if (Oot3dNativeGame::IsTopScreenVisionsListPresentation(inputs) ||
            Oot3dNativeGame::IsTopScreenGameOverPresentation(inputs)) {
          const auto &viewport = Oot3dNativeGame::kTopScreenCentredLowerViewport;
          runtime.TopScreenLowerMenuTouchSurface = Oot3dNativeGame::TopScreenTouchSurfaceForViewport(
              viewport[0], viewport[1], viewport[2], viewport[3], 480);
        }
      }
    } else if (pc == kGlBindFramebuffer &&
               runtime.PendingTopScreenPauseTarget.has_value()) {
      uint32_t framebuffer = 0U;
      const auto &plan = *runtime.PendingTopScreenPauseTarget;
      if (runtime.TraceMemory->Read32(runtime.PendingTopScreenPauseRenderer +
                                          plan.FramebufferBindingOffset,
                                      &framebuffer)) {
        state.r[1] = framebuffer;
      }
    } else if (pc == kGlViewport &&
               runtime.PendingTopScreenPauseTarget.has_value()) {
      const auto plan = *runtime.PendingTopScreenPauseTarget;
      state.r[0] = plan.ViewportX;
      state.r[1] = plan.ViewportY;
      state.r[2] = plan.ViewportWidth;
      state.r[3] = plan.ViewportHeight;
      runtime.PendingTopScreenPauseTarget.reset();
      runtime.PendingTopScreenPauseRenderer = 0U;
      ++runtime.TopScreenPauseTargetRewrites;
    } else if (pc == kPauseUiDraw && state.r[14] == kPauseUiDrawReturn &&
               !runtime.PendingTopScreenPauseChildRestore.has_value()) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (Oot3dNativeGame::ReadTopScreenPauseDrawInputs(*runtime.TraceMemory,
                                                        &inputs)) {
        runtime.HasTopScreenPauseDrawInputs = true;
        runtime.LastTopScreenPauseDrawInputs = inputs;
        runtime.TopScreenPauseDrawRouting.NativeSceneRequested =
            runtime.WideItems != nullptr && runtime.WideItems->ViewModelActive();
        const auto action = Oot3dNativeGame::ResolveTopScreenPauseDrawAction(
            inputs, &runtime.TopScreenPauseDrawRouting);
        runtime.LastTopScreenPauseChildrenSuppressed =
            action ==
            Oot3dNativeGame::TopScreenPauseDrawAction::SuppressNativeChildren;
        if (runtime.ItemDescription != nullptr) runtime.ItemDescription->DisarmPageDraw();
        if (!runtime.LastTopScreenPauseChildrenSuppressed) {
          runtime.TopScreenPausePageRedrawActive = false;
          if (runtime.UiLifecycleBridge != nullptr) {
            runtime.UiLifecycleBridge
                ->SetTopScreenPausePageRedrawEdgePresentation(std::nullopt);
          }
          ++runtime.TopScreenPauseChildNativeDraws;
        }
        if (runtime.LastTopScreenPauseChildrenSuppressed) {
          ++runtime.TopScreenPausePageCompositionCalls;
          Oot3dNativeGame::TopScreenPausePageRedrawInputs pageInputs;
          const bool runtimeSceneLatch =
              runtime.TopScreenTouchCoordinateRoute != nullptr &&
              runtime.TopScreenTouchCoordinateRoute->RuntimeSceneLatch;
          const bool systemPage = inputs.ChildStates[3] != 0U;
          const bool pageRedraw = systemPage
              ? runtime.SystemPage != nullptr && runtime.SystemPage->Redraw()
              : Oot3dNativeGame::ReadTopScreenPausePageRedrawInputs(
                  *runtime.TraceMemory, runtimeSceneLatch, &pageInputs) &&
              Oot3dNativeGame::ResolveTopScreenPausePageRedraw(
                  pageInputs, &runtime.TopScreenPausePageRedraw);
          runtime.TopScreenPausePageRedrawActive = pageRedraw;
          if (pageRedraw && !systemPage && runtime.ItemDescription != nullptr)
            runtime.ItemDescription->ArmPageDraw();
          if (runtime.UiLifecycleBridge != nullptr) {
            Oot3dNativeGame::TopScreenWidePageState page;
            const bool dungeonMap =
                pageRedraw && Oot3dNativeGame::ReadTopScreenWidePageState(
                                  *runtime.TraceMemory, &page) &&
                page.DungeonMapActive;
            runtime.UiLifecycleBridge
                ->SetTopScreenPausePageRedrawEdgePresentation(
                    pageRedraw && !systemPage
                        ? std::optional(
                              Oot3dNativeGame::BuildTopScreenPauseEdgeGeometry(
                                  pageInputs.Pause.HasScene,
                                  pageInputs.Pause.SceneMode,
                                  pageInputs.Pause.RuntimeMode, 0U, 0, 1.0F,
                                  1.0F, pageInputs.Pause.ChildStates[1] != 0U,
                                  dungeonMap))
                        : std::nullopt);
          }
          const auto suppressionSet =
              pageRedraw && !systemPage
                  ? Oot3dNativeGame::TopScreenPauseChildSuppressionSet::
                        PageRedraw
                  : Oot3dNativeGame::TopScreenPauseChildSuppressionSet::Tail;
          Oot3dNativeGame::TopScreenPauseChildState saved;
          if (Oot3dNativeGame::BeginTopScreenPauseChildSuppression(
                  *runtime.TraceMemory, suppressionSet, &saved)) {
            runtime.PendingTopScreenPauseChildRestore = saved;
            ++runtime.TopScreenPauseChildSuppressions;
            if (pageRedraw) {
              ++runtime.TopScreenPausePageRedraws;
            } else {
              ++runtime.TopScreenPausePageRedrawSkips;
            }
          }
        }
      } else if (runtime.UiLifecycleBridge != nullptr) {
        runtime.UiLifecycleBridge->SetTopScreenPausePageRedrawEdgePresentation(
            std::nullopt);
      }
    }
  }
  if (pc == Oot3dNativeGame::kOot3dFramePacingBeginEntry &&
      runtime.FrameRatePolicy != nullptr) {
    runtime.FrameRatePolicy->BeginFramePacing(state.r[0]);
  }
  if (pc == kGameStateUpdate && runtime.FrameRatePolicy != nullptr &&
      !runtime.FrameRatePolicy->ApplyNativeTimeScaleAtGameStateUpdate(
          state.r[0], memory)) {
    throw std::runtime_error("native frame-rate policy failed: " +
                             runtime.FrameRatePolicy->LastError());
  }
  if (pc == Oot3dNativeGame::kOot3dFramePacingCommitEntry &&
      runtime.FrameRatePolicy != nullptr &&
      !runtime.FrameRatePolicy->ApplyFramePacingInterval(state.r[4], memory)) {
    throw std::runtime_error("native frame-pacing policy failed: " +
                             runtime.FrameRatePolicy->LastError());
  }
  if (pc == Oot3dNativeGame::kOot3dFramePacingDecisionEntry &&
      runtime.FrameRatePolicy != nullptr &&
      !runtime.FrameRatePolicy->ApplyFramePacingDeadline(state.r[4], memory)) {
    throw std::runtime_error("native frame-pacing deadline policy failed: " +
                             runtime.FrameRatePolicy->LastError());
  }
  if (runtime.ProfileA32Blocks) {
    ++runtime.BlockEntries;
    runtime.BlockSampleState =
        runtime.BlockSampleState * 1664525U + 1013904223U;
    if ((runtime.BlockSampleState & 63U) == 0U) {
      ++runtime.BlockSamples[pc];
    }
  }
  if (runtime.CollectExtendedDiagnostics &&
      (pc == kGameStateUpdate || pc == kFileChooseInit ||
       pc == kPlayTransitionUpdate || pc == kFileSelectActivate)) {
    const auto read32 = [&](uint32_t address) {
      uint32_t value = 0;
      memory.Read32(address, &value);
      return value;
    };
    if (pc == kFileSelectActivate) {
      runtime.GameStateEvents.push_back({
          {"host_frame", runtime.CurrentHostFrame},
          {"function", "FileSelect_Activate"},
          {"argument", state.r[0]},
          {"controller_state_before", read32(kFileSelectState + 0x10U)},
          {"return_address", state.r[14]},
      });
      if (runtime.GameStateEvents.size() > 128U) {
        runtime.GameStateEvents.erase(runtime.GameStateEvents.begin());
      }
      return;
    }
    const uint32_t gameState = state.r[0];
    const auto read8 = [&](uint32_t address) {
      uint8_t value = 0;
      memory.Read8(address, &value);
      return value;
    };
    const uint32_t main = read32(gameState + 0x04U);
    const uint32_t destroy = read32(gameState + 0x08U);
    const uint32_t nextInit = read32(gameState + 0x0CU);
    const uint32_t nextSize = read32(gameState + 0x10U);
    const uint8_t running = read8(gameState + 0x101U);
    const uint32_t nativeGameMode = read32(kNativeGameMode);
    const uint8_t transitionTrigger = read8(gameState + 0x5C2DU);
    const uint8_t transitionType = read8(gameState + 0x5C76U);
    const uint8_t transitionState = read8(gameState + 0x7F12U);
    const bool changed = !runtime.HasGameStateSnapshot ||
                         gameState != runtime.LastGameStateAddress ||
                         main != runtime.LastGameStateMain ||
                         nextInit != runtime.LastNextGameStateInit ||
                         running != runtime.LastGameStateRunning ||
                         nativeGameMode != runtime.LastNativeGameMode ||
                         transitionTrigger != runtime.LastTransitionTrigger ||
                         transitionType != runtime.LastTransitionType ||
                         transitionState != runtime.LastTransitionState;
    if (pc == kFileChooseInit || changed) {
      const char *function = pc == kGameStateUpdate  ? "GameState_Update"
                             : pc == kFileChooseInit ? "FileChoose_Init"
                                                     : "PlayTransition_Update";
      runtime.GameStateEvents.push_back({
          {"host_frame", runtime.CurrentHostFrame},
          {"function", function},
          {"state", gameState},
          {"main", main},
          {"destroy", destroy},
          {"next_init", nextInit},
          {"next_size", nextSize},
          {"running", running},
          {"native_game_mode", nativeGameMode},
          {"transition_trigger", transitionTrigger},
          {"transition_type", transitionType},
          {"transition_state", transitionState},
          {"return_address", state.r[14]},
      });
      if (runtime.GameStateEvents.size() > 128U) {
        runtime.GameStateEvents.erase(runtime.GameStateEvents.begin());
      }
    }
    runtime.GameStateUpdateCalls += pc == kGameStateUpdate ? 1U : 0U;
    runtime.LastGameStateAddress = gameState;
    runtime.LastGameStateMain = main;
    runtime.LastNextGameStateInit = nextInit;
    runtime.LastGameStateRunning = running;
    runtime.LastNativeGameMode = nativeGameMode;
    runtime.LastTransitionTrigger = transitionTrigger;
    runtime.LastTransitionType = transitionType;
    runtime.LastTransitionState = transitionState;
    runtime.HasGameStateSnapshot = true;
  }
  if (runtime.CollectExtendedDiagnostics &&
      (pc == kPauseUiUpdate || pc == kPauseUiDraw || pc == kFileSelectUpdate ||
       pc == kFileSelectInit)) {
    const char *function = pc == kPauseUiUpdate      ? "PauseUi_Update"
                           : pc == kPauseUiDraw      ? "PauseUi_Draw"
                           : pc == kFileSelectUpdate ? "FileSelect_Update"
                                                     : "FileSelect_Init";
    ++runtime.UiLifecycleCallCounts[function];
    runtime.UiLifecycleEvents.push_back({
        {"host_frame", runtime.CurrentHostFrame},
        {"function", function},
        {"argument_0", state.r[0]},
        {"argument_1", state.r[1]},
        {"return_address", state.r[14]},
        {"thread_pointer", state.thread_pointer},
    });
    if (runtime.UiLifecycleEvents.size() > 256U) {
      runtime.UiLifecycleEvents.erase(runtime.UiLifecycleEvents.begin());
    }
    return;
  }
  if (runtime.CollectExtendedDiagnostics &&
      (pc == kBlockingQueuePop || pc == kBlockingQueuePush ||
       pc == kRendererCommandQueueProcessPending ||
       pc == kBlockingQueueTryPush || pc == kTaskQueueTakeFirstFromPriority ||
       pc == kTaskQueueEnqueuePriorityNode ||
       pc == kTaskQueueWorkerDrainPriorityNodes || pc == kNngxP3dCallback ||
       pc == kNngxCommonInterruptHandler ||
       pc == kNngxCmdlistCompletionDispatch ||
       pc == kRendererGlobalCallbackAfterLookup)) {
    const char *function =
        pc == kBlockingQueuePop    ? "BlockingQueue_Pop"
        : pc == kBlockingQueuePush ? "BlockingQueue_Push"
        : pc == kRendererCommandQueueProcessPending
            ? "RendererCommandQueue_ProcessPending"
        : pc == kBlockingQueueTryPush ? "BlockingQueue_TryPush"
        : pc == kTaskQueueTakeFirstFromPriority
            ? "TaskQueue_TakeFirstFromPriority"
        : pc == kTaskQueueEnqueuePriorityNode ? "TaskQueue_EnqueuePriorityNode"
        : pc == kTaskQueueWorkerDrainPriorityNodes
            ? "TaskQueueWorker_DrainPriorityNodes"
        : pc == kNngxP3dCallback            ? "nngx_P3DCallback"
        : pc == kNngxCommonInterruptHandler ? "nngx_CommonInterruptHandler"
        : pc == kNngxCmdlistCompletionDispatch
            ? "nngx_CmdlistCompletionDispatch"
            : "RendererGlobalCallback_AfterLookup";
    nlohmann::json argumentWords = nlohmann::json::array();
    if (pc == kTaskQueueEnqueuePriorityNode) {
      for (uint32_t offset = 0; offset < 0x20U; offset += 4U) {
        uint32_t value = 0;
        if (!memory.Read32(state.r[1] + offset, &value)) {
          break;
        }
        argumentWords.push_back(value);
      }
    }
    uint32_t queue = state.r[0];
    uint32_t argument = state.r[1];
    if (pc == kNngxP3dCallback || pc == kNngxCommonInterruptHandler) {
      uint32_t commandList = 0;
      uint32_t callback = 0;
      memory.Read32(kNngxGlobalState + 0xA0U, &commandList);
      if (commandList != 0U) {
        memory.Read32(commandList + 0x30U, &callback);
        for (uint32_t offset = 0; offset < 0x38U; offset += 4U) {
          uint32_t value = 0;
          if (!memory.Read32(commandList + offset, &value)) {
            break;
          }
          argumentWords.push_back(value);
        }
      }
      queue = commandList;
      argument = callback;
    } else if (pc == kNngxCmdlistCompletionDispatch) {
      uint32_t owner = 0;
      uint32_t vtable = 0;
      uint32_t callback = 0;
      memory.Read32(kNngxCmdlistOwnerState + 0x14U, &owner);
      if (owner != 0U && memory.Read32(owner, &vtable)) {
        memory.Read32(vtable + 0x1CU, &callback);
        for (uint32_t offset = 0; offset < 0x30U; offset += 4U) {
          uint32_t value = 0;
          if (!memory.Read32(owner + offset, &value)) {
            break;
          }
          argumentWords.push_back(value);
        }
      }
      queue = owner;
      argument = callback;
    } else if (pc == kRendererGlobalCallbackAfterLookup) {
      uint32_t callback = 0;
      uint32_t callbackArgument = 0;
      if (state.r[0] != 0U) {
        memory.Read32(state.r[0] + 0x10U, &callback);
        memory.Read32(state.r[0] + 0x14U, &callbackArgument);
      }
      queue = state.r[0];
      argument = callback;
      argumentWords.push_back(callbackArgument);
    }
    runtime.RendererQueueEvents.push_back({
        {"host_frame", runtime.CurrentHostFrame},
        {"function", function},
        {"thread_pointer", state.thread_pointer},
        {"queue", queue},
        {"argument", argument},
        {"return_address", state.r[14]},
        {"argument_words", std::move(argumentWords)},
    });
    ++runtime.RendererQueueCallCounts[function];
    if (runtime.RendererQueueEvents.size() > 512U) {
      runtime.RendererQueueEvents.erase(runtime.RendererQueueEvents.begin());
    }
    return;
  }
  if (pc == kEnMagInit || pc == kEnMagUpdate || pc == kEnMagDraw) {
    uint32_t &callCount = pc == kEnMagInit     ? runtime.EnMagInitCalls
                          : pc == kEnMagUpdate ? runtime.EnMagUpdateCalls
                                               : runtime.EnMagDrawCalls;
    ++callCount;

    const uint32_t actor = state.r[0];
    const auto read16 = [&](uint32_t offset) {
      uint16_t value = 0;
      return memory.Read16(actor + offset, &value) ? value : 0xFFFFU;
    };
    const auto read32 = [&](uint32_t offset) {
      uint32_t value = 0;
      return memory.Read32(actor + offset, &value) ? value : 0xFFFFFFFFU;
    };
    const auto readFloat = [&](uint32_t offset) {
      return std::bit_cast<float>(read32(offset));
    };
    nlohmann::json event{
        {"host_frame", runtime.CurrentHostFrame},
        {"function", pc == kEnMagInit     ? "EnMag_Init"
                     : pc == kEnMagUpdate ? "EnMag_Update"
                                          : "EnMag_Draw"},
        {"call", callCount},
        {"actor", actor},
        {"handle_main", read32(0x1A4U)},
        {"handle_title", read32(0x1A8U)},
        {"handle_copyright", read32(0x1ACU)},
        {"delay_timer", read16(0x1C0U)},
        {"substate", read16(0x1C4U)},
        {"timer", read16(0x1C6U)},
        {"state", read16(0x1C8U)},
        {"title_alpha", readFloat(0x1D0U)},
        {"main_alpha", readFloat(0x1D4U)},
        {"copyright_alpha", readFloat(0x1D8U)},
        {"effect_alpha", readFloat(0x1DCU)},
    };
    if (pc == kEnMagDraw) {
      nlohmann::json queueWords = nlohmann::json::array();
      for (uint32_t offset = 0; offset < 0x80U; offset += 4U) {
        uint32_t value = 0;
        if (!memory.Read32(kRendererCommandQueue + offset, &value)) {
          break;
        }
        queueWords.push_back(value);
      }
      event["renderer_command_queue_address"] = kRendererCommandQueue;
      event["renderer_command_queue_words"] = std::move(queueWords);
    }
    if (runtime.EnMagEvents.size() == 256U) {
      runtime.EnMagEvents.erase(runtime.EnMagEvents.begin());
    }
    runtime.EnMagEvents.push_back(std::move(event));
    return;
  }
  auto &projection = runtime.Projection;
  if (pc != projection.PerspectiveFrustumEntry &&
      pc != kMtx4x4BuildOrthographicProjectionRotated) {
    return;
  }
  const float left = std::bit_cast<float>(state.vfp[0]);
  const float right = std::bit_cast<float>(state.vfp[1]);
  if (!std::isfinite(left) || !std::isfinite(right) || left == right) {
    return;
  }
  auto &callsite = projection.Callsites[{pc, state.r[14]}];
  if (callsite.Calls == 0) {
    callsite.FirstLeft = left;
    callsite.FirstRight = right;
  }
  ++callsite.Calls;
  if (pc == kMtx4x4BuildOrthographicProjectionRotated) {
    ++projection.OrthographicCalls;
    return;
  }
  const float bottom = std::bit_cast<float>(state.vfp[2]);
  const float top = std::bit_cast<float>(state.vfp[3]);
  const float nearPlane = std::bit_cast<float>(state.vfp[4]);
  const float farPlane = std::bit_cast<float>(state.vfp[5]);
  callsite.LastFrustum = {left, right, bottom, top, nearPlane, farPlane};
  const auto resolved = Fast::Extension::ResolvePerspectiveFov({
      {left, right, bottom, top, nearPlane, farPlane},
      projection.FovMultiplier,
      projection.Presentation,
      true,
  });
  const float resolvedLeft = resolved.Frustum.Left;
  const float resolvedRight = resolved.Frustum.Right;
  if (resolvedLeft != left || resolvedRight != right ||
      resolved.Frustum.Bottom != bottom || resolved.Frustum.Top != top) {
    state.vfp[0] = std::bit_cast<uint32_t>(resolvedLeft);
    state.vfp[1] = std::bit_cast<uint32_t>(resolvedRight);
    state.vfp[2] = std::bit_cast<uint32_t>(resolved.Frustum.Bottom);
    state.vfp[3] = std::bit_cast<uint32_t>(resolved.Frustum.Top);
    ++projection.FrustumCalls;
  }
  if (!projection.FrustumIsSceneView) {
    projection.PendingSceneFrustum = NativeSceneProjectionState::PendingFrustum{
        pc, state.r[14], resolvedLeft, resolvedRight, resolved.Frustum.Bottom,
        resolved.Frustum.Top, resolved.Frustum.NearPlane,
        resolved.Frustum.FarPlane};
  } else if (runtime.SceneViewProbe != nullptr) {
    runtime.SceneViewProbe->ObservePerspective(
        pc, state.r[14], resolvedLeft, resolvedRight,
        resolved.Frustum.Bottom, resolved.Frustum.Top,
        resolved.Frustum.NearPlane, resolved.Frustum.FarPlane);
  }
}

} // namespace Oot3dA32Window
