#include "oot3d_object_bank_extension.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {
using namespace Oot3dNativeGame;
using GuestState = oot3d::recomp::a32::GuestState;
using ExecutionResult = oot3d::recomp::a32::ExecutionResult;
constexpr std::uint32_t kBase = kObjectBankExtensionBase;
constexpr std::uint32_t kPlay = 0x01000000U;
constexpr std::uint32_t kContext = kPlay + 0x3A58U;
constexpr std::uint32_t kModel = kPlay + 0x9000U;
constexpr std::uint32_t kTable = kPlay + 0xA000U;
constexpr std::uint32_t kActor = kPlay + 0xB000U;
constexpr std::uint32_t kStack = 0x02008000U;
constexpr std::uint32_t kResolve = 0x0033AB24U;
constexpr std::uint32_t kSweep = 0x002E4FC8U;
constexpr std::uint32_t kDestroy = 0x0044E778U;
constexpr std::uint32_t kRoomReplace = 0x003A75F0U;
constexpr std::uint32_t kReleaseMain = 0x00452430U;
constexpr std::uint32_t kDestroyFileRequest = 0x0031B99CU;
constexpr std::uint32_t kDestroyArchive = 0x002F70C4U;
constexpr std::uint32_t kFree = 0x0034FC6CU;
constexpr std::uint32_t kNativeSlot = kPlay + 0x3A5CU;

void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

template <typename F> void RequireThrows(F action, const char *message) {
  bool threw = false;
  try { action(); } catch (const std::runtime_error &) { threw = true; }
  Require(threw, message);
}

bool Same(const GuestState &left, const GuestState &right) {
  return left.r == right.r && left.vfp == right.vfp && left.cpsr == right.cpsr &&
      left.fpscr == right.fpscr && left.thread_pointer == right.thread_pointer &&
      left.exclusive_address == right.exclusive_address &&
      left.exclusive_token == right.exclusive_token &&
      left.exclusive_size == right.exclusive_size && left.exclusive_valid == right.exclusive_valid;
}

GuestState Caller(std::uint32_t pc) {
  GuestState state;
  for (unsigned i = 0; i < state.r.size(); ++i) state.r[i] = 0xAB000000U + i;
  for (unsigned i = 0; i < state.vfp.size(); ++i) state.vfp[i] = 0xCD000000U + i;
  state.r[13] = kStack;
  state.r[14] = 0x00300000U;
  state.r[15] = pc;
  state.cpsr = 0xA0000010U;
  state.fpscr = 0x03C00010U;
  state.thread_pointer = 0x1FF82000U;
  state.exclusive_address = kPlay;
  state.exclusive_token = 0x123456789ABCDEF0ULL;
  state.exclusive_size = 4U;
  state.exclusive_valid = true;
  return state;
}

void Returned(GuestState &state, const GuestState &caller, std::uint32_t result = 0U) {
  state = Caller(caller.r[15]);
  state.r.fill(0xEEEEEEEEU);
  state.vfp.fill(0xDDDDDDDDU);
  state.r[0] = result;
  state.r[13] = caller.r[13] - 0x20U;
  state.r[15] = caller.r[15];
  state.thread_pointer = caller.thread_pointer;
  state.cpsr = state.fpscr = 0U;
  state.exclusive_valid = false;
}

struct Fixture {
  NativeA32Memory Memory;
  ObjectBankExtension Runtime{Memory};
  ExecutionResult Result{};
  std::uint32_t Blocks = 0;
  Fixture() {
    std::string error;
    Require(Memory.MapRegion({"globals", 0x00504000U, 0x1000U, true, false, {}}, &error),
            "cannot map globals");
    Require(Memory.MapRegion({"play", kPlay, 0x20000U, true, false, {}}, &error),
            "cannot map play");
    Require(Memory.MapRegion({"stack", 0x02000000U, 0x10000U, true, false, {}}, &error),
            "cannot map stack");
    W32(0x005043E0U, kPlay);
    W32(kModel + 4U, kTable);
    Runtime.SetEnabled(true);
  }
  void W8(std::uint32_t address, std::uint8_t value) {
    Require(Memory.Write8(address, value), "fixture byte write failed");
  }
  void W16(std::uint32_t address, std::uint16_t value) {
    Require(Memory.Write16(address, value), "fixture halfword write failed");
  }
  void W32(std::uint32_t address, std::uint32_t value) {
    Require(Memory.Write32(address, value), "fixture word write failed");
  }
  std::uint8_t R8(std::uint32_t address) {
    std::uint8_t value = 0;
    Require(Memory.Read8(address, &value), "fixture byte read failed");
    return value;
  }
  std::uint16_t R16(std::uint32_t address) {
    std::uint16_t value = 0;
    Require(Memory.Read16(address, &value), "fixture halfword read failed");
    return value;
  }
  std::uint32_t R32(std::uint32_t address) {
    std::uint32_t value = 0;
    Require(Memory.Read32(address, &value), "fixture word read failed");
    return value;
  }
  void Bank() {
    std::string error;
    Require(Runtime.EnsureRegion(&error), "cannot map extension");
  }
  static std::uint32_t Slot(std::uint32_t i) { return kBase + 4U + i * 0x80U; }
  void Seed(std::uint32_t i, std::uint16_t id, std::uint32_t size = 0x200U) {
    W16(Slot(i), id);
    W32(Slot(i) + 4U, kPlay + 0x10000U + i * 0x200U);
    W32(Slot(i) + 8U, size);
    W32(Slot(i) + 0xCU, 0U);
  }
  bool Execute(std::uint32_t pc, GuestState &state) {
    Blocks = 99U;
    const auto handled = Runtime.Execute(pc, state, &Result, &Blocks);
    if (handled) {
      Require(Blocks == 1U, "handled hook did not consume one block");
      Require(Result.kind == oot3d::recomp::a32::ExitKind::Branch,
              "handled hook did not branch");
    }
    return handled;
  }
  GuestState Resolve(std::uint32_t id) {
    W32(kTable, id);
    auto state = Caller(kResolve);
    state.r[4] = kModel;
    state.r[5] = 0U;
    return state;
  }
  GuestState SweepCaller() {
    auto state = Caller(kSweep);
    state.r[8] = kContext;
    for (unsigned i = 0; i < 9; ++i) W32(kStack + 0x214U + i * 4U, 0x00310000U + i * 4U);
    return state;
  }
  std::vector<std::pair<std::uint32_t, std::uint32_t>> Sweep() {
    auto state = SweepCaller();
    const auto caller = state;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> calls;
    const bool handled = Execute(kSweep, state);
    while (Runtime.Pending()) {
      calls.emplace_back(state.r[15], state.r[0]);
      Returned(state, caller);
      Require(Execute(kSweep, state), "sweep continuation did not run");
    }
    if (handled) {
      Require(state.r[15] == 0x00310020U && state.r[13] == kStack + 0x238U,
              "Object_UpdateBank tail frame was not restored");
      Require(state.r[4] == 0x00310000U && state.r[11] == 0x0031001CU &&
                  state.vfp == caller.vfp && state.cpsr == caller.cpsr &&
                  state.fpscr == caller.fpscr && state.exclusive_token == caller.exclusive_token,
              "sweep changed caller registers beyond the native epilogue");
    }
    return calls;
  }
};

void LookupAndDisabled() {
  NativeA32Memory empty;
  ObjectBankExtension disabled(empty);
  auto state = Caller(0x00164BC8U);
  const auto original = state;
  ExecutionResult result{};
  std::uint32_t blocks = 123U;
  for (const auto pc : ObjectBankExtension::HookPcs()) {
    // The 3.1 lifetime fixes always look, and leave a foreign frame native.
    const bool lifetime = pc == kRoomReplace || pc == kDestroy;
    Require((lifetime || !disabled.ShouldObserve(pc)) &&
                !disabled.Execute(pc, state, &result, &blocks),
            "disabled empty bank intercepted a hook");
  }
  Require(empty.RegionCount() == 0U && blocks == 123U && Same(state, original),
          "disabled path touched memory, registers, or block accounting");
  Require(ObjectBankExtension::SlotAddress(kPlay, -1) == 0U &&
              ObjectBankExtension::SlotAddress(kPlay, 0) == kPlay + 0x3A5CU &&
              ObjectBankExtension::SlotAddress(kPlay, 18) == kPlay + 0x435CU &&
              ObjectBankExtension::SlotAddress(kPlay, 19) == kBase + 4U &&
              ObjectBankExtension::SlotAddress(kPlay, 38) == kBase + 0x984U &&
              ObjectBankExtension::SlotAddress(kPlay, 39) == 0U &&
              ObjectBankExtension::SlotAddress(0xFFFFF000U, 18) == 0U &&
              ObjectBankExtension::SlotAddress(0U, 0) == 0U,
          "lookup range or record-base contract differs from 0x0060629C");

  Fixture f;
  constexpr std::array sites{
      0x00164BC8U, 0x00167DA4U, 0x00167E38U, 0x00168B20U, 0x0018D7ACU,
      0x001B3130U, 0x001B3208U, 0x001BBC9CU, 0x001F85B0U, 0x00215A60U,
      0x002293ACU, 0x0022AB70U, 0x0022DEC4U, 0x00339DFCU, 0x0033A950U,
      0x0034FE68U, 0x00352F28U, 0x00353CE4U, 0x0036A96CU, 0x00372F7CU,
      0x0034F270U};
  for (auto pc : sites) {
    state = Caller(pc);
    state.r[0] = 38U;
    auto expected = state;
    expected.r[0] = kBase + 0x984U;
    expected.r[15] = pc + 4U;
    expected.cpsr = 0x60000010U; // CMP 38,38
    if (pc != 0x0034F270U) expected.r[14] = pc + 4U;
    Require(f.Execute(pc, state) && Same(state, expected), "slot wrapper lost its register ABI");
  }
  Require(!f.Memory.IsExecutable(kBase, kObjectBankExtensionSize), "object region is executable");
  const auto hooks = ObjectBankExtension::HookPcs();
  Require(hooks.size() == 34U && std::is_sorted(hooks.begin(), hooks.end()) &&
              std::adjacent_find(hooks.begin(), hooks.end()) == hooks.end(),
          "hook list is not complete/sorted/unique");
  Require(!f.Runtime.ShouldObserve(0x006063A4U), "payload pc was exposed as a runtime hook");
  f.Runtime.SetEnabled(false);
  const auto generation = f.Memory.WriteGeneration();
  state = Caller(0x00164BC8U);
  Require(!f.Execute(state.r[15], state) && f.Memory.WriteGeneration() == generation,
          "disabled empty mapped bank was not inert");
}

void MappingCollisions() {
  for (const auto offset : {0U, 0x800U}) {
    NativeA32Memory memory;
    ObjectBankExtension runtime(memory);
    std::string error;
    Require(memory.MapRegion({"occupied", kBase + offset, 0x800U, true, false, {}}, &error),
            "cannot seed mapping collision");
    Require(memory.Write32(kBase + offset, 0x12345678U), "cannot seed collision sentinel");
    const auto generation = memory.WriteGeneration();
    Require(!runtime.EnsureRegion(&error) && !error.empty() &&
                memory.WriteGeneration() == generation && memory.RegionCount() == 1U,
            "object bank accepted or overwrote an unrelated/partial mapping");
  }
  Fixture f;
  f.Bank();
  f.Seed(0, 77U);
  const auto generation = f.Memory.WriteGeneration();
  Require(f.Runtime.EnsureRegion() && f.R16(kBase + 4U) == 77U &&
              generation == f.Memory.WriteGeneration(), "mapping check reset existing slots");
}

void IndexAndPredicates() {
  Fixture f;
  f.Bank();
  f.W8(kBase, 2U);
  f.Seed(0, static_cast<std::uint16_t>(-77));
  f.Seed(1, 88U);
  f.W8(kBase + 0xA04U, 3U);
  auto state = Caller(0x00363CACU);
  state.r[1] = 77U;
  f.W32(kStack, 0x12345678U);
  const auto expectedLr = state.r[14];
  Require(f.Execute(0x00363CACU, state) && state.r[0] == 19U &&
              state.r[4] == 0x12345678U && state.r[13] == kStack + 4U &&
              state.r[15] == expectedLr && f.R8(kBase + 0xA04U) == 0U,
          "extended index lookup did not match negative ID/cancel retirement/pop r4");
  state = Caller(0x00363CACU);
  state.r[1] = 99U;
  Require(f.Execute(0x00363CACU, state) && state.r[0] == UINT32_MAX,
          "missing object did not return -1");
  for (const auto id : {std::uint16_t{0}, std::uint16_t{88}, std::uint16_t{0xFFA8}}) {
    f.W16(Fixture::Slot(1), id);
    state = Caller(0x00373074U);
    state.r[0] = kContext;
    state.r[1] = 20U;
    Require(f.Execute(0x00373074U, state) && state.r[0] == (id == 88U ? 1U : 0U),
            "loaded predicate did not require a positive ID");
    state = Caller(0x004849F0U);
    state.r[4] = kActor;
    state.r[7] = kContext;
    f.W8(kActor + 0x1EU, 20U);
    Require(f.Execute(0x004849F0U, state) &&
                state.r[0] == (id == 0xFFA8U ? 0U : 1U) &&
                state.r[1] == 20U && state.r[15] == 0x004849FCU &&
                state.r[14] == 0x004849FCU,
            "readiness block did not emulate leading instructions/zero-ready distinction");
  }
}

void AllocationAndCapacity() {
  Fixture f;
  f.W8(kContext, 1U);
  f.W16(kContext + 4U, 77U);
  auto state = f.Resolve(77U);
  Require(f.Execute(kResolve, state) && state.r[0] == kContext + 4U &&
              !f.Runtime.Pending() && !f.Memory.IsMapped(kBase, 1U),
          "existing native object was unnecessarily loaded into the extra bank");
  f.Bank();
  f.W8(kBase, 19U);
  for (std::uint32_t i = 0; i < 19U; ++i) f.Seed(i, static_cast<std::uint16_t>(100U + i));
  state = f.Resolve(150U);
  const auto caller = state;
  Require(f.Execute(kResolve, state) && f.Runtime.Pending() && state.r[15] == 0x0032E21CU &&
              state.r[0] == kBase && state.r[1] == 150U && state.r[2] == kPlay &&
              state.r[13] == kStack - 0x20U && state.r[14] == kResolve,
          "twentieth extra load did not use native Object_Spawn with re-entry");
  Require(!f.Execute(0x0032E21CU, state), "twentieth extra slot was rejected by nineteen-slot guard");
  f.Seed(19, 150U);
  f.W8(kBase, 20U);
  Returned(state, caller, 19U);
  Require(f.Execute(kResolve, state) && !f.Runtime.Pending(), "native spawn did not finish");
  auto expected = caller;
  expected.r[0] = Fixture::Slot(19);
  expected.r[14] = expected.r[15] = 0x0033AB28U;
  Require(Same(state, expected) && f.Runtime.Stats().Loads == 1U,
          "load completion failed to restore full caller state");

  state = f.Resolve(150U);
  f.W8(kBase + 0xA04U + 19U, 3U);
  Require(f.Execute(kResolve, state) && !f.Runtime.Pending() &&
              state.r[0] == Fixture::Slot(19) && f.R8(kBase + 0xA04U + 19U) == 0U,
          "duplicate request allocated again or left its retirement flag");
  state = f.Resolve(151U);
  Require(f.Execute(kResolve, state) && !f.Runtime.Pending() && state.r[15] == 0x0033AB54U &&
              state.r[0] == 0U && f.R8(kBase) == 20U,
          "full bank did not return through native loader failure");
  state = Caller(0x0032E21CU);
  state.r[0] = kBase;
  Require(f.Execute(0x0032E21CU, state) && state.r[0] == UINT32_MAX &&
              state.r[15] == 0x00300000U, "full extra spawn did not return -1");

  f.W8(kContext, 19U);
  state = Caller(0x0032E21CU);
  state.r[0] = kContext;
  Require(f.Execute(0x0032E21CU, state) && state.r[0] == UINT32_MAX,
          "native bank overflow was accepted");
  f.Runtime.SetEnabled(false);
  state = f.Resolve(152U);
  Require(f.Execute(kResolve, state) && !f.Runtime.Pending() && state.r[15] == 0x0033AB54U,
          "disabled extension started a new allocation");
  state = Caller(0x00164BC8U);
  state.r[0] = 38U;
  Require(f.Execute(0x00164BC8U, state) && state.r[0] == Fixture::Slot(19),
          "disabling broke an already-live extra actor");
}

void AllocationFailure() {
  Fixture f;
  auto state = f.Resolve(123U);
  const auto caller = state;
  Require(f.Execute(kResolve, state), "allocation failure fixture did not start");
  f.Seed(0, 123U);
  f.W32(Fixture::Slot(0) + 4U, 0U);
  // Native Object_Spawn has pushed six registers and reserved 0x210 bytes.
  auto allocationReturn = Caller(0x0032E274U);
  allocationReturn.r[0] = 0U;
  allocationReturn.r[4] = kBase;
  allocationReturn.r[13] = kStack - 0x20U - 0x18U - 0x210U;
  for (unsigned reg = 4; reg <= 8; ++reg)
    f.W32(allocationReturn.r[13] + 0x210U + (reg - 4U) * 4U, caller.r[reg]);
  f.W32(allocationReturn.r[13] + 0x224U, kResolve);
  Require(f.Execute(0x0032E274U, allocationReturn) && allocationReturn.r[0] == UINT32_MAX &&
              allocationReturn.r[13] == kStack - 0x20U && allocationReturn.r[15] == kResolve &&
              allocationReturn.r[4] == caller.r[4] && f.R16(Fixture::Slot(0)) == 0U &&
              f.R32(Fixture::Slot(0) + 8U) == 0U && f.R8(kBase) == 0U,
          "native allocator failure did not clear slot/unwind its exact frame");
  Require(f.Execute(kResolve, allocationReturn) && !f.Runtime.Pending() &&
              allocationReturn.r[15] == 0x0033AB54U && allocationReturn.r[0] == 0U,
          "OOM exposed a pre-bank/null archive to the model loader");
  auto expected = caller;
  expected.r[0] = 0U;
  expected.r[15] = 0x0033AB54U;
  Require(Same(allocationReturn, expected) && f.Runtime.Stats().LoadFailures == 1U,
          "OOM failed to restore the outer caller");

  state = f.Resolve(124U);
  Require(f.Execute(kResolve, state), "second load did not start after failure");
  auto wrong = state;
  wrong.r[15] = kResolve;
  wrong.thread_pointer += 4U;
  RequireThrows([&] { f.Execute(kResolve, wrong); }, "foreign thread consumed a continuation");
  Require(f.Runtime.Pending(), "foreign re-entry discarded pending load");
}

void RoomLifetime() {
  Fixture f;
  f.Bank();
  f.W8(kBase, 2U);
  f.Seed(0, 100U);
  f.Seed(1, 101U);
  f.W8(kPlay + 0x101U, 1U);
  auto state = Caller(0x003A7700U);
  state.r[8] = kPlay + 0x3000U;
  state.r[9] = kPlay;
  state.r[10] = 7U;
  for (unsigned i = 0; i < 9U; ++i) f.W32(kStack + 0x1CU + i * 4U, 0x1000U + i);
  Require(f.Execute(0x003A7700U, state) && f.R8(kBase + 1U) == 2U &&
              f.R8(kContext) == 7U && state.r[0] == 1U && state.r[15] == 0x1008U &&
              state.r[13] == kStack + 0x40U && state.r[4] == 0x1000U,
          "scene phase 1 did not freeze persistent boundary/emulate native room epilogue");
  f.W8(kBase, 4U);
  f.Seed(2, 102U);
  f.Seed(3, 103U);
  f.W8(kPlay + 0x101U, 2U);
  state = Caller(0x003A7698U);
  state.r[9] = kPlay;
  f.W32(kStack + 0x18U, kTable);
  f.W8(kTable + 1U, 4U);
  f.W32(kStack + 4U, 3U);
  Require(f.Execute(0x003A7698U, state) && state.r[15] == 0x003A76ACU &&
              state.r[0] == 3U && state.r[1] == 4U &&
              f.R8(kBase + 0xA04U) == 0U && f.R8(kBase + 0xA06U) == 1U &&
              f.R8(kBase + 0xA07U) == 1U && f.R32(kBase + 0xA1CU) == kPlay,
          "room marking or leading instruction emulation is wrong");
  state = Caller(0x003A7698U);
  state.r[9] = kPlay;
  f.W32(kStack + 4U, 4U);
  Require(f.Execute(0x003A7698U, state) && state.r[15] == 0x003A7700U,
          "room-list completed branch resumed in the middle of a block");

  f.W32(kPlay + 0x209CU + 11U * 8U, kActor);
  f.W8(kActor + 0x1EU, 22U); // last extra, category 11 must be scanned
  for (int i = 0; i < 5; ++i) Require(f.Sweep().empty(), "live tail actor was freed");
  Require(f.R8(kBase) == 4U && f.R8(kBase + 0xA06U) == 6U &&
              f.R8(kBase + 0xA07U) == 1U,
          "retirement did not retain stable indices behind a live tail");
  f.W8(kBase + 0xA06U, 255U);
  Require(f.Sweep().empty() && f.R8(kBase + 0xA06U) == 255U, "retirement age wrapped");
  f.W32(kPlay + 0x209CU + 11U * 8U, 0U);
  Require(f.Sweep().empty() && f.Sweep().empty(), "retired tail was freed before age four");
  const auto calls = f.Sweep();
  Require(calls.size() == 4U &&
              calls[0] == std::pair{0x002F70C4U, Fixture::Slot(3) + 0x10U} &&
              calls[1] == std::pair{0x0034FC6CU, kPlay + 0x10600U} &&
              calls[2] == std::pair{0x002F70C4U, Fixture::Slot(2) + 0x10U} &&
              f.R8(kBase) == 2U && f.R8(kBase + 1U) == 2U &&
              f.R16(Fixture::Slot(2)) == 0U && f.R16(Fixture::Slot(3)) == 0U &&
              f.R16(Fixture::Slot(0)) == 100U && f.R8(kBase + 0xA18U) == 0U,
          "suffix retirement moved a live slot, freed the prefix, or skipped native destruction");
}

void SceneAndPauseGates() {
  Fixture f;
  f.Bank();
  f.W8(kBase, 1U);
  f.Seed(0, 100U);
  f.W8(kBase + 0xA04U, 3U);
  f.W8(kBase + 0xA18U, 1U);
  f.W32(kBase + 0xA1CU, kPlay + 0x100U);
  f.W8(kPlay + 0x101U, 2U);
  Require(f.Sweep().empty() && f.R8(kBase + 0xA04U) == 3U,
          "foreign PlayState advanced retirement");
  f.W32(kBase + 0xA1CU, kPlay);
  f.W8(kPlay + 0x101U, 1U);
  Require(f.Sweep().empty() && f.R8(kBase + 0xA04U) == 3U,
          "scene setup advanced room retirement");
  f.W8(kPlay + 0x101U, 2U);
  auto state = f.SweepCaller();
  state.r[8] = kBase;
  Require(!f.Execute(kSweep, state) && f.R8(kBase + 0xA04U) == 3U,
          "non-Play object update ran a second retirement tick");
}

void TeardownAndSnapshots() {
  Fixture f;
  auto state = f.Resolve(123U);
  const auto caller = state;
  Require(f.Execute(kResolve, state), "snapshot load did not start");
  const auto savedMemory = nlohmann::json::from_cbor(nlohmann::json::to_cbor(f.Memory.CaptureState()));
  const auto savedRuntime = nlohmann::json::parse(f.Runtime.CaptureState().dump());
  Fixture restored;
  std::string error;
  Require(restored.Memory.RestoreState(savedMemory, &error) &&
              restored.Runtime.RestoreState(savedRuntime, &error) && restored.Runtime.Pending(),
          "pending native load did not restore");
  restored.Runtime.SetEnabled(false);
  restored.Seed(0, 123U);
  restored.W8(kBase, 1U);
  Returned(state, caller, 0U);
  Require(restored.Execute(kResolve, state) && restored.Runtime.Stats().Loads == 1U,
          "restored/disabled in-flight load did not complete");
  const auto generation = restored.Memory.WriteGeneration();
  auto corrupt = restored.Runtime.CaptureState();
  corrupt["operation"] = 99U;
  Require(!restored.Runtime.RestoreState(corrupt, &error) &&
              restored.Memory.WriteGeneration() == generation && !restored.Runtime.Pending(),
          "invalid snapshot changed memory/host continuation");

  restored.W8(kBase, 3U);
  restored.W8(kBase + 1U, 2U);
  restored.Seed(1, 124U, 0U);
  restored.Seed(2, 125U);
  state = Caller(kDestroy);
  state.r[5] = kPlay;
  const auto destroyCaller = state;
  Require(restored.Execute(kDestroy, state) && state.r[15] == 0x002F70C4U &&
              state.r[0] == Fixture::Slot(0) + 0x10U, "teardown did not destroy first native archive");
  const auto pending = restored.Runtime.CaptureState();
  ObjectBankExtension replacement(restored.Memory);
  Require(replacement.RestoreState(pending, &error) && replacement.ShouldObserve(kDestroy),
          "pending teardown did not survive runtime replacement");
  Returned(state, destroyCaller);
  std::uint32_t blocks = 0;
  ExecutionResult result{};
  Require(replacement.Execute(kDestroy, state, &result, &blocks) &&
              state.r[15] == 0x0034FC6CU && state.r[0] == kPlay + 0x10000U &&
              restored.R32(Fixture::Slot(0) + 8U) == 0U,
          "restored archive completion did not free original buffer");
  const auto awaitingFree = replacement.CaptureState();
  ObjectBankExtension afterFreeSnapshot(restored.Memory);
  Require(afterFreeSnapshot.RestoreState(awaitingFree, &error), "pending free did not restore");
  unsigned frees = 0, destroys = 1;
  while (afterFreeSnapshot.Pending()) {
    if (state.r[15] == 0x0034FC6CU) ++frees;
    else if (state.r[15] == 0x002F70C4U) ++destroys;
    else Require(false, "teardown invoked an unexpected native function");
    Returned(state, destroyCaller);
    Require(afterFreeSnapshot.Execute(kDestroy, state, &result, &blocks), "teardown re-entry failed");
  }
  auto expected = destroyCaller;
  expected.r[14] = 0x0044E784U;
  expected.r[15] = 0x0046053CU;
  Require(Same(state, expected) && frees == 3U && destroys == 2U &&
              restored.R32(kBase) == 0U && restored.R32(kBase + 0xA1CU) == 0U &&
              afterFreeSnapshot.Stats().Frees == 3U &&
              !afterFreeSnapshot.ShouldObserve(kResolve),
          "scene teardown did not release prefix/suffix and restore native continuation");
  for (std::uint32_t i = 0; i < 3U; ++i)
    Require(restored.R16(Fixture::Slot(i)) == 0U &&
                restored.R32(Fixture::Slot(i) + 4U) == 0U &&
                restored.R32(Fixture::Slot(i) + 8U) == 0U,
            "teardown retained an object ID/buffer/size");

  Fixture missingRegion;
  Require(!missingRegion.Runtime.RestoreState(savedRuntime, &error),
          "pending host state accepted missing guest region");
  ObjectBankExtension legacy(missingRegion.Memory);
  legacy.Reset();
  Require(!legacy.Pending() && !legacy.ShouldObserve(kResolve), "legacy reset enabled the feature");
}

void RetirementSnapshotAndReuse() {
  Fixture f;
  f.Bank();
  f.W8(kBase, 1U);
  f.Seed(0, 123U);
  f.W8(kBase + 0xA04U, 3U);
  f.W8(kBase + 0xA18U, 1U);
  f.W32(kBase + 0xA1CU, kPlay);
  f.W8(kPlay + 0x101U, 2U);
  auto state = f.SweepCaller();
  const auto caller = state;
  Require(f.Execute(kSweep, state) && f.Runtime.Pending(), "retirement snapshot did not start");
  const auto memoryState = f.Memory.CaptureState();
  const auto runtimeState = f.Runtime.CaptureState();
  Fixture restored;
  std::string error;
  Require(restored.Memory.RestoreState(memoryState, &error) &&
              restored.Runtime.RestoreState(runtimeState, &error),
          "retirement snapshot failed to restore");
  Returned(state, caller);
  Require(restored.Execute(kSweep, state) && state.r[15] == 0x0034FC6CU,
          "retirement restore repeated archive destruction");
  Returned(state, caller);
  Require(restored.Execute(kSweep, state) && !restored.Runtime.Pending() &&
              restored.R8(kBase) == 0U && restored.Runtime.Stats().Sweeps == 1U,
          "retirement restore repeated age increment or lost count");
  state = restored.Resolve(124U);
  const auto loadCaller = state;
  Require(restored.Execute(kResolve, state) && state.r[0] == kBase,
          "freed tail was not reusable");
  restored.Seed(0, 124U);
  restored.W8(kBase, 1U);
  Returned(state, loadCaller, 0U);
  Require(restored.Execute(kResolve, state) && state.r[0] == Fixture::Slot(0) &&
              restored.R8(kBase + 0xA04U) == 0U,
          "reused slot retained stale retirement state");
}
// Runs a continuation to its end; returns each native call (entry, r0).
std::vector<std::pair<std::uint32_t, std::uint32_t>>
Drive(Fixture &f, std::uint32_t pc, GuestState &state, const GuestState &caller) {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> calls;
  while (f.Runtime.Pending()) {
    calls.emplace_back(state.r[15], state.r[0]);
    Returned(state, caller);
    f.Execute(pc, state);
  }
  return calls;
}

void LoadingCancelAtTeardown() {
  // 3.1 0x00606AC8: loads still in flight at scene teardown, native and
  // extra, release their file request and buffer before 3.0's teardown.
  Fixture f;
  f.Bank();
  const auto native = kNativeSlot + 2U * 0x80U;
  f.W16(native, static_cast<std::uint16_t>(-0x10));
  f.W32(native + 4U, kPlay + 0x12000U);
  f.W32(native + 8U, 0x55U);
  f.W32(native + 0xCU, kPlay + 0x13000U);
  f.W8(kBase, 2U);
  f.Seed(0, 0x20U);
  f.Seed(1, static_cast<std::uint16_t>(-0x21));
  auto state = Caller(kDestroy);
  state.r[5] = kPlay;
  const auto caller = state;
  Require(f.Execute(kDestroy, state), "teardown with loading slots stayed native");
  const auto calls = Drive(f, kDestroy, state, caller);
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> expected{
      {kDestroyFileRequest, kPlay + 0x13000U}, {kFree, kPlay + 0x12000U},
      {kFree, kPlay + 0x10200U},
      {kDestroyArchive, Fixture::Slot(0) + 0x10U}, {kFree, kPlay + 0x10000U}};
  Require(calls == expected, "teardown released loads in the wrong order or with the wrong arguments");
  auto continuation = caller;
  continuation.r[14] = 0x0044E784U;
  continuation.r[15] = 0x0046053CU;
  Require(Same(state, continuation), "teardown did not resume the native continuation");
  Require(f.R16(native) == 0U && f.R32(native + 4U) == 0U && f.R32(native + 8U) == 0U &&
              f.R32(native + 0xCU) == 0U && f.R16(Fixture::Slot(1)) == 0U &&
              f.Runtime.Stats().LoadingCancels == 2U,
          "loading records were not cleared");
}

void LoadingCancelWithoutExtension() {
  Fixture f;
  f.Runtime.SetEnabled(false);
  auto state = Caller(kDestroy);
  state.r[5] = kPlay;
  const auto caller = state;
  Require(!f.Execute(kDestroy, state) && Same(state, caller),
          "teardown without loads left the native block");
  f.W16(kNativeSlot, static_cast<std::uint16_t>(-7));
  f.W32(kNativeSlot + 0xCU, kPlay + 0x13000U);
  Require(f.Execute(kDestroy, state) && state.r[15] == kDestroyFileRequest &&
              state.r[0] == kPlay + 0x13000U, "loading native slot was not released");
  Returned(state, caller);
  Require(!f.Execute(kDestroy, state) && Same(state, caller) && !f.Runtime.Pending() &&
              f.R16(kNativeSlot) == 0U && f.R32(kNativeSlot + 0xCU) == 0U &&
              !f.Memory.IsWritable(kBase, kObjectBankExtensionSize),
          "teardown without the extension did not return to its native block untouched");
}

void RoomReplaceLifetime() {
  Fixture f;
  f.Runtime.SetEnabled(false);
  const auto record = kNativeSlot + 5U * 0x80U;
  // 3.1 0x006069BC: a replaced slot whose file request is still open.
  f.W32(record + 4U, kPlay + 0x14000U);
  f.W32(record + 8U, 0x77U);
  f.W32(record + 0xCU, kPlay + 0x15000U);
  auto state = Caller(kRoomReplace);
  state.r[4] = record;
  state.r[9] = kPlay;
  const auto caller = state;
  Require(f.Execute(kRoomReplace, state), "open request on a replaced slot stayed native");
  const auto calls = Drive(f, kRoomReplace, state, caller);
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> expected{
      {kDestroyFileRequest, kPlay + 0x15000U}, {kFree, kPlay + 0x14000U}};
  auto resumed = caller;
  resumed.r[15] = 0x003A763CU;
  Require(calls == expected && Same(state, resumed) && f.R32(record + 4U) == 0U &&
              f.R32(record + 8U) == 0U && f.R32(record + 0xCU) == 0U,
          "replaced slot was not released before the loop continued");

  // Room on the native deferred list: native.
  f.W32(record + 8U, 0x77U);
  f.W16(kPlay + 0x43DCU, 18U);
  state = caller;
  Require(!f.Execute(kRoomReplace, state) && Same(state, caller) &&
              !f.Memory.IsWritable(kObjectBankReleaseQueueBase, kObjectBankReleaseQueueSize),
          "replacement with room on the native list left native code");
  Require(!f.Runtime.ShouldObserve(kReleaseMain), "empty release queue was observed");

  // 3.1 0x00606A1C: the native list is full; the archive goes to the queue.
  f.W16(kPlay + 0x43DCU, 19U);
  for (std::uint32_t offset = 0; offset < 0x70U; offset += 4U)
    f.W32(record + 0x10U + offset, 0xA0000000U + offset);
  f.W32(record + 0x28U, kPlay + 0x16000U);
  f.W32(record + 4U, kPlay + 0x16000U);
  Require(f.Execute(kRoomReplace, state) && Same(state, resumed) &&
              f.R16(kPlay + 0x43DEU) == 1U && f.R32(record + 4U) == 0U &&
              f.R32(record + 8U) == 0U && f.R8(kObjectBankReleaseQueueBase) == 1U &&
              f.R32(kObjectBankReleaseQueueBase + 0x10U) == 0xA0000000U &&
              f.R32(kObjectBankReleaseQueueBase + 0x28U) == kPlay + 0x16000U &&
              f.Runtime.Stats().DeferredQueued == 1U &&
              !f.Memory.IsExecutable(kObjectBankReleaseQueueBase, kObjectBankReleaseQueueSize),
          "archive overflowing the native list was not queued");

  // Play_Main releases the queue; the pending release survives a snapshot.
  Require(f.Runtime.ShouldObserve(kReleaseMain), "queued archive is not released");
  state = Caller(kReleaseMain);
  const auto mainCaller = state;
  Require(f.Execute(kReleaseMain, state) && state.r[15] == kDestroyArchive &&
              state.r[0] == kObjectBankReleaseQueueBase + 0x10U,
          "queued archive destructor was not called");
  ObjectBankExtension replacement(f.Memory);
  std::string error;
  Require(replacement.RestoreState(f.Runtime.CaptureState(), &error) && replacement.Pending(),
          "pending release did not restore");
  ExecutionResult result{};
  std::uint32_t blocks = 0;
  Returned(state, mainCaller);
  Require(replacement.Execute(kReleaseMain, state, &result, &blocks) && state.r[15] == kFree &&
              state.r[0] == kPlay + 0x16000U,
          "released archive's buffer was not freed");
  Returned(state, mainCaller);
  Require(!replacement.Execute(kReleaseMain, state, &result, &blocks) && Same(state, mainCaller) &&
              !replacement.Pending() && f.R8(kObjectBankReleaseQueueBase) == 0U &&
              replacement.Stats().DeferredReleases == 1U &&
              !replacement.ShouldObserve(kReleaseMain),
          "release did not return to the native Play_Main block");
}
} // namespace

int main() {
  LookupAndDisabled();
  MappingCollisions();
  IndexAndPredicates();
  AllocationAndCapacity();
  AllocationFailure();
  RoomLifetime();
  SceneAndPauseGates();
  TeardownAndSnapshots();
  RetirementSnapshotAndReuse();
  LoadingCancelAtTeardown();
  LoadingCancelWithoutExtension();
  RoomReplaceLifetime();
  std::cout << "SingleScreen 3.0 object bank extension tests passed\n";
}
