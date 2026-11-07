#pragma once

#include "fast/extension/pica_effect_decisions.h"
#include "oot3d/renderer/pica_shader_hooks.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Extension {

// The always-available program of one parametric structural configuration:
// every effect composition branch, taken per draw from PicaEffectDecisions
// recorded by the instrumentation that composes the specialized program.
inline constexpr uint32_t kPicaUberEffectBinding = 15U;

// control.y packs the small enums of PicaEffectDecisions in 4-bit fields.
inline constexpr unsigned kPicaUberFieldBits = 4U;
inline constexpr unsigned kPicaUberNormalShift = 0U, kPicaUberNormalAtShift = 4U, kPicaUberMaterialAtShift = 8U,
                          kPicaUberCoverageShift = 12U, kPicaUberAlbedoShift = 16U, kPicaUberAmbientShift = 20U,
                          kPicaUberReactiveShift = 24U;

struct PicaUberEffectProgramRequest {
    std::string_view BaseSource;
    ::Oot3d::Renderer::PicaShaderHookLayout BaseHooks;
    // The paired vertex program writes the temporal clip positions (7, 8)
    // and the surface normal and tint (9, 10).
    bool TemporalInputs = false;
    bool SurfaceInputs = false;
    // Fragment primitive IDs exist (PBR seam softening). Surface programs
    // come in both forms; a device without the capability never sees one
    // that declares them.
    bool PrimitiveIds = false;
    // Separate structural family: never reinterpret legacy uniform semantics.
    bool FragmentLightingOperand = false;
    bool FragmentLightingBalance = false;
    bool NativeFragmentLitShadows = false;
    bool PbrNativeBalance = false;
    bool PbrNativeUnlit = false;
};

[[nodiscard]] inline PicaUberEffectProgramRequest WithPicaUberLightingPolicy(
    PicaUberEffectProgramRequest request, const PicaEffectDecisions& decisions) {
    request.FragmentLightingOperand = decisions.FragmentLightingOperand;
    request.FragmentLightingBalance = decisions.FragmentLightingBalance;
    request.NativeFragmentLitShadows = decisions.NativeFragmentLitShadows;
    request.PbrNativeBalance = decisions.PbrNativeBalance;
    request.PbrNativeUnlit = decisions.PbrNativeUnlit;
    return request;
}

// Every structural choice must participate in the renderer AND replay caches.
[[nodiscard]] inline uint8_t PicaUberEffectVariantKey(const PicaUberEffectProgramRequest& request) {
    return (request.TemporalInputs ? 1U : 0U) | (request.SurfaceInputs ? 2U : 0U) |
        (request.PrimitiveIds ? 4U : 0U) | (request.FragmentLightingOperand ? 8U : 0U) |
        (request.FragmentLightingBalance ? 16U : 0U) | (request.NativeFragmentLitShadows ? 32U : 0U) |
        (request.PbrNativeBalance ? 64U : 0U) | (request.PbrNativeUnlit ? 128U : 0U);
}

// Empty when the base lacks a hook the composition needs.
[[nodiscard]] std::string BuildPicaUberEffectProgram(const PicaUberEffectProgramRequest& request);

// Whether the program the request describes implements every branch of
// decisions; a draw whose decisions it does not cover keeps only its
// specialized program.
[[nodiscard]] bool PicaUberEffectSupports(
    const PicaEffectDecisions& decisions, const PicaUberEffectProgramRequest& request) noexcept;

// std140 block at kPicaUberEffectBinding: the toon parameters (as packed by
// PackToonSurfaceParameters) followed by the decisions.
struct alignas(16) PicaUberEffectBlock {
    std::array<float, 32> Toon{};
    std::array<uint32_t, 4> Control{};
    std::array<float, 4> Values{};
};

[[nodiscard]] PicaUberEffectBlock PackPicaUberEffectBlock(
    const PicaEffectDecisions& decisions, const std::array<float, 32>& toon) noexcept;

} // namespace Fast::Extension
