#include "oot3d_typed_gameplay_bridge.h"

#include "oot3d_a32_generated.h"
#include "oot3d_gameplay_actor.h"
#include "oot3d_gameplay_camera.h"
#include "oot3d_gameplay_camera_animation.h"
#include "oot3d_gameplay_cutscene.h"
#include "oot3d_gameplay_math.h"
#include "oot3d_gameplay_quake.h"
#include "oot3d_gameplay_skel_anime.h"
#include "oot3d_gameplay_time.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d_native_a32_process.h"
#include "oot3d_typed_actor_lifecycle.h"
#include "oot3d_typed_actor_update_records.h"
#include "oot3d_typed_audio_request_callback.h"
#include "oot3d_typed_dyna_interaction_reset.h"
#include "oot3d_typed_game_state.h"
#include "oot3d_typed_pause_ui_alpha.h"
#include "oot3d_typed_player_lock_on.h"
#include "oot3d_typed_record_initializer.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

using oot3d::gameplay::Actor;
using oot3d::gameplay::AngleTableEntry;
using oot3d::gameplay::SkelAnime;
using Oot3dNativeGame::NativeA32Memory;

constexpr std::uint32_t kActorAddress = 0x10000000U;
constexpr std::uint32_t kValueAddress = 0x10001000U;
constexpr std::uint32_t kTimeStateAddress = 0x10002000U;
constexpr std::uint32_t kPlayAddress = 0x10003000U;
constexpr std::uint32_t kStackTop = 0x10006000U;
constexpr std::uint32_t kReturnSentinel = 0x0BADF00CU;
constexpr std::uint32_t kAngleTableAddress = 0x004DF42CU;
constexpr std::uint32_t kTimeStatePointerAddress = 0x0051B2F4U;
constexpr std::uint32_t kFakeAnimationEntry = 0x10004000U;
constexpr std::uint32_t kZarAddress = 0x10001080U;
constexpr std::uint32_t kZarGroupTableAddress = 0x10001100U;
constexpr std::uint32_t kCsabTableAddress = 0x10001200U;
constexpr std::uint32_t kCsabHandleAddress = 0x10001300U;
constexpr std::uint32_t kCsabDataAddress = 0x10001400U;
constexpr std::uint32_t kMeshPacketAddress = 0x10000000U;
constexpr std::uint32_t kMeshCommandSourceAddress = 0x10002000U;
constexpr std::uint32_t kMeshCommandDestinationAddress = 0x10003000U;
constexpr std::uint32_t kPicaCommandListCursorAddress = 0x0054CC4CU;
constexpr std::uint32_t kCutsceneContextAddress = kValueAddress;
constexpr std::uint32_t kCutsceneCommandStreamAddress = 0x10001800U;
constexpr std::uint32_t kCutsceneActorCueAddress = 0x10001900U;
constexpr std::uint32_t kCameraAddress = 0x10000000U;
constexpr std::uint32_t kCameraResourcePairAddress = 0x10006F00U;
constexpr std::uint32_t kCameraDefaultsAddress = 0x10007000U;
constexpr std::uint32_t kCameraCmadContainerAddress = 0x10007100U;
constexpr std::uint32_t kCameraCmadRecordAddress = 0x10007300U;
constexpr std::uint32_t kMainCutsceneCameraLocalFrameAddress = 0x0051B310U;
constexpr std::uint32_t kActorCutsceneCameraOwnerAddress = 0x10008000U;
constexpr std::uint32_t kActorCutsceneCameraPrimingStateAddress = 0x0059B964U;
constexpr std::uint32_t kActorCutsceneCameraStateAddress = 0x0059BB20U;
constexpr std::uint32_t kCameraGlobalStateAddress = 0x00516E9CU;
constexpr std::uint32_t kCameraQuakeRequestArrayAddress = 0x005A543CU;
constexpr std::uint32_t kCameraQuakeRandomStateAddress = 0x0050C0C4U;
constexpr std::uint32_t kCameraQuakeOutputAddress = kValueAddress + 0x800U;

constexpr std::array kSkelAnimeEffectEntries{
    0x002BB1CCU, 0x002BB34CU, 0x002BD9ECU, 0x002C3814U, 0x0030F6B0U,
    0x0030F900U, 0x003204A4U, 0x00324154U, 0x00350820U, 0x00358338U,
};

constexpr std::array kAnimationChangeEffectEntries{0x003204A4U, 0x00358338U};
constexpr std::array kLinkAnimationChangeEffectEntries{0x002BD9ECU,
                                                       0x00324154U};

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void MapRegion(NativeA32Memory &memory, const char *name, std::uint32_t address,
               std::size_t size) {
  std::string error;
  Expect(memory.MapRegion({name, address, size, true, false, {}}, &error),
         std::string("could not map test region: ") + error);
}

void WriteU32(NativeA32Memory &memory, std::uint32_t address,
              std::uint32_t value) {
  Expect(memory.WriteFast(address, value), "could not write test u32");
}

void WriteFloat(NativeA32Memory &memory, std::uint32_t address, float value) {
  WriteU32(memory, address, std::bit_cast<std::uint32_t>(value));
}

template <typename T>
void WriteObject(NativeA32Memory &memory, std::uint32_t address,
                 const T &value) {
  static_assert(std::is_trivially_copyable_v<T>);
  Expect(memory.WriteBytes(
             address,
             std::span<const std::uint8_t>(
                 reinterpret_cast<const std::uint8_t *>(&value), sizeof(T))),
         "could not write test object");
}

template <typename T>
T ReadObject(const NativeA32Memory &memory, std::uint32_t address) {
  static_assert(std::is_trivially_copyable_v<T>);
  T value{};
  Expect(memory.ReadBytes(
             address, std::span<std::uint8_t>(
                          reinterpret_cast<std::uint8_t *>(&value), sizeof(T))),
         "could not read test object");
  return value;
}

NativeA32Memory BuildMemory(std::int16_t updateRate, std::uint16_t angle) {
  NativeA32Memory memory;
  MapRegion(memory, "enkanban_code", 0x0022C000U, 0x1000U);
  MapRegion(memory, "animation_helper_code", 0x002BB000U, 0x1000U);
  MapRegion(memory, "sin_code", 0x002CF000U, 0x1000U);
  MapRegion(memory, "actor_destroy_code", 0x002D6000U, 0x1000U);
  MapRegion(memory, "camera_update_code", 0x002D8000U, 0x2000U);
  MapRegion(memory, "player_lock_on_code", 0x00334000U, 0x1000U);
  MapRegion(memory, "cos_code", 0x00338000U, 0x1000U);
  MapRegion(memory, "actor_position_rotation_code", 0x0033B000U, 0x1000U);
  MapRegion(memory, "link_play_speed_code", 0x00340000U, 0x1000U);
  MapRegion(memory, "angle_step_code", 0x00352000U, 0x1000U);
  MapRegion(memory, "animation_change_code", 0x00353000U, 0x1000U);
  MapRegion(memory, "link_loop_speed_code", 0x00358000U, 0x1000U);
  MapRegion(memory, "link_play_once_code", 0x00359000U, 0x1000U);
  MapRegion(memory, "actor_velocity_code", 0x0035F000U, 0x1000U);
  MapRegion(memory, "link_animation_change_code", 0x00360000U, 0x1000U);
  MapRegion(memory, "actor_velocity_xyz_code", 0x00365000U, 0x1000U);
  MapRegion(memory, "actor_update_code", 0x0036B000U, 0x1000U);
  MapRegion(memory, "smooth_float_code", 0x0036E000U, 0x1000U);
  MapRegion(memory, "approach_zero_code", 0x0036F000U, 0x1000U);
  MapRegion(memory, "smooth_code", 0x00370000U, 0x1000U);
  MapRegion(memory, "signed_step_code", 0x00372000U, 0x1000U);
  MapRegion(memory, "pause_context_code", 0x00369000U, 0x1000U);
  MapRegion(memory, "approach_float_code", 0x00373000U, 0x1000U);
  MapRegion(memory, "smooth_signed_code", 0x00375000U, 0x1000U);
  MapRegion(memory, "actor_move_code", 0x00376000U, 0x1000U);
  MapRegion(memory, "audio_request_callback_code", 0x00465000U, 0x1000U);
  MapRegion(memory, "pause_ui_alpha_code", 0x00479000U, 0x1000U);
  MapRegion(memory, "actor_update_record_code", 0x0047C000U, 0x1000U);
  MapRegion(memory, "tapered_morph_code", 0x00485000U, 0x1000U);
  MapRegion(memory, "animation_length_code", 0x003FE000U, 0x1000U);
  MapRegion(memory, "angle_table", 0x004DF000U, 0x3000U);
  MapRegion(memory, "pause_context_global", 0x00504000U, 0x1000U);
  MapRegion(memory, "quake_random_state", 0x0050C000U, 0x1000U);
  MapRegion(memory, "camera_global_state", 0x00516000U, 0x2000U);
  MapRegion(memory, "time_pointer", 0x0051B000U, 0x1000U);
  MapRegion(memory, "audio_request_state", 0x0054A000U, 0x1000U);
  MapRegion(memory, "actor_cutscene_camera_state", 0x0059B000U, 0x2000U);
  MapRegion(memory, "camera_quake_requests", 0x005A5000U, 0x1000U);
  MapRegion(memory, "actor_allocator_global", 0x0055A000U, 0x2000U);
  MapRegion(memory, "runtime", 0x10000000U, 0xC000U);

  WriteU32(memory, 0x0022CB00U, 0xFFFFEE00U);
  WriteU32(memory, 0x002D64F0U, 0x0055A1A8U);
  WriteFloat(memory, 0x002CFCD8U, 1.0f / 256.0f);
  WriteU32(memory, 0x002CFCDCU, kAngleTableAddress);
  WriteFloat(memory, 0x002D8FA0U, 113.33333587646484375F);
  WriteFloat(memory, 0x002D8FA4U, 0.01111111138015985489F);
  WriteFloat(memory, 0x002D9370U, 165.3333282470703125F);
  WriteFloat(memory, 0x002D9374U, -60.0F);
  WriteFloat(memory, 0x002D9378U, -0.21999999880790710449F);
  WriteFloat(memory, 0x002D937CU, 0.21999999880790710449F);
  WriteFloat(memory, 0x002D9380U, 0.11999999731779098511F);
  WriteFloat(memory, 0x002D9384U, 0.10000000149011611938F);
  WriteFloat(memory, 0x002D9388U, 0.00833333376795053482F);
  WriteFloat(memory, 0x002BB338U, 0.0f);
  WriteU32(memory, 0x002BB33CU, 0x004BCB04U);
  WriteFloat(memory, 0x002BB340U, 1.0f);
  WriteU32(memory, 0x002BB344U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x002BB348U, 1.0f / 3.0f);
  WriteFloat(memory, 0x002BB430U, 0.0f);
  WriteU32(memory, 0x002BB434U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x002BB438U, 0.5f);
  WriteFloat(memory, 0x00338F98U, 1.0f / 256.0f);
  WriteU32(memory, 0x00338F9CU, kAngleTableAddress);
  WriteU32(memory, 0x0033BE58U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x0033BE5CU, 0.5f);
  WriteFloat(memory, 0x00340518U, 0.0f);
  WriteU32(memory, 0x00352A9CU, kTimeStatePointerAddress);
  WriteFloat(memory, 0x00352AA0U, 1.0f / 3.0f);
  WriteFloat(memory, 0x00352AA4U, 0.5f);
  WriteU32(memory, 0x0035FB8CU, kTimeStatePointerAddress);
  WriteFloat(memory, 0x0035FB90U, 1.0f / 3.0f);
  WriteU32(memory, 0x0036B9D8U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x0036B9DCU, 0.5f);
  WriteU32(memory, 0x0036E27CU, kTimeStatePointerAddress);
  WriteFloat(memory, 0x0036E280U, 1.0f / 3.0f);
  WriteU32(memory, 0x0036E284U, 0x3727C5ACU);
  WriteFloat(memory, 0x0036E66CU, 0.0f);
  WriteU32(memory, 0x0036FC9CU, kTimeStatePointerAddress);
  WriteFloat(memory, 0x0036FCA0U, 1.0f / 3.0f);
  WriteU32(memory, 0x0036FCA4U, 0x3727C5ACU);
  WriteU32(memory, 0x00370164U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x00370168U, 1.0f / 3.0f);
  WriteFloat(memory, 0x0037016CU, 0.5f);
  WriteU32(memory, 0x00370408U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x0037040CU, 0.5f);
  WriteFloat(memory, 0x00370628U, 0.0f);
  WriteU32(memory, 0x0037062CU, kTimeStatePointerAddress);
  WriteFloat(memory, 0x00370630U, 1.0f / 3.0f);
  WriteU32(memory, 0x00372B44U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x00372B48U, 1.0f / 3.0f);
  WriteFloat(memory, 0x00372B4CU, 1.0f);
  WriteU32(memory, 0x0037358CU, kTimeStatePointerAddress);
  WriteFloat(memory, 0x00373590U, 1.0f / 3.0f);
  WriteU32(memory, 0x00373594U, 0x3727C5ACU);
  WriteFloat(memory, 0x00373788U, 0.0f);
  WriteU32(memory, 0x00375B64U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x00375B68U, 1.0f / 3.0f);
  WriteFloat(memory, 0x00375B6CU, 0.5f);
  WriteU32(memory, 0x00376930U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x00376934U, 1.0f / 3.0f);
  WriteFloat(memory, 0x00376938U, 0.5f);
  WriteU32(memory, 0x0036B7FCU, kTimeStatePointerAddress);
  WriteU32(memory, 0x00369604U, 0x005043D4U);
  WriteFloat(memory, 0x00353188U, 0.0f);
  WriteFloat(memory, 0x0035318CU, 1.0f);
  WriteFloat(memory, 0x00358E6CU, 0.0f);
  WriteFloat(memory, 0x00359B04U, 0.0f);
  WriteFloat(memory, 0x00359B08U, 1.0f);
  WriteFloat(memory, 0x003603B8U, 0.0f);
  WriteFloat(memory, 0x003603BCU, 1.0f);
  WriteFloat(memory, 0x00360554U, 0.0f);
  WriteFloat(memory, 0x00360558U, 1.0f);
  WriteU32(memory, 0x0036B2C0U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x0036B2C4U, 0.5f);
  WriteFloat(memory, 0x0036B2C8U, 0.0f);
  WriteFloat(memory, 0x0036B2CCU, 100000.0f);
  WriteU32(memory, 0x0036B2D0U, 0x3727C5ACU);
  constexpr std::array skelAnimeJumpTable{
      0x0036B5BCU, 0x0036B564U, 0x0036B5C8U, 0x0036B668U, 0x0036B6E4U,
      0x0036B730U, 0x0036B7A4U, 0x0036B880U, 0x0036B92CU,
  };
  for (std::size_t index = 0; index < skelAnimeJumpTable.size(); ++index) {
    WriteU32(memory, 0x0036B540U + static_cast<std::uint32_t>(index) * 4U,
             skelAnimeJumpTable[index]);
  }
  WriteFloat(memory, 0x0036B800U, 0.5f);
  WriteFloat(memory, 0x0036B804U, 1.0f);
  WriteFloat(memory, 0x0036B808U, 1.0f / 3.0f);
  WriteFloat(memory, 0x0036B80CU, 0.0f);
  WriteU32(memory, 0x00465368U, 0x0054ABD4U);
  WriteU32(memory, Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool,
           kTimeStatePointerAddress);
  WriteU32(memory, Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x04U,
           0x3F000000U);
  WriteU32(memory, Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x08U,
           0x41200000U);
  WriteU32(memory, Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x0CU,
           0x41BAAAABU);
  WriteU32(memory, Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x10U,
           0x40555556U);
  WriteU32(memory, Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x14U,
           0x40D55556U);
  WriteU32(memory,
           Oot3dNativeGame::kOot3dActorUpdateRecordDefaultWordLiteral,
           0U);
  WriteFloat(memory, 0x0048539CU, 16384.0f);
  WriteU32(memory, 0x004853A0U, kTimeStatePointerAddress);
  WriteFloat(memory, 0x004853A4U, 1.0f / 3.0f);
  WriteFloat(memory, 0x004853A8U, 0.0f);
  WriteFloat(memory, 0x004853ACU, 1.0f);
  WriteU32(memory, kTimeStatePointerAddress, kTimeStateAddress);
  Expect(memory.WriteFast(kTimeStateAddress + 0x110U,
                          std::bit_cast<std::uint16_t>(updateRate)),
         "could not write native update rate");

  const AngleTableEntry entry{
      0.381234735f,
      0.924478412f,
      0.022104263f,
      -0.009317219f,
  };
  for (std::uint32_t index = 0; index < 256U; ++index) {
    WriteObject(memory, kAngleTableAddress + index * 0x10U, entry);
  }
  return memory;
}

void ConfigureAnimationResource(NativeA32Memory &memory,
                                std::uint32_t animationIndex,
                                std::int16_t frameCount) {
  WriteU32(memory, kZarAddress + 0x0CU, kZarGroupTableAddress);
  WriteU32(memory, kZarAddress + 0x20U, 0U);
  WriteU32(memory, kZarAddress + 0x50U, kCsabTableAddress);
  WriteU32(memory, kZarGroupTableAddress, animationIndex + 1U);
  WriteU32(memory, kCsabTableAddress + animationIndex * 4U, kCsabHandleAddress);
  WriteU32(memory, kCsabHandleAddress, kCsabDataAddress);
  WriteU32(memory, kCsabDataAddress + 0x14U, 0x20U);
  WriteU32(memory, kCsabDataAddress + 0x30U,
           static_cast<std::uint16_t>(frameCount));
}

Actor BuildActor(std::int16_t yaw) {
  Actor actor;
  actor.WorldPosition = {12.25f, -3.5f, 41.0f};
  actor.WorldRotation.X = -0x2345;
  actor.WorldRotation.Y = yaw;
  actor.Velocity = {-7.0f, 1.75f, 9.0f};
  actor.SpeedXZ = 3.25f;
  actor.Gravity = -2.5f;
  actor.MinimumVelocityY = -20.0f;
  actor.CollisionCheck.Displacement = {0.125f, -0.25f, 0.5f};
  return actor;
}

oot3d::recomp::a32::GuestState BuildState(std::uint32_t entry) {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = kActorAddress;
  state.r[13] = kStackTop;
  state.r[14] = kReturnSentinel;
  state.r[15] = entry;
  return state;
}

void RunReference(std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
                  NativeA32Memory &memory) {
  const auto result = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), entry, state, memory, nullptr,
      nullptr, 20'000U);
  Expect(result.pc == kReturnSentinel &&
             result.kind == oot3d::recomp::a32::ExitKind::MissingBlock,
         "A32 reference did not return through LR: pc=" +
             std::to_string(result.pc) +
             " kind=" +
             std::to_string(static_cast<unsigned>(result.kind)));
}

void TestGameStateUpdateOwnerDifferential() {
  constexpr std::uint32_t kInitialFrameCounter = 0xFFFFFFFFU;
  constexpr std::uint32_t kSavedR4 = 0x44444444U;
  constexpr std::uint32_t kCallbackR0 = 0xA0A0A0A0U;
  constexpr std::uint32_t kCallbackR1 = 0xB1B1B1B1U;
  constexpr std::uint32_t kCallbackFlags = 0xA0000010U;

  auto referenceMemory = BuildMemory(2, 0U);
  WriteU32(referenceMemory,
           kActorAddress + Oot3dNativeGame::kOot3dGameStateMainOffset,
           kFakeAnimationEntry);
  WriteU32(
      referenceMemory,
      kActorAddress + Oot3dNativeGame::kOot3dGameStateFrameCounterOffset,
      kInitialFrameCounter);
  auto typedMemory = referenceMemory;

  const auto buildEntryState = [] {
    auto state = BuildState(
        Oot3dNativeGame::kOot3dGameStateUpdateOwnerEntry);
    state.r[4] = kSavedR4;
    state.r[1] = 0x11111111U;
    state.cpsr = 0x60000010U;
    return state;
  };

  auto referenceState = buildEntryState();
  const auto referenceDispatch = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(),
      Oot3dNativeGame::kOot3dGameStateUpdateOwnerEntry, referenceState,
      referenceMemory, nullptr, nullptr, 20'000U);
  Expect(referenceDispatch.kind ==
             oot3d::recomp::a32::ExitKind::MissingBlock &&
             referenceDispatch.pc == kFakeAnimationEntry,
         "A32 GameState_Update did not dispatch GameState.main");

  auto typedState = buildEntryState();
  oot3d::recomp::a32::ExecutionResult typedResult;
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dGameStateUpdateOwnerEntry, typedState,
             typedMemory, &typedResult, {2.0F}, &blocksConsumed),
         "typed GameState_Update entry retained AOT unexpectedly");
  Expect(typedResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
             typedResult.pc == kFakeAnimationEntry && blocksConsumed == 1U,
         "typed GameState_Update did not dispatch GameState.main");
  Expect(typedState.r == referenceState.r &&
             typedState.cpsr == referenceState.cpsr,
         "typed/A32 GameState_Update entry ABI mismatch");
  Expect(ReadObject<std::array<std::uint32_t, 2>>(
             typedMemory, kStackTop - 8U) ==
             ReadObject<std::array<std::uint32_t, 2>>(
                 referenceMemory, kStackTop - 8U),
         "typed/A32 GameState_Update saved-frame mismatch");

  referenceState.r[0] = kCallbackR0;
  referenceState.r[1] = kCallbackR1;
  referenceState.cpsr = kCallbackFlags;
  referenceState.r[15] =
      Oot3dNativeGame::kOot3dGameStateUpdateMainReturn;
  typedState.r[0] = kCallbackR0;
  typedState.r[1] = kCallbackR1;
  typedState.cpsr = kCallbackFlags;
  typedState.r[15] = Oot3dNativeGame::kOot3dGameStateUpdateMainReturn;

  const auto referenceReturn = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(),
      Oot3dNativeGame::kOot3dGameStateUpdateMainReturn, referenceState,
      referenceMemory, nullptr, nullptr, 20'000U);
  Expect(referenceReturn.kind ==
             oot3d::recomp::a32::ExitKind::MissingBlock &&
             referenceReturn.pc == kReturnSentinel,
         "A32 GameState_Update continuation did not return to its caller");

  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dGameStateUpdateMainReturn, typedState,
             typedMemory, &typedResult, {2.0F}, &blocksConsumed),
         "typed GameState_Update continuation retained AOT unexpectedly");
  Expect(typedResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
             typedResult.pc == kReturnSentinel && blocksConsumed == 1U,
         "typed GameState_Update continuation returned an invalid ABI result");
  Expect(typedState.r == referenceState.r &&
             typedState.cpsr == referenceState.cpsr,
         "typed/A32 GameState_Update return ABI mismatch");
  Expect(ReadObject<std::uint32_t>(
             typedMemory,
             kActorAddress +
                 Oot3dNativeGame::kOot3dGameStateFrameCounterOffset) ==
             ReadObject<std::uint32_t>(
                 referenceMemory,
                 kActorAddress +
                     Oot3dNativeGame::kOot3dGameStateFrameCounterOffset),
         "typed/A32 GameState_Update frame counter mismatch");

  NativeA32Memory invalidMemory;
  auto invalidState = buildEntryState();
  const auto originalState = invalidState;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dGameStateUpdateOwnerEntry, invalidState,
             invalidMemory, &typedResult, {2.0F}, &blocksConsumed),
         "invalid GameState_Update input did not retain AOT");
  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(invalidState.r == originalState.r &&
             invalidState.cpsr == originalState.cpsr &&
             stats.Calls == 0U && stats.RetainedAotFallbacks == 1U &&
             stats.ReadFailures == 1U,
         "GameState_Update fallback was destructive");
}

class GameStateCheckpointHost final
    : public Oot3dNativeGame::NativeA32HostServices {
public:
  Oot3dNativeGame::NativeA32HostResult HandleSvc(
      std::uint32_t, oot3d::recomp::a32::GuestState &,
      Oot3dNativeGame::NativeA32Memory &,
      Oot3dNativeGame::NativeA32HostContext &) override {
    return {Oot3dNativeGame::NativeA32HostAction::Fault, std::nullopt, 0U,
            "unexpected SVC in GameState checkpoint test"};
  }
};

void TestTypedGameStateCheckpointRoundTrip() {
  constexpr std::uint32_t kOwnerTextBase =
      Oot3dNativeGame::kOot3dGameStateUpdateOwnerEntry & ~0xFFU;
  constexpr std::uint32_t kCheckpointStackBase = 0x10001000U;
  constexpr std::uint32_t kCheckpointTlsBase = 0x10003000U;
  constexpr std::uint32_t kMainCallback = 0x00500000U;
  constexpr std::uint32_t kSavedR4 = 0x44556677U;
  constexpr std::uint32_t kInitialFrames = 41U;
  constexpr std::array<std::uint8_t, 0x100> kOwnerCode{};

  const oot3d::recomp::a32::Registry registry{};
  GameStateCheckpointHost host;
  Oot3dNativeGame::NativeA32Process process(registry, host);
  std::string error;
  Expect(process.MapRegion({"game_state_text", kOwnerTextBase,
                            kOwnerCode.size(), false, true, kOwnerCode},
                           &error) &&
             process.MapRegion(
                 {"game_state", kActorAddress, 0x200U, true, false, {}},
                 &error) &&
             process.CreatePrimaryThread(
                 {Oot3dNativeGame::kOot3dGameStateUpdateOwnerEntry,
                  kCheckpointStackBase, 0x1000U, kCheckpointTlsBase, 0x1000U,
                  0U, kActorAddress, 0x10U, 0x03C00010U, 48U},
                 &error),
         "could not create GameState checkpoint process: " + error);

  auto &state = process.PrimaryThreadState();
  state.r[4] = kSavedR4;
  state.r[14] = kReturnSentinel;
  Expect(process.Memory().Write32(
             kActorAddress + Oot3dNativeGame::kOot3dGameStateMainOffset,
             kMainCallback) &&
             process.Memory().Write32(
                 kActorAddress +
                     Oot3dNativeGame::kOot3dGameStateFrameCounterOffset,
                 kInitialFrames),
         "could not initialize GameState checkpoint memory");

  oot3d::recomp::a32::ExecutionResult dispatch{};
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameState(
             Oot3dNativeGame::kOot3dGameStateUpdateOwnerEntry, state,
             process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedGameStateResult::MainDispatched &&
             dispatch.pc == kMainCallback && blocksConsumed == 1U,
         "GameState owner did not reach its checkpoint boundary");

  state.r[15] = Oot3dNativeGame::kOot3dGameStateUpdateMainReturn;
  const auto encoded = process.CaptureState();
  const auto bytes = nlohmann::json::to_msgpack(encoded);
  const auto decoded = nlohmann::json::from_msgpack(bytes);

  state = {};
  Expect(process.Memory().Write32(
             kActorAddress +
                 Oot3dNativeGame::kOot3dGameStateFrameCounterOffset,
             0U) &&
             process.RestoreState(decoded, &error),
         "could not restore GameState checkpoint: " + error);

  auto &restored = process.PrimaryThreadState();
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameState(
             Oot3dNativeGame::kOot3dGameStateUpdateMainReturn, restored,
             process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedGameStateResult::UpdateReturned &&
             dispatch.pc == kReturnSentinel && blocksConsumed == 1U,
         "GameState owner did not resume from its checkpoint");
  const auto frames = ReadObject<std::uint32_t>(
      process.Memory(),
      kActorAddress + Oot3dNativeGame::kOot3dGameStateFrameCounterOffset);
  Expect(frames == kInitialFrames + 1U && restored.r[0] == frames &&
             restored.r[4] == kSavedR4 &&
             restored.r[13] == kCheckpointStackBase + 0x1000U,
         "GameState checkpoint did not preserve guest ABI state");
}

oot3d::recomp::a32::ExecutionResult ExecuteLifecycleReferenceBlock(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory) {
  const auto *block = oot3d::recomp::a32::FindBlock(
      oot3d::recomp::GetA32GeneratedRegistry(), entry);
  Expect(block != nullptr,
         "missing generated lifecycle block at " + std::to_string(entry));
  return oot3d::recomp::a32::ExecuteBlock(*block, state, memory);
}

oot3d::recomp::a32::ExecutionResult ExecuteAudioRequestReferenceBoundary(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory) {
  constexpr std::uint32_t kCallbackStart =
      Oot3dNativeGame::kOot3dAudioRequestFlag100CallbackEntry;
  constexpr std::uint32_t kCallbackEndExclusive = 0x00465368U;
  std::uint32_t pc = entry;
  oot3d::recomp::a32::ExecutionResult result{};
  for (std::uint32_t block = 0U; block < 8U; ++block) {
    result = ExecuteLifecycleReferenceBlock(pc, state, memory);
    if (result.pc < kCallbackStart || result.pc >= kCallbackEndExclusive) {
      return result;
    }
    pc = result.pc;
  }
  Expect(false, "audio callback reference did not reach a service boundary");
  return result;
}

oot3d::recomp::a32::ExecutionResult ExecutePauseUiAlphaReferenceBoundary(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory) {
  constexpr std::uint32_t kOwnerEndExclusive = 0x004796A4U;
  std::uint32_t pc = entry;
  oot3d::recomp::a32::ExecutionResult result{};
  for (std::uint32_t block = 0U; block < 12U; ++block) {
    result = ExecuteLifecycleReferenceBlock(pc, state, memory);
    if (result.pc < Oot3dNativeGame::kOot3dPauseUiUpdateDualAlphaEntry ||
        result.pc >= kOwnerEndExclusive) {
      return result;
    }
    pc = result.pc;
  }
  Expect(false, "pause UI alpha reference did not reach a service boundary");
  return result;
}

oot3d::recomp::a32::ExecutionResult ExecuteMathStepToSReferenceBoundary(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory) {
  constexpr std::uint32_t kMathEndExclusive = 0x00372B44U;
  std::uint32_t pc = Oot3dNativeGame::kOot3dPauseUiAlphaMathStepToSEntry;
  oot3d::recomp::a32::ExecutionResult result{};
  for (std::uint32_t block = 0U; block < 12U; ++block) {
    result = ExecuteLifecycleReferenceBlock(pc, state, memory);
    if (result.pc <
            Oot3dNativeGame::kOot3dPauseUiAlphaMathStepToSEntry ||
        result.pc >= kMathEndExclusive) {
      return result;
    }
    pc = result.pc;
  }
  Expect(false, "Math_StepToS reference did not return to its caller");
  return result;
}

oot3d::recomp::a32::ExecutionResult DispatchLifecycleReference(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory) {
  return oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), entry, state, memory,
      nullptr, nullptr, 20'000U);
}

void ExpectLifecycleStateEqual(
    const oot3d::recomp::a32::GuestState &actual,
    const oot3d::recomp::a32::GuestState &expected,
    const std::string &label) {
  Expect(actual.r == expected.r, label + ": register mismatch");
  Expect(actual.cpsr == expected.cpsr, label + ": CPSR mismatch");
  Expect(actual.fpscr == expected.fpscr, label + ": FPSCR mismatch");
  Expect(actual.thread_pointer == expected.thread_pointer,
         label + ": thread pointer mismatch");
  Expect(actual.vfp == expected.vfp, label + ": VFP mismatch");
  Expect(actual.exclusive_address == expected.exclusive_address &&
             actual.exclusive_token == expected.exclusive_token &&
             actual.exclusive_size == expected.exclusive_size &&
             actual.exclusive_valid == expected.exclusive_valid,
         label + ": exclusive monitor mismatch");
}

void ExpectActorLifecycleMemoryEqual(const NativeA32Memory &actual,
                                     const NativeA32Memory &expected,
                                     const std::string &label) {
  Expect(ReadObject<std::array<std::uint8_t, 0x1A4>>(
             actual, kActorAddress) ==
             ReadObject<std::array<std::uint8_t, 0x1A4>>(
                 expected, kActorAddress),
         label + ": Actor memory mismatch");
  Expect(ReadObject<std::array<std::uint32_t, 6>>(
             actual, kStackTop - 24U) ==
             ReadObject<std::array<std::uint32_t, 6>>(
                 expected, kStackTop - 24U),
         label + ": saved frame mismatch");
}

void TestActorInstanceCallbackContractDifferential() {
  constexpr std::uint32_t kInitCallback = 0x0E100000U;
  constexpr std::uint32_t kUpdateCallback = 0x0E100100U;
  constexpr std::uint32_t kInitClearValue = 0xA5A5A5A5U;

  auto referenceMemory = BuildMemory(2, 0U);
  WriteU32(referenceMemory,
           kActorAddress + Oot3dNativeGame::kOot3dActorInitOffset,
           kInitCallback);
  WriteU32(referenceMemory,
           kActorAddress + Oot3dNativeGame::kOot3dActorUpdateOffset,
           kUpdateCallback);
  WriteU32(referenceMemory, kStackTop + 0x3CU, kPlayAddress);
  auto typedMemory = referenceMemory;

  const auto buildState = [=](std::uint32_t entry) {
    auto state = BuildState(entry);
    state.r[0] = 0x10101010U;
    state.r[1] = 0x11111111U;
    state.r[2] = 0x22222222U;
    state.r[4] = kActorAddress;
    state.r[10] = kInitClearValue;
    state.cpsr = oot3d::recomp::a32::kFlagN |
                 oot3d::recomp::a32::kFlagC | 0x10U;
    return state;
  };

  auto referenceInit = buildState(
      Oot3dNativeGame::kOot3dActorUpdateAllInitCallbackEntry);
  auto typedInit = referenceInit;
  const auto referenceInitResult = ExecuteLifecycleReferenceBlock(
      Oot3dNativeGame::kOot3dActorUpdateAllInitCallbackEntry,
      referenceInit, referenceMemory);
  oot3d::recomp::a32::ExecutionResult typedResult{};
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllInitCallbackEntry,
             typedInit, typedMemory, &typedResult, {2.0F},
             &blocksConsumed),
         "typed Actor init callback boundary retained A32");
  Expect(referenceInitResult.pc == kInitCallback &&
             typedResult.pc == referenceInitResult.pc &&
             blocksConsumed == 1U,
         "Actor init callback target mismatch");
  ExpectLifecycleStateEqual(typedInit, referenceInit,
                            "Actor init callback dispatch");

  for (auto *state : {&referenceInit, &typedInit}) {
    state->r[0] = 0xA0A0A0A0U;
    state->r[1] = 0xB1B1B1B1U;
    state->r[2] = 0xC2C2C2C2U;
    state->r[3] = 0xD3D3D3D3U;
    state->cpsr = oot3d::recomp::a32::kFlagV | 0x10U;
    state->r[15] =
        Oot3dNativeGame::kOot3dActorUpdateAllInitCallbackReturn;
  }
  const auto referenceInitReturn = ExecuteLifecycleReferenceBlock(
      Oot3dNativeGame::kOot3dActorUpdateAllInitCallbackReturn,
      referenceInit, referenceMemory);
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllInitCallbackReturn,
             typedInit, typedMemory, &typedResult, {2.0F},
             &blocksConsumed),
         "typed Actor init callback return retained A32");
  Expect(referenceInitReturn.pc ==
             Oot3dNativeGame::kOot3dActorUpdateAllInitContinue &&
             typedResult.pc == referenceInitReturn.pc &&
             blocksConsumed == 1U,
         "Actor init callback continuation mismatch");
  ExpectLifecycleStateEqual(typedInit, referenceInit,
                            "Actor init callback return");
  ExpectActorLifecycleMemoryEqual(typedMemory, referenceMemory,
                                  "Actor init callback return");

  auto referenceUpdate = buildState(
      Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock);
  referenceUpdate.r[0] = kActorAddress + 0x100U;
  auto typedUpdate = referenceUpdate;
  const auto referenceUpdateResult = DispatchLifecycleReference(
      Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock,
      referenceUpdate, referenceMemory);
  oot3d::gameplay::TimeContext updateTime;
  updateTime.NativeUpdateRate = 2.0F;
  updateTime.CrossedLogicalFrame = true;
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock,
             typedUpdate, typedMemory, &typedResult,
             {2.0F, &updateTime}, &blocksConsumed) &&
             typedResult.pc ==
                 Oot3dNativeGame::kOot3dActorUpdateAllUpdateCallbackEntry,
         "typed Actor effect timers did not reach the update boundary");
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllUpdateCallbackEntry,
             typedUpdate, typedMemory, &typedResult, {2.0F},
             &blocksConsumed),
         "typed Actor update callback boundary retained A32");
  Expect(referenceUpdateResult.pc == kUpdateCallback &&
             typedResult.pc == referenceUpdateResult.pc &&
             blocksConsumed == 1U,
         "Actor update callback target mismatch");
  ExpectLifecycleStateEqual(typedUpdate, referenceUpdate,
                            "Actor update callback dispatch");

  NativeA32Memory invalidMemory;
  auto invalidState = buildState(
      Oot3dNativeGame::kOot3dActorUpdateAllUpdateCallbackEntry);
  const auto originalInvalidState = invalidState;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllUpdateCallbackEntry,
             invalidState, invalidMemory, &typedResult, {2.0F},
             &blocksConsumed),
         "invalid Actor callback boundary did not retain A32");
  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  ExpectLifecycleStateEqual(invalidState, originalInvalidState,
                            "invalid Actor callback boundary");
  Expect(stats.RetainedAotFallbacks == 1U &&
             stats.ReadFailures == 1U,
         "invalid Actor callback telemetry mismatch");
}

void TestActorDestroyOwnerDifferential() {
  constexpr std::uint32_t kDestroyCallback = 0x0E200000U;
  constexpr std::uint32_t kModelDestroyCallback = 0x0E200100U;
  constexpr std::uint32_t kSlotDestroyCallback = 0x0E200200U;
  constexpr std::uint32_t kLastDestroyCallback = 0x0E200300U;
  constexpr std::uint32_t kAllocator = 0x10008000U;
  constexpr std::uint32_t kAllocatorVtable = 0x10008100U;
  constexpr std::uint32_t kModelContext = 0x10008200U;
  constexpr std::uint32_t kSlotResource = 0x10008300U;
  constexpr std::uint32_t kSlotVtable = 0x10008400U;
  constexpr std::uint32_t kLastResource = 0x10008500U;
  constexpr std::uint32_t kLastVtable = 0x10008600U;

  auto referenceMemory = BuildMemory(2, 0U);
  WriteU32(referenceMemory,
           kActorAddress + Oot3dNativeGame::kOot3dActorDestroyOffset,
           kDestroyCallback);
  WriteU32(referenceMemory,
           kActorAddress +
               Oot3dNativeGame::kOot3dActorModelContextOffset,
           kModelContext);
  WriteU32(referenceMemory,
           kActorAddress +
               Oot3dNativeGame::kOot3dActorOwnedModelSlotsOffset,
           kSlotResource);
  WriteU32(referenceMemory,
           kActorAddress +
               Oot3dNativeGame::kOot3dActorLastOwnedModelOffset,
           kLastResource);
  WriteU32(referenceMemory, 0x0055A1A8U, kAllocator);
  WriteU32(referenceMemory, kAllocator, kAllocatorVtable);
  WriteU32(referenceMemory, kAllocatorVtable + 0x10U,
           kModelDestroyCallback);
  WriteU32(referenceMemory, kSlotResource, kSlotVtable);
  WriteU32(referenceMemory, kSlotVtable + 0x04U,
           kSlotDestroyCallback);
  WriteU32(referenceMemory, kLastResource, kLastVtable);
  WriteU32(referenceMemory, kLastVtable + 0x04U,
           kLastDestroyCallback);
  auto typedMemory = referenceMemory;

  const auto buildState = [] {
    auto state = BuildState(Oot3dNativeGame::kOot3dActorDestroyEntry);
    state.r[0] = kActorAddress;
    state.r[1] = 0x11111111U;
    state.r[2] = 0x22222222U;
    state.r[3] = 0x33333333U;
    state.r[4] = 0x44444444U;
    state.r[5] = 0x55555555U;
    state.r[6] = 0x66666666U;
    state.r[7] = 0x77777777U;
    state.r[8] = 0x88888888U;
    state.cpsr = oot3d::recomp::a32::kFlagN |
                 oot3d::recomp::a32::kFlagV | 0x10U;
    return state;
  };

  auto referenceState = buildState();
  auto typedState = referenceState;
  const auto referenceEntry = DispatchLifecycleReference(
      Oot3dNativeGame::kOot3dActorDestroyEntry, referenceState,
      referenceMemory);
  oot3d::recomp::a32::ExecutionResult typedResult{};
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorDestroyEntry, typedState,
             typedMemory, &typedResult, {2.0F}, &blocksConsumed),
         "typed Actor_Destroy entry retained A32");
  Expect(referenceEntry.pc == kDestroyCallback &&
             typedResult.pc == referenceEntry.pc && blocksConsumed == 1U,
         "Actor_Destroy callback target mismatch");
  ExpectLifecycleStateEqual(typedState, referenceState,
                            "Actor_Destroy callback dispatch");
  ExpectActorLifecycleMemoryEqual(typedMemory, referenceMemory,
                                  "Actor_Destroy callback dispatch");

  const auto resumeBoth = [&](std::uint32_t continuation) {
    for (auto *state : {&referenceState, &typedState}) {
      state->r[0] = 0xA0A0A0A0U;
      state->r[1] = 0xB1B1B1B1U;
      state->r[2] = 0xC2C2C2C2U;
      state->r[3] = 0xD3D3D3D3U;
      state->cpsr = oot3d::recomp::a32::kFlagC | 0x10U;
      state->r[15] = continuation;
    }
  };

  resumeBoth(Oot3dNativeGame::kOot3dActorDestroyCallbackReturn);
  const auto referenceModelDispatch = DispatchLifecycleReference(
      Oot3dNativeGame::kOot3dActorDestroyCallbackReturn, referenceState,
      referenceMemory);
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorDestroyCallbackReturn,
             typedState, typedMemory, &typedResult, {2.0F},
             &blocksConsumed),
         "typed Actor destroy continuation retained A32");
  Expect(referenceModelDispatch.pc == kModelDestroyCallback &&
             typedResult.pc == referenceModelDispatch.pc &&
             blocksConsumed == 1U,
         "Actor model-context destroy target mismatch");
  ExpectLifecycleStateEqual(typedState, referenceState,
                            "Actor model-context dispatch");
  ExpectActorLifecycleMemoryEqual(typedMemory, referenceMemory,
                                  "Actor model-context dispatch");

  resumeBoth(Oot3dNativeGame::kOot3dActorDestroyModelContextReturn);
  const auto referenceSlotDispatch = DispatchLifecycleReference(
      Oot3dNativeGame::kOot3dActorDestroyModelContextReturn,
      referenceState, referenceMemory);
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorDestroyModelContextReturn,
             typedState, typedMemory, &typedResult, {2.0F},
             &blocksConsumed),
         "typed model-context continuation retained A32");
  Expect(referenceSlotDispatch.pc == kSlotDestroyCallback &&
             typedResult.pc == referenceSlotDispatch.pc &&
             blocksConsumed == 1U,
         "Actor owned-slot destroy target mismatch");
  ExpectLifecycleStateEqual(typedState, referenceState,
                            "Actor owned-slot dispatch");
  ExpectActorLifecycleMemoryEqual(typedMemory, referenceMemory,
                                  "Actor owned-slot dispatch");

  resumeBoth(Oot3dNativeGame::kOot3dActorDestroyOwnedSlotReturn);
  const auto referenceLastDispatch = DispatchLifecycleReference(
      Oot3dNativeGame::kOot3dActorDestroyOwnedSlotReturn,
      referenceState, referenceMemory);
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorDestroyOwnedSlotReturn,
             typedState, typedMemory, &typedResult, {2.0F},
             &blocksConsumed),
         "typed owned-slot continuation retained A32");
  Expect(referenceLastDispatch.pc == kLastDestroyCallback &&
             typedResult.pc == referenceLastDispatch.pc &&
             blocksConsumed == 1U,
         "Actor last-owned-resource target mismatch");
  ExpectLifecycleStateEqual(typedState, referenceState,
                            "Actor last-owned-resource dispatch");
  ExpectActorLifecycleMemoryEqual(typedMemory, referenceMemory,
                                  "Actor last-owned-resource dispatch");

  resumeBoth(Oot3dNativeGame::kOot3dActorDestroyLastOwnedSlotReturn);
  const auto referenceReturn = DispatchLifecycleReference(
      Oot3dNativeGame::kOot3dActorDestroyLastOwnedSlotReturn,
      referenceState, referenceMemory);
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorDestroyLastOwnedSlotReturn,
             typedState, typedMemory, &typedResult, {2.0F},
             &blocksConsumed),
         "typed Actor_Destroy final continuation retained A32");
  Expect(referenceReturn.pc == kReturnSentinel &&
             typedResult.pc == referenceReturn.pc &&
             blocksConsumed == 1U,
         "Actor_Destroy return target mismatch");
  ExpectLifecycleStateEqual(typedState, referenceState,
                            "Actor_Destroy return");
  ExpectActorLifecycleMemoryEqual(typedMemory, referenceMemory,
                                  "Actor_Destroy return");
  Expect(ReadObject<std::uint8_t>(
             typedMemory,
             kActorAddress +
                 Oot3dNativeGame::kOot3dActorDestroyStateOffset) == 2U,
         "Actor_Destroy did not publish its terminal state");

  auto noCallbackReferenceMemory = BuildMemory(2, 0U);
  auto noCallbackTypedMemory = noCallbackReferenceMemory;
  auto noCallbackReferenceState = buildState();
  auto noCallbackTypedState = noCallbackReferenceState;
  const auto noCallbackReference = DispatchLifecycleReference(
      Oot3dNativeGame::kOot3dActorDestroyEntry,
      noCallbackReferenceState, noCallbackReferenceMemory);
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorDestroyEntry,
             noCallbackTypedState, noCallbackTypedMemory, &typedResult,
             {2.0F}, &blocksConsumed),
         "callback-free Actor_Destroy retained A32");
  Expect(noCallbackReference.pc == kReturnSentinel &&
             typedResult.pc == noCallbackReference.pc &&
             blocksConsumed == 1U,
         "callback-free Actor_Destroy return mismatch");
  ExpectLifecycleStateEqual(noCallbackTypedState,
                            noCallbackReferenceState,
                            "callback-free Actor_Destroy");
  ExpectActorLifecycleMemoryEqual(noCallbackTypedMemory,
                                  noCallbackReferenceMemory,
                                  "callback-free Actor_Destroy");

  NativeA32Memory invalidMemory;
  auto invalidState = buildState();
  const auto incomingInvalidState = invalidState;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorDestroyEntry, invalidState,
             invalidMemory, &typedResult, {2.0F}, &blocksConsumed),
         "invalid Actor_Destroy did not retain A32");
  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  ExpectLifecycleStateEqual(invalidState, incomingInvalidState,
                            "invalid Actor_Destroy");
  Expect(stats.RetainedAotFallbacks == 1U &&
             stats.ReadFailures == 1U,
         "invalid Actor_Destroy telemetry mismatch");
}

void TestTypedActorLifecycleCheckpointRoundTrip() {
  constexpr std::uint32_t kActorDestroyTextBase = 0x002D6400U;
  constexpr std::uint32_t kCheckpointStackBase = 0x10001000U;
  constexpr std::uint32_t kCheckpointTlsBase = 0x10003000U;
  constexpr std::uint32_t kDestroyCallback = 0x0E300000U;
  constexpr std::uint32_t kSavedR4 = 0x44556677U;
  constexpr std::uint32_t kSavedR8 = 0x8899AABBU;

  std::array<std::uint8_t, 0x100> actorDestroyCode{};
  const std::uint32_t allocatorGlobalAddress = 0x0055A1A8U;
  std::memcpy(actorDestroyCode.data() + 0xF0U,
              &allocatorGlobalAddress, sizeof(allocatorGlobalAddress));

  const oot3d::recomp::a32::Registry registry{};
  GameStateCheckpointHost host;
  Oot3dNativeGame::NativeA32Process process(registry, host);
  std::string error;
  Expect(process.MapRegion(
             {"actor_destroy_text", kActorDestroyTextBase,
              actorDestroyCode.size(), false, true, actorDestroyCode},
             &error) &&
             process.MapRegion(
                 {"actor", kActorAddress, 0x1000U, true, false, {}},
                 &error) &&
             process.MapRegion(
                 {"actor_allocator_global", 0x0055A000U, 0x1000U,
                  true, false, {}},
                 &error) &&
             process.CreatePrimaryThread(
                 {Oot3dNativeGame::kOot3dActorDestroyEntry,
                  kCheckpointStackBase, 0x1000U, kCheckpointTlsBase,
                  0x1000U, 0U, kActorAddress, 0x10U, 0x03C00010U,
                  48U},
                 &error),
         "could not create Actor lifecycle checkpoint process: " + error);

  auto &state = process.PrimaryThreadState();
  state.r[4] = kSavedR4;
  state.r[8] = kSavedR8;
  state.r[14] = kReturnSentinel;
  Expect(process.Memory().Write32(
             kActorAddress + Oot3dNativeGame::kOot3dActorDestroyOffset,
             kDestroyCallback),
         "could not initialize Actor lifecycle checkpoint memory");

  oot3d::recomp::a32::ExecutionResult dispatch{};
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedActorLifecycle(
             Oot3dNativeGame::kOot3dActorDestroyEntry, state,
             process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedActorLifecycleResult::
                     DestroyCallbackDispatched &&
             dispatch.pc == kDestroyCallback && blocksConsumed == 1U,
         "Actor lifecycle did not reach its checkpoint boundary");

  state.r[0] = 0xAAAAAAAAU;
  state.r[1] = 0xBBBBBBBBU;
  state.cpsr = oot3d::recomp::a32::kFlagV | 0x10U;
  state.r[15] = Oot3dNativeGame::kOot3dActorDestroyCallbackReturn;
  const auto encoded = process.CaptureState();
  const auto bytes = nlohmann::json::to_msgpack(encoded);
  const auto decoded = nlohmann::json::from_msgpack(bytes);

  state = {};
  Expect(process.Memory().Write32(
             kActorAddress + Oot3dNativeGame::kOot3dActorDestroyOffset,
             0xFFFFFFFFU) &&
             process.RestoreState(decoded, &error),
         "could not restore Actor lifecycle checkpoint: " + error);

  auto &restored = process.PrimaryThreadState();
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedActorLifecycle(
             Oot3dNativeGame::kOot3dActorDestroyCallbackReturn,
             restored, process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedActorLifecycleResult::
                     DestroyReturned &&
             dispatch.pc == kReturnSentinel && blocksConsumed == 1U,
         "Actor lifecycle did not resume from its checkpoint");
  Expect(restored.r[4] == kSavedR4 && restored.r[8] == kSavedR8 &&
             restored.r[13] == kCheckpointStackBase + 0x1000U &&
             ReadObject<std::uint32_t>(
                 process.Memory(),
                 kActorAddress +
                     Oot3dNativeGame::kOot3dActorDestroyOffset) == 0U &&
             ReadObject<std::uint8_t>(
                 process.Memory(),
                 kActorAddress +
                     Oot3dNativeGame::kOot3dActorDestroyStateOffset) == 2U,
         "Actor lifecycle checkpoint did not preserve guest-owned state");
}

void ExpectAudioRequestCallbackMemoryEqual(
    const NativeA32Memory &actual, const NativeA32Memory &expected,
    const std::string &label) {
  constexpr std::uint32_t kRequestStateAddress = 0x0054ABD4U;
  Expect(ReadObject<std::uint8_t>(actual, kRequestStateAddress + 4U) ==
             ReadObject<std::uint8_t>(expected,
                                      kRequestStateAddress + 4U),
         label + ": request flag mismatch");
  Expect(ReadObject<std::array<std::uint32_t, 4>>(
             actual, kStackTop - 16U) ==
             ReadObject<std::array<std::uint32_t, 4>>(
                 expected, kStackTop - 16U),
         label + ": saved frame mismatch");
}

void TestAudioRequestFlag100CallbackDifferential() {
  constexpr std::uint32_t kDescriptorAddress = 0x10009000U;
  constexpr std::uint32_t kObjectAddress = 0x10009100U;
  constexpr std::uint32_t kRequestStateAddress = 0x0054ABD4U;

  struct Scenario {
    const char *Label;
    bool HasObject;
    std::uint32_t ObjectSfxId;
    std::uint32_t RequestedSfxId;
    std::uint32_t AcquiredHandle;
    bool QueriesStatus;
    std::uint32_t Status;
    bool ClearsPending;
  };
  constexpr std::array scenarios{
      Scenario{"different request id", true, 0x18001234U, 0x18005678U,
               0x10009200U, false, 0U, false},
      Scenario{"matching id without handle", true, 0x18001234U,
               0x18001234U, 0U, false, 0U, true},
      Scenario{"accepted handle status", true, 0x18001234U, 0x18001234U,
               0x10009200U, true, 12U, true},
      Scenario{"rejected handle status", true, 0x18001234U, 0x18001234U,
               0x10009200U, true, 13U, false},
      Scenario{"null object matching sentinel", false, 0U, 0xFFFFFFFFU, 0U,
               false, 0U, true},
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  for (const auto &scenario : scenarios) {
    auto referenceMemory = BuildMemory(2, 0U);
    WriteU32(referenceMemory, kDescriptorAddress,
             scenario.HasObject ? kObjectAddress : 0U);
    if (scenario.HasObject) {
      WriteU32(referenceMemory, kObjectAddress + 0x9CU,
               scenario.ObjectSfxId);
    }
    WriteU32(referenceMemory, kRequestStateAddress + 0x28U,
             scenario.RequestedSfxId);
    WriteObject(referenceMemory, kRequestStateAddress + 4U,
                std::uint8_t{1U});
    auto typedMemory = referenceMemory;

    auto referenceState = BuildState(
        Oot3dNativeGame::kOot3dAudioRequestFlag100CallbackEntry);
    referenceState.r[0] = kDescriptorAddress;
    referenceState.r[1] = 0x11111111U;
    referenceState.r[2] = 0x22222222U;
    referenceState.r[3] = 0x33333333U;
    referenceState.r[4] = 0x44444444U;
    referenceState.r[5] = 0x55555555U;
    referenceState.cpsr = oot3d::recomp::a32::kFlagN |
                          oot3d::recomp::a32::kFlagV | 0x10U;
    auto typedState = referenceState;

    const auto referenceEntry = ExecuteAudioRequestReferenceBoundary(
        Oot3dNativeGame::kOot3dAudioRequestFlag100CallbackEntry,
        referenceState, referenceMemory);
    oot3d::recomp::a32::ExecutionResult typedResult{};
    std::uint32_t blocksConsumed = 0U;
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dAudioRequestFlag100CallbackEntry,
               typedState, typedMemory, &typedResult, {2.0F},
               &blocksConsumed),
           std::string(scenario.Label) +
               ": typed callback entry retained A32");
    Expect(referenceEntry.pc ==
               Oot3dNativeGame::kOot3dRendererObjectRefAssignEntry &&
               typedResult.pc == referenceEntry.pc &&
               blocksConsumed == 1U,
           std::string(scenario.Label) +
               ": reference-acquire dispatch mismatch");
    ExpectLifecycleStateEqual(typedState, referenceState,
                              std::string(scenario.Label) +
                                  ": callback entry");
    ExpectAudioRequestCallbackMemoryEqual(
        typedMemory, referenceMemory,
        std::string(scenario.Label) + ": callback entry");

    WriteU32(referenceMemory, referenceState.r[13],
             scenario.AcquiredHandle);
    WriteU32(typedMemory, typedState.r[13], scenario.AcquiredHandle);
    for (auto *state : {&referenceState, &typedState}) {
      state->r[0] = 0xA0A0A0A0U;
      state->r[1] = 0xB1B1B1B1U;
      state->r[2] = 0xC2C2C2C2U;
      state->r[3] = 0xD3D3D3D3U;
      state->cpsr = oot3d::recomp::a32::kFlagC | 0x10U;
      state->r[15] =
          Oot3dNativeGame::
              kOot3dAudioRequestFlag100ReferenceAcquireReturn;
    }

    const auto referenceAcquireReturn =
        ExecuteAudioRequestReferenceBoundary(
        Oot3dNativeGame::kOot3dAudioRequestFlag100ReferenceAcquireReturn,
        referenceState, referenceMemory);
    blocksConsumed = 0U;
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::
                   kOot3dAudioRequestFlag100ReferenceAcquireReturn,
               typedState, typedMemory, &typedResult, {2.0F},
               &blocksConsumed),
           std::string(scenario.Label) +
               ": typed acquire continuation retained A32");
    const std::uint32_t expectedAcquireTarget =
        scenario.QueriesStatus
            ? Oot3dNativeGame::kOot3dAudioRequestStatusQueryEntry
            : Oot3dNativeGame::kOot3dRendererObjectRefClearEntry;
    Expect(referenceAcquireReturn.pc == expectedAcquireTarget &&
               typedResult.pc == referenceAcquireReturn.pc &&
               blocksConsumed == 1U,
           std::string(scenario.Label) +
               ": acquire continuation target mismatch");
    ExpectLifecycleStateEqual(typedState, referenceState,
                              std::string(scenario.Label) +
                                  ": acquire continuation");
    ExpectAudioRequestCallbackMemoryEqual(
        typedMemory, referenceMemory,
        std::string(scenario.Label) + ": acquire continuation");

    if (scenario.QueriesStatus) {
      for (auto *state : {&referenceState, &typedState}) {
        state->r[0] = scenario.Status;
        state->r[1] = 0xE1E1E1E1U;
        state->r[2] = 0xE2E2E2E2U;
        state->r[3] = 0xE3E3E3E3U;
        state->cpsr = oot3d::recomp::a32::kFlagV | 0x10U;
        state->r[15] =
            Oot3dNativeGame::kOot3dAudioRequestFlag100StatusQueryReturn;
      }
      const auto referenceStatusReturn =
          ExecuteAudioRequestReferenceBoundary(
          Oot3dNativeGame::kOot3dAudioRequestFlag100StatusQueryReturn,
          referenceState, referenceMemory);
      blocksConsumed = 0U;
      Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
                 Oot3dNativeGame::
                     kOot3dAudioRequestFlag100StatusQueryReturn,
                 typedState, typedMemory, &typedResult, {2.0F},
                 &blocksConsumed),
             std::string(scenario.Label) +
                 ": typed status continuation retained A32");
      Expect(referenceStatusReturn.pc ==
                 Oot3dNativeGame::kOot3dRendererObjectRefClearEntry &&
                 typedResult.pc == referenceStatusReturn.pc &&
                 blocksConsumed == 1U,
             std::string(scenario.Label) +
                 ": status continuation target mismatch");
      ExpectLifecycleStateEqual(typedState, referenceState,
                                std::string(scenario.Label) +
                                    ": status continuation");
      ExpectAudioRequestCallbackMemoryEqual(
          typedMemory, referenceMemory,
          std::string(scenario.Label) + ": status continuation");
    }

    for (auto *state : {&referenceState, &typedState}) {
      state->r[0] = 0xF0F0F0F0U;
      state->r[1] = 0xF1F1F1F1U;
      state->r[2] = 0xF2F2F2F2U;
      state->r[3] = 0xF3F3F3F3U;
      state->cpsr = oot3d::recomp::a32::kFlagZ | 0x10U;
      state->r[15] =
          Oot3dNativeGame::
              kOot3dAudioRequestFlag100ReferenceCleanupReturn;
    }
    const auto referenceCleanupReturn =
        ExecuteAudioRequestReferenceBoundary(
        Oot3dNativeGame::kOot3dAudioRequestFlag100ReferenceCleanupReturn,
        referenceState, referenceMemory);
    blocksConsumed = 0U;
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::
                   kOot3dAudioRequestFlag100ReferenceCleanupReturn,
               typedState, typedMemory, &typedResult, {2.0F},
               &blocksConsumed),
           std::string(scenario.Label) +
               ": typed cleanup continuation retained A32");
    Expect(referenceCleanupReturn.pc == kReturnSentinel &&
               typedResult.pc == referenceCleanupReturn.pc &&
               blocksConsumed == 1U,
           std::string(scenario.Label) +
               ": callback return target mismatch");
    ExpectLifecycleStateEqual(typedState, referenceState,
                              std::string(scenario.Label) +
                                  ": callback return");
    ExpectAudioRequestCallbackMemoryEqual(
        typedMemory, referenceMemory,
        std::string(scenario.Label) + ": callback return");
    Expect(ReadObject<std::uint8_t>(
               typedMemory, kRequestStateAddress + 4U) ==
               (scenario.ClearsPending ? 0U : 1U),
           std::string(scenario.Label) +
               ": terminal request flag mismatch");
  }

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.AudioRequestReferenceAcquireDispatches == scenarios.size() &&
             stats.AudioRequestStatusQueryDispatches == 2U &&
             stats.AudioRequestReferenceCleanupDispatches ==
                 scenarios.size() &&
             stats.AudioRequestCallbackReturns == scenarios.size(),
         "audio request callback telemetry mismatch");

  auto invalidState = BuildState(
      Oot3dNativeGame::kOot3dAudioRequestFlag100CallbackEntry);
  invalidState.r[0] = kDescriptorAddress;
  const auto incomingInvalidState = invalidState;
  NativeA32Memory invalidMemory;
  oot3d::recomp::a32::ExecutionResult invalidResult{};
  std::uint32_t blocksConsumed = 0U;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dAudioRequestFlag100CallbackEntry,
             invalidState, invalidMemory, &invalidResult, {2.0F},
             &blocksConsumed),
         "unmapped audio callback stack did not retain A32");
  ExpectLifecycleStateEqual(invalidState, incomingInvalidState,
                            "invalid audio callback entry");
  auto invalidStats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(invalidStats.RetainedAotFallbacks == 1U &&
             invalidStats.WriteFailures == 1U,
         "invalid audio callback entry telemetry mismatch");

  auto readFailureMemory = BuildMemory(2, 0U);
  auto readFailureState = BuildState(
      Oot3dNativeGame::kOot3dAudioRequestFlag100ReferenceAcquireReturn);
  readFailureState.r[4] = 0xDEAD0000U;
  const auto incomingReadFailureState = readFailureState;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::
                 kOot3dAudioRequestFlag100ReferenceAcquireReturn,
             readFailureState, readFailureMemory, &invalidResult, {2.0F},
             &blocksConsumed),
         "unmapped audio descriptor did not retain A32");
  ExpectLifecycleStateEqual(readFailureState, incomingReadFailureState,
                            "invalid audio acquire continuation");
  invalidStats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(invalidStats.RetainedAotFallbacks == 1U &&
             invalidStats.ReadFailures == 1U,
         "invalid audio acquire telemetry mismatch");
}

void TestTypedAudioRequestCallbackCheckpointRoundTrip() {
  constexpr std::uint32_t kCallbackTextBase = 0x00465300U;
  constexpr std::uint32_t kRequestStateAddress = 0x0054ABD4U;
  constexpr std::uint32_t kDescriptorAddress = 0x10004000U;
  constexpr std::uint32_t kObjectAddress = 0x10004100U;
  constexpr std::uint32_t kCheckpointStackBase = 0x10001000U;
  constexpr std::uint32_t kCheckpointTlsBase = 0x10003000U;
  constexpr std::uint32_t kSfxId = 0x18001234U;
  constexpr std::uint32_t kInitialR3 = 0x33445566U;
  constexpr std::uint32_t kAcquiredHandle = 0x10004200U;
  constexpr std::uint32_t kSavedR4 = 0x44556677U;
  constexpr std::uint32_t kSavedR5 = 0x55667788U;

  std::array<std::uint8_t, 0x100> callbackCode{};
  std::memcpy(callbackCode.data() + 0x68U, &kRequestStateAddress,
              sizeof(kRequestStateAddress));

  const oot3d::recomp::a32::Registry registry{};
  GameStateCheckpointHost host;
  Oot3dNativeGame::NativeA32Process process(registry, host);
  std::string error;
  Expect(process.MapRegion(
             {"audio_request_callback_text", kCallbackTextBase,
              callbackCode.size(), false, true, callbackCode},
             &error) &&
             process.MapRegion(
                 {"audio_request_state", 0x0054A000U, 0x1000U, true,
                  false, {}},
                 &error) &&
             process.MapRegion(
                 {"audio_request_objects", kDescriptorAddress, 0x1000U,
                  true, false, {}},
                 &error) &&
             process.CreatePrimaryThread(
                 {Oot3dNativeGame::kOot3dAudioRequestFlag100CallbackEntry,
                  kCheckpointStackBase, 0x1000U, kCheckpointTlsBase,
                  0x1000U, 0U, kDescriptorAddress, 0x10U, 0x03C00010U,
                  48U},
                 &error),
         "could not create audio callback checkpoint process: " + error);

  auto &state = process.PrimaryThreadState();
  state.r[3] = kInitialR3;
  state.r[4] = kSavedR4;
  state.r[5] = kSavedR5;
  state.r[14] = kReturnSentinel;
  Expect(process.Memory().Write32(kDescriptorAddress, kObjectAddress) &&
             process.Memory().Write32(kObjectAddress + 0x9CU, kSfxId) &&
             process.Memory().Write32(kRequestStateAddress + 0x28U,
                                      kSfxId) &&
             process.Memory().WriteFast(kRequestStateAddress + 4U,
                                        std::uint8_t{1U}),
         "could not initialize audio callback checkpoint memory");

  oot3d::recomp::a32::ExecutionResult dispatch{};
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedAudioRequestCallback(
             Oot3dNativeGame::kOot3dAudioRequestFlag100CallbackEntry,
             state, process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedAudioRequestCallbackResult::
                     ReferenceAcquireDispatched &&
             dispatch.pc ==
                 Oot3dNativeGame::kOot3dRendererObjectRefAssignEntry &&
             blocksConsumed == 1U,
         "audio callback did not dispatch reference acquisition");

  Expect(process.Memory().Write32(state.r[13], kAcquiredHandle),
         "could not emulate acquired audio request handle");
  state.r[15] =
      Oot3dNativeGame::kOot3dAudioRequestFlag100ReferenceAcquireReturn;
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedAudioRequestCallback(
             Oot3dNativeGame::
                 kOot3dAudioRequestFlag100ReferenceAcquireReturn,
             state, process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedAudioRequestCallbackResult::
                     StatusQueryDispatched &&
             dispatch.pc ==
                 Oot3dNativeGame::kOot3dAudioRequestStatusQueryEntry &&
             blocksConsumed == 1U,
         "audio callback did not reach its checkpoint boundary");

  state.r[0] = 12U;
  state.r[15] =
      Oot3dNativeGame::kOot3dAudioRequestFlag100StatusQueryReturn;
  const auto encoded = process.CaptureState();
  const auto bytes = nlohmann::json::to_msgpack(encoded);
  const auto decoded = nlohmann::json::from_msgpack(bytes);

  state = {};
  Expect(process.Memory().WriteFast(kRequestStateAddress + 4U,
                                    std::uint8_t{0x7FU}) &&
             process.RestoreState(decoded, &error),
         "could not restore audio callback checkpoint: " + error);

  auto &restored = process.PrimaryThreadState();
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedAudioRequestCallback(
             Oot3dNativeGame::kOot3dAudioRequestFlag100StatusQueryReturn,
             restored, process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedAudioRequestCallbackResult::
                     ReferenceCleanupDispatched &&
             dispatch.pc ==
                 Oot3dNativeGame::kOot3dRendererObjectRefClearEntry &&
             ReadObject<std::uint8_t>(
                 process.Memory(), kRequestStateAddress + 4U) == 0U &&
             blocksConsumed == 1U,
         "audio callback did not resume from its checkpoint");

  restored.r[0] = 0xAAAAAAAAU;
  restored.r[15] =
      Oot3dNativeGame::kOot3dAudioRequestFlag100ReferenceCleanupReturn;
  blocksConsumed = 0U;
  const auto returnResult =
      Oot3dNativeGame::ExecuteOot3dTypedAudioRequestCallback(
          Oot3dNativeGame::
              kOot3dAudioRequestFlag100ReferenceCleanupReturn,
          restored, process.Memory(), &dispatch, &blocksConsumed);
  Expect(returnResult ==
                 Oot3dNativeGame::Oot3dTypedAudioRequestCallbackResult::
                     CallbackReturned &&
             dispatch.pc == kReturnSentinel && blocksConsumed == 1U,
         "audio callback checkpoint did not return through the guest LR");
  Expect(restored.r[3] == kAcquiredHandle && restored.r[4] == kSavedR4 &&
             restored.r[5] == kSavedR5,
         "audio callback checkpoint did not restore its native frame");
  Expect(restored.r[13] == kCheckpointStackBase + 0x1000U,
         "audio callback checkpoint did not restore the guest stack");
}

void ExpectPauseUiAlphaMemoryEqual(const NativeA32Memory &actual,
                                   const NativeA32Memory &expected,
                                   const std::string &label) {
  Expect(
      ReadObject<std::array<std::uint8_t, 0x20>>(actual, kValueAddress) ==
          ReadObject<std::array<std::uint8_t, 0x20>>(expected, kValueAddress),
      label + ": pause UI state mismatch");
  Expect(
      ReadObject<std::array<std::uint32_t, 6>>(actual, kStackTop - 24U) ==
          ReadObject<std::array<std::uint32_t, 6>>(expected, kStackTop - 24U),
      label + ": saved frame mismatch");
}

void TestPauseUiUpdateDualAlphaDifferential() {
  constexpr std::uint32_t kPauseStateAddress = 0x005043E8U;
  struct Scenario {
    const char *Label;
    std::uint32_t PauseState;
    std::uint8_t GateTimer;
    std::uint8_t FadeTimer;
    std::int16_t PrimaryAlpha;
    std::int16_t SecondaryAlpha;
  };
  constexpr std::array scenarios{
      Scenario{"pause state blocks update", 2U, 3U, 4U, 91, 123},
      Scenario{"gate timer remains active", 0U, 2U, 4U, 91, 123},
      Scenario{"gate expiry enters fade in", 0U, 1U, 3U, 19, 31},
      Scenario{"active fade in", 0U, 0U, 2U, 41, 53},
      Scenario{"fade expiry enters fade out", 0U, 0U, 1U, 181, 193},
      Scenario{"idle fade out", 0U, 0U, 0U, 211, 223},
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    for (const auto &scenario : scenarios) {
      auto referenceMemory = BuildMemory(updateRate, 0U);
      WriteU32(referenceMemory, kPauseStateAddress, scenario.PauseState);
      WriteObject(referenceMemory,
                  kValueAddress + Oot3dNativeGame::kOot3dPauseUiGateTimerOffset,
                  scenario.GateTimer);
      WriteObject(referenceMemory,
                  kValueAddress + Oot3dNativeGame::kOot3dPauseUiFadeTimerOffset,
                  scenario.FadeTimer);
      WriteObject(referenceMemory,
                  kValueAddress +
                      Oot3dNativeGame::kOot3dPauseUiPrimaryAlphaOffset,
                  scenario.PrimaryAlpha);
      WriteObject(referenceMemory,
                  kValueAddress +
                      Oot3dNativeGame::kOot3dPauseUiSecondaryAlphaOffset,
                  scenario.SecondaryAlpha);
      auto typedMemory = referenceMemory;

      auto referenceState =
          BuildState(Oot3dNativeGame::kOot3dPauseUiUpdateDualAlphaEntry);
      referenceState.r[0] = 0x10101010U;
      referenceState.r[1] = kValueAddress;
      referenceState.r[4] = 0x44444444U;
      referenceState.r[5] = 0x55555555U;
      referenceState.r[6] = 0x66666666U;
      referenceState.cpsr =
          oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV | 0x10U;
      referenceState.fpscr = 0x01000000U;
      referenceState.vfp[0] = 0x01020304U;
      referenceState.vfp[1] = 0x11121314U;
      referenceState.vfp[2] = 0x21222324U;
      referenceState.vfp[16] = 0x41424344U;
      referenceState.vfp[17] = 0x51525354U;
      auto typedState = referenceState;

      const auto compareBoundary =
          [&](const oot3d::recomp::a32::ExecutionResult &referenceResult,
              const oot3d::recomp::a32::ExecutionResult &typedResult,
              const std::string &stage) {
            Expect(referenceResult.kind == typedResult.kind &&
                       referenceResult.pc == typedResult.pc,
                   std::string(scenario.Label) + ": " + stage +
                       " dispatch mismatch");
            ExpectLifecycleStateEqual(typedState, referenceState,
                                      std::string(scenario.Label) + ": " +
                                          stage);
            ExpectPauseUiAlphaMemoryEqual(typedMemory, referenceMemory,
                                          std::string(scenario.Label) + ": " +
                                              stage);
          };

      const auto referenceEntry = ExecutePauseUiAlphaReferenceBoundary(
          Oot3dNativeGame::kOot3dPauseUiUpdateDualAlphaEntry, referenceState,
          referenceMemory);
      oot3d::recomp::a32::ExecutionResult typedResult{};
      std::uint32_t blocksConsumed = 0U;
      Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
                 Oot3dNativeGame::kOot3dPauseUiUpdateDualAlphaEntry, typedState,
                 typedMemory, &typedResult, {static_cast<float>(updateRate)},
                 &blocksConsumed),
             std::string(scenario.Label) + ": typed owner entry retained A32");
      Expect(referenceEntry.pc ==
                     Oot3dNativeGame::kOot3dPauseContextGetStateEntry &&
                 blocksConsumed == 1U,
             std::string(scenario.Label) +
                 ": pause-state query dispatch mismatch");
      compareBoundary(referenceEntry, typedResult, "owner entry");

      const auto referencePauseQuery = ExecuteLifecycleReferenceBlock(
          Oot3dNativeGame::kOot3dPauseContextGetStateEntry, referenceState,
          referenceMemory);
      const auto typedPauseQuery = ExecuteLifecycleReferenceBlock(
          Oot3dNativeGame::kOot3dPauseContextGetStateEntry, typedState,
          typedMemory);
      Expect(referencePauseQuery.pc ==
                 Oot3dNativeGame::kOot3dPauseUiAlphaPauseStateReturn,
             std::string(scenario.Label) +
                 ": pause-state query returned to the wrong boundary");
      compareBoundary(referencePauseQuery, typedPauseQuery,
                      "pause-state service");

      std::uint32_t ownerBoundary = referencePauseQuery.pc;
      bool returned = false;
      for (std::uint32_t call = 0U; call < 3U && !returned; ++call) {
        const auto referenceOwner = ExecutePauseUiAlphaReferenceBoundary(
            ownerBoundary, referenceState, referenceMemory);
        blocksConsumed = 0U;
        Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
                   ownerBoundary, typedState, typedMemory, &typedResult,
                   {static_cast<float>(updateRate)}, &blocksConsumed),
               std::string(scenario.Label) +
                   ": typed owner continuation retained A32");
        Expect(blocksConsumed == 1U,
               std::string(scenario.Label) +
                   ": owner continuation consumed an invalid block count");
        compareBoundary(referenceOwner, typedResult, "owner continuation");

        if (referenceOwner.pc == kReturnSentinel) {
          returned = true;
          break;
        }
        Expect(referenceOwner.pc ==
                   Oot3dNativeGame::kOot3dPauseUiAlphaMathStepToSEntry,
               std::string(scenario.Label) +
                   ": owner reached an unknown guest service");

        const auto referenceMath = ExecuteMathStepToSReferenceBoundary(
            referenceState, referenceMemory);
        const auto typedMath =
            ExecuteMathStepToSReferenceBoundary(typedState, typedMemory);
        compareBoundary(referenceMath, typedMath, "Math_StepToS service");
        if (referenceMath.pc == kReturnSentinel) {
          returned = true;
        } else {
          Expect(referenceMath.pc ==
                         Oot3dNativeGame::kOot3dPauseUiAlphaFadeOutStepReturn ||
                     referenceMath.pc ==
                         Oot3dNativeGame::kOot3dPauseUiAlphaFadeInStepReturn,
                 std::string(scenario.Label) +
                     ": Math_StepToS returned to an unknown continuation");
          ownerBoundary = referenceMath.pc;
        }
      }
      Expect(returned, std::string(scenario.Label) +
                           ": owner did not return through its original LR");

      const std::uint8_t expectedGate =
          scenario.PauseState != 0U || scenario.GateTimer == 0U
              ? scenario.GateTimer
              : static_cast<std::uint8_t>(scenario.GateTimer - 1U);
      const bool reachesFade =
          scenario.PauseState == 0U && scenario.GateTimer <= 1U;
      const std::uint8_t expectedFade =
          reachesFade && scenario.FadeTimer != 0U
              ? static_cast<std::uint8_t>(scenario.FadeTimer - 1U)
              : scenario.FadeTimer;
      Expect(
          ReadObject<std::uint8_t>(
              typedMemory,
              kValueAddress + Oot3dNativeGame::kOot3dPauseUiGateTimerOffset) ==
                  expectedGate &&
              ReadObject<std::uint8_t>(
                  typedMemory,
                  kValueAddress +
                      Oot3dNativeGame::kOot3dPauseUiFadeTimerOffset) ==
                  expectedFade,
          std::string(scenario.Label) + ": timer result mismatch");
    }
  }

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 32U && stats.ActorCalls == 32U &&
             stats.PauseUiAlphaPauseStateDispatches == scenarios.size() * 2U &&
             stats.PauseUiAlphaFirstStepDispatches == 8U &&
             stats.PauseUiAlphaTailStepDispatches == 8U &&
             stats.PauseUiAlphaReturns == 4U,
         "pause UI alpha telemetry mismatch");

  oot3d::recomp::a32::ExecutionResult invalidResult{};
  std::uint32_t blocksConsumed = 0U;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();

  NativeA32Memory invalidWriteMemory;
  auto invalidWriteState =
      BuildState(Oot3dNativeGame::kOot3dPauseUiUpdateDualAlphaEntry);
  invalidWriteState.r[1] = kValueAddress;
  const auto incomingInvalidWriteState = invalidWriteState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPauseUiUpdateDualAlphaEntry,
             invalidWriteState, invalidWriteMemory, &invalidResult, {2.0F},
             &blocksConsumed),
         "unmapped pause UI frame did not retain A32");
  ExpectLifecycleStateEqual(invalidWriteState, incomingInvalidWriteState,
                            "invalid pause UI entry");

  auto invalidReadMemory = BuildMemory(2, 0U);
  auto invalidReadState =
      BuildState(Oot3dNativeGame::kOot3dPauseUiAlphaPauseStateReturn);
  invalidReadState.r[0] = 0U;
  invalidReadState.r[4] = 0xDEAD0000U;
  invalidReadState.r[13] = kStackTop - 24U;
  const auto incomingInvalidReadState = invalidReadState;
  blocksConsumed = 0U;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPauseUiAlphaPauseStateReturn,
             invalidReadState, invalidReadMemory, &invalidResult, {2.0F},
             &blocksConsumed),
         "unmapped pause UI state did not retain A32");
  ExpectLifecycleStateEqual(invalidReadState, incomingInvalidReadState,
                            "invalid pause UI continuation");

  const auto invalidStats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(invalidStats.RetainedAotFallbacks == 2U &&
             invalidStats.ReadFailures == 1U &&
             invalidStats.WriteFailures == 1U,
         "invalid pause UI alpha telemetry mismatch");
}

void TestTypedPauseUiAlphaCheckpointRoundTrip() {
  constexpr std::uint32_t kPauseTextBase = 0x00479500U;
  constexpr std::uint32_t kMathTextBase = 0x00372A00U;
  constexpr std::uint32_t kTimePointerBase = 0x0051B000U;
  constexpr std::uint32_t kCheckpointStackBase = 0x10003000U;
  constexpr std::uint32_t kCheckpointTlsBase = 0x10005000U;
  constexpr std::int16_t kInitialPrimaryAlpha = 21;
  constexpr std::int16_t kInitialSecondaryAlpha = 33;
  constexpr std::uint32_t kSavedR4 = 0x44556677U;
  constexpr std::uint32_t kSavedR5 = 0x55667788U;
  constexpr std::uint32_t kSavedR6 = 0x66778899U;
  constexpr std::uint32_t kSavedS16 = 0x41424344U;
  constexpr std::uint32_t kSavedS17 = 0x51525354U;

  std::array<std::uint8_t, 0x200> pauseCode{};
  const auto writePauseLiteral = [&](std::uint32_t address,
                                     std::uint32_t value) {
    const std::size_t offset = address - kPauseTextBase;
    std::memcpy(pauseCode.data() + offset, &value, sizeof(value));
  };
  writePauseLiteral(Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool,
                    kTimeStatePointerAddress);
  writePauseLiteral(Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x04U,
                    0x3F000000U);
  writePauseLiteral(Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x08U,
                    0x41200000U);
  writePauseLiteral(Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x0CU,
                    0x41BAAAABU);
  writePauseLiteral(Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x10U,
                    0x40555556U);
  writePauseLiteral(Oot3dNativeGame::kOot3dPauseUiAlphaLiteralPool + 0x14U,
                    0x40D55556U);

  std::array<std::uint8_t, 0x200> mathCode{};
  const auto writeMathLiteral = [&](std::uint32_t address,
                                    std::uint32_t value) {
    const std::size_t offset = address - kMathTextBase;
    std::memcpy(mathCode.data() + offset, &value, sizeof(value));
  };
  writeMathLiteral(0x00372B44U, kTimeStatePointerAddress);
  writeMathLiteral(0x00372B48U, 0x3EAAAAABU);
  writeMathLiteral(0x00372B4CU, 0x3F800000U);

  const oot3d::recomp::a32::Registry registry{};
  GameStateCheckpointHost host;
  Oot3dNativeGame::NativeA32Process process(registry, host);
  std::string error;
  Expect(process.MapRegion({"pause_ui_alpha_text", kPauseTextBase,
                            pauseCode.size(), false, true, pauseCode},
                           &error) &&
             process.MapRegion({"math_step_to_s_text", kMathTextBase,
                                mathCode.size(), false, true, mathCode},
                               &error) &&
             process.MapRegion(
                 {"time_pointer", kTimePointerBase, 0x1000U, true, false, {}},
                 &error) &&
             process.MapRegion(
                 {"pause_ui_state", kActorAddress, 0x1000U, true, false, {}},
                 &error) &&
             process.MapRegion(
                 {"time_state", kTimeStateAddress, 0x1000U, true, false, {}},
                 &error) &&
             process.CreatePrimaryThread(
                 {Oot3dNativeGame::kOot3dPauseUiUpdateDualAlphaEntry,
                  kCheckpointStackBase, 0x1000U, kCheckpointTlsBase, 0x1000U,
                  0U, 0U, 0x10U, 0x01000000U, 48U},
                 &error),
         "could not create pause UI alpha checkpoint process: " + error);

  auto &state = process.PrimaryThreadState();
  state.r[1] = kActorAddress;
  state.r[4] = kSavedR4;
  state.r[5] = kSavedR5;
  state.r[6] = kSavedR6;
  state.r[14] = kReturnSentinel;
  state.vfp[16] = kSavedS16;
  state.vfp[17] = kSavedS17;
  Expect(
      process.Memory().Write32(kTimeStatePointerAddress, kTimeStateAddress) &&
          process.Memory().WriteFast(
              kTimeStateAddress + 0x110U,
              std::bit_cast<std::uint16_t>(std::int16_t{2})) &&
          process.Memory().WriteFast(
              kActorAddress + Oot3dNativeGame::kOot3dPauseUiGateTimerOffset,
              std::uint8_t{0U}) &&
          process.Memory().WriteFast(
              kActorAddress + Oot3dNativeGame::kOot3dPauseUiFadeTimerOffset,
              std::uint8_t{2U}) &&
          process.Memory().WriteFast(
              kActorAddress + Oot3dNativeGame::kOot3dPauseUiPrimaryAlphaOffset,
              std::bit_cast<std::uint16_t>(kInitialPrimaryAlpha)) &&
          process.Memory().WriteFast(
              kActorAddress +
                  Oot3dNativeGame::kOot3dPauseUiSecondaryAlphaOffset,
              std::bit_cast<std::uint16_t>(kInitialSecondaryAlpha)),
      "could not initialize pause UI alpha checkpoint memory");

  oot3d::recomp::a32::ExecutionResult dispatch{};
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedPauseUiAlpha(
             Oot3dNativeGame::kOot3dPauseUiUpdateDualAlphaEntry, state,
             process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedPauseUiAlphaResult::
                     PauseStateQueryDispatched &&
             dispatch.pc == Oot3dNativeGame::kOot3dPauseContextGetStateEntry &&
             blocksConsumed == 1U,
         "pause UI alpha owner did not dispatch its state query");

  state.r[0] = 0U;
  state.r[15] = Oot3dNativeGame::kOot3dPauseUiAlphaPauseStateReturn;
  blocksConsumed = 0U;
  Expect(
      Oot3dNativeGame::ExecuteOot3dTypedPauseUiAlpha(
          Oot3dNativeGame::kOot3dPauseUiAlphaPauseStateReturn, state,
          process.Memory(), &dispatch, &blocksConsumed) ==
              Oot3dNativeGame::Oot3dTypedPauseUiAlphaResult::
                  FirstAlphaStepDispatched &&
          dispatch.pc == Oot3dNativeGame::kOot3dPauseUiAlphaMathStepToSEntry &&
          state.r[14] == Oot3dNativeGame::kOot3dPauseUiAlphaFadeInStepReturn &&
          blocksConsumed == 1U,
      "pause UI alpha owner did not dispatch its first fade-in step");

  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPauseUiAlphaMathStepToSEntry, state,
             process.Memory(), &dispatch, {2.0F}, &blocksConsumed) &&
             dispatch.pc ==
                 Oot3dNativeGame::kOot3dPauseUiAlphaFadeInStepReturn &&
             blocksConsumed == 1U,
         "typed Math_StepToS did not reach the pause UI checkpoint");

  const auto savedFrame =
      ReadObject<std::array<std::uint32_t, 6>>(process.Memory(), state.r[13]);
  state.r[15] = Oot3dNativeGame::kOot3dPauseUiAlphaFadeInStepReturn;
  const auto encoded = process.CaptureState();
  const auto bytes = nlohmann::json::to_msgpack(encoded);
  const auto decoded = nlohmann::json::from_msgpack(bytes);

  state = {};
  Expect(process.Memory().Write32(kActorAddress, 0xFFFFFFFFU) &&
             process.Memory().Write32(kCheckpointStackBase + 0x1000U - 24U,
                                      0xFFFFFFFFU) &&
             process.RestoreState(decoded, &error),
         "could not restore pause UI alpha checkpoint: " + error);

  auto &restored = process.PrimaryThreadState();
  Expect(ReadObject<std::array<std::uint32_t, 6>>(process.Memory(),
                                                  restored.r[13]) == savedFrame,
         "pause UI alpha checkpoint did not restore its guest frame");

  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedPauseUiAlpha(
             Oot3dNativeGame::kOot3dPauseUiAlphaFadeInStepReturn, restored,
             process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedPauseUiAlphaResult::
                     TailAlphaStepDispatched &&
             dispatch.pc ==
                 Oot3dNativeGame::kOot3dPauseUiAlphaMathStepToSEntry &&
             blocksConsumed == 1U,
         "pause UI alpha checkpoint did not dispatch its tail step");

  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dPauseUiAlphaMathStepToSEntry, restored,
             process.Memory(), &dispatch, {2.0F}, &blocksConsumed) &&
             dispatch.pc == kReturnSentinel && blocksConsumed == 1U,
         "pause UI alpha checkpoint did not return through the guest LR");

  const auto primaryAlpha =
      std::bit_cast<std::int16_t>(ReadObject<std::uint16_t>(
          process.Memory(),
          kActorAddress + Oot3dNativeGame::kOot3dPauseUiPrimaryAlphaOffset));
  const auto secondaryAlpha =
      std::bit_cast<std::int16_t>(ReadObject<std::uint16_t>(
          process.Memory(),
          kActorAddress + Oot3dNativeGame::kOot3dPauseUiSecondaryAlphaOffset));
  Expect(primaryAlpha > kInitialPrimaryAlpha &&
             secondaryAlpha > kInitialSecondaryAlpha &&
             ReadObject<std::uint8_t>(
                 process.Memory(),
                 kActorAddress +
                     Oot3dNativeGame::kOot3dPauseUiFadeTimerOffset) == 1U &&
             restored.r[4] == kSavedR4 && restored.r[5] == kSavedR5 &&
             restored.r[6] == kSavedR6 && restored.vfp[16] == kSavedS16 &&
             restored.vfp[17] == kSavedS17 &&
             restored.r[13] == kCheckpointStackBase + 0x1000U,
         "pause UI alpha checkpoint did not preserve owner state");
}

void TestDynaInteractionResetDifferential() {
  constexpr std::uint32_t kDynaPlayAddress = kActorAddress;
  constexpr std::uint32_t kDynaContextAddress = kPlayAddress;
  constexpr std::uint32_t kDynaActorAddress = 0x10008000U;
  constexpr std::uint32_t kOtherActorAddress = 0x10009000U;
  constexpr std::uint8_t kInitialInteractFlags = 0xA5U;
  constexpr std::uint16_t kSlotActive = 0x0001U;
  constexpr std::uint16_t kSlotDeleted = 0x0002U;

  const auto writeSlot = [](NativeA32Memory &memory, std::uint32_t index,
                            std::uint16_t deleteFlags,
                            std::uint16_t actorFlags,
                            std::uint32_t actorAddress) {
    const std::uint32_t actorContext =
        kDynaPlayAddress +
        Oot3dNativeGame::kOot3dPlayDynaActorContextOffset;
    WriteObject(
        memory,
        kDynaContextAddress +
            Oot3dNativeGame::kOot3dDynaDeleteFlagsOffset +
            index * sizeof(std::uint16_t),
        deleteFlags);
    WriteObject(
        memory,
        actorContext + Oot3dNativeGame::kOot3dDynaActorFlagsOffset +
            index * sizeof(std::uint16_t),
        actorFlags);
    WriteU32(
        memory,
        actorContext + index * Oot3dNativeGame::kOot3dDynaActorEntrySize +
            Oot3dNativeGame::kOot3dDynaActorEntryPointerOffset,
        actorAddress);
  };

  enum class ScenarioKind : std::uint8_t {
    Empty,
    FirstMatch,
    MixedMiddleMatch,
    LastMatch,
    InactiveActorSlot,
    DeletedActorSlot,
    NonMatchingActor,
  };
  struct Scenario {
    const char *Label;
    ScenarioKind Kind;
    bool ResetsInteraction;
  };
  constexpr std::array scenarios{
      Scenario{"no active slot", ScenarioKind::Empty, false},
      Scenario{"first slot match", ScenarioKind::FirstMatch, true},
      Scenario{"mixed middle match", ScenarioKind::MixedMiddleMatch, true},
      Scenario{"last slot match", ScenarioKind::LastMatch, true},
      Scenario{"inactive actor slot", ScenarioKind::InactiveActorSlot, false},
      Scenario{"deleted actor slot", ScenarioKind::DeletedActorSlot, false},
      Scenario{"nonmatching actor", ScenarioKind::NonMatchingActor, false},
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  for (const auto &scenario : scenarios) {
    auto referenceMemory = BuildMemory(2, 0U);
    WriteObject(referenceMemory,
                kDynaActorAddress +
                    Oot3dNativeGame::kOot3dDynaActorInteractFlagsOffset,
                kInitialInteractFlags);
    switch (scenario.Kind) {
    case ScenarioKind::Empty:
      break;
    case ScenarioKind::FirstMatch:
      writeSlot(referenceMemory, 0U, kSlotActive, kSlotActive,
                kDynaActorAddress);
      break;
    case ScenarioKind::MixedMiddleMatch:
      writeSlot(referenceMemory, 1U, kSlotActive, 0U, kOtherActorAddress);
      writeSlot(referenceMemory, 2U, kSlotActive,
                kSlotActive | kSlotDeleted, kOtherActorAddress);
      writeSlot(referenceMemory, 3U, kSlotActive, kSlotActive,
                kOtherActorAddress);
      writeSlot(referenceMemory, 17U, kSlotActive, kSlotActive,
                kDynaActorAddress);
      break;
    case ScenarioKind::LastMatch:
      writeSlot(referenceMemory,
                Oot3dNativeGame::kOot3dDynaActorCount - 1U, kSlotActive,
                kSlotActive, kDynaActorAddress);
      break;
    case ScenarioKind::InactiveActorSlot:
      writeSlot(referenceMemory,
                Oot3dNativeGame::kOot3dDynaActorCount - 1U, kSlotActive, 0U,
                kDynaActorAddress);
      break;
    case ScenarioKind::DeletedActorSlot:
      writeSlot(referenceMemory,
                Oot3dNativeGame::kOot3dDynaActorCount - 1U, kSlotActive,
                kSlotActive | kSlotDeleted, kDynaActorAddress);
      break;
    case ScenarioKind::NonMatchingActor:
      writeSlot(referenceMemory,
                Oot3dNativeGame::kOot3dDynaActorCount - 1U, kSlotActive,
                kSlotActive, kOtherActorAddress);
      break;
    }
    WriteObject(referenceMemory, kStackTop - 8U,
                std::array<std::uint32_t, 2>{0xCCCCCCCCU, 0xDDDDDDDDU});
    auto typedMemory = referenceMemory;

    auto referenceState = BuildState(
        Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry);
    for (std::uint32_t index = 0U; index < 13U; ++index) {
      referenceState.r[index] = 0xA0000000U + index * 0x01010101U;
    }
    referenceState.r[0] = kDynaPlayAddress;
    referenceState.r[1] = kDynaContextAddress;
    referenceState.r[2] = kDynaActorAddress;
    referenceState.r[4] = 0x44556677U;
    referenceState.r[13] = kStackTop;
    referenceState.r[14] = kReturnSentinel;
    referenceState.r[15] =
        Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry;
    referenceState.cpsr = oot3d::recomp::a32::kFlagN |
                          oot3d::recomp::a32::kFlagV | 0x10U;
    referenceState.fpscr = 0x01000000U;
    referenceState.vfp[0] = 0x10203040U;
    referenceState.vfp[31] = 0x50607080U;
    auto typedState = referenceState;

    RunReference(
        Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry,
        referenceState, referenceMemory);

    oot3d::recomp::a32::ExecutionResult typedResult{};
    std::uint32_t blocksConsumed = 0U;
    Expect(
        Oot3dNativeGame::ExecuteOot3dTypedGameplay(
            Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry,
            typedState, typedMemory, &typedResult, {2.0F}, &blocksConsumed),
        std::string(scenario.Label) +
            ": typed Dyna interaction reset retained A32");
    Expect(typedResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
               typedResult.pc == kReturnSentinel && blocksConsumed == 1U,
           std::string(scenario.Label) +
               ": typed Dyna interaction reset returned an invalid result");
    ExpectLifecycleStateEqual(typedState, referenceState, scenario.Label);
    Expect(
        ReadObject<std::array<std::uint8_t, 0x2100>>(
            typedMemory, kDynaPlayAddress) ==
            ReadObject<std::array<std::uint8_t, 0x2100>>(
                referenceMemory, kDynaPlayAddress),
        std::string(scenario.Label) + ": PlayState Dyna table mismatch");
    Expect(
        ReadObject<std::array<std::uint8_t, 0x1600>>(
            typedMemory, kDynaContextAddress) ==
            ReadObject<std::array<std::uint8_t, 0x1600>>(
                referenceMemory, kDynaContextAddress),
        std::string(scenario.Label) + ": Dyna context mismatch");
    Expect(
        ReadObject<std::array<std::uint8_t, 0x1C0>>(
            typedMemory, kDynaActorAddress) ==
            ReadObject<std::array<std::uint8_t, 0x1C0>>(
                referenceMemory, kDynaActorAddress),
        std::string(scenario.Label) + ": Dyna actor mismatch");
    Expect(
        ReadObject<std::array<std::uint32_t, 2>>(typedMemory,
                                                 kStackTop - 8U) ==
            ReadObject<std::array<std::uint32_t, 2>>(referenceMemory,
                                                     kStackTop - 8U),
        std::string(scenario.Label) + ": saved frame mismatch");
    Expect(
        ReadObject<std::uint8_t>(
            typedMemory,
            kDynaActorAddress +
                Oot3dNativeGame::kOot3dDynaActorInteractFlagsOffset) ==
            (scenario.ResetsInteraction ? 0U : kInitialInteractFlags),
        std::string(scenario.Label) + ": interaction flag result mismatch");
  }

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == scenarios.size() &&
             stats.ActorCalls == scenarios.size() &&
             stats.DynaInteractionResetMatches == 3U &&
             stats.DynaInteractionResetMisses == 4U,
         "Dyna interaction reset telemetry mismatch");

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result{};
  std::uint32_t blocksConsumed = 0U;

  auto invalidStackMemory = BuildMemory(2, 0U);
  auto invalidStackState = BuildState(
      Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry);
  invalidStackState.r[0] = kDynaPlayAddress;
  invalidStackState.r[1] = kDynaContextAddress;
  invalidStackState.r[2] = kDynaActorAddress;
  invalidStackState.r[13] = 4U;
  const auto incomingInvalidStackState = invalidStackState;
  Expect(
      !Oot3dNativeGame::ExecuteOot3dTypedGameplay(
          Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry,
          invalidStackState, invalidStackMemory, &result, {2.0F},
          &blocksConsumed),
      "invalid Dyna interaction stack did not retain A32");
  ExpectLifecycleStateEqual(invalidStackState, incomingInvalidStackState,
                            "invalid Dyna interaction stack");

  auto invalidReadMemory = BuildMemory(2, 0U);
  auto invalidReadState = BuildState(
      Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry);
  invalidReadState.r[0] = kDynaPlayAddress;
  invalidReadState.r[1] = 0xDEAD0000U;
  invalidReadState.r[2] = kDynaActorAddress;
  const auto incomingInvalidReadState = invalidReadState;
  blocksConsumed = 0U;
  Expect(
      !Oot3dNativeGame::ExecuteOot3dTypedGameplay(
          Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry,
          invalidReadState, invalidReadMemory, &result, {2.0F},
          &blocksConsumed),
      "unmapped Dyna interaction table did not retain A32");
  ExpectLifecycleStateEqual(invalidReadState, incomingInvalidReadState,
                            "invalid Dyna interaction read");

  auto invalidActorMemory = BuildMemory(2, 0U);
  writeSlot(invalidActorMemory, 0U, kSlotActive, kSlotActive, 0xDEAD0000U);
  WriteObject(invalidActorMemory, kStackTop - 8U,
              std::array<std::uint32_t, 2>{0xCCCCCCCCU, 0xDDDDDDDDU});
  const auto incomingInvalidActorFrame =
      ReadObject<std::array<std::uint32_t, 2>>(invalidActorMemory,
                                               kStackTop - 8U);
  auto invalidActorState = BuildState(
      Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry);
  invalidActorState.r[0] = kDynaPlayAddress;
  invalidActorState.r[1] = kDynaContextAddress;
  invalidActorState.r[2] = 0xDEAD0000U;
  const auto incomingInvalidActorState = invalidActorState;
  blocksConsumed = 0U;
  Expect(
      !Oot3dNativeGame::ExecuteOot3dTypedGameplay(
          Oot3dNativeGame::kOot3dDynaResetActorInteractionIfRegisteredEntry,
          invalidActorState, invalidActorMemory, &result, {2.0F},
          &blocksConsumed),
      "unmapped Dyna actor interaction byte did not retain A32");
  ExpectLifecycleStateEqual(invalidActorState, incomingInvalidActorState,
                            "invalid Dyna interaction write");
  Expect(ReadObject<std::array<std::uint32_t, 2>>(
             invalidActorMemory, kStackTop - 8U) == incomingInvalidActorFrame,
         "failed Dyna interaction reset modified its guest frame");

  const auto invalidStats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(invalidStats.Calls == 0U &&
             invalidStats.RetainedAotFallbacks == 3U &&
             invalidStats.ReadFailures == 1U &&
             invalidStats.WriteFailures == 2U,
         "invalid Dyna interaction reset telemetry mismatch");
}

void TestTypedDynaInteractionResetCheckpointRoundTrip() {
  constexpr std::uint32_t kTextBase = 0x0047AF00U;
  constexpr std::uint32_t kCheckpointPlayAddress = 0x10000000U;
  constexpr std::uint32_t kCheckpointDynaAddress = 0x10004000U;
  constexpr std::uint32_t kCheckpointActorAddress = 0x10007000U;
  constexpr std::uint32_t kCheckpointStackBase = 0x10009000U;
  constexpr std::uint32_t kCheckpointTlsBase = 0x1000B000U;
  constexpr std::uint32_t kSlotIndex = 23U;
  constexpr std::uint32_t kSavedR4 = 0x44556677U;
  constexpr std::uint8_t kInitialInteractFlags = 0xA5U;
  constexpr std::array<std::uint8_t, 0x100> kText{};

  const oot3d::recomp::a32::Registry registry{};
  GameStateCheckpointHost host;
  Oot3dNativeGame::NativeA32Process process(registry, host);
  std::string error;
  Expect(
      process.MapRegion(
          {"dyna_interaction_text", kTextBase, kText.size(), false, true,
           kText},
          &error) &&
          process.MapRegion(
              {"dyna_play_state", kCheckpointPlayAddress, 0x3000U, true,
               false, {}},
              &error) &&
          process.MapRegion(
              {"dyna_context", kCheckpointDynaAddress, 0x2000U, true, false,
               {}},
              &error) &&
          process.MapRegion(
              {"dyna_actor", kCheckpointActorAddress, 0x1000U, true, false,
               {}},
              &error) &&
          process.CreatePrimaryThread(
              {Oot3dNativeGame::
                   kOot3dDynaResetActorInteractionIfRegisteredEntry,
               kCheckpointStackBase, 0x1000U, kCheckpointTlsBase, 0x1000U,
               0U, kCheckpointPlayAddress, 0xA0000010U, 0x01000000U, 48U},
              &error),
      "could not create Dyna interaction checkpoint process: " + error);

  auto &state = process.PrimaryThreadState();
  state.r[1] = kCheckpointDynaAddress;
  state.r[2] = kCheckpointActorAddress;
  state.r[4] = kSavedR4;
  state.r[14] = kReturnSentinel;
  const std::uint32_t actorContext =
      kCheckpointPlayAddress +
      Oot3dNativeGame::kOot3dPlayDynaActorContextOffset;
  Expect(
      process.Memory().WriteFast(
          kCheckpointDynaAddress +
              Oot3dNativeGame::kOot3dDynaDeleteFlagsOffset +
              kSlotIndex * sizeof(std::uint16_t),
          std::uint16_t{1U}) &&
          process.Memory().WriteFast(
              actorContext + Oot3dNativeGame::kOot3dDynaActorFlagsOffset +
                  kSlotIndex * sizeof(std::uint16_t),
              std::uint16_t{1U}) &&
          process.Memory().Write32(
              actorContext +
                  kSlotIndex *
                      Oot3dNativeGame::kOot3dDynaActorEntrySize +
                  Oot3dNativeGame::kOot3dDynaActorEntryPointerOffset,
              kCheckpointActorAddress) &&
          process.Memory().WriteFast(
              kCheckpointActorAddress +
                  Oot3dNativeGame::kOot3dDynaActorInteractFlagsOffset,
              kInitialInteractFlags),
      "could not initialize Dyna interaction checkpoint");

  const auto encoded = process.CaptureState();
  const auto bytes = nlohmann::json::to_msgpack(encoded);
  const auto decoded = nlohmann::json::from_msgpack(bytes);

  state = {};
  Expect(
      process.Memory().WriteFast(
          kCheckpointDynaAddress +
              Oot3dNativeGame::kOot3dDynaDeleteFlagsOffset +
              kSlotIndex * sizeof(std::uint16_t),
          std::uint16_t{0U}) &&
          process.Memory().WriteFast(
              kCheckpointActorAddress +
                  Oot3dNativeGame::kOot3dDynaActorInteractFlagsOffset,
              std::uint8_t{0xFFU}) &&
          process.RestoreState(decoded, &error),
      "could not restore Dyna interaction checkpoint: " + error);

  auto &restored = process.PrimaryThreadState();
  oot3d::recomp::a32::ExecutionResult dispatch{};
  std::uint32_t blocksConsumed = 0U;
  Expect(
      Oot3dNativeGame::ExecuteOot3dTypedDynaInteractionReset(
          Oot3dNativeGame::
              kOot3dDynaResetActorInteractionIfRegisteredEntry,
          restored, process.Memory(), &dispatch, &blocksConsumed) ==
              Oot3dNativeGame::Oot3dTypedDynaInteractionResetResult::
                  InteractionReset &&
          dispatch.kind == oot3d::recomp::a32::ExitKind::Branch &&
          dispatch.pc == kReturnSentinel && blocksConsumed == 1U,
      "Dyna interaction reset did not resume from MessagePack state");
  Expect(
      ReadObject<std::uint8_t>(
          process.Memory(),
          kCheckpointActorAddress +
              Oot3dNativeGame::kOot3dDynaActorInteractFlagsOffset) == 0U &&
          ReadObject<std::array<std::uint32_t, 2>>(
              process.Memory(), kCheckpointStackBase + 0x1000U - 8U) ==
              std::array<std::uint32_t, 2>{kSavedR4, kReturnSentinel} &&
          restored.r[0] == kCheckpointActorAddress && restored.r[1] == 0U &&
          restored.r[3] == kSlotIndex &&
          restored.r[12] == kCheckpointActorAddress &&
          restored.r[13] == kCheckpointStackBase + 0x1000U &&
          restored.r[14] == kReturnSentinel,
      "Dyna interaction checkpoint did not preserve native guest state");
}

void TestActorOwnerMicroleafDifferential() {
  constexpr std::uint32_t kLockOnActor = 0x12345678U;
  constexpr std::uint32_t kPlayerStateFlags = 0xA5A52005U;
  constexpr std::uint32_t kDefaultWord = 0xBF400000U;
  constexpr std::uint32_t kRecordAddress = kValueAddress;

  oot3d::recomp::a32::ExecutionResult typedResult{};
  std::uint32_t blocksConsumed = 0U;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();

  {
    auto referenceMemory = BuildMemory(2, 0U);
    WriteU32(referenceMemory,
             kActorAddress +
                 Oot3dNativeGame::kOot3dPlayerLockOnActorOffset,
             kLockOnActor);
    WriteU32(referenceMemory,
             kActorAddress +
                 Oot3dNativeGame::kOot3dPlayerLockOnStateFlagsOffset,
             kPlayerStateFlags);
    auto typedMemory = referenceMemory;

    auto referenceState =
        BuildState(Oot3dNativeGame::kOot3dPlayerReleaseLockOnEntry);
    referenceState.r[1] = 0x11111111U;
    referenceState.r[2] = 0x22222222U;
    referenceState.cpsr =
        oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagV | 0x10U;
    referenceState.fpscr = 0x01C00000U;
    referenceState.vfp[0] = 0x01020304U;
    auto typedState = referenceState;

    RunReference(Oot3dNativeGame::kOot3dPlayerReleaseLockOnEntry,
                 referenceState, referenceMemory);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dPlayerReleaseLockOnEntry, typedState,
               typedMemory, &typedResult, {2.0F}, &blocksConsumed),
           "typed Player_ReleaseLockOn retained A32");
    Expect(typedResult.pc == kReturnSentinel && blocksConsumed == 1U,
           "typed Player_ReleaseLockOn return mismatch");
    ExpectLifecycleStateEqual(typedState, referenceState,
                              "Player_ReleaseLockOn");
    Expect(
        ReadObject<std::array<std::uint8_t, 0x30>>(
            typedMemory,
            kActorAddress +
                Oot3dNativeGame::kOot3dPlayerLockOnActorOffset - 8U) ==
            ReadObject<std::array<std::uint8_t, 0x30>>(
                referenceMemory,
                kActorAddress +
                    Oot3dNativeGame::kOot3dPlayerLockOnActorOffset - 8U),
        "Player_ReleaseLockOn memory mismatch");
  }

  {
    auto referenceMemory = BuildMemory(2, 0U);
    WriteU32(referenceMemory,
             Oot3dNativeGame::kOot3dActorUpdateRecordDefaultWordLiteral,
             kDefaultWord);
    std::array<std::uint8_t, 0x20> initialRecord{};
    initialRecord.fill(0xA5U);
    WriteObject(referenceMemory, kRecordAddress, initialRecord);
    auto typedMemory = referenceMemory;

    auto referenceState = BuildState(
        Oot3dNativeGame::kOot3dActorUpdateRecordInitializeDefaultsEntry);
    referenceState.r[0] = kRecordAddress;
    referenceState.r[1] = 0x11111111U;
    referenceState.r[2] = 0x22222222U;
    referenceState.cpsr =
        oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV | 0x10U;
    referenceState.fpscr = 0x02C00000U;
    referenceState.vfp[0] = 0xDEADBEEFU;
    referenceState.vfp[7] = 0x07070707U;
    auto typedState = referenceState;

    RunReference(
        Oot3dNativeGame::kOot3dActorUpdateRecordInitializeDefaultsEntry,
        referenceState, referenceMemory);
    blocksConsumed = 0U;
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::
                   kOot3dActorUpdateRecordInitializeDefaultsEntry,
               typedState, typedMemory, &typedResult, {2.0F},
               &blocksConsumed),
           "typed ActorUpdateRecord_InitializeDefaults retained A32");
    Expect(typedResult.pc == kReturnSentinel && blocksConsumed == 1U,
           "ActorUpdateRecord_InitializeDefaults return mismatch");
    ExpectLifecycleStateEqual(
        typedState, referenceState,
        "ActorUpdateRecord_InitializeDefaults");
    Expect(ReadObject<decltype(initialRecord)>(typedMemory, kRecordAddress) ==
               ReadObject<decltype(initialRecord)>(referenceMemory,
                                                   kRecordAddress),
           "ActorUpdateRecord_InitializeDefaults memory mismatch");
    Expect(typedState.vfp[0] == kDefaultWord,
           "ActorUpdateRecord_InitializeDefaults hardcoded its literal");
  }

  {
    auto referenceMemory = BuildMemory(2, 0U);
    std::array<std::uint8_t, 0x20> initialRecord{};
    for (std::size_t index = 0; index < initialRecord.size(); ++index) {
      initialRecord[index] =
          static_cast<std::uint8_t>(0x40U + index);
    }
    WriteObject(referenceMemory, kRecordAddress, initialRecord);
    auto typedMemory = referenceMemory;

    auto referenceState = BuildState(
        Oot3dNativeGame::kOot3dActorUpdateRecordClearHalfwordsEntry);
    referenceState.r[0] = kRecordAddress;
    referenceState.r[1] = 0x11111111U;
    referenceState.cpsr =
        oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagC | 0x10U;
    referenceState.fpscr = 0x03C00000U;
    referenceState.vfp[0] = 0xCAFEBABEU;
    auto typedState = referenceState;

    RunReference(
        Oot3dNativeGame::kOot3dActorUpdateRecordClearHalfwordsEntry,
        referenceState, referenceMemory);
    blocksConsumed = 0U;
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dActorUpdateRecordClearHalfwordsEntry,
               typedState, typedMemory, &typedResult, {2.0F},
               &blocksConsumed),
           "typed ActorUpdateRecord_ClearHalfwords retained A32");
    Expect(typedResult.pc == kReturnSentinel && blocksConsumed == 1U,
           "ActorUpdateRecord_ClearHalfwords return mismatch");
    ExpectLifecycleStateEqual(typedState, referenceState,
                              "ActorUpdateRecord_ClearHalfwords");
    Expect(ReadObject<decltype(initialRecord)>(typedMemory, kRecordAddress) ==
               ReadObject<decltype(initialRecord)>(referenceMemory,
                                                   kRecordAddress),
           "ActorUpdateRecord_ClearHalfwords memory mismatch");
  }

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.PlayerReleaseLockOnCalls == 1U &&
             stats.ActorUpdateRecordInitializeCalls == 1U &&
             stats.ActorUpdateRecordClearCalls == 1U &&
             stats.PlayerCalls == 1U && stats.ActorCalls == 2U,
         "actor owner microleaf telemetry mismatch");

  NativeA32Memory invalidMemory;
  const std::array invalidEntries{
      Oot3dNativeGame::kOot3dPlayerReleaseLockOnEntry,
      Oot3dNativeGame::kOot3dActorUpdateRecordInitializeDefaultsEntry,
      Oot3dNativeGame::kOot3dActorUpdateRecordClearHalfwordsEntry,
  };
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  for (const auto entry : invalidEntries) {
    auto invalidState = BuildState(entry);
    invalidState.r[1] = 0x11111111U;
    invalidState.cpsr = 0xA0000010U;
    invalidState.vfp[0] = 0x12345678U;
    const auto incomingState = invalidState;
    blocksConsumed = 0U;
    Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               entry, invalidState, invalidMemory, &typedResult, {2.0F},
               &blocksConsumed),
           "invalid actor owner microleaf did not retain A32");
    ExpectLifecycleStateEqual(invalidState, incomingState,
                              "invalid actor owner microleaf");
  }
  const auto invalidStats =
      Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(invalidStats.RetainedAotFallbacks == 3U &&
             invalidStats.ReadFailures == 2U &&
             invalidStats.WriteFailures == 1U,
         "invalid actor owner microleaf telemetry mismatch");
}

void TestTypedActorOwnerMicroleafCheckpointRoundTrip() {
  constexpr std::uint32_t kPlayerTextBase = 0x00334300U;
  constexpr std::uint32_t kActorRecordTextBase = 0x0047C900U;
  constexpr std::uint32_t kCheckpointStackBase = 0x10003000U;
  constexpr std::uint32_t kCheckpointTlsBase = 0x10005000U;
  constexpr std::uint32_t kRecordAddress = kValueAddress;
  constexpr std::uint32_t kDefaultWord = 0x3F400000U;
  constexpr std::uint32_t kInitialFlags = 0x60002005U;

  std::array<std::uint8_t, 0x500> actorRecordCode{};
  constexpr std::size_t kDefaultLiteralOffset =
      Oot3dNativeGame::kOot3dActorUpdateRecordDefaultWordLiteral -
      kActorRecordTextBase;
  std::memcpy(actorRecordCode.data() + kDefaultLiteralOffset, &kDefaultWord,
              sizeof(kDefaultWord));

  const oot3d::recomp::a32::Registry registry{};
  GameStateCheckpointHost host;
  Oot3dNativeGame::NativeA32Process process(registry, host);
  std::string error;
  Expect(
      process.MapRegion(
          {"player_lock_on_text", kPlayerTextBase, 0x100U, false, true, {}},
          &error) &&
          process.MapRegion(
              {"actor_update_record_text", kActorRecordTextBase,
               actorRecordCode.size(), false, true, actorRecordCode},
              &error) &&
          process.MapRegion(
              {"actor_owner_records", kActorAddress, 0x2000U, true, false,
               {}},
              &error) &&
          process.CreatePrimaryThread(
              {Oot3dNativeGame::kOot3dPlayerReleaseLockOnEntry,
               kCheckpointStackBase, 0x1000U, kCheckpointTlsBase, 0x1000U,
               0U, kActorAddress, 0x10U, 0x03C00010U, 48U},
              &error),
      "could not create actor owner microleaf checkpoint process: " +
          error);

  auto &state = process.PrimaryThreadState();
  state.r[1] = 0x11111111U;
  state.r[14] = kReturnSentinel;
  state.vfp[0] = 0xDEADBEEFU;
  std::array<std::uint8_t, 0x20> initialRecord{};
  initialRecord.fill(0xCCU);
  Expect(
      process.Memory().Write32(
          kActorAddress +
              Oot3dNativeGame::kOot3dPlayerLockOnActorOffset,
          0x12345678U) &&
          process.Memory().Write32(
              kActorAddress +
                  Oot3dNativeGame::kOot3dPlayerLockOnStateFlagsOffset,
              kInitialFlags) &&
          process.Memory().WriteBytes(kRecordAddress, initialRecord),
      "could not initialize actor owner microleaf checkpoint");

  const auto encoded = process.CaptureState();
  const auto bytes = nlohmann::json::to_msgpack(encoded);
  const auto decoded = nlohmann::json::from_msgpack(bytes);

  state = {};
  Expect(
      process.Memory().Write32(
          kActorAddress +
              Oot3dNativeGame::kOot3dPlayerLockOnActorOffset,
          0xFFFFFFFFU) &&
          process.Memory().Write32(kRecordAddress, 0U) &&
          process.RestoreState(decoded, &error),
      "could not restore actor owner microleaf checkpoint: " + error);

  auto &restored = process.PrimaryThreadState();
  oot3d::recomp::a32::ExecutionResult dispatch{};
  std::uint32_t blocksConsumed = 0U;
  Expect(
      Oot3dNativeGame::ExecuteOot3dTypedPlayerLockOn(
          Oot3dNativeGame::kOot3dPlayerReleaseLockOnEntry, restored,
          process.Memory(), &dispatch, &blocksConsumed) ==
              Oot3dNativeGame::Oot3dTypedPlayerLockOnResult::Released &&
          dispatch.pc == kReturnSentinel && blocksConsumed == 1U &&
          ReadObject<std::uint32_t>(
              process.Memory(),
              kActorAddress +
                  Oot3dNativeGame::kOot3dPlayerLockOnActorOffset) == 0U &&
          ReadObject<std::uint32_t>(
              process.Memory(),
              kActorAddress +
                  Oot3dNativeGame::kOot3dPlayerLockOnStateFlagsOffset) ==
              (kInitialFlags &
               ~Oot3dNativeGame::kOot3dPlayerLockOnStateFlag),
      "Player_ReleaseLockOn did not resume from MessagePack state");

  restored.r[0] = kRecordAddress;
  restored.r[1] = 0x11111111U;
  restored.r[14] = kReturnSentinel;
  restored.r[15] =
      Oot3dNativeGame::kOot3dActorUpdateRecordInitializeDefaultsEntry;
  blocksConsumed = 0U;
  Expect(
      Oot3dNativeGame::ExecuteOot3dTypedActorUpdateRecord(
          restored.r[15], restored, process.Memory(), &dispatch,
          &blocksConsumed) ==
              Oot3dNativeGame::Oot3dTypedActorUpdateRecordResult::
                  InitializedDefaults &&
          dispatch.pc == kReturnSentinel && blocksConsumed == 1U &&
          restored.vfp[0] == kDefaultWord,
      "ActorUpdateRecord_InitializeDefaults did not resume from state");

  restored.r[0] = kRecordAddress;
  restored.r[1] = 0x11111111U;
  restored.r[14] = kReturnSentinel;
  restored.r[15] =
      Oot3dNativeGame::kOot3dActorUpdateRecordClearHalfwordsEntry;
  blocksConsumed = 0U;
  Expect(
      Oot3dNativeGame::ExecuteOot3dTypedActorUpdateRecord(
          restored.r[15], restored, process.Memory(), &dispatch,
          &blocksConsumed) ==
              Oot3dNativeGame::Oot3dTypedActorUpdateRecordResult::
                  ClearedHalfwords &&
          dispatch.pc == kReturnSentinel && blocksConsumed == 1U,
      "ActorUpdateRecord_ClearHalfwords did not resume from state");

  Expect(
      ReadObject<std::uint16_t>(process.Memory(), kRecordAddress + 0x04U) ==
              0U &&
          ReadObject<std::uint16_t>(process.Memory(),
                                    kRecordAddress + 0x0CU) == 0U &&
          ReadObject<std::uint32_t>(process.Memory(),
                                    kRecordAddress + 0x08U) ==
              kDefaultWord &&
          ReadObject<std::array<std::uint8_t, 4>>(
              process.Memory(), kRecordAddress + 0x18U) ==
              std::array<std::uint8_t, 4>{},
      "actor update record checkpoint memory mismatch");
}

void TestRecordInitializerDifferential() {
  constexpr std::uint32_t kRecordAddress = kValueAddress;
  constexpr std::uint32_t kSavedR4 = 0x44556677U;
  constexpr std::uint32_t kTrailingWord = 0xDEADBEEFU;

  std::array<std::uint8_t,
             Oot3dNativeGame::kOot3dRecordInitializedPrefixSize +
                 sizeof(std::uint32_t)>
      initialRecord{};
  initialRecord.fill(0xA5U);
  std::memcpy(initialRecord.data() +
                  Oot3dNativeGame::kOot3dRecordInitializedPrefixSize,
              &kTrailingWord, sizeof(kTrailingWord));

  auto referenceMemory = BuildMemory(2, 0U);
  WriteObject(referenceMemory, kRecordAddress, initialRecord);
  auto typedMemory = referenceMemory;

  auto referenceState =
      BuildState(Oot3dNativeGame::kOot3dRecordInitializerEntry);
  referenceState.r[0] = kRecordAddress;
  referenceState.r[4] = kSavedR4;
  referenceState.r[1] = 0x11111111U;
  referenceState.cpsr =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagC | 0x10U;
  auto typedState = referenceState;

  const auto referenceDispatch = ExecuteLifecycleReferenceBlock(
      Oot3dNativeGame::kOot3dRecordInitializerEntry, referenceState,
      referenceMemory);
  oot3d::recomp::a32::ExecutionResult typedResult{};
  std::uint32_t blocksConsumed = 0U;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dRecordInitializerEntry, typedState,
             typedMemory, &typedResult, {2.0F}, &blocksConsumed),
         "typed record initializer entry retained A32");
  Expect(referenceDispatch.pc == Oot3dNativeGame::kOot3dRuntimeMemzeroEntry &&
             typedResult.pc == referenceDispatch.pc &&
             blocksConsumed == 1U,
         "record initializer memzero dispatch mismatch");
  ExpectLifecycleStateEqual(typedState, referenceState,
                            "record initializer memzero dispatch");
  Expect(ReadObject<decltype(initialRecord)>(typedMemory, kRecordAddress) ==
             ReadObject<decltype(initialRecord)>(referenceMemory,
                                                 kRecordAddress),
         "record initializer entry memory mismatch");
  Expect(ReadObject<std::array<std::uint32_t, 2>>(
             typedMemory, kStackTop - 8U) ==
             ReadObject<std::array<std::uint32_t, 2>>(
                 referenceMemory, kStackTop - 8U),
         "record initializer saved frame mismatch");

  std::array<std::uint8_t,
             Oot3dNativeGame::kOot3dRecordInitializerMemzeroSize>
      zeroPrefix{};
  Expect(referenceMemory.WriteBytes(kRecordAddress, zeroPrefix) &&
             typedMemory.WriteBytes(kRecordAddress, zeroPrefix),
         "could not apply reference record memzero");
  for (auto *state : {&referenceState, &typedState}) {
    state->r[0] = 0xAAAAAAAAU;
    state->r[1] = 0xBBBBBBBBU;
    state->cpsr = oot3d::recomp::a32::kFlagV | 0x10U;
    state->r[15] =
        Oot3dNativeGame::kOot3dRecordInitializerMemzeroReturn;
  }

  const auto referenceReturn = ExecuteLifecycleReferenceBlock(
      Oot3dNativeGame::kOot3dRecordInitializerMemzeroReturn,
      referenceState, referenceMemory);
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dRecordInitializerMemzeroReturn,
             typedState, typedMemory, &typedResult, {2.0F},
             &blocksConsumed),
         "typed record initializer continuation retained A32");
  Expect(referenceReturn.pc == kReturnSentinel &&
             typedResult.pc == referenceReturn.pc &&
             blocksConsumed == 1U,
         "record initializer return mismatch");
  ExpectLifecycleStateEqual(typedState, referenceState,
                            "record initializer return");
  const auto initialized =
      ReadObject<decltype(initialRecord)>(typedMemory, kRecordAddress);
  Expect(std::all_of(
             initialized.begin(),
             initialized.begin() +
                 Oot3dNativeGame::kOot3dRecordInitializedPrefixSize,
             [](std::uint8_t value) { return value == 0U; }) &&
             ReadObject<std::uint32_t>(
                 typedMemory,
                 kRecordAddress +
                     Oot3dNativeGame::kOot3dRecordInitializedPrefixSize) ==
                 kTrailingWord,
         "record initializer did not preserve the stride tail");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.RecordInitializerMemzeroDispatches == 1U &&
             stats.RecordInitializerReturns == 1U,
         "record initializer telemetry mismatch");

  NativeA32Memory invalidMemory;
  auto invalidState =
      BuildState(Oot3dNativeGame::kOot3dRecordInitializerEntry);
  invalidState.r[0] = kRecordAddress;
  invalidState.r[4] = kSavedR4;
  const auto incomingInvalidState = invalidState;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dRecordInitializerEntry, invalidState,
             invalidMemory, &typedResult, {2.0F}, &blocksConsumed),
         "invalid record initializer did not retain A32");
  ExpectLifecycleStateEqual(invalidState, incomingInvalidState,
                            "invalid record initializer");
  const auto invalidStats =
      Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(invalidStats.RetainedAotFallbacks == 1U &&
             invalidStats.WriteFailures == 1U,
         "invalid record initializer telemetry mismatch");
}

void TestTypedRecordInitializerCheckpointRoundTrip() {
  constexpr std::uint32_t kRecordTextBase = 0x002FFA00U;
  constexpr std::uint32_t kCheckpointStackBase = 0x10001000U;
  constexpr std::uint32_t kCheckpointTlsBase = 0x10003000U;
  constexpr std::uint32_t kSavedR4 = 0x44556677U;
  constexpr std::uint32_t kTrailingWord = 0xC0DEC0DEU;

  const oot3d::recomp::a32::Registry registry{};
  GameStateCheckpointHost host;
  Oot3dNativeGame::NativeA32Process process(registry, host);
  std::string error;
  Expect(process.MapRegion(
             {"record_initializer_text", kRecordTextBase, 0x100U, false,
              true, {}},
             &error) &&
             process.MapRegion(
                 {"record", kActorAddress, 0x1000U, true, false, {}},
                 &error) &&
             process.CreatePrimaryThread(
                 {Oot3dNativeGame::kOot3dRecordInitializerEntry,
                  kCheckpointStackBase, 0x1000U, kCheckpointTlsBase,
                  0x1000U, 0U, kActorAddress, 0x10U, 0x03C00010U,
                  48U},
                 &error),
         "could not create record initializer checkpoint process: " +
             error);

  auto &state = process.PrimaryThreadState();
  state.r[4] = kSavedR4;
  state.r[14] = kReturnSentinel;
  std::array<std::uint8_t,
             Oot3dNativeGame::kOot3dRecordInitializedPrefixSize>
      initialPrefix{};
  initialPrefix.fill(0x5AU);
  Expect(process.Memory().WriteBytes(kActorAddress, initialPrefix) &&
             process.Memory().Write32(
                 kActorAddress +
                     Oot3dNativeGame::kOot3dRecordInitializedPrefixSize,
                 kTrailingWord),
         "could not initialize record checkpoint memory");

  oot3d::recomp::a32::ExecutionResult dispatch{};
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedRecordInitializer(
             Oot3dNativeGame::kOot3dRecordInitializerEntry, state,
             process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedRecordInitializerResult::
                     MemzeroDispatched &&
             dispatch.pc == Oot3dNativeGame::kOot3dRuntimeMemzeroEntry &&
             blocksConsumed == 1U,
         "record initializer did not reach its checkpoint boundary");

  std::array<std::uint8_t,
             Oot3dNativeGame::kOot3dRecordInitializerMemzeroSize>
      zeroPrefix{};
  Expect(process.Memory().WriteBytes(kActorAddress, zeroPrefix),
         "could not emulate record initializer memzero");
  state.r[0] = 0xAAAAAAAAU;
  state.r[15] =
      Oot3dNativeGame::kOot3dRecordInitializerMemzeroReturn;
  const auto encoded = process.CaptureState();
  const auto bytes = nlohmann::json::to_msgpack(encoded);
  const auto decoded = nlohmann::json::from_msgpack(bytes);

  state = {};
  Expect(process.Memory().Write32(kActorAddress, 0xFFFFFFFFU) &&
             process.RestoreState(decoded, &error),
         "could not restore record initializer checkpoint: " + error);

  auto &restored = process.PrimaryThreadState();
  blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedRecordInitializer(
             Oot3dNativeGame::kOot3dRecordInitializerMemzeroReturn,
             restored, process.Memory(), &dispatch, &blocksConsumed) ==
                 Oot3dNativeGame::Oot3dTypedRecordInitializerResult::
                     InitReturned &&
             dispatch.pc == kReturnSentinel && blocksConsumed == 1U,
         "record initializer did not resume from its checkpoint");
  const auto initializedPrefix =
      ReadObject<decltype(initialPrefix)>(process.Memory(), kActorAddress);
  Expect(std::all_of(initializedPrefix.begin(), initializedPrefix.end(),
                     [](std::uint8_t value) { return value == 0U; }) &&
             ReadObject<std::uint32_t>(
                 process.Memory(),
                 kActorAddress +
                     Oot3dNativeGame::kOot3dRecordInitializedPrefixSize) ==
                 kTrailingWord &&
             restored.r[0] == kActorAddress &&
             restored.r[4] == kSavedR4 &&
             restored.r[13] == kCheckpointStackBase + 0x1000U,
         "record initializer checkpoint did not preserve guest state");
}

bool StubSkelAnimeEffect(std::uint32_t pc,
                         oot3d::recomp::a32::GuestState &state,
                         oot3d::recomp::a32::MemoryBus &,
                         oot3d::recomp::a32::ExecutionResult *result,
                         void *user) {
  const bool executeHelperBodies =
      user != nullptr && *static_cast<const bool *>(user);
  if (executeHelperBodies && (pc == 0x002BB1CCU || pc == 0x002BB34CU)) {
    return false;
  }
  if (pc == 0x00324154U) {
    state.r[0] = kFakeAnimationEntry;
  }
  state.r[15] = state.r[14];
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      state.r[15],
      oot3d::recomp::a32::FallbackReason::None,
      pc,
  };
  return true;
}

struct AnimationChangeEffectCall {
  std::uint32_t Pc = 0U;
  std::array<std::uint32_t, 4> Arguments{};
  std::uint32_t S0 = 0U;

  bool operator==(const AnimationChangeEffectCall &other) const noexcept {
    return Pc == other.Pc && Arguments == other.Arguments && S0 == other.S0;
  }
};

struct AnimationChangeEffectTrace {
  std::vector<AnimationChangeEffectCall> Calls;
};

bool StubAnimationChangeEffect(std::uint32_t pc,
                               oot3d::recomp::a32::GuestState &state,
                               oot3d::recomp::a32::MemoryBus &memory,
                               oot3d::recomp::a32::ExecutionResult *result,
                               void *user) {
  static_cast<AnimationChangeEffectTrace *>(user)->Calls.push_back(
      {pc, {state.r[0], state.r[1], state.r[2], state.r[3]}, state.vfp[0]});
  return StubSkelAnimeEffect(pc, state, memory, result, nullptr);
}

struct LinkAnimationEffectCall {
  std::uint32_t Pc = 0U;
  std::array<std::uint32_t, 4> Arguments{};
  std::array<std::uint32_t, 3> StackArguments{};
  std::uint32_t S0 = 0U;

  bool operator==(const LinkAnimationEffectCall &other) const noexcept {
    return Pc == other.Pc && Arguments == other.Arguments &&
           StackArguments == other.StackArguments && S0 == other.S0;
  }
};

struct LinkAnimationEffectTrace {
  std::vector<LinkAnimationEffectCall> Calls;
};

struct LinkAnimationBoundaryCapture {
  oot3d::recomp::a32::GuestState State;
  bool Called = false;
};

bool CaptureLinkAnimationChange(std::uint32_t pc,
                                oot3d::recomp::a32::GuestState &state,
                                oot3d::recomp::a32::MemoryBus &,
                                oot3d::recomp::a32::ExecutionResult *result,
                                void *user) {
  Expect(pc == Oot3dNativeGame::kOot3dLinkAnimationChangeEntry,
         "unexpected LinkAnimation wrapper tail");
  auto &capture = *static_cast<LinkAnimationBoundaryCapture *>(user);
  capture.State = state;
  capture.Called = true;
  state.r[15] = state.r[14];
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      state.r[15],
      oot3d::recomp::a32::FallbackReason::None,
      pc,
  };
  return true;
}

struct CutsceneProcessCapture {
  oot3d::recomp::a32::GuestState State;
  std::uint32_t Calls = 0U;
};

bool CaptureCutsceneProcessCommands(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::MemoryBus &,
    oot3d::recomp::a32::ExecutionResult *result, void *user) {
  Expect(pc == Oot3dNativeGame::kOot3dCutsceneProcessCommandsEntry,
         "unexpected cutscene command dispatcher entry");
  auto &capture = *static_cast<CutsceneProcessCapture *>(user);
  capture.State = state;
  ++capture.Calls;
  state.r[15] = state.r[14];
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      state.r[15],
      oot3d::recomp::a32::FallbackReason::None,
      pc,
  };
  return true;
}

bool StubLinkAnimationEffect(std::uint32_t pc,
                             oot3d::recomp::a32::GuestState &state,
                             oot3d::recomp::a32::MemoryBus &memory,
                             oot3d::recomp::a32::ExecutionResult *result,
                             void *user) {
  LinkAnimationEffectCall call{
      .Pc = pc,
      .Arguments = {state.r[0], state.r[1], state.r[2], state.r[3]},
      .S0 = state.vfp[0],
  };
  if (pc == 0x002BD9ECU) {
    for (std::size_t index = 0U; index < call.StackArguments.size(); ++index) {
      Expect(memory.Read32(state.r[13] + static_cast<std::uint32_t>(index) * 4U,
                           &call.StackArguments[index]),
             "could not read LinkAnimation stack argument");
    }
  }
  static_cast<LinkAnimationEffectTrace *>(user)->Calls.push_back(call);
  return StubSkelAnimeEffect(pc, state, memory, result, nullptr);
}

struct TypedBoundaryContext {
  NativeA32Memory *Memory = nullptr;
  float NativeUpdateRate = 0.0f;
};

bool ExecuteTypedBoundary(std::uint32_t pc,
                          oot3d::recomp::a32::GuestState &state,
                          oot3d::recomp::a32::MemoryBus &,
                          oot3d::recomp::a32::ExecutionResult *result,
                          std::uint32_t, std::uint32_t *blocksConsumed,
                          void *user) {
  auto &context = *static_cast<TypedBoundaryContext *>(user);
  return context.Memory != nullptr &&
         Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             pc, state, *context.Memory, result, {context.NativeUpdateRate},
             blocksConsumed);
}

void RunSkelAnimeReference(std::uint32_t entry,
                           oot3d::recomp::a32::GuestState &state,
                           NativeA32Memory &memory) {
  const auto result = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), entry, state, memory, nullptr,
      nullptr, 40'000U, nullptr, nullptr, nullptr, 0U, &StubSkelAnimeEffect,
      nullptr, kSkelAnimeEffectEntries.data(), kSkelAnimeEffectEntries.size());
  if (result.pc != kReturnSentinel ||
      result.kind != oot3d::recomp::a32::ExitKind::MissingBlock) {
    throw std::runtime_error(
        "SkelAnime A32 reference stopped at pc=" + std::to_string(result.pc) +
        " kind=" + std::to_string(static_cast<unsigned>(result.kind)));
  }
}

void ExpectFloatBits(float actual, float expected, const char *field) {
  if (std::bit_cast<std::uint32_t>(actual) !=
      std::bit_cast<std::uint32_t>(expected)) {
    throw std::runtime_error(std::string("typed/A32 float mismatch at ") +
                             field + ": typed=" + std::to_string(actual) +
                             ", A32=" + std::to_string(expected));
  }
}

void ExpectActorMotionEqual(const Actor &actual, const Actor &expected) {
  ExpectFloatBits(actual.WorldPosition.X, expected.WorldPosition.X,
                  "world_position.x");
  ExpectFloatBits(actual.WorldPosition.Y, expected.WorldPosition.Y,
                  "world_position.y");
  ExpectFloatBits(actual.WorldPosition.Z, expected.WorldPosition.Z,
                  "world_position.z");
  ExpectFloatBits(actual.Velocity.X, expected.Velocity.X, "velocity.x");
  ExpectFloatBits(actual.Velocity.Y, expected.Velocity.Y, "velocity.y");
  ExpectFloatBits(actual.Velocity.Z, expected.Velocity.Z, "velocity.z");
}

void TestActorFunction(std::uint32_t entry, std::int16_t updateRate,
                       std::uint16_t angle) {
  auto referenceMemory = BuildMemory(updateRate, angle);
  auto typedMemory = referenceMemory;
  const Actor initial = BuildActor(std::bit_cast<std::int16_t>(angle));
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  auto referenceState = BuildState(entry);
  RunReference(entry, referenceState, referenceMemory);

  auto typedState = BuildState(entry);
  oot3d::recomp::a32::ExecutionResult typedResult;
  std::uint32_t blocksConsumed = 0;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             entry, typedState, typedMemory, &typedResult,
             {static_cast<float>(updateRate)}, &blocksConsumed),
         "typed actor function retained AOT unexpectedly");
  Expect(typedResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
             typedResult.pc == kReturnSentinel && blocksConsumed == 1U,
         "typed actor function returned an invalid ABI result");

  ExpectActorMotionEqual(ReadObject<Actor>(typedMemory, kActorAddress),
                         ReadObject<Actor>(referenceMemory, kActorAddress));
}

void TestActorDifferential() {
  constexpr std::uint16_t angle = 0x1234U;
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    TestActorFunction(
        Oot3dNativeGame::kOot3dActorUpdatePosWithVelocityFromRotationEntry,
        updateRate, angle);
    TestActorFunction(Oot3dNativeGame::kOot3dActorUpdateVelocityXZGravityEntry,
                      updateRate, angle);
    TestActorFunction(Oot3dNativeGame::kOot3dActorUpdateVelocityXYZEntry,
                      updateRate, angle);
    TestActorFunction(Oot3dNativeGame::kOot3dActorUpdatePosEntry, updateRate,
                      angle);
    TestActorFunction(Oot3dNativeGame::kOot3dActorMoveForwardEntry, updateRate,
                      angle);
  }
}

void ExpectActorBytesEqual(const Actor &actual, const Actor &expected,
                           const char *operation) {
  Expect(std::memcmp(&actual, &expected, sizeof(Actor)) == 0,
         std::string("typed/A32 Actor mismatch after ") + operation);
}

void TestActorParentQuery(std::uint32_t entry, std::uint32_t parentAddress,
                          std::uint32_t expected) {
  auto referenceMemory = BuildMemory(2, 0U);
  auto typedMemory = referenceMemory;
  Actor initial = BuildActor(0);
  initial.Parent.Address = parentAddress;
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  auto referenceState = BuildState(entry);
  RunReference(entry, referenceState, referenceMemory);
  auto typedState = BuildState(entry);
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             entry, typedState, typedMemory, &result, {2.0f}),
         "typed parent query retained AOT unexpectedly");
  Expect(typedState.r[0] == referenceState.r[0] && typedState.r[0] == expected,
         "typed/A32 parent query mismatch");
  ExpectActorBytesEqual(ReadObject<Actor>(typedMemory, kActorAddress),
                        ReadObject<Actor>(referenceMemory, kActorAddress),
                        "parent query");
}

void TestActorKillDifferential() {
  auto referenceMemory = BuildMemory(2, 0U);
  auto typedMemory = referenceMemory;
  Actor initial = BuildActor(0);
  initial.Flags = 0xA5A5A5A5U;
  initial.Update.Address = 0x00123456U;
  initial.Draw.Address = 0x00654320U;
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  auto referenceState = BuildState(Oot3dNativeGame::kOot3dActorKillEntry);
  RunReference(Oot3dNativeGame::kOot3dActorKillEntry, referenceState,
               referenceMemory);
  auto typedState = BuildState(Oot3dNativeGame::kOot3dActorKillEntry);
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorKillEntry, typedState, typedMemory,
             &result, {2.0f}),
         "typed Actor_Kill retained AOT unexpectedly");
  ExpectActorBytesEqual(ReadObject<Actor>(typedMemory, kActorAddress),
                        ReadObject<Actor>(referenceMemory, kActorAddress),
                        "Actor_Kill");
}

void TestActorScaleDifferential() {
  auto referenceMemory = BuildMemory(2, 0U);
  auto typedMemory = referenceMemory;
  Actor initial = BuildActor(0);
  initial.Scale = {1.0f, 2.0f, 3.0f};
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  constexpr float scale = 0.015f;
  const auto buildScaleState = [scale] {
    auto state = BuildState(Oot3dNativeGame::kOot3dActorSetScaleEntry);
    state.vfp[0] = std::bit_cast<std::uint32_t>(scale);
    return state;
  };
  auto referenceState = buildScaleState();
  RunReference(Oot3dNativeGame::kOot3dActorSetScaleEntry, referenceState,
               referenceMemory);
  auto typedState = buildScaleState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorSetScaleEntry, typedState, typedMemory,
             &result, {2.0f}),
         "typed Actor_SetScale retained AOT unexpectedly");
  ExpectActorBytesEqual(ReadObject<Actor>(typedMemory, kActorAddress),
                        ReadObject<Actor>(referenceMemory, kActorAddress),
                        "Actor_SetScale");
}

void TestActorLeafDifferential() {
  TestActorParentQuery(Oot3dNativeGame::kOot3dActorHasParentEntry, 0U, 0U);
  TestActorParentQuery(Oot3dNativeGame::kOot3dActorHasParentEntry, 0x10203040U,
                       1U);
  TestActorParentQuery(Oot3dNativeGame::kOot3dActorHasNoParentEntry, 0U, 1U);
  TestActorParentQuery(Oot3dNativeGame::kOot3dActorHasNoParentEntry,
                       0x10203040U, 0U);
  TestActorKillDifferential();
  TestActorScaleDifferential();
}

void TestSmoothStepCase(std::int16_t current, std::int16_t target,
                        std::int32_t scale, std::int32_t maximumStep,
                        std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  referenceMemory.WriteFast(kValueAddress,
                            std::bit_cast<std::uint16_t>(current));
  typedMemory.WriteFast(kValueAddress, std::bit_cast<std::uint16_t>(current));

  const auto buildSmoothState = [&] {
    auto state =
        BuildState(Oot3dNativeGame::kOot3dMathSmoothStepToSUpdateRateEntry);
    state.r[0] = kValueAddress;
    state.r[1] = static_cast<std::uint16_t>(target);
    state.r[2] = static_cast<std::uint32_t>(scale);
    state.r[3] = static_cast<std::uint32_t>(maximumStep);
    return state;
  };
  auto referenceState = buildSmoothState();
  RunReference(Oot3dNativeGame::kOot3dMathSmoothStepToSUpdateRateEntry,
               referenceState, referenceMemory);

  auto typedState = buildSmoothState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMathSmoothStepToSUpdateRateEntry,
             typedState, typedMemory, &result,
             {static_cast<float>(updateRate)}),
         "typed smooth-step retained AOT unexpectedly");
  const auto typed = ReadObject<std::uint16_t>(typedMemory, kValueAddress);
  const auto reference =
      ReadObject<std::uint16_t>(referenceMemory, kValueAddress);
  Expect(typed == reference, "typed/A32 smooth-step mismatch");
}

void TestSmoothStepDifferential() {
  TestSmoothStepCase(0, 1000, 10, 50, 2);
  TestSmoothStepCase(1000, -500, 7, 80, 2);
  TestSmoothStepCase(-20, -25, 3, 40, 1);
  TestSmoothStepCase(32760, -32760, 4, 100, 1);
}

void TestStepToFCase(float current, float target, float step,
                     std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  WriteFloat(referenceMemory, kValueAddress, current);
  WriteFloat(typedMemory, kValueAddress, current);

  const auto buildStepState = [&] {
    auto state = BuildState(Oot3dNativeGame::kOot3dMathStepToFEntry);
    state.r[0] = kValueAddress;
    state.vfp[0] = std::bit_cast<std::uint32_t>(target);
    state.vfp[1] = std::bit_cast<std::uint32_t>(step);
    return state;
  };
  auto referenceState = buildStepState();
  RunReference(Oot3dNativeGame::kOot3dMathStepToFEntry, referenceState,
               referenceMemory);
  auto typedState = buildStepState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMathStepToFEntry, typedState, typedMemory,
             &result, {static_cast<float>(updateRate)}),
         "typed Math_StepToF retained AOT unexpectedly");
  Expect(ReadObject<std::uint32_t>(typedMemory, kValueAddress) ==
             ReadObject<std::uint32_t>(referenceMemory, kValueAddress),
         "typed/A32 Math_StepToF value mismatch");
  Expect(typedState.r[0] == referenceState.r[0],
         "typed/A32 Math_StepToF return mismatch");
}

void TestStepToFDifferential() {
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    TestStepToFCase(0.0f, 10.0f, 3.0f, updateRate);
    TestStepToFCase(10.0f, -2.0f, 4.0f, updateRate);
    TestStepToFCase(9.5f, 10.0f, 3.0f, updateRate);
    TestStepToFCase(4.0f, 4.0f, 0.0f, updateRate);
    TestStepToFCase(4.0f, 5.0f, 0.0f, updateRate);
  }
}

void TestApproachFCase(float current, float target, float fraction,
                       float maximumStep, std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  WriteFloat(referenceMemory, kValueAddress, current);
  WriteFloat(typedMemory, kValueAddress, current);
  const auto buildState = [&] {
    auto state = BuildState(Oot3dNativeGame::kOot3dMathApproachFEntry);
    state.r[0] = kValueAddress;
    state.vfp[0] = std::bit_cast<std::uint32_t>(target);
    state.vfp[1] = std::bit_cast<std::uint32_t>(fraction);
    state.vfp[2] = std::bit_cast<std::uint32_t>(maximumStep);
    return state;
  };
  auto referenceState = buildState();
  RunReference(Oot3dNativeGame::kOot3dMathApproachFEntry, referenceState,
               referenceMemory);
  auto typedState = buildState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMathApproachFEntry, typedState, typedMemory,
             &result, {static_cast<float>(updateRate)}),
         "typed Math_ApproachF retained AOT unexpectedly");
  Expect(ReadObject<std::uint32_t>(typedMemory, kValueAddress) ==
             ReadObject<std::uint32_t>(referenceMemory, kValueAddress),
         "typed/A32 Math_ApproachF mismatch");
}

void TestApproachZeroFCase(float current, float fraction, float maximumStep,
                           std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  WriteFloat(referenceMemory, kValueAddress, current);
  WriteFloat(typedMemory, kValueAddress, current);
  const auto buildState = [&] {
    auto state = BuildState(Oot3dNativeGame::kOot3dMathApproachZeroFEntry);
    state.r[0] = kValueAddress;
    state.vfp[0] = std::bit_cast<std::uint32_t>(fraction);
    state.vfp[1] = std::bit_cast<std::uint32_t>(maximumStep);
    return state;
  };
  auto referenceState = buildState();
  RunReference(Oot3dNativeGame::kOot3dMathApproachZeroFEntry, referenceState,
               referenceMemory);
  auto typedState = buildState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMathApproachZeroFEntry, typedState,
             typedMemory, &result, {static_cast<float>(updateRate)}),
         "typed Math_ApproachZeroF retained AOT unexpectedly");
  Expect(ReadObject<std::uint32_t>(typedMemory, kValueAddress) ==
             ReadObject<std::uint32_t>(referenceMemory, kValueAddress),
         "typed/A32 Math_ApproachZeroF mismatch");
}

void TestFloatApproachDifferential() {
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    TestApproachFCase(0.0f, 20.0f, 0.2f, 3.0f, updateRate);
    TestApproachFCase(20.0f, -5.0f, 0.25f, 2.0f, updateRate);
    TestApproachFCase(1.0f, 1.000005f, 0.3f, 2.0f, updateRate);
    TestApproachFCase(4.0f, 4.0f, 0.3f, 2.0f, updateRate);
    TestApproachZeroFCase(12.0f, 0.2f, 2.0f, updateRate);
    TestApproachZeroFCase(-12.0f, 0.2f, 2.0f, updateRate);
    TestApproachZeroFCase(0.000005f, 0.2f, 2.0f, updateRate);
  }
}

void TestScaledStepToSCase(std::int16_t current, std::int16_t target,
                           std::int16_t step, std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  referenceMemory.WriteFast(kValueAddress,
                            std::bit_cast<std::uint16_t>(current));
  typedMemory.WriteFast(kValueAddress, std::bit_cast<std::uint16_t>(current));
  const auto buildState = [&] {
    auto state = BuildState(Oot3dNativeGame::kOot3dMathScaledStepToSEntry);
    state.r[0] = kValueAddress;
    state.r[1] =
        std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(target));
    state.r[2] = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(step));
    return state;
  };
  auto referenceState = buildState();
  RunReference(Oot3dNativeGame::kOot3dMathScaledStepToSEntry, referenceState,
               referenceMemory);
  auto typedState = buildState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMathScaledStepToSEntry, typedState,
             typedMemory, &result, {static_cast<float>(updateRate)}),
         "typed Math_ScaledStepToS retained AOT unexpectedly");
  Expect(ReadObject<std::uint16_t>(typedMemory, kValueAddress) ==
             ReadObject<std::uint16_t>(referenceMemory, kValueAddress),
         "typed/A32 Math_ScaledStepToS value mismatch");
  Expect(typedState.r[0] == referenceState.r[0],
         "typed/A32 Math_ScaledStepToS return mismatch");
}

void TestScaledStepToSDifferential() {
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    TestScaledStepToSCase(0, 1000, 100, updateRate);
    TestScaledStepToSCase(1000, -500, 80, updateRate);
    TestScaledStepToSCase(990, 1000, 100, updateRate);
    TestScaledStepToSCase(100, 100, 0, updateRate);
    TestScaledStepToSCase(100, 120, 0, updateRate);
    TestScaledStepToSCase(32760, -32760, 100, updateRate);
  }
}

void TestStepToSCase(std::int16_t current, std::int16_t target,
                     std::int16_t step, std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  referenceMemory.WriteFast(kValueAddress,
                            std::bit_cast<std::uint16_t>(current));
  typedMemory.WriteFast(kValueAddress, std::bit_cast<std::uint16_t>(current));
  const auto buildState = [&] {
    auto state = BuildState(Oot3dNativeGame::kOot3dMathStepToSEntry);
    state.r[0] = kValueAddress;
    state.r[1] =
        std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(target));
    state.r[2] = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(step));
    return state;
  };
  auto referenceState = buildState();
  RunReference(Oot3dNativeGame::kOot3dMathStepToSEntry, referenceState,
               referenceMemory);
  auto typedState = buildState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMathStepToSEntry, typedState, typedMemory,
             &result, {static_cast<float>(updateRate)}),
         "typed Math_StepToS retained AOT unexpectedly");
  Expect(ReadObject<std::uint16_t>(typedMemory, kValueAddress) ==
             ReadObject<std::uint16_t>(referenceMemory, kValueAddress),
         "typed/A32 Math_StepToS value mismatch");
  Expect(typedState.r[0] == referenceState.r[0],
         "typed/A32 Math_StepToS return mismatch");
}

void TestStepToSDifferential() {
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    TestStepToSCase(0, 1000, 100, updateRate);
    TestStepToSCase(1000, -500, 80, updateRate);
    TestStepToSCase(990, 1000, 100, updateRate);
    TestStepToSCase(100, 100, 0, updateRate);
    TestStepToSCase(100, 120, 0, updateRate);
    TestStepToSCase(32760, -32760, 100, updateRate);
  }
}

void TestStepToAngleSCase(std::int16_t current, std::int16_t target,
                          std::int16_t step, std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  referenceMemory.WriteFast(kValueAddress,
                            std::bit_cast<std::uint16_t>(current));
  typedMemory.WriteFast(kValueAddress, std::bit_cast<std::uint16_t>(current));
  const auto buildState = [&] {
    auto state = BuildState(Oot3dNativeGame::kOot3dMathStepToAngleSEntry);
    state.r[0] = kValueAddress;
    state.r[1] =
        std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(target));
    state.r[2] = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(step));
    return state;
  };
  auto referenceState = buildState();
  RunReference(Oot3dNativeGame::kOot3dMathStepToAngleSEntry, referenceState,
               referenceMemory);
  auto typedState = buildState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMathStepToAngleSEntry, typedState,
             typedMemory, &result, {static_cast<float>(updateRate)}),
         "typed Math_StepToAngleS retained AOT unexpectedly");
  Expect(ReadObject<std::uint16_t>(typedMemory, kValueAddress) ==
             ReadObject<std::uint16_t>(referenceMemory, kValueAddress),
         "typed/A32 Math_StepToAngleS value mismatch");
  Expect(typedState.r[0] == referenceState.r[0],
         "typed/A32 Math_StepToAngleS return mismatch");
}

void TestStepToAngleSDifferential() {
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    TestStepToAngleSCase(0, 1000, 100, updateRate);
    TestStepToAngleSCase(1000, -500, 80, updateRate);
    TestStepToAngleSCase(32760, -32760, 100, updateRate);
    TestStepToAngleSCase(-32760, 32760, 100, updateRate);
    TestStepToAngleSCase(100, 100, 0, updateRate);
    TestStepToAngleSCase(100, 120, 0, updateRate);
  }
}

void TestSmoothStepToFCase(float current, float target, float fraction,
                           float maximumStep, float minimumStep,
                           std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  WriteFloat(referenceMemory, kValueAddress, current);
  WriteFloat(typedMemory, kValueAddress, current);

  const auto buildSmoothState = [&] {
    auto state = BuildState(Oot3dNativeGame::kOot3dMathSmoothStepToFEntry);
    state.r[0] = kValueAddress;
    state.vfp[0] = std::bit_cast<std::uint32_t>(target);
    state.vfp[1] = std::bit_cast<std::uint32_t>(fraction);
    state.vfp[2] = std::bit_cast<std::uint32_t>(maximumStep);
    state.vfp[3] = std::bit_cast<std::uint32_t>(minimumStep);
    return state;
  };
  auto referenceState = buildSmoothState();
  RunReference(Oot3dNativeGame::kOot3dMathSmoothStepToFEntry, referenceState,
               referenceMemory);
  auto typedState = buildSmoothState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMathSmoothStepToFEntry, typedState,
             typedMemory, &result, {static_cast<float>(updateRate)}),
         "typed Math_SmoothStepToF retained AOT unexpectedly");
  Expect(ReadObject<std::uint32_t>(typedMemory, kValueAddress) ==
             ReadObject<std::uint32_t>(referenceMemory, kValueAddress),
         "typed/A32 Math_SmoothStepToF value mismatch");
  Expect(typedState.vfp[0] == referenceState.vfp[0],
         "typed/A32 Math_SmoothStepToF return mismatch");
}

void TestSmoothStepToFDifferential() {
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    TestSmoothStepToFCase(0.0f, 100.0f, 0.2f, 8.0f, 0.5f, updateRate);
    TestSmoothStepToFCase(100.0f, -20.0f, 0.15f, 6.0f, 0.25f, updateRate);
    TestSmoothStepToFCase(9.9f, 10.0f, 0.01f, 5.0f, 0.5f, updateRate);
    TestSmoothStepToFCase(10.0f, 10.0f, 0.3f, 5.0f, 0.5f, updateRate);
    TestSmoothStepToFCase(1.0f, 1.000005f, 0.3f, 5.0f, 0.0f, updateRate);
  }
}

void TestSmoothStepToSCase(std::int16_t current, std::int16_t target,
                           std::int32_t scale, std::int32_t maximumStep,
                           std::int32_t minimumStep, std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  referenceMemory.WriteFast(kValueAddress,
                            std::bit_cast<std::uint16_t>(current));
  typedMemory.WriteFast(kValueAddress, std::bit_cast<std::uint16_t>(current));

  const auto buildSmoothState = [&](NativeA32Memory &memory) {
    auto state = BuildState(Oot3dNativeGame::kOot3dMathSmoothStepToSEntry);
    state.r[0] = kValueAddress;
    state.r[1] =
        std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(target));
    state.r[2] = std::bit_cast<std::uint32_t>(scale);
    state.r[3] = std::bit_cast<std::uint32_t>(maximumStep);
    state.r[13] = kStackTop - 0x100U;
    WriteU32(memory, state.r[13], std::bit_cast<std::uint32_t>(minimumStep));
    return state;
  };
  auto referenceState = buildSmoothState(referenceMemory);
  RunReference(Oot3dNativeGame::kOot3dMathSmoothStepToSEntry, referenceState,
               referenceMemory);
  auto typedState = buildSmoothState(typedMemory);
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMathSmoothStepToSEntry, typedState,
             typedMemory, &result, {static_cast<float>(updateRate)}),
         "typed Math_SmoothStepToS retained AOT unexpectedly");
  Expect(ReadObject<std::uint16_t>(typedMemory, kValueAddress) ==
             ReadObject<std::uint16_t>(referenceMemory, kValueAddress),
         "typed/A32 Math_SmoothStepToS value mismatch");
  Expect(typedState.r[0] == referenceState.r[0],
         "typed/A32 Math_SmoothStepToS return mismatch");
}

void TestSmoothStepToSDifferential() {
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    TestSmoothStepToSCase(0, 1000, 10, 80, 4, updateRate);
    TestSmoothStepToSCase(1000, -500, 7, 90, 5, updateRate);
    TestSmoothStepToSCase(20, 25, 8, 40, 3, updateRate);
    TestSmoothStepToSCase(-20, -25, 8, 40, 3, updateRate);
    TestSmoothStepToSCase(100, 100, 4, 50, 2, updateRate);
    TestSmoothStepToSCase(32760, -32760, 4, 100, 2, updateRate);
  }
}

SkelAnime BuildSkelAnime(std::uint8_t updateMode) {
  SkelAnime skelAnime;
  skelAnime.AnimationIndex = 3;
  skelAnime.MorphWeight = 0.875f;
  skelAnime.MorphRate = 0.125f;
  skelAnime.CurrentFrame = 1.25f;
  skelAnime.PlaySpeed = 0.75f;
  skelAnime.StartFrame = 1.0f;
  skelAnime.EndFrame = 4.0f;
  skelAnime.AnimationLength = 5.0f;
  skelAnime.MorphTaper = 1;
  skelAnime.AnimationMode = 3U;
  skelAnime.UpdateMode = updateMode;
  skelAnime.LimbCount = 8U;
  skelAnime.SpecialLimb = 2U;
  skelAnime.FrameDataPath = 0U;
  skelAnime.JointMatrices.Address = 0x10004800U;
  skelAnime.MorphMatrices.Address = 0x10005000U;
  return skelAnime;
}

oot3d::recomp::a32::GuestState BuildSkelAnimeState() {
  auto state = BuildState(Oot3dNativeGame::kOot3dSkelAnimeUpdateEntry);
  state.r[1] = kPlayAddress;
  state.r[4] = 0x44444444U;
  state.r[5] = 0x55555555U;
  state.r[6] = 0x66666666U;
  state.vfp[16] = 0x10101010U;
  state.vfp[17] = 0x11111111U;
  state.vfp[18] = 0x12121212U;
  state.vfp[19] = 0x13131313U;
  return state;
}

void TestSkelAnimeSetUpdateCase(std::uint8_t animationMode) {
  auto referenceMemory = BuildMemory(2, 0U);
  auto typedMemory = referenceMemory;
  auto initial = BuildSkelAnime(0U);
  initial.AnimationMode = animationMode;
  initial.UpdateMode = 0xA5U;
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  auto referenceState =
      BuildState(Oot3dNativeGame::kOot3dSkelAnimeSetUpdateEntry);
  RunReference(Oot3dNativeGame::kOot3dSkelAnimeSetUpdateEntry, referenceState,
               referenceMemory);
  auto typedState = BuildState(Oot3dNativeGame::kOot3dSkelAnimeSetUpdateEntry);
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dSkelAnimeSetUpdateEntry, typedState,
             typedMemory, &result, {2.0f}),
         "typed SkelAnime_SetUpdate retained AOT unexpectedly");
  const auto typed = ReadObject<SkelAnime>(typedMemory, kActorAddress);
  const auto reference = ReadObject<SkelAnime>(referenceMemory, kActorAddress);
  Expect(std::memcmp(&typed, &reference, sizeof(SkelAnime)) == 0,
         "typed/A32 SkelAnime_SetUpdate mismatch");
}

void TestSkelAnimeSetUpdateDifferential() {
  for (const std::uint8_t mode :
       {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3},
        std::uint8_t{4}, std::uint8_t{0xFF}}) {
    TestSkelAnimeSetUpdateCase(mode);
  }
}

void TestAnimationResourceFunctions() {
  constexpr std::uint32_t animationIndex = 2U;
  auto memory = BuildMemory(2, 0U);
  ConfigureAnimationResource(memory, animationIndex, 11);
  auto skelAnime = BuildSkelAnime(0U);
  skelAnime.ZarArchive.Address = kZarAddress;
  WriteObject(memory, kActorAddress, skelAnime);

  const auto testFunction = [&](std::uint32_t entry, std::uint32_t argument0,
                                std::uint32_t argument1) {
    auto referenceMemory = memory;
    auto typedMemory = memory;
    auto referenceState = BuildState(entry);
    referenceState.r[0] = argument0;
    referenceState.r[1] = argument1;
    RunReference(entry, referenceState, referenceMemory);
    auto typedState = BuildState(entry);
    typedState.r[0] = argument0;
    typedState.r[1] = argument1;
    oot3d::recomp::a32::ExecutionResult result;
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               entry, typedState, typedMemory, &result, {2.0f}),
           "typed animation resource function retained AOT unexpectedly");
    Expect(typedState.r[0] == referenceState.r[0],
           "typed/A32 animation resource result mismatch");
  };

  testFunction(Oot3dNativeGame::kOot3dZarGetCsabByIndexEntry, kZarAddress,
               animationIndex);
  testFunction(Oot3dNativeGame::kOot3dZarGetCsabByIndexEntry, kZarAddress,
               animationIndex + 1U);
  testFunction(Oot3dNativeGame::kOot3dAnimationGetLengthEntry, kActorAddress,
               animationIndex);
  testFunction(Oot3dNativeGame::kOot3dAnimationGetLengthEntry, kActorAddress,
               animationIndex + 1U);
}

oot3d::recomp::a32::GuestState BuildAnimationChangeGuestState(
    std::uint32_t animationIndex, std::uint8_t animationMode,
    std::int8_t morphTaper, float playSpeed, float startFrame, float endFrame,
    float morphFrames) {
  auto state = BuildSkelAnimeState();
  state.r[15] = Oot3dNativeGame::kOot3dAnimationChangeEntry;
  state.r[1] = animationIndex;
  state.r[2] = animationMode;
  state.r[3] = static_cast<std::uint8_t>(morphTaper);
  state.r[7] = 0x77777777U;
  state.r[8] = 0x88888888U;
  state.vfp[0] = std::bit_cast<std::uint32_t>(playSpeed);
  state.vfp[1] = std::bit_cast<std::uint32_t>(startFrame);
  state.vfp[2] = std::bit_cast<std::uint32_t>(endFrame);
  state.vfp[3] = std::bit_cast<std::uint32_t>(morphFrames);
  state.vfp[20] = 0x20202020U;
  state.vfp[21] = 0x21212121U;
  return state;
}

void RunAnimationChangeReference(oot3d::recomp::a32::GuestState &state,
                                 NativeA32Memory &memory,
                                 AnimationChangeEffectTrace &trace) {
  const auto result = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(),
      Oot3dNativeGame::kOot3dAnimationChangeEntry, state, memory, nullptr,
      nullptr, 40'000U, nullptr, nullptr, nullptr, 0U,
      &StubAnimationChangeEffect, &trace, kAnimationChangeEffectEntries.data(),
      kAnimationChangeEffectEntries.size());
  Expect(result.pc == kReturnSentinel &&
             result.kind == oot3d::recomp::a32::ExitKind::MissingBlock,
         "A32 Animation_Change did not return through LR");
}

void RunTypedAnimationChange(oot3d::recomp::a32::GuestState &state,
                             NativeA32Memory &memory,
                             AnimationChangeEffectTrace &trace) {
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dAnimationChangeEntry, state, memory,
             &result, {2.0f}, &blocksConsumed),
         "typed Animation_Change retained AOT unexpectedly");
  Expect(blocksConsumed == 1U,
         "typed Animation_Change reported an invalid block count");
  const auto tailResult = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), result.pc, state, memory,
      nullptr, nullptr, 40'000U, nullptr, nullptr, nullptr, 0U,
      &StubAnimationChangeEffect, &trace, kAnimationChangeEffectEntries.data(),
      kAnimationChangeEffectEntries.size());
  Expect(tailResult.pc == kReturnSentinel &&
             tailResult.kind == oot3d::recomp::a32::ExitKind::MissingBlock,
         "typed Animation_Change effect did not return through LR");
}

void ExpectAnimationChangeAbiEqual(
    const oot3d::recomp::a32::GuestState &actual,
    const oot3d::recomp::a32::GuestState &expected) {
  for (std::size_t index = 4U; index <= 8U; ++index) {
    Expect(actual.r[index] == expected.r[index],
           "Animation_Change callee-preserved register mismatch");
  }
  Expect(actual.r[13] == expected.r[13], "Animation_Change stack mismatch");
  for (std::size_t index = 16U; index <= 21U; ++index) {
    Expect(actual.vfp[index] == expected.vfp[index],
           "Animation_Change callee-preserved VFP register mismatch");
  }
}

void TestAnimationChangeCase(SkelAnime initial, std::uint32_t animationIndex,
                             std::uint8_t animationMode, std::int8_t morphTaper,
                             float playSpeed, float startFrame, float endFrame,
                             float morphFrames) {
  auto referenceMemory = BuildMemory(2, 0U);
  ConfigureAnimationResource(referenceMemory, animationIndex, 11);
  auto typedMemory = referenceMemory;
  initial.ZarArchive.Address = kZarAddress;
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  auto referenceState = BuildAnimationChangeGuestState(
      animationIndex, animationMode, morphTaper, playSpeed, startFrame,
      endFrame, morphFrames);
  AnimationChangeEffectTrace referenceTrace;
  RunAnimationChangeReference(referenceState, referenceMemory, referenceTrace);
  auto typedState = BuildAnimationChangeGuestState(
      animationIndex, animationMode, morphTaper, playSpeed, startFrame,
      endFrame, morphFrames);
  AnimationChangeEffectTrace typedTrace;
  RunTypedAnimationChange(typedState, typedMemory, typedTrace);

  const auto reference = ReadObject<SkelAnime>(referenceMemory, kActorAddress);
  const auto typed = ReadObject<SkelAnime>(typedMemory, kActorAddress);
  Expect(std::memcmp(&typed, &reference, sizeof(SkelAnime)) == 0,
         "typed/A32 Animation_Change state mismatch");
  Expect(typedTrace.Calls == referenceTrace.Calls,
         "typed/A32 Animation_Change pose effect mismatch");
  ExpectAnimationChangeAbiEqual(typedState, referenceState);
}

void TestAnimationChangeDifferential() {
  constexpr std::uint32_t animationIndex = 2U;
  auto initial = BuildSkelAnime(4U);
  TestAnimationChangeCase(initial, animationIndex, 0U, 0, 1.25f, 2.0f, 8.0f,
                          0.0f);

  auto samePose = initial;
  samePose.AnimationIndex = static_cast<std::int32_t>(animationIndex);
  samePose.CurrentFrame = 2.0f;
  TestAnimationChangeCase(samePose, animationIndex, 2U, 0, 0.75f, 2.0f, 8.0f,
                          4.0f);
  TestAnimationChangeCase(initial, animationIndex, 2U, 0, 0.75f, 2.0f, 8.0f,
                          4.0f);
  TestAnimationChangeCase(initial, animationIndex, 4U, -1, 0.5f, 3.0f, 9.0f,
                          5.0f);
  TestAnimationChangeCase(initial, animationIndex, 0U, 0, 1.0f, 1.0f, 7.0f,
                          -6.0f);
  TestAnimationChangeCase(initial, animationIndex, 3U, 0, -0.5f, 7.0f, 1.0f,
                          -3.0f);
}

void TestLinkAnimationPlayCase(std::uint32_t entry,
                               std::uint32_t configuredAnimationIndex,
                               std::uint32_t requestedAnimationIndex,
                               float playSpeed) {
  auto referenceMemory = BuildMemory(2, 0U);
  ConfigureAnimationResource(referenceMemory, configuredAnimationIndex, 11);
  auto initial = BuildSkelAnime(4U);
  initial.ZarArchive.Address = kZarAddress;
  WriteObject(referenceMemory, kActorAddress, initial);
  auto typedMemory = referenceMemory;

  const auto buildPlayState = [&] {
    auto state = BuildSkelAnimeState();
    state.r[15] = entry;
    state.r[2] = requestedAnimationIndex;
    state.r[4] = 0x44444444U;
    state.r[5] = 0x55555555U;
    state.r[6] = 0x66666666U;
    state.vfp[0] = std::bit_cast<std::uint32_t>(playSpeed);
    state.vfp[16] = 0x16161616U;
    return state;
  };

  auto referenceState = buildPlayState();
  LinkAnimationBoundaryCapture capture;
  constexpr std::array linkChangeEntry{
      Oot3dNativeGame::kOot3dLinkAnimationChangeEntry};
  const auto referenceResult = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), entry, referenceState,
      referenceMemory, nullptr, nullptr, 40'000U, nullptr, nullptr, nullptr, 0U,
      &CaptureLinkAnimationChange, &capture, linkChangeEntry.data(),
      linkChangeEntry.size());
  Expect(capture.Called && referenceResult.pc == kReturnSentinel &&
             referenceResult.kind == oot3d::recomp::a32::ExitKind::MissingBlock,
         "A32 LinkAnimation play wrapper did not reach LinkAnimation_Change");

  auto typedState = buildPlayState();
  oot3d::recomp::a32::ExecutionResult typedResult;
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(entry, typedState,
                                                    typedMemory, &typedResult,
                                                    {2.0f}, &blocksConsumed),
         "typed LinkAnimation play wrapper retained AOT unexpectedly");
  Expect(typedResult.pc == Oot3dNativeGame::kOot3dLinkAnimationChangeEntry &&
             typedResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
             blocksConsumed == 1U,
         "typed LinkAnimation play wrapper returned an invalid tail");
  for (std::size_t index = 0U; index < 4U; ++index) {
    Expect(typedState.r[index] == capture.State.r[index],
           "typed/A32 LinkAnimation play argument mismatch");
    Expect(typedState.vfp[index] == capture.State.vfp[index],
           "typed/A32 LinkAnimation play VFP argument mismatch");
  }
  for (std::size_t index = 4U; index <= 6U; ++index) {
    Expect(typedState.r[index] == capture.State.r[index],
           "LinkAnimation play callee-preserved register mismatch");
  }
  Expect(typedState.r[13] == capture.State.r[13] &&
             typedState.r[14] == capture.State.r[14] &&
             typedState.vfp[16] == capture.State.vfp[16],
         "LinkAnimation play ABI mismatch");
}

void TestLinkAnimationPlayDifferential() {
  constexpr std::uint32_t animationIndex = 2U;
  TestLinkAnimationPlayCase(
      Oot3dNativeGame::kOot3dLinkAnimationPlayOnceWithSpeedEntry,
      animationIndex, animationIndex, 1.25f);
  TestLinkAnimationPlayCase(
      Oot3dNativeGame::kOot3dLinkAnimationPlayLoopSetSpeedEntry, animationIndex,
      animationIndex, 0.75f);
  TestLinkAnimationPlayCase(Oot3dNativeGame::kOot3dLinkAnimationPlayOnceEntry,
                            animationIndex, animationIndex, 9.0f);
  TestLinkAnimationPlayCase(Oot3dNativeGame::kOot3dLinkAnimationPlayLoopEntry,
                            animationIndex, animationIndex, 9.0f);
  TestLinkAnimationPlayCase(Oot3dNativeGame::kOot3dLinkAnimationPlayLoopEntry,
                            0U, animationIndex + 8U, 9.0f);
}

void TestLinkAnimationOnFrameCase(float currentFrame, float playSpeed,
                                  float animationLength, float frame,
                                  std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  auto initial = BuildSkelAnime(4U);
  initial.CurrentFrame = currentFrame;
  initial.PlaySpeed = playSpeed;
  initial.AnimationLength = animationLength;
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  const auto buildOnFrameState = [&] {
    auto state = BuildState(Oot3dNativeGame::kOot3dLinkAnimationOnFrameEntry);
    state.vfp[0] = std::bit_cast<std::uint32_t>(frame);
    return state;
  };
  auto referenceState = buildOnFrameState();
  RunReference(Oot3dNativeGame::kOot3dLinkAnimationOnFrameEntry, referenceState,
               referenceMemory);
  auto typedState = buildOnFrameState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dLinkAnimationOnFrameEntry, typedState,
             typedMemory, &result, {static_cast<float>(updateRate)}),
         "typed LinkAnimation_OnFrame retained AOT unexpectedly");
  Expect(typedState.r[0] == referenceState.r[0],
         "typed/A32 LinkAnimation_OnFrame result mismatch");
  const auto typed = ReadObject<SkelAnime>(typedMemory, kActorAddress);
  const auto reference = ReadObject<SkelAnime>(referenceMemory, kActorAddress);
  Expect(std::memcmp(&typed, &reference, sizeof(SkelAnime)) == 0,
         "LinkAnimation_OnFrame modified SkelAnime state");
}

void TestLinkAnimationOnFrameDifferential() {
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    TestLinkAnimationOnFrameCase(4.0f, 1.0f, 10.0f, 4.0f, updateRate);
    TestLinkAnimationOnFrameCase(4.0f, 1.0f, 10.0f, 3.5f, updateRate);
    TestLinkAnimationOnFrameCase(4.0f, 1.0f, 10.0f, 3.0f, updateRate);
    TestLinkAnimationOnFrameCase(4.0f, 1.0f, 10.0f, 4.5f, updateRate);
    TestLinkAnimationOnFrameCase(0.0f, 1.0f, 10.0f, 0.0f, updateRate);
    TestLinkAnimationOnFrameCase(5.0f, -1.0f, 10.0f, 5.0f, updateRate);
    TestLinkAnimationOnFrameCase(5.0f, -1.0f, 10.0f, 5.5f, updateRate);
    TestLinkAnimationOnFrameCase(5.0f, 0.0f, 10.0f, 5.0f, updateRate);
  }
}

void TestAnimationFrameCrossingCase(std::uint32_t entry, float currentFrame,
                                    float playSpeed, float animationLength,
                                    float frame, float updateScale) {
  auto referenceMemory = BuildMemory(2, 0U);
  auto typedMemory = referenceMemory;
  auto initial = BuildSkelAnime(4U);
  initial.CurrentFrame = currentFrame;
  initial.PlaySpeed = playSpeed;
  initial.AnimationLength = animationLength;
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  const auto buildState = [&] {
    auto state = BuildState(entry);
    state.vfp[0] = std::bit_cast<std::uint32_t>(frame);
    state.vfp[1] = std::bit_cast<std::uint32_t>(updateScale);
    return state;
  };
  auto referenceState = buildState();
  RunReference(entry, referenceState, referenceMemory);
  auto typedState = buildState();
  oot3d::recomp::a32::ExecutionResult result;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             entry, typedState, typedMemory, &result, {2.0f}),
         "typed animation frame crossing retained AOT unexpectedly");
  Expect(typedState.r[0] == referenceState.r[0],
         "typed/A32 animation frame crossing result mismatch");
  const auto typed = ReadObject<SkelAnime>(typedMemory, kActorAddress);
  const auto reference = ReadObject<SkelAnime>(referenceMemory, kActorAddress);
  Expect(std::memcmp(&typed, &reference, sizeof(SkelAnime)) == 0,
         "animation frame crossing modified SkelAnime state");
}

void TestAnimationFrameCrossingDifferential() {
  constexpr std::array entries{
      Oot3dNativeGame::kOot3dSkelAnimeIsFrameCrossedEntry,
      Oot3dNativeGame::kOot3dAnimationOnFrameImplEntry,
  };
  for (const std::uint32_t entry : entries) {
    TestAnimationFrameCrossingCase(entry, 4.0f, 1.0f, 10.0f, 4.0f, 1.0f);
    TestAnimationFrameCrossingCase(entry, 4.0f, 1.0f, 10.0f, 3.0f, 0.5f);
    TestAnimationFrameCrossingCase(entry, 0.25f, 1.0f, 10.0f, 0.0f, 0.5f);
    TestAnimationFrameCrossingCase(entry, 9.75f, 1.0f, 10.0f, 0.0f, 0.25f);
    TestAnimationFrameCrossingCase(entry, 4.0f, -1.0f, 10.0f, 4.25f, 0.25f);
  }
}

oot3d::recomp::a32::GuestState BuildLinkAnimationChangeGuestState(
    std::uint32_t animationIndex, std::uint8_t animationMode, float playSpeed,
    float startFrame, float endFrame, float morphFrames) {
  auto state = BuildSkelAnimeState();
  state.r[15] = Oot3dNativeGame::kOot3dLinkAnimationChangeEntry;
  state.r[2] = animationIndex;
  state.r[3] = animationMode;
  for (std::size_t index = 4U; index <= 10U; ++index) {
    state.r[index] = 0x40404040U + static_cast<std::uint32_t>(index);
  }
  state.vfp[0] = std::bit_cast<std::uint32_t>(playSpeed);
  state.vfp[1] = std::bit_cast<std::uint32_t>(startFrame);
  state.vfp[2] = std::bit_cast<std::uint32_t>(endFrame);
  state.vfp[3] = std::bit_cast<std::uint32_t>(morphFrames);
  for (std::size_t index = 16U; index <= 21U; ++index) {
    state.vfp[index] = 0x50505050U + static_cast<std::uint32_t>(index);
  }
  return state;
}

void RunLinkAnimationChangeReference(oot3d::recomp::a32::GuestState &state,
                                     NativeA32Memory &memory,
                                     LinkAnimationEffectTrace &trace) {
  const auto result = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(),
      Oot3dNativeGame::kOot3dLinkAnimationChangeEntry, state, memory, nullptr,
      nullptr, 40'000U, nullptr, nullptr, nullptr, 0U, &StubLinkAnimationEffect,
      &trace, kLinkAnimationChangeEffectEntries.data(),
      kLinkAnimationChangeEffectEntries.size());
  Expect(result.pc == kReturnSentinel &&
             result.kind == oot3d::recomp::a32::ExitKind::MissingBlock,
         "A32 LinkAnimation_Change did not return through LR");
}

void RunTypedLinkAnimationChange(oot3d::recomp::a32::GuestState &state,
                                 NativeA32Memory &memory,
                                 LinkAnimationEffectTrace &trace) {
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dLinkAnimationChangeEntry, state, memory,
             &result, {2.0f}, &blocksConsumed),
         "typed LinkAnimation_Change retained AOT unexpectedly");
  Expect(blocksConsumed == 1U,
         "typed LinkAnimation_Change reported an invalid block count");
  const auto tailResult = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), result.pc, state, memory,
      nullptr, nullptr, 40'000U, nullptr, nullptr, nullptr, 0U,
      &StubLinkAnimationEffect, &trace,
      kLinkAnimationChangeEffectEntries.data(),
      kLinkAnimationChangeEffectEntries.size());
  if (tailResult.pc != kReturnSentinel ||
      tailResult.kind != oot3d::recomp::a32::ExitKind::MissingBlock) {
    throw std::runtime_error(
        "typed LinkAnimation_Change effect stopped at pc=" +
        std::to_string(tailResult.pc) +
        " kind=" + std::to_string(static_cast<unsigned>(tailResult.kind)));
  }
}

std::vector<std::uint8_t> ReadBytes(const NativeA32Memory &memory,
                                    std::uint32_t address, std::size_t size) {
  std::vector<std::uint8_t> bytes(size);
  Expect(memory.ReadBytes(address, bytes), "could not read test byte range");
  return bytes;
}

void ExpectLinkAnimationChangeAbiEqual(
    const oot3d::recomp::a32::GuestState &actual,
    const oot3d::recomp::a32::GuestState &expected) {
  for (std::size_t index = 4U; index <= 10U; ++index) {
    Expect(actual.r[index] == expected.r[index],
           "LinkAnimation_Change callee-preserved register mismatch");
  }
  Expect(actual.r[13] == expected.r[13], "LinkAnimation_Change stack mismatch");
  for (std::size_t index = 16U; index <= 21U; ++index) {
    Expect(actual.vfp[index] == expected.vfp[index],
           "LinkAnimation_Change callee-preserved VFP register mismatch");
  }
}

void TestLinkAnimationChangeCase(SkelAnime initial,
                                 std::uint32_t configuredAnimationIndex,
                                 std::uint32_t requestedAnimationIndex,
                                 std::uint8_t animationMode, float playSpeed,
                                 float startFrame, float endFrame,
                                 float morphFrames) {
  auto referenceMemory = BuildMemory(2, 0U);
  ConfigureAnimationResource(referenceMemory, configuredAnimationIndex, 11);
  initial.ZarArchive.Address = kZarAddress;
  constexpr std::size_t poseBytes = 8U * 0x34U;
  std::array<std::uint8_t, poseBytes> jointPose{};
  std::array<std::uint8_t, poseBytes> morphPose{};
  for (std::size_t index = 0U; index < poseBytes; ++index) {
    jointPose[index] = static_cast<std::uint8_t>((index * 17U + 3U) & 0xFFU);
    morphPose[index] = static_cast<std::uint8_t>((index * 7U + 0xA5U) & 0xFFU);
  }
  Expect(referenceMemory.WriteBytes(initial.JointMatrices.Address, jointPose),
         "could not initialize LinkAnimation joint pose");
  Expect(referenceMemory.WriteBytes(initial.MorphMatrices.Address, morphPose),
         "could not initialize LinkAnimation morph pose");
  WriteObject(referenceMemory, kActorAddress, initial);
  auto typedMemory = referenceMemory;

  auto referenceState = BuildLinkAnimationChangeGuestState(
      requestedAnimationIndex, animationMode, playSpeed, startFrame, endFrame,
      morphFrames);
  LinkAnimationEffectTrace referenceTrace;
  RunLinkAnimationChangeReference(referenceState, referenceMemory,
                                  referenceTrace);
  auto typedState = BuildLinkAnimationChangeGuestState(
      requestedAnimationIndex, animationMode, playSpeed, startFrame, endFrame,
      morphFrames);
  LinkAnimationEffectTrace typedTrace;
  RunTypedLinkAnimationChange(typedState, typedMemory, typedTrace);

  const auto reference = ReadObject<SkelAnime>(referenceMemory, kActorAddress);
  const auto typed = ReadObject<SkelAnime>(typedMemory, kActorAddress);
  Expect(std::memcmp(&typed, &reference, sizeof(SkelAnime)) == 0,
         "typed/A32 LinkAnimation_Change state mismatch");
  Expect(typedTrace.Calls.size() == referenceTrace.Calls.size(),
         "typed/A32 LinkAnimation_Change pose effect count mismatch");
  for (std::size_t index = 0U; index < typedTrace.Calls.size(); ++index) {
    const auto &actual = typedTrace.Calls[index];
    const auto &expected = referenceTrace.Calls[index];
    if (!(actual == expected)) {
      throw std::runtime_error(
          "typed/A32 LinkAnimation_Change pose effect mismatch at call " +
          std::to_string(index) + ": pc=" + std::to_string(actual.Pc) + "/" +
          std::to_string(expected.Pc) +
          " r0=" + std::to_string(actual.Arguments[0]) + "/" +
          std::to_string(expected.Arguments[0]) +
          " r1=" + std::to_string(actual.Arguments[1]) + "/" +
          std::to_string(expected.Arguments[1]) +
          " r2=" + std::to_string(actual.Arguments[2]) + "/" +
          std::to_string(expected.Arguments[2]) +
          " r3=" + std::to_string(actual.Arguments[3]) + "/" +
          std::to_string(expected.Arguments[3]) + " s0=" +
          std::to_string(actual.S0) + "/" + std::to_string(expected.S0) +
          " stack0=" + std::to_string(actual.StackArguments[0]) + "/" +
          std::to_string(expected.StackArguments[0]));
    }
  }
  Expect(
      ReadBytes(typedMemory, initial.JointMatrices.Address, poseBytes) ==
          ReadBytes(referenceMemory, initial.JointMatrices.Address, poseBytes),
      "typed/A32 LinkAnimation_Change joint pose mismatch");
  Expect(
      ReadBytes(typedMemory, initial.MorphMatrices.Address, poseBytes) ==
          ReadBytes(referenceMemory, initial.MorphMatrices.Address, poseBytes),
      "typed/A32 LinkAnimation_Change morph pose mismatch");
  ExpectLinkAnimationChangeAbiEqual(typedState, referenceState);
}

void TestLinkAnimationChangeDifferential() {
  constexpr std::uint32_t animationIndex = 2U;
  auto initial = BuildSkelAnime(4U);
  TestLinkAnimationChangeCase(initial, animationIndex, animationIndex, 0U,
                              1.25f, 2.0f, 8.0f, 0.0f);
  TestLinkAnimationChangeCase(initial, animationIndex, animationIndex, 2U,
                              0.75f, 2.0f, 8.0f, 4.0f);
  TestLinkAnimationChangeCase(initial, animationIndex, animationIndex, 0U, 1.0f,
                              1.0f, 7.0f, -6.0f);
  TestLinkAnimationChangeCase(initial, animationIndex, animationIndex, 3U,
                              -0.5f, 7.0f, 1.0f, -3.0f);

  auto samePose = initial;
  samePose.AnimationIndex = static_cast<std::int32_t>(animationIndex);
  samePose.CurrentFrame = 2.0f;
  TestLinkAnimationChangeCase(samePose, animationIndex, animationIndex, 2U,
                              0.75f, 2.0f, 8.0f, 4.0f);
  TestLinkAnimationChangeCase(initial, 0U, animationIndex + 8U, 0U, 1.0f, 0.0f,
                              10.0f, 3.0f);
}

void RunTypedSkelAnime(oot3d::recomp::a32::GuestState &state,
                       NativeA32Memory &memory, float nativeUpdateRate) {
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dSkelAnimeUpdateEntry, state, memory,
             &result, {nativeUpdateRate}, &blocksConsumed),
         "typed SkelAnime_Update retained AOT unexpectedly");
  Expect(blocksConsumed == 1U,
         "typed SkelAnime_Update reported an invalid block count");
  if (result.pc == kReturnSentinel) {
    return;
  }
  const auto tailResult = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), result.pc, state, memory,
      nullptr, nullptr, 40'000U, nullptr, nullptr, nullptr, 0U,
      &StubSkelAnimeEffect, nullptr, kSkelAnimeEffectEntries.data(),
      kSkelAnimeEffectEntries.size());
  Expect(tailResult.pc == kReturnSentinel &&
             tailResult.kind == oot3d::recomp::a32::ExitKind::MissingBlock,
         "typed SkelAnime tail did not return through LR");
}

void ExpectSkelAnimeAbiEqual(const oot3d::recomp::a32::GuestState &actual,
                             const oot3d::recomp::a32::GuestState &expected,
                             bool compareReturnValue = true) {
  if (compareReturnValue) {
    Expect(actual.r[0] == expected.r[0], "SkelAnime return value mismatch");
  }
  Expect(actual.r[4] == expected.r[4] && actual.r[5] == expected.r[5] &&
             actual.r[6] == expected.r[6] && actual.r[13] == expected.r[13],
         "SkelAnime callee-preserved register mismatch");
  for (std::size_t index = 16U; index <= 19U; ++index) {
    Expect(actual.vfp[index] == expected.vfp[index],
           "SkelAnime callee-preserved VFP register mismatch");
  }
}

void TestSkelAnimeCase(SkelAnime initial, std::int16_t updateRate,
                       std::uint32_t secondArgument = kPlayAddress) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  auto referenceState = BuildSkelAnimeState();
  referenceState.r[1] = secondArgument;
  RunSkelAnimeReference(Oot3dNativeGame::kOot3dSkelAnimeUpdateEntry,
                        referenceState, referenceMemory);
  auto typedState = BuildSkelAnimeState();
  typedState.r[1] = secondArgument;
  RunTypedSkelAnime(typedState, typedMemory, static_cast<float>(updateRate));

  const auto reference = ReadObject<SkelAnime>(referenceMemory, kActorAddress);
  const auto typed = ReadObject<SkelAnime>(typedMemory, kActorAddress);
  Expect(std::memcmp(&typed, &reference, sizeof(SkelAnime)) == 0,
         "typed/A32 SkelAnime state mismatch for mode " +
             std::to_string(initial.UpdateMode));
  ExpectSkelAnimeAbiEqual(typedState, referenceState);
}

void TestSkelAnimeDifferential() {
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    for (std::uint8_t mode = 0U; mode <= 8U; ++mode) {
      auto skelAnime = BuildSkelAnime(mode);
      if (mode == 5U) {
        skelAnime.CurrentFrame = 3.75f;
      }
      TestSkelAnimeCase(skelAnime, updateRate);
    }

    for (const std::uint8_t mode : {std::uint8_t{2}, std::uint8_t{6}}) {
      auto terminal = BuildSkelAnime(mode);
      terminal.CurrentFrame = terminal.EndFrame;
      TestSkelAnimeCase(terminal, updateRate);
      if (mode == 6U) {
        TestSkelAnimeCase(terminal, updateRate, 0xDEADBEEFU);
      }
    }

    auto reverse = BuildSkelAnime(6U);
    reverse.CurrentFrame = 1.0f;
    reverse.EndFrame = 0.0f;
    reverse.PlaySpeed = -1.0f;
    TestSkelAnimeCase(reverse, updateRate);

    auto cosineTaper = BuildSkelAnime(8U);
    cosineTaper.MorphTaper = -1;
    cosineTaper.FrameDataPath = 1U;
    TestSkelAnimeCase(cosineTaper, updateRate);
  }
}

void RunSkelAnimeHelper(std::uint32_t entry,
                        oot3d::recomp::a32::GuestState &state,
                        NativeA32Memory &memory, float nativeUpdateRate,
                        bool typedBoundary) {
  bool executeHelperBodies = true;
  TypedBoundaryContext typedContext{&memory, nativeUpdateRate};
  const auto result = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), entry, state, memory, nullptr,
      nullptr, 40'000U, nullptr, nullptr, nullptr, 0U, &StubSkelAnimeEffect,
      &executeHelperBodies, kSkelAnimeEffectEntries.data(),
      kSkelAnimeEffectEntries.size(),
      typedBoundary ? &ExecuteTypedBoundary : nullptr,
      typedBoundary ? static_cast<void *>(&typedContext) : nullptr);
  if (result.pc != kReturnSentinel ||
      result.kind != oot3d::recomp::a32::ExitKind::MissingBlock) {
    throw std::runtime_error(
        "SkelAnime helper stopped at pc=" + std::to_string(result.pc) +
        " kind=" + std::to_string(static_cast<unsigned>(result.kind)));
  }
}

void TestSkelAnimeHelperCase(std::uint32_t entry, SkelAnime initial,
                             std::int16_t updateRate) {
  auto referenceMemory = BuildMemory(updateRate, 0U);
  auto typedMemory = referenceMemory;
  WriteObject(referenceMemory, kActorAddress, initial);
  WriteObject(typedMemory, kActorAddress, initial);

  auto referenceState = BuildSkelAnimeState();
  referenceState.r[15] = entry;
  RunSkelAnimeHelper(entry, referenceState, referenceMemory,
                     static_cast<float>(updateRate), false);
  auto typedState = BuildSkelAnimeState();
  typedState.r[15] = entry;
  RunSkelAnimeHelper(entry, typedState, typedMemory,
                     static_cast<float>(updateRate), true);

  const auto reference = ReadObject<SkelAnime>(referenceMemory, kActorAddress);
  const auto typed = ReadObject<SkelAnime>(typedMemory, kActorAddress);
  Expect(std::memcmp(&typed, &reference, sizeof(SkelAnime)) == 0,
         "typed/A32 SkelAnime helper state mismatch at " +
             std::to_string(entry));
  // These helpers return void; the replaced timing instructions own r0.
  ExpectSkelAnimeAbiEqual(typedState, referenceState, false);
}

void TestSkelAnimeHelperDifferential() {
  constexpr std::array helperEntries{0x002BB1CCU, 0x002BB34CU};
  for (const std::int16_t updateRate : {std::int16_t{2}, std::int16_t{1}}) {
    for (const std::uint32_t entry : helperEntries) {
      auto active = BuildSkelAnime(4U);
      active.AnimationMode = 2U;
      TestSkelAnimeHelperCase(entry, active, updateRate);

      auto clamped = active;
      clamped.MorphWeight = 0.01f;
      TestSkelAnimeHelperCase(entry, clamped, updateRate);

      auto inactive = active;
      inactive.MorphWeight = 0.0f;
      TestSkelAnimeHelperCase(entry, inactive, updateRate);
    }
  }
}

void TestSkelAnimeHelperFractionalRate() {
  constexpr std::array helperEntries{0x002BB1CCU, 0x002BB34CU};
  for (const std::uint32_t entry : helperEntries) {
    auto initial = BuildSkelAnime(4U);
    initial.AnimationMode = 2U;

    auto sixtyMemory = BuildMemory(1, 0U);
    WriteObject(sixtyMemory, kActorAddress, initial);
    auto sixtyState = BuildSkelAnimeState();
    sixtyState.r[15] = entry;
    RunSkelAnimeHelper(entry, sixtyState, sixtyMemory, 1.0f, true);

    auto oneTwentyMemory = BuildMemory(1, 0U);
    WriteObject(oneTwentyMemory, kActorAddress, initial);
    for (int update = 0; update < 2; ++update) {
      auto oneTwentyState = BuildSkelAnimeState();
      oneTwentyState.r[15] = entry;
      RunSkelAnimeHelper(entry, oneTwentyState, oneTwentyMemory, 0.5f, true);
    }

    const auto sixty = ReadObject<SkelAnime>(sixtyMemory, kActorAddress);
    const auto oneTwenty =
        ReadObject<SkelAnime>(oneTwentyMemory, kActorAddress);
    Expect(std::abs(sixty.MorphWeight - oneTwenty.MorphWeight) < 1.0e-6f,
           "two 120 Hz morph updates differ from one 60 Hz update");
  }
}

void TestSkelAnimeFractionalRate() {
  const oot3d::gameplay::SkelAnimeUpdateConstants constants{
      .LegacyUpdateScale = 0.5f,
      .DirectUpdateScale = 1.0f / 3.0f,
      .TaperAngleScale = 16384.0f,
      .TaperUpdateScale = 1.0f / 3.0f,
      .TaperZero = 0.0f,
      .TaperOne = 1.0f,
      .Zero = 0.0f,
      .One = 1.0f,
  };
  oot3d::gameplay::SkelAnimePlaybackState atSixty{
      .MorphWeight = 0.875f,
      .MorphRate = 0.125f,
      .CurrentFrame = 1.25f,
      .PlaySpeed = 0.75f,
      .StartFrame = 1.0f,
      .EndFrame = 4.0f,
      .AnimationLength = 5.0f,
      .AnimationMode = 3U,
      .UpdateMode = 4U,
  };
  auto atOneTwenty = atSixty;
  const auto sixtyPlan =
      oot3d::gameplay::AdvanceSkelAnimePlayback(atSixty, 1.0f, constants);
  const auto firstHalf =
      oot3d::gameplay::AdvanceSkelAnimePlayback(atOneTwenty, 0.5f, constants);
  const auto secondHalf =
      oot3d::gameplay::AdvanceSkelAnimePlayback(atOneTwenty, 0.5f, constants);
  Expect(sixtyPlan.Supported && firstHalf.Supported && secondHalf.Supported,
         "fractional SkelAnime rate was rejected");
  Expect(std::abs(atSixty.CurrentFrame - atOneTwenty.CurrentFrame) < 1.0e-6f,
         "two 120 Hz SkelAnime updates differ from one 60 Hz update");
}

void TestFractionalTimeContract() {
  const auto step = oot3d::gameplay::ResolveTimeStep(120U);
  Expect(std::abs(step.Seconds - 1.0 / 120.0) < 1.0e-12,
         "typed 120 Hz seconds mismatch");
  Expect(std::abs(step.NativeUpdateRate - 0.5f) < 1.0e-7f,
         "typed 120 Hz update-rate mismatch");
}

oot3d::gameplay::TimeContext CutsceneTime(double previous, double current) {
  oot3d::gameplay::TimeContext time;
  time.DeltaSeconds = (current - previous) / 30.0;
  time.NativeUpdateRate =
      static_cast<float>((current - previous) * 2.0);
  time.PreviousLogicalFrame = previous;
  time.CurrentLogicalFrame = current;
  time.LogicalFrameIndex =
      static_cast<std::uint64_t>(std::floor(current + 1.0e-9));
  time.CrossedLogicalFrame =
      time.LogicalFrameIndex >
      static_cast<std::uint64_t>(std::floor(previous + 1.0e-9));
  return time;
}

void TestActorUpdateAllTemporalKernel() {
  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  const auto logicalFrame = CutsceneTime(10.5, 11.0);
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();

  const std::uint32_t actorContext = kValueAddress;
  const std::uint32_t contextTimerAddress = actorContext + 0x02U;
  WriteObject(memory, contextTimerAddress, std::uint8_t{3U});
  auto contextIntermediate = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllContextFreezeBlock);
  contextIntermediate.r[0] = actorContext;
  contextIntermediate.r[1] = 0x12345678U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllContextFreezeBlock,
             contextIntermediate, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::
                     kOot3dActorUpdateAllContextFreezeContinue &&
             contextIntermediate.r[0] == 3U &&
             contextIntermediate.r[1] == actorContext &&
             ReadObject<std::uint8_t>(memory, contextTimerAddress) == 3U,
         "Actor_UpdateAll context freeze intermediate hold mismatch");

  auto contextLogical = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllContextFreezeBlock);
  contextLogical.r[0] = actorContext;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllContextFreezeBlock,
             contextLogical, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             contextLogical.r[0] == 2U &&
             contextLogical.r[1] == actorContext &&
             ReadObject<std::uint8_t>(memory, contextTimerAddress) == 2U,
         "Actor_UpdateAll context freeze logical advance mismatch");

  WriteObject(memory, contextTimerAddress, std::uint8_t{0U});
  auto contextZero = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllContextFreezeBlock);
  contextZero.r[0] = actorContext;
  contextZero.r[1] = 0x12345678U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllContextFreezeBlock,
             contextZero, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             contextZero.r[0] == 0U &&
             contextZero.r[1] == 0x12345678U &&
             (contextZero.cpsr &
              (oot3d::recomp::a32::kFlagN |
               oot3d::recomp::a32::kFlagZ |
               oot3d::recomp::a32::kFlagC |
               oot3d::recomp::a32::kFlagV)) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC),
         "Actor_UpdateAll context freeze zero path mismatch");

  const std::uint32_t instanceTimerAddress = kActorAddress + 0x118U;
  WriteObject(memory, instanceTimerAddress, std::uint16_t{1U});
  auto instanceIntermediate = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllInstanceFreezeBlock);
  instanceIntermediate.r[0] = kActorAddress + 0x100U;
  instanceIntermediate.r[4] = kActorAddress;
  instanceIntermediate.cpsr = oot3d::recomp::a32::kFlagV;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllInstanceFreezeBlock,
             instanceIntermediate, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::
                     kOot3dActorUpdateAllInstanceFreezeSkip &&
             instanceIntermediate.r[1] == 1U &&
             ReadObject<std::uint16_t>(memory, instanceTimerAddress) == 1U,
         "Actor_UpdateAll instance freeze intermediate gate mismatch");

  auto instanceLogical = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllInstanceFreezeBlock);
  instanceLogical.r[0] = kActorAddress + 0x100U;
  instanceLogical.r[4] = kActorAddress;
  instanceLogical.cpsr = oot3d::recomp::a32::kFlagV;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllInstanceFreezeBlock,
             instanceLogical, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::
                     kOot3dActorUpdateAllInstanceFreezePass &&
             instanceLogical.r[1] == 0U &&
             ReadObject<std::uint16_t>(memory, instanceTimerAddress) == 0U,
         "Actor_UpdateAll instance freeze zero crossing mismatch");

  auto instanceZero = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllInstanceFreezeBlock);
  instanceZero.r[0] = kActorAddress + 0x100U;
  instanceZero.r[4] = kActorAddress;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllInstanceFreezeBlock,
             instanceZero, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::
                     kOot3dActorUpdateAllInstanceFreezePass &&
             instanceZero.r[1] == 0U,
         "Actor_UpdateAll unfrozen instance failed its timer gate");

  const std::uint32_t colorTimerAddress = kActorAddress + 0x11AU;
  const std::uint32_t sfxTimerAddress = kActorAddress + 0x19CU;
  WriteObject(memory, colorTimerAddress, std::uint16_t{3U});
  WriteObject(memory, sfxTimerAddress, std::int16_t{2});
  auto effectsIntermediate = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock);
  effectsIntermediate.r[0] = kActorAddress + 0x100U;
  effectsIntermediate.r[4] = kActorAddress;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock,
             effectsIntermediate, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::
                     kOot3dActorUpdateAllEffectTimersContinue &&
             effectsIntermediate.r[1] == 2U &&
             ReadObject<std::uint16_t>(memory, colorTimerAddress) == 3U &&
             ReadObject<std::int16_t>(memory, sfxTimerAddress) == 2,
         "Actor_UpdateAll effect timer intermediate hold mismatch");

  auto effectsLogical = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock);
  effectsLogical.r[0] = kActorAddress + 0x100U;
  effectsLogical.r[4] = kActorAddress;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock,
             effectsLogical, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             effectsLogical.r[1] == 1U &&
             ReadObject<std::uint16_t>(memory, colorTimerAddress) == 2U &&
             ReadObject<std::int16_t>(memory, sfxTimerAddress) == 1,
         "Actor_UpdateAll effect timer logical advance mismatch");

  WriteObject(memory, colorTimerAddress, std::uint16_t{0U});
  WriteObject(memory, sfxTimerAddress, std::int16_t{-1});
  auto effectsTerminal = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock);
  effectsTerminal.r[0] = kActorAddress + 0x100U;
  effectsTerminal.r[4] = kActorAddress;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock,
             effectsTerminal, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             effectsTerminal.r[1] == 0xFFFFFFFFU &&
             ReadObject<std::uint16_t>(memory, colorTimerAddress) == 0U &&
             ReadObject<std::int16_t>(memory, sfxTimerAddress) == -1,
         "Actor_UpdateAll effect timer terminal state mismatch");

  auto invalidState = BuildState(
      Oot3dNativeGame::kOot3dActorUpdateAllInstanceFreezeBlock);
  invalidState.r[0] = kActorAddress + 0x104U;
  invalidState.r[4] = kActorAddress;
  const auto incomingInvalidState = invalidState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorUpdateAllInstanceFreezeBlock,
             invalidState, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             invalidState.r == incomingInvalidState.r &&
             invalidState.vfp == incomingInvalidState.vfp &&
             invalidState.cpsr == incomingInvalidState.cpsr,
         "invalid Actor_UpdateAll timer base was destructive");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(
      stats.Calls == 9U && stats.ActorCalls == 9U &&
          stats.ActorUpdateAllContextFreezeBlockCalls == 3U &&
          stats.ActorUpdateAllContextFreezeLogicalAdvances == 1U &&
          stats.ActorUpdateAllContextFreezeIntermediateHolds == 1U &&
          stats.ActorUpdateAllInstanceFreezeBlockCalls == 3U &&
          stats.ActorUpdateAllInstanceFreezeLogicalAdvances == 1U &&
          stats.ActorUpdateAllInstanceFreezeIntermediateHolds == 1U &&
          stats.ActorUpdateAllInstanceFreezeGatePasses == 2U &&
          stats.ActorUpdateAllInstanceFreezeGateSkips == 1U &&
          stats.ActorUpdateAllEffectTimerBlockCalls == 3U &&
          stats.ActorUpdateAllEffectTimerLogicalFieldAdvances == 2U &&
          stats.ActorUpdateAllEffectTimerIntermediateFieldHolds == 2U &&
          stats.RetainedAotFallbacks == 1U,
      "Actor_UpdateAll temporal kernel telemetry mismatch");
}

void TestEnKoBlinkTemporalKernel() {
  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  const auto logicalFrame = CutsceneTime(10.5, 11.0);
  constexpr std::uint32_t kTimerAddress = kActorAddress + 0x2B8U;
  constexpr std::uint32_t kSequenceAddress = kActorAddress + 0x2BAU;

  const auto buildBlinkState = [](std::uint32_t entry) {
    auto state = BuildState(entry);
    state.r[4] = kActorAddress;
    state.r[11] = kActorAddress + 0x200U;
    return state;
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  WriteObject(memory, kTimerAddress, std::int16_t{3});
  WriteObject(memory, kSequenceAddress, std::uint16_t{0U});
  auto intermediate =
      buildBlinkState(Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock, intermediate,
             memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             result.pc == Oot3dNativeGame::kOot3dEnKoBlinkContinue &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 3 &&
             ReadObject<std::uint16_t>(memory, kSequenceAddress) == 0U,
         "EnKo blink advanced on an intermediate substep");

  auto timerLogical =
      buildBlinkState(Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock, timerLogical,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             result.pc == Oot3dNativeGame::kOot3dEnKoBlinkContinue &&
             timerLogical.r[0] == 2U &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 2 &&
             ReadObject<std::uint16_t>(memory, kSequenceAddress) == 0U,
         "EnKo blink timer did not advance on a logical frame");

  WriteObject(memory, kTimerAddress, std::int16_t{1});
  auto zeroCrossing =
      buildBlinkState(Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock, zeroCrossing,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             result.pc == Oot3dNativeGame::kOot3dEnKoBlinkContinue &&
             zeroCrossing.r[0] == 1U &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 0 &&
             ReadObject<std::uint16_t>(memory, kSequenceAddress) == 1U,
         "EnKo blink zero crossing did not advance the face sequence");

  WriteObject(memory, kTimerAddress, std::int16_t{0});
  WriteObject(memory, kSequenceAddress, std::uint16_t{3U});
  auto refresh =
      buildBlinkState(Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock, refresh, memory,
             &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             result.pc == Oot3dNativeGame::kOot3dEnKoBlinkRngEntry &&
             refresh.r[0] == 30U && refresh.r[1] == 30U &&
             refresh.r[14] ==
                 Oot3dNativeGame::kOot3dEnKoBlinkRngReturnBlock &&
             (refresh.cpsr &
              (oot3d::recomp::a32::kFlagN |
               oot3d::recomp::a32::kFlagZ |
               oot3d::recomp::a32::kFlagC |
               oot3d::recomp::a32::kFlagV)) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC) &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 0 &&
             ReadObject<std::uint16_t>(memory, kSequenceAddress) == 0U,
         "EnKo blink did not dispatch its native random refresh");

  auto randomReturn =
      buildBlinkState(Oot3dNativeGame::kOot3dEnKoBlinkRngReturnBlock);
  randomReturn.r[0] = 0xABCD002FU;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKoBlinkRngReturnBlock, randomReturn,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             result.pc == Oot3dNativeGame::kOot3dEnKoBlinkContinue &&
             randomReturn.r[0] == 0xABCD002FU &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 47,
         "EnKo blink did not commit the native random timer result");

  auto invalid =
      buildBlinkState(Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock);
  invalid.r[11] += 4U;
  const auto incomingInvalid = invalid;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock, invalid, memory,
             &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             invalid.r == incomingInvalid.r &&
             invalid.vfp == incomingInvalid.vfp &&
             invalid.cpsr == incomingInvalid.cpsr,
         "invalid EnKo blink ABI was destructive");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 5U && stats.ActorCalls == 5U &&
             stats.EnKoBlinkBlockCalls == 4U &&
             stats.EnKoBlinkLogicalAdvances == 3U &&
             stats.EnKoBlinkIntermediateHolds == 1U &&
             stats.EnKoBlinkTimerAdvances == 2U &&
             stats.EnKoBlinkSequenceAdvances == 2U &&
             stats.EnKoBlinkRngDispatches == 1U &&
             stats.EnKoBlinkRngReturns == 1U &&
             stats.RetainedAotFallbacks == 1U,
         "EnKo blink temporal telemetry mismatch");
}

void TestEnKanbanPhaseTemporalKernel() {
  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  const auto logicalFrame = CutsceneTime(10.5, 11.0);
  constexpr std::uint32_t kPhaseAddress = kActorAddress + 0x1A8U;
  constexpr std::uint32_t kConditionFlags =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  const auto buildPhaseState = [](std::uint8_t phase) {
    auto state =
        BuildState(Oot3dNativeGame::kOot3dEnKanbanPhaseAdvanceBlock);
    state.r[4] = kActorAddress;
    state.r[0] = phase;
    return state;
  };
  const auto buildRippleState = [](std::uint32_t mask) {
    auto state =
        BuildState(Oot3dNativeGame::kOot3dEnKanbanRippleEventGateBlock);
    state.r[4] = kActorAddress;
    state.r[0] = mask;
    state.cpsr =
        oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;
    return state;
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  WriteObject(memory, kPhaseAddress, std::uint8_t{0xFEU});
  auto intermediate = buildPhaseState(0xFEU);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPhaseAdvanceBlock, intermediate,
             memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanPhaseAdvanceContinue &&
             intermediate.r[0] == 0xFEU &&
             ReadObject<std::uint8_t>(memory, kPhaseAddress) == 0xFEU,
         "EnKanban phase advanced on an intermediate substep");

  auto logical = buildPhaseState(0xFEU);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPhaseAdvanceBlock, logical,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanPhaseAdvanceContinue &&
             logical.r[0] == 0xFFU &&
             ReadObject<std::uint8_t>(memory, kPhaseAddress) == 0xFFU,
         "EnKanban phase did not advance on a logical frame");

  WriteObject(memory, kPhaseAddress, std::uint8_t{0xFFU});
  auto wrapping = buildPhaseState(0xFFU);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPhaseAdvanceBlock, wrapping,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             wrapping.r[0] == 0x100U &&
             ReadObject<std::uint8_t>(memory, kPhaseAddress) == 0U,
         "EnKanban phase did not retain native ADD/STRB wrap semantics");

  WriteObject(memory, kPhaseAddress, std::uint8_t{0x10U});
  auto suppressed = buildRippleState(0x5U);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanRippleEventGateBlock,
             suppressed, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             result.pc == Oot3dNativeGame::kOot3dEnKanbanRippleEventSkip &&
             suppressed.r[1] == 0x10U &&
             (suppressed.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC |
                  oot3d::recomp::a32::kFlagV),
         "EnKanban ripple event was not suppressed between logical frames");

  auto dispatched = buildRippleState(0x5U);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanRippleEventGateBlock,
             dispatched, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             result.pc == Oot3dNativeGame::kOot3dEnKanbanRippleEventPath &&
             dispatched.r[1] == 0x10U &&
             (dispatched.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC |
                  oot3d::recomp::a32::kFlagV),
         "EnKanban native ripple event did not dispatch on a logical frame");

  WriteObject(memory, kPhaseAddress, std::uint8_t{1U});
  auto nativeSkip = buildRippleState(0x5U);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanRippleEventGateBlock,
             nativeSkip, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             result.pc == Oot3dNativeGame::kOot3dEnKanbanRippleEventSkip &&
             (nativeSkip.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagC |
                  oot3d::recomp::a32::kFlagV),
         "EnKanban ripple gate changed its native phase-mask condition");

  WriteObject(memory, kPhaseAddress, std::uint8_t{7U});
  auto invalid = buildPhaseState(8U);
  const auto incomingInvalid = invalid;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPhaseAdvanceBlock, invalid,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             invalid.r == incomingInvalid.r &&
             invalid.vfp == incomingInvalid.vfp &&
             invalid.cpsr == incomingInvalid.cpsr &&
             ReadObject<std::uint8_t>(memory, kPhaseAddress) == 7U,
         "invalid EnKanban phase ABI was destructive");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 6U && stats.ActorCalls == 6U &&
             stats.EnKanbanPhaseBlockCalls == 3U &&
             stats.EnKanbanPhaseLogicalAdvances == 2U &&
             stats.EnKanbanPhaseIntermediateHolds == 1U &&
             stats.EnKanbanRippleGateCalls == 3U &&
             stats.EnKanbanRippleDispatches == 1U &&
             stats.EnKanbanRippleIntermediateSuppressions == 1U &&
             stats.RetainedAotFallbacks == 1U,
         "EnKanban phase temporal telemetry mismatch");
}

void TestEnKanbanState0CountdownTemporalKernel() {
  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  const auto logicalFrame = CutsceneTime(10.5, 11.0);
  constexpr std::uint32_t kState0TimerAddress = kActorAddress + 0x1B2U;
  constexpr std::uint32_t kActorFlagTimerAddress = kActorAddress + 0x1F2U;
  constexpr std::uint32_t kInteractionTimerAddress = kActorAddress + 0x1F5U;
  constexpr std::uint32_t kActorFlagsAddress = kActorAddress + 0x04U;
  constexpr std::uint32_t kConditionFlags =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  const auto cmpZeroFlags = [](std::uint32_t value) {
    return oot3d::recomp::a32::kFlagC |
           ((value & 0x80000000U) != 0U
                ? oot3d::recomp::a32::kFlagN
                : 0U) |
           (value == 0U ? oot3d::recomp::a32::kFlagZ : 0U);
  };
  const auto buildSignedTimerState =
      [&](std::uint32_t entry, std::int16_t timer) {
        auto state = BuildState(entry);
        state.r[4] = kActorAddress;
        state.r[5] = kActorAddress + 0x100U;
        state.r[0] = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(timer));
        state.cpsr = cmpZeroFlags(state.r[0]);
        return state;
      };
  const auto buildByteTimerState =
      [&](std::uint32_t entry, std::uint8_t timer) {
        auto state = BuildState(entry);
        state.r[4] = kActorAddress;
        state.r[5] = kActorAddress + 0x100U;
        state.r[0] = timer;
        state.cpsr = cmpZeroFlags(state.r[0]);
        return state;
      };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  WriteObject(memory, kState0TimerAddress, std::int16_t{2});
  auto state0Intermediate = buildSignedTimerState(
      Oot3dNativeGame::kOot3dEnKanbanState0CountdownBlock, 2);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanState0CountdownBlock,
             state0Intermediate, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanState0CountdownContinue &&
             state0Intermediate.r[0] == 2U &&
             ReadObject<std::int16_t>(memory, kState0TimerAddress) == 2,
         "EnKanban state-zero countdown advanced between logical frames");

  auto state0Logical = buildSignedTimerState(
      Oot3dNativeGame::kOot3dEnKanbanState0CountdownBlock, 2);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanState0CountdownBlock,
             state0Logical, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanState0CountdownContinue &&
             state0Logical.r[0] == 1U &&
             ReadObject<std::int16_t>(memory, kState0TimerAddress) == 1,
         "EnKanban state-zero countdown did not advance logically");

  WriteObject(memory, kState0TimerAddress,
              std::numeric_limits<std::int16_t>::min());
  auto state0Wrapping = buildSignedTimerState(
      Oot3dNativeGame::kOot3dEnKanbanState0CountdownBlock,
      std::numeric_limits<std::int16_t>::min());
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanState0CountdownBlock,
             state0Wrapping, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             state0Wrapping.r[0] == 0xFFFF7FFFU &&
             (state0Wrapping.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagN |
                  oot3d::recomp::a32::kFlagC) &&
             ReadObject<std::int16_t>(memory, kState0TimerAddress) ==
                 std::numeric_limits<std::int16_t>::max(),
         "EnKanban state-zero countdown lost native SUB/STRH wrap");

  WriteObject(memory, kActorFlagTimerAddress, std::int16_t{2});
  WriteU32(memory, kActorFlagsAddress, 0xA5U);
  auto flagIntermediate = buildSignedTimerState(
      Oot3dNativeGame::kOot3dEnKanbanActorFlagCountdownBlock, 2);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanActorFlagCountdownBlock,
             flagIntermediate, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanActorFlagCountdownContinue &&
             ReadObject<std::int16_t>(memory, kActorFlagTimerAddress) == 2 &&
             ReadObject<std::uint32_t>(memory, kActorFlagsAddress) == 0xA5U,
         "EnKanban actor-flag countdown mutated on an intermediate substep");

  auto flagLogical = buildSignedTimerState(
      Oot3dNativeGame::kOot3dEnKanbanActorFlagCountdownBlock, 2);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanActorFlagCountdownBlock,
             flagLogical, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             flagLogical.r[0] == 1U &&
             (flagLogical.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC) &&
             ReadObject<std::int16_t>(memory, kActorFlagTimerAddress) == 1 &&
             ReadObject<std::uint32_t>(memory, kActorFlagsAddress) == 0xA4U,
         "EnKanban result-one actor-flag side effect was not reproduced");

  WriteObject(memory, kActorFlagTimerAddress, std::int16_t{1});
  WriteU32(memory, kActorFlagsAddress, 0xA5U);
  auto flagZero = buildSignedTimerState(
      Oot3dNativeGame::kOot3dEnKanbanActorFlagCountdownBlock, 1);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanActorFlagCountdownBlock,
             flagZero, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             flagZero.r[0] == 0U &&
             (flagZero.cpsr & kConditionFlags) ==
                 oot3d::recomp::a32::kFlagN &&
             ReadObject<std::int16_t>(memory, kActorFlagTimerAddress) == 0 &&
             ReadObject<std::uint32_t>(memory, kActorFlagsAddress) == 0xA5U,
         "EnKanban actor flag was cleared outside the native result-one edge");

  WriteObject(memory, kInteractionTimerAddress, std::uint8_t{1U});
  auto interactionIntermediate = buildByteTimerState(
      Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownBlock, 1U);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownBlock,
             interactionIntermediate, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownBlocked &&
             ReadObject<std::uint8_t>(memory, kInteractionTimerAddress) == 1U,
         "EnKanban interaction cooldown did not hold and block");

  auto interactionLogical = buildByteTimerState(
      Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownBlock, 1U);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownBlock,
             interactionLogical, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownBlocked &&
             interactionLogical.r[0] == 0U &&
             (interactionLogical.cpsr & kConditionFlags) ==
                 oot3d::recomp::a32::kFlagC &&
             ReadObject<std::uint8_t>(memory, kInteractionTimerAddress) == 0U,
         "EnKanban cooldown zero crossing admitted interaction too early");

  auto interactionReady = buildByteTimerState(
      Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownBlock, 0U);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownBlock,
             interactionReady, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownReady &&
             (interactionReady.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC),
         "EnKanban zero cooldown did not admit the native interaction path");

  WriteObject(memory, kState0TimerAddress, std::int16_t{3});
  auto invalid = buildSignedTimerState(
      Oot3dNativeGame::kOot3dEnKanbanState0CountdownBlock, 3);
  invalid.r[5] += 4U;
  const auto incomingInvalid = invalid;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanState0CountdownBlock, invalid,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             invalid.r == incomingInvalid.r &&
             invalid.cpsr == incomingInvalid.cpsr &&
             ReadObject<std::int16_t>(memory, kState0TimerAddress) == 3,
         "invalid EnKanban state-zero timer ABI was destructive");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(
      stats.Calls == 9U && stats.ActorCalls == 9U &&
          stats.EnKanbanState0CountdownBlockCalls == 3U &&
          stats.EnKanbanState0CountdownLogicalAdvances == 2U &&
          stats.EnKanbanState0CountdownIntermediateHolds == 1U &&
          stats.EnKanbanActorFlagCountdownBlockCalls == 3U &&
          stats.EnKanbanActorFlagCountdownLogicalAdvances == 2U &&
          stats.EnKanbanActorFlagCountdownIntermediateHolds == 1U &&
          stats.EnKanbanActorFlagClears == 1U &&
          stats.EnKanbanInteractionCooldownBlockCalls == 3U &&
          stats.EnKanbanInteractionCooldownLogicalAdvances == 1U &&
          stats.EnKanbanInteractionCooldownIntermediateHolds == 1U &&
          stats.EnKanbanInteractionCooldownReadyPasses == 1U &&
          stats.EnKanbanInteractionCooldownBlockedPasses == 2U &&
          stats.RetainedAotFallbacks == 1U,
      "EnKanban state-zero countdown telemetry mismatch");
}

void TestEnKanbanDrawGateRampTemporalKernel() {
  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  const auto logicalFrame = CutsceneTime(10.5, 11.0);
  constexpr std::uint32_t kTimerAddress = kActorAddress + 0x1EEU;
  constexpr std::uint32_t kRampAddress = kActorAddress + 0x1F0U;
  constexpr std::uint32_t kConditionFlags =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;
  constexpr std::uint32_t kR1Sentinel = 0x12345678U;

  const auto cmpZeroFlags = [](std::uint32_t value) {
    return oot3d::recomp::a32::kFlagC |
           ((value & 0x80000000U) != 0U
                ? oot3d::recomp::a32::kFlagN
                : 0U) |
           (value == 0U ? oot3d::recomp::a32::kFlagZ : 0U);
  };
  const auto buildState = [&](std::int16_t timer) {
    auto state =
        BuildState(Oot3dNativeGame::kOot3dEnKanbanDrawGateRampBlock);
    state.r[4] = kActorAddress;
    state.r[5] = kActorAddress + 0x100U;
    state.r[7] = 0U;
    state.r[0] =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
    state.r[1] = kR1Sentinel;
    state.cpsr = cmpZeroFlags(state.r[0]);
    return state;
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  WriteObject(memory, kTimerAddress, std::int16_t{8});
  WriteObject(memory, kRampAddress, std::uint16_t{0});
  auto intermediate = buildState(8);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanDrawGateRampBlock,
             intermediate, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanDrawGateRampContinue &&
             intermediate.r[0] == 8U &&
             intermediate.r[1] == kR1Sentinel &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 8 &&
             ReadObject<std::uint16_t>(memory, kRampAddress) == 0U,
         "EnKanban draw-gate ramp advanced between logical frames");

  auto increase = buildState(8);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanDrawGateRampBlock, increase,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             increase.r[0] == 7U && increase.r[1] == 0xFFU &&
             (increase.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC) &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 7 &&
             ReadObject<std::uint16_t>(memory, kRampAddress) == 0xFFU,
         "EnKanban draw-gate ramp increase mismatch");

  WriteObject(memory, kTimerAddress, std::int16_t{8});
  WriteObject(memory, kRampAddress, std::uint16_t{10});
  auto upperClamp = buildState(8);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanDrawGateRampBlock, upperClamp,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             upperClamp.r[0] == 7U && upperClamp.r[1] == 0xFFU &&
             (upperClamp.cpsr & kConditionFlags) ==
                 oot3d::recomp::a32::kFlagC &&
             ReadObject<std::uint16_t>(memory, kRampAddress) == 0xFFU,
         "EnKanban draw-gate ramp upper clamp mismatch");

  WriteObject(memory, kTimerAddress, std::int16_t{7});
  WriteObject(memory, kRampAddress, std::uint16_t{0xFFU});
  auto decrease = buildState(7);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanDrawGateRampBlock, decrease,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             decrease.r[0] == 6U && decrease.r[1] == 190U &&
             (decrease.cpsr & kConditionFlags) ==
                 oot3d::recomp::a32::kFlagC &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 6 &&
             ReadObject<std::uint16_t>(memory, kRampAddress) == 190U,
         "EnKanban draw-gate ramp decrease mismatch");

  WriteObject(memory, kTimerAddress, std::int16_t{1});
  WriteObject(memory, kRampAddress, std::uint16_t{0});
  auto lowerClamp = buildState(1);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanDrawGateRampBlock, lowerClamp,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             lowerClamp.r[0] == 0U &&
             lowerClamp.r[1] == 0xFFFFFFBFU &&
             (lowerClamp.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagN |
                  oot3d::recomp::a32::kFlagC) &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 0 &&
             ReadObject<std::uint16_t>(memory, kRampAddress) == 0U,
         "EnKanban draw-gate ramp lower clamp mismatch");

  WriteObject(memory, kTimerAddress, std::int16_t{0});
  WriteObject(memory, kRampAddress, std::uint16_t{123U});
  auto inactive = buildState(0);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanDrawGateRampBlock, inactive,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             inactive.r[0] == 0U && inactive.r[1] == kR1Sentinel &&
             (inactive.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC) &&
             ReadObject<std::uint16_t>(memory, kRampAddress) == 123U,
         "EnKanban inactive draw-gate ramp changed guest state");

  WriteObject(memory, kTimerAddress, std::int16_t{8});
  auto invalid = buildState(8);
  invalid.r[7] = 1U;
  const auto incomingInvalid = invalid;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanDrawGateRampBlock, invalid,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             invalid.r == incomingInvalid.r &&
             invalid.cpsr == incomingInvalid.cpsr &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 8,
         "invalid EnKanban draw-gate ramp ABI was destructive");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 6U && stats.ActorCalls == 6U &&
             stats.EnKanbanDrawGateRampBlockCalls == 6U &&
             stats.EnKanbanDrawGateRampLogicalAdvances == 4U &&
             stats.EnKanbanDrawGateRampIntermediateHolds == 1U &&
             stats.EnKanbanDrawGateRampInactivePasses == 1U &&
             stats.EnKanbanDrawGateRampIncreaseSteps == 2U &&
             stats.EnKanbanDrawGateRampDecreaseSteps == 2U &&
             stats.EnKanbanDrawGateRampClamps == 2U &&
             stats.RetainedAotFallbacks == 1U,
         "EnKanban draw-gate ramp telemetry mismatch");
}

void TestEnKanbanOscillatorTemporalKernel() {
  auto memory = BuildMemory(1, 0U);
  constexpr std::uint32_t kBackgroundFlagsAddress = kActorAddress + 0x90U;
  constexpr std::uint32_t kActorStateAddress = kActorAddress + 0x1ACU;
  constexpr std::uint32_t kTerminalVelocityBits = 0xFFFFEE00U;
  constexpr std::uint32_t kConditionFlags =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  struct Site {
    std::uint32_t Entry;
    std::uint32_t Continuation;
    std::uint32_t DisplacementOffset;
    std::uint32_t VelocityOffset;
    std::uint32_t DirectionOffset;
  };
  constexpr Site kX{
      Oot3dNativeGame::kOot3dEnKanbanOscillatorXBlock,
      Oot3dNativeGame::kOot3dEnKanbanOscillatorXContinue,
      0x1C0U,
      0x1C6U,
      0x1CCU,
  };
  constexpr Site kY{
      Oot3dNativeGame::kOot3dEnKanbanOscillatorYBlock,
      Oot3dNativeGame::kOot3dEnKanbanOscillatorYContinue,
      0x1C4U,
      0x1CAU,
      0x1CDU,
  };
  const auto writeOwner = [&](const Site &site, std::int16_t displacement,
                              std::int16_t velocity, std::uint8_t direction,
                              bool grounded,
                              std::uint8_t actorState = 1U) {
    WriteObject(memory, kActorAddress + site.DisplacementOffset,
                displacement);
    WriteObject(memory, kActorAddress + site.VelocityOffset, velocity);
    WriteObject(memory, kActorAddress + site.DirectionOffset, direction);
    WriteObject(memory, kBackgroundFlagsAddress,
                static_cast<std::uint16_t>(grounded ? 1U : 0U));
    WriteObject(memory, kActorStateAddress, actorState);
  };
  const auto buildState = [&](const Site &site, std::int16_t displacement,
                              std::int16_t velocity, std::uint8_t direction,
                              bool grounded) {
    auto state = BuildState(site.Entry);
    state.r[0] = static_cast<std::uint16_t>(displacement);
    state.r[1] = static_cast<std::uint16_t>(velocity);
    state.r[2] = kTerminalVelocityBits;
    state.r[4] = kActorAddress;
    state.r[5] = kActorAddress + 0x100U;
    state.r[6] = grounded ? 1U : 0U;
    state.r[7] = 0U;
    state.cpsr = oot3d::recomp::a32::kFlagC |
                 (direction == 0U ? oot3d::recomp::a32::kFlagZ : 0U);
    return state;
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  writeOwner(kX, -10000, 0x1800, 1U, false);
  auto nativeX = buildState(kX, -10000, 0x1800, 1U, false);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             kX.Entry, nativeX, memory, &result, {2.0F, nullptr},
             &blocksConsumed) &&
             result.pc == kX.Continuation && nativeX.r[0] == 0xC00U &&
             nativeX.r[1] == 0x1800U &&
             (nativeX.cpsr & kConditionFlags) == 0U &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kX.DisplacementOffset) == -3856 &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kX.VelocityOffset) == 0xC00,
         "EnKanban X oscillator native-step mismatch");

  writeOwner(kY, -1000, 500, 1U, true);
  auto resetY = buildState(kY, -1000, 500, 1U, true);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             kY.Entry, resetY, memory, &result, {2.0F, nullptr},
             &blocksConsumed) &&
             result.pc == kY.Continuation &&
             resetY.r[0] == 0xFFFFFE0CU &&
             (resetY.cpsr & kConditionFlags) ==
                 oot3d::recomp::a32::kFlagC &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kY.DisplacementOffset) == 0 &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kY.VelocityOffset) == 0,
         "EnKanban Y oscillator ground reset mismatch");

  writeOwner(kX, -10000, 0x1800, 1U, false);
  auto firstHalf = buildState(kX, -10000, 0x1800, 1U, false);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             kX.Entry, firstHalf, memory, &result, {1.0F, nullptr},
             &blocksConsumed) &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kX.DisplacementOffset) == -6544 &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kX.VelocityOffset) == 0x1200,
         "EnKanban X oscillator first half-step mismatch");

  auto secondHalf = buildState(kX, -6544, 0x1200, 1U, false);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             kX.Entry, secondHalf, memory, &result, {1.0F, nullptr},
             &blocksConsumed) &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kX.DisplacementOffset) == -3856 &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kX.VelocityOffset) == 0xC00,
         "EnKanban X oscillator half steps changed the native endpoint");

  writeOwner(kY, -10000, -3000, 1U, false);
  auto clampY = buildState(kY, -10000, -3000, 1U, false);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             kY.Entry, clampY, memory, &result, {2.0F, nullptr},
             &blocksConsumed) &&
             clampY.r[0] == 0xFFFFE848U &&
             (clampY.cpsr & kConditionFlags) ==
                 oot3d::recomp::a32::kFlagN &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kY.DisplacementOffset) == -13000 &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kY.VelocityOffset) == -0x1200,
         "EnKanban Y oscillator terminal clamp mismatch");

  writeOwner(kX, -10000, 0x1800, 1U, false, 3U);
  auto invalid = buildState(kX, -10000, 0x1800, 1U, false);
  const auto incomingInvalid = invalid;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             kX.Entry, invalid, memory, &result, {1.0F, nullptr},
             &blocksConsumed) &&
             invalid.r == incomingInvalid.r &&
             invalid.cpsr == incomingInvalid.cpsr &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kX.DisplacementOffset) == -10000 &&
             ReadObject<std::int16_t>(
                 memory, kActorAddress + kX.VelocityOffset) == 0x1800,
         "invalid EnKanban oscillator ABI was destructive");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 5U && stats.ActorCalls == 5U &&
             stats.EnKanbanOscillatorAxisBlockCalls == 5U &&
             stats.EnKanbanOscillatorXBlockCalls == 3U &&
             stats.EnKanbanOscillatorYBlockCalls == 2U &&
             stats.EnKanbanOscillatorRateAdjustedSteps == 2U &&
             stats.EnKanbanOscillatorGroundResets == 1U &&
             stats.EnKanbanOscillatorVelocityClamps == 1U &&
             stats.RetainedAotFallbacks == 1U,
         "EnKanban oscillator telemetry mismatch");
}

void TestEnKanbanPieceLifetimeTemporalKernel() {
  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  const auto logicalFrame = CutsceneTime(10.5, 11.0);
  constexpr std::uint32_t kTimerAddress = kActorAddress + 0x1AAU;
  constexpr std::uint32_t kStateAddress = kActorAddress + 0x1ACU;
  constexpr std::uint32_t kConditionFlags =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  const auto cmpZeroFlags = [](std::uint32_t value) {
    return oot3d::recomp::a32::kFlagC |
           ((value & 0x80000000U) != 0U
                ? oot3d::recomp::a32::kFlagN
                : 0U) |
           (value == 0U ? oot3d::recomp::a32::kFlagZ : 0U);
  };
  const auto buildState = [&](std::int16_t timer) {
    auto state =
        BuildState(Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeBlock);
    state.r[4] = kActorAddress;
    state.r[5] = kActorAddress + 0x100U;
    state.r[0] =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
    state.cpsr = cmpZeroFlags(state.r[0]);
    return state;
  };
  const auto writeOwner = [&](std::int16_t timer,
                              std::uint8_t actorState = 2U) {
    WriteObject(memory, kTimerAddress, timer);
    WriteObject(memory, kStateAddress, actorState);
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  writeOwner(3);
  auto intermediatePositive = buildState(3);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeBlock,
             intermediatePositive, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeContinue &&
             intermediatePositive.r[0] == 3U &&
             (intermediatePositive.cpsr & kConditionFlags) ==
                 oot3d::recomp::a32::kFlagC &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 3 &&
             ReadObject<std::uint8_t>(memory, kStateAddress) == 2U,
         "EnKanban piece lifetime advanced between logical frames");

  auto logicalPositive = buildState(3);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeBlock,
             logicalPositive, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             logicalPositive.r[0] == 2U &&
             (logicalPositive.cpsr & kConditionFlags) ==
                 oot3d::recomp::a32::kFlagC &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 2 &&
             ReadObject<std::uint8_t>(memory, kStateAddress) == 2U,
         "EnKanban piece lifetime logical decrement mismatch");

  writeOwner(0);
  auto intermediateZero = buildState(0);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeBlock,
             intermediateZero, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             intermediateZero.r[0] == 0U &&
             (intermediateZero.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC) &&
             ReadObject<std::uint8_t>(memory, kStateAddress) == 2U,
         "EnKanban existing-zero transition was not deferred");

  writeOwner(1);
  auto zeroCrossing = buildState(1);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeBlock,
             zeroCrossing, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             zeroCrossing.r[0] == 3U &&
             (zeroCrossing.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC) &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 0 &&
             ReadObject<std::uint8_t>(memory, kStateAddress) == 3U,
         "EnKanban piece lifetime zero-crossing transition mismatch");

  writeOwner(0);
  auto existingZero = buildState(0);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeBlock,
             existingZero, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             existingZero.r[0] == 3U &&
             (existingZero.cpsr & kConditionFlags) ==
                 (oot3d::recomp::a32::kFlagZ |
                  oot3d::recomp::a32::kFlagC) &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 0 &&
             ReadObject<std::uint8_t>(memory, kStateAddress) == 3U,
         "EnKanban piece lifetime existing-zero transition mismatch");

  writeOwner(std::numeric_limits<std::int16_t>::min());
  auto wrapping = buildState(std::numeric_limits<std::int16_t>::min());
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeBlock, wrapping,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             wrapping.r[0] ==
                 static_cast<std::uint32_t>(
                     std::numeric_limits<std::int16_t>::max()) &&
             (wrapping.cpsr & kConditionFlags) ==
                 oot3d::recomp::a32::kFlagC &&
             ReadObject<std::int16_t>(memory, kTimerAddress) ==
                 std::numeric_limits<std::int16_t>::max() &&
             ReadObject<std::uint8_t>(memory, kStateAddress) == 2U,
         "EnKanban piece lifetime signed wrap mismatch");

  writeOwner(3, 1U);
  auto invalid = buildState(3);
  const auto incomingInvalid = invalid;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeBlock, invalid,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             invalid.r == incomingInvalid.r &&
             invalid.cpsr == incomingInvalid.cpsr &&
             ReadObject<std::int16_t>(memory, kTimerAddress) == 3 &&
             ReadObject<std::uint8_t>(memory, kStateAddress) == 1U,
         "invalid EnKanban piece-lifetime ABI was destructive");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 6U && stats.ActorCalls == 6U &&
             stats.EnKanbanPieceLifetimeBlockCalls == 6U &&
             stats.EnKanbanPieceLifetimeLogicalDecrements == 3U &&
             stats.EnKanbanPieceLifetimeIntermediateHolds == 2U &&
             stats.EnKanbanPieceLifetimeStateTransitions == 2U &&
             stats.EnKanbanPieceLifetimeZeroCrossingTransitions == 1U &&
             stats.EnKanbanPieceLifetimeExistingZeroTransitions == 1U &&
             stats.RetainedAotFallbacks == 1U,
         "EnKanban piece-lifetime telemetry mismatch");
}

void TestCameraUpdateLogicalCounters() {
  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  const auto logicalFrame = CutsceneTime(10.5, 11.0);
  const auto buildCounterState = [](std::uint32_t entry) {
    auto state = BuildState(entry);
    state.r[4] = kCameraAddress;
    state.r[5] = kCameraAddress + 0x100U;
    state.r[7] = kCameraGlobalStateAddress;
    state.r[8] = 0x3200U;
    return state;
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  const std::uint32_t floorCounterAddress =
      kCameraGlobalStateAddress + 0x9CU;
  WriteU32(memory, floorCounterAddress, 199U);
  auto floorState = buildCounterState(
      Oot3dNativeGame::kOot3dCameraFloorMissCounterAdvanceBlock);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraFloorMissCounterAdvanceBlock,
             floorState, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed),
         "logical camera floor-miss counter retained AOT unexpectedly");
  Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
             result.pc ==
                 Oot3dNativeGame::kOot3dCameraFloorMissCounterContinue &&
             blocksConsumed == 1U && floorState.r[0] == 200U &&
             ReadObject<std::uint32_t>(memory, floorCounterAddress) == 200U,
         "logical camera floor-miss counter result mismatch");

  floorState = buildCounterState(
      Oot3dNativeGame::kOot3dCameraFloorMissCounterAdvanceBlock);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraFloorMissCounterAdvanceBlock,
             floorState, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             floorState.r[0] == 200U &&
             ReadObject<std::uint32_t>(memory, floorCounterAddress) == 200U,
         "intermediate camera floor-miss counter was not held");

  WriteU32(memory, floorCounterAddress,
           std::numeric_limits<std::uint32_t>::max());
  floorState = buildCounterState(
      Oot3dNativeGame::kOot3dCameraFloorMissCounterAdvanceBlock);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraFloorMissCounterAdvanceBlock,
             floorState, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             floorState.r[0] == 0U &&
             ReadObject<std::uint32_t>(memory, floorCounterAddress) == 0U,
         "camera floor-miss counter did not preserve native u32 wrap");

  const std::uint32_t interfaceDelayAddress =
      kCameraGlobalStateAddress + 0x28U;
  WriteU32(memory, interfaceDelayAddress, 3U);
  auto interfaceState = buildCounterState(
      Oot3dNativeGame::kOot3dCameraInterfaceDelayAdvanceBlock);
  interfaceState.r[1] = 3U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraInterfaceDelayAdvanceBlock,
             interfaceState, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed),
         "logical camera interface delay retained AOT unexpectedly");
  Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
             result.pc ==
                 Oot3dNativeGame::kOot3dCameraInterfaceDelayContinue &&
             blocksConsumed == 1U && interfaceState.r[0] == 2U &&
             ReadObject<std::uint32_t>(memory, interfaceDelayAddress) == 2U,
         "logical camera interface delay result mismatch");

  interfaceState = buildCounterState(
      Oot3dNativeGame::kOot3dCameraInterfaceDelayAdvanceBlock);
  interfaceState.r[1] = 2U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraInterfaceDelayAdvanceBlock,
             interfaceState, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             interfaceState.r[0] == 2U &&
             ReadObject<std::uint32_t>(memory, interfaceDelayAddress) == 2U,
         "intermediate camera interface delay was not held");

  const std::uint32_t waterTimerAddress = kCameraAddress + 0x198U;
  const auto buildWaterState = [] {
    auto state = BuildState(
        Oot3dNativeGame::kOot3dCameraWaterDistortionTimerAdvanceBlock);
    state.r[4] = kCameraAddress + 0x100U;
    state.r[5] = kCameraAddress;
    state.r[6] = kCameraAddress + 0x168U;
    return state;
  };
  WriteObject(memory, waterTimerAddress, std::int16_t{80});
  auto waterState = buildWaterState();
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraWaterDistortionTimerAdvanceBlock,
             waterState, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed),
         "logical water distortion timer retained AOT unexpectedly");
  Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
             result.pc ==
                 Oot3dNativeGame::kOot3dCameraWaterDistortionTimerContinue &&
             blocksConsumed == 1U && waterState.r[0] == 79U &&
             ReadObject<std::int16_t>(memory, waterTimerAddress) == 79,
         "logical water distortion timer result mismatch");

  waterState = buildWaterState();
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraWaterDistortionTimerAdvanceBlock,
             waterState, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             waterState.r[0] == 79U &&
             ReadObject<std::int16_t>(memory, waterTimerAddress) == 79,
         "intermediate water distortion timer was not held");

  WriteObject(memory, waterTimerAddress, std::int16_t{0});
  waterState = buildWaterState();
  const auto incomingWaterState = waterState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraWaterDistortionTimerAdvanceBlock,
             waterState, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             waterState.r == incomingWaterState.r &&
             waterState.vfp == incomingWaterState.vfp &&
             waterState.cpsr == incomingWaterState.cpsr &&
             waterState.fpscr == incomingWaterState.fpscr,
         "terminal water distortion timer did not retain its native branch");

  auto invalidState = buildCounterState(
      Oot3dNativeGame::kOot3dCameraInterfaceDelayAdvanceBlock);
  invalidState.r[1] = 2U;
  invalidState.r[7] = kCameraGlobalStateAddress + 4U;
  const auto incomingInvalidState = invalidState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraInterfaceDelayAdvanceBlock,
             invalidState, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             invalidState.r == incomingInvalidState.r &&
             invalidState.vfp == incomingInvalidState.vfp &&
             invalidState.cpsr == incomingInvalidState.cpsr &&
             invalidState.fpscr == incomingInvalidState.fpscr &&
             ReadObject<std::uint32_t>(memory, interfaceDelayAddress) == 2U,
         "invalid camera counter context was not a non-destructive fallback");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 7U && stats.CameraCalls == 7U &&
             stats.CameraFloorMissCounterBlockCalls == 3U &&
             stats.CameraFloorMissCounterLogicalAdvances == 2U &&
             stats.CameraFloorMissCounterIntermediateHolds == 1U &&
             stats.CameraInterfaceDelayBlockCalls == 2U &&
             stats.CameraInterfaceDelayLogicalAdvances == 1U &&
             stats.CameraInterfaceDelayIntermediateHolds == 1U &&
             stats.CameraWaterDistortionTimerBlockCalls == 2U &&
             stats.CameraWaterDistortionTimerLogicalAdvances == 1U &&
             stats.CameraWaterDistortionTimerIntermediateHolds == 1U &&
             stats.RetainedAotFallbacks == 2U,
         "camera logical-counter telemetry mismatch");
}

void TestCameraModeFrameCountdowns() {
  struct Site {
    std::uint32_t Entry;
    std::uint32_t Continuation;
    std::uint8_t CameraRegister;
    std::uint8_t ModeBaseRegister;
    std::uint16_t ModeBaseOffset;
    std::uint16_t CountdownOffset;
    bool SignedLoad;
  };
  constexpr std::array<Site, 12> sites{{
      {Oot3dNativeGame::kOot3dCameraJump1FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraJump1FrameCountdownContinue, 4U, 5U,
       0x20U, 0x3AU, false},
      {Oot3dNativeGame::kOot3dCameraJump2FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraJump2FrameCountdownContinue, 4U, 5U,
       0x24U, 0x30U, false},
      {Oot3dNativeGame::kOot3dCameraBattle1FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraBattle1FrameCountdownContinue, 4U, 5U,
       0x30U, 0x4AU, false},
      {Oot3dNativeGame::kOot3dCameraBattle4FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraBattle4FrameCountdownContinue, 4U, 6U,
       0x1CU, 0x1CU, false},
      {Oot3dNativeGame::kOot3dCameraKeepOn1FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraKeepOn1FrameCountdownContinue, 4U, 5U,
       0x34U, 0x4AU, false},
      {Oot3dNativeGame::kOot3dCameraKeepOn3FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraKeepOn3FrameCountdownContinue, 6U, 4U,
       0x30U, 0x4CU, false},
      {Oot3dNativeGame::kOot3dCameraNormal1FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraNormal1FrameCountdownContinue, 4U, 5U,
       0x24U, 0x4CU, true},
      {Oot3dNativeGame::kOot3dCameraNormal1SpeedCountdownBlock,
       Oot3dNativeGame::kOot3dCameraNormal1SpeedCountdownContinue, 4U, 5U,
       0x24U, 0x4EU, false},
      {Oot3dNativeGame::kOot3dCameraNormal1RateCountdownBlock,
       Oot3dNativeGame::kOot3dCameraNormal1RateCountdownContinue, 4U, 5U,
       0x24U, 0x3EU, false},
      {Oot3dNativeGame::kOot3dCameraUnique1FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraUnique1FrameCountdownContinue, 4U, 5U,
       0x1CU, 0x24U, false},
      {Oot3dNativeGame::kOot3dCameraSubj3FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraSubj3FrameCountdownContinue, 4U, 5U,
       0x24U, 0x2CU, false},
      {Oot3dNativeGame::kOot3dCameraParallel1FrameCountdownBlock,
       Oot3dNativeGame::kOot3dCameraParallel1FrameCountdownContinue, 4U, 5U,
       0x28U, 0x40U, false},
  }};

  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  const auto logicalFrame = CutsceneTime(10.5, 11.0);
  const auto buildState = [](const Site &site, std::int16_t countdown) {
    auto state = BuildState(site.Entry);
    const std::uint16_t bits = std::bit_cast<std::uint16_t>(countdown);
    state.r[0] =
        site.SignedLoad
            ? static_cast<std::uint32_t>(static_cast<std::int32_t>(countdown))
            : static_cast<std::uint32_t>(bits);
    state.r[site.CameraRegister] = kCameraAddress;
    state.r[site.ModeBaseRegister] =
        kCameraAddress + site.ModeBaseOffset;
    state.cpsr = oot3d::recomp::a32::kFlagC;
    return state;
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  for (const auto &site : sites) {
    const std::uint32_t countdownAddress =
        kCameraAddress + site.CountdownOffset;
    WriteObject(memory, countdownAddress, std::int16_t{6});
    auto logicalState = buildState(site, 6);
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               site.Entry, logicalState, memory, &result,
               {1.0F, &logicalFrame}, &blocksConsumed),
           "camera mode countdown retained AOT unexpectedly");
    Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == site.Continuation && blocksConsumed == 1U &&
               logicalState.r[0] == 5U &&
               ReadObject<std::int16_t>(memory, countdownAddress) == 5,
           "camera mode countdown result mismatch");
  }

  const Site &normal1 = sites[6];
  const std::uint32_t normal1CountdownAddress =
      kCameraAddress + normal1.CountdownOffset;
  WriteObject(memory, normal1CountdownAddress, std::int16_t{14});
  auto intermediateState = buildState(normal1, 14);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             normal1.Entry, intermediateState, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             intermediateState.r[0] == 14U &&
             ReadObject<std::int16_t>(memory, normal1CountdownAddress) == 14,
         "intermediate camera mode countdown was not held");

  WriteObject(memory, normal1CountdownAddress,
              std::numeric_limits<std::int16_t>::min());
  auto wrappingState = buildState(
      normal1, std::numeric_limits<std::int16_t>::min());
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             normal1.Entry, wrappingState, memory, &result,
             {1.0F, &logicalFrame}, &blocksConsumed) &&
             wrappingState.r[0] == 0xFFFF7FFFU &&
             ReadObject<std::int16_t>(memory, normal1CountdownAddress) ==
                 std::numeric_limits<std::int16_t>::max(),
         "signed camera mode countdown did not preserve ARM/s16 wrap");

  WriteObject(memory, normal1CountdownAddress, std::int16_t{0});
  auto zeroState = buildState(normal1, 0);
  zeroState.cpsr =
      oot3d::recomp::a32::kFlagZ | oot3d::recomp::a32::kFlagC;
  const auto incomingZeroState = zeroState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             normal1.Entry, zeroState, memory, &result,
             {1.0F, &logicalFrame},
             &blocksConsumed) &&
             zeroState.r == incomingZeroState.r &&
             zeroState.vfp == incomingZeroState.vfp &&
             zeroState.cpsr == incomingZeroState.cpsr &&
             ReadObject<std::int16_t>(memory, normal1CountdownAddress) == 0,
         "terminal Camera_Normal1 countdown did not retain native A32");

  const Site &jump1 = sites[0];
  const std::uint32_t jump1CountdownAddress =
      kCameraAddress + jump1.CountdownOffset;
  WriteObject(memory, jump1CountdownAddress, std::int16_t{0});
  auto unsignedZeroState = buildState(jump1, 0);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             jump1.Entry, unsignedZeroState, memory, &result,
             {1.0F, &logicalFrame}, &blocksConsumed) &&
             unsignedZeroState.r[0] == 0xFFFFFFFFU &&
             ReadObject<std::int16_t>(memory, jump1CountdownAddress) == -1,
         "unconditional camera mode countdown lost native zero underflow");

  const Site &parallel1 = sites[11];
  const std::uint32_t parallelCountdownAddress =
      kCameraAddress + parallel1.CountdownOffset;
  WriteObject(memory, parallelCountdownAddress, std::int16_t{7});
  auto invalidState = buildState(parallel1, 7);
  invalidState.r[parallel1.ModeBaseRegister] += 4U;
  const auto incomingInvalidState = invalidState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             parallel1.Entry, invalidState, memory, &result,
             {1.0F, &logicalFrame},
             &blocksConsumed) &&
             invalidState.r == incomingInvalidState.r &&
             invalidState.vfp == incomingInvalidState.vfp &&
             invalidState.cpsr == incomingInvalidState.cpsr &&
             ReadObject<std::int16_t>(memory, parallelCountdownAddress) == 7,
         "invalid camera mode countdown context was destructive");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 15U && stats.CameraCalls == 15U &&
             stats.CameraModeFrameCountdownBlockCalls == 15U &&
             stats.CameraModeFrameCountdownLogicalAdvances == 14U &&
             stats.CameraModeFrameCountdownIntermediateHolds == 1U &&
             stats.RetainedAotFallbacks == 2U,
         "camera mode countdown telemetry mismatch");
}

void TestCameraSpecial5Timer() {
  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  const auto logicalFrame = CutsceneTime(10.5, 11.0);
  constexpr std::uint32_t timerAddress = kCameraAddress + 0x1CU;
  const auto buildState = [](std::int16_t timer) {
    auto state =
        BuildState(Oot3dNativeGame::kOot3dCameraSpecial5TimerBlock);
    state.r[0] =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
    state.r[2] = 0U;
    state.r[4] = kCameraAddress;
    state.r[7] = timerAddress;
    state.cpsr = oot3d::recomp::a32::kFlagC;
    if (timer == 0) {
      state.cpsr |= oot3d::recomp::a32::kFlagZ;
    } else if (timer < 0) {
      state.cpsr |= oot3d::recomp::a32::kFlagN;
    }
    return state;
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  WriteObject(memory, timerAddress, std::int16_t{3});
  auto logicalPositive = buildState(3);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraSpecial5TimerBlock,
             logicalPositive, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             result.kind == oot3d::recomp::a32::ExitKind::Branch &&
             result.pc ==
                 Oot3dNativeGame::kOot3dCameraSpecial5CommonContinue &&
             blocksConsumed == 1U && logicalPositive.r[0] == 2U &&
             ReadObject<std::int16_t>(memory, timerAddress) == 2,
         "Camera_Special5 positive timer result mismatch");

  auto intermediatePositive = buildState(2);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraSpecial5TimerBlock,
             intermediatePositive, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dCameraSpecial5CommonContinue &&
             intermediatePositive.r[0] == 2U &&
             ReadObject<std::int16_t>(memory, timerAddress) == 2,
         "Camera_Special5 positive timer was not held");

  WriteObject(memory, timerAddress, std::int16_t{0});
  auto intermediateZero = buildState(0);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraSpecial5TimerBlock,
             intermediateZero, memory, &result,
             {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dCameraSpecial5CommonContinue &&
             ReadObject<std::int16_t>(memory, timerAddress) == 0,
         "Camera_Special5 zero transition was not deferred");

  auto logicalZero = buildState(0);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraSpecial5TimerBlock, logicalZero,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dCameraSpecial5ZeroTransition &&
             logicalZero.r[0] == 0U && logicalZero.r[2] == 0U &&
             ReadObject<std::int16_t>(memory, timerAddress) == 0,
         "Camera_Special5 zero transition did not retain its native body");

  WriteObject(memory, timerAddress, std::int16_t{-1});
  auto terminalState = buildState(-1);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraSpecial5TimerBlock, terminalState,
             memory, &result, {1.0F, &intermediateFrame}, &blocksConsumed) &&
             result.pc ==
                 Oot3dNativeGame::kOot3dCameraSpecial5CommonContinue &&
             terminalState.r[0] == 0xFFFFFFFFU &&
             ReadObject<std::int16_t>(memory, timerAddress) == -1,
         "Camera_Special5 terminal sentinel was not retained");

  auto invalidState = buildState(-1);
  invalidState.cpsr &= ~oot3d::recomp::a32::kFlagC;
  const auto incomingInvalidState = invalidState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraSpecial5TimerBlock, invalidState,
             memory, &result, {1.0F, &logicalFrame}, &blocksConsumed) &&
             invalidState.r == incomingInvalidState.r &&
             invalidState.vfp == incomingInvalidState.vfp &&
             invalidState.cpsr == incomingInvalidState.cpsr &&
             ReadObject<std::int16_t>(memory, timerAddress) == -1,
         "invalid Camera_Special5 flags were destructive");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 5U && stats.CameraCalls == 5U &&
             stats.CameraSpecial5TimerBlockCalls == 5U &&
             stats.CameraSpecial5TimerLogicalDecrements == 1U &&
             stats.CameraSpecial5TimerIntermediateHolds == 2U &&
             stats.CameraSpecial5TimerZeroTransitions == 1U &&
             stats.CameraSpecial5TimerTerminalContinues == 1U &&
             stats.RetainedAotFallbacks == 1U,
         "Camera_Special5 timer telemetry mismatch");
}

void TestCameraWaterDistortionFractionalSampling() {
  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(11.0, 11.5);
  const auto logicalFrame = CutsceneTime(11.5, 12.0);
  const std::uint32_t timerAddress = kCameraAddress + 0x198U;
  WriteObject(memory, timerAddress, std::int16_t{79});

  const auto buildSampleState = [](std::uint32_t entry) {
    auto state = BuildState(entry);
    state.r[4] = kCameraAddress;
    state.r[5] = kCameraAddress + 0x100U;
    state.vfp[16] = std::bit_cast<std::uint32_t>(0.125F);
    return state;
  };
  const auto lane = [](const oot3d::recomp::a32::GuestState &state,
                       std::size_t index) {
    return std::bit_cast<float>(state.vfp[index]);
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;

  auto flag4State = buildSampleState(
      Oot3dNativeGame::kOot3dCameraWaterDistortionFlag4SampleBlock);
  flag4State.r[1] = 0x4U;
  flag4State.vfp[19] = std::bit_cast<std::uint32_t>(0.75F);
  const std::uint32_t flag4IncomingS16 = flag4State.vfp[16];
  const std::uint32_t flag4IncomingS19 = flag4State.vfp[19];
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraWaterDistortionFlag4SampleBlock,
             flag4State, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed),
         "flag-4 water distortion sample retained AOT unexpectedly");
  Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
             result.pc ==
                 Oot3dNativeGame::kOot3dCameraWaterDistortionSampleContinue &&
             blocksConsumed == 1U && flag4State.r[0] == 79U &&
             std::abs(lane(flag4State, 2U) - 78.5F) < 1.0e-6F &&
             std::abs(lane(flag4State, 16U) -
                      78.5F * 0.01111111138015985489F) <
                 1.0e-6F &&
             flag4State.vfp[18] == flag4IncomingS19 &&
             flag4State.vfp[19] == flag4IncomingS16 &&
             flag4State.vfp[20] == flag4IncomingS16 &&
             flag4State.vfp[21] == flag4IncomingS16 &&
             flag4State.vfp[22] == flag4IncomingS16 &&
             flag4State.vfp[26] == flag4IncomingS16,
         "flag-4 water distortion VFP state mismatch");

  auto flag8State = buildSampleState(
      Oot3dNativeGame::kOot3dCameraWaterDistortionFlag8SampleBlock);
  flag8State.r[1] = 0x8U;
  const std::uint32_t flag8IncomingS16 = flag8State.vfp[16];
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraWaterDistortionFlag8SampleBlock,
             flag8State, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed),
         "flag-8 water distortion sample retained AOT unexpectedly");
  Expect(result.pc ==
                 Oot3dNativeGame::kOot3dCameraWaterDistortionSampleContinue &&
             flag8State.r[0] == 79U &&
             std::abs(lane(flag8State, 16U) -
                      78.5F * 0.00833333376795053482F) <
                 1.0e-6F &&
             std::abs(lane(flag8State, 26U) - 165.3333282470703125F) <
                 1.0e-6F &&
             lane(flag8State, 0U) == -60.0F &&
             flag8State.vfp[20] == flag8IncomingS16 &&
             flag8State.vfp[21] == flag8IncomingS16 &&
             flag8State.vfp[22] == flag8IncomingS16,
         "flag-8 water distortion VFP state mismatch");

  auto customState = buildSampleState(
      Oot3dNativeGame::kOot3dCameraWaterDistortionCustomSampleBlock);
  customState.r[0] = 100U;
  customState.vfp[1] = std::bit_cast<std::uint32_t>(-0.01F);
  customState.vfp[2] = 30U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraWaterDistortionCustomSampleBlock,
             customState, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed),
         "custom water distortion sample retained AOT unexpectedly");
  Expect(result.pc ==
                 Oot3dNativeGame::kOot3dCameraWaterDistortionSampleContinue &&
             customState.r[0] == 100U && customState.r[1] == 79U &&
             std::abs(lane(customState, 1U) - 78.5F) < 1.0e-6F &&
             std::abs(lane(customState, 2U) - 100.0F) < 1.0e-6F &&
             std::abs(lane(customState, 16U) - 0.785F) < 1.0e-6F &&
             std::abs(lane(customState, 24U) + 0.3F) < 1.0e-6F,
         "custom water distortion VFP state mismatch");

  auto logicalState = buildSampleState(
      Oot3dNativeGame::kOot3dCameraWaterDistortionFlag8SampleBlock);
  logicalState.r[1] = 0x8U;
  const auto incomingLogicalState = logicalState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraWaterDistortionFlag8SampleBlock,
             logicalState, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             logicalState.r == incomingLogicalState.r &&
             logicalState.vfp == incomingLogicalState.vfp &&
             logicalState.cpsr == incomingLogicalState.cpsr &&
             logicalState.fpscr == incomingLogicalState.fpscr,
         "logical water distortion frame replaced its native VFP path");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 3U && stats.CameraCalls == 3U &&
             stats.CameraWaterDistortionFractionalSamples == 3U &&
             stats.CameraWaterDistortionFlag4Samples == 1U &&
             stats.CameraWaterDistortionFlag8Samples == 1U &&
             stats.CameraWaterDistortionCustomSamples == 1U &&
             stats.CameraWaterDistortionSampleFailures == 0U &&
             stats.RetainedAotFallbacks == 0U,
         "water distortion fractional-sample telemetry mismatch");
}

void TestCameraQuakeLifecycle() {
  using oot3d::gameplay::CameraQuakeCallback;
  using oot3d::gameplay::CameraQuakeRequestWire;

  auto memory = BuildMemory(1, 0U);
  const auto intermediateFrame = CutsceneTime(11.0, 11.5);
  const auto logicalFrame = CutsceneTime(11.5, 12.0);
  constexpr std::uint32_t initialSeed = 0x12345678U;
  WriteU32(memory, kCameraQuakeRandomStateAddress, initialSeed);
  WriteU32(memory, kCameraQuakeRandomStateAddress + 4U, 0U);

  CameraQuakeRequestWire request;
  request.RequestId = 0x1234;
  request.InitialCountdown = 3;
  request.CameraAddress = kCameraAddress;
  request.CallbackIndex =
      static_cast<std::uint8_t>(CameraQuakeCallback::SineRandom);
  request.Speed = 1;
  request.Countdown = 3;
  WriteObject(memory, kCameraQuakeRequestArrayAddress, request);
  std::array<std::uint8_t, 0x20> zeroOutput{};
  WriteObject(memory, kCameraQuakeOutputAddress, zeroOutput);

  const auto buildCallbackState = [] {
    auto state = BuildState(
        Oot3dNativeGame::kOot3dCameraQuakeSineRandomCallbackEntry);
    state.r[0] = kCameraQuakeRequestArrayAddress;
    state.r[1] = kCameraQuakeOutputAddress;
    state.r[14] =
        Oot3dNativeGame::kOot3dCameraQuakeCallbackReturnBoundary;
    return state;
  };
  const auto lane = [](const oot3d::recomp::a32::GuestState &state,
                       std::size_t index) {
    return std::bit_cast<float>(state.vfp[index]);
  };

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;
  auto state = buildCallbackState();
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraQuakeSineRandomCallbackEntry,
             state, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed),
         "logical camera quake callback retained AOT unexpectedly");

  const auto firstRandom =
      oot3d::gameplay::AdvanceCameraQuakeRandom(initialSeed);
  constexpr float expectedSine =
      0.381234735F + 0.022104263F * (3.0F / 256.0F);
  Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
             result.pc == 0x00369D44U && blocksConsumed == 1U &&
             state.r[14] ==
                 Oot3dNativeGame::kOot3dCameraQuakeCallbackReturnBoundary &&
             std::abs(lane(state, 0U) - expectedSine) < 1.0e-6F &&
             std::abs(lane(state, 1U) -
                      expectedSine * firstRandom.Value) < 1.0e-6F &&
             ReadObject<CameraQuakeRequestWire>(
                 memory, kCameraQuakeRequestArrayAddress)
                     .Countdown == 2 &&
             ReadObject<std::uint32_t>(
                 memory, kCameraQuakeRandomStateAddress) == firstRandom.Seed,
         "logical camera quake callback state mismatch");

  state.r[0] = 0xDEADBEEFU;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraQuakeCallbackReturnBoundary,
             state, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             result.pc == 0x00478904U && state.r[0] == 2U &&
             (state.cpsr & oot3d::recomp::a32::kFlagC) != 0U &&
             (state.cpsr & oot3d::recomp::a32::kFlagZ) == 0U,
         "logical camera quake return mismatch");

  const std::uint32_t seedAfterLogical =
      ReadObject<std::uint32_t>(memory, kCameraQuakeRandomStateAddress);
  state = buildCallbackState();
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraQuakeSineRandomCallbackEntry,
             state, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             result.pc == 0x00369D44U &&
             ReadObject<CameraQuakeRequestWire>(
                 memory, kCameraQuakeRequestArrayAddress)
                     .Countdown == 2 &&
             ReadObject<std::uint32_t>(
                 memory, kCameraQuakeRandomStateAddress) ==
                 seedAfterLogical &&
             std::abs(lane(state, 1U) -
                      expectedSine * firstRandom.Value) < 1.0e-6F,
         "intermediate camera quake did not reuse its logical RNG sample");
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraQuakeCallbackReturnBoundary,
             state, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             state.r[0] == 2U && result.pc == 0x00478904U,
         "intermediate camera quake return mismatch");

  request.Countdown = 1;
  WriteObject(memory, kCameraQuakeRequestArrayAddress, request);
  state = buildCallbackState();
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraQuakeSineRandomCallbackEntry,
             state, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             ReadObject<CameraQuakeRequestWire>(
                 memory, kCameraQuakeRequestArrayAddress)
                     .Countdown == 0,
         "camera quake terminal logical decrement mismatch");
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraQuakeCallbackReturnBoundary,
             state, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             state.r[0] == 0U && result.pc == 0x004788E4U &&
             (state.cpsr & oot3d::recomp::a32::kFlagZ) != 0U &&
             (state.cpsr & oot3d::recomp::a32::kFlagC) != 0U,
         "camera quake terminal return mismatch");

  request.Countdown = 2;
  WriteObject(memory, kCameraQuakeRequestArrayAddress, request);
  const std::uint32_t seedBeforeCacheMiss =
      ReadObject<std::uint32_t>(memory, kCameraQuakeRandomStateAddress);
  Oot3dNativeGame::ResetOot3dTypedGameplayTransientState();
  state = buildCallbackState();
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraQuakeSineRandomCallbackEntry,
             state, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             result.pc == 0x00478904U && state.r[0] == 2U &&
             ReadObject<std::uint32_t>(
                 memory, kCameraQuakeRandomStateAddress) ==
                 seedBeforeCacheMiss,
         "camera quake cache miss consumed gameplay RNG");

  request.CallbackIndex =
      static_cast<std::uint8_t>(CameraQuakeCallback::Random);
  WriteObject(memory, kCameraQuakeRequestArrayAddress, request);
  const auto invalidState = buildCallbackState();
  state = invalidState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraQuakeSineRandomCallbackEntry,
             state, memory, &result, {1.0F, &logicalFrame},
             &blocksConsumed) &&
             state.r == invalidState.r && state.vfp == invalidState.vfp &&
             state.cpsr == invalidState.cpsr,
         "camera quake callback mismatch was not a non-destructive fallback");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 4U && stats.CameraCalls == 4U &&
             stats.CameraQuakeCallbackCalls == 4U &&
             stats.CameraQuakeLogicalAdvances == 2U &&
             stats.CameraQuakeIntermediateHolds == 2U &&
             stats.CameraQuakeRandomSamples == 2U &&
             stats.CameraQuakeSignalReuses == 1U &&
             stats.CameraQuakeSignalCacheMisses == 1U &&
             stats.CameraQuakeHelperDispatches == 3U &&
             stats.CameraQuakeReturnDispatches == 4U &&
             stats.CameraQuakeFailures == 1U &&
             stats.RetainedAotFallbacks == 1U,
         "camera quake lifecycle telemetry mismatch");
}

oot3d::recomp::a32::GuestState ConfigureCutsceneFrameBlock(
    NativeA32Memory &memory, std::uint16_t frame,
    std::uint32_t commandStreamAddress) {
  auto state =
      BuildState(Oot3dNativeGame::kOot3dCutsceneNormalFrameAdvanceBlock);
  state.r[0] = 0x10101010U;
  state.r[1] = 0x11111111U;
  state.r[2] = 0x12121212U;
  state.r[3] = 0x13131313U;
  state.r[4] = kCutsceneContextAddress;
  state.r[5] = kPlayAddress + 0x2000U;
  state.r[6] = 0x16161616U;
  state.r[7] = kPlayAddress;
  state.r[8] = 0x18181818U;
  state.r[9] = 0x19191919U;
  state.r[10] = 0x1A1A1A1AU;
  state.r[11] = 0x1B1B1B1BU;
  state.r[12] = 0x1C1C1C1CU;
  state.cpsr = oot3d::recomp::a32::kFlagN |
               oot3d::recomp::a32::kFlagC | 0x13U;

  constexpr std::array savedRegisters{
      0x44444444U,
      0x55555555U,
      0x66666666U,
      0x77777777U,
      0x88888888U,
      kReturnSentinel,
  };
  state.r[13] =
      kStackTop -
      static_cast<std::uint32_t>(savedRegisters.size() * sizeof(std::uint32_t));
  WriteObject(memory, state.r[13], savedRegisters);
  WriteObject(memory, kCutsceneContextAddress + 0x20U, frame);
  WriteU32(memory, kPlayAddress + 0x229CU, commandStreamAddress);
  return state;
}

void ExpectCutsceneDispatchAbiEqual(
    const oot3d::recomp::a32::GuestState &actual,
    const oot3d::recomp::a32::GuestState &expected) {
  for (std::size_t index = 0U; index < 15U; ++index) {
    Expect(actual.r[index] == expected.r[index],
           "typed/A32 cutscene register mismatch at r" +
               std::to_string(index));
  }
  Expect(actual.cpsr == expected.cpsr,
         "typed/A32 cutscene CPSR mismatch");
}

void TestCutsceneFrameOwnerDifferential() {
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  constexpr std::array processCommandsEntry{
      Oot3dNativeGame::kOot3dCutsceneProcessCommandsEntry};
  const auto logicalFrame = CutsceneTime(8.0, 9.0);

  for (const std::uint16_t initialFrame :
       {std::uint16_t{0U}, std::uint16_t{41U}, std::uint16_t{0xFFFFU}}) {
    const std::uint32_t commandStreamAddress =
        initialFrame == 41U ? 0U : kCutsceneCommandStreamAddress;
    auto referenceMemory = BuildMemory(2, 0U);
    auto referenceState = ConfigureCutsceneFrameBlock(
        referenceMemory, initialFrame, commandStreamAddress);
    CutsceneProcessCapture referenceCapture;
    const auto referenceResult = oot3d::recomp::a32::Dispatch(
        oot3d::recomp::GetA32GeneratedRegistry(),
        Oot3dNativeGame::kOot3dCutsceneNormalFrameAdvanceBlock,
        referenceState, referenceMemory, nullptr, nullptr, 40'000U, nullptr,
        nullptr, nullptr, 0U, &CaptureCutsceneProcessCommands,
        &referenceCapture, processCommandsEntry.data(),
        processCommandsEntry.size());
    Expect(referenceCapture.Calls == 1U &&
               referenceResult.pc == kReturnSentinel &&
               referenceResult.kind ==
                   oot3d::recomp::a32::ExitKind::MissingBlock,
           "A32 cutscene frame block did not tail-call command processing");

    auto typedMemory = BuildMemory(2, 0U);
    auto typedState = ConfigureCutsceneFrameBlock(
        typedMemory, initialFrame, commandStreamAddress);
    oot3d::recomp::a32::ExecutionResult typedResult;
    std::uint32_t blocksConsumed = 0U;
    Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
               Oot3dNativeGame::kOot3dCutsceneNormalFrameAdvanceBlock,
               typedState, typedMemory, &typedResult,
               {2.0F, &logicalFrame}, &blocksConsumed),
           "typed cutscene frame block retained AOT unexpectedly");
    Expect(typedResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
               typedResult.pc ==
                   Oot3dNativeGame::kOot3dCutsceneProcessCommandsEntry &&
               blocksConsumed == 1U,
           "typed cutscene frame block returned an invalid command tail");
    ExpectCutsceneDispatchAbiEqual(typedState, referenceCapture.State);
    Expect(ReadObject<std::uint16_t>(typedMemory,
                                    kCutsceneContextAddress + 0x20U) ==
               ReadObject<std::uint16_t>(referenceMemory,
                                         kCutsceneContextAddress + 0x20U),
           "typed/A32 cutscene frame field mismatch");
  }

  auto intermediateMemory = BuildMemory(1, 0U);
  auto intermediateState = ConfigureCutsceneFrameBlock(
      intermediateMemory, 41U, kCutsceneCommandStreamAddress);
  const auto incomingState = intermediateState;
  const auto intermediateFrame = CutsceneTime(9.0, 9.5);
  oot3d::recomp::a32::ExecutionResult intermediateResult;
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCutsceneNormalFrameAdvanceBlock,
             intermediateState, intermediateMemory, &intermediateResult,
             {1.0F, &intermediateFrame}, &blocksConsumed),
         "typed cutscene intermediate frame retained AOT unexpectedly");
  Expect(intermediateResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
             intermediateResult.pc ==
                 Oot3dNativeGame::kOot3dCutsceneFrameAdvanceEpilogue &&
             blocksConsumed == 1U,
         "typed cutscene intermediate frame did not select the epilogue");
  for (std::size_t index = 0U; index < 15U; ++index) {
    Expect(intermediateState.r[index] == incomingState.r[index],
           "cutscene intermediate frame changed guest ABI state");
  }
  Expect(ReadObject<std::uint16_t>(intermediateMemory,
                                  kCutsceneContextAddress + 0x20U) == 41U,
         "cutscene intermediate frame changed the legacy frame");

  const auto epilogueResult = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), intermediateResult.pc,
      intermediateState, intermediateMemory, nullptr, nullptr, 20U);
  Expect(epilogueResult.pc == kReturnSentinel &&
             epilogueResult.kind ==
                 oot3d::recomp::a32::ExitKind::MissingBlock &&
             intermediateState.r[13] == kStackTop,
         "cutscene intermediate epilogue did not restore its native frame");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.CutsceneCalls == 4U &&
             stats.CutsceneNormalFrameBlockCalls == 4U &&
             stats.CutsceneLogicalFrameAdvances == 3U &&
             stats.CutsceneIntermediateHolds == 1U &&
             stats.CutsceneCommandDispatches == 3U,
         "typed cutscene frame-owner telemetry mismatch");
}

void TestCutsceneActorCueFractionalInterpolation() {
  using oot3d::gameplay::CutsceneActorCueWire;

  auto memory = BuildMemory(1, 0U);
  constexpr std::uint32_t cueIndex = 2U;
  const CutsceneActorCueWire cue{
      .Action = 3U,
      .StartFrame = 10U,
      .EndFrame = 14U,
      .StartX = 100,
      .StartY = 20,
      .StartZ = -40,
      .EndX = 140,
      .EndY = 60,
      .EndZ = 80,
  };
  WriteObject(memory, kCutsceneActorCueAddress, cue);
  WriteU32(memory, kPlayAddress + 0x22DCU + cueIndex * 4U,
           kCutsceneActorCueAddress);
  WriteObject(memory, kPlayAddress + 0x22B8U, std::uint16_t{11U});

  auto state = BuildState(
      Oot3dNativeGame::
          kOot3dEnvironmentPathInterpolateActorPosAndRotationEntry);
  state.r[1] = kPlayAddress;
  state.r[2] = cueIndex;
  state.r[3] = 0U;
  state.r[4] = 0x44444444U;
  state.r[5] = 0x55555555U;
  for (std::size_t lane = 0U; lane < 6U; ++lane) {
    state.vfp[16U + lane] =
        0x3F000000U + static_cast<std::uint32_t>(lane);
  }
  const auto incomingState = state;
  const auto intermediateFrame = CutsceneTime(9.0, 9.5);

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  oot3d::recomp::a32::ExecutionResult typedResult;
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::
                 kOot3dEnvironmentPathInterpolateActorPosAndRotationEntry,
             state, memory, &typedResult, {1.0F, &intermediateFrame},
             &blocksConsumed),
         "fractional actor cue sample retained AOT unexpectedly");
  Expect(typedResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
             typedResult.pc ==
                 Oot3dNativeGame::
                     kOot3dEnvironmentPathInterpolationContinue &&
             blocksConsumed == 1U,
         "fractional actor cue sample selected an invalid continuation");

  const auto continuationResult = oot3d::recomp::a32::Dispatch(
      oot3d::recomp::GetA32GeneratedRegistry(), typedResult.pc, state, memory,
      nullptr, nullptr, 200U);
  Expect(continuationResult.pc == kReturnSentinel &&
             continuationResult.kind ==
                 oot3d::recomp::a32::ExitKind::MissingBlock,
         "fractional actor cue continuation did not return through native ABI");
  Expect(std::abs(ReadObject<float>(memory, kActorAddress + 0x28U) - 115.0F) <
                 1.0e-6F &&
             std::abs(ReadObject<float>(memory, kActorAddress + 0x2CU) -
                      35.0F) <
                 1.0e-6F &&
             std::abs(ReadObject<float>(memory, kActorAddress + 0x30U) - 5.0F) <
                 1.0e-6F,
         "fractional actor cue position mismatch");
  Expect(state.r[13] == kStackTop && state.r[4] == incomingState.r[4] &&
             state.r[5] == incomingState.r[5],
         "fractional actor cue native stack frame was not restored");
  for (std::size_t lane = 0U; lane < 6U; ++lane) {
    Expect(state.vfp[16U + lane] == incomingState.vfp[16U + lane],
           "fractional actor cue did not restore a callee-saved VFP lane");
  }

  auto logicalState = incomingState;
  const auto logicalFrame = CutsceneTime(9.0, 10.0);
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::
                 kOot3dEnvironmentPathInterpolateActorPosAndRotationEntry,
             logicalState, memory, &typedResult, {2.0F, &logicalFrame},
             &blocksConsumed),
         "logical actor cue sample replaced the original A32 body");
  Expect(logicalState.r == incomingState.r &&
             logicalState.vfp == incomingState.vfp &&
             logicalState.cpsr == incomingState.cpsr &&
             logicalState.fpscr == incomingState.fpscr,
         "logical actor cue fallback changed guest state");

  WriteObject(memory, kPlayAddress + 0x6028U, std::uint8_t{1U});
  WriteObject(memory, kPlayAddress + 0x0101U, std::uint8_t{2U});
  WriteObject(memory, kPlayAddress + 0x6029U, std::uint8_t{1U});
  WriteObject(memory, kPlayAddress + 0x7C60U, std::int32_t{30});
  auto backendClockState = incomingState;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::
                 kOot3dEnvironmentPathInterpolateActorPosAndRotationEntry,
             backendClockState, memory, &typedResult,
             {1.0F, &intermediateFrame}, &blocksConsumed),
         "backend-clock actor cue was sampled from the normal frame cursor");
  Expect(backendClockState.r == incomingState.r &&
             backendClockState.vfp == incomingState.vfp,
         "backend-clock actor cue fallback changed guest state");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.CutsceneActorCueInterpolationCalls == 1U &&
             stats.CutsceneActorCueFractionalSamples == 1U,
         "fractional actor cue telemetry mismatch");
}

void TestCutsceneCameraFractionalSampling() {
  using oot3d::gameplay::CameraAnimationCmadContainerHeaderWire;
  using oot3d::gameplay::CameraAnimationCmadRecordWire;
  using oot3d::gameplay::CameraAnimationDefaultsWire;
  using oot3d::gameplay::CameraAnimationResourcePairWire;
  using oot3d::gameplay::CameraAnimationStateWire;
  using oot3d::gameplay::CameraCurveHeaderWire;
  using oot3d::gameplay::CameraCurveType;
  using oot3d::gameplay::CameraLinearKeyframeWire;
  using oot3d::gameplay::CameraDemo1Wire;

  auto memory = BuildMemory(1, 0U);
  const std::uint32_t outputAddress = kPlayAddress + 0x232CU;
  const auto writeLinearCurve =
      [&memory](std::uint32_t address, float first, float second) {
        const CameraCurveHeaderWire header{
            .Type = static_cast<std::uint8_t>(CameraCurveType::Linear),
            .PointCount = 2,
            .LoopEndFrame = 11,
        };
        const std::array points{
            CameraLinearKeyframeWire{10, first},
            CameraLinearKeyframeWire{11, second},
        };
        WriteObject(memory, address, header);
        WriteObject(memory, address + sizeof(header), points);
      };

  const CameraAnimationDefaultsWire defaults{
      .Field8C = -10.0F,
      .Field90 = -20.0F,
      .Field94 = -30.0F,
      .Field80 = 10.0F,
      .Field84 = 20.0F,
      .Field88 = 30.0F,
      .Field1A2Source = 0.0F,
      .Field144Source = 1.0F,
      .FieldD0 = 55.0F,
  };
  const CameraAnimationResourcePairWire resources{
      .Defaults = {kCameraDefaultsAddress},
      .CmadContainer = {kCameraCmadContainerAddress},
  };
  const CameraAnimationCmadContainerHeaderWire container{
      .RecordCount = 1,
  };
  const CameraAnimationCmadRecordWire record{
      .Type = 1U,
      .CurveOffsets = {0x20, 0x40, 0x60},
  };
  WriteObject(memory, kCameraDefaultsAddress, defaults);
  WriteObject(memory, kCameraResourcePairAddress, resources);
  WriteObject(memory, kCameraCmadContainerAddress, container);
  WriteU32(memory, kCameraCmadContainerAddress + 0x08U,
           kCameraCmadRecordAddress - kCameraCmadContainerAddress);
  WriteObject(memory, kCameraCmadRecordAddress, record);
  writeLinearCurve(kCameraCmadRecordAddress + 0x20U, 1.0F, 3.0F);
  writeLinearCurve(kCameraCmadRecordAddress + 0x40U, 2.0F, 4.0F);
  writeLinearCurve(kCameraCmadRecordAddress + 0x60U, -1.0F, 1.0F);
  WriteU32(memory, kPlayAddress + 0x229CU,
           kCutsceneCommandStreamAddress);
  WriteU32(memory, kMainCutsceneCameraLocalFrameAddress, 10U);

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  auto applyState = BuildState(
      Oot3dNativeGame::kOot3dCameraAnimationApplyFrameEntry);
  applyState.r[0] = kCameraResourcePairAddress;
  applyState.r[1] = 10U;
  applyState.r[2] = outputAddress;
  const auto incomingApplyState = applyState;
  const auto logicalFrame = CutsceneTime(9.0, 10.0);
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraAnimationApplyFrameEntry,
             applyState, memory, &result, {2.0F, &logicalFrame},
             &blocksConsumed),
         "camera animation observer replaced the original A32 body");
  Expect(applyState.r == incomingApplyState.r &&
             applyState.vfp == incomingApplyState.vfp &&
             applyState.cpsr == incomingApplyState.cpsr &&
             applyState.fpscr == incomingApplyState.fpscr,
         "camera animation observer changed guest state");

  WriteU32(memory,
           kCameraAddress +
               offsetof(CameraDemo1Wire, AttachedAnimationState),
           outputAddress);
  WriteObject(memory,
              kCameraAddress + offsetof(CameraDemo1Wire, AnimationFlags),
              std::uint16_t{4U});
  WriteFloat(memory,
             outputAddress + offsetof(CameraAnimationStateWire, Field8C),
             -999.0F);

  auto demoState = BuildState(Oot3dNativeGame::kOot3dCameraDemo1Entry);
  demoState.r[0] = kCameraAddress;
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraDemo1Entry, demoState, memory,
             &result, {1.0F, &intermediateFrame}, &blocksConsumed),
         "camera fractional pre-sample replaced Camera_Demo1");
  Expect(std::abs(ReadObject<float>(
                      memory,
                      outputAddress +
                          offsetof(CameraAnimationStateWire, Field8C)) -
                  80.0F) <
                 1.0e-5F &&
             std::abs(ReadObject<float>(
                          memory,
                          outputAddress +
                              offsetof(CameraAnimationStateWire, Field90)) -
                      120.0F) <
                 1.0e-5F &&
             std::abs(ReadObject<float>(
                          memory,
                          outputAddress +
                              offsetof(CameraAnimationStateWire, Field94))) <
                 1.0e-5F,
         "camera fractional position sample mismatch");
  Expect(ReadObject<float>(
             memory,
             outputAddress + offsetof(CameraAnimationStateWire, Field80)) ==
             defaults.Field80 &&
             ReadObject<float>(
                 memory,
                 outputAddress +
                     offsetof(CameraAnimationStateWire, FieldD0)) ==
                 defaults.FieldD0 &&
             ReadObject<std::int16_t>(
                 memory,
                 outputAddress +
                     offsetof(CameraAnimationStateWire, Field1A2)) == 0,
         "camera fractional default state mismatch");

  WriteFloat(memory,
             outputAddress + offsetof(CameraAnimationStateWire, Field8C),
             -123.0F);
  const auto nextLogicalFrame = CutsceneTime(10.5, 11.0);
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraDemo1Entry, demoState, memory,
             &result, {1.0F, &nextLogicalFrame}, &blocksConsumed),
         "logical Camera_Demo1 sample replaced its original A32 body");
  Expect(ReadObject<float>(
             memory,
             outputAddress + offsetof(CameraAnimationStateWire, Field8C)) ==
             -123.0F,
         "logical Camera_Demo1 sample overwrote the native frame");

  Oot3dNativeGame::ResetOot3dTypedGameplayTransientState();
  WriteFloat(memory,
             outputAddress + offsetof(CameraAnimationStateWire, Field8C),
             -321.0F);
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraDemo1Entry, demoState, memory,
             &result, {1.0F, &intermediateFrame}, &blocksConsumed) &&
             ReadObject<float>(
                 memory,
                 outputAddress +
                     offsetof(CameraAnimationStateWire, Field8C)) == -321.0F,
         "transient reset retained a stale camera binding");

  auto actorOwnedState = incomingApplyState;
  actorOwnedState.r[2] = 0x0059BB20U;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraAnimationApplyFrameEntry,
             actorOwnedState, memory, &result, {2.0F, &logicalFrame},
             &blocksConsumed),
         "actor-owned camera observation replaced the original A32 body");
  auto primingState = incomingApplyState;
  primingState.r[2] = kActorCutsceneCameraPrimingStateAddress;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraAnimationApplyFrameEntry,
             primingState, memory, &result, {2.0F, &logicalFrame},
             &blocksConsumed),
         "actor-owned camera priming observation replaced the A32 body");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.CutsceneCameraBindingObservations == 1U &&
             stats.CutsceneCameraRejectedObservations == 0U &&
             stats.CutsceneCameraFractionalSamples == 1U &&
             stats.CutsceneCameraCurveSamples == 3U &&
             stats.CutsceneCameraSampleFailures == 0U &&
             stats.ActorCutsceneCameraPrimingObservations == 1U &&
             stats.ActorCutsceneCameraRejectedObservations == 1U,
         "cutscene camera telemetry mismatch");
}

void TestActorCutsceneCameraFractionalSampling() {
  using oot3d::gameplay::ActorCutsceneCameraOwnerWire;
  using oot3d::gameplay::CameraAnimationCmadContainerHeaderWire;
  using oot3d::gameplay::CameraAnimationCmadRecordWire;
  using oot3d::gameplay::CameraAnimationDefaultsWire;
  using oot3d::gameplay::CameraAnimationResourcePairWire;
  using oot3d::gameplay::CameraAnimationStateWire;
  using oot3d::gameplay::CameraCurveHeaderWire;
  using oot3d::gameplay::CameraCurveType;
  using oot3d::gameplay::CameraDemo1Wire;
  using oot3d::gameplay::CameraLinearKeyframeWire;
  using oot3d::gameplay::CameraPlayStateWire;

  auto memory = BuildMemory(1, 0U);
  const auto writeLinearCurve =
      [&memory](std::uint32_t address, float first, float second) {
        const CameraCurveHeaderWire header{
            .Type = static_cast<std::uint8_t>(CameraCurveType::Linear),
            .PointCount = 2,
            .LoopEndFrame = 11,
        };
        const std::array points{
            CameraLinearKeyframeWire{10, first},
            CameraLinearKeyframeWire{11, second},
        };
        WriteObject(memory, address, header);
        WriteObject(memory, address + sizeof(header), points);
      };

  const CameraAnimationDefaultsWire defaults{
      .SegmentEndFrame = 11,
      .Field8C = -10.0F,
      .Field90 = -20.0F,
      .Field94 = -30.0F,
      .Field80 = 10.0F,
      .Field84 = 20.0F,
      .Field88 = 30.0F,
      .Field1A2Source = 0.0F,
      .Field144Source = 1.0F,
      .FieldD0 = 55.0F,
  };
  const CameraAnimationResourcePairWire resources{
      .Defaults = {kCameraDefaultsAddress},
      .CmadContainer = {kCameraCmadContainerAddress},
  };
  const CameraAnimationCmadContainerHeaderWire container{
      .RecordCount = 1,
  };
  const CameraAnimationCmadRecordWire record{
      .Type = 1U,
      .CurveOffsets = {0x20, 0x40, 0x60},
  };
  const ActorCutsceneCameraOwnerWire owner{
      .NativeFrameCursor = 10,
      .AnimationIndex = 2,
      .Resources = resources,
  };
  WriteObject(memory, kActorCutsceneCameraOwnerAddress, owner);
  WriteObject(memory, kCameraDefaultsAddress, defaults);
  WriteObject(memory, kCameraCmadContainerAddress, container);
  WriteU32(memory, kCameraCmadContainerAddress + 0x08U,
           kCameraCmadRecordAddress - kCameraCmadContainerAddress);
  WriteObject(memory, kCameraCmadRecordAddress, record);
  writeLinearCurve(kCameraCmadRecordAddress + 0x20U, 1.0F, 3.0F);
  writeLinearCurve(kCameraCmadRecordAddress + 0x40U, 2.0F, 4.0F);
  writeLinearCurve(kCameraCmadRecordAddress + 0x60U, -1.0F, 1.0F);

  WriteU32(memory,
           kPlayAddress + offsetof(CameraPlayStateWire, Cameras),
           kCameraAddress);
  WriteObject(memory,
              kPlayAddress +
                  offsetof(CameraPlayStateWire, ActiveCameraIndex),
              std::int16_t{0});
  WriteU32(memory,
           kCameraAddress +
               offsetof(CameraDemo1Wire, AttachedAnimationState),
           kActorCutsceneCameraStateAddress);
  WriteObject(memory,
              kCameraAddress + offsetof(CameraDemo1Wire, AnimationFlags),
              std::uint16_t{4U});

  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  auto applyState = BuildState(
      Oot3dNativeGame::kOot3dCameraAnimationApplyFrameEntry);
  applyState.r[0] =
      kActorCutsceneCameraOwnerAddress +
      offsetof(ActorCutsceneCameraOwnerWire, Resources);
  applyState.r[1] = 10U;
  applyState.r[2] = kActorCutsceneCameraStateAddress;
  const auto incomingApplyState = applyState;
  const auto logicalFrame = CutsceneTime(9.0, 10.0);
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocksConsumed = 0U;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dCameraAnimationApplyFrameEntry,
             applyState, memory, &result, {2.0F, &logicalFrame},
             &blocksConsumed),
         "actor camera observer replaced CameraAnimation_ApplyFrame");
  Expect(applyState.r == incomingApplyState.r &&
             applyState.vfp == incomingApplyState.vfp &&
             applyState.cpsr == incomingApplyState.cpsr &&
             applyState.fpscr == incomingApplyState.fpscr,
         "actor camera observer changed guest state");

  WriteObject(
      memory,
      kActorCutsceneCameraOwnerAddress +
          offsetof(ActorCutsceneCameraOwnerWire, NativeFrameCursor),
      std::int32_t{11});
  WriteFloat(memory,
             kActorCutsceneCameraStateAddress +
                 offsetof(CameraAnimationStateWire, Field8C),
             -999.0F);

  auto advanceState = BuildState(
      Oot3dNativeGame::kOot3dEnZl4ActorCameraAdvanceBlock);
  advanceState.r[4] = kActorCutsceneCameraOwnerAddress;
  advanceState.r[5] = kPlayAddress + 0xA00U;
  advanceState.r[6] = kPlayAddress;
  advanceState.r[7] = kPlayAddress + 0x20ACU;
  advanceState.r[8] = kCameraAddress;
  const auto intermediateFrame = CutsceneTime(10.0, 10.5);
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnZl4ActorCameraAdvanceBlock,
             advanceState, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed),
         "actor camera intermediate sample fell back to A32");
  Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
             result.pc ==
                 Oot3dNativeGame::kOot3dEnZl4ActorCameraAdvanceContinue &&
             advanceState.r[15] ==
                 Oot3dNativeGame::kOot3dEnZl4ActorCameraAdvanceContinue &&
             blocksConsumed == 1U,
         "actor camera intermediate sample did not skip native advance");
  Expect(ReadObject<std::int32_t>(
             memory,
             kActorCutsceneCameraOwnerAddress +
                 offsetof(ActorCutsceneCameraOwnerWire, NativeFrameCursor)) ==
             11 &&
             ReadObject<std::int32_t>(
                 memory,
                 kActorCutsceneCameraOwnerAddress +
                     offsetof(ActorCutsceneCameraOwnerWire, AnimationIndex)) ==
                 2,
         "actor camera intermediate sample changed native lifecycle state");
  Expect(std::abs(ReadObject<float>(
                      memory,
                      kActorCutsceneCameraStateAddress +
                          offsetof(CameraAnimationStateWire, Field8C)) -
                  80.0F) <
                 1.0e-5F &&
             std::abs(ReadObject<float>(
                          memory,
                          kActorCutsceneCameraStateAddress +
                              offsetof(CameraAnimationStateWire, Field90)) -
                      120.0F) <
                 1.0e-5F &&
             std::abs(ReadObject<float>(
                          memory,
                          kActorCutsceneCameraStateAddress +
                              offsetof(CameraAnimationStateWire, Field94))) <
                 1.0e-5F,
         "actor camera fractional position sample mismatch");

  WriteFloat(memory,
             kActorCutsceneCameraStateAddress +
                 offsetof(CameraAnimationStateWire, Field8C),
             -123.0F);
  const auto nextLogicalFrame = CutsceneTime(10.5, 11.0);
  auto logicalState = advanceState;
  logicalState.r[15] =
      Oot3dNativeGame::kOot3dEnZl4ActorCameraAdvanceBlock;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnZl4ActorCameraAdvanceBlock,
             logicalState, memory, &result, {1.0F, &nextLogicalFrame},
             &blocksConsumed) &&
             ReadObject<float>(
                 memory,
                 kActorCutsceneCameraStateAddress +
                     offsetof(CameraAnimationStateWire, Field8C)) == -123.0F,
         "logical actor camera frame did not retain the original A32 block");

  Oot3dNativeGame::ResetOot3dTypedGameplayTransientState();
  WriteFloat(memory,
             kActorCutsceneCameraStateAddress +
                 offsetof(CameraAnimationStateWire, Field8C),
             -321.0F);
  auto resetState = advanceState;
  resetState.r[15] =
      Oot3dNativeGame::kOot3dEnZl4ActorCameraAdvanceBlock;
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dEnZl4ActorCameraAdvanceBlock,
             resetState, memory, &result, {1.0F, &intermediateFrame},
             &blocksConsumed) &&
             ReadObject<float>(
                 memory,
                 kActorCutsceneCameraStateAddress +
                     offsetof(CameraAnimationStateWire, Field8C)) == -321.0F,
         "transient reset retained a stale actor camera binding");

  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.ActorCutsceneCameraBindingObservations == 1U &&
             stats.ActorCutsceneCameraRejectedObservations == 0U &&
             stats.ActorCutsceneCameraIntermediateHolds == 1U &&
             stats.ActorCutsceneCameraFractionalSamples == 1U &&
             stats.ActorCutsceneCameraCurveSamples == 3U &&
             stats.ActorCutsceneCameraSampleFailures == 0U,
         "actor cutscene camera telemetry mismatch");
}

void TestMeshCommandPacketSubmitDifferential() {
  auto referenceMemory = BuildMemory(2, 0U);
  std::string error;
  Expect(referenceMemory.MapRegion(
             {"pica_command_stats_code", 0x002F9000U, 0x1000U, true, true, {}},
             &error),
         "could not map PICA command stats code: " + error);
  Expect(referenceMemory.MapRegion(
             {"mesh_submit_code", 0x00466000U, 0x1000U, true, true, {}},
             &error),
         "could not map mesh submit code: " + error);
  Expect(referenceMemory.MapRegion(
             {"pica_command_cursor", 0x0054C000U, 0x1000U, true, false, {}},
             &error),
         "could not map PICA command cursor: " + error);
  Expect(referenceMemory.MapRegion(
             {"nngx_command_state", 0x005A6000U, 0x1000U, true, false, {}},
             &error),
         "could not map NNGX command state: " + error);

  WriteU32(referenceMemory, 0x002F9C9CU, kPicaCommandListCursorAddress);
  WriteU32(referenceMemory, 0x002F9CD8U, 0x002F9D6CU);
  WriteU32(referenceMemory, 0x002F9E5CU, 0x005A6F30U);
  WriteU32(referenceMemory, 0x002F9E60U, kPicaCommandListCursorAddress);
  WriteU32(referenceMemory, 0x005A6FCCU, 0U);
  WriteU32(referenceMemory, kPicaCommandListCursorAddress,
           kMeshCommandDestinationAddress);

  constexpr std::uint32_t byteCount = 24U;
  constexpr std::uint32_t activePacketIndex = 8U;
  WriteU32(referenceMemory, kMeshPacketAddress + 0x10U, byteCount);
  WriteU32(referenceMemory, kMeshPacketAddress + 0x14U, activePacketIndex);
  WriteU32(referenceMemory,
           kMeshPacketAddress + activePacketIndex * 4U + 0x08U,
           kMeshCommandSourceAddress);
  const std::array<std::uint32_t, 6> commands{
      0x01020304U, 0x11121314U, 0x21222324U,
      0x31323334U, 0x41424344U, 0x51525354U,
  };
  WriteObject(referenceMemory, kMeshCommandSourceAddress, commands);

  auto typedMemory = referenceMemory;
  auto referenceState =
      BuildState(Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry);
  referenceState.r[0] = kMeshPacketAddress;
  RunReference(Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry,
               referenceState, referenceMemory);

  auto typedState =
      BuildState(Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry);
  typedState.r[0] = kMeshPacketAddress;
  oot3d::recomp::a32::ExecutionResult typedResult;
  std::uint32_t blocksConsumed = 0U;
  Expect(Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry, typedState,
             typedMemory, &typedResult, {2.0f}, &blocksConsumed),
         "typed mesh command submit retained AOT unexpectedly");
  Expect(typedResult.kind == oot3d::recomp::a32::ExitKind::Branch &&
             typedResult.pc == kReturnSentinel && blocksConsumed == 1U,
         "typed mesh command submit returned an invalid ABI result");

  Expect(ReadObject<std::array<std::uint32_t, 6>>(
             typedMemory, kMeshCommandDestinationAddress) ==
             ReadObject<std::array<std::uint32_t, 6>>(
                 referenceMemory, kMeshCommandDestinationAddress),
         "typed/A32 mesh command payload mismatch");
  Expect(ReadObject<std::uint32_t>(typedMemory,
                                  kPicaCommandListCursorAddress) ==
             ReadObject<std::uint32_t>(referenceMemory,
                                       kPicaCommandListCursorAddress),
         "typed/A32 mesh command cursor mismatch");
  Expect(typedState.r[0] == referenceState.r[0],
         "typed/A32 mesh command return register mismatch");
}

void TestFallbackIsNonDestructive() {
  NativeA32Memory memory;
  auto state = BuildState(Oot3dNativeGame::kOot3dActorMoveForwardEntry);
  state.r[0] = 0xDEAD0000U;
  oot3d::recomp::a32::ExecutionResult result;
  Oot3dNativeGame::ResetOot3dTypedGameplayStats();
  Expect(!Oot3dNativeGame::ExecuteOot3dTypedGameplay(
             Oot3dNativeGame::kOot3dActorMoveForwardEntry, state, memory,
             &result, {1.0f}),
         "invalid typed actor pointer did not retain AOT");
  const auto stats = Oot3dNativeGame::GetOot3dTypedGameplayStats();
  Expect(stats.Calls == 0U && stats.RetainedAotFallbacks == 1U &&
             stats.ReadFailures == 1U,
         "typed fallback diagnostics mismatch");
}

void TestEntryCatalog() {
  const auto entries = Oot3dNativeGame::Oot3dTypedGameplayEntryPoints();
  Expect(entries.size() == 131U, "typed gameplay entry count mismatch");
  for (std::size_t i = 1; i < entries.size(); ++i) {
    Expect(entries[i - 1] < entries[i],
           "typed gameplay entries are not sorted");
  }
  const auto observableExits =
      Oot3dNativeGame::Oot3dTypedGameplayObservableExitPoints();
  constexpr std::array actorLifecycleBoundaries{
      Oot3dNativeGame::kOot3dActorDestroyEntry,
      Oot3dNativeGame::kOot3dActorDestroyCallbackReturn,
      Oot3dNativeGame::kOot3dActorDestroyModelContextReturn,
      Oot3dNativeGame::kOot3dActorDestroyOwnedSlotReturn,
      Oot3dNativeGame::kOot3dActorDestroyLastOwnedSlotReturn,
      Oot3dNativeGame::kOot3dActorUpdateAllInitCallbackEntry,
      Oot3dNativeGame::kOot3dActorUpdateAllInitCallbackReturn,
      Oot3dNativeGame::kOot3dActorUpdateAllUpdateCallbackEntry,
  };
  for (const auto boundary : actorLifecycleBoundaries) {
    Expect(std::find(observableExits.begin(), observableExits.end(),
                     boundary) != observableExits.end(),
           "actor lifecycle entry is not a whole-AOT observable boundary");
  }
  constexpr std::array audioRequestCallbackBoundaries{
      Oot3dNativeGame::kOot3dAudioRequestFlag100CallbackEntry,
      Oot3dNativeGame::
          kOot3dAudioRequestFlag100ReferenceAcquireReturn,
      Oot3dNativeGame::kOot3dAudioRequestFlag100StatusQueryReturn,
      Oot3dNativeGame::
          kOot3dAudioRequestFlag100ReferenceCleanupReturn,
  };
  for (const auto boundary : audioRequestCallbackBoundaries) {
    Expect(std::find(observableExits.begin(), observableExits.end(),
                     boundary) != observableExits.end(),
           "audio request callback is not a whole-AOT observable boundary");
  }
  constexpr std::array pauseUiAlphaBoundaries{
      Oot3dNativeGame::kOot3dPauseUiUpdateDualAlphaEntry,
      Oot3dNativeGame::kOot3dPauseUiAlphaPauseStateReturn,
      Oot3dNativeGame::kOot3dPauseUiAlphaFadeOutStepReturn,
      Oot3dNativeGame::kOot3dPauseUiAlphaFadeInStepReturn,
  };
  for (const auto boundary : pauseUiAlphaBoundaries) {
    Expect(std::find(observableExits.begin(), observableExits.end(),
                     boundary) != observableExits.end(),
           "pause UI alpha owner is not a whole-AOT observable boundary");
  }
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::
              kOot3dDynaResetActorInteractionIfRegisteredEntry) !=
          observableExits.end(),
      "Dyna interaction reset is not a whole-AOT observable boundary");
  constexpr std::array recordInitializerBoundaries{
      Oot3dNativeGame::kOot3dRecordInitializerEntry,
      Oot3dNativeGame::kOot3dRecordInitializerMemzeroReturn,
  };
  for (const auto boundary : recordInitializerBoundaries) {
    Expect(std::find(observableExits.begin(), observableExits.end(),
                     boundary) != observableExits.end(),
           "record initializer entry is not a whole-AOT observable boundary");
  }
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dEnKoBlinkAdvanceBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dEnKoBlinkRngReturnBlock) !=
              observableExits.end(),
      "EnKo blink lifecycle is not a whole-AOT observable boundary");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dEnKanbanPhaseAdvanceBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dEnKanbanState0CountdownBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dEnKanbanActorFlagCountdownBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dEnKanbanInteractionCooldownBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dEnKanbanDrawGateRampBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dEnKanbanOscillatorXBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dEnKanbanOscillatorYBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dEnKanbanPieceLifetimeBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dEnKanbanRippleEventGateBlock) !=
              observableExits.end(),
      "EnKanban phase lifecycle is not a whole-AOT observable boundary");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dActorUpdateAllContextFreezeBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dActorUpdateAllInstanceFreezeBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dActorUpdateAllEffectTimersBlock) !=
              observableExits.end(),
      "Actor_UpdateAll timing kernel is not a whole-AOT observable boundary");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dEnZl4ActorCameraAdvanceBlock) !=
          observableExits.end(),
      "actor camera advance is not a whole-AOT observable boundary");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dCameraSpecial5TimerBlock) !=
          observableExits.end(),
      "Camera_Special5 timer is not a whole-AOT observable boundary");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dCameraWaterDistortionTimerAdvanceBlock) !=
          observableExits.end(),
      "water distortion timer is not a whole-AOT observable boundary");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dCameraFloorMissCounterAdvanceBlock) !=
          observableExits.end(),
      "camera floor-miss counter is not a whole-AOT observable boundary");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dCameraInterfaceDelayAdvanceBlock) !=
          observableExits.end(),
      "camera interface delay is not a whole-AOT observable boundary");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dCameraWaterDistortionFlag4SampleBlock) !=
          observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dCameraWaterDistortionFlag8SampleBlock) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::
                  kOot3dCameraWaterDistortionCustomSampleBlock) !=
              observableExits.end(),
      "water distortion consumers are not whole-AOT observable boundaries");
  Expect(
      std::find(
          observableExits.begin(), observableExits.end(),
          Oot3dNativeGame::kOot3dCameraQuakeSineRandomCallbackEntry) !=
              observableExits.end() &&
          std::find(
              observableExits.begin(), observableExits.end(),
              Oot3dNativeGame::kOot3dCameraQuakeCallbackReturnBoundary) !=
              observableExits.end(),
      "camera quake lifecycle is not a whole-AOT observable boundary");
  std::vector<std::uint32_t> nativeCandidates(entries.begin(), entries.end());
  nativeCandidates.insert(nativeCandidates.end(),
                          kSkelAnimeEffectEntries.begin(),
                          kSkelAnimeEffectEntries.end());
  std::sort(nativeCandidates.begin(), nativeCandidates.end());
  nativeCandidates.erase(
      std::unique(nativeCandidates.begin(), nativeCandidates.end()),
      nativeCandidates.end());
  oot3d::recomp::ConfigureA32GeneratedNativeCandidates(nativeCandidates);
}

} // namespace

int main() {
  try {
    TestEntryCatalog();
    TestGameStateUpdateOwnerDifferential();
    TestTypedGameStateCheckpointRoundTrip();
    TestActorInstanceCallbackContractDifferential();
    TestActorDestroyOwnerDifferential();
    TestTypedActorLifecycleCheckpointRoundTrip();
    TestAudioRequestFlag100CallbackDifferential();
    TestTypedAudioRequestCallbackCheckpointRoundTrip();
    TestPauseUiUpdateDualAlphaDifferential();
    TestTypedPauseUiAlphaCheckpointRoundTrip();
    TestDynaInteractionResetDifferential();
    TestTypedDynaInteractionResetCheckpointRoundTrip();
    TestActorOwnerMicroleafDifferential();
    TestTypedActorOwnerMicroleafCheckpointRoundTrip();
    TestRecordInitializerDifferential();
    TestTypedRecordInitializerCheckpointRoundTrip();
    TestFractionalTimeContract();
    TestActorUpdateAllTemporalKernel();
    TestEnKoBlinkTemporalKernel();
    TestEnKanbanPhaseTemporalKernel();
    TestEnKanbanState0CountdownTemporalKernel();
    TestEnKanbanDrawGateRampTemporalKernel();
    TestEnKanbanOscillatorTemporalKernel();
    TestEnKanbanPieceLifetimeTemporalKernel();
    TestCutsceneFrameOwnerDifferential();
    TestCutsceneActorCueFractionalInterpolation();
    TestCutsceneCameraFractionalSampling();
    TestActorCutsceneCameraFractionalSampling();
    TestCameraUpdateLogicalCounters();
    TestCameraModeFrameCountdowns();
    TestCameraSpecial5Timer();
    TestCameraWaterDistortionFractionalSampling();
    TestCameraQuakeLifecycle();
    TestMeshCommandPacketSubmitDifferential();
    TestActorDifferential();
    TestActorLeafDifferential();
    TestSmoothStepDifferential();
    TestStepToFDifferential();
    TestFloatApproachDifferential();
    TestScaledStepToSDifferential();
    TestStepToSDifferential();
    TestStepToAngleSDifferential();
    TestSmoothStepToFDifferential();
    TestSmoothStepToSDifferential();
    TestSkelAnimeSetUpdateDifferential();
    TestAnimationResourceFunctions();
    TestAnimationChangeDifferential();
    TestLinkAnimationPlayDifferential();
    TestLinkAnimationOnFrameDifferential();
    TestAnimationFrameCrossingDifferential();
    TestLinkAnimationChangeDifferential();
    TestSkelAnimeDifferential();
    TestSkelAnimeHelperDifferential();
    TestSkelAnimeHelperFractionalRate();
    TestSkelAnimeFractionalRate();
    TestFallbackIsNonDestructive();
    std::cout << "oot3d typed gameplay tests passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "oot3d typed gameplay tests failed: " << ex.what() << '\n';
    return 1;
  }
}
