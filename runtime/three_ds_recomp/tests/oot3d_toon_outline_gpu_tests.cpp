#include "fast/oot3d/toon_outline_shader.h"

#include "oot3d_vulkan_compute_fixture.h"

TEST(Oot3dToonOutlineGpu, ForegroundContourKeepsFullWidthOverBackgroundGrass) {
    const std::string source = std::string(R"glsl(#version 450
layout(local_size_x=8) in;
layout(std430,set=0,binding=0) buffer Output { vec4 values[]; } output_data;
vec4 oot3d_outline_sample_geometry(vec2 uv) {
    bool front=uv.x<=0.001;
    if(gl_GlobalInvocationID.y==5u) return vec4(front ? vec3(0,0,1) : vec3(1,0,0),0.25);
    return vec4(0,0,1,front ? 0.25 : 0.85);
}
float oot3d_outline_sample_occlusion(vec2 uv) {
    uint scenario=gl_GlobalInvocationID.y;
    if(scenario==0u) return 0.0;
    if(scenario==2u) return 0.9; // Grass in front of the object.
    if(uv.x<=0.001) return 0.0; // Opaque foreground is visible here.
    if(scenario==3u) return 0.05; // Behind everything.
    if(scenario==4u) return 0.75; // Equal depth: no foreground occlusion.
    return 0.5; // Behind the object, but in front of its background.
}
float oot3d_outline_sample_scene_depth(vec2 uv) {
    float depth=oot3d_outline_sample_geometry(uv).a;
    float occlusion=oot3d_outline_sample_occlusion(uv);
    return occlusion>0.0 ? min(depth,1.0-occlusion) : depth;
}
vec4 oot3d_outline_sample_normal(vec2 uv) {
    vec4 native=oot3d_outline_sample_geometry(uv);
    bool grass=oot3d_outline_sample_scene_depth(uv)<native.a;
    return grass ? vec4(0.5,1,0.5,64.0/255.0) : vec4(native.xyz*0.5+0.5,1);
}
)glsl") + Fast::Oot3d::ToonOutlineShaderLibrary() + R"glsl(
void main() {
    vec2 uv=vec2(float(gl_GlobalInvocationID.x)-3.0,0);
    float normal=gl_GlobalInvocationID.y==5u ? 1.0 : 0.0;
    float depth=1.0-normal;
    float raw=oot3d_toon_outline_native_edge(uv,vec2(1),2.0,depth,normal,1.0);
    float visible=oot3d_toon_outline_edge(uv,vec2(1),2.0,depth,normal,1.0);
    output_data.values[gl_GlobalInvocationID.y*8u+gl_GlobalInvocationID.x]=vec4(raw,visible,0,0);
}
)glsl";
    ComputeFixture gpu;
    const auto result = gpu.Run(source);
    for (size_t scenario = 0; scenario < 6; ++scenario) {
        size_t active = 0;
        for (size_t pixel = 0; pixel < 8; ++pixel) {
            SCOPED_TRACE("scenario=" + std::to_string(scenario) + " pixel=" + std::to_string(pixel));
            const auto& sample = result[scenario * 8 + pixel];
            active += sample[0] > 0;
            EXPECT_FLOAT_EQ(sample[1], scenario == 2 ? 0.0F : sample[0]);
        }
        EXPECT_EQ(active, 4U) << "Both halves of the two-pixel-radius contour must be tested";
    }
}

TEST(Oot3dToonOutlineGpu, HistoricalSceneGuideResponseIsNotNativeOnlyIsolation) {
    const std::string source = std::string(R"glsl(#version 450
layout(local_size_x=8) in;
layout(std430,set=0,binding=0) buffer Output { vec4 values[]; } output_data;
vec4 oot3d_outline_sample_geometry(vec2 uv) {
    uint scenario=gl_GlobalInvocationID.y;
    return vec4(0,0,1,scenario==1u || scenario==3u ? 0.5 : (uv.x<=0.001 ? 0.25 : 0.85));
}
float oot3d_outline_sample_scene_depth(vec2 uv) {
    return gl_GlobalInvocationID.y==1u ? 0.25 : (uv.x<=0.001 ? 0.25 : 0.85);
}
vec4 oot3d_outline_sample_normal(vec2 uv) {
    uint scenario=gl_GlobalInvocationID.y;
    if(scenario==2u) return vec4(0.5,1,0.5,0); // No scene coverage.
    if(scenario==1u && uv.x>0.001) return vec4(1,0.5,0.5,1);
    return vec4(0.5,0.5,1,scenario==5u ? 0.0 : 1.0);
}
float oot3d_outline_sample_occlusion(vec2 uv) {
    return gl_GlobalInvocationID.y==5u ? 0.01 : 0.0;
}
)glsl") + Fast::Oot3d::ToonOutlineShaderLibrary() + R"glsl(
void main() {
    vec2 uv=vec2(float(gl_GlobalInvocationID.x)-3.0,0);
    uint scenario=gl_GlobalInvocationID.y;
    float normal=scenario==1u ? 1.0 : 0.0;
    float depth=scenario==4u ? 0.0 : 1.0-normal;
    float raw=oot3d_toon_outline_native_edge(uv,vec2(1),2.0,depth,normal,1.0);
    float visible=oot3d_toon_outline_edge(uv,vec2(1),2.0,depth,normal,1.0);
    output_data.values[scenario*8u+gl_GlobalInvocationID.x]=vec4(raw,visible,0,0);
}
)glsl";
    ComputeFixture gpu;
    const auto result = gpu.Run(source);
    for (size_t scenario = 0; scenario < 6; ++scenario) {
        for (size_t pixel = 0; pixel < 8; ++pixel) {
            SCOPED_TRACE("scenario=" + std::to_string(scenario) + " pixel=" + std::to_string(pixel));
            const bool footprint = pixel >= 2 && pixel <= 5;
            const bool nativeEdge = scenario != 1 && scenario != 3 && scenario != 4 && footprint;
            EXPECT_FLOAT_EQ(result[scenario * 8 + pixel][0], nativeEdge ? 1.0F : 0.0F);
            const bool edge = scenario != 2 && scenario != 3 && scenario != 4 && footprint;
            EXPECT_FLOAT_EQ(result[scenario * 8 + pixel][1], edge ? 1.0F : 0.0F);
        }
    }
}
