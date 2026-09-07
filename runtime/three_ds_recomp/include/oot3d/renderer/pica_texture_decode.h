#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Oot3d::Renderer {

bool DecodePicaTextureRgba8(
    uint8_t nativeFormat, uint16_t width, uint16_t height,
    std::span<const uint8_t> nativeBytes, std::vector<uint8_t>& rgba8,
    std::string* error = nullptr);

} // namespace Oot3d::Renderer
