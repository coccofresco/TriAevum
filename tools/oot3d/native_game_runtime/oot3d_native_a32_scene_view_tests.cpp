#include "oot3d_native_a32_scene_view.h"

#include "a32_runtime.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

class SceneViewMemoryBus final : public oot3d::recomp::a32::MemoryBus {
public:
  bool Read32(uint32_t address, uint32_t *value) override {
    if (value == nullptr || address + sizeof(uint32_t) > Bytes.size()) {
      return false;
    }
    *value = static_cast<uint32_t>(Bytes[address]) |
             (static_cast<uint32_t>(Bytes[address + 1U]) << 8U) |
             (static_cast<uint32_t>(Bytes[address + 2U]) << 16U) |
             (static_cast<uint32_t>(Bytes[address + 3U]) << 24U);
    return true;
  }

  bool Write32(uint32_t address, uint32_t value) override {
    if (address + sizeof(uint32_t) > Bytes.size()) {
      return false;
    }
    for (uint32_t index = 0; index < sizeof(uint32_t); ++index) {
      Bytes[address + index] = static_cast<uint8_t>(value >> (index * 8U));
    }
    return true;
  }

  void WriteFloat(uint32_t address, float value) {
    if (!Write32(address, std::bit_cast<uint32_t>(value))) {
      throw std::runtime_error("scene-view test write failed");
    }
  }

  std::array<uint8_t, 0x8000U> Bytes{};
};

void ExpectNear(float actual, float expected, const char *role) {
  if (std::abs(actual - expected) > 1.0e-6F) {
    throw std::runtime_error(role);
  }
}

} // namespace

void RunNativeA32SceneViewProbeTests() {
  Oot3dNativeGame::NativeA32SceneViewProbe probe;
  SceneViewMemoryBus memory;
  if (probe.Capture(memory).has_value()) {
    throw std::runtime_error("scene-view probe captured without evidence");
  }

  constexpr uint32_t playState = 0x1000U;
  constexpr uint32_t view = playState + 0x0188U;
  probe.ObservePerspective(0x002FDE9CU, 0x00300F50U, -0.04F, 0.04F, -0.03F,
                           0.03F, 0.1F, 5000.0F);
  const auto perspectiveOnly = probe.Capture(memory);
  if (!perspectiveOnly.has_value() || perspectiveOnly->CameraAvailable ||
      perspectiveOnly->PlayStateAddress != 0U) {
    throw std::runtime_error("scene-view probe did not publish frustum-only state");
  }

  probe.ObservePlayState(playState);
  memory.WriteFloat(view + 0x30U, 10.0F);
  memory.WriteFloat(view + 0x34U, 20.0F);
  memory.WriteFloat(view + 0x38U, 30.0F);
  memory.WriteFloat(view + 0x3CU, 11.0F);
  memory.WriteFloat(view + 0x40U, 22.0F);
  memory.WriteFloat(view + 0x44U, 33.0F);

  const auto snapshot = probe.Capture(memory);
  if (!snapshot.has_value() || snapshot->PlayStateAddress != playState ||
      !snapshot->CameraAvailable ||
      snapshot->GuestFunction != 0x002FDE9CU ||
      snapshot->GuestReturnAddress != 0x00300F50U || snapshot->Serial == 0U) {
    throw std::runtime_error("native scene-view identity mismatch");
  }
  ExpectNear(snapshot->Left, -0.04F, "scene-view left");
  ExpectNear(snapshot->FarPlane, 5000.0F, "scene-view far");
  ExpectNear(snapshot->Eye[1], 20.0F, "scene-view eye");
  ExpectNear(snapshot->At[2], 33.0F, "scene-view at");

  probe.ObservePerspective(0U, 0U, 1.0F, 1.0F, -1.0F, 1.0F, 0.1F, 10.0F);
  if (probe.Stats().PerspectiveObservations != 2U ||
      probe.Stats().InvalidPerspectiveObservations != 1U ||
      probe.Stats().SnapshotsCaptured != 2U ||
      probe.Stats().ReadFailures != 0U) {
    throw std::runtime_error("native scene-view probe stats mismatch");
  }

  probe.Reset();
  if (probe.Capture(memory).has_value()) {
    throw std::runtime_error("scene-view probe survived reset");
  }
}
