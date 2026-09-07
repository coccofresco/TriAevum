#include "recomp/a32_runtime.h"

namespace oot3d::recomp::a32 {

bool MemoryBus::Read8(uint32_t address, uint8_t* value) {
    uint32_t word = 0U;
    if (value == nullptr || !Read32(address & ~3U, &word)) {
        return false;
    }
    *value = static_cast<uint8_t>(word >> ((address & 3U) * 8U));
    return true;
}

bool MemoryBus::Read16(uint32_t address, uint16_t* value) {
    if (value == nullptr) {
        return false;
    }
    uint8_t low = 0U;
    uint8_t high = 0U;
    if (!Read8(address, &low) || !Read8(address + 1U, &high)) {
        return false;
    }
    *value = static_cast<uint16_t>(low) |
             static_cast<uint16_t>(high) << 8U;
    return true;
}

bool MemoryBus::Write8(uint32_t address, uint8_t value) {
    const uint32_t aligned = address & ~3U;
    uint32_t word = 0U;
    if (!Read32(aligned, &word)) {
        return false;
    }
    const unsigned shift = (address & 3U) * 8U;
    const uint32_t mask = 0xFFU << shift;
    return Write32(aligned,
                   (word & ~mask) | (static_cast<uint32_t>(value) << shift));
}

bool MemoryBus::Write16(uint32_t address, uint16_t value) {
    return Write8(address, static_cast<uint8_t>(value)) &&
           Write8(address + 1U, static_cast<uint8_t>(value >> 8U));
}

bool MemoryBus::Read64(uint32_t address, uint64_t*, uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return false;
}

bool MemoryBus::Write64(uint32_t address, uint64_t, uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return false;
}

bool MemoryBus::LoadExclusive(uint32_t address, uint8_t, uint64_t*, uint64_t*,
                              uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return false;
}

ExclusiveStoreResult MemoryBus::StoreExclusive(uint32_t address, uint8_t,
                                                uint64_t, uint64_t,
                                                uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return ExclusiveStoreResult::MemoryFault;
}

bool MemoryBus::AtomicSwap(uint32_t address, uint8_t, uint32_t, uint32_t*,
                           uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return false;
}

} // namespace oot3d::recomp::a32
