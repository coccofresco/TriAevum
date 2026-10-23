#include "oot3d_top_screen_wide_items_runtime.h"
#include "oot3d_top_screen_items_layout.h"
#include "oot3d_native_trigonometry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {
constexpr std::uint32_t kItemsPageState = 0x0050672CU;
constexpr std::uint32_t kMapPageState = 0x00506CE8U;
constexpr std::uint32_t kMapMode = 0x004FDABCU;
constexpr std::uint32_t kSystemPageState = 0x0050A530U;
constexpr std::array<std::uint32_t, 6> kDungeonMapIconCoordinateSites{
    0x004426B4U, 0x004426D4U, 0x00442704U,
    0x00442730U, 0x00442760U, 0x0044278CU};
constexpr std::uint32_t kItemsModel = 0x00506718U;
constexpr std::uint32_t kItemsRenderBuffer = 0x0050671CU;
constexpr std::uint32_t kMaterialState = 0x005C58D8U;
constexpr std::uint32_t kBindCommandList = 0x0032471CU;
constexpr std::uint32_t kUploadUniformBlock0 = 0x003246ECU;
constexpr std::uint32_t kUploadUniformBlock4 = 0x003246BCU;
constexpr std::uint32_t kAccumulateWriter = 0x002F9C74U;
constexpr std::uint32_t kQuadGroupDraw = 0x002FB934U;
constexpr std::uint32_t kAssignmentGroupDraw = 0x002F780CU;
constexpr std::uint32_t kAssignmentDraw = 0x002F78A0U;
constexpr std::uint32_t kWideScale = 0xBBA3D70AU;
constexpr std::uint32_t kPageOffset = 0x3F4CCCCDU;
constexpr std::uint32_t kItemsOffset = 0x3F19999AU;
constexpr std::uint32_t kCursorOffset = 0x3F147AE1U; // 3.0 0x005D60CC: 0.58.
// 0x20 outgoing, then four 64-byte matrices.
constexpr std::uint32_t kScratch = 0x120U;
constexpr std::uint32_t kQuadBytes = 48U;
constexpr std::array<std::uint32_t, 6> kTransformOffsets{0xCU, 0x10U, 0x18U, 0x1CU, 0x20U, 0x28U};
// View Model inputs: the native upper-screen 3D model of the Gear cursor item
// and that item's id.
constexpr std::uint32_t kViewModel = 0x004FC67CU;
constexpr std::uint32_t kViewEquipment = 0x004FC6ACU;
// 0x002FCAD4 reads 0x0050446C+0x18, this same word, so Gear is active at 2.
constexpr std::uint32_t kGearPageState = 0x00504484U;
constexpr std::uint32_t kGearCursorSlot = 0x0050448CU;
constexpr std::uint32_t kCirclePadX = 0x005655ACU; // s16, then Y at +2.
constexpr std::uint32_t kModelHandleCopyOverrideTransform = 0x003721E0U;
// 0x002E9B00(item) previews an item's model (table 0x004D2EC0, 0x8A none);
// 0x002E112C(model, object) requests that object directly.
constexpr std::uint32_t kPreviewItem = 0x002E9B00U;
constexpr std::uint32_t kPreviewObject = 0x002E112CU;
constexpr std::uint32_t kItemPreviewObjects = 0x004D2EC0U;

std::uint32_t Word(float value) { return std::bit_cast<std::uint32_t>(value); }

// 3.0 0x005CAE40: an owner's selected model stream, or 0.
bool ResolveStream(NativeA32Memory &memory, std::uint32_t owner, std::uint32_t offset,
                   std::uint32_t *stream) {
  *stream = 0U;
  if (owner == 0U) return true;
  std::uint32_t model = 0U, instance = 0U, index = 0U, selected = 0U;
  if (!memory.Read32(owner + offset, &model)) return false;
  if (model == 0U || (model & 3U) != 0U) return true;
  if (!memory.Read32(model + 4U, &instance)) return false;
  if (instance == 0U || (instance & 3U) != 0U) return true;
  if (!memory.Read32(instance + 0x124U, &index)) return false;
  if (index > 3U) return true;
  if (!memory.Read32(instance + (index + 0x68U) * 4U, &selected)) return false;
  if ((selected & 3U) == 0U) *stream = selected;
  return true;
}

std::array<std::uint32_t, 16> Projection(std::uint32_t offset) {
  // 3.0 0x005CD760, word for word, negative zeros included.
  return {0x00000000U, 0xBC088889U, 0x00000000U, 0x3F800000U,
          kWideScale,  0x80000000U, 0x80000000U, offset,
          0x00000000U, 0x00000000U, 0x3F000000U, 0xBF000000U,
          0x00000000U, 0x00000000U, 0x00000000U, 0x3F800000U};
}
constexpr std::array<std::uint32_t, 16> kIdentity{
    0x3F800000U, 0, 0, 0, 0, 0x3F800000U, 0, 0,
    0, 0, 0x3F800000U, 0, 0, 0, 0, 0x3F800000U};
} // namespace

bool ReadTopScreenWidePageState(NativeA32Memory &memory,
                                TopScreenWidePageState *page) {
  if (page == nullptr) return false;
  *page = {};
  std::uint32_t items = 0U, map = 0U, mode = 0U, system = 0U;
  if (!memory.Read32(kItemsPageState, &items) ||
      !memory.Read32(kMapPageState, &map) ||
      !memory.Read32(kSystemPageState, &system))
    return false;
  // PauseCameraControl_IsActive (0x002FCDD4) actually reads Map owner+0x38.
  // 0x002F1268 returns true only for shared map owner 0x004FDA6C+0x50 == 1.
  if (map != 0U && !memory.Read32(kMapMode, &mode)) return false;
  page->ItemsActive = items != 0U;
  page->DungeonMapActive = map != 0U && mode == 1U;
  page->SystemActive = system != 0U; // PauseSystemMenu_IsActive, 0x004392B0.
  return true;
}

std::span<const std::uint32_t> TopScreenDungeonMapIconCoordinateSites() noexcept {
  return kDungeonMapIconCoordinateSites;
}

bool ExecuteTopScreenDungeonMapIconCoordinate(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed) {
  if (std::find(kDungeonMapIconCoordinateSites.begin(),
                kDungeonMapIconCoordinateSites.end(), pc) ==
      kDungeonMapIconCoordinateSites.end())
    return false;
  state.r[2] = 0xFFFFFFE2U; // MVN R2,#29: -30, without changing flags.
  state.r[15] = pc + 4U;
  *result = {oot3d::recomp::a32::ExitKind::Branch, pc + 4U,
             oot3d::recomp::a32::FallbackReason::None, pc};
  if (blocksConsumed != nullptr) *blocksConsumed = 1U;
  return true;
}

std::uint32_t TopScreenWideItemsRuntime::ReadWord(std::uint32_t address) {
  std::uint32_t value = 0U;
  if (!mMemory.Read32(address, &value))
    throw std::runtime_error("SingleScreen Items cannot read native draw state");
  return value;
}

void TopScreenWideItemsRuntime::WriteWord(std::uint32_t address, std::uint32_t value) {
  if (!mMemory.Write32(address, value))
    throw std::runtime_error("SingleScreen Items cannot write native shortcut state");
}

float TopScreenWideItemsRuntime::ReadScalar(std::uint32_t address) {
  return std::bit_cast<float>(ReadWord(address));
}

bool TopScreenWideItemsRuntime::Restore(Sequence &sequence) noexcept {
  bool restored = true;
  while (sequence.SavedCount != 0U) {
    const auto saved = sequence.Saved[--sequence.SavedCount];
    if (!mMemory.Write32(saved.Address, saved.Value)) restored = false;
  }
  return restored;
}

void TopScreenWideItemsRuntime::Reset() noexcept {
  (void)Restore(mItems);
  (void)Restore(mGear);
  (void)Restore(mUpdate);
  (void)Restore(mShortcutCursor);
  mPageOnTop = false;
  mGear = {};
  mPageWide = false;
  mItemsWide = false;
  mItems = {};
  mUpdate = {};
  mShortcutCursor = {};
  const auto order = mShortcuts.Order;
  mShortcuts = {};
  mShortcuts.Order = order;
  mShortcutDraw = {};
  mDpadPicker = {};
  mDpadPickerDraw = {};
  mShortcutDescriptionDirty = false;
  mViewModel = {};
  mGearLegendBlink = mGearLegendHover = 0U;
  mGearLegendSlot = -1;
  mGearHoldSlot = -1;
  mGearHoldFrames = 0;
  mAssignmentFrame = 0U;
  mAssignmentDelay = 0;
}

void TopScreenWideItemsRuntime::Begin(Sequence &sequence, std::uint32_t site,
                                      const oot3d::recomp::a32::GuestState &state,
                                      std::uint32_t scratch) {
  if (state.r[13] < scratch || !mMemory.IsWritable(state.r[13] - scratch, scratch))
    throw std::runtime_error("SingleScreen Items has no valid caller stack");
  sequence = {};
  sequence.Site = site;
  sequence.Scratch = scratch;
  sequence.Caller = state;
}

void TopScreenWideItemsRuntime::SaveWrite(Sequence &sequence, std::uint32_t address,
                                          std::uint32_t value) {
  if (sequence.SavedCount == sequence.Saved.size())
    throw std::runtime_error("SingleScreen Items exhausted temporary draw state");
  sequence.Saved[sequence.SavedCount++] = {address, ReadWord(address)};
  if (!mMemory.Write32(address, value))
    throw std::runtime_error("SingleScreen Items cannot write temporary draw state");
}

void TopScreenWideItemsRuntime::UpdateItemsInset() {
  // 0x005F3BA0 calls 0x005E460C after native 0x0042DAEC. Its 0x005E3CFC
  // tail writes quad 2 before the native model uploads its separate stream.
  // Persist this update like 0x005C7BDC / 0x005C7C68; never undo it at draw.
  const auto buffer = ReadWord(kItemsRenderBuffer);
  if (buffer == 0U || (buffer & 3U) != 0U ||
      static_cast<std::int32_t>(ReadWord(buffer)) <= 2)
    return;
  const auto positions = ReadWord(buffer + 0xCU);
  const auto texcoords = ReadWord(buffer + 0x14U);
  const float width = ReadScalar(buffer + 4U), height = ReadScalar(buffer + 8U);
  if (positions == 0U || texcoords == 0U || (positions & 3U) != 0U || (texcoords & 3U) != 0U ||
      !std::isfinite(width) || !std::isfinite(height) || width <= 0.0F || height <= 0.0F ||
      !mMemory.IsWritable(positions, 3U * kQuadBytes) ||
      !mMemory.IsWritable(texcoords, 3U * 32U))
    throw std::runtime_error("SingleScreen Items inset buffer is invalid");
  const bool active = ReadWord(kItemsPageState) != 0U; // 0x005CA734.
  const auto &rect = kTopScreenItemsInsetRect;
  const auto &atlas = kTopScreenItemsInsetAtlas;
  const auto write = [this](std::uint32_t address, float value) {
    if (!mMemory.Write32(address, Word(value)))
      throw std::runtime_error("SingleScreen Items cannot update its inset");
  };
  for (std::uint32_t vertex = 0; vertex < 4U; ++vertex) {
    const float x = rect[0] + (active ? (vertex % 2U) * rect[2] : 0.0F);
    const float y = rect[1] + (active ? (vertex / 2U) * rect[3] : 0.0F);
    const auto position = positions + 2U * kQuadBytes + vertex * 12U;
    write(position, x);
    write(position + 4U, y);
    write(position + 8U, 0.0F);
    const float u = (atlas[0] + (vertex % 2U) * atlas[2]) / width;
    const float v = (height - atlas[1] - (vertex / 2U) * atlas[3]) / height;
    const auto uv = texcoords + 2U * 32U + vertex * 8U;
    write(uv, u);
    write(uv + 4U, v);
  }
  ++mItemsInsetUpdates;
}

bool TopScreenWideItemsRuntime::ExecuteItemsUpdate(
    oot3d::recomp::a32::GuestState &state, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (mUpdate.Pending()) return Step(mUpdate, state, result, blocksConsumed);
  UpdateItemsInset();
  Begin(mUpdate, kTopScreenItemsUpdateBoundary, state, 0x20U);
  try {
    // 0x005E460C: clears the View Model's one-update "just closed" flag
    // (0x005E461C), counts the Gear legend, runs the Gear A hold, then
    // 0x005D29E4 advances the picker, possibly opened by that hold.
    mViewModel.Closed = false;
    UpdateGearLegend();
    UpdateGearHold(mUpdate);
    UpdateShortcuts(mUpdate);
    UpdateViewModel(mUpdate);
  } catch (...) {
    mUpdate = {};
    throw;
  }
  // The block's own `mov r0, r4; bl 0x0042DF3C`, returning to a block start.
  mUpdate.Caller.r[0] = state.r[4];
  mUpdate.Caller.r[14] = kTopScreenItemsInsetNextReturn;
  mUpdate.Resume = kTopScreenItemsInsetNextCall;
  return Step(mUpdate, state, result, blocksConsumed);
}

void TopScreenWideItemsRuntime::SetShortcutOrder(const TopScreenItemsShortcutOrder &order) noexcept {
  const auto valid = ValidateTopScreenItemsShortcutOrder(order);
  if (valid == mShortcuts.Order) return;
  mShortcuts.Order = valid;
  mShortcuts.MovingRow = mShortcuts.SwapTarget = mShortcuts.SwapFrame = mShortcuts.HoldFrames = -1;
  mShortcuts.SelectionSettled = false;
  mShortcutDescriptionDirty = true;
  mDpadPicker = {};
}

void TopScreenWideItemsRuntime::SetDpadAssignments(
    const TopScreenDpadAssignments &child, const TopScreenDpadAssignments &adult) noexcept {
  if (mDpadPicker.Active() && (mDpadPicker.Child ? child : adult) != mDpadPicker.Assignments)
    mDpadPicker = {};
  mChildDpad = child;
  mAdultDpad = adult;
}

void TopScreenWideItemsRuntime::ConsumePickerInput() {
  // 0x005D2B30 clears all four native edge/repeat lanes, including shoulders.
  // Keep held HID intact so the modal can detect releases on the next update.
  for (const auto address : {0x0050AF14U, 0x0050AF18U, 0x0050AF2CU, 0x0050AF30U})
    WriteWord(address, 0U);
}

void TopScreenWideItemsRuntime::AppendUiSound(Sequence &sequence, std::uint32_t sound) {
  if (sound == 0U) return;
  // 0x005D1FE4 -> native sound: position=null, bank=4, volume/frequency/reverb.
  Call play{0x0037547CU, sound, 0U, 4U, 0x0054AC20U};
  play.SetStack = true;
  play.Stack = {0x0054AC20U, 0x0054AC24U};
  sequence.Calls.push_back(play);
}

void TopScreenWideItemsRuntime::ReadDpadPickerDrawState() {
  mDpadPickerDraw = {};
  constexpr std::uint32_t save = 0x00587958U;
  const auto byte = [&](std::uint32_t address) {
    std::uint8_t value = 0xFF;
    if (!mMemory.Read8(address, &value))
      throw std::runtime_error("SingleScreen picker cannot read native inventory");
    return value;
  };
  std::uint16_t owned = 0, equipped = 0;
  if (!mMemory.Read16(save + 0xB6U, &owned) || !mMemory.Read16(save + 0x8AU, &equipped))
    throw std::runtime_error("SingleScreen picker cannot read native equipment");
  const auto inventory = [&](std::uint8_t item) {
    const auto slot = byte(0x0053CC44U + item);
    return slot < 24U && byte(save + 0x8CU + slot) == item;
  };
  const auto set = [&](TopScreenDpadAction action, std::uint8_t item) {
    mDpadPickerDraw.Items[static_cast<unsigned>(action)] = item;
  };
  set(TopScreenDpadAction::Ocarina, mShortcutDraw.Ocarina);
  set(TopScreenDpadAction::ItemZr, byte(save + 0x83U));
  set(TopScreenDpadAction::ItemZl, byte(save + 0x84U));
  if ((owned & 0x2000U) || byte(save + 0xA4U) == 0x45U) set(TopScreenDpadAction::IronBoots, 0x45U);
  if ((owned & 0x4000U) || byte(save + 0xA5U) == 0x46U) set(TopScreenDpadAction::HoverBoots, 0x46U);
  if (mShortcutDraw.BootsOwned) set(TopScreenDpadAction::AllBootsToggle, mShortcutDraw.Boots);
  if (inventory(0x0EU)) set(TopScreenDpadAction::Boomerang, 0x0EU);
  if (inventory(6U)) set(TopScreenDpadAction::Slingshot, 6U);
  // 0x005D07AC / 0x005D09E8: cycle icons use the equipped member.
  const auto group = [&](TopScreenDpadAction action, unsigned shift, unsigned mask, unsigned first) {
    const unsigned member = (equipped >> shift) & 15U;
    if (std::popcount((owned >> shift) & mask) >= 2 && member >= 1 && member <= 3)
      set(action, static_cast<std::uint8_t>(first + member - 1));
  };
  group(TopScreenDpadAction::SwordToggle, 0, 6, 0x3BU);
  group(TopScreenDpadAction::TunicToggle, 8, 7, 0x41U);
  group(TopScreenDpadAction::ShieldToggle, 4, 6, 0x3EU);
  mDpadPickerDraw.ViewModel = mDpadPicker.Active() && ReadWord(kViewModel) != 0U &&
                              PickerViewItem() >= 0;
}

std::int32_t TopScreenWideItemsRuntime::PickerViewItem() {
  // 0x005D0B2C: the picked action's item. View and the minimap have none;
  // 0x005D0B4C shows the sword actually carried as 0x3D.
  using Action = TopScreenDpadAction;
  const auto action = mDpadPicker.Action;
  if (static_cast<unsigned>(action) <= 1U || action == Action::MinimapToggle) return -1;
  constexpr std::uint32_t save = 0x00587958U;
  std::uint16_t equipped = 0U;
  std::uint8_t zr = 0xFF, zl = 0xFF;
  if (!mMemory.Read16(save + 0x8AU, &equipped) || !mMemory.Read8(save + 0x83U, &zr) ||
      !mMemory.Read8(save + 0x84U, &zl))
    throw std::runtime_error("SingleScreen View Model cannot read the equipment");
  // 0x005D09E8 reads the equipped member directly, without 0x005D07AC.
  const auto member = [&](unsigned shift, std::int32_t base, std::int32_t fallback) {
    const auto value = static_cast<std::int32_t>((equipped >> shift) & 15U);
    return value >= 1 && value <= 3 ? base + value : fallback;
  };
  std::int32_t item = 0xFF;
  switch (action) {
  case Action::Ocarina: item = mShortcutDraw.Ocarina != 0xFF ? mShortcutDraw.Ocarina : 7; break;
  case Action::IronBoots: item = 0x45; break;
  case Action::HoverBoots: item = 0x46; break;
  case Action::ItemZr: item = zr >= 0x3EU ? 0xFF : zr; break;
  case Action::ItemZl: item = zl >= 0x3EU ? 0xFF : zl; break;
  case Action::SwordToggle: item = member(0, 0x3A, 0x3B); break;
  case Action::AllBootsToggle: item = member(12, 0x43, 0x44); break;
  case Action::Boomerang: item = 0x0E; break;
  case Action::Slingshot: item = 6; break;
  case Action::TunicToggle: item = member(8, 0x40, 0x41); break;
  case Action::ShieldToggle: item = member(4, 0x3D, 0x3E); break;
  default: break;
  }
  if (item == 0x3D) {
    std::uint8_t flag = 0U;
    std::uint16_t owned = 0U;
    if (!mMemory.Read8(save + 0x52U, &flag) || !mMemory.Read16(save + 0xB6U, &owned))
      throw std::runtime_error("SingleScreen View Model cannot read the sword state");
    if (flag != 0U) item = 0x7B;
    else if ((owned & 8U) != 0U) item = 0x55;
  }
  return item == 0x44 || item > 0x7B ? -1 : item;
}

void TopScreenWideItemsRuntime::ReadShortcutDrawState() {
  mShortcutDraw = {};
  constexpr std::uint32_t save = 0x00587958U;
  std::uint16_t owned = 0, equipped = 0;
  std::uint8_t iron = 0, hover = 0;
  if (!mMemory.Read16(save + 0xB6U, &owned) || !mMemory.Read16(save + 0x8AU, &equipped) ||
      !mMemory.Read8(save + 0xA4U, &iron) || !mMemory.Read8(save + 0xA5U, &hover))
    throw std::runtime_error("SingleScreen Items cannot read shortcut equipment");
  mShortcutDraw.Child = ReadWord(save + 4U) != 0U;
  // 0x005D07D8 requires BOTH special boots; its child restriction is only
  // in 0x005D09B8, so an owned child boots icon is visible but dimmed.
  mShortcutDraw.BootsOwned = ((owned & 0x2000U) != 0 || iron == 0x45U) &&
                            ((owned & 0x4000U) != 0 || hover == 0x46U);
  const auto boots = equipped >> 12;
  mShortcutDraw.Boots = boots >= 1 && boots <= 3 ? static_cast<std::uint8_t>(0x43U + boots) : 0x44U;
  // 0x005D09E8 prefers Ocarina of Time when both inventory slots are set.
  for (const std::uint8_t item : {7U, 8U}) {
    std::uint8_t slot = 0xFF, value = 0xFF;
    if (!mMemory.Read8(0x0053CC44U + item, &slot) ||
        (slot < 24U && !mMemory.Read8(save + 0x8CU + slot, &value)))
      throw std::runtime_error("SingleScreen Items cannot read shortcut inventory");
    if (slot < 24U && value == item) mShortcutDraw.Ocarina = item;
  }
  // The column itself needs the Items page; a Gear-page picker reads the
  // equipment above for its icons.
  if (ReadWord(kItemsPageState) == 0U || ReadWord(0x00506758U) != 0U ||
      ReadWord(0x0050677CU) != 0U) return;
  mShortcutDraw.Dimmed = static_cast<std::int32_t>(ReadWord(0x00506730U)) > 1;
  mShortcutDraw.Active = true;
}

void TopScreenWideItemsRuntime::AppendShortcutDescription(Sequence &sequence) {
  // 0x005E2D60 clears the native grid description. The boots action keeps
  // its native equipment description; the other three are host actions.
  sequence.Calls.push_back({0x002E9B00U, 0xFFFFFFFFU});
  sequence.Calls.push_back({0x002E9A1CU, 0x005C1878U});
  if (mShortcuts.SelectedRow < 0) return;
  const auto action = ResolveTopScreenItemsShortcutAction(mShortcuts.Order, mShortcuts.SelectedRow);
  if (action == TopScreenDpadAction::AllBootsToggle && mShortcutDraw.BootsOwned &&
      mShortcutDraw.Boots != 0x44U) {
    sequence.Calls.push_back({0x002E9A3CU, 0x005C1878U, 0x700U + mShortcutDraw.Boots, 1U});
    if (!mMemory.Write8(0x005C1885U, 1U))
      throw std::runtime_error("SingleScreen Items cannot select the boots description");
  }
}

void TopScreenWideItemsRuntime::UpdateShortcuts(Sequence &sequence) {
  ObserveDpadPickerPage();
  ReadShortcutDrawState();
  TopScreenItemsShortcutInput input;
  input.Active = ReadWord(kItemsPageState) == 2U;
  input.Column = ReadWord(0x00506770U);
  input.Row = ReadWord(0x00506774U);
  const auto directions = ReadWord(0x0050AF30U);
  input.Directions = directions != 0U ? directions : ReadWord(0x0050AF2CU);
  const auto pressed = ReadWord(0x0050AF18U);
  input.Pressed = pressed != 0U ? pressed : ReadWord(0x0050AF14U);
  input.Held = ReadWord(0x0050AF10U);
  const bool pickerViewModel = mViewModel.Active && mViewModel.Mode == 1U;
  // 0x005CD22C: a picker lives while the page that opened it is active.
  const bool pickerPage = mDpadPicker.Gear ? ReadWord(kGearPageState) == 2U : input.Active;
  if (pickerViewModel && mDpadPicker.Active() && pickerPage &&
      mShortcutDraw.Child == mDpadPicker.Child) {
    // 0x005D2AC0: mode 1 runs inside the picker, which stays frozen. Its
    // edge uses the picker's previous held buttons (0x0060F230).
    const auto pressed = input.Held & ~mDpadPicker.PreviousHeld;
    mDpadPicker.PreviousHeld = input.Held;
    StepViewModel(sequence, input.Held, pressed);
    ConsumePickerInput();
    ReadDpadPickerDrawState();
    return;
  }
  if (mDpadPicker.Active()) {
    const bool wasChild = mDpadPicker.Child;
    const auto update = AdvanceTopScreenDpadPicker(
        mDpadPicker, pickerPage, mShortcutDraw.Child, input.Held);
    ConsumePickerInput();
    if (update.Changed) {
      // Store before the callback: Preview may synchronously refresh this runtime.
      auto &assignments = wasChild ? mChildDpad : mAdultDpad;
      assignments = mDpadPicker.Assignments;
      const auto snapshot = assignments;
      if (mDpadAssignmentChanged) mDpadAssignmentChanged(wasChild, snapshot);
    }
    AppendUiSound(sequence, update.Sound);
    if (pickerPage) {
      ReadDpadPickerDrawState();
      if (update.ViewModel) {
        // 0x005D2CF8: 0x005D24FC(1, item) previews the picked item.
        const auto item = PickerViewItem();
        if (item >= 0) EnterViewModel(sequence, 1U, item);
      }
      return;
    }
  }
  // 0x005D2A58 / 0x005D2A8C: a closed picker closes mode 1 without restore.
  if (pickerViewModel) ExitViewModel(sequence, false);
  for (unsigned row = 0; row < 4; ++row)
    input.Available[row] = TopScreenItemsShortcutAvailable(
        ResolveTopScreenItemsShortcutAction(mShortcuts.Order, row), mShortcutDraw);
  std::uint16_t touch = 0, touchX = 0, touchY = 0;
  if (!mMemory.Read16(0x0050BB40U, &touch) || !mMemory.Read16(0x0050BB3AU, &touchX) ||
      !mMemory.Read16(0x0050BB3CU, &touchY))
    throw std::runtime_error("SingleScreen Items cannot read shortcut touch input");
  // 0x005E3EC4: lower native touch space uses the compact transform.
  const float x = (static_cast<float>(touchX) - 2.0F) / 0.85F - 44.0F;
  if (touch != 0 && touchX <= 255U && touchY <= 197U && x >= -37.0F && x <= 5.0F) {
    for (int row = 0; row < 4; ++row)
      if (touchY >= row * 48 + 7 && touchY <= row * 48 + 49) input.TouchRow = row;
    if (input.TouchRow >= 0 && input.TouchRow == mShortcuts.SelectedRow) input.Pressed |= 1U;
  }
  const auto update = AdvanceTopScreenItemsShortcuts(mShortcuts, input);
  if (!input.Active && !mDpadPicker.Gear) mDpadPicker = {};
  if (update.ConsumeDirections != 0U) {
    for (const auto address : {0x0050AF2CU, 0x0050AF30U})
      WriteWord(address, ReadWord(address) & ~update.ConsumeDirections);
  }
  if (update.ConsumeButtons != 0U) {
    for (const auto address : {0x0050AF14U, 0x0050AF18U})
      WriteWord(address, ReadWord(address) & ~update.ConsumeButtons);
  }
  if (update.Entered) {
    const auto slot = ReadWord(0x00506760U);
    const auto group = ReadWord(0x00506734U);
    if (slot < 24U && group != 0U) {
      // Native 0x002EB304 coordinates, followed by 0x002F8D40's 42x42 reset.
      std::uint32_t x = 11U + (slot % 6U) * 48U, y = 7U + (slot / 6U) * 48U;
      if (slot == 5U || slot == 23U) { x = 271U; y = slot == 5U ? 7U : 191U; }
      if (slot == 11U) { x = 275U; y = 73U; }
      if (slot == 17U) { x = 263U; y = 125U; }
      Call restore{0x002F8D40U, group, slot, x, y};
      restore.SetStack = true;
      restore.Stack = {42U, 42U};
      sequence.Calls.push_back(restore);
    }
  }
  if (update.CursorChanged) {
    WriteWord(0x00506770U, update.Column);
    WriteWord(0x00506774U, update.Row);
    if (!update.Left) {
      WriteWord(0x0050678CU, 0U);
      WriteWord(0x00506790U, update.Row);
      WriteWord(0x005067A4U, 1U);
      AppendShortcutDescription(sequence);
    }
  }
  // 0x005E45C0 leaves previous-column/history to the native horizontal
  // navigation. Only cancellation/page changes clear the shortcut pin here.
  if (update.Left && !update.CursorChanged) {
    WriteWord(0x0050678CU, update.Column);
    WriteWord(0x00506790U, update.Row);
    WriteWord(0x005067A4U, 0U);
  }
  if (mShortcutDescriptionDirty && mShortcuts.SelectedRow >= 0 && !update.CursorChanged)
    AppendShortcutDescription(sequence);
  mShortcutDescriptionDirty = false;
  if (update.OrderChanged) {
    AppendShortcutDescription(sequence);
    if (mShortcutOrderChanged) mShortcutOrderChanged(mShortcuts.Order);
  }
  if (update.Action != TopScreenDpadAction::None) {
    OpenTopScreenDpadPicker(mDpadPicker, update.Action, mShortcutDraw.Child,
                           mShortcutDraw.Child ? mChildDpad : mAdultDpad, input.Held);
    // 0x005F3BA0 calls 0x005D29E4 after the row handler in this same update.
    (void)AdvanceTopScreenDpadPicker(mDpadPicker, true, mShortcutDraw.Child, input.Held);
    ReadDpadPickerDrawState();
    ConsumePickerInput();
    AppendUiSound(sequence, 0x01000497U);
  }
  if (update.UiSound != TopScreenItemsShortcutUpdate::Sound::None) {
    const auto sound = update.UiSound == TopScreenItemsShortcutUpdate::Sound::Move ? 0x0100048EU :
                       update.UiSound == TopScreenItemsShortcutUpdate::Sound::Decide ? 0x0100048DU :
                       0x0100048FU;
    AppendUiSound(sequence, sound);
  }
}

void TopScreenWideItemsRuntime::UpdateViewModel(Sequence &sequence) {
  // 0x005D28C4 after the picker 0x005D29E4. Its 0x005CB8BC HID buttons are
  // the native held lane; the edge is against the previous update.
  const auto held = ReadWord(0x0050AF10U);
  const auto pressed = held & ~mViewModel.PreviousHeld;
  mViewModel.PreviousHeld = held;
  const bool gear = ReadWord(kGearPageState) == 2U;
  if (mViewModel.Active) {
    // 0x005D28EC: the picker owns mode 1.
    if (mViewModel.Mode != 2U) return;
    if (gear) StepViewModel(sequence, held, pressed);
    else ExitViewModel(sequence, false);
    ConsumePickerInput();
    return;
  }
  if (mDpadPicker.Active() || (pressed & 0x400U) == 0U || !gear) return;
  const auto equipment = static_cast<std::int32_t>(ReadWord(kViewEquipment));
  if (equipment < 0 || equipment == 0x8A) return;
  EnterViewModel(sequence, 2U, -1);
  ConsumePickerInput();
}

void TopScreenWideItemsRuntime::UpdateGearLegend() {
  // 0x005E46A0: pulse every update; hover while the Gear cursor stays put.
  ++mGearLegendBlink;
  if (ReadWord(kGearPageState) != 2U) {
    mGearLegendHover = 0U;
    mGearLegendSlot = -1;
    return;
  }
  const auto slot = static_cast<std::int32_t>(ReadWord(kGearCursorSlot));
  if (slot != mGearLegendSlot) {
    mGearLegendHover = 0U;
    mGearLegendSlot = slot;
  } else if (mGearLegendHover <= 0x65U) {
    ++mGearLegendHover;
  }
}

std::int32_t TopScreenWideItemsRuntime::GearSlotAction(std::int32_t slot) {
  // 0x005D0878: swords, shields and tunics cycle; slot 18 is the Ocarina.
  TopScreenDpadAction action = TopScreenDpadAction::None;
  if (slot >= 0 && slot <= 2) action = TopScreenDpadAction::SwordToggle;
  else if (slot >= 3 && slot <= 5) action = TopScreenDpadAction::ShieldToggle;
  else if (slot >= 6 && slot <= 8) action = TopScreenDpadAction::TunicToggle;
  else if (slot == 18) action = TopScreenDpadAction::Ocarina;
  else return -1;
  constexpr std::uint32_t save = 0x00587958U;
  const bool child = ReadWord(save + 4U) != 0U;
  if (child && (action == TopScreenDpadAction::SwordToggle ||
                action == TopScreenDpadAction::TunicToggle))
    return -1;
  std::uint16_t owned = 0U;
  if (!mMemory.Read16(save + 0xB6U, &owned))
    throw std::runtime_error("SingleScreen Gear hold cannot read the equipment");
  // 0x005D07AC: a cycle needs two members (both adult shields, or Deku and
  // Hylian as a child).
  bool available = true;
  if (action == TopScreenDpadAction::SwordToggle) {
    available = (owned & 6U) == 6U;
  } else if (action == TopScreenDpadAction::TunicToggle) {
    available = std::popcount(static_cast<unsigned>((owned >> 8) & 7U)) >= 2;
  } else if (action == TopScreenDpadAction::ShieldToggle) {
    const unsigned mask = child ? 3U : 6U;
    available = (mask & ~(static_cast<unsigned>(owned) >> 4)) == 0U;
  }
  if (!available) return -1;
  // 0x002E9D78: the slot itself must be owned, through the native group
  // masks (0x0053CB24) and shifts (0x0053CB08).
  bool slotOwned = false;
  if (action == TopScreenDpadAction::Ocarina) {
    for (const std::uint8_t item : {7U, 8U}) {
      std::uint8_t inventorySlot = 0xFF, value = 0xFF;
      if (!mMemory.Read8(0x0053CC44U + item, &inventorySlot) ||
          !mMemory.Read8(save + 0x8CU + inventorySlot, &value))
        throw std::runtime_error("SingleScreen Gear hold cannot read the Ocarina");
      slotOwned = slotOwned || value == item;
    }
  } else {
    std::uint8_t shift = 0U;
    const auto group = static_cast<std::uint32_t>(slot) / 3U;
    if (!mMemory.Read8(0x0053CB08U + group, &shift))
      throw std::runtime_error("SingleScreen Gear hold cannot read the equipment shifts");
    const auto mask = ReadWord(0x0053CB24U + (static_cast<std::uint32_t>(slot) % 3U) * 4U);
    slotOwned = (owned & (mask << shift)) != 0U;
  }
  return slotOwned ? static_cast<std::int32_t>(action) : -1;
}

void TopScreenWideItemsRuntime::UpdateGearHold(Sequence &sequence) {
  // 0x005E46D8: only on Gear page state 2 without the picker or the View
  // Model (0x005D1F40; its closed flag was just cleared).
  if (mDpadPicker.Active() || mViewModel.Active || ReadWord(kGearPageState) != 2U) {
    mGearHoldSlot = -1;
    return;
  }
  const auto lane = ReadWord(0x0050AF18U);
  const auto pressed = lane != 0U ? lane : ReadWord(0x0050AF14U);
  const auto slot = static_cast<std::int32_t>(ReadWord(kGearCursorSlot));
  const auto clearA = [this] {
    for (const auto address : {0x0050AF14U, 0x0050AF18U})
      WriteWord(address, ReadWord(address) & ~1U);
  };
  if (mGearHoldSlot < 0) {
    // 0x005E475C: A on a cyclable slot is held back from the native page.
    if ((pressed & 1U) == 0U || GearSlotAction(slot) < 0) return;
    clearA();
    mGearHoldSlot = slot;
    mGearHoldFrames = 0;
    return;
  }
  if (slot != mGearHoldSlot) {
    mGearHoldSlot = -1;
    return;
  }
  if ((ReadWord(0x0050AF10U) & 1U) == 0U) {
    // 0x005E47D0: released early, the native page gets its A after all.
    WriteWord(0x0050AF18U, ReadWord(0x0050AF18U) | 1U);
    mGearHoldSlot = -1;
    return;
  }
  clearA();
  if (++mGearHoldFrames <= 14) return;
  const auto action = GearSlotAction(mGearHoldSlot);
  mGearHoldSlot = -1;
  if (action < 0) return;
  // 0x005D2014(action | 0x1000, 3); 0x005D29E4 advances it in this update.
  ReadShortcutDrawState();
  OpenTopScreenDpadPicker(mDpadPicker, static_cast<TopScreenDpadAction>(action),
                          mShortcutDraw.Child, mShortcutDraw.Child ? mChildDpad : mAdultDpad,
                          ReadWord(0x0050AF10U));
  mDpadPicker.Gear = true;
  AppendUiSound(sequence, 0x01000497U);
}

void TopScreenWideItemsRuntime::EnterViewModel(Sequence &sequence, std::uint32_t mode,
                                               std::int32_t item) {
  // 0x005D24FC(mode, item): mode 2 (Gear) passes no item; mode 1 (picker)
  // previews the picked item through 0x002E9B00 and restores it on B.
  const auto model = ReadWord(kViewModel);
  std::uint8_t ready = 0U;
  if (model == 0U || (model & 3U) != 0U) return;
  if (!mMemory.Read8(model + 0x11DU, &ready))
    throw std::runtime_error("SingleScreen View Model cannot read the item model");
  if (ready == 0U) return;
  const auto saved = static_cast<std::int32_t>(ReadWord(kViewEquipment));
  if (item >= 0) {
    std::uint8_t object = 0U;
    if (!mMemory.Read8(kItemPreviewObjects + static_cast<std::uint32_t>(item), &object))
      throw std::runtime_error("SingleScreen View Model cannot read the item preview table");
    if (object == 0x8AU) return;
    sequence.Calls.push_back({kPreviewItem, static_cast<std::uint32_t>(item)});
  }
  std::uint16_t angle = 0U;
  if (!mMemory.Read16(model + 0x118U, &angle))
    throw std::runtime_error("SingleScreen View Model cannot read the model angle");
  mViewModel.Yaw = static_cast<std::uint16_t>(
      (static_cast<std::int32_t>(static_cast<std::int16_t>(angle)) << 16) / 360);
  mViewModel.Pitch = 0;
  mViewModel.Released = false;
  mViewModel.Zoom = 1.0F;
  mViewModel.OffsetX = mViewModel.OffsetY = 0.0F;
  mViewModel.Mode = mode;
  mViewModel.EntryButton = mode == 2U ? 0x400U : 0x800U;
  mViewModel.SavedEquipment = saved;
  mViewModel.Active = true;
  AppendUiSound(sequence, 0x01000497U);
}

void TopScreenWideItemsRuntime::StepViewModel(Sequence &sequence, std::uint32_t held,
                                              std::uint32_t pressed) {
  // 0x005D2274: an invalid or unloading (0x8C, 0x8C) model exits; B exits
  // and restores the previewed item.
  const auto model = ReadWord(kViewModel);
  std::uint8_t ready = 0U, first = 0U, second = 0U;
  if (model != 0U && (model & 3U) == 0U &&
      (!mMemory.Read8(model + 0x11DU, &ready) || !mMemory.Read8(model + 0x11AU, &first) ||
       !mMemory.Read8(model + 0x11BU, &second)))
    throw std::runtime_error("SingleScreen View Model cannot read the item model");
  if (model == 0U || (model & 3U) != 0U || ready == 0U || (first == 0x8CU && second == 0x8CU)) {
    ExitViewModel(sequence, false);
    return;
  }
  if ((pressed & 2U) != 0U) {
    ExitViewModel(sequence, true);
    return;
  }
  std::uint16_t stickX = 0U, stickY = 0U;
  if (!mMemory.Read16(kCirclePadX, &stickX) || !mMemory.Read16(kCirclePadX + 2U, &stickY))
    throw std::runtime_error("SingleScreen View Model cannot read the circle pad");
  const std::int32_t x = static_cast<std::int16_t>(stickX), y = static_cast<std::int16_t>(stickY);
  if (static_cast<std::uint32_t>(x + 12) > 24U)
    mViewModel.Yaw = static_cast<std::uint16_t>(mViewModel.Yaw + x * 0x300 / 105);
  if (static_cast<std::uint32_t>(y + 12) > 24U)
    mViewModel.Pitch = static_cast<std::int16_t>(
        std::clamp(mViewModel.Pitch + y * 512 / -105, -0x3800, 0x3800));
  // X zooms in, Y out; the button that opened the View Model only after
  // its first release.
  const auto entry = mViewModel.EntryButton;
  if ((held & entry) == 0U) mViewModel.Released = true;
  if ((held & 0x400U) != 0U && (entry != 0x400U || mViewModel.Released))
    mViewModel.Zoom = std::min(mViewModel.Zoom * 1.04F, 2.5F);
  if ((held & 0x800U) != 0U && (entry != 0x800U || mViewModel.Released))
    mViewModel.Zoom = std::max(mViewModel.Zoom / 1.04F, 0.5F);
  if ((held & 0x10U) != 0U) mViewModel.OffsetX += 0.4F;
  if ((held & 0x20U) != 0U) mViewModel.OffsetX -= 0.4F;
  if ((held & 0x40U) != 0U) mViewModel.OffsetY += 0.4F;
  if ((held & 0x80U) != 0U) mViewModel.OffsetY -= 0.4F;
  mViewModel.OffsetX = std::clamp(mViewModel.OffsetX, -12.0F, 12.0F);
  mViewModel.OffsetY = std::clamp(mViewModel.OffsetY, -9.0F, 9.0F);
  if (!mMemory.Write16(model + 0x118U, static_cast<std::uint16_t>(
                                           (static_cast<std::uint32_t>(mViewModel.Yaw) * 360U) >> 16)))
    throw std::runtime_error("SingleScreen View Model cannot write the model angle");
}

void TopScreenWideItemsRuntime::ExitViewModel(Sequence &sequence, bool restore) {
  // 0x005D21E8(restore): put back the item previewed at entry, if it changed.
  mViewModel.Active = false;
  mViewModel.Closed = true;
  if (restore) {
    const auto saved = mViewModel.SavedEquipment;
    const auto model = ReadWord(kViewModel);
    if (static_cast<std::int32_t>(ReadWord(kViewEquipment)) != saved && model != 0U) {
      if (saved < 0) {
        sequence.Calls.push_back({kPreviewItem, 0xFFFFFFFFU});
      } else {
        WriteWord(kViewEquipment, static_cast<std::uint32_t>(saved));
        sequence.Calls.push_back({kPreviewObject, model, static_cast<std::uint32_t>(saved) & 0xFFU});
      }
    }
  }
  AppendUiSound(sequence, 0x01000496U);
}

void TopScreenWideItemsRuntime::WriteViewModelTransform(std::uint32_t model, std::uint32_t matrix) {
  // 0x005D2734, word for word: scale model+0x14 x zoom, translation model+8
  // and +0xC plus the offset, model+0x10 as is. A matrix with m[9] == 0 keeps
  // the scaled identity; otherwise pitch + 10 degrees (0x71C) and yaw.
  const float scale = ReadScalar(model + 0x14U) * mViewModel.Zoom;
  const float x = ReadScalar(model + 8U) + mViewModel.OffsetX;
  const float y = ReadScalar(model + 0xCU) + mViewModel.OffsetY;
  const auto z = ReadWord(model + 0x10U);
  std::array<float, 11> m{};
  if (ReadScalar(matrix + 0x24U) != 0.0F) {
    const std::int32_t pitch = static_cast<std::int16_t>(mViewModel.Pitch + 0x71C);
    const std::int32_t yaw = static_cast<std::int16_t>(mViewModel.Yaw);
    const float sinA = NativeTrigonometry(mMemory, pitch, false);
    const float cosA = NativeTrigonometry(mMemory, pitch, true);
    const float sinB = NativeTrigonometry(mMemory, yaw, false);
    const float cosB = NativeTrigonometry(mMemory, yaw, true);
    const float sinASinB = sinA * sinB, sinACosB = -(sinA * cosB);
    const float cosASinB = -(cosA * sinB), cosACosB = cosA * cosB;
    m = {cosB * scale, scale * 0.0F, sinB * scale, x,
         sinASinB * scale, cosA * scale, sinACosB * scale, y,
         cosASinB * scale, sinA * scale, cosACosB * scale};
  } else {
    m = {1.0F * scale, scale * 0.0F, 0.0F * scale, x,
         0.0F * scale, 1.0F * scale, 0.0F * scale, y,
         0.0F * scale, 0.0F * scale, 1.0F * scale};
  }
  for (std::uint32_t lane = 0; lane < m.size(); ++lane)
    WriteWord(matrix + lane * 4U, Word(m[lane]));
  WriteWord(matrix + 0x2CU, z);
}

bool TopScreenWideItemsRuntime::ExecuteViewModelTransform(
    oot3d::recomp::a32::GuestState &state, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (!mViewModel.Active) return false;
  // The block's own `ldr r0, [r6, #0xFC]; mov r1, r8`, then 3.0's
  // replacement of its `bl ModelHandle_CopyOverrideTransform`.
  state.r[0] = ReadWord(state.r[6] + 0xFCU);
  state.r[1] = state.r[8];
  const auto model = ReadWord(kViewModel);
  if (model != 0U && state.r[8] != 0U) WriteViewModelTransform(model, state.r[8]);
  state.r[14] = kTopScreenViewModelTransformReturn;
  state.r[15] = kModelHandleCopyOverrideTransform;
  *result = {oot3d::recomp::a32::ExitKind::Branch, kModelHandleCopyOverrideTransform,
             oot3d::recomp::a32::FallbackReason::None, kTopScreenViewModelTransformBlock};
  if (blocksConsumed != nullptr) *blocksConsumed = 1U;
  return true;
}

bool TopScreenWideItemsRuntime::ExecuteShortcutCursor(
    oot3d::recomp::a32::GuestState &state, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (mShortcutCursor.Pending()) return Step(mShortcutCursor, state, result, blocksConsumed);
  if (mShortcuts.SelectedRow < 0 || ReadWord(kItemsPageState) != 2U) return false;
  Begin(mShortcutCursor, kTopScreenItemsShortcutCursorBoundary, state, 0x20U);
  mShortcutCursor.Resume = kTopScreenItemsShortcutCursorResume;
  // 0x00434B98/9C are NOPs; first run the displaced native call. The
  // correction follows it, never a draw-time position edit after upload.
  mShortcutCursor.Calls.push_back({0x00446E98U});
  const auto cursor = ReadWord(0x005067A8U);
  if (cursor != 0U) {
    Call correct{0x002F7AF4U, cursor};
    correct.SetS01 = true;
    correct.S0 = -39.0F;
    correct.S1 = static_cast<float>(mShortcuts.SelectedRow * 48 + 5);
    mShortcutCursor.Calls.push_back(correct);
  }
  return Step(mShortcutCursor, state, result, blocksConsumed);
}

void TopScreenWideItemsRuntime::TraceItemsState() {
  const char *trace = std::getenv("TRIAEVUM_TOPSCREEN_ITEMS_TRACE");
  if (trace == nullptr || trace[0] != '1' || mWideItemsDraws >= 160U || mWideItemsDraws % 20U != 0U)
    return;
  const auto buffer = ReadWord(kItemsRenderBuffer);
  std::uint32_t positions = 0U, selected = 0U;
  if (buffer != 0U) (void)mMemory.Read32(buffer + 0xCU, &positions);
  (void)ResolveStream(mMemory, 0x005066F8U, 0x20U, &selected);
  float x0 = 0.0F, x2 = 0.0F, width2 = 0.0F, selectedWidth = 0.0F;
  if (positions != 0U) {
    x0 = ReadScalar(positions);
    x2 = ReadScalar(positions + 0x60U);
    width2 = ReadScalar(positions + 0x6CU) - x2;
  }
  if (selected != 0U)
    selectedWidth = ReadScalar(selected + 0x6CU) - ReadScalar(selected + 0x60U);
  std::fprintf(stderr,
               "SS3_ITEMS draw=%llu buffer=%08X positions=%08X selected=%08X "
               "grid_x=%.2f inset_x=%.2f base_inset_w=%.2f selected_inset_w=%.2f updates=%llu "
               "cursor_col=%u cursor_row=%u selection=%u inset_target=-44,0,100,200\n",
               static_cast<unsigned long long>(mWideItemsDraws + 1U), buffer, positions, selected,
               x0, x2, width2, selectedWidth, static_cast<unsigned long long>(mItemsInsetUpdates),
               ReadWord(0x00506770U), ReadWord(0x00506774U), ReadWord(0x0050674CU));
}

bool TopScreenWideItemsRuntime::PrepareCall(Sequence &sequence, Call &call) {
  if (call.Model == 0U) return true;
  // 0x005CD8C8: narrow the index count and selected u16 index pointer, never
  // the position stream. Non-indexed models draw only for a range from zero.
  if (call.Range) {
    const auto instance = ReadWord(call.Model + 4U);
    if (instance == 0U || call.QuadCount == 0U) return false;
    const auto stream = ReadWord(instance + 0x124U);
    const auto kind = ReadWord(instance + 0x128U);
    const auto count = ReadWord(call.Model + 0x174U);
    if (stream <= 1U && kind == 2U) {
      if ((static_cast<std::uint64_t>(call.FirstQuad) + call.QuadCount) * 6U > count)
        return false;
      const auto indices = instance + (stream + 0x6AU) * 4U;
      const auto pointer = ReadWord(indices);
      SaveWrite(sequence, call.Model + 0x174U, call.QuadCount * 6U);
      SaveWrite(sequence, indices, pointer + call.FirstQuad * 12U);
    } else if (call.FirstQuad != 0U) {
      return false;
    }
    // 0x005CB0C0: only the original model's native virtual draw runs.
    call.Entry = ReadWord(ReadWord(call.Model) + 0xCU);
    call.R0 = call.Model;
    if (call.Entry == 0U || (call.Entry & 3U) != 0U || call.Entry >= 0x005C7000U)
      throw std::runtime_error("SingleScreen Items model has no native draw entry");
  }
  TopScreenItemsTransform matrix;
  for (std::size_t lane = 0; lane < matrix.size(); ++lane)
    matrix[lane] = ReadScalar(call.Model + kTransformOffsets[lane]);
  const auto transformed = TransformTopScreenItemsModel(matrix, call.OffsetX, call.OffsetY);
  for (std::size_t lane = 0; lane < matrix.size(); ++lane)
    SaveWrite(sequence, call.Model + kTransformOffsets[lane], Word(transformed[lane]));
  return true;
}

bool TopScreenWideItemsRuntime::Step(Sequence &sequence,
                                     oot3d::recomp::a32::GuestState &state,
                                     oot3d::recomp::a32::ExecutionResult *result,
                                     std::uint32_t *blocksConsumed) {
  try {
    if (!Restore(sequence))
      throw std::runtime_error("SingleScreen Items cannot restore native draw state");
    while (sequence.Next < sequence.Calls.size()) {
      auto call = sequence.Calls[sequence.Next++];
      if (!PrepareCall(sequence, call)) {
        if (!Restore(sequence))
          throw std::runtime_error("SingleScreen cannot restore skipped page draw state");
        continue;
      }
      state = sequence.Caller;
      state.r[0] = call.R0;
      state.r[1] = call.R1;
      state.r[2] = call.R2;
      state.r[3] = call.R3;
      state.r[13] -= sequence.Scratch;
      if (call.SetS01) {
        state.vfp[0] = Word(call.S0);
        state.vfp[1] = Word(call.S1);
      }
      if (call.SetStack) {
        WriteWord(state.r[13], call.Stack[0]);
        WriteWord(state.r[13] + 4U, call.Stack[1]);
      }
      state.r[14] = sequence.Site;
      state.r[15] = call.Entry;
      *result = {oot3d::recomp::a32::ExitKind::Branch, call.Entry,
                 oot3d::recomp::a32::FallbackReason::None, sequence.Site};
      if (blocksConsumed != nullptr) *blocksConsumed = 1U;
      return true;
    }
  } catch (...) {
    (void)Restore(sequence);
    state = sequence.Caller;
    sequence = {};
    throw;
  }
  state = sequence.Caller;
  const auto resume = sequence.Resume != 0U ? sequence.Resume : sequence.Site + 4U;
  *result = {oot3d::recomp::a32::ExitKind::Branch, resume,
             oot3d::recomp::a32::FallbackReason::None, sequence.Site};
  state.r[15] = resume;
  sequence = {};
  if (blocksConsumed != nullptr) *blocksConsumed = 1U;
  return true;
}

std::uint32_t TopScreenWideItemsRuntime::WriteProjection(std::uint32_t address,
                                                         std::uint32_t offset) {
  const auto words = offset == 0U ? kIdentity : Projection(offset);
  for (std::size_t word = 0; word < words.size(); ++word)
    if (!mMemory.Write32(address + static_cast<std::uint32_t>(word) * 4U, words[word]))
      throw std::runtime_error("SingleScreen Items matrices cannot be written");
  return address;
}

void TopScreenWideItemsRuntime::AppendProjection(Sequence &sequence,
                                                  std::uint32_t projection,
                                                  std::uint32_t identity) {
  // 0x005CDDA8 -> 0x005CD760. Keep the writer bind/upload/accumulate contiguous.
  sequence.Calls.push_back({kBindCommandList, kMaterialState});
  sequence.Calls.push_back({kUploadUniformBlock0, kMaterialState, projection, identity});
  sequence.Calls.push_back({kUploadUniformBlock4, kMaterialState, identity});
  sequence.Calls.push_back({kAccumulateWriter, kMaterialState});
}

void TopScreenWideItemsRuntime::AppendRange(Sequence &sequence, std::uint32_t model,
                                            std::uint32_t first, std::uint32_t count,
                                            float x, float y) {
  if (model != 0U && count != 0U)
    sequence.Calls.push_back({0U, model, 0U, 0U, 0U, model, first, count, x, y, true});
}

void TopScreenWideItemsRuntime::AppendAssignments(Sequence &sequence, std::uint32_t group) {
  if (group == 0U) return;
  const auto model = ReadWord(group + 8U);
  const auto kind = static_cast<std::int32_t>(ReadWord(group + 0x428U));
  std::uint32_t stream = 0U;
  if (!ResolveStream(mMemory, group, 8U, &stream))
    throw std::runtime_error("SingleScreen Items cannot read assignment positions");
  if (model == 0U || stream == 0U || kind == 7) {
    sequence.Calls.push_back({kAssignmentGroupDraw, group});
    return;
  }
  // 0x005CE1D0: partition contiguous quads at X 256. The upper pass has
  // equal X/Y scales, so 0x005CD9D0 uses the simple +4 transform (mode 1).
  const auto count = ReadWord(model + 0x174U) / 6U;
  if (count > 0x10000U)
    throw std::runtime_error("SingleScreen Items assignment index count is invalid");
  std::uint32_t first = 0U;
  while (first < count) {
    const bool left = ReadScalar(stream + first * kQuadBytes) < 256.0F;
    auto end = first + 1U;
    while (end < count && (ReadScalar(stream + end * kQuadBytes) < 256.0F) == left) ++end;
    AppendRange(sequence, model, first, end - first, left ? 4.0F : 0.0F);
    first = end;
  }
  if (kind > 1) return;
  const auto slots = static_cast<std::int32_t>(ReadWord(group + 0x224U));
  if (slots > static_cast<std::int32_t>((0x224U - 0xCU) / 4U))
    throw std::runtime_error("SingleScreen Items assignment slot count is invalid");
  for (std::int32_t index = 0; index < slots; ++index) {
    const auto slot = ReadWord(group + 0xCU + static_cast<std::uint32_t>(index) * 4U);
    std::uint8_t visible = 0U;
    if (!mMemory.Read8(group + 0x434U + static_cast<std::uint32_t>(index), &visible))
      throw std::runtime_error("SingleScreen Items cannot read assignment visibility");
    if (slot == 0U || visible == 0U) continue;
    Call call{kAssignmentDraw, slot};
    const auto buffer = ReadWord(slot + 8U);
    const auto slotModel = ReadWord(slot + 0x10U);
    if (buffer != 0U && (slot & 3U) == 0U && slotModel != 0U) {
      const auto base = ReadWord(buffer + 0xCU);
      if (base != 0U) {
        const bool left = index < 32 && static_cast<std::uint32_t>(index) < count
                              ? ReadScalar(stream + index * kQuadBytes) < 256.0F
                              : ReadScalar(base) < 256.0F;
        if (left) {
          call.Model = slotModel;
          call.OffsetX = 4.0F;
        }
      }
    }
    sequence.Calls.push_back(call);
  }
}

bool TopScreenWideItemsRuntime::ResolveCursorProjection(std::uint32_t object, bool selection) {
  if (object == 0U) return false;
  std::uint32_t stream = 0U;
  if (!selection) {
    // 0x005E3264: first position of the icon group.
    if (!ResolveStream(mMemory, object, 4U, &stream))
      throw std::runtime_error("SingleScreen Items cannot read icon positions");
    return stream != 0U && ReadScalar(stream) < 256.0F;
  }
  // 0x005D6020: owner X, else the first quad below the hidden X=390 sentinel.
  float x = ReadScalar(object + 0x18U);
  if (!(x >= 0.0F && x < 390.0F)) {
    if (!ResolveStream(mMemory, object, 4U, &stream))
      throw std::runtime_error("SingleScreen Items cannot read selection positions");
    if (stream == 0U) return false;
    bool found = false;
    for (std::uint32_t quad = 0; quad < 4U && !found; ++quad) {
      x = ReadScalar(stream + quad * kQuadBytes);
      found = x < 390.0F;
    }
    if (!found || x < 0.0F) return false;
  }
  return x < 256.0F;
}

void TopScreenWideItemsRuntime::AppendCursor(Sequence &sequence, std::uint32_t object,
                                             bool selection, std::uint32_t cursor,
                                             std::uint32_t items, std::uint32_t identity) {
  if (object == 0U) return;
  const bool grid = ResolveCursorProjection(object, selection);
  if (grid) AppendProjection(sequence, cursor, identity);
  sequence.Calls.push_back({kQuadGroupDraw, object});
  if (grid) {
    AppendProjection(sequence, items, identity);
    ++mCursorDraws;
  }
}

bool TopScreenWideItemsRuntime::AssignmentGroupVisible() {
  // 0x005CAF74 caches the native assignment countdown once per update key.
  // Its cache is host state; no mod-private guest globals are installed.
  const auto owner = ReadWord(0x005043E0U);
  const auto frame = owner != 0U ? ReadWord(owner + 0xF8U) : 0xFFFFFFFFU;
  if (owner == 0U || frame != mAssignmentFrame) {
    mAssignmentFrame = frame;
    mAssignmentDelay = static_cast<std::int32_t>(ReadWord(0x0050677CU));
    if (mAssignmentDelay > 0 &&
        !mMemory.Write32(0x0050677CU, static_cast<std::uint32_t>(mAssignmentDelay - 1)))
      throw std::runtime_error("SingleScreen Items cannot update assignment countdown");
  }
  return mAssignmentDelay == 0;
}

void TopScreenWideItemsRuntime::BuildItemsSequence(Sequence &sequence, std::uint32_t scratch) {
  const auto items = WriteProjection(scratch + 0x20U, kItemsOffset);
  const auto page = WriteProjection(scratch + 0x60U, kPageOffset);
  const auto cursor = WriteProjection(scratch + 0xA0U, kCursorOffset);
  const auto identity = WriteProjection(scratch + 0xE0U, 0U);
  AppendProjection(sequence, items, identity);
  const auto model = ReadWord(kItemsModel);
  AppendRange(sequence, model, 0U, 2U, 4.0F);
  // 0x005CB13C: quad 2 is the shortcut-column inset, only while it has width.
  const auto instance = ReadWord(0x00506714U);
  if (instance != 0U) {
    const auto index = ReadWord(instance + 0x124U);
    if (index < 4U) {
      const auto positions = ReadWord(instance + (index + 0x68U) * 4U);
      if (positions != 0U && ReadScalar(positions + 0x6CU) - ReadScalar(positions + 0x60U) > 1.0F)
        AppendRange(sequence, model, 2U, 1U,
                    4.0F + ReadScalar(positions) - 48.0F - ReadScalar(positions + 0x60U),
                    ReadScalar(positions + 4U) - ReadScalar(positions + 0x64U));
    }
  }
  // 0x005E395C..0x005E3A00: omit pillars 3..5 and 25..27.
  AppendRange(sequence, model, 6U, 6U, -40.0F);
  AppendRange(sequence, model, 12U, 13U, 0.0F);
  AppendRange(sequence, model, 28U, 9U, -40.0F);
  if (ReadWord(0x00506758U) == 0U) {
    if (AssignmentGroupVisible()) AppendAssignments(sequence, ReadWord(0x00506734U));
    AppendCursor(sequence, ReadWord(0x005067A8U), false, cursor, items, identity);
    AppendCursor(sequence, ReadWord(0x005067ACU), false, cursor, items, identity);
    sequence.Calls.push_back({kQuadGroupDraw, ReadWord(0x00506728U)});
    sequence.Calls.push_back({0x00441FECU, ReadWord(0x00506744U)});
    sequence.Calls.push_back({kAssignmentGroupDraw, ReadWord(0x00506738U)});
    AppendAssignments(sequence, ReadWord(0x0050673CU));
    AppendAssignments(sequence, ReadWord(0x00506740U));
    if (ReadWord(0x0055B62CU) == 1U || ReadWord(0x0055B630U) == 1U || ReadWord(0x0055B634U) == 1U)
      AppendCursor(sequence, ReadWord(0x00506724U), true, cursor, items, identity);
    if (static_cast<std::int32_t>(ReadWord(0x00506730U)) > 1)
      sequence.Calls.push_back({kQuadGroupDraw, ReadWord(0x00506720U)});
  }
  AppendProjection(sequence, page, identity);
}

bool TopScreenWideItemsRuntime::ExecutePageProjection(oot3d::recomp::a32::GuestState &state,
                                                      bool pageOnTop) {
  mPageWide = false;
  mItemsWide = false;
  mPageOnTop = false;
  if (!pageOnTop) return false;
  TopScreenWidePageState page;
  if (!ReadTopScreenWidePageState(mMemory, &page))
    throw std::runtime_error("TopScreen cannot read the wide page state");
  if (!page.Wide()) {
    mPageOnTop = true;
    return false;
  }
  // 3.0 0x005F3B10: the projection in r1, before the native upload. The page
  // viewport is already full (target plan or System compositor transaction).
  // No other call may run here: PicaCommandWriter 0x0032471C (0x0041EE08) has
  // bound the material writer at the command cursor, and a GL call in
  // between would write over the same commands.
  if (!mMemory.Write32(state.r[1] + 0x10U, kWideScale) ||
      !mMemory.Write32(state.r[1] + 0x1CU, kPageOffset))
    throw std::runtime_error("TopScreen cannot write the wide page projection");
  mPageOnTop = true;
  mPageWide = true;
  mItemsWide = page.ItemsActive;
  ++mWidePages;
  return false;
}

bool TopScreenWideItemsRuntime::ExecuteItemsDraw(oot3d::recomp::a32::GuestState &state,
                                                 oot3d::recomp::a32::ExecutionResult *result,
                                                 std::uint32_t *blocksConsumed) {
  if (mItems.Pending()) return Step(mItems, state, result, blocksConsumed);
  if (!mItemsWide) return false;
  mPageWide = false;
  mItemsWide = false;
  if (ReadWord(kItemsPageState) == 0U) return false;
  Begin(mItems, kTopScreenItemsPageDrawCall, state, kScratch);
  try {
    TraceItemsState();
    BuildItemsSequence(mItems, state.r[13] - kScratch);
    // 0x005E2DF4 sees the native countdown after 0x005CAF74 decrements it.
    ReadShortcutDrawState();
  } catch (...) {
    mItems = {};
    throw;
  }
  ++mWideItemsDraws;
  return Step(mItems, state, result, blocksConsumed);
}

bool TopScreenWideItemsRuntime::ExecuteGearModelDraw(
    oot3d::recomp::a32::GuestState &state, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (mGear.Pending()) return Step(mGear, state, result, blocksConsumed);
  if (!mPageOnTop || ReadWord(0x00504484U) == 0U) return false;
  const auto model = ReadWord(0x00504470U);
  // r0 of the block's BLX at 0x00424C40: ldr r0, [r4, #4].
  if (model == 0U || ReadWord(state.r[4] + 4U) != model) return false;
  // 0x005D5AE4 replaces the main draw with 0..24, 28..29, 32..40.
  // Only the model's BLX is replaced, so the native tail stays intact.
  Begin(mGear, kTopScreenGearModelDrawCall, state, kScratch);
  mGear.Resume = kTopScreenGearModelDrawResume;
  try {
    AppendRange(mGear, model, 0U, 25U, 0.0F);
    AppendRange(mGear, model, 28U, 2U, 0.0F);
    AppendRange(mGear, model, 32U, 9U, 0.0F);
  } catch (...) {
    mGear = {};
    throw;
  }
  const char *trace = std::getenv("TRIAEVUM_TOPSCREEN_ITEMS_TRACE");
  if (trace != nullptr && trace[0] == '1' && mGearModelDraws < 160U && mGearModelDraws % 20U == 0U)
    std::fprintf(stderr, "SS3_GEAR draw=%llu model=%08X ranges=0+25,28+2,32+9\n",
                 static_cast<unsigned long long>(mGearModelDraws + 1U), model);
  ++mGearModelDraws;
  return Step(mGear, state, result, blocksConsumed);
}

bool TopScreenWideItemsRuntime::ExecuteCursorDraw(
    std::uint32_t, oot3d::recomp::a32::GuestState &,
    oot3d::recomp::a32::ExecutionResult *, std::uint32_t *) {
  // The 3.0 sequence owns its cursor calls at 0x0041EE74. The old native
  // page's two observed sites remain harmless for lower/default draws.
  return false;
}

} // namespace Oot3dNativeGame
