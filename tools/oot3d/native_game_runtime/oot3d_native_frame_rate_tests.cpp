#include "oot3d_native_frame_rate.h"
#include "oot3d_native_presentation_pacer.h"

#include "a32_runtime.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

class TestMemoryBus final : public oot3d::recomp::a32::MemoryBus {
  public:
    bool Read32(uint32_t address, uint32_t* value) override {
        if (value == nullptr) {
            return false;
        }
        if (address == Oot3dNativeGame::kOot3dVsyncCounterAddress) {
            *value = VsyncCounter;
            return true;
        }
        if (address == Oot3dNativeGame::kOot3dTimeStatePointerAddress) {
            *value = TimeStateAddress;
            return true;
        }
        if (address + sizeof(uint32_t) > Bytes.size()) {
            return false;
        }
        *value = static_cast<uint32_t>(Bytes[address]) |
                 (static_cast<uint32_t>(Bytes[address + 1U]) << 8U) |
                 (static_cast<uint32_t>(Bytes[address + 2U]) << 16U) |
                 (static_cast<uint32_t>(Bytes[address + 3U]) << 24U);
        return true;
    }

    bool Write32(uint32_t address, uint32_t value) override {
        if (address == Oot3dNativeGame::kOot3dVsyncCounterAddress) {
            VsyncCounter = value;
            return true;
        }
        if (address == Oot3dNativeGame::kOot3dTimeStatePointerAddress) {
            TimeStateAddress = value;
            return true;
        }
        if (address + sizeof(uint32_t) > Bytes.size()) {
            return false;
        }
        for (uint32_t index = 0; index < sizeof(uint32_t); ++index) {
            Bytes[address + index] =
                static_cast<uint8_t>(value >> (index * 8U));
        }
        return true;
    }

    std::array<uint8_t, 0x400U> Bytes{};
    uint32_t VsyncCounter = 0;
    uint32_t TimeStateAddress = 0x40U;
};

void ExpectNear(double actual, double expected, const char* role) {
    if (std::abs(actual - expected) > 1.0e-12) {
        throw std::runtime_error(role);
    }
}

} // namespace

void RunNativeFrameRateTests() {
    using namespace std::chrono_literals;
    using Oot3dNativeGame::NativePacerDeadlineAction;
    if (Oot3dNativeGame::ResolveNativePacerDeadlineAction(-1ns, 16666667ns) !=
            NativePacerDeadlineAction::Wait ||
        Oot3dNativeGame::ResolveNativePacerDeadlineAction(8ms, 16666667ns) !=
            NativePacerDeadlineAction::CarryDebt ||
        Oot3dNativeGame::ResolveNativePacerDeadlineAction(34ms, 16666667ns) !=
            NativePacerDeadlineAction::Resync ||
        Oot3dNativeGame::ResolveNativePacerDeadlineAction(200ms, 16666667ns) !=
            NativePacerDeadlineAction::Resync ||
        Oot3dNativeGame::ResolveNativePacerDeadlineAction(
            200ms, 16666667ns, 15U) != NativePacerDeadlineAction::CarryDebt ||
        Oot3dNativeGame::ResolveNativePacerDeadlineAction(
            300ms, 16666667ns, 15U) != NativePacerDeadlineAction::Resync) {
      throw std::runtime_error(
          "presentation pacer bounded-debt policy mismatch");
    }

    const Oot3dNativeGame::NativeFrameRateContract defaultContract;
    if (defaultContract.Mode !=
            Oot3dNativeGame::GameplayTimingMode::Native30Interpolated ||
        defaultContract.SimulationRateHz !=
            Oot3dNativeGame::kOot3dOriginalSimulationRateHz ||
        defaultContract.PresentationRateHz != 60U ||
        defaultContract.A32UpdateRate !=
            Oot3dNativeGame::kOot3dOriginalUpdateRate ||
        !defaultContract.VisualInterpolationAllowed) {
        throw std::runtime_error(
            "default frame-rate contract must preserve native gameplay timing");
    }
    ExpectNear(defaultContract.StepSeconds, 1.0 / 30.0,
               "default frame-rate step mismatch");
    ExpectNear(defaultContract.NativeStepScale, 1.0,
               "default frame-rate scale mismatch");
    ExpectNear(defaultContract.LogicalFrameDelta, 1.0,
               "default logical-frame delta mismatch");

    Oot3dNativeGame::GameplayTimingMode parsedMode{};
    if (!Oot3dNativeGame::ParseGameplayTimingMode(
            "native30_interpolated", &parsedMode) ||
        parsedMode !=
            Oot3dNativeGame::GameplayTimingMode::Native30Interpolated ||
        !Oot3dNativeGame::ParseGameplayTimingMode("enhanced60",
                                                  &parsedMode) ||
        parsedMode != Oot3dNativeGame::GameplayTimingMode::Enhanced60 ||
        Oot3dNativeGame::ParseGameplayTimingMode("arbitrary120",
                                                 &parsedMode)) {
        throw std::runtime_error("gameplay timing mode parsing mismatch");
    }

    const auto native30 =
        Oot3dNativeGame::ResolveNativeFrameRateContract(
            Oot3dNativeGame::GameplayTimingMode::Native30Interpolated, 60);
    if (!native30.A32UpdateRateExact || native30.A32UpdateRate != 2 ||
        !native30.VisualInterpolationAllowed ||
        !Oot3dNativeGame::ShouldUseNativeVisualInterpolation(native30, true) ||
        Oot3dNativeGame::ShouldUseNativeVisualInterpolation(native30, false)) {
        throw std::runtime_error("native 30 Hz frame-rate contract mismatch");
    }
    ExpectNear(native30.StepSeconds, 1.0 / 30.0, "native 30 Hz step mismatch");
    ExpectNear(native30.NativeStepScale, 1.0, "native 30 Hz scale mismatch");

    const auto native30Direct =
        Oot3dNativeGame::ResolveNativeFrameRateContract(
            Oot3dNativeGame::GameplayTimingMode::Native30NoInterpolation,
            120);
    if (native30Direct.VisualInterpolationAllowed ||
        Oot3dNativeGame::ShouldUseNativeVisualInterpolation(native30Direct,
                                                            true)) {
        throw std::runtime_error(
            "native 30 Hz diagnostic mode enabled visual interpolation");
    }

    ExpectNear(
        Oot3dNativeGame::ResolveDelayedVisualSampleAlpha(true, 1.0F), 0.0,
        "new visual frame must begin at the previous logical sample");
    ExpectNear(
        Oot3dNativeGame::ResolveDelayedVisualSampleAlpha(false, 0.5F), 0.5,
        "repeated visual frame did not advance the delayed transition");
    ExpectNear(
        Oot3dNativeGame::ResolveDelayedVisualSampleAlpha(false, -1.0F), 0.0,
        "delayed visual interpolation did not clamp its lower bound");
    ExpectNear(
        Oot3dNativeGame::ResolveDelayedVisualSampleAlpha(false, 2.0F), 1.0,
        "delayed visual interpolation did not clamp its upper bound");

    Oot3dNativeGame::NativeVisualSampleCadence x2Cadence(2U);
    ExpectNear(x2Cadence.Resolve(true, 0.91F), 0.0,
               "x2 cadence did not start from the previous source frame");
    ExpectNear(x2Cadence.Resolve(false, 0.13F), 0.5,
               "x2 cadence depended on wall-clock jitter");
    ExpectNear(x2Cadence.Resolve(false, 0.97F), 0.5,
               "x2 cadence advanced past its fixed sample lattice");
    Oot3dNativeGame::NativeVisualSampleCadence x3Cadence(3U);
    constexpr std::array<float, 3> kExpectedX3Cadence{
        0.0F, 1.0F / 3.0F, 2.0F / 3.0F};
    ExpectNear(x3Cadence.Resolve(true, 0.44F), kExpectedX3Cadence[0],
               "x3 cadence source boundary mismatch");
    ExpectNear(x3Cadence.Resolve(false, 0.02F), kExpectedX3Cadence[1],
               "x3 cadence first intermediate mismatch");
    ExpectNear(x3Cadence.Resolve(false, 0.99F), kExpectedX3Cadence[2],
               "x3 cadence second intermediate mismatch");
    ExpectNear(x3Cadence.Resolve(true, 0.5F), 0.0,
               "x3 cadence did not reset on a new source frame");
    x3Cadence.Configure(2U);
    ExpectNear(x3Cadence.Resolve(true, 1.0F), 0.0,
               "fixed cadence mode change did not reset phase");
    ExpectNear(x3Cadence.Resolve(false, 0.0F), 0.5,
               "fixed cadence mode change retained the old multiplier");

    const auto native60 =
        Oot3dNativeGame::ResolveNativeFrameRateContract(
            Oot3dNativeGame::GameplayTimingMode::Enhanced60, 60);
    if (!native60.A32UpdateRateExact || native60.A32UpdateRate != 1 ||
        native60.VisualInterpolationAllowed) {
        throw std::runtime_error("native 60 Hz frame-rate contract mismatch");
    }
    ExpectNear(native60.StepSeconds, 1.0 / 60.0, "native 60 Hz step mismatch");
    ExpectNear(native60.NativeStepScale, 0.5, "native 60 Hz scale mismatch");
    ExpectNear(native60.LogicalFrameDelta, 0.5,
               "native 60 Hz logical-frame delta mismatch");
    if (Oot3dNativeGame::ShouldUseNativeVisualInterpolation(native60, false) ||
        Oot3dNativeGame::ShouldUseNativeVisualInterpolation(native60, true)) {
        throw std::runtime_error(
            "enhanced 60 Hz mode allowed renderer-driven interpolation");
    }

    const auto earlyDeadline =
        Oot3dNativeGame::ResolveNativeGuestClockDeadline(100U, 140U);
    const auto exactDeadline =
        Oot3dNativeGame::ResolveNativeGuestClockDeadline(140U, 140U);
    const auto lateDeadline =
        Oot3dNativeGame::ResolveNativeGuestClockDeadline(165U, 140U);
    if (earlyDeadline.AdvanceTicks != 40U ||
        earlyDeadline.OvershootTicks != 0U ||
        exactDeadline.AdvanceTicks != 0U ||
        exactDeadline.OvershootTicks != 0U ||
        lateDeadline.AdvanceTicks != 0U ||
        lateDeadline.OvershootTicks != 25U) {
        throw std::runtime_error(
            "monotonic guest-clock deadline resolution mismatch");
    }

    try {
        (void)Oot3dNativeGame::ResolveNativeFrameRateContract(120, 120);
        throw std::runtime_error("unsupported arbitrary timing rate accepted");
    } catch (const std::invalid_argument&) {
    }

    const auto unlimited60 =
        Oot3dNativeGame::ResolveNativeFrameRateContract(
            Oot3dNativeGame::GameplayTimingMode::Enhanced60, 0);
    if (!unlimited60.PresentationRateUnlimited ||
        unlimited60.VisualInterpolationAllowed ||
        unlimited60.PresentationRateHz != 0U) {
        throw std::runtime_error(
            "unlimited presentation frame-rate contract mismatch");
    }
    const auto unlimited30 =
        Oot3dNativeGame::ResolveNativeFrameRateContract(
            Oot3dNativeGame::GameplayTimingMode::Native30Interpolated, 0);
    if (!unlimited30.VisualInterpolationAllowed) {
        throw std::runtime_error(
            "native 30 Hz unlimited presentation lost interpolation");
    }

    Oot3dNativeGame::NativePresentationScheduler presentationScheduler;
    auto presentationStep = presentationScheduler.Advance(0.0);
    if (presentationStep.GuestRefreshesDue != 1U ||
        presentationStep.DroppedGuestRefreshes != 0U ||
        presentationStep.InterpolationAlpha != 1.0) {
        throw std::runtime_error(
            "presentation scheduler did not prime the guest clock");
    }
    presentationStep = presentationScheduler.Advance(1.0 / 120.0);
    if (presentationStep.GuestRefreshesDue != 0U ||
        presentationStep.DroppedGuestRefreshes != 0U) {
        throw std::runtime_error(
            "120 Hz intermediate presentation advanced the guest clock");
    }
    ExpectNear(presentationStep.GuestRefreshPhase, 0.5,
               "120 Hz guest phase mismatch");
    ExpectNear(presentationStep.InterpolationAlpha, 0.5,
               "120 Hz interpolation alpha mismatch");
    presentationStep = presentationScheduler.Advance(1.0 / 120.0);
    if (presentationStep.GuestRefreshesDue != 1U ||
        presentationStep.DroppedGuestRefreshes != 0U) {
        throw std::runtime_error(
            "120 Hz presentation did not reach the next guest refresh");
    }
    ExpectNear(presentationStep.InterpolationAlpha, 1.0,
               "guest-refresh boundary interpolation mismatch");

    presentationStep = presentationScheduler.Advance(0.05);
    if (presentationStep.GuestRefreshesDue != 2U ||
        presentationStep.DroppedGuestRefreshes != 1U) {
        throw std::runtime_error(
            "presentation scheduler catch-up bound mismatch");
    }
    const auto& presentationStats = presentationScheduler.Stats();
    if (presentationStats.PresentationFrames != 4U ||
        presentationStats.GuestRefreshes != 4U ||
        presentationStats.DeferredGuestRefreshObservations != 0U ||
        presentationStats.DroppedGuestRefreshes != 1U ||
        presentationStats.BatchedPresentationFrames != 1U ||
        presentationStats.MaximumGuestRefreshBatch != 2U) {
        throw std::runtime_error(
            "presentation scheduler diagnostics mismatch");
    }

    Oot3dNativeGame::NativePresentationScheduler debtPreservingScheduler(
        60U,
        Oot3dNativeGame::NativeSimulationCatchUpPolicy::
            PreserveBoundedDebt);
    (void)debtPreservingScheduler.Advance(0.0);
    presentationStep = debtPreservingScheduler.Advance(0.05);
    if (presentationStep.GuestRefreshesDue != 2U ||
        presentationStep.DeferredGuestRefreshes != 1U ||
        presentationStep.DroppedGuestRefreshes != 0U) {
        throw std::runtime_error(
            "enhanced scheduler did not retain bounded simulation debt");
    }
    presentationStep = debtPreservingScheduler.Advance(0.0);
    if (presentationStep.GuestRefreshesDue != 1U ||
        presentationStep.DeferredGuestRefreshes != 0U ||
        presentationStep.DroppedGuestRefreshes != 0U) {
        throw std::runtime_error(
            "enhanced scheduler did not repay simulation debt");
    }
    presentationStep = debtPreservingScheduler.Advance(1.0);
    if (presentationStep.GuestRefreshesDue != 2U ||
        presentationStep.DeferredGuestRefreshes !=
            Oot3dNativeGame::kMaximumDeferredGuestRefreshes ||
        presentationStep.DroppedGuestRefreshes !=
            60U - Oot3dNativeGame::kMaximumGuestRefreshesPerPresentation -
                Oot3dNativeGame::kMaximumDeferredGuestRefreshes) {
        throw std::runtime_error(
            "enhanced scheduler protective debt bound mismatch");
    }
    try {
        presentationScheduler.Advance(-0.001);
        throw std::runtime_error(
            "negative presentation elapsed time was accepted");
    } catch (const std::invalid_argument&) {
    }

    Oot3dNativeGame::NativePresentationScheduler capped30Presentation;
    (void)capped30Presentation.Advance(0.0);
    presentationStep = capped30Presentation.Advance(1.0 / 30.0);
    if (presentationStep.GuestRefreshesDue != 2U ||
        presentationStep.DroppedGuestRefreshes != 0U) {
        throw std::runtime_error(
            "30 Hz presentation did not preserve both CTR refreshes");
    }

    Oot3dNativeGame::NativePresentationScheduler native30Presentation(30U);
    presentationStep = native30Presentation.Advance(0.0);
    ExpectNear(presentationStep.InterpolationAlpha, 1.0,
               "30 Hz visual scheduler prime mismatch");
    constexpr std::array<double, 4> kExpected30HzAlphas{
        0.25, 0.5, 0.75, 1.0};
    for (const double expectedAlpha : kExpected30HzAlphas) {
        presentationStep = native30Presentation.Advance(1.0 / 120.0);
        ExpectNear(presentationStep.InterpolationAlpha, expectedAlpha,
                   "30-to-120 Hz visual interpolation phase mismatch");
    }

    Oot3dNativeGame::NativePresentationScheduler native90Presentation(30U);
    (void)native90Presentation.Advance(0.0);
    constexpr std::array<double, 3> kExpected90HzAlphas{
        1.0 / 3.0, 2.0 / 3.0, 1.0};
    for (const double expectedAlpha : kExpected90HzAlphas) {
        const auto x3Step = native90Presentation.Advance(1.0 / 90.0);
        ExpectNear(x3Step.InterpolationAlpha, expectedAlpha,
                   "30-to-90 Hz visual interpolation phase mismatch");
    }
    presentationStep = native30Presentation.Advance(1.0 / 120.0);
    ExpectNear(presentationStep.InterpolationAlpha, 0.25,
               "30 Hz visual interpolation phase moved backwards");
    const auto capturedPresentationState =
        native30Presentation.CaptureState();
    Oot3dNativeGame::NativePresentationScheduler restoredPresentation(30U);
    std::string presentationRestoreError;
    if (!restoredPresentation.Restore(capturedPresentationState,
                                      &presentationRestoreError)) {
        throw std::runtime_error(presentationRestoreError);
    }
    const auto uninterruptedStep =
        native30Presentation.Advance(1.0 / 120.0);
    const auto restoredStep =
        restoredPresentation.Advance(1.0 / 120.0);
    if (uninterruptedStep.GuestRefreshesDue !=
            restoredStep.GuestRefreshesDue ||
        uninterruptedStep.DroppedGuestRefreshes !=
            restoredStep.DroppedGuestRefreshes) {
        throw std::runtime_error(
            "restored presentation scheduler changed guest refresh timing");
    }
    ExpectNear(restoredStep.GuestRefreshPhase,
               uninterruptedStep.GuestRefreshPhase,
               "restored presentation guest phase mismatch");
    ExpectNear(restoredStep.VisualSamplePhase,
               uninterruptedStep.VisualSamplePhase,
               "restored presentation visual phase mismatch");
    ExpectNear(restoredStep.InterpolationAlpha,
               uninterruptedStep.InterpolationAlpha,
               "restored presentation interpolation mismatch");
    auto invalidPresentationState = capturedPresentationState;
    invalidPresentationState.VisualSamplePhase = 1.5;
    if (restoredPresentation.Restore(invalidPresentationState)) {
        throw std::runtime_error(
            "invalid presentation scheduler state was accepted");
    }
    try {
        Oot3dNativeGame::NativePresentationScheduler invalidScheduler(0U);
        (void)invalidScheduler;
        throw std::runtime_error("zero visual sample rate was accepted");
    } catch (const std::invalid_argument&) {
    }

    TestMemoryBus memory;
    constexpr uint32_t gameState = 0x80U;
    constexpr uint32_t timeState = 0x40U;
    constexpr uint32_t updateRate =
        timeState + Oot3dNativeGame::kOot3dTimeStateUpdateRateOffset;
    constexpr uint32_t unrelatedGameStateField =
        gameState + Oot3dNativeGame::kOot3dTimeStateUpdateRateOffset;
    memory.Bytes[updateRate - 1U] = 0xA5U;
    memory.Bytes[updateRate] = 2U;
    memory.Bytes[updateRate + 1U] = 0U;
    memory.Bytes[updateRate + 2U] = 0x5AU;
    memory.Bytes[unrelatedGameStateField] = 0xC3U;
    memory.Bytes[unrelatedGameStateField + 1U] = 0x3CU;

    Oot3dNativeGame::NativeFrameRatePolicy policy(native60);
    if (!policy.ApplyNativeTimeScaleAtGameStateUpdate(gameState, memory) ||
        memory.Bytes[updateRate] != 1U || memory.Bytes[updateRate + 1U] != 0U ||
        memory.Bytes[updateRate - 1U] != 0xA5U ||
        memory.Bytes[updateRate + 2U] != 0x5AU ||
        memory.Bytes[unrelatedGameStateField] != 0xC3U ||
        memory.Bytes[unrelatedGameStateField + 1U] != 0x3CU) {
        throw std::runtime_error(
            "A32 update-rate write corrupted guest memory");
    }
    if (!policy.ApplyNativeTimeScaleAtGameStateUpdate(gameState, memory)) {
        throw std::runtime_error("stable A32 update-rate application failed");
    }
    const auto enhancedClock = policy.GameplayClock().Context();
    if (enhancedClock.SimulationTick != 2U ||
        enhancedClock.LogicalFrameIndex != 1U ||
        !enhancedClock.CrossedLogicalFrame) {
        throw std::runtime_error(
            "enhanced 60 Hz gameplay clock did not preserve logical time");
    }
    ExpectNear(enhancedClock.PreviousLogicalFrame, 0.5,
               "enhanced clock previous logical frame mismatch");
    ExpectNear(enhancedClock.CurrentLogicalFrame, 1.0,
               "enhanced clock current logical frame mismatch");
    ExpectNear(enhancedClock.DeltaSeconds, 1.0 / 60.0,
               "enhanced clock delta mismatch");
    ExpectNear(enhancedClock.NativeUpdateRate, 1.0,
               "enhanced clock native update rate mismatch");

    const auto capturedClock = policy.GameplayClock().CaptureState();
    policy.ResetGameplayClock();
    if (policy.GameplayClock().Context().SimulationTick != 0U ||
        !policy.RestoreGameplayClock(capturedClock) ||
        policy.GameplayClock().Context().SimulationTick != 2U) {
        throw std::runtime_error("gameplay clock state restore mismatch");
    }
    auto invalidClock = capturedClock;
    invalidClock.CurrentLogicalFrame =
        invalidClock.PreviousLogicalFrame + 1.0;
    if (policy.RestoreGameplayClock(invalidClock)) {
        throw std::runtime_error("invalid gameplay clock state was accepted");
    }
    const auto& stats = policy.Stats();
    if (stats.GameStateUpdatesObserved != 2U || stats.UpdateRateWrites != 1U ||
        stats.ReadFailures != 0U || stats.WriteFailures != 0U ||
        stats.LastGameStateAddress != gameState ||
        stats.LastTimeStateAddress != timeState ||
        stats.LastObservedUpdateRate != 1) {
        throw std::runtime_error("A32 update-rate diagnostics mismatch");
    }

    memory.Bytes[updateRate] = 2U;
    Oot3dNativeGame::NativeFrameRatePolicy native30Policy(native30);
    if (!native30Policy.ApplyNativeTimeScaleAtGameStateUpdate(gameState,
                                                              memory) ||
        memory.Bytes[updateRate] != 2U ||
        native30Policy.Stats().UpdateRateWrites != 0U ||
        native30Policy.GameplayClock().Context().SimulationTick != 1U ||
        native30Policy.GameplayClock().Context().LogicalFrameIndex != 1U ||
        !native30Policy.GameplayClock().Context().CrossedLogicalFrame) {
        throw std::runtime_error("native 30 Hz timing state was modified");
    }

    constexpr uint32_t pacingState = 0x20U;
    memory.Write32(pacingState +
                       Oot3dNativeGame::kOot3dFramePacingMaximumIntervalOffset,
                   2U);
    memory.Write32(
        pacingState + Oot3dNativeGame::kOot3dFramePacingIntervalOffset, 2U);
    memory.Write32(
        pacingState + Oot3dNativeGame::kOot3dFramePacingDeadlineOffset, 102U);
    memory.Write32(Oot3dNativeGame::kOot3dVsyncCounterAddress, 102U);
    if (!policy.ApplyFramePacingInterval(pacingState, memory)) {
        throw std::runtime_error("A32 frame-pacing policy failed");
    }
    uint32_t maximumInterval = 0;
    uint32_t interval = 0;
    memory.Read32(pacingState +
                      Oot3dNativeGame::kOot3dFramePacingMaximumIntervalOffset,
                  &maximumInterval);
    memory.Read32(pacingState +
                      Oot3dNativeGame::kOot3dFramePacingIntervalOffset,
                  &interval);
    if (maximumInterval != 1U || interval != 1U ||
        policy.Stats().FramePacingCommitsObserved != 1U ||
        policy.Stats().FramePacingWrites != 2U ||
        policy.Stats().LastObservedDeadline != 102U ||
        policy.Stats().LastObservedVsyncCounter != 102U) {
        throw std::runtime_error("A32 frame-pacing contract mismatch");
    }
    memory.Write32(
        pacingState + Oot3dNativeGame::kOot3dFramePacingDeadlineOffset, 104U);
    policy.BeginFramePacing(pacingState);
    if (!policy.ApplyFramePacingDeadline(pacingState, memory)) {
        throw std::runtime_error("A32 frame-pacing deadline policy failed");
    }
    uint32_t deadline = 0;
    memory.Read32(pacingState +
                      Oot3dNativeGame::kOot3dFramePacingDeadlineOffset,
                  &deadline);
    if (deadline != 102U || policy.Stats().FramePacingDecisionsObserved != 1U ||
        policy.Stats().FramePacingDeadlineWrites != 1U) {
        throw std::runtime_error("A32 frame-pacing deadline mismatch");
    }
    const auto capturedTemporalState = policy.CaptureTemporalState();
    Oot3dNativeGame::NativeFrameRatePolicy restoredPolicy(native60);
    std::string temporalRestoreError;
    if (!restoredPolicy.RestoreTemporalState(capturedTemporalState,
                                             &temporalRestoreError)) {
        throw std::runtime_error(temporalRestoreError);
    }
    memory.Write32(
        pacingState + Oot3dNativeGame::kOot3dFramePacingDeadlineOffset, 103U);
    if (!restoredPolicy.ApplyFramePacingDeadline(pacingState, memory)) {
        throw std::runtime_error(
            "restored A32 frame-pacing decision failed");
    }
    memory.Read32(pacingState +
                      Oot3dNativeGame::kOot3dFramePacingDeadlineOffset,
                  &deadline);
    if (deadline != 103U) {
        throw std::runtime_error(
            "restored frame-pacing policy repeated a consumed decision");
    }
    auto invalidTemporalState = capturedTemporalState;
    invalidTemporalState.FramePacingStateAddress = 0U;
    if (restoredPolicy.RestoreTemporalState(invalidTemporalState)) {
        throw std::runtime_error(
            "invalid frame-rate policy temporal state was accepted");
    }
    memory.Write32(
        pacingState + Oot3dNativeGame::kOot3dFramePacingDeadlineOffset, 103U);
    if (!policy.ApplyFramePacingDeadline(pacingState, memory)) {
        throw std::runtime_error("A32 repeated frame-pacing decision failed");
    }
    memory.Read32(pacingState +
                      Oot3dNativeGame::kOot3dFramePacingDeadlineOffset,
                  &deadline);
    if (deadline != 103U || policy.Stats().FramePacingFramesObserved != 1U ||
        policy.Stats().FramePacingDecisionsObserved != 2U ||
        policy.Stats().FramePacingDeadlineWrites != 1U) {
        throw std::runtime_error(
            "A32 repeated frame-pacing decision was not stable");
    }
}
