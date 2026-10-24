#pragma once
#include <string>

namespace Fast::Extension {
std::string BuildGrassVertexShader();
// Once per visible root, before the blades: the lighting every blade vertex reads.
std::string BuildGrassRootLightingComputeShader();
// rasterDepth: the blades' PICA depth equals the rasterized depth, so the
// program leaves depth to fixed function and keeps early depth testing.
// wholeBlades (with rasterDepth): no LOD stipple and no tuft cutout, hence no
// discard, for the whole pass of a split batch (kGrassWholePassFlag), whose
// hidden fragments the early depth test then rejects.
// geometryGuide false: an instrumented program for layouts that leave the
// geometry guide unbound (no face normal, no derivatives).
std::string BuildGrassFragmentShader(bool rasterDepth = false, bool wholeBlades = false, bool geometryGuide = true);
// The distant grass cards (grass_card_lod.h): the same environment, lighting,
// shadow grid, toon, fog and guides as the blades, one camera-facing quad per
// card textured by the card atlas (grass_card_atlas.h). The fragment writes
// the depth of the texel's blade (from the atlas); the quad is drawn pulled
// toward the eye by the largest blade offset, so that depth is never nearer
// than the rasterized one. With the raster depth mapping
// (GrassDepthIsRasterDepth) the conservative variant declares depth_greater
// and keeps the early depth test.
std::string BuildGrassCardVertexShader();
std::string BuildGrassCardFragmentShader(bool conservativeDepth);
std::string BuildGrassCompactionComputeShader();
std::string BuildGrassShadowGridComputeShader();
// GRASS_SELECTION_PASS 0 (count), 1 (scan), 2 (scatter).
std::string BuildGrassSelectionComputeShader();
}
