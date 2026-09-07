#include "oot3d_native_pica_semantic_trace.h"

#include "oot3d_native_pica_program_descriptor.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

bool SelectedDrawFrame(uint64_t frame) {
    static const auto range = [] {
        std::array<uint64_t, 2> result{0, std::numeric_limits<uint64_t>::max()};
        const std::array<const char*, 2> names{"OOT3D_PICA_TRACE_FIRST_FRAME", "OOT3D_PICA_TRACE_LAST_FRAME"};
        for (size_t i = 0; i < names.size(); ++i) {
            if (const char* value = std::getenv(names[i])) {
                char* end = nullptr;
                const auto parsed = std::strtoull(value, &end, 10);
                if (end != value && *end == '\0') result[i] = parsed;
            }
        }
        return result;
    }();
    return frame >= range[0] && frame <= range[1];
}

const char* CompositionDomainName(
    Oot3dPicaCompositionDomain domain) noexcept {
    switch (domain) {
    case Oot3dPicaCompositionDomain::Scene:
        return "scene";
    case Oot3dPicaCompositionDomain::Ui:
        return "ui";
    case Oot3dPicaCompositionDomain::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* CompositionLayerName(
    Oot3dPicaCompositionLayer layer) noexcept {
    switch (layer) {
    case Oot3dPicaCompositionLayer::OpaqueWorld:
        return "opaque_world";
    case Oot3dPicaCompositionLayer::TransparentWorld:
        return "transparent_world";
    case Oot3dPicaCompositionLayer::Atmosphere:
        return "atmosphere";
    case Oot3dPicaCompositionLayer::Ui:
        return "ui";
    case Oot3dPicaCompositionLayer::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* CompositionProvenanceName(
    Oot3dPicaCompositionProvenance provenance) noexcept {
    switch (provenance) {
    case Oot3dPicaCompositionProvenance::NativeCmbDrawPass:
        return "native_cmb_draw_pass";
    case Oot3dPicaCompositionProvenance::NativeControlFlow:
        return "native_control_flow";
    case Oot3dPicaCompositionProvenance::NativeUiLifecycle:
        return "native_ui_lifecycle";
    case Oot3dPicaCompositionProvenance::Unknown:
        return "unknown";
    }
    return "unknown";
}

nlohmann::json RegisterBlock(const Oot3dPicaDrawPacket& packet,
                             uint16_t first, uint16_t last) {
    nlohmann::json values = nlohmann::json::array();
    for (uint16_t reg = first; reg <= last; ++reg) {
        values.push_back(packet.Registers[reg]);
    }
    return values;
}

nlohmann::json NonzeroRegisters(const Oot3dPicaDrawPacket& packet) {
    nlohmann::json values = nlohmann::json::array();
    for (size_t reg = 0; reg < packet.Registers.size(); ++reg) {
        if (packet.Registers[reg] != 0U) {
            values.push_back({reg, packet.Registers[reg]});
        }
    }
    return values;
}

nlohmann::json ShaderWords(const uint32_t* words, size_t count) {
    nlohmann::json values = nlohmann::json::array();
    for (size_t index = 0; index < count; ++index) {
        values.push_back(words[index]);
    }
    return values;
}

nlohmann::json DrawIdentityJson(
    const Oot3dPicaCanonicalDrawIdentity& identity) {
    return {
        {"schema_version", identity.SchemaVersion},
        {"vertex_program",
         FormatOot3dPicaCanonicalId(identity.VertexProgramId)},
        {"fragment_program",
         FormatOot3dPicaCanonicalId(identity.FragmentProgramId)},
        {"raster_state",
         FormatOot3dPicaCanonicalId(identity.RasterStateId)},
        {"pipeline", FormatOot3dPicaCanonicalId(identity.PipelineId)},
        {"dynamic_state",
         FormatOot3dPicaCanonicalId(identity.DynamicStateId)},
        {"full_register_state",
         FormatOot3dPicaCanonicalId(identity.FullRegisterStateId)},
    };
}

nlohmann::json FragmentLightingLayoutJson(
    const Oot3d::Renderer::PicaFragmentLightingLayout& layout) {
    if (!layout.Available()) {
        return nullptr;
    }
    nlohmann::json permutation = nlohmann::json::array();
    for (size_t slot = 0U; slot < layout.ActiveLightCount; ++slot) {
        permutation.push_back(layout.LightPermutation[slot]);
    }
    nlohmann::json lights = nlohmann::json::array();
    for (size_t nativeIndex = 0U; nativeIndex < layout.Lights.size();
         ++nativeIndex) {
        const auto& light = layout.Lights[nativeIndex];
        lights.push_back({
            {"native_index", nativeIndex},
            {"directional", light.Directional},
            {"two_sided_diffuse", light.TwoSidedDiffuse},
            {"geometric_factor_0", light.GeometricFactor0},
            {"geometric_factor_1", light.GeometricFactor1},
            {"shadow", light.ShadowEnabled},
            {"spot_attenuation", light.SpotAttenuationEnabled},
            {"distance_attenuation", light.DistanceAttenuationEnabled},
        });
    }
    nlohmann::json lutSamplers = nlohmann::json::array();
    for (const auto& sampler : layout.LutSamplers) {
        lutSamplers.push_back({
            {"input", static_cast<uint32_t>(sampler.Input)},
            {"scale", sampler.Scale},
            {"absolute_input", sampler.AbsoluteInput},
        });
    }
    return {
        {"schema_version", layout.SchemaVersion},
        {"valid", layout.Valid()},
        {"active_light_count", layout.ActiveLightCount},
        {"light_permutation", std::move(permutation)},
        {"lights", std::move(lights)},
        {"lut_samplers", std::move(lutSamplers)},
        {"environment_configuration", layout.EnvironmentConfiguration},
        {"fresnel_selector", layout.FresnelSelector},
        {"bump_texture_unit", layout.BumpTextureUnit},
        {"shadow_texture_unit", layout.ShadowTextureUnit},
        {"bump_mode", static_cast<uint32_t>(layout.BumpMode)},
        {"clamp_highlights", layout.ClampHighlights},
        {"recalculate_bump_vectors", layout.RecalculateBumpVectors},
        {"shadow_factor", layout.ShadowFactorEnabled},
        {"shadow_primary", layout.ShadowPrimary},
        {"shadow_secondary", layout.ShadowSecondary},
        {"shadow_alpha", layout.ShadowAlpha},
        {"invert_shadow", layout.InvertShadow},
    };
}

nlohmann::json FragmentFeaturesJson(
    const Oot3dPicaFragmentFeatureSet& features) {
    return {
        {"fragment_lighting", features.FragmentLightingEnabled},
        {"procedural_texture_enabled",
         features.ProceduralTextureEnabled},
        {"procedural_texture_referenced",
         features.ProceduralTextureReferenced},
        {"fog", features.FogEnabled},
        {"gas", features.GasEnabled},
        {"fog_mode", features.FogMode},
        {"texture0_type", features.Texture0Type},
        {"enabled_texture_mask", features.EnabledTextureMask},
        {"referenced_texture_mask", features.ReferencedTextureMask},
        {"unsupported_feature_mask", features.UnsupportedFeatureMask},
        {"fully_supported", features.FullySupported() },
        {"fragment_lighting_layout",
         FragmentLightingLayoutJson(features.FragmentLighting)},
    };
}

nlohmann::json FragmentOutputContractJson(
    const Oot3d::Renderer::PicaFragmentOutputContract& outputs) {
    return {
        {"schema_version", outputs.SchemaVersion},
        {"valid", outputs.Valid()},
        {"canonical_native", outputs.CanonicalNative()},
        {"color_location_mask", outputs.ColorLocationMask},
        {"native_color_location", outputs.NativeColorLocation},
        {"depth", static_cast<uint32_t>(outputs.Depth)},
        {"unsupported_color_location",
         outputs.UnsupportedColorLocation},
        {"duplicate_color_location", outputs.DuplicateColorLocation},
    };
}

nlohmann::json VertexProgramLayoutJson(const Oot3d::Renderer::PicaVertexShaderHookLayout& hooks) {
    const auto& transform = hooks.Transform;
    const auto& skeleton = hooks.Skeleton;
    nlohmann::json textureInputs = nlohmann::json::array();
    for (const auto& coordinate : hooks.TextureCoordinates) {
        for (size_t i = 0; i < coordinate.SourceCount && i < coordinate.Sources.size(); ++i)
            textureInputs.push_back(coordinate.Sources[i].InputRegister);
    }
    return {
        { "schema_version", hooks.SchemaVersion },
        { "texture_inputs", std::move(textureInputs) },
        { "transform",
          { { "available", transform.Available() },
            { "operation", static_cast<uint32_t>(transform.Operation) },
            { "projection_first_uniform", transform.ProjectionFirstUniform },
            { "view_first_uniform", transform.ViewFirstUniform },
            { "model_first_uniform", transform.ModelFirstUniform },
            { "position_input", transform.PositionInputRegister },
            { "normal_input", transform.NormalInputRegister } } },
        { "skeleton",
          { { "available", skeleton.Available() },
            { "operation", static_cast<uint32_t>(skeleton.Operation) },
            { "enable_boolean_uniform", skeleton.EnableBooleanUniform },
            { "multiple_influence_boolean_uniform", skeleton.MultipleInfluenceBooleanUniform },
            { "palette_first_uniform", skeleton.PaletteFirstUniform },
            { "rows_per_matrix", skeleton.RowsPerMatrix },
            { "bone_index_input", skeleton.BoneIndexInputRegister },
            { "bone_weight_input", skeleton.BoneWeightInputRegister },
            { "maximum_influences", skeleton.MaximumInfluences } } },
    };
}

nlohmann::json VertexProgramStateJson(const Oot3d::Renderer::PicaVertexShaderHookLayout& hooks,
                                      uint32_t booleanMask) {
    const auto& skeleton = hooks.Skeleton;
    const bool skeletonProgram = skeleton.Available();
    const bool skeletonActive = skeletonProgram && (booleanMask & (1U << skeleton.EnableBooleanUniform)) != 0U;
    const bool multipleInfluences = skeletonActive && skeleton.MultipleInfluenceBooleanUniform < 16U &&
                                    (booleanMask & (1U << skeleton.MultipleInfluenceBooleanUniform)) != 0U;
    return {
        { "schema_version", hooks.SchemaVersion },
        { "transform", { { "available", hooks.Transform.Available() }, { "uses_skeleton", skeletonActive } } },
        { "skeleton",
          { { "program_available", skeletonProgram },
            { "active", skeletonActive },
            { "multiple_influences", multipleInfluences },
            { "influence_count", skeletonActive ? (multipleInfluences ? skeleton.MaximumInfluences : 1U) : 0U } } },
    };
}

} // namespace

Oot3dPicaSemanticTraceWriter::Oot3dPicaSemanticTraceWriter(
    const std::filesystem::path& outputPath)
    : mOutputPath(outputPath) {
    if (mOutputPath.empty()) {
        return;
    }
    if (!mOutputPath.parent_path().empty()) {
        std::filesystem::create_directories(mOutputPath.parent_path());
    }
    mStream.open(mOutputPath, std::ios::binary | std::ios::trunc);
    if (!mStream) {
        throw std::runtime_error("could not open PICA semantic trace: " +
                                 mOutputPath.string());
    }
}

Oot3dPicaSemanticTraceWriter::~Oot3dPicaSemanticTraceWriter() {
    try {
        Finish();
    } catch (...) {
    }
}

bool Oot3dPicaSemanticTraceWriter::Enabled() const noexcept {
    return mStream.is_open();
}

const std::filesystem::path&
Oot3dPicaSemanticTraceWriter::OutputPath() const noexcept {
    return mOutputPath;
}

void Oot3dPicaSemanticTraceWriter::BeginSession(
    std::string_view renderer, std::string_view gameplayTiming,
    std::string_view uiProfile) {
    if (!Enabled() || mSessionStarted) {
        return;
    }
    mSessionStarted = true;
    WriteEvent({
        {"event", "session_begin"},
        {"descriptor_schema_version",
         kOot3dPicaProgramDescriptorSchemaVersion},
        {"renderer", renderer},
        {"gameplay_timing", gameplayTiming},
        {"ui_profile", uiProfile},
    });
}

void Oot3dPicaSemanticTraceWriter::RecordMemoryFill(
    uint64_t presentationFrame, uint32_t guestFrame,
    const Oot3dPicaMemoryFillSubmission& fill) {
    if (!Enabled()) {
        return;
    }
    WriteEvent({
        {"event", "memory_fill"},
        {"presentation_frame", presentationFrame},
        {"guest_frame", guestFrame},
        {"completion_id", fill.CompletionId},
        {"before_draw_submission_id", fill.BeforeDrawSubmissionId},
        {"start_physical_address", fill.StartPhysicalAddress},
        {"end_physical_address", fill.EndPhysicalAddress},
        {"value", fill.Value},
        {"control", fill.Control},
        {"interrupt",
         fill.Interrupt.has_value()
             ? nlohmann::json(static_cast<uint32_t>(*fill.Interrupt))
             : nlohmann::json(nullptr)},
    });
}

void Oot3dPicaSemanticTraceWriter::RecordDisplayTransfer(
    uint64_t presentationFrame, uint32_t guestFrame,
    const Oot3dPicaDisplayTransferSubmission& transfer,
    bool selectedForTopAtSubmission,
    bool topScreenBackdropSuppressionActive) {
    if (!Enabled()) {
        return;
    }
    WriteEvent({
        {"event", "display_transfer"},
        {"presentation_frame", presentationFrame},
        {"guest_frame", guestFrame},
        {"completion_id", transfer.CompletionId},
        {"after_draw_submission_id", transfer.AfterDrawSubmissionId},
        {"input_address", transfer.Transfer.InputAddress},
        {"output_address", transfer.Transfer.OutputAddress},
        {"input_physical_address", transfer.InputPhysicalAddress},
        {"output_physical_address", transfer.OutputPhysicalAddress},
        {"input_size", transfer.Transfer.InputSize},
        {"output_size", transfer.Transfer.OutputSize},
        {"flags", transfer.Transfer.Flags},
        {"signal_interrupt", transfer.SignalInterrupt},
        {"selected_for_top_at_submission", selectedForTopAtSubmission},
        {"topscreen_backdrop_suppression_active",
         topScreenBackdropSuppressionActive},
    });
}

void Oot3dPicaSemanticTraceWriter::RecordDraw(
    uint64_t presentationFrame, uint32_t guestFrame,
    const Oot3dPicaDrawSubmission& submission,
    const Oot3dPicaVulkanDrawPlan& plan, bool suppressed,
    bool usesCommonBackground, bool opaqueTargetInitialization) {
    if (!Enabled() || !SelectedDrawFrame(presentationFrame)) {
        return;
    }
    const auto& identity = plan.CanonicalIdentity;
    const auto fragmentFeatures = AnalyzeOot3dPicaFragmentFeatures(
        submission.Packet, plan.State);
    const std::string_view vertexSource =
        plan.ResolvedVertexShaderSource();
    const std::string_view fragmentSource =
        plan.ResolvedFragmentShaderSource();
    const uint64_t vertexSourceId = HashOot3dPicaCanonicalBytes(
        {reinterpret_cast<const uint8_t*>(vertexSource.data()),
         vertexSource.size()});
    const uint64_t fragmentSourceId = HashOot3dPicaCanonicalBytes(
        {reinterpret_cast<const uint8_t*>(fragmentSource.data()),
         fragmentSource.size()});
    const auto* vertexHooks =
        plan.VertexShader.TemporalProgram != nullptr ? &plan.VertexShader.TemporalProgram->Hooks : nullptr;
    const nlohmann::json vertexProgramLayout =
        vertexHooks != nullptr ? VertexProgramLayoutJson(*vertexHooks) : nlohmann::json(nullptr);
    const nlohmann::json vertexProgramState =
        vertexHooks != nullptr ? VertexProgramStateJson(*vertexHooks, plan.VertexShader.Uniforms.BooleanMask)
                               : nlohmann::json(nullptr);
    if (mWrittenVertexSources.insert(vertexSourceId).second) {
        WriteEvent({
            {"event", "shader_source"},
            {"stage", "vertex"},
            {"source_id", FormatOot3dPicaCanonicalId(vertexSourceId)},
            {"canonical_program_id",
             FormatOot3dPicaCanonicalId(identity.VertexProgramId)},
            {"legacy_state_key",
             FormatOot3dPicaCanonicalId(plan.VertexShader.StateKey) },
            { "main_offset", plan.State.ShaderInterface.VertexMainOffset },
            { "program_words", ShaderWords(submission.Packet.VertexShader.Program.data(),
                                           submission.Packet.VertexShader.ProgramWordCount) },
            { "swizzle_words", ShaderWords(submission.Packet.VertexShader.Swizzles.data(),
                                           submission.Packet.VertexShader.SwizzleWordCount) },
            { "vertex_program_layout", vertexProgramLayout },
            {"source", vertexSource},
        });
    }
    if (mWrittenFragmentSources.insert(fragmentSourceId).second) {
        WriteEvent({
            {"event", "shader_source"},
            {"stage", "fragment"},
            {"source_id", FormatOot3dPicaCanonicalId(fragmentSourceId)},
            {"canonical_program_id",
             FormatOot3dPicaCanonicalId(identity.FragmentProgramId)},
            {"legacy_state_key",
             FormatOot3dPicaCanonicalId(plan.FragmentShader.StateKey)},
            {"fragment_output_contract",
             FragmentOutputContractJson(plan.FragmentShader.Hooks.Outputs)},
            {"source", fragmentSource},
        });
    }
    nlohmann::json textures = nlohmann::json::array();
    for (const auto& texture : plan.Textures) {
        const auto bytes = texture.ResolvedNativeBytes();
        const uint64_t resourceHash = texture.NativeContentHashAvailable
                                          ? texture.NativeContentHash
                                          : HashOot3dPicaCanonicalBytes(bytes);
        const uint64_t baseLevelHash =
            texture.NativeBaseLevelContentHashAvailable
                ? texture.NativeBaseLevelContentHash
                : resourceHash;
        textures.push_back({
            {"slot", texture.Slot},
            {"physical_address", texture.State.PhysicalAddress},
            {"width", texture.State.Width},
            {"height", texture.State.Height},
            {"format", texture.State.Format},
            {"type", texture.State.Type},
            {"wrap_s", texture.State.WrapS},
            {"wrap_t", texture.State.WrapT},
            {"min_linear", texture.State.MinLinear},
            {"mag_linear", texture.State.MagLinear},
            {"mip_linear", texture.State.MipLinear},
            {"lod_bias_raw", texture.State.LodBiasRaw},
            {"lod_bias", static_cast<float>(texture.State.LodBiasRaw) /
                             256.0F},
            {"min_mip_level", texture.State.MinMipLevel},
            {"max_mip_level", texture.State.MaxMipLevel},
            {"mip_level_count",
             Oot3dPicaTextureMipLevelCount(texture.State)},
            {"byte_count", bytes.size()},
            {"content_id", FormatOot3dPicaCanonicalId(baseLevelHash)},
            {"resource_content_id",
             FormatOot3dPicaCanonicalId(resourceHash)},
        });
    }
    nlohmann::json vertexBindings = nlohmann::json::array();
    for (const auto& binding : plan.VertexBindings) {
        const auto bytes = binding.ResolvedBytes();
        vertexBindings.push_back({
            {"binding", binding.Binding},
            {"stride", binding.ByteStride},
            {"input_rate", static_cast<uint32_t>(binding.InputRate)},
            {"source_physical_address", binding.SourcePhysicalAddress},
            {"byte_count", bytes.size()},
            {"content_id", FormatOot3dPicaCanonicalId(
                               HashOot3dPicaCanonicalBytes(bytes))},
            {"content_version",
             binding.ContentVersionAvailable
                 ? nlohmann::json(binding.ContentVersion)
                 : nlohmann::json(nullptr)},
        });
    }
    nlohmann::json attributes = nlohmann::json::array();
    for (const auto& attribute : plan.VertexAttributes) {
        nlohmann::json samples = nlohmann::json::array();
        if (attribute.Format == Oot3dPicaVertexFormat::Float) {
            for (const auto& binding : plan.VertexBindings) {
                if (binding.Binding != attribute.Binding || binding.ByteStride == 0) continue;
                const auto bytes = binding.ResolvedBytes();
                for (size_t record = 0; record < 6; ++record) {
                    const size_t offset = record * binding.ByteStride + attribute.ByteOffset;
                    if (offset > bytes.size() || attribute.ComponentCount * sizeof(float) > bytes.size() - offset) break;
                    nlohmann::json values = nlohmann::json::array();
                    for (size_t component = 0; component < attribute.ComponentCount; ++component) {
                        float value;
                        std::memcpy(&value, bytes.data() + offset + component * sizeof(float), sizeof(value));
                        values.push_back(std::isfinite(value) ? nlohmann::json(value) : nlohmann::json(nullptr));
                    }
                    samples.push_back(std::move(values));
                }
            }
        }
        attributes.push_back({
            {"location", attribute.Location},
            {"binding", attribute.Binding},
            {"format", static_cast<uint32_t>(attribute.Format)},
            {"component_count", attribute.ComponentCount},
            {"byte_offset", attribute.ByteOffset},
            {"first_float_records", std::move(samples)},
        });
    }

    WriteEvent({
        {"event", "draw"},
        {"presentation_frame", presentationFrame},
        {"guest_frame", guestFrame},
        {"submission_id", submission.Id},
        {"command_list_address", submission.Packet.CommandListAddress},
        {"command_list_offset_words",
         submission.Packet.CommandListOffsetWords},
        {"composition_domain",
         CompositionDomainName(plan.CompositionDomain)},
        {"composition_layer",
         CompositionLayerName(plan.Composition.Layer)},
        {"composition_provenance",
         CompositionProvenanceName(plan.Composition.Provenance)},
        {"composition_source_pc", plan.Composition.SourcePc},
        {"composition_native_value", plan.Composition.NativeValue},
        {"identity", DrawIdentityJson(identity)},
        {"fragment_features", FragmentFeaturesJson(fragmentFeatures)},
        {"legacy_shader_keys",
         {{"vertex", FormatOot3dPicaCanonicalId(plan.VertexShader.StateKey)},
          {"fragment",
           FormatOot3dPicaCanonicalId(plan.FragmentShader.StateKey)}}},
        {"generated_source_ids",
         {{"vertex", FormatOot3dPicaCanonicalId(vertexSourceId)},
          {"fragment", FormatOot3dPicaCanonicalId(fragmentSourceId)}} },
        { "vertex_program_state", vertexProgramState },
        {"routing",
         {{"suppressed", suppressed},
          {"uses_common_background", usesCommonBackground},
          {"opaque_target_initialization", opaqueTargetInitialization}}},
        {"geometry",
         {{"identity", FormatOot3dPicaCanonicalId(plan.GeometryIdentity)},
          {"identity_available", plan.GeometryIdentityAvailable},
          {"content_version", plan.GeometryContentVersion},
          {"indexed", plan.Indexed},
          {"indices_are_16_bit", plan.IndicesAre16Bit},
          {"base_vertex", plan.BaseVertex},
          {"vertex_count", plan.VertexCount},
          {"index_physical_address", plan.IndexPhysicalAddress},
          {"index_byte_count", plan.ResolvedIndexBytes().size()},
          {"vertex_bindings", std::move(vertexBindings)},
          {"attributes", std::move(attributes)}}},
        {"framebuffer",
         {{"color_physical_address",
           plan.State.Framebuffer.ColorPhysicalAddress},
          {"depth_physical_address",
           plan.State.Framebuffer.DepthPhysicalAddress},
          {"width", plan.State.Framebuffer.Width},
          {"height", plan.State.Framebuffer.Height},
          {"color_format", plan.State.Framebuffer.ColorFormat},
          {"depth_format", plan.State.Framebuffer.DepthFormat},
          {"flipped", plan.State.Framebuffer.Flipped}}},
        {"viewport",
         {{"half_width", plan.State.Viewport.HalfWidth},
          {"half_height", plan.State.Viewport.HalfHeight},
          {"depth_range", plan.State.Viewport.DepthRange},
          {"near_plane", plan.State.Viewport.NearPlane},
          {"corner_x", plan.State.Viewport.CornerX},
          {"corner_y", plan.State.Viewport.CornerY},
          {"z_buffering", plan.State.Viewport.ZBuffering}}},
        {"scissor",
         {{"mode", static_cast<uint32_t>(plan.State.Scissor.Mode)},
          {"x1", plan.State.Scissor.X1},
          {"y1", plan.State.Scissor.Y1},
          {"x2", plan.State.Scissor.X2},
          {"y2", plan.State.Scissor.Y2}}},
        {"topology", static_cast<uint32_t>(plan.State.Topology)},
        {"cull_mode", static_cast<uint32_t>(plan.State.CullMode)},
        {"output_merger",
         {{"fragment_operation_mode",
           plan.State.OutputMerger.FragmentOperationMode},
          {"color_write_mask", plan.State.OutputMerger.ColorWriteMask},
          {"logic_operation",
           static_cast<uint32_t>(plan.State.OutputMerger.LogicOperation)},
          {"blend_enabled", plan.State.OutputMerger.Blend.Enabled},
          {"blend_color_equation",
           static_cast<uint32_t>(
               plan.State.OutputMerger.Blend.ColorEquation)},
          {"blend_alpha_equation",
           static_cast<uint32_t>(
               plan.State.OutputMerger.Blend.AlphaEquation)},
          {"blend_source_color",
           static_cast<uint32_t>(plan.State.OutputMerger.Blend.SourceColor)},
          {"blend_destination_color",
           static_cast<uint32_t>(
               plan.State.OutputMerger.Blend.DestinationColor)},
          {"depth_test_enabled",
           plan.State.OutputMerger.Depth.TestEnabled},
          {"depth_write_enabled",
           plan.State.OutputMerger.Depth.WriteEnabled},
          {"depth_compare",
           static_cast<uint32_t>(plan.State.OutputMerger.Depth.Compare)},
          {"stencil_enabled", plan.State.OutputMerger.Stencil.Enabled}}},
        {"textures", std::move(textures)},
        {"register_blocks",
         {{"texture", RegisterBlock(submission.Packet, 0x080U, 0x0AFU)},
          {"tev_fog", RegisterBlock(submission.Packet, 0x0C0U, 0x0FDU)},
          {"output_merger",
           RegisterBlock(submission.Packet, 0x100U, 0x11FU)},
          {"lighting", RegisterBlock(submission.Packet, 0x140U, 0x1D9U)}}},
        {"nonzero_registers", NonzeroRegisters(submission.Packet)},
    });
}

void Oot3dPicaSemanticTraceWriter::RecordPresentationSelection(
    uint64_t presentationFrame, uint32_t guestFrame, bool lcdForceBlack,
    std::optional<uint32_t> topAddressLeft,
    std::optional<uint32_t> topAddressRight,
    const Oot3dPicaDisplayTransferSubmission* selectedTransfer) {
    if (!Enabled()) {
        return;
    }
    WriteEvent({
        {"event", "presentation_selection"},
        {"presentation_frame", presentationFrame},
        {"guest_frame", guestFrame},
        {"lcd_force_black", lcdForceBlack},
        {"top_address_left",
         topAddressLeft.has_value() ? nlohmann::json(*topAddressLeft)
                                    : nlohmann::json(nullptr)},
        {"top_address_right",
         topAddressRight.has_value() ? nlohmann::json(*topAddressRight)
                                     : nlohmann::json(nullptr)},
        {"selected_transfer_completion_id",
         selectedTransfer != nullptr
             ? nlohmann::json(selectedTransfer->CompletionId)
             : nlohmann::json(nullptr)},
        {"selected_transfer_input_address",
         selectedTransfer != nullptr
             ? nlohmann::json(selectedTransfer->Transfer.InputAddress)
             : nlohmann::json(nullptr)},
        {"selected_transfer_output_address",
         selectedTransfer != nullptr
             ? nlohmann::json(selectedTransfer->Transfer.OutputAddress)
             : nlohmann::json(nullptr)},
        {"selected_transfer_flags",
         selectedTransfer != nullptr
             ? nlohmann::json(selectedTransfer->Transfer.Flags)
             : nlohmann::json(nullptr)},
    });
}

void Oot3dPicaSemanticTraceWriter::RecordFrameBoundary(
    uint64_t presentationFrame, uint32_t guestFrame, bool guestAdvanced) {
    if (!Enabled()) {
        return;
    }
    WriteEvent({
        {"event", "frame_boundary"},
        {"presentation_frame", presentationFrame},
        {"guest_frame", guestFrame},
        {"guest_advanced", guestAdvanced},
    });
}

void Oot3dPicaSemanticTraceWriter::Finish() {
    if (!Enabled() || mFinished) {
        return;
    }
    mFinished = true;
    WriteEvent({{"event", "session_end"}});
    mStream.flush();
    if (!mStream) {
        throw std::runtime_error("failed to finish PICA semantic trace: " +
                                 mOutputPath.string());
    }
}

void Oot3dPicaSemanticTraceWriter::WriteEvent(nlohmann::json event) {
    if (!Enabled() ||
        (mFinished && event.value("event", "") != "session_end")) {
        return;
    }
    event["format"] = kOot3dPicaSemanticTraceFormat;
    event["event_id"] = mNextEventId++;
    mStream << event.dump() << '\n';
    if (!mStream) {
        throw std::runtime_error("failed to write PICA semantic trace: " +
                                 mOutputPath.string());
    }
}

} // namespace Oot3dNativeGame
