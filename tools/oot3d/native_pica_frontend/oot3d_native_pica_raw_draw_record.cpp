#include "oot3d_native_pica_raw_draw_record.h"

#include <array>
#include <bit>
#include <charconv>
#include <cstdio>
#include <functional>
#include <span>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {
using Json = nlohmann::json;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::string Hex64(uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

uint64_t ParseHex64(const Json& value) {
    const auto& text = value.get_ref<const std::string&>();
    uint64_t parsed = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
    if (ec != std::errc{} || end != text.data() + text.size())
        throw std::runtime_error("invalid hexadecimal identity");
    return parsed;
}

// Words as consecutive eight-digit hexadecimal groups. Trailing zero words
// are dropped; decoding zero-fills the destination, so the round trip is exact.
std::string EncodeWords(std::span<const uint32_t> words) {
    size_t used = words.size();
    while (used > 0U && words[used - 1U] == 0U) --used;
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text(used * 8U, '0');
    for (size_t index = 0; index < used; ++index)
        for (size_t nibble = 0; nibble < 8U; ++nibble)
            text[index * 8U + nibble] = kDigits[(words[index] >> (28U - nibble * 4U)) & 0xFU];
    return text;
}

void DecodeWords(std::string_view text, std::span<uint32_t> words) {
    if (text.size() % 8U != 0U || text.size() / 8U > words.size())
        throw std::runtime_error("raw draw word array has an invalid length");
    std::fill(words.begin(), words.end(), 0U);
    for (size_t index = 0; index < text.size() / 8U; ++index) {
        uint32_t value = 0;
        const char* begin = text.data() + index * 8U;
        const auto [end, ec] = std::from_chars(begin, begin + 8U, value, 16);
        if (ec != std::errc{} || end != begin + 8U) throw std::runtime_error("raw draw word is not hexadecimal");
        words[index] = value;
    }
}

template <size_t Rows, size_t Columns>
std::vector<uint32_t> FloatBits(const std::array<std::array<float, Columns>, Rows>& values) {
    std::vector<uint32_t> bits;
    bits.reserve(Rows * Columns);
    for (const auto& row : values)
        for (const float value : row) bits.push_back(std::bit_cast<uint32_t>(value));
    return bits;
}

template <size_t Rows, size_t Columns>
void ReadFloatBits(std::string_view text, std::array<std::array<float, Columns>, Rows>& values) {
    std::array<uint32_t, Rows * Columns> bits{};
    DecodeWords(text, bits);
    for (size_t row = 0; row < Rows; ++row)
        for (size_t column = 0; column < Columns; ++column)
            values[row][column] = std::bit_cast<float>(bits[row * Columns + column]);
}

uint64_t Fnv1a(std::string_view bytes) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

using ResourceSink = std::function<std::string(std::string_view, std::string)>;

Json EncodeShader(const Oot3dPicaShaderState& shader, const ResourceSink& resource) {
    std::vector<uint32_t> integers;
    for (const auto& vector : shader.IntegerUniforms)
        integers.push_back(static_cast<uint32_t>(vector[0]) | (static_cast<uint32_t>(vector[1]) << 8U) |
                           (static_cast<uint32_t>(vector[2]) << 16U) | (static_cast<uint32_t>(vector[3]) << 24U));
    uint32_t booleans = 0;
    for (size_t index = 0; index < shader.BooleanUniforms.size(); ++index)
        if (shader.BooleanUniforms[index]) booleans |= 1U << index;
    return {{"program", resource("program", EncodeWords(shader.Program.Values()))},
            {"swizzles", resource("program", EncodeWords(shader.Swizzles.Values()))},
            {"program_words", shader.ProgramWordCount},
            {"swizzle_words", shader.SwizzleWordCount},
            {"f", EncodeWords(FloatBits(shader.FloatUniforms))},
            {"i", EncodeWords(integers)},
            {"b", booleans}};
}

void DecodeShader(const Json& value, const std::map<std::string, std::string>& resources,
                  Oot3dPicaShaderState& shader) {
    const auto payload = [&](const char* key) -> const std::string& {
        const auto found = resources.find(value.at(key).get<std::string>());
        if (found == resources.end()) throw std::runtime_error("raw draw references a missing program");
        return found->second;
    };
    std::array<uint32_t, Oot3dPicaShaderState::MaximumProgramWords> program{};
    DecodeWords(payload("program"), program);
    shader.Program = program;
    std::array<uint32_t, Oot3dPicaShaderState::MaximumSwizzleWords> swizzles{};
    DecodeWords(payload("swizzles"), swizzles);
    shader.Swizzles = swizzles;
    shader.ProgramWordCount = value.at("program_words").get<size_t>();
    shader.SwizzleWordCount = value.at("swizzle_words").get<size_t>();
    if (shader.ProgramWordCount > program.size() || shader.SwizzleWordCount > swizzles.size())
        throw std::runtime_error("raw draw program word count is invalid");
    ReadFloatBits(value.at("f").get<std::string>(), shader.FloatUniforms);
    std::array<uint32_t, 4> integers{};
    DecodeWords(value.at("i").get<std::string>(), integers);
    for (size_t index = 0; index < integers.size(); ++index)
        for (size_t component = 0; component < 4U; ++component)
            shader.IntegerUniforms[index][component] = static_cast<uint8_t>(integers[index] >> (component * 8U));
    const auto booleans = value.at("b").get<uint32_t>();
    for (size_t index = 0; index < shader.BooleanUniforms.size(); ++index)
        shader.BooleanUniforms[index] = (booleans & (1U << index)) != 0U;
}

std::string EncodeLuts(const Oot3dPicaDrawPacket& packet) {
    std::vector<uint32_t> words;
    const auto append = [&](std::span<const uint32_t> values) { words.insert(words.end(), values.begin(), values.end()); };
    append(packet.FogLut);
    append(packet.ProcTexLuts.Noise);
    append(packet.ProcTexLuts.ColorMap);
    append(packet.ProcTexLuts.AlphaMap);
    append(packet.ProcTexLuts.Color);
    append(packet.ProcTexLuts.ColorDifference);
    if (packet.LightingLuts != nullptr) append(packet.LightingLuts->PackedEntries);
    // The lighting flag keeps "absent" distinct from "all zero".
    return (packet.LightingLuts != nullptr ? "L" : "-") + EncodeWords(words);
}

void DecodeLuts(std::string_view text, Oot3dPicaDrawPacket& packet) {
    if (text.empty()) throw std::runtime_error("raw draw LUT payload is empty");
    const bool lighting = text.front() == 'L';
    constexpr size_t fixed = 128U + 128U * 3U + 256U * 2U;
    std::vector<uint32_t> words(fixed + Oot3dPicaLightingLutState::PackedEntryCount);
    DecodeWords(text.substr(1), words);
    size_t offset = 0;
    const auto copy = [&](auto& target) {
        for (auto& word : target) word = words[offset++];
    };
    copy(packet.FogLut);
    copy(packet.ProcTexLuts.Noise);
    copy(packet.ProcTexLuts.ColorMap);
    copy(packet.ProcTexLuts.AlphaMap);
    copy(packet.ProcTexLuts.Color);
    copy(packet.ProcTexLuts.ColorDifference);
    packet.LightingLuts.reset();
    if (lighting) {
        auto state = std::make_shared<Oot3dPicaLightingLutState>();
        copy(state->PackedEntries);
        state->ContentHash = ComputeOot3dPicaLightingLutContentHash(*state);
        state->ContentHashAvailable = true;
        packet.LightingLuts = std::move(state);
    } else if (std::any_of(words.begin() + static_cast<std::ptrdiff_t>(fixed), words.end(),
                           [](uint32_t word) { return word != 0U; })) {
        throw std::runtime_error("raw draw has lighting LUT words without a lighting LUT");
    }
}

Json EncodeIdentity(uint64_t id, uint64_t secondary, uint64_t size) {
    return Json::array({Hex64(id), Hex64(secondary), size});
}

void DecodeIdentity(const Json& value, uint64_t& id, uint64_t& secondary, uint64_t& size) {
    if (!value.is_array() || value.size() != 3U) throw std::runtime_error("raw draw witness identity is invalid");
    id = ParseHex64(value[0]);
    secondary = ParseHex64(value[1]);
    size = value[2].get<uint64_t>();
}
} // namespace

Json EncodeOot3dPicaRawDraw(const Oot3dPicaRawDraw& draw, const ResourceSink& resource) {
    const auto& packet = draw.Packet;
    const auto& composition = packet.Composition;
    const auto& witness = draw.Witness;
    return {
        {"record", "draw"},
        {"pipeline_id", Hex64(draw.CanonicalPipelineId)},
        {"registers", EncodeWords(packet.Registers)},
        {"indexed", packet.Indexed},
        {"command_list", Json::array({packet.CommandListAddress, packet.CommandListOffsetWords})},
        {"domain", static_cast<uint32_t>(packet.CompositionDomain)},
        {"composition",
         {{"layer", static_cast<uint32_t>(composition.Layer)},
          {"provenance", static_cast<uint32_t>(composition.Provenance)},
          {"source_pc", composition.SourcePc},
          {"native_value", composition.NativeValue},
          {"projected_shadow", composition.NativeProjectedShadow},
          {"actor_geometry", composition.NativeActorGeometry},
          {"isolated_lighting", composition.IsolatedLighting}}},
        {"default_attributes", EncodeWords(FloatBits(packet.DefaultAttributes))},
        {"vertex", EncodeShader(packet.VertexShader, resource)},
        {"geometry", EncodeShader(packet.GeometryShader, resource)},
        {"luts", resource("luts", EncodeLuts(packet))},
        {"witness",
         {{"vertex_key", Hex64(witness.VertexShaderKey)},
          {"fragment_key", Hex64(witness.FragmentShaderKey)},
          {"vertex", EncodeIdentity(witness.VertexSourceId, witness.VertexSourceSecondaryHash,
                                    witness.VertexSourceSize)},
          {"fragment", EncodeIdentity(witness.FragmentSourceId, witness.FragmentSourceSecondaryHash,
                                      witness.FragmentSourceSize)}}},
    };
}

bool DecodeOot3dPicaRawDraw(const Json& line, const std::map<std::string, std::string>& resources,
                            Oot3dPicaRawDraw& draw, std::string* error) {
    try {
        Oot3dPicaRawDraw decoded;
        auto& packet = decoded.Packet;
        decoded.CanonicalPipelineId = ParseHex64(line.at("pipeline_id"));
        DecodeWords(line.at("registers").get<std::string>(), packet.Registers);
        packet.Indexed = line.at("indexed").get<bool>();
        const auto& commandList = line.at("command_list");
        packet.CommandListAddress = commandList.at(0).get<uint32_t>();
        packet.CommandListOffsetWords = commandList.at(1).get<uint32_t>();
        packet.CompositionDomain = static_cast<Oot3dPicaCompositionDomain>(line.at("domain").get<uint32_t>());
        const auto& composition = line.at("composition");
        packet.Composition.Layer = static_cast<Oot3dPicaCompositionLayer>(composition.at("layer").get<uint32_t>());
        packet.Composition.Provenance =
            static_cast<Oot3dPicaCompositionProvenance>(composition.at("provenance").get<uint32_t>());
        packet.Composition.SourcePc = composition.at("source_pc").get<uint32_t>();
        packet.Composition.NativeValue = composition.at("native_value").get<uint32_t>();
        packet.Composition.NativeProjectedShadow = composition.at("projected_shadow").get<bool>();
        packet.Composition.NativeActorGeometry = composition.at("actor_geometry").get<bool>();
        packet.Composition.IsolatedLighting = composition.at("isolated_lighting").get<bool>();
        ReadFloatBits(line.at("default_attributes").get<std::string>(), packet.DefaultAttributes);
        DecodeShader(line.at("vertex"), resources, packet.VertexShader);
        DecodeShader(line.at("geometry"), resources, packet.GeometryShader);
        const auto luts = resources.find(line.at("luts").get<std::string>());
        if (luts == resources.end()) throw std::runtime_error("raw draw references missing LUTs");
        DecodeLuts(luts->second, packet);
        const auto& witness = line.at("witness");
        decoded.Witness.VertexShaderKey = ParseHex64(witness.at("vertex_key"));
        decoded.Witness.FragmentShaderKey = ParseHex64(witness.at("fragment_key"));
        DecodeIdentity(witness.at("vertex"), decoded.Witness.VertexSourceId,
                       decoded.Witness.VertexSourceSecondaryHash, decoded.Witness.VertexSourceSize);
        DecodeIdentity(witness.at("fragment"), decoded.Witness.FragmentSourceId,
                       decoded.Witness.FragmentSourceSecondaryHash, decoded.Witness.FragmentSourceSize);
        draw = std::move(decoded);
        return true;
    } catch (const std::exception& exception) {
        SetError(error, std::string("invalid raw draw record: ") + exception.what());
        return false;
    }
}

bool Oot3dPicaRawDrawRecorder::Configure(const std::filesystem::path& path, bool parametricTev,
                                         bool uberFragments, std::string* error) {
    mStream.close();
    mDraws.clear();
    mResources.clear();
    mStream.open(path, std::ios::binary | std::ios::trunc);
    if (!mStream) {
        SetError(error, "cannot write raw draw records to " + path.string());
        return false;
    }
    mStream << Json{{"record", "session"},
                    {"format", kOot3dPicaRawDrawRecordFormat},
                    {"parametric_tev", parametricTev},
                    {"uber_fragments", uberFragments}}
                   .dump()
            << '\n';
    mStream.flush();
    return true;
}

std::string Oot3dPicaRawDrawRecorder::Resource(std::string_view kind, std::string payload) {
    std::string id = std::string(kind) + ":" + Hex64(Fnv1a(payload));
    for (unsigned suffix = 1;; ++suffix) {
        const auto found = mResources.find(id);
        if (found == mResources.end()) break;
        if (found->second == payload) return id;
        id = std::string(kind) + ":" + Hex64(Fnv1a(payload)) + "-" + std::to_string(suffix);
    }
    mStream << Json{{"record", "resource"}, {"id", id}, {"data", payload}}.dump() << '\n';
    mResources.emplace(id, std::move(payload));
    return id;
}

void Oot3dPicaRawDrawRecorder::Observe(uint64_t canonicalPipelineId, const Oot3dPicaDrawPacket& packet,
                                       const Oot3dPicaRawDrawWitness& witness) {
    if (!Enabled() || !mDraws.insert(canonicalPipelineId).second) return;
    const auto line = EncodeOot3dPicaRawDraw(
        {canonicalPipelineId, packet, witness},
        [this](std::string_view kind, std::string payload) { return Resource(kind, std::move(payload)); });
    mStream << line.dump() << '\n';
    mStream.flush();
}

bool LoadOot3dPicaRawDraws(const std::filesystem::path& path, Oot3dPicaRawDrawSession& loaded,
                           std::string* error) {
    Oot3dPicaRawDrawSession result;
    auto& draws = result.Draws;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "cannot read " + path.string());
        return false;
    }
    std::map<std::string, std::string> resources;
    std::string text;
    bool session = false;
    size_t lineNumber = 0;
    while (std::getline(input, text)) {
        ++lineNumber;
        Json line;
        try {
            line = Json::parse(text);
        } catch (const std::exception&) {
            // An interrupted session can end with a partial line.
            if (input.peek() == std::char_traits<char>::eof()) break;
            SetError(error, path.string() + ":" + std::to_string(lineNumber) + ": invalid JSON");
            return false;
        }
        const auto record = line.value("record", std::string{});
        if (record == "session") {
            if (line.value("format", std::string{}) != kOot3dPicaRawDrawRecordFormat) {
                SetError(error, path.string() + ": unsupported raw draw format");
                return false;
            }
            result.ParametricTev = line.at("parametric_tev").get<bool>();
            result.UberFragments = line.at("uber_fragments").get<bool>();
            session = true;
        } else if (!session) {
            SetError(error, path.string() + ": raw draws lack a session header");
            return false;
        } else if (record == "resource") {
            resources[line.at("id").get<std::string>()] = line.at("data").get<std::string>();
        } else if (record == "draw") {
            Oot3dPicaRawDraw draw;
            std::string decodeError;
            if (!DecodeOot3dPicaRawDraw(line, resources, draw, &decodeError)) {
                SetError(error, path.string() + ":" + std::to_string(lineNumber) + ": " + decodeError);
                return false;
            }
            draws.push_back(std::move(draw));
        }
    }
    if (!session) {
        SetError(error, path.string() + ": empty raw draw recording");
        return false;
    }
    loaded = std::move(result);
    return true;
}

} // namespace Oot3dNativeGame
