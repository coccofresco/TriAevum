#include "fast/renderer3ds/pica_tev_program.h"

#include <algorithm>
#include <cmath>

namespace Fast::Renderer3ds {

namespace {

using Rgba = std::array<float, 4>;

float Round255(float value) noexcept { return std::floor(value * 255.0F + 0.5F) / 255.0F; }

Rgba Source(uint32_t selector, size_t stage, const PicaTevInputs& inputs, const Rgba& buffer,
            const Rgba& previous) noexcept {
    switch (selector) {
    case 0U: return inputs.Primary;
    case 1U: return inputs.PrimaryFragment;
    case 2U: return inputs.SecondaryFragment;
    case 3U: case 4U: case 5U: case 6U: return inputs.Textures[selector - 3U];
    case 13U: return buffer;
    case 14U: return inputs.Constants[stage];
    case 15U: return previous;
    default: return {};
    }
}

Rgba ColorOperand(const Rgba& v, uint32_t op) noexcept {
    Rgba value = v;
    switch (op & 14U) {
    case 2U: value = {v[3], v[3], v[3], 0.0F}; break;
    case 4U: value = {v[0], v[0], v[0], 0.0F}; break;
    case 8U: value = {v[1], v[1], v[1], 0.0F}; break;
    case 12U: value = {v[2], v[2], v[2], 0.0F}; break;
    default: break;
    }
    if ((op & 1U) != 0U)
        for (size_t c = 0; c < 3U; ++c) value[c] = 1.0F - value[c];
    value[3] = 0.0F;
    return value;
}

float AlphaOperand(const Rgba& v, uint32_t op) noexcept {
    const float value = op < 2U ? v[3] : (op < 4U ? v[0] : (op < 6U ? v[1] : v[2]));
    return (op & 1U) != 0U ? 1.0F - value : value;
}

Rgba Operation(uint32_t op, const Rgba& a, const Rgba& b, const Rgba& c) noexcept {
    Rgba result{};
    if (op == 6U || op == 7U) {
        float dot = 0.0F;
        for (size_t i = 0; i < 3U; ++i) dot += (a[i] - 0.5F) * (b[i] - 0.5F);
        result.fill(dot * 4.0F);
        return result;
    }
    for (size_t i = 0; i < 4U; ++i) {
        switch (op) {
        case 0U: result[i] = a[i]; break;
        case 1U: result[i] = a[i] * b[i]; break;
        case 2U: result[i] = a[i] + b[i]; break;
        case 3U: result[i] = a[i] + b[i] - 0.5F; break;
        case 4U: result[i] = b[i] + (a[i] - b[i]) * c[i]; break;
        case 5U: result[i] = a[i] - b[i]; break;
        case 8U: result[i] = a[i] * b[i] + c[i]; break;
        case 9U: result[i] = std::min(a[i] + b[i], 1.0F) * c[i]; break;
        default: break;
        }
    }
    return result;
}

} // namespace

std::array<float, 4> EvaluatePicaTevProgram(const PicaTevProgram& program, const PicaTevInputs& inputs) noexcept {
    Rgba buffer{}, nextBuffer = inputs.BufferColor, previous{};
    for (size_t stage = 0; stage < program.Stages.size(); ++stage) {
        const auto& words = program.Stages[stage];
        std::array<Rgba, 3> colors{}, alphas{};
        for (uint32_t input = 0; input < 3U; ++input) {
            uint32_t rgb = (words[0] >> (input * 4U)) & 15U;
            uint32_t alpha = (words[0] >> (16U + input * 4U)) & 15U;
            // Native stage-zero PREVIOUS aliases source C for A/B only.
            if (stage == 0U && input < 2U) {
                if (rgb == 15U) rgb = (words[0] >> 8U) & 15U;
                if (alpha == 15U) alpha = (words[0] >> 24U) & 15U;
            }
            colors[input] =
                ColorOperand(Source(rgb, stage, inputs, buffer, previous), (words[1] >> (input * 4U)) & 15U);
            alphas[input].fill(AlphaOperand(Source(alpha, stage, inputs, buffer, previous),
                                            (words[1] >> (12U + input * 4U)) & 7U));
        }
        const uint32_t colorOp = words[2] & 15U;
        Rgba color = Operation(colorOp, colors[0], colors[1], colors[2]);
        for (float& value : color) value = Round255(std::clamp(value, 0.0F, 1.0F));
        const float alpha = colorOp == 7U
            ? color[0]
            : Round255(std::clamp(Operation((words[2] >> 16U) & 15U, alphas[0], alphas[1], alphas[2])[0], 0.0F, 1.0F));
        const uint32_t rgbScale = words[3] & 3U;
        const uint32_t alphaScale = (words[3] >> 16U) & 3U;
        const float rgbFactor = static_cast<float>(rgbScale < 3U ? 1U << rgbScale : 1U);
        const float alphaFactor = static_cast<float>(alphaScale < 3U ? 1U << alphaScale : 1U);
        for (size_t c = 0; c < 3U; ++c) previous[c] = std::clamp(color[c] * rgbFactor, 0.0F, 1.0F);
        previous[3] = std::clamp(alpha * alphaFactor, 0.0F, 1.0F);
        // Buffer updates become visible one stage later than PREVIOUS.
        buffer = nextBuffer;
        if (stage < 4U) {
            if ((program.Control[0] & (1U << (8U + stage))) != 0U)
                for (size_t c = 0; c < 3U; ++c) nextBuffer[c] = previous[c];
            if ((program.Control[0] & (1U << (12U + stage))) != 0U) nextBuffer[3] = previous[3];
        }
    }
    return previous;
}

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
        const auto inputMask = [](uint32_t operation) {
            return operation == 0 ? 1U :
                   (operation == 4 || operation == 8 || operation == 9) ? 7U : 3U;
        };
        for (uint32_t channel = 0; channel < 2; ++channel) {
            if (channel == 1 && color == 7) continue;
            const auto mask = inputMask(channel == 0 ? color : alpha);
            for (uint32_t input = 0; input < 3; ++input) {
                if ((mask & (1U << input)) == 0) continue;
                auto source = (words[0] >> (channel * 16 + input * 4)) & 15;
                if (stage == 0 && input < 2 && source == 15)
                    source = (words[0] >> (channel * 16 + 8)) & 15;
                if (source >= 3 && source <= 6) candidate.Control[1] |= 1U << (source - 3);
            }
        }
    }
    candidate.Control[0] = registers[0xE0] & 0xFF00;
    output = candidate;
    return PicaTevDecodeError::None;
}

uint8_t PicaTevMaterialTextureMask(const PicaTevProgram& program) noexcept {
    uint8_t previous = 0U, buffer = 0U, nextBuffer = 0U;
    for (size_t stage = 0; stage < program.Stages.size(); ++stage) {
        const auto& words = program.Stages[stage];
        const auto operation = words[2] & 15U;
        const auto inputs = operation == 0U ? 1U :
            (operation == 4U || operation == 8U || operation == 9U) ? 3U : 2U;
        uint8_t color = 0U;
        for (uint32_t input = 0U; input < inputs; ++input) {
            if (((words[1] >> (input * 4U)) & 14U) != 0U) continue;
            auto source = (words[0] >> (input * 4U)) & 15U;
            if (stage == 0U && input < 2U && source == 15U)
                source = (words[0] >> 8U) & 15U;
            if (source >= 3U && source <= 6U)
                color |= static_cast<uint8_t>(1U << (source - 3U));
            else if (source == 13U) color |= buffer;
            else if (source == 15U) color |= previous;
        }
        previous = color;
        buffer = nextBuffer;
        if (stage < 4U && (program.Control[0] & (1U << (8U + stage))) != 0U)
            nextBuffer = color;
    }
    return previous;
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
    case 0u: return inputs.primary;
    case 1u: return inputs.primary_fragment;
    case 2u: return inputs.secondary_fragment;
    case 3u: return inputs.textures[0];
    case 4u: return inputs.textures[1];
    case 5u: return inputs.textures[2];
    case 6u: return inputs.textures[3];
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
void pica_tev_input(uvec4 words, int stage, uint input_index,
                    PicaTevInputs inputs, vec4 combiner_buffer, vec4 previous,
                    out vec4 color, out vec4 alpha_value) {
    uint rgb = (words.x >> (input_index * 4u)) & 15u;
    uint alpha = (words.x >> (16u + input_index * 4u)) & 15u;
    // Native stage-zero PREVIOUS aliases source C for A/B only.
    if (stage == 0 && input_index < 2u) {
        if (rgb == 15u) rgb = (words.x >> 8u) & 15u;
        if (alpha == 15u) alpha = (words.x >> 24u) & 15u;
    }
    vec4 cv = pica_tev_source(rgb, stage, inputs, combiner_buffer, previous);
    vec4 av = pica_tev_source(alpha, stage, inputs, combiner_buffer, previous);
    color = vec4(pica_tev_color_operand(cv, (words.y >> (input_index * 4u)) & 15u), 0.0);
    alpha_value = vec4(pica_tev_alpha_operand(av, (words.y >> (12u + input_index * 4u)) & 7u));
}
// Resolved primary includes native quantization and authorized lighting hooks.
vec4 pica_evaluate_tev_resolved(PicaTevProgram program, PicaTevInputs inputs) {
    vec4 combiner_buffer = vec4(0.0);
    vec4 next_buffer = inputs.buffer_color;
    vec4 previous = vec4(0.0);
    for (int stage = 0; stage < 6; ++stage) {
        uvec4 words = program.stages[stage];
        vec4 colors[3];
        vec4 alphas[3];
        // Fixed operand slots must not become dynamically indexed local arrays.
        pica_tev_input(words, stage, 0u, inputs, combiner_buffer, previous, colors[0], alphas[0]);
        pica_tev_input(words, stage, 1u, inputs, combiner_buffer, previous, colors[1], alphas[1]);
        pica_tev_input(words, stage, 2u, inputs, combiner_buffer, previous, colors[2], alphas[2]);
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
vec4 pica_evaluate_tev(PicaTevProgram program, PicaTevInputs inputs) {
    inputs.primary = pica_tev_round(inputs.primary);
    return pica_evaluate_tev_resolved(program, inputs);
}
)glsl";
}
} // namespace Fast::Renderer3ds
