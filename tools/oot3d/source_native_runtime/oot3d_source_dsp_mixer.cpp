#include "oot3d_source_dsp_mixer.h"

#include <cstring>
#include <stdexcept>

namespace Oot3dSourceRuntime {
namespace {

std::vector<SourceDspMixer::PhysicalMapping> BuildMappings(
    const SourceProcessImageDescriptor& descriptor) {
    std::vector<SourceDspMixer::PhysicalMapping> mappings;
    for (const auto& region : descriptor.ZeroRegions) {
        if (region.Name == "linear_heap") {
            mappings.push_back({0x20000000U, region.Address, region.MappedSize});
        }
    }
    for (const auto& region : descriptor.SystemRegions) {
        if (region.Name == "ctr_vram") {
            mappings.push_back({0x18000000U, region.Address, region.MappedSize});
        }
    }
    if (mappings.size() != 2U) {
        throw std::invalid_argument(
            "source process profile must map DSP physical memory");
    }
    return mappings;
}

std::vector<Oot3dNativeGame::NativeA32DspPhysicalRegion> BuildMixerRegions(
    const std::vector<SourceDspMixer::PhysicalMapping>& mappings) {
    std::vector<Oot3dNativeGame::NativeA32DspPhysicalRegion> result;
    for (const auto& mapping : mappings) {
        result.push_back(
            {mapping.PhysicalAddress, mapping.VirtualAddress, mapping.Size});
    }
    return result;
}

} // namespace

SourceDspMixer::SourceDspMixer(
    GuestAddressSpace& memory, const SourceProcessImageDescriptor& descriptor)
    : mMemory(memory), mMappings(BuildMappings(descriptor)),
      mMixer(BuildMixerRegions(mMappings)) {}

bool SourceDspMixer::ProcessFrame(
    std::vector<std::int16_t>& interleavedStereo, std::string* error) {
    Oot3dNativeGame::NativeDspMemoryAccess access;
    access.Read = [this](std::uint32_t address, std::span<std::uint8_t> output) {
        const auto source = mMemory.ResolveRead(address, output.size());
        if (source.size() != output.size()) return false;
        std::memcpy(output.data(), source.data(), output.size());
        return true;
    };
    access.Write = [this](std::uint32_t address,
                          std::span<const std::uint8_t> input) {
        auto target = mMemory.ResolveWrite(address, input.size());
        if (target.size() != input.size()) return false;
        std::memcpy(target.data(), input.data(), input.size());
        return true;
    };
    access.ResolvePhysical = [this](std::uint32_t physicalAddress) {
        for (const auto& mapping : mMappings) {
            if (physicalAddress < mapping.PhysicalAddress) continue;
            const std::uint64_t offset = physicalAddress - mapping.PhysicalAddress;
            if (offset >= mapping.Size) continue;
            const auto bytes = mMemory.ResolveRead(
                mapping.VirtualAddress + static_cast<std::uint32_t>(offset),
                mapping.Size - static_cast<std::size_t>(offset));
            if (!bytes.empty()) {
                return reinterpret_cast<const std::uint8_t*>(bytes.data());
            }
        }
        return static_cast<const std::uint8_t*>(nullptr);
    };
    return mMixer.ProcessFrame(access, interleavedStereo, error);
}

} // namespace Oot3dSourceRuntime
