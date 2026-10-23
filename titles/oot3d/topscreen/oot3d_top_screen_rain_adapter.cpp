#include "oot3d_top_screen_rain_adapter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {
using GuestState = oot3d::recomp::a32::GuestState;
using ExecutionResult = oot3d::recomp::a32::ExecutionResult;

// Gameplay_Draw: 0x002E2CC0 is the whole block that sets up and calls the
// native weather draw at 0x002E2CCC; 0x002E2CD0 is its return.
constexpr std::uint32_t kWeatherBlock = 0x002E2CC0U;
constexpr std::uint32_t kWeatherReturn = kTopScreenRainWeatherReturn;
constexpr std::uint32_t kWeatherDraw = 0x00463544U;
// Native model-instance enqueue, called from the streak and splash loops.
constexpr std::uint32_t kEnqueue = 0x00371F1CU;
constexpr std::uint32_t kStreakReturn = 0x004638C8U;
constexpr std::uint32_t kSplashReturn = 0x00463CB4U;
// Splash-loop precondition block 0x00463ADC..0x00463AEC (player Y < eye Y).
constexpr std::uint32_t kSplashGate = 0x00463ADCU;
constexpr std::uint32_t kSplashLoop = 0x00463AF0U;
constexpr std::uint32_t kSplashSkip = 0x00463CDCU;
constexpr std::uint32_t kFloorQuery = 0x00358410U;
constexpr std::uint32_t kWaterQuery = 0x0035E8A0U;

constexpr std::uint32_t kAgeWord = 0x00587958U + 4U;
constexpr std::uint32_t kScratchSize = 0x40U;

constexpr std::array kHooks{kWeatherBlock, kWeatherReturn, kEnqueue, kSplashGate};
constexpr std::array kResumes{kWeatherBlock, kWeatherReturn, kEnqueue,      kStreakReturn,
                              kSplashReturn, kSplashLoop,    kSplashSkip};
constexpr std::array kCalls{kFloorQuery, kWaterQuery, kWeatherDraw};

std::uint32_t Add(std::uint32_t address, std::uint32_t offset) {
  if (address > UINT32_MAX - offset) throw std::runtime_error("rain guest address overflow");
  return address + offset;
}

bool Branch(std::uint32_t site, std::uint32_t pc, GuestState &state, ExecutionResult *result,
            std::uint32_t *blocksConsumed) {
  state.r[15] = pc;
  if (result) *result = {oot3d::recomp::a32::ExitKind::Branch, pc,
                         oot3d::recomp::a32::FallbackReason::None, site};
  if (blocksConsumed) *blocksConsumed = 1U;
  return true;
}

float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }

// VCMPE.F32 then VMRS APSR_nzcv, FPSCR.
void CompareFloat(GuestState &state, float left, float right) {
  std::uint32_t flags = 0x30000000U;  // unordered: C, V
  if (left < right) flags = 0x80000000U;
  else if (left == right) flags = 0x60000000U;
  else if (left > right) flags = 0x20000000U;
  state.fpscr = (state.fpscr & 0x0FFFFFFFU) | flags;
  state.cpsr = (state.cpsr & 0x0FFFFFFFU) | flags;
}

// R2 of the enqueue is a row-major 3x4 matrix: basis columns and translation.
TopScreenRainInstance ReadInstance(const std::array<float, 12> &m) {
  TopScreenRainInstance instance;
  instance.Position = {m[3], m[7], m[11]};
  instance.Basis = {{{m[0], m[4], m[8]}, {m[1], m[5], m[9]}, {m[2], m[6], m[10]}}};
  return instance;
}

std::array<float, 12> WriteInstance(const TopScreenRainInstance &instance) {
  const auto &b = instance.Basis;
  const auto &p = instance.Position;
  return {b[0].X, b[1].X, b[2].X, p.X, b[0].Y, b[1].Y, b[2].Y, p.Y,
          b[0].Z, b[1].Z, b[2].Z, p.Z};
}
} // namespace

std::span<const std::uint32_t> TopScreenRainAdapter::HookPcs() noexcept { return kHooks; }
std::span<const std::uint32_t> TopScreenRainAdapter::ResumePcs() noexcept { return kResumes; }
std::span<const std::uint32_t> TopScreenRainAdapter::NativeCallPcs() noexcept { return kCalls; }

bool TopScreenRainAdapter::ShouldObserve(std::uint32_t pc) const noexcept {
  switch (pc) {
  case kWeatherBlock:
    return mPhase == Phase::Floor || mPhase == Phase::Water ||
        (mPhase == Phase::Idle &&
         (mPendingConfig.Enabled || mPendingConfig.DensityPercent != 100U));
  case kWeatherReturn:
    return mPhase == Phase::Weather;
  case kEnqueue:
    // Count-only densities (25..100%) leave every transform unchanged.
    return mPhase == Phase::Weather && (mPolicy.SurfaceAware || mPolicy.HorizontalScale > 1.0F);
  case kSplashGate:
    return mPhase == Phase::Weather && mPolicy.SurfaceAware;
  default:
    return false;
  }
}

std::uint8_t TopScreenRainAdapter::Read8(std::uint32_t address) const {
  std::uint8_t value = 0;
  if (!mMemory.Read8(address, &value)) throw std::runtime_error("rain byte read failed");
  return value;
}
std::uint16_t TopScreenRainAdapter::Read16(std::uint32_t address) const {
  std::uint16_t value = 0;
  if (!mMemory.Read16(address, &value)) throw std::runtime_error("rain halfword read failed");
  return value;
}
std::uint32_t TopScreenRainAdapter::Read32(std::uint32_t address) const {
  std::uint32_t value = 0;
  if (!mMemory.Read32(address, &value)) throw std::runtime_error("rain word read failed");
  return value;
}
float TopScreenRainAdapter::ReadFloat(std::uint32_t address) const {
  return Float(Read32(address));
}
void TopScreenRainAdapter::Write8(std::uint32_t address, std::uint8_t value) {
  if (!mMemory.Write8(address, value)) throw std::runtime_error("rain byte write failed");
}
void TopScreenRainAdapter::Write32(std::uint32_t address, std::uint32_t value) {
  if (!mMemory.Write32(address, value)) throw std::runtime_error("rain word write failed");
}
void TopScreenRainAdapter::WriteFloat(std::uint32_t address, float value) {
  Write32(address, Bits(value));
}

TopScreenRainInput TopScreenRainAdapter::ReadInput(std::uint32_t play) const {
  TopScreenRainInput input;
  input.Play = play;
  input.CollisionHeader = Read32(Add(play, 0xA98U));
  input.Update = Read32(Add(play, 0x5BF4U));
  input.NativeDrawReached = true;
  input.NativeCount = Read8(Add(play, 0x326FU));
  const auto camera = Read32(Add(play, 0xA54U));
  input.CameraFlags = camera != 0U ? Read16(Add(camera, 0x194U)) : 0x100U;
  input.SnowCount = Read8(Add(play, 0x3270U));
  input.DrawScale = ReadFloat(Add(play, 0x7F44U));
  const auto capacity = [&](std::uint32_t slot) -> std::optional<std::int32_t> {
    const auto model = Read32(Add(play, slot));
    if (model == 0U) return std::nullopt;
    return std::bit_cast<std::int32_t>(Read32(Add(model, 0x1F8U)));
  };
  input.StreakCapacity = capacity(0x3378U);
  input.SplashCapacity = capacity(0x3380U);
  // Gameplay_Draw passes the view at play+0x188: eye +0x30, at +0x3C.
  const auto view = Add(play, 0x188U);
  input.Eye = {ReadFloat(view + 0x30U), ReadFloat(view + 0x34U), ReadFloat(view + 0x38U)};
  input.At = {ReadFloat(view + 0x3CU), ReadFloat(view + 0x40U), ReadFloat(view + 0x44U)};
  for (std::uint32_t axis = 0; axis < 3U; ++axis)
    input.Wind[axis] = std::bit_cast<std::int16_t>(Read16(Add(play, 0x3220U + axis * 2U)));
  const auto player = Read32(Add(play, 0x20ACU));
  input.PlayerY = player != 0U ? ReadFloat(Add(player, 0x2CU)) : 0.0F;
  input.Adult = Read32(kAgeWord) == 0U;
  return input;
}

bool TopScreenRainAdapter::Execute(std::uint32_t pc, GuestState &state, ExecutionResult *result,
                                   std::uint32_t *blocksConsumed) {
  if (!ShouldObserve(pc)) return false;
  switch (pc) {
  case kWeatherBlock:
    return mPhase == Phase::Idle ? Begin(state, result, blocksConsumed)
                                 : StepQuery(state, result, blocksConsumed);
  case kWeatherReturn:
    // Restore the native count, then run the original return block once.
    Write8(Add(mInput.Play, 0x326FU), mInput.NativeCount);
    mPhase = Phase::Idle;
    return false;
  case kEnqueue:
    return Enqueue(state, result, blocksConsumed);
  case kSplashGate:
    return SplashGate(state, result, blocksConsumed);
  default:
    return false;
  }
}

bool TopScreenRainAdapter::Begin(GuestState &state, ExecutionResult *result,
                                 std::uint32_t *blocksConsumed) {
  mRuntime.SetConfig(mPendingConfig);
  const auto input = ReadInput(state.r[4]);
  const auto policy = EvaluateTopScreenRain(mRuntime.Config(), input);
  if (!policy.Intercept) return false;
  if (state.r[13] < kScratchSize + 8U || !mMemory.IsWritable(state.r[13] - kScratchSize, kScratchSize))
    throw std::runtime_error("rain native query has no caller stack");
  mCaller = state;
  mScratch = (state.r[13] - kScratchSize) & ~7U;
  mInput = input;
  mPolicy = policy;
  mStats.LastNativeCount = input.NativeCount;
  mStats.LastCount = policy.Count;
  // An invalid camera leaves the cache untouched; the transforms then refuse
  // surface work for this draw.
  if (policy.SurfaceAware) mRuntime.PrepareQueries(input);
  return NextQuery(state, result, blocksConsumed);
}

bool TopScreenRainAdapter::NextQuery(GuestState &state, ExecutionResult *result,
                                     std::uint32_t *blocksConsumed) {
  const auto pending = mRuntime.PendingQueries();
  if (!mPolicy.SurfaceAware || pending.empty()) return CallWeather(state, result, blocksConsumed);
  // 0x005F4CE0: floor ray from (cell centre, ray Y) with zeroed outputs.
  const auto &query = pending.front();
  WriteFloat(mScratch + 0x0U, query.Origin.X);
  WriteFloat(mScratch + 0x4U, query.Origin.Y);
  WriteFloat(mScratch + 0x8U, query.Origin.Z);
  for (std::uint32_t offset = 0xCU; offset < 0x1CU; offset += 4U) Write32(mScratch + offset, 0U);
  state = mCaller;
  state.r[0] = Add(mInput.Play, 0xA98U);
  state.r[1] = mScratch + 0xCU;
  state.r[2] = mScratch + 0x10U;
  state.r[3] = mScratch;
  state.r[13] = mScratch;
  state.r[14] = kWeatherBlock;
  mPhase = Phase::Floor;
  ++mStats.Queries;
  return Branch(kWeatherBlock, kFloorQuery, state, result, blocksConsumed);
}

bool TopScreenRainAdapter::StepQuery(GuestState &state, ExecutionResult *result,
                                     std::uint32_t *blocksConsumed) {
  if (state.r[13] != mScratch || state.thread_pointer != mCaller.thread_pointer)
    throw std::runtime_error("rain query re-entry caller does not match");
  if (mPhase == Phase::Floor) {
    mSurface = {};
    const float height = Float(state.vfp[0]);
    const auto polygon = Read32(mScratch + 0xCU);
    mSurface.FloorY = height;
    mSurface.HasFloor = polygon != 0U && height > -32000.0F;
    if (polygon != 0U)
      for (std::uint32_t axis = 0; axis < 3U; ++axis)
        mSurface.Normal[axis] =
            std::bit_cast<std::int16_t>(Read16(Add(polygon, 0xAU + axis * 2U)));
    // Water query at the same XZ: R0 play, R1 collision, S0 x, S1 z.
    state = mCaller;
    state.r[0] = mInput.Play;
    state.r[1] = Add(mInput.Play, 0xA98U);
    state.r[2] = mScratch + 0x14U;
    state.r[3] = mScratch + 0x18U;
    state.vfp[0] = Read32(mScratch + 0x0U);
    state.vfp[1] = Read32(mScratch + 0x8U);
    state.r[13] = mScratch;
    state.r[14] = kWeatherBlock;
    mPhase = Phase::Water;
    return Branch(kWeatherBlock, kWaterQuery, state, result, blocksConsumed);
  }
  mSurface.HasWater = state.r[0] != 0U;
  mSurface.WaterY = ReadFloat(mScratch + 0x14U);
  if (!mRuntime.CompleteQuery(mSurface)) throw std::runtime_error("rain query result was rejected");
  return NextQuery(state, result, blocksConsumed);
}

bool TopScreenRainAdapter::CallWeather(GuestState &state, ExecutionResult *result,
                                       std::uint32_t *blocksConsumed) {
  ++mStats.Draws;
  if (mPolicy.SurfaceAware) ++mStats.SurfaceDraws;
  Write8(Add(mInput.Play, 0x326FU), mPolicy.Count);
  // The replaced block 0x002E2CC0..0x002E2CCC, from the block-entry frame.
  state = mCaller;
  state.r[2] = Read32(Add(state.r[13], 0x1ECU));
  state.r[1] = Add(state.r[4], 0x188U);
  state.r[0] = state.r[4];
  state.r[14] = kWeatherReturn;
  mPhase = Phase::Weather;
  return Branch(kWeatherBlock, kWeatherDraw, state, result, blocksConsumed);
}

bool TopScreenRainAdapter::Enqueue(GuestState &state, ExecutionResult *result,
                                   std::uint32_t *blocksConsumed) {
  const auto caller = state.r[14];
  if (caller != kStreakReturn && caller != kSplashReturn) return false;
  std::array<float, 12> matrix{};
  for (std::uint32_t i = 0; i < matrix.size(); ++i) matrix[i] = ReadFloat(Add(state.r[2], i * 4U));
  auto instance = ReadInstance(matrix);
  const bool streak = caller == kStreakReturn;
  const bool keep = streak ? mRuntime.TransformStreak(mInput, instance)
                           : mRuntime.TransformSplash(mInput, instance, mEdgeRandom);
  if (!keep) {
    ++(streak ? mStats.StreaksDropped : mStats.SplashesDropped);
    // The loops ignore the enqueue's result; return without an instance.
    return Branch(kEnqueue, caller, state, result, blocksConsumed);
  }
  ++(streak ? mStats.StreaksKept : mStats.SplashesKept);
  const auto adjusted = WriteInstance(instance);
  for (std::uint32_t i = 0; i < adjusted.size(); ++i)
    if (Bits(adjusted[i]) != Bits(matrix[i])) WriteFloat(state.r[2] + i * 4U, adjusted[i]);
  return false;  // the original enqueue runs once
}

bool TopScreenRainAdapter::SplashGate(GuestState &state, ExecutionResult *result,
                                      std::uint32_t *blocksConsumed) {
  // 0x005F5530 answers -1e30 for the player height, so the splash loop runs
  // and each splash is tested against the cached surfaces instead.
  const float playerY = -1.0e30F;
  const float eyeY = ReadFloat(Add(state.r[4], 0x34U));
  state.vfp[0] = Bits(playerY);
  state.vfp[1] = Bits(eyeY);
  CompareFloat(state, playerY, eyeY);
  const bool higherOrSame = (state.cpsr & 0x20000000U) != 0U;  // BHS
  return Branch(kSplashGate, higherOrSame ? kSplashSkip : kSplashLoop, state, result,
                blocksConsumed);
}

void TopScreenRainAdapter::Reset() noexcept {
  mRuntime.Reset();
  mEdgeRandom = 0x2545F491U;
  mPhase = Phase::Idle;
  mStats.LastNativeCount = mStats.LastCount = 0;
}

} // namespace Oot3dNativeGame
