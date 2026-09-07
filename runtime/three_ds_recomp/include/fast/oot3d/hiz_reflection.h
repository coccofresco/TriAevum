#pragma once

#include <string>

namespace Fast::Oot3d {

[[nodiscard]] std::string BuildHiZReflectionComputeShader();
[[nodiscard]] std::string BuildHiZReflectionBilateralFilterShader();

} // namespace Fast::Oot3d
