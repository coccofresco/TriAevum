#pragma once

#include "oot3d_native_control_config.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Oot3dNativeGame {

inline constexpr uint16_t NativeA32TouchWidth =
    ThreeDsRecomp::Input::kTouchWidth;
inline constexpr uint16_t NativeA32TouchHeight =
    ThreeDsRecomp::Input::kTouchHeight;
inline constexpr uint16_t NativeA32TopScreenWidth = 400U;
inline constexpr uint16_t NativeA32TopScreenHeight = 240U;

using NativeA32HidButton = ThreeDsRecomp::Input::Button;

constexpr uint32_t NativeA32HidButtonMask(NativeA32HidButton button) {
    return ThreeDsRecomp::Input::ButtonMask(button);
}

using NativeA32HidState = ThreeDsRecomp::Input::HidState;
using NativeFreeCameraInputKind = ThreeDsRecomp::Input::AxisInputKind;
using NativeFreeCameraInputSample = ThreeDsRecomp::Input::AxisInputSample;
using NativeFreeCameraInputAccumulator =
    ThreeDsRecomp::Input::AxisInputAccumulator;

struct NativeA32InputFrame : ThreeDsRecomp::Input::InputFrame {
    bool Exit = false;
    bool Scripted = false;
    int32_t ScriptSegmentIndex = -1;
};

bool ResolveNativeGameplayMouseOwnership(
    bool nativeFrontendTouchEnabled, bool hostGuiVisible,
    bool mouseEnabled, bool sourceUsesMouse) noexcept;

// Host polling may observe a complete press between guest refreshes. Preserve
// that rising edge without replacing the physical held level with a pulse.
struct NativeA32PolledButtonLatch {
    bool PhysicalHeld = false;
    bool PendingPressed = false;
    uint64_t ObservedPresses = 0;
    uint64_t RecoveredShortPresses = 0;
};

void ObserveNativeA32PolledButton(
    bool physicalHeld, NativeA32PolledButtonLatch& state) noexcept;

bool ResolveNativeA32PolledButton(
    bool physicalHeld, NativeA32PolledButtonLatch& state) noexcept;

// Platform-independent sample assembled by the SDL/window adapter. Digital
// actions are resolved from the active binding profile before reaching this
// boundary; analog and motion values retain their physical units.
struct NativeControlHostInputState
    : ThreeDsRecomp::Input::PhysicalInputState {
    std::array<bool, kNativeControlActionCount> Actions{};
};

// Optional application/profile adjustment applied only to right-stick-derived
// native aiming. Free-camera routing retains its own TopScreen speed,
// inversion and dead-zone contract.
struct NativeAimProfileTransform {
  float RightStickScale = 1.0F;
  float RightStickSmoothingCoefficient = 1.0F;
  bool RightStickInvertX = false;
  bool RightStickInvertY = false;
};

using NativeRightStickProfileState =
    ThreeDsRecomp::Input::CStickFilterState;

NativeA32InputFrame MapNativeControlInput(
    const NativeControlConfig& config,
    const NativeControlHostInputState& host,
    const NativeAimProfileTransform& aimTransform = {},
    NativeRightStickProfileState* rightStickState = nullptr,
    bool advanceRightStickState = true,
    ThreeDsRecomp::Input::VirtualMotionState* virtualMotion = nullptr) noexcept;

using NativeA32TouchMapping = ThreeDsRecomp::Input::TouchMapping;

enum class NativeA32TouchPresentation : uint8_t {
    NativeLowerScreen320x240,
    TopScreen400x240,
};

enum class NativeA32TopScreenPage : uint8_t {
    None,
    Gear,
    Map,
    Items,
};

// Native lower-screen tab centers retained by the TopScreen mod. Desktop
// shortcuts synthesize these touch positions instead of adding a new menu
// control path.
NativeA32TouchMapping MapTopScreenPageShortcutToNativeTouch(
    NativeA32TopScreenPage page) noexcept;

// OoT3D convenience bindings are adapters, not additional gameplay inputs.
// The regular actions resolve to DigitalControl; Gear/Map/Items synthesize
// the title's native lower-screen touch targets.
[[nodiscard]] bool MapNativeControlActionToThreeDsControl(
    NativeControlAction action,
    ThreeDsRecomp::Input::DigitalControl* control) noexcept;
[[nodiscard]] NativeA32TopScreenPage ResolveNativeControlPageShortcut(
    const NativeControlHostInputState& host) noexcept;
void ApplyNativeControlShortcutTouch(
    const NativeControlHostInputState& host,
    NativeA32InputFrame& frame) noexcept;

// The native pause-open gesture spans multiple guest refreshes. Keep START
// suppressed until that physical gesture is released so it cannot also close
// the page it just opened.
struct TopScreenStartRoutingState {
    bool OpenGestureHeld = false;
    bool CloseGestureHeld = false;
};

void MarkTopScreenStartOpenGestureConsumed(
    TopScreenStartRoutingState& state) noexcept;

void MarkTopScreenStartCloseGestureConsumed(
    TopScreenStartRoutingState& state) noexcept;

// Guest refreshes release the consumed-gesture latch only when one of them
// observes the physical release. During guest stalls several host polls can
// pass without a consuming refresh, so a release-plus-repress landing in that
// window would be swallowed by the stale latch. Releasing the latch at host
// poll rate keeps the suppression window tied to the physical gesture.
void ReleaseTopScreenStartRoutingLatch(
    bool physicalStartHeld, TopScreenStartRoutingState& state) noexcept;

// TopScreen keeps pointer ownership only for actual pause pages and native
// frontends. The broad OoT3D touch-presentation flag is still authoritative
// for the unchanged default two-screen profile.
bool ResolveNativeA32TouchInputEnabled(
    bool topScreenProfile, bool nativeTouchPresentationActive,
    bool nativeFrontendPresentationActive,
    bool nativePausePageActive) noexcept;

// The TopScreen payload routes START to the native Items touch target while
// gameplay owns the UI. Once Pause owns the UI, this boundary leaves START
// available for the source-level pause-close dispatcher. Routing state changes
// only when a guest refresh will consume the sample.
void ApplyTopScreenStartRouting(NativeA32InputFrame& frame,
                                bool topScreenProfile,
                                bool routeGameplayStartToItems,
                                bool nativePauseOpen,
                                bool guestRefreshWillConsume,
                                TopScreenStartRoutingState& state) noexcept;

// SaveContext.game_mode uses 0 for ordinary play and 1 for gameplay
// cutscenes. Frontend/title ownership begins at 2 and must retain START.
constexpr bool
IsTopScreenGameplayInputMode(std::uint32_t nativeGameMode) noexcept {
  return nativeGameMode <= 1U;
}

NativeA32TouchMapping MapHostPointerToNativeA32Touch(
    int32_t pointerX, int32_t pointerY, uint32_t hostWidth,
    uint32_t hostHeight, bool pointerPressed,
    NativeA32TouchPresentation presentation =
        NativeA32TouchPresentation::NativeLowerScreen320x240) noexcept;

struct NativeA32InputDiagnostics {
    uint32_t SampledFrameCount = 0;
    uint32_t ScriptedFrameCount = 0;
    uint32_t ActiveSegmentFrameCount = 0;
    uint32_t ButtonFrameCount = 0;
    uint32_t CirclePadFrameCount = 0;
    uint32_t TouchFrameCount = 0;
    NativeA32InputFrame LastFrame;

    void Observe(const NativeA32InputFrame& frame);
};

enum class NativeA32InputTimelineFrameOrigin : uint8_t {
    Guest,
    Run,
};

const char* NativeA32InputTimelineFrameOriginName(
    NativeA32InputTimelineFrameOrigin origin) noexcept;

class NativeA32InputTimeline {
  public:
    static NativeA32InputTimeline LoadFile(const std::filesystem::path& path);

    bool Enabled() const;
    size_t SegmentCount() const;
    const std::filesystem::path& SourcePath() const;
    NativeA32InputTimelineFrameOrigin FrameOrigin() const;
    NativeA32InputFrame Sample(uint32_t guestFrame, uint32_t runFrame) const;

  private:
    struct Segment : ThreeDsRecomp::Input::InputFrame {
        uint32_t StartFrame = 0;
        uint32_t EndFrameExclusive = 0;
    };

    std::filesystem::path mSourcePath;
    std::vector<Segment> mSegments;
    NativeA32InputTimelineFrameOrigin mFrameOrigin =
        NativeA32InputTimelineFrameOrigin::Guest;
};

} // namespace Oot3dNativeGame
