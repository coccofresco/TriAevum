#pragma once
#include "../native_pica_frontend/oot3d_native_pica_fragment_shader_gen.h"

namespace Oot3dNativeGame::FragmentUniformCodec {
template<class Writer>
void WriteFragmentUniforms(Writer& writer,
                           const Oot3dPicaFragmentUniformState& value) {
    for (const auto& vector : value.TevConstants) {
        for (const float component : vector) writer.Float(component);
    }
    for (const float component : value.CombinerBufferColor) {
        writer.Float(component);
    }
    writer.I32(value.AlphaReference);
    for (const float component : value.FogColor) writer.Float(component);
    for (const auto& pair : value.FogLut) {
        writer.Float(pair[0]);
        writer.Float(pair[1]);
    }
    for (const float bias : value.TextureLodBias) writer.Float(bias);
    for (const auto& vectors :
         {&value.Lighting.Specular0, &value.Lighting.Specular1,
          &value.Lighting.Diffuse, &value.Lighting.Ambient,
          &value.Lighting.Position, &value.Lighting.SpotDirection,
          &value.Lighting.Attenuation}) {
        for (const auto& vector : *vectors) {
            for (const float component : vector) writer.Float(component);
        }
    }
    for (const float component : value.Lighting.GlobalAmbient) {
        writer.Float(component);
    }
    writer.I32(value.ShadowTextureBias);
    writer.I32(value.ShadowOrthographic);
    writer.Float(value.ShadowBiasConstant);
    writer.Float(value.ShadowBiasLinear);
    for (const auto& stage : value.TevProgram.Stages)
        for (const auto word : stage) writer.U32(word);
    for (const auto word : value.TevProgram.Control) writer.U32(word);
}

template<class Reader>
bool ReadFragmentUniforms(Reader& reader,
                          Oot3dPicaFragmentUniformState& value,
                          bool extended, bool fragmentLighting,
                          bool shadowUniforms, bool tevProgram) {
    for (auto& vector : value.TevConstants) {
        for (auto& component : vector) {
            if (!reader.Float(component)) return false;
        }
    }
    for (auto& component : value.CombinerBufferColor) {
        if (!reader.Float(component)) return false;
    }
    if (!reader.I32(value.AlphaReference)) return false;
    for (auto& component : value.FogColor) {
        if (!reader.Float(component)) return false;
    }
    for (auto& pair : value.FogLut) {
        if (!reader.Float(pair[0]) || !reader.Float(pair[1])) return false;
    }
    if (!extended) return true;
    for (auto& bias : value.TextureLodBias) {
        if (!reader.Float(bias)) return false;
    }
    if (!fragmentLighting) return true;
    for (auto* vectors : {&value.Lighting.Specular0,
                          &value.Lighting.Specular1,
                          &value.Lighting.Diffuse,
                          &value.Lighting.Ambient,
                          &value.Lighting.Position,
                          &value.Lighting.SpotDirection,
                          &value.Lighting.Attenuation}) {
        for (auto& vector : *vectors) {
            for (auto& component : vector) {
                if (!reader.Float(component)) return false;
            }
        }
    }
    for (auto& component : value.Lighting.GlobalAmbient) {
        if (!reader.Float(component)) return false;
    }
    if (!shadowUniforms) return true;
    if (!reader.I32(value.ShadowTextureBias) ||
        !reader.I32(value.ShadowOrthographic) ||
        !reader.Float(value.ShadowBiasConstant) ||
        !reader.Float(value.ShadowBiasLinear)) {
        return false;
    }
    if (tevProgram) {
        for (auto& stage : value.TevProgram.Stages)
            for (auto& word : stage) if (!reader.U32(word)) return false;
        for (auto& word : value.TevProgram.Control) if (!reader.U32(word)) return false;
    }
    return true;
}
} // namespace Oot3dNativeGame::FragmentUniformCodec
