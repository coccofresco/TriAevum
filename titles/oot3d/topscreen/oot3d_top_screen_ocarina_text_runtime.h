#pragma once

#include "oot3d_n64_ui_renderer.h"
#include "oot3d_top_screen_ocarina.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d_top_screen_input_cadence.h"

namespace Oot3dNativeGame {

// The original message builder owns localization, layout and font resources.
// Only an immutable presentation snapshot survives a build, never guest pointers.
class TopScreenOcarinaTextRuntime final : public oot3d::ui::UiTextureProvider {
public:
  TopScreenOcarinaTextRuntime(NativeA32Memory &memory,
                             const oot3d::ui::UiTextureProvider &nativeTextures);
  bool Prepare(const TopScreenOcarinaGeometry &geometry, std::string *error);
  bool NeedsExecution() const noexcept;
  bool Pending() const noexcept { return mPhase != Phase::Idle; }
  bool Execute(oot3d::recomp::a32::GuestState &state,
               oot3d::recomp::a32::ExecutionResult *result,
               std::uint32_t *blocksConsumed);
  void Reset() noexcept;
  void Append(std::vector<oot3d::ui::UiPrimitive> &output) const;
  bool Resolve(const oot3d::ui::UiTextureIdentity &identity,
               oot3d::ui::UiTexturePixels &pixels, std::string *error) const override;
  std::uint64_t Builds() const noexcept { return mBuilds; }
  std::uint64_t Releases() const noexcept { return mReleases; }

private:
  bool Capture(std::uint32_t overlay, std::string *error);
  NativeA32Memory &mMemory;
  const oot3d::ui::UiTextureProvider &mNativeTextures;
  std::vector<oot3d::ui::UiPrimitive> mPrimitives;
  struct Texture {
    oot3d::ui::UiTextureIdentity Identity;
    oot3d::ui::UiTexturePixels Pixels;
  };
  std::vector<Texture> mTextures;
  std::uint32_t mMessage = 0;
  std::uint32_t mLanguage = 0;
  enum class Phase { Idle, Allocate, Create, Destroy, Free };
  Phase mPhase = Phase::Idle;
  oot3d::recomp::a32::GuestState mCallerState{};
  TopScreenOcarinaGeometry mGeometry;
  std::uint32_t mRequestedMessage = 0;
  std::uint32_t mRequestedLanguage = 0;
  std::uint32_t mAllocation = 0;
  std::string mCaptureError;
  std::uint64_t mBuilds = 0;
  std::uint64_t mReleases = 0;
};
} // namespace Oot3dNativeGame
