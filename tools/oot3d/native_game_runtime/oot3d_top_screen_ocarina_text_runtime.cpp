#include "oot3d_top_screen_ocarina_text_runtime.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {
// Mod 0x005D4A8C/0x005CAD58; these are native game services, not mod code.
constexpr std::uint32_t kAllocate = 0x00313CE0U;
constexpr std::uint32_t kCreateText = 0x002F57F0U;
constexpr std::uint32_t kDestroyText = 0x002F6944U;
constexpr std::uint32_t kFree = 0x003525D4U;

std::string TextureKey(const oot3d::ui::UiTexturePixels &pixels) {
  std::uint64_t hash = 0xCBF29CE484222325ULL;
  for (auto byte : pixels.rgba8) { hash ^= byte; hash *= 0x100000001B3ULL; }
  std::ostringstream key;
  key << "oot3d/topscreen/song_text/" << pixels.width << 'x' << pixels.height << '/' << std::hex << hash;
  return key.str();
}
}

TopScreenOcarinaTextRuntime::TopScreenOcarinaTextRuntime(
    NativeA32Memory &memory, const oot3d::ui::UiTextureProvider &nativeTextures)
    : mMemory(memory), mNativeTextures(nativeTextures) {}

void TopScreenOcarinaTextRuntime::Reset() noexcept {
  mMessage = 0;
  mLanguage = 0;
  mPrimitives.clear();
  mTextures.clear();
  mPhase = Phase::Idle;
  mRequestedMessage = 0;
  mRequestedLanguage = 0;
  mAllocation = 0;
  mCaptureError.clear();
}

bool TopScreenOcarinaTextRuntime::Prepare(const TopScreenOcarinaGeometry &geometry,
                                        std::string *error) {
  if (Pending()) return true;
  std::uint32_t message = 0;
  if (!ReadTopScreenOcarinaSongMessage(mMemory, geometry, &message, error)) return false;
  if (!message) { Reset(); return true; }
  // Same language owner read by native message generator 0x003446E8.
  std::uint32_t languageOwner = 0, language = 0;
  if (!mMemory.Read32(0x003448C8U, &languageOwner) || !languageOwner ||
      languageOwner > UINT32_MAX - 0xF40U ||
      !mMemory.Read32(languageOwner + 0xF3CU, &language)) {
    if (error) *error = "cannot read native message language";
    return false;
  }
  mRequestedMessage = message;
  mRequestedLanguage = language;
  mGeometry = geometry;
  return true;
}

bool TopScreenOcarinaTextRuntime::NeedsExecution() const noexcept {
  return Pending() || (mRequestedMessage &&
      (mRequestedMessage != mMessage || mRequestedLanguage != mLanguage));
}

bool TopScreenOcarinaTextRuntime::Execute(oot3d::recomp::a32::GuestState &state,
                                         oot3d::recomp::a32::ExecutionResult *result,
                                         std::uint32_t *blocksConsumed) {
  if (!NeedsExecution()) return false;
  const auto call = [&](Phase phase, std::uint32_t entry, std::uint32_t arg0) {
    mPhase = phase;
    state = mCallerState;
    state.r[0] = arg0;
    state.r[13] -= 0x20;
    state.r[14] = kTopScreenInputUpdateBoundary;
    state.r[15] = entry;
    *result = {oot3d::recomp::a32::ExitKind::Branch, entry,
               oot3d::recomp::a32::FallbackReason::None, kTopScreenInputUpdateBoundary};
    if (blocksConsumed) *blocksConsumed = 1;
    return true;
  };
  switch (mPhase) {
  case Phase::Idle:
    if (state.r[13] < 0x20 || !mMemory.IsWritable(state.r[13] - 0x20, 0x20))
      throw std::runtime_error("native song text has no valid caller stack");
    mCallerState = state;
    mCaptureError.clear();
    mPrimitives.clear();
    mTextures.clear();
    return call(Phase::Allocate, kAllocate, 0x4C);
  case Phase::Allocate:
    mAllocation = state.r[0];
    if (!mAllocation) throw std::runtime_error("native song text allocation failed");
    call(Phase::Create, kCreateText, mAllocation);
    state.r[1] = mRequestedMessage;
    state.r[2] = 0x20;
    state.r[3] = 0x1E;
    if (!mMemory.Write32(state.r[13], 0))
      throw std::runtime_error("cannot write native song text stack argument");
    return true;
  case Phase::Create:
    if (state.r[0] != mAllocation) mCaptureError = "native text constructor returned a different owner";
    else if (!Capture(mAllocation, &mCaptureError) && mCaptureError.empty())
      mCaptureError = "native text capture failed";
    return call(Phase::Destroy, kDestroyText, mAllocation);
  case Phase::Destroy:
    return call(Phase::Free, kFree, mAllocation);
  case Phase::Free:
    state = mCallerState;
    mPhase = Phase::Idle;
    mAllocation = 0;
    ++mReleases;
    if (!mCaptureError.empty()) throw std::runtime_error("native song text: " + mCaptureError);
    mMessage = mRequestedMessage;
    mLanguage = mRequestedLanguage;
    ++mBuilds;
    return false;
  }
  return false;
}

bool TopScreenOcarinaTextRuntime::Capture(std::uint32_t overlay, std::string *error) {
  std::vector<oot3d::ui::UiPrimitive> primitives;
  std::vector<Texture> textures;
  if (!ReadTopScreenOcarinaTextPrimitives(mMemory, overlay, mGeometry, primitives, error)) return false;
  for (auto &primitive : primitives) {
    const auto source = primitive.texture;
    auto found = std::find_if(textures.begin(), textures.end(), [&](const auto &texture) {
      return texture.Identity.guest_resource_address == source.guest_resource_address;
    });
    if (found == textures.end()) {
      Texture texture;
      texture.Identity = source;
      if (!mNativeTextures.Resolve(source, texture.Pixels, error)) return false;
      texture.Identity.semantic_name = TextureKey(texture.Pixels);
      textures.push_back(std::move(texture));
      found = std::prev(textures.end());
    }
    primitive.texture = {0, 0, found->Identity.semantic_name};
  }
  mPrimitives = std::move(primitives);
  mTextures = std::move(textures);
  return true;
}

void TopScreenOcarinaTextRuntime::Append(std::vector<oot3d::ui::UiPrimitive> &output) const {
  output.insert(output.end(), mPrimitives.begin(), mPrimitives.end());
}

bool TopScreenOcarinaTextRuntime::Resolve(const oot3d::ui::UiTextureIdentity &identity,
                                         oot3d::ui::UiTexturePixels &pixels,
                                         std::string *error) const {
  for (const auto &texture : mTextures) {
    if (texture.Identity.semantic_name == identity.semantic_name) {
      pixels = texture.Pixels;
      return true;
    }
  }
  return mNativeTextures.Resolve(identity, pixels, error);
}
} // namespace Oot3dNativeGame
