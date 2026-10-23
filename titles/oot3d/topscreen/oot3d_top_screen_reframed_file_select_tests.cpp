#include "oot3d_top_screen_reframed_file_select.h"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

using namespace Oot3dNativeGame;

constexpr std::uint32_t kRoot = 0x00700000U;
constexpr std::uint32_t kContext = 0x00710000U;
constexpr std::uint32_t kPass = 0x00720000U;
constexpr std::uint32_t kBoard = 0x00730000U;
constexpr std::uint32_t kPositions = 0x00731000U;
constexpr std::uint32_t kStack = 0x007FF000U;
constexpr std::uint32_t kCommands = 0x00740000U;
constexpr std::uint32_t kCallback = 0x00477E30U;

std::uint32_t Read(NativeA32Memory &memory, std::uint32_t address) {
  std::uint32_t value = 0U;
  Require(memory.Read32(address, &value), "cannot read fixture word");
  return value;
}
float ReadF(NativeA32Memory &memory, std::uint32_t address) {
  return std::bit_cast<float>(Read(memory, address));
}
void Write(NativeA32Memory &memory, std::uint32_t address, std::uint32_t value) {
  Require(memory.Write32(address, value), "cannot write fixture word");
}
void WriteF(NativeA32Memory &memory, std::uint32_t address, float value) {
  Write(memory, address, std::bit_cast<std::uint32_t>(value));
}

void SetFileSelect(NativeA32Memory &memory, std::uint32_t boardState) {
  Write(memory, 0x00588E3CU, 2U);
  Write(memory, 0x00504FB0U, boardState);
}

oot3d::recomp::a32::GuestState Caller(NativeA32Memory &memory, std::uint32_t pc,
                                      bool upperView = true) {
  oot3d::recomp::a32::GuestState state{};
  state.r[0] = kPass;
  state.r[4] = kRoot;
  state.r[9] = kContext;
  state.r[13] = kStack;
  state.r[14] = pc + 4U;
  state.r[15] = pc;
  Write(memory, kStack + 0x6CU, upperView ? 0x00419524U : 0x004197D4U);
  return state;
}

struct Branch {
  std::uint32_t Target;
  std::uint32_t Return;
  std::array<std::uint32_t, 4> Arguments;
};

// Each guest call returns to the site, which the runtime handles again.
std::vector<Branch> Run(TopScreenReframedFileSelect &runtime, std::uint32_t pc,
                        oot3d::recomp::a32::GuestState &state) {
  std::vector<Branch> branches;
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0U;
  while (runtime.Execute(pc, state, &result, &blocks)) {
    branches.push_back({state.r[15], state.r[14], {state.r[0], state.r[1], state.r[2], state.r[3]}});
    if (state.r[14] != pc || state.r[15] == pc + 4U) break;
  }
  return branches;
}

bool IsViewport(const Branch &branch, std::uint32_t x, std::int32_t y, std::uint32_t width,
                std::int32_t height) {
  return branch.Target == 0x002FEABCU && branch.Arguments[0] == x &&
         branch.Arguments[1] == static_cast<std::uint32_t>(y) && branch.Arguments[2] == width &&
         branch.Arguments[3] == static_cast<std::uint32_t>(height);
}

void TestInactiveLeavesNativeCalls(NativeA32Memory &memory) {
  TopScreenReframedFileSelect runtime(memory);
  Write(memory, 0x00588E3CU, 0U);
  Write(memory, 0x00504FB0U, 7U);
  for (const auto pc : TopScreenReframedFileSelect::HookPcs()) {
    auto state = Caller(memory, pc);
    Require(Run(runtime, pc, state).empty(), "a site was taken outside the file selection");
  }
  SetFileSelect(memory, 0U);
  auto state = Caller(memory, kReframedFileFinishCall);
  Require(Run(runtime, kReframedFileFinishCall, state).empty(),
          "the file interface was drawn before the native board exists");
  SetFileSelect(memory, 2U);
  state = Caller(memory, kReframedFileFinishCall, false);
  Require(Run(runtime, kReframedFileFinishCall, state).empty(),
          "the lower view pass drew the file interface");
}

void TestFinishDrawsInterfaceTwice(NativeA32Memory &memory) {
  TopScreenReframedFileSelect runtime(memory);
  SetFileSelect(memory, 2U);
  Write(memory, kRoot + 0x7444U, kCallback);
  Write(memory, 0x0054CC4CU, kCommands);
  Write(memory, 0x0054CC50U, kCommands + 0x1000U);
  auto state = Caller(memory, kReframedFileFinishCall);
  const auto branches = Run(runtime, kReframedFileFinishCall, state);
  Require(branches.size() == 9U, "the file interface sequence has the wrong length");
  Require(branches[0].Target == 0x004228D4U && branches[0].Arguments[0] == kPass,
          "the native pass stage 5 did not run first");
  Require(IsViewport(branches[1], 56U, 164, 336U, 224), "interface viewport is not (56,164,336,224)");
  Require(branches[2].Target == 0x0041EC50U && branches[3].Target == 0x0041F308U &&
              branches[4].Target == kCallback && branches[4].Arguments[0] == kContext &&
              branches[5].Target == 0x0041EC50U && branches[7].Target == kCallback,
          "the lower interface was not drawn in both include regions");
  // Reframed's separate Back pass is not replayed: TopScreen has no Back.
  // The final viewport returns straight to the pass, with the caller's state.
  Require(IsViewport(branches[8], 0U, 0, 480U, 400) &&
              branches[8].Return == kReframedFileFinishCall + 4U && state.r[4] == kRoot &&
              state.r[13] == kStack,
          "the full viewport was not restored before continuing the pass");
  // Three scissors, all include (3) or disabled (0): the renderer has no
  // exclude scissor. Interface without Back's corner, then disable.
  Require(Read(memory, 0x0054CC4CU) == kCommands + 18U * 4U, "three scissors were not written");
  for (std::uint32_t index = 0; index < 3U; ++index)
    Require(Read(memory, kCommands + index * 24U + 4U) == 0x000F0065U &&
                Read(memory, kCommands + index * 24U) != 1U,
            "an exclude scissor was written");
  Require(Read(memory, kCommands + 8U) == 0U && Read(memory, kCommands + 16U) == ((345U << 16U) | 479U) &&
              Read(memory, kCommands + 32U) == ((346U << 16U) | 110U) &&
              Read(memory, kCommands + 48U) == 0U,
          "scissor rectangles differ from Reframed's interface regions");
}

void TestSceneKeepsAspect(NativeA32Memory &memory) {
  TopScreenReframedFileSelect runtime(memory);
  SetFileSelect(memory, 2U);
  auto state = Caller(memory, 0x00300400U);
  const auto branches = Run(runtime, 0x00300400U, state);
  Require(branches.size() == 2U && branches[1].Target == 0x004224ACU &&
              branches[1].Arguments[0] == kRoot + 0x180U,
          "the file scene was not drawn after its viewport");
  // The rotated surface: x/width vertical of 480, y/height horizontal of 400.
  // Link's viewport takes the same fraction of both, so he is not squeezed.
  Require(IsViewport(branches[0], 119U, -33, 300U, 250) && 300U * 400U == 250U * 480U,
          "Link's viewport does not keep the scene's aspect ratio");
}

void TestLogoSplitAndRestore(NativeA32Memory &memory) {
  TopScreenReframedFileSelect runtime(memory);
  SetFileSelect(memory, 2U);
  constexpr std::uint32_t logo = 0x00750000U, fade = 0x00750100U;
  Write(memory, kPass + 0x24U, 2U);
  Write(memory, kPass + 0x764U, 0U);
  Write(memory, kPass + 0x394U, logo);
  Write(memory, kPass + 0x398U, fade);
  Write(memory, logo, 0x004EBD60U);
  Write(memory, fade, 0x004EBD60U);
  Write(memory, fade + 4U, 0x00760000U);
  Write(memory, logo + 4U, 0x00760000U + 0x1C8U);
  WriteF(memory, fade + 60U, 200.0F);
  WriteF(memory, fade + 64U, 120.0F);
  auto state = Caller(memory, kReframedFileLogoCall);
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0U;
  std::vector<Branch> branches;
  std::vector<std::uint32_t> drawnNode, drawnCount;
  while (runtime.Execute(kReframedFileLogoCall, state, &result, &blocks)) {
    branches.push_back({state.r[15], state.r[14], {state.r[0], state.r[1], state.r[2], state.r[3]}});
    if (state.r[15] == 0x004228E4U) {
      drawnNode.push_back(Read(memory, kPass + 0x394U));
      drawnCount.push_back(Read(memory, kPass + 0x24U));
    }
    if (state.r[14] != kReframedFileLogoCall) break;
  }
  Require(IsViewport(branches.front(), 290U, 114, 324U, 324), "logo viewport is not (290,114,324,324)");
  Require(drawnNode == std::vector<std::uint32_t>{logo, fade} &&
              drawnCount == std::vector<std::uint32_t>{1U, 1U},
          "logo and fade were not drawn as separate single-node passes");
  Require(Read(memory, kPass + 0x394U) == logo && Read(memory, kPass + 0x24U) == 2U,
          "the native logo pass was not restored");
  Require(IsViewport(branches.back(), 48U, 0, 384U, 180) &&
              branches.back().Return == kReframedFileLogoCall + 4U,
          "the late overlay viewport (48,0,384,180) did not end the logo site");
}

void TestCaptionCallbackAndBoards(NativeA32Memory &memory) {
  TopScreenReframedFileSelect runtime(memory);
  SetFileSelect(memory, 2U);
  Write(memory, kContext + 0x890U, 1U);
  Write(memory, kRoot + 0x7440U, 0x00471F60U);
  auto text = Caller(memory, kReframedFileTextBlock);
  const auto textBranches = Run(runtime, kReframedFileTextBlock, text);
  Require(textBranches.size() == 2U && IsViewport(textBranches[0], 56U, 2, 264U, 192) &&
              textBranches[1].Target == 0x00471F60U && textBranches[1].Arguments[0] == kContext &&
              textBranches[1].Return == 0x0030051CU && text.r[6] == kRoot + 0x7000U,
          "the caption callback was not drawn under the caption viewport");
  // Caption hidden (+0x890 != 2): the board pass is drawn once.
  Write(memory, kStack + 0x40U, kPass);
  auto boards = Caller(memory, kReframedFileBoardsBlock);
  Require(Run(runtime, kReframedFileBoardsBlock, boards).size() == 2U,
          "a hidden caption's board was composited more than once");
  Write(memory, kContext + 0x890U, 2U);
  text = Caller(memory, kReframedFileTextBlock);
  (void)Run(runtime, kReframedFileTextBlock, text);
  boards = Caller(memory, kReframedFileBoardsBlock);
  const auto composite = Run(runtime, kReframedFileBoardsBlock, boards);
  Require(composite.size() == 5U && composite[1].Arguments[0] == kPass &&
              composite[4].Target == 0x004228DCU &&
              composite[4].Return == kReframedFilePresentationBlock,
          "a shown caption's board was not composited four times");
  // No callback installed: blxne is not taken.
  Write(memory, kRoot + 0x7440U, 0U);
  text = Caller(memory, kReframedFileTextBlock);
  Require(Run(runtime, kReframedFileTextBlock, text).empty(), "an absent callback was called");
}

void TestLeatherSkipped(NativeA32Memory &memory) {
  TopScreenReframedFileSelect runtime(memory);
  SetFileSelect(memory, 2U);
  Write(memory, kRoot + 0x20U, 0U);
  auto state = Caller(memory, kReframedFileLeatherBlock);
  oot3d::recomp::a32::ExecutionResult result;
  Require(runtime.Execute(kReframedFileLeatherBlock, state, &result, nullptr) &&
              state.r[15] == 0x0041EE54U,
          "the leather base board was drawn in the file selection");
  // A nonzero +0x20 already skips the draw natively.
  Write(memory, kRoot + 0x20U, 1U);
  state = Caller(memory, kReframedFileLeatherBlock);
  Require(!runtime.Execute(kReframedFileLeatherBlock, state, &result, nullptr),
          "the native skip branch was replaced");
}

void TestBoardGeometry(NativeA32Memory &memory) {
  TopScreenReframedFileSelect runtime(memory);
  SetFileSelect(memory, 7U);
  Write(memory, kBoard, 184U);
  Write(memory, kBoard + 0x10U, kPositions);
  Write(memory, kBoard + 0x14U, 0U);
  Write(memory, kBoard + 0x1CU, 0U);
  for (std::uint32_t index = 0; index < 184U * 12U; ++index)
    WriteF(memory, kPositions + index * 4U, 0.0F);
  // Slot 0 badge (quad 10): a 20x20 square at (100,100).
  const auto badge = kPositions + 10U * 48U;
  const std::array<std::array<float, 2>, 4> square{{{100, 100}, {120, 100}, {100, 120}, {120, 120}}};
  for (std::uint32_t corner = 0; corner < 4U; ++corner) {
    WriteF(memory, badge + corner * 12U, square[corner][0]);
    WriteF(memory, badge + corner * 12U + 4U, square[corner][1]);
  }
  // Heading slice 0 at (20,10).
  WriteF(memory, kPositions, 20.0F);
  WriteF(memory, kPositions + 4U, 10.0F);
  for (std::uint32_t slot = 0; slot < 10U; ++slot) Write(memory, 0x0055B548U + slot * 4U, 0U);
  Write(memory, 0x00504FB8U, 9U);
  // Caption context from the latest upper pass.
  Write(memory, kRoot + 0x7440U, 0x00471F60U);
  auto text = Caller(memory, kReframedFileTextBlock);
  Write(memory, kContext + 0x89CU, 0U);
  (void)Run(runtime, kReframedFileTextBlock, text);

  // Back from the native regeneration of the board at r6+0x30: the
  // adjustment, then the native continuation.
  // Past the 184-quad positions (0x00731000..0x00733280).
  constexpr std::uint32_t owner = 0x00736000U;
  Write(memory, owner + 0x30U, kBoard);
  auto state = Caller(memory, kReframedFileBoardReturn);
  state.r[6] = owner;
  Require(Run(runtime, kReframedFileBoardReturn, state).empty(),
          "the native board continuation was replaced");
  Require(std::fabs(ReadF(memory, badge) - 102.0F) < 1e-4F &&
              std::fabs(ReadF(memory, badge + 3U * 12U) - 118.0F) < 1e-4F,
          "the slot badge was not scaled to 80% around its center");
  Require(ReadF(memory, kPositions) == -1000.0F && ReadF(memory, kPositions + 4U) == -1000.0F,
          "the heading board was shown on the action page");
  Require(Read(memory, kContext + 0x89CU) == 1U, "the caption was not kept on the action page");

  // Another board size is not the file board.
  Write(memory, kBoard, 183U);
  WriteF(memory, badge, 50.0F);
  state = Caller(memory, kReframedFileBoardReturn);
  state.r[6] = owner;
  (void)Run(runtime, kReframedFileBoardReturn, state);
  Require(ReadF(memory, badge) == 50.0F, "a foreign board was modified");

  // An erase confirmation (state 36): each control's seven quads shrink to
  // 60% around the group's center, and the heading widens.
  SetFileSelect(memory, 36U);
  Write(memory, kBoard, 184U);
  constexpr std::uint32_t offsets = 0x00734000U;
  Write(memory, kBoard + 0x1CU, offsets);
  for (std::uint32_t index = 0; index < 184U * 2U; ++index) WriteF(memory, offsets + index * 4U, 0.0F);
  Write(memory, 0x00504FD4U, 0U);
  const std::array<std::array<float, 2>, 4> control{{{200, 200}, {240, 200}, {200, 220}, {240, 220}}};
  for (std::uint32_t quad = 134U; quad < 141U; ++quad)
    for (std::uint32_t corner = 0; corner < 4U; ++corner) {
      WriteF(memory, kPositions + quad * 48U + corner * 12U, control[corner][0]);
      WriteF(memory, kPositions + quad * 48U + corner * 12U + 4U, control[corner][1]);
    }
  WriteF(memory, kPositions, 20.0F);
  WriteF(memory, kPositions + 4U, 10.0F);
  state = Caller(memory, kReframedFileBoardReturn);
  state.r[6] = owner;
  (void)Run(runtime, kReframedFileBoardReturn, state);
  const auto first = kPositions + 134U * 48U;
  Require(std::fabs(ReadF(memory, first) - 208.0F) < 1e-4F &&
              std::fabs(ReadF(memory, first + 4U) - 204.0F) < 1e-4F &&
              std::fabs(ReadF(memory, first + 3U * 12U) - 232.0F) < 1e-4F,
          "the confirmation control was not scaled to 60% around its center");
  Require(std::fabs(ReadF(memory, kPositions) - (160.0F - 140.0F * 1.12F)) < 1e-4F,
          "the confirmation heading was not widened");

  // The cursor: eight corner quads at translations around a center, with
  // points relative to them. It shrinks only while it frames a control.
  constexpr std::uint32_t cursor = 0x00737000U, cursorBoard = 0x00737100U;
  constexpr std::uint32_t cursorPoints = 0x00737200U, cursorTranslations = 0x00737600U;
  Write(memory, 0x00504FD4U, cursor);
  Write(memory, cursor + 8U, cursorBoard);
  Write(memory, cursor + 0x24U, 0U);
  Write(memory, cursorBoard, 8U);
  Write(memory, cursorBoard + 0x0CU, cursorPoints);
  Write(memory, cursorBoard + 0x1CU, cursorTranslations);
  const auto placeCursor = [&](float cx, float cy) {
    for (std::uint32_t quad = 0; quad < 8U; ++quad) {
      WriteF(memory, cursorTranslations + quad * 8U, cx + (quad % 2U == 0U ? -30.0F : 30.0F));
      WriteF(memory, cursorTranslations + quad * 8U + 4U, cy + (quad / 2U % 2U == 0U ? -10.0F : 10.0F));
      for (std::uint32_t corner = 0; corner < 4U; ++corner) {
        WriteF(memory, cursorPoints + quad * 48U + corner * 12U, corner % 2U == 0U ? 0.0F : 4.0F);
        WriteF(memory, cursorPoints + quad * 48U + corner * 12U + 4U, corner < 2U ? 0.0F : 4.0F);
      }
    }
  };
  const auto runConfirmation = [&] {
    for (std::uint32_t quad = 134U; quad < 141U; ++quad)
      for (std::uint32_t corner = 0; corner < 4U; ++corner) {
        WriteF(memory, kPositions + quad * 48U + corner * 12U, control[corner][0]);
        WriteF(memory, kPositions + quad * 48U + corner * 12U + 4U, control[corner][1]);
      }
    state = Caller(memory, kReframedFileBoardReturn);
    state.r[6] = owner;
    (void)Run(runtime, kReframedFileBoardReturn, state);
  };
  placeCursor(220.0F, 210.0F);
  runConfirmation();
  Require(ReadF(memory, cursorPoints + 12U) != 4.0F, "the cursor framing a control did not shrink with it");
  placeCursor(100.0F, 60.0F);
  runConfirmation();
  Require(ReadF(memory, cursorTranslations) == 70.0F && ReadF(memory, cursorPoints + 12U) == 4.0F,
          "the cursor around a file slot was shrunk");
}
} // namespace

int main() {
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"reframed-file-select-test", 0x004E0000U, 0x00320000U, true, false, {}},
                           &error),
          "cannot map the file selection fixture");
  TestInactiveLeavesNativeCalls(memory);
  TestFinishDrawsInterfaceTwice(memory);
  TestSceneKeepsAspect(memory);
  TestLogoSplitAndRestore(memory);
  TestCaptionCallbackAndBoards(memory);
  TestLeatherSkipped(memory);
  TestBoardGeometry(memory);
  std::cout << "oot3d_top_screen_reframed_file_select_tests: ok\n";
  return 0;
}
