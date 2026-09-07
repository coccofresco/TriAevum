#pragma once

#include "oot3d_native_pica_frontend.h"

#include <cstdint>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

struct Oot3dAzaharPicaDrawMetadata {
  uint64_t CaptureIndex = 0;
  uint64_t FrameIndex = 0;
  uint64_t DrawIndex = 0;
  std::string DrawMode;
  uint32_t TriangleTopology = 0;
  std::string VertexProgramHash;
  std::string VertexSwizzleHash;
  uint32_t VertexEntryPoint = 0;
  uint32_t VertexProgramWords = 0;
  uint32_t VertexSwizzleWords = 0;
  bool GeometryShaderEnabled = false;
  std::string GeometryProgramHash;
  std::string GeometrySwizzleHash;
  uint32_t GeometryEntryPoint = 0;
  uint32_t GeometryProgramWords = 0;
  uint32_t GeometrySwizzleWords = 0;
  std::string FragmentConfigHash;
};

// Validation-only adapter for the native state snapshots emitted by the
// instrumented Azahar build. Captured state must never become runtime input.
bool DecodeOot3dAzaharPicaDraw(const nlohmann::json &event,
                               Oot3dPicaDrawPacket &packet,
                               Oot3dAzaharPicaDrawMetadata &metadata,
                               std::string *error = nullptr);

// Matches the identity tuple stored in oot3d_azahar_shader_coverage_v1.
std::string
BuildOot3dAzaharPipelineKey(const Oot3dAzaharPicaDrawMetadata &metadata);

} // namespace Oot3dNativeGame
