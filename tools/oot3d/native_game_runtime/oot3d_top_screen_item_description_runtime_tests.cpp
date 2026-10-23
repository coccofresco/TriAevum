#include "oot3d_top_screen_item_description_runtime.h"

#include <cstdlib>
#include <iostream>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

constexpr std::uint32_t kItemsState = 0x0050672CU;
constexpr std::uint32_t kItemsCursorRow = 0x00506774U;
constexpr std::uint32_t kSystemState = 0x0050A530U;
constexpr std::uint32_t kStackTop = 0x005FF000U;

oot3d::recomp::a32::GuestState CallerState() {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 123;
  state.r[4] = 456;
  state.r[13] = kStackTop;
  state.r[14] = 0x00400000;
  state.r[15] = Oot3dNativeGame::kTopScreenItemDescriptionBoundary;
  state.vfp[0] = 789;
  state.cpsr = 0xA0000010;
  return state;
}

bool Same(const oot3d::recomp::a32::GuestState &a, const oot3d::recomp::a32::GuestState &b) {
  for (std::size_t index = 0; index < 16U; ++index)
    if (a.r[index] != b.r[index]) return false;
  return a.vfp[0] == b.vfp[0] && a.cpsr == b.cpsr;
}

// One native call returns to the boundary, as the callee's `bx lr` does.
bool CallsAndReturns(Oot3dNativeGame::TopScreenItemDescriptionRuntime &runtime,
                     oot3d::recomp::a32::GuestState &state, std::uint32_t entry,
                     std::uint32_t r0, std::uint32_t r1, std::uint32_t r2, std::uint32_t r3) {
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0;
  if (!runtime.Execute(state, &result, &blocks) || !runtime.Pending()) return false;
  const bool ok = state.r[15] == entry && result.pc == entry && state.r[0] == r0 &&
                  state.r[1] == r1 && state.r[2] == r2 && state.r[3] == r3 &&
                  state.r[13] == kStackTop - 0xA0U &&
                  state.r[14] == Oot3dNativeGame::kTopScreenItemDescriptionBoundary &&
                  blocks == 1;
  state.r[15] = Oot3dNativeGame::kTopScreenItemDescriptionBoundary;
  state.r[0] = 0xDEAD;
  return ok;
}
} // namespace

int main() {
  using namespace Oot3dNativeGame;
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"item-description-test", 0x00300000, 0x00300000, true, false, {}}, &error),
          "cannot map description fixture");
  TopScreenItemDescriptionRuntime runtime(memory);
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0;
  auto state = CallerState();

  Require(memory.Write32(kItemsState, 2) && memory.Write32(kItemsCursorRow, 0),
          "cannot seed a browsing Items page");
  runtime.ArmPageDraw();
  Require(!runtime.Execute(state, &result, &blocks) && Same(state, CallerState()),
          "a hidden description entered the guest");
  runtime.ArmPageDraw();
  runtime.VisibleState() = true;
  runtime.DisarmPageDraw();
  Require(!runtime.Execute(state, &result, &blocks) && Same(state, CallerState()),
          "a return with no page draw redrew the description");

  // Visible on Items row >= 2: the 2.1.1 sequence at X = 300, then the page
  // draw resumes with its own registers.
  Require(memory.Write32(kItemsCursorRow, 2), "cannot seed the cursor row");
  runtime.ArmPageDraw();
  const std::uint32_t scratch = kStackTop - 0xA0U;
  const std::uint32_t projection = scratch + 0x20U;
  const std::uint32_t identity = scratch + 0x60U;
  Require(CallsAndReturns(runtime, state, 0x0032471CU, 0x005C58D8U, 0, 0, 0),
          "the material command list was not bound first");
  std::uint32_t word = 0;
  Require(memory.Read32(projection + 4U, &word) && word == 0xBC088889U &&
              memory.Read32(identity, &word) && word == 0x3F800000U &&
              memory.Read32(identity + 4U, &word) && word == 0U,
          "the payload matrices were not written to the stack scratch");
  Require(CallsAndReturns(runtime, state, 0x003246ECU, 0x005C58D8U, projection, identity, 0),
          "the projection upload is wrong");
  Require(CallsAndReturns(runtime, state, 0x003246BCU, 0x005C58D8U, identity, 0, 0),
          "the model-view upload is wrong");
  Require(CallsAndReturns(runtime, state, 0x002F9C74U, 0x005C58D8U, 0, 0, 0),
          "the material state was not committed");
  Require(CallsAndReturns(runtime, state, 0x002FEABCU, 300U, 0, 480U, 400U),
          "the description viewport does not follow the cursor row");
  Require(CallsAndReturns(runtime, state, 0x0042CBCCU, 0x005C1878U, 0, 0, 0),
          "the native message was not drawn");
  Require(CallsAndReturns(runtime, state, 0x002FEABCU, 0, 0, 480U, 400U),
          "the full viewport was not restored");
  Require(!runtime.Execute(state, &result, &blocks) && !runtime.Pending() &&
              Same(state, CallerState()) && runtime.Redraws() == 1U,
          "the page draw did not resume with its own state");
  Require(!runtime.Execute(state, &result, &blocks), "one page draw redrew twice");

  // A System page blocks the redraw; Reset clears visibility and pending work.
  Require(memory.Write32(kSystemState, 1), "cannot seed a System page");
  runtime.ArmPageDraw();
  Require(!runtime.Execute(state, &result, &blocks) && runtime.Redraws() == 1U,
          "the description was drawn over a System page");
  Require(memory.Write32(kSystemState, 0), "cannot clear the System page");
  runtime.ArmPageDraw();
  Require(runtime.Execute(state, &result, &blocks) && runtime.Pending(),
          "the redraw did not start");
  runtime.Reset();
  Require(!runtime.Pending() && !runtime.Visible(), "Reset kept description state");
  std::cout << "item description runtime tests passed\n";
  return 0;
}
