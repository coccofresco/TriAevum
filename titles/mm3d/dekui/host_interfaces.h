// SPDX-License-Identifier: GPL-2.0-or-later
// Host boundary for code ported from mm3d-dekui src/core/arm/dynarmic/arm_dynarmic.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace Mm3dDekui {
using U32 = std::uint32_t;

// Little-endian guest words. Read returns nullopt for inaccessible memory.
// Write has the same best-effort semantics as the donor's Write32 callback.
// The adapter owns address translation/fault policy; the core owns no image.
class Memory {
public:
    virtual ~Memory() = default;
    // Supplied only after the corresponding Restoration sites are verified
    // and present in the registry. False is the default for all old adapters.
    virtual bool RestorationMessagePort() const { return false; }
    virtual bool RestorationCameraPort() const { return false; }
    virtual U32 RestorationHeartWidget() const { return 0; }
    virtual std::optional<U32> Read(U32 address) = 0;
    virtual void Write(U32 address, U32 value) = 0;
    std::optional<U32> Read32OrNullopt(U32 address) { return Read(address); }
    U32 Read32(U32 address) { return Read(address).value_or(0); }
    virtual std::uint8_t Read8(U32 address) {
        return static_cast<std::uint8_t>(Read32(address & ~3U) >> (8 * (address & 3U)));
    }
    void Write32(U32 address, U32 value) { Write(address, value); }
};

// Dynarmic S-register word layout, not an array of double-precision D values.
// Current hooks use only S0, S1 and S16. The 64-word contract preserves the
// donor API; a 32-word runtime can copy its S0..S31 into this adapter's buffer.
class Cpu {
public:
    virtual ~Cpu() = default;
    virtual std::array<U32, 16>& Regs() = 0;
    virtual U32 Cpsr() = 0;
    virtual void SetCpsr(U32 value) = 0;
    virtual std::array<U32, 64>& ExtRegs() = 0;
};

struct HostFacts {
    std::function<std::int64_t()> global_time_us;
    std::function<bool()> widescreen_active;
    // Final draw-time tree, after all temporary dekui layout transforms.
    // Host consumes values only, before the scratch tree is restored.
    std::function<void(Memory&, U32 layout, U32 root, U32 pass)> layout_draw;
};

// Diagnostic-only donor LOG_INFO sink. Arguments remain type checked and
// evaluated; no formatter, emulator log subsystem, file or global callback.
template<class... Args> inline void HostLog(const char*, Args&&...) {}

// The one Common::Rectangle operation reached by widescreen.h. Keeping this
// value type here avoids a dependency on Azahar's common/math_util.h.
template<class T> struct HostRectangle {
    T left{}, top{}, right{}, bottom{};
    constexpr T GetHeight() const { return bottom - top; }
};
} // namespace Mm3dDekui
