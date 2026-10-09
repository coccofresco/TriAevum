#include "fast/renderer/framebuffer_readback.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void Check(bool ok) { if (!ok) std::abort(); }
}
int main() {
    using Fast::Renderer::CopyScaledFramebufferRgba8;
    // Preserve all 256 values, including dark halo ramps and sky low bits.
    std::vector<uint8_t> ramp(256U * 2U * 4U);
    for (size_t y = 0; y < 2; ++y) for (size_t x = 0; x < 256; ++x) {
        const auto i = (y * 256U + x) * 4U;
        ramp[i] = static_cast<uint8_t>(x);
        ramp[i + 1] = static_cast<uint8_t>(255U - x);
        ramp[i + 2] = static_cast<uint8_t>(x ^ (y ? 0x55U : 0xAAU));
        ramp[i + 3] = static_cast<uint8_t>(x ^ 0x17U);
    }
    std::vector<uint8_t> exact(ramp.size(), 0xCCU);
    Check(CopyScaledFramebufferRgba8(ramp, 256, 2, false, exact, 256, 2));
    Check(exact == ramp);
    auto bgraRamp = ramp;
    for (size_t i = 0; i < bgraRamp.size(); i += 4) std::swap(bgraRamp[i], bgraRamp[i + 2]);
    Check(CopyScaledFramebufferRgba8(bgraRamp, 256, 2, true, exact, 256, 2));
    Check(exact == ramp);
    std::vector<uint8_t> scaled(512U * 4U * 4U);
    Check(CopyScaledFramebufferRgba8(bgraRamp, 256, 2, true, scaled, 512, 4));
    for (size_t y = 0; y < 4; ++y) for (size_t x = 0; x < 512; ++x)
        for (size_t c = 0; c < 4; ++c)
            Check(scaled[(y * 512U + x) * 4U + c] == ramp[((y / 2) * 256U + x / 2) * 4U + c]);
    Check(CopyScaledFramebufferRgba8(scaled, 512, 4, false, exact, 256, 2));
    Check(exact == ramp);
    Check(!CopyScaledFramebufferRgba8(std::span(ramp).first(ramp.size() - 1), 256, 2, false, exact, 256, 2));
    Check(!CopyScaledFramebufferRgba8(ramp, 256, 2, false, std::span(exact).first(exact.size() - 1), 256, 2));
    Check(!CopyScaledFramebufferRgba8(ramp, UINT32_MAX, UINT32_MAX, false, exact, 256, 2));
    Check(!CopyScaledFramebufferRgba8(ramp, 256, 2, false, exact, UINT32_MAX, UINT32_MAX));
    Check(!CopyScaledFramebufferRgba8(ramp, 0, 2, false, exact, 256, 2));
    Check(exact == ramp); // Rejected reads leave the destination intact.
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
