#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Fast::Oot3d {

bool DetilePicaShadow2d(std::span<const uint8_t> tiledBytes,
                        uint32_t width, uint32_t height,
                        std::vector<uint32_t>& linearPixels,
                        std::string* error = nullptr);

[[nodiscard]] float DecodePicaFloat16(uint16_t bits);
[[nodiscard]] uint32_t PackPicaShadow2d(uint32_t depth24,
                                        uint32_t penumbra8);
[[nodiscard]] uint32_t UpdatePicaShadow2d(uint32_t packedPixel,
                                          uint32_t fragmentDepth24,
                                          uint32_t fragmentPenumbra8,
                                          float biasConstant,
                                          float biasLinear);
[[nodiscard]] float ComparePicaShadow2d(uint32_t packedPixel,
                                        uint32_t fragmentDepth24);

} // namespace Fast::Oot3d
