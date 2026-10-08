// SPDX-License-Identifier: GPL-2.0-or-later
// Guest access in the form mm3d-dekui's hooks use it (tgacquin/mm3d-dekui,
// src/mm3d): a word read that may fail and a word write.
#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <optional>

namespace Mm3dDekui {

using U32 = std::uint32_t;

class GuestMemory {
  public:
    explicit GuestMemory(oot3d::recomp::a32::MemoryBus& bus) noexcept : mBus(bus) {}
    std::optional<U32> Read(U32 address) {
        U32 value = 0;
        if (!mBus.Read32(address, &value)) {
            return std::nullopt;
        }
        return value;
    }
    void Write(U32 address, U32 value) { (void)mBus.Write32(address, value); }

  private:
    oot3d::recomp::a32::MemoryBus& mBus;
};

} // namespace Mm3dDekui
