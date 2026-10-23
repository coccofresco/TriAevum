#include "fast/renderer3ds/pica_pbr_shading.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "vulkan_compute_test.h"

#include <cmath>
#include <iostream>
#include <regex>
#include <vector>

using namespace Fast::Renderer3ds;
using V4 = std::array<float, 4>;

namespace {
struct Case {
    PicaSurfaceLightingUniforms Lighting;
    PicaPbrUniforms Material = BuildPicaPbrUniforms({}, 0, {});
    V4 Primary{0.8F, 0.8F, 0.8F, 0.37F}, Tint{1, 1, 1, 1};
    V4 Texel{168.0F / 204, 190.0F / 204, 33.0F / 204, 1};
    V4 Normal{0, 1, 0, 0}, View{0, 1, 0, 0}, Visibility{1, 0, 0, 0};
};
struct Result { V4 Color, Aux; };

void Near(float actual, float expected, const std::string& name, float tolerance = 2.0e-5F) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
        throw std::runtime_error(name + ": got " + std::to_string(actual) + " expected " + std::to_string(expected));
}
float Decode(float c) { return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F); }
float Encode(float c) { return c <= 0.0031308F ? c * 12.92F : 1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F; }

// Actual surface-uniform builder: bool/ambient-only ownership, the material's
// native ambient and the scene's day/night source all enter at this boundary.
Case MakeCase(bool profile, bool preserve, bool ambientOnly, bool night = false, bool sky = true,
              float keyIntensity = 1.0F, float nativeAmbient = 0.0F, float authoredAmbient = 0.4F,
              bool directlyLit = false, bool nonzeroDiffuseCoefficient = false) {
    PicaVertexShaderHookLayout hooks;
    hooks.Transform = {PicaVertexTransformOperation::ModelViewProjection3x4, 0, 4, 4, 3, 20, 3, 0, 1};
    auto& s = hooks.Surface;
    s.ViewNormalTemporary = 14;
    s.LightingBooleanUniform = 9;
    s.DiffuseUniform = 8; s.AmbientUniform = 9;
    s.TintBooleanUniform = 5; s.TintInputRegister = 2; s.TintScale = {90, 2};
    s.LightEnabledValue = {93, 1}; s.LightDirectionUniforms = {80, 83, 86};
    std::array<V4, 96> native{};
    native[8] = ambientOnly && !nonzeroDiffuseCoefficient ? V4{} : V4{1, 1, 1, 1};
    native[9] = {1, 1, 1, 1};
    native[80] = {0, -1, 0, 1};
    if (directlyLit) native[81] = {0.8F, 0.8F, 0.8F, 0};
    const float level = night ? 0.25F : 0.8F;
    native[82] = {level, level, level, 0};
    native[93][1] = 1;
    PicaNativeLightBinding binding;
    auto scene = std::make_shared<PicaSceneDirectionalLights>();
    scene->Count = 2;
    scene->Lights[0] = {42, {0.6F, 0.8F, 0}, {0.8F, 0.8F, 0.8F}, sky};
    scene->Lights[1] = {43, {-0.6F, -0.8F, 0}, {0.12F, 0.16F, 0.24F}, sky};
    if (night) {
        scene->Lights[0].DirectionTowardSource = {-0.6F, -0.8F, 0};
        scene->Lights[1].DirectionTowardSource = {0.6F, 0.8F, 0};
    }
    binding.SceneDirectionals = scene;
    PicaEnvironmentLighting environment;
    environment.Enabled = profile;
    environment.PreserveSceneBrightness = preserve;
    environment.NativeAmbientStrength = nativeAmbient;
    environment.AmbientIntensity = authoredAmbient;
    environment.KeyDirection = PicaEnvironmentKeyDirection::SceneMain;
    environment.Lights[0].Intensity = keyIntensity;
    const auto view = BuildPicaSurfaceLightingView({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    const auto uniforms = BuildPicaSurfaceLightingUniforms(hooks, ambientOnly || directlyLit ? 512U : 0U,
        {reinterpret_cast<const uint8_t*>(native.data()), sizeof(native)}, binding, environment, &*view);
    Require(uniforms.has_value(), "surface fixture rejected");
    Require((uniforms->BakedReferenceUp[3] != 0) == (sky && !directlyLit), "sky/indoor/actor ownership is wrong");
    Case c;
    c.Lighting = *uniforms;
    c.Primary = {level, level, level, 0.37F};
    if (night) c.Texel = {42.0F / 63.75F, 56.0F / 63.75F, 18.0F / 63.75F, 1};
    return c;
}

std::string Storage(std::string source) {
    source = std::regex_replace(source, std::regex(R"(layout\([^)]*\)\s*)"), "");
    source = std::regex_replace(source, std::regex(R"(\bin\s+)"), "");
    source = std::regex_replace(source, std::regex(R"(uniform sampler2D (\w+);)"), "int $1;");
    return std::regex_replace(source, std::regex(R"(\buniform\b)"), "struct");
}

// Execute complete production shader bodies with fixed texture/derivative
// inputs in compute. Arithmetic is unmodified; no ROM, game, AO or exposure.
std::vector<Result> Evaluate(const std::vector<Case>& cases, int path, bool nativeFragment = false) {
    auto pbr = BuildPicaPbrUberShader({"true", "0u", "false", nativeFragment ? "true" : "false", "true", "false"},
                                     true, false);
    if (path == 1) {
        PicaShaderHookLayout hooks;
        hooks.SampledTextureMask = 1;
        hooks.TextureSamples[0] = {0, PicaTextureCoordinateOperation::NativeVFlip};
        PicaPbrShaderOptions options;
        options.TextureSlot = 0;
        options.NativeFragmentLighting = nativeFragment;
        const auto specialized = BuildPicaPbrShader(hooks, options);
        Require(specialized.has_value(), "specialized PBR fixture rejected");
        pbr.Declarations = specialized->Declarations;
        pbr.MainDeclarations.clear();
        pbr.UnlitInputs = specialized->UnlitInputs;
        pbr.ShadingBody = specialized->ShadingBody;
    }
    std::string shader = R"glsl(#version 450
layout(local_size_x=1) in;
#define dFdx(v) ((v)*0.0)
#define dFdy(v) ((v)*0.0)
#define texture(s,uv) vec4(1.0)
#define textureGrad(s,uv,dx,dy) vec4(1.0)
vec3 pica_view;
vec4 test_visibility;
vec2 pica_texcoord0 = vec2(0), pica_texcoord1 = vec2(0), pica_texcoord2 = vec2(0);
float pica_texcoord0_w = 1.0;
vec4 primary_fragment_color = vec4(1), secondary_fragment_color = vec4(0), pica_shadow_factor = vec4(1);
struct NativeLightUniforms { vec4 lighting_position[8]; vec4 lighting_diffuse[8]; vec4 lighting_ambient[8];
    vec4 lighting_attenuation[8]; vec3 lighting_spot_direction[8]; vec4 lighting_global_ambient; };
NativeLightUniforms fragment_uniforms;
float pica_lighting_lut_unsigned(int table, float value) { return value; }
float pica_lighting_lut_signed(int table, float value) { return value; }
// Inputs represent already rounded native primary colours. Keep the following
// evaluation unquantized to measure calibration independently of 8-bit steps.
vec3 byteround3(vec3 v) { return v; }
)glsl";
    shader += Storage(std::string(PicaSurfaceLightingDeclarations()));
    shader += Storage(pbr.Declarations);
    shader += R"glsl(
struct Case { PicaSurfaceLighting lighting; PicaPbrMaterial material;
    vec4 primary; vec4 tint; vec4 texel; vec4 normal; vec4 view; vec4 visibility; };
struct Result { vec4 color; vec4 aux; };
layout(set=0,binding=0,std430) readonly buffer Cases { Case cases[]; } inputs;
layout(set=0,binding=1,std430) writeonly buffer Results { Result values[]; } outputs;
vec3 pica_surface_shadow(int slot) { return vec3(slot < 0 ? 1.0 : test_visibility.x); }
float pica_point_shadow(float slot, vec3 delta, float radius) { return 1.0; }
void main() {
    uint i = gl_GlobalInvocationID.x;
    pica_surface = inputs.cases[i].lighting;
    pbr_material = inputs.cases[i].material;
    pica_surface_normal = inputs.cases[i].normal.xyz;
    pica_surface_tint = inputs.cases[i].tint;
    pica_view = inputs.cases[i].view.xyz;
    test_visibility = inputs.cases[i].visibility;
    vec4 rounded_primary_color = inputs.cases[i].primary;
    vec4 combiner_output;
    vec4 aux = vec4(0);
    fragment_uniforms.lighting_global_ambient = vec4(0.3,0.3,0.3,0);
)glsl";
    if (path == 0) {
        shader += "rounded_primary_color.rgb = pica_surface_primary(rounded_primary_color.rgb, pica_surface_normal);\n";
    } else {
        shader += pbr.MainDeclarations + pbr.UnlitInputs;
    }
    shader += "combiner_output = rounded_primary_color * inputs.cases[i].texel;\n";
    if (path != 0) {
        shader += pbr.ShadingBody;
        shader += "aux = vec4(pbr_ao_response, length(pbr_gi_radiance));\n";
    }
    shader += "outputs.values[i].color = combiner_output; outputs.values[i].aux = aux;\n}\n";
    std::vector<Result> result(cases.size());
    RunComputeTest(shader, std::as_bytes(std::span(cases)), std::as_writable_bytes(std::span(result)), uint32_t(cases.size()));
    return result;
}
} // namespace

int main() try {
    std::vector<Case> cases;
    enum Check { Identity, Darker, Half, Black, Legacy, Additive, NotBrighter };
    std::vector<Check> checks;
    auto add = [&](Case c, Check check) { cases.push_back(c); checks.push_back(check); };
    for (bool night : {false, true})
        for (bool ambientOnly : {false, true})
            for (bool profile : {false, true})
                for (bool preserve : {false, true}) {
                    auto c = MakeCase(profile, preserve, ambientOnly, night);
                    add(c, Identity);
                    c.Visibility[0] = 0; add(c, Darker);
                    c.Visibility[0] = 1; c.Normal = {0, 0, 1, 0}; add(c, Darker);
                }
    for (float intensity : {0.0F, 0.5F, 1.0F, 2.0F}) {
        auto c = MakeCase(true, false, false, false, true, intensity, 0, 0);
        add(c, intensity == 0 ? Black : intensity < 1 ? Half : intensity == 1 ? Identity : Additive);
    }
    // A diffuse-lit actor is not a baked room even under the same sky, even
    // when the enabled profile subsequently suppresses its native lights.
    MakeCase(true, false, false, false, true, 1, 0.4F, 0.4F, true);
    MakeCase(false, false, false, false, true, 1, 0.4F, 0.4F, true);
    // Nonzero material diffuse alone does not make a directly lit draw. The
    // active native slot has ambient but zero diffuse; it owns baked colour.
    for (bool night : {false, true})
        for (bool profile : {false, true})
            for (bool preserve : {false, true}) {
                auto c = MakeCase(profile, preserve, true, night, true, 1, 0, 0.4F, false, true);
                add(c, Identity);
                c.Visibility[0] = 0; add(c, Darker);
            }
    auto black = MakeCase(true, false, false); black.Primary[2] = 0; add(black, Identity);
    auto c = MakeCase(true, true, false);
    c.Lighting.EnvironmentDirections[1] = {0, 1, 0, 0.3F};
    c.Lighting.EnvironmentColors[1] = {0.2F, 0.3F, 0.4F, 0}; add(c, Identity);
    c = MakeCase(false, false, false);
    c.Material.OrmMinimumRoughness[1] = 0.2F;
    c.Material.OrmMinimumRoughness[2] = 0.7F; add(c, Identity); // Same BRDF in the reference.
    c = MakeCase(true, false, true);
    c.Lighting.EnvironmentParameters[0] = 2; add(c, Identity); // Native local gain cannot scale baked colour.
    // Static interior: preserve=false, native ambient 0.4, no added key or
    // ambient. This boundary deliberately keeps its previous absolute units.
    add(MakeCase(true, false, false, false, false, 0, 0.4F, 0), Legacy);
    add(MakeCase(true, false, true, false, false, 0, 0.4F, 0), Legacy);
    c = MakeCase(true, false, true);
    c.Lighting.BakedReferenceUp = {}; // Native/isolated/fragment-lit admission clears it.
    add(c, Legacy);
    // Camera covariance: rotate the entire view basis, including the reference.
    c = MakeCase(true, false, true);
    c.Normal = c.View = {1, 0, 0, 0}; c.Lighting.BakedReferenceUp = {1, 0, 0, 1};
    c.Lighting.EnvironmentDirections[0] = {0.8F, -0.6F, 0, 1}; add(c, Identity);
    // Scene-following offsets and near-horizon keys have no exposure gain.
    for (float elevation : {0.0174524F, 0.35F, 1.0F}) {
        c = MakeCase(true, false, false);
        c.Lighting.EnvironmentDirections[0] = {std::sqrt(1 - elevation * elevation), elevation, 0, 1};
        add(c, Identity);
        c.Normal = {1, 0, 0, 0}; add(c, NotBrighter);
    }
    c = MakeCase(true, false, false);
    for (int k = 0; k < 3; ++k) c.Lighting.EnvironmentColors[0][k] *= 0.5F;
    add(c, Darker); // Key tint must not cancel out of the reference.
    // A local source remains additive and is absent when disabled.
    c = MakeCase(true, false, false);
    c.Lighting.Control[1] = 1;
    c.Lighting.ScenePoints[0].PositionRadius = {0, 3, 0, 10};
    c.Lighting.ScenePoints[0].Color = {0.1F, 0.1F, 0.1F, -1}; add(c, Additive);
    c.Lighting.Control[1] = 0; add(c, Identity);
    // Zero light channel / grazing reference: finite, no invented white gain.
    c = MakeCase(true, false, false, false, true, 1, 0, 0);
    c.Lighting.EnvironmentColors[0][2] = 0; c.Primary[2] = 0; add(c, Identity);
    c.Lighting.EnvironmentDirections[0] = {1, 0, 0, 1}; add(c, Black);
    // All optional controls carry junk while disabled; no extra radiance/GI.
    c = MakeCase(true, false, false);
    c.Lighting.RimColor = {1, 0, 0, 1}; c.Lighting.RimDirection = {1, 0, 0, 0};
    c.Lighting.Placed[0].Color = {8, 8, 8, 0}; add(c, Identity);

    std::array<std::vector<Result>, 3> results;
    for (int path = 0; path < 3; ++path) {
        results[path] = Evaluate(cases, path);
        for (size_t i = 0; i < cases.size(); ++i) {
            const auto& c = cases[i]; const auto& out = results[path][i];
            const auto name = "path " + std::to_string(path) + " case " + std::to_string(i);
            for (size_t k = 0; k < 3; ++k) {
                const float native = c.Primary[k] * c.Texel[k];
                const float value = out.Color[k];
                Require(std::isfinite(value), "nonfinite output");
                if (checks[i] == Identity) Near(value, native, name);
                if (checks[i] == Half) Near(value, 0.5F * native, name);
                if (checks[i] == Black) Near(value, 0, name);
                if (checks[i] == NotBrighter) Require(value <= native + 2.0e-5F, "grazing key amplified a wall");
                if (checks[i] == Darker) Require(value < native - 1.0e-4F, "wall/shadow/lower intensity failed to darken");
                if (checks[i] == Additive) Require(value > native + 1.0e-4F, "local light or higher intensity is inert");
                if (checks[i] == Legacy && c.Lighting.EnvironmentDirections[0][3] == 0) {
                    const float ambient = c.Lighting.Control[0] == 0 ? 0.4F : 0.32F;
                    const float expected = path == 0 ? c.Texel[k] * c.Lighting.ResolvedAmbient[k]
                        : Encode(Decode(c.Texel[k]) * ambient);
                    Near(value, expected, name);
                }
                if (path == 2) Near(value, results[1][i].Color[k], "specialized/uber agreement");
            }
            Near(out.Color[3], c.Primary[3], "native alpha");
            if (path != 0) {
                Near(out.Aux[3], 0, "disabled GI must publish zero");
                for (int k = 0; k < 3; ++k)
                    Require(out.Aux[k] >= 0 && out.Aux[k] <= 1, "invalid AO ownership");
            }
        }
        std::cout << "path " << path << " ground RGB: ";
        for (int k = 0; k < 3; ++k) std::cout << results[path][0].Color[k] * 255 << ' ';
        std::cout << '\n';
    }
    // Fragment-lit PBR with zero active lights still owns its native global
    // ambient; it must not become a baked surface merely because the count is 0.
    c = MakeCase(true, false, true, false, false, 0, 0.4F, 0);
    for (int path : {1, 2}) {
        const auto native = Evaluate({c}, path, true)[0];
        for (int k = 0; k < 3; ++k)
            Near(native.Color[k], Encode(Decode(c.Texel[k]) * 0.12F), "fragment ambient unchanged");
    }
    std::cout << "Baked lighting: " << cases.size() << " cases x 3 paths + 2 native-fragment cases passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "Baked lighting FAILED: " << e.what() << '\n';
    return 1;
}
