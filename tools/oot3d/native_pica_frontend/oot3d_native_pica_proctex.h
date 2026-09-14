#pragma once
#include "fast/renderer3ds/pica_proctex_program.h"

#include "oot3d_native_pica_frontend.h"

#include <string>

namespace Oot3dNativeGame {
Fast::Renderer3ds::PicaProcTexProgram BuildOot3dPicaProcTexProgram(const Oot3dPicaDrawPacket& packet);

bool Oot3dPicaReferencesProceduralTexture(
    const Oot3dPicaDrawPacket& packet);

bool Oot3dPicaProceduralTextureConfigurationSupported(
    const Oot3dPicaDrawPacket& packet);

bool GenerateOot3dPicaProceduralTextureSampler(
    const Oot3dPicaDrawPacket& packet, std::string& source,
    std::string* error = nullptr);

} // namespace Oot3dNativeGame
