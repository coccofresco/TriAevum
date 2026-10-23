#pragma once

#include "oot3d_native_a32_memory.h"
#include "oot3d_top_screen_dpad_picker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

struct TopScreenWidePageState {
  bool ItemsActive = false;
  bool DungeonMapActive = false;
  bool SystemActive = false;
  bool Wide() const noexcept { return ItemsActive || DungeonMapActive || SystemActive; }
};

// 0x005D325C..0x005D329C / 0x005D345C: Items, System, or an active Map
// page whose native mode is exactly 1. World Map keeps the centred viewport.
bool ReadTopScreenWidePageState(NativeA32Memory &memory,
                                TopScreenWidePageState *page);

// MOV R2,#10 -> MVN R2,#29 at the six map/compass/key producer sites.
// The native owned/absent branches retain their Y, size and visibility.
std::span<const std::uint32_t> TopScreenDungeonMapIconCoordinateSites() noexcept;
bool ExecuteTopScreenDungeonMapIconCoordinate(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);

// After native Items update 0x0042DAEC, the block `mov r0, r4; bl 0x0042DF3C`
// (block starts only: the call is issued here and returns to 0x0041E9AC).
inline constexpr std::uint32_t kTopScreenItemsUpdateBoundary = 0x0041E9A4U;
inline constexpr std::uint32_t kTopScreenItemsInsetNextCall = 0x0042DF3CU;
inline constexpr std::uint32_t kTopScreenItemsInsetNextReturn = 0x0041E9ACU;
// PauseUi_Draw 0x0041EC50 shared projection and Items draw call sites.
inline constexpr std::uint32_t kTopScreenPageProjectionUploadCall = 0x0041EE1CU;
inline constexpr std::uint32_t kTopScreenItemsPageDrawCall = 0x0041EE74U;
// PauseGearPage_Draw main model call block `ldr r0, [r4, #4]; ldr r1, [r0];
// ldr r1, [r1, #0xC]; blx r1`; its native tail resumes at 0x00424C44.
inline constexpr std::uint32_t kTopScreenGearModelDrawCall = 0x00424C34U;
inline constexpr std::uint32_t kTopScreenGearModelDrawResume = 0x00424C44U;
// Native block: two NOPs, then BL 0x00446E98 at 0x00434BA0. The 3.0
// wrapper 0x005F3B74 corrects the shortcut cursor after that native update.
inline constexpr std::uint32_t kTopScreenItemsShortcutCursorBoundary = 0x00434B98U;
inline constexpr std::uint32_t kTopScreenItemsShortcutCursorResume = 0x00434BA4U;
// PauseItemsPage_Draw 0x00434C80 cursor subdraws (PauseQuadGroup_Draw).
inline constexpr std::uint32_t kTopScreenItemsCursorDrawCallA = 0x00434CDCU;
inline constexpr std::uint32_t kTopScreenItemsCursorDrawCallB = 0x00434D48U;
// The Gear item model's override transform: block `ldr r0, [r6, #0xFC];
// mov r1, r8; bl ModelHandle_CopyOverrideTransform` (3.0 redirects the call
// at 0x00425880 to 0x005D2734), returning to 0x00425884.
inline constexpr std::uint32_t kTopScreenViewModelTransformBlock = 0x00425878U;
inline constexpr std::uint32_t kTopScreenViewModelTransformReturn = 0x00425884U;

// SingleScreen 3.0 0x005E3CA0 / 0x005E3728: native quad sub-ranges,
// temporary model transforms and 0.6 / 0.58 projections. Quad 2 is updated
// before native buffer upload; draw-time model writes are restored on return.
class TopScreenWideItemsRuntime final {
public:
  explicit TopScreenWideItemsRuntime(NativeA32Memory &memory) : mMemory(memory) {}

  // 0x0041E9A4: 0x005E3CFC inset update, before native stream upload.
  bool ExecuteItemsUpdate(oot3d::recomp::a32::GuestState &state,
                          oot3d::recomp::a32::ExecutionResult *result,
                          std::uint32_t *blocksConsumed);
  // 0x0041EE1C, the page projection upload: patches the projection and lets
  // the native upload run for Items, dungeon Map or System. `pageOnTop`:
  // this PauseUi_Draw is the upper-screen page.
  bool ExecutePageProjection(oot3d::recomp::a32::GuestState &state, bool pageOnTop);
  // 0x0041EE74, the Items page draw.
  bool ExecuteItemsDraw(oot3d::recomp::a32::GuestState &state,
                        oot3d::recomp::a32::ExecutionResult *result,
                        std::uint32_t *blocksConsumed);
  // 3.0 0x005D5AE4: omit the upper Gear BACK quads, retaining the native tail.
  bool ExecuteGearModelDraw(oot3d::recomp::a32::GuestState &state,
                            oot3d::recomp::a32::ExecutionResult *result,
                            std::uint32_t *blocksConsumed);
  // 0x00434CDC and 0x00434D48.
  bool ExecuteCursorDraw(std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
                         oot3d::recomp::a32::ExecutionResult *result,
                         std::uint32_t *blocksConsumed);
  bool ExecuteShortcutCursor(oot3d::recomp::a32::GuestState &state,
                             oot3d::recomp::a32::ExecutionResult *result,
                             std::uint32_t *blocksConsumed);
  // SingleScreen 3.0 View Model (0x005D2734): while it is open the Gear
  // item model's transform takes its rotation, zoom and offset. Returns false
  // (native block runs unchanged) while it is closed.
  bool ExecuteViewModelTransform(oot3d::recomp::a32::GuestState &state,
                                 oot3d::recomp::a32::ExecutionResult *result,
                                 std::uint32_t *blocksConsumed);
  bool ViewModelActive() const noexcept { return mViewModel.Active; }
  // 3.0 0x005E2B10..0x005E2B84: the Gear "[X] View Model" legend applies, and
  // 0x006102C0 - 90 (its fade-in) and 0x006102C4 (its pulse). The caller
  // still needs the cursor bounds of 0x00504480 and the hint setting.
  bool GearViewModelLegend(std::uint32_t *fade, std::uint32_t *pulse) const noexcept {
    // Gear page 2, no View Model, not modal (0x005D1F40), a viewable
    // previewed item (0x004FC6AC), cursor slots 0..26 (0x0050448C) with a
    // cursor owner (0x00504480) and 90 updates on the same slot.
    // 0x005E0F54: a visible A-hold ring takes the legend's place.
    std::uint32_t page = 0U, equipment = 0U, slot = 0U, owner = 0U, ringSlot = 0U;
    std::int32_t ringFrames = 0;
    if (fade == nullptr || pulse == nullptr || GearHold(&ringSlot, &ringFrames) ||
        mViewModel.Active || mViewModel.Closed ||
        mDpadPicker.Active() || mGearLegendHover <= 0x5AU ||
        !mMemory.Read32(0x00504484U, &page) || page != 2U ||
        !mMemory.Read32(0x004FC6ACU, &equipment) || !mMemory.Read32(0x0050448CU, &slot) ||
        !mMemory.Read32(0x00504480U, &owner) || static_cast<std::int32_t>(equipment) < 0 ||
        equipment == 0x8AU || slot > 0x1AU || owner == 0U)
      return false;
    *fade = mGearLegendHover - 0x5AU;
    *pulse = mGearLegendBlink;
    return true;
  }
  void SetShortcutOrder(const TopScreenItemsShortcutOrder &order) noexcept;
  void SetShortcutOrderChanged(std::function<void(const TopScreenItemsShortcutOrder &)> changed) {
    mShortcutOrderChanged = std::move(changed);
  }
  void SetDpadAssignments(const TopScreenDpadAssignments &child,
                          const TopScreenDpadAssignments &adult) noexcept;
  void SetDpadAssignmentChanged(std::function<void(bool, const TopScreenDpadAssignments &)> changed) {
    mDpadAssignmentChanged = std::move(changed);
  }
  void CancelDpadPicker() noexcept {
    mDpadPicker = {};
    mDpadPickerDraw = {};
    mShortcuts.HoldFrames = -1;
    mGearHoldSlot = -1;
    mGearHoldFrames = 0;
  }
  // A page/close/age transition may skip Items update, including before open.
  void ObserveDpadPickerPage() noexcept {
    if (!mDpadPicker.Active() && mShortcuts.HoldFrames < 0 && mGearHoldSlot < 0) return;
    const bool child = mDpadPicker.Active() ? mDpadPicker.Child : mShortcutDraw.Child;
    const bool gear = mDpadPicker.Active() ? mDpadPicker.Gear : mShortcuts.HoldFrames < 0;
    std::uint32_t page = 0, age = 0;
    const bool pageActive = mMemory.Read32(gear ? 0x00504484U : 0x0050672CU, &page) && page == 2U;
    // A Gear hold has no age yet; the picker and the Items hold do.
    const bool ageKept = (gear && !mDpadPicker.Active()) ||
                         (mMemory.Read32(0x0058795CU, &age) && (age != 0U) == child);
    if (!pageActive || !ageKept) CancelDpadPicker();
  }
  // 3.0 0x005CD470 / 0x005E0F58: the Gear A hold on slot 0..26, shown from
  // its second update until the picker opens on the fifteenth.
  bool GearHold(std::uint32_t *slot, std::int32_t *frames) const noexcept {
    if (slot == nullptr || frames == nullptr || mGearHoldSlot < 0 || mGearHoldSlot > 0x1A ||
        mGearHoldFrames <= 1)
      return false;
    *slot = static_cast<std::uint32_t>(mGearHoldSlot);
    *frames = mGearHoldFrames;
    return true;
  }
  const TopScreenDpadPickerState &DpadPicker() const noexcept { return mDpadPicker; }
  const TopScreenDpadPickerDrawState &DpadPickerDraw() const noexcept { return mDpadPickerDraw; }
  const TopScreenItemsShortcutState &Shortcuts() const noexcept { return mShortcuts; }
  const TopScreenItemsShortcutDrawState &ShortcutDraw() const noexcept { return mShortcutDraw; }
  void Reset() noexcept;

  bool PageWide() const noexcept { return mPageWide; }
  std::uint64_t WidePages() const noexcept { return mWidePages; }
  std::uint64_t WideItemsDraws() const noexcept { return mWideItemsDraws; }
  std::uint64_t CursorDraws() const noexcept { return mCursorDraws; }
  std::uint64_t GearModelDraws() const noexcept { return mGearModelDraws; }
  std::uint64_t ItemsInsetUpdates() const noexcept { return mItemsInsetUpdates; }

private:
  struct Call {
    std::uint32_t Entry = 0;
    std::uint32_t R0 = 0, R1 = 0, R2 = 0, R3 = 0;
    std::uint32_t Model = 0;
    std::uint32_t FirstQuad = 0, QuadCount = 0;
    float OffsetX = 0.0F, OffsetY = 0.0F;
    bool Range = false;
    bool SetS01 = false;
    float S0 = 0.0F, S1 = 0.0F;
    bool SetStack = false;
    std::array<std::uint32_t, 2> Stack{};
  };
  struct SavedWord {
    std::uint32_t Address = 0, Value = 0;
  };
  // Each native call returns to the observed Items site. Step restores the
  // previous call's model before preparing the next one, including on reset.
  struct Sequence {
    std::vector<Call> Calls;
    std::size_t Next = 0;
    std::uint32_t Site = 0;
    // Native continuation after the last call; 0 means Site + 4.
    std::uint32_t Resume = 0;
    std::uint32_t Scratch = 0;
    oot3d::recomp::a32::GuestState Caller{};
    std::array<SavedWord, 8> Saved{};
    std::size_t SavedCount = 0;
    bool Pending() const noexcept { return Site != 0U; }
  };
  void Begin(Sequence &sequence, std::uint32_t site,
             const oot3d::recomp::a32::GuestState &state, std::uint32_t scratch);
  bool Step(Sequence &sequence, oot3d::recomp::a32::GuestState &state,
            oot3d::recomp::a32::ExecutionResult *result,
            std::uint32_t *blocksConsumed);
  bool PrepareCall(Sequence &sequence, Call &call);
  void UpdateItemsInset();
  void ReadShortcutDrawState();
  void ReadDpadPickerDrawState();
  void AppendUiSound(Sequence &sequence, std::uint32_t sound);
  void ConsumePickerInput();
  void UpdateShortcuts(Sequence &sequence);
  void AppendShortcutDescription(Sequence &sequence);
  // 3.0 0x005D28C4 (Gear page entry and update), 0x005D0B2C (picker item),
  // 0x005D24FC (enter), 0x005D2274 (update) and 0x005D21E8 (exit).
  void UpdateGearLegend();
  // 3.0 0x005E4740 (Gear A hold) and 0x005D0878 (slot to picker action).
  void UpdateGearHold(Sequence &sequence);
  std::int32_t GearSlotAction(std::int32_t slot);
  void UpdateViewModel(Sequence &sequence);
  std::int32_t PickerViewItem();
  void EnterViewModel(Sequence &sequence, std::uint32_t mode, std::int32_t item);
  void StepViewModel(Sequence &sequence, std::uint32_t held, std::uint32_t pressed);
  void ExitViewModel(Sequence &sequence, bool restore);
  void WriteViewModelTransform(std::uint32_t model, std::uint32_t matrix);
  void WriteWord(std::uint32_t address, std::uint32_t value);
  void TraceItemsState();
  void SaveWrite(Sequence &sequence, std::uint32_t address, std::uint32_t value);
  bool Restore(Sequence &sequence) noexcept;
  std::uint32_t ReadWord(std::uint32_t address);
  float ReadScalar(std::uint32_t address);
  std::uint32_t WriteProjection(std::uint32_t address, std::uint32_t offset);
  void AppendProjection(Sequence &sequence, std::uint32_t projection,
                        std::uint32_t identity);
  void AppendRange(Sequence &sequence, std::uint32_t model,
                   std::uint32_t first, std::uint32_t count, float x, float y = 0.0F);
  void AppendAssignments(Sequence &sequence, std::uint32_t group);
  void AppendCursor(Sequence &sequence, std::uint32_t object, bool selection,
                    std::uint32_t cursor, std::uint32_t items, std::uint32_t identity);
  bool ResolveCursorProjection(std::uint32_t object, bool selection);
  bool AssignmentGroupVisible();
  void BuildItemsSequence(Sequence &sequence, std::uint32_t scratch);

  NativeA32Memory &mMemory;
  bool mPageWide = false;
  bool mItemsWide = false;
  bool mPageOnTop = false;
  Sequence mItems{};
  Sequence mGear{};
  Sequence mUpdate{};
  Sequence mShortcutCursor{};
  TopScreenItemsShortcutState mShortcuts;
  TopScreenItemsShortcutDrawState mShortcutDraw;
  TopScreenDpadPickerState mDpadPicker;
  TopScreenDpadPickerDrawState mDpadPickerDraw;
  TopScreenDpadAssignments mChildDpad = kTopScreenChildDpadDefaults;
  TopScreenDpadAssignments mAdultDpad = kTopScreenAdultDpadDefaults;
  std::function<void(bool, const TopScreenDpadAssignments &)> mDpadAssignmentChanged;
  bool mShortcutDescriptionDirty = false;
  std::function<void(const TopScreenItemsShortcutOrder &)> mShortcutOrderChanged;
  // 3.0 View Model state: 0x0060ECC4 (open), 0x0060F24C (mode: 1 picker,
  // 2 Gear), 0x0060D254 (entry button), 0x0060D250 (previewed item to
  // restore), 0x0060F23C/0x0060F23E (yaw, pitch), 0x0060F240 (entry button
  // released), 0x0060F244/0x0060F248 (offset), 0x0060D258 (zoom),
  // 0x0060F254 (Gear held), 0x0060F224 (closed during this update).
  struct ViewModelState {
    bool Active = false;
    std::uint32_t Mode = 0;
    std::uint32_t EntryButton = 0;
    std::int32_t SavedEquipment = -1;
    std::uint16_t Yaw = 0;
    std::int16_t Pitch = 0;
    bool Released = false;
    float OffsetX = 0.0F, OffsetY = 0.0F;
    float Zoom = 1.0F;
    std::uint32_t PreviousHeld = 0;
    bool Closed = false;
  };
  ViewModelState mViewModel{};
  // 0x005E46A0..0x005E4738: 0x006102C4 counts every update, 0x006102C0 the
  // updates on one Gear cursor slot (0x0060D30C), capped at 102.
  std::uint32_t mGearLegendBlink = 0;
  std::uint32_t mGearLegendHover = 0;
  std::int32_t mGearLegendSlot = -1;
  // 0x0060D23C (held Gear slot, -1 none) and 0x0060ECCC (its updates).
  std::int32_t mGearHoldSlot = -1;
  std::int32_t mGearHoldFrames = 0;
  std::uint32_t mAssignmentFrame = 0;
  std::int32_t mAssignmentDelay = 0;
  std::uint64_t mWidePages = 0;
  std::uint64_t mWideItemsDraws = 0;
  std::uint64_t mCursorDraws = 0;
  std::uint64_t mGearModelDraws = 0;
  std::uint64_t mItemsInsetUpdates = 0;
};


} // namespace Oot3dNativeGame
