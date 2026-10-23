#pragma once

#include "oot3d_native_a32_memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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

// PauseUi_Draw 0x0041EC50 shared projection and Items draw call sites.
inline constexpr std::uint32_t kTopScreenPageProjectionUploadCall = 0x0041EE1CU;
inline constexpr std::uint32_t kTopScreenItemsPageDrawCall = 0x0041EE74U;
// PauseItemsPage_Draw 0x00434C80 cursor subdraws (PauseQuadGroup_Draw).
inline constexpr std::uint32_t kTopScreenItemsCursorDrawCallA = 0x00434CDCU;
inline constexpr std::uint32_t kTopScreenItemsCursorDrawCallB = 0x00434D48U;

// TopScreen 2.1.1 widened Items page. While Items is active (word 0x0050672C;
// the payload's 0x005E21F4 is a constant 1) the compositor draws PauseUi_Draw
// in the full 400-wide viewport with a projection offset by 0.8 (0x0041EE1C ->
// 0x005DF428), the native X+40 of the centred page. Around the Items draw
// (0x0041EE74 -> 0x005D19F8) it moves the page's background columns by -40,
// its grid by -17 and its assignment icons to -17, then draws with the offset
// 0.6 (another 40 pixels) and restores 0.8. The cursor subdraws (0x005D1CBC,
// 0x005D1BE8) use 0.685 for a cursor over the grid. Only native OoT3D
// functions run; the payload's constant matrices are written to guest stack
// scratch for the uniform uploads.
class TopScreenWideItemsRuntime final {
public:
  explicit TopScreenWideItemsRuntime(NativeA32Memory &memory) : mMemory(memory) {}

  // 0x0041EE1C, the page projection upload: patches the projection and lets
  // the native upload run for Items, dungeon Map or System. `pageOnTop`:
  // this PauseUi_Draw is the upper-screen page.
  bool ExecutePageProjection(oot3d::recomp::a32::GuestState &state, bool pageOnTop);
  // 0x0041EE74, the Items page draw.
  bool ExecuteItemsDraw(oot3d::recomp::a32::GuestState &state,
                        oot3d::recomp::a32::ExecutionResult *result,
                        std::uint32_t *blocksConsumed);
  // 0x00434CDC and 0x00434D48.
  bool ExecuteCursorDraw(std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
                         oot3d::recomp::a32::ExecutionResult *result,
                         std::uint32_t *blocksConsumed);
  void Reset() noexcept;

  bool PageWide() const noexcept { return mPageWide; }
  std::uint64_t WidePages() const noexcept { return mWidePages; }
  std::uint64_t WideItemsDraws() const noexcept { return mWideItemsDraws; }
  std::uint64_t CursorDraws() const noexcept { return mCursorDraws; }

private:
  struct Call {
    std::uint32_t Entry = 0;
    std::uint32_t R0 = 0, R1 = 0, R2 = 0, R3 = 0;
  };
  // Native calls issued from one call site, each returning to it.
  struct Sequence {
    std::array<Call, 9> Calls{};
    std::size_t Count = 0;
    std::size_t Next = 0;
    std::uint32_t Site = 0;
    std::uint32_t Scratch = 0;
    oot3d::recomp::a32::GuestState Caller{};
    bool Pending() const noexcept { return Count != 0U; }
  };
  void Begin(Sequence &sequence, std::uint32_t site,
             const oot3d::recomp::a32::GuestState &state, std::uint32_t scratch);
  // Issues the next call, or ends the sequence: the caller resumes at the
  // site (`skipSite` false, the native call runs) or after it.
  bool Step(Sequence &sequence, oot3d::recomp::a32::GuestState &state,
            oot3d::recomp::a32::ExecutionResult *result,
            std::uint32_t *blocksConsumed, bool skipSite);
  std::uint32_t WriteProjection(std::uint32_t address, std::uint32_t offset);
  std::uint32_t AppendProjection(Sequence &sequence, std::size_t at,
                                 std::uint32_t projection, std::uint32_t identity);
  bool ResolveCursorProjection(std::uint32_t pc, std::uint32_t object,
                               bool *cursorProjection);

  NativeA32Memory &mMemory;
  bool mPageWide = false;
  bool mItemsWide = false;
  bool mItemsDrawing = false;
  Sequence mItems{};
  Sequence mCursor{};
  std::uint64_t mWidePages = 0;
  std::uint64_t mWideItemsDraws = 0;
  std::uint64_t mCursorDraws = 0;
};

// 0x005D1A10..0x005D1B84: the widened Items geometry edits on the page's
// selected model streams. Returns false when guest memory cannot be accessed.
bool ApplyTopScreenWideItemsGeometry(NativeA32Memory &memory);

} // namespace Oot3dNativeGame
