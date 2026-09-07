#pragma once

#include "oot3d_native_a32_dsp_hle.h"
#include "oot3d_source_process_image.h"

namespace Oot3dSourceRuntime {

class SourceDspMixer {
  public:
    struct PhysicalMapping {
        std::uint32_t PhysicalAddress = 0;
        GuestAddress VirtualAddress = 0;
        std::size_t Size = 0;
    };

    SourceDspMixer(GuestAddressSpace& memory,
                   const SourceProcessImageDescriptor& descriptor);
    bool ProcessFrame(std::vector<std::int16_t>& interleavedStereo,
                      std::string* error = nullptr);

  private:
    GuestAddressSpace& mMemory;
    std::vector<PhysicalMapping> mMappings;
    Oot3dNativeGame::NativeA32DspHle mMixer;
};

} // namespace Oot3dSourceRuntime
