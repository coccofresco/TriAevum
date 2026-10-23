#pragma once

#include "oot3d_native_pica_draw_state.h"
#include "fast/renderer3ds/pica_affine_uniform_layout.h"
#include "fast/renderer3ds/pica_vertex_program.h"

namespace Oot3dNativeGame {

inline constexpr Fast::Renderer3ds::PicaAffineUniformLayout kOot3dCmbAffineLayout{
    {4U, 1U}, {20U, 10U}};

// Original CmbVShader.shbin, SHA256
// 1125cfb9193e6c605ae4a3f8a80accdae948af2fb35bbaf432ef426e66de076a.
// Same instruction/operand fingerprints as the translated title manifest.
// All entry-0 branches/calls end inside these prefixes; unused upload tails
// may retain another program. Entry, code AND operands are mandatory.
inline Fast::Renderer3ds::PicaAffineUniformLayout DescribeOot3dAffineUniforms(
    const Oot3dPicaDrawPacket& packet, const Oot3dPicaDecodedDrawState& state) {
    using namespace Fast::Renderer3ds;
    constexpr PicaShaderSourceIdentity code{305449005829121044ULL, 5924619242408287756ULL, 1240ULL};
    constexpr PicaShaderSourceIdentity operands{12282991529206199656ULL, 3035664946662145040ULL, 216ULL};
    const auto& shader = packet.VertexShader;
    if (state.ShaderInterface.GeometryShaderEnabled || state.ShaderInterface.VertexMainOffset != 0U ||
        shader.ProgramWordCount < 310U || shader.ProgramWordCount > shader.Program.size() ||
        shader.SwizzleWordCount < 54U || shader.SwizzleWordCount > shader.Swizzles.size() ||
        IdentifyPicaProgramWords({shader.Program.data(), 310U}) != code ||
        IdentifyPicaProgramWords({shader.Swizzles.data(), 54U}) != operands) return {};
    return kOot3dCmbAffineLayout;
}

} // namespace Oot3dNativeGame
