#include "fast/oot3d/pica_pipeline_manifest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <system_error>
#include <type_traits>

namespace Fast::Oot3d {
namespace {

constexpr size_t kMaximumPipelineCount = 65536U;
constexpr size_t kMaximumVertexBindings = 16U;
constexpr size_t kMaximumVertexAttributes = 16U;
constexpr uint32_t kAllInstrumentationFeatures = 0x7ffU;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

void HashByte(uint64_t& hash, uint8_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ULL;
}

template <typename T>
void HashValue(uint64_t& hash, T value) noexcept {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    if constexpr (std::is_enum_v<T>) {
        HashValue(hash, static_cast<std::underlying_type_t<T>>(value));
    } else if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
        HashByte(hash, value ? 1U : 0U);
    } else {
        using Unsigned = std::make_unsigned_t<T>;
        const Unsigned encoded = static_cast<Unsigned>(value);
        for (size_t byte = 0U; byte < sizeof(encoded); ++byte) {
            HashByte(hash,
                     static_cast<uint8_t>(encoded >> (byte * 8U)));
        }
    }
}

void HashSource(uint64_t& hash,
                const PicaAotShaderSourceIdentity& value) noexcept {
    HashValue(hash, value.Id);
    HashValue(hash, value.SecondaryHash);
    HashValue(hash, value.Size);
}

bool ValidSource(const PicaAotShaderSourceIdentity& value) noexcept {
    return value.Id != 0U && value.SecondaryHash != 0U && value.Size != 0U;
}

template <typename Enum>
bool EnumAtMost(Enum value, Enum maximum) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(value) <=
           static_cast<std::underlying_type_t<Enum>>(maximum);
}

std::string Hex64(uint64_t value) {
    return FormatPicaAotShaderId(value);
}

bool ParseHex64(const nlohmann::json& value, uint64_t& result) {
    if (!value.is_string()) {
        return false;
    }
    std::string_view text = value.get_ref<const std::string&>();
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2U);
    }
    if (text.empty() || text.size() > 16U) {
        return false;
    }
    result = 0U;
    const auto parsed = std::from_chars(text.data(),
                                        text.data() + text.size(), result, 16);
    return parsed.ec == std::errc{} &&
           parsed.ptr == text.data() + text.size();
}

nlohmann::json SourceJson(const PicaAotShaderSourceIdentity& value) {
    return {
        {"source_id", Hex64(value.Id)},
        {"secondary_hash", Hex64(value.SecondaryHash)},
        {"source_size", value.Size},
    };
}

bool ReadSource(const nlohmann::json& value,
                PicaAotShaderSourceIdentity& result) {
    return value.is_object() && value.contains("source_id") &&
           value.contains("secondary_hash") &&
           value.contains("source_size") &&
           ParseHex64(value["source_id"], result.Id) &&
           ParseHex64(value["secondary_hash"], result.SecondaryHash) &&
           value["source_size"].is_number_unsigned() &&
           (result.Size = value["source_size"].get<uint64_t>()) != 0U;
}

template <typename Enum>
bool ReadEnum(const nlohmann::json& value, Enum maximum, Enum& result) {
    if (!value.is_number_unsigned()) {
        return false;
    }
    const uint64_t encoded = value.get<uint64_t>();
    if (encoded > static_cast<uint64_t>(maximum)) {
        return false;
    }
    result = static_cast<Enum>(encoded);
    return true;
}

nlohmann::json HexSet(const std::set<uint64_t>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (const uint64_t value : values) {
        result.push_back(Hex64(value));
    }
    return result;
}

nlohmann::json IntegerSet(const std::set<uint64_t>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (const uint64_t value : values) {
        result.push_back(value);
    }
    return result;
}

bool ReadHexSet(const nlohmann::json& value, std::set<uint64_t>& result) {
    if (!value.is_array()) {
        return false;
    }
    for (const auto& item : value) {
        uint64_t parsed = 0U;
        if (!ParseHex64(item, parsed)) {
            return false;
        }
        result.insert(parsed);
    }
    return true;
}

bool ReadIntegerSet(const nlohmann::json& value,
                    std::set<uint64_t>& result) {
    if (!value.is_array()) {
        return false;
    }
    for (const auto& item : value) {
        if (!item.is_number_unsigned()) {
            return false;
        }
        result.insert(item.get<uint64_t>());
    }
    return true;
}

nlohmann::json EntryJson(
    const PicaGraphicsPipelineManifestEntry& entry) {
    nlohmann::json bindings = nlohmann::json::array();
    for (const auto& binding : entry.VertexBindings) {
        bindings.push_back({
            {"binding", binding.Binding},
            {"byte_stride", binding.ByteStride},
            {"per_instance", binding.PerInstance},
        });
    }
    nlohmann::json attributes = nlohmann::json::array();
    for (const auto& attribute : entry.VertexAttributes) {
        attributes.push_back({
            {"location", attribute.Location},
            {"binding", attribute.Binding},
            {"format", static_cast<uint8_t>(attribute.Format)},
            {"component_count", attribute.ComponentCount},
            {"byte_offset", attribute.ByteOffset},
        });
    }
    const auto& blend = entry.Blend;
    const auto& stencil = entry.Stencil;
    const auto& outputs = entry.ShaderOutputs;
    return {
        {"pipeline_id", Hex64(entry.StructuralId())},
        {"observation_count", entry.ObservationCount},
        {"canonical_pipeline_ids", HexSet(entry.CanonicalPipelineIds)},
        {"settings_revisions", IntegerSet(entry.SettingsRevisions)},
        {"domain", static_cast<uint8_t>(entry.Domain)},
        {"vertex_shader_key", Hex64(entry.VertexShaderKey)},
        {"fragment_shader_key", Hex64(entry.FragmentShaderKey)},
        {"vertex_source", SourceJson(entry.VertexSource)},
        {"fragment_source", SourceJson(entry.FragmentSource)},
        {"nri_fragment_available", entry.NriFragmentAvailable},
        {"nri_fragment_source",
         entry.NriFragmentAvailable
              ? SourceJson(entry.NriFragmentSource)
              : nlohmann::json(nullptr)},
        {"requested_instrumentation_features",
         static_cast<uint32_t>(entry.RequestedFeatures)},
        {"applied_instrumentation_features",
         static_cast<uint32_t>(entry.AppliedFeatures)},
        {"attachment_requirements_key", entry.AttachmentRequirementsKey},
        {"sample_count", entry.SampleCount},
        {"writes_reactive_mask", entry.WritesReactiveMask},
        {"outline_occlusion_only", entry.OutlineOcclusionOnly},
        {"topology", static_cast<uint8_t>(entry.Topology)},
        {"cull_mode", static_cast<uint8_t>(entry.CullMode)},
        {"framebuffer_flipped", entry.FramebufferFlipped},
        {"vertex_bindings", std::move(bindings)},
        {"vertex_attributes", std::move(attributes)},
        {"color_write_mask", entry.ColorWriteMask},
        {"fragment_operation_mode", entry.FragmentOperationMode},
        {"logic_operation", static_cast<uint8_t>(entry.LogicOperation)},
        {"blend", {
            {"enabled", blend.Enabled},
            {"equation_rgb", static_cast<uint8_t>(blend.EquationRgb)},
            {"equation_alpha", static_cast<uint8_t>(blend.EquationAlpha)},
            {"source_rgb", static_cast<uint8_t>(blend.SourceRgb)},
            {"dest_rgb", static_cast<uint8_t>(blend.DestRgb)},
            {"source_alpha", static_cast<uint8_t>(blend.SourceAlpha)},
            {"dest_alpha", static_cast<uint8_t>(blend.DestAlpha)},
        }},
        {"alpha_test_enabled", entry.AlphaTestEnabled},
        {"depth_test_enabled", entry.DepthTestEnabled},
        {"depth_write_enabled", entry.DepthWriteEnabled},
        {"depth_compare", static_cast<uint8_t>(entry.DepthCompare)},
        {"stencil", {
            {"enabled", stencil.Enabled},
            {"compare", static_cast<uint8_t>(stencil.Compare)},
            {"reference", stencil.Reference},
            {"compare_mask", stencil.CompareMask},
            {"write_mask", stencil.WriteMask},
            {"fail", static_cast<uint8_t>(stencil.Fail)},
            {"depth_fail", static_cast<uint8_t>(stencil.DepthFail)},
            {"pass", static_cast<uint8_t>(stencil.Pass)},
        }},
        {"shader_outputs", {
            {"scene_domain_blended_overlay",
             outputs.SceneDomainBlendedOverlay},
            {"scene_domain_normal_overlay",
             outputs.SceneDomainNormalOverlay},
            {"scene_domain_ambient_overlay",
             outputs.SceneDomainAmbientOverlay},
            {"scene_domain_transparent_depth_overlay",
             outputs.SceneDomainTransparentDepthOverlay},
            {"writes_ambient_guide", outputs.WritesAmbientGuide},
            {"writes_fog_guide", outputs.WritesFogGuide},
            {"writes_outline_geometry_guide", outputs.WritesOutlineGeometryGuide},
        }},
    };
}

template <typename T>
bool ReadUnsigned(const nlohmann::json& object, const char* name,
                  T& result) {
    static_assert(std::is_unsigned_v<T>);
    if (!object.contains(name) || !object[name].is_number_unsigned()) {
        return false;
    }
    const uint64_t value = object[name].get<uint64_t>();
    if (value > std::numeric_limits<T>::max()) {
        return false;
    }
    result = static_cast<T>(value);
    return true;
}

bool ReadBool(const nlohmann::json& object, const char* name,
              bool& result) {
    if (!object.contains(name) || !object[name].is_boolean()) {
        return false;
    }
    result = object[name].get<bool>();
    return true;
}

bool ReadEntry(const nlohmann::json& value,
               uint32_t descriptorSchemaVersion,
               PicaGraphicsPipelineManifestEntry& entry) {
    if (!value.is_object()) {
        return false;
    }
    entry = {};
    entry.DescriptorSchemaVersion = descriptorSchemaVersion;
    uint64_t declaredPipelineId = 0U;
    if (!value.contains("pipeline_id") ||
        !ParseHex64(value["pipeline_id"], declaredPipelineId) ||
        !ReadUnsigned(value, "observation_count", entry.ObservationCount) ||
        !value.contains("canonical_pipeline_ids") ||
        !ReadHexSet(value["canonical_pipeline_ids"],
                    entry.CanonicalPipelineIds) ||
        !value.contains("settings_revisions") ||
        !ReadIntegerSet(value["settings_revisions"],
                        entry.SettingsRevisions) ||
        !value.contains("domain") ||
        !ReadEnum(value["domain"],
                  PicaGraphicsPipelineDomain::Instrumented, entry.Domain) ||
        !value.contains("vertex_shader_key") ||
        !ParseHex64(value["vertex_shader_key"], entry.VertexShaderKey) ||
        !value.contains("fragment_shader_key") ||
        !ParseHex64(value["fragment_shader_key"], entry.FragmentShaderKey) ||
        !value.contains("vertex_source") ||
        !ReadSource(value["vertex_source"], entry.VertexSource) ||
        !value.contains("fragment_source") ||
        !ReadSource(value["fragment_source"], entry.FragmentSource) ||
        !ReadBool(value, "nri_fragment_available",
                  entry.NriFragmentAvailable) ||
        !value.contains("nri_fragment_source") ||
        (entry.NriFragmentAvailable &&
         !ReadSource(value["nri_fragment_source"],
                     entry.NriFragmentSource)) ||
        (!entry.NriFragmentAvailable &&
         !value["nri_fragment_source"].is_null()) ||
        !value.contains("requested_instrumentation_features") ||
        !ReadEnum(
            value["requested_instrumentation_features"],
            static_cast<PicaShaderInstrumentationFeature>(kAllInstrumentationFeatures),
            entry.RequestedFeatures) ||
        !value.contains("applied_instrumentation_features") ||
        !ReadEnum(
            value["applied_instrumentation_features"],
            static_cast<PicaShaderInstrumentationFeature>(kAllInstrumentationFeatures),
            entry.AppliedFeatures) ||
        !ReadUnsigned(value, "attachment_requirements_key",
                      entry.AttachmentRequirementsKey) ||
        !ReadUnsigned(value, "sample_count", entry.SampleCount) ||
        !ReadBool(value, "writes_reactive_mask",
                  entry.WritesReactiveMask) ||
        (value.contains("outline_occlusion_only") &&
         !ReadBool(value, "outline_occlusion_only",
                   entry.OutlineOcclusionOnly)) ||
        !value.contains("topology") ||
        !ReadEnum(value["topology"],
                  ::Oot3d::Renderer::PicaTopology::GeometryShader,
                  entry.Topology) ||
        !value.contains("cull_mode") ||
        !ReadEnum(value["cull_mode"],
                  ::Oot3d::Renderer::NativeCullMode::KeepCounterClockwise,
                  entry.CullMode) ||
        !ReadBool(value, "framebuffer_flipped",
                  entry.FramebufferFlipped) ||
        !ReadUnsigned(value, "color_write_mask", entry.ColorWriteMask) ||
        !ReadUnsigned(value, "fragment_operation_mode",
                      entry.FragmentOperationMode) ||
        !value.contains("logic_operation") ||
        !ReadEnum(value["logic_operation"],
                  ::Oot3d::Renderer::PicaLogicOperation::OrInverted,
                  entry.LogicOperation) ||
        !ReadBool(value, "alpha_test_enabled", entry.AlphaTestEnabled) ||
        !ReadBool(value, "depth_test_enabled", entry.DepthTestEnabled) ||
        !ReadBool(value, "depth_write_enabled", entry.DepthWriteEnabled) ||
        !value.contains("depth_compare") ||
        !ReadEnum(value["depth_compare"],
                  ::Oot3d::Renderer::PicaCompareFunction::GreaterOrEqual,
                  entry.DepthCompare)) {
        return false;
    }

    if (!value.contains("vertex_bindings") ||
        !value["vertex_bindings"].is_array() ||
        value["vertex_bindings"].size() > kMaximumVertexBindings ||
        !value.contains("vertex_attributes") ||
        !value["vertex_attributes"].is_array() ||
        value["vertex_attributes"].size() > kMaximumVertexAttributes) {
        return false;
    }
    for (const auto& source : value["vertex_bindings"]) {
        PicaGraphicsPipelineVertexBinding binding;
        if (!source.is_object() ||
            !ReadUnsigned(source, "binding", binding.Binding) ||
            !ReadUnsigned(source, "byte_stride", binding.ByteStride) ||
            !ReadBool(source, "per_instance", binding.PerInstance)) {
            return false;
        }
        entry.VertexBindings.push_back(binding);
    }
    for (const auto& source : value["vertex_attributes"]) {
        PicaGraphicsPipelineVertexAttribute attribute;
        if (!source.is_object() ||
            !ReadUnsigned(source, "location", attribute.Location) ||
            !ReadUnsigned(source, "binding", attribute.Binding) ||
            !source.contains("format") ||
            !ReadEnum(source["format"],
                      ::Oot3d::Renderer::PicaVertexFormat::Float,
                      attribute.Format) ||
            !ReadUnsigned(source, "component_count",
                          attribute.ComponentCount) ||
            !ReadUnsigned(source, "byte_offset", attribute.ByteOffset)) {
            return false;
        }
        entry.VertexAttributes.push_back(attribute);
    }

    if (!value.contains("blend") || !value["blend"].is_object() ||
        !ReadBool(value["blend"], "enabled", entry.Blend.Enabled) ||
        !value["blend"].contains("equation_rgb") ||
        !ReadEnum(value["blend"]["equation_rgb"],
                  ::Oot3d::Renderer::NativeBlendEquation::Max,
                  entry.Blend.EquationRgb) ||
        !value["blend"].contains("equation_alpha") ||
        !ReadEnum(value["blend"]["equation_alpha"],
                  ::Oot3d::Renderer::NativeBlendEquation::Max,
                  entry.Blend.EquationAlpha) ||
        !value["blend"].contains("source_rgb") ||
        !ReadEnum(value["blend"]["source_rgb"],
                  ::Oot3d::Renderer::NativeBlendFactor::SourceAlphaSaturate,
                  entry.Blend.SourceRgb) ||
        !value["blend"].contains("dest_rgb") ||
        !ReadEnum(value["blend"]["dest_rgb"],
                  ::Oot3d::Renderer::NativeBlendFactor::SourceAlphaSaturate,
                  entry.Blend.DestRgb) ||
        !value["blend"].contains("source_alpha") ||
        !ReadEnum(value["blend"]["source_alpha"],
                  ::Oot3d::Renderer::NativeBlendFactor::SourceAlphaSaturate,
                  entry.Blend.SourceAlpha) ||
        !value["blend"].contains("dest_alpha") ||
        !ReadEnum(value["blend"]["dest_alpha"],
                  ::Oot3d::Renderer::NativeBlendFactor::SourceAlphaSaturate,
                  entry.Blend.DestAlpha)) {
        return false;
    }
    if (!value.contains("stencil") || !value["stencil"].is_object() ||
        !ReadBool(value["stencil"], "enabled", entry.Stencil.Enabled) ||
        !value["stencil"].contains("compare") ||
        !ReadEnum(value["stencil"]["compare"],
                  ::Oot3d::Renderer::PicaCompareFunction::GreaterOrEqual,
                  entry.Stencil.Compare) ||
        !ReadUnsigned(value["stencil"], "reference",
                      entry.Stencil.Reference) ||
        !ReadUnsigned(value["stencil"], "compare_mask",
                      entry.Stencil.CompareMask) ||
        !ReadUnsigned(value["stencil"], "write_mask",
                      entry.Stencil.WriteMask) ||
        !value["stencil"].contains("fail") ||
        !ReadEnum(value["stencil"]["fail"],
                  ::Oot3d::Renderer::PicaStencilAction::DecrementWrap,
                  entry.Stencil.Fail) ||
        !value["stencil"].contains("depth_fail") ||
        !ReadEnum(value["stencil"]["depth_fail"],
                  ::Oot3d::Renderer::PicaStencilAction::DecrementWrap,
                  entry.Stencil.DepthFail) ||
        !value["stencil"].contains("pass") ||
        !ReadEnum(value["stencil"]["pass"],
                  ::Oot3d::Renderer::PicaStencilAction::DecrementWrap,
                  entry.Stencil.Pass)) {
        return false;
    }
    if (!value.contains("shader_outputs") ||
        !value["shader_outputs"].is_object() ||
        !ReadBool(value["shader_outputs"],
                  "scene_domain_blended_overlay",
                  entry.ShaderOutputs.SceneDomainBlendedOverlay) ||
        !ReadBool(value["shader_outputs"],
                  "scene_domain_normal_overlay",
                  entry.ShaderOutputs.SceneDomainNormalOverlay) ||
        !ReadBool(value["shader_outputs"],
                  "scene_domain_ambient_overlay",
                  entry.ShaderOutputs.SceneDomainAmbientOverlay) ||
        (value["shader_outputs"].contains(
             "scene_domain_transparent_depth_overlay") &&
         !ReadBool(value["shader_outputs"],
                   "scene_domain_transparent_depth_overlay",
                   entry.ShaderOutputs.SceneDomainTransparentDepthOverlay)) ||
        (value["shader_outputs"].contains("writes_fog_guide") &&
         !ReadBool(value["shader_outputs"], "writes_fog_guide", entry.ShaderOutputs.WritesFogGuide)) ||
        (value["shader_outputs"].contains("writes_outline_geometry_guide") &&
         !ReadBool(value["shader_outputs"], "writes_outline_geometry_guide", entry.ShaderOutputs.WritesOutlineGeometryGuide)) ||
        !ReadBool(value["shader_outputs"], "writes_ambient_guide",
                  entry.ShaderOutputs.WritesAmbientGuide)) {
        return false;
    }
    return entry.Valid() && entry.StructuralId() == declaredPipelineId;
}

bool LoadManifestJson(const std::filesystem::path& path,
                      uint32_t& descriptorSchemaVersion,
                      std::vector<PicaGraphicsPipelineManifestEntry>& entries,
                      std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "could not open PICA pipeline manifest: " +
                            path.string());
        return false;
    }
    nlohmann::json root;
    try {
        input >> root;
    } catch (const std::exception& exception) {
        SetError(error, "could not parse PICA pipeline manifest: " +
                            std::string(exception.what()));
        return false;
    }
    if (!root.is_object() ||
        root.value("format", std::string{}) !=
            "oot3d_pica_pipeline_manifest_v2" ||
        root.value("schema_version", 0U) !=
            kPicaGraphicsPipelineManifestSchemaVersion ||
        !root.contains("descriptor_schema_version") ||
        !root["descriptor_schema_version"].is_number_unsigned() ||
        !root.contains("pipeline_count") ||
        !root["pipeline_count"].is_number_unsigned() ||
        !root.contains("pipelines") || !root["pipelines"].is_array() ||
        root["pipelines"].size() > kMaximumPipelineCount ||
        root["pipeline_count"].get<size_t>() !=
            root["pipelines"].size()) {
        SetError(error, "PICA pipeline manifest header is invalid");
        return false;
    }
    descriptorSchemaVersion =
        root["descriptor_schema_version"].get<uint32_t>();
    if (descriptorSchemaVersion == 0U) {
        SetError(error, "PICA pipeline descriptor schema is invalid");
        return false;
    }
    std::map<uint64_t, PicaGraphicsPipelineManifestEntry> unique;
    for (const auto& source : root["pipelines"]) {
        PicaGraphicsPipelineManifestEntry entry;
        if (!ReadEntry(source, descriptorSchemaVersion, entry)) {
            SetError(error, "PICA pipeline manifest entry is invalid");
            return false;
        }
        const uint64_t identity = entry.StructuralId();
        auto [found, inserted] = unique.emplace(identity, entry);
        if (!inserted &&
            !found->second.StructurallyEquivalent(entry)) {
            SetError(error, "PICA pipeline manifest identity collision");
            return false;
        }
        if (!inserted) {
            found->second.ObservationCount += entry.ObservationCount;
            found->second.CanonicalPipelineIds.insert(
                entry.CanonicalPipelineIds.begin(),
                entry.CanonicalPipelineIds.end());
            found->second.SettingsRevisions.insert(
                entry.SettingsRevisions.begin(),
                entry.SettingsRevisions.end());
        }
    }
    entries.clear();
    entries.reserve(unique.size());
    for (auto& [identity, entry] : unique) {
        entries.push_back(std::move(entry));
    }
    return true;
}

} // namespace

bool PicaGraphicsPipelineManifestEntry::Valid() const noexcept {
    const uint32_t requestedFeatures =
        static_cast<uint32_t>(RequestedFeatures);
    const uint32_t appliedFeatures =
        static_cast<uint32_t>(AppliedFeatures);
    if (SchemaVersion != kPicaGraphicsPipelineManifestSchemaVersion ||
        DescriptorSchemaVersion == 0U || VertexShaderKey == 0U ||
        FragmentShaderKey == 0U || !ValidSource(VertexSource) ||
        !ValidSource(FragmentSource) ||
        (NriFragmentAvailable && !ValidSource(NriFragmentSource)) ||
        (AttachmentRequirementsKey &
         ~static_cast<uint8_t>(kAllPicaAuxiliaryOutputs)) != 0U ||
        (requestedFeatures & ~kAllInstrumentationFeatures) != 0U ||
        (appliedFeatures & ~requestedFeatures) != 0U ||
        (Domain == PicaGraphicsPipelineDomain::Canonical &&
         appliedFeatures != 0U) ||
        (Domain == PicaGraphicsPipelineDomain::Instrumented &&
         appliedFeatures == 0U) ||
        (SampleCount != 1U && SampleCount != 2U && SampleCount != 4U &&
         SampleCount != 8U) ||
        !EnumAtMost(Domain, PicaGraphicsPipelineDomain::Instrumented) ||
        !EnumAtMost(Topology,
                    ::Oot3d::Renderer::PicaTopology::GeometryShader) ||
        !EnumAtMost(
            CullMode,
            ::Oot3d::Renderer::NativeCullMode::KeepCounterClockwise) ||
        !EnumAtMost(
            LogicOperation,
            ::Oot3d::Renderer::PicaLogicOperation::OrInverted) ||
        !EnumAtMost(Blend.EquationRgb,
                    ::Oot3d::Renderer::NativeBlendEquation::Max) ||
        !EnumAtMost(Blend.EquationAlpha,
                    ::Oot3d::Renderer::NativeBlendEquation::Max) ||
        !EnumAtMost(
            Blend.SourceRgb,
            ::Oot3d::Renderer::NativeBlendFactor::SourceAlphaSaturate) ||
        !EnumAtMost(
            Blend.DestRgb,
            ::Oot3d::Renderer::NativeBlendFactor::SourceAlphaSaturate) ||
        !EnumAtMost(
            Blend.SourceAlpha,
            ::Oot3d::Renderer::NativeBlendFactor::SourceAlphaSaturate) ||
        !EnumAtMost(
            Blend.DestAlpha,
            ::Oot3d::Renderer::NativeBlendFactor::SourceAlphaSaturate) ||
        !EnumAtMost(
            DepthCompare,
            ::Oot3d::Renderer::PicaCompareFunction::GreaterOrEqual) ||
        !EnumAtMost(
            Stencil.Compare,
            ::Oot3d::Renderer::PicaCompareFunction::GreaterOrEqual) ||
        !EnumAtMost(
            Stencil.Fail,
            ::Oot3d::Renderer::PicaStencilAction::DecrementWrap) ||
        !EnumAtMost(
            Stencil.DepthFail,
            ::Oot3d::Renderer::PicaStencilAction::DecrementWrap) ||
        !EnumAtMost(
            Stencil.Pass,
            ::Oot3d::Renderer::PicaStencilAction::DecrementWrap) ||
        VertexBindings.empty() || VertexBindings.size() > 16U ||
        VertexAttributes.empty() || VertexAttributes.size() > 16U) {
        return false;
    }
    std::set<uint8_t> bindings;
    for (const auto& binding : VertexBindings) {
        if (binding.ByteStride == 0U ||
            !bindings.insert(binding.Binding).second) {
            return false;
        }
    }
    std::set<uint8_t> locations;
    for (const auto& attribute : VertexAttributes) {
        if (!bindings.contains(attribute.Binding) ||
            attribute.ComponentCount == 0U ||
            attribute.ComponentCount > 4U ||
            !EnumAtMost(attribute.Format,
                        ::Oot3d::Renderer::PicaVertexFormat::Float) ||
            !locations.insert(attribute.Location).second) {
            return false;
        }
    }
    return true;
}

uint64_t PicaGraphicsPipelineManifestEntry::StructuralId() const noexcept {
    uint64_t hash = 1469598103934665603ULL;
    HashValue(hash, SchemaVersion);
    HashValue(hash, DescriptorSchemaVersion);
    HashValue(hash, Domain);
    HashValue(hash, VertexShaderKey);
    HashValue(hash, FragmentShaderKey);
    HashSource(hash, VertexSource);
    HashSource(hash, FragmentSource);
    HashValue(hash, NriFragmentAvailable);
    if (NriFragmentAvailable) {
        HashSource(hash, NriFragmentSource);
    }
    HashValue(hash, RequestedFeatures);
    HashValue(hash, AppliedFeatures);
    HashValue(hash, AttachmentRequirementsKey);
    HashValue(hash, SampleCount);
    HashValue(hash, WritesReactiveMask);
    // Only hashed when set so ids of pre-existing entries stay valid.
    if (OutlineOcclusionOnly) {
        HashValue(hash, OutlineOcclusionOnly);
    }
    HashValue(hash, Topology);
    HashValue(hash, CullMode);
    HashValue(hash, FramebufferFlipped);
    HashValue(hash, static_cast<uint32_t>(VertexBindings.size()));
    for (const auto& binding : VertexBindings) {
        HashValue(hash, binding.Binding);
        HashValue(hash, binding.ByteStride);
        HashValue(hash, binding.PerInstance);
    }
    HashValue(hash, static_cast<uint32_t>(VertexAttributes.size()));
    for (const auto& attribute : VertexAttributes) {
        HashValue(hash, attribute.Location);
        HashValue(hash, attribute.Binding);
        HashValue(hash, attribute.Format);
        HashValue(hash, attribute.ComponentCount);
        HashValue(hash, attribute.ByteOffset);
    }
    HashValue(hash, ColorWriteMask);
    HashValue(hash, FragmentOperationMode);
    HashValue(hash, LogicOperation);
    HashValue(hash, Blend.Enabled);
    HashValue(hash, Blend.EquationRgb);
    HashValue(hash, Blend.EquationAlpha);
    HashValue(hash, Blend.SourceRgb);
    HashValue(hash, Blend.DestRgb);
    HashValue(hash, Blend.SourceAlpha);
    HashValue(hash, Blend.DestAlpha);
    HashValue(hash, AlphaTestEnabled);
    HashValue(hash, DepthTestEnabled);
    HashValue(hash, DepthWriteEnabled);
    HashValue(hash, DepthCompare);
    HashValue(hash, Stencil.Enabled);
    HashValue(hash, Stencil.Compare);
    HashValue(hash, Stencil.Reference);
    HashValue(hash, Stencil.CompareMask);
    HashValue(hash, Stencil.WriteMask);
    HashValue(hash, Stencil.Fail);
    HashValue(hash, Stencil.DepthFail);
    HashValue(hash, Stencil.Pass);
    HashValue(hash, ShaderOutputs.SceneDomainBlendedOverlay);
    HashValue(hash, ShaderOutputs.SceneDomainNormalOverlay);
    HashValue(hash, ShaderOutputs.SceneDomainAmbientOverlay);
    if (ShaderOutputs.SceneDomainTransparentDepthOverlay) {
        HashValue(hash, ShaderOutputs.SceneDomainTransparentDepthOverlay);
    }
    HashValue(hash, ShaderOutputs.WritesAmbientGuide);
    if (ShaderOutputs.WritesFogGuide) HashValue(hash, ShaderOutputs.WritesFogGuide);
    if (ShaderOutputs.WritesOutlineGeometryGuide) HashValue(hash, ShaderOutputs.WritesOutlineGeometryGuide);
    return hash;
}

bool PicaGraphicsPipelineManifestEntry::StructurallyEquivalent(
    const PicaGraphicsPipelineManifestEntry& other) const noexcept {
    return SchemaVersion == other.SchemaVersion &&
           DescriptorSchemaVersion == other.DescriptorSchemaVersion &&
           Domain == other.Domain &&
           VertexShaderKey == other.VertexShaderKey &&
           FragmentShaderKey == other.FragmentShaderKey &&
           VertexSource == other.VertexSource &&
           FragmentSource == other.FragmentSource &&
           NriFragmentSource == other.NriFragmentSource &&
           NriFragmentAvailable == other.NriFragmentAvailable &&
           RequestedFeatures == other.RequestedFeatures &&
           AppliedFeatures == other.AppliedFeatures &&
           AttachmentRequirementsKey == other.AttachmentRequirementsKey &&
           SampleCount == other.SampleCount &&
           WritesReactiveMask == other.WritesReactiveMask &&
           OutlineOcclusionOnly == other.OutlineOcclusionOnly &&
           Topology == other.Topology && CullMode == other.CullMode &&
           FramebufferFlipped == other.FramebufferFlipped &&
           VertexBindings == other.VertexBindings &&
           VertexAttributes == other.VertexAttributes &&
           ColorWriteMask == other.ColorWriteMask &&
           FragmentOperationMode == other.FragmentOperationMode &&
           LogicOperation == other.LogicOperation && Blend == other.Blend &&
           AlphaTestEnabled == other.AlphaTestEnabled &&
           DepthTestEnabled == other.DepthTestEnabled &&
           DepthWriteEnabled == other.DepthWriteEnabled &&
           DepthCompare == other.DepthCompare && Stencil == other.Stencil &&
           ShaderOutputs == other.ShaderOutputs;
}

bool PicaGraphicsPipelineManifestEntry::MatchesPrewarmProfile(
    PicaShaderInstrumentationFeature activeFeatures,
    bool nativeFidelity) const noexcept {
    if (nativeFidelity &&
        Domain != PicaGraphicsPipelineDomain::Canonical) {
        return false;
    }
    const uint32_t requested = static_cast<uint32_t>(RequestedFeatures);
    const uint32_t active = static_cast<uint32_t>(activeFeatures);
    return (requested & ~active) == 0U;
}

bool WritePicaGraphicsPipelineManifest(
    const std::filesystem::path& path,
    uint32_t descriptorSchemaVersion,
    std::span<const PicaGraphicsPipelineManifestEntry> entries,
    std::string* error) {
    if (path.empty() || descriptorSchemaVersion == 0U || entries.empty() ||
        entries.size() > kMaximumPipelineCount) {
        SetError(error, "PICA pipeline manifest input is invalid");
        return false;
    }
    std::map<uint64_t, PicaGraphicsPipelineManifestEntry> unique;
    for (const auto& source : entries) {
        if (!source.Valid() ||
            source.DescriptorSchemaVersion != descriptorSchemaVersion) {
            SetError(error, "PICA pipeline manifest entry is invalid");
            return false;
        }
        const uint64_t identity = source.StructuralId();
        auto [found, inserted] = unique.emplace(identity, source);
        if (!inserted &&
            !found->second.StructurallyEquivalent(source)) {
            SetError(error, "PICA pipeline manifest identity collision");
            return false;
        }
        if (!inserted) {
            found->second.ObservationCount += source.ObservationCount;
            found->second.CanonicalPipelineIds.insert(
                source.CanonicalPipelineIds.begin(),
                source.CanonicalPipelineIds.end());
            found->second.SettingsRevisions.insert(
                source.SettingsRevisions.begin(),
                source.SettingsRevisions.end());
        }
    }
    nlohmann::json pipelines = nlohmann::json::array();
    for (const auto& [identity, entry] : unique) {
        pipelines.push_back(EntryJson(entry));
    }
    const nlohmann::json root = {
        {"format", "oot3d_pica_pipeline_manifest_v2"},
        {"schema_version", kPicaGraphicsPipelineManifestSchemaVersion},
        {"descriptor_schema_version", descriptorSchemaVersion},
        {"pipeline_count", pipelines.size()},
        {"pipelines", std::move(pipelines)},
    };
    std::error_code filesystemError;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(),
                                            filesystemError);
    }
    if (filesystemError) {
        SetError(error, "could not create pipeline manifest directory: " +
                            filesystemError.message());
        return false;
    }
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << root.dump(2) << '\n';
        if (!output) {
            SetError(error, "could not write PICA pipeline manifest");
            return false;
        }
    }
    std::filesystem::remove(path, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(temporary, path, filesystemError);
    if (filesystemError) {
        SetError(error, "could not publish PICA pipeline manifest: " +
                            filesystemError.message());
        return false;
    }
    return true;
}

bool PicaGraphicsPipelineManifest::Load(const std::filesystem::path& path,
                                        std::string* error) {
    Clear();
    if (!LoadManifestJson(path, mDescriptorSchemaVersion, mEntries, error)) {
        Clear();
        return false;
    }
    mPath = path;
    return true;
}

void PicaGraphicsPipelineManifest::Clear() {
    mPath.clear();
    mDescriptorSchemaVersion = 0U;
    mEntries.clear();
}

bool PicaGraphicsPipelineManifest::Loaded() const noexcept {
    return !mPath.empty() && !mEntries.empty();
}

uint32_t PicaGraphicsPipelineManifest::DescriptorSchemaVersion() const
    noexcept {
    return mDescriptorSchemaVersion;
}

std::span<const PicaGraphicsPipelineManifestEntry>
PicaGraphicsPipelineManifest::Entries() const noexcept {
    return mEntries;
}

const std::filesystem::path& PicaGraphicsPipelineManifest::Path() const
    noexcept {
    return mPath;
}

bool PicaGraphicsPipelineInventory::Configure(
    const std::filesystem::path& path, std::string* error) {
    Clear();
    if (path.empty()) {
        SetError(error, "PICA pipeline inventory path is empty");
        return false;
    }
    mPath = path;
    return true;
}

void PicaGraphicsPipelineInventory::Observe(
    PicaGraphicsPipelineManifestEntry entry,
    uint64_t canonicalPipelineId, uint64_t settingsRevision) {
    if (!Enabled() || !entry.Valid()) {
        return;
    }
    if (mDescriptorSchemaVersion == 0U) {
        mDescriptorSchemaVersion = entry.DescriptorSchemaVersion;
    } else if (mDescriptorSchemaVersion != entry.DescriptorSchemaVersion) {
        mDescriptorSchemaMismatch = true;
        return;
    }
    entry.ObservationCount = 1U;
    if (canonicalPipelineId != 0U) {
        entry.CanonicalPipelineIds.insert(canonicalPipelineId);
    }
    entry.SettingsRevisions.insert(settingsRevision);
    const uint64_t identity = entry.StructuralId();
    auto [found, inserted] = mEntries.emplace(identity, entry);
    if (!inserted &&
        !found->second.StructurallyEquivalent(entry)) {
        mStructuralCollision = true;
        return;
    }
    if (!inserted) {
        ++found->second.ObservationCount;
        found->second.CanonicalPipelineIds.insert(
            entry.CanonicalPipelineIds.begin(),
            entry.CanonicalPipelineIds.end());
        found->second.SettingsRevisions.insert(settingsRevision);
    }
}

bool PicaGraphicsPipelineInventory::Finish(std::string* error) {
    if (!Enabled() || mFinished) {
        return !mPath.empty();
    }
    if (mEntries.empty()) {
        SetError(error, "PICA pipeline inventory is empty");
        return false;
    }
    if (mDescriptorSchemaVersion == 0U || mDescriptorSchemaMismatch ||
        mStructuralCollision) {
        SetError(error, "PICA pipeline inventory contract is invalid");
        return false;
    }
    std::vector<PicaGraphicsPipelineManifestEntry> entries;
    entries.reserve(mEntries.size());
    for (const auto& [identity, entry] : mEntries) {
        entries.push_back(entry);
    }
    if (!WritePicaGraphicsPipelineManifest(
            mPath, mDescriptorSchemaVersion, entries, error)) {
        return false;
    }
    mFinished = true;
    return true;
}

void PicaGraphicsPipelineInventory::Clear() {
    mPath.clear();
    mEntries.clear();
    mDescriptorSchemaVersion = 0U;
    mDescriptorSchemaMismatch = false;
    mStructuralCollision = false;
    mFinished = false;
}

bool PicaGraphicsPipelineInventory::Enabled() const noexcept {
    return !mPath.empty() && !mFinished;
}

size_t PicaGraphicsPipelineInventory::EntryCount() const noexcept {
    return mEntries.size();
}

const std::filesystem::path& PicaGraphicsPipelineInventory::Path() const
    noexcept {
    return mPath;
}

} // namespace Fast::Oot3d
