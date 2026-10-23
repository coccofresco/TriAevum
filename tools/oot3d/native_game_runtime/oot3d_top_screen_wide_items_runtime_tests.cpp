#include "oot3d_top_screen_wide_items_runtime.h"
#include "oot3d_top_screen_mod_profile.h"

#include <bit>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

constexpr std::uint32_t kItemsState = 0x0050672CU;
constexpr std::uint32_t kMapState = 0x00506CE8U;
constexpr std::uint32_t kMapMode = 0x004FDABCU;
constexpr std::uint32_t kSystemState = 0x0050A530U;
constexpr std::uint32_t kItemsModel = 0x00506714U;
constexpr std::uint32_t kIconGroup = 0x00506734U;
constexpr std::uint32_t kStackTop = 0x005FF000U;
constexpr std::uint32_t kHeap = 0x00700000U;
constexpr std::uint32_t kMatrix = 0x005FE800U;
constexpr std::uint32_t kItemsDraw = 0x00434C80U;
constexpr std::uint32_t kQuadGroupDraw = 0x002FB934U;
constexpr std::uint32_t kUploadUniformBlock0 = 0x003246ECU;

float ReadFloat(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t address) {
  std::uint32_t bits = 0U;
  Require(memory.Read32(address, &bits), "cannot read a float");
  return std::bit_cast<float>(bits);
}
void WriteFloat(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t address, float value) {
  Require(memory.Write32(address, std::bit_cast<std::uint32_t>(value)), "cannot write a float");
}
void WriteQuadX(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t quad, float x) {
  for (std::uint32_t vertex = 0; vertex < 4U; ++vertex) WriteFloat(memory, quad + vertex * 12U, x);
}

oot3d::recomp::a32::GuestState CallerState(std::uint32_t pc) {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 0x0123U;
  state.r[1] = kMatrix;
  state.r[4] = 456;
  state.r[13] = kStackTop;
  state.r[14] = 0x00400000;
  state.r[15] = pc;
  state.vfp[0] = 789;
  state.cpsr = 0xA0000010;
  return state;
}

bool Same(const oot3d::recomp::a32::GuestState &a, const oot3d::recomp::a32::GuestState &b) {
  for (std::size_t index = 0; index < 16U; ++index)
    if (index != 15U && a.r[index] != b.r[index]) return false;
  return a.vfp == b.vfp && a.cpsr == b.cpsr && a.fpscr == b.fpscr &&
         a.thread_pointer == b.thread_pointer &&
         a.exclusive_address == b.exclusive_address &&
         a.exclusive_token == b.exclusive_token &&
         a.exclusive_size == b.exclusive_size && a.exclusive_valid == b.exclusive_valid;
}

// The runtime issues `entry` returning to `site`; the callee then returns.
template <typename Execute>
bool Calls(Execute execute, oot3d::recomp::a32::GuestState &state, std::uint32_t site,
           std::uint32_t entry, std::uint32_t r0, std::uint32_t sp) {
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0;
  if (!execute(state, &result, &blocks)) return false;
  const bool ok = state.r[15] == entry && result.pc == entry && state.r[0] == r0 &&
                  state.r[13] == sp && state.r[14] == site && blocks == 1;
  state.r[15] = site;
  state.r[0] = 0xDEAD;
  return ok;
}

// Items model stream, an icon group, and one assignment icon.
void BuildItemsPage(Oot3dNativeGame::NativeA32Memory &memory) {
  const std::uint32_t model = kHeap, stream = kHeap + 0x1000U;
  Require(memory.Write32(kItemsModel, model) && memory.Write32(model + 0x124U, 1U) &&
              memory.Write32(model + (1U + 0x68U) * 4U, stream),
          "items model");
  for (std::uint32_t quad = 0; quad < 40U; ++quad) WriteQuadX(memory, stream + quad * 48U, 100.0F);
  const std::uint32_t group = kHeap + 0x3000U, groupModel = kHeap + 0x4000U,
                      instance = kHeap + 0x4100U, groupStream = kHeap + 0x5000U;
  Require(memory.Write32(kIconGroup, group) && memory.Write32(group + 8U, groupModel) &&
              memory.Write32(groupModel + 4U, instance) && memory.Write32(instance + 0x124U, 0U) &&
              memory.Write32(instance + 0x68U * 4U, groupStream),
          "icon group");
  WriteQuadX(memory, groupStream, 120.0F);
  WriteQuadX(memory, groupStream + 48U, 300.0F);
  // Slot 0: visible, base X 100, origins (150, 250).
  const std::uint32_t slot = kHeap + 0x6000U, buffer = kHeap + 0x6100U,
                      base = kHeap + 0x6200U, origins = kHeap + 0x6300U;
  Require(memory.Write32(group + 0x224U, 1U) && memory.Write32(group + 0x0CU, slot) &&
              memory.Write8(group + 0x434U, 1U) && memory.Write32(slot + 8U, buffer) &&
              memory.Write32(buffer, 2U) && memory.Write32(buffer + 0x0CU, base) &&
              memory.Write32(buffer + 0x1CU, origins),
          "assignment icon");
  WriteFloat(memory, base, 100.0F);
  WriteFloat(memory, origins, 150.0F);
  WriteFloat(memory, origins + 8U, 250.0F);
}
} // namespace

int main() {
  using namespace Oot3dNativeGame;
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"wide-items-test", 0x004F0000, 0x00310000, true, false, {}}, &error),
          "cannot map wide Items fixture");
  BuildItemsPage(memory);

  // The geometry edits move the background, grid, icon groups and icons.
  Require(ApplyTopScreenWideItemsGeometry(memory), "geometry edits failed");
  const std::uint32_t stream = kHeap + 0x1000U, groupStream = kHeap + 0x5000U;
  Require(ReadFloat(memory, stream + 3U * 48U) == 60.0F &&
              ReadFloat(memory, stream + 36U * 48U + 36U) == 60.0F &&
              ReadFloat(memory, stream) == 83.0F && ReadFloat(memory, stream + 48U + 24U) == 83.0F &&
              ReadFloat(memory, stream + 2U * 48U) == 100.0F &&
              ReadFloat(memory, stream + 12U * 48U) == 100.0F,
          "Items stream edits do not match 0x005D1A10..0x005D1A50");
  Require(ReadFloat(memory, groupStream) == 103.0F && ReadFloat(memory, groupStream + 48U) == 300.0F,
          "icon group edits do not match 0x005CA184");
  Require(ReadFloat(memory, kHeap + 0x6300U) == -17.0F &&
              ReadFloat(memory, kHeap + 0x6308U) == 250.0F,
          "assignment origins do not match 0x005D1A84..0x005D1B84");

  TopScreenWideItemsRuntime runtime(memory);
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0;

  // Items inactive, or the page not on the upper screen: native draw.
  auto page = CallerState(kTopScreenPageProjectionUploadCall);
  Require(memory.Write32(kItemsState, 0U), "items state");
  Require(!runtime.ExecutePageProjection(page, true) && !runtime.PageWide(),
          "an inactive Items page was widened");
  Require(memory.Write32(kItemsState, 2U), "items state");
  Require(!runtime.ExecutePageProjection(page, false) && !runtime.PageWide(),
          "a lower-screen page was widened");
  auto items = CallerState(kTopScreenItemsPageDrawCall);
  Require(!runtime.ExecuteItemsDraw(items, &result, &blocks), "a narrow page used the Items wrapper");

  // Active Items on the upper screen: the native upload runs at once with
  // the patched projection; no call may run between the writer bind and it.
  const auto pageCaller = page;
  Require(!runtime.ExecutePageProjection(page, true) && Same(page, pageCaller) &&
              page.r[15] == kTopScreenPageProjectionUploadCall && runtime.PageWide(),
          "the native upload does not run at once with the caller state");
  Require(memory.Read32(kMatrix + 0x10U, &blocks) && blocks == 0xBBA3D70AU &&
              memory.Read32(kMatrix + 0x1CU, &blocks) && blocks == 0x3F4CCCCDU,
          "the page projection is not -0.005 / 0.8");

  // The Items wrapper: projection 0.6, the native page draw, projection 0.8.
  const auto itemsCaller = items;
  auto executeItems = [&](auto &s, auto *r, auto *b) { return runtime.ExecuteItemsDraw(s, r, b); };
  for (std::size_t call = 0; call < 4U; ++call) {
    oot3d::recomp::a32::ExecutionResult step;
    Require(runtime.ExecuteItemsDraw(items, &step, &blocks) && items.r[14] == kTopScreenItemsPageDrawCall,
            "the Items projection upload is not issued");
    if (call == 1U) {
      Require(step.pc == kUploadUniformBlock0 &&
                  ReadFloat(memory, items.r[1] + 0x1CU) == std::bit_cast<float>(0x3F19999AU),
              "the Items projection is not offset by 0.6");
    }
    items.r[15] = kTopScreenItemsPageDrawCall;
  }
  Require(Calls(executeItems, items, kTopScreenItemsPageDrawCall, kItemsDraw, 0x0123U, kStackTop - 0xE0U),
          "the native Items draw is not issued");

  // Inside the Items draw, a cursor over the grid uses the 0.685 projection.
  WriteQuadX(memory, groupStream, 100.0F);
  auto cursor = CallerState(kTopScreenItemsCursorDrawCallA);
  // A cursor owner whose +4 model resolves to the icon group's stream.
  Require(memory.Write32(kHeap + 0x7000U + 4U, kHeap + 0x4000U), "cursor owner");
  cursor.r[0] = kHeap + 0x7000U;
  // The page draw's own frame lies below the Items scratch.
  cursor.r[13] = kStackTop - 0x200U;
  const auto cursorCaller = cursor;
  auto executeCursor = [&](auto &s, auto *r, auto *b) {
    return runtime.ExecuteCursorDraw(kTopScreenItemsCursorDrawCallA, s, r, b);
  };
  for (std::size_t call = 0; call < 4U; ++call) {
    oot3d::recomp::a32::ExecutionResult step;
    Require(runtime.ExecuteCursorDraw(kTopScreenItemsCursorDrawCallA, cursor, &step, &blocks),
            "the cursor projection upload is not issued");
    if (call == 1U) {
      Require(ReadFloat(memory, cursor.r[1] + 0x1CU) == std::bit_cast<float>(0x3F2F5C29U),
              "the cursor projection is not offset by 0.685");
    }
    cursor.r[15] = kTopScreenItemsCursorDrawCallA;
  }
  Require(Calls(executeCursor, cursor, kTopScreenItemsCursorDrawCallA, kQuadGroupDraw,
                kHeap + 0x7000U, kStackTop - 0x2E0U),
          "the cursor group is not drawn");
  for (std::size_t call = 0; call < 4U; ++call) {
    Require(runtime.ExecuteCursorDraw(kTopScreenItemsCursorDrawCallA, cursor, &result, &blocks),
            "the Items projection is not restored after the cursor");
    cursor.r[15] = kTopScreenItemsCursorDrawCallA;
  }
  Require(runtime.ExecuteCursorDraw(kTopScreenItemsCursorDrawCallA, cursor, &result, &blocks) &&
              result.pc == kTopScreenItemsCursorDrawCallA + 4U && Same(cursor, cursorCaller),
          "the cursor draw does not resume after its call site");
  // A cursor right of X 256 keeps the Items projection; a missing second
  // cursor group draws nothing.
  WriteQuadX(memory, groupStream, 300.0F);
  auto rightCursor = cursorCaller;
  Require(!runtime.ExecuteCursorDraw(kTopScreenItemsCursorDrawCallA, rightCursor, &result, &blocks),
          "a right-column cursor changed projection");
  auto missing = CallerState(kTopScreenItemsCursorDrawCallB);
  missing.r[0] = 0U;
  Require(runtime.ExecuteCursorDraw(kTopScreenItemsCursorDrawCallB, missing, &result, &blocks) &&
              result.pc == kTopScreenItemsCursorDrawCallB + 4U,
          "a missing second cursor group was drawn");

  // The page draw returned: projection 0.8, then resume after the call site.
  for (std::size_t call = 0; call < 4U; ++call) {
    oot3d::recomp::a32::ExecutionResult step;
    Require(runtime.ExecuteItemsDraw(items, &step, &blocks), "the page projection is not restored");
    if (call == 1U) {
      Require(ReadFloat(memory, items.r[1] + 0x1CU) == std::bit_cast<float>(0x3F4CCCCDU),
              "the restored projection is not offset by 0.8");
    }
    items.r[15] = kTopScreenItemsPageDrawCall;
  }
  Require(runtime.ExecuteItemsDraw(items, &result, &blocks) &&
              result.pc == kTopScreenItemsPageDrawCall + 4U && Same(items, itemsCaller),
          "the Items draw does not resume after its call site");
  auto outside = CallerState(kTopScreenItemsCursorDrawCallA);
  outside.r[0] = kHeap + 0x7000U;
  WriteQuadX(memory, groupStream, 100.0F);
  Require(!runtime.ExecuteCursorDraw(kTopScreenItemsCursorDrawCallA, outside, &result, &blocks),
          "a cursor outside the Items draw changed projection");
  Require(runtime.WidePages() == 1U && runtime.WideItemsDraws() == 1U && runtime.CursorDraws() == 1U,
          "wide Items counters are wrong");
  // System joins Items and the native active-Map && mode == 1 predicate.
  // Exercise the shared viewport/projection decision, including lower draws and exits.
  struct PageCase {
    std::uint32_t Items, Map, Mode, System;
    bool OnTop, Wide, ItemsWide;
  };
  constexpr std::array pageCases{
      PageCase{0U, 0U, 0U, 1U, true, true, false},
      PageCase{0U, 0U, 0U, 2U, true, true, false},
      PageCase{0U, 0U, 0U, 2U, false, false, false},
      PageCase{0U, 1U, 0U, 2U, true, true, false},
      PageCase{0U, 1U, 1U, 0U, true, true, false},
      PageCase{0U, 1U, 0U, 0U, true, false, false},
      PageCase{0U, 1U, 2U, 0U, true, false, false},
      PageCase{0U, 0U, 1U, 0U, true, false, false},
      PageCase{0U, 2U, 1U, 0U, false, false, false},
      PageCase{0U, 2U, 1U, 0U, true, true, false},
      PageCase{2U, 1U, 1U, 0U, true, true, true},
      PageCase{2U, 0U, 0U, 0U, true, true, true},
      PageCase{0U, 0U, 0U, 0U, true, false, false}};
  for (const auto &test : pageCases) {
    Require(memory.Write32(kItemsState, test.Items) &&
                memory.Write32(kMapState, test.Map) && memory.Write32(kMapMode, test.Mode) &&
                memory.Write32(kSystemState, test.System),
            "cannot seed wide-page predicates");
    TopScreenWidePageState width;
    Require(ReadTopScreenWidePageState(memory, &width) &&
                width.ItemsActive == (test.Items != 0U) &&
                width.DungeonMapActive == (test.Map != 0U && test.Mode == 1U) &&
                width.SystemActive == (test.System != 0U),
            "wide-page predicate differs from the native Map/Items/System checks");
    auto plan = ResolveTopScreenPauseTargetCommand({true, 0x401U, false}, test.OnTop);
    WidenTopScreenPauseTargetForPage(plan, width.Wide());
    Require((plan.BindTopTarget && plan.ViewportY == 0U && plan.ViewportHeight == 400U) ==
                test.Wide,
            "page projection and target viewport disagree");
    WriteFloat(memory, kMatrix + 0x10U, -0.00625F);
    WriteFloat(memory, kMatrix + 0x1CU, 1.0F);
    auto mapPage = CallerState(kTopScreenPageProjectionUploadCall);
    const auto before = mapPage;
    Require(!runtime.ExecutePageProjection(mapPage, test.OnTop) &&
                Same(mapPage, before) && mapPage.r[15] == before.r[15] &&
                runtime.PageWide() == test.Wide &&
                ReadFloat(memory, kMatrix + 0x10U) ==
                    (test.Wide ? std::bit_cast<float>(0xBBA3D70AU) : -0.00625F) &&
                ReadFloat(memory, kMatrix + 0x1CU) ==
                    (test.Wide ? std::bit_cast<float>(0x3F4CCCCDU) : 1.0F),
            "page upload changed guest state or used the wrong wide projection");
    if (!test.ItemsWide) {
      const float gridX = ReadFloat(memory, stream);
      auto inactiveItems = CallerState(kTopScreenItemsPageDrawCall);
      const auto inactiveCaller = inactiveItems;
      Require(!runtime.ExecuteItemsDraw(inactiveItems, &result, &blocks) &&
                  Same(inactiveItems, inactiveCaller) &&
                  ReadFloat(memory, stream) == gridX &&
                  runtime.WideItemsDraws() == 1U,
              "a Map/System draw invoked the Items geometry wrapper");
    }
  }
  Require(memory.Write32(kItemsState, 1U), "cannot arm Items before reset");
  auto armedPage = CallerState(kTopScreenPageProjectionUploadCall);
  Require(!runtime.ExecutePageProjection(armedPage, true) && runtime.PageWide(),
          "Items was not armed before reset");
  runtime.Reset();
  Require(!runtime.PageWide(), "reset retained the wide-page projection");
  auto resetItems = CallerState(kTopScreenItemsPageDrawCall);
  Require(!runtime.ExecuteItemsDraw(resetItems, &result, &blocks),
          "reset retained an armed Items draw");

  Require(memory.Write32(kItemsState, 0U) && memory.Write32(kSystemState, 2U),
          "cannot seed System projection failure");
  auto failedPage = CallerState(kTopScreenPageProjectionUploadCall);
  failedPage.r[1] = 0U;
  const auto failedCaller = failedPage;
  bool failed = false;
  try { runtime.ExecutePageProjection(failedPage, true); }
  catch (const std::runtime_error &) { failed = true; }
  Require(failed && !runtime.PageWide() && Same(failedPage, failedCaller) &&
              failedPage.r[15] == failedCaller.r[15],
          "failed System projection changed guest state or armed a wide draw");
  Require(!runtime.ExecuteItemsDraw(resetItems, &result, &blocks),
          "failed System projection armed the Items geometry wrapper");

  NativeA32Memory missingPage;
  TopScreenWidePageState missingWidth{true, true, true};
  Require(!ReadTopScreenWidePageState(missingPage, &missingWidth) && !missingWidth.Wide(),
          "a failed page-state read retained the previous wide decision");

  // All six instruction sites are coordinate-only replacements; the native
  // branch still owns icon index, Y, size, flags and LR.
  constexpr std::array<std::uint32_t, 6> iconSites{
      0x004426B4U, 0x004426D4U, 0x00442704U,
      0x00442730U, 0x00442760U, 0x0044278CU};
  const auto registeredSites = TopScreenDungeonMapIconCoordinateSites();
  Require(registeredSites.size() == iconSites.size(), "missing dungeon icon hook sites");
  for (std::size_t index = 0; index < iconSites.size(); ++index) {
    const auto pc = iconSites[index];
    Require(registeredSites[index] == pc, "wrong dungeon icon hook site");
    auto icon = CallerState(pc);
    icon.r[1] = static_cast<std::uint32_t>(index / 2U);
    icon.r[2] = 10U;
    icon.r[3] = 112U - static_cast<std::uint32_t>(index / 2U) * 50U;
    icon.fpscr = 0x1000000U;
    icon.vfp[31] = 0x12345678U;
    const std::uint32_t size = index % 2U == 0U ? 42U : 0U;
    Require(memory.Write32(icon.r[13], size) && memory.Write32(icon.r[13] + 4U, size),
            "cannot seed icon size arguments");
    auto expected = icon;
    expected.r[2] = 0xFFFFFFE2U;
    std::uint32_t width = 0U, height = 0U;
    blocks = 0U;
    Require(ExecuteTopScreenDungeonMapIconCoordinate(pc, icon, &result, &blocks) &&
                Same(icon, expected) && icon.r[15] == pc + 4U &&
                result.kind == oot3d::recomp::a32::ExitKind::Branch &&
                result.pc == pc + 4U && blocks == 1U &&
                memory.Read32(icon.r[13], &width) && width == size &&
                memory.Read32(icon.r[13] + 4U, &height) && height == size,
            "dungeon icon hook changed more than R2 and the resume PC");
    auto resume = CallerState(pc + 4U);
    const auto resumeCaller = resume;
    Require(!ExecuteTopScreenDungeonMapIconCoordinate(pc + 4U, resume, &result, &blocks) &&
                Same(resume, resumeCaller) && resume.r[15] == resumeCaller.r[15],
            "dungeon icon hook intercepted its native continuation");
  }
  std::cout << "TopScreen wide page runtime tests passed\n";
  return 0;
}
