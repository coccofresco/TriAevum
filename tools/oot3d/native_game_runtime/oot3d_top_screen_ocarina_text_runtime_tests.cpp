#include "oot3d_top_screen_ocarina_text_runtime.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
class Textures final : public oot3d::ui::UiTextureProvider {
public:
  mutable unsigned Resolutions = 0;
  unsigned FailAt = 0;
  bool Resolve(const oot3d::ui::UiTextureIdentity &, oot3d::ui::UiTexturePixels &pixels,
               std::string *) const override {
    if (++Resolutions == FailAt) return false;
    pixels.width = pixels.height = 8;
    pixels.rgba8.assign(8 * 8 * 4, 255);
    return true;
  }
};
}

int main() {
  using namespace Oot3dNativeGame;
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"text-owner-test", 0x00300000, 0x00300000, true, false, {}}, &error),
          "cannot map text fixture");
  Require(memory.Write32(0x003448C8, 0x00510000) &&
              memory.Write32(0x00510F3C, 1) && memory.Write32(0x004D5480, 8),
          "cannot seed message/language");
  Textures textures;
  TopScreenOcarinaTextRuntime runtime(memory, textures);
  TopScreenOcarinaGeometry geometry;
  Require(runtime.Prepare(geometry, &error) && !runtime.NeedsExecution(),
          "inactive ocarina scheduled guest work");
  geometry.Active = geometry.SongSelected = geometry.SongLearned = true;
  Require(runtime.Prepare(geometry, &error) && runtime.NeedsExecution() && !runtime.Pending(),
          "selected learned song did not request native text");
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 123;
  state.r[4] = 456;
  state.r[13] = 0x005FF000;
  state.r[14] = 0x00400000;
  state.r[15] = kTopScreenInputUpdateBoundary;
  state.vfp[0] = 789;
  state.cpsr = 0xA0000010;
  const auto original = state;
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0;
  Require(runtime.Execute(state, &result, &blocks) && runtime.Pending() &&
              state.r[15] == 0x00313CE0 && state.r[0] == 0x4C &&
              state.r[14] == kTopScreenInputUpdateBoundary && blocks == 1,
          "allocation does not return through the native dispatch boundary");
  Require(runtime.Prepare({}, &error) && runtime.Pending(),
          "input refresh discarded an in-flight guest continuation");
  state.r[0] = 0x00550000;
  Require(runtime.Execute(state, &result, &blocks) && state.r[15] == 0x002F57F0 &&
              state.r[0] == 0x00550000 && state.r[1] == 0x9B5 &&
              state.r[2] == 0x20 && state.r[3] == 0x1E && state.r[13] == original.r[13] - 0x20,
          "native text constructor ABI differs from the mod");
  std::uint32_t stackArgument = 1;
  Require(memory.Read32(state.r[13], &stackArgument) && stackArgument == 0,
          "fifth constructor argument is not on the guest stack");
  // An invalid model must still run native destruction and deallocation. Both
  // calls may yield to normal GPU/host-service scheduling without losing state.
  state.r[0] = 0x00550000;
  Require(runtime.Execute(state, &result, &blocks) && state.r[15] == 0x002F6944 && runtime.Pending(),
          "snapshot failure skipped native destruction");
  state.r[0] = 999;
  Require(runtime.Execute(state, &result, &blocks) && state.r[15] == 0x003525D4 &&
              state.r[0] == 0x00550000 && runtime.Pending(),
          "native destruction did not free the original allocation");
  bool failed = false;
  try { runtime.Execute(state, &result, &blocks); }
  catch (const std::runtime_error &) { failed = true; }
  Require(failed && !runtime.Pending() && runtime.Releases() == 1 && runtime.Builds() == 0 &&
              state.r == original.r && state.vfp == original.vfp && state.cpsr == original.cpsr,
          "failed snapshot did not restore caller state after cleanup");
  runtime.Reset();
  std::vector<oot3d::ui::UiPrimitive> primitives;
  runtime.Append(primitives);
  Require(!runtime.NeedsExecution() && primitives.empty(), "state-load reset retained stale text");

  constexpr std::uint32_t overlay = 0x00550000;
  const auto floats = [&](std::uint32_t address, const auto &values) {
    return memory.WriteBytes(address, std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(values.data()), sizeof(values)));
  };
  for (std::uint32_t layer = 0; layer < 2; ++layer) {
    const auto model = overlay + 0x100 + layer * 0x200;
    const auto descriptor = overlay + 0x600 + layer * 0x100;
    const auto buffer = overlay + 0x900 + layer * 0x200;
    const auto texture = overlay + 0xE00 + layer * 0x80;
    const std::array<float, 12> positions{100, 30, 0, 100, 46, 0, 110, 30, 0, 110, 46, 0};
    const std::array<float, 8> uv{0, 1, 0, 0.5F, 0.5F, 1, 0.5F, 0.5F};
    const std::array<float, 16> colors{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    Require(memory.Write32(overlay + 8 + layer * 4, model) && memory.Write32(model, descriptor) &&
                memory.Write32(descriptor + 0xC, 4) && memory.Write32(descriptor + 0x1C, 0xC4) &&
                memory.Write32(model + 0x128, 1) && memory.Write32(model + 0x1A0, buffer) &&
                memory.Write32(overlay + layer * 4, texture) &&
                memory.Write32(texture + 0x4C, 0x18000000 + layer * 0x1000) &&
                floats(buffer, positions) && floats(buffer + 48, uv) && floats(buffer + 80, colors),
            "cannot seed successful text snapshot");
  }
  Require(runtime.Prepare(geometry, &error), "cannot request successful text");
  state = original;
  Require(runtime.Execute(state, &result, &blocks), "allocation was not scheduled");
  state.r[0] = overlay;
  Require(runtime.Execute(state, &result, &blocks), "constructor was not scheduled");
  state.r[0] = overlay;
  Require(runtime.Execute(state, &result, &blocks), "destructor was not scheduled");
  Require(runtime.Execute(state, &result, &blocks), "deallocation was not scheduled");
  Require(!runtime.Execute(state, &result, &blocks) && !runtime.NeedsExecution() &&
              runtime.Builds() == 1 && runtime.Releases() == 2 && state.r == original.r,
          "successful snapshot did not return to the original continuation");
  runtime.Append(primitives);
  Require(primitives.size() == 2 && primitives[0].texture.guest_resource_address == 0 &&
              primitives[0].texture.guest_surface_address == 0,
          "presentation retained a guest texture after native deallocation");
  Require(runtime.Prepare(geometry, &error) && !runtime.NeedsExecution(),
          "unchanged presentation rebuilt the native text");
  Require(memory.Write32(0x00510F3C, 2) && runtime.Prepare(geometry, &error) && runtime.NeedsExecution(),
          "language change did not invalidate localized text");
  geometry.SongLearned = false;
  Require(runtime.Prepare(geometry, &error) && !runtime.NeedsExecution(),
          "unlearned song left a pending localized name");
  primitives.clear();
  runtime.Append(primitives);
  Require(primitives.empty(), "unlearned selection retained its previous name");

  geometry.SongLearned = true;
  Require(runtime.Prepare(geometry, &error), "cannot request failing texture snapshot");
  textures.FailAt = textures.Resolutions + 2;
  state = original;
  Require(runtime.Execute(state, &result, &blocks), "allocation was not scheduled");
  state.r[0] = overlay;
  Require(runtime.Execute(state, &result, &blocks), "constructor was not scheduled");
  state.r[0] = overlay;
  Require(runtime.Execute(state, &result, &blocks), "failed texture skipped destruction");
  runtime.Append(primitives);
  Require(primitives.empty(), "failed second texture published a partial text snapshot");
  Require(runtime.Execute(state, &result, &blocks), "failed texture skipped deallocation");
  failed = false;
  try { runtime.Execute(state, &result, &blocks); }
  catch (const std::runtime_error &) { failed = true; }
  Require(failed && runtime.Releases() == 3 && runtime.Builds() == 1,
          "failed texture did not release native resources");
  runtime.Reset();
  Require(memory.Write32(0x003448C8, 0xFFFFF100) && !runtime.Prepare(geometry, &error),
          "language owner pointer overflow was accepted");
  std::cout << "TopScreen native text lifecycle passed\n";
}
