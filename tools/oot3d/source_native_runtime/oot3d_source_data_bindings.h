#pragma once

#include "oot3d_guest_address_space.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace Oot3dSourceRuntime {

class ScopedSourceAddressSpace {
  public:
    explicit ScopedSourceAddressSpace(GuestAddressSpace& memory);
    ~ScopedSourceAddressSpace();

    ScopedSourceAddressSpace(const ScopedSourceAddressSpace&) = delete;
    ScopedSourceAddressSpace& operator=(const ScopedSourceAddressSpace&) = delete;

  private:
    GuestAddressSpace* mPrevious = nullptr;
};

GuestAddressSpace* CurrentSourceAddressSpace();
const std::byte* ResolveSourceRead(GuestAddress address, std::size_t size);
std::byte* ResolveSourceWrite(GuestAddress address, std::size_t size);

template <typename T>
const T& SourceReadRef(GuestAddress address) {
    static_assert(std::is_trivially_copyable_v<T>);
    const std::byte* bytes = ResolveSourceRead(address, sizeof(T));
    if (bytes == nullptr) {
        throw std::out_of_range("unmapped source data read");
    }
    return *reinterpret_cast<const T*>(bytes);
}

template <typename T>
T& SourceWriteRef(GuestAddress address) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::byte* bytes = ResolveSourceWrite(address, sizeof(T));
    if (bytes == nullptr) {
        throw std::out_of_range("unmapped or protected source data write");
    }
    return *reinterpret_cast<T*>(bytes);
}

template <typename T>
T* SourceReadPointer32(GuestAddress storageAddress, std::size_t targetSize = sizeof(T)) {
    const std::byte* storage = ResolveSourceRead(storageAddress, sizeof(std::uint32_t));
    if (storage == nullptr) {
        return nullptr;
    }
    std::uint32_t targetAddress = 0;
    std::memcpy(&targetAddress, storage, sizeof(targetAddress));
    if (targetAddress == 0) {
        return nullptr;
    }
    const std::byte* target = ResolveSourceRead(targetAddress, targetSize);
    return const_cast<T*>(reinterpret_cast<const T*>(target));
}

} // namespace Oot3dSourceRuntime

extern "C" {
const void* oot3d_source_resolve_read(std::uint32_t address, std::size_t size);
void* oot3d_source_resolve_write(std::uint32_t address, std::size_t size);
std::uint32_t oot3d_source_read_pointer32(std::uint32_t storageAddress);
}
