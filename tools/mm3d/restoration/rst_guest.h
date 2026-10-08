// SPDX-License-Identifier: GPL-2.0-or-later
// Guest access for code ported from Project Restoration
// (leoetlino/project-restoration 8bb2a7b8): Restoration dereferences the
// game's structures in place; the port reads and writes the same fields at
// their offsets (computed from Restoration's own headers for a 32-bit target).
#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <bit>
#include <optional>

namespace Mm3dRestoration {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using u64 = std::uint64_t;

class Guest {
  public:
    explicit Guest(oot3d::recomp::a32::MemoryBus& bus) noexcept : mBus(bus) {}
    u8 Read8(u32 address) {
        u8 value = 0;
        (void)mBus.Read8(address, &value);
        return value;
    }
    u16 Read16(u32 address) {
        u16 value = 0;
        (void)mBus.Read16(address, &value);
        return value;
    }
    u32 Read32(u32 address) {
        u32 value = 0;
        (void)mBus.Read32(address, &value);
        return value;
    }
    void Write8(u32 address, u8 value) { (void)mBus.Write8(address, value); }
    void Write16(u32 address, u16 value) { (void)mBus.Write16(address, value); }
    void Write32(u32 address, u32 value) { (void)mBus.Write32(address, value); }
    float Float(u32 address) { return std::bit_cast<float>(Read32(address)); }
    void Float(u32 address, float value) { Write32(address, std::bit_cast<u32>(value)); }
    s8 Signed8(u32 address) { return static_cast<s8>(Read8(address)); }
    s16 Signed16(u32 address) { return static_cast<s16>(Read16(address)); }
    std::optional<u32> Read(u32 address) {
        u32 value=0;return mBus.Read32(address,&value)?std::optional<u32>(value):std::nullopt;
    }
    void Write(u32 address,u32 value) { Write32(address,value); }
    oot3d::recomp::a32::MemoryBus& Bus() noexcept { return mBus; }

  private:
    oot3d::recomp::a32::MemoryBus& mBus;
};

} // namespace Mm3dRestoration
