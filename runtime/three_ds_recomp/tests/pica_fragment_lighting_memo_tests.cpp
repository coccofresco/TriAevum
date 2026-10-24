// Differential test: DecodePicaFragmentLighting memoizes per thread on the
// registers it reads. Every result must equal a decode on a fresh thread,
// whose cache is empty, including after changes outside the decoded range.
#include "fast/extension/pica_fragment_lighting.h"
#include <bit>
#include <cstdio>
#include <thread>
#include <vector>
using namespace Fast::Extension;
static std::vector<uint32_t> Signature(const PicaFragmentLightingState& s) {
    std::vector<uint32_t> out;
    auto f = [&](float v) { out.push_back(std::bit_cast<uint32_t>(v)); };
    auto c = [&](const PicaLightingColor& v) { for (float x : v.Rgb) f(x); };
    out.insert(out.end(), {s.Enabled, s.ActiveLightCount, s.EnvironmentConfiguration, s.FresnelSelector,
        s.BumpTextureUnit, s.ShadowTextureUnit, uint32_t(s.BumpMode), s.ClampHighlights, s.RecalculateBumpVectors,
        s.ShadowFactorEnabled, s.ShadowPrimary, s.ShadowSecondary, s.ShadowAlpha, s.InvertShadow, s.Config0, s.Config1});
    for (auto p : s.LightPermutation) out.push_back(p);
    for (const auto& l : s.Lights) {
        c(l.Specular0); c(l.Specular1); c(l.Diffuse); c(l.Ambient);
        for (float x : l.Position) f(x);
        for (float x : l.SpotDirection) f(x);
        f(l.DistanceAttenuationBias); f(l.DistanceAttenuationScale);
        out.insert(out.end(), {l.Directional, l.TwoSidedDiffuse, l.GeometricFactor0, l.GeometricFactor1,
            l.ShadowEnabled, l.SpotAttenuationEnabled, l.DistanceAttenuationEnabled});
    }
    c(s.GlobalAmbient);
    for (const auto& l : s.LutSamplers) { out.push_back(l.AbsoluteInput); out.push_back(uint32_t(l.Input)); f(l.Scale); }
    return out;
}
int main() {
    std::vector<uint32_t> regs(0x300, 0);
    uint32_t r = 0x1234567u;
    auto next = [&] { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
    int failures = 0, hits = 0;
    for (int i = 0; i < 20000; ++i) {
        switch (next() % 4) {
        case 0: regs[next() % 0x300] = next(); break;                     // anywhere
        case 1: regs[0x140 + next() % (0x1DA - 0x140)] = next(); break;   // lighting range
        case 2: regs[0x08F] ^= 1; break;                                   // enable
        default: regs[next() % 0x140] = next(); ++hits; break;            // outside the range read
        }
        const auto cached = Signature(DecodePicaFragmentLighting(regs));
        std::vector<uint32_t> fresh;
        std::thread([&] { fresh = Signature(DecodePicaFragmentLighting(regs)); }).join();
        if (cached != fresh && ++failures < 5) std::printf("mismatch at %d\n", i);
    }
    std::printf("%s (%d steps outside the decoded range)\n", failures ? "FAILED" : "lighting memo matches fresh decode", hits);
    return failures != 0;
}
