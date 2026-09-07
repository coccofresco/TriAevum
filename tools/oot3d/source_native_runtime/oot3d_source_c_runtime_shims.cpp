#include "oot3d_source_c_runtime_shims.h"

void* Oot3dHostMemsetStandard(
    void* destination, int value, std::size_t size) noexcept {
    auto* bytes = static_cast<volatile unsigned char*>(destination);
    const auto byteValue = static_cast<unsigned char>(value);
    for (std::size_t index = 0; index < size; ++index) {
        bytes[index] = byteValue;
    }
    return destination;
}

extern "C" void* __wrap_memset(
    void* destination, int value, std::size_t size) noexcept {
    return Oot3dHostMemsetStandard(destination, value, size);
}

