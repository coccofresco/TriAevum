#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Fast::Oot3d {

[[nodiscard]] std::array<float, 3> ResolveReflectionDebugColor(
    uint32_t mode, const std::array<float, 4>& material,
    const std::array<float, 4>& reflection) noexcept;

[[nodiscard]] std::string ReflectionDebugShaderLibrary();

} // namespace Fast::Oot3d
