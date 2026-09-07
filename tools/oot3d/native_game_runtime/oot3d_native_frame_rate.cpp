#include "oot3d_native_frame_rate.h"

#include "a32_runtime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Oot3dNativeGame {

namespace {

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

uint32_t SimulationRateForMode(GameplayTimingMode mode) {
    switch (mode) {
    case GameplayTimingMode::Native30Interpolated:
    case GameplayTimingMode::Native30NoInterpolation:
        return kOot3dOriginalSimulationRateHz;
    case GameplayTimingMode::Enhanced60:
        return kOot3dNativeTimeUnitsPerSecond;
    }
    throw std::invalid_argument("unknown OOT3D gameplay timing mode");
}

} // namespace

const char* GameplayTimingModeName(GameplayTimingMode mode) noexcept {
    switch (mode) {
    case GameplayTimingMode::Native30Interpolated:
        return "native30_interpolated";
    case GameplayTimingMode::Native30NoInterpolation:
        return "native30_no_interpolation";
    case GameplayTimingMode::Enhanced60:
        return "enhanced60";
    }
    return "unknown";
}

bool ParseGameplayTimingMode(std::string_view value,
                             GameplayTimingMode* mode) noexcept {
    if (mode == nullptr) {
        return false;
    }
    if (value == "native30_interpolated" ||
        value == "native30-interpolated") {
        *mode = GameplayTimingMode::Native30Interpolated;
        return true;
    }
    if (value == "native30_no_interpolation" ||
        value == "native30-no-interpolation") {
        *mode = GameplayTimingMode::Native30NoInterpolation;
        return true;
    }
    if (value == "enhanced60" || value == "enhanced-60") {
        *mode = GameplayTimingMode::Enhanced60;
        return true;
    }
    return false;
}

NativeFrameRateContract
ResolveNativeFrameRateContract(GameplayTimingMode mode,
                               uint32_t presentationRateHz) {
    NativeFrameRateContract contract;
    contract.Mode = mode;
    const uint32_t simulationRateHz = SimulationRateForMode(mode);
    contract.SimulationRateHz = simulationRateHz;
    contract.PresentationRateHz = presentationRateHz;
    contract.PresentationRateUnlimited = presentationRateHz == 0U;
    contract.StepSeconds = 1.0 / static_cast<double>(simulationRateHz);
    contract.NativeUpdateRate =
        static_cast<double>(kOot3dNativeTimeUnitsPerSecond) /
        static_cast<double>(simulationRateHz);
    contract.NativeStepScale = contract.NativeUpdateRate /
                               static_cast<double>(kOot3dOriginalUpdateRate);
    contract.LogicalFrameDelta =
        static_cast<double>(kOot3dOriginalSimulationRateHz) /
        static_cast<double>(simulationRateHz);
    contract.VisualInterpolationAllowed =
        mode == GameplayTimingMode::Native30Interpolated;

    const double roundedUpdateRate = std::round(contract.NativeUpdateRate);
    contract.A32UpdateRateExact =
        std::abs(contract.NativeUpdateRate - roundedUpdateRate) <= 1.0e-9 &&
        roundedUpdateRate >= 1.0 &&
        roundedUpdateRate <=
            static_cast<double>(std::numeric_limits<int16_t>::max());
    contract.A32UpdateRate = contract.A32UpdateRateExact
                                 ? static_cast<int16_t>(roundedUpdateRate)
                                 : 0;
    return contract;
}

NativeFrameRateContract
ResolveNativeFrameRateContract(uint32_t simulationRateHz,
                               uint32_t presentationRateHz) {
    if (simulationRateHz == kOot3dOriginalSimulationRateHz) {
        return ResolveNativeFrameRateContract(
            GameplayTimingMode::Native30Interpolated, presentationRateHz);
    }
    if (simulationRateHz == kOot3dNativeTimeUnitsPerSecond) {
        return ResolveNativeFrameRateContract(
            GameplayTimingMode::Enhanced60, presentationRateHz);
    }
    throw std::invalid_argument(
        "OOT3D gameplay timing supports only semantic 30 or 60 Hz modes");
}

bool ShouldUseNativeVisualInterpolation(
    const NativeFrameRateContract& contract,
    bool interpolationRequested) noexcept {
    return contract.VisualInterpolationAllowed && interpolationRequested;
}

NativeGuestClockDeadlineResolution
ResolveNativeGuestClockDeadline(uint64_t currentTicks,
                                uint64_t deadlineTicks) noexcept {
    if (currentTicks <= deadlineTicks) {
        return {deadlineTicks - currentTicks, 0U};
    }
    return {0U, currentTicks - deadlineTicks};
}

NativeGameplayClock::NativeGameplayClock(NativeFrameRateContract contract)
    : mContract(contract) {
    ResolveContext();
}

const Oot3dTimeContext& NativeGameplayClock::Advance() {
    mState.PreviousLogicalFrame = mState.CurrentLogicalFrame;
    mState.CurrentLogicalFrame += mContract.LogicalFrameDelta;
    ++mState.SimulationTick;
    ResolveContext();
    return mContext;
}

void NativeGameplayClock::Reset() {
    mState = {};
    ResolveContext();
}

bool NativeGameplayClock::Restore(const NativeGameplayClockState& state,
                                  std::string* error) {
    if (!std::isfinite(state.PreviousLogicalFrame) ||
        !std::isfinite(state.CurrentLogicalFrame) ||
        state.PreviousLogicalFrame < 0.0 ||
        state.CurrentLogicalFrame < state.PreviousLogicalFrame ||
        state.CurrentLogicalFrame - state.PreviousLogicalFrame >
            mContract.LogicalFrameDelta + 1.0e-9) {
        SetError(error, "native gameplay clock state is invalid");
        return false;
    }
    mState = state;
    ResolveContext();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

const Oot3dTimeContext& NativeGameplayClock::Context() const {
    return mContext;
}

NativeGameplayClockState NativeGameplayClock::CaptureState() const {
    return mState;
}

void NativeGameplayClock::ResolveContext() {
    mContext.SimulationTick = mState.SimulationTick;
    mContext.DeltaSeconds = mContract.StepSeconds;
    mContext.NativeUpdateRate =
        static_cast<float>(mContract.NativeUpdateRate);
    mContext.PreviousLogicalFrame = mState.PreviousLogicalFrame;
    mContext.CurrentLogicalFrame = mState.CurrentLogicalFrame;
    mContext.LogicalFrameIndex = static_cast<uint64_t>(
        std::floor(mState.CurrentLogicalFrame + 1.0e-9));
    const uint64_t previousFrameIndex = static_cast<uint64_t>(
        std::floor(mState.PreviousLogicalFrame + 1.0e-9));
    mContext.CrossedLogicalFrame =
        mContext.LogicalFrameIndex > previousFrameIndex;
}

float ResolveDelayedVisualSampleAlpha(
    bool selectedNewVisualFrame, float schedulerInterpolationAlpha) {
    if (selectedNewVisualFrame) {
        return 0.0F;
    }
    if (!std::isfinite(schedulerInterpolationAlpha)) {
        return 0.0F;
    }
    return std::clamp(schedulerInterpolationAlpha, 0.0F, 1.0F);
}

NativeVisualSampleCadence::NativeVisualSampleCadence(
    uint8_t fixedSampleMultiplier) {
    Configure(fixedSampleMultiplier);
}

void NativeVisualSampleCadence::Configure(
    uint8_t fixedSampleMultiplier) noexcept {
    if (fixedSampleMultiplier != 2U && fixedSampleMultiplier != 3U) {
        fixedSampleMultiplier = 0U;
    }
    if (mFixedSampleMultiplier == fixedSampleMultiplier) {
        return;
    }
    mFixedSampleMultiplier = fixedSampleMultiplier;
    Reset();
}

float NativeVisualSampleCadence::Resolve(
    bool selectedNewVisualFrame,
    float schedulerInterpolationAlpha) noexcept {
    if (selectedNewVisualFrame) {
        mHasSourceFrame = true;
        mStats.SampleOrdinal = 0U;
        ++mStats.NewSourceFrames;
        return 0.0F;
    }
    if (mFixedSampleMultiplier == 0U) {
        return ResolveDelayedVisualSampleAlpha(
            false, schedulerInterpolationAlpha);
    }
    if (!mHasSourceFrame) {
        return 0.0F;
    }
    ++mStats.RepeatedSamples;
    if (mStats.SampleOrdinal + 1U < mFixedSampleMultiplier) {
        ++mStats.SampleOrdinal;
    } else {
        ++mStats.ClampedRepeatedSamples;
    }
    return static_cast<float>(mStats.SampleOrdinal) /
           static_cast<float>(mFixedSampleMultiplier);
}

void NativeVisualSampleCadence::Reset() noexcept {
    mHasSourceFrame = false;
    mStats.SampleOrdinal = 0U;
}

uint8_t NativeVisualSampleCadence::FixedSampleMultiplier() const noexcept {
    return mFixedSampleMultiplier;
}

const NativeVisualSampleCadenceStats&
NativeVisualSampleCadence::Stats() const noexcept {
    return mStats;
}

NativePresentationScheduler::NativePresentationScheduler(
    uint32_t visualSampleRateHz,
    NativeSimulationCatchUpPolicy catchUpPolicy)
    : mVisualSampleRateHz(visualSampleRateHz),
      mCatchUpPolicy(catchUpPolicy) {
    if (mVisualSampleRateHz == 0U) {
        throw std::invalid_argument(
            "visual sample rate must be positive");
    }
}

NativePresentationStep
NativePresentationScheduler::Advance(double elapsedSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) {
        throw std::invalid_argument(
            "presentation elapsed time must be finite and non-negative");
    }

    NativePresentationStep step;
    ++mStats.PresentationFrames;
    if (!mStarted) {
        mStarted = true;
        step.GuestRefreshesDue = 1U;
        step.InterpolationAlpha = 1.0;
        ++mStats.GuestRefreshes;
        mStats.MaximumGuestRefreshBatch =
            std::max(mStats.MaximumGuestRefreshBatch, 1U);
        return step;
    }

    mGuestRefreshPhase +=
        elapsedSeconds * static_cast<double>(kOot3dNativeTimeUnitsPerSecond);
    mVisualSamplePhase +=
        elapsedSeconds * static_cast<double>(mVisualSampleRateHz);
    constexpr double kBoundaryTolerance = 1.0e-9;
    const uint32_t elapsedGuestRefreshes = static_cast<uint32_t>(
        std::floor(mGuestRefreshPhase + kBoundaryTolerance));
    if (elapsedGuestRefreshes != 0U) {
        step.GuestRefreshesDue = std::min(
            elapsedGuestRefreshes,
            kMaximumGuestRefreshesPerPresentation);
        if (mCatchUpPolicy ==
            NativeSimulationCatchUpPolicy::PreserveBoundedDebt) {
            mGuestRefreshPhase -=
                static_cast<double>(step.GuestRefreshesDue);
            const uint32_t remainingRefreshes = static_cast<uint32_t>(
                std::floor(mGuestRefreshPhase + kBoundaryTolerance));
            if (remainingRefreshes > kMaximumDeferredGuestRefreshes) {
                step.DroppedGuestRefreshes =
                    remainingRefreshes - kMaximumDeferredGuestRefreshes;
                mGuestRefreshPhase -=
                    static_cast<double>(step.DroppedGuestRefreshes);
            }
            step.DeferredGuestRefreshes = static_cast<uint32_t>(
                std::floor(mGuestRefreshPhase + kBoundaryTolerance));
        } else {
            mGuestRefreshPhase -=
                static_cast<double>(elapsedGuestRefreshes);
            step.DroppedGuestRefreshes =
                elapsedGuestRefreshes - step.GuestRefreshesDue;
        }
        mGuestRefreshPhase =
            std::max(0.0, mGuestRefreshPhase);
    }
    const uint32_t elapsedVisualSamples = static_cast<uint32_t>(
        std::floor(mVisualSamplePhase + kBoundaryTolerance));
    if (elapsedVisualSamples != 0U) {
        mVisualSamplePhase -= static_cast<double>(elapsedVisualSamples);
        mVisualSamplePhase = std::clamp(mVisualSamplePhase, 0.0, 1.0);
    }
    if (step.DroppedGuestRefreshes != 0U) {
        // Catch-up is deliberately discarded. Rebase the visual clock to the
        // one guest refresh that will actually be simulated.
        mVisualSamplePhase = 0.0;
        step.InterpolationAlpha = 1.0;
    } else {
        step.InterpolationAlpha =
            elapsedVisualSamples != 0U ? 1.0 : mVisualSamplePhase;
    }
    step.GuestRefreshPhase = mGuestRefreshPhase;
    step.VisualSamplePhase = mVisualSamplePhase;
    mStats.GuestRefreshes += step.GuestRefreshesDue;
    mStats.DeferredGuestRefreshObservations +=
        step.DeferredGuestRefreshes;
    mStats.DroppedGuestRefreshes += step.DroppedGuestRefreshes;
    mStats.BatchedPresentationFrames +=
        step.GuestRefreshesDue > 1U ? 1U : 0U;
    mStats.MaximumGuestRefreshBatch = std::max(
        mStats.MaximumGuestRefreshBatch, step.GuestRefreshesDue);
    mStats.MaximumDeferredGuestRefreshes = std::max(
        mStats.MaximumDeferredGuestRefreshes,
        step.DeferredGuestRefreshes);
    return step;
}

void NativePresentationScheduler::Reset() {
    mGuestRefreshPhase = 0.0;
    mVisualSamplePhase = 0.0;
    mStarted = false;
}

bool NativePresentationScheduler::Restore(
    const NativePresentationSchedulerState& state, std::string* error) {
    constexpr double kPhaseTolerance = 1.0e-9;
    if (!std::isfinite(state.GuestRefreshPhase) ||
        !std::isfinite(state.VisualSamplePhase) ||
        state.GuestRefreshPhase < 0.0 ||
        state.GuestRefreshPhase >=
            static_cast<double>(kMaximumDeferredGuestRefreshes + 1U) +
                kPhaseTolerance ||
        state.VisualSamplePhase < 0.0 ||
        state.VisualSamplePhase >= 1.0 + kPhaseTolerance ||
        (!state.Started &&
         (state.GuestRefreshPhase != 0.0 ||
          state.VisualSamplePhase != 0.0))) {
        SetError(error, "native presentation scheduler state is invalid");
        return false;
    }
    mGuestRefreshPhase = state.GuestRefreshPhase;
    mVisualSamplePhase = std::min(state.VisualSamplePhase, 1.0);
    mStarted = state.Started;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

NativePresentationSchedulerState
NativePresentationScheduler::CaptureState() const noexcept {
    return {mGuestRefreshPhase, mVisualSamplePhase, mStarted};
}

const NativePresentationSchedulerStats&
NativePresentationScheduler::Stats() const {
    return mStats;
}

NativeFrameRatePolicy::NativeFrameRatePolicy(NativeFrameRateContract contract)
    : mContract(contract), mGameplayClock(contract) {
    if (!mContract.A32UpdateRateExact) {
        throw std::invalid_argument(
            "requested simulation rate cannot be represented exactly by the "
            "native A32 integer update-rate ABI");
    }
}

bool NativeFrameRatePolicy::ApplyNativeTimeScaleAtGameStateUpdate(
    uint32_t gameStateAddress, oot3d::recomp::a32::MemoryBus& memory) {
    ++mStats.GameStateUpdatesObserved;
    mStats.LastGameStateAddress = gameStateAddress;
    if (gameStateAddress == 0U) {
        ++mStats.ReadFailures;
        mLastError = "GameState_Update received a null GameState pointer";
        return false;
    }

    uint32_t timeStateAddress = 0;
    if (!memory.Read32(kOot3dTimeStatePointerAddress, &timeStateAddress) ||
        timeStateAddress == 0U) {
        ++mStats.ReadFailures;
        mLastError = "could not resolve the native timing-state pointer";
        return false;
    }
    mStats.LastTimeStateAddress = timeStateAddress;
    const uint32_t updateRateAddress =
        timeStateAddress + kOot3dTimeStateUpdateRateOffset;
    uint16_t encodedUpdateRate = 0;
    if (!memory.Read16(updateRateAddress, &encodedUpdateRate)) {
        ++mStats.ReadFailures;
        mLastError = "could not read the native timing-state update rate";
        return false;
    }

    mStats.LastObservedUpdateRate = static_cast<int16_t>(encodedUpdateRate);
    mStats.HasObservedUpdateRate = true;
    const uint16_t requestedUpdateRate =
        static_cast<uint16_t>(mContract.A32UpdateRate);
    if (encodedUpdateRate != requestedUpdateRate) {
        if (!memory.Write16(updateRateAddress, requestedUpdateRate)) {
            ++mStats.WriteFailures;
            mLastError = "could not write the native timing-state update rate";
            return false;
        }
        ++mStats.UpdateRateWrites;
    }

    mGameplayClock.Advance();
    mLastError.clear();
    return true;
}

void NativeFrameRatePolicy::BeginFramePacing(uint32_t framePacingStateAddress) {
    ++mStats.FramePacingFramesObserved;
    mStats.LastFramePacingStateAddress = framePacingStateAddress;
    mCurrentFramePacingStateAddress = framePacingStateAddress;
    mFramePacingDecisionPending = true;
}

bool NativeFrameRatePolicy::ApplyFramePacingInterval(
    uint32_t framePacingStateAddress, oot3d::recomp::a32::MemoryBus& memory) {
    ++mStats.FramePacingCommitsObserved;
    mStats.LastFramePacingStateAddress = framePacingStateAddress;
    if (framePacingStateAddress == 0U) {
        ++mStats.ReadFailures;
        mLastError = "native frame pacing received a null state pointer";
        return false;
    }

    uint32_t maximumInterval = 0;
    uint32_t interval = 0;
    uint32_t deadline = 0;
    uint32_t vsyncCounter = 0;
    if (!memory.Read32(framePacingStateAddress +
                           kOot3dFramePacingMaximumIntervalOffset,
                       &maximumInterval) ||
        !memory.Read32(framePacingStateAddress +
                           kOot3dFramePacingIntervalOffset,
                       &interval) ||
        !memory.Read32(framePacingStateAddress +
                           kOot3dFramePacingDeadlineOffset,
                       &deadline) ||
        !memory.Read32(kOot3dVsyncCounterAddress, &vsyncCounter)) {
        ++mStats.ReadFailures;
        mLastError = "could not read the native frame-pacing state";
        return false;
    }

    mStats.LastObservedMaximumInterval = maximumInterval;
    mStats.LastObservedInterval = interval;
    mStats.LastObservedDeadline = deadline;
    mStats.LastObservedVsyncCounter = vsyncCounter;
    mStats.HasObservedFramePacing = true;

    const uint32_t requestedInterval =
        static_cast<uint32_t>(mContract.A32UpdateRate);
    if (maximumInterval != requestedInterval &&
        !memory.Write32(framePacingStateAddress +
                            kOot3dFramePacingMaximumIntervalOffset,
                        requestedInterval)) {
        ++mStats.WriteFailures;
        mLastError = "could not write the native maximum frame interval";
        return false;
    }
    if (maximumInterval != requestedInterval) {
        ++mStats.FramePacingWrites;
    }

    if (interval > requestedInterval &&
        !memory.Write32(framePacingStateAddress +
                            kOot3dFramePacingIntervalOffset,
                        requestedInterval)) {
        ++mStats.WriteFailures;
        mLastError = "could not clamp the native frame interval";
        return false;
    }
    if (interval > requestedInterval) {
        ++mStats.FramePacingWrites;
    }

    mLastError.clear();
    return true;
}

bool NativeFrameRatePolicy::ApplyFramePacingDeadline(
    uint32_t framePacingStateAddress, oot3d::recomp::a32::MemoryBus& memory) {
    ++mStats.FramePacingDecisionsObserved;
    mStats.LastFramePacingStateAddress = framePacingStateAddress;
    if (framePacingStateAddress == 0U) {
        ++mStats.ReadFailures;
        mLastError = "native frame pacing received a null state pointer";
        return false;
    }

    uint32_t deadline = 0;
    uint32_t vsyncCounter = 0;
    if (!memory.Read32(framePacingStateAddress +
                           kOot3dFramePacingDeadlineOffset,
                       &deadline) ||
        !memory.Read32(kOot3dVsyncCounterAddress, &vsyncCounter)) {
        ++mStats.ReadFailures;
        mLastError = "could not read the native frame-pacing deadline";
        return false;
    }

    mStats.LastObservedDeadline = deadline;
    mStats.LastObservedVsyncCounter = vsyncCounter;
    mStats.HasObservedFramePacing = true;

    if (!mFramePacingDecisionPending) {
        mLastError.clear();
        return true;
    }
    if (mCurrentFramePacingStateAddress != 0U &&
        mCurrentFramePacingStateAddress != framePacingStateAddress) {
        ++mStats.ReadFailures;
        mLastError = "native frame-pacing state changed within a frame";
        return false;
    }

    // This block runs after the mandatory presentation VSync. The native
    // 30 Hz path leaves one additional refresh in its deadline. A 60 Hz step
    // must leave none; lower exact rates retain the corresponding remainder.
    constexpr uint64_t kCounterModulus = 0x7FFFFFFFULL;
    const uint64_t remainingRefreshes =
        static_cast<uint32_t>(mContract.A32UpdateRate - 1);
    const uint32_t requestedDeadline = static_cast<uint32_t>(
        (static_cast<uint64_t>(vsyncCounter) + remainingRefreshes) %
        kCounterModulus);
    if (deadline != requestedDeadline &&
        !memory.Write32(framePacingStateAddress +
                            kOot3dFramePacingDeadlineOffset,
                        requestedDeadline)) {
        ++mStats.WriteFailures;
        mLastError = "could not write the native frame-pacing deadline";
        return false;
    }
    if (deadline != requestedDeadline) {
        ++mStats.FramePacingDeadlineWrites;
    }

    mCurrentFramePacingStateAddress = framePacingStateAddress;
    mFramePacingDecisionPending = false;
    mLastError.clear();
    return true;
}

const NativeFrameRateContract& NativeFrameRatePolicy::Contract() const {
    return mContract;
}

void NativeFrameRatePolicy::ResetGameplayClock() {
    mGameplayClock.Reset();
    mLastError.clear();
}

bool NativeFrameRatePolicy::RestoreGameplayClock(
    const NativeGameplayClockState& state) {
    return mGameplayClock.Restore(state, &mLastError);
}

void NativeFrameRatePolicy::ResetTemporalState() noexcept {
    mCurrentFramePacingStateAddress = 0U;
    mFramePacingDecisionPending = true;
}

bool NativeFrameRatePolicy::RestoreTemporalState(
    const NativeFrameRatePolicyTemporalState& state, std::string* error) {
    if (!state.FramePacingDecisionPending &&
        state.FramePacingStateAddress == 0U) {
        SetError(error, "native frame-rate policy temporal state is invalid");
        return false;
    }
    mCurrentFramePacingStateAddress = state.FramePacingStateAddress;
    mFramePacingDecisionPending = state.FramePacingDecisionPending;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

NativeFrameRatePolicyTemporalState
NativeFrameRatePolicy::CaptureTemporalState() const noexcept {
    return {mCurrentFramePacingStateAddress, mFramePacingDecisionPending};
}

const NativeGameplayClock& NativeFrameRatePolicy::GameplayClock() const {
    return mGameplayClock;
}

const NativeFrameRatePolicyStats& NativeFrameRatePolicy::Stats() const {
    return mStats;
}

const std::string& NativeFrameRatePolicy::LastError() const {
    return mLastError;
}

} // namespace Oot3dNativeGame
