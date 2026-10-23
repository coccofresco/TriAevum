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
    bool StartHeld = false;
    // One consuming sample's START edge, transferred to the pause transaction.
    bool OpenRequested = false;
    bool OpenGestureHeld = false;
    bool CloseGestureHeld = false;
    // TopScreen 2.1.1 005D52C4..537C records the page that owns the pause UI
    // (Items 0xC, Gear 0x10, Map 0x14 in 005E21C8, initial 0xC) on every
    // update, and the START open at 005D5B0C..5B24 reopens that page.
    NativeA32TopScreenPage LastPage = NativeA32TopScreenPage::Items;
};

// Source form of the 005E21C8 page memory: Items, then Gear, then Map, as in
// 005D52C4..537C. Other states (System Menu, no page) keep the last value.
void ObserveTopScreenStartRoutingPage(bool itemsActive, bool gearActive,
                                      bool mapActive,
                                      TopScreenStartRoutingState& state) noexcept;

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

// On one screen the title keeps its "PRESS START" until the player acts: the
// game slides the normal / Master Quest cards onto the lower screen by itself,
// so TopScreen withholds them and every guest input until a START, A or touch
// press reveals them. That press only reveals (it is swallowed until released,
// so it cannot also confirm a card). A press during the title prompt, before
// the panels enter, reaches the game as on hardware and counts as the reveal.
// Returns true on the revealing sample.
struct TopScreenVersionChoiceRevealState {
    bool GestureHeld = false;
};

bool ApplyTopScreenVersionChoiceReveal(
    NativeA32InputFrame& frame, bool awaitingReveal, bool prompting,
    TopScreenVersionChoiceRevealState& state) noexcept;

// Capture one OpenLastPage request on a gameplay START edge, without touch.
// Once Pause owns the UI, leave START for the pause-close dispatcher. Routing
// state changes only when a guest refresh will consume the sample.
void ApplyTopScreenStartRouting(NativeA32InputFrame& frame,
                                bool topScreenProfile,
                                bool routeGameplayStartToLastPage,
                                bool nativePauseOpen,
                                bool guestRefreshWillConsume,
                                TopScreenStartRoutingState& state) noexcept;

// SaveContext.game_mode uses 0 for ordinary play and 1 for the title
// screen's gameplay demo; frontend modes begin at 2.
constexpr bool
IsTopScreenGameplayInputMode(std::uint32_t nativeGameMode) noexcept {
  return nativeGameMode <= 1U;
}

// START opens the last page only in ordinary play, as 2.1.1 0x005D5A54
// does. The title demo draws the gameplay HUD but owns START: routing it
// there swallowed the press that leaves the title.
constexpr bool
IsTopScreenStartRoutingMode(std::uint32_t nativeGameMode) noexcept {
  return nativeGameMode == 0U;
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
