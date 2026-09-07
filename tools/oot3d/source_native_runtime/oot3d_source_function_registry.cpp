#include "oot3d_source_function_registry.h"
#include "oot3d_source_data_bindings.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace Oot3dSourceRuntime {

namespace {
SourceFunctionRegistry* gActiveRegistry = nullptr;
bool gRegistryFinalized = false;
std::size_t gBaseRegistrationCount = 0;
std::size_t gOverlayRegistrationCount = 0;
std::size_t gOverlayBaseOverlapCount = 0;
std::vector<GuestAddress> gOverlayAddresses;
struct HostFunctionOverride {
    void* Original = nullptr;
    void* Replacement = nullptr;
};
std::vector<HostFunctionOverride> gHostFunctionOverrides;
}

bool SourceFunctionRegistry::RegisterRaw(GuestAddress address, void* function,
                                         std::string name) {
    if (address == 0 || function == nullptr || Contains(address)) {
        return false;
    }
    mEntries.push_back({address, std::move(name), function});
    return true;
}

void* SourceFunctionRegistry::ResolveRaw(GuestAddress address) const {
    const auto* found = Find(address);
    if (found == nullptr) return nullptr;
    void* const* function = std::any_cast<void*>(&found->FunctionPointer);
    return function == nullptr ? nullptr : *function;
}

bool SourceFunctionRegistry::Contains(GuestAddress address) const {
    return Find(address) != nullptr;
}

std::string_view SourceFunctionRegistry::Name(GuestAddress address) const {
    const auto* found = Find(address);
    return found == nullptr ? std::string_view{} : std::string_view(found->Name);
}

std::size_t SourceFunctionRegistry::Size() const {
    return mEntries.size();
}

SourceFunctionRegistry::Entry* SourceFunctionRegistry::Find(GuestAddress address) {
    for (auto& entry : mEntries) if (entry.Address == address) return &entry;
    return nullptr;
}

const SourceFunctionRegistry::Entry* SourceFunctionRegistry::Find(GuestAddress address) const {
    for (const auto& entry : mEntries) if (entry.Address == address) return &entry;
    return nullptr;
}

} // namespace Oot3dSourceRuntime

extern "C" void oot3d_host_register_target_function(
    std::uint32_t address, void* function, const char* name) {
    using namespace Oot3dSourceRuntime;
    static SourceFunctionRegistry registry;
    if (gRegistryFinalized) {
        std::fprintf(stderr, "target registry modified after finalization\n");
        std::abort();
    }
    if (gActiveRegistry == nullptr) gActiveRegistry = &registry;
    ++gBaseRegistrationCount;
    if (gActiveRegistry->Contains(address)) {
        const bool suppliedByOverlay =
            std::ranges::find(gOverlayAddresses, address) !=
            gOverlayAddresses.end();
        if (!suppliedByOverlay) {
            std::fprintf(stderr,
                         "duplicate base target registration 0x%08x\n",
                         address);
            std::abort();
        }
        void* replacement = gActiveRegistry->ResolveRaw(address);
        if (replacement == nullptr) {
            std::abort();
        }
        gHostFunctionOverrides.push_back({function, replacement});
        ++gOverlayBaseOverlapCount;
        return;
    }
    if (!gActiveRegistry->RegisterRaw(
            address, function, name == nullptr ? "" : name)) {
        std::abort();
    }
}

extern "C" void oot3d_host_register_target_function_overlay(
    std::uint32_t address, void* function, const char* name) {
    using namespace Oot3dSourceRuntime;
    static SourceFunctionRegistry registry;
    if (gRegistryFinalized || gBaseRegistrationCount != 0) {
        std::fprintf(stderr,
                     "target overlay registered outside the overlay phase\n");
        std::abort();
    }
    if (gActiveRegistry == nullptr) gActiveRegistry = &registry;
    if (!gActiveRegistry->RegisterRaw(
            address, function, name == nullptr ? "" : name)) {
        std::fprintf(stderr,
                     "duplicate target overlay registration 0x%08x\n",
                     address);
        std::abort();
    }
    gOverlayAddresses.push_back(address);
    ++gOverlayRegistrationCount;
}

extern "C" void oot3d_host_finalize_target_function_registry(
    std::uint32_t expectedCount) {
    using namespace Oot3dSourceRuntime;
    const std::size_t expectedTotal =
        static_cast<std::size_t>(expectedCount) +
        gOverlayRegistrationCount - gOverlayBaseOverlapCount;
    if (gActiveRegistry == nullptr || gRegistryFinalized ||
        gBaseRegistrationCount != expectedCount ||
        gActiveRegistry->Size() != expectedTotal) {
        std::fprintf(stderr,
                     "target registry coverage mismatch: base expected %u, "
                     "base calls %llu, overlay %llu, overlap %llu, got %llu\n",
                     expectedCount,
                     static_cast<unsigned long long>(gBaseRegistrationCount),
                     static_cast<unsigned long long>(gOverlayRegistrationCount),
                     static_cast<unsigned long long>(gOverlayBaseOverlapCount),
                     static_cast<unsigned long long>(
                         gActiveRegistry == nullptr ? 0 : gActiveRegistry->Size()));
        std::abort();
    }
    gRegistryFinalized = true;
}

extern "C" void* oot3d_host_resolve_target_function(std::uintptr_t address) {
    using namespace Oot3dSourceRuntime;
    if (address > UINT32_MAX) {
        void* function = reinterpret_cast<void*>(address);
        const auto found = std::ranges::find_if(
            gHostFunctionOverrides,
            [function](const HostFunctionOverride& overrideEntry) {
                return overrideEntry.Original == function;
            });
        return found == gHostFunctionOverrides.end()
                   ? function
                   : found->Replacement;
    }
    if (gActiveRegistry == nullptr || !gRegistryFinalized) {
        std::fprintf(stderr, "target resolver used before registry finalization\n");
        std::abort();
    }
    void* function = gActiveRegistry->ResolveRaw(static_cast<std::uint32_t>(address));
    if (function == nullptr) {
        const GuestAddress slotAddress = static_cast<GuestAddress>(address);
        const std::byte* slot =
            ResolveSourceRead(slotAddress, sizeof(GuestAddress));
        if (slot != nullptr) {
            GuestAddress targetAddress = 0;
            std::memcpy(&targetAddress, slot, sizeof(targetAddress));
            function = gActiveRegistry->ResolveRaw(targetAddress);
        }
    }
    if (function == nullptr) {
        std::fprintf(stderr, "unregistered guest target 0x%08llx\n",
                     static_cast<unsigned long long>(address));
        std::abort();
    }
    return function;
}
