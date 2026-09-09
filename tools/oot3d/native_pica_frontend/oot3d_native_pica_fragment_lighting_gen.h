#pragma once

#include "oot3d_native_pica_frontend.h"
#include "oot3d/renderer/pica_render_backend.h"

#include <array>
#include <string>
#include <string_view>

namespace Oot3dNativeGame {

enum class Oot3dPicaShaderBuildPurpose { RuntimeDraw, OfflineSource };

struct Oot3dPicaFragmentLightingUniformState {
  std::array<std::array<float, 4>, 8> Specular0{};
  std::array<std::array<float, 4>, 8> Specular1{};
  std::array<std::array<float, 4>, 8> Diffuse{};
  std::array<std::array<float, 4>, 8> Ambient{};
  std::array<std::array<float, 4>, 8> Position{};
  std::array<std::array<float, 4>, 8> SpotDirection{};
  std::array<std::array<float, 4>, 8> Attenuation{};
  std::array<float, 4> GlobalAmbient{};

  bool operator==(
      const Oot3dPicaFragmentLightingUniformState &) const = default;
};

bool Oot3dPicaFragmentLightingEnabled(const Oot3dPicaDrawPacket &packet);

bool Oot3dPicaFragmentLightingUsesLuts(const Oot3dPicaDrawPacket &packet);

bool Oot3dPicaFragmentLightingConfigurationSupported(
    const Oot3dPicaDrawPacket &packet, std::string *error = nullptr,
    Oot3dPicaShaderBuildPurpose purpose = Oot3dPicaShaderBuildPurpose::RuntimeDraw);

uint64_t ComputeOot3dPicaFragmentLightingStructuralKey(
    const Oot3dPicaDrawPacket &packet);

Oot3d::Renderer::PicaFragmentLightingLayout
BuildOot3dPicaFragmentLightingLayout(const Oot3dPicaDrawPacket &packet);

Oot3dPicaFragmentLightingUniformState
BuildOot3dPicaFragmentLightingUniformState(const Oot3dPicaDrawPacket &packet);

bool GenerateOot3dPicaFragmentLightingSource(const Oot3dPicaDrawPacket &packet,
                                             std::string_view bumpTextureSample,
                                             std::string_view shadowTextureSample,
                                             std::string &declarations,
                                             std::string &mainBody,
                                             std::string *error = nullptr,
                                             Oot3dPicaShaderBuildPurpose purpose = Oot3dPicaShaderBuildPurpose::RuntimeDraw);

} // namespace Oot3dNativeGame
