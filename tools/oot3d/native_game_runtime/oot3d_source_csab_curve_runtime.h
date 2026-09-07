#pragma once

#include "oot3d_source_csab_curve_core.h"
#include "recomp/a32_runtime.h"

#include <cstdint>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Memory;

// Executes the two revision-pinned CSAB curve evaluators against copied
// guest curve bytes. Unsupported FPSCR modes or malformed guest layouts
// remain on the existing ARM path without mutating guest state.
class SourceCsabCurveRuntime {
  public:
    explicit SourceCsabCurveRuntime(NativeA32Memory& memory);

    bool Execute(
        uint32_t pc, oot3d::recomp::a32::GuestState& state,
        oot3d::recomp::a32::MemoryBus& memory,
        oot3d::recomp::a32::ExecutionResult* result,
        uint32_t* blocksConsumed);

    SourceCsabCurveStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    NativeA32Memory& mMemory;
    SourceCsabCurveCore mCore;
};

} // namespace Oot3dNativeGame
