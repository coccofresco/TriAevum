#include "oot3d_top_screen_wide_items_runtime.h"
#include "oot3d_native_trigonometry.h"
#include "oot3d_top_screen_mod_profile.h"
#include "oot3d_top_screen_items_layout.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

constexpr std::uint32_t kItemsState = 0x0050672CU;
constexpr std::uint32_t kMapState = 0x00506CE8U;
constexpr std::uint32_t kMapMode = 0x004FDABCU;
constexpr std::uint32_t kSystemState = 0x0050A530U;
constexpr std::uint32_t kStackTop = 0x005FF000U;
// Gear page object: its +4 word is the model the block's BLX draws.
constexpr std::uint32_t kGearOwner = 0x005FE800U;
constexpr std::uint32_t kHeap = 0x00700000U;
constexpr std::uint32_t kMatrix = 0x005FE800U;
constexpr std::uint32_t kModelDraw = 0x00450000U;
constexpr std::uint32_t kQuadGroupDraw = 0x002FB934U;
constexpr std::uint32_t kUploadUniformBlock0 = 0x003246ECU;
constexpr std::uint32_t kPageModel = kHeap;
constexpr std::uint32_t kPageInstance = kHeap + 0x200U;
constexpr std::uint32_t kPagePositions = kHeap + 0x1000U;
constexpr std::uint32_t kPageIndices = kHeap + 0x2000U;
constexpr std::uint32_t kPageTexcoords = kHeap + 0x1800U;
constexpr std::uint32_t kPageBuffer = kHeap + 0x2800U;
constexpr std::uint32_t kBasePositions = kHeap + 0x16000U;
constexpr std::uint32_t kBaseTexcoords = kHeap + 0x16800U;
constexpr std::uint32_t kGearModel = kHeap + 0x13000U;
constexpr std::uint32_t kGearPositions = kHeap + 0x14000U;
constexpr std::uint32_t kGearIndices = kHeap + 0x15000U;
constexpr std::uint32_t kGroup = kHeap + 0x4000U;
constexpr std::uint32_t kGroupModel = kHeap + 0x5000U;
constexpr std::uint32_t kGroupInstance = kHeap + 0x5300U;
constexpr std::uint32_t kGroupPositions = kHeap + 0x6000U;
constexpr std::uint32_t kGroupIndices = kHeap + 0x6800U;
constexpr std::uint32_t kFirstSlot = kHeap + 0x8000U;
constexpr std::uint32_t kIcon = kHeap + 0xB000U;
constexpr std::uint32_t kRightIcon = kHeap + 0xD000U;
constexpr std::uint32_t kCursor = kHeap + 0xF000U;
constexpr std::uint32_t kFrameOwner = kHeap + 0x11000U;
constexpr std::array<std::uint32_t, 6> kLanes{0xCU, 0x10U, 0x18U, 0x1CU, 0x20U, 0x28U};
constexpr Oot3dNativeGame::TopScreenItemsTransform kPageTransform{2.0F, 0.25F, 10.0F, -0.5F, 3.0F, 20.0F};
constexpr Oot3dNativeGame::TopScreenItemsTransform kIconTransform{2.0F, 0.0F, 7.0F, 0.0F, 3.0F, 9.0F};

std::uint32_t ReadWord(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t address) {
  std::uint32_t bits = 0U;
  Require(memory.Read32(address, &bits), "cannot read fixture word");
  return bits;
}
float ReadFloat(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t address) {
  return std::bit_cast<float>(ReadWord(memory, address));
}
void WriteWord(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t address, std::uint32_t value) {
  Require(memory.Write32(address, value), "cannot write fixture word");
}
void WriteFloat(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t address, float value) {
  WriteWord(memory, address, std::bit_cast<std::uint32_t>(value));
}
void WriteQuad(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t address, float x, float y) {
  for (std::uint32_t vertex = 0; vertex < 4U; ++vertex) {
    WriteFloat(memory, address + vertex * 12U, x + (vertex % 2U) * 40.0F);
    WriteFloat(memory, address + vertex * 12U + 4U, y + (vertex / 2U) * 40.0F);
  }
}
void WriteTransform(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t model,
                    const Oot3dNativeGame::TopScreenItemsTransform &matrix) {
  for (std::size_t lane = 0; lane < kLanes.size(); ++lane) WriteFloat(memory, model + kLanes[lane], matrix[lane]);
}
Oot3dNativeGame::TopScreenItemsTransform ReadTransform(Oot3dNativeGame::NativeA32Memory &memory,
                                                       std::uint32_t model) {
  Oot3dNativeGame::TopScreenItemsTransform matrix;
  for (std::size_t lane = 0; lane < kLanes.size(); ++lane) matrix[lane] = ReadFloat(memory, model + kLanes[lane]);
  return matrix;
}
std::vector<std::uint8_t> Snapshot(Oot3dNativeGame::NativeA32Memory &memory) {
  std::vector<std::uint8_t> data(0x18000U);
  Require(memory.ReadBytes(kHeap, data), "cannot snapshot fixture models");
  return data;
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

void BuildModel(Oot3dNativeGame::NativeA32Memory &memory, std::uint32_t model,
                std::uint32_t instance, std::uint32_t positions, std::uint32_t indices,
                std::uint32_t quads, std::uint32_t stream = 1U) {
  WriteWord(memory, model, kHeap + 0x3000U);
  WriteWord(memory, kHeap + 0x300CU, kModelDraw);
  WriteWord(memory, model + 4U, instance);
  WriteWord(memory, instance + 0x124U, stream);
  WriteWord(memory, instance + 0x128U, 2U);
  WriteWord(memory, instance + (stream + 0x68U) * 4U, positions);
  WriteWord(memory, instance + (stream + 0x6AU) * 4U, indices);
  WriteWord(memory, model + 0x174U, quads * 6U);
  WriteTransform(memory, model, kIconTransform);
}

void UploadItemsStreams(Oot3dNativeGame::NativeA32Memory &memory) {
  // Native model instance owns an uploaded copy, separate from the quad buffer.
  std::vector<std::uint8_t> positions(37U * 48U), texcoords(37U * 32U);
  Require(memory.ReadBytes(kBasePositions, positions) && memory.WriteBytes(kPagePositions, positions) &&
              memory.ReadBytes(kBaseTexcoords, texcoords) && memory.WriteBytes(kPageTexcoords, texcoords),
          "cannot emulate native geometry upload");
}

void RunItemsUpdate(Oot3dNativeGame::TopScreenWideItemsRuntime &runtime) {
  using namespace Oot3dNativeGame;
  auto state = CallerState(kTopScreenItemsUpdateBoundary);
  auto expected = state;
  expected.r[0] = expected.r[4];
  expected.r[14] = kTopScreenItemsInsetNextReturn;
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0U;
  Require(runtime.ExecuteItemsUpdate(state, &result, &blocks) && Same(state, expected) &&
              state.r[15] == kTopScreenItemsInsetNextCall && result.pc == state.r[15] &&
              result.kind == oot3d::recomp::a32::ExitKind::Branch && blocks == 1U,
          "Items update did not preserve the displaced MOV R0,R4 contract");
}

void BuildItemsPage(Oot3dNativeGame::NativeA32Memory &memory) {
  WriteWord(memory, 0x00506718U, kPageModel);
  WriteWord(memory, 0x00506714U, kPageInstance);
  BuildModel(memory, kPageModel, kPageInstance, kPagePositions, kPageIndices, 37U);
  WriteTransform(memory, kPageModel, kPageTransform);
  for (std::uint32_t quad = 0; quad < 37U; ++quad) WriteQuad(memory, kBasePositions + quad * 48U, 100.0F, 20.0F);
  // Real native constructor: quad 2 has zero extent until 0x005E3CFC.
  WriteWord(memory, 0x0050671CU, kPageBuffer);
  WriteWord(memory, kPageBuffer, 37U);
  WriteFloat(memory, kPageBuffer + 4U, 512.0F);
  WriteFloat(memory, kPageBuffer + 8U, 256.0F);
  WriteWord(memory, kPageBuffer + 0xCU, kBasePositions);
  WriteWord(memory, kPageBuffer + 0x14U, kBaseTexcoords);
  for (std::uint32_t vertex = 0; vertex < 4U; ++vertex) {
    WriteFloat(memory, kBasePositions + 0x60U + vertex * 12U, 0.0F);
    WriteFloat(memory, kBasePositions + 0x64U + vertex * 12U, 202.0F);
    WriteFloat(memory, kBasePositions + 0x68U + vertex * 12U, 7.0F);
  }
  UploadItemsStreams(memory);
  WriteWord(memory, 0x00506734U, kGroup);
  WriteWord(memory, kGroup + 8U, kGroupModel);
  BuildModel(memory, kGroupModel, kGroupInstance, kGroupPositions, kGroupIndices, 4U, 0U);
  constexpr std::array<float, 4> xs{100.0F, 256.0F, 255.0F, 300.0F};
  for (std::size_t quad = 0; quad < xs.size(); ++quad)
    WriteQuad(memory, kGroupPositions + static_cast<std::uint32_t>(quad) * 48U, xs[quad], 20.0F);
  WriteWord(memory, kGroup + 0x224U, 4U);
  for (std::uint32_t index = 0; index < 3U; ++index) {
    const auto slot = kFirstSlot + index * 0x500U;
    WriteWord(memory, kGroup + 0xCU + index * 4U, slot);
    Require(memory.Write8(kGroup + 0x434U + index, index < 2U ? 1U : 0U), "slot visibility");
    WriteWord(memory, slot + 8U, slot + 0x40U);
    WriteWord(memory, slot + 0x10U, slot + 0x100U);
    WriteWord(memory, slot + 0x4CU, slot + 0x300U);
    WriteWord(memory, slot + 0x5CU, slot + 0x400U);
    WriteFloat(memory, slot + 0x300U, index == 0U ? 300.0F : 100.0F);
    WriteFloat(memory, slot + 0x400U, 150.0F);
    WriteTransform(memory, slot + 0x100U, kIconTransform);
  }
  // One ordinary tail group and two assignment helpers, including type 7.
  WriteWord(memory, 0x00506728U, kHeap + 0xA000U);
  WriteWord(memory, 0x00506744U, kHeap + 0xA100U);
  WriteWord(memory, 0x00506738U, kHeap + 0xA200U);
  WriteWord(memory, 0x0050673CU, kHeap + 0xA300U);
  WriteWord(memory, kHeap + 0xA728U, 7U);
  WriteWord(memory, 0x00506740U, 0U);
  WriteWord(memory, 0x00506720U, kHeap + 0xA800U);
  WriteWord(memory, 0x00506730U, 2U);
  for (const auto object : {kIcon, kRightIcon, kCursor}) {
    WriteWord(memory, object + 4U, object + 0x100U);
    BuildModel(memory, object + 0x100U, object + 0x400U, object + 0x1000U, object + 0x1800U, 4U);
    for (std::uint32_t quad = 0; quad < 4U; ++quad)
      WriteQuad(memory, object + 0x1000U + quad * 48U, object == kIcon ? 100.0F : 390.0F, 20.0F);
  }
  WriteQuad(memory, kRightIcon + 0x1000U, 256.0F, 20.0F);
  WriteQuad(memory, kCursor + 0x1030U, 100.0F, 20.0F);
  WriteFloat(memory, kCursor + 0x18U, 390.0F);
  WriteWord(memory, 0x005067A8U, kIcon);
  WriteWord(memory, 0x005067ACU, kRightIcon);
  WriteWord(memory, 0x00506724U, kCursor);
  WriteWord(memory, 0x0055B62CU, 1U);
  WriteWord(memory, 0x005043E0U, kFrameOwner);
  WriteWord(memory, kFrameOwner + 0xF8U, 1U);
}

struct Draw {
  std::uint32_t Entry, Object, Count, Indices;
  float Projection;
  Oot3dNativeGame::TopScreenItemsTransform Transform{};
  std::array<float, 12> InsetPositions{};
  std::array<float, 8> InsetTexcoords{};
};

std::vector<Draw> RunItems(Oot3dNativeGame::TopScreenWideItemsRuntime &runtime,
                           Oot3dNativeGame::NativeA32Memory &memory) {
  using namespace Oot3dNativeGame;
  auto page = CallerState(kTopScreenPageProjectionUploadCall);
  Require(!runtime.ExecutePageProjection(page, true), "page upload intercepted its native call");
  auto state = CallerState(kTopScreenItemsPageDrawCall);
  const auto caller = state;
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0U;
  std::vector<Draw> draws;
  float projection = 0.8F;
  std::uint32_t writerStage = 0U;
  for (std::uint32_t step = 0; step < 512U; ++step) {
    Require(runtime.ExecuteItemsDraw(state, &result, &blocks), "Items sequence did not consume its call site");
    Require(blocks == 1U && result.kind == oot3d::recomp::a32::ExitKind::Branch, "invalid Items branch contract");
    if (result.pc == kTopScreenItemsPageDrawCall + 4U) {
      Require(Same(state, caller) && state.r[15] == result.pc, "Items sequence leaked callee registers");
      Require(writerStage == 0U && projection == 0.8F, "Items sequence did not restore page projection");
      return draws;
    }
    Require(state.r[14] == kTopScreenItemsPageDrawCall && state.r[13] == kStackTop - 0x120U,
            "Items native call has the wrong return site or stack");
    Require(result.pc < 0x005C7000U, "Items attempted to execute payload code");
    if (result.pc == 0x0032471CU) {
      Require(writerStage == 0U, "nested command writer bind");
      writerStage = 1U;
    } else if (result.pc == kUploadUniformBlock0) {
      Require(writerStage == 1U, "matrix upload did not follow writer bind");
      projection = ReadFloat(memory, state.r[1] + 0x1CU);
      Require(ReadWord(memory, state.r[1] + 0x10U) == 0xBBA3D70AU &&
                  ReadWord(memory, state.r[2]) == 0x3F800000U, "wrong projection matrix");
      writerStage = 2U;
    } else if (result.pc == 0x003246BCU) {
      Require(writerStage == 2U, "block 4 upload reordered");
      writerStage = 3U;
    } else if (result.pc == 0x002F9C74U) {
      Require(writerStage == 3U, "writer accumulation reordered");
      writerStage = 0U;
    } else {
      Require(writerStage == 0U, "native draw interrupted a command writer transaction");
      Draw draw{result.pc, state.r[0], 0U, 0U, projection};
      if (result.pc == kModelDraw) {
        const auto instance = ReadWord(memory, state.r[0] + 4U);
        const auto stream = ReadWord(memory, instance + 0x124U);
        draw.Count = ReadWord(memory, state.r[0] + 0x174U);
        draw.Indices = ReadWord(memory, instance + (stream + 0x6AU) * 4U);
        draw.Transform = ReadTransform(memory, state.r[0]);
        if (state.r[0] == kPageModel) {
          for (std::uint32_t word = 0; word < 12U; ++word)
            draw.InsetPositions[word] = ReadFloat(memory, kPagePositions + 0x60U + word * 4U);
          for (std::uint32_t word = 0; word < 8U; ++word)
            draw.InsetTexcoords[word] = ReadFloat(memory, kPageTexcoords + 0x40U + word * 4U);
        }
      } else if (result.pc == 0x002F78A0U) {
        draw.Transform = ReadTransform(memory, ReadWord(memory, state.r[0] + 0x10U));
      }
      draws.push_back(draw);
    }
    // Emulate arbitrary caller-saved and callee state damage on return.
    state.r.fill(0xDEADBEEFU);
    state.r[15] = kTopScreenItemsPageDrawCall;
    state.vfp.fill(0xBADU);
    state.cpsr = 0U;
  }
  Require(false, "Items sequence did not terminate");
  return {};
}
std::vector<Draw> RunGear(Oot3dNativeGame::TopScreenWideItemsRuntime &runtime,
                          Oot3dNativeGame::NativeA32Memory &memory) {
  using namespace Oot3dNativeGame;
  auto page = CallerState(kTopScreenPageProjectionUploadCall);
  Require(!runtime.ExecutePageProjection(page, true) && !runtime.PageWide(),
          "Gear must keep the native centred projection");
  auto state = CallerState(kTopScreenGearModelDrawCall);
  state.r[4] = kGearOwner;
  Require(memory.Write32(kGearOwner + 4U, kGearModel), "cannot place the Gear model");
  const auto caller = state;
  oot3d::recomp::a32::ExecutionResult result;
  std::vector<Draw> draws;
  for (std::uint32_t step = 0; step < 4U; ++step) {
    std::uint32_t blocks = 0U;
    Require(runtime.ExecuteGearModelDraw(state, &result, &blocks) && blocks == 1U &&
                result.kind == oot3d::recomp::a32::ExitKind::Branch,
            "Gear range sequence did not consume the model call");
    if (result.pc == kTopScreenGearModelDrawResume) {
      Require(Same(state, caller) && state.r[15] == result.pc,
              "Gear did not resume the native tail with the caller restored");
      return draws;
    }
    Require(result.pc == kModelDraw && state.r[14] == kTopScreenGearModelDrawCall &&
                state.r[13] == kStackTop - 0x120U,
            "Gear range did not call the original model draw");
    const auto instance = ReadWord(memory, state.r[0] + 4U);
    const auto stream = ReadWord(memory, instance + 0x124U);
    Draw draw{result.pc, state.r[0], ReadWord(memory, state.r[0] + 0x174U),
              ReadWord(memory, instance + (stream + 0x6AU) * 4U), 0.0F};
    draw.Transform = ReadTransform(memory, state.r[0]);
    draws.push_back(draw);
    state.r.fill(0xDEADBEEFU);
    state.r[15] = kTopScreenGearModelDrawCall;
    state.vfp.fill(0xBADU);
  }
  Require(false, "Gear sequence did not terminate");
  return {};
}
} // namespace


void TestShortcutPolicy() {
  using namespace Oot3dNativeGame;
  TopScreenItemsShortcutOrder order{0, 1, 2, 3};
  constexpr std::array actions{TopScreenDpadAction::MinimapToggle, TopScreenDpadAction::View,
                              TopScreenDpadAction::Ocarina, TopScreenDpadAction::AllBootsToggle};
  unsigned permutations = 0;
  do {
    Require(ValidateTopScreenItemsShortcutOrder(order) == order, "valid row permutation was rejected");
    for (unsigned row = 0; row < 4; ++row)
      Require(ResolveTopScreenItemsShortcutAction(order, row) == actions[order[row]],
              "shortcut row resolved to the wrong action");
    ++permutations;
  } while (std::next_permutation(order.begin(), order.end()));
  Require(permutations == 24U &&
              ValidateTopScreenItemsShortcutOrder({0, 0, 2, 3}) == kTopScreenItemsShortcutIdentity &&
              ValidateTopScreenItemsShortcutOrder({0, 1, 2, 255}) == kTopScreenItemsShortcutIdentity,
          "invalid row order did not use identity fallback");
  for (const unsigned side : {0U, 5U}) {
    for (unsigned row = 0; row < 4; ++row) {
      TopScreenItemsShortcutState state;
      TopScreenItemsShortcutInput in{true, side, row, side == 0 ? 0x20U : 0x10U};
      auto out = AdvanceTopScreenItemsShortcuts(state, in);
      Require(out.Entered && out.CursorChanged && out.ConsumeDirections == 0xF0U &&
                  out.Column == 0 && out.Row == row && state.SelectedRow == static_cast<int>(row),
              "grid/assignment edge bypassed the shortcut column");
      in.Column = 0;
      in.Directions = 0x80;
      out = AdvanceTopScreenItemsShortcuts(state, in);
      Require(state.SelectedRow == static_cast<int>((row + 1) % 4) && out.ConsumeDirections == 0xC0,
              "shortcut down did not wrap and consume native movement");
      in.Row = out.Row;
      in.Directions = 0x40;
      out = AdvanceTopScreenItemsShortcuts(state, in);
      Require(out.Row == row, "shortcut up did not wrap back");
      in.Row = row;
      in.Directions = side == 0 ? 0x10U : 0x20U;
      out = AdvanceTopScreenItemsShortcuts(state, in);
      Require(out.Left && out.CursorChanged && out.Column == (side == 0 ? 5U : 0U) &&
                  state.SelectedRow == -1 && (out.ConsumeDirections & 0x30U) == 0U,
              "shortcut exit did not permit exactly one native horizontal step");
    }
  }
  for (unsigned row = 0; row < 4; ++row) {
    TopScreenItemsShortcutState state;
    state.Order = {3, 2, 1, 0};
    state.SelectedRow = row;
    TopScreenItemsShortcutInput in{true, 0U, row};
    in.Available.fill(true);
    in.Pressed = in.Held = 1;
    auto out = AdvanceTopScreenItemsShortcuts(state, in);
    Require(out.Action == TopScreenDpadAction::None, "A press activated before hold threshold");
    in.Pressed = 0;
    for (unsigned held = 0; held < 15; ++held) {
      out = AdvanceTopScreenItemsShortcuts(state, in);
      Require((out.ConsumeButtons & 0xC01U) == 0xC01U, "shortcut A/X/Y leaked to native assignment");
      if (held < 14) Require(out.Action == TopScreenDpadAction::None, "hold request was early");
    }
    Require(out.Action == actions[3 - row], "hold request ignored the reordered action");
    Require(AdvanceTopScreenItemsShortcuts(state, in).Action == TopScreenDpadAction::None,
            "held A repeated its action without a new press");
    state.HoldFrames = 14;
    in.Available[row] = false;
    Require(AdvanceTopScreenItemsShortcuts(state, in).Action == TopScreenDpadAction::None,
            "unavailable action opened assignment");
  }
  TopScreenItemsShortcutState state;
  state.SelectedRow = 0;
  TopScreenItemsShortcutInput in{true, 0, 0};
  in.Pressed = in.Held = 1;
  AdvanceTopScreenItemsShortcuts(state, in);
  in.Pressed = in.Held = 0;
  AdvanceTopScreenItemsShortcuts(state, in);
  Require(state.MovingRow == 0, "short A tap did not start reorder");
  in.Directions = 0x90; // Right is consumed during reorder, down still moves.
  auto out = AdvanceTopScreenItemsShortcuts(state, in);
  Require(state.SelectedRow == 1 && (out.ConsumeDirections & 0xF0) == 0xF0 && !out.Left,
          "reorder navigation escaped to the native grid");
  in.Row = 1;
  in.Directions = 0;
  in.Pressed = in.Held = 1;
  AdvanceTopScreenItemsShortcuts(state, in);
  in.Pressed = in.Held = 0;
  AdvanceTopScreenItemsShortcuts(state, in);
  Require(state.SwapFrame == 0, "second row tap did not start swap animation");
  unsigned changes = 0;
  for (unsigned frame = 0; frame < 8; ++frame) {
    out = AdvanceTopScreenItemsShortcuts(state, in);
    changes += out.OrderChanged;
    Require(out.ConsumeDirections == 0xF0 && out.ConsumeButtons == 0xF03,
            "swap animation leaked native input");
  }
  Require(changes == 1 && state.Order == TopScreenItemsShortcutOrder{1, 0, 2, 3} &&
              state.SwapFrame == -1 && state.MovingRow == -1,
          "swap did not commit once after six animation updates");
  state.MovingRow = 1;
  in.Pressed = 2;
  out = AdvanceTopScreenItemsShortcuts(state, in);
  Require(state.MovingRow == -1 && (out.ConsumeButtons & 2) && !out.Left,
          "B did not cancel reorder without leaving the page");
  in.Active = false;
  out = AdvanceTopScreenItemsShortcuts(state, in);
  Require(out.Left && state.SelectedRow == -1 && state.Order == TopScreenItemsShortcutOrder{1, 0, 2, 3},
          "page exit lost host order or retained shortcut selection");
}

void TestShortcutInputOwnership(Oot3dNativeGame::NativeA32Memory &memory) {
  using namespace Oot3dNativeGame;
  TopScreenWideItemsRuntime runtime(memory);
  WriteWord(memory, kItemsState, 2U);
  WriteWord(memory, 0x00506760U, 0xFFFFFFFFU);
  Require(memory.Write16(0x0050BB40U, 0U), "cannot clear shortcut touch input");
  const auto runUpdate = [&] {
    auto state = CallerState(kTopScreenItemsUpdateBoundary);
    auto expected = state;
    expected.r[0] = state.r[4];
    expected.r[14] = kTopScreenItemsInsetNextReturn;
    oot3d::recomp::a32::ExecutionResult result;
    for (unsigned call = 0; call < 16; ++call) {
      Require(runtime.ExecuteItemsUpdate(state, &result, nullptr), "input update declined");
      if (result.pc == kTopScreenItemsInsetNextCall) {
        Require(Same(state, expected), "input update damaged the native continuation");
        return;
      }
      state.r[15] = kTopScreenItemsUpdateBoundary;
    }
    Require(false, "input update did not finish");
  };
  // D-pad and circle pad reach the same native direction words. Neither
  // the pressed nor repeat lane may be eaten while browsing ordinary cells.
  for (const auto lane : {0x0050AF2CU, 0x0050AF30U}) {
    for (unsigned column = 0; column < 6; ++column) {
      for (unsigned row = 0; row < 4; ++row) {
        for (const auto direction : {0x10U, 0x20U, 0x40U, 0x80U}) {
          if ((column == 0 && direction == 0x20U) || (column == 5 && direction == 0x10U)) continue;
          runtime.Reset();
          WriteWord(memory, 0x00506770U, column);
          WriteWord(memory, 0x00506774U, row);
          WriteWord(memory, 0x0050AF2CU, 0U);
          WriteWord(memory, 0x0050AF30U, 0U);
          WriteWord(memory, lane, direction);
          WriteWord(memory, 0x0050AF14U, 0xC01U);
          WriteWord(memory, 0x0050AF18U, 0x300U);
          WriteWord(memory, 0x0050AF10U, direction);
          runUpdate();
          Require(ReadWord(memory, 0x0050AF2CU) == (lane == 0x0050AF2CU ? direction : 0U) &&
                      ReadWord(memory, 0x0050AF30U) == (lane == 0x0050AF30U ? direction : 0U) &&
                      ReadWord(memory, 0x0050AF14U) == 0xC01U &&
                      ReadWord(memory, 0x0050AF18U) == 0x300U &&
                      ReadWord(memory, 0x0050AF10U) == direction &&
                      ReadWord(memory, 0x00506770U) == column && ReadWord(memory, 0x00506774U) == row &&
                      runtime.Shortcuts().SelectedRow == -1,
                  "shortcut interception consumed ordinary grid/assignment input");
        }
      }
    }
  }
  for (const unsigned side : {0U, 5U}) {
    runtime.Reset();
    WriteWord(memory, 0x00506770U, side);
    WriteWord(memory, 0x00506774U, 1U);
    const auto enter = side == 0 ? 0x20U : 0x10U;
    WriteWord(memory, 0x0050AF2CU, enter | 0x100U);
    WriteWord(memory, 0x0050AF30U, enter | 0x200U);
    WriteWord(memory, 0x0050AF14U, 0U);
    WriteWord(memory, 0x0050AF18U, 0U);
    runUpdate();
    Require(runtime.Shortcuts().SelectedRow == 1 && ReadWord(memory, 0x0050AF2CU) == 0x100U &&
                ReadWord(memory, 0x0050AF30U) == 0x200U,
            "shortcut entry failed to consume only native direction bits in both lanes");
    const auto leave = side == 0 ? 0x10U : 0x20U;
    WriteWord(memory, 0x0050AF2CU, leave | 0x1C0U);
    WriteWord(memory, 0x0050AF30U, leave | 0x2C0U);
    runUpdate();
    Require(runtime.Shortcuts().SelectedRow == -1 &&
                ReadWord(memory, 0x00506770U) == (side == 0 ? 5U : 0U) &&
                ReadWord(memory, 0x0050AF2CU) == (leave | 0x100U) &&
                ReadWord(memory, 0x0050AF30U) == (leave | 0x200U),
            "shortcut exit consumed the native horizontal step or leaked vertical input");
  }
  runtime.Reset();
  WriteWord(memory, kItemsState, 1U);
  WriteWord(memory, 0x00506770U, 0U);
  WriteWord(memory, 0x0050AF2CU, 0x20U);
  WriteWord(memory, 0x0050AF30U, 0x20U);
  runUpdate();
  Require(runtime.Shortcuts().SelectedRow == -1 && ReadWord(memory, 0x0050AF2CU) == 0x20U &&
              ReadWord(memory, 0x0050AF30U) == 0x20U,
          "opening animation consumed shortcut navigation");
}

void TestDpadPickerPolicy() {
  using namespace Oot3dNativeGame;
  using Phase = TopScreenDpadPickerPhase;
  constexpr std::array actions{TopScreenDpadAction::MinimapToggle, TopScreenDpadAction::View,
                              TopScreenDpadAction::Ocarina, TopScreenDpadAction::AllBootsToggle};
  constexpr std::array<std::uint32_t, 4> buttons{0x40, 0x80, 0x20, 0x10};
  for (bool child : {false, true}) for (auto action : actions) for (unsigned direction = 0; direction < 4; ++direction) {
    TopScreenDpadPickerState state;
    const auto initial = child ? kTopScreenChildDpadDefaults : kTopScreenAdultDpadDefaults;
    OpenTopScreenDpadPicker(state, action, child, initial, 1U);
    for (unsigned frame = 1; frame <= 8; ++frame) {
      const auto update = AdvanceTopScreenDpadPicker(state, true, child, 1U | buttons[direction]);
      Require(!update.Changed && update.Sound == 0 && state.Assignments == initial &&
                  state.Phase == (frame < 8 ? Phase::Opening : Phase::Choosing),
              "picker opening edge escaped the eight-update lockout");
    }
    Require(!AdvanceTopScreenDpadPicker(state, true, child, 1U | buttons[direction]).Changed,
            "held opening direction confirmed without a new edge");
    AdvanceTopScreenDpadPicker(state, true, child, 0);
    const auto update = AdvanceTopScreenDpadPicker(state, true, child, buttons[direction]);
    auto expected = initial;
    if (initial[direction] == action) expected[direction] = TopScreenDpadAction::None;
    else {
      const auto previous = std::find(expected.begin(), expected.end(), action);
      if (previous != expected.end()) *previous = initial[direction];
      expected[direction] = action;
    }
    Require(update.Changed && state.Assignments == expected &&
                update.Sound == (initial[direction] == action ? 0x0100048FU : 0x0100048DU),
            "picker did not assign/swap/unassign the current age mapping");
    if (state.Phase == Phase::Confirming) {
      for (unsigned frame = 1; frame <= 12; ++frame) {
        Require(!AdvanceTopScreenDpadPicker(state, true, child, 0xFFFU).Changed,
                "confirmation animation accepted another assignment");
        Require(state.Phase == (frame < 12 ? Phase::Confirming : Phase::Closing),
                "confirmation animation duration changed");
      }
    }
    for (unsigned frame = 1; frame <= 6; ++frame) {
      Require(!AdvanceTopScreenDpadPicker(state, true, child, 0xFFFU).Changed,
              "closing animation accepted input");
      Require(state.Active() == (frame < 6), "picker closing duration changed");
    }
  }
  const auto choosing = [] {
    TopScreenDpadPickerState state;
    OpenTopScreenDpadPicker(state, TopScreenDpadAction::MinimapToggle, true,
                           kTopScreenChildDpadDefaults, 1);
    for (unsigned i = 0; i < 8; ++i) AdvanceTopScreenDpadPicker(state, true, true, 0);
    return state;
  };
  for (const auto button : {1U, 2U}) {
    auto state = choosing();
    const auto result = AdvanceTopScreenDpadPicker(state, true, true, button);
    Require(!result.Changed && result.Sound == 0x01000496U && state.Phase == Phase::Closing &&
                state.Assignments == kTopScreenChildDpadDefaults, "A/B cancel mutated mappings");
  }
  auto state = choosing();
  Require(!AdvanceTopScreenDpadPicker(state, true, true, 0xF0000000U).Changed,
          "circle-pad chose a physical D-pad slot");
  state = choosing();
  AdvanceTopScreenDpadPicker(state, true, true, 0x400U);
  for (unsigned i = 0; i < 44; ++i)
    Require(!AdvanceTopScreenDpadPicker(state, true, true, 0x400U).Changed, "clear-all fired early");
  auto result = AdvanceTopScreenDpadPicker(state, true, true, 0x400U);
  Require(result.Changed && result.Sound == 0x01000496U && state.Assignments == TopScreenDpadAssignments{} &&
              state.Phase == Phase::Choosing, "held X did not clear all mappings in-place");
  for (unsigned i = 0; i < 60; ++i)
    Require(!AdvanceTopScreenDpadPicker(state, true, true, 0x400U).Changed, "held X repeated clear-all");
  state = choosing();
  AdvanceTopScreenDpadPicker(state, true, false, 0x40U);
  Require(!state.Active(), "age change retained the picker");
  state = choosing();
  AdvanceTopScreenDpadPicker(state, false, true, 0x40U);
  Require(!state.Active(), "page/pause exit retained the picker");
}

void TestDpadPickerRuntime(Oot3dNativeGame::NativeA32Memory &memory) {
  using namespace Oot3dNativeGame;
  TopScreenWideItemsRuntime runtime(memory);
  const auto update = [&](std::uint32_t held) {
    WriteWord(memory, 0x0050AF10U, held);
    auto state = CallerState(kTopScreenItemsUpdateBoundary);
    auto expected = state;
    expected.r[0] = state.r[4];
    expected.r[14] = kTopScreenItemsInsetNextReturn;
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t sound = 0;
    for (unsigned n = 0; n < 20; ++n) {
      runtime.ExecuteItemsUpdate(state, &result, nullptr);
      if (result.pc == kTopScreenItemsInsetNextCall) {
        Require(Same(state, expected), "picker native sequence damaged caller state");
        return sound;
      }
      if (result.pc == 0x0037547CU) {
        sound = state.r[0];
        Require(state.r[1] == 0 && state.r[2] == 4 && state.r[3] == 0x0054AC20U &&
                    ReadWord(memory, state.r[13]) == 0x0054AC20U &&
                    ReadWord(memory, state.r[13] + 4) == 0x0054AC24U,
                "picker sound arguments differ from 0x005D1FE4");
      }
      Require(state.r[14] == kTopScreenItemsUpdateBoundary, "picker sound returns outside observed boundary");
      state.r[0] = state.vfp[0] = 0xBAD;
    }
    Require(false, "picker native sequence did not finish");
    return 0U;
  };
  const auto hold = [&](bool child) {
    runtime.Reset();
    WriteWord(memory, kItemsState, 2);
    WriteWord(memory, 0x0058795CU, child ? 1U : 0U);
    WriteWord(memory, 0x00506770U, 0);
    WriteWord(memory, 0x00506774U, 0);
    WriteWord(memory, 0x00506758U, 0);
    WriteWord(memory, 0x0050677CU, 0);
    WriteWord(memory, 0x0050AF2CU, 0);
    WriteWord(memory, 0x0050AF30U, 0x20);
    WriteWord(memory, 0x0050AF14U, 0);
    WriteWord(memory, 0x0050AF18U, 0);
    update(0);
    WriteWord(memory, 0x0050AF18U, 1);
    update(1);
    for (unsigned frame = 0; frame < 14; ++frame) update(1);
    Require(runtime.Shortcuts().HoldFrames == 14 && !runtime.DpadPicker().Active(),
            "A hold did not reach the final pre-picker update");
  };
  const auto open = [&](bool child) {
    hold(child);
    Require(update(1) == 0x01000497U && runtime.DpadPicker().Active() &&
                runtime.DpadPicker().Action == TopScreenDpadAction::MinimapToggle &&
                runtime.DpadPicker().Frame == 1,
            "hold A failed to open picker with the native open sound");
  };
  auto childMapping = kTopScreenChildDpadDefaults, adultMapping = kTopScreenAdultDpadDefaults;
  unsigned changes = 0;
  bool changedChild = false;
  runtime.SetDpadAssignmentChanged([&](bool child, const auto &mapping) {
    ++changes;
    changedChild = child;
    (child ? childMapping : adultMapping) = mapping;
    runtime.SetDpadAssignments(childMapping, adultMapping);
  });
  for (bool child : {false, true}) {
    open(child);
    for (unsigned frame = 0; frame < 8; ++frame) {
      for (const auto address : {0x0050AF14U, 0x0050AF18U, 0x0050AF2CU, 0x0050AF30U})
        WriteWord(memory, address, 0xFFFFU);
      update(0);
      for (const auto address : {0x0050AF14U, 0x0050AF18U, 0x0050AF2CU, 0x0050AF30U})
        Require(ReadWord(memory, address) == 0, "picker leaked native grid/column/shoulder input");
      Require(runtime.Shortcuts().SelectedRow == 0 && runtime.Shortcuts().MovingRow < 0 &&
                  ReadWord(memory, 0x00506770U) == 0 && ReadWord(memory, 0x00506774U) == 0,
              "picker changed the selected shortcut or started reordering");
    }
    const auto previousChanges = changes;
    Require(update(0x10U) == 0x0100048DU && changes == previousChanges + 1 && changedChild == child &&
                (child ? childMapping : adultMapping)[3] == TopScreenDpadAction::MinimapToggle &&
                runtime.DpadPicker().Phase == TopScreenDpadPickerPhase::Confirming,
            "confirmation did not Preview exactly once or reentrant refresh closed the picker");
    for (unsigned frame = 0; frame < 18; ++frame) update(0x10U);
    Require(!runtime.DpadPicker().Active() && changes == previousChanges + 1,
            "confirmation/close repeated callback or left modal input active");
  }
  Require(childMapping[0] == kTopScreenChildDpadDefaults[0] &&
              adultMapping[2] == kTopScreenAdultDpadDefaults[2], "assignment modified an unrelated age/slot");
  open(true);
  auto replacement = childMapping;
  replacement[0] = TopScreenDpadAction::None;
  runtime.SetDpadAssignments(replacement, adultMapping);
  Require(!runtime.DpadPicker().Active(), "external mapping edit left a stale picker");
  open(true);
  runtime.SetShortcutOrder({1, 0, 2, 3});
  Require(!runtime.DpadPicker().Active(), "external row edit left stale selected action");
  runtime.SetShortcutOrder(kTopScreenItemsShortcutIdentity);
  for (const auto page : {0U, 1U, 3U}) {
    open(true);
    WriteWord(memory, kItemsState, page);
    update(0x40U);
    Require(!runtime.DpadPicker().Active() && runtime.Shortcuts().SelectedRow == -1,
            "pause/page change did not tear down picker and shortcut selection");
  }
  open(true);
  WriteWord(memory, 0x0058795CU, 0);
  update(0x40U);
  Require(!runtime.DpadPicker().Active(), "native age transition retained old age picker");
  open(true);
  WriteWord(memory, kItemsState, 0);
  runtime.ObserveDpadPickerPage();
  Require(!runtime.DpadPicker().Active(), "lifecycle-only pause close retained modal state");
  for (const auto page : {0U, 1U, 3U}) {
    hold(true);
    WriteWord(memory, kItemsState, page);
    runtime.ObserveDpadPickerPage();
    Require(runtime.Shortcuts().HoldFrames == -1 && !runtime.DpadPicker().Active(),
            "lifecycle-only transition retained the pre-picker A hold");
    WriteWord(memory, kItemsState, 2);
    update(1);
    Require(!runtime.DpadPicker().Active(), "returning to Items resumed a cancelled A hold");
  }
  hold(true);
  WriteWord(memory, 0x0058795CU, 0);
  runtime.ObserveDpadPickerPage();
  Require(runtime.Shortcuts().HoldFrames == -1, "age observation retained pre-picker A progress");
  hold(true);
  WriteWord(memory, 0x0058795CU, 0);
  update(1);
  Require(runtime.Shortcuts().HoldFrames == -1 && !runtime.DpadPicker().Active(),
          "guest update opened a picker with the previous age hold");
  open(true);
  const auto beforeCancel = changes;
  runtime.CancelDpadPicker();
  Require(!runtime.DpadPicker().Active() && changes == beforeCancel &&
              runtime.Shortcuts().HoldFrames == -1,
          "lifecycle savestate cancellation retained input or committed mapping");
  open(true);
  const auto beforeReset = changes;
  runtime.Reset();
  Require(!runtime.DpadPicker().Active() && changes == beforeReset, "savestate reset committed or retained modal state");
  WriteWord(memory, kItemsState, 0);
  WriteWord(memory, 0x0050AF10U, 0);
}

void TestViewModel(Oot3dNativeGame::NativeA32Memory &memory) {
  using namespace Oot3dNativeGame;
  std::string error;
  // Piecewise-constant table (zero slopes): angle a reads entry a >> 8.
  Require(memory.MapRegion({"view-model-trig", 0x004DF000U, 0x2000U, true, false, {}}, &error),
          "cannot map the sine table fixture");
  const auto sine = [](std::uint32_t index, bool cosine) {
    const float angle = static_cast<float>(index) * 6.28318530718F / 256.0F;
    return cosine ? std::cos(angle) : std::sin(angle);
  };
  for (std::uint32_t index = 0; index < 256U; ++index) {
    WriteFloat(memory, kNativeTrigonometryTable + index * 16U, sine(index, false));
    WriteFloat(memory, kNativeTrigonometryTable + index * 16U + 4U, sine(index, true));
    WriteWord(memory, kNativeTrigonometryTable + index * 16U + 8U, 0U);
    WriteWord(memory, kNativeTrigonometryTable + index * 16U + 12U, 0U);
  }
  constexpr std::uint32_t model = kHeap + 0x20000U;
  constexpr std::uint32_t owner = kHeap + 0x20400U;
  constexpr std::uint32_t matrix = kHeap + 0x20800U;
  WriteWord(memory, kItemsState, 0U);
  WriteWord(memory, 0x00504484U, 2U);
  WriteWord(memory, 0x004FC6ACU, 0x3BU);
  WriteWord(memory, 0x004FC67CU, model);
  WriteWord(memory, 0x005655ACU, 0U);
  Require(memory.Write8(model + 0x11DU, 1U) && memory.Write8(model + 0x11AU, 0U) &&
              memory.Write8(model + 0x11BU, 0U) && memory.Write16(model + 0x118U, 90U),
          "cannot write View Model fixture");
  WriteFloat(memory, model + 8U, 1.0F);
  WriteFloat(memory, model + 0xCU, 2.0F);
  WriteFloat(memory, model + 0x10U, 3.0F);
  WriteFloat(memory, model + 0x14U, 0.5F);
  WriteWord(memory, owner + 0xFCU, 0x00712340U);
  TopScreenWideItemsRuntime runtime(memory);
  const auto update = [&](std::uint32_t held) {
    WriteWord(memory, 0x0050AF10U, held);
    for (const auto address : {0x0050AF14U, 0x0050AF18U, 0x0050AF2CU, 0x0050AF30U})
      WriteWord(memory, address, 0xFFFFU);
    auto state = CallerState(kTopScreenItemsUpdateBoundary);
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t sound = 0;
    for (unsigned n = 0; n < 20; ++n) {
      runtime.ExecuteItemsUpdate(state, &result, nullptr);
      if (result.pc == kTopScreenItemsInsetNextCall) return sound;
      if (result.pc == 0x0037547CU) sound = state.r[0];
    }
    Require(false, "View Model update did not finish");
    return 0U;
  };
  const auto consumed = [&] {
    for (const auto address : {0x0050AF14U, 0x0050AF18U, 0x0050AF2CU, 0x0050AF30U})
      if (ReadWord(memory, address) != 0U) return false;
    return true;
  };
  const auto transform = [&](float m9, bool *handled) {
    for (std::uint32_t lane = 0; lane < 12U; ++lane) WriteFloat(memory, matrix + lane * 4U, 7.0F);
    WriteFloat(memory, matrix + 0x24U, m9);
    auto state = CallerState(kTopScreenViewModelTransformBlock);
    state.r[6] = owner;
    state.r[8] = matrix;
    const auto caller = state;
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocks = 0U;
    *handled = runtime.ExecuteViewModelTransform(state, &result, &blocks);
    if (!*handled) {
      Require(Same(state, caller) && ReadFloat(memory, matrix) == 7.0F,
              "closed View Model changed the native transform block");
      return;
    }
    Require(state.r[0] == 0x00712340U && state.r[1] == matrix &&
                state.r[14] == kTopScreenViewModelTransformReturn && state.r[15] == 0x003721E0U &&
                result.pc == 0x003721E0U && result.kind == oot3d::recomp::a32::ExitKind::Branch &&
                blocks == 1U,
            "View Model transform did not continue into ModelHandle_CopyOverrideTransform");
  };

  bool handled = true;
  transform(1.0F, &handled);
  Require(!handled, "closed View Model intercepted the native transform");
  WriteWord(memory, 0x00504484U, 0U);
  update(0x400U);
  WriteWord(memory, 0x00504484U, 2U);
  Require(update(0x400U) == 0U && !runtime.ViewModelActive(),
          "View Model opened on a held X without a new press");
  update(0U);
  constexpr std::array<std::array<std::uint32_t, 2>, 4> refusals{
      {{0xFFFFFFFFU, 2U}, {0x8AU, 2U}, {0x3BU, 0U}, {0x3BU, 1U}}};
  for (const auto &[equipment, pageState] : refusals) {
    WriteWord(memory, 0x004FC6ACU, equipment);
    WriteWord(memory, 0x00504484U, pageState);
    Require(update(0x400U) == 0U && !runtime.ViewModelActive(),
            "View Model opened without Gear page state 2 or a previewed item");
    update(0U);
  }
  WriteWord(memory, 0x004FC6ACU, 0x3BU);
  WriteWord(memory, 0x00504484U, 2U);
  Require(update(0x400U) == 0x01000497U && runtime.ViewModelActive() && consumed(),
          "X on the Gear page did not open the View Model with its sound and consume input");
  transform(1.0F, &handled);
  // Yaw (90 << 16) / 360 = 0x4000 (entry 64), pitch 0x71C (entry 7).
  const float sinA = sine(7, false), cosA = sine(7, true), sinB = sine(64, false), cosB = sine(64, true);
  const std::array<float, 12> rotated{cosB * 0.5F, 0.0F, sinB * 0.5F, 1.0F,
                                      sinA * sinB * 0.5F, cosA * 0.5F, -(sinA * cosB) * 0.5F, 2.0F,
                                      -(cosA * sinB) * 0.5F, sinA * 0.5F, cosA * cosB * 0.5F, 3.0F};
  for (std::uint32_t lane = 0; lane < 12U; ++lane)
    Require(handled && ReadFloat(memory, matrix + lane * 4U) == rotated[lane],
            "View Model matrix differs from 0x005D2734");
  transform(0.0F, &handled);
  const std::array<float, 12> identity{0.5F, 0.0F, 0.0F, 1.0F, 0.0F, 0.5F, 0.0F, 2.0F, 0.0F, 0.0F, 0.5F, 3.0F};
  for (std::uint32_t lane = 0; lane < 12U; ++lane)
    Require(ReadFloat(memory, matrix + lane * 4U) == identity[lane],
            "View Model matrix without rotation lost the scaled identity");

  // The entry X zooms only after a release; Y zooms out at once.
  update(0x400U);
  transform(0.0F, &handled);
  Require(ReadFloat(memory, matrix) == 0.5F, "the entry X zoomed before its release");
  update(0U);
  update(0x400U);
  transform(0.0F, &handled);
  Require(ReadFloat(memory, matrix) == 0.5F * 1.04F, "released X did not zoom by 1.04");
  for (unsigned frame = 0; frame < 40; ++frame) update(0x400U);
  transform(0.0F, &handled);
  Require(ReadFloat(memory, matrix) == 0.5F * 2.5F, "X zoom is not capped at 2.5");
  for (unsigned frame = 0; frame < 60; ++frame) update(0x800U);
  transform(0.0F, &handled);
  Require(ReadFloat(memory, matrix) == 0.5F * 0.5F && consumed(), "Y zoom is not capped at 0.5");

  // D-pad pan, 0.4 per update, within +-12 / +-9.
  for (unsigned frame = 0; frame < 40; ++frame) update(0x10U | 0x80U);
  transform(0.0F, &handled);
  Require(ReadFloat(memory, matrix + 0xCU) == 13.0F && ReadFloat(memory, matrix + 0x1CU) == -7.0F,
          "D-pad pan is not clamped to +-12 / +-9");

  // Circle pad: |value| <= 12 is dead; yaw x*0x300/105, pitch y*512/-105.
  std::uint16_t angle = 0U;
  Require(memory.Write16(0x005655ACU, 12U) && memory.Write16(0x005655AEU, 0xFFF4U), "cannot write stick");
  update(0U);
  Require(memory.Read16(model + 0x118U, &angle) && angle == 90U, "stick dead zone rotated the model");
  Require(memory.Write16(0x005655ACU, 105U) && memory.Write16(0x005655AEU, 0U), "cannot write stick");
  update(0U);
  Require(memory.Read16(model + 0x118U, &angle) && angle == ((0x4000U + 0x300U) * 360U) >> 16,
          "stick X did not turn the yaw and the model angle as 0x005D2274 does");
  Require(memory.Write16(0x005655ACU, 0U) && memory.Write16(0x005655AEU, 0xFF97U), "cannot write stick");
  for (unsigned frame = 0; frame < 40; ++frame) update(0U);
  Require(memory.Write16(0x005655AEU, 0U), "cannot write stick");
  transform(1.0F, &handled);
  // Pitch +512 per update, capped at 0x3800: 0x3800 + 0x71C reads entry 63.
  Require(ReadFloat(memory, matrix + 0x14U) == sine(63, true) * 0.25F,
          "stick Y pitch is not clamped at 0x3800");

  Require(update(2U) == 0x01000496U && !runtime.ViewModelActive() && consumed(),
          "B did not close the View Model with its sound");
  transform(1.0F, &handled);
  Require(!handled, "closed View Model kept its transform");
  update(0U);
  Require(update(0x400U) == 0x01000497U && runtime.ViewModelActive(), "View Model did not reopen");
  transform(1.0F, &handled);
  // Reopening restarts pitch at zero: the pitch clamp above must not persist.
  Require(ReadFloat(memory, matrix + 0x14U) == cosA * 0.5F, "reopened View Model kept its pitch");
  WriteWord(memory, 0x00504484U, 1U);
  Require(update(0U) == 0x01000496U && !runtime.ViewModelActive(),
          "leaving Gear page state 2 did not close the View Model");
  WriteWord(memory, 0x00504484U, 2U);
  update(0U);
  update(0x400U);
  Require(memory.Write8(model + 0x11AU, 0x8CU) && memory.Write8(model + 0x11BU, 0x8CU), "cannot write model");
  Require(update(0U) == 0x01000496U && !runtime.ViewModelActive(), "unloading model kept the View Model");
  Require(memory.Write8(model + 0x11AU, 0U) && memory.Write8(model + 0x11BU, 0U), "cannot write model");
  update(0U);
  update(0x400U);
  runtime.Reset();
  transform(1.0F, &handled);
  Require(!runtime.ViewModelActive() && !handled, "reset retained the View Model");
  WriteWord(memory, 0x00504484U, 0U);
  WriteWord(memory, 0x0050AF10U, 0U);
}

// Mode 1 (Y in the picker) and the Gear legend counters; after TestViewModel,
// whose sine table and item model fixture it reuses.
void TestPickerViewModel(Oot3dNativeGame::NativeA32Memory &memory) {
  using namespace Oot3dNativeGame;
  std::string error;
  Require(memory.MapRegion({"view-model-preview", 0x004D2000U, 0x1000U, true, false, {}}, &error),
          "cannot map the item preview table fixture");
  constexpr std::uint32_t model = kHeap + 0x20000U;
  constexpr std::uint32_t matrix = kHeap + 0x20800U;
  constexpr std::uint32_t save = 0x00587958U;
  Require(memory.Write8(model + 0x11DU, 1U) && memory.Write8(model + 0x11AU, 0U) &&
              memory.Write8(model + 0x11BU, 0U) && memory.Write8(0x004D2EC0U + 8U, 0x20U) &&
              memory.Write8(0x0053CC44U + 7U, 0xFFU) && memory.Write8(0x0053CC44U + 8U, 3U) &&
              memory.Write8(save + 0x8CU + 3U, 8U),
          "cannot write the picker View Model fixture");
  WriteWord(memory, 0x004FC67CU, model);
  WriteWord(memory, 0x00504484U, 0U);
  WriteWord(memory, 0x005655ACU, 0U);
  TopScreenWideItemsRuntime runtime(memory);
  struct NativeCall { std::uint32_t Pc, R0, R1; };
  std::vector<NativeCall> calls;
  const auto update = [&](std::uint32_t held, std::uint32_t lanes = 0U) {
    calls.clear();
    WriteWord(memory, 0x0050AF10U, held);
    for (const auto address : {0x0050AF14U, 0x0050AF18U, 0x0050AF2CU, 0x0050AF30U})
      WriteWord(memory, address, lanes);
    auto state = CallerState(kTopScreenItemsUpdateBoundary);
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t sound = 0;
    for (unsigned n = 0; n < 20; ++n) {
      runtime.ExecuteItemsUpdate(state, &result, nullptr);
      if (result.pc == kTopScreenItemsInsetNextCall) return sound;
      if (result.pc == 0x0037547CU) sound = state.r[0];
      else calls.push_back({result.pc, state.r[0], state.r[1]});
    }
    Require(false, "picker View Model update did not finish");
    return 0U;
  };
  const auto called = [&](std::uint32_t pc, std::uint32_t r0, std::uint32_t r1) {
    for (const auto &call : calls)
      if (call.Pc == pc && call.R0 == r0 && (pc == 0x002E9B00U || call.R1 == r1)) return true;
    return false;
  };
  const auto nativeCalls = [&] {
    return std::count_if(calls.begin(), calls.end(), [](const NativeCall &call) {
      return call.Pc == 0x002E9B00U || call.Pc == 0x002E112CU;
    });
  };
  const auto zoom = [&] {
    for (std::uint32_t lane = 0; lane < 12U; ++lane) WriteFloat(memory, matrix + lane * 4U, 7.0F);
    WriteFloat(memory, matrix + 0x24U, 0.0F);
    auto state = CallerState(kTopScreenViewModelTransformBlock);
    state.r[6] = kHeap + 0x20400U;
    state.r[8] = matrix;
    oot3d::recomp::a32::ExecutionResult result;
    Require(runtime.ExecuteViewModelTransform(state, &result, nullptr),
            "open picker View Model left the native transform");
    return ReadFloat(memory, matrix) / ReadFloat(memory, model + 0x14U);
  };
  // 0x005D2014 opens the picker after a 15-update A hold on the first row.
  const auto open = [&](const TopScreenItemsShortcutOrder &order) {
    runtime.Reset();
    runtime.SetShortcutOrder(order);
    WriteWord(memory, kItemsState, 2);
    WriteWord(memory, 0x0058795CU, 0U);
    for (const auto address : {0x00506770U, 0x00506774U, 0x00506758U, 0x0050677CU})
      WriteWord(memory, address, 0U);
    WriteWord(memory, 0x0050AF10U, 0U);
    for (const auto address : {0x0050AF14U, 0x0050AF18U, 0x0050AF2CU}) WriteWord(memory, address, 0U);
    WriteWord(memory, 0x0050AF30U, 0x20U);
    auto state = CallerState(kTopScreenItemsUpdateBoundary);
    oot3d::recomp::a32::ExecutionResult result;
    for (unsigned n = 0; n < 20 && result.pc != kTopScreenItemsInsetNextCall; ++n)
      runtime.ExecuteItemsUpdate(state, &result, nullptr);
    WriteWord(memory, 0x0050AF18U, 1U);
    WriteWord(memory, 0x0050AF10U, 1U);
    for (unsigned frame = 0; frame < 16; ++frame) {
      auto hold = CallerState(kTopScreenItemsUpdateBoundary);
      result = {};
      for (unsigned n = 0; n < 20 && result.pc != kTopScreenItemsInsetNextCall; ++n)
        runtime.ExecuteItemsUpdate(hold, &result, nullptr);
    }
    Require(runtime.DpadPicker().Active(), "A hold did not open the picker");
    for (unsigned frame = 0; frame < 8; ++frame) update(0U);
    Require(runtime.DpadPicker().Phase == TopScreenDpadPickerPhase::Choosing,
            "picker did not settle on Choosing");
  };
  constexpr TopScreenItemsShortcutOrder ocarinaFirst{2, 1, 0, 3};

  // The Ocarina row: 0x005D289C adds [Y] View Model, and Y previews item 8.
  WriteWord(memory, 0x004FC6ACU, 0x50U);
  open(ocarinaFirst);
  Require(runtime.DpadPickerDraw().ViewModel, "picker legend has no [Y] for the Ocarina");
  Require(update(0x800U) == 0x01000497U && runtime.ViewModelActive() && called(0x002E9B00U, 8U, 0U) &&
              runtime.DpadPicker().Phase == TopScreenDpadPickerPhase::Choosing,
          "Y did not open mode 1 with 0x002E9B00(8) and the entry sound");
  WriteWord(memory, 0x004FC6ACU, 0x20U); // 0x002E9B00 stores the preview object.
  const auto frame = runtime.DpadPicker().Frame;
  const auto assignments = runtime.DpadPicker().Assignments;
  // Y opened mode 1, so it zooms out only after its release; X zooms at once.
  update(0x800U);
  Require(zoom() == 1.0F, "the entry Y zoomed out before its release");
  update(0x400U);
  Require(zoom() == 1.04F, "X did not zoom in at once in mode 1");
  update(0x800U);
  Require(zoom() == 1.0F, "released Y did not zoom out");
  update(0x40U, 0xFFFFU);
  for (const auto address : {0x0050AF14U, 0x0050AF18U, 0x0050AF2CU, 0x0050AF30U})
    Require(ReadWord(memory, address) == 0U, "mode 1 leaked native input");
  Require(runtime.DpadPicker().Frame == frame && runtime.DpadPicker().Assignments == assignments &&
              runtime.DpadPicker().Phase == TopScreenDpadPickerPhase::Choosing,
          "the picker advanced or assigned under mode 1");
  // B closes and restores the item previewed at entry (0x002E112C).
  Require(update(2U) == 0x01000496U && !runtime.ViewModelActive() && called(0x002E112CU, model, 0x50U) &&
              ReadWord(memory, 0x004FC6ACU) == 0x50U && runtime.DpadPicker().Active(),
          "B did not restore the previewed item and return to the picker");
  update(2U);
  Require(runtime.DpadPicker().Phase == TopScreenDpadPickerPhase::Choosing,
          "the B that closed the View Model also closed the picker");
  // No previewed item at entry: B clears the preview through 0x002E9B00(-1).
  update(0U);
  WriteWord(memory, 0x004FC6ACU, 0xFFFFFFFFU);
  update(0x800U);
  WriteWord(memory, 0x004FC6ACU, 0x20U);
  update(0U);
  Require(update(2U) == 0x01000496U && called(0x002E9B00U, 0xFFFFFFFFU, 0U),
          "B did not clear a preview that had no item at entry");
  // An unchanged preview needs no restore.
  update(0U);
  WriteWord(memory, 0x004FC6ACU, 0x20U);
  update(0x800U);
  update(0U);
  Require(update(2U) == 0x01000496U && nativeCalls() == 0, "B restored an unchanged preview");
  // A picker closed from outside closes mode 1 without a restore.
  update(0U);
  WriteWord(memory, 0x004FC6ACU, 0x50U);
  update(0x800U);
  WriteWord(memory, 0x004FC6ACU, 0x20U);
  runtime.CancelDpadPicker();
  update(0U);
  Require(!runtime.ViewModelActive() && !called(0x002E112CU, model, 0x50U) &&
              ReadWord(memory, 0x004FC6ACU) == 0x20U,
          "closing the picker kept mode 1 or restored the preview");
  // No item model in the preview table, and actions without an item.
  open(ocarinaFirst);
  Require(memory.Write8(0x004D2EC0U + 8U, 0x8AU), "cannot write the preview table");
  Require(update(0x800U) == 0U && !runtime.ViewModelActive() && nativeCalls() == 0,
          "Y opened an item without a preview object");
  Require(memory.Write8(0x004D2EC0U + 8U, 0x20U), "cannot write the preview table");
  open(kTopScreenItemsShortcutIdentity);
  Require(!runtime.DpadPickerDraw().ViewModel && update(0x800U) == 0U && !runtime.ViewModelActive(),
          "the minimap action offered a View Model");
  runtime.Reset();
  WriteWord(memory, kItemsState, 0U);

  // 0x005E46A0 / 0x005E2B10: the Gear legend after 90 updates on one slot.
  WriteWord(memory, 0x00504484U, 2U);
  WriteWord(memory, 0x0050448CU, 5U);
  WriteWord(memory, 0x00504480U, kHeap + 0x21000U);
  WriteWord(memory, 0x004FC6ACU, 0x3BU);
  std::uint32_t fade = 0, pulse = 0;
  for (unsigned n = 0; n < 91; ++n) update(0U);
  Require(!runtime.GearViewModelLegend(&fade, &pulse), "Gear legend appeared before 90 updates");
  update(0U);
  Require(runtime.GearViewModelLegend(&fade, &pulse) && fade == 1U && pulse == 92U,
          "Gear legend did not fade in after 90 updates on one slot");
  for (unsigned n = 0; n < 30; ++n) update(0U);
  Require(runtime.GearViewModelLegend(&fade, &pulse) && fade == 12U, "Gear legend fade is not capped");
  update(0x400U);
  Require(runtime.ViewModelActive() && !runtime.GearViewModelLegend(&fade, &pulse),
          "Gear legend stayed under the View Model");
  update(0U);
  update(2U);
  Require(!runtime.GearViewModelLegend(&fade, &pulse), "Gear legend skipped the closing update");
  update(0U);
  Require(runtime.GearViewModelLegend(&fade, &pulse), "Gear legend did not return after the View Model");
  for (const auto [address, value] : std::array<std::array<std::uint32_t, 2>, 3>{
           {{0x004FC6ACU, 0x8AU}, {0x0050448CU, 0x1BU}, {0x00504480U, 0U}}}) {
    const auto previous = ReadWord(memory, address);
    WriteWord(memory, address, value);
    Require(!runtime.GearViewModelLegend(&fade, &pulse), "Gear legend ignored a 0x005E2B10 condition");
    WriteWord(memory, address, previous);
  }
  WriteWord(memory, 0x0050448CU, 6U);
  update(0U);
  Require(!runtime.GearViewModelLegend(&fade, &pulse), "Gear legend survived a cursor move");
  WriteWord(memory, 0x00504484U, 0U);
  WriteWord(memory, 0x0050AF10U, 0U);
}

// 0x005E4740: the Gear-page A hold that opens the picker (after
// TestPickerViewModel, whose Ocarina, model and preview fixtures it reuses).
void TestGearHold(Oot3dNativeGame::NativeA32Memory &memory) {
  using namespace Oot3dNativeGame;
  constexpr std::uint32_t save = 0x00587958U;
  for (std::uint32_t group = 0; group < 3U; ++group) {
    Require(memory.Write8(0x0053CB08U + group, static_cast<std::uint8_t>(group * 4U)),
            "cannot write the equipment shift fixture");
    WriteWord(memory, 0x0053CB24U + group * 4U, 1U << group);
  }
  Require(memory.Write16(save + 0xB6U, 0x0777U) && memory.Write16(save + 0x8AU, 0x0112U) &&
              memory.Write8(0x004D2EC0U + 0x3CU, 0x21U),
          "cannot write the Gear hold fixture");
  WriteWord(memory, 0x0058795CU, 0U);
  WriteWord(memory, kItemsState, 0U);
  WriteWord(memory, 0x00504484U, 2U);
  WriteWord(memory, 0x0050448CU, 1U);
  TopScreenWideItemsRuntime runtime(memory);
  struct NativeCall { std::uint32_t Pc, R0; };
  std::vector<NativeCall> calls;
  const auto update = [&](std::uint32_t held, std::uint32_t pressed = 0U) {
    calls.clear();
    WriteWord(memory, 0x0050AF10U, held);
    WriteWord(memory, 0x0050AF14U, pressed);
    WriteWord(memory, 0x0050AF18U, pressed);
    WriteWord(memory, 0x0050AF2CU, 0U);
    WriteWord(memory, 0x0050AF30U, 0U);
    auto state = CallerState(kTopScreenItemsUpdateBoundary);
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t sound = 0;
    for (unsigned n = 0; n < 20; ++n) {
      runtime.ExecuteItemsUpdate(state, &result, nullptr);
      if (result.pc == kTopScreenItemsInsetNextCall) return sound;
      if (result.pc == 0x0037547CU) sound = state.r[0];
      else calls.push_back({result.pc, state.r[0]});
    }
    Require(false, "Gear hold update did not finish");
    return 0U;
  };
  const auto aLanes = [&] {
    return (ReadWord(memory, 0x0050AF14U) & 1U) | (ReadWord(memory, 0x0050AF18U) & 1U);
  };
  std::uint32_t slot = 0;
  std::int32_t frames = 0;

  // A tap is held back, then handed to the native page on release.
  update(1U, 1U);
  Require(aLanes() == 0U && !runtime.DpadPicker().Active(), "Gear A press reached the native page at once");
  update(0U);
  Require((ReadWord(memory, 0x0050AF18U) & 1U) == 1U && !runtime.GearHold(&slot, &frames),
          "released Gear A was not handed back to the native page");
  // Fifteen held updates open the sword picker; the ring shows from update 2.
  update(1U, 1U);
  update(1U);
  Require(!runtime.GearHold(&slot, &frames), "Gear ring appeared on the first held update");
  update(1U);
  Require(runtime.GearHold(&slot, &frames) && slot == 1U && frames == 2 && aLanes() == 0U,
          "Gear ring or A ownership differs from 0x005CD470");
  for (unsigned n = 0; n < 12; ++n) update(1U);
  Require(!runtime.DpadPicker().Active(), "Gear picker opened before the fifteenth update");
  Require(update(1U) == 0x01000497U && runtime.DpadPicker().Active() && runtime.DpadPicker().Gear &&
              runtime.DpadPicker().Action == TopScreenDpadAction::SwordToggle &&
              runtime.DpadPicker().Frame == 1 && !runtime.GearHold(&slot, &frames),
          "Gear hold did not open the sword picker as 0x005D2014(7 | 0x1000, 3)");
  for (unsigned n = 0; n < 8; ++n) update(0U);
  Require(runtime.DpadPicker().Phase == TopScreenDpadPickerPhase::Choosing,
          "Gear picker closed without the Items page");
  // Mode 1 from the Gear picker: the carried Master Sword (0x3C).
  update(0x800U);
  bool previewed = false;
  for (const auto &call : calls) previewed = previewed || (call.Pc == 0x002E9B00U && call.R0 == 0x3CU);
  Require(runtime.ViewModelActive() && previewed, "Y in the Gear picker did not preview the sword");
  update(2U);
  Require(!runtime.ViewModelActive() && runtime.DpadPicker().Active(), "B did not return to the Gear picker");
  WriteWord(memory, 0x00504484U, 1U);
  update(0U);
  Require(!runtime.DpadPicker().Active(), "leaving the Gear page kept its picker");
  WriteWord(memory, 0x00504484U, 2U);
  update(0U);

  // Moving the cursor ends the hold; slots without an action pass A through.
  update(1U, 1U);
  WriteWord(memory, 0x0050448CU, 2U);
  for (unsigned n = 0; n < 20; ++n) update(1U);
  Require(!runtime.DpadPicker().Active(), "a moved cursor kept its Gear hold");
  update(0U);
  for (const std::uint32_t free : {9U, 17U, 19U}) {
    WriteWord(memory, 0x0050448CU, free);
    update(1U, 1U);
    Require(aLanes() == 1U && !runtime.GearHold(&slot, &frames), "a slot without an action took A");
    update(0U);
  }
  // Child: no sword or tunic cycle; Deku + Hylian shields and the Ocarina do.
  WriteWord(memory, 0x0058795CU, 1U);
  for (const std::uint32_t refused : {0U, 6U}) {
    WriteWord(memory, 0x0050448CU, refused);
    update(1U, 1U);
    Require(aLanes() == 1U, "a child Gear hold started on a sword or tunic");
    update(0U);
  }
  for (const auto [held, action] : std::array<std::pair<std::uint32_t, TopScreenDpadAction>, 2>{
           {{3U, TopScreenDpadAction::ShieldToggle}, {18U, TopScreenDpadAction::Ocarina}}}) {
    WriteWord(memory, 0x0050448CU, held);
    update(1U, 1U);
    for (unsigned n = 0; n < 15; ++n) update(1U);
    Require(runtime.DpadPicker().Active() && runtime.DpadPicker().Action == action &&
                runtime.DpadPicker().Child,
            "child Gear hold did not open the shield or Ocarina picker");
    runtime.CancelDpadPicker();
    update(0U);
  }
  // An unowned slot (the Deku Shield, with the adult pair owned) refuses it.
  WriteWord(memory, 0x0058795CU, 0U);
  Require(memory.Write16(save + 0xB6U, 0x0767U), "cannot write equipment");
  WriteWord(memory, 0x0050448CU, 3U);
  update(1U, 1U);
  Require(aLanes() == 1U, "an unowned Gear slot took A");
  update(0U);
  WriteWord(memory, 0x00504484U, 0U);
  WriteWord(memory, 0x0050AF10U, 0U);
}

void TestShortcutRuntime(Oot3dNativeGame::NativeA32Memory &memory) {
  using namespace Oot3dNativeGame;
  TopScreenWideItemsRuntime runtime(memory);
  WriteWord(memory, kItemsState, 2);
  WriteWord(memory, 0x00506770U, 0);
  WriteWord(memory, 0x00506774U, 2);
  WriteWord(memory, 0x00506760U, 12);
  WriteWord(memory, 0x0050677CU, 0);
  WriteWord(memory, 0x00506758U, 0);
  WriteWord(memory, 0x0050AF30U, 0x20);
  WriteWord(memory, 0x0050AF2CU, 0x20);
  WriteWord(memory, 0x0050AF18U, 0);
  WriteWord(memory, 0x0050AF14U, 0);
  WriteWord(memory, 0x0050AF10U, 0);
  Require(memory.Write8(0x0053CC4BU, 7) && memory.Write8(0x005879EBU, 7), "ocarina fixture");
  auto runUpdate = [&] {
    auto state = CallerState(kTopScreenItemsUpdateBoundary);
    auto expected = state;
    expected.r[0] = state.r[4];
    expected.r[14] = kTopScreenItemsInsetNextReturn;
    oot3d::recomp::a32::ExecutionResult result;
    std::vector<std::uint32_t> entries;
    for (unsigned n = 0; n < 20; ++n) {
      Require(runtime.ExecuteItemsUpdate(state, &result, nullptr), "shortcut update not consumed");
      if (result.pc == kTopScreenItemsInsetNextCall) {
        Require(Same(state, expected), "shortcut native calls damaged caller/continuation");
        return entries;
      }
      entries.push_back(result.pc);
      Require(state.r[14] == kTopScreenItemsUpdateBoundary, "update returned mid-block");
      if (result.pc == 0x002F8D40U) {
        Require(state.r[1] == 12 && state.r[2] == 11 && state.r[3] == 103 &&
                    ReadWord(memory, state.r[13]) == 42 && ReadWord(memory, state.r[13] + 4) == 42,
                "entering shortcuts did not restore the enlarged native inventory icon");
      }
      state.r[0] = 0xBAD;
      state.vfp[0] = 0xBAD;
      state.r[15] = kTopScreenItemsUpdateBoundary;
    }
    Require(false, "shortcut update failed to finish");
    return entries;
  };
  const auto entries = runUpdate();
  Require(entries == std::vector<std::uint32_t>{0x002F8D40U, 0x002E9B00U, 0x002E9A1CU, 0x0037547CU} &&
              runtime.Shortcuts().SelectedRow == 2 && ReadWord(memory, 0x005067A4U) == 1 &&
              ReadWord(memory, 0x0050678CU) == 0 && ReadWord(memory, 0x00506790U) == 2 &&
              ReadWord(memory, 0x0050AF30U) == 0 && ReadWord(memory, 0x0050AF2CU) == 0,
          "native input words or selection history were not corrected on entry");
  Require(runtime.ShortcutDraw().Active && runtime.ShortcutDraw().Ocarina == 7,
          "native inventory did not reach shortcut presentation state");
  auto cursor = CallerState(kTopScreenItemsShortcutCursorBoundary);
  const auto caller = cursor;
  oot3d::recomp::a32::ExecutionResult result;
  Require(runtime.ExecuteShortcutCursor(cursor, &result, nullptr) && result.pc == 0x00446E98U &&
              cursor.r[14] == kTopScreenItemsShortcutCursorBoundary,
          "shortcut cursor skipped original update");
  cursor.r[0] = 0xBAD;
  Require(runtime.ExecuteShortcutCursor(cursor, &result, nullptr) && result.pc == 0x002F7AF4U &&
              cursor.r[0] == kIcon && std::bit_cast<float>(cursor.vfp[0]) == -39.0F &&
              std::bit_cast<float>(cursor.vfp[1]) == 101.0F,
          "shortcut cursor correction has wrong native owner or coordinates");
  Require(runtime.ExecuteShortcutCursor(cursor, &result, nullptr) && Same(cursor, caller) &&
              result.pc == kTopScreenItemsShortcutCursorResume,
          "shortcut cursor resumed mid-block or lost caller state");
  WriteWord(memory, 0x0050AF18U, 1);
  WriteWord(memory, 0x0050AF10U, 1);
  runUpdate();
  for (unsigned frame = 0; frame < 15; ++frame) {
    WriteWord(memory, 0x0050AF18U, 0);
    runUpdate();
  }
  Require(runtime.DpadPicker().Active() && runtime.DpadPicker().Action == TopScreenDpadAction::Ocarina,
          "native hold did not open the in-page picker");
  for (unsigned frame = 0; frame < 8; ++frame) runUpdate();
  WriteWord(memory, 0x0050AF10U, 2U);
  runUpdate();
  for (unsigned frame = 0; frame < 6; ++frame) runUpdate();
  Require(!runtime.DpadPicker().Active(), "B did not close the picker before reorder");
  unsigned orderChanges = 0;
  TopScreenItemsShortcutOrder changedOrder{};
  runtime.SetShortcutOrderChanged([&](const auto &order) { ++orderChanges; changedOrder = order; });
  WriteWord(memory, 0x0050AF10U, 0);
  runUpdate();
  const auto tap = [&] {
    WriteWord(memory, 0x0050AF18U, 1);
    WriteWord(memory, 0x0050AF10U, 1);
    runUpdate();
    WriteWord(memory, 0x0050AF18U, 0);
    WriteWord(memory, 0x0050AF10U, 0);
    runUpdate();
  };
  tap();
  WriteWord(memory, 0x0050AF30U, 0x80);
  runUpdate();
  tap();
  for (unsigned frame = 0; frame < 8; ++frame) runUpdate();
  Require(orderChanges == 1 && changedOrder == TopScreenItemsShortcutOrder{0, 1, 3, 2},
          "native reorder did not notify host config exactly once");
  runtime.SetShortcutOrder({3, 2, 1, 0});
  Require(runtime.Shortcuts().SelectedRow == 3 && runtime.Shortcuts().MovingRow == -1,
          "F1 order refresh dropped live column selection or retained a drag");
  runUpdate();
  Require(memory.Write16(0x00587A0EU, 0x2000U), "single boots fixture");
  runUpdate();
  Require(!runtime.ShortcutDraw().BootsOwned, "one boot type incorrectly enabled the cycle row");
  Require(memory.Write16(0x00587A0EU, 0x6000U) && memory.Write16(0x005879E2U, 0x2000U),
          "both boots fixture");
  WriteWord(memory, 0x0058795CU, 1U);
  runUpdate();
  Require(runtime.ShortcutDraw().BootsOwned && runtime.ShortcutDraw().Child &&
              runtime.ShortcutDraw().Boots == 0x45U &&
              !TopScreenItemsShortcutAvailable(TopScreenDpadAction::AllBootsToggle, runtime.ShortcutDraw()),
          "child boots row did not preserve visibility while disabling assignment");
  WriteWord(memory, 0x0050AF30U, 0x10);
  WriteWord(memory, 0x0050AF10U, 0);
  runUpdate();
  Require(runtime.Shortcuts().SelectedRow == -1 && ReadWord(memory, 0x00506770U) == 5 &&
              ReadWord(memory, 0x0050AF30U) == 0x10 && ReadWord(memory, 0x0050678CU) == 0,
          "exit did not leave the native right step to wrap into grid column zero");
  Require(!runtime.ExecuteShortcutCursor(cursor, &result, nullptr), "unselected native cursor was intercepted");
  runtime.SetShortcutOrder({3, 2, 1, 0});
  runtime.Reset();
  Require(runtime.Shortcuts().Order == TopScreenItemsShortcutOrder{3, 2, 1, 0} &&
              !runtime.ShortcutDraw().Active && runtime.Shortcuts().SelectedRow == -1,
          "savestate reset retained transient state or erased preferences");
  WriteWord(memory, 0x0050AF30U, 0);
}

int main() {
  using namespace Oot3dNativeGame;
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"wide-items-test", 0x004F0000, 0x00310000, true, false, {}}, &error),
          "cannot map wide Items fixture");
  BuildItemsPage(memory);
  TopScreenWideItemsRuntime runtime(memory);
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0U;
  auto page = CallerState(kTopScreenPageProjectionUploadCall);
  Require(!runtime.ExecutePageProjection(page, true) && !runtime.PageWide(), "inactive Items widened");
  WriteWord(memory, kItemsState, 2U);
  Require(!runtime.ExecutePageProjection(page, false) && !runtime.PageWide(), "lower-screen page widened");
  auto items = CallerState(kTopScreenItemsPageDrawCall);
  Require(!runtime.ExecuteItemsDraw(items, &result, &blocks), "lower Items draw intercepted");
  const auto caller = page;
  Require(!runtime.ExecutePageProjection(page, true) && Same(page, caller) && runtime.PageWide() &&
              ReadWord(memory, kMatrix + 0x10U) == 0xBBA3D70AU && ReadWord(memory, kMatrix + 0x1CU) == 0x3F4CCCCDU,
          "page upload changed registers or failed to patch -0.005 / 0.8");
  // The real constructor leaves a degenerate quad. Draw-time base writes do
  // not reach the selected stream: the update must precede native upload.
  TopScreenWideItemsRuntime beforeUpload(memory);
  Require(RunItems(beforeUpload, memory).size() == 18U, "zero-width native inset was drawn");
  RunItemsUpdate(runtime);
  Require(runtime.ItemsInsetUpdates() == 1U && ReadFloat(memory, kBasePositions + 0x60U) == -44.0F &&
              ReadFloat(memory, kPagePositions + 0x60U) == 0.0F &&
              RunItems(beforeUpload, memory).size() == 18U,
          "inset update bypassed native upload or draw used the base stream");
  UploadItemsStreams(memory);
  const auto before = Snapshot(memory);
  const auto draws = RunItems(runtime, memory);
  Require(before == Snapshot(memory), "Items draw changed native vertices, origins, transforms or ranges");
  Require(draws.size() == 19U, "unexpected number of native Items draws");
  constexpr std::array<std::uint32_t, 5> starts{0U, 2U, 6U, 12U, 28U};
  constexpr std::array<std::uint32_t, 5> counts{2U, 1U, 6U, 13U, 9U};
  constexpr std::array<float, 5> translationsX{18.0F, 215.0F, -70.0F, 10.0F, -70.0F};
  constexpr std::array<float, 5> translationsY{18.0F, 30.0F, 40.0F, 20.0F, 40.0F};
  for (std::size_t range = 0; range < starts.size(); ++range) {
    const auto &draw = draws[range];
    Require(draw.Entry == kModelDraw && draw.Object == kPageModel && draw.Count == counts[range] * 6U &&
                draw.Indices == kPageIndices + starts[range] * 12U && draw.Projection == 0.6F &&
                draw.Transform[2] == translationsX[range] && draw.Transform[5] == translationsY[range] &&
                draw.Transform[0] == 2.0F && draw.Transform[4] == 3.0F,
            "3.0 page sub-range or temporary transform is wrong");
  }
  for (std::uint32_t vertex = 0; vertex < 4U; ++vertex) {
    Require(draws[1].InsetPositions[vertex * 3U] == -44.0F + (vertex % 2U) * 100.0F &&
                draws[1].InsetPositions[vertex * 3U + 1U] == (vertex / 2U) * 200.0F &&
                draws[1].InsetPositions[vertex * 3U + 2U] == 0.0F &&
                draws[1].InsetTexcoords[vertex * 2U] == (-1.0F + (vertex % 2U) * 100.0F) / 512.0F &&
                draws[1].InsetTexcoords[vertex * 2U + 1U] == 1.0F - (vertex / 2U) * 200.0F / 256.0F,
            "native zero-width inset was not prepared with the 3.0 rectangle and UVs");
  }
  for (std::size_t range = 0; range < 4U; ++range) {
    const auto &draw = draws[5U + range];
    Require(draw.Object == kGroupModel && draw.Count == 6U && draw.Indices == kGroupIndices + range * 12U &&
                draw.Transform[2] == (range % 2U == 0U ? 15.0F : 7.0F),
            "assignment ranges do not split at X 256");
  }
  Require(draws[9].Entry == 0x002F78A0U && draws[9].Object == kFirstSlot && draws[9].Transform[2] == 15.0F &&
              draws[10].Object == kFirstSlot + 0x500U && draws[10].Transform[2] == 7.0F,
          "assignment slots lost their quad-side decision or drew hidden slots");
  Require(draws[11].Object == kIcon && draws[11].Projection == 0.58F &&
              draws[12].Object == kRightIcon && draws[12].Projection == 0.6F &&
              draws[17].Object == kCursor && draws[17].Projection == 0.58F &&
              draws[18].Object == kHeap + 0xA800U && draws[18].Projection == 0.6F,
          "icon groups, cursor sentinel scan or native tail projection is wrong");
  Require(draws[13].Object == kHeap + 0xA000U && draws[14].Entry == 0x00441FECU &&
              draws[15].Entry == 0x002F780CU && draws[15].Object == kHeap + 0xA200U &&
              draws[16].Entry == 0x002F780CU && draws[16].Object == kHeap + 0xA300U,
          "native tail draw order changed");
  Require(runtime.WideItemsDraws() == 1U && runtime.CursorDraws() == 2U, "draw counters are wrong");
  auto outside = CallerState(kTopScreenItemsCursorDrawCallA);
  Require(!runtime.ExecuteCursorDraw(kTopScreenItemsCursorDrawCallA, outside, &result, &blocks),
          "legacy cursor site changed an unrelated draw");
  const auto compact = TransformTopScreenItemsModel({1, 0, 10, 0, 1, 20}, 4.0F, 0.0F, true);
  Require(std::abs(compact[0] - 0.85F) < 0.0001F && std::abs(compact[2] - 47.9F) < 0.0001F && compact[4] == 1.0F,
          "compact 0.85 transform contract differs from 0x005CAFCC");
  const auto stream = kPagePositions;
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
  WriteWord(memory, kItemsState, 2U);
  WriteWord(memory, kSystemState, 0U);
  WriteWord(memory, kMapState, 0U);
  const auto countDraws = [](const auto &trace, std::uint32_t object) {
    return std::count_if(trace.begin(), trace.end(), [=](const auto &draw) { return draw.Object == object; });
  };
  const auto stableModels = Snapshot(memory);
  Require(RunItems(runtime, memory).size() == 19U && Snapshot(memory) == stableModels,
          "repeated draws accumulated model offsets");
  WriteWord(memory, 0x00506758U, 1U);
  Require(RunItems(runtime, memory).size() == 5U, "hidden page content drew icons or native tail");
  WriteWord(memory, 0x00506758U, 0U);
  WriteWord(memory, 0x00506714U, 0U);
  Require(countDraws(RunItems(runtime, memory), kPageModel) == 4, "missing inset instance was drawn");
  WriteWord(memory, 0x00506714U, kPageInstance);
  WriteWord(memory, kPageInstance + 0x128U, 1U);
  const auto nonIndexed = RunItems(runtime, memory);
  Require(countDraws(nonIndexed, kPageModel) == 1 && nonIndexed.front().Count == 37U * 6U &&
              nonIndexed.front().Indices == kPageIndices,
          "non-indexed model fallback did not draw only its first range");
  WriteWord(memory, kPageInstance + 0x128U, 2U);
  WriteWord(memory, kPageModel + 0x174U, 2U * 6U);
  const auto shortModel = Snapshot(memory);
  Require(countDraws(RunItems(runtime, memory), kPageModel) == 1 &&
              ReadWord(memory, kPageModel + 0x174U) == 12U && Snapshot(memory) == shortModel,
          "skipped out-of-range inset leaked temporary model state");
  WriteWord(memory, kPageModel + 0x174U, 37U * 6U);
  WriteWord(memory, 0x00506718U, 0U);
  Require(countDraws(RunItems(runtime, memory), kPageModel) == 0, "missing page model was called");
  WriteWord(memory, 0x00506718U, kPageModel);

  // Assignment visibility is sampled once per native update key, not presentation draw.
  TopScreenWideItemsRuntime delayed(memory);
  WriteWord(memory, 0x0050677CU, 2U);
  WriteWord(memory, kFrameOwner + 0xF8U, 10U);
  Require(countDraws(RunItems(delayed, memory), kGroupModel) == 0 && ReadWord(memory, 0x0050677CU) == 1U,
          "assignment countdown did not hide the group");
  Require(countDraws(RunItems(delayed, memory), kGroupModel) == 0 && ReadWord(memory, 0x0050677CU) == 1U,
          "presentation redraw accelerated assignment countdown");
  WriteWord(memory, kFrameOwner + 0xF8U, 11U);
  Require(countDraws(RunItems(delayed, memory), kGroupModel) == 0 && ReadWord(memory, 0x0050677CU) == 0U &&
              delayed.ShortcutDraw().Active,
          "shortcut visibility did not observe the decremented native countdown");
  WriteWord(memory, kFrameOwner + 0xF8U, 12U);
  Require(countDraws(RunItems(delayed, memory), kGroupModel) == 4, "assignment group did not reappear");
  WriteWord(memory, kGroup + 0x428U, 7U);
  Require(countDraws(RunItems(delayed, memory), kGroup) == 1, "type-7 assignment fallback was not native");
  WriteWord(memory, kGroup + 0x428U, 2U);
  Require(countDraws(RunItems(delayed, memory), kFirstSlot) == 0, "assignment kind >1 drew slot overlays");
  WriteWord(memory, kGroup + 0x428U, 0U);

  // Owner position wins below 390. Otherwise stop at the first quad below
  // the sentinel, including a negative X (do not skip to a later visible quad).
  struct CursorCase { float Owner, First, Second, Projection; };
  constexpr std::array cursorCases{
      CursorCase{255.0F, 390.0F, 390.0F, 0.58F},
      CursorCase{256.0F, 100.0F, 100.0F, 0.6F},
      CursorCase{390.0F, 390.0F, 100.0F, 0.58F},
      CursorCase{-1.0F, 390.0F, 256.0F, 0.6F},
      CursorCase{390.0F, -1.0F, 100.0F, 0.6F},
      CursorCase{390.0F, 390.0F, 390.0F, 0.6F}};
  for (const auto &test : cursorCases) {
    WriteFloat(memory, kCursor + 0x18U, test.Owner);
    WriteFloat(memory, kCursor + 0x1000U, test.First);
    WriteFloat(memory, kCursor + 0x1030U, test.Second);
    const auto trace = RunItems(delayed, memory);
    const auto found = std::find_if(trace.begin(), trace.end(),
                                    [](const auto &draw) { return draw.Object == kCursor; });
    Require(found != trace.end() && found->Projection == test.Projection, "cursor projection boundary is wrong");
  }
  WriteWord(memory, 0x0055B62CU, 0U);
  Require(countDraws(RunItems(delayed, memory), kCursor) == 0, "inactive assignment cursor was drawn");
  WriteWord(memory, 0x0055B630U, 1U);
  Require(countDraws(RunItems(delayed, memory), kCursor) == 1, "second assignment cursor gate was lost");
  WriteWord(memory, 0x0055B630U, 0U);
  WriteWord(memory, 0x0055B634U, 1U);
  Require(countDraws(RunItems(delayed, memory), kCursor) == 1, "third assignment cursor gate was lost");
  WriteWord(memory, 0x00506724U, 0U);
  WriteWord(memory, 0x005067A8U, 0U);
  WriteWord(memory, 0x005067ACU, 0U);
  WriteWord(memory, 0x00506730U, 1U);
  const auto missingGroups = RunItems(delayed, memory);
  Require(countDraws(missingGroups, kCursor) == 0 && countDraws(missingGroups, kIcon) == 0 &&
              countDraws(missingGroups, kHeap + 0xA800U) == 0, "missing cursor or inactive tail was drawn");
  BuildItemsPage(memory);

  RunItemsUpdate(runtime);
  UploadItemsStreams(memory);
  // Reset undoes the eight draw-time words; the update survives until the next update.
  auto armed = CallerState(kTopScreenPageProjectionUploadCall);
  Require(!runtime.ExecutePageProjection(armed, true), "cannot arm reset fixture");
  auto inFlight = CallerState(kTopScreenItemsPageDrawCall);
  const auto resetSnapshot = Snapshot(memory);
  for (std::size_t step = 0; step < 6U; ++step) {
    Require(runtime.ExecuteItemsDraw(inFlight, &result, &blocks), "cannot enter range draw");
    inFlight.r[15] = kTopScreenItemsPageDrawCall;
  }
  Require(result.pc == kModelDraw && ReadWord(memory, kPageModel + 0x174U) == 6U &&
              ReadFloat(memory, kPagePositions + 0x60U) == -44.0F,
          "reset fixture has no temporary inset model state");
  runtime.Reset();
  Require(Snapshot(memory) == resetSnapshot && !runtime.PageWide(), "reset leaked temporary model writes");

  // A failure after range writes restores them and clears the pending sequence.
  WriteWord(memory, kHeap + 0x300CU, 0x005E3728U);
  const auto failedSnapshot = Snapshot(memory);
  auto failurePage = CallerState(kTopScreenPageProjectionUploadCall);
  Require(!runtime.ExecutePageProjection(failurePage, true), "cannot arm failure fixture");
  auto failureState = CallerState(kTopScreenItemsPageDrawCall);
  const auto failureCaller = failureState;
  failed = false;
  try {
    for (std::size_t step = 0; step < 5U; ++step) {
      runtime.ExecuteItemsDraw(failureState, &result, &blocks);
      failureState.r[15] = kTopScreenItemsPageDrawCall;
    }
  } catch (const std::runtime_error &) { failed = true; }
  Require(failed && Same(failureState, failureCaller) && Snapshot(memory) == failedSnapshot &&
              !runtime.ExecuteItemsDraw(failureState, &result, &blocks),
          "failed native draw entry leaked range state or pending calls");
  WriteWord(memory, kHeap + 0x300CU, kModelDraw);
  auto badStackPage = CallerState(kTopScreenPageProjectionUploadCall);
  Require(!runtime.ExecutePageProjection(badStackPage, true), "cannot arm bad-stack fixture");
  auto badStack = CallerState(kTopScreenItemsPageDrawCall);
  badStack.r[13] = 0x10U;
  const auto badStackCaller = badStack;
  failed = false;
  try { runtime.ExecuteItemsDraw(badStack, &result, &blocks); }
  catch (const std::runtime_error &) { failed = true; }
  Require(failed && Same(badStack, badStackCaller) && !runtime.ExecuteItemsDraw(badStack, &result, &blocks),
          "invalid caller stack modified state or left a pending sequence");

  // Update validates both base streams before making any persistent writes.
  WriteFloat(memory, kPageBuffer + 4U, 0.0F);
  const auto malformedInset = Snapshot(memory);
  failed = false;
  try { RunItemsUpdate(runtime); }
  catch (const std::runtime_error &) { failed = true; }
  Require(failed && Snapshot(memory) == malformedInset,
          "invalid inset dimensions changed model or stream state");
  WriteFloat(memory, kPageBuffer + 4U, 512.0F);
  const auto updatedCount = runtime.ItemsInsetUpdates();
  for (const auto buffer : {0U, kPageBuffer + 1U}) {
    WriteWord(memory, 0x0050671CU, buffer);
    const auto unchanged = Snapshot(memory);
    RunItemsUpdate(runtime);
    Require(Snapshot(memory) == unchanged && runtime.ItemsInsetUpdates() == updatedCount,
            "missing or unaligned native buffer was updated");
  }
  WriteWord(memory, 0x0050671CU, kPageBuffer);
  WriteWord(memory, kPageBuffer, 2U);
  const auto shortBuffer = Snapshot(memory);
  RunItemsUpdate(runtime);
  Require(Snapshot(memory) == shortBuffer && runtime.ItemsInsetUpdates() == updatedCount,
          "short native buffer was updated");
  WriteWord(memory, kPageBuffer, 37U);
  WriteWord(memory, kItemsState, 0U);
  RunItemsUpdate(runtime);
  for (std::uint32_t vertex = 0; vertex < 4U; ++vertex)
    Require(ReadFloat(memory, kBasePositions + 0x60U + vertex * 12U) == -44.0F &&
                ReadFloat(memory, kBasePositions + 0x64U + vertex * 12U) == 0.0F,
            "inactive Items update retained a visible inset");

  WriteWord(memory, kItemsState, 0U);
  WriteWord(memory, 0x00504484U, 2U);
  WriteWord(memory, 0x00504470U, kGearModel);
  BuildModel(memory, kGearModel, kGearModel + 0x200U, kGearPositions, kGearIndices, 41U);
  WriteTransform(memory, kGearModel, kPageTransform);
  const auto beforeGear = Snapshot(memory);
  const auto gearDraws = RunGear(runtime, memory);
  Require(gearDraws.size() == 3U && runtime.GearModelDraws() == 1U && Snapshot(memory) == beforeGear,
          "Gear draws changed geometry or failed to restore index ranges");
  constexpr std::array<std::uint32_t, 3> gearStarts{0U, 28U, 32U};
  constexpr std::array<std::uint32_t, 3> gearCounts{25U, 2U, 9U};
  for (std::size_t range = 0; range < gearDraws.size(); ++range)
    Require(gearDraws[range].Count == gearCounts[range] * 6U &&
                gearDraws[range].Indices == kGearIndices + gearStarts[range] * 12U &&
                gearDraws[range].Transform == kPageTransform,
            "Gear BACK quads were retained or other Gear quads were omitted");
  auto gearPage = CallerState(kTopScreenPageProjectionUploadCall);
  auto gearCall = CallerState(kTopScreenGearModelDrawCall);
  gearCall.r[4] = kGearOwner;
  const auto gearCaller = gearCall;
  Require(!runtime.ExecutePageProjection(gearPage, false) &&
              !runtime.ExecuteGearModelDraw(gearCall, &result, &blocks) && Same(gearCall, gearCaller),
          "lower Gear draw was intercepted");
  Require(!runtime.ExecutePageProjection(gearPage, true), "cannot arm Gear reset fixture");
  Require(runtime.ExecuteGearModelDraw(gearCall, &result, &blocks) &&
              ReadWord(memory, kGearModel + 0x174U) == 25U * 6U,
          "Gear reset fixture has no pending range");
  runtime.Reset();
  Require(Snapshot(memory) == beforeGear &&
              !runtime.ExecuteGearModelDraw(gearCall, &result, &blocks),
          "Gear reset retained temporary writes or an armed draw");
  WriteWord(memory, 0x00504484U, 0U);
  Require(!runtime.ExecutePageProjection(gearPage, true) &&
              !runtime.ExecuteGearModelDraw(gearCall, &result, &blocks),
          "inactive Gear draw was intercepted");

  TestShortcutPolicy();
  TestDpadPickerPolicy();
  TestShortcutRuntime(memory);
  TestDpadPickerRuntime(memory);
  TestShortcutInputOwnership(memory);
  TestViewModel(memory);
  TestPickerViewModel(memory);
  TestGearHold(memory);
  std::cout << "SingleScreen 3.0 wide page runtime tests passed\n";
  return 0;
}
