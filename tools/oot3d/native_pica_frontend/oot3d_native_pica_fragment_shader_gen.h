#pragma once

#include "oot3d_native_pica_draw_state.h"
#include "fast/renderer3ds/pica_tev_program.h"
#include "fast/renderer3ds/pica_lighting_program.h"
#include "fast/renderer3ds/pica_proctex_program.h"
#include "oot3d_native_pica_fragment_lighting_gen.h"
#include "oot3d/renderer/pica_shader_hooks.h"
#include "oot3d/renderer/pica_shader_source_identity.h"

#include <array>
#include <cstdint>
#include <string>

namespace Oot3dNativeGame {

enum class Oot3dPicaTevMode { Specialized, Parametric };

struct Oot3dPicaFragmentUniformState {
    std::array<std::array<float, 4>, 6> TevConstants{};
    std::array<float, 4> CombinerBufferColor{};
    int32_t AlphaReference = 0;
    std::array<float, 4> FogColor{};
    std::array<std::array<float, 2>, 128> FogLut{};
    std::array<float, 4> TextureLodBias{};
    Oot3dPicaFragmentLightingUniformState Lighting{};
    int32_t ShadowTextureBias = 0;
    int32_t ShadowOrthographic = 0;
    float ShadowBiasConstant = 0.0F;
    float ShadowBiasLinear = 0.0F;
    Fast::Renderer3ds::PicaTevProgram TevProgram;
    Fast::Renderer3ds::PicaLightingProgram LightingProgram;
    std::array<uint32_t, 4> FragmentControl{}; // native fog, alpha-test, texture main/config0
    Fast::Renderer3ds::PicaProcTexProgram ProcTexProgram;
};

struct Oot3dPicaGeneratedFragmentShader {
    uint64_t StateKey = 0;
    std::string Source;
    Oot3d::Renderer::PicaShaderSourceIdentity SourceIdentity;
    Oot3d::Renderer::PicaShaderHookLayout Hooks;
    Oot3dPicaFragmentUniformState Uniforms;
};

// True when a specialized program's PICA depth equals the rasterized depth
// exactly, so it leaves depth to fixed function.
bool Oot3dPicaFragmentDepthIsRasterDepth(const Oot3dPicaDecodedDrawState& state,
                                         Oot3dPicaTevMode mode = Oot3dPicaTevMode::Specialized);

uint64_t ComputeOot3dPicaFragmentShaderStateKey(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state,
    Oot3dPicaTevMode mode = Oot3dPicaTevMode::Specialized);

Oot3dPicaFragmentUniformState BuildOot3dPicaFragmentUniformState(
    const Oot3dPicaDrawPacket& packet);

// OfflineSource emits identical code without draw-time uniforms or resident
// lighting LUTs. It rejects missing data that would be embedded into source.
bool GenerateOot3dPicaFragmentShader(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state,
    Oot3dPicaGeneratedFragmentShader& shader,
    std::string* error = nullptr,
    Oot3dPicaShaderBuildPurpose purpose = Oot3dPicaShaderBuildPurpose::RuntimeDraw,
    Oot3dPicaTevMode mode = Oot3dPicaTevMode::Specialized);

// Native fragment lighting of a parametric program. Its lookup tables are a
// descriptor (binding 13) only when the draw's lighting reads one: lit draws
// without tables have a program of their own.
enum class Oot3dPicaParametricLighting : uint8_t { Off, LookupTables, NoLookupTables };
inline constexpr Oot3dPicaParametricLighting kOot3dPicaParametricLightings[] = {
    Oot3dPicaParametricLighting::Off, Oot3dPicaParametricLighting::LookupTables,
    Oot3dPicaParametricLighting::NoLookupTables};

// The parametric program of one structural configuration. Its text depends
// only on native fragment lighting and an integer (shadow) texture 0; every
// parametric draw program is one of these, with draw-specific hooks.
bool GenerateOot3dPicaParametricBaseFragmentShader(
    Oot3dPicaParametricLighting lighting, bool integerTexture0,
    Oot3dPicaGeneratedFragmentShader& shader, std::string* error = nullptr);

} // namespace Oot3dNativeGame
