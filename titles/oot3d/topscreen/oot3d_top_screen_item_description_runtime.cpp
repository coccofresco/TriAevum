#include "oot3d_top_screen_item_description_runtime.h"

#include <array>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {
constexpr std::uint32_t kMaterialState = 0x005C58D8U;
constexpr std::uint32_t kMessageContext = 0x005C1878U;
constexpr std::uint32_t kBindCommandList = 0x0032471CU;
constexpr std::uint32_t kUploadUniformBlock0 = 0x003246ECU;
constexpr std::uint32_t kUploadUniformBlock4 = 0x003246BCU;
constexpr std::uint32_t kAccumulateWriter = 0x002F9C74U;
constexpr std::uint32_t kGlViewport = 0x002FEABCU;
constexpr std::uint32_t kMessageDraw = 0x0042CBCCU;
// 0xA0 bytes below the caller: 0x20 outgoing, then the two 64-byte matrices.
constexpr std::uint32_t kScratch = 0xA0U;
constexpr std::uint32_t kProjectionOffset = 0x20U;
constexpr std::uint32_t kIdentityOffset = 0x60U;
// Payload 0x005E14CC (rotated 400x240 projection) and 0x005E148C (identity),
// word for word, negative zeros included.
constexpr std::array<std::uint32_t, 16> kProjection{
    0x00000000U, 0xBC088889U, 0x00000000U, 0x3F800000U,
    0xBBA3D70AU, 0x80000000U, 0x80000000U, 0x3F800000U,
    0x00000000U, 0x00000000U, 0x3F000000U, 0xBF000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x3F800000U};
constexpr std::array<std::uint32_t, 16> kIdentity{
    0x3F800000U, 0, 0, 0, 0, 0x3F800000U, 0, 0,
    0, 0, 0x3F800000U, 0, 0, 0, 0, 0x3F800000U};
} // namespace

void TopScreenItemDescriptionRuntime::Reset() noexcept {
  mVisible = false;
  mArmed = false;
  mStepCount = 0;
  mNextStep = 0;
}

bool TopScreenItemDescriptionRuntime::Execute(oot3d::recomp::a32::GuestState &state,
                                              oot3d::recomp::a32::ExecutionResult *result,
                                              std::uint32_t *blocksConsumed) {
  if (!Pending()) {
    if (!mArmed) return false;
    mArmed = false;
    TopScreenItemDescriptionInputs inputs;
    if (!ReadTopScreenItemDescriptionInputs(mMemory, &inputs))
      throw std::runtime_error("TopScreen item description cannot read the native pause pages");
    if (!TopScreenItemDescriptionDrawEligible(inputs, mVisible)) return false;
    if (state.r[13] < kScratch || !mMemory.IsWritable(state.r[13] - kScratch, kScratch))
      throw std::runtime_error("TopScreen item description has no valid caller stack");
    mCallerState = state;
    const std::uint32_t scratch = state.r[13] - kScratch;
    for (std::size_t word = 0; word < 16U; ++word) {
      if (!mMemory.Write32(scratch + kProjectionOffset + word * 4U, kProjection[word]) ||
          !mMemory.Write32(scratch + kIdentityOffset + word * 4U, kIdentity[word]))
        throw std::runtime_error("TopScreen item description matrices cannot be written");
    }
    const std::uint32_t projection = scratch + kProjectionOffset;
    const std::uint32_t identity = scratch + kIdentityOffset;
    // 0x005D33B8..0x005D340C, in order.
    mCalls = {{{kBindCommandList, kMaterialState},
               {kUploadUniformBlock0, kMaterialState, projection, identity},
               {kUploadUniformBlock4, kMaterialState, identity},
               {kAccumulateWriter, kMaterialState},
               {kGlViewport, ResolveTopScreenItemDescriptionOffset(inputs), 0U, 480U, 400U},
               {kMessageDraw, kMessageContext},
               {kGlViewport, 0U, 0U, 480U, 400U}}};
    mStepCount = mCalls.size();
    mNextStep = 0;
    ++mRedraws;
  }
  if (mNextStep < mStepCount) {
    const auto call = mCalls[mNextStep++];
    state = mCallerState;
    state.r[0] = call.R0;
    state.r[1] = call.R1;
    state.r[2] = call.R2;
    state.r[3] = call.R3;
    state.r[13] -= kScratch;
    state.r[14] = kTopScreenItemDescriptionBoundary;
    state.r[15] = call.Entry;
    *result = {oot3d::recomp::a32::ExitKind::Branch, call.Entry,
               oot3d::recomp::a32::FallbackReason::None, kTopScreenItemDescriptionBoundary};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  }
  // The seventh call returned: the page draw continues with its own state.
  state = mCallerState;
  mStepCount = 0;
  mNextStep = 0;
  return false;
}

} // namespace Oot3dNativeGame
