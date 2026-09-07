#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace Fast::Renderer {
namespace ContentHashDetail {
// CityHash, by Geoff Pike and Jyrki Alakuijala.
// Copyright (c) 2011 Google, Inc. Distributed under the MIT license.
constexpr uint64_t kCityHash0 = 0xc3a5c85c97cb3127ULL;
constexpr uint64_t kCityHash1 = 0xb492b66fbe98f273ULL;
constexpr uint64_t kCityHash2 = 0x9ae16a3b2f90404fULL;

inline uint64_t ByteSwap64(uint64_t value) noexcept {
    value = ((value & 0x00FF00FF00FF00FFULL) << 8U) | ((value & 0xFF00FF00FF00FF00ULL) >> 8U);
    value = ((value & 0x0000FFFF0000FFFFULL) << 16U) | ((value & 0xFFFF0000FFFF0000ULL) >> 16U);
    return (value << 32U) | (value >> 32U);
}

inline uint32_t ByteSwap32(uint32_t value) noexcept {
    value = ((value & 0x00FF00FFU) << 8U) | ((value & 0xFF00FF00U) >> 8U);
    return (value << 16U) | (value >> 16U);
}

inline uint64_t Fetch64(const char* bytes) noexcept {
    uint64_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    if constexpr (std::endian::native == std::endian::big) {
        value = ByteSwap64(value);
    }
    return value;
}

inline uint32_t Fetch32(const char* bytes) noexcept {
    uint32_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    if constexpr (std::endian::native == std::endian::big) {
        value = ByteSwap32(value);
    }
    return value;
}

inline uint64_t RotateRight(uint64_t value, int shift) noexcept {
    return shift == 0 ? value : (value >> shift) | (value << (64 - shift));
}

inline uint64_t ShiftMix(uint64_t value) noexcept {
    return value ^ (value >> 47U);
}

inline uint64_t HashLength16(uint64_t left, uint64_t right, uint64_t multiplier) noexcept {
    uint64_t first = (left ^ right) * multiplier;
    first ^= first >> 47U;
    uint64_t second = (right ^ first) * multiplier;
    second ^= second >> 47U;
    return second * multiplier;
}

inline uint64_t HashLength16(uint64_t left, uint64_t right) noexcept {
    constexpr uint64_t multiplier = 0x9ddfea08eb382d69ULL;
    return HashLength16(left, right, multiplier);
}

inline uint64_t HashLength0To16(const char* bytes, size_t size) noexcept {
    if (size >= 8U) {
        const uint64_t multiplier = kCityHash2 + size * 2U;
        const uint64_t first = Fetch64(bytes) + kCityHash2;
        const uint64_t second = Fetch64(bytes + size - 8U);
        const uint64_t third = RotateRight(second, 37) * multiplier + first;
        const uint64_t fourth = (RotateRight(first, 25) + second) * multiplier;
        return HashLength16(third, fourth, multiplier);
    }
    if (size >= 4U) {
        const uint64_t multiplier = kCityHash2 + size * 2U;
        const uint64_t first = Fetch32(bytes);
        return HashLength16(size + (first << 3U), Fetch32(bytes + size - 4U), multiplier);
    }
    if (size > 0U) {
        const auto first = static_cast<uint8_t>(bytes[0]);
        const auto middle = static_cast<uint8_t>(bytes[size >> 1U]);
        const auto last = static_cast<uint8_t>(bytes[size - 1U]);
        const uint32_t left = static_cast<uint32_t>(first) | (static_cast<uint32_t>(middle) << 8U);
        const uint32_t right = static_cast<uint32_t>(size) | (static_cast<uint32_t>(last) << 2U);
        return ShiftMix(left * kCityHash2 ^ right * kCityHash0) * kCityHash2;
    }
    return kCityHash2;
}

inline uint64_t HashLength17To32(const char* bytes, size_t size) noexcept {
    const uint64_t multiplier = kCityHash2 + size * 2U;
    const uint64_t first = Fetch64(bytes) * kCityHash1;
    const uint64_t second = Fetch64(bytes + 8U);
    const uint64_t third = Fetch64(bytes + size - 8U) * multiplier;
    const uint64_t fourth = Fetch64(bytes + size - 16U) * kCityHash2;
    return HashLength16(RotateRight(first + second, 43) + RotateRight(third, 30) + fourth,
                        first + RotateRight(second + kCityHash2, 18) + third, multiplier);
}

inline std::pair<uint64_t, uint64_t> WeakHashLength32WithSeeds(uint64_t first, uint64_t second, uint64_t third,
                                                        uint64_t fourth, uint64_t seedA, uint64_t seedB) noexcept {
    seedA += first;
    seedB = RotateRight(seedB + seedA + fourth, 21);
    const uint64_t savedA = seedA;
    seedA += second + third;
    seedB += RotateRight(seedA, 44);
    return { seedA + fourth, seedB + savedA };
}

inline std::pair<uint64_t, uint64_t> WeakHashLength32WithSeeds(const char* bytes, uint64_t seedA, uint64_t seedB) noexcept {
    return WeakHashLength32WithSeeds(Fetch64(bytes), Fetch64(bytes + 8U), Fetch64(bytes + 16U), Fetch64(bytes + 24U),
                                     seedA, seedB);
}

inline uint64_t HashLength33To64(const char* bytes, size_t size) noexcept {
    const uint64_t multiplier = kCityHash2 + size * 2U;
    uint64_t first = Fetch64(bytes) * kCityHash2;
    uint64_t second = Fetch64(bytes + 8U);
    const uint64_t third = Fetch64(bytes + size - 24U);
    const uint64_t fourth = Fetch64(bytes + size - 32U);
    const uint64_t fifth = Fetch64(bytes + 16U) * kCityHash2;
    const uint64_t sixth = Fetch64(bytes + 24U) * 9U;
    const uint64_t seventh = Fetch64(bytes + size - 8U);
    const uint64_t eighth = Fetch64(bytes + size - 16U) * multiplier;
    const uint64_t firstMix = RotateRight(first + seventh, 43) + (RotateRight(second, 30) + third) * 9U;
    const uint64_t secondMix = ((first + seventh) ^ fourth) + sixth + 1U;
    const uint64_t thirdMix = ByteSwap64((firstMix + secondMix) * multiplier) + eighth;
    const uint64_t fourthMix = RotateRight(fifth + sixth, 42) + third;
    const uint64_t fifthMix = (ByteSwap64((secondMix + thirdMix) * multiplier) + seventh) * multiplier;
    const uint64_t sixthMix = fifth + sixth + third;
    first = ByteSwap64((fourthMix + sixthMix) * multiplier + fifthMix) + second;
    second = ShiftMix((sixthMix + first) * multiplier + fourth + eighth) * multiplier;
    return second + fourthMix;
}

inline uint64_t CityHash64(const char* bytes, size_t size) noexcept {
    if (size <= 32U) {
        return size <= 16U ? HashLength0To16(bytes, size) : HashLength17To32(bytes, size);
    }
    if (size <= 64U) {
        return HashLength33To64(bytes, size);
    }

    uint64_t first = Fetch64(bytes + size - 40U);
    uint64_t second = Fetch64(bytes + size - 16U) + Fetch64(bytes + size - 56U);
    uint64_t third = HashLength16(Fetch64(bytes + size - 48U) + size, Fetch64(bytes + size - 24U));
    auto weakFirst = WeakHashLength32WithSeeds(bytes + size - 64U, size, third);
    auto weakSecond = WeakHashLength32WithSeeds(bytes + size - 32U, second + kCityHash1, first);
    first = first * kCityHash1 + Fetch64(bytes);

    size = (size - 1U) & ~static_cast<size_t>(63U);
    do {
        first = RotateRight(first + second + weakFirst.first + Fetch64(bytes + 8U), 37) * kCityHash1;
        second = RotateRight(second + weakFirst.second + Fetch64(bytes + 48U), 42) * kCityHash1;
        first ^= weakSecond.second;
        second += weakFirst.first + Fetch64(bytes + 40U);
        third = RotateRight(third + weakSecond.first, 33) * kCityHash1;
        weakFirst = WeakHashLength32WithSeeds(bytes, weakFirst.second * kCityHash1, first + weakSecond.first);
        weakSecond = WeakHashLength32WithSeeds(bytes + 32U, third + weakSecond.second, second + Fetch64(bytes + 16U));
        std::swap(third, first);
        bytes += 64U;
        size -= 64U;
    } while (size != 0U);
    return HashLength16(HashLength16(weakFirst.first, weakSecond.first) + ShiftMix(second) * kCityHash1 + third,
                        HashLength16(weakFirst.second, weakSecond.second) + first);
}
} // namespace ContentHashDetail

// Runtime content versions only. Persistent shader/asset identities keep their
// existing algorithms and schemas. Azahar's CityHash wrapper uses this same core.
[[nodiscard]] inline uint64_t ContentHash64(std::span<const uint8_t> bytes) noexcept {
    return ContentHashDetail::CityHash64(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
} // namespace Fast::Renderer
