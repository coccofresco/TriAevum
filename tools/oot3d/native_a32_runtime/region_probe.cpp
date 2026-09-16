#include "oot3d_native_whole_aot_runtime.h"
#include "oot3d_native_a32_memory.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;
#include "region_fixture.inc"

struct Result {
    a32::GuestState Guest{};
    Oot3dWholeAotFlow Flow{};
    uint32_t Remaining{}, Consumed{};
    uint64_t Memory{}, Trace{}, Faults{}, Limits{};
    std::vector<uint32_t> Hooks;
};

static void Callback(uint32_t pc, a32::GuestState& guest, a32::MemoryBus&, void* user) {
    static_cast<std::vector<uint32_t>*>(user)->push_back(pc);
    guest.r[2] ^= 0x98765432U;
    guest.cpsr ^= a32::kFlagC;
}

static Result Run(bool candidate, unsigned budget, unsigned mode, bool fault,
                  bool skip, uint32_t entry) {
    Result result;
    NativeA32Memory memory;
    std::array<uint8_t, 4096> bytes{};
    if (!memory.MapRegion({"ram", 0x10000, bytes.size(), true, false, bytes})) std::abort();
    memory.EnableWriteTraceFingerprint(true);
    result.Guest.r[0] = 4;
    result.Guest.r[1] = fault ? 0x20000 : 0x10000;
    result.Guest.r[14] = 0x9000;
    Oot3dWholeAotStats stats;
    Oot3dWholeAotContext context{memory, memory, stats};
    const std::array<uint32_t, 2> outside{0x900, 0x2000};
    const std::array<uint32_t, 1> inside{0x1000};
    context.BlocksRemaining = budget;
    context.SkipFirstBlockEntry = skip;
    if (mode != 0) {
        context.BlockEntry = Callback;
        context.BlockEntryUser = &result.Hooks;
        if (mode == 2) {
            context.BlockEntryPcs = outside.data();
            context.BlockEntryPcCount = outside.size();
        } else if (mode == 3) {
            context.BlockEntryPcs = inside.data();
            context.BlockEntryPcCount = inside.size();
        }
    }
    Oot3dWholeAotFrame frame(result.Guest);
    Oot3dAotArchitecturalState state(result.Guest);
    result.Flow = candidate ? Execute_Loop_00001000(frame, context, state, entry)
                            : Execute_Loop_00001000_Observed(frame, context, state, entry);
    state.Flush(0x7FFFU, true);
    result.Remaining = context.BlocksRemaining;
    result.Consumed = context.BlocksConsumed;
    result.Memory = memory.ContentFingerprint();
    result.Trace = memory.WriteTraceFingerprint();
    result.Faults = stats.MemoryFaults;
    result.Limits = stats.BlockLimitExits;
    return result;
}

int main() {
    unsigned count = 0;
    for (unsigned budget = 0; budget <= 12; ++budget)
    for (unsigned mode = 0; mode < 4; ++mode)
    for (bool fault : {false, true})
    for (bool skip : {false, true})
    for (uint32_t entry : {0x1000U, 0x1018U, 0xDEADU}) {
        const auto a = Run(false, budget, mode, fault, skip, entry);
        const auto b = Run(true, budget, mode, fault, skip, entry);
        if (a.Guest.r != b.Guest.r || a.Guest.cpsr != b.Guest.cpsr ||
            a.Flow.Kind != b.Flow.Kind || a.Flow.Pc != b.Flow.Pc ||
            a.Flow.Detail != b.Flow.Detail || a.Remaining != b.Remaining ||
            a.Consumed != b.Consumed || a.Memory != b.Memory ||
            a.Trace != b.Trace || a.Hooks != b.Hooks ||
            a.Faults != b.Faults || a.Limits != b.Limits) std::abort();
        ++count;
    }
    std::printf("Region differential: %u cases passed\n", count);
}
