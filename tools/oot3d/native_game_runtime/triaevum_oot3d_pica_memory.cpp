#include "triaevum_oot3d_pica_memory.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

std::optional<Oot3dPicaPhysicalMemoryView>
BuildTriAevumOot3dPicaPhysicalMemoryView(
    const triaevum::module::TamModuleMetadataV1& metadata,
    triaevum::module::GuestMemoryReadLeasePoolV1& leases,
    std::string* error) {
    if (metadata.physicalMemoryRegions.empty()) {
        if (error != nullptr) {
            *error = "TriAevum module has no physical memory regions";
        }
        return std::nullopt;
    }
    std::vector<Oot3dPicaPhysicalMemoryRegion> regions;
    regions.reserve(metadata.physicalMemoryRegions.size());
    for (const auto& region : metadata.physicalMemoryRegions) {
        regions.push_back({region.physicalBase, region.guestBase,
                           region.byteCount});
    }
    if (error != nullptr) {
        error->clear();
    }
    return Oot3dPicaPhysicalMemoryView(
        std::move(regions),
        [&leases](std::uint32_t guestAddress, std::size_t byteCount) {
            return leases.Read(guestAddress, byteCount);
        },
        [&leases](std::uint32_t guestAddress, std::size_t byteCount) {
            return leases.ContentVersion(guestAddress, byteCount);
        });
}

} // namespace Oot3dNativeGame
