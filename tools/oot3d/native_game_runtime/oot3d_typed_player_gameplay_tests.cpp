#include "oot3d_typed_gameplay_bridge.h"

#include "oot3d_a32_generated.h"
#include "oot3d_gameplay_player.h"
#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using oot3d::gameplay::Actor;
using oot3d::gameplay::GuestPtr;
using oot3d::gameplay::PlayerMovementContextWire;
using oot3d::gameplay::PlayerPlayStateWire;
using oot3d::gameplay::PlayerWireState;
using oot3d::gameplay::SkelAnime;
using Oot3dNativeGame::NativeA32Memory;

constexpr std::uint32_t kPlayerAddress = 0x10000000U;
constexpr std::uint32_t kPlayAddress = 0x10004000U;
constexpr std::uint32_t kMovementContextAddress = 0x1000A000U;
constexpr std::uint32_t kFocusActorAddress = 0x1000B000U;
constexpr std::uint32_t kStackTop = 0x1001F000U;
constexpr std::uint32_t kReturnSentinel = 0x0BADF00CU;
constexpr std::uint32_t kIdleTableAddress = 0x0053A5F8U;
constexpr std::uint32_t kIdleTransitionAddress = 0x0054AC55U;
constexpr std::uint32_t kHeightStateAddress = 0x00587958U;
constexpr std::uint32_t kCutsceneGlobalAddress = 0x00588E58U;
constexpr std::size_t kRuntimeCompareSize = 0xC000U;
constexpr std::array kInvincibilityHoldActionLiterals{
    0x00251314U,
    0x00251318U,
    0x0025131CU,
    0x00251320U,
};
constexpr std::array kInvincibilityHoldActions{
    0x004886F4U,
    0x004BC22CU,
    0x004C3064U,
    0x00495C30U,
};

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void MapRegion(NativeA32Memory &memory, const char *name,
               std::uint32_t address, std::size_t size) {
  std::string error;
  Expect(memory.MapRegion({name, address, size, true, false, {}}, &error),
         std::string("could not map ") + name + ": " + error);
}

template <typename T>
void WriteObject(NativeA32Memory &memory, std::uint32_t address,
                 const T &value) {
  static_assert(std::is_trivially_copyable_v<T>);
  Expect(memory.WriteBytes(
             address,
             std::span<const std::uint8_t>(
                 reinterpret_cast<const std::uint8_t *>(&value), sizeof(T))),
         "could not write player test object");
}

template <typename T>
T ReadObject(const NativeA32Memory &memory, std::uint32_t address) {
  static_assert(std::is_trivially_copyable_v<T>);
  T value{};
  Expect(memory.ReadBytes(
             address,
             std::span<std::uint8_t>(
                 reinterpret_cast<std::uint8_t *>(&value), sizeof(T))),
         "could not read player test object");
  return value;
}

void WriteU32(NativeA32Memory &memory, std::uint32_t address,
              std::uint32_t value) {
  WriteObject(memory, address, value);
}

void WriteFloat(NativeA32Memory &memory, std::uint32_t address, float value) {
  WriteU32(memory, address, std::bit_cast<std::uint32_t>(value));
}

template <typename T>
void WritePlayerField(NativeA32Memory &memory, std::size_t offset,
                      const T &value) {
  WriteObject(memory, kPlayerAddress + static_cast<std::uint32_t>(offset),
              value);
}

NativeA32Memory BuildMemory() {
  NativeA32Memory memory;
  MapRegion(memory, "player_251", 0x00251000U, 0x1000U);
  MapRegion(memory, "player_252", 0x00252000U, 0x1000U);
  MapRegion(memory, "player_313", 0x00313000U, 0x1000U);
  MapRegion(memory, "player_327", 0x00327000U, 0x1000U);
  MapRegion(memory, "player_330", 0x00330000U, 0x1000U);
  MapRegion(memory, "player_349", 0x00349000U, 0x1000U);
  MapRegion(memory, "player_34b", 0x0034B000U, 0x1000U);
  MapRegion(memory, "player_34d", 0x0034D000U, 0x1000U);
  MapRegion(memory, "player_355", 0x00355000U, 0x1000U);
  MapRegion(memory, "player_35a", 0x0035A000U, 0x1000U);
  MapRegion(memory, "player_35d", 0x0035D000U, 0x1000U);
  MapRegion(memory, "player_367", 0x00367000U, 0x1000U);
  MapRegion(memory, "player_36a", 0x0036A000U, 0x1000U);
  MapRegion(memory, "player_36e", 0x0036E000U, 0x1000U);
  MapRegion(memory, "idle_table", 0x0053A000U, 0x2000U);
  MapRegion(memory, "idle_transition", 0x0054A000U, 0x1000U);
  MapRegion(memory, "player_globals", 0x00587000U, 0x2000U);
  MapRegion(memory, "runtime", kPlayerAddress, 0x20000U);

  WriteFloat(memory, 0x003495DCU, 0.0f);
  WriteFloat(memory, 0x00313CC0U, 0.1f);
  WriteFloat(memory, 0x00313CC4U, 1.0f);
  for (std::size_t index = 0U;
       index < kInvincibilityHoldActionLiterals.size(); ++index) {
    WriteU32(memory, kInvincibilityHoldActionLiterals[index],
             kInvincibilityHoldActions[index]);
  }
  WriteU32(
      memory, 0x00252144U,
      oot3d::gameplay::kPlayerUnderwaterTimerSaturationLogicalFrames);
  WriteFloat(memory, 0x0034B25CU, -5.0f);
  WriteFloat(memory, 0x0034B260U, 0.0f);
  WriteFloat(memory, 0x0034B264U, 2.0f / 3.0f);
  WriteFloat(memory, 0x0034B268U, 0.5f);
  WriteFloat(memory, 0x0034B26CU, -0.1f);
  WriteFloat(memory, 0x0034B270U, -3.0f);
  WriteFloat(memory, 0x0034B274U, -6.0f);
  WriteFloat(memory, 0x0034B278U, -0.2f);
  WriteFloat(memory, 0x0034B27CU, 2.0f);
  WriteFloat(memory, 0x0034B280U, 1.0f / 15.0f);
  WriteFloat(memory, 0x0034B284U, 100.0f);
  WriteU32(memory, 0x0034D4E4U,
           static_cast<std::uint32_t>(offsetof(PlayerWireState,
                                               HeldItemAction)));
  WriteU32(memory, 0x0034D67CU,
           static_cast<std::uint32_t>(offsetof(PlayerWireState, RuntimeFlags)));
  WriteU32(memory, 0x0034D680U, kIdleTableAddress);
  WriteU32(memory, 0x0034D684U, kIdleTransitionAddress);
  WriteFloat(memory, 0x00367F20U, 0.0f);
  WriteFloat(memory, 0x00367F24U, 32.0f);
  WriteU32(memory, 0x00367F28U, kHeightStateAddress);
  WriteFloat(memory, 0x00367F2CU, 44.0f);
  WriteFloat(memory, 0x00367F30U, 68.0f);
  WriteU32(memory, 0x0036A830U,
           static_cast<std::uint32_t>(offsetof(PlayerPlayStateWire, Player)));
  WriteU32(memory, 0x0036A834U, 0x20000080U);
  WriteU32(memory, 0x0036A838U, kCutsceneGlobalAddress);
  WriteU32(memory, 0x0036E9B4U,
           static_cast<std::uint32_t>(offsetof(PlayerWireState, HaltActors)));

  WritePlayerField(memory, offsetof(PlayerWireState, MovementContext),
                   GuestPtr<PlayerMovementContextWire>{
                       kMovementContextAddress});
  WriteObject(memory,
              kPlayAddress +
                  static_cast<std::uint32_t>(
                      offsetof(PlayerPlayStateWire, Player)),
              GuestPtr<PlayerWireState>{kPlayerAddress});
  return memory;
}

oot3d::recomp::a32::GuestState BuildState(std::uint32_t entry,
                                          std::uint32_t firstArgument) {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = firstArgument;
  state.r[13] = kStackTop;
  state.r[14] = kReturnSentinel;
  state.r[15] = entry;
  return state;
}

void ExpectRuntimeEqual(const NativeA32Memory &actual,
                        const NativeA32Memory &expected,
                        const std::string &label) {
  std::array<std::uint8_t, kRuntimeCompareSize> actualBytes{};
  std::array<std::uint8_t, kRuntimeCompareSize> expectedBytes{};
  Expect(actual.ReadBytes(kPlayerAddress, actualBytes) &&
             expected.ReadBytes(kPlayerAddress, expectedBytes),
         label + ": could not read runtime comparison range");
  if (actualBytes != expectedBytes) {
    const auto mismatch = std::mismatch(actualBytes.begin(), actualBytes.end(),
                                        expectedBytes.begin());
    const std::size_t offset =
        static_cast<std::size_t>(mismatch.first - actualBytes.begin());
    const std::size_t wordOffset = offset & ~std::size_t{3};
    std::uint32_t actualWord = 0U;
    std::uint32_t expectedWord = 0U;
    std::memcpy(&actualWord, actualBytes.data() + wordOffset,
                sizeof(actualWord));
    std::memcpy(&expectedWord, expectedBytes.data() + wordOffset,
                sizeof(expectedWord));
    throw std::runtime_error(
        label + ": guest memory mismatch at +" + std::to_string(offset) +
        " typed=" + std::to_string(*mismatch.first) +
        " A32=" + std::to_string(*mismatch.second) +
        " typed_word=" + std::to_string(actualWord) +
        " A32_word=" + std::to_string(expectedWord));
  }
}

void RunDifferential(const std::string &label, std::uint32_t entry,
                     const NativeA32Memory &initialMemory,
                     const oot3d::recomp::a32::GuestState &initialState) {
  auto referenceMemory = initialMemory;
  auto typedMemory = initialMemory;
  auto referenceState = initialState;
  auto typedState = initialState;

  const auto referenceResult = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), entry, referenceState,
      referenceMemory, nullptr, nullptr, 20'000U);
  Expect(referenceResult.kind ==
                 oot3d::recomp::a32::ExitKind::MissingBlock &&
             referenceResult.pc == kReturnSentinel,
         label + ": A32 reference did not return through LR");

  oot3d::recomp::a32::ExecutionResult typedResult;
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             entry, typedState, typedMemory, &typedResult, {2.0f},
             &blocksConsumed),
         label + ": typed Player entry retained AOT");
  Expect(typedResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
             typedResult.pc == kReturnSentinel && blocksConsumed == 1U,
         label + ": typed Player entry returned invalid ABI state");
  Expect(typedState.r[0] == referenceState.r[0],
         label + ": r0 mismatch");
  if (entry == Oot3dNativeGame::kOot3dPlayerGetHeightEntry) {
    Expect(typedState.vfp[0] == referenceState.vfp[0],
           label + ": floating return mismatch");
  }
  ExpectRuntimeEqual(typedMemory, referenceMemory, label);
}

void TestEquipmentQueries() {
  struct QueryCase {
    std::uint32_t Entry;
    std::int8_t HeldItem;
    std::uint32_t Flags;
    std::uint32_t HeldActor;
    const char *Label;
  };
  constexpr std::array cases{
      QueryCase{Oot3dNativeGame::kOot3dPlayerGetExplosiveHeldEntry, 0x12, 0U,
                0U, "explosive-0"},
      QueryCase{Oot3dNativeGame::kOot3dPlayerGetExplosiveHeldEntry, 0x13, 0U,
                0U, "explosive-1"},
      QueryCase{Oot3dNativeGame::kOot3dPlayerGetExplosiveHeldEntry, 0x11, 0U,
                0U, "explosive-none"},
      QueryCase{Oot3dNativeGame::kOot3dPlayerHoldsHookshotEntry, 0x10, 0U, 0U,
                "hookshot"},
      QueryCase{Oot3dNativeGame::kOot3dPlayerHoldsHookshotEntry, 0x12, 0U, 0U,
                "not-hookshot"},
      QueryCase{Oot3dNativeGame::kOot3dPlayerHoldsTwoHandedWeaponEntry, 5, 0U,
                0U, "two-handed-low"},
      QueryCase{Oot3dNativeGame::kOot3dPlayerHoldsTwoHandedWeaponEntry, 8, 0U,
                0U, "not-two-handed"},
      QueryCase{
          Oot3dNativeGame::kOot3dPlayerHoldsHookshotWithoutHeldActorEntry,
          0x11, 0U, 0U, "free-hookshot"},
      QueryCase{
          Oot3dNativeGame::kOot3dPlayerHoldsHookshotWithoutHeldActorEntry,
          0x11, 0U, kFocusActorAddress, "occupied-hookshot"},
      QueryCase{Oot3dNativeGame::kOot3dPlayerIsItemInHandEntry, 0, 0x18U, 0U,
                "item-in-hand-mask"},
  };
  for (const QueryCase &test : cases) {
    auto memory = BuildMemory();
    WritePlayerField(memory, offsetof(PlayerWireState, HeldItemAction),
                     test.HeldItem);
    WritePlayerField(memory, offsetof(PlayerWireState, StateFlags),
                     test.Flags);
    WritePlayerField(memory, offsetof(PlayerWireState, HeldActor),
                     GuestPtr<Actor>{test.HeldActor});
    RunDifferential(test.Label, test.Entry, memory,
                    BuildState(test.Entry, kPlayerAddress));
  }
}

void TestRuntimeFlag() {
  for (const std::uint32_t enabled : {0U, 1U, 2U}) {
    auto memory = BuildMemory();
    WritePlayerField(memory, offsetof(PlayerWireState, RuntimeFlags),
                     0xA5A50300U);
    auto state = BuildState(
        Oot3dNativeGame::kOot3dPlayerSetRuntimeFlag200Entry, kPlayerAddress);
    state.r[1] = enabled;
    RunDifferential("runtime-flag-" + std::to_string(enabled),
                    Oot3dNativeGame::kOot3dPlayerSetRuntimeFlag200Entry,
                    memory, state);
  }
}

void TestCutsceneActionSetters() {
  for (const std::uint32_t entry : {
           Oot3dNativeGame::kOot3dPlayerSetCsActionEntry,
           Oot3dNativeGame::kOot3dPlayerSetCsActionWithHaltedActorsEntry}) {
    auto memory = BuildMemory();
    WritePlayerField(memory, offsetof(PlayerWireState, CutsceneActionMode),
                     std::uint8_t{0xAA});
    WritePlayerField(memory, offsetof(PlayerWireState, CutsceneAction),
                     GuestPtr<oot3d::gameplay::GuestFunction>{0xDEADBEEFU});
    WritePlayerField(memory, offsetof(PlayerWireState, HaltActors),
                     std::uint16_t{0x55AA});
    auto state = BuildState(entry, kPlayAddress);
    state.r[1] = 0x004991B4U;
    state.r[2] = 0x12345607U;
    RunDifferential(entry == Oot3dNativeGame::kOot3dPlayerSetCsActionEntry
                        ? "set-cs-action"
                        : "set-cs-action-halt",
                    entry, memory, state);
  }
}

void TestHostileLockOn() {
  struct LockCase {
    std::uint32_t Focus;
    std::uint32_t FocusFlags;
    std::uint32_t StateFlags;
    float Speed;
    const char *Label;
  };
  constexpr std::array cases{
      LockCase{kFocusActorAddress, 0x5U, 0U, 1.0f, "lock-on-acquire"},
      LockCase{kFocusActorAddress, 0x1U, 0x10U, 0.0f, "lock-on-clear-yaw"},
      LockCase{0U, 0U, 0x10U, 2.0f, "lock-on-clear-moving"},
      LockCase{0U, 0U, 0U, 0.0f, "lock-on-inactive"},
  };
  for (const LockCase &test : cases) {
    auto memory = BuildMemory();
    WritePlayerField(memory, offsetof(PlayerWireState, FocusActor),
                     GuestPtr<Actor>{test.Focus});
    WritePlayerField(memory, offsetof(PlayerWireState, StateFlags),
                     test.StateFlags);
    WritePlayerField(memory, offsetof(PlayerWireState, Speed), test.Speed);
    constexpr std::size_t shapeYawOffset =
        offsetof(PlayerWireState, BaseActor) + offsetof(Actor, Shape) +
        offsetof(oot3d::gameplay::ActorShape, Rotation) +
        offsetof(oot3d::gameplay::Vec3s, Y);
    WritePlayerField(memory, shapeYawOffset, std::int16_t{-0x2345});
    WritePlayerField(memory, offsetof(PlayerWireState, LockOnYaw),
                     std::int16_t{0x1234});
    WriteObject(memory, kFocusActorAddress + offsetof(Actor, Flags),
                test.FocusFlags);
    RunDifferential(
        test.Label, Oot3dNativeGame::kOot3dPlayerUpdateHostileLockOnEntry,
        memory,
        BuildState(Oot3dNativeGame::kOot3dPlayerUpdateHostileLockOnEntry,
                   kPlayerAddress));
  }
}

void TestSwimVerticalVelocity() {
  struct SwimCase {
    float Velocity;
    float Depth;
    float Surface;
    std::int32_t Animation;
    std::uint32_t Flags;
    std::uint8_t Movement;
    const char *Label;
  };
  constexpr std::array cases{
      SwimCase{3.0f, 2.0f, 4.0f, 0, 0U, 0U, "swim-surface-rising"},
      SwimCase{-1.0f, 2.0f, 4.0f, 0, 0U, 0U, "swim-surface-falling"},
      SwimCase{-2.0f, 120.0f, 4.0f, 0, 0U, 1U, "swim-sink"},
      SwimCase{-4.0f, 120.0f, 4.0f, 0x34, 0U, 1U,
               "swim-alternate-sink"},
      SwimCase{-1.0f, 80.0f, 4.0f, 0, 0x80U, 0U, "swim-buoyancy"},
  };
  for (const SwimCase &test : cases) {
    auto memory = BuildMemory();
    constexpr std::size_t velocityYOffset =
        offsetof(PlayerWireState, BaseActor) + offsetof(Actor, Velocity) +
        offsetof(oot3d::gameplay::Vec3f, Y);
    constexpr std::size_t gravityOffset =
        offsetof(PlayerWireState, BaseActor) + offsetof(Actor, Gravity);
    constexpr std::size_t depthInWaterOffset =
        offsetof(PlayerWireState, BaseActor) + offsetof(Actor, DepthInWater);
    constexpr std::size_t animationIndexOffset =
        offsetof(PlayerWireState, MainAnimation) +
        offsetof(SkelAnime, AnimationIndex);
    WritePlayerField(memory, velocityYOffset, test.Velocity);
    WritePlayerField(memory, gravityOffset, -1.5f);
    WritePlayerField(memory, depthInWaterOffset, test.Depth);
    WriteObject(memory,
                kMovementContextAddress +
                    offsetof(PlayerMovementContextWire, SurfaceReference),
                test.Surface);
    WritePlayerField(memory, animationIndexOffset, test.Animation);
    WritePlayerField(memory, offsetof(PlayerWireState, StateFlags),
                     test.Flags);
    WritePlayerField(memory, offsetof(PlayerWireState, SecondaryStateFlags),
                     0x80000000U);
    WritePlayerField(memory, offsetof(PlayerWireState, MovementState),
                     test.Movement);
    RunDifferential(
        test.Label,
        Oot3dNativeGame::kOot3dPlayerUpdateSwimVerticalVelocityEntry, memory,
        BuildState(
            Oot3dNativeGame::kOot3dPlayerUpdateSwimVerticalVelocityEntry,
            kPlayerAddress));
  }
}

void TestIdleSelection() {
  struct IdleCase {
    std::uint32_t RuntimeFlags;
    std::uint8_t Background;
    std::int8_t Transition;
    const char *Label;
  };
  constexpr std::array cases{
      IdleCase{0U, 0U, 0, "idle-normal"},
      IdleCase{0x200U, 0U, 0, "idle-runtime-alternate"},
      IdleCase{0U, 1U, 0x51, "idle-transition-alternate"},
      IdleCase{0x400U, 1U, 0x51, "idle-transition-override"},
  };
  constexpr std::uint8_t animationType = 3U;
  for (const IdleCase &test : cases) {
    auto memory = BuildMemory();
    WritePlayerField(memory, offsetof(PlayerWireState, RuntimeFlags),
                     test.RuntimeFlags);
    WritePlayerField(memory, offsetof(PlayerWireState, BackgroundState),
                     test.Background);
    WritePlayerField(memory, offsetof(PlayerWireState, IdleAnimationType),
                     animationType);
    WriteObject(memory, kIdleTransitionAddress, test.Transition);
    WriteU32(memory, kIdleTableAddress + animationType * 4U, 0x123U);
    WriteU32(memory, kIdleTableAddress + 0x4F8U + animationType * 4U, 0x456U);
    RunDifferential(
        test.Label, Oot3dNativeGame::kOot3dPlayerGetIdleAnimEntry, memory,
        BuildState(Oot3dNativeGame::kOot3dPlayerGetIdleAnimEntry,
                   kPlayerAddress));
  }
}

void TestHeight() {
  for (const std::uint32_t flags : {0U, 0x00800000U}) {
    for (const std::uint32_t alternate : {0U, 1U}) {
      auto memory = BuildMemory();
      WritePlayerField(memory, offsetof(PlayerWireState, StateFlags), flags);
      WriteU32(memory, kHeightStateAddress + 4U, alternate);
      RunDifferential(
          "height-" + std::to_string(flags) + '-' +
              std::to_string(alternate),
          Oot3dNativeGame::kOot3dPlayerGetHeightEntry, memory,
          BuildState(Oot3dNativeGame::kOot3dPlayerGetHeightEntry,
                     kPlayerAddress));
    }
  }
}

void TestCutsceneMode() {
  struct CsCase {
    std::uint32_t Flags;
    std::uint8_t Action;
    std::uint8_t StateByte;
    std::int8_t ItemAction;
    std::uint8_t State1749;
    std::uint8_t PlayState;
    std::uint16_t GlobalState;
    const char *Label;
  };
  constexpr std::array cases{
      CsCase{0U, 0U, 0U, 0, 0U, 0U, 0U, "cs-inactive"},
      CsCase{0x80U, 0U, 0U, 0, 0U, 0U, 0U, "cs-blocking-mask"},
      CsCase{0U, 1U, 0U, 0, 0U, 0U, 0U, "cs-action"},
      CsCase{0U, 0U, 0U, 0, 0U, 0x14U, 0U, "cs-play-state"},
      CsCase{1U, 0U, 0U, 0, 0U, 0U, 0U, "cs-player-flag"},
      CsCase{0U, 0U, 0x80U, 0, 0U, 0U, 0U, "cs-byte-flag"},
      CsCase{0U, 0U, 0U, 0x17, 0U, 0U, 1U, "cs-item-range"},
      CsCase{0U, 0U, 0U, 0, 4U, 0U, 0U, "cs-state-1749"},
  };
  for (const CsCase &test : cases) {
    auto memory = BuildMemory();
    WritePlayerField(memory, offsetof(PlayerWireState, StateFlags),
                     test.Flags);
    WritePlayerField(memory, offsetof(PlayerWireState, CutsceneActionMode),
                     test.Action);
    WritePlayerField(memory, offsetof(PlayerWireState, StateFlagsByte),
                     test.StateByte);
    WritePlayerField(memory, offsetof(PlayerWireState, ItemAction),
                     test.ItemAction);
    WritePlayerField(memory, offsetof(PlayerWireState, State1749),
                     test.State1749);
    WriteObject(memory,
                kPlayAddress +
                    offsetof(PlayerPlayStateWire, State5C2D),
                test.PlayState);
    WriteObject(memory, kCutsceneGlobalAddress + 0x80U, test.GlobalState);
    RunDifferential(test.Label, Oot3dNativeGame::kOot3dPlayerInCsModeEntry,
                    memory,
                    BuildState(Oot3dNativeGame::kOot3dPlayerInCsModeEntry,
                               kPlayAddress));
  }
}

oot3d::gameplay::TimeContext PlayerTime(bool crossedLogicalFrame,
                                        float nativeUpdateRate) {
  oot3d::gameplay::TimeContext time;
  time.NativeUpdateRate = nativeUpdateRate;
  time.CrossedLogicalFrame = crossedLogicalFrame;
  return time;
}

oot3d::recomp::a32::GuestState BuildFishingRecoveryBlockState() {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = static_cast<std::uint32_t>(
      oot3d::gameplay::kPlayerHeldItemActionFishingPole);
  state.r[4] = kPlayerAddress;
  state.r[6] = kPlayerAddress + 0x2000U;
  state.r[7] = kPlayerAddress + 0x2200U;
  state.r[15] =
      Oot3dNativeGame::kOot3dPlayerFishingStateRecoveryBlock;
  state.cpsr = 0x01234567U;
  return state;
}

std::int16_t ReadFishingItemState(const NativeA32Memory &memory) {
  return ReadObject<std::int16_t>(
      memory, kPlayerAddress +
                  offsetof(PlayerWireState, ItemActionStateOrBurnTimer));
}

void TestFishingItemStateDomain() {
  std::int16_t native30State = -3;
  for (std::uint32_t tick = 0; tick < 3U; ++tick) {
    oot3d::gameplay::PlayerAdvanceFishingItemStateTowardReady(
        native30State,
        oot3d::gameplay::kPlayerHeldItemActionFishingPole,
        PlayerTime(true, 2.0F));
  }
  Expect(native30State == 0,
         "native 30 Hz fishing state did not recover in three frames");

  std::int16_t enhanced60State = -3;
  for (std::uint32_t tick = 0; tick < 6U; ++tick) {
    oot3d::gameplay::PlayerAdvanceFishingItemStateTowardReady(
        enhanced60State,
        oot3d::gameplay::kPlayerHeldItemActionFishingPole,
        PlayerTime((tick & 1U) != 0U, 1.0F));
  }
  Expect(enhanced60State == 0,
         "enhanced 60 Hz fishing state did not preserve wall time");

  std::int16_t sharedBurnTimer = 300;
  oot3d::gameplay::PlayerAdvanceFishingItemStateTowardReady(
      sharedBurnTimer, oot3d::gameplay::kPlayerHeldItemActionDekuStick,
      PlayerTime(true, 2.0F));
  Expect(sharedBurnTimer == 300,
         "fishing helper mutated the shared burning-stick domain");

  std::int16_t readyState = 2;
  oot3d::gameplay::PlayerAdvanceFishingItemStateTowardReady(
      readyState, oot3d::gameplay::kPlayerHeldItemActionFishingPole,
      PlayerTime(true, 2.0F));
  Expect(readyState == 2,
         "fishing helper mutated a non-negative semantic state");
}

void TestFishingRecoveryBlockNativeParity() {
  const auto *block = oot3d::recomp::a32::FindBlock(
      oot3d::recomp::GetA32GeneratedRegistry(),
      Oot3dNativeGame::kOot3dPlayerFishingStateRecoveryBlock);
  Expect(block != nullptr, "native fishing recovery block is absent");

  for (const std::int16_t initialState : {-3, 0, 4}) {
    auto referenceMemory = BuildMemory();
    auto typedMemory = referenceMemory;
    WritePlayerField(referenceMemory,
                     offsetof(PlayerWireState,
                              ItemActionStateOrBurnTimer),
                     initialState);
    WritePlayerField(typedMemory,
                     offsetof(PlayerWireState,
                              ItemActionStateOrBurnTimer),
                     initialState);
    auto referenceState = BuildFishingRecoveryBlockState();
    auto typedState = referenceState;

    const auto referenceResult = oot3d::recomp::a32::ExecuteBlock(
        *block, referenceState, referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerFishingStateRecoveryBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           "typed fishing recovery block retained native A32");
    Expect(referenceResult.pc ==
                   Oot3dNativeGame::kOot3dPlayerFishingStateRecoveryContinue &&
               typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           "typed fishing recovery block returned to the wrong boundary");
    Expect(typedState.r[0] == referenceState.r[0] &&
               typedState.r[15] == referenceState.r[15] &&
               typedState.cpsr == referenceState.cpsr,
           "typed fishing recovery block changed native register semantics");
    ExpectRuntimeEqual(typedMemory, referenceMemory,
                       "fishing-recovery-native-parity-" +
                           std::to_string(initialState));
  }
}

void TestFishingRecoveryBlockEnhanced60() {
  auto memory = BuildMemory();
  WritePlayerField(memory,
                   offsetof(PlayerWireState, ItemActionStateOrBurnTimer),
                   std::int16_t{-3});

  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;
  auto state = BuildFishingRecoveryBlockState();
  const auto intermediate = PlayerTime(false, 1.0F);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPlayerFishingStateRecoveryBlock,
             state, memory, &result, {1.0F, &intermediate},
             &blocksConsumed),
         "enhanced fishing block retained A32 on intermediate substep");
  Expect(ReadFishingItemState(memory) == -3,
         "enhanced fishing block advanced between logical frames");

  state = BuildFishingRecoveryBlockState();
  blocksConsumed = 0U;
  const auto logical = PlayerTime(true, 1.0F);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPlayerFishingStateRecoveryBlock,
             state, memory, &result, {1.0F, &logical}, &blocksConsumed),
         "enhanced fishing block retained A32 on logical frame");
  Expect(ReadFishingItemState(memory) == -2,
         "enhanced fishing block did not advance on logical frame");
}

oot3d::recomp::a32::GuestState BuildUnderwaterTimerBlockState(
    std::uint32_t entry) {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 0xCAFEU;
  state.r[1] = 0xBABEU;
  state.r[4] = kPlayerAddress;
  state.r[7] = kPlayerAddress + 0x2200U;
  state.r[15] = entry;
  state.cpsr = 0x01234567U;
  return state;
}

std::uint16_t ReadUnderwaterTimer(const NativeA32Memory &memory) {
  return ReadObject<std::uint16_t>(
      memory,
      kPlayerAddress + offsetof(PlayerWireState, UnderwaterTimer));
}

const oot3d::recomp::a32::Block &RequireNativeBlock(std::uint32_t entry) {
  const auto *block = oot3d::recomp::a32::FindBlock(
      oot3d::recomp::GetA32GeneratedRegistry(), entry);
  Expect(block != nullptr, "required native Player block is absent");
  return *block;
}

oot3d::recomp::a32::ExecutionResult ExecuteNativeBlock(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory) {
  return oot3d::recomp::a32::ExecuteBlock(RequireNativeBlock(entry), state,
                                          memory);
}

oot3d::recomp::a32::ExecutionResult ExecuteNativeUntil(
    std::uint32_t entry, std::uint32_t stop,
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory) {
  std::uint32_t pc = entry;
  for (std::uint32_t step = 0U; step < 8U; ++step) {
    const auto result = ExecuteNativeBlock(pc, state, memory);
    if (result.pc == stop) {
      return result;
    }
    pc = result.pc;
  }
  throw std::runtime_error("native Player subgraph did not reach boundary");
}

void ExpectCoreStateEqual(
    const oot3d::recomp::a32::GuestState &actual,
    const oot3d::recomp::a32::GuestState &expected,
    const std::string &label) {
  Expect(actual.r == expected.r && actual.cpsr == expected.cpsr,
         label + ": typed block changed native register semantics");
}

oot3d::recomp::a32::GuestState
BuildRespawnDamageAdvanceBlockState(std::int8_t respawnDamageState) {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = static_cast<std::uint32_t>(
      static_cast<std::int32_t>(respawnDamageState));
  state.r[4] = kPlayerAddress;
  state.r[6] = kPlayerAddress + 0x2000U;
  state.r[13] = kStackTop;
  state.r[14] = kReturnSentinel;
  state.r[15] =
      Oot3dNativeGame::kOot3dPlayerRespawnDamageAdvanceBlock;
  state.cpsr = 0xF1234567U;
  return state;
}

void WriteRespawnDamageState(NativeA32Memory &memory,
                             std::int8_t state) {
  WritePlayerField(
      memory, offsetof(PlayerWireState, RespawnDamageState),
      std::bit_cast<std::uint8_t>(state));
}

std::int8_t ReadRespawnDamageState(const NativeA32Memory &memory) {
  return std::bit_cast<std::int8_t>(ReadObject<std::uint8_t>(
      memory, kPlayerAddress +
                  offsetof(PlayerWireState, RespawnDamageState)));
}

void TestRespawnDamageDomain() {
  std::int8_t native30 = -2;
  Expect(!oot3d::gameplay::PlayerAdvanceRespawnDamageState(
             native30, PlayerTime(true, 2.0F)) &&
             native30 == -1,
         "respawn damage state did not perform its first native advance");
  Expect(oot3d::gameplay::PlayerAdvanceRespawnDamageState(
             native30, PlayerTime(true, 2.0F)) &&
             native30 == 0,
         "respawn damage state did not dispatch at the native crossing");

  std::int8_t enhanced60 = -2;
  std::uint32_t dispatches = 0U;
  for (std::uint32_t tick = 0U; tick < 4U; ++tick) {
    dispatches +=
        oot3d::gameplay::PlayerAdvanceRespawnDamageState(
            enhanced60,
            PlayerTime((tick & 1U) != 0U, 1.0F))
            ? 1U
            : 0U;
  }
  Expect(enhanced60 == 0 && dispatches == 1U,
         "respawn damage duration or one-shot dispatch changed at 60 Hz");

  std::int8_t armedConsumer = 1;
  Expect(!oot3d::gameplay::PlayerAdvanceRespawnDamageState(
             armedConsumer, PlayerTime(true, 1.0F)) &&
             armedConsumer == 1,
         "respawn damage timing owner consumed the hazard state");
}

void TestRespawnDamageBlockNativeParity() {
  constexpr std::array cases{
      std::int8_t{-128},
      std::int8_t{-2},
      std::int8_t{-1},
  };
  for (const std::int8_t initial : cases) {
    auto referenceMemory = BuildMemory();
    WriteRespawnDamageState(referenceMemory, initial);
    auto referenceState =
        BuildRespawnDamageAdvanceBlockState(initial);
    auto typedMemory = referenceMemory;
    auto typedState = referenceState;

    const auto referenceResult = ExecuteNativeBlock(
        Oot3dNativeGame::kOot3dPlayerRespawnDamageAdvanceBlock,
        referenceState, referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerRespawnDamageAdvanceBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           "typed respawn damage block retained native A32");
    Expect(typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           "typed respawn damage block returned incorrectly");
    ExpectCoreStateEqual(
        typedState, referenceState,
        "respawn-damage-native-parity-" +
            std::to_string(static_cast<std::int32_t>(initial)));
    ExpectRuntimeEqual(
        typedMemory, referenceMemory,
        "respawn-damage-native-parity-" +
            std::to_string(static_cast<std::int32_t>(initial)));
  }
}

void TestRespawnDamageBlockEnhanced60() {
  auto memory = BuildMemory();
  WriteRespawnDamageState(memory, -2);

  const auto execute = [&](bool crossedLogicalFrame,
                           std::uint32_t expectedPc) {
    auto state =
        BuildRespawnDamageAdvanceBlockState(
            ReadRespawnDamageState(memory));
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(crossedLogicalFrame, 1.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerRespawnDamageAdvanceBlock,
               state, memory, &result, {1.0F, &time},
               &blocksConsumed),
           "enhanced respawn damage block retained native A32");
    Expect(result.pc == expectedPc && blocksConsumed == 1U,
           "enhanced respawn damage block selected the wrong successor");
  };

  execute(false, Oot3dNativeGame::kOot3dPlayerRespawnDamageContinue);
  Expect(ReadRespawnDamageState(memory) == -2,
         "respawn damage state advanced between logical frames");
  execute(true, Oot3dNativeGame::kOot3dPlayerRespawnDamageContinue);
  Expect(ReadRespawnDamageState(memory) == -1,
         "respawn damage state did not advance on a logical frame");
  execute(false, Oot3dNativeGame::kOot3dPlayerRespawnDamageContinue);
  Expect(ReadRespawnDamageState(memory) == -1,
         "respawn damage state crossed on an intermediate substep");
  execute(true,
          Oot3dNativeGame::kOot3dPlayerRespawnDamageAudioBranch);
  Expect(ReadRespawnDamageState(memory) == 0,
         "respawn damage state did not reach its native audio boundary");
}

void WriteRandomTurnState(NativeA32Memory &memory, std::int16_t state,
                          std::int16_t timer) {
  WritePlayerField(memory, offsetof(PlayerWireState, RandomTurnState), state);
  WritePlayerField(memory, offsetof(PlayerWireState, RandomTurnTimer), timer);
}

std::int16_t ReadRandomTurnTimer(const NativeA32Memory &memory) {
  return ReadObject<std::int16_t>(
      memory, kPlayerAddress +
                  offsetof(PlayerWireState, RandomTurnTimer));
}

oot3d::recomp::a32::GuestState BuildRandomTurnTimerBlockState(
    std::uint32_t entry, std::int16_t timer) {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 0x14U;
  state.r[1] = 0x50U;
  state.r[3] =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
  state.r[4] =
      kPlayerAddress +
      static_cast<std::uint32_t>(
          offsetof(PlayerWireState, RandomTurnState));
  state.r[5] = 6U;
  state.r[6] = 0xA5A5A5A5U;
  state.r[13] = kStackTop;
  state.r[14] = kReturnSentinel;
  state.r[15] = entry;
  state.cpsr = 0x71234567U;
  return state;
}

void TestRandomTurnTimerDomain() {
  std::int16_t timer = 2;
  Expect(!oot3d::gameplay::PlayerAdvanceRandomTurnTimer(
             timer, PlayerTime(false, 1.0F)) &&
             timer == 2,
         "random turn timer advanced between logical frames");
  Expect(!oot3d::gameplay::PlayerAdvanceRandomTurnTimer(
             timer, PlayerTime(true, 1.0F)) &&
             timer == 1,
         "random turn timer did not perform its first logical advance");
  Expect(oot3d::gameplay::PlayerAdvanceRandomTurnTimer(
             timer, PlayerTime(true, 1.0F)) &&
             timer == 0,
         "random turn timer did not request RNG at zero");
  Expect(!oot3d::gameplay::PlayerAdvanceRandomTurnTimer(
             timer, PlayerTime(false, 1.0F)) &&
             timer == 0,
         "zero random turn timer requested RNG on an intermediate substep");
  Expect(oot3d::gameplay::PlayerAdvanceRandomTurnTimer(
             timer, PlayerTime(true, 2.0F)),
         "zero random turn timer did not request native RNG at 30 Hz");

  timer = std::numeric_limits<std::int16_t>::min();
  Expect(!oot3d::gameplay::PlayerAdvanceRandomTurnTimer(
             timer, PlayerTime(true, 2.0F)) &&
             timer == std::numeric_limits<std::int16_t>::max(),
         "random turn timer did not preserve native signed-halfword wrap");
}

void TestRandomTurnTimerBlocksNativeParity() {
  constexpr std::array decrementCases{
      std::numeric_limits<std::int16_t>::min(),
      std::int16_t{-2},
      std::int16_t{-1},
      std::int16_t{1},
      std::int16_t{2},
      std::numeric_limits<std::int16_t>::max(),
  };
  for (const std::int16_t initial : decrementCases) {
    auto referenceMemory = BuildMemory();
    WriteRandomTurnState(referenceMemory, 2, initial);
    auto referenceState = BuildRandomTurnTimerBlockState(
        Oot3dNativeGame::kOot3dPlayerRandomTurnTimerDecrementBlock,
        initial);
    auto typedMemory = referenceMemory;
    auto typedState = referenceState;

    const auto referenceResult = ExecuteNativeBlock(
        Oot3dNativeGame::kOot3dPlayerRandomTurnTimerDecrementBlock,
        referenceState, referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerRandomTurnTimerDecrementBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           "typed random turn decrement block retained native A32");
    Expect(typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           "typed random turn decrement block returned incorrectly");
    const std::string label =
        "random-turn-decrement-native-parity-" +
        std::to_string(static_cast<std::int32_t>(initial));
    ExpectCoreStateEqual(typedState, referenceState, label);
    ExpectRuntimeEqual(typedMemory, referenceMemory, label);
  }

  auto referenceMemory = BuildMemory();
  WriteRandomTurnState(referenceMemory, 1, 0);
  auto referenceState = BuildRandomTurnTimerBlockState(
      Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock, 0);
  auto typedMemory = referenceMemory;
  auto typedState = referenceState;
  const auto referenceResult = ExecuteNativeBlock(
      Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock,
      referenceState, referenceMemory);
  oot3d::recomp::a32::ExecutionResult typedResult;
  std::uint32_t blocksConsumed = 0U;
  const auto time = PlayerTime(true, 2.0F);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock,
             typedState, typedMemory, &typedResult, {2.0F, &time},
             &blocksConsumed),
         "typed random turn refresh block retained native A32");
  Expect(typedResult.pc == referenceResult.pc &&
             blocksConsumed == 1U,
         "typed random turn refresh block returned incorrectly");
  ExpectCoreStateEqual(typedState, referenceState,
                       "random-turn-refresh-native-parity");
  ExpectRuntimeEqual(typedMemory, referenceMemory,
                     "random-turn-refresh-native-parity");
}

void TestRandomTurnTimerBlocksEnhanced60() {
  auto memory = BuildMemory();
  WriteRandomTurnState(memory, 2, 2);

  const auto executeDecrement = [&](bool crossedLogicalFrame,
                                    std::uint32_t expectedPc) {
    const std::int16_t timer = ReadRandomTurnTimer(memory);
    auto state = BuildRandomTurnTimerBlockState(
        Oot3dNativeGame::kOot3dPlayerRandomTurnTimerDecrementBlock,
        timer);
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(crossedLogicalFrame, 1.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerRandomTurnTimerDecrementBlock,
               state, memory, &result, {1.0F, &time}, &blocksConsumed),
           "enhanced random turn decrement retained native A32");
    Expect(result.pc == expectedPc && blocksConsumed == 1U,
           "enhanced random turn decrement selected the wrong successor");
  };

  executeDecrement(
      false, Oot3dNativeGame::kOot3dPlayerRandomTurnTimerContinue);
  Expect(ReadRandomTurnTimer(memory) == 2,
         "random turn timer advanced on the first intermediate substep");
  executeDecrement(
      true, Oot3dNativeGame::kOot3dPlayerRandomTurnTimerContinue);
  Expect(ReadRandomTurnTimer(memory) == 1,
         "random turn timer did not advance on its logical frame");
  executeDecrement(
      false, Oot3dNativeGame::kOot3dPlayerRandomTurnTimerContinue);
  Expect(ReadRandomTurnTimer(memory) == 1,
         "random turn timer advanced on the second intermediate substep");
  executeDecrement(
      true, Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock);
  Expect(ReadRandomTurnTimer(memory) == 0,
         "random turn timer did not reach its native refresh boundary");

  auto refreshState = BuildRandomTurnTimerBlockState(
      Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock, 0);
  oot3d::recomp::a32::ExecutionResult refreshResult;
  std::uint32_t blocksConsumed = 0U;
  auto time = PlayerTime(true, 1.0F);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock,
             refreshState, memory, &refreshResult, {1.0F, &time},
             &blocksConsumed),
         "logical random turn refresh retained native A32");
  Expect(refreshResult.pc ==
             Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRngEntry &&
             refreshState.r[14] ==
                 Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRngReturn,
         "logical random turn refresh did not dispatch native RNG");

  WriteRandomTurnState(memory, 1, 0);
  refreshState = BuildRandomTurnTimerBlockState(
      Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock, 0);
  blocksConsumed = 0U;
  time = PlayerTime(false, 1.0F);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock,
             refreshState, memory, &refreshResult, {1.0F, &time},
             &blocksConsumed),
         "intermediate random turn refresh retained native A32");
  Expect(refreshResult.pc ==
             Oot3dNativeGame::kOot3dPlayerRandomTurnTimerEpilogue &&
             refreshState.r[14] == kReturnSentinel &&
             ReadRandomTurnTimer(memory) == 0,
         "intermediate random turn refresh consumed RNG or timer state");
}

void WriteAttentionPersistenceCounter(NativeA32Memory &memory,
                                      std::uint8_t counter) {
  WritePlayerField(
      memory, offsetof(PlayerWireState, AttentionPersistenceCounter),
      counter);
}

std::uint8_t
ReadAttentionPersistenceCounter(const NativeA32Memory &memory) {
  return ReadObject<std::uint8_t>(
      memory, kPlayerAddress +
                  offsetof(PlayerWireState,
                           AttentionPersistenceCounter));
}

oot3d::recomp::a32::GuestState
BuildAttentionPersistenceBlockState() {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 0xA5A5A5A5U;
  state.r[4] = kPlayerAddress;
  state.r[5] = kPlayerAddress + 0x1000U;
  state.r[13] = kStackTop;
  state.r[14] = kReturnSentinel;
  state.r[15] =
      Oot3dNativeGame::kOot3dPlayerAttentionPersistenceAdvanceBlock;
  state.cpsr = 0x51234567U;
  return state;
}

void TestAttentionPersistenceDomain() {
  std::uint8_t counter = 3U;
  oot3d::gameplay::PlayerAdvanceAttentionPersistenceCounter(
      counter, PlayerTime(false, 1.0F));
  Expect(counter == 3U,
         "attention persistence advanced between logical frames");
  oot3d::gameplay::PlayerAdvanceAttentionPersistenceCounter(
      counter, PlayerTime(true, 1.0F));
  Expect(counter == 4U,
         "attention persistence did not advance on a logical frame");

  counter = 0xFFU;
  oot3d::gameplay::PlayerAdvanceAttentionPersistenceCounter(
      counter, PlayerTime(true, 2.0F));
  Expect(counter == 0U,
         "attention persistence did not preserve 0xFF wrap");
  counter = 0xFEU;
  oot3d::gameplay::PlayerAdvanceAttentionPersistenceCounter(
      counter, PlayerTime(true, 2.0F));
  Expect(counter == 0xFEU,
         "attention persistence did not preserve 0xFE saturation");
}

void TestAttentionPersistenceBlockNativeParity() {
  constexpr std::array cases{
      std::uint8_t{0U},
      std::uint8_t{3U},
      std::uint8_t{0xFDU},
      std::uint8_t{0xFEU},
      std::uint8_t{0xFFU},
  };
  for (const std::uint8_t initial : cases) {
    auto referenceMemory = BuildMemory();
    WriteAttentionPersistenceCounter(referenceMemory, initial);
    auto referenceState = BuildAttentionPersistenceBlockState();
    auto typedMemory = referenceMemory;
    auto typedState = referenceState;

    const auto referenceResult = ExecuteNativeBlock(
        Oot3dNativeGame::kOot3dPlayerAttentionPersistenceAdvanceBlock,
        referenceState, referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::
                   kOot3dPlayerAttentionPersistenceAdvanceBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           "typed attention persistence block retained native A32");
    Expect(typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           "typed attention persistence block returned incorrectly");
    const std::string label =
        "attention-persistence-native-parity-" +
        std::to_string(static_cast<std::uint32_t>(initial));
    ExpectCoreStateEqual(typedState, referenceState, label);
    ExpectRuntimeEqual(typedMemory, referenceMemory, label);
  }
}

void TestAttentionPersistenceBlockEnhanced60() {
  auto memory = BuildMemory();
  WriteAttentionPersistenceCounter(memory, 3U);

  const auto execute = [&](bool crossedLogicalFrame) {
    auto state = BuildAttentionPersistenceBlockState();
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(crossedLogicalFrame, 1.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::
                   kOot3dPlayerAttentionPersistenceAdvanceBlock,
               state, memory, &result, {1.0F, &time}, &blocksConsumed),
           "enhanced attention persistence block retained native A32");
    Expect(result.pc ==
               Oot3dNativeGame::kOot3dPlayerAttentionPersistenceContinue &&
               blocksConsumed == 1U,
           "enhanced attention persistence block selected wrong successor");
  };

  execute(false);
  Expect(ReadAttentionPersistenceCounter(memory) == 3U,
         "attention persistence advanced on an intermediate substep");
  execute(true);
  Expect(ReadAttentionPersistenceCounter(memory) == 4U,
         "attention persistence did not cross its authored threshold");
  execute(false);
  Expect(ReadAttentionPersistenceCounter(memory) == 4U,
         "attention persistence advanced twice in one logical frame");
}

oot3d::recomp::a32::GuestState BuildCommonCountdownBlockState(
    NativeA32Memory &memory, std::uint32_t stateFlags) {
  WriteU32(memory, kStackTop + 0x3CU, 0xDEADC0DEU);
  WriteU32(memory, kStackTop + 0x40U, kPlayerAddress + 0x2400U);
  WritePlayerField(memory, offsetof(PlayerWireState, StateFlags), stateFlags);

  oot3d::recomp::a32::GuestState state;
  state.r[0] = 0xCAFEU;
  state.r[1] = 0xBABEU;
  state.r[4] = kPlayerAddress;
  state.r[5] = 0xD15EA5EU;
  state.r[6] = kPlayerAddress + 0x2000U;
  state.r[10] = kPlayAddress;
  state.r[13] = kStackTop;
  state.r[14] = kReturnSentinel;
  state.r[15] = Oot3dNativeGame::kOot3dPlayerCommonCountdownBlock;
  state.cpsr = 0xF1234567U;
  return state;
}

void WriteCommonCountdownState(
    NativeA32Memory &memory,
    const oot3d::gameplay::PlayerCommonCountdownState &state) {
  WritePlayerField(memory, offsetof(PlayerWireState, ItemActionCooldownTimer),
                   state.ItemActionCooldownTimer);
  WritePlayerField(
      memory, offsetof(PlayerWireState, TextboxButtonCooldownTimer),
      state.TextboxButtonCooldownTimer);
  WritePlayerField(memory, offsetof(PlayerWireState, FairyReviveGraceTimer),
                   state.FairyReviveGraceTimer);
  WritePlayerField(memory, offsetof(PlayerWireState, CollisionSfxCooldownTimer),
                   state.CollisionSfxCooldownTimer);
}

oot3d::gameplay::PlayerCommonCountdownState ReadCommonCountdownState(
    const NativeA32Memory &memory) {
  return {
      ReadObject<std::uint8_t>(
          memory, kPlayerAddress +
                      offsetof(PlayerWireState, ItemActionCooldownTimer)),
      ReadObject<std::uint8_t>(
          memory, kPlayerAddress +
                      offsetof(PlayerWireState,
                               TextboxButtonCooldownTimer)),
      ReadObject<std::uint8_t>(
          memory, kPlayerAddress +
                      offsetof(PlayerWireState, FairyReviveGraceTimer)),
      ReadObject<std::uint8_t>(
          memory, kPlayerAddress +
                      offsetof(PlayerWireState, CollisionSfxCooldownTimer)),
  };
}

void TestCommonCountdownDomain() {
  oot3d::gameplay::PlayerCommonCountdownState intermediate{
      2U, 2U, 2U, 2U};
  oot3d::gameplay::PlayerUpdateCommonCountdowns(
      intermediate, PlayerTime(false, 1.0F));
  Expect(intermediate.ItemActionCooldownTimer == 2U &&
             intermediate.TextboxButtonCooldownTimer == 2U &&
             intermediate.CollisionSfxCooldownTimer == 2U &&
             intermediate.FairyReviveGraceTimer == 1U,
         "common countdown fields lost their mixed native cadence");

  oot3d::gameplay::PlayerCommonCountdownState native30{
      4U, 4U, 30U, 4U};
  for (std::uint32_t tick = 0U; tick < 4U; ++tick) {
    oot3d::gameplay::PlayerUpdateCommonCountdowns(
        native30, PlayerTime(true, 2.0F));
  }
  oot3d::gameplay::PlayerCommonCountdownState enhanced60{
      4U, 4U, 60U, 4U};
  for (std::uint32_t tick = 0U; tick < 8U; ++tick) {
    oot3d::gameplay::PlayerUpdateCommonCountdowns(
        enhanced60, PlayerTime((tick & 1U) != 0U, 1.0F));
  }
  Expect(native30.ItemActionCooldownTimer == 0U &&
             native30.TextboxButtonCooldownTimer == 0U &&
             native30.CollisionSfxCooldownTimer == 0U &&
             enhanced60.ItemActionCooldownTimer == 0U &&
             enhanced60.TextboxButtonCooldownTimer == 0U &&
             enhanced60.CollisionSfxCooldownTimer == 0U,
         "common logical countdown duration changed at enhanced 60 Hz");
  Expect(static_cast<std::uint32_t>(native30.FairyReviveGraceTimer) * 2U ==
             enhanced60.FairyReviveGraceTimer,
         "rate-scaled Fairy grace timer changed wall-clock duration");
}

void TestCommonCountdownBlockNativeParity() {
  struct TestCase {
    oot3d::gameplay::PlayerCommonCountdownState Timers;
    std::uint32_t StateFlags = 0U;
    const char *Label = nullptr;
  };
  constexpr std::array cases{
      TestCase{{0U, 0U, 0U, 0U}, 0U, "all-zero"},
      TestCase{{1U, 2U, 3U, 4U}, 0x02000000U, "first-state-bit"},
      TestCase{{255U, 1U, 1U, 0U}, 0x20000000U, "second-state-bit"},
      TestCase{{7U, 8U, 9U, 0x80U}, 0x22000000U,
               "actor-branch-signed-wrap"},
  };

  for (const auto &test : cases) {
    auto referenceMemory = BuildMemory();
    WriteCommonCountdownState(referenceMemory, test.Timers);
    auto referenceState =
        BuildCommonCountdownBlockState(referenceMemory, test.StateFlags);
    auto typedMemory = referenceMemory;
    auto typedState = referenceState;

    const auto referenceResult = ExecuteNativeBlock(
        Oot3dNativeGame::kOot3dPlayerCommonCountdownBlock, referenceState,
        referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerCommonCountdownBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           std::string("typed common countdown block retained A32: ") +
               test.Label);
    Expect(typedResult.pc == referenceResult.pc && blocksConsumed == 1U,
           std::string("typed common countdown block returned incorrectly: ") +
               test.Label);
    ExpectCoreStateEqual(
        typedState, referenceState,
        std::string("common-countdown-native-parity-") + test.Label);
    ExpectRuntimeEqual(
        typedMemory, referenceMemory,
        std::string("common-countdown-native-parity-") + test.Label);
    Expect(ReadObject<std::uint32_t>(typedMemory, kStackTop + 0x3CU) ==
               ReadObject<std::uint32_t>(referenceMemory,
                                         kStackTop + 0x3CU),
           std::string("common countdown stack slot mismatch: ") +
               test.Label);
  }
}

void TestCommonCountdownBlockEnhanced60() {
  auto memory = BuildMemory();
  WriteCommonCountdownState(memory, {2U, 2U, 4U, 2U});

  const auto execute = [&](bool crossedLogicalFrame) {
    auto state = BuildCommonCountdownBlockState(memory, 0U);
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(crossedLogicalFrame, 1.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerCommonCountdownBlock, state,
               memory, &result, {1.0F, &time}, &blocksConsumed),
           "enhanced common countdown block retained native A32");
    Expect(result.pc ==
                   Oot3dNativeGame::kOot3dPlayerInvincibilityTimerBlock &&
               blocksConsumed == 1U,
           "enhanced common countdown block returned to the wrong boundary");
  };

  execute(false);
  auto timers = ReadCommonCountdownState(memory);
  Expect(timers.ItemActionCooldownTimer == 2U &&
             timers.TextboxButtonCooldownTimer == 2U &&
             timers.FairyReviveGraceTimer == 3U &&
             timers.CollisionSfxCooldownTimer == 2U,
         "common countdown cadence changed on first enhanced substep");
  execute(true);
  timers = ReadCommonCountdownState(memory);
  Expect(timers.ItemActionCooldownTimer == 1U &&
             timers.TextboxButtonCooldownTimer == 1U &&
             timers.FairyReviveGraceTimer == 2U &&
             timers.CollisionSfxCooldownTimer == 1U,
         "common countdowns did not advance on a logical frame");
  execute(false);
  execute(true);
  timers = ReadCommonCountdownState(memory);
  Expect(timers.ItemActionCooldownTimer == 0U &&
             timers.TextboxButtonCooldownTimer == 0U &&
             timers.FairyReviveGraceTimer == 0U &&
             timers.CollisionSfxCooldownTimer == 0U,
         "common countdowns changed their native expiration");
}

oot3d::recomp::a32::GuestState BuildInvincibilityTimerBlockState(
    NativeA32Memory &memory, std::uint32_t actionFunction) {
  WriteU32(memory, kStackTop + 0x40U, kPlayerAddress + 0x2400U);
  WritePlayerField(memory, offsetof(PlayerWireState, ActionFunction),
                   actionFunction);

  oot3d::recomp::a32::GuestState state;
  state.r[0] = 0xCAFEU;
  state.r[1] = 0xBABEU;
  state.r[2] = 0xD15EA5EU;
  state.r[4] = kPlayerAddress;
  state.r[5] = kPlayerAddress + 0x1000U;
  state.r[6] = kPlayerAddress + 0x2000U;
  state.r[10] = kPlayAddress;
  state.r[13] = kStackTop;
  state.r[14] = kReturnSentinel;
  state.r[15] = Oot3dNativeGame::kOot3dPlayerInvincibilityTimerBlock;
  state.cpsr = 0xF1234567U;
  return state;
}

void TestInvincibilityTimerDomain() {
  std::int8_t nativeNegative = -3;
  std::int8_t enhancedNegative = -3;
  for (std::uint32_t tick = 0U; tick < 3U; ++tick) {
    oot3d::gameplay::PlayerUpdateInvincibilityTimer(
        nativeNegative, false, PlayerTime(true, 2.0F));
  }
  for (std::uint32_t tick = 0U; tick < 6U; ++tick) {
    oot3d::gameplay::PlayerUpdateInvincibilityTimer(
        enhancedNegative, false,
        PlayerTime((tick & 1U) != 0U, 1.0F));
  }
  Expect(nativeNegative == 0 && enhancedNegative == nativeNegative,
         "signed invulnerability duration changed at enhanced 60 Hz");

  std::int8_t nativePositive = 3;
  std::int8_t enhancedPositive = 3;
  for (std::uint32_t tick = 0U; tick < 3U; ++tick) {
    oot3d::gameplay::PlayerUpdateInvincibilityTimer(
        nativePositive, false, PlayerTime(true, 2.0F));
  }
  for (std::uint32_t tick = 0U; tick < 6U; ++tick) {
    oot3d::gameplay::PlayerUpdateInvincibilityTimer(
        enhancedPositive, false,
        PlayerTime((tick & 1U) != 0U, 1.0F));
  }
  Expect(nativePositive == 0 && enhancedPositive == nativePositive,
         "positive intangibility duration changed at enhanced 60 Hz");

  std::int8_t heldPositive = 3;
  oot3d::gameplay::PlayerUpdateInvincibilityTimer(
      heldPositive, true, PlayerTime(true, 1.0F));
  Expect(heldPositive == 3,
         "native action did not suspend positive intangibility");
}

void TestInvincibilityTimerBlockNativeParity() {
  struct TestCase {
    std::int8_t Timer = 0;
    std::uint32_t ActionFunction = 0U;
    const char *Label = nullptr;
  };
  constexpr std::array cases{
      TestCase{-3, 0x00400000U, "negative"},
      TestCase{0, 0x00400000U, "zero"},
      TestCase{4, 0x00400000U, "positive-countdown"},
      TestCase{4, kInvincibilityHoldActions[0], "positive-hold-0"},
      TestCase{4, kInvincibilityHoldActions[1], "positive-hold-1"},
      TestCase{4, kInvincibilityHoldActions[2], "positive-hold-2"},
      TestCase{4, kInvincibilityHoldActions[3], "positive-hold-3"},
  };

  for (const auto &test : cases) {
    auto referenceMemory = BuildMemory();
    auto typedMemory = referenceMemory;
    WritePlayerField(referenceMemory,
                     offsetof(PlayerWireState, InvincibilityTimer),
                     test.Timer);
    WritePlayerField(typedMemory,
                     offsetof(PlayerWireState, InvincibilityTimer),
                     test.Timer);
    auto referenceState = BuildInvincibilityTimerBlockState(
        referenceMemory, test.ActionFunction);
    auto typedState = BuildInvincibilityTimerBlockState(
        typedMemory, test.ActionFunction);

    const auto referenceResult = ExecuteNativeUntil(
        Oot3dNativeGame::kOot3dPlayerInvincibilityTimerBlock,
        Oot3dNativeGame::kOot3dPlayerDamageRunTimerBlock, referenceState,
        referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerInvincibilityTimerBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           std::string("typed invincibility block retained A32: ") +
               test.Label);
    Expect(referenceResult.pc ==
                   Oot3dNativeGame::kOot3dPlayerDamageRunTimerBlock &&
               typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           std::string("typed invincibility block returned incorrectly: ") +
               test.Label);
    ExpectCoreStateEqual(
        typedState, referenceState,
        std::string("invincibility-native-parity-") + test.Label);
    ExpectRuntimeEqual(
        typedMemory, referenceMemory,
        std::string("invincibility-native-parity-") + test.Label);
  }
}

void TestInvincibilityTimerBlockEnhanced60() {
  auto memory = BuildMemory();
  WritePlayerField(memory, offsetof(PlayerWireState, InvincibilityTimer),
                   std::int8_t{-2});

  const auto execute = [&](bool crossedLogicalFrame,
                           std::uint32_t actionFunction) {
    auto state =
        BuildInvincibilityTimerBlockState(memory, actionFunction);
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(crossedLogicalFrame, 1.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerInvincibilityTimerBlock, state,
               memory, &result, {1.0F, &time}, &blocksConsumed),
           "enhanced invincibility block retained native A32");
    Expect(result.pc ==
                   Oot3dNativeGame::kOot3dPlayerDamageRunTimerBlock &&
               blocksConsumed == 1U,
           "enhanced invincibility block returned to the wrong boundary");
  };
  const auto readTimer = [&]() {
    return ReadObject<std::int8_t>(
        memory, kPlayerAddress +
                    offsetof(PlayerWireState, InvincibilityTimer));
  };

  execute(false, 0x00400000U);
  Expect(readTimer() == -2,
         "negative invincibility timer advanced on an intermediate substep");
  execute(true, 0x00400000U);
  Expect(readTimer() == -1,
         "negative invincibility timer did not approach zero");

  WritePlayerField(memory, offsetof(PlayerWireState, InvincibilityTimer),
                   std::int8_t{2});
  execute(false, 0x00400000U);
  Expect(readTimer() == 2,
         "positive invincibility timer advanced on an intermediate substep");
  execute(true, 0x00400000U);
  Expect(readTimer() == 1,
         "positive invincibility timer did not approach zero");

  WritePlayerField(memory, offsetof(PlayerWireState, InvincibilityTimer),
                   std::int8_t{2});
  execute(true, kInvincibilityHoldActions[0]);
  Expect(readTimer() == 2,
         "exception action did not hold positive invincibility");
}

oot3d::recomp::a32::GuestState BuildDamageFlickerCounterBlockState(
    std::uint32_t authoredAdvance) {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 0xCAFEU;
  state.r[4] = kPlayerAddress;
  state.r[5] = kPlayerAddress + 0x2000U;
  state.r[11] = authoredAdvance;
  state.r[14] = kReturnSentinel;
  state.r[15] = Oot3dNativeGame::kOot3dPlayerDamageFlickerCounterBlock;
  state.cpsr = 0xF1234567U;
  return state;
}

void TestDamageFlickerCounterDomain() {
  std::uint8_t native30Counter = 250U;
  oot3d::gameplay::PlayerAdvanceDamageFlickerAnimationCounter(
      native30Counter, 40U, PlayerTime(true, 2.0F));
  Expect(native30Counter == 34U,
         "damage flicker counter lost its native byte wrap");

  std::uint8_t enhanced60Counter = 10U;
  oot3d::gameplay::PlayerAdvanceDamageFlickerAnimationCounter(
      enhanced60Counter, 12U, PlayerTime(false, 1.0F));
  Expect(enhanced60Counter == 10U,
         "damage flicker phase advanced on an intermediate substep");
  oot3d::gameplay::PlayerAdvanceDamageFlickerAnimationCounter(
      enhanced60Counter, 12U, PlayerTime(true, 1.0F));
  Expect(enhanced60Counter == 22U,
         "damage flicker phase did not advance on a logical frame");
}

void TestDamageFlickerCounterBlockNativeParity() {
  struct TestCase {
    std::uint8_t Counter = 0U;
    std::uint32_t Advance = 0U;
    const char *Label = nullptr;
  };
  constexpr std::array cases{
      TestCase{3U, 4U, "clamp-low"},
      TestCase{3U, 8U, "lower-bound"},
      TestCase{3U, 12U, "authored-step"},
      TestCase{3U, 50U, "clamp-high"},
      TestCase{250U, 40U, "byte-wrap"},
  };

  for (const auto &test : cases) {
    auto referenceMemory = BuildMemory();
    auto typedMemory = referenceMemory;
    WritePlayerField(
        referenceMemory,
        offsetof(PlayerWireState, DamageFlickerAnimationCounter),
        test.Counter);
    WritePlayerField(
        typedMemory,
        offsetof(PlayerWireState, DamageFlickerAnimationCounter),
        test.Counter);
    auto referenceState =
        BuildDamageFlickerCounterBlockState(test.Advance);
    auto typedState = referenceState;

    const auto referenceResult = ExecuteNativeUntil(
        Oot3dNativeGame::kOot3dPlayerDamageFlickerCounterBlock,
        Oot3dNativeGame::kOot3dPlayerDamageFlickerCounterContinue,
        referenceState, referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerDamageFlickerCounterBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           std::string("typed damage flicker block retained A32: ") +
               test.Label);
    Expect(referenceResult.pc ==
                   Oot3dNativeGame::
                       kOot3dPlayerDamageFlickerCounterContinue &&
               typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           std::string("typed damage flicker block returned incorrectly: ") +
               test.Label);
    ExpectCoreStateEqual(
        typedState, referenceState,
        std::string("damage-flicker-native-parity-") + test.Label);
    ExpectRuntimeEqual(
        typedMemory, referenceMemory,
        std::string("damage-flicker-native-parity-") + test.Label);
  }
}

void TestDamageFlickerCounterBlockEnhanced60() {
  auto memory = BuildMemory();
  WritePlayerField(
      memory, offsetof(PlayerWireState, DamageFlickerAnimationCounter),
      std::uint8_t{0U});

  const auto execute = [&](bool crossedLogicalFrame) {
    auto state = BuildDamageFlickerCounterBlockState(10U);
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(crossedLogicalFrame, 1.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerDamageFlickerCounterBlock,
               state, memory, &result, {1.0F, &time}, &blocksConsumed),
           "enhanced damage flicker block retained native A32");
    Expect(result.pc ==
                   Oot3dNativeGame::
                       kOot3dPlayerDamageFlickerCounterContinue &&
               blocksConsumed == 1U,
           "enhanced damage flicker block returned to the wrong boundary");
  };
  const auto readCounter = [&]() {
    return ReadObject<std::uint8_t>(
        memory, kPlayerAddress +
                    offsetof(PlayerWireState,
                             DamageFlickerAnimationCounter));
  };

  execute(false);
  Expect(readCounter() == 0U,
         "damage flicker counter advanced on an intermediate substep");
  execute(true);
  Expect(readCounter() == 10U,
         "damage flicker counter did not advance on a logical frame");
  execute(false);
  Expect(readCounter() == 10U,
         "damage flicker counter advanced twice in one logical frame");
  execute(true);
  Expect(readCounter() == 20U,
         "damage flicker counter changed its native cadence");
}

oot3d::recomp::a32::GuestState BuildDamageRunTimerBlockState() {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 0xCAFEU;
  state.r[1] = 0xBABEU;
  state.r[4] = kPlayerAddress;
  state.r[6] = kPlayerAddress + 0x2000U;
  state.r[10] = kPlayAddress;
  state.r[14] = kReturnSentinel;
  state.r[15] = Oot3dNativeGame::kOot3dPlayerDamageRunTimerBlock;
  state.cpsr = 0xF1234567U;
  return state;
}

void TestDamageRunTimerDomain() {
  std::uint8_t native30Timer = 3U;
  for (std::uint32_t tick = 0U; tick < 3U; ++tick) {
    oot3d::gameplay::PlayerUpdateDamageRunTimer(
        native30Timer, PlayerTime(true, 2.0F));
  }
  Expect(native30Timer == 0U,
         "native damage-run timer changed its logical duration");

  std::uint8_t enhanced60Timer = 3U;
  for (std::uint32_t tick = 0U; tick < 6U; ++tick) {
    oot3d::gameplay::PlayerUpdateDamageRunTimer(
        enhanced60Timer, PlayerTime((tick & 1U) != 0U, 1.0F));
  }
  Expect(enhanced60Timer == native30Timer,
         "enhanced damage-run timer changed its wall-clock duration");

  oot3d::gameplay::PlayerUpdateDamageRunTimer(
      enhanced60Timer, PlayerTime(true, 1.0F));
  Expect(enhanced60Timer == 0U,
         "expired damage-run timer underflowed");
}

void TestDamageRunTimerBlockNativeParity() {
  for (const std::uint8_t initialTimer :
       {std::uint8_t{0U}, std::uint8_t{1U}, std::uint8_t{30U},
        std::uint8_t{255U}}) {
    auto referenceMemory = BuildMemory();
    auto typedMemory = referenceMemory;
    WritePlayerField(referenceMemory,
                     offsetof(PlayerWireState, DamageRunTimer), initialTimer);
    WritePlayerField(typedMemory, offsetof(PlayerWireState, DamageRunTimer),
                     initialTimer);
    auto referenceState = BuildDamageRunTimerBlockState();
    auto typedState = referenceState;

    const auto referenceResult = ExecuteNativeBlock(
        Oot3dNativeGame::kOot3dPlayerDamageRunTimerBlock, referenceState,
        referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerDamageRunTimerBlock, typedState,
               typedMemory, &typedResult, {2.0F, &time}, &blocksConsumed),
           "typed damage-run timer block retained native A32");
    Expect(referenceResult.pc ==
                   Oot3dNativeGame::
                       kOot3dPlayerUpdateContextActionAndSequenceStateEntry &&
               typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           "typed damage-run timer returned to the wrong call target");
    ExpectCoreStateEqual(
        typedState, referenceState,
        "damage-run-timer-native-parity-" +
            std::to_string(static_cast<std::uint32_t>(initialTimer)));
    ExpectRuntimeEqual(
        typedMemory, referenceMemory,
        "damage-run-timer-native-parity-" +
            std::to_string(static_cast<std::uint32_t>(initialTimer)));
  }
}

void TestDamageRunTimerBlockEnhanced60() {
  auto memory = BuildMemory();
  WritePlayerField(memory, offsetof(PlayerWireState, DamageRunTimer),
                   std::uint8_t{2U});

  const auto execute = [&](bool crossedLogicalFrame) {
    auto state = BuildDamageRunTimerBlockState();
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(crossedLogicalFrame, 1.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerDamageRunTimerBlock, state,
               memory, &result, {1.0F, &time}, &blocksConsumed),
           "enhanced damage-run timer block retained native A32");
    Expect(result.pc ==
                   Oot3dNativeGame::
                       kOot3dPlayerUpdateContextActionAndSequenceStateEntry &&
               state.r[14] ==
                   Oot3dNativeGame::kOot3dPlayerDamageRunTimerReturn &&
               blocksConsumed == 1U,
           "enhanced damage-run timer did not preserve the native call");
  };

  const auto readTimer = [&]() {
    return ReadObject<std::uint8_t>(
        memory, kPlayerAddress +
                    offsetof(PlayerWireState, DamageRunTimer));
  };
  execute(false);
  Expect(readTimer() == 2U,
         "damage-run timer advanced on an intermediate substep");
  execute(true);
  Expect(readTimer() == 1U,
         "damage-run timer did not advance on a logical frame");
  execute(false);
  Expect(readTimer() == 1U,
         "damage-run timer advanced twice in one logical frame");
  execute(true);
  Expect(readTimer() == 0U,
         "damage-run timer did not expire on schedule");
  execute(false);
  Expect(readTimer() == 0U,
         "expired damage-run timer changed on an intermediate substep");
}

oot3d::recomp::a32::GuestState BuildMeleeActionTimerBlockState() {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 0xCAFEU;
  state.r[4] = kPlayerAddress;
  state.r[6] = kPlayerAddress + 0x2000U;
  state.r[7] = kPlayerAddress + 0x2200U;
  state.r[15] = Oot3dNativeGame::kOot3dPlayerMeleeActionTimerBlock;
  state.cpsr = 0x01234567U;
  return state;
}

oot3d::gameplay::PlayerMeleeActionTimingState
ReadMeleeActionTiming(const NativeA32Memory &memory) {
  return {
      ReadObject<std::int8_t>(
          memory, kPlayerAddress +
                      offsetof(PlayerWireState, MeleeWeaponActionTimer)),
      ReadObject<std::uint8_t>(
          memory, kPlayerAddress +
                      offsetof(PlayerWireState, MeleeWeaponComboState)),
  };
}

void TestMeleeActionTimingDomain() {
  oot3d::gameplay::PlayerMeleeActionTimingState native30{8, 4U};
  for (std::uint32_t tick = 0; tick < 8U; ++tick) {
    oot3d::gameplay::PlayerUpdateMeleeActionTiming(
        native30, PlayerTime(true, 2.0F));
  }
  Expect(native30.Timer == 0 && native30.ComboState == 4U,
         "native melee timer did not preserve the terminal-frame combo");
  oot3d::gameplay::PlayerUpdateMeleeActionTiming(
      native30, PlayerTime(true, 2.0F));
  Expect(native30.ComboState == 0U,
         "native melee combo was not cleared after timer expiry");

  oot3d::gameplay::PlayerMeleeActionTimingState enhanced60{8, 4U};
  for (std::uint32_t tick = 0; tick < 16U; ++tick) {
    oot3d::gameplay::PlayerUpdateMeleeActionTiming(
        enhanced60, PlayerTime((tick & 1U) != 0U, 1.0F));
  }
  Expect(enhanced60.Timer == 0 && enhanced60.ComboState == 4U,
         "enhanced melee timer changed its logical duration");
  oot3d::gameplay::PlayerUpdateMeleeActionTiming(
      enhanced60, PlayerTime(false, 1.0F));
  Expect(enhanced60.ComboState == 0U,
         "enhanced melee combo clear waited for another logical frame");

  oot3d::gameplay::PlayerMeleeActionTimingState negative{-2, 1U};
  oot3d::gameplay::PlayerUpdateMeleeActionTiming(
      negative, PlayerTime(false, 1.0F));
  Expect(negative.Timer == -2,
         "negative melee timer advanced between logical frames");
  oot3d::gameplay::PlayerUpdateMeleeActionTiming(
      negative, PlayerTime(true, 1.0F));
  Expect(negative.Timer == -1,
         "negative melee timer did not approach zero");
}

void TestMeleeActionTimerBlockNativeParity() {
  for (const std::int8_t initialTimer : {
           std::int8_t{-3}, std::int8_t{0}, std::int8_t{4}}) {
    auto referenceMemory = BuildMemory();
    auto typedMemory = referenceMemory;
    WritePlayerField(referenceMemory,
                     offsetof(PlayerWireState, MeleeWeaponActionTimer),
                     initialTimer);
    WritePlayerField(typedMemory,
                     offsetof(PlayerWireState, MeleeWeaponActionTimer),
                     initialTimer);
    WritePlayerField(referenceMemory,
                     offsetof(PlayerWireState, MeleeWeaponComboState),
                     std::uint8_t{7U});
    WritePlayerField(typedMemory,
                     offsetof(PlayerWireState, MeleeWeaponComboState),
                     std::uint8_t{7U});
    auto referenceState = BuildMeleeActionTimerBlockState();
    auto typedState = referenceState;

    auto referenceResult = ExecuteNativeBlock(
        Oot3dNativeGame::kOot3dPlayerMeleeActionTimerBlock, referenceState,
        referenceMemory);
    for (std::uint32_t step = 0U;
         referenceResult.pc !=
             Oot3dNativeGame::kOot3dPlayerMeleeActionTimerContinue &&
         step < 2U;
         ++step) {
      referenceResult =
          ExecuteNativeBlock(referenceResult.pc, referenceState,
                             referenceMemory);
    }

    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerMeleeActionTimerBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           "typed melee timer block retained native A32");
    Expect(referenceResult.pc ==
                   Oot3dNativeGame::kOot3dPlayerMeleeActionTimerContinue &&
               typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           "typed melee timer returned to the wrong boundary");
    ExpectCoreStateEqual(
        typedState, referenceState,
        "melee-timer-native-parity-" +
            std::to_string(static_cast<std::int32_t>(initialTimer)));
    ExpectRuntimeEqual(
        typedMemory, referenceMemory,
        "melee-timer-native-parity-" +
            std::to_string(static_cast<std::int32_t>(initialTimer)));
  }
}

void TestMeleeActionTimerBlockEnhanced60() {
  auto memory = BuildMemory();
  WritePlayerField(memory,
                   offsetof(PlayerWireState, MeleeWeaponActionTimer),
                   std::int8_t{2});
  WritePlayerField(memory,
                   offsetof(PlayerWireState, MeleeWeaponComboState),
                   std::uint8_t{7U});

  const auto execute = [&](bool crossedLogicalFrame) {
    auto state = BuildMeleeActionTimerBlockState();
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(crossedLogicalFrame, 1.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerMeleeActionTimerBlock, state,
               memory, &result, {1.0F, &time}, &blocksConsumed),
           "enhanced melee timer block retained native A32");
    Expect(result.pc ==
                   Oot3dNativeGame::kOot3dPlayerMeleeActionTimerContinue &&
               blocksConsumed == 1U,
           "enhanced melee timer returned to the wrong boundary");
  };

  execute(false);
  Expect(ReadMeleeActionTiming(memory).Timer == 2,
         "enhanced melee timer advanced on an intermediate substep");
  execute(true);
  Expect(ReadMeleeActionTiming(memory).Timer == 1,
         "enhanced melee timer did not advance on a logical frame");
  execute(false);
  Expect(ReadMeleeActionTiming(memory).Timer == 1,
         "enhanced melee timer advanced twice in one logical frame");
  execute(true);
  const auto terminal = ReadMeleeActionTiming(memory);
  Expect(terminal.Timer == 0 && terminal.ComboState == 7U,
         "enhanced melee timer changed terminal-frame semantics");
  execute(false);
  const auto cleared = ReadMeleeActionTiming(memory);
  Expect(cleared.Timer == 0 && cleared.ComboState == 0U,
         "enhanced melee combo did not clear after timer expiry");
}

oot3d::recomp::a32::GuestState BuildMeleeWeaponTipComboBlockState(
    std::uint8_t comboState) {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = kPlayerAddress + 0x2000U;
  state.r[1] = comboState;
  state.r[2] = 0x1000C000U;
  state.r[15] =
      Oot3dNativeGame::kOot3dPlayerMeleeWeaponTipComboAdvanceBlock;
  state.cpsr = comboState == 3U ? 0x61234567U : 0x21234567U;
  state.fpscr = 0x02000000U;
  state.vfp[0] = std::bit_cast<std::uint32_t>(1234.5F);
  return state;
}

void TestMeleeWeaponTipComboDomain() {
  std::uint8_t native30State = 3U;
  oot3d::gameplay::PlayerAdvanceMeleeWeaponTipComboState(
      native30State, PlayerTime(true, 2.0F));
  Expect(native30State == 4U,
         "native melee tip combo state did not advance");

  std::uint8_t enhanced60State = 3U;
  oot3d::gameplay::PlayerAdvanceMeleeWeaponTipComboState(
      enhanced60State, PlayerTime(false, 1.0F));
  Expect(enhanced60State == 3U,
         "enhanced melee tip combo advanced between logical frames");
  oot3d::gameplay::PlayerAdvanceMeleeWeaponTipComboState(
      enhanced60State, PlayerTime(true, 1.0F));
  Expect(enhanced60State == native30State,
         "enhanced melee tip combo changed logical cadence");

  std::uint8_t inactiveState = 2U;
  oot3d::gameplay::PlayerAdvanceMeleeWeaponTipComboState(
      inactiveState, PlayerTime(true, 1.0F));
  Expect(inactiveState == 2U,
         "melee tip combo helper advanced an inactive state");
}

void TestMeleeWeaponTipComboBlockNativeParity() {
  for (const std::uint8_t initialState :
       {std::uint8_t{3U}, std::uint8_t{9U}, std::uint8_t{255U}}) {
    auto referenceMemory = BuildMemory();
    auto typedMemory = referenceMemory;
    WritePlayerField(referenceMemory,
                     offsetof(PlayerWireState, MeleeWeaponComboState),
                     initialState);
    WritePlayerField(typedMemory,
                     offsetof(PlayerWireState, MeleeWeaponComboState),
                     initialState);
    auto referenceState =
        BuildMeleeWeaponTipComboBlockState(initialState);
    auto typedState = referenceState;

    const auto referenceResult = ExecuteNativeBlock(
        Oot3dNativeGame::kOot3dPlayerMeleeWeaponTipComboAdvanceBlock,
        referenceState, referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerMeleeWeaponTipComboAdvanceBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           "typed melee tip combo block retained native A32");
    Expect(referenceResult.pc ==
                   Oot3dNativeGame::
                       kOot3dPlayerMeleeWeaponTipComboAdvanceContinue &&
               typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           "typed melee tip combo returned to the wrong boundary");
    ExpectCoreStateEqual(
        typedState, referenceState,
        "melee-tip-combo-native-parity-" +
            std::to_string(static_cast<std::uint32_t>(initialState)));
    Expect(typedState.fpscr == referenceState.fpscr &&
               typedState.vfp == referenceState.vfp,
           "typed melee tip combo changed native VFP semantics");
    ExpectRuntimeEqual(
        typedMemory, referenceMemory,
        "melee-tip-combo-native-parity-" +
            std::to_string(static_cast<std::uint32_t>(initialState)));
  }
}

void TestMeleeWeaponTipComboBlockEnhanced60() {
  auto memory = BuildMemory();
  WritePlayerField(memory,
                   offsetof(PlayerWireState, MeleeWeaponComboState),
                   std::uint8_t{3U});

  const auto execute = [&](bool crossedLogicalFrame,
                           std::uint8_t expectedInput) {
    auto state = BuildMeleeWeaponTipComboBlockState(expectedInput);
    oot3d::recomp::a32::ExecutionResult result;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(crossedLogicalFrame, 1.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerMeleeWeaponTipComboAdvanceBlock,
               state, memory, &result, {1.0F, &time}, &blocksConsumed),
           "enhanced melee tip combo block retained native A32");
    Expect(result.pc ==
                   Oot3dNativeGame::
                       kOot3dPlayerMeleeWeaponTipComboAdvanceContinue &&
               blocksConsumed == 1U,
           "enhanced melee tip combo returned to the wrong boundary");
  };

  execute(false, 3U);
  Expect(ReadMeleeActionTiming(memory).ComboState == 3U,
         "melee tip combo advanced on an intermediate substep");
  execute(true, 3U);
  Expect(ReadMeleeActionTiming(memory).ComboState == 4U,
         "melee tip combo did not advance on a logical frame");
}

void TestUnderwaterTimerDomain() {
  std::uint16_t native30Timer = 0U;
  for (std::uint32_t tick = 0;
       tick <
       oot3d::gameplay::kPlayerUnderwaterTimerSaturationLogicalFrames;
       ++tick) {
    oot3d::gameplay::PlayerUpdateUnderwaterTimer(
        native30Timer, true, PlayerTime(true, 2.0F));
  }
  Expect(native30Timer ==
             oot3d::gameplay::
                 kPlayerUnderwaterTimerSaturationLogicalFrames,
         "native 30 Hz underwater timer did not reach saturation");

  std::uint16_t enhanced60Timer = 0U;
  for (std::uint32_t tick = 0;
       tick <
       oot3d::gameplay::kPlayerUnderwaterTimerSaturationLogicalFrames * 2U;
       ++tick) {
    oot3d::gameplay::PlayerUpdateUnderwaterTimer(
        enhanced60Timer, true, PlayerTime((tick & 1U) != 0U, 1.0F));
  }
  Expect(enhanced60Timer == native30Timer,
         "enhanced 60 Hz underwater timer changed wall-clock duration");

  oot3d::gameplay::PlayerUpdateUnderwaterTimer(
      enhanced60Timer, true, PlayerTime(true, 1.0F));
  Expect(enhanced60Timer == native30Timer,
         "underwater timer exceeded native saturation");
  oot3d::gameplay::PlayerUpdateUnderwaterTimer(
      enhanced60Timer, false, PlayerTime(false, 1.0F));
  Expect(enhanced60Timer == 0U,
         "underwater timer reset was delayed to a logical frame");
}

void TestUnderwaterTimerResetBlockNativeParity() {
  auto referenceMemory = BuildMemory();
  auto typedMemory = referenceMemory;
  WritePlayerField(referenceMemory, offsetof(PlayerWireState, UnderwaterTimer),
                   std::uint16_t{173U});
  WritePlayerField(typedMemory, offsetof(PlayerWireState, UnderwaterTimer),
                   std::uint16_t{173U});
  auto referenceState = BuildUnderwaterTimerBlockState(
      Oot3dNativeGame::kOot3dPlayerUnderwaterTimerResetBlock);
  auto typedState = referenceState;

  auto referenceResult = ExecuteNativeBlock(
      Oot3dNativeGame::kOot3dPlayerUnderwaterTimerResetBlock, referenceState,
      referenceMemory);
  Expect(referenceResult.pc == 0x00252060U,
         "native underwater reset did not reach its store block");
  referenceResult =
      ExecuteNativeBlock(0x00252060U, referenceState, referenceMemory);

  oot3d::recomp::a32::ExecutionResult typedResult;
  std::uint32_t blocksConsumed = 0U;
  const auto time = PlayerTime(true, 2.0F);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPlayerUnderwaterTimerResetBlock,
             typedState, typedMemory, &typedResult, {2.0F, &time},
             &blocksConsumed),
         "typed underwater reset block retained native A32");
  Expect(referenceResult.pc ==
                 Oot3dNativeGame::kOot3dPlayerUnderwaterTimerContinue &&
             typedResult.pc == referenceResult.pc && blocksConsumed == 1U,
         "typed underwater reset returned to the wrong boundary");
  ExpectCoreStateEqual(typedState, referenceState,
                       "underwater-reset-native-parity");
  ExpectRuntimeEqual(typedMemory, referenceMemory,
                     "underwater-reset-native-parity");
}

void TestUnderwaterTimerIncrementBlockNativeParity() {
  for (const std::uint16_t initialTimer : {0U, 449U}) {
    auto prefixMemory = BuildMemory();
    WritePlayerField(prefixMemory, offsetof(PlayerWireState, UnderwaterTimer),
                     initialTimer);
    auto prefixState = BuildUnderwaterTimerBlockState(0x0025204CU);
    const auto prefixResult =
        ExecuteNativeBlock(0x0025204CU, prefixState, prefixMemory);
    Expect(prefixResult.pc ==
               Oot3dNativeGame::kOot3dPlayerUnderwaterTimerIncrementBlock,
           "native underwater prefix did not select the increment block");

    auto referenceMemory = prefixMemory;
    auto typedMemory = prefixMemory;
    auto referenceState = prefixState;
    auto typedState = prefixState;
    auto referenceResult = ExecuteNativeBlock(
        Oot3dNativeGame::kOot3dPlayerUnderwaterTimerIncrementBlock,
        referenceState, referenceMemory);
    Expect(referenceResult.pc == 0x00252060U,
           "native underwater increment did not reach its store block");
    referenceResult =
        ExecuteNativeBlock(0x00252060U, referenceState, referenceMemory);

    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    const auto time = PlayerTime(true, 2.0F);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerUnderwaterTimerIncrementBlock,
               typedState, typedMemory, &typedResult, {2.0F, &time},
               &blocksConsumed),
           "typed underwater increment block retained native A32");
    Expect(referenceResult.pc ==
                   Oot3dNativeGame::kOot3dPlayerUnderwaterTimerContinue &&
               typedResult.pc == referenceResult.pc &&
               blocksConsumed == 1U,
           "typed underwater increment returned to the wrong boundary");
    ExpectCoreStateEqual(
        typedState, referenceState,
        "underwater-increment-native-parity-" +
            std::to_string(initialTimer));
    ExpectRuntimeEqual(
        typedMemory, referenceMemory,
        "underwater-increment-native-parity-" +
            std::to_string(initialTimer));
  }
}

void TestUnderwaterTimerBlocksEnhanced60() {
  auto memory = BuildMemory();
  WritePlayerField(memory, offsetof(PlayerWireState, UnderwaterTimer),
                   std::uint16_t{448U});

  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;
  auto state = BuildUnderwaterTimerBlockState(
      Oot3dNativeGame::kOot3dPlayerUnderwaterTimerIncrementBlock);
  state.r[0] = 448U;
  state.r[1] =
      oot3d::gameplay::kPlayerUnderwaterTimerSaturationLogicalFrames;
  const auto intermediate = PlayerTime(false, 1.0F);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPlayerUnderwaterTimerIncrementBlock,
             state, memory, &result, {1.0F, &intermediate}, &blocksConsumed),
         "enhanced underwater block retained A32 on intermediate substep");
  Expect(ReadUnderwaterTimer(memory) == 448U,
         "enhanced underwater timer advanced between logical frames");

  state = BuildUnderwaterTimerBlockState(
      Oot3dNativeGame::kOot3dPlayerUnderwaterTimerIncrementBlock);
  state.r[0] = 448U;
  state.r[1] =
      oot3d::gameplay::kPlayerUnderwaterTimerSaturationLogicalFrames;
  blocksConsumed = 0U;
  const auto logical = PlayerTime(true, 1.0F);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPlayerUnderwaterTimerIncrementBlock,
             state, memory, &result, {1.0F, &logical}, &blocksConsumed),
         "enhanced underwater block retained A32 on logical frame");
  Expect(ReadUnderwaterTimer(memory) == 449U,
         "enhanced underwater timer did not advance on logical frame");

  state = BuildUnderwaterTimerBlockState(
      Oot3dNativeGame::kOot3dPlayerUnderwaterTimerResetBlock);
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPlayerUnderwaterTimerResetBlock, state,
             memory, &result, {1.0F, &intermediate}, &blocksConsumed),
         "enhanced underwater reset block retained A32");
  Expect(ReadUnderwaterTimer(memory) == 0U,
         "enhanced underwater reset waited for a logical frame");
}

void TestEntryCatalog() {
  constexpr std::array entries{
      Oot3dNativeGame::kOot3dPlayerRespawnDamageAdvanceBlock,
      Oot3dNativeGame::kOot3dPlayerCommonCountdownBlock,
      Oot3dNativeGame::kOot3dPlayerInvincibilityTimerBlock,
      Oot3dNativeGame::kOot3dPlayerDamageRunTimerBlock,
      Oot3dNativeGame::kOot3dPlayerAttentionPersistenceAdvanceBlock,
      Oot3dNativeGame::kOot3dPlayerFishingStateRecoveryBlock,
      Oot3dNativeGame::kOot3dPlayerMeleeActionTimerBlock,
      Oot3dNativeGame::kOot3dPlayerUnderwaterTimerResetBlock,
      Oot3dNativeGame::kOot3dPlayerUnderwaterTimerIncrementBlock,
      Oot3dNativeGame::kOot3dPlayerRandomTurnTimerDecrementBlock,
      Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock,
      Oot3dNativeGame::kOot3dPlayerMeleeWeaponTipComboAdvanceBlock,
      Oot3dNativeGame::kOot3dPlayerGetExplosiveHeldEntry,
      Oot3dNativeGame::kOot3dPlayerSetCsActionEntry,
      Oot3dNativeGame::kOot3dPlayerUpdateHostileLockOnEntry,
      Oot3dNativeGame::kOot3dPlayerUpdateSwimVerticalVelocityEntry,
      Oot3dNativeGame::kOot3dPlayerHoldsHookshotWithoutHeldActorEntry,
      Oot3dNativeGame::kOot3dPlayerGetIdleAnimEntry,
      Oot3dNativeGame::kOot3dPlayerIsItemInHandEntry,
      Oot3dNativeGame::kOot3dPlayerHoldsHookshotEntry,
      Oot3dNativeGame::kOot3dPlayerSetRuntimeFlag200Entry,
      Oot3dNativeGame::kOot3dPlayerHoldsTwoHandedWeaponEntry,
      Oot3dNativeGame::kOot3dPlayerGetHeightEntry,
      Oot3dNativeGame::kOot3dPlayerInCsModeEntry,
      Oot3dNativeGame::kOot3dPlayerSetCsActionWithHaltedActorsEntry,
      Oot3dNativeGame::kOot3dPlayerDamageFlickerCounterBlock,
  };
  const auto catalog = Oot3dNativeGame::Oot3dTypedGameplayEntryPoints();
  for (const std::uint32_t entry : entries) {
    Expect(std::find(catalog.begin(), catalog.end(), entry) != catalog.end(),
           "typed Player entry missing from dispatcher catalog");
  }
  const auto observableExits =
      Oot3dNativeGame::Oot3dTypedGameplayObservableExitPoints();
  Expect(std::find(
             observableExits.begin(), observableExits.end(),
             Oot3dNativeGame::kOot3dPlayerRespawnDamageAdvanceBlock) !=
             observableExits.end(),
         "typed respawn damage block is not a whole-AOT observable exit");
  for (const std::uint32_t entry :
       {Oot3dNativeGame::kOot3dPlayerRandomTurnTimerDecrementBlock,
        Oot3dNativeGame::kOot3dPlayerRandomTurnTimerRefreshBlock}) {
    Expect(std::find(observableExits.begin(), observableExits.end(), entry) !=
               observableExits.end(),
           "typed random turn timer block is not a whole-AOT observable exit");
  }
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dPlayerAttentionPersistenceAdvanceBlock) !=
          observableExits.end(),
      "typed attention persistence block is not a whole-AOT observable exit");
  Expect(std::find(observableExits.begin(), observableExits.end(),
                   Oot3dNativeGame::kOot3dPlayerCommonCountdownBlock) !=
             observableExits.end(),
         "typed common countdown block is not a whole-AOT observable exit");
  Expect(std::find(observableExits.begin(), observableExits.end(),
                   Oot3dNativeGame::kOot3dPlayerInvincibilityTimerBlock) !=
             observableExits.end(),
         "typed invincibility block is not a whole-AOT observable exit");
  Expect(std::find(observableExits.begin(), observableExits.end(),
                   Oot3dNativeGame::kOot3dPlayerDamageRunTimerBlock) !=
             observableExits.end(),
         "typed damage-run timer block is not a whole-AOT observable exit");
  Expect(std::find(observableExits.begin(), observableExits.end(),
                   Oot3dNativeGame::kOot3dPlayerFishingStateRecoveryBlock) !=
             observableExits.end(),
         "typed Player internal block is not a whole-AOT observable exit");
  Expect(std::find(observableExits.begin(), observableExits.end(),
                   Oot3dNativeGame::kOot3dPlayerMeleeActionTimerBlock) !=
             observableExits.end(),
         "typed melee timer block is not a whole-AOT observable exit");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dPlayerMeleeWeaponTipComboAdvanceBlock) !=
          observableExits.end(),
      "typed melee tip combo block is not a whole-AOT observable exit");
  for (const std::uint32_t entry :
       {Oot3dNativeGame::kOot3dPlayerUnderwaterTimerResetBlock,
        Oot3dNativeGame::kOot3dPlayerUnderwaterTimerIncrementBlock}) {
    Expect(std::find(observableExits.begin(), observableExits.end(), entry) !=
               observableExits.end(),
           "typed underwater block is not a whole-AOT observable exit");
  }
  Expect(std::find(
             observableExits.begin(), observableExits.end(),
             Oot3dNativeGame::kOot3dPlayerDamageFlickerCounterBlock) !=
             observableExits.end(),
         "typed damage flicker block is not a whole-AOT observable exit");
}

} // namespace

int main() {
  try {
    TestEntryCatalog();
    Oot3dNativeGame::ResetOot3dTypedGameplayStats();
    TestEquipmentQueries();
    TestRuntimeFlag();
    TestCutsceneActionSetters();
    TestHostileLockOn();
    TestSwimVerticalVelocity();
    TestIdleSelection();
    TestHeight();
    TestCutsceneMode();
    TestRespawnDamageDomain();
    TestRespawnDamageBlockNativeParity();
    TestRespawnDamageBlockEnhanced60();
    TestRandomTurnTimerDomain();
    TestRandomTurnTimerBlocksNativeParity();
    TestRandomTurnTimerBlocksEnhanced60();
    TestAttentionPersistenceDomain();
    TestAttentionPersistenceBlockNativeParity();
    TestAttentionPersistenceBlockEnhanced60();
    TestCommonCountdownDomain();
    TestCommonCountdownBlockNativeParity();
    TestCommonCountdownBlockEnhanced60();
    TestInvincibilityTimerDomain();
    TestInvincibilityTimerBlockNativeParity();
    TestInvincibilityTimerBlockEnhanced60();
    TestDamageFlickerCounterDomain();
    TestDamageFlickerCounterBlockNativeParity();
    TestDamageFlickerCounterBlockEnhanced60();
    TestDamageRunTimerDomain();
    TestDamageRunTimerBlockNativeParity();
    TestDamageRunTimerBlockEnhanced60();
    TestFishingItemStateDomain();
    TestFishingRecoveryBlockNativeParity();
    TestFishingRecoveryBlockEnhanced60();
    TestMeleeActionTimingDomain();
    TestMeleeActionTimerBlockNativeParity();
    TestMeleeActionTimerBlockEnhanced60();
    TestMeleeWeaponTipComboDomain();
    TestMeleeWeaponTipComboBlockNativeParity();
    TestMeleeWeaponTipComboBlockEnhanced60();
    TestUnderwaterTimerDomain();
    TestUnderwaterTimerResetBlockNativeParity();
    TestUnderwaterTimerIncrementBlockNativeParity();
    TestUnderwaterTimerBlocksEnhanced60();
    const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
    Expect(stats.PlayerCalls > 0U && stats.ActorCalls == 0U &&
               stats.InvincibilityTimerBlockCalls == 12U &&
               stats.InvincibilityTimerLogicalAdvances == 4U &&
               stats.InvincibilityTimerIntermediateHolds == 2U &&
               stats.InvincibilityTimerPositiveActionHolds == 5U &&
               stats.DamageFlickerCounterBlockCalls == 9U &&
               stats.DamageFlickerCounterLogicalAdvances == 7U &&
               stats.DamageFlickerCounterIntermediateHolds == 2U &&
               stats.RespawnDamageBlockCalls == 7U &&
               stats.RespawnDamageLogicalAdvances == 5U &&
               stats.RespawnDamageIntermediateHolds == 2U &&
               stats.RespawnDamageAudioDispatches == 2U &&
               stats.RandomTurnTimerDecrementBlockCalls == 10U &&
               stats.RandomTurnTimerRefreshBlockCalls == 3U &&
               stats.RandomTurnTimerLogicalAdvances == 8U &&
               stats.RandomTurnTimerIntermediateHolds == 2U &&
               stats.RandomTurnTimerRngDispatches == 2U &&
               stats.RandomTurnTimerRngIntermediateSuppressions == 1U &&
               stats.AttentionPersistenceBlockCalls == 8U &&
               stats.AttentionPersistenceLogicalAdvances == 5U &&
               stats.AttentionPersistenceIntermediateHolds == 2U &&
               stats.AttentionPersistenceSaturationHolds == 1U &&
               stats.CommonCountdownBlockCalls == 8U &&
               stats.CommonCountdownLogicalFieldAdvances == 14U &&
               stats.CommonCountdownLogicalFieldIntermediateHolds == 6U &&
               stats.FairyReviveGraceTimerAdvances == 7U &&
               stats.DamageRunTimerBlockCalls == 9U &&
               stats.DamageRunTimerLogicalAdvances == 5U &&
               stats.DamageRunTimerIntermediateHolds == 2U &&
               stats.FishingStateBlockCalls == 5U &&
               stats.FishingStateLogicalAdvances == 2U &&
               stats.FishingStateIntermediateHolds == 1U &&
               stats.UnderwaterTimerResetBlockCalls == 2U &&
               stats.UnderwaterTimerIncrementBlockCalls == 4U &&
               stats.UnderwaterTimerLogicalAdvances == 3U &&
               stats.UnderwaterTimerIntermediateHolds == 1U &&
               stats.MeleeActionTimerBlockCalls == 8U &&
               stats.MeleeActionTimerLogicalAdvances == 4U &&
               stats.MeleeActionTimerIntermediateHolds == 2U &&
               stats.MeleeActionComboClears == 2U &&
               stats.MeleeWeaponTipComboBlockCalls == 5U &&
               stats.MeleeWeaponTipComboLogicalAdvances == 4U &&
               stats.MeleeWeaponTipComboIntermediateHolds == 1U &&
               stats.RetainedAotFallbacks == 0U,
           "typed Player diagnostics mismatch");
    std::cout << "oot3d typed Player gameplay tests passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "oot3d typed Player gameplay tests failed: " << ex.what()
              << '\n';
    return 1;
  }
}
