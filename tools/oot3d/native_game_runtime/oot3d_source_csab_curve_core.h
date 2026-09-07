#pragma once

#include "recomp/a32_runtime.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t kSourceCsabCurveS16Entry = 0x003084E8U;
inline constexpr std::uint32_t kSourceCsabCurveF32Entry = 0x003087A4U;

// The CSAB evaluator is shared by the in-process source runtime and the hot
// source-overlay DLL. Keeping memory access behind this small ABI avoids
// copying the guest address space or creating temporary NativeA32Memory maps.
struct SourceCsabCurveMemory {
    void* Context = nullptr;
    bool (*IsWritable)(void* context, std::uint32_t address,
                       std::size_t size) = nullptr;
    bool (*Read)(void* context, std::uint32_t address, void* bytes,
                 std::size_t size) = nullptr;
    bool (*Write32)(void* context, std::uint32_t address,
                    std::uint32_t value) = nullptr;
};

struct SourceCsabCurveStats {
    std::uint64_t S16Calls = 0U;
    std::uint64_t F32Calls = 0U;
    std::uint64_t GuestBytesRead = 0U;
    std::uint64_t Fallbacks = 0U;
    std::uint64_t FpscrExceptionUpdates = 0U;
};

class SourceCsabCurveCore final {
  public:
    bool Execute(
        std::uint32_t pc, oot3d::recomp::a32::GuestState& state,
        const SourceCsabCurveMemory& memory,
        oot3d::recomp::a32::ExecutionResult* result,
        std::uint32_t* blocksConsumed);

    SourceCsabCurveStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    SourceCsabCurveStats mStats;
    std::string mLastError;
};

} // namespace Oot3dNativeGame
