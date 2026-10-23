#pragma once
#include <string>

namespace Fast::Oot3d {
std::string BuildGrassVertexShader();
// rasterDepth: the blades' PICA depth equals the rasterized depth, so the
// program leaves depth to fixed function and keeps early depth testing.
std::string BuildGrassFragmentShader(bool rasterDepth = false);
std::string BuildGrassCompactionComputeShader();
std::string BuildGrassShadowGridComputeShader();
// GRASS_SELECTION_PASS 0 (count), 1 (scan), 2 (scatter).
std::string BuildGrassSelectionComputeShader();
}
