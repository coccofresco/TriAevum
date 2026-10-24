#pragma once

#include <string>

namespace Fast::Extension {

// FXAA library consumed by the PICA scanout shader. SMAA is intentionally a
// separate three-pass NRI pipeline and must never fall back to this shader.
[[nodiscard]] std::string BuildSpatialAaShaderLibrary();

} // namespace Fast::Extension
