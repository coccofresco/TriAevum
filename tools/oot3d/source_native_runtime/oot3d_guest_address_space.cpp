#include "oot3d_guest_address_space.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace Oot3dSourceRuntime {
namespace {

bool RangeEnd(GuestAddress base, std::size_t size, std::uint64_t& end) {
    end = static_cast<std::uint64_t>(base) + size;
    return end <= static_cast<std::uint64_t>(std::numeric_limits<GuestAddress>::max()) + 1;
}

} // namespace

bool MappedGuestAddressSpace::Map(GuestAddress base, std::vector<std::byte> bytes,
                                  GuestMemoryAccess access, std::string name) {
    if (bytes.empty()) {
        return false;
    }

    std::uint64_t end = 0;
    if (!RangeEnd(base, bytes.size(), end)) {
        return false;
    }

    for (const Region& region : mRegions) {
        const std::uint64_t regionEnd = static_cast<std::uint64_t>(region.Base) + region.Bytes.size();
        if (static_cast<std::uint64_t>(base) < regionEnd &&
            static_cast<std::uint64_t>(region.Base) < end) {
            return false;
        }
    }

    mRegions.push_back({base, std::move(bytes), access, std::move(name)});
    std::ranges::sort(mRegions, {}, &Region::Base);
    return true;
}

bool MappedGuestAddressSpace::MapZeroed(GuestAddress base, std::size_t size,
                                        GuestMemoryAccess access, std::string name) {
    return Map(base, std::vector<std::byte>(size), access, std::move(name));
}

bool MappedGuestAddressSpace::Unmap(GuestAddress base, std::size_t size) {
    const auto found = std::ranges::find_if(mRegions, [base, size](const Region& region) {
        return region.Base == base && region.Bytes.size() == size;
    });
    if (found == mRegions.end()) return false;
    mRegions.erase(found);
    return true;
}

std::span<const std::byte> MappedGuestAddressSpace::ResolveRead(GuestAddress address,
                                                                std::size_t size) const {
    const Region* region = FindRegion(address, size, GuestMemoryAccess::Read);
    if (region == nullptr) {
        return {};
    }
    const std::size_t offset = static_cast<std::size_t>(address - region->Base);
    return std::span<const std::byte>(region->Bytes).subspan(offset, size);
}

std::span<std::byte> MappedGuestAddressSpace::ResolveWrite(GuestAddress address,
                                                           std::size_t size) {
    Region* region = FindRegion(address, size, GuestMemoryAccess::Write);
    if (region == nullptr) {
        return {};
    }
    const std::size_t offset = static_cast<std::size_t>(address - region->Base);
    return std::span<std::byte>(region->Bytes).subspan(offset, size);
}

std::size_t MappedGuestAddressSpace::RegionCount() const {
    return mRegions.size();
}

std::optional<std::string_view> MappedGuestAddressSpace::RegionName(GuestAddress address) const {
    const Region* region = FindRegion(address, 1, GuestMemoryAccess::Read);
    if (region == nullptr) {
        return std::nullopt;
    }
    return region->Name;
}

const MappedGuestAddressSpace::Region* MappedGuestAddressSpace::FindRegion(
    GuestAddress address, std::size_t size, GuestMemoryAccess access) const {
    std::uint64_t end = 0;
    if (!RangeEnd(address, size, end)) {
        return nullptr;
    }

    for (const Region& region : mRegions) {
        const std::uint64_t regionEnd = static_cast<std::uint64_t>(region.Base) + region.Bytes.size();
        if (address >= region.Base && end <= regionEnd && HasAccess(region.Access, access)) {
            return &region;
        }
    }
    return nullptr;
}

MappedGuestAddressSpace::Region* MappedGuestAddressSpace::FindRegion(
    GuestAddress address, std::size_t size, GuestMemoryAccess access) {
    return const_cast<Region*>(std::as_const(*this).FindRegion(address, size, access));
}

DirectMappedGuestAddressSpace::~DirectMappedGuestAddressSpace() {
    Release();
}

DirectMappedGuestAddressSpace::DirectMappedGuestAddressSpace(
    DirectMappedGuestAddressSpace&& other) noexcept
    : mRegions(std::move(other.mRegions)),
      mAllocations(std::move(other.mAllocations)) {
    other.mRegions.clear();
    other.mAllocations.clear();
}

DirectMappedGuestAddressSpace& DirectMappedGuestAddressSpace::operator=(
    DirectMappedGuestAddressSpace&& other) noexcept {
    if (this != &other) {
        Release();
        mRegions = std::move(other.mRegions);
        mAllocations = std::move(other.mAllocations);
        other.mRegions.clear();
        other.mAllocations.clear();
    }
    return *this;
}

bool DirectMappedGuestAddressSpace::Map(GuestAddress base,
                                        std::vector<std::byte> bytes,
                                        GuestMemoryAccess access,
                                        std::string name) {
    return MapStorage(base, bytes.size(), bytes, access, std::move(name));
}

bool DirectMappedGuestAddressSpace::MapZeroed(GuestAddress base, std::size_t size,
                                              GuestMemoryAccess access,
                                              std::string name) {
    return MapStorage(base, size, {}, access, std::move(name));
}

bool DirectMappedGuestAddressSpace::Unmap(GuestAddress base, std::size_t size) {
    const auto found = std::ranges::find_if(mRegions, [base, size](const Region& region) {
        return region.Base == base && region.Size == size;
    });
    if (found == mRegions.end()) return false;
    mRegions.erase(found);
    return true;
}

bool DirectMappedGuestAddressSpace::MapStorage(
    GuestAddress base, std::size_t size, std::span<const std::byte> initialBytes,
    GuestMemoryAccess access, std::string name) {
    if (size == 0 || initialBytes.size() > size) {
        return false;
    }
    std::uint64_t end = 0;
    if (!RangeEnd(base, size, end)) {
        return false;
    }
    for (const Region& region : mRegions) {
        const std::uint64_t regionEnd = static_cast<std::uint64_t>(region.Base) + region.Size;
        if (static_cast<std::uint64_t>(base) < regionEnd &&
            static_cast<std::uint64_t>(region.Base) < end) {
            return false;
        }
    }

#ifdef _WIN32
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity = systemInfo.dwAllocationGranularity;
    const std::uintptr_t reservationBase =
        static_cast<std::uintptr_t>(base) & ~(granularity - 1U);
    const std::uintptr_t reservationEnd =
        (static_cast<std::uintptr_t>(end) + granularity - 1U) & ~(granularity - 1U);
    const auto overlapsAllocation = [this](std::uintptr_t start,
                                            std::uintptr_t finish) {
        return std::ranges::any_of(mAllocations, [start, finish](const Allocation& existing) {
            const auto existingStart = reinterpret_cast<std::uintptr_t>(existing.Base);
            const auto existingEnd = existingStart + existing.Size;
            return start < existingEnd && existingStart < finish;
        });
    };
    if (!overlapsAllocation(reservationBase, reservationEnd)) {
        const std::size_t reservationSize = reservationEnd - reservationBase;
        void* allocation = VirtualAlloc(reinterpret_cast<void*>(reservationBase),
                                        reservationSize, MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
        if (allocation != reinterpret_cast<void*>(reservationBase)) {
            if (allocation != nullptr) {
                VirtualFree(allocation, 0, MEM_RELEASE);
            }
            return false;
        }
        mAllocations.push_back({allocation, reservationSize});
    } else {
        for (std::uintptr_t block = reservationBase; block < reservationEnd;
             block += granularity) {
            if (overlapsAllocation(block, block + granularity)) {
                continue;
            }
            void* allocation = VirtualAlloc(reinterpret_cast<void*>(block), granularity,
                                            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (allocation != reinterpret_cast<void*>(block)) {
                if (allocation != nullptr) {
                    VirtualFree(allocation, 0, MEM_RELEASE);
                }
                return false;
            }
            mAllocations.push_back({allocation, granularity});
        }
    }
    void* logicalAddress = reinterpret_cast<void*>(static_cast<std::uintptr_t>(base));
    if (!initialBytes.empty()) {
        std::memcpy(logicalAddress, initialBytes.data(), initialBytes.size());
    }
    DWORD protection = PAGE_READONLY;
    if (HasAccess(access, GuestMemoryAccess::Execute)) {
        protection = HasAccess(access, GuestMemoryAccess::Write)
                         ? PAGE_EXECUTE_READWRITE
                         : PAGE_EXECUTE_READ;
    } else if (HasAccess(access, GuestMemoryAccess::Write)) {
        protection = PAGE_READWRITE;
    }
    const std::uintptr_t logicalStart = static_cast<std::uintptr_t>(base);
    const std::uintptr_t logicalEnd = logicalStart + size;
    for (const Allocation& allocation : mAllocations) {
        const auto allocationStart = reinterpret_cast<std::uintptr_t>(allocation.Base);
        const auto allocationEnd = allocationStart + allocation.Size;
        const auto protectStart = std::max(logicalStart, allocationStart);
        const auto protectEnd = std::min(logicalEnd, allocationEnd);
        if (protectStart >= protectEnd) {
            continue;
        }
        DWORD previousProtection = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(protectStart),
                            protectEnd - protectStart, protection,
                            &previousProtection)) {
            return false;
        }
    }
#else
#ifdef MAP_FIXED_NOREPLACE
    void* allocation = mmap(reinterpret_cast<void*>(static_cast<std::uintptr_t>(base)),
                      size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (allocation == MAP_FAILED ||
        allocation != reinterpret_cast<void*>(static_cast<std::uintptr_t>(base))) {
        return false;
    }
    if (!initialBytes.empty()) {
        std::memcpy(allocation, initialBytes.data(), initialBytes.size());
    }
    int protection = PROT_READ;
    if (HasAccess(access, GuestMemoryAccess::Write)) {
        protection |= PROT_WRITE;
    }
    if (HasAccess(access, GuestMemoryAccess::Execute)) {
        protection |= PROT_EXEC;
    }
    if (mprotect(allocation, size, protection) != 0) {
        munmap(allocation, size);
        return false;
    }
    mAllocations.push_back({allocation, size});
#else
    return false;
#endif
#endif

    mRegions.push_back({base, size, access, std::move(name)});
    std::ranges::sort(mRegions, {}, &Region::Base);
    return true;
}

std::span<const std::byte> DirectMappedGuestAddressSpace::ResolveRead(
    GuestAddress address, std::size_t size) const {
    const Region* region = FindRegion(address, size, GuestMemoryAccess::Read);
    if (region == nullptr) {
        return {};
    }
    return {reinterpret_cast<const std::byte*>(static_cast<std::uintptr_t>(address)), size};
}

std::span<std::byte> DirectMappedGuestAddressSpace::ResolveWrite(
    GuestAddress address, std::size_t size) {
    const Region* region = FindRegion(address, size, GuestMemoryAccess::Write);
    if (region == nullptr) {
        return {};
    }
    return {reinterpret_cast<std::byte*>(static_cast<std::uintptr_t>(address)), size};
}

std::size_t DirectMappedGuestAddressSpace::RegionCount() const {
    return mRegions.size();
}

const DirectMappedGuestAddressSpace::Region* DirectMappedGuestAddressSpace::FindRegion(
    GuestAddress address, std::size_t size, GuestMemoryAccess access) const {
    std::uint64_t end = 0;
    if (!RangeEnd(address, size, end)) {
        return nullptr;
    }
    for (const Region& region : mRegions) {
        const std::uint64_t regionEnd = static_cast<std::uint64_t>(region.Base) + region.Size;
        if (address >= region.Base && end <= regionEnd && HasAccess(region.Access, access)) {
            return &region;
        }
    }
    return nullptr;
}

void DirectMappedGuestAddressSpace::Release() {
    for (const Allocation& allocation : mAllocations) {
#ifdef _WIN32
        VirtualFree(allocation.Base, 0, MEM_RELEASE);
#else
        munmap(allocation.Base, allocation.Size);
#endif
    }
    mRegions.clear();
    mAllocations.clear();
}

} // namespace Oot3dSourceRuntime
