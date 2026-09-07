#pragma once

#include "oot3d/renderer/pica_texture_decode.h"

namespace Fast::Oot3d {

inline bool DecodePicaTextureRgba8(
    uint8_t nativeFormat, uint16_t width, uint16_t height,
    std::span<const uint8_t> nativeBytes, std::vector<uint8_t>& rgba8,
    std::string* error = nullptr) {
    return ::Oot3d::Renderer::DecodePicaTextureRgba8(
        nativeFormat, width, height, nativeBytes, rgba8, error);
}

} // namespace Fast::Oot3d
