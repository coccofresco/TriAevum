#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fast::Renderer3ds {

// std140/std430-compatible data. Material constants and sampled colors are
// inputs, not part of a program/pipeline identity.
struct alignas(16) PicaTevProgram {
    std::array<std::array<uint32_t, 4>, 6> Stages{};
    std::array<uint32_t, 4> Control{};
};
static_assert(sizeof(PicaTevProgram) == 112);

enum class PicaTevDecodeError { None, TruncatedRegisters, Source, ColorOperand, ColorOperation, AlphaOperation };

PicaTevDecodeError DecodePicaTevProgram(std::span<const uint32_t> registers,
                                      PicaTevProgram& output) noexcept;

// Immutable shader library: no per-draw generation, shaderc, texture sampling,
// output attachments or composition policy. Call pica_evaluate_tev after the
// native lighting and sampling stages and before alpha/depth/fog/composition.
// pica_evaluate_tev_resolved accepts primary already quantized and processed by
// authorized lighting hooks; it must not re-quantize those hook results.
std::string_view PicaTevProgramGlsl() noexcept;

} // namespace Fast::Renderer3ds
