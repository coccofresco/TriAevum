#pragma once

#include "recomp/a32_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Oot3dNativeGame {

enum class Oot3dAotFlagMask : uint8_t {
    None = 0,
    V = 1U << 0U,
    C = 1U << 1U,
    Z = 1U << 2U,
    N = 1U << 3U,
    Nz = (1U << 3U) | (1U << 2U),
    Nzc = (1U << 3U) | (1U << 2U) | (1U << 1U),
    Nzcv = (1U << 3U) | (1U << 2U) | (1U << 1U) | (1U << 0U),
};

constexpr Oot3dAotFlagMask operator|(
    Oot3dAotFlagMask left, Oot3dAotFlagMask right) noexcept {
    return static_cast<Oot3dAotFlagMask>(
        static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
}

constexpr bool Oot3dAotFlagLive(
    Oot3dAotFlagMask mask, Oot3dAotFlagMask flag) noexcept {
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(flag)) != 0U;
}

class Oot3dAotLazyFlags {
  public:
    explicit Oot3dAotLazyFlags(uint32_t cpsr) noexcept {
        AssignPackedNzcv(cpsr);
    }

    [[nodiscard]] bool N() const noexcept { return mN; }
    [[nodiscard]] bool Z() const noexcept { return mZ; }
    [[nodiscard]] bool C() const noexcept { return mC; }
    [[nodiscard]] bool V() const noexcept { return mV; }

    void AssignPackedNzcv(uint32_t cpsr) noexcept {
        using namespace oot3d::recomp::a32;
        mN = (cpsr & kFlagN) != 0U;
        mZ = (cpsr & kFlagZ) != 0U;
        mC = (cpsr & kFlagC) != 0U;
        mV = (cpsr & kFlagV) != 0U;
    }

    [[nodiscard]] uint32_t Materialize(uint32_t preservedCpsr) const noexcept {
        using namespace oot3d::recomp::a32;
        return (preservedCpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
               (mN ? kFlagN : 0U) | (mZ ? kFlagZ : 0U) |
               (mC ? kFlagC : 0U) | (mV ? kFlagV : 0U);
    }

    void SetNz(uint32_t value, Oot3dAotFlagMask live) noexcept {
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::N)) {
            mN = (value & 0x80000000U) != 0U;
        }
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::Z)) {
            mZ = value == 0U;
        }
    }

    void SetNz64(uint64_t value, Oot3dAotFlagMask live) noexcept {
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::N)) {
            mN = (value & UINT64_C(0x8000000000000000)) != 0U;
        }
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::Z)) {
            mZ = value == 0U;
        }
    }

    void SetLogical(
        uint32_t value, bool carry, Oot3dAotFlagMask live) noexcept {
        SetNz(value, live);
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::C)) {
            mC = carry;
        }
    }

    void SetAdd(
        uint32_t left, uint32_t right, Oot3dAotFlagMask live) noexcept {
        const uint32_t value = left + right;
        SetNz(value, live);
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::C)) {
            mC = static_cast<uint64_t>(left) + right > UINT64_C(0xFFFFFFFF);
        }
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::V)) {
            mV = ((~(left ^ right) & (left ^ value)) & 0x80000000U) != 0U;
        }
    }

    void SetSubtract(
        uint32_t left, uint32_t right, Oot3dAotFlagMask live) noexcept {
        const uint32_t value = left - right;
        SetNz(value, live);
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::C)) {
            mC = left >= right;
        }
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::V)) {
            mV = (((left ^ right) & (left ^ value)) & 0x80000000U) != 0U;
        }
    }

    [[nodiscard]] uint32_t AddWithCarry(
        uint32_t left, uint32_t right, bool carryIn,
        Oot3dAotFlagMask live) noexcept {
        const uint64_t wide = static_cast<uint64_t>(left) + right +
                              (carryIn ? UINT64_C(1) : UINT64_C(0));
        const uint32_t value = static_cast<uint32_t>(wide);
        SetNz(value, live);
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::C)) {
            mC = wide > UINT64_C(0xFFFFFFFF);
        }
        if (Oot3dAotFlagLive(live, Oot3dAotFlagMask::V)) {
            mV = ((~(left ^ right) & (left ^ value)) & 0x80000000U) != 0U;
        }
        return value;
    }

  private:
    bool mN = false;
    bool mZ = false;
    bool mC = false;
    bool mV = false;
};

template <typename Function>
class Oot3dAotScopeExit final {
  public:
    explicit Oot3dAotScopeExit(Function function) noexcept
        : mFunction(function) {
    }
    Oot3dAotScopeExit(const Oot3dAotScopeExit&) = delete;
    Oot3dAotScopeExit& operator=(const Oot3dAotScopeExit&) = delete;
    ~Oot3dAotScopeExit() noexcept {
        if (mActive) {
            mFunction();
        }
    }

    void Dismiss() noexcept { mActive = false; }

  private:
    Function mFunction;
    bool mActive = true;
};

template <typename Function>
Oot3dAotScopeExit(Function) -> Oot3dAotScopeExit<Function>;

class Oot3dAotArchitecturalState {
  public:
    explicit Oot3dAotArchitecturalState(
        oot3d::recomp::a32::GuestState& guest) noexcept
        : mGuest(guest), Flags(guest.cpsr), mPreservedCpsr(guest.cpsr) {
        for (size_t index = 0; index < R.size(); ++index) {
            R[index] = guest.r[index];
        }
    }

    void Flush(uint16_t gprMask, bool flushFlags) noexcept {
        for (size_t index = 0; index < R.size(); ++index) {
            if ((gprMask & (1U << index)) != 0U) {
                mGuest.r[index] = R[index];
            }
        }
        if (flushFlags) {
            mGuest.cpsr = Flags.Materialize(mPreservedCpsr);
        }
    }

    void Reload(uint16_t gprMask, bool reloadFlags) noexcept {
        for (size_t index = 0; index < R.size(); ++index) {
            if ((gprMask & (1U << index)) != 0U) {
                R[index] = mGuest.r[index];
            }
        }
        if (reloadFlags) {
            mPreservedCpsr = mGuest.cpsr;
            Flags.AssignPackedNzcv(mGuest.cpsr);
        }
    }

    void SetPreservedCpsrBits(uint32_t mask, uint32_t value) noexcept {
        mPreservedCpsr = (mPreservedCpsr & ~mask) | (value & mask);
    }

    void SetSaturationFlag(bool saturated) noexcept {
        if (saturated) {
            mPreservedCpsr |= 1U << 27U;
        }
    }

    [[nodiscard]] uint32_t MaterializeCpsr() const noexcept {
        return Flags.Materialize(mPreservedCpsr);
    }

    [[nodiscard]] uint32_t PreservedCpsr() const noexcept {
        return mPreservedCpsr;
    }

    std::array<uint32_t, 15> R{};
    Oot3dAotLazyFlags Flags;

  private:
    oot3d::recomp::a32::GuestState& mGuest;
    uint32_t mPreservedCpsr = 0U;
};

inline void Oot3dAotSetNz(
    Oot3dAotArchitecturalState& state, uint32_t value,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nz) noexcept {
    state.Flags.SetNz(value, live);
}

inline void Oot3dAotSetNz(
    Oot3dAotLazyFlags& flags, uint32_t value,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nz) noexcept {
    flags.SetNz(value, live);
}

inline void Oot3dAotSetNz64(
    Oot3dAotArchitecturalState& state, uint64_t value,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nz) noexcept {
    state.Flags.SetNz64(value, live);
}

inline void Oot3dAotSetNz64(
    Oot3dAotLazyFlags& flags, uint64_t value,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nz) noexcept {
    flags.SetNz64(value, live);
}

inline void Oot3dAotSetLogicalFlags(
    Oot3dAotArchitecturalState& state, uint32_t value, bool carry,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nzc) noexcept {
    state.Flags.SetLogical(value, carry, live);
}

inline void Oot3dAotSetLogicalFlags(
    Oot3dAotLazyFlags& flags, uint32_t value, bool carry,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nzc) noexcept {
    flags.SetLogical(value, carry, live);
}

inline void Oot3dAotSetSubtractFlags(
    Oot3dAotArchitecturalState& state,
    uint32_t left, uint32_t right,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nzcv) noexcept {
    state.Flags.SetSubtract(left, right, live);
}

inline void Oot3dAotSetSubtractFlags(
    Oot3dAotLazyFlags& flags, uint32_t left, uint32_t right,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nzcv) noexcept {
    flags.SetSubtract(left, right, live);
}

inline void Oot3dAotSetAddFlags(
    Oot3dAotArchitecturalState& state,
    uint32_t left, uint32_t right,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nzcv) noexcept {
    state.Flags.SetAdd(left, right, live);
}

inline void Oot3dAotSetAddFlags(
    Oot3dAotLazyFlags& flags, uint32_t left, uint32_t right,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nzcv) noexcept {
    flags.SetAdd(left, right, live);
}

inline uint32_t Oot3dAotAddWithCarry(
    Oot3dAotArchitecturalState& state,
    uint32_t left, uint32_t right, bool carryIn, bool setFlags,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nzcv) noexcept {
    return state.Flags.AddWithCarry(
        left, right, carryIn,
        setFlags ? live : Oot3dAotFlagMask::None);
}

inline uint32_t Oot3dAotAddWithCarry(
    Oot3dAotLazyFlags& flags,
    uint32_t left, uint32_t right, bool carryIn, bool setFlags,
    Oot3dAotFlagMask live = Oot3dAotFlagMask::Nzcv) noexcept {
    return flags.AddWithCarry(
        left, right, carryIn,
        setFlags ? live : Oot3dAotFlagMask::None);
}

inline void Oot3dAotSetSaturationFlag(
    Oot3dAotArchitecturalState& state, bool saturated) noexcept {
    state.SetSaturationFlag(saturated);
}

} // namespace Oot3dNativeGame
