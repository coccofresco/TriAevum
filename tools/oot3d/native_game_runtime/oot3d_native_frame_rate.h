#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "oot3d_gameplay_time.h"

namespace oot3d::recomp::a32 {
class MemoryBus;
}

namespace Oot3dNativeGame {

// GameState update interception point used by the temporary A32 adapter.
inline constexpr uint32_t kOot3dGameStateUpdateEntry = 0x00417014U;
// Authoritative timing state recovered from GameState_Init (0x00416F60) and
// its store at 0x00417000. SkelAnime, Actor and Math helpers read this state.
inline constexpr uint32_t kOot3dTimeStatePointerAddress = 0x0051B2F4U;
inline constexpr uint32_t kOot3dTimeStateUpdateRateOffset = 0x110U;
// Frame limiter commit block in the native presentation root at 0x003FD27C.
inline constexpr uint32_t kOot3dFramePacingBeginEntry = 0x003FD27CU;
inline constexpr uint32_t kOot3dFramePacingDecisionEntry = 0x003FD3A8U;
inline constexpr uint32_t kOot3dFramePacingCommitEntry = 0x003FD3B8U;
inline constexpr uint32_t kOot3dFramePacingMaximumIntervalOffset = 0x84U;
inline constexpr uint32_t kOot3dFramePacingIntervalOffset = 0x88U;
inline constexpr uint32_t kOot3dFramePacingDeadlineOffset = 0x8CU;
inline constexpr uint32_t kOot3dVsyncCounterAddress = 0x0054CC3CU;
inline constexpr uint32_t kOot3dOriginalSimulationRateHz = 30U;
inline constexpr int16_t kOot3dOriginalUpdateRate = 2;
inline constexpr uint32_t kOot3dNativeTimeUnitsPerSecond =
    kOot3dOriginalSimulationRateHz * kOot3dOriginalUpdateRate;
#if defined(__SWITCH__)
// A slow Switch frame must not schedule two complete CTR refresh/render
// passes into the following presentation. Dropping the missed refresh keeps
// the renderer out of a self-sustaining catch-up spiral; at full speed this
// does not change the native 30 Hz simulation contract.
inline constexpr uint32_t kMaximumGuestRefreshesPerPresentation = 1U;
#else
inline constexpr uint32_t kMaximumGuestRefreshesPerPresentation = 2U;
#endif
inline constexpr uint32_t kMaximumDeferredGuestRefreshes = 8U;

enum class GameplayTimingMode : uint8_t {
    Native30Interpolated,
    Native30NoInterpolation,
    Enhanced60,
};

const char* GameplayTimingModeName(GameplayTimingMode mode) noexcept;
bool ParseGameplayTimingMode(std::string_view value,
                             GameplayTimingMode* mode) noexcept;

struct NativeFrameRateContract {
    GameplayTimingMode Mode = GameplayTimingMode::Native30Interpolated;
    uint32_t SimulationRateHz = kOot3dOriginalSimulationRateHz;
    uint32_t PresentationRateHz = 60U;
    bool PresentationRateUnlimited = false;
    double StepSeconds = 1.0 / kOot3dOriginalSimulationRateHz;
    double NativeUpdateRate = kOot3dOriginalUpdateRate;
    double NativeStepScale = 1.0;
    double LogicalFrameDelta = 1.0;
    int16_t A32UpdateRate = kOot3dOriginalUpdateRate;
    bool A32UpdateRateExact = true;
    bool VisualInterpolationAllowed = true;
};

NativeFrameRateContract
ResolveNativeFrameRateContract(GameplayTimingMode mode,
                               uint32_t presentationRateHz);

// Compatibility resolver for existing launch scripts. Product-facing
// configuration is restricted to the semantic modes above.
NativeFrameRateContract
ResolveNativeFrameRateContract(uint32_t simulationRateHz,
                               uint32_t presentationRateHz);

bool ShouldUseNativeVisualInterpolation(
    const NativeFrameRateContract& contract,
    bool interpolationRequested) noexcept;

struct NativeGuestClockDeadlineResolution {
    uint64_t AdvanceTicks = 0;
    uint64_t OvershootTicks = 0;
};

// Resolves a display deadline without ever moving the guest clock backwards.
// A positive overshoot means the caller must deliver the overdue display event
// at the current tick and let subsequent deadlines catch up.
NativeGuestClockDeadlineResolution
ResolveNativeGuestClockDeadline(uint64_t currentTicks,
                                uint64_t deadlineTicks) noexcept;

struct NativeGameplayClockState {
    uint64_t SimulationTick = 0;
    double PreviousLogicalFrame = 0.0;
    double CurrentLogicalFrame = 0.0;
};

using Oot3dTimeContext = oot3d::gameplay::TimeContext;

class NativeGameplayClock {
  public:
    explicit NativeGameplayClock(NativeFrameRateContract contract);

    const Oot3dTimeContext& Advance();
    void Reset();
    bool Restore(const NativeGameplayClockState& state,
                 std::string* error = nullptr);

    const Oot3dTimeContext& Context() const;
    NativeGameplayClockState CaptureState() const;

  private:
    void ResolveContext();

    NativeFrameRateContract mContract;
    NativeGameplayClockState mState;
    Oot3dTimeContext mContext;
};

// Visual interpolation intentionally presents one logical frame behind the
// simulation. A newly completed frame therefore starts its transition at
// zero; repeated presentation frames advance through that transition.
float ResolveDelayedVisualSampleAlpha(bool selectedNewVisualFrame,
                                      float schedulerInterpolationAlpha);

struct NativeVisualSampleCadenceStats {
    uint64_t NewSourceFrames = 0;
    uint64_t RepeatedSamples = 0;
    uint64_t ClampedRepeatedSamples = 0;
    uint8_t SampleOrdinal = 0;
};

// Fixed x2/x3 modes are ordinal contracts: each native transition receives
// exactly two or three presentation samples. Wall-clock phase remains the
// authority only for adaptive/uncapped presentation.
class NativeVisualSampleCadence {
  public:
    explicit NativeVisualSampleCadence(uint8_t fixedSampleMultiplier = 0U);

    void Configure(uint8_t fixedSampleMultiplier) noexcept;
    float Resolve(bool selectedNewVisualFrame,
                  float schedulerInterpolationAlpha) noexcept;
    void Reset() noexcept;

    [[nodiscard]] uint8_t FixedSampleMultiplier() const noexcept;
    [[nodiscard]] const NativeVisualSampleCadenceStats& Stats() const noexcept;

  private:
    uint8_t mFixedSampleMultiplier = 0U;
    bool mHasSourceFrame = false;
    NativeVisualSampleCadenceStats mStats;
};

struct NativePresentationStep {
    uint32_t GuestRefreshesDue = 0;
    uint32_t DeferredGuestRefreshes = 0;
    uint32_t DroppedGuestRefreshes = 0;
    double GuestRefreshPhase = 0.0;
    double VisualSamplePhase = 0.0;
    double InterpolationAlpha = 0.0;
};

struct NativePresentationSchedulerStats {
    uint64_t PresentationFrames = 0;
    uint64_t GuestRefreshes = 0;
    uint64_t DeferredGuestRefreshObservations = 0;
    uint64_t DroppedGuestRefreshes = 0;
    uint64_t BatchedPresentationFrames = 0;
    uint32_t MaximumGuestRefreshBatch = 0;
    uint32_t MaximumDeferredGuestRefreshes = 0;
};

struct NativePresentationSchedulerState {
    double GuestRefreshPhase = 0.0;
    double VisualSamplePhase = 0.0;
    bool Started = false;
};

enum class NativeSimulationCatchUpPolicy : uint8_t {
    DropExcess,
    PreserveBoundedDebt,
};

// Separates host presentation from the CTR 60 Hz display clock. Additional
// host frames cannot advance guest gameplay, audio or physics. A 30 Hz host
// frame may execute two complete CTR refreshes; longer catch-up is discarded
// so a slow frame cannot create an unbounded recovery loop.
class NativePresentationScheduler {
  public:
    explicit NativePresentationScheduler(
        uint32_t visualSampleRateHz = kOot3dNativeTimeUnitsPerSecond,
        NativeSimulationCatchUpPolicy catchUpPolicy =
            NativeSimulationCatchUpPolicy::DropExcess);

    NativePresentationStep Advance(double elapsedSeconds);
    void Reset();
    bool Restore(const NativePresentationSchedulerState& state,
                 std::string* error = nullptr);
    NativePresentationSchedulerState CaptureState() const noexcept;

    const NativePresentationSchedulerStats& Stats() const;

  private:
    NativePresentationSchedulerStats mStats;
    uint32_t mVisualSampleRateHz = kOot3dNativeTimeUnitsPerSecond;
    NativeSimulationCatchUpPolicy mCatchUpPolicy =
        NativeSimulationCatchUpPolicy::DropExcess;
    double mGuestRefreshPhase = 0.0;
    double mVisualSamplePhase = 0.0;
    bool mStarted = false;
};

struct NativeFrameRatePolicyStats {
    uint64_t GameStateUpdatesObserved = 0;
    uint64_t UpdateRateWrites = 0;
    uint64_t FramePacingCommitsObserved = 0;
    uint64_t FramePacingWrites = 0;
    uint64_t FramePacingFramesObserved = 0;
    uint64_t FramePacingDecisionsObserved = 0;
    uint64_t FramePacingDeadlineWrites = 0;
    uint64_t ReadFailures = 0;
    uint64_t WriteFailures = 0;
    uint32_t LastGameStateAddress = 0;
    uint32_t LastTimeStateAddress = 0;
    int16_t LastObservedUpdateRate = 0;
    bool HasObservedUpdateRate = false;
    uint32_t LastFramePacingStateAddress = 0;
    uint32_t LastObservedMaximumInterval = 0;
    uint32_t LastObservedInterval = 0;
    uint32_t LastObservedDeadline = 0;
    uint32_t LastObservedVsyncCounter = 0;
    bool HasObservedFramePacing = false;
};

struct NativeFrameRatePolicyTemporalState {
    uint32_t FramePacingStateAddress = 0;
    bool FramePacingDecisionPending = true;
};

// A narrow compatibility adapter for the still-A32 GameState ABI. Decompiled
// C++ can consume NativeFrameRateContract directly and is not coupled to these
// guest addresses.
class NativeFrameRatePolicy {
  public:
    explicit NativeFrameRatePolicy(NativeFrameRateContract contract);

    bool ApplyNativeTimeScaleAtGameStateUpdate(
        uint32_t gameStateAddress, oot3d::recomp::a32::MemoryBus& memory);
    void BeginFramePacing(uint32_t framePacingStateAddress);
    bool ApplyFramePacingInterval(uint32_t framePacingStateAddress,
                                  oot3d::recomp::a32::MemoryBus& memory);
    bool ApplyFramePacingDeadline(uint32_t framePacingStateAddress,
                                  oot3d::recomp::a32::MemoryBus& memory);
    void ResetGameplayClock();
    bool RestoreGameplayClock(const NativeGameplayClockState& state);
    void ResetTemporalState() noexcept;
    bool RestoreTemporalState(
        const NativeFrameRatePolicyTemporalState& state,
        std::string* error = nullptr);
    NativeFrameRatePolicyTemporalState CaptureTemporalState() const noexcept;

    const NativeFrameRateContract& Contract() const;
    const NativeGameplayClock& GameplayClock() const;
    const NativeFrameRatePolicyStats& Stats() const;
    const std::string& LastError() const;

  private:
    NativeFrameRateContract mContract;
    NativeGameplayClock mGameplayClock;
    NativeFrameRatePolicyStats mStats;
    std::string mLastError;
    uint32_t mCurrentFramePacingStateAddress = 0;
    bool mFramePacingDecisionPending = true;
};

} // namespace Oot3dNativeGame
