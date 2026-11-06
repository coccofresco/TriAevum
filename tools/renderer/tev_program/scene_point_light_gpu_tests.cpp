#include "fast/renderer3ds/pica_surface_lighting.h"
#include "fast/renderer3ds/pica_native_point_replacement.h"
#include "fast/renderer3ds/pica_surface_lighting_shader.h"
#include "vulkan_compute_test.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <regex>
#include <vector>

int main() {
    try {
        using namespace Fast::Renderer3ds;
        using Vec4 = std::array<float, 4>;
        PicaVertexShaderHookLayout hooks;
        hooks.Transform = { PicaVertexTransformOperation::ModelViewProjection3x4, 0, 4, 4, 3, 20, 3, 0, 1 };
        hooks.Skeleton = { PicaVertexSkeletonOperation::MatrixPalette3x4, 2, 3, 20, 3, 6, 7, 4 };
        auto& surface = hooks.Surface;
        surface.ViewNormalTemporary = 14;
        surface.LightingBooleanUniform = 9;
        surface.DiffuseUniform = 8;
        surface.AmbientUniform = 9;
        surface.TintBooleanUniform = 5;
        surface.TintInputRegister = 2;
        surface.TintScale = { 90, 2 };
        surface.LightEnabledValue = { 93, 1 };
        surface.LightDirectionUniforms = { 80, 83, 86 };
        surface.BoneIndexScale = { 93, 3 };
        surface.BoneWeightScale = { 94, 3 };
        surface.SkinnedNormalScale = { 90, 1 };
        surface.InfluenceCount = { 92, 3 };
        surface.AdditionalInfluenceThresholds = { PicaVertexUniformComponent{ 95, 0 },
                                                  PicaVertexUniformComponent{ 95, 1 } };
        std::array<Vec4, 96> native{};
        native[8] = native[9] = { .5F, .5F, .5F, 1 };
        native[80] = { 0, -1, 0, 1 };
        native[81] = { .8F, .8F, .8F, 0 };
        native[82] = { .4F, .4F, .4F, 0 };
        native[93][1] = 1;
        struct Case {
            PicaSurfaceLightingUniforms Lighting;
            Vec4 View{}, Normal{ 0, 1, 0, 0 }, Base{ .2F, .3F, .4F, 1 };
        };
        std::array<Case, 24> cases{};
        const std::array<Vec4, 24> expected{ { { .575F, .2F, .2F, 1 },
                                               { .2F, .2F, .2F, 1 },
                                               { .2F, .2F, .2F, 1 },
                                               { .2F, .3F, .4F, 1 },
                                               { .575F, .3F, .4F, 1 },
                                               { .575F, .2F, .2F, 1 },
                                               { .66875F, .2F, .2F, 1 },
                                               { .3F, 0, 0, 1 },
                                               { .3F, 0, 0, 1 },
                                               { 0, 0, .4F, 1 },
                                               { 0, .15F, 0, 1 },
                                               { .3F, 0, 0, 1 },
                                               { .2F, .2F, .2F, 1 },
                                               { .5F, .3F, .4F, 1 },
                                               { 0, 0, 0, 1 },
                                               { .5F, .2F, .2F, 1 },
                                               { .2F, .2F, .6F, 1 },
                                               { .2F, .2F, .2F, 1 },
                                               { .2F, .3F, .4F, 1 },
                                               { .2F, 0, .2F, 1 },
                                               { .4F, .1F, .2F, 1 },
                                               { .2F, .2F, .2F, 1 },
                                               { .7F, .7F, .7F, 1 },
                                               { .7F, .8F, .9F, 1 } } };
        for (size_t i = 0; i < cases.size(); ++i) {
            PicaNativeLightBinding binding;
            auto points = std::make_shared<PicaScenePointLights>();
            points->Count = 1;
            points->Lights[0] = { 42, { 0, 10, 0 }, { 1, 0, 0 }, 20, 0 };
            auto& selected = binding.Sources[0];
            selected.Kind = PicaNativeLightSource::Type::Point;
            selected.SourceIdentity = 42;
            selected.PositionWorld = { 0, 10, 0 };
            selected.Radius = 20;
            selected.UnattenuatedColor = selected.ReferenceColor = { 1, 1, 1 };
            binding.ScenePoints = points;
            PicaEnvironmentLighting settings;
            settings.PreserveSceneBrightness = false; // Existing probes explicitly exercise authored-only lighting.
            std::array<float, 16> camera{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            if (i == 1)
                settings.PointLights[0].Enabled = false;
            if (i == 2)
                points->Lights[0].Radius = 0;
            if (i == 3)
                cases[i].View[1] = 20; // A natively unlit pixel outside the sphere.
            if (i == 5)
                camera[5] = 2; // Native world radius must not become a view-space radius.
            if (i == 6) {
                points->Lights[0].Group = 1;
                settings.PointLights[0].Enabled = false;
                settings.PointLights[1].Intensity = 2;
                settings.PointLights[1].RadiusScale = 2;
                settings.PointLights[1].ColorTint = { .5F, 1, 1 };
            }
            auto uniforms = native;
            if (i >= 7) {
                binding = {};
                auto directions = std::make_shared<PicaSceneDirectionalLights>();
                directions->Count = 2;
                directions->Lights[0] = {51, {0,1,0}, {.6F,0,0}};
                directions->Lights[1] = {52, {0,-1,0}, {0,0,.8F}};
                binding.SceneDirectionals = directions;
                settings.Enabled = i != 13;
                settings.AmbientIntensity = 0;
                settings.NativeDirectStrength = i == 10 || i == 12 ? 0.0F : 1.0F;
                settings.NativeLocalStrength = i == 8 || i == 10 ? 1.0F : 0.0F;
                settings.NativeAmbientStrength = i == 12 ? 1.0F : 0.0F;
                if (i != 7 && i != 13) {
                    binding.Sources[0].Kind = PicaNativeLightSource::Type::Directional;
                    binding.Sources[0].SourceIdentity = 51;
                }
                if (i == 9) cases[i].Normal[1] = -1;
                if (i == 10 || i == 11) {
                    auto& local = binding.Sources[1];
                    local.Kind = PicaNativeLightSource::Type::Point;
                    local.SourceIdentity = 73;
                    local.PositionWorld = {0,10,0};
                    local.UnattenuatedColor = local.ReferenceColor = {0,.4F,0};
                    local.Radius = 20;
                    uniforms[83] = {0,-1,0,1};
                    uniforms[84] = {0,.4F,0,0};
                }
                if (i == 14) {
                    // Native lighting is disabled, but unused slot uniforms
                    // may still contain the preceding draw's values.
                    binding.Sources = {};
                    settings.NativeDirectStrength = 0;
                    settings.NativeLocalStrength = 1;
                }
            }
            if (i >= 15 && i < 17) {
                // Baked world materials have no native diffuse response or
                // selected direct lights. Scene lights must still respond to
                // the material normal without discarding native ambient.
                binding.Sources = {};
                settings = {};
                uniforms[8] = {0, 0, 0, 1};
                uniforms[81] = {};
                if (i == 16) cases[i].Normal[1] = -1;
            }
            if (i >= 17) {
                binding = {};
                settings = {};
                settings.Enabled = true;
                settings.AmbientIntensity = 0;
                uniforms = native;
                if (i == 19) uniforms[9][1] = 0; // No invented brightness in an authored dark channel.
                if (i == 20) uniforms[82] = {.8F, .2F, .4F, 0}; // A new native environment snapshot.
                if (i == 21) {
                    binding.ScenePoints = points;
                    binding.Sources[0].Kind = PicaNativeLightSource::Type::Point;
                    binding.Sources[0].SourceIdentity = 42;
                    settings.PointLights[0].Enabled = false;
                }
                if (i == 22 || i == 23) settings.Lights[0].Intensity = 1;
            }
            const auto view = BuildPicaSurfaceLightingView(camera);
            if (!view)
                throw std::runtime_error("Scene point-light view rejected");
            const auto lighting = BuildPicaSurfaceLightingUniforms(
                hooks, i == 3 || i == 4 || i == 7 || i == 13 || i == 14 || i == 18 || i == 23 ? 0U : 1U << 9U,
                { reinterpret_cast<const uint8_t*>(uniforms.data()), sizeof(uniforms) }, binding, settings,
                &*view);
            if (!lighting)
                throw std::runtime_error("Scene point-light surface rejected");
            cases[i].Lighting = *lighting;
        }
        // Change stage/storage declarations only; execute the production lighting
        // arithmetic verbatim against CPU-built native binding snapshots.
        auto declarations =
            std::regex_replace(std::string(PicaSurfaceLightingDeclarations()), std::regex(R"(layout\([^)]*\)\s*)"), "");
        declarations = std::regex_replace(declarations, std::regex(R"(\bin\s+)"), "");
        declarations = std::regex_replace(declarations, std::regex(R"(\buniform\b)"), "struct");
        std::string shader = "#version 450\nlayout(local_size_x=1) in;\nvec3 pica_view;\n" + declarations;
        shader += R"glsl(
struct Case { PicaSurfaceLighting lighting; vec4 view; vec4 normal; vec4 base; };
layout(set=0,binding=0,std430) readonly buffer Cases { Case cases[]; } inputs;
layout(set=0,binding=1,std430) writeonly buffer Results { vec4 values[]; } outputs;
vec3 pica_surface_shadow(int slot) { return vec3(1.0); }
float pica_point_shadow(float slot, vec3 light_to_fragment, float radius) { return 1.0; }
void main() {
    uint index = gl_GlobalInvocationID.x;
    pica_surface = inputs.cases[index].lighting;
    pica_view = inputs.cases[index].view.xyz;
    pica_surface_normal = inputs.cases[index].normal.xyz;
    pica_surface_tint = vec4(1.0);
    outputs.values[index] = vec4(pica_surface_primary(inputs.cases[index].base.rgb, pica_surface_normal), 1.0);
}
)glsl";
        std::array<Vec4, cases.size()> output{};
        RunComputeTest(shader, std::as_bytes(std::span(cases)), std::as_writable_bytes(std::span(output)),
                       cases.size());
        unsigned failures = 0;
        for (size_t i = 0; i < cases.size(); ++i)
            for (size_t c = 0; c < 4; ++c)
                if (!std::isfinite(output[i][c]) || std::abs(output[i][c] - expected[i][c]) > 2e-5F) {
                    std::cerr << "case=" << i << " channel=" << c << " actual=" << output[i][c]
                              << " expected=" << expected[i][c] << '\n';
                    ++failures;
                }
        std::cout << "Scene lighting: " << cases.size() << " GPU cases, " << failures << " failures\n";
        // Placed lights over the fragment at the origin (normal +Y): native
        // ambient 0.2, material diffuse 0.5, red light; each kind's term.
        {
            const auto window = [](float distance, float reach) {
                const float ratio = distance / reach;
                const float value = std::clamp(1.0F - ratio * ratio * ratio * ratio, 0.0F, 1.0F);
                return value * value;
            };
            constexpr float kPi = 3.14159265F;
            struct Placed { PicaPlacedLight Light; float Red; const char* Name; };
            const auto light = [&](PicaPlacedLightKind kind, std::array<float, 3> position, std::array<float, 3> axis,
                                   float reach) {
                PicaPlacedLight value;
                value.SourceIdentity = 7;
                value.Kind = kind;
                value.PositionWorld = position;
                value.AxisWorld = axis;
                value.TangentWorld = { 1, 0, 0 };
                value.Color = { 1, 0, 0 };
                value.Reach = reach;
                value.HalfWidth = value.HalfHeight = 50;
                value.CosOuter = std::cos(22.5F * kPi / 180.0F);
                value.CosInner = std::cos(18.0F * kPi / 180.0F);
                return value;
            };
            const float rectangleForm = 10000.0F / (kPi * 10000.0F + 10000.0F) * window(100, 1000);
            const float discArea = kPi * 2500.0F;
            const float discForm = discArea / (kPi * 10000.0F + discArea) * window(100, 1000);
            auto twoSided = light(PicaPlacedLightKind::Rectangle, { 0, 100, 0 }, { 0, 1, 0 }, 1000);
            twoSided.TwoSided = true;
            // Spread 30: the footprint (radius 100 tan 15) lies inside the
            // shape over the fragment and ends 73 units short of the rectangle
            // beside it.
            const auto straightened = [&](PicaPlacedLightKind kind, std::array<float, 3> position) {
                auto value = light(kind, position, { 0, -1, 0 }, 1000);
                value.Spread = 30;
                return value;
            };
            const float besideDistance = std::sqrt(150.0F * 150.0F + 100.0F * 100.0F);
            const float besideForm = 10000.0F * (100.0F / besideDistance) /
                (kPi * (100.0F * 100.0F + 100.0F * 100.0F) + 10000.0F) * window(besideDistance, 1000) *
                (100.0F / std::sqrt(100.0F * 100.0F + 100.0F * 100.0F));
            const std::vector<Placed> placed{
                { light(PicaPlacedLightKind::Omni, { 0, 10, 0 }, { 0, -1, 0 }, 20), .2F + .5F * .75F, "omni" },
                { light(PicaPlacedLightKind::Point, { 0, 100, 0 }, { 0, -1, 0 }, 1000), .2F + .5F * window(100, 1000),
                  "point at the reference distance" },
                { light(PicaPlacedLightKind::Point, { 0, 50, 0 }, { 0, -1, 0 }, 1000), .2F + .5F * 4.0F * window(50, 1000),
                  "point at half the reference distance" },
                { light(PicaPlacedLightKind::Spot, { 0, 100, 0 }, { 0, -1, 0 }, 1000), .2F + .5F * window(100, 1000),
                  "spot on its axis" },
                { light(PicaPlacedLightKind::Spot, { 0, 100, 0 }, { 1, 0, 0 }, 1000), .2F, "spot outside its cone" },
                { light(PicaPlacedLightKind::Rectangle, { 0, 100, 0 }, { 0, -1, 0 }, 1000), .2F + .5F * rectangleForm,
                  "rectangle facing the fragment" },
                { light(PicaPlacedLightKind::Rectangle, { 0, 100, 0 }, { 0, 1, 0 }, 1000), .2F,
                  "one-sided rectangle facing away" },
                { twoSided, .2F + .5F * rectangleForm, "two-sided rectangle facing away" },
                { light(PicaPlacedLightKind::Disc, { 0, 100, 0 }, { 0, -1, 0 }, 1000), .2F + .5F * discForm, "disc" },
                { straightened(PicaPlacedLightKind::Rectangle, { 0, 100, 0 }), .2F + .5F * rectangleForm,
                  "straightened rectangle over the fragment" },
                { straightened(PicaPlacedLightKind::Disc, { 0, 100, 0 }), .2F + .5F * discForm,
                  "straightened disc over the fragment" },
                { straightened(PicaPlacedLightKind::Rectangle, { 150, 100, 0 }), .2F,
                  "straightened rectangle beside the fragment" },
                { light(PicaPlacedLightKind::Rectangle, { 150, 100, 0 }, { 0, -1, 0 }, 1000), .2F + .5F * besideForm,
                  "diffuse rectangle beside the fragment" },
                { light(PicaPlacedLightKind::Sun, { 0, 0, 0 }, { 0, -1, 0 }, 0), .7F, "sun from above" },
                { light(PicaPlacedLightKind::Point, { 0, 2000, 0 }, { 0, -1, 0 }, 1000), .2F, "point beyond its reach" },
            };
            std::vector<Case> placedCases(placed.size());
            const auto view = BuildPicaSurfaceLightingView({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });
            for (size_t i = 0; i < placed.size(); ++i) {
                PicaEnvironmentLighting settings;
                settings.Enabled = true;
                settings.AmbientIntensity = 0;
                settings.PlacedLights[0] = placed[i].Light;
                settings.PlacedLightCount = 1;
                const auto lighting = BuildPicaSurfaceLightingUniforms(
                    hooks, 1U << 9U, { reinterpret_cast<const uint8_t*>(native.data()), sizeof(native) },
                    PicaNativeLightBinding{}, settings, &*view);
                if (!lighting) throw std::runtime_error("Placed-light surface rejected");
                placedCases[i].Lighting = *lighting;
            }
            std::vector<Vec4> placedOutput(placed.size());
            RunComputeTest(shader, std::as_bytes(std::span(placedCases)),
                           std::as_writable_bytes(std::span(placedOutput)), placed.size());
            for (size_t i = 0; i < placed.size(); ++i) {
                const std::array<float, 3> expect{ std::min(placed[i].Red, 1.0F), .2F, .2F };
                for (size_t c = 0; c < 3; ++c)
                    if (!std::isfinite(placedOutput[i][c]) || std::abs(placedOutput[i][c] - expect[c]) > 2e-4F) {
                        std::cerr << "placed case=" << placed[i].Name << " channel=" << c
                                  << " actual=" << placedOutput[i][c] << " expected=" << expect[c] << '\n';
                        ++failures;
                    }
            }
            std::cout << "Placed lights: " << placed.size() << " GPU cases, " << failures << " total failures\n";
        }
        // Execute the coefficient producer and consumer, not a CPU facsimile.
        // These cases distinguish selected sun from points/fill, preserve native
        // unlit appearance, and expose post-clamp shadowing and view-space bugs.
        struct ShadowCase { Case Surface; Vec4 Shadow{0,1,0,0}; };
        std::array<ShadowCase, 10> shadows{};
        const std::array<size_t, 8> selected{0, 7, 10, 13, 15, 16, 22, 23};
        for (size_t i = 0; i < selected.size(); ++i) shadows[i].Surface = cases[selected[i]];
        shadows[8].Surface = cases[22];
        shadows[8].Surface.Lighting.EnvironmentDirections[1] = {0,1,0,1};
        shadows[8].Surface.Lighting.EnvironmentColors[1] = {0,.1F,0,0};
        shadows[9].Surface = cases[22];
        shadows[9].Shadow[3] = .5F;
        shadows[9].Surface.Normal = {1,0,0,0};
        shadows[9].Surface.Lighting.EnvironmentDirections[0] = {1,0,0,2.4F};
        shadows[9].Surface.Lighting.ViewToWorldLinear = {{{0,-1,0,0},{1,0,0,0},{0,0,1,0}}};
        const std::array<Vec4, shadows.size()> shadowExpected{{
            {.575F,.2F,.2F,1}, {0,0,0,1}, {0,.15F,0,1}, {.2F,.3F,.4F,1},
            {.2F,.2F,.2F,1}, {.2F,.2F,.6F,1}, {.2F,.2F,.2F,1}, {.2F,.3F,.4F,1},
            {.2F,.3F,.2F,1}, {.8F,.8F,.8F,1}}};
        auto coefficients = BuildPicaSurfaceLightingCoefficientsShader(true);
        coefficients.erase(0, coefficients.find('\n') + 1); // Compute stage owns #version.
        coefficients = std::regex_replace(coefficients, std::regex(R"(layout\([^)]*\)\s*)"), "");
        coefficients = std::regex_replace(coefficients,
            std::regex(R"((^|\n)(in|out) (vec[234] \w+;))"), "$1$3");
        coefficients = std::regex_replace(coefficients, std::regex(R"(\buniform\b)"), "struct");
        const std::string shadowShader = "#version 450\nlayout(local_size_x=1) in;\n"
            "#define main coefficients_main\n" + coefficients + "\n#undef main\n" +
            std::string(kPicaSurfaceLightingRecomposeShader) + R"glsl(
struct ShadowCase { PicaSurfaceLighting lighting; vec4 view; vec4 normal; vec4 base; vec4 shadow; };
layout(set=0,binding=0,std430) readonly buffer Cases { ShadowCase cases[]; } inputs;
layout(set=0,binding=1,std430) writeonly buffer Results { vec4 values[]; } outputs;
void main() {
    uint index = gl_GlobalInvocationID.x;
    pica_surface = inputs.cases[index].lighting;
    pica_view = inputs.cases[index].view.xyz;
    pica_surface_normal = inputs.cases[index].normal.xyz;
    pica_surface_tint = vec4(1.0);
    pica_primary_color = inputs.cases[index].base;
    surface_atlas.shadow_direction = inputs.cases[index].shadow;
    coefficients_main();
    outputs.values[index] = vec4(pica_surface_recompose(surface_primary, surface_raw.rgb,
        surface_directional.rgb, inputs.cases[index].shadow.w), 1.0);
}
)glsl";
        std::array<Vec4, shadows.size()> shadowOutput{};
        RunComputeTest(shadowShader, std::as_bytes(std::span(shadows)),
            std::as_writable_bytes(std::span(shadowOutput)), shadows.size());
        for (size_t i = 0; i < shadows.size(); ++i)
            for (size_t c = 0; c < 3; ++c)
                if (!std::isfinite(shadowOutput[i][c]) ||
                    std::abs(shadowOutput[i][c] - shadowExpected[i][c]) > 2e-5F) {
                    std::cerr << "shadow case=" << i << " channel=" << c << " actual=" << shadowOutput[i][c]
                              << " expected=" << shadowExpected[i][c] << '\n';
                    ++failures;
                }
        std::cout << "Shadow lighting: " << shadows.size() << " GPU cases, " << failures << " total failures\n";
        // Native-fragment Local replacement: production admission, uniform
        // builder, pre-clamp recomposition and scene-point evaluator together.
        // The shadow stub controls visibility only (atlas sampling has its own
        // tests); it proves the dynamic term, not ambient, reads that value.
        struct NativePointCase { PicaBalancedSurfaceUniforms Lighting; Vec4 Visibility{1,0,0,0}; };
        std::array<NativePointCase, 5> nativePoints{};
        for (size_t i = 0; i < nativePoints.size(); ++i) {
            PicaFragmentLightingLayout layout;
            layout.SchemaVersion = kPicaFragmentLightingLayoutSchemaVersion;
            layout.ActiveLightCount = 1; layout.LightPermutation[0] = 0;
            PicaNativeLightBinding binding;
            auto& source = binding.Sources[0];
            source.Kind = PicaNativeLightSource::Type::Point;
            source.SourceIdentity = 42; source.Origin = PicaNativeLightOrigin::Actor;
            auto points = std::make_shared<PicaScenePointLights>(); points->Count = 1;
            points->Lights[0] = {42,{0,10,0},{1,0,0},20,2};
            points->Lights[0].ReplacesNativeLocal = true;
            binding.ScenePoints = points;
            PicaEnvironmentLighting settings;
            if (i == 4) settings.PointLights[2].Enabled = false;
            const auto replacement = BuildPicaNativePointReplacement(true, layout, binding, settings, {});
            auto& entry = nativePoints[i];
            if (replacement) entry.Lighting.Balance = replacement->Balance;
            else {
                entry.Lighting.Balance.Control = {1,8,0,0};
                entry.Lighting.Balance.Parameters[0] = 1;
                entry.Lighting.Balance.Gains[0] = {1,1,1,0};
            }
            auto localHooks = hooks; localHooks.Surface.VertexLightsReconstructed = false;
            auto uniforms = native;
            if (i == 3) uniforms[8][0] = 0; // authored black material channel
            auto view = BuildPicaSurfaceLightingView({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
            auto lighting = BuildPicaSurfaceLightingUniforms(localHooks, 0,
                {reinterpret_cast<const uint8_t*>(uniforms.data()),sizeof(uniforms)},
                binding, settings, &*view, nullptr, true);
            if (!lighting) throw std::runtime_error("native-point builder rejected fixture");
            entry.Lighting.Surface = *lighting;
            if (replacement) ApplyPicaNativePointReplacementControls(entry.Lighting.Surface, *replacement, settings);
            if (i == 1) entry.Visibility[0] = 0;
            if (i == 2) entry.Visibility[0] = .5F;
        }
        auto nativeDeclarations = std::regex_replace(PicaSurfaceLightingDeclarations(true,true),
            std::regex(R"(layout\([^)]*\)\s*)"), "");
        nativeDeclarations = std::regex_replace(nativeDeclarations, std::regex(R"(\bin\s+)"), "");
        nativeDeclarations = std::regex_replace(nativeDeclarations, std::regex(R"(\buniform\b)"), "struct");
        const auto nativeShader = std::string("#version 450\nlayout(local_size_x=1) in;\nvec3 pica_view;\nvec4 pica_primary_color;\n") +
            R"glsl(
struct FU { vec4 lighting_global_ambient; vec4 lighting_attenuation[8]; };
FU fragment_uniforms;
float pica_lighting_lut_unsigned(int table,float x){return 1.0-x;}
)glsl" + nativeDeclarations + R"glsl(
struct NativePointCase { PicaSurfaceLighting lighting; vec4 visibility; };
layout(set=0,binding=0,std430) readonly buffer Cases { NativePointCase cases[]; } inputs;
layout(set=0,binding=1,std430) writeonly buffer Results { vec4 values[]; } outputs;
float point_visibility;
vec3 pica_surface_shadow(int slot) { return vec3(1); }
float pica_point_shadow(float slot, vec3 to_fragment, float radius) { return point_visibility; }
void main() {
    uint index=gl_GlobalInvocationID.x;
    pica_surface=inputs.cases[index].lighting;
    point_visibility=inputs.cases[index].visibility.x;
    pica_view=vec3(0); pica_surface_normal=vec3(0,1,0);
    bool surface_water=false;
    vec4 primary_fragment_color=vec4(1,.3,.3,.37);
    vec4 secondary_fragment_color=vec4(.4,0,0,.63);
    struct LP { uvec4 control; uvec4 lights[8]; };
    LP lp; lp.control=uvec4(1,0,0,0); lp.lights[0]=uvec4(0);
    fragment_uniforms.lighting_global_ambient=vec4(.1,.1,.1,0);
)glsl" + std::string(kPicaFragmentBalanceCaptureBegin) + R"glsl(
    balance_diffuse[0]=vec3(index==3u?0.0:.8,0,0);
    balance_ambient[0]=vec3(.2);
    balance_combined[0]=balance_diffuse[0]+balance_ambient[0];
    balance_specular[0]=vec3(.4,0,0);
)glsl" + std::string(kPicaFragmentBalanceBody) + R"glsl(
    outputs.values[index]=vec4(primary_fragment_color.rgb,primary_fragment_color.a);
}
)glsl";
        std::array<Vec4, nativePoints.size()> nativeOutput{};
        RunComputeTest(nativeShader, std::as_bytes(std::span(nativePoints)),
            std::as_writable_bytes(std::span(nativeOutput)), nativePoints.size());
        const std::array<float, nativePoints.size()> nativeExpected{1.F,.3F,.7F,.3F,1};
        for (size_t i=0;i<nativePoints.size();++i) {
            const Vec4 expectedNative{nativeExpected[i],.3F,.3F,.37F};
            for (size_t c=0;c<4;++c)
                if (!std::isfinite(nativeOutput[i][c]) || std::abs(nativeOutput[i][c]-expectedNative[c])>2e-5F) {
                    std::cerr << "native-point case=" << i << " channel=" << c << " actual=" << nativeOutput[i][c]
                              << " expected=" << expectedNative[c] << '\n'; ++failures;
                }
        }
        std::cout << "Native fragment points: " << nativePoints.size() << " GPU cases, " << failures << " total failures\n";
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
