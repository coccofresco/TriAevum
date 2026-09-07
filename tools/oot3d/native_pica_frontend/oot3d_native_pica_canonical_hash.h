#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace Oot3dNativeGame::CanonicalHashDetail {

inline constexpr uint64_t OffsetBasis = 1469598103934665603ULL;
inline constexpr uint64_t Prime = 1099511628211ULL;
inline constexpr uint64_t ZeroWordMultiplier = Prime * Prime * Prime * Prime;
inline constexpr uint64_t ZeroBlockMultiplier = ZeroWordMultiplier * ZeroWordMultiplier;

// FNV-1a on N zero bytes is h * prime^N modulo 2^64. Register files and
// uniform arrays contain long zero runs; their bytes remain part of the ID.
constexpr uint64_t AppendWord(uint64_t hash, uint32_t value) noexcept {
    if (value == 0) {
        return hash * ZeroWordMultiplier;
    }
    for (uint32_t shift = 0; shift < 32U; shift += 8U) {
        hash = (hash ^ static_cast<uint8_t>(value >> shift)) * Prime;
    }
    return hash;
}

inline uint64_t AppendBytes(uint64_t hash, std::span<const uint8_t> bytes) noexcept {
    size_t offset = 0;
    for (; bytes.size() - offset >= 8; offset += 8) {
        uint64_t word;
        std::memcpy(&word, bytes.data() + offset, sizeof(word));
        // Used only as a zero test: alignment and host byte order do not matter.
        if (word == 0) {
            hash *= ZeroBlockMultiplier;
        } else {
            for (size_t i = 0; i < 8; ++i) {
                hash = (hash ^ bytes[offset + i]) * Prime;
            }
        }
    }
    for (; offset < bytes.size(); ++offset) {
        hash = (hash ^ bytes[offset]) * Prime;
    }
    return hash;
}

} // namespace Oot3dNativeGame::CanonicalHashDetail
