#include "fast/renderer/framebuffer_readback.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void Check(bool ok) { if (!ok) std::abort(); }
}
int main() {
    using Fast::Renderer::CopyScaledFramebufferRgba5551;
    const std::array<uint8_t, 16> rgba{255, 0, 0, 255, 0, 255, 0, 0,
                                      0, 0, 255, 1, 255, 255, 255, 255};
    const std::array<uint16_t, 4> expected{0xf801, 0x07c0, 0x003f, 0xffff};
    std::array<uint16_t, 4> pixels{};
    Check(CopyScaledFramebufferRgba5551(rgba, 2, 2, false, pixels, 2, 2));
    Check(pixels == expected);
    auto bgra = rgba;
    for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    Check(CopyScaledFramebufferRgba5551(bgra, 2, 2, true, pixels, 2, 2));
    Check(pixels == expected);
    std::array<uint16_t, 24> enlarged{};
    Check(CopyScaledFramebufferRgba5551(rgba, 2, 2, false, enlarged, 6, 4));
    for (size_t y = 0; y < 4; ++y)
        for (size_t x = 0; x < 6; ++x) Check(enlarged[y * 6 + x] == expected[(y / 2) * 2 + x / 3]);
    std::vector<uint8_t> retina(6 * 4 * 4);
    for (size_t y = 0; y < 4; ++y)
        for (size_t x = 0; x < 6; ++x)
            for (size_t c = 0; c < 4; ++c) retina[(y * 6 + x) * 4 + c] = rgba[((y / 2) * 2 + x / 3) * 4 + c];
    Check(CopyScaledFramebufferRgba5551(retina, 6, 4, false, pixels, 2, 2));
    Check(pixels == expected); // A top-left crop would return only red.
    pixels.fill(123);
    Check(!CopyScaledFramebufferRgba5551(rgba, 0, 2, false, pixels, 2, 2));
    Check(!CopyScaledFramebufferRgba5551(rgba, 2, 2, false, pixels, 0, 2));
    Check(!CopyScaledFramebufferRgba5551(std::span(rgba).first(15), 2, 2, false, pixels, 2, 2));
    Check(!CopyScaledFramebufferRgba5551(rgba, 2, 2, false, pixels, 3, 2));
    Check(!CopyScaledFramebufferRgba5551(rgba, UINT32_MAX, UINT32_MAX, false, pixels, 2, 2));
    Check(!CopyScaledFramebufferRgba5551(rgba, 2, 2, false, pixels, UINT32_MAX, UINT32_MAX));
    Check((pixels == std::array<uint16_t, 4>{123, 123, 123, 123}));
    std::cout << "framebuffer readback: RGBA/BGRA, up/down/equal sizes and bounds passed\n";
}
