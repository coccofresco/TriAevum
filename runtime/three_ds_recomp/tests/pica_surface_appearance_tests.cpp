#include "fast/renderer3ds/pica_surface_appearance.h"
#include "fast/renderer3ds/pica_surface_debug.h"

#include <iostream>
#include <stdexcept>

using namespace Fast::Renderer3ds;

static void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static NativeBlendState Blend(bool enabled, NativeBlendEquation equation, NativeBlendFactor source,
                              NativeBlendFactor destination) {
    NativeBlendState blend;
    blend.Enabled = enabled;
    blend.EquationRgb = equation;
    blend.SourceRgb = source;
    blend.DestRgb = destination;
    return blend;
}

int main() {
    using F = NativeBlendFactor;
    using E = NativeBlendEquation;
    using A = PicaSurfaceAppearance;
    const auto opaque = Blend(false, E::Add, F::One, F::Zero);
    const auto replace = Blend(true, E::Add, F::One, F::Zero);
    const auto alpha = Blend(true, E::Add, F::SourceAlpha, F::OneMinusSourceAlpha);
    const auto additive = Blend(true, E::Add, F::SourceAlpha, F::One);
    const auto additiveOne = Blend(true, E::Add, F::One, F::One);
    int checks = 0;
    // Lit world surfaces (vertex or fragment lighting) stay relightable, whatever their transparency.
    for (const auto& blend : {opaque, replace, alpha}) {
        expect(ClassifyPicaSurfaceAppearance(blend, true, false) == A::Relightable, "vertex-lit surface");
        expect(ClassifyPicaSurfaceAppearance(blend, false, true) == A::Relightable, "fragment-lit surface");
        expect(ClassifyPicaSurfaceAppearance(blend, true, true) == A::Relightable, "fully lit surface");
        checks += 3;
    }
    // Additive light keeps its appearance, lit or not (Kakariko windows, Temple of Time shafts, torch flames).
    for (const auto& blend : {additive, additiveOne})
        for (bool vertexLit : {false, true})
            for (bool fragmentLit : {false, true}) {
                expect(ClassifyPicaSurfaceAppearance(blend, vertexLit, fragmentLit) == A::AdditiveComposition,
                       "additive composition");
                ++checks;
            }
    // A disabled blend is not additive even with additive factors left in the registers.
    expect(ClassifyPicaSurfaceAppearance(Blend(false, E::Add, F::SourceAlpha, F::One), true, false) == A::Relightable,
           "disabled blend");
    // Subtractive or min/max composition with destination One is not additive light.
    expect(ClassifyPicaSurfaceAppearance(Blend(true, E::ReverseSubtract, F::SourceAlpha, F::One), true, false) ==
               A::Relightable, "reverse subtract");
    expect(ClassifyPicaSurfaceAppearance(Blend(true, E::Max, F::One, F::One), true, false) == A::Relightable,
           "max");
    checks += 3;
    // Colour no light produced (smoke, particles), opaque or blended.
    for (const auto& blend : {opaque, replace, alpha}) {
        expect(ClassifyPicaSurfaceAppearance(blend, false, false) == A::Unlit, "unlit");
        ++checks;
    }

    // The debug TEV source mask: primary colour x texture 0, through previous and buffer.
    PicaTevProgram tev{};
    // Stage 0: modulate (operation 1) of primary colour (0) and texture 0 (3); later stages pass previous (15).
    tev.Stages[0] = {0x00000030U, 0U, 1U, 0U};
    for (size_t stage = 1; stage < tev.Stages.size(); ++stage) tev.Stages[stage] = {0x0000000FU, 0U, 0U, 0U};
    expect(PicaTevColorSourceMask(tev) == ((1U << 0U) | (1U << 3U)), "primary x texture 0");
    // Stage 5 replaced by the constant colour (14): only the constant reaches the output.
    tev.Stages[5] = {0x0000000EU, 0U, 0U, 0U};
    expect(PicaTevColorSourceMask(tev) == (1U << 14U), "constant only");
    // Stage 0 updates the buffer; stage 5 reads it (13).
    tev.Control[0] = 1U << 8U;
    tev.Stages[5] = {0x0000000DU, 0U, 0U, 0U};
    expect(PicaTevColorSourceMask(tev) == ((1U << 0U) | (1U << 3U)), "buffer carries stage 0");
    checks += 3;

    // Debug exemption ranges: none by default, inclusive when set, latched whole at the next frame.
    auto& debug = PicaSurfaceDebug::Instance();
    debug.BeginFrame();
    expect(!debug.Exempt(0) && !debug.Exempt(5), "no exemption by default");
    debug.SetExemptRange(3, 4);
    expect(!debug.Exempt(3), "not before the next frame");
    debug.BeginFrame();
    expect(!debug.Exempt(2) && debug.Exempt(3) && debug.Exempt(4) && !debug.Exempt(5), "inclusive range");
    debug.SetExemptRange(1, 0);
    debug.BeginFrame();
    expect(!debug.Exempt(0) && !debug.Exempt(1), "cleared");
    checks += 4;
    std::cout << "surface appearance tests passed: " << checks << " checks\n";
}
