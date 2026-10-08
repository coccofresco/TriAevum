#include "mm3d_single_screen.h"

#include "core.h"
#include "mm3d_a32_substitute.h"

#include <cstdio>

namespace Oot3dNativeGame {

namespace a32 = oot3d::recomp::a32;

namespace {

class BusMemory final : public Mm3dDekui::Memory {
  public:
    explicit BusMemory(a32::MemoryBus& bus) noexcept : mBus(bus) {}
    std::optional<Mm3dDekui::U32> Read(Mm3dDekui::U32 address) override {
        std::uint32_t value = 0;
        if (!mBus.Read32(address, &value)) {
            return std::nullopt;
        }
        return value;
    }
    void Write(Mm3dDekui::U32 address, Mm3dDekui::U32 value) override {
        (void)mBus.Write32(address, value);
    }
    std::uint8_t Read8(Mm3dDekui::U32 address) override {
        std::uint8_t value = 0;
        return mBus.Read8(address, &value) ? value : 0U;
    }

  private:
    a32::MemoryBus& mBus;
};

// The guest thread's state in dynarmic's register view: S0..S31 are copied
// into the 64-word extension bank and back.
class StateCpu final : public Mm3dDekui::Cpu {
  public:
    explicit StateCpu(a32::GuestState& state) : mState(state) {
        std::copy(state.vfp.begin(), state.vfp.end(), mExt.begin());
    }
    ~StateCpu() override { std::copy(mExt.begin(), mExt.begin() + 32, mState.vfp.begin()); }
    std::array<Mm3dDekui::U32, 16>& Regs() override { return mState.r; }
    Mm3dDekui::U32 Cpsr() override { return mState.cpsr; }
    void SetCpsr(Mm3dDekui::U32 value) override { mState.cpsr = value; }
    std::array<Mm3dDekui::U32, 64>& ExtRegs() override { return mExt; }

  private:
    a32::GuestState& mState;
    std::array<Mm3dDekui::U32, 64> mExt{};
};

constexpr std::uint32_t kThumbBit = 0x20U;

} // namespace

Mm3dSingleScreen::Mm3dSingleScreen(std::function<std::int64_t()> globalTimeUs)
    : mCore(std::make_unique<Mm3dDekui::Core>(Mm3dDekui::HostFacts{std::move(globalTimeUs), {}})) {}

Mm3dSingleScreen::~Mm3dSingleScreen() = default;

void Mm3dSingleScreen::SetDamageMultiplier(int multiplier) {
    Mm3dDekui::Difficulty::damage_multiplier = multiplier;
}

std::vector<std::uint32_t> Mm3dSingleScreen::CandidatePcs() {
    return Mm3dDekui::Core::CandidatePcs();
}

void Mm3dSingleScreen::Register(Mm3dGuestPatches& patches) {
    for (const std::uint32_t pc : CandidatePcs()) {
        char name[32];
        std::snprintf(name, sizeof(name), "dekui@%08X", pc);
        Mm3dGuestPatchSite site;
        site.Pc = pc;
        site.Name = name;
        site.Prepare = [this, pc](a32::MemoryBus& bus) {
            std::uint32_t word = 0;
            if (!bus.Read32(pc, &word)) {
                return false;
            }
            BusMemory memory(bus);
            const std::uint32_t replacement = mCore->Instruction(memory, pc, word);
            if (replacement == word) {
                return false;
            }
            mReplacements[pc] = replacement;
            return true;
        };
        site.Handler = [this, pc](a32::GuestState& state,
                                  a32::MemoryBus& bus) -> std::optional<std::uint32_t> {
            const std::uint32_t replacement = mReplacements.at(pc);
            if ((replacement & 0x0F000000U) != 0x0F000000U) {
                return ExecuteMm3dSubstitute(replacement, pc, state, bus);
            }
            if (!Mm3dConditionPassed(replacement, state.cpsr)) {
                return pc + 4U;
            }
            state.r[15] = pc + 4U;
            {
                StateCpu cpu(state);
                BusMemory memory(bus);
                mCore->CallSvc(replacement & 0x00FFFFFFU, cpu, memory);
            }
            // dekui leaves Thumb as the CPSR T bit; the A32 runtime keeps it
            // in the PC.
            std::uint32_t next = state.r[15];
            if ((state.cpsr & kThumbBit) != 0U) {
                state.cpsr &= ~kThumbBit;
                next |= 1U;
            }
            return next;
        };
        patches.Add(std::move(site));
    }
}

} // namespace Oot3dNativeGame
