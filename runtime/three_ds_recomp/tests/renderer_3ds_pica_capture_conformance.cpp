#include "renderer_3ds_pica_capture_conformance.h"

#include "fast/renderer3ds/pica_native_state.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <istream>
#include <limits>
#include <sstream>

namespace Fast::Renderer3ds::Diagnostics {
namespace {

constexpr size_t kPicaRegisterCount = 0x300U;
constexpr std::array<uint32_t, 6U> kTevStageBases{ 0x0C0U, 0x0C8U, 0x0D0U, 0x0D8U, 0x0F0U, 0x0F8U };

struct StreamState {
    bool CaptureOpen = false;
    bool CommandListOpen = false;
    bool DrawOpen = false;
};

void ObserveIssue(PicaCaptureConformanceReport& report, std::string_view issue) {
    ++report.IssueCounts[std::string(issue)];
}

bool ReadUnsigned(const nlohmann::json& value, uint64_t& result) {
    if (value.is_number_unsigned()) {
        result = value.get<uint64_t>();
        return true;
    }
    if (value.is_number_integer()) {
        const int64_t signedValue = value.get<int64_t>();
        if (signedValue < 0) {
            return false;
        }
        result = static_cast<uint64_t>(signedValue);
        return true;
    }
    if (!value.is_string()) {
        return false;
    }
    std::string_view text = value.get_ref<const std::string&>();
    int base = 10;
    if (text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2U);
        base = 16;
    }
    if (text.empty()) {
        return false;
    }
    uint64_t parsed = 0U;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), parsed, base);
    if (code != std::errc{} || end != text.data() + text.size()) {
        return false;
    }
    result = parsed;
    return true;
}

bool ReadU32(const nlohmann::json& value, uint32_t& result) {
    uint64_t parsed = 0U;
    if (!ReadUnsigned(value, parsed) || parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    result = static_cast<uint32_t>(parsed);
    return true;
}

bool ReadBoolean(const nlohmann::json& value, bool& result) {
    if (value.is_boolean()) {
        result = value.get<bool>();
        return true;
    }
    uint64_t integer = 0U;
    if (!ReadUnsigned(value, integer) || integer > 1U) {
        return false;
    }
    result = integer != 0U;
    return true;
}

bool DecodeRegisterSnapshot(const nlohmann::json& event, std::array<uint32_t, kPicaRegisterCount>& registers,
                            PicaCaptureConformanceReport& report) {
    const auto snapshot = event.find("register_snapshot");
    if (snapshot == event.end() || !snapshot->is_object() || snapshot->empty()) {
        ObserveIssue(report, "missing_register_snapshot");
        return false;
    }

    std::array<bool, kPicaRegisterCount> assigned{};
    for (const auto& [name, group] : snapshot->items()) {
        (void)name;
        if (!group.is_object() || !group.contains("first") || !group.contains("last") || !group.contains("values")) {
            ObserveIssue(report, "invalid_register_group");
            return false;
        }
        uint32_t first = 0U;
        uint32_t last = 0U;
        const auto& values = group.at("values");
        if (!ReadU32(group.at("first"), first) || !ReadU32(group.at("last"), last) || first > last ||
            last >= registers.size() || !values.is_array() || values.size() != static_cast<size_t>(last - first + 1U)) {
            ObserveIssue(report, "invalid_register_group");
            return false;
        }
        for (uint32_t index = first; index <= last; ++index) {
            if (assigned[index] || !ReadU32(values.at(index - first), registers[index])) {
                ObserveIssue(report, "invalid_register_value");
                return false;
            }
            assigned[index] = true;
        }
    }

    for (const auto [first, last] : std::array<std::pair<uint32_t, uint32_t>, 5U>{
             std::pair{ 0x040U, 0x06FU }, std::pair{ 0x080U, 0x0FDU }, std::pair{ 0x100U, 0x130U },
             std::pair{ 0x140U, 0x1D9U }, std::pair{ 0x200U, 0x25FU } }) {
        for (uint32_t index = first; index <= last; ++index) {
            if (!assigned[index]) {
                ObserveIssue(report, "incomplete_register_snapshot");
                return false;
            }
        }
    }
    return true;
}

uint64_t HashWord(uint64_t hash, uint32_t value) {
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t HashRegisters(const std::array<uint32_t, kPicaRegisterCount>& regs, std::span<const uint32_t> indices) {
    uint64_t hash = 1469598103934665603ULL;
    for (const uint32_t index : indices) {
        hash = HashWord(hash, index);
        hash = HashWord(hash, regs[index]);
    }
    return hash;
}

std::string NumberName(uint32_t value) {
    return "unknown_" + std::to_string(value);
}

std::string TopologyName(uint32_t value) {
    switch (value) {
        case 0U:
            return "triangle_list";
        case 1U:
            return "triangle_strip";
        case 2U:
            return "triangle_fan";
        case 3U:
            return "geometry_shader";
        default:
            return NumberName(value);
    }
}

std::string CullModeName(uint32_t value) {
    switch (value) {
        case 0U:
            return "keep_all";
        case 1U:
            return "keep_clockwise";
        case 2U:
            return "keep_counter_clockwise";
        default:
            return NumberName(value);
    }
}

std::string FogModeName(uint32_t value) {
    switch (value) {
        case 0U:
            return "none";
        case 5U:
            return "fog";
        case 7U:
            return "gas";
        default:
            return NumberName(value);
    }
}

std::string FragmentOperationName(uint32_t value) {
    switch (value) {
        case 0U:
            return "default";
        case 1U:
            return "gas";
        case 3U:
            return "shadow";
        default:
            return NumberName(value);
    }
}

std::string TextureFormatName(uint32_t value) {
    constexpr std::array<std::string_view, 14U> names{ "rgba8", "rgb8", "rgb5a1", "rgb565", "rgba4", "ia8",  "rg8",
                                                       "i8",    "a8",   "ia4",    "i4",     "a4",    "etc1", "etc1a4" };
    return value < names.size() ? std::string(names[value]) : NumberName(value);
}

std::string TextureTypeName(uint32_t value) {
    constexpr std::array<std::string_view, 6U> names{ "texture_2d",    "texture_cube", "shadow_2d",
                                                      "projection_2d", "shadow_cube",  "disabled" };
    return value < names.size() ? std::string(names[value]) : NumberName(value);
}

std::string TevOperationName(uint32_t value) {
    constexpr std::array<std::string_view, 10U> names{ "replace",          "modulate",  "add",
                                                       "add_signed",       "lerp",      "subtract",
                                                       "dot3_rgb",         "dot3_rgba", "multiply_then_add",
                                                       "add_then_multiply" };
    return value < names.size() ? std::string(names[value]) : NumberName(value);
}

std::string TevSourceName(uint32_t value) {
    switch (value) {
        case 0x0U:
            return "primary_color";
        case 0x1U:
            return "primary_fragment_color";
        case 0x2U:
            return "secondary_fragment_color";
        case 0x3U:
            return "texture0";
        case 0x4U:
            return "texture1";
        case 0x5U:
            return "texture2";
        case 0x6U:
            return "texture3";
        case 0xDU:
            return "previous_buffer";
        case 0xEU:
            return "constant";
        case 0xFU:
            return "previous";
        default:
            return NumberName(value);
    }
}

std::string TextureDimensionBucket(uint32_t width, uint32_t height) {
    const uint32_t largest = std::max(width, height);
    if (largest <= 32U)
        return "up_to_32";
    if (largest <= 64U)
        return "up_to_64";
    if (largest <= 128U)
        return "up_to_128";
    if (largest <= 256U)
        return "up_to_256";
    if (largest <= 512U)
        return "up_to_512";
    if (largest <= 1024U)
        return "up_to_1024";
    return "over_1024";
}

void ObserveIdentity(const nlohmann::json& identity, PicaCaptureConformanceReport& report) {
    if (!identity.is_object()) {
        ObserveIssue(report, "missing_shader_identity");
        return;
    }
    const auto observeShader = [&](std::string_view member, std::set<std::string>& output, bool required) {
        const auto found = identity.find(member);
        if (found == identity.end() || !found->is_object()) {
            if (required)
                ObserveIssue(report, "missing_shader_identity");
            return;
        }
        if (!found->value("enabled", true))
            return;
        const std::string program = found->value("program_hash", std::string{});
        const std::string swizzle = found->value("swizzle_hash", std::string{});
        if (program.empty() || swizzle.empty()) {
            ObserveIssue(report, "incomplete_shader_identity");
            return;
        }
        output.insert(program + ':' + swizzle + ':' + std::to_string(found->value("entry_point", 0U)));
    };
    observeShader("vertex", report.VertexProgramIdentities, true);
    observeShader("geometry", report.GeometryProgramIdentities, false);
    const std::string fragment = identity.value("fragment_config_hash", std::string{});
    if (fragment.empty()) {
        ObserveIssue(report, "incomplete_shader_identity");
    } else {
        report.FragmentConfigurationIdentities.insert(fragment);
    }
    report.PipelineIdentities.insert(identity.dump());
}

bool ObserveTextures(const nlohmann::json& event, PicaCaptureConformanceReport& report, bool& shadowTexture) {
    const auto textures = event.find("textures");
    if (textures == event.end() || !textures->is_array()) {
        ObserveIssue(report, "missing_texture_descriptors");
        return false;
    }
    bool valid = true;
    for (const auto& texture : *textures) {
        if (!texture.is_object()) {
            ObserveIssue(report, "invalid_texture_descriptor");
            valid = false;
            continue;
        }
        bool enabled = false;
        const auto enabledValue = texture.find("enabled");
        if (enabledValue == texture.end() || !ReadBoolean(*enabledValue, enabled)) {
            ObserveIssue(report, "invalid_texture_descriptor");
            valid = false;
            continue;
        }
        if (!enabled)
            continue;

        uint32_t slot = 0U;
        uint32_t format = 0U;
        uint32_t type = 0U;
        uint32_t width = 0U;
        uint32_t height = 0U;
        if (!texture.contains("index") || !texture.contains("format") || !texture.contains("type") ||
            !texture.contains("width") || !texture.contains("height") || !ReadU32(texture.at("index"), slot) ||
            slot > 2U || !ReadU32(texture.at("format"), format) || !ReadU32(texture.at("type"), type) ||
            !ReadU32(texture.at("width"), width) || !ReadU32(texture.at("height"), height) || width == 0U ||
            height == 0U) {
            ObserveIssue(report, "invalid_texture_descriptor");
            valid = false;
            continue;
        }
        ++report.EnabledTextureCount;
        ++report.TextureFormatCounts[TextureFormatName(format)];
        ++report.TextureTypeCounts[TextureTypeName(type)];
        ++report.TextureDimensionBucketCounts[TextureDimensionBucket(width, height)];
        report.MaximumTextureWidth = std::max(report.MaximumTextureWidth, width);
        report.MaximumTextureHeight = std::max(report.MaximumTextureHeight, height);
        if (format == 12U || format == 13U) {
            ++report.CompressedTextureCount;
        }
        if (format > 13U) {
            ObserveIssue(report, "unknown_texture_format");
            valid = false;
        }
        if (type > 5U) {
            ObserveIssue(report, "unknown_texture_type");
            valid = false;
        }
        if (type == 2U || type == 4U)
            shadowTexture = true;
    }
    return valid;
}

bool ObserveDraw(const nlohmann::json& event, PicaCaptureConformanceReport& report) {
    std::array<uint32_t, kPicaRegisterCount> registers{};
    if (!DecodeRegisterSnapshot(event, registers, report))
        return false;

    bool valid = true;
    uint32_t topology = 0U;
    if (!event.contains("triangle_topology") || !ReadU32(event.at("triangle_topology"), topology)) {
        ObserveIssue(report, "invalid_topology");
        return false;
    }
    ++report.TopologyCounts[TopologyName(topology)];
    if (topology > static_cast<uint32_t>(PicaTopology::GeometryShader)) {
        ObserveIssue(report, "unknown_topology");
        valid = false;
    }

    bool indexed = false;
    if (const auto found = event.find("is_indexed"); found != event.end()) {
        if (!ReadBoolean(*found, indexed)) {
            ObserveIssue(report, "invalid_indexed_state");
            valid = false;
        }
    }
    if (indexed)
        ++report.IndexedDrawCount;
    ++report.DrawModeCounts[event.value("mode", std::string{ "unspecified" })];

    bool geometryShader = false;
    if (const auto found = event.find("use_gs"); found != event.end() && !ReadBoolean(*found, geometryShader)) {
        ObserveIssue(report, "invalid_geometry_shader_state");
        valid = false;
    }
    if (topology == 3U)
        ++report.GeometryShaderTopologyDrawCount;
    if (geometryShader)
        ++report.ExplicitGeometryShaderDrawCount;

    const uint32_t cullMode = registers[0x040U] & 0x3U;
    ++report.CullModeCounts[CullModeName(cullMode)];
    if (cullMode > 2U) {
        ObserveIssue(report, "unknown_cull_mode");
        valid = false;
    }

    const bool fragmentLighting = (registers[0x08FU] & 1U) != 0U;
    if (fragmentLighting) {
        ++report.FragmentLightingDrawCount;
        const uint32_t activeLights = (registers[0x1C2U] & 0x7U) + 1U;
        ++report.ActiveLightCountDraws[std::to_string(activeLights)];
    }

    const uint32_t fogMode = registers[0x0E0U] & 0x7U;
    ++report.FogModeCounts[FogModeName(fogMode)];
    if (fogMode == 5U)
        ++report.FogDrawCount;
    if (fogMode != 0U && fogMode != 5U && fogMode != 7U) {
        ObserveIssue(report, "unknown_fog_mode");
        valid = false;
    }

    PicaFragmentFeatureView fragmentFeatures;
    fragmentFeatures.SchemaVersion = kPicaFragmentFeatureLegacySchemaVersion;
    fragmentFeatures.FragmentLightingEnabled = fragmentLighting;
    fragmentFeatures.FogEnabled = fogMode == 5U;
    fragmentFeatures.FogFlip = (registers[0x0E0U] & (1U << 16U)) != 0U;
    fragmentFeatures.FogMode = static_cast<uint8_t>(fogMode);
    if (!fragmentFeatures.Valid()) {
        ObserveIssue(report, "shared_fragment_contract_rejected_state");
        valid = false;
    }

    if ((registers[0x080U] & (1U << 10U)) != 0U) {
        ++report.ProceduralTextureDrawCount;
    }

    const uint32_t fragmentOperation = registers[0x100U] & 0x3U;
    ++report.FragmentOperationCounts[FragmentOperationName(fragmentOperation)];
    if (fragmentOperation == 1U || fogMode == 7U)
        ++report.GasDrawCount;
    if (fragmentOperation == 3U)
        ++report.ShadowRenderDrawCount;
    if (fragmentOperation == 2U) {
        ObserveIssue(report, "unknown_fragment_operation");
        valid = false;
    }

    const bool alphaBlend = (registers[0x100U] & (1U << 8U)) != 0U;
    if (alphaBlend) {
        ++report.AlphaBlendDrawCount;
        const uint32_t blend = registers[0x101U];
        if ((blend & 0x7U) > 4U || ((blend >> 8U) & 0x7U) > 4U || ((blend >> 16U) & 0xFU) > 14U ||
            ((blend >> 20U) & 0xFU) > 14U || ((blend >> 24U) & 0xFU) > 14U || ((blend >> 28U) & 0xFU) > 14U) {
            ObserveIssue(report, "unknown_blend_state");
            valid = false;
        }
    } else {
        ++report.LogicOperationDrawCount;
    }
    if ((registers[0x104U] & 1U) != 0U)
        ++report.AlphaTestDrawCount;
    if ((registers[0x105U] & 1U) != 0U)
        ++report.StencilTestDrawCount;
    if ((registers[0x107U] & 1U) != 0U)
        ++report.DepthTestDrawCount;
    if ((registers[0x107U] & (1U << 12U)) != 0U) {
        ++report.DepthWriteDrawCount;
    }

    bool shadowTexture = false;
    if (!ObserveTextures(event, report, shadowTexture))
        valid = false;
    if (shadowTexture)
        ++report.ShadowTextureDrawCount;

    std::array<uint32_t, 42U> tevRegisters{};
    size_t tevRegisterCount = 0U;
    for (const uint32_t base : kTevStageBases) {
        for (uint32_t offset = 0U; offset < 5U; ++offset) {
            tevRegisters[tevRegisterCount++] = base + offset;
        }
        const uint32_t sources = registers[base];
        const uint32_t operations = registers[base + 2U];
        const uint32_t colorOperation = operations & 0xFU;
        const uint32_t alphaOperation = (operations >> 16U) & 0xFU;
        ++report.TevColorOperationCounts[TevOperationName(colorOperation)];
        ++report.TevAlphaOperationCounts[TevOperationName(alphaOperation)];
        if (colorOperation > 9U || alphaOperation > 9U) {
            ObserveIssue(report, "unknown_tev_operation");
            valid = false;
        }
        for (const uint32_t shift : { 0U, 4U, 8U, 16U, 20U, 24U }) {
            const uint32_t source = (sources >> shift) & 0xFU;
            const std::string sourceName = TevSourceName(source);
            ++report.TevSourceReferenceCounts[sourceName];
            if (sourceName.starts_with("unknown_")) {
                ObserveIssue(report, "unknown_tev_source");
                valid = false;
            }
        }
    }
    tevRegisters[tevRegisterCount++] = 0x0E0U;
    tevRegisters[tevRegisterCount++] = 0x0FDU;
    report.TevStateIdentities.insert(
        HashRegisters(registers, std::span<const uint32_t>(tevRegisters.data(), tevRegisterCount)));

    constexpr std::array<uint32_t, 10U> rasterRegisters{ 0x040U, 0x041U, 0x043U, 0x065U, 0x067U,
                                                         0x100U, 0x101U, 0x104U, 0x105U, 0x107U };
    report.RasterStateIdentities.insert(HashRegisters(registers, rasterRegisters));

    const auto identity = event.find("shader_identity");
    if (identity != event.end()) {
        ++report.ShaderIdentityDrawCount;
        ObserveIdentity(*identity, report);
    }

    ++report.DecodedDrawCount;
    if (valid)
        ++report.ContractRepresentableDrawCount;
    return valid;
}

void ObserveRegisterWrite(const nlohmann::json& event, PicaCaptureConformanceReport& report) {
    const auto idValue = event.find("id");
    uint32_t id = 0U;
    if (idValue == event.end() || !ReadU32(*idValue, id)) {
        ObserveIssue(report, "invalid_register_write");
        return;
    }
    switch (id) {
        case 0x2CBU:
            ++report.VertexProgramUploadCount;
            break;
        case 0x2D5U:
            ++report.VertexSwizzleUploadCount;
            break;
        case 0x29BU:
            ++report.GeometryProgramUploadCount;
            break;
        case 0x2A5U:
            ++report.GeometrySwizzleUploadCount;
            break;
        default:
            break;
    }
    if (id >= 0x2CCU && id <= 0x2D3U) {
        ++report.VertexProgramUploadWordCount;
    } else if (id >= 0x2D6U && id <= 0x2DDU) {
        ++report.VertexSwizzleUploadWordCount;
    } else if (id >= 0x29CU && id <= 0x2A3U) {
        ++report.GeometryProgramUploadWordCount;
    } else if (id >= 0x2A6U && id <= 0x2ADU) {
        ++report.GeometrySwizzleUploadWordCount;
    }
}

nlohmann::json MapToJson(const std::map<std::string, uint64_t>& values) {
    nlohmann::json result = nlohmann::json::object();
    for (const auto& [name, count] : values)
        result[name] = count;
    return result;
}

} // namespace

bool PicaCaptureConformanceReport::Conformant() const noexcept {
    return IssueCounts.empty() && CaptureBeginCount == CaptureEndCount && DrawBeginCount == DrawEndCount &&
           DecodedDrawCount == DrawBeginCount && ContractRepresentableDrawCount == DecodedDrawCount;
}

nlohmann::json PicaCaptureConformanceReport::ToJson(std::string_view corpusId,
                                                    std::string_view sourceImageSha256) const {
    nlohmann::json provenance = {
        { "corpus_id", corpusId },
        { "source_file_count", SourceFileCount },
        { "source_bytes", SourceByteCount },
        { "source_lines", SourceLineCount },
    };
    if (!sourceImageSha256.empty()) {
        provenance["source_image_sha256"] = sourceImageSha256;
    }
    return {
        { "format", "renderer3ds.pica_capture_conformance.v1" },
        { "status", Conformant() ? "pass" : "fail" },
        { "privacy",
          { { "contains_addresses", false },
            { "contains_geometry", false },
            { "contains_uniforms", false },
            { "contains_asset_hashes", false },
            { "contains_texture_payloads", false } } },
        { "provenance", std::move(provenance) },
        { "structure",
          { { "capture_begin", CaptureBeginCount },
            { "capture_end", CaptureEndCount },
            { "frame_boundaries", FrameBoundaryCount },
            { "command_lists", CommandListCount },
            { "draw_begin", DrawBeginCount },
            { "draw_end", DrawEndCount },
            { "decoded_draws", DecodedDrawCount },
            { "contract_representable_draws", ContractRepresentableDrawCount },
            { "events", MapToJson(EventCounts) },
            { "unknown_events", MapToJson(UnknownEventCounts) },
            { "issues", MapToJson(IssueCounts) } } },
        { "draw_surface",
          { { "modes", MapToJson(DrawModeCounts) },
            { "indexed_draws", IndexedDrawCount },
            { "topologies", MapToJson(TopologyCounts) },
            { "geometry_shader_topology_draws", GeometryShaderTopologyDrawCount },
            { "explicit_geometry_shader_draws", ExplicitGeometryShaderDrawCount },
            { "cull_modes", MapToJson(CullModeCounts) },
            { "unique_raster_states", RasterStateIdentities.size() } } },
        { "fragment_surface",
          { { "fragment_lighting_draws", FragmentLightingDrawCount },
            { "active_light_counts", MapToJson(ActiveLightCountDraws) },
            { "fog_draws", FogDrawCount },
            { "gas_draws", GasDrawCount },
            { "fog_modes", MapToJson(FogModeCounts) },
            { "procedural_texture_draws", ProceduralTextureDrawCount },
            { "shadow_texture_draws", ShadowTextureDrawCount },
            { "shadow_render_draws", ShadowRenderDrawCount },
            { "fragment_operations", MapToJson(FragmentOperationCounts) },
            { "alpha_blend_draws", AlphaBlendDrawCount },
            { "logic_operation_draws", LogicOperationDrawCount },
            { "alpha_test_draws", AlphaTestDrawCount },
            { "depth_test_draws", DepthTestDrawCount },
            { "depth_write_draws", DepthWriteDrawCount },
            { "stencil_test_draws", StencilTestDrawCount } } },
        { "texture_surface",
          { { "enabled_textures", EnabledTextureCount },
            { "compressed_textures", CompressedTextureCount },
            { "formats", MapToJson(TextureFormatCounts) },
            { "types", MapToJson(TextureTypeCounts) },
            { "dimension_buckets", MapToJson(TextureDimensionBucketCounts) },
            { "maximum_width", MaximumTextureWidth },
            { "maximum_height", MaximumTextureHeight } } },
        { "tev_surface",
          { { "unique_states", TevStateIdentities.size() },
            { "color_operations", MapToJson(TevColorOperationCounts) },
            { "alpha_operations", MapToJson(TevAlphaOperationCounts) },
            { "source_references", MapToJson(TevSourceReferenceCounts) } } },
        { "shader_surface",
          { { "draws_with_explicit_identity", ShaderIdentityDrawCount },
            { "command_stream_uploads",
              { { "vertex_programs", VertexProgramUploadCount },
                { "vertex_program_words", VertexProgramUploadWordCount },
                { "vertex_swizzles", VertexSwizzleUploadCount },
                { "vertex_swizzle_words", VertexSwizzleUploadWordCount },
                { "geometry_programs", GeometryProgramUploadCount },
                { "geometry_program_words", GeometryProgramUploadWordCount },
                { "geometry_swizzles", GeometrySwizzleUploadCount },
                { "geometry_swizzle_words", GeometrySwizzleUploadWordCount } } },
            { "unique_vertex_programs", VertexProgramIdentities.size() },
            { "unique_geometry_programs", GeometryProgramIdentities.size() },
            { "unique_fragment_configurations", FragmentConfigurationIdentities.size() },
            { "unique_pipelines", PipelineIdentities.size() } } },
    };
}

bool AnalyzePicaCaptureStream(std::istream& input, PicaCaptureConformanceReport& report, std::string* error) {
    if (error != nullptr)
        error->clear();
    ++report.SourceFileCount;
    StreamState state;
    uint64_t lineNumber = 0U;
    for (std::string line; std::getline(input, line);) {
        ++lineNumber;
        ++report.SourceLineCount;
        report.SourceByteCount += line.size() + 1U;
        if (line.empty())
            continue;
        nlohmann::json event = nlohmann::json::parse(line, nullptr, false);
        if (event.is_discarded() || !event.is_object()) {
            ObserveIssue(report, "invalid_json_line");
            continue;
        }
        const auto typeValue = event.find("event");
        if (typeValue == event.end() || !typeValue->is_string()) {
            ObserveIssue(report, "missing_event_type");
            continue;
        }
        const std::string type = typeValue->get<std::string>();
        ++report.EventCounts[type];

        if (type == "capture_begin") {
            if (state.CaptureOpen || state.CommandListOpen || state.DrawOpen) {
                ObserveIssue(report, "nested_capture_begin");
            }
            state = {};
            state.CaptureOpen = true;
            ++report.CaptureBeginCount;
        } else if (type == "capture_end") {
            if (!state.CaptureOpen) {
                ObserveIssue(report, "capture_end_without_begin");
            }
            if (state.DrawOpen)
                ObserveIssue(report, "unclosed_draw");
            state = {};
            ++report.CaptureEndCount;
        } else if (type == "command_list_begin") {
            if (!state.CaptureOpen) {
                ObserveIssue(report, "command_list_outside_capture");
            }
            if (state.DrawOpen) {
                ObserveIssue(report, "command_list_started_inside_draw");
                state.DrawOpen = false;
            }
            state.CommandListOpen = true;
            ++report.CommandListCount;
        } else if (type == "draw_begin") {
            if (!state.CaptureOpen || !state.CommandListOpen) {
                ObserveIssue(report, "draw_outside_command_list");
            }
            if (state.DrawOpen)
                ObserveIssue(report, "nested_draw_begin");
            state.DrawOpen = true;
            ++report.DrawBeginCount;
            ObserveDraw(event, report);
        } else if (type == "draw_end") {
            if (!state.DrawOpen)
                ObserveIssue(report, "draw_end_without_begin");
            state.DrawOpen = false;
            ++report.DrawEndCount;
        } else if (type == "frame_boundary") {
            ++report.FrameBoundaryCount;
        } else if (type == "register_write") {
            ObserveRegisterWrite(event, report);
        } else if (type != "shader_uniform_upload" && type != "output_vertex") {
            ++report.UnknownEventCounts[type];
        }
    }
    if (input.bad()) {
        if (error != nullptr)
            *error = "failed while reading capture stream";
        return false;
    }
    if (state.DrawOpen)
        ObserveIssue(report, "unclosed_draw_at_eof");
    if (state.CaptureOpen)
        ObserveIssue(report, "unclosed_capture_at_eof");
    return true;
}

} // namespace Fast::Renderer3ds::Diagnostics
