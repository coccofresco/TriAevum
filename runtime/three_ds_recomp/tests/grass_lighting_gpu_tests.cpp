#include "fast/oot3d/grass_draw_state.h"
#include "fast/oot3d/grass_shader_sources.h"
#include "fast/renderer3ds/pica_surface_lighting_shader.h"
#include "vulkan_compute_test.h"
#include <cmath>
#include <iostream>
#include <regex>
#include <vector>

using namespace Fast::Renderer3ds;
using namespace Fast::Oot3d;
using V4 = std::array<float, 4>;
namespace {
struct Case {
    PicaSurfaceLightingUniforms Lighting;
    GrassEnvironmentRecord Grass;
    V4 Primary{1,1,1,1}, Normal{0,1,0,0}, View{0,1,0,0};
    V4 Shadow{0.6F,0.8F,0,1};
    V4 Controls{}; // native presentation, height, packed terrain colour, rim weight
};
struct Result { V4 Atlas, Ground, Blade, Legacy, Ceiling; };
void Near(float a, float b, const std::string& name, float tolerance = 2e-5F) {
    if (!std::isfinite(a) || std::abs(a-b) > tolerance)
        throw std::runtime_error(name + ": got " + std::to_string(a) + " expected " + std::to_string(b));
}
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

    return c;
}

std::string Slice(const std::string& s, const char* first, const char* last) {
    auto a=s.find(first), b=s.find(last,a);
    Require(a!=std::string::npos && b!=std::string::npos, "production shader seam missing");
    return s.substr(a,b-a);
}
std::vector<Result> Evaluate(const std::vector<Case>& cases) {
    auto producer = BuildPicaSurfaceLightingCoefficientsShader(true);
    producer.erase(0, producer.find('\n')+1);
    producer = std::regex_replace(producer, std::regex(R"(layout\([^)]*\)\s*)"), "");
    producer = std::regex_replace(producer, std::regex(R"((^|\n)(in|out) (vec[234] \w+;))"), "$1$3");
    producer = std::regex_replace(producer, std::regex(R"(\buniform\b)"), "struct");
    const std::string stub="vec3 pica_surface_shadow(int slot) { return vec3(1.0); }";
    auto at=producer.find(stub); Require(at!=std::string::npos,"shadow seam missing");
    producer.replace(at,stub.size(),"vec3 pica_surface_shadow(int slot) { return vec3((test_mask & (1u << uint(slot))) != 0u ? test_visibility : 1.0); }");
    const auto vertex=BuildGrassVertexShader(), fragment=BuildGrassFragmentShader(false);
    std::string shader="#version 450\nlayout(local_size_x=1) in;\nfloat test_visibility=1.0; uint test_mask=0u;\n#define main coefficients_main\n" + producer + "\n#undef main\n";
    auto nativeProducer=BuildPicaSurfaceLightingCoefficientsShader(false);
    nativeProducer=nativeProducer.substr(nativeProducer.find("void main()"));
    nativeProducer.replace(5,4,"native_coefficients_main");
    shader += nativeProducer;
    shader += kPicaSurfaceLightingRecomposeShader;
    shader += kToonSurfaceResponseShader;
    shader += Slice(vertex,"struct GrassEnvironmentRecord {","layout(std430, set=0, binding=0)");
    shader += "struct GrassEnvironmentState { GrassEnvironmentRecord records[1]; }; GrassEnvironmentState environment_state;\n";
    shader += Slice(vertex,"vec3 grass_native_material_rgb(","vec4 grass_surface_sample(");
    shader += Slice(vertex,"vec3 evaluate_blade_color(","vec2 evaluate_wind(");
    shader += R"glsl(
struct Case { PicaSurfaceLighting lighting; GrassEnvironmentRecord grass;
    vec4 primary; vec4 normal; vec4 view; vec4 shadow; vec4 controls; };
struct Result { vec4 atlas; vec4 ground; vec4 blade; vec4 legacy; vec4 ceiling; };
layout(set=0,binding=0,std430) readonly buffer Cases { Case cases[]; } inputs;
layout(set=0,binding=1,std430) writeonly buffer Results { Result values[]; } outputs;
struct GrassState { uvec4 flags; }; GrassState grass;
void main() {
    uint i=gl_GlobalInvocationID.x;
    Case c=inputs.cases[i];
    pica_surface=c.lighting; pica_primary_color=c.primary;
    pica_surface_normal=c.normal.xyz; pica_view=c.view.xyz; pica_surface_tint=vec4(1.0);
    surface_atlas.shadow_direction=c.shadow;
    test_mask=pica_surface_shadow_mask();
    if (c.controls.x==1.0) native_coefficients_main(); else coefficients_main();
    vec3 lighting=pica_surface_recompose(surface_primary,surface_raw.rgb,surface_directional.rgb,c.shadow.w);
    test_visibility=c.shadow.w;
    vec3 ground=pica_surface_primary(c.primary.rgb,c.normal.xyz);
    // Pre-fix ambient-only grass atlas: absolute profile response, unrelated to C.
    vec3 unused;
    vec3 legacy=min(abs(pica_surface_native_response(c.normal.xyz,0u,unused)),vec3(1.0));
    if (c.controls.x==1.0) ground=legacy=c.primary.rgb; // Native replay (no surface uniforms).
    if (c.controls.x==2.0) lighting=legacy=vec3(1.0); // ReceiveLighting opt-out.
    environment_state.records[0]=c.grass; grass.flags=uvec4(0);
    uint terrain=floatBitsToUint(c.controls.z);
    vec4 blade_color=vec4(evaluate_blade_color(0u,c.controls.y,lighting,terrain),1.0);
    vec3 old_color=evaluate_blade_color(0u,c.controls.y,legacy,terrain);
    vec3 blade_lit_ceiling=grass_native_material_rgb(vec3(1.0),lighting,c.grass.native_lighting);
    if (c.grass.toon.flags.x>0.5) {
        blade_color.rgb=oot3d_toon_banded_color(blade_color.rgb,lighting,c.grass.toon.flags.y,true,c.grass.toon);
        old_color=oot3d_toon_banded_color(old_color,legacy,c.grass.toon.flags.y,true,c.grass.toon);
    }
    vec4 blade_normal_guide=vec4(c.normal.xyz*0.5+0.5,0);
    vec3 blade_view_direction=c.view.xyz;
    float blade_rim_weight=c.controls.w;
)glsl";
    // Execute the actual fragment's complete colour/rim/ceiling block, before fog.
    shader += Slice(fragment,"    vec4 resolved_color = blade_color;","#if GRASS_AUXILIARY_OUTPUTS");
    shader += R"glsl(
    outputs.values[i]=Result(vec4(lighting,1),vec4(ground,1),resolved_color,
        vec4(old_color,1),vec4(blade_lit_ceiling,1));
}
)glsl";
    std::vector<Result> result(cases.size());
    RunComputeTest(shader,std::as_bytes(std::span(cases)),std::as_writable_bytes(std::span(result)),uint32_t(cases.size()));
    return result;
}
void CompileVariants() {
    shaderc::Compiler compiler;
    auto check=[&](const std::string& source, shaderc_shader_kind stage) {
        auto result=compiler.CompileGlslToSpv(source,stage,"grass_lighting_contract");
        Require(result.GetCompilationStatus()==shaderc_compilation_status_success,result.GetErrorMessage().c_str());
    };
    check(BuildGrassVertexShader(),shaderc_vertex_shader);
    for (bool raster : {false,true}) for (bool aux : {false,true}) {
        auto source=BuildGrassFragmentShader(raster);
        source.insert(source.find('\n')+1,aux?"#define GRASS_AUXILIARY_OUTPUTS 1\n":"#define GRASS_AUXILIARY_OUTPUTS 0\n");
        check(source,shaderc_fragment_shader);
    }
    for (bool surface : {false,true}) for (bool coefficients : {false,true})
        check(BuildPicaSurfaceLightingFragmentShader(surface,coefficients),shaderc_fragment_shader);
}
} // namespace

int main() try {
    CompileVariants();
    std::vector<Case> cases;
    // User appearance values; texture samples include roots, tips, dark twins,
    // bright texels, with TEV passthrough scales covered below.
    for (bool night : {false,true}) for (bool ambient : {false,true})
      for (bool profile : {false,true}) for (bool preserve : {false,true})
       for (float visibility : {0.0F,0.5F,1.0F}) for (float height : {0.0F,0.5F,1.0F}) {
        auto c=MakeCase(profile,preserve,ambient,night);
        c.Grass.AppearanceRoot={.09882352F,.19F,.035F,1};
        c.Grass.AppearanceTip={.42F,.72F,.12F,1};
        c.Grass.TextureColor={.2F,.35F,.07F,.22F};
        c.Grass.TextureBrightnessFlags={2.87F,4,1,0};
        c.Grass.NativeLighting={0,3,1,1};
        c.Shadow[3]=visibility; c.Controls[1]=height;
        cases.push_back(c);
        c.Normal={1,0,0,0}; cases.push_back(c);
       }
    // Local/fill/rim light stays additive in the same units as the ground;
    // the selected key's shadow cannot incorrectly suppress these sources.
    for (float visibility : {0.0F,0.3F,1.0F}) {
        auto c=cases.back(); c.Normal={0,1,0,0}; c.Shadow[3]=visibility;
        c.Lighting.Control[1]=1;
        c.Lighting.ScenePoints[0].PositionRadius={0,3,0,10};
        c.Lighting.ScenePoints[0].Color={.1F,.04F,.07F,-1};
        c.Lighting.EnvironmentDirections[1]={0,1,0,.3F};
        c.Lighting.EnvironmentColors[1]={.2F,.3F,.4F,0};
        c.Lighting.Rim={.2F,3,1,.25F};
        c.Lighting.RimColor={.2F,.1F,.1F,0};
        c.Lighting.RimDirection={1,0,0,0};
        cases.push_back(c);
        c.Grass.NativeLighting[2]=2; cases.push_back(c); // Native TEV gain.
    }
    // Untinted white key follows the reference continuously through twilight,
    // including a dark channel and a rotated camera basis.
    for (float level : {0.0F,.01F,.12F,.25F,.4F,.6F,.8F,1.0F}) {
        auto c=cases[2]; c.Shadow[3]=1; c.Primary={level,.5F*level,0,1};
        cases.push_back(c);
        c.Normal=c.View={1,0,0,0}; c.Shadow={.8F,-.6F,0,.5F};
        c.Lighting.BakedReferenceUp={1,0,0,1};
        c.Lighting.EnvironmentDirections[0]={.8F,-.6F,0,1};
        cases.push_back(c);
    }
    // The new atlas mode belongs only to baked outdoor surfaces. Static
    // interiors and directly lit native materials retain their old evaluator.
    for (bool direct : {false,true}) for (float visibility : {0.0F,0.5F,1.0F}) {
        auto c=MakeCase(true,false,true,false,false,1,.3F,.3F,direct);
        c.Grass=cases[0].Grass; c.Shadow[3]=visibility;
        cases.push_back(c);
    }
    const size_t matrix=cases.size();
    // Strong tip gains and toon rim must respect dark/black native channels,
    // including in Native presentation; ReceiveLighting=false remains unlit.
    for (int mode : {0,1,2}) for (float primary : {0.0F,0.08F,0.25F,1.0F}) {
        auto c=cases[0]; c.Controls={float(mode),1,0,1};
        c.Primary={primary,primary,primary,1};
        c.Grass.TextureColor={1,1,1,1};
        c.Grass.Toon.Flags={1,1,2,0}; c.Grass.Toon.Control={3,.2F,1.09F,0};
        c.Grass.Toon.Rim={1,1,1,4}; c.View={0,0,1,0};
        cases.push_back(c);
    }
    // Quantify the day change where the native terrain is at unit lit level:
    // same user appearance, white unit key, unshadowed horizontal surface.
    size_t day=cases.size();
    auto c=MakeCase(true,false,true); c.Grass=cases[0].Grass;
    c.Primary={1,1,1,1}; c.Controls[1]=1; cases.push_back(c);
    auto night=c; night.Primary={.25F,.25F,.25F,1};
    // Same raw profile as day: this is the regression that must darken.
    cases.push_back(night);
    auto result=Evaluate(cases);
    for (size_t i=0;i<cases.size();++i) for (int k=0;k<3;++k) {
        if (cases[i].Controls[0]!=2) Near(result[i].Atlas[k],result[i].Ground[k],"terrain/grass atlas case "+std::to_string(i));
        Require(std::isfinite(result[i].Blade[k]) && result[i].Blade[k]>=0,"nonfinite/negative blade");
        Require(result[i].Blade[k]<=result[i].Ceiling[k]+2e-5F,"tip/rim bypassed native lighting ceiling");
        if (i>=matrix && i<day && cases[i].Primary[k]==0 && cases[i].Controls[0]!=2)
            Near(result[i].Blade[k],0,"black native reference with bright rim");
    }
    for (int k=0;k<3;++k) Near(result[day].Blade[k],result[day].Legacy[k],"day identity");
    std::cout<<"Day old/new and night old/new RGB (controlled production shader):\n";
    for (size_t i : {day,day+1}) {
        for (auto v : {result[i].Legacy,result[i].Blade}) {
            for(int k=0;k<3;++k) std::cout<<v[k]*255<<' ';
            std::cout<<" | ";
        }
        std::cout<<'\n';
    }
    std::cout<<"Grass lighting: "<<cases.size()<<" GPU cases, 9 shader variants passed\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"Grass lighting FAILED: "<<e.what()<<'\n';return 1; }
