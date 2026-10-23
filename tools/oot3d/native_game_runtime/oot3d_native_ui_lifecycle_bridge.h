#pragma once

#include "oot3d_n64_integrated_ui_runtime.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d_native_omote_ura_selector.h"
#include "oot3d_top_screen_config.h"
#include "oot3d_top_screen_dpad_picker.h"
#include "oot3d_top_screen_items_hint.h"
#include "oot3d_top_screen_mod_profile.h"
#include "oot3d_top_screen_ocarina.h"
#include "oot3d_ui/ui_localized_menu_resources.h"
#include "oot3d_ui/ui_state_adapter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

class TopScreenWideItemsRuntime;

inline constexpr std::size_t NativeUiSeamPhaseCount = 10U;
inline constexpr std::uint32_t kTopScreenQuestMaterializedHook = 0x0042B884U;
inline constexpr std::uint32_t kTopScreenQuestSubmitModelsHook = 0x0042B86CU;
inline constexpr std::uint32_t kTopScreenQuestDrawHook = 0x0042B9F4U;

struct NativeUiLifecycleBridgeStats {
  uint64_t topscreen_ocarina_frames = 0;
  uint64_t topscreen_ocarina_primitives = 0;
  uint64_t topscreen_ocarina_failures = 0;
  std::string topscreen_ocarina_error;
  uint64_t matched_entries = 0;
  uint64_t guest_routed_entries = 0;
  uint64_t host_routed_entries = 0;
  uint64_t state_captures = 0;
  uint64_t fields_read = 0;
  uint64_t fields_missing = 0;
  uint32_t current_host_frame = 0;
  uint32_t last_guest_entry = 0;
  uint64_t shadow_presentation_frames = 0;
  uint64_t shadow_presentation_primitives = 0;
  uint64_t topscreen_quest_hook_calls = 0;
  uint64_t topscreen_quest_transforms = 0;
  uint64_t topscreen_quest_hook_failures = 0;
  uint64_t topscreen_native_touch_copy_frames = 0;
  uint64_t topscreen_native_touch_copy_primitives = 0;
  uint64_t topscreen_native_touch_visible_primitives = 0;
  uint64_t topscreen_horse_stamina_visible_frames = 0;
  uint64_t topscreen_horse_stamina_visible_primitives = 0;
  // State captures that found the title's normal / Master Quest choice on
  // the lower screen, and its last highlighted panel (-1 before any).
  uint64_t version_choice_captures = 0;
  int32_t version_choice_last_selection = -1;
  // Reveals of the choice on one screen (START, A or touch on the title).
  uint64_t version_choice_reveals = 0;
  std::array<uint64_t, oot3d::ui::kUiSubsystemCount> calls_by_subsystem{};
  std::array<uint64_t, NativeUiSeamPhaseCount> calls_by_phase{};
};

struct NativeUiLifecycleObservation {
  bool matched = false;
  bool route_to_guest = true;
  const oot3d::ui::UiNativeFunctionContract *contract = nullptr;
  oot3d::ui::UiFramePlan frame_plan;
};

[[nodiscard]] bool BeginsNativeGameplayUiPresentation(
    const NativeUiLifecycleObservation &observation) noexcept;

// Adapter boundary between the A32 process and the integrated N64 UI. This is
// the only layer allowed to decode guest addresses; the UI runtime receives
// only UiBackendStateView through UiRuntimeRouter.
class Oot3dNativeUiLifecycleBridge {
public:
  explicit Oot3dNativeUiLifecycleBridge(NativeA32Memory &memory);

  void BeginHostFrame(uint32_t hostFrame) noexcept;
  void ResetAfterStateLoad(uint32_t hostFrame) noexcept;
  NativeUiLifecycleObservation ObserveGuestEntry(uint32_t guestEntry);
  void ObserveGameplayComposition() noexcept;
  bool ApplyTopScreenGuestHook(uint32_t guestPc, uint32_t guestR0 = 0U);
  bool AdvanceTopScreenViewIcon() {
    return AdvanceTopScreenViewAlpha(mMemory, &mTopScreenViewAlpha);
  }
  bool BeginTopScreenTouchCopies() {
    return BeginTopScreenTouchCopyUpdate(mMemory, &mTopScreenTouchCopies);
  }
  bool FinishTopScreenTouchCopies(bool retainPrevious) {
    return FinishTopScreenTouchCopyUpdate(mMemory, retainPrevious,
                                         &mTopScreenTouchCopies);
  }
  std::vector<oot3d::ui::UiPrimitive>
  BuildShadowPresentation(oot3d::ui::UiSubsystem subsystem);
  std::vector<oot3d::ui::UiPrimitive>
  BuildTopScreenPresentation(oot3d::ui::UiSubsystem subsystem);
  void SetTopScreenPauseEdgePresentation(
      std::optional<TopScreenPauseEdgeGeometry> geometry) noexcept;
  void SetTopScreenPausePageRedrawEdgePresentation(
      std::optional<TopScreenPauseEdgeGeometry> geometry) noexcept;
  // Retained across host refreshes, including the native tail's temporary
  // System suppression. Updated before the first render-target bind.
  void SetTopScreenSystemPagePresentation(bool active) noexcept {
    mTopScreenSystemPagePresentation = active;
  }
  void
  SetTopScreenInputFrame(const TopScreenExtendedInputFrame &input) noexcept;
  void SetTopScreenConfig(const TopScreenUiConfig &config) noexcept;
  void SetTopScreenItemsShortcutSource(TopScreenWideItemsRuntime *source) noexcept {
    mTopScreenItemsShortcuts = source;
    mTopScreenDpadPickerCursor = {};
    mTopScreenItemsShortcutCursor = {};
  }
  // Produced with BuildTopScreenPresentation(GameplayHud); consume in the same
  // composition, using OcarinaPage alpha-only, post-filter TextureAlphaScale,
  // and layer 37 before the layer-38 X/A glyph. Neither accessor advances time.
  const TopScreenDpadPickerHoldCursor &TopScreenDpadPickerCursor() const noexcept {
    return mTopScreenDpadPickerCursor;
  }
  const TopScreenDpadPickerHoldCursor &TopScreenItemsShortcutCursor() const noexcept {
    return mTopScreenItemsShortcutCursor;
  }
  bool ReadOcarinaGeometry(TopScreenOcarinaGeometry &geometry, std::string *error) {
    return ReadTopScreenOcarinaGeometry(mMemory, mTopScreenOcarina, &geometry, error);
  }
  std::uint32_t TakeOcarinaGuideSound() noexcept {
    return mTopScreenOcarina.TakeGuideSound();
  }
  bool
  NativePresentationActive(oot3d::ui::UiSubsystem subsystem) const noexcept;
  bool NativeGameplayPresentationActive() const noexcept;
  bool NativeFrontendPresentationActive() const noexcept;
  bool NativeTouchPresentationActive() const noexcept;
  // The guest address of the version choice's own menu background
  // (common_bg01) while the choice is presented.
  std::optional<uint32_t> NativeVersionChoiceBackdropGuestAddress() const noexcept;
  // One-screen profiles show the version choice only once the player reveals
  // it (see ApplyTopScreenVersionChoiceReveal); the two-screen profile shows
  // it as the game does. The reveal ends with the choice.
  void SetVersionChoiceRevealRequired(bool required) noexcept {
    mVersionChoiceRevealRequired = required;
  }
  bool NativeVersionChoiceAwaitingReveal() const noexcept;
  // The title prompt before the panels enter: a press there reaches the game
  // (which starts the panels) and also counts as the reveal.
  bool NativeVersionChoicePrompting() const noexcept;
  void RevealNativeVersionChoice() noexcept;
  std::optional<oot3d::ui::UiTextureIdentity> NativePauseSharedTextureIdentity(
      oot3d::ui::UiPauseSharedTextureSlot slot) const;
  std::optional<oot3d::ui::UiTextureIdentity>
  NativeActiveFileSelectTextureIdentity() const;
  std::optional<uint32_t> NativePauseSharedTextureGuestAddress(
      oot3d::ui::UiPauseSharedTextureSlot slot) const;

  const NativeUiLifecycleBridgeStats &Stats() const noexcept;
  const oot3d::ui::N64IntegratedUiRuntime &Runtime() const noexcept;
  TopScreenPauseProjectionState &TopScreenPauseProjection() noexcept;

  static const std::vector<uint32_t> &GuestEntryPoints();

private:
  void BeginObservationFrameIfNeeded() noexcept;
  void CaptureState();

  NativeA32Memory &mMemory;
  oot3d::ui::Oot3dUiGuestRoots mRoots;
  oot3d::ui::N64IntegratedUiRuntime mRuntime;
  oot3d::ui::UiRuntimeRouter mRouter;
  NativeUiLifecycleBridgeStats mStats;
  uint64_t mHostFrameGeneration = 0;
  uint64_t mObservationFrameGeneration = UINT64_MAX;
  uint64_t mCapturedGeneration = UINT64_MAX;
  oot3d::ui::Oot3dUiSemanticState mLatestState;
  bool mHasLatestState = false;
  NativeOmoteUraSelectorState mOmoteUraSelector;
  std::array<bool, oot3d::ui::kUiSubsystemCount> mObservedSubsystems{};
  std::optional<oot3d::ui::UiSubsystem> mLastObservedSubsystem;
  TopScreenPauseProjectionState mTopScreenPauseProjection;
  // Captured at the Quest draw, cleared at the next submit.
  std::vector<TopScreenQuestCopiedQuad> mTopScreenQuestBCopies;
  std::optional<TopScreenPauseEdgeGeometry> mTopScreenPauseEdgeGeometry;
  std::optional<TopScreenPauseEdgeGeometry>
      mTopScreenPausePageRedrawEdgeGeometry;
  bool mTopScreenSystemPagePresentation = false;
  TopScreenExtendedInputFrame mTopScreenInput;
  TopScreenOcarinaBrowser mTopScreenOcarina;
  TopScreenItemsHintRuntimeState mTopScreenItemsHint;
  TopScreenWideItemsRuntime *mTopScreenItemsShortcuts = nullptr;
  TopScreenDpadPickerHoldCursor mTopScreenDpadPickerCursor;
  TopScreenDpadPickerHoldCursor mTopScreenItemsShortcutCursor;
  TopScreenTouchCopyState mTopScreenTouchCopies;
  float mTopScreenViewAlpha = 1.0F;
  bool mVersionChoiceRevealRequired = false;
  bool mVersionChoiceRevealed = false;
  TopScreenUiConfig mTopScreenConfig;
};

} // namespace Oot3dNativeGame
