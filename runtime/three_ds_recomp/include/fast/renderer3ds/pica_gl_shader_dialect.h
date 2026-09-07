#pragma once

#include <string>
#include <string_view>

namespace Fast::Renderer3ds {

// Converts a canonical GLSL 4.5/Vulkan PICA shader to the GLSL 4.3 dialect
// accepted by the Mesa OpenGL stack used by libnx. This is a backend-local
// representation; canonical source identity always refers to the input.
[[nodiscard]] bool TranslatePicaShaderToOpenGl43(
    std::string_view canonicalSource, std::string& translatedSource,
    std::string* error = nullptr);

} // namespace Fast::Renderer3ds
