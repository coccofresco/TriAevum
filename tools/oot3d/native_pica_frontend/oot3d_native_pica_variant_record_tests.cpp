// Issue #76 recordings: the variant request and raw draw codecs must be
// lossless, recorded effects must decode exactly (effective values, never
// "corrected" by settings validation), and the recorders must deduplicate.
#include "fast/oot3d/graphics_settings_persistence.h"
#include "fast/oot3d/pica_variant_request_record.h"
#include "oot3d_native_pica_raw_draw_record.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

using Json = nlohmann::json;
using namespace Fast::Oot3d;

namespace {
void Require(bool value, const char* what) {
    if (!value) throw std::runtime_error(std::string("test failed: ") + what);
}

PicaShaderPipelineRequest Request() {
    PicaShaderPipelineRequest request;
    request.VertexShaderSource = "void main() { gl_Position = vec4(0.0); }";
    request.FragmentShaderSource = "void main() {}";
    request.VertexShaderKey = 0x0123456789abcdefULL;
    request.FragmentShaderKey = 0xfedcba9876543210ULL;
    auto& draw = request.Draw;
    draw.FramebufferWidth = 400;
    draw.FramebufferHeight = 240;
    draw.FragmentOperationMode = 3;
    draw.DepthTestEnabled = true;
    draw.Blend.Enabled = true;
    draw.Blend.EquationRgb = static_cast<decltype(draw.Blend.EquationRgb)>(1);
    draw.Blend.DestAlpha = static_cast<decltype(draw.Blend.DestAlpha)>(7);
    const float constant[] = {0.25F, -0.0F, 1.0F, 0.1F};
    std::copy(std::begin(constant), std::end(constant), std::begin(draw.Blend.ConstantColor));
    draw.ColorWriteMask = 0x0f;
    draw.PerspectiveProjection = true;
    draw.PointShadowReceiver = true;
    draw.IsolatedLighting = true;
    draw.CompositionLayer = static_cast<decltype(draw.CompositionLayer)>(2);
    request.TemporalMotionEnabled = true;
    request.ReflectionProfile = ReflectionMaterialParameters{};
    request.ReflectionProfile->Reflectivity = 0.375F;
    request.ReflectionProfile->Roughness = 0.1F;
    request.SurfaceLightingAvailable = true;
    request.Pbr.TextureSlot = 2;
    request.Pbr.SeamSoftening = true;
    request.ReliefTexture = 1;
    return request;
}

PicaShaderPipelineResult Result() {
    PicaShaderPipelineResult result;
    result.VertexShaderKey = 0x1111;
    result.FragmentShaderKey = 0x2222;
    result.VertexShaderSourceIdentity = ::Oot3d::Renderer::IdentifyPicaShaderSource("vertex variant");
    result.FragmentShaderSourceIdentity = ::Oot3d::Renderer::IdentifyPicaShaderSource("fragment variant");
    result.RequestedFeatures = static_cast<PicaShaderInstrumentationFeature>(0x1f2U);
    result.AppliedFeatures = static_cast<PicaShaderInstrumentationFeature>(0x0f2U);
    return result;
}

void RequestRoundTrip() {
    const auto request = Request();
    const auto witness = WitnessPicaVariantRequest(request, Result());
    const Json encoded = EncodePicaVariantRequest(0xabcdef0123456789ULL, 3, 17, request, witness);
    PicaVariantRequestRecord decoded;
    std::string error;
    Require(DecodePicaVariantRequest(encoded, decoded, &error), "request decodes");
    Require(decoded.CanonicalPipelineId == 0xabcdef0123456789ULL && decoded.EffectsId == 3 &&
                decoded.SettingsRevision == 17,
            "request header");
    Require(decoded.Witness == witness, "witness round trip");
    Require(decoded.Request.ReflectionProfile.has_value() &&
                decoded.Request.ReflectionProfile->Roughness == 0.1F,
            "reflection profile bits");
    Require(EncodePicaVariantRequest(decoded.CanonicalPipelineId, decoded.EffectsId, decoded.SettingsRevision,
                                     decoded.Request, decoded.Witness) == encoded,
            "request re-encodes identically");
    Json broken = encoded;
    broken["vertex_key"] = "not hex";
    Require(!DecodePicaVariantRequest(broken, decoded), "invalid key rejected");
}

void EffectsRoundTrip() {
    // Native surface lighting is an effective value (capability fallback, F2
    // override); settings validation would rewrite it to NativePBR.
    EffectsSettings effects;
    effects.SurfaceLighting = SurfaceLightingMode::Native;
    effects.AmbientOcclusion = AmbientOcclusionMode::Off;
    effects.AoRadius = 3.5F;
    const Json encoded = EncodePicaVariantEffects(effects);
    EffectsSettings decoded;
    std::string error;
    Require(DecodePicaVariantEffects(encoded, decoded, &error), "effects decode exactly");
    Require(decoded.SurfaceLighting == SurfaceLightingMode::Native, "native surface lighting kept");
    GraphicsSettings settings;
    settings.Effects = decoded;
    Require(SerializeGraphicsSettings(settings).at("Effects") == encoded.at("Effects"),
            "effects re-encode identically");
    Json unknown = encoded;
    unknown["Effects"]["SurfaceLighting"] = "FutureModel";
    Require(!DecodePicaVariantEffects(unknown, decoded, &error), "unknown effects value rejected");
}

void RecorderDeduplicates(const std::filesystem::path& directory) {
    const auto path = directory / "variant-requests.jsonl";
    {
        PicaVariantRequestRecorder recorder;
        Require(recorder.Configure(path), "recorder configured");
        const auto request = Request();
        const auto result = Result();
        EffectsSettings effects;
        effects.SurfaceLighting = SurfaceLightingMode::Native;
        recorder.Observe(1, request, effects, {}, 1, result);
        recorder.Observe(1, request, effects, {}, 1, result);
        recorder.Observe(2, request, effects, {}, 1, result);
        recorder.Observe(2, request, effects, {}, 2, result);
        Require(recorder.RequestCount() == 3, "distinct requests counted once");
    }
    PicaVariantRequestSession session;
    std::string error;
    Require(LoadPicaVariantRequests(path, session, &error), "session loads");
    Require(session.Requests.size() == 3 && session.Effects.size() == 1, "session contents");
    Require(session.Effects.begin()->second.SurfaceLighting == SurfaceLightingMode::Native, "session effects");
    Require(session.Requests.back().SettingsRevision == 2, "revision recorded");
}

Oot3dNativeGame::Oot3dPicaRawDraw RawDraw(uint64_t id) {
    Oot3dNativeGame::Oot3dPicaRawDraw draw;
    draw.CanonicalPipelineId = id;
    auto& packet = draw.Packet;
    packet.CommandListAddress = 0x20001234;
    packet.CommandListOffsetWords = 17;
    packet.Indexed = true;
    packet.Composition.SourcePc = 0x002E2CD0;
    packet.Composition.IsolatedLighting = true;
    for (size_t i = 0; i < packet.Registers.size(); ++i) packet.Registers[i] = static_cast<uint32_t>(i * 2654435761U);
    packet.VertexShader.ProgramWordCount = 3;
    packet.VertexShader.Program[0] = 0x4e000000;
    packet.VertexShader.Program[2] = 0x88000000;
    packet.VertexShader.SwizzleWordCount = 1;
    packet.VertexShader.Swizzles[0] = 0x1b;
    packet.VertexShader.FloatUniforms[5] = {1.0F, -2.5F, 0.0F, -0.0F};
    packet.VertexShader.BooleanUniforms[3] = true;
    packet.FogLut[9] = 0xdeadbeef;
    packet.ProcTexLuts.ColorDifference[255] = 7;
    auto luts = std::make_shared<Oot3dNativeGame::Oot3dPicaLightingLutState>();
    luts->Entry(23, 255) = 0x12345;
    luts->ContentHash = 99;
    luts->ContentHashAvailable = true;
    packet.LightingLuts = luts;
    packet.DefaultAttributes[15] = {0.5F, 0.25F, 0.125F, 1.0F};
    draw.Witness.VertexShaderKey = 5;
    draw.Witness.FragmentSourceSize = 4096;
    return draw;
}

void RawDrawRoundTrip(const std::filesystem::path& directory) {
    std::map<std::string, std::string> resources;
    const auto resource = [&resources](std::string_view kind, std::string payload) {
        const std::string id = std::string(kind) + std::to_string(resources.size());
        resources.emplace(id, std::move(payload));
        return id;
    };
    const auto draw = RawDraw(42);
    const Json encoded = Oot3dNativeGame::EncodeOot3dPicaRawDraw(draw, resource);
    Oot3dNativeGame::Oot3dPicaRawDraw decoded;
    std::string error;
    Require(Oot3dNativeGame::DecodeOot3dPicaRawDraw(encoded, resources, decoded, &error), "raw draw decodes");
    Require(decoded.Witness == draw.Witness && decoded.Packet.Registers == draw.Packet.Registers &&
                decoded.Packet.LightingLuts != nullptr &&
                decoded.Packet.LightingLuts->PackedEntries == draw.Packet.LightingLuts->PackedEntries,
            "raw draw fields");
    std::map<std::string, std::string> second;
    const Json reencoded = Oot3dNativeGame::EncodeOot3dPicaRawDraw(
        decoded, [&second](std::string_view kind, std::string payload) {
            const std::string id = std::string(kind) + std::to_string(second.size());
            second.emplace(id, std::move(payload));
            return id;
        });
    Require(reencoded == encoded && second == resources, "raw draw re-encodes identically");

    const auto path = directory / "raw-draws.jsonl";
    {
        Oot3dNativeGame::Oot3dPicaRawDrawRecorder recorder;
        Require(recorder.Configure(path, true, false), "raw recorder configured");
        recorder.Observe(42, draw.Packet, draw.Witness);
        recorder.Observe(42, draw.Packet, draw.Witness);
        recorder.Observe(43, RawDraw(43).Packet, draw.Witness);
        Require(recorder.DrawCount() == 2, "one record per canonical pipeline");
    }
    Oot3dNativeGame::Oot3dPicaRawDrawSession session;
    Require(Oot3dNativeGame::LoadOot3dPicaRawDraws(path, session, &error), "raw session loads");
    Require(session.ParametricTev && !session.UberFragments && session.Draws.size() == 2, "raw session header");
    Require(session.Draws[0].Packet.VertexShader.Program[2] == 0x88000000, "program resource restored");
}
} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() / "oot3d_variant_record_tests";
    try {
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        RequestRoundTrip();
        EffectsRoundTrip();
        RecorderDeduplicates(directory);
        RawDrawRoundTrip(directory);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    std::filesystem::remove_all(directory);
    std::cout << "oot3d_native_pica_variant_record_tests: ok\n";
    return 0;
}
