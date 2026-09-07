#pragma once

#include <string>

namespace Fast::Oot3d {

// FXAA library consumed by the PICA scanout shader. SMAA is intentionally a
// separate three-pass NRI pipeline and must never fall back to this shader.
[[nodiscard]] std::string BuildSpatialAaShaderLibrary();

} // namespace Fast::Oot3d
