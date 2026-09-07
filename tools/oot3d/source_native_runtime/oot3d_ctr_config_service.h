#pragma once

#include "oot3d_ctr_ipc_router.h"
#include "oot3d_guest_address_space.h"

#include <array>
#include <cstdint>

namespace Oot3dSourceRuntime {

struct CtrConfigServiceProfile {
    std::uint8_t SoundOutputMode = 1;
    std::uint8_t SystemLanguage = 1;
    std::uint8_t SystemRegion = 2;
    std::array<float, 8> StereoCameraSettings{
        62.0F, 289.0F, 76.80000305175781F, 46.08000183105469F,
        10.0F, 5.0F, 55.58000183105469F, 21.56999969482422F,
    };
};

class CtrConfigService final : public CtrIpcSession {
  public:
    CtrConfigService(GuestAddressSpace& memory,
                     CtrConfigServiceProfile profile = {});

    CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) override;

  private:
    bool WriteConfigBlock(std::uint32_t blockId, GuestAddress address,
                          std::size_t size);

    GuestAddressSpace& mMemory;
    CtrConfigServiceProfile mProfile;
};

} // namespace Oot3dSourceRuntime
