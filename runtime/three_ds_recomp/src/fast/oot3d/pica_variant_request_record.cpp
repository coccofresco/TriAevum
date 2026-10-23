#include "fast/oot3d/pica_variant_request_record.h"

#include "fast/oot3d/graphics_settings_persistence.h"

#include <bit>
#include <charconv>
#include <cstdio>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Fast::Oot3d {
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
        throw std::runtime_error("invalid hexadecimal value");
    return parsed;
}

PicaVariantShaderIdentity Identity(const ::Oot3d::Renderer::PicaShaderSourceIdentity& identity) {
    return {identity.Id, identity.SecondaryHash, identity.Size};
}

Json EncodeIdentity(const PicaVariantShaderIdentity& identity) {
    return Json::array({Hex64(identity.Id), Hex64(identity.SecondaryHash), identity.Size});
}

PicaVariantShaderIdentity DecodeIdentity(const Json& value) {
    if (!value.is_array() || value.size() != 3U) throw std::runtime_error("invalid shader identity");
    return {ParseHex64(value[0]), ParseHex64(value[1]), value[2].get<uint64_t>()};
}

template <typename Enum>
uint32_t Raw(Enum value) {
    return static_cast<uint32_t>(value);
}

template <typename Enum>
Enum FromRaw(const Json& value) {
    return static_cast<Enum>(value.get<uint32_t>());
}
} // namespace

PicaVariantRequestWitness WitnessPicaVariantRequest(const PicaShaderPipelineRequest& request,
                                                    const PicaShaderPipelineResult& result) {
    PicaVariantRequestWitness witness;
    witness.CanonicalVertex = Identity(request.VertexShaderSourceIdentity.Available()
                                           ? request.VertexShaderSourceIdentity
                                           : ::Oot3d::Renderer::IdentifyPicaShaderSource(request.VertexShaderSource));
    witness.CanonicalFragment =
        Identity(request.FragmentShaderSourceIdentity.Available()
                     ? request.FragmentShaderSourceIdentity
                     : ::Oot3d::Renderer::IdentifyPicaShaderSource(request.FragmentShaderSource));
    witness.TypedVertexValid = request.TemporalVertexProgram.ValidFor(request.VertexShaderSource);
    witness.TypedVertexSurface = request.TemporalVertexProgram.Hooks.SurfaceProgram() != nullptr;
    witness.TypedVertexTransform = request.TemporalVertexProgram.Hooks.TransformProgram() != nullptr;
    witness.Vertex = Identity(result.VertexShaderSourceIdentity);
    witness.Fragment = Identity(result.FragmentShaderSourceIdentity);
    witness.VertexKey = result.VertexShaderKey;
    witness.FragmentKey = result.FragmentShaderKey;
    witness.RequestedFeatures = static_cast<uint32_t>(result.RequestedFeatures);
    witness.AppliedFeatures = static_cast<uint32_t>(result.AppliedFeatures);
    return witness;
}

Json EncodePicaVariantRequest(uint64_t canonicalPipelineId, uint32_t effectsId, uint64_t settingsRevision,
                              const PicaShaderPipelineRequest& request, const PicaVariantRequestWitness& witness) {
    const auto& draw = request.Draw;
    const auto& blend = draw.Blend;
    Json constant = Json::array();
    for (const float value : blend.ConstantColor) constant.push_back(std::bit_cast<uint32_t>(value));
    Json reflection = nullptr;
    if (request.ReflectionProfile.has_value())
        reflection = Json::array({std::bit_cast<uint32_t>(request.ReflectionProfile->Reflectivity),
                                  std::bit_cast<uint32_t>(request.ReflectionProfile->Roughness)});
    return {
        {"record", "request"},
        {"pipeline_id", Hex64(canonicalPipelineId)},
        {"effects", effectsId},
        {"revision", settingsRevision},
        {"vertex_key", Hex64(request.VertexShaderKey)},
        {"fragment_key", Hex64(request.FragmentShaderKey)},
        {"draw",
         {{"framebuffer", Json::array({draw.FramebufferWidth, draw.FramebufferHeight})},
          {"fragment_mode", draw.FragmentOperationMode},
          {"depth_test", draw.DepthTestEnabled},
          {"depth_write", draw.DepthWriteEnabled},
          {"blend",
           {{"enabled", blend.Enabled},
            {"equation", Json::array({Raw(blend.EquationRgb), Raw(blend.EquationAlpha)})},
            {"factors", Json::array({Raw(blend.SourceRgb), Raw(blend.DestRgb), Raw(blend.SourceAlpha),
                                     Raw(blend.DestAlpha)})},
            {"constant", constant}}},
          {"color_mask", draw.ColorWriteMask},
          {"perspective", draw.PerspectiveProjection},
          {"directional_shadow_receiver", draw.DirectionalShadowReceiver},
          {"domain", Raw(draw.CompositionDomain)},
          {"depth_compare", Raw(draw.DepthCompare)},
          {"layer", Raw(draw.CompositionLayer)},
          {"point_shadow_receiver", draw.PointShadowReceiver},
          {"isolated_lighting", draw.IsolatedLighting}}},
        {"temporal_motion", request.TemporalMotionEnabled},
        {"reflection", reflection},
        {"native_fidelity", request.RequireNativeFidelity},
        {"surface_lighting", request.SurfaceLightingAvailable},
        {"pbr",
         {{"slot", request.Pbr.TextureSlot},
          {"native_lighting", request.Pbr.NativeFragmentLighting},
          {"native_luts", request.Pbr.NativeAttenuationLuts},
          {"seam_softening", request.Pbr.SeamSoftening}}},
        {"relief", request.ReliefTexture},
        {"witness",
         {{"canonical_vertex", EncodeIdentity(witness.CanonicalVertex)},
          {"canonical_fragment", EncodeIdentity(witness.CanonicalFragment)},
          {"typed_vertex", Json::array({witness.TypedVertexValid, witness.TypedVertexSurface,
                                        witness.TypedVertexTransform})},
          {"vertex", EncodeIdentity(witness.Vertex)},
          {"fragment", EncodeIdentity(witness.Fragment)},
          {"vertex_key", Hex64(witness.VertexKey)},
          {"fragment_key", Hex64(witness.FragmentKey)},
          {"requested", witness.RequestedFeatures},
          {"applied", witness.AppliedFeatures}}},
    };
}

bool DecodePicaVariantRequest(const Json& line, PicaVariantRequestRecord& record, std::string* error) {
    try {
        PicaVariantRequestRecord decoded;
        decoded.CanonicalPipelineId = ParseHex64(line.at("pipeline_id"));
        decoded.EffectsId = line.at("effects").get<uint32_t>();
        decoded.SettingsRevision = line.at("revision").get<uint64_t>();
        auto& request = decoded.Request;
        request.VertexShaderKey = ParseHex64(line.at("vertex_key"));
        request.FragmentShaderKey = ParseHex64(line.at("fragment_key"));
        const auto& draw = line.at("draw");
        auto& info = request.Draw;
        info.FramebufferWidth = draw.at("framebuffer").at(0).get<uint16_t>();
        info.FramebufferHeight = draw.at("framebuffer").at(1).get<uint16_t>();
        info.FragmentOperationMode = draw.at("fragment_mode").get<uint8_t>();
        info.DepthTestEnabled = draw.at("depth_test").get<bool>();
        info.DepthWriteEnabled = draw.at("depth_write").get<bool>();
        const auto& blend = draw.at("blend");
        info.Blend.Enabled = blend.at("enabled").get<bool>();
        info.Blend.EquationRgb = FromRaw<::Oot3d::Renderer::NativeBlendEquation>(blend.at("equation").at(0));
        info.Blend.EquationAlpha = FromRaw<::Oot3d::Renderer::NativeBlendEquation>(blend.at("equation").at(1));
        info.Blend.SourceRgb = FromRaw<::Oot3d::Renderer::NativeBlendFactor>(blend.at("factors").at(0));
        info.Blend.DestRgb = FromRaw<::Oot3d::Renderer::NativeBlendFactor>(blend.at("factors").at(1));
        info.Blend.SourceAlpha = FromRaw<::Oot3d::Renderer::NativeBlendFactor>(blend.at("factors").at(2));
        info.Blend.DestAlpha = FromRaw<::Oot3d::Renderer::NativeBlendFactor>(blend.at("factors").at(3));
        const auto& constant = blend.at("constant");
        if (constant.size() != 4U) throw std::runtime_error("blend constant is not a vec4");
        for (size_t index = 0; index < 4U; ++index)
            info.Blend.ConstantColor[index] = std::bit_cast<float>(constant[index].get<uint32_t>());
        info.ColorWriteMask = draw.at("color_mask").get<uint8_t>();
        info.PerspectiveProjection = draw.at("perspective").get<bool>();
        info.DirectionalShadowReceiver = draw.at("directional_shadow_receiver").get<bool>();
        info.CompositionDomain = FromRaw<::Oot3d::Renderer::PicaCompositionDomain>(draw.at("domain"));
        info.DepthCompare = FromRaw<::Oot3d::Renderer::PicaCompareFunction>(draw.at("depth_compare"));
        info.CompositionLayer = FromRaw<Renderer3ds::PicaCompositionLayer>(draw.at("layer"));
        info.PointShadowReceiver = draw.at("point_shadow_receiver").get<bool>();
        info.IsolatedLighting = draw.at("isolated_lighting").get<bool>();
        request.TemporalMotionEnabled = line.at("temporal_motion").get<bool>();
        if (const auto& reflection = line.at("reflection"); !reflection.is_null())
            request.ReflectionProfile = ReflectionMaterialParameters{
                std::bit_cast<float>(reflection.at(0).get<uint32_t>()),
                std::bit_cast<float>(reflection.at(1).get<uint32_t>())};
        request.RequireNativeFidelity = line.at("native_fidelity").get<bool>();
        request.SurfaceLightingAvailable = line.at("surface_lighting").get<bool>();
        const auto& pbr = line.at("pbr");
        request.Pbr.TextureSlot = pbr.at("slot").get<uint8_t>();
        request.Pbr.NativeFragmentLighting = pbr.at("native_lighting").get<bool>();
        request.Pbr.NativeAttenuationLuts = pbr.at("native_luts").get<bool>();
        request.Pbr.SeamSoftening = pbr.at("seam_softening").get<bool>();
        request.ReliefTexture = line.at("relief").get<uint8_t>();
        const auto& witness = line.at("witness");
        auto& out = decoded.Witness;
        out.CanonicalVertex = DecodeIdentity(witness.at("canonical_vertex"));
        out.CanonicalFragment = DecodeIdentity(witness.at("canonical_fragment"));
        const auto& typed = witness.at("typed_vertex");
        out.TypedVertexValid = typed.at(0).get<bool>();
        out.TypedVertexSurface = typed.at(1).get<bool>();
        out.TypedVertexTransform = typed.at(2).get<bool>();
        out.Vertex = DecodeIdentity(witness.at("vertex"));
        out.Fragment = DecodeIdentity(witness.at("fragment"));
        out.VertexKey = ParseHex64(witness.at("vertex_key"));
        out.FragmentKey = ParseHex64(witness.at("fragment_key"));
        out.RequestedFeatures = witness.at("requested").get<uint32_t>();
        out.AppliedFeatures = witness.at("applied").get<uint32_t>();
        record = std::move(decoded);
        return true;
    } catch (const std::exception& exception) {
        SetError(error, std::string("invalid variant request record: ") + exception.what());
        return false;
    }
}

Json EncodePicaVariantEffects(const EffectsSettings& effects, const GraphicsSettings& context) {
    GraphicsSettings settings = context;
    settings.Preset = GraphicsPreset::Custom;
    settings.Effects = effects;
    return SerializeGraphicsSettings(settings);
}

bool DecodePicaVariantEffects(const Json& value, EffectsSettings& effects, std::string* error) {
    // The recorded effects are the effective ones the cache resolved (after
    // capability fallbacks and F2 overrides), not user settings: decode the
    // fields without the settings validation, which would "correct" them.
    const auto loaded = DeserializeGraphicsSettingsFields(value);
    // A recording is valid only if its effects decode exactly: re-encoding
    // must reproduce the stored document (no field corrected or unknown).
    if (loaded.UnsupportedFutureVersion) {
        SetError(error, "recorded effects use a future settings schema");
        return false;
    }
    // Only the effects reach the variant cache; they must survive unchanged.
    if (const auto encoded = SerializeGraphicsSettings(loaded.Value);
        !encoded.contains("Effects") || !value.contains("Effects") || encoded.at("Effects") != value.at("Effects")) {
        SetError(error, "recorded effects do not decode exactly with this build: " +
                            Json::diff(value.value("Effects", Json{}), encoded.value("Effects", Json{}))
                                .dump()
                                .substr(0, 600));
        return false;
    }
    effects = loaded.Value.Effects;
    return true;
}

bool LoadPicaVariantRequests(const std::filesystem::path& path, PicaVariantRequestSession& session,
                             std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "cannot read " + path.string());
        return false;
    }
    PicaVariantRequestSession loaded;
    bool header = false;
    std::string text;
    size_t lineNumber = 0;
    while (std::getline(input, text)) {
        ++lineNumber;
        Json line;
        try {
            line = Json::parse(text);
        } catch (const std::exception&) {
            if (input.peek() == std::char_traits<char>::eof()) break;  // interrupted session
            SetError(error, path.string() + ":" + std::to_string(lineNumber) + ": invalid JSON");
            return false;
        }
        const auto record = line.value("record", std::string{});
        std::string lineError;
        if (record == "session") {
            if (line.value("format", std::string{}) != kPicaVariantRequestRecordFormat) {
                SetError(error, path.string() + ": unsupported variant request format");
                return false;
            }
            loaded.FaceNormalGuideDiagnostic = line.at("face_normal_guide").get<bool>();
            header = true;
        } else if (!header) {
            SetError(error, path.string() + ": variant requests lack a session header");
            return false;
        } else if (record == "effects") {
            EffectsSettings effects;
            if (!DecodePicaVariantEffects(line.at("graphics"), effects, &lineError)) {
                SetError(error, path.string() + ":" + std::to_string(lineNumber) + ": " + lineError);
                return false;
            }
            loaded.Effects[line.at("id").get<uint32_t>()] = effects;
        } else if (record == "request") {
            PicaVariantRequestRecord request;
            if (!DecodePicaVariantRequest(line, request, &lineError) ||
                !loaded.Effects.contains(request.EffectsId)) {
                SetError(error, path.string() + ":" + std::to_string(lineNumber) + ": " +
                                    (lineError.empty() ? "request references unknown effects" : lineError));
                return false;
            }
            loaded.Requests.push_back(std::move(request));
        }
    }
    if (!header) {
        SetError(error, path.string() + ": empty variant request recording");
        return false;
    }
    session = std::move(loaded);
    return true;
}

bool PicaVariantRequestRecorder::Configure(const std::filesystem::path& path, std::string* error) {
    mStream.close();
    mSeen.clear();
    mEffectsIds.clear();
    mLastEffects = nullptr;
    mRequestCount = 0;
    mStream.open(path, std::ios::binary | std::ios::trunc);
    if (!mStream) {
        SetError(error, "cannot write variant requests to " + path.string());
        return false;
    }
    mStream << Json{{"record", "session"},
                    {"format", kPicaVariantRequestRecordFormat},
                    {"face_normal_guide", PicaFaceNormalGuideDiagnosticRequested()}}
                   .dump()
            << '\n';
    mStream.flush();
    return true;
}

uint32_t PicaVariantRequestRecorder::EffectsId(const EffectsSettings& effects, const GraphicsSettings& context,
                                               uint64_t settingsRevision) {
    if (mLastEffects == &effects && mLastRevision == settingsRevision) return mLastEffectsId;
    auto graphics = EncodePicaVariantEffects(effects, context);
    auto text = graphics.dump();
    auto [found, inserted] = mEffectsIds.try_emplace(std::move(text), static_cast<uint32_t>(mEffectsIds.size() + 1U));
    if (inserted) {
        mStream << Json{{"record", "effects"}, {"id", found->second}, {"graphics", std::move(graphics)}}.dump()
                << '\n';
    }
    mLastEffects = &effects;
    mLastRevision = settingsRevision;
    mLastEffectsId = found->second;
    return mLastEffectsId;
}

void PicaVariantRequestRecorder::Observe(uint64_t canonicalPipelineId, const PicaShaderPipelineRequest& request,
                                         const EffectsSettings& effects, const GraphicsSettings& context,
                                         uint64_t settingsRevision, const PicaShaderPipelineResult& result) {
    if (!Enabled()) return;
    const uint32_t effectsId = EffectsId(effects, context, settingsRevision);
    // Every draw reaches here; the encoded line is built only for a key not
    // seen yet. The key holds every encoded field.
    std::string key;
    key.reserve(160);
    const auto append = [&key](const auto& value) {
        key.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    const auto& draw = request.Draw;
    append(canonicalPipelineId);
    append(effectsId);
    append(settingsRevision);
    append(request.VertexShaderKey);
    append(request.FragmentShaderKey);
    append(draw.FramebufferWidth);
    append(draw.FramebufferHeight);
    append(draw.FragmentOperationMode);
    append(draw.ColorWriteMask);
    append(static_cast<uint32_t>(draw.Blend.EquationRgb) | (static_cast<uint32_t>(draw.Blend.EquationAlpha) << 4U) |
           (static_cast<uint32_t>(draw.Blend.SourceRgb) << 8U) | (static_cast<uint32_t>(draw.Blend.DestRgb) << 13U) |
           (static_cast<uint32_t>(draw.Blend.SourceAlpha) << 18U) |
           (static_cast<uint32_t>(draw.Blend.DestAlpha) << 23U));
    append(draw.Blend.ConstantColor);
    append(static_cast<uint32_t>(draw.CompositionDomain) | (static_cast<uint32_t>(draw.DepthCompare) << 8U) |
           (static_cast<uint32_t>(draw.CompositionLayer) << 16U));
    append(static_cast<uint16_t>((draw.DepthTestEnabled ? 1U : 0U) | (draw.DepthWriteEnabled ? 2U : 0U) |
                                 (draw.Blend.Enabled ? 4U : 0U) | (draw.PerspectiveProjection ? 8U : 0U) |
                                 (draw.DirectionalShadowReceiver ? 16U : 0U) |
                                 (draw.PointShadowReceiver ? 32U : 0U) | (draw.IsolatedLighting ? 64U : 0U) |
                                 (request.TemporalMotionEnabled ? 128U : 0U) |
                                 (request.RequireNativeFidelity ? 256U : 0U) |
                                 (request.SurfaceLightingAvailable ? 512U : 0U) |
                                 (request.Pbr.NativeFragmentLighting ? 1024U : 0U) |
                                 (request.Pbr.NativeAttenuationLuts ? 2048U : 0U) |
                                 (request.Pbr.SeamSoftening ? 4096U : 0U) |
                                 (request.ReflectionProfile.has_value() ? 8192U : 0U)));
    append(request.Pbr.TextureSlot);
    append(request.ReliefTexture);
    if (request.ReflectionProfile.has_value()) {
        append(request.ReflectionProfile->Reflectivity);
        append(request.ReflectionProfile->Roughness);
    }
    append(request.VertexShaderSourceIdentity.Id);
    append(request.FragmentShaderSourceIdentity.Id);
    append(result.VertexShaderSourceIdentity.Id);
    append(result.FragmentShaderSourceIdentity.Id);
    if (!mSeen.insert(std::move(key)).second) return;
    const auto line = EncodePicaVariantRequest(canonicalPipelineId, effectsId, settingsRevision, request,
                                               WitnessPicaVariantRequest(request, result))
                          .dump();
    mStream << line << '\n';
    mStream.flush();
    ++mRequestCount;
}

} // namespace Fast::Oot3d
