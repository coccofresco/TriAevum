#include "oot3d_source_csab_curve_runtime.h"

#include "oot3d_native_a32_memory.h"

#include <span>

namespace Oot3dNativeGame {
namespace {

} // namespace

SourceCsabCurveRuntime::SourceCsabCurveRuntime(
    NativeA32Memory& memory)
    : mMemory(memory) {
}

bool SourceCsabCurveRuntime::Execute(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t* blocksConsumed) {
    if (result == nullptr || &memory != &mMemory) {
        return false;
    }
    const SourceCsabCurveMemory access{
        &mMemory,
        [](void* context, uint32_t address, size_t size) {
            return static_cast<NativeA32Memory*>(context)->IsWritable(
                address, size);
        },
        [](void* context, uint32_t address, void* bytes, size_t size) {
            return static_cast<NativeA32Memory*>(context)->ReadBytes(
                address, std::span<uint8_t>(static_cast<uint8_t*>(bytes), size));
        },
        [](void* context, uint32_t address, uint32_t value) {
            return static_cast<NativeA32Memory*>(context)->Write32(
                address, value);
        },
    };
    return mCore.Execute(pc, state, access, result, blocksConsumed);
}

SourceCsabCurveStats SourceCsabCurveRuntime::Stats() const noexcept {
    return mCore.Stats();
}

void SourceCsabCurveRuntime::ResetStats() noexcept {
    mCore.ResetStats();
}

const std::string& SourceCsabCurveRuntime::LastError() const noexcept {
    return mCore.LastError();
}

} // namespace Oot3dNativeGame
