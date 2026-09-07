#include "oot3d_native_pica_proctex.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Oot3dNativeGame {
namespace {

constexpr std::array<uint16_t, 6> kTevStageRegisters{
    0x0C0U, 0x0C8U, 0x0D0U, 0x0D8U, 0x0F0U, 0x0F8U};

struct ProcTexConfig {
    bool Enabled = false;
    bool SeparateAlpha = false;
    bool NoiseEnabled = false;
    uint8_t Coordinate = 0;
    uint8_t UClamp = 0;
    uint8_t VClamp = 0;
    uint8_t ColorCombiner = 0;
    uint8_t AlphaCombiner = 0;
    uint8_t UShift = 0;
    uint8_t VShift = 0;
    uint8_t Filter = 0;
    uint8_t LodMin = 0;
    uint8_t LodMax = 0;
    uint8_t Width = 0;
    std::array<uint8_t, 4> LodOffsets{};
    float Bias = 0.0F;
    std::array<float, 2> NoiseFrequency{};
    std::array<float, 2> NoiseAmplitude{};
    std::array<float, 2> NoisePhase{};
};

void SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

float DecodePicaFloat16(uint16_t raw) {
    constexpr uint32_t kMantissaBits = 10U;
    constexpr uint32_t kExponentBits = 5U;
    constexpr int32_t kExponentBiasAdjustment = 112;
    const uint32_t mantissa = raw & 0x03FFU;
    uint32_t exponent = (raw >> kMantissaBits) & 0x1FU;
    const uint32_t sign = static_cast<uint32_t>(raw >> 15U) << 31U;
    uint32_t ieee = sign;
    if ((raw & 0x7FFFU) != 0U) {
        exponent = exponent == ((1U << kExponentBits) - 1U)
                       ? 255U
                       : exponent + kExponentBiasAdjustment;
        ieee |= mantissa << (23U - kMantissaBits);
        ieee |= exponent << 23U;
    }
    return std::bit_cast<float>(ieee);
}

ProcTexConfig DecodeProcTexConfig(const Oot3dPicaDrawPacket& packet) {
    ProcTexConfig config;
    const uint32_t main = packet.Registers[0x080U];
    const uint32_t proctex = packet.Registers[0x0A8U];
    const uint32_t noiseU = packet.Registers[0x0A9U];
    const uint32_t noiseV = packet.Registers[0x0AAU];
    const uint32_t frequency = packet.Registers[0x0ABU];
    const uint32_t lut = packet.Registers[0x0ACU];
    const uint32_t offsets = packet.Registers[0x0ADU];
    config.Enabled = (main & (1U << 10U)) != 0U;
    config.Coordinate = static_cast<uint8_t>((main >> 8U) & 3U);
    config.UClamp = static_cast<uint8_t>(proctex & 7U);
    config.VClamp = static_cast<uint8_t>((proctex >> 3U) & 7U);
    config.ColorCombiner = static_cast<uint8_t>((proctex >> 6U) & 0xFU);
    config.AlphaCombiner = static_cast<uint8_t>((proctex >> 10U) & 0xFU);
    config.SeparateAlpha = (proctex & (1U << 14U)) != 0U;
    config.NoiseEnabled = (proctex & (1U << 15U)) != 0U;
    config.UShift = static_cast<uint8_t>((proctex >> 16U) & 3U);
    config.VShift = static_cast<uint8_t>((proctex >> 18U) & 3U);
    config.Filter = static_cast<uint8_t>(lut & 7U);
    config.LodMin = static_cast<uint8_t>((lut >> 3U) & 0xFU);
    config.LodMax = static_cast<uint8_t>((lut >> 7U) & 0xFU);
    config.Width = static_cast<uint8_t>((lut >> 11U) & 0xFFU);
    for (size_t level = 0; level < config.LodOffsets.size(); ++level) {
        config.LodOffsets[level] =
            static_cast<uint8_t>(offsets >> (level * 8U));
    }
    const uint16_t biasRaw = static_cast<uint16_t>(
        ((proctex >> 20U) & 0xFFU) | (((lut >> 19U) & 0xFFU) << 8U));
    config.Bias = DecodePicaFloat16(biasRaw);
    config.NoiseFrequency = {
        DecodePicaFloat16(static_cast<uint16_t>(frequency)),
        DecodePicaFloat16(static_cast<uint16_t>(frequency >> 16U)),
    };
    config.NoiseAmplitude = {
        static_cast<float>(static_cast<int16_t>(noiseU & 0xFFFFU)) / 4095.0F,
        static_cast<float>(static_cast<int16_t>(noiseV & 0xFFFFU)) / 4095.0F,
    };
    config.NoisePhase = {
        DecodePicaFloat16(static_cast<uint16_t>(noiseU >> 16U)),
        DecodePicaFloat16(static_cast<uint16_t>(noiseV >> 16U)),
    };
    return config;
}

bool ValidateProcTexConfig(const ProcTexConfig& config, std::string* error) {
    if (!config.Enabled) {
        return true;
    }
    if (config.UClamp > 4U || config.VClamp > 4U ||
        config.ColorCombiner > 9U ||
        (config.SeparateAlpha && config.AlphaCombiner > 9U) ||
        config.UShift > 2U || config.VShift > 2U || config.Filter > 5U ||
        config.Width == 0U) {
        std::ostringstream detail;
        detail << "PICA procedural texture configuration is invalid: clamp="
               << static_cast<uint32_t>(config.UClamp) << ','
               << static_cast<uint32_t>(config.VClamp) << " combiner="
               << static_cast<uint32_t>(config.ColorCombiner) << ','
               << static_cast<uint32_t>(config.AlphaCombiner) << " shift="
               << static_cast<uint32_t>(config.UShift) << ','
               << static_cast<uint32_t>(config.VShift) << " filter="
               << static_cast<uint32_t>(config.Filter) << " width="
               << static_cast<uint32_t>(config.Width);
        SetError(error, detail.str());
        return false;
    }
    return true;
}

std::string FloatLiteral(float value) {
    if (!std::isfinite(value)) {
        return value < 0.0F ? "-3.402823466e+38" : "3.402823466e+38";
    }
    std::ostringstream literal;
    literal << std::scientific
            << std::setprecision(std::numeric_limits<float>::max_digits10)
            << value;
    return literal.str();
}

std::array<float, 2> DecodeProcTexValue(uint32_t raw) {
    const uint32_t differenceBits = (raw >> 12U) & 0xFFFU;
    const int32_t difference = (differenceBits & 0x800U) != 0U
                                   ? static_cast<int32_t>(differenceBits) - 0x1000
                                   : static_cast<int32_t>(differenceBits);
    return {static_cast<float>(raw & 0xFFFU) / 4095.0F,
            static_cast<float>(difference) / 4095.0F};
}

std::array<float, 4> DecodeColor(uint32_t raw) {
    return {
        static_cast<float>(raw & 0xFFU) / 255.0F,
        static_cast<float>((raw >> 8U) & 0xFFU) / 255.0F,
        static_cast<float>((raw >> 16U) & 0xFFU) / 255.0F,
        static_cast<float>((raw >> 24U) & 0xFFU) / 255.0F,
    };
}

std::array<float, 4> DecodeColorDifference(uint32_t raw) {
    std::array<float, 4> result{};
    for (size_t channel = 0; channel < result.size(); ++channel) {
        const auto value = static_cast<int8_t>(
            static_cast<uint8_t>(raw >> (channel * 8U)));
        result[channel] = static_cast<float>(value) * (2.0F / 255.0F);
    }
    return result;
}

void AppendValueLuts(std::ostringstream& source,
                     const Oot3dPicaProcTexLutState& luts) {
    source << "const vec2 pica_proctex_value_lut[384] = vec2[384](\n";
    size_t emitted = 0;
    const auto appendTable = [&](const auto& table) {
        for (const uint32_t raw : table) {
            const auto value = DecodeProcTexValue(raw);
            source << "vec2(" << FloatLiteral(value[0]) << ','
                   << FloatLiteral(value[1]) << ')';
            if (++emitted != 384U) {
                source << ',';
            }
            source << '\n';
        }
    };
    appendTable(luts.Noise);
    appendTable(luts.ColorMap);
    appendTable(luts.AlphaMap);
    source << ");\n";
}

void AppendColorLuts(std::ostringstream& source,
                     const Oot3dPicaProcTexLutState& luts) {
    const auto append = [&](const char* name, const auto& table,
                            const auto& decode) {
        source << "const vec4 " << name << "[256] = vec4[256](\n";
        for (size_t index = 0; index < table.size(); ++index) {
            const auto value = decode(table[index]);
            source << "vec4(" << FloatLiteral(value[0]) << ','
                   << FloatLiteral(value[1]) << ',' << FloatLiteral(value[2])
                   << ',' << FloatLiteral(value[3]) << ')';
            if (index + 1U != table.size()) {
                source << ',';
            }
            source << '\n';
        }
        source << ");\n";
    };
    append("pica_proctex_color_lut", luts.Color,
           [](uint32_t raw) { return DecodeColor(raw); });
    append("pica_proctex_color_diff_lut", luts.ColorDifference,
           [](uint32_t raw) { return DecodeColorDifference(raw); });
}

std::string ShiftExpression(const char* coordinate, uint8_t mode,
                            uint8_t clamp) {
    if (mode == 0U) {
        return "0.0";
    }
    const char* offset = clamp == 3U ? "1.0" : "0.5";
    if (mode == 1U) {
        return std::string(offset) + " * float((int(" + coordinate +
               ") / 2) % 2)";
    }
    return std::string(offset) + " * float(((int(" + coordinate +
           ") + 1) / 2) % 2)";
}

void AppendClamp(std::ostringstream& source, const char* value,
                 uint8_t mode) {
    switch (mode) {
    case 0U:
        source << value << " = " << value << " > 1.0 ? 0.0 : " << value
               << ";\n";
        break;
    case 1U:
        source << value << " = min(" << value << ", 1.0);\n";
        break;
    case 2U:
        source << value << " = fract(" << value << ");\n";
        break;
    case 3U:
        source << value << " = int(" << value << ") % 2 == 0 ? fract("
               << value << ") : 1.0 - fract(" << value << ");\n";
        break;
    case 4U:
        source << value << " = " << value << " > 0.5 ? 1.0 : 0.0;\n";
        break;
    }
}

std::string CombinedCoordinate(uint8_t combiner) {
    switch (combiner) {
    case 0U:
        return "u";
    case 1U:
        return "(u * u)";
    case 2U:
        return "v";
    case 3U:
        return "(v * v)";
    case 4U:
        return "((u + v) * 0.5)";
    case 5U:
        return "((u * u + v * v) * 0.5)";
    case 6U:
        return "min(sqrt(u * u + v * v), 1.0)";
    case 7U:
        return "min(u, v)";
    case 8U:
        return "max(u, v)";
    case 9U:
        return "min(((u + v) * 0.5 + sqrt(u * u + v * v)) * 0.5, 1.0)";
    default:
        return "0.0";
    }
}

void AppendNoise(std::ostringstream& source, const ProcTexConfig& config) {
    source << "const vec2 pica_proctex_noise_f = vec2("
           << FloatLiteral(config.NoiseFrequency[0]) << ','
           << FloatLiteral(config.NoiseFrequency[1]) << ");\n"
           << "const vec2 pica_proctex_noise_a = vec2("
           << FloatLiteral(config.NoiseAmplitude[0]) << ','
           << FloatLiteral(config.NoiseAmplitude[1]) << ");\n"
           << "const vec2 pica_proctex_noise_p = vec2("
           << FloatLiteral(config.NoisePhase[0]) << ','
           << FloatLiteral(config.NoisePhase[1]) << ");\n"
           << R"(
int pica_proctex_noise_rand_1d(int v) {
    const int table[16] = int[16](0,4,10,8,4,9,7,12,5,15,13,14,11,15,2,11);
    return (((v % 9 + 2) * 3) & 0xF) ^ table[(v / 9) & 0xF];
}
float pica_proctex_noise_rand_2d(vec2 point) {
    const int table[16] = int[16](10,2,15,8,0,7,4,5,5,13,2,6,13,9,3,14);
    int u2 = pica_proctex_noise_rand_1d(int(point.x));
    int v2 = pica_proctex_noise_rand_1d(int(point.y));
    v2 += ((u2 & 3) == 1) ? 4 : 0;
    v2 ^= (u2 & 1) * 6;
    v2 += 10 + u2;
    v2 &= 0xF;
    v2 ^= table[u2];
    return -1.0 + float(v2) * (2.0 / 15.0);
}
float pica_proctex_noise_coef(vec2 x) {
    vec2 grid = 9.0 * pica_proctex_noise_f * abs(x + pica_proctex_noise_p);
    vec2 point = floor(grid);
    vec2 fraction = grid - point;
    float g0 = pica_proctex_noise_rand_2d(point) * (fraction.x + fraction.y);
    float g1 = pica_proctex_noise_rand_2d(point + vec2(1.0, 0.0)) * (fraction.x + fraction.y - 1.0);
    float g2 = pica_proctex_noise_rand_2d(point + vec2(0.0, 1.0)) * (fraction.x + fraction.y - 1.0);
    float g3 = pica_proctex_noise_rand_2d(point + vec2(1.0, 1.0)) * (fraction.x + fraction.y - 2.0);
    float x_noise = pica_proctex_lookup_value(0, fraction.x);
    float y_noise = pica_proctex_lookup_value(0, fraction.y);
    return mix(mix(g0, g1, x_noise), mix(g2, g3, x_noise), y_noise);
}
)";
}

void AppendColorSampler(std::ostringstream& source,
                        const ProcTexConfig& config) {
    source << "vec4 pica_sample_proctex_color(float lut_coord, int level) {\n"
           << "    int lut_width = " << static_cast<uint32_t>(config.Width)
           << " >> level;\n"
           << "    const int lut_offsets[8] = int[8]("
           << static_cast<uint32_t>(config.LodOffsets[0]) << ','
           << static_cast<uint32_t>(config.LodOffsets[1]) << ','
           << static_cast<uint32_t>(config.LodOffsets[2]) << ','
           << static_cast<uint32_t>(config.LodOffsets[3])
           << ",240,248,252,254);\n"
           << "    int lut_offset = lut_offsets[level];\n"
           << "    lut_coord *= float(lut_width - 1);\n";
    if (config.Filter == 1U || config.Filter == 3U || config.Filter == 5U) {
        source << "    int lut_index_i = int(lut_coord) + lut_offset;\n"
                  "    float lut_index_f = fract(lut_coord);\n"
                  "    return pica_proctex_color_lut[lut_index_i] + "
                  "lut_index_f * pica_proctex_color_diff_lut[lut_index_i];\n";
    } else {
        source << "    lut_coord += float(lut_offset);\n"
                  "    return pica_proctex_color_lut[int(round(lut_coord))];\n";
    }
    source << "}\n";
}

void AppendMainSampler(std::ostringstream& source,
                       const ProcTexConfig& config) {
    source << "vec4 pica_sample_proctex() {\n"
           << "    vec2 uv = abs(pica_texcoord"
           << static_cast<uint32_t>(config.Coordinate < 3U
                                        ? config.Coordinate
                                        : 0U)
           << ");\n"
              "    vec2 duv = max(abs(dFdx(uv)), abs(dFdy(uv)));\n"
           << "    const float proctex_bias = " << FloatLiteral(config.Bias)
           << ";\n"
           << "    float lod = log2(abs(float("
           << static_cast<uint32_t>(config.Width)
           << ") * proctex_bias) * (duv.x + duv.y));\n"
              "    if (proctex_bias == 0.0) lod = 0.0;\n"
           << "    lod = clamp(lod, "
           << FloatLiteral(static_cast<float>(config.LodMin)) << ", "
           << FloatLiteral(static_cast<float>(
                  std::min<uint8_t>(config.LodMax, 7U)))
           << ");\n"
           << "    float u_shift = "
           << ShiftExpression("uv.y", config.UShift, config.UClamp) << ";\n"
           << "    float v_shift = "
           << ShiftExpression("uv.x", config.VShift, config.VClamp) << ";\n";
    if (config.NoiseEnabled) {
        source << "    uv += pica_proctex_noise_a * "
                  "pica_proctex_noise_coef(uv);\n"
                  "    uv = abs(uv);\n";
    }
    source << "    float u = uv.x + u_shift;\n"
              "    float v = uv.y + v_shift;\n";
    AppendClamp(source, "u", config.UClamp);
    AppendClamp(source, "v", config.VClamp);
    source << "    float lut_coord = pica_proctex_lookup_value(128, "
           << CombinedCoordinate(config.ColorCombiner) << ");\n";
    switch (config.Filter) {
    case 0U:
    case 1U:
        source << "    vec4 final_color = "
                  "pica_sample_proctex_color(lut_coord, 0);\n";
        break;
    case 2U:
    case 3U:
        source << "    vec4 final_color = pica_sample_proctex_color("
                  "lut_coord, int(round(lod)));\n";
        break;
    case 4U:
    case 5U:
        source << "    int lod_i = int(lod);\n"
                  "    float lod_f = fract(lod);\n"
                  "    vec4 final_color = mix(pica_sample_proctex_color("
                  "lut_coord, lod_i), pica_sample_proctex_color(lut_coord, "
                  "lod_i + 1), lod_f);\n";
        break;
    }
    if (config.SeparateAlpha) {
        source << "    float final_alpha = pica_proctex_lookup_value(256, "
               << CombinedCoordinate(config.AlphaCombiner) << ");\n"
                  "    return vec4(final_color.rgb, final_alpha);\n";
    } else {
        source << "    return final_color;\n";
    }
    source << "}\n";
}

} // namespace

bool Oot3dPicaReferencesProceduralTexture(
    const Oot3dPicaDrawPacket& packet) {
    for (const uint16_t base : kTevStageRegisters) {
        const uint32_t sources = packet.Registers[base];
        for (size_t input = 0; input < 3U; ++input) {
            if (((sources >> (input * 4U)) & 0xFU) == 0x6U ||
                ((sources >> (16U + input * 4U)) & 0xFU) == 0x6U) {
                return true;
            }
        }
    }
    return false;
}

bool Oot3dPicaProceduralTextureConfigurationSupported(
    const Oot3dPicaDrawPacket& packet) {
    return ValidateProcTexConfig(DecodeProcTexConfig(packet), nullptr);
}

bool GenerateOot3dPicaProceduralTextureSampler(
    const Oot3dPicaDrawPacket& packet, std::string& generated,
    std::string* error) {
    generated.clear();
    const ProcTexConfig config = DecodeProcTexConfig(packet);
    if (!config.Enabled) {
        return true;
    }
    if (!ValidateProcTexConfig(config, error)) {
        return false;
    }

    std::ostringstream source;
    AppendValueLuts(source, packet.ProcTexLuts);
    AppendColorLuts(source, packet.ProcTexLuts);
    source << R"(
float pica_proctex_lookup_value(int offset, float coord) {
    coord *= 128.0;
    float index_i = clamp(floor(coord), 0.0, 127.0);
    float index_f = coord - index_i;
    vec2 entry = pica_proctex_value_lut[int(index_i) + offset];
    return clamp(entry.x + entry.y * index_f, 0.0, 1.0);
}
)";
    if (config.NoiseEnabled) {
        AppendNoise(source, config);
    }
    AppendColorSampler(source, config);
    AppendMainSampler(source, config);
    generated = source.str();
    return true;
}

} // namespace Oot3dNativeGame
