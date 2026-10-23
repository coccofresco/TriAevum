#include "oot3d_top_screen_visions_runtime.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

using Oot3dNativeGame::NativeA32Memory;
using Oot3dNativeGame::TopScreenVisionsRuntime;
using Oot3dNativeGame::kTopScreenVisionsLowerDispatch;
using Oot3dNativeGame::kTopScreenVisionsUpperDispatch;

constexpr std::uint32_t kPlay = 0x00700000U;
constexpr std::uint32_t kCallerReturn = 0x0030051CU;
constexpr std::uint32_t kStack = 0x005FF000U;
constexpr std::array<std::uint32_t, 6> kPauseWords{
    0x00504484U, 0x0050AF68U, 0x0050672CU, 0x00506CE8U, 0x0050A530U, 0x005043F4U};

std::uint32_t Read(NativeA32Memory &memory, std::uint32_t address) {
  std::uint32_t value = 0U;
  Require(memory.Read32(address, &value), "cannot read fixture word");
  return value;
}

void Write(NativeA32Memory &memory, std::uint32_t address, std::uint32_t value) {
  Require(memory.Write32(address, value), "cannot write fixture word");
}

oot3d::recomp::a32::GuestState Caller(std::uint32_t pc) {
  oot3d::recomp::a32::GuestState state{};
  state.r[0] = kPlay;
  state.r[4] = 0x44444444U;
  state.r[13] = kStack;
  state.r[14] = kCallerReturn;
  state.r[15] = pc;
  return state;
}

struct Branch {
  std::uint32_t Target;
  std::array<std::uint32_t, 4> Arguments;
};

// Runs one dispatcher entry, returning each guest call; a call returns to
// the entry, which the runtime handles again.
std::vector<Branch> Run(TopScreenVisionsRuntime &runtime, std::uint32_t pc,
                        oot3d::recomp::a32::GuestState &state) {
  std::vector<Branch> branches;
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0U;
  while (runtime.Execute(pc, state, &result, &blocks)) {
    branches.push_back({state.r[15], {state.r[0], state.r[1], state.r[2], state.r[3]}});
    if (state.r[14] != pc) break;
  }
  return branches;
}

void TestUpperDrawsListOnTop(NativeA32Memory &memory) {
  TopScreenVisionsRuntime runtime(memory);
  runtime.ObserveControllerDraw(0x00ABCDEFU);
  for (const auto word : kPauseWords) Write(memory, word, 7U);
  Write(memory, 0x00534C94U + 4U, 0x0032B76CU);
  Write(memory, kPlay + 0x104U, 1U);
  auto state = Caller(kTopScreenVisionsUpperDispatch);
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0U;
  std::vector<Branch> branches;
  bool pauseWordsCleared = false;
  while (runtime.Execute(kTopScreenVisionsUpperDispatch, state, &result, &blocks)) {
    branches.push_back({state.r[15], {state.r[0], state.r[1], state.r[2], state.r[3]}});
    if (state.r[15] == 0x0041EC50U)
      pauseWordsCleared = Read(memory, kPauseWords[0]) == 0U && Read(memory, kPauseWords[5]) == 0U;
    if (state.r[14] != kTopScreenVisionsUpperDispatch) break;
  }
  Require(branches.size() == 5U, "upper Visions sequence has the wrong length");
  Require(branches[0].Target == 0x002FEABCU && branches[0].Arguments[1] == 40U &&
              branches[0].Arguments[2] == 480U && branches[0].Arguments[3] == 320U,
          "list viewport is not 0x005D5698's (0,40,480,320)");
  Require(branches[1].Target == 0x0041EC50U && pauseWordsCleared,
          "PauseUi_Draw did not run with 0x005CB848's six words cleared");
  Require(branches[2].Target == 0x0032B76CU && branches[2].Arguments[0] == kPlay,
          "the native lower handler of the submode was not drawn on top");
  for (const auto word : kPauseWords)
    Require(Read(memory, word) == 7U, "the six pause words were not restored");
  Require(branches[3].Target == 0x002FEABCU && branches[3].Arguments[3] == 400U,
          "the full upper viewport was not restored");
  Require(branches[4].Target == 0x0041AFACU && branches[4].Arguments[0] == 0x00ABCDEFU &&
              state.r[14] == kCallerReturn && state.r[4] == 0x44444444U &&
              state.r[13] == kStack,
          "queue 6 is not a tail call with the caller's state");
  Require(runtime.TakeUpperDrawn() && !runtime.TakeUpperDrawn(),
          "the upper pass did not report its list once");

  Write(memory, kPlay + 0x104U, 3U);
  auto movie = Caller(kTopScreenVisionsUpperDispatch);
  Require(Run(runtime, kTopScreenVisionsUpperDispatch, movie).empty(),
          "submodes from 3 on must keep the native table");
}

void TestLowerPagesWithShoulders(NativeA32Memory &memory) {
  TopScreenVisionsRuntime runtime(memory);
  const auto setList = [&](std::uint32_t top, std::uint32_t cursor) {
    Write(memory, kPlay + 0x104U, 1U);
    Write(memory, kPlay + 0x118U, 0U);
    Write(memory, kPlay + 0x11CU, 0U);
    Write(memory, kPlay + 0x130U, top);
    Write(memory, kPlay + 0x12CU, cursor);
    Write(memory, kPlay + 0x17CU, 8U);
  };
  setList(0U, 1U);
  Write(memory, 0x005655A0U, 0x100U);  // R
  auto state = Caller(kTopScreenVisionsLowerDispatch);
  auto branches = Run(runtime, kTopScreenVisionsLowerDispatch, state);
  Require(branches.size() == 3U && branches[0].Target == 0x003243A4U &&
              branches[0].Arguments[0] == kPlay,
          "the native lower backdrop handler did not run first");
  Require(branches[1].Target == 0x0037547CU && branches[1].Arguments[0] == 0x010004BFU &&
              branches[1].Arguments[2] == 4U && Read(memory, (kStack - 8U)) == 0x0054AC20U &&
              Read(memory, kStack - 4U) == 0x0054AC24U,
          "R did not play 0x010004BF with the native sound arguments");
  Require(Read(memory, kPlay + 0x134U) == 3U && Read(memory, kPlay + 0x12CU) == 4U &&
              Read(memory, kPlay + 0x118U) == 0xFFFFFFFBU && Read(memory, kPlay + 0x11CU) == 0xFFFFFFFBU,
          "R did not move the list down by three");
  Require(branches[2].Target == kCallerReturn && state.r[13] == kStack,
          "the lower dispatch did not return with the caller's stack");

  // Held R is not a new press; L pages back up.
  setList(3U, 4U);
  state = Caller(kTopScreenVisionsLowerDispatch);
  Require(Run(runtime, kTopScreenVisionsLowerDispatch, state).size() == 2U &&
              Read(memory, kPlay + 0x12CU) == 4U,
          "a held shoulder paged again");
  Write(memory, 0x005655A0U, 0x200U);  // L
  state = Caller(kTopScreenVisionsLowerDispatch);
  Require(Run(runtime, kTopScreenVisionsLowerDispatch, state).size() == 3U &&
              Read(memory, kPlay + 0x134U) == 0U && Read(memory, kPlay + 0x12CU) == 1U &&
              Read(memory, kPlay + 0x118U) == 5U,
          "L did not move the list up by three");

  // R near the end clamps the cursor to the last entry.
  Write(memory, 0x005655A0U, 0U);
  state = Caller(kTopScreenVisionsLowerDispatch);
  (void)Run(runtime, kTopScreenVisionsLowerDispatch, state);
  setList(3U, 7U);
  Write(memory, kPlay + 0x17CU, 7U);
  Write(memory, 0x005655A0U, 0x100U);
  state = Caller(kTopScreenVisionsLowerDispatch);
  (void)Run(runtime, kTopScreenVisionsLowerDispatch, state);
  Require(Read(memory, kPlay + 0x134U) == 6U && Read(memory, kPlay + 0x12CU) == 6U,
          "R did not clamp the cursor to the last entry");
}
} // namespace

int main() {
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"visions-test", 0x004F0000U, 0x00310000U, true, false, {}}, &error),
          "cannot map the Visions fixture");
  TestUpperDrawsListOnTop(memory);
  TestLowerPagesWithShoulders(memory);
  std::cout << "oot3d_top_screen_visions_runtime_tests: ok\n";
  return 0;
}
