#include "fast/renderer3ds/pica_tev_program.h"

namespace Fast::Renderer3ds {

PicaTevDecodeError DecodePicaTevProgram(std::span<const uint32_t> registers,
                                      PicaTevProgram& output) noexcept {
    output = {};
    if (registers.size() <= 0xFC) return PicaTevDecodeError::TruncatedRegisters;
    constexpr std::array<uint16_t, 6> bases{0xC0, 0xC8, 0xD0, 0xD8, 0xF0, 0xF8};
    PicaTevProgram candidate;
    for (size_t stage = 0; stage < bases.size(); ++stage) {
        const auto base = bases[stage];
        auto& words = candidate.Stages[stage];
        words = {registers[base], registers[base + 1], registers[base + 2], registers[base + 4]};
        for (uint32_t input = 0; input < 3; ++input) {
            for (const auto shift : {input * 4, 16 + input * 4}) {
                const auto source = (words[0] >> shift) & 15;
                if (source > 6 && source < 13) return PicaTevDecodeError::Source;
            }
            const auto operand = (words[1] >> (input * 4)) & 15;
            if (operand == 6 || operand == 7 || operand == 10 || operand == 11 || operand > 13)
                return PicaTevDecodeError::ColorOperand;
        }
        const auto color = words[2] & 15;
        const auto alpha = (words[2] >> 16) & 15;
        if (color > 9) return PicaTevDecodeError::ColorOperation;
        if (color != 7 && (alpha > 9 || alpha == 6 || alpha == 7))
            return PicaTevDecodeError::AlphaOperation;
    }
    candidate.Control[0] = registers[0xE0] & 0xFF00;
    output = candidate;
    return PicaTevDecodeError::None;
}

std::string_view PicaTevProgramGlsl() noexcept {
    return R"glsl(
struct PicaTevProgram {
    uvec4 stages[6];
    uvec4 control;
};
struct PicaTevInputs {
    vec4 primary;
    vec4 primary_fragment;
    vec4 secondary_fragment;
    vec4 textures[4];
    vec4 constants[6];
    vec4 buffer_color;
};
vec4 pica_tev_round(vec4 v) {
    return floor(v * 255.0 + 0.5) / 255.0;
}
vec4 pica_tev_source(uint selector, int stage, PicaTevInputs inputs, vec4 combiner_buffer, vec4 previous) {
    switch (selector) {
    case 0u: return pica_tev_round(inputs.primary);
    case 1u: return inputs.primary_fragment;
    case 2u: return inputs.secondary_fragment;
    case 3u: case 4u: case 5u: case 6u: return inputs.textures[selector - 3u];
    case 13u: return combiner_buffer;
    case 14u: return inputs.constants[stage];
    case 15u: return previous;
    }
    return vec4(0.0);
}
vec3 pica_tev_color_operand(vec4 v, uint op) {
    vec3 value = v.rgb;
    switch (op & 14u) {
    case 2u: value = v.aaa; break;
    case 4u: value = v.rrr; break;
    case 8u: value = v.ggg; break;
    case 12u: value = v.bbb; break;
    }
    return (op & 1u) != 0u ? vec3(1.0) - value : value;
}
float pica_tev_alpha_operand(vec4 v, uint op) {
    float value = op < 2u ? v.a : (op < 4u ? v.r : (op < 6u ? v.g : v.b));
    return (op & 1u) != 0u ? 1.0 - value : value;
}
vec4 pica_tev_operation(uint op, vec4 a, vec4 b, vec4 c) {
    vec4 result;
    switch (op) {
    case 0u: result = a; break;
    case 1u: result = a * b; break;
    case 2u: result = a + b; break;
    case 3u: result = (a + b) - vec4(0.5); break;
    case 4u: result = mix(b, a, c); break;
    case 5u: result = a - b; break;
    case 6u: case 7u: result = vec4(dot(a.rgb - vec3(0.5), b.rgb - vec3(0.5)) * 4.0); break;
    case 8u: result = fma(a, b, c); break;
    case 9u: result = min(a + b, vec4(1.0)) * c; break;
    default: result = vec4(0.0); break;
    }
    return result;
}
vec4 pica_evaluate_tev(PicaTevProgram program, PicaTevInputs inputs) {
    vec4 combiner_buffer = vec4(0.0);
    vec4 next_buffer = inputs.buffer_color;
    vec4 previous = vec4(0.0);
    for (int stage = 0; stage < 6; ++stage) {
        uvec4 words = program.stages[stage];
        vec4 colors[3];
        vec4 alphas[3];
        for (uint input_index = 0u; input_index < 3u; ++input_index) {
            uint rgb = (words.x >> (input_index * 4u)) & 15u;
            uint alpha = (words.x >> (16u + input_index * 4u)) & 15u;
            // Native stage-zero PREVIOUS aliases source C for A/B only.
            if (stage == 0 && input_index < 2u) {
                if (rgb == 15u) rgb = (words.x >> 8u) & 15u;
                if (alpha == 15u) alpha = (words.x >> 24u) & 15u;
            }
            vec4 cv = pica_tev_source(rgb, stage, inputs, combiner_buffer, previous);
            vec4 av = pica_tev_source(alpha, stage, inputs, combiner_buffer, previous);
            colors[input_index] = vec4(pica_tev_color_operand(cv, (words.y >> (input_index * 4u)) & 15u), 0.0);
            alphas[input_index] = vec4(pica_tev_alpha_operand(av, (words.y >> (12u + input_index * 4u)) & 7u));
        }
        uint color_op = words.z & 15u;
        vec4 color = pica_tev_round(clamp(pica_tev_operation(color_op, colors[0], colors[1], colors[2]), 0.0, 1.0));
        float alpha = color_op == 7u ? color.r : pica_tev_round(clamp(
            pica_tev_operation((words.z >> 16u) & 15u, alphas[0], alphas[1], alphas[2]), 0.0, 1.0)).x;
        uint rgb_scale = words.w & 3u;
        uint alpha_scale = (words.w >> 16u) & 3u;
        previous = clamp(vec4(color.rgb * float(rgb_scale < 3u ? 1u << rgb_scale : 1u),
                              alpha * float(alpha_scale < 3u ? 1u << alpha_scale : 1u)), 0.0, 1.0);
        // Buffer updates become visible one stage later than PREVIOUS.
        combiner_buffer = next_buffer;
        if (stage < 4) {
            if ((program.control.x & (1u << (8 + stage))) != 0u) next_buffer.rgb = previous.rgb;
            if ((program.control.x & (1u << (12 + stage))) != 0u) next_buffer.a = previous.a;
        }
    }
    return previous;
}
)glsl";
}
} // namespace Fast::Renderer3ds
