#pragma once

#include "oot3d_native_pica_draw_state.h"
#include "oot3d/renderer/pica_shader_hooks.h"
#include "oot3d/renderer/pica_shader_source_identity.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

struct Oot3dPicaVertexUniformState {
    uint32_t BooleanMask = 0;
    std::array<std::array<uint32_t, 4>, 4> Integers{};
    std::array<std::array<float, 4>, 96> Floats{};
};

struct Oot3dPicaTemporalVertexProgram {
    Oot3d::Renderer::PicaVertexShaderHookLayout Hooks;
    std::string PreviousRegisterState;
    std::string PreviousMainBody;
};

struct Oot3dPicaGeneratedVertexShader {
    uint64_t StateKey = 0;
    std::string Source;
    Oot3d::Renderer::PicaShaderSourceIdentity SourceIdentity;
    std::shared_ptr<const Oot3dPicaTemporalVertexProgram>
        TemporalProgram;
    Oot3dPicaVertexUniformState Uniforms;
};

uint64_t ComputeOot3dPicaVertexShaderStateKey(
    const Oot3dPicaDrawPacket& packet);

Oot3dPicaVertexUniformState BuildOot3dPicaVertexUniformState(
    const Oot3dPicaDrawPacket& packet);

bool GenerateOot3dPicaVertexShader(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state,
    Oot3dPicaGeneratedVertexShader& shader,
    std::string* error = nullptr);

} // namespace Oot3dNativeGame
