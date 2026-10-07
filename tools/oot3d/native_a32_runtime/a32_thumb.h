#pragma once

#include <cstdint>

#include "recomp/a32_runtime.h"

namespace oot3d::recomp::a32 {

// CPSR T bit. Thumb state is carried by an odd guest PC, the interworking
// address form BX, BLX, POP {pc} and LDR pc already produce: the instruction
// lives at pc & ~1. CPSR.T mirrors the PC's state for code that reads CPSR.
constexpr std::uint32_t kCpsrThumb = 1U << 5;

constexpr bool IsThumbPc(std::uint32_t pc) noexcept { return (pc & 1U) != 0U; }

// Executes ARMv6K Thumb (Thumb-1 plus the BL/BLX prefix/suffix pairs and the
// v6 extend/reverse forms) read from guest memory, starting at the odd `pc`,
// up to and including the first instruction that writes the PC, or until
// `instruction_limit` instructions. Results:
//   Branch       next PC (odd = Thumb, even = A32); also after the limit.
//   MemoryFault  pc = faulting instruction (odd), detail = data address.
//   Unsupported  pc = instruction (odd), fallback Core, detail = halfword
//                (SVC, BKPT, UDF, CPS, SETEND and undefined encodings).
// The PC and CPSR.T of `state` match the returned PC.
ExecutionResult ExecuteThumbBlock(
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    std::uint32_t instruction_limit = 4096U);

}  // namespace oot3d::recomp::a32
