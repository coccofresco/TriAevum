#include "oot3d_top_screen_reframed_file_select.h"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kGlViewport = 0x002FEABCU;
constexpr std::uint32_t kNativeGameMode = 0x00588E3CU;
constexpr std::uint32_t kFileBoardState = 0x00504FB0U;
constexpr std::uint32_t kFileFocusedAction = 0x00504FB8U;
constexpr std::uint32_t kFileCursor = 0x00504FD4U;
// Text owners of the file board: three slot names, then the two headings.
constexpr std::uint32_t kFileTextOwners = 0x0055B548U;
constexpr std::uint32_t kFileTimestampOwners = 0x0055B554U;
constexpr std::uint32_t kPicaCommandCursor = 0x0054CC4CU;
constexpr std::uint32_t kPicaCommandEnd = 0x0054CC50U;
constexpr std::uint32_t kBoardVtable = 0x004EBD60U;
// 0x00300328 runs twice per frame; the upper view returns here.
constexpr std::uint32_t kUpperViewReturn = 0x00419524U;
constexpr std::uint32_t kViewReturnSlot = 0x6CU;

constexpr std::uint32_t kSceneDraw = 0x004224ACU;
constexpr std::uint32_t kPassStage3 = 0x004228DCU;
constexpr std::uint32_t kPresentationDraw = 0x0042B9F4U;
constexpr std::uint32_t kPassStage4 = 0x004228E4U;
constexpr std::uint32_t kPassStage5 = 0x004228D4U;
constexpr std::uint32_t kPauseUiDraw = 0x0041EC50U;
constexpr std::uint32_t kPauseOverlayDraw = 0x0041F308U;
// Drawn natively by Reframed (inside its late-overlay viewport) where the
// SingleScreen port skips them in scene mode 5.
constexpr std::uint32_t kMessageBackdropCall = 0x0030052CU;
constexpr std::uint32_t kMessageBackdropDraw = 0x0042CB54U;
constexpr std::uint32_t kMessageDrawCall = 0x00300550U;
constexpr std::uint32_t kMessageDraw = 0x0042CBCCU;

constexpr std::array kHookPcs{
    kReframedFileSceneBlock,  kReframedFileAfterSceneReturn, kReframedFileBoardsBlock,
    kReframedFilePresentationBlock, kReframedFileTextBlock,  kReframedFileLogoCall,
    kMessageBackdropCall,     kMessageDrawCall,              kReframedFileFinishCall,
    kReframedFileLeatherBlock, kReframedFileBoardReturn};

} // namespace

std::span<const std::uint32_t> TopScreenReframedFileSelect::HookPcs() noexcept {
  return kHookPcs;
}

bool TopScreenReframedFileSelect::Active(NativeA32Memory &memory) noexcept {
  std::uint32_t mode = 0U, board = 0U;
  return memory.Read32(kNativeGameMode, &mode) && mode == 2U &&
         memory.Read32(kFileBoardState, &board) && board != 0U;
}

void TopScreenReframedFileSelect::Reset() noexcept {
  mSite = 0U;
  mContinue = 0U;
  mSteps.clear();
  mNext = 0U;
  mLogo = 0U;
  mTextContext = 0U;
  mGuidanceVisible = true;
  mConfirmationNameY = {};
}

std::uint32_t TopScreenReframedFileSelect::Read32(std::uint32_t address) const {
  std::uint32_t value = 0U;
  if (!mMemory.Read32(address, &value))
    throw std::runtime_error("Reframed file selection cannot read native state");
  return value;
}

float TopScreenReframedFileSelect::ReadF(std::uint32_t address) const {
  return std::bit_cast<float>(Read32(address));
}

void TopScreenReframedFileSelect::Write32(std::uint32_t address, std::uint32_t value) {
  if (!mMemory.WriteHost<std::uint32_t>(address, value))
    throw std::runtime_error("Reframed file selection cannot write native state");
}

void TopScreenReframedFileSelect::WriteF(std::uint32_t address, float value) {
  Write32(address, std::bit_cast<std::uint32_t>(value));
}

bool TopScreenReframedFileSelect::UpperView(const oot3d::recomp::a32::GuestState &state) const {
  std::uint32_t view = 0U;
  return mMemory.Read32(state.r[13] + kViewReturnSlot, &view) && view == kUpperViewReturn;
}

void TopScreenReframedFileSelect::Call(std::uint32_t entry,
                                       std::array<std::uint32_t, 4> arguments) {
  Step step;
  step.Entry = entry;
  std::copy(arguments.begin(), arguments.end(), step.Words.begin());
  mSteps.push_back(step);
}

// OoT3D's presentation surface is rotated: x and width are vertical.
void TopScreenReframedFileSelect::Viewport(std::uint32_t x, std::int32_t y,
                                           std::uint32_t width, std::int32_t height) {
  Call(kGlViewport, {x, static_cast<std::uint32_t>(y), width,
                     static_cast<std::uint32_t>(height)});
}

void TopScreenReframedFileSelect::Scissor(std::uint32_t mode, std::uint32_t a,
                                          std::uint32_t b, std::uint32_t c,
                                          std::uint32_t d) {
  Step step;
  step.Type = Step::Kind::Scissor;
  step.Words = {mode, a, b, c, d};
  mSteps.push_back(step);
}

// Six command words (GPUREG_SCISSORTEST_MODE, POS1, POS2) at the native
// command cursor, as Reframed writes them between the replayed draws.
void TopScreenReframedFileSelect::ApplyScissor(const Step &step) {
  const auto cursor = Read32(kPicaCommandCursor);
  if (cursor + 6U * 4U > Read32(kPicaCommandEnd)) return;
  const std::array<std::uint32_t, 6> words{
      step.Words[0], 0x000F0065U, (step.Words[2] << 16U) | step.Words[1],
      0x000F0066U,   (step.Words[4] << 16U) | step.Words[3], 0x000F0067U};
  for (std::size_t index = 0; index < words.size(); ++index)
    Write32(cursor + static_cast<std::uint32_t>(index) * 4U, words[index]);
  Write32(kPicaCommandCursor, cursor + 6U * 4U);
}

// Pass stage 4 holds the title logo followed by the full-screen fade: two
// board nodes with adjacent resources, the fade centered at (200,120).
bool TopScreenReframedFileSelect::LogoPair(std::uint32_t pass) const {
  if (Read32(pass + 0x24U) != 2U || Read32(pass + 0x764U) != 0U) return false;
  const auto logo = Read32(pass + 0x394U), fade = Read32(pass + 0x398U);
  return logo != 0U && fade != 0U && Read32(logo) == kBoardVtable &&
         Read32(fade) == kBoardVtable && Read32(logo + 4U) == Read32(fade + 4U) + 0x1C8U &&
         ReadF(fade + 15U * 4U) == 200.0F && ReadF(fade + 16U * 4U) == 120.0F;
}

bool TopScreenReframedFileSelect::Begin(std::uint32_t pc,
                                        oot3d::recomp::a32::GuestState &state) {
  mSteps.clear();
  mNext = 0U;
  mContinue = pc + 4U;
  if (pc == kReframedFileAfterSceneReturn) {
    // After 0x002FAD1C: Reframed restores the full viewport for the whole
    // file-select mode, then the pass continues natively here.
    if (Read32(kNativeGameMode) != 2U || !UpperView(state)) return false;
    Viewport(0U, 0, 480U, 400);
    mContinue = 0U;
  } else {
    if (!Active(mMemory) || !UpperView(state)) return false;
    switch (pc) {
    case kReframedFileSceneBlock:
      // add r0, r4, #0x180; bl 0x004224AC. Link's pedestal: aligned with the
      // bottom file slot, on the right. Reframed's (119,-8,300,200) keeps
      // 300/480 of the height but 200/400 of the width, so Link came out 80%
      // as wide. The same fraction (5/8) on both axes keeps the scene's own
      // projection undistorted at any output aspect ratio; the horizontal
      // center (92) is Reframed's.
      state.r[0] = state.r[4] + 0x180U;
      Viewport(119U, 92 - 125, 300U, 250);
      Call(kSceneDraw, {state.r[0]});
      mContinue = 0x00300408U;
      break;
    case kReframedFileBoardsBlock:
      // ldr r0, [sp, #0x40]; bl 0x004228DC. The caption board below Link;
      // its translucency is baked into the native texture, so it is
      // composited four times when shown.
      state.r[0] = Read32(state.r[13] + 0x40U);
      Viewport(56U, 2, 264U, 192);
      for (int layer = 0; layer < (mGuidanceVisible ? 4 : 1); ++layer)
        Call(kPassStage3, {state.r[0]});
      mContinue = kReframedFilePresentationBlock;
      break;
    case kReframedFilePresentationBlock:
      // mov r0, sb; bl 0x0042B9F4.
      state.r[0] = state.r[9];
      Viewport(290U, 114, 324U, 324);
      Call(kPresentationDraw, {state.r[0], state.r[1], state.r[2], state.r[3]});
      mContinue = 0x003004E0U;
      break;
    case kReframedFileTextBlock: {
      // add r6, r4, #0x7000; ldr r1, [r6, #0x440]; cmp r1, #0;
      // movne r0, sb; blxne r1: the caption glyph callback, if installed.
      const auto callback = Read32(state.r[4] + 0x7440U);
      if (callback == 0U) return false;
      state.r[6] = state.r[4] + 0x7000U;
      state.r[1] = callback;
      state.r[0] = state.r[9];
      state.cpsr &= ~(oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
                      oot3d::recomp::a32::kFlagV);
      state.cpsr |= oot3d::recomp::a32::kFlagC;
      mTextContext = state.r[9];
      mGuidanceVisible = Read32(mTextContext + 0x890U) == 2U;
      Viewport(56U, 2, 264U, 192);
      Call(callback, {mTextContext});
      mContinue = 0x0030051CU;
      break;
    }
    case kReframedFileLogoCall: {
      const auto argument = state.r[0];
      Viewport(mGuidanceVisible ? 290U : 392U, 114, 324U, 324);
      if (LogoPair(argument)) {
        Step split;
        split.Type = Step::Kind::LogoSplit;
        split.Words[0] = argument;
        mSteps.push_back(split);
        Call(kPassStage4, {argument});
        Viewport(0U, 0, 480U, 400);
        split.Type = Step::Kind::LogoSwap;
        mSteps.push_back(split);
        Call(kPassStage4, {argument});
        split.Type = Step::Kind::LogoRestore;
        mSteps.push_back(split);
      } else {
        Call(kPassStage4, {argument});
      }
      // The late file-selection overlay that follows the logo.
      Viewport(48U, 0, 384U, 180);
      break;
    }
    case kMessageBackdropCall:
      Call(kMessageBackdropDraw, {state.r[0]});
      break;
    case kMessageDrawCall:
      Call(kMessageDraw, {state.r[0]});
      break;
    case kReframedFileFinishCall: {
      const auto argument = state.r[0];
      // The native lower-screen file interface again, on the upper image:
      // 56% x 70% on the left without Back's corner. Reframed then draws that
      // corner alone against the screen's bottom-left edge; TopScreen has no
      // Back there, so that pass is not replayed.
      const auto callback = Read32(state.r[4] + 0x7444U);
      const auto context = state.r[9];
      const auto drawInterface = [&] {
        Call(kPauseUiDraw);
        Call(kPauseOverlayDraw);
        if (callback != 0U) Call(callback, {context});
      };
      Call(kPassStage5, {argument});
      Viewport(56U, 164, 336U, 224);
      // Reframed excludes Back's corner (54..109 x 346..399) with an exclude
      // scissor, which the host renderer does not emulate. Two disjoint
      // include scissors cover the rest of the interface viewport instead.
      Scissor(3U, 0U, 0U, 479U, 345U);
      drawInterface();
      Scissor(3U, 110U, 346U, 479U, 399U);
      drawInterface();
      Scissor(0U, 0U, 0U, 479U, 399U);
      Viewport(0U, 0, 480U, 400);
      break;
    }
    default:
      return false;
    }
  }
  mSite = pc;
  return true;
}

bool TopScreenReframedFileSelect::Advance(oot3d::recomp::a32::GuestState &state,
                                          oot3d::recomp::a32::ExecutionResult *result,
                                          std::uint32_t *blocksConsumed) {
  const auto branch = [&](std::uint32_t target) {
    state.r[15] = target;
    *result = {oot3d::recomp::a32::ExitKind::Branch, target,
               oot3d::recomp::a32::FallbackReason::None, mSite};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  };
  while (mNext < mSteps.size()) {
    const auto step = mSteps[mNext++];
    switch (step.Type) {
    case Step::Kind::Call: {
      for (std::size_t index = 0; index < 4U; ++index) state.r[index] = step.Words[index];
      // The last call returns straight to a continuation elsewhere.
      const bool tail = mNext == mSteps.size() && mContinue != 0U;
      state.r[14] = tail ? mContinue : mSite;
      const auto entry = step.Entry;
      if (tail) {
        mSite = 0U;
        mSteps.clear();
      }
      return branch(entry);
    }
    case Step::Kind::Scissor:
      ApplyScissor(step);
      break;
    case Step::Kind::LogoSplit:
      mLogo = Read32(step.Words[0] + 0x394U);
      Write32(step.Words[0] + 0x24U, 1U);
      break;
    case Step::Kind::LogoSwap:
      Write32(step.Words[0] + 0x394U, Read32(step.Words[0] + 0x398U));
      break;
    case Step::Kind::LogoRestore:
      Write32(step.Words[0] + 0x394U, mLogo);
      Write32(step.Words[0] + 0x24U, 2U);
      break;
    }
  }
  const auto next = mContinue;
  mSite = 0U;
  mSteps.clear();
  // Zero: the steps returned to the site, which now runs natively.
  if (next == 0U) return false;
  state.r[14] = next;
  return branch(next);
}

bool TopScreenReframedFileSelect::Execute(std::uint32_t pc,
                                          oot3d::recomp::a32::GuestState &state,
                                          oot3d::recomp::a32::ExecutionResult *result,
                                          std::uint32_t *blocksConsumed) {
  if (pc == kReframedFileLeatherBlock) {
    // ldr r0, [r4, #0x20]; cmp r0, #0; bne 0x0041EE54; ... blx (0x0041EE50):
    // the leather base board under the file slots is not drawn (the slots,
    // the Items shelf and Gear are separate boards).
    if (!Active(mMemory)) return false;
    state.r[0] = Read32(state.r[4] + 0x20U);
    if (state.r[0] != 0U) return false;
    state.r[15] = 0x0041EE54U;
    *result = {oot3d::recomp::a32::ExitKind::Branch, 0x0041EE54U,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  }
  if (pc == kReframedFileBoardReturn) {
    // Back from the native 0x002F9A1C regeneration of the board at r6+0x30.
    if (Active(mMemory)) AdjustBoard(Read32(state.r[6] + 0x30U));
    return false;
  }
  if (Pending()) {
    if (pc != mSite) return false;
    return Advance(state, result, blocksConsumed);
  }
  if (!Begin(pc, state)) return false;
  return Advance(state, result, blocksConsumed);
}

void TopScreenReframedFileSelect::AdjustBoard(std::uint32_t board) {
  if (board == 0U || Read32(board) != 184U) return;
  const auto positions = Read32(board + 0x10U);
  if (positions == 0U) return;
  const auto vertex = [&](std::uint32_t quad, std::uint32_t corner) {
    return positions + (quad * 12U + corner * 3U) * 4U;
  };
  const auto scaleQuad = [&](std::uint32_t quad, float cx, float cy, float sx, float sy) {
    for (std::uint32_t corner = 0; corner < 4U; ++corner) {
      const auto at = vertex(quad, corner);
      WriteF(at, cx + (ReadF(at) - cx) * sx);
      WriteF(at + 4U, cy + (ReadF(at + 4U) - cy) * sy);
    }
  };
  const auto fileState = Read32(kFileBoardState);
  const bool fileActions = fileState >= 7U && fileState <= 10U;
  // State 4/5 start the occupied/empty file exit; 7..10 is the Start/Copy/
  // Erase page. Hide the prompt and its board from the first tick.
  const bool hideHeading = fileState == 4U || fileState == 5U || fileActions;
  const auto setNodesVisible = [&](std::uint32_t owner, float alpha) {
    if (owner == 0U || Read32(owner + 0x40U) != 0U) return;
    for (std::uint32_t node = 0; node < 2U; ++node)
      if (const auto address = Read32(owner + 0x10U + node * 4U); address != 0U)
        WriteF(address + 0xFCU, alpha);
  };
  setNodesVisible(Read32(kFileTextOwners + 6U * 4U), hideHeading ? 0.0F : 1.0F);
  if (fileActions) {
    // Keep the caption on the action page; drop the redundant File n heading.
    if (mTextContext != 0U) Write32(mTextContext + 0x89CU, 1U);
    setNodesVisible(Read32(kFileTextOwners + 7U * 4U), 0.0F);
  }
  // Start 124x52 at 60%; Copy and Erase (84x36) at 90% of that.
  constexpr std::array actionX{160.0F, 112.0F, 208.0F};
  constexpr std::array actionY{132.0F, 180.0F, 180.0F};
  constexpr std::array actionScaleX{0.6F, 66.96F / 84.0F, 66.96F / 84.0F};
  constexpr std::array actionScaleY{0.6F, 28.08F / 36.0F, 28.08F / 36.0F};
  if (const auto offsets = Read32(board + 0x1CU); offsets != 0U) {
    for (std::uint32_t action = 0; action < 3U; ++action) {
      const auto first = 122U + action * 4U;
      const float cx = actionX[action] + ReadF(offsets + first * 8U);
      const float cy = actionY[action] + ReadF(offsets + first * 8U + 4U);
      for (auto quad = first; quad < first + 4U; ++quad)
        scaleQuad(quad, cx, cy, actionScaleX[action], actionScaleY[action]);
      // The pressed-state frame (175..183) is its own three-slice layer.
      for (auto quad = 175U + action * 3U; quad < 178U + action * 3U; ++quad)
        scaleQuad(quad, actionX[action] + ReadF(offsets + quad * 8U),
                  actionY[action] + ReadF(offsets + quad * 8U + 4U), actionScaleX[action],
                  actionScaleY[action]);
    }
  }
  const auto focused = Read32(kFileFocusedAction);
  if ((fileState == 8U || fileState == 10U) && focused < 3U) {
    // Cursor mode 0: four corners and their shadows around the action.
    const auto cursor = Read32(kFileCursor);
    const auto cursorBoard = cursor != 0U ? Read32(cursor + 8U) : 0U;
    if (cursorBoard != 0U && Read32(cursor + 0x24U) == 0U && Read32(cursorBoard) == 8U) {
      const auto points = Read32(cursorBoard + 0x0CU), translations = Read32(cursorBoard + 0x1CU);
      if (points != 0U && translations != 0U) {
        const float cx = actionX[focused], cy = actionY[focused];
        const float sx = actionScaleX[focused], sy = actionScaleY[focused];
        for (std::uint32_t quad = 0; quad < 8U; ++quad) {
          const auto t = translations + quad * 8U;
          WriteF(t, cx + (ReadF(t) - cx) * sx);
          WriteF(t + 4U, cy + (ReadF(t + 4U) - cy) * sy);
          for (std::uint32_t corner = 0; corner < 4U; ++corner) {
            const auto p = points + (quad * 12U + corner * 3U) * 4U;
            WriteF(p, ReadF(p) * sx);
            WriteF(p + 4U, ReadF(p + 4U) * sy);
          }
        }
      }
    }
  }
  // Copy/erase and their confirmations (states 11..42): each control is a
  // group of quads (points relative to per-quad translations), scaled to
  // 60% around the group's own center.
  const auto scaleGroup = [&](std::uint32_t points, std::uint32_t translations,
                              std::uint32_t first, std::uint32_t count) {
    float left = 1000.0F, right = -1000.0F, top = 1000.0F, bottom = -1000.0F;
    for (auto quad = first; quad < first + count; ++quad)
      for (std::uint32_t corner = 0; corner < 4U; ++corner) {
        const auto p = points + (quad * 12U + corner * 3U) * 4U;
        const float x = ReadF(p) - ReadF(translations + quad * 8U);
        const float y = ReadF(p + 4U) - ReadF(translations + quad * 8U + 4U);
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    const float cx = (left + right) * 0.5F, cy = (top + bottom) * 0.5F;
    for (auto quad = first; quad < first + count; ++quad) {
      const float tx = ReadF(translations + quad * 8U), ty = ReadF(translations + quad * 8U + 4U);
      for (std::uint32_t corner = 0; corner < 4U; ++corner) {
        const auto p = points + (quad * 12U + corner * 3U) * 4U;
        WriteF(p, tx + cx + (ReadF(p) - tx - cx) * 0.6F);
        WriteF(p + 4U, ty + cy + (ReadF(p + 4U) - ty - cy) * 0.6F);
      }
    }
  };
  if (const auto offsets = Read32(board + 0x1CU); fileState >= 11U && fileState <= 42U && offsets != 0U) {
    // Each control's screen bounds before scaling, to tell whether the
    // cursor frames one of them or a file slot.
    std::array<std::array<float, 4>, 4> bounds{};
    for (std::uint32_t control = 0; control < 4U; ++control) {
      auto &box = bounds[control];
      box = {1000.0F, -1000.0F, 1000.0F, -1000.0F};
      for (auto quad = 134U + control * 7U; quad < 141U + control * 7U; ++quad)
        for (std::uint32_t corner = 0; corner < 4U; ++corner) {
          const float x = ReadF(vertex(quad, corner)), y = ReadF(vertex(quad, corner) + 4U);
          box = {std::min(box[0], x), std::max(box[1], x), std::min(box[2], y), std::max(box[3], y)};
        }
      scaleGroup(positions, offsets, 134U + control * 7U, 7U);
    }
    // Reframed shrinks the cursor in every one of these states; it shrinks
    // with its control only, not around a copy target's file slot.
    const auto cursor = Read32(kFileCursor);
    const auto cursorBoard = cursor != 0U ? Read32(cursor + 8U) : 0U;
    if (cursorBoard != 0U && Read32(cursor + 0x24U) == 0U && Read32(cursorBoard) == 8U) {
      const auto points = Read32(cursorBoard + 0x0CU), translations = Read32(cursorBoard + 0x1CU);
      if (points != 0U && translations != 0U) {
        // Its quads share one origin (translation) with local corner points:
        // the frame's center is the middle of their screen extent.
        float left = 1000.0F, right = -1000.0F, top = 1000.0F, bottom = -1000.0F;
        for (std::uint32_t quad = 0; quad < 8U; ++quad)
          for (std::uint32_t corner = 0; corner < 4U; ++corner) {
            const auto p = points + (quad * 12U + corner * 3U) * 4U;
            const float px = ReadF(translations + quad * 8U) + ReadF(p);
            const float py = ReadF(translations + quad * 8U + 4U) + ReadF(p + 4U);
            left = std::min(left, px);
            right = std::max(right, px);
            top = std::min(top, py);
            bottom = std::max(bottom, py);
          }
        const float x = (left + right) * 0.5F, y = (top + bottom) * 0.5F;
        const bool onControl = std::any_of(bounds.begin(), bounds.end(), [&](const auto &box) {
          return x >= box[0] && x <= box[1] && y >= box[2] && y <= box[3];
        });
        if (onControl) scaleGroup(points, translations, 0U, 8U);
      }
    }
  }
  for (std::uint32_t slot = 0; slot < 3U; ++slot) {
    // File names: eight outline nodes and the foreground, scaled together
    // from the left edge, centered in the row.
    const auto text = Read32(kFileTextOwners + slot * 4U);
    if (text != 0U && Read32(text + 0x40U) == 2U) {
      if (const auto foreground = Read32(text + 0x30U); foreground != 0U) {
        // The name stays in its action-page row, visible, through the
        // copy/erase confirmation.
        if ((fileState == 8U || fileState == 10U) && ReadF(foreground + 0x3CU) > 0.0F)
          mConfirmationNameY[slot] = ReadF(foreground + 0x40U);
        const bool holdName = fileState >= 35U && fileState <= 42U &&
                              mConfirmationNameY[slot] != 0.0F && ReadF(foreground + 0x3CU) > 0.0F;
        const float shiftY = holdName ? mConfirmationNameY[slot] - ReadF(foreground + 0x40U) : 0.0F;
        const float left = ReadF(foreground + 0x3CU);
        const float centerY = ReadF(foreground + 0x40U) + 8.0F;
        for (std::uint32_t node = 0; node < 9U; ++node) {
          const auto address = Read32(text + 0x10U + node * 4U);
          if (address == 0U) continue;
          WriteF(address + 0x3CU, left + (ReadF(address + 0x3CU) - left) * 0.8F);
          WriteF(address + 0x40U, centerY + (ReadF(address + 0x40U) - centerY) * 0.8F + shiftY);
          WriteF(address + 0x48U, 0.8F);
          WriteF(address + 0x4CU, 0.8F);
          if (holdName) WriteF(address + 0xFCU, 1.0F);
        }
      }
    }
    // The numbered badge is quad 10 of each 37-quad slot.
    const auto badge = 10U + slot * 37U;
    const float centerX = (ReadF(vertex(badge, 0U)) + ReadF(vertex(badge, 3U))) * 0.5F;
    const float centerY = (ReadF(vertex(badge, 0U) + 4U) + ReadF(vertex(badge, 3U) + 4U)) * 0.5F;
    scaleQuad(badge, centerX, centerY, 0.8F, 0.8F);
    // The timestamp (one kind-3 node): its translation is regenerated each
    // tick, its scale is set absolutely.
    if (const auto stamp = Read32(kFileTimestampOwners + slot * 4U);
        stamp != 0U && Read32(stamp + 0x40U) == 3U) {
      if (const auto node = Read32(stamp + 0x10U); node != 0U) {
        WriteF(node + 0x3CU, ReadF(node + 0x3CU) + 11.0F);
        WriteF(node + 0x40U, ReadF(node + 0x40U) - 4.0F);
        WriteF(node + 0x48U, 0.8F);
        WriteF(node + 0x4CU, 0.8F);
      }
    }
    // Twenty heart quads from quad 11 of the slot.
    const auto firstHeart = 11U + slot * 37U;
    const float heartsX = ReadF(vertex(firstHeart, 0U)) + 55.0F;
    const float heartsTop = ReadF(vertex(firstHeart, 0U) + 4U);
    for (std::uint32_t heart = 0; heart < 20U; ++heart)
      scaleQuad(firstHeart + heart, heartsX, heartsTop, 0.8F, 0.8F);
  }
  // The heading's six slices (native x 12..308, y 4..44); its glyphs are a
  // separate draw and keep their size. Copy/erase prompts are longer, and
  // their confirmations longer still.
  const float headingWidth = fileState >= 35U && fileState <= 42U ? 1.12F
                             : fileState >= 11U                   ? 1.0F
                                                                  : 0.60F;
  for (std::uint32_t quad = 0; quad < 6U; ++quad) {
    scaleQuad(quad, 160.0F, 24.0F, headingWidth, 0.70F);
    if (hideHeading)
      for (std::uint32_t corner = 0; corner < 4U; ++corner) {
        WriteF(vertex(quad, corner), -1000.0F);
        WriteF(vertex(quad, corner) + 4U, -1000.0F);
      }
  }
  // The heading's middle stretches atlas column 286: end both rounded caps
  // at that texel's center so no transparent neighbor filters in.
  const auto uv = Read32(board + 0x14U);
  const float atlasWidth = ReadF(board + 4U);
  if (uv != 0U && atlasWidth > 0.0F) {
    const float join = 286.5F / atlasWidth;
    for (std::uint32_t row = 0; row < 2U; ++row) {
      const auto left = uv + row * 24U * 4U, middle = left + 8U * 4U, right = left + 16U * 4U;
      for (const auto index : {2U, 6U}) WriteF(left + index * 4U, join);
      for (const auto index : {0U, 2U, 4U, 6U}) WriteF(middle + index * 4U, join);
      for (const auto index : {0U, 4U}) WriteF(right + index * 4U, join);
    }
  }
}

} // namespace Oot3dNativeGame
