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
    std::array<uint32_t, 4> Control{}; // native buffer-update mask, effective texture-reference mask, reserved
};
static_assert(sizeof(PicaTevProgram) == 112);

enum class PicaTevDecodeError { None, TruncatedRegisters, Source, ColorOperand, ColorOperation, AlphaOperation };

PicaTevDecodeError DecodePicaTevProgram(std::span<const uint32_t> registers,
                                      PicaTevProgram& output) noexcept;

// Texture RGB that reaches the final color through native TEV dependencies.
// Alpha-only/scalar operands and overwritten stages are not albedo candidates.
// Bit three denotes the procedural texture; physical samplers occupy bits 0–2.
[[nodiscard]] uint8_t PicaTevMaterialTextureMask(const PicaTevProgram& program) noexcept;

// The combiner's inputs, as PicaTevProgramGlsl's PicaTevInputs.
struct PicaTevInputs {
    std::array<float, 4> Primary{};
    std::array<float, 4> PrimaryFragment{};
    std::array<float, 4> SecondaryFragment{};
    std::array<std::array<float, 4>, 4> Textures{};
    std::array<std::array<float, 4>, 6> Constants{};
    std::array<float, 4> BufferColor{};
};

// The combiner's output on the CPU: pica_evaluate_tev_resolved (primary as
// given, not quantized).
[[nodiscard]] std::array<float, 4> EvaluatePicaTevProgram(const PicaTevProgram& program,
                                                          const PicaTevInputs& inputs) noexcept;

// Immutable shader library: no per-draw generation, shaderc, texture sampling,
// output attachments or composition policy. Call pica_evaluate_tev after the
// native lighting and sampling stages and before alpha/depth/fog/composition.
// pica_evaluate_tev_resolved accepts primary already quantized and processed by
// authorized lighting hooks; it must not re-quantize those hook results.
std::string_view PicaTevProgramGlsl() noexcept;

} // namespace Fast::Renderer3ds
