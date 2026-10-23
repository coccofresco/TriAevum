#include "fast/renderer3ds/pica_motion_provenance.h"
#include "fast/oot3d/pica_rigid_motion.h"
#include "fast/oot3d/pica_shader_instrumentation.h"
#include "fast/oot3d/pica_shader_pipeline_cache.h"
#include "fast/oot3d/pica_uber_effect_program.h"
#include "fast/oot3d/motion_vectors.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Fast::Oot3d;
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    constexpr std::string_view source = R"glsl(#version 450
layout(location=0) in vec4 pica_primary_color;
layout(location=1) in vec2 pica_texcoord0;
layout(location=2) in vec2 pica_texcoord1;
layout(location=3) in vec2 pica_texcoord2;
layout(location=4) in vec4 pica_normquat;
layout(location=5) in vec3 pica_view;
layout(location=0) out vec4 pica_color;
layout(set=0,binding=1) uniform sampler2D pica_texture0;
layout(set=0,binding=2) uniform sampler2D pica_texture1;
layout(set=0,binding=3) uniform sampler2D pica_texture2;
layout(set=0,binding=4) uniform Fragment { vec4 fog_color; vec4 texture_lod_bias; } fragment_uniforms;
float pica_texture_lod(vec2 coord, vec2 size) {
    vec2 scaled = coord * size;
    vec2 delta = max(abs(dFdx(scaled)), abs(dFdy(scaled)));
    return log2(max(delta.x, delta.y));
}
void main() {
    vec4 rounded_primary_color = pica_primary_color;
    vec4 primary_fragment_color = vec4(1.0);
    vec4 secondary_fragment_color = vec4(0.0);
    float fog_factor = 1.0;
    // LIGHTING
    vec4 combiner_output = primary_fragment_color * rounded_primary_color;
    // AFTER_TEV
    // BEFORE_DEPTH
    pica_color = combiner_output;
}
)glsl";
    auto hooks = AnalyzePicaFragmentShaderHooks(source);
    const auto set = [&](auto hook, std::string_view marker) { hooks.Offsets[static_cast<size_t>(hook)] = source.find(marker); };
    set(::Oot3d::Renderer::PicaShaderHook::PicaLighting,"    // LIGHTING");
    set(::Oot3d::Renderer::PicaShaderHook::AfterNativeTev,"    // AFTER_TEV");
    set(::Oot3d::Renderer::PicaShaderHook::BeforeDepth,"    // BEFORE_DEPTH");
    PicaFragmentInstrumentationRequest request;
    request.Source=source; request.FragmentKey=17; request.Hooks=&hooks;
    request.RequestedFeatures=PicaShaderInstrumentationFeature::RigidMotionGuide;
    request.Draw.DepthTestEnabled=request.Draw.DepthWriteEnabled=true;
    request.Draw.ColorWriteMask=15;
    request.Draw.CompositionDomain=::Oot3d::Renderer::PicaCompositionDomain::Scene;
    const auto composed=BuildPicaFragmentInstrumentationVariant(request);
    const auto standalone=BuildPicaRigidMotionShaderVariant(source,17);
    PicaUberEffectProgramRequest uberRequest{source,hooks,true,false,false};
    const auto uber=BuildPicaUberEffectProgram(uberRequest);
    for (const auto* shader : {&composed.Source,&standalone.Source,&uber})
        Require(shader->find(Fast::Renderer3ds::PicaMotionGuideAssignment)!=std::string::npos,
                "shader variants disagree on exact/invalid motion");
    Require(composed.RigidMotionApplied && standalone.Applied && !uber.empty(), "guide writer absent");
    request.TemporalInputs=false;
    const auto fallback=BuildPicaFragmentInstrumentationVariant(request);
    uberRequest.TemporalInputs=false;
    const auto uberFallback=BuildPicaUberEffectProgram(uberRequest);
    for (const auto* shader : {&fallback.Source,&uberFallback}) {
        Require(shader->find("layout(location=7) in")==std::string::npos,"fallback requires missing vertex outputs");
        Require(shader->find("pica_rigid_motion_guide = vec4(oot3d_draw.rigid_motion.xyz, 0.0)")!=std::string::npos,
                "untyped draw leaves stale object guide");
    }
    Require(PicaUberEffectSupports(fallback.Decisions,uberRequest),"fallback unavailable in parametric shader");
    const auto compute=BuildCameraMotionComputeShader();
    Require(compute.find(Fast::Renderer3ds::PicaMotionGuideResolve)!=std::string::npos,"consumer lost invalid policy");
    PicaShaderPipelineCache cache;
    PicaShaderPipelineRequest pipeline;
    constexpr std::string_view vertex = "#version 450\nvec4 pica_output0 = vec4(0,0,0,1);\nvoid main() { gl_Position = pica_output0; }\n";
    pipeline.VertexShaderSource=vertex; pipeline.FragmentShaderSource=source;
    pipeline.VertexShaderKey=1; pipeline.FragmentShaderKey=17; pipeline.FragmentShaderHooks=hooks;
    pipeline.Draw.CompositionDomain=::Oot3d::Renderer::PicaCompositionDomain::Scene;
    pipeline.Draw.DepthTestEnabled=pipeline.Draw.DepthWriteEnabled=true;
    pipeline.Draw.ColorWriteMask=15;
    EffectsSettings effects;
    cache.BeginFrame(1);
    const auto& disabled=cache.Resolve(pipeline,effects);
    Require(disabled.IsCanonical() && disabled.VertexShaderSource==vertex && disabled.FragmentShaderSource==source,
            "disabled temporal effects changed shaders");
    pipeline.TemporalMotionEnabled=true;
    const auto& untyped=cache.Resolve(pipeline,effects);
    Require(!untyped.TemporalVertexApplied && untyped.RigidMotionApplied,
            "missing typed program kept camera-only/stale motion");
    const auto temporalVertex=BuildPicaTemporalVertexVariant(vertex,1);
    Require(temporalVertex.Applied,"exact vertex path lost");
    if (argc==2) {
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        const auto write=[&](const char* file, const std::string& text){std::ofstream(directory/file)<<text;};
        write("standalone.frag",standalone.Source); write("composed.frag",composed.Source);
        write("uber.frag",uber); write("fallback.frag",fallback.Source); write("uber_fallback.frag",uberFallback);
        write("motion.comp",compute); write("temporal.vert",temporalVertex.Source);
    }
    std::cout << "standalone/composed/parametric provenance, untyped fallback and disabled paths passed\n";
}
