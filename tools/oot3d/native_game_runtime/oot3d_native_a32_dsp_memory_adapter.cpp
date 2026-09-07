#include "oot3d_native_a32_dsp_memory_adapter.h"

#include "oot3d_native_a32_memory.h"
#include "oot3d_native_a32_process_image.h"

namespace Oot3dNativeGame {

std::vector<NativeA32DspPhysicalRegion>
BuildNativeA32DspPhysicalRegions(
    const NativeA32ProcessImageManifest& manifest) {
    std::vector<NativeA32DspPhysicalRegion> regions{
        {0x20000000U, manifest.LinearHeapBaseAddress, manifest.LinearHeapSize}};
    for (const auto& region : manifest.SystemRegions) {
        if (region.Name == "ctr_vram") {
            regions.push_back(
                {0x18000000U, region.Address, region.MappedSize});
            break;
        }
    }
    return regions;
}

bool NativeA32DspHle::ProcessFrame(
    NativeA32Memory& memory, std::vector<int16_t>& interleavedStereo,
    std::string* error) {
    NativeDspMemoryAccess access;
    access.Read = [&memory](uint32_t address, std::span<uint8_t> output) {
        return memory.ReadBytes(address, output);
    };
    access.Write = [&memory](uint32_t address,
                             std::span<const uint8_t> input) {
        return memory.WriteBytes(address, input);
    };
    access.ResolvePhysical = [this, &memory](uint32_t physicalAddress) {
        for (const auto& region : PhysicalRegions()) {
            if (physicalAddress < region.PhysicalAddress) continue;
            const uint64_t offset = physicalAddress - region.PhysicalAddress;
            if (offset < region.Size) {
                return memory.GetReadPointer(
                    region.VirtualAddress + static_cast<uint32_t>(offset));
            }
        }
        return static_cast<const uint8_t*>(nullptr);
    };
    return ProcessFrame(access, interleavedStereo, error);
}

} // namespace Oot3dNativeGame
