#include "oot3d_source_data_bindings.h"
#include "oot3d_source_execution_stack.h"

#include <cstring>

namespace Oot3dSourceRuntime {
namespace {

thread_local GuestAddressSpace* gSourceAddressSpace = nullptr;

} // namespace

ScopedSourceAddressSpace::ScopedSourceAddressSpace(GuestAddressSpace& memory)
    : mPrevious(gSourceAddressSpace) {
    gSourceAddressSpace = &memory;
}

ScopedSourceAddressSpace::~ScopedSourceAddressSpace() {
    gSourceAddressSpace = mPrevious;
}

GuestAddressSpace* CurrentSourceAddressSpace() {
    return gSourceAddressSpace;
}

const std::byte* ResolveSourceRead(GuestAddress address, std::size_t size) {
    if (gSourceAddressSpace != nullptr) {
        const auto resolved = gSourceAddressSpace->ResolveRead(address, size);
        if (resolved.size() == size) {
            return resolved.data();
        }
    }
    const auto resolved = ResolveCurrentSourceStackRead(address, size);
    return resolved.size() == size ? resolved.data() : nullptr;
}

std::byte* ResolveSourceWrite(GuestAddress address, std::size_t size) {
    if (gSourceAddressSpace != nullptr) {
        auto resolved = gSourceAddressSpace->ResolveWrite(address, size);
        if (resolved.size() == size) {
            return resolved.data();
        }
    }
    auto resolved = ResolveCurrentSourceStackWrite(address, size);
    return resolved.size() == size ? resolved.data() : nullptr;
}

} // namespace Oot3dSourceRuntime

extern "C" const void* oot3d_source_resolve_read(std::uint32_t address,
                                                   std::size_t size) {
    return Oot3dSourceRuntime::ResolveSourceRead(address, size);
}

extern "C" void* oot3d_source_resolve_write(std::uint32_t address,
                                              std::size_t size) {
    return Oot3dSourceRuntime::ResolveSourceWrite(address, size);
}

extern "C" std::uint32_t oot3d_source_read_pointer32(std::uint32_t storageAddress) {
    const std::byte* storage =
        Oot3dSourceRuntime::ResolveSourceRead(storageAddress, sizeof(std::uint32_t));
    std::uint32_t targetAddress = 0;
    if (storage != nullptr) {
        std::memcpy(&targetAddress, storage, sizeof(targetAddress));
    }
    return targetAddress;
}
