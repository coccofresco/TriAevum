#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool CheckTexture(uint8_t format, std::span<const uint8_t> encoded,
                  const std::array<uint8_t, 4>& expected) {
    std::vector<uint8_t> rgba8;
    std::string error;
    if (!ThreeDsRecomp::Oot3d::DecodeOot3dPicaTextureRgba8(
            format, 1, 1, encoded, rgba8, &error)) {
        std::cerr << "decode failed for format " << static_cast<int>(format)
                  << ": " << error << '\n';
        return false;
    }
    if (rgba8.size() != expected.size() ||
        !std::equal(rgba8.begin(), rgba8.end(), expected.begin())) {
        std::cerr << "decoded channels differ for format "
                  << static_cast<int>(format) << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::array<uint8_t, 8U * 8U * 4U> rgbaEncoded{};
    rgbaEncoded[0] = 40;
    rgbaEncoded[1] = 30;
    rgbaEncoded[2] = 20;
    rgbaEncoded[3] = 10;
    if (!CheckTexture(0, rgbaEncoded, {10, 20, 30, 40})) {
        return 1;
    }

    std::array<uint8_t, 8U * 8U * 2U> rgEncoded{};
    rgEncoded[0] = 77;
    rgEncoded[1] = 33;
    if (!CheckTexture(6, rgEncoded, {33, 77, 0, 255})) {
        return 1;
    }

    std::array<uint8_t, 8U * 8U> alphaEncoded{};
    alphaEncoded[0] = 64;
    if (!CheckTexture(8, alphaEncoded, {0, 0, 0, 64})) {
        return 1;
    }
    std::cout << "native PICA texture tests passed\n";
    return 0;
}
