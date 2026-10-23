#include "oot3d_top_screen_wide_items_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
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
constexpr std::uint32_t kItemsModel = 0x00506714U;
constexpr std::uint32_t kItemsIconGroup = 0x00506734U;
constexpr std::uint32_t kItemsIconGroupB = 0x0050673CU;
constexpr std::uint32_t kItemsIconGroupC = 0x00506740U;
constexpr std::uint32_t kMaterialState = 0x005C58D8U;
constexpr std::uint32_t kBindCommandList = 0x0032471CU;
constexpr std::uint32_t kUploadUniformBlock0 = 0x003246ECU;
constexpr std::uint32_t kUploadUniformBlock4 = 0x003246BCU;
constexpr std::uint32_t kAccumulateWriter = 0x002F9C74U;
constexpr std::uint32_t kItemsPageDraw = 0x00434C80U;
constexpr std::uint32_t kQuadGroupDraw = 0x002FB934U;
// Payload literals: -0.005, and the offsets 0.8 (0x005E219C), 0.6, 0.685.
constexpr std::uint32_t kWideScale = 0xBBA3D70AU;
constexpr std::uint32_t kPageOffset = 0x3F4CCCCDU;
constexpr std::uint32_t kItemsOffset = 0x3F19999AU;
constexpr std::uint32_t kCursorOffset = 0x3F2F5C29U;
// 0x20 outgoing, then three 64-byte matrices.
constexpr std::uint32_t kScratch = 0xE0U;
constexpr std::uint32_t kQuadBytes = 48U;
constexpr std::uint32_t kVertexBytes = 12U;
// 0x005E1668 and 0x005E1660.
constexpr std::array<std::uint32_t, 21> kBackgroundQuads{
    3, 4, 5, 6, 7, 8, 9, 10, 11, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36};
constexpr std::array<std::uint32_t, 2> kGridQuads{0, 1};

std::uint32_t Word(float value) { return std::bit_cast<std::uint32_t>(value); }

bool ReadFloat(NativeA32Memory &memory, std::uint32_t address, float *value) {
  std::uint32_t bits = 0U;
  if (!memory.Read32(address, &bits)) return false;
  *value = std::bit_cast<float>(bits);
  return true;
}

// The four vertices' X of one 48-byte position quad.
bool AddQuadX(NativeA32Memory &memory, std::uint32_t quad, float delta) {
  for (std::uint32_t vertex = 0; vertex < 4U; ++vertex) {
    float x = 0.0F;
    if (!ReadFloat(memory, quad + vertex * kVertexBytes, &x) ||
        !memory.Write32(quad + vertex * kVertexBytes, Word(x + delta)))
      return false;
  }
  return true;
}

// 0x005CA124: an owner's selected model stream, or 0.
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

// 0x005CA184: the first `count` quads of a group's stream left of X 256.
bool ShiftGroup(NativeA32Memory &memory, std::uint32_t groupAddress, std::uint32_t count,
                float delta) {
  std::uint32_t group = 0U, stream = 0U;
  if (!memory.Read32(groupAddress, &group) || !ResolveStream(memory, group, 8U, &stream))
    return false;
  if (stream == 0U) return true;
  for (std::uint32_t quad = 0; quad < count; ++quad) {
    float x = 0.0F;
    const std::uint32_t address = stream + quad * kQuadBytes;
    if (!ReadFloat(memory, address, &x)) return false;
    if (x < 256.0F && !AddQuadX(memory, address, delta)) return false;
  }
  return true;
}

std::array<std::uint32_t, 16> Projection(std::uint32_t offset) {
  // 0x005CACA0, word for word, negative zeros included.
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

bool ApplyTopScreenWideItemsGeometry(NativeA32Memory &memory) {
  // 0x005D1A10..0x005D1A50: the Items model's selected stream.
  std::uint32_t model = 0U, stream = 0U;
  if (!memory.Read32(kItemsModel, &model)) return false;
  if (model != 0U) {
    std::uint32_t index = 0U;
    if (!memory.Read32(model + 0x124U, &index) ||
        !memory.Read32(model + (index + 0x68U) * 4U, &stream))
      return false;
  }
  if (stream != 0U) {
    for (const auto quad : kBackgroundQuads)
      if (!AddQuadX(memory, stream + quad * kQuadBytes, -40.0F)) return false;
    for (const auto quad : kGridQuads)
      if (!AddQuadX(memory, stream + quad * kQuadBytes, -17.0F)) return false;
  }
  // 0x005D1A54..0x005D1A80: the icon groups.
  if (!ShiftGroup(memory, kItemsIconGroup, 24U, -17.0F) ||
      !ShiftGroup(memory, kItemsIconGroupB, 1U, -17.0F) ||
      !ShiftGroup(memory, kItemsIconGroupC, 1U, -17.0F))
    return false;
  // 0x005D1A84..0x005D1B84: the visible assignment icons' origins left of
  // X 200 are set to -17.
  std::uint32_t group = 0U, rawCount = 0U;
  if (!memory.Read32(kItemsIconGroup, &group)) return false;
  if (group == 0U || (group & 3U) != 0U) return true;
  if (!memory.Read32(group + 0x224U, &rawCount)) return false;
  const auto count = std::min(static_cast<std::int32_t>(rawCount), 24);
  for (std::int32_t slotIndex = 0; slotIndex < count; ++slotIndex) {
    std::uint32_t slot = 0U, buffer = 0U, base = 0U, origins = 0U, origin = 0U;
    std::uint8_t visible = 0U;
    if (!memory.Read32(group + 0x0CU + slotIndex * 4U, &slot) ||
        !memory.Read8(group + 0x434U + slotIndex, &visible))
      return false;
    if (slot == 0U || visible == 0U || (slot & 3U) != 0U) continue;
    if (!memory.Read32(slot + 8U, &buffer)) return false;
    if (buffer == 0U || (buffer & 3U) != 0U) continue;
    if (!memory.Read32(buffer + 0x0CU, &base) || !memory.Read32(buffer + 0x1CU, &origins))
      return false;
    if (base == 0U || origins == 0U) continue;
    float baseX = 0.0F;
    if (!ReadFloat(memory, base, &baseX) || !memory.Read32(buffer, &origin)) return false;
    if (!(baseX < 256.0F) || static_cast<std::int32_t>(origin) <= 0) continue;
    for (std::int32_t entry = 0; entry < static_cast<std::int32_t>(origin); ++entry) {
      float x = 0.0F;
      const std::uint32_t address = origins + entry * 8U;
      if (!ReadFloat(memory, address, &x)) return false;
      if (x < 200.0F && !memory.Write32(address, Word(-17.0F))) return false;
    }
  }
  return true;
}

void TopScreenWideItemsRuntime::Reset() noexcept {
  mPageWide = false;
  mItemsWide = false;
  mItemsDrawing = false;
  mItems = {};
  mCursor = {};
}

void TopScreenWideItemsRuntime::Begin(Sequence &sequence, std::uint32_t site,
                                      const oot3d::recomp::a32::GuestState &state,
                                      std::uint32_t scratch) {
  if (scratch != 0U &&
      (state.r[13] < scratch || !mMemory.IsWritable(state.r[13] - scratch, scratch)))
    throw std::runtime_error("TopScreen wide Items has no valid caller stack");
  sequence = {};
  sequence.Site = site;
  sequence.Scratch = scratch;
  sequence.Caller = state;
}

bool TopScreenWideItemsRuntime::Step(Sequence &sequence,
                                     oot3d::recomp::a32::GuestState &state,
                                     oot3d::recomp::a32::ExecutionResult *result,
                                     std::uint32_t *blocksConsumed, bool skipSite) {
  if (sequence.Next < sequence.Count) {
    const auto call = sequence.Calls[sequence.Next++];
    state = sequence.Caller;
    state.r[0] = call.R0;
    state.r[1] = call.R1;
    state.r[2] = call.R2;
    state.r[3] = call.R3;
    state.r[13] -= sequence.Scratch;
    state.r[14] = sequence.Site;
    state.r[15] = call.Entry;
    *result = {oot3d::recomp::a32::ExitKind::Branch, call.Entry,
               oot3d::recomp::a32::FallbackReason::None, sequence.Site};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  }
  // The last call returned: the caller continues with its own state.
  state = sequence.Caller;
  const std::uint32_t site = sequence.Site;
  sequence = {};
  if (!skipSite) return false;
  state.r[15] = site + 4U;
  *result = {oot3d::recomp::a32::ExitKind::Branch, site + 4U,
             oot3d::recomp::a32::FallbackReason::None, site};
  if (blocksConsumed != nullptr) *blocksConsumed = 1U;
  return true;
}

std::uint32_t TopScreenWideItemsRuntime::WriteProjection(std::uint32_t address,
                                                         std::uint32_t offset) {
  const auto words = offset == 0U ? kIdentity : Projection(offset);
  for (std::size_t word = 0; word < words.size(); ++word)
    if (!mMemory.Write32(address + static_cast<std::uint32_t>(word) * 4U, words[word]))
      throw std::runtime_error("TopScreen wide Items matrices cannot be written");
  return address;
}

std::uint32_t TopScreenWideItemsRuntime::AppendProjection(Sequence &sequence, std::size_t at,
                                                          std::uint32_t projection,
                                                          std::uint32_t identity) {
  // 0x005CAD48 -> 0x005CACA0, in order.
  sequence.Calls[at] = {kBindCommandList, kMaterialState};
  sequence.Calls[at + 1U] = {kUploadUniformBlock0, kMaterialState, projection, identity};
  sequence.Calls[at + 2U] = {kUploadUniformBlock4, kMaterialState, identity};
  sequence.Calls[at + 3U] = {kAccumulateWriter, kMaterialState};
  return static_cast<std::uint32_t>(at + 4U);
}

bool TopScreenWideItemsRuntime::ExecutePageProjection(oot3d::recomp::a32::GuestState &state,
                                                      bool pageOnTop) {
  mPageWide = false;
  mItemsWide = false;
  if (!pageOnTop) return false;
  TopScreenWidePageState page;
  if (!ReadTopScreenWidePageState(mMemory, &page))
    throw std::runtime_error("TopScreen cannot read the wide page state");
  if (!page.Wide()) return false;
  // 0x005DF428: the projection in r1, before the native upload. The page
  // viewport is already full (target plan or System compositor transaction).
  // No other call may run here: PicaCommandWriter 0x0032471C (0x0041EE08) has
  // bound the material writer at the command cursor, and a GL call in
  // between would write over the same commands.
  if (!mMemory.Write32(state.r[1] + 0x10U, kWideScale) ||
      !mMemory.Write32(state.r[1] + 0x1CU, kPageOffset))
    throw std::runtime_error("TopScreen cannot write the wide page projection");
  mPageWide = true;
  mItemsWide = page.ItemsActive;
  ++mWidePages;
  return false;
}

bool TopScreenWideItemsRuntime::ExecuteItemsDraw(oot3d::recomp::a32::GuestState &state,
                                                 oot3d::recomp::a32::ExecutionResult *result,
                                                 std::uint32_t *blocksConsumed) {
  if (mItems.Pending()) {
    // Call 4 is the page draw, whose cursors use the Items projection.
    mItemsDrawing = mItems.Next == 4U;
    return Step(mItems, state, result, blocksConsumed, true);
  }
  if (!mItemsWide) return false;
  mPageWide = false;
  mItemsWide = false;
  if (!ApplyTopScreenWideItemsGeometry(mMemory))
    throw std::runtime_error("TopScreen wide Items cannot edit the Items page geometry");
  Begin(mItems, kTopScreenItemsPageDrawCall, state, kScratch);
  const std::uint32_t scratch = state.r[13] - kScratch;
  const auto items = WriteProjection(scratch + 0x20U, kItemsOffset);
  const auto page = WriteProjection(scratch + 0x60U, kPageOffset);
  const auto identity = WriteProjection(scratch + 0xA0U, 0U);
  // 0x005D1B88..0x005D1BA4.
  auto next = AppendProjection(mItems, 0U, items, identity);
  mItems.Calls[next++] = {kItemsPageDraw, state.r[0], state.r[1], state.r[2], state.r[3]};
  next = AppendProjection(mItems, next, page, identity);
  mItems.Count = next;
  ++mWideItemsDraws;
  return Step(mItems, state, result, blocksConsumed, true);
}

bool TopScreenWideItemsRuntime::ResolveCursorProjection(std::uint32_t pc, std::uint32_t object,
                                                        bool *cursorProjection) {
  *cursorProjection = false;
  std::uint32_t stream = 0U;
  float x = 0.0F;
  if (pc == kTopScreenItemsCursorDrawCallA) {
    // 0x005D1CD8..0x005D1CF8: the first quad of the group's stream.
    if (!ResolveStream(mMemory, object, 4U, &stream)) return false;
    if (stream == 0U) return true;
    if (!ReadFloat(mMemory, stream, &x)) return false;
    *cursorProjection = x < 256.0F;
    return true;
  }
  // 0x005D1BF4..0x005D1C74: the group's X, else its first quad left of X 390.
  if (!ReadFloat(mMemory, object + 24U, &x)) return false;
  if (!(x < 390.0F && x >= 0.0F)) {
    if (!ResolveStream(mMemory, object, 4U, &stream)) return false;
    if (stream == 0U) return true;
    bool found = false;
    for (std::uint32_t quad = 0; quad < 4U && !found; ++quad) {
      if (!ReadFloat(mMemory, stream + quad * kQuadBytes, &x)) return false;
      found = x < 390.0F;
    }
    if (!found || x < 0.0F) return true;
  }
  *cursorProjection = x < 256.0F;
  return true;
}

bool TopScreenWideItemsRuntime::ExecuteCursorDraw(std::uint32_t pc,
                                                  oot3d::recomp::a32::GuestState &state,
                                                  oot3d::recomp::a32::ExecutionResult *result,
                                                  std::uint32_t *blocksConsumed) {
  if (mCursor.Pending()) {
    if (mCursor.Site != pc)
      throw std::runtime_error("TopScreen wide Items cursor draw re-entered another site");
    return Step(mCursor, state, result, blocksConsumed, true);
  }
  if (!mItemsDrawing) return false;
  const std::uint32_t object = state.r[0];
  // 0x005D1BEC: the second cursor draws nothing without a group.
  if (pc == kTopScreenItemsCursorDrawCallB && object == 0U) {
    state.r[15] = pc + 4U;
    *result = {oot3d::recomp::a32::ExitKind::Branch, pc + 4U,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  }
  bool cursorProjection = false;
  if (!ResolveCursorProjection(pc, object, &cursorProjection))
    throw std::runtime_error("TopScreen wide Items cannot read the cursor geometry");
  if (!cursorProjection) return false;
  // The payload also records the selected cursor's bounds (0x005E40A8..BC),
  // which only payload code reads.
  Begin(mCursor, pc, state, kScratch);
  const std::uint32_t scratch = state.r[13] - kScratch;
  const auto cursor = WriteProjection(scratch + 0x20U, kCursorOffset);
  const auto items = WriteProjection(scratch + 0x60U, kItemsOffset);
  const auto identity = WriteProjection(scratch + 0xA0U, 0U);
  auto next = AppendProjection(mCursor, 0U, cursor, identity);
  mCursor.Calls[next++] = {kQuadGroupDraw, object};
  next = AppendProjection(mCursor, next, items, identity);
  mCursor.Count = next;
  ++mCursorDraws;
  return Step(mCursor, state, result, blocksConsumed, true);
}

} // namespace Oot3dNativeGame
