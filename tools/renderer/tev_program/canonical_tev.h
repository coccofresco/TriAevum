#pragma once
#include "../../oot3d/native_pica_frontend/oot3d_native_pica_tev_expressions.h"
#include <sstream>

// Uses the production specialized expression emitters, not the scalar oracle.
inline std::string CanonicalTev(const std::vector<Case>& tests) {
    using namespace Oot3dNativeGame::TevExpressions;
    std::ostringstream s;
    s << ByteRoundHelpers << "vec4 canonical(uint index, PicaTevInputs inputs) {\n"
         "precise vec4 rounded_primary_color=vec4(byteround3(inputs.primary.rgb),byteround1(inputs.primary.a));\n"
         "vec4 combiner_buffer=vec4(0), pending=inputs.buffer_color, previous=vec4(0);\n"
         "switch(index % 64u) {\n";
    for (size_t n = 0; n < 64; ++n) {
        s << "case " << n << "u: {\n";
        bool supported = true;
        for (size_t stage = 0; stage < 6; ++stage) {
            const auto& w = tests[n].Program.Stages[stage];
            const auto source = [&](uint32_t v) -> std::string {
                if (v == 0) return "rounded_primary_color";
                if (v == 1) return "inputs.primary_fragment";
                if (v == 2) return "inputs.secondary_fragment";
                if (v <= 6) return "inputs.textures[" + std::to_string(v - 3) + "]";
                if (v == 13) return "combiner_buffer";
                if (v == 14) return "inputs.constants[" + std::to_string(stage) + "]";
                return "previous";
            };
            s << "{\n";
            std::array<std::string, 3> colors, alphas;
            for (uint32_t i = 0; i < 3; ++i) {
                auto rgb = (w[0] >> (4*i)) & 15u;
                auto alpha = (w[0] >> (16+4*i)) & 15u;
                if (!stage && i < 2) {
                    if (rgb == 15) rgb = (w[0] >> 8) & 15;
                    if (alpha == 15) alpha = (w[0] >> 24) & 15;
                }
                colors[i] = ColorModifier(source(rgb), (w[1]>>(4*i))&15, supported);
                alphas[i] = AlphaModifier(source(alpha), (w[1]>>(12+4*i))&7, supported);
            }
            s << "precise vec3 rgb=byteround3(clamp(" << ColorOperation(w[2]&15,colors[0],colors[1],colors[2],supported)
              << ",0.0,1.0));\n";
            s << "precise float alpha=";
            if ((w[2]&15) == 7) s << "byteround1(clamp(rgb.r,0.0,1.0))";
            else s << "byteround1(clamp(" << AlphaOperation((w[2]>>16)&15,alphas[0],alphas[1],alphas[2],supported) << ",0.0,1.0))";
            const auto scale = [](unsigned v) { return v == 3 ? 1 : 1 << v; };
            s << ";\nprevious=clamp(vec4(rgb*" << scale(w[3]&3) << ".0,alpha*" << scale((w[3]>>16)&3) << ".0),0.0,1.0);\ncombiner_buffer=pending;\n";
            if (stage < 4) {
                if (tests[n].Program.Control[0] & (1u << (8+stage))) s << "pending.rgb=previous.rgb;\n";
                if (tests[n].Program.Control[0] & (1u << (12+stage))) s << "pending.a=previous.a;\n";
            }
            s << "}\n";
        }
        Require(supported, "canonical expression rejected");
        s << "break;}\n";
    }
    s << "}\nreturn previous;}\n";
    return s.str();
}
