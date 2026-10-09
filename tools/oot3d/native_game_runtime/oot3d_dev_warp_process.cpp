#include "oot3d_dev_warp.h"

#include "oot3d_native_a32_process.h"

#include <array>

namespace Oot3dNativeGame {

namespace {

class DevWarpProcessGuest final : public DevWarpGuest {
 public:
  explicit DevWarpProcessGuest(NativeA32Process &process) : mProcess(process) {}

  bool Read(uint32_t address, std::span<uint8_t> bytes) override {
    return mProcess.Memory().ReadBytes(address, bytes);
  }

  bool Write(uint32_t address, std::span<const uint8_t> bytes) override {
    return mProcess.Memory().WriteBytes(address, bytes);
  }

  bool RequestTransition(uint32_t playState, uint16_t entrance, bool *accepted, std::string *error) override {
    constexpr uint32_t kRequestTransition = 0x003348E8U;
    constexpr uint32_t kPrepareTransitionEffect = 0x0035DA3CU;
    constexpr uint32_t kDirectCallReturn = 0x60000000U;
    constexpr uint32_t kTransitionTriggerStart = 20U;
    const std::array<uint32_t, 3> arguments{playState, entrance, kTransitionTriggerStart};
    uint32_t result = 0U;
    if (!mProcess.InvokeFunctionWithResult(kRequestTransition, arguments, kDirectCallReturn, &result, error))
      return false;
    *accepted = result != 0U;
    if (!*accepted) return true;
    const std::array<uint32_t, 1> prepare{playState};
    return mProcess.InvokeFunction(kPrepareTransitionEffect, prepare, kDirectCallReturn, error);
  }

 private:
  NativeA32Process &mProcess;
};

} // namespace

std::unique_ptr<DevWarpGuest> MakeDevWarpProcessGuest(NativeA32Process &process) {
  return std::make_unique<DevWarpProcessGuest>(process);
}

} // namespace Oot3dNativeGame
