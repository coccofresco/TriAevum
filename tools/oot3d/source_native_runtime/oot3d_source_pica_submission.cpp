#include "oot3d_source_pica_submission.h"

#include <stdexcept>

namespace Oot3dSourceRuntime {
namespace {

std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion>
BuildPhysicalRegions(const SourceProcessImageDescriptor& descriptor) {
    std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion> regions;
    for (const auto& region : descriptor.ZeroRegions) {
        if (region.Name == "linear_heap") {
            regions.push_back({0x20000000U, region.Address, region.MappedSize});
        }
    }
    for (const auto& region : descriptor.SystemRegions) {
        if (region.Name == "ctr_vram") {
            regions.push_back({0x18000000U, region.Address, region.MappedSize});
        }
    }
    if (regions.size() != 2U) {
        throw std::invalid_argument(
            "source process profile must map linear_heap and ctr_vram");
    }
    return regions;
}

Oot3dNativeGame::Oot3dPicaPhysicalMemoryView BuildMemoryView(
    GuestAddressSpace& memory, const SourceProcessImageDescriptor& descriptor) {
    return Oot3dNativeGame::Oot3dPicaPhysicalMemoryView(
        BuildPhysicalRegions(descriptor),
        [&memory](std::uint32_t address, std::size_t size) {
            const auto bytes = memory.ResolveRead(address, size);
            return std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(bytes.data()),
                bytes.size());
        });
}

} // namespace

SourcePicaSubmission::SourcePicaSubmission(
    GuestAddressSpace& memory, const SourceProcessImageDescriptor& descriptor,
    bool deferGpuBackedDisplayTransfers)
    : mQueue(BuildMemoryView(memory, descriptor),
             deferGpuBackedDisplayTransfers) {}

Oot3dNativeGame::Oot3dNativePicaSubmissionQueue&
SourcePicaSubmission::Queue() noexcept {
    return mQueue;
}

const Oot3dNativeGame::Oot3dNativePicaSubmissionQueue&
SourcePicaSubmission::Queue() const noexcept {
    return mQueue;
}

} // namespace Oot3dSourceRuntime
