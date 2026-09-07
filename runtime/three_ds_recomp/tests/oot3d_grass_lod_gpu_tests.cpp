#include "fast/oot3d/grass_distant_tuft.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "oot3d_vulkan_compute_fixture.h"

TEST(Oot3dGrassLodGpu, IndexedVertexDecoderMatchesExpandedStreamForEveryTopology) {
    using namespace Fast::Oot3d;
    const auto topology = BuildGrassIndexedTopology();
    std::string arrays = "const uint indices[] = uint[](";
    for (size_t i=0; i<topology.Indices.size(); ++i)
        arrays += (i ? "," : "") + std::to_string(topology.Indices[i]) + "u";
    arrays += ");\nconst uvec2 ranges[] = uvec2[](";
    for (uint32_t i=0; i<25; ++i) {
        const auto& r=i==24 ? topology.Tuft : topology.Blades[i/2][i%2];
        arrays += (i ? "," : "") + std::string("uvec2(")+std::to_string(r.First)+"u,"+std::to_string(r.Count)+"u)";
    }
    arrays += ");\n";
    const auto source = std::string(R"glsl(#version 450
layout(local_size_x=8) in;
layout(std430,set=0,binding=0) buffer Output { vec4 values[]; } output_data;
)glsl") + std::string(kGrassIndexedVertexShader) + arrays + R"glsl(
void main() {
    uint id=gl_GlobalInvocationID.y*8u+gl_GlobalInvocationID.x;
    vec4 result=vec4(0);
    if (id<25u) {
        bool tuft=id==24u;
        uint segments=id/2u+1u;
        uint plane_count=3u+(segments-1u)*6u;
        for(uint i=0u;i<ranges[id].y;++i) {
            uint plane; float h; float w;
            grass_indexed_vertex(indices[ranges[id].x+i],segments,tuft,plane,h,w);
            uint local=i%plane_count;
            float old_h; float old_w;
            if(tuft) {
                const vec2 corners[6]=vec2[6](vec2(-1,0),vec2(1,0),vec2(1,1),vec2(-1,0),vec2(1,1),vec2(-1,1));
                old_w=corners[i].x; old_h=corners[i].y;
            } else if(local>=(segments-1u)*6u) {
                uint c=local-(segments-1u)*6u;
                old_h=c==2u ? 1.0 : float(segments-1u)/float(segments);
                old_w=c==0u ? -1.0 : c==1u ? 1.0 : 0.0;
            } else {
                uint s=local/6u; uint c=local%6u;
                old_h=float(s+((c==2u||c==4u||c==5u)?1u:0u))/float(segments);
                old_w=c==0u||c==3u||c==5u ? -1.0 : 1.0;
            }
            if(h!=old_h || w!=old_w || plane!=(tuft?0u:i/plane_count)) result.x+=1.0;
        }
        result.y=float(ranges[id].y);
    }
    output_data.values[id]=result;
}
)glsl";
    ComputeFixture gpu;
    const auto values=gpu.Run(source);
    for(size_t i=0;i<values.size();++i) {
        EXPECT_EQ(values[i][0],0) << "topology " << i;
        if(i<25) EXPECT_GT(values[i][1],0);
    }
}

TEST(Oot3dGrassLodGpu, TransitionAndFadeMathMatchesCpuAcrossDensityScales) {
    const auto source = std::string(R"glsl(#version 450
layout(local_size_x=8) in;
layout(std430,set=0,binding=0) buffer Output { vec4 values[]; } output_data;
)glsl") + std::string(Fast::Oot3d::kGrassDistantTuftShader) + R"glsl(
void main() {
    float row=float(gl_GlobalInvocationID.y);
    float x=float(gl_GlobalInvocationID.x)/7.0;
    float start=0.2+row*0.1;
    float end=start+(row==0.0 ? 0.0 : 0.4);
    float weight=grass_tuft_weight(x,start,end);
    float retention=row==0.0 ? 1.0 : (row==5.0 ? 0.0001 : 0.25);
    float fade=grass_visibility_fade(retention,retention*x,0.3);
    float distance_fade=1.0-grass_tuft_weight(x,0.6,1.0);
    float scale=grass_tuft_retention_scale(weight,5.0,0.1+row*0.78);
    output_data.values[gl_GlobalInvocationID.y*8u+gl_GlobalInvocationID.x]=vec4(weight,scale,fade,distance_fade);
}
)glsl";
    ComputeFixture gpu;
    const auto values = gpu.Run(source);
    using namespace Fast::Oot3d;
    for (size_t row = 0; row < 6; ++row) {
        for (size_t column = 0; column < 8; ++column) {
            SCOPED_TRACE("row=" + std::to_string(row) + " column=" + std::to_string(column));
            const float x = static_cast<float>(column) / 7.0F;
            const float start = 0.2F + static_cast<float>(row) * 0.1F;
            const float weight = GrassTuftWeight(x, start, start + (row == 0 ? 0 : 0.4F));
            const float retention = row == 0 ? 1.0F : row == 5 ? 0.0001F : 0.25F;
            const auto& value = values[row * 8 + column];
            EXPECT_NEAR(value[0], weight, 1.0e-5F);
            EXPECT_NEAR(value[1], GrassTuftRetentionScale(weight, 5, 0.1F + static_cast<float>(row) * 0.78F), 1.0e-5F);
            EXPECT_NEAR(value[2], GrassVisibilityFade(retention, retention*x, 0.3F), 1.0e-5F);
            EXPECT_NEAR(value[3], GrassDistanceFade(x, 1, 0.4F), 1.0e-5F);
        }
    }
}
