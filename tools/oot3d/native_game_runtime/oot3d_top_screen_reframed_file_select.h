#pragma once

#include "oot3d_native_a32_memory.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Oot3dNativeGame {

// The single-screen file selection of Ocarina Reframed 2.0.2 (OTPR26/OOT3DHud,
// GPL-3.0-or-later), reimplemented from its documented behavior: Link's
// pedestal moves to the right of the upper screen and the native lower-screen
// file interface is drawn again on its left, with Back in the corner.
// Reframed wraps calls of the upper view pass 0x00300328 (and two file-select
// owners) with a different glViewport or scissor. AOT blocks start only after
// calls, so each wrap runs at the block start before its call (executing the
// instructions that prepare the argument) or at the call's return. TopScreen's
// frontend target relocation stays off while it owns the file selection.
inline constexpr std::uint32_t kReframedFileSceneBlock = 0x00300400U;     // before 0x004224AC
inline constexpr std::uint32_t kReframedFileAfterSceneReturn = 0x0030046CU; // after 0x002FAD1C
inline constexpr std::uint32_t kReframedFileBoardsBlock = 0x003004C8U;    // before pass stage 3
inline constexpr std::uint32_t kReframedFilePresentationBlock = 0x003004D4U; // before 0x0042B9F4
inline constexpr std::uint32_t kReframedFileTextBlock = 0x00300508U;      // blxne [+0x7440]
inline constexpr std::uint32_t kReframedFileLogoCall = 0x00300520U;       // pass stage 4
inline constexpr std::uint32_t kReframedFileFinishCall = 0x0030055CU;     // pass stage 5
inline constexpr std::uint32_t kReframedFileLeatherBlock = 0x0041EE38U;   // board draw at 0x0041EE50
inline constexpr std::uint32_t kReframedFileBoardReturn = 0x00433810U;    // after 0x002F9A1C

class TopScreenReframedFileSelect {
public:
  explicit TopScreenReframedFileSelect(NativeA32Memory &memory) : mMemory(memory) {}

  static std::span<const std::uint32_t> HookPcs() noexcept;
  // Reframed's gate: SaveContext.game_mode 2 with the native file board
  // initialized (its state word is zero before and after the menu).
  static bool Active(NativeA32Memory &memory) noexcept;

  // Runs at a hook site; false leaves the native instructions.
  bool Execute(std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
               oot3d::recomp::a32::ExecutionResult *result,
               std::uint32_t *blocksConsumed);
  bool Pending() const noexcept { return mSite != 0U; }
  void Reset() noexcept;

private:
  struct Step {
    enum class Kind : std::uint8_t { Call, Scissor, LogoSplit, LogoSwap, LogoRestore };
    Kind Type = Kind::Call;
    std::uint32_t Entry = 0U;
    std::array<std::uint32_t, 5> Words{};
  };

  std::uint32_t Read32(std::uint32_t address) const;
  float ReadF(std::uint32_t address) const;
  void Write32(std::uint32_t address, std::uint32_t value);
  void WriteF(std::uint32_t address, float value);
  bool UpperView(const oot3d::recomp::a32::GuestState &state) const;
  void Viewport(std::uint32_t x, std::int32_t y, std::uint32_t width, std::int32_t height);
  void Call(std::uint32_t entry, std::array<std::uint32_t, 4> arguments = {});
  void Scissor(std::uint32_t mode, std::uint32_t a, std::uint32_t b, std::uint32_t c,
               std::uint32_t d);
  bool Begin(std::uint32_t pc, oot3d::recomp::a32::GuestState &state);
  bool Advance(oot3d::recomp::a32::GuestState &state,
               oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);
  void ApplyScissor(const Step &step);
  bool LogoPair(std::uint32_t pass) const;
  // Reframed's file board pass after the native 0x002F9A1C regeneration.
  void AdjustBoard(std::uint32_t board);

  NativeA32Memory &mMemory;
  // The site the guest calls return to, and where the native code resumes
  // once the steps are done (zero: the site itself, natively).
  std::uint32_t mSite = 0U;
  std::uint32_t mContinue = 0U;
  std::vector<Step> mSteps;
  std::size_t mNext = 0U;
  std::uint32_t mLogo = 0U;
  // The caption callback's context and whether its caption is shown
  // (context +0x890 == 2), from the latest upper pass.
  std::uint32_t mTextContext = 0U;
  bool mGuidanceVisible = true;
  // Each file name's y on the action page, held while its copy/erase
  // confirmation (states 35..42) slides the regenerated name away.
  std::array<float, 3> mConfirmationNameY{};
};

} // namespace Oot3dNativeGame
