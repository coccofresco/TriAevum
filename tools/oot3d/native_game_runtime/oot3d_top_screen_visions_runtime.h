#pragma once

#include "oot3d_native_a32_memory.h"

#include <array>
#include <cstdint>
#include <utility>

namespace Oot3dNativeGame {

// SingleScreen 3.1 Sheikah Stone Visions (scene mode 7). Once, 3.1 0x005D7438
// replaces submodes 0..2 of the hint movie's two native callback tables: the
// upper one at 0x00534C80 (dispatched by 0x00471F60 inside the upper view
// pass) with 0x005D5698, the lower one at 0x00534C94 (0x00477E30, lower pass)
// with 0x005D5C18. The location and vision lists are thereby drawn into the
// upper image, and L/R page them.
inline constexpr std::uint32_t kTopScreenVisionsUpperDispatch = 0x00471F60U;
inline constexpr std::uint32_t kTopScreenVisionsLowerDispatch = 0x00477E30U;

class TopScreenVisionsRuntime {
public:
  explicit TopScreenVisionsRuntime(NativeA32Memory &memory) : mMemory(memory) {}

  // Runs at either dispatch entry; false leaves the native table call.
  bool Execute(std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
               oot3d::recomp::a32::ExecutionResult *result,
               std::uint32_t *blocksConsumed);
  // 3.1 0x005D7438 keeps the argument of the queue-6 draw at 0x0041988C;
  // 0x005D5698 draws that queue on top with the previous one.
  void ObserveControllerDraw(std::uint32_t argument) noexcept {
    mControllerArgument = argument;
  }
  // True once after an upper pass that drew the list (its edges follow it).
  bool TakeUpperDrawn() noexcept { return std::exchange(mUpperDrawn, false); }
  void Reset() noexcept;

private:
  std::uint32_t Read32(std::uint32_t address) const;
  void Write32(std::uint32_t address, std::uint32_t value);
  bool Call(std::uint32_t site, std::uint32_t entry,
            const std::array<std::uint32_t, 4> &arguments,
            oot3d::recomp::a32::GuestState &state,
            oot3d::recomp::a32::ExecutionResult *result,
            std::uint32_t *blocksConsumed);
  bool ExecuteUpper(oot3d::recomp::a32::GuestState &state,
                    oot3d::recomp::a32::ExecutionResult *result,
                    std::uint32_t *blocksConsumed);
  bool ExecuteLower(oot3d::recomp::a32::GuestState &state,
                    oot3d::recomp::a32::ExecutionResult *result,
                    std::uint32_t *blocksConsumed);
  // 3.1 0x005D5C34..0x005D5D2C: L/R move the list by three entries.
  std::uint32_t PageList();

  NativeA32Memory &mMemory;
  oot3d::recomp::a32::GuestState mCaller{};
  std::uint8_t mStep = 0U;
  std::uint32_t mSubmode = 0U;
  std::array<std::uint32_t, 6> mPauseWords{};
  std::uint32_t mControllerArgument = 0U;
  std::uint32_t mPreviousButtons = 0U;
  bool mUpperDrawn = false;
};

} // namespace Oot3dNativeGame
