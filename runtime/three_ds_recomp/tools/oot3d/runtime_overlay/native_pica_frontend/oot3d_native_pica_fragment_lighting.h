#pragma once

#include "oot3d_native_pica_draw_state.h"

#include <array>
#include <cstdint>
#include <string>

namespace Oot3dNativeGame {

constexpr size_t kOot3dPicaPackedLightingLutCount = 22U;
constexpr std::array<uint8_t, kOot3dPicaPackedLightingLutCount>
    kOot3dPicaPackedLightingLutTables{
        0, 1, 3, 4, 5, 6, 8, 9, 10, 11, 12,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };

struct Oot3dPicaFragmentLightingUniforms {
    std::array<float, 4> GlobalAmbient{};
    std::array<std::array<float, 4>, 8> Specular0{};
    std::array<std::array<float, 4>, 8> Specular1{};
    std::array<std::array<float, 4>, 8> Diffuse{};
    std::array<std::array<float, 4>, 8> Ambient{};
    std::array<std::array<float, 4>, 8> Position{};
    std::array<std::array<float, 4>, 8> SpotDirection{};
    std::array<std::array<float, 4>, 8> Attenuation{};
    std::shared_ptr<const Oot3dPicaLightingLutState> LutTables;
};

struct Oot3dPicaFragmentLightingShader {
    bool Enabled = false;
    uint64_t StructuralKey = 0;
    std::string UniformDeclarations;
    std::string Helpers;
    std::string Body;
    Oot3dPicaFragmentLightingUniforms Uniforms;
};

bool BuildOot3dPicaFragmentLightingShader(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state,
    Oot3dPicaFragmentLightingShader& shader,
    std::string* error = nullptr);

} // namespace Oot3dNativeGame
