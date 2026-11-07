#pragma once

#include "fast/renderer3ds/pica_lighting_program.h"
#include "fast/renderer3ds/pica_pbr_material.h"
#include "fast/renderer3ds/pica_shader_hooks.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace Fast::Renderer3ds {

struct alignas(16) PicaPbrUniforms {
    // RGB base-color factor; dielectric normal-incidence reflectance in W.
    std::array<float, 4> BaseColorDielectricF0{};
    // Emissive RGB; normal scale with the normal-Y convention in its sign.
    std::array<float, 4> EmissiveNormalScale{};
    std::array<float, 4> OrmMinimumRoughness{};
    // Map mask, float bits of glossiness strength, diagnostic view, height scale bits.
    std::array<uint32_t, 4> Control{};
    // X: complete specular lobe weight; Y: combined screen-space reflection weight.
    std::array<float, 4> SpecularReflectionWeight{};
    // Strength, edge-relative width, channel bits, native primitive count bits.
    std::array<float, 4> SeamSettings{};
    PicaLightingProgram NativeLighting;
};
static_assert(sizeof(PicaPbrUniforms) == 352U);

struct PicaPbrShaderOptions {
    uint8_t TextureSlot = 0xffU;
    bool NativeFragmentLighting = false;
    bool NativeAttenuationLuts = false;
    bool SeamSoftening = false;
    // Uses the identity-proved per-hardware-slot balance. Kept out of legacy
    // supersets so their shader text and uniform ABI remain unchanged.
    bool NativeBalance = false;
    bool PreserveNativeUnlit = false;
};

struct PicaPbrShader {
    std::string Declarations;
    std::string UnlitInputs;
    std::string NativeCoordinates;
    std::string ShadingBody;
    std::string_view DiagnosticOutput;
};

// Draw-time controls of the superset material program, as GLSL expressions.
struct PicaPbrUberControls {
    std::string Enabled;
    std::string Coordinate; // uint: the material's texture coordinate (0-2)
    std::string Projected;
    std::string NativeLighting;
    std::string AttenuationLuts;
    std::string Seams;
};

// The specialized material program's text with its options as controls.
// MainDeclarations declare, at main scope before the lighting hook, every
// value later blocks read; UnlitInputs expects pica_texcoord0..2 to be
// writable main-scope copies of the inputs (the relief coordinate). The
// native-lighting branch needs a base that evaluates native fragment
// lighting; the seam branches need fragment primitive IDs.
struct PicaPbrUberShader {
    std::string Declarations;
    std::string MainDeclarations;
    std::string UnlitInputs;
    std::string ShadingBody;
    std::string_view DiagnosticOutput;
};

// Rewrites the declaration statements of names in body (at its statement
// starts) into assignments and appends zero-initialized declarations to
// hoisted: a superset program runs body in a block, the values outlive it.
// A statement must declare only hoisted names or none.
[[nodiscard]] std::string HoistPicaGlslDeclarations(
    std::string_view body, std::initializer_list<std::string_view> names, std::string& hoisted);

[[nodiscard]] PicaPbrUniforms BuildPicaPbrUniforms(
    const PicaPbrMaterialParameters& parameters, uint8_t textureMask,
    const PicaLightingProgram& nativeLighting) noexcept;
[[nodiscard]] std::optional<PicaPbrShader> BuildPicaPbrShader(
    const PicaShaderHookLayout& hooks, const PicaPbrShaderOptions& options);
[[nodiscard]] PicaPbrUberShader BuildPicaPbrUberShader(
    const PicaPbrUberControls& controls, bool nativeLighting, bool primitiveIds,
    bool nativeBalance = false, bool preserveNativeUnlit = false);

} // namespace Fast::Renderer3ds
