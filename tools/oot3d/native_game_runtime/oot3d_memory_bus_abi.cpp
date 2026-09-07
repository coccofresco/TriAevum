#include "recomp/a32_runtime.h"

// Whole-AOT code uses MemoryBus only as its guest-memory ABI.  These are the
// base-class sized/atomic defaults and RTTI anchor; this file intentionally
// contains no decoded instruction execution or dispatcher.
namespace oot3d::recomp::a32 {

bool MemoryBus::Read8(std::uint32_t address, std::uint8_t* value) {
    std::uint32_t word = 0U;
    if (value == nullptr || !Read32(address & ~3U, &word)) {
        return false;
    }
    *value = static_cast<std::uint8_t>(word >> ((address & 3U) * 8U));
    return true;
}

bool MemoryBus::Read16(std::uint32_t address, std::uint16_t* value) {
    if (value == nullptr) {
        return false;
    }
    std::uint8_t low = 0U;
    std::uint8_t high = 0U;
    if (!Read8(address, &low) || !Read8(address + 1U, &high)) {
        return false;
    }
    *value = static_cast<std::uint16_t>(low) |
             static_cast<std::uint16_t>(high) << 8U;
    return true;
}

bool MemoryBus::Write8(std::uint32_t address, std::uint8_t value) {
    const std::uint32_t aligned = address & ~3U;
    std::uint32_t word = 0U;
    if (!Read32(aligned, &word)) {
        return false;
    }
    const unsigned shift = (address & 3U) * 8U;
    const std::uint32_t mask = 0xFFU << shift;
    return Write32(
        aligned,
        (word & ~mask) | (static_cast<std::uint32_t>(value) << shift));
}

bool MemoryBus::Write16(std::uint32_t address, std::uint16_t value) {
    return Write8(address, static_cast<std::uint8_t>(value)) &&
           Write8(address + 1U, static_cast<std::uint8_t>(value >> 8U));
}

bool MemoryBus::Read64(
    std::uint32_t address, std::uint64_t*, std::uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return false;
}

bool MemoryBus::Write64(
    std::uint32_t address, std::uint64_t, std::uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return false;
}

bool MemoryBus::LoadExclusive(
    std::uint32_t address, std::uint8_t, std::uint64_t*, std::uint64_t*,
    std::uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return false;
}

ExclusiveStoreResult MemoryBus::StoreExclusive(
    std::uint32_t address, std::uint8_t, std::uint64_t, std::uint64_t,
    std::uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return ExclusiveStoreResult::MemoryFault;
}

bool MemoryBus::AtomicSwap(
    std::uint32_t address, std::uint8_t, std::uint32_t, std::uint32_t*,
    std::uint32_t* faultAddress) {
    if (faultAddress != nullptr) {
        *faultAddress = address;
    }
    return false;
}

} // namespace oot3d::recomp::a32
