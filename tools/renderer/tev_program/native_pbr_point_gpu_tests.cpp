#include "fast/renderer3ds/pica_pbr_shading.h"
#include "fast/renderer3ds/pica_fragment_balance.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "vulkan_compute_test.h"
#include <regex>
#include <iostream>
#include <cmath>
using namespace Fast::Renderer3ds;
using V4=std::array<float,4>;
struct Case {
    PicaBalancedSurfaceUniforms Lighting;
    PicaPbrUniforms Pbr;
    V4 Position,Diffuse,Ambient,Global,View,Normal,Parameters;
};
int main() try {
    PicaShaderHookLayout hooks;hooks.SampledTextureMask=1;
    hooks.TextureSamples[0]={0,PicaTextureCoordinateOperation::NativeVFlip};
    PicaPbrShaderOptions options;options.TextureSlot=0;options.NativeFragmentLighting=true;options.NativeBalance=true;options.NativeAttenuationLuts=true;
    const auto pbr=BuildPicaPbrShader(hooks,options);
    if(!pbr)throw std::runtime_error("native PBR contract rejected");
    auto storage=[](std::string text){
        text=std::regex_replace(text,std::regex(R"(layout\([^)]*\)\s*)"),"");
        text=std::regex_replace(text,std::regex(R"(\bin\s+)"),"");
        return std::regex_replace(text,std::regex(R"(\buniform\b)"),"struct");
    };
    std::string shader="#version 450\nlayout(local_size_x=1) in;\nvec3 pica_view;float visibility;\n";
    shader+=R"glsl(
struct NativeFragment {vec4 lighting_position[8];vec4 lighting_diffuse[8];vec4 lighting_ambient[8];
 vec4 lighting_attenuation[8];vec4 lighting_spot_direction[8];vec4 lighting_global_ambient;};
NativeFragment fragment_uniforms;
float pica_lighting_lut_unsigned(int table,float x){return 1.0-x;}
float pica_lighting_lut_signed(int table,float x){return 1.0;}
)glsl";
    shader+=storage(PicaSurfaceLightingDeclarations(false,true));
    std::string material=pbr->Declarations;
    material=std::regex_replace(material,std::regex(R"(layout\([^)]*\)\s*uniform sampler2D ([a-z_]+);)"),"int $1;");
    material=storage(material);
    material=std::regex_replace(material,std::regex("dFdx\\("),"test_dx(");
    material=std::regex_replace(material,std::regex("dFdy\\("),"test_dy(");
    material=std::regex_replace(material,std::regex("textureGrad\\("),"test_texture_grad(");
    material=std::regex_replace(material,std::regex("texture\\("),"test_texture(");
    shader+=R"glsl(
vec2 test_dx(vec2 v){return vec2(0);}vec3 test_dx(vec3 v){return vec3(0);}
vec2 test_dy(vec2 v){return vec2(0);}vec3 test_dy(vec3 v){return vec3(0);}
vec4 test_texture(int s,vec2 uv){return vec4(1);}
vec4 test_texture_grad(int s,vec2 uv,vec2 x,vec2 y){return vec4(1);}
vec3 pica_surface_shadow(int slot){return vec3(1);}
float pica_point_shadow(float slot,vec3 delta,float radius){return visibility;}
vec4 pica_shadow_factor=vec4(1);
)glsl";
    shader+=material;
    shader+=R"glsl(
struct Case {PicaSurfaceLighting lighting;PicaPbrMaterial pbr;vec4 position;vec4 diffuse;vec4 ambient;vec4 global;
 vec4 view;vec4 normal;vec4 parameters;};
layout(set=0,binding=0,std430) readonly buffer Cases {Case values[];} inputs;
layout(set=0,binding=1,std430) writeonly buffer Results {vec4 values[];} outputs;
void main(){uint index=gl_GlobalInvocationID.x;Case sample_case=inputs.values[index];
pica_surface=sample_case.lighting;pbr_material=sample_case.pbr;visibility=sample_case.parameters.x;
pica_view=sample_case.view.xyz;pica_surface_normal=sample_case.normal.xyz;
fragment_uniforms.lighting_position[0]=sample_case.position;fragment_uniforms.lighting_diffuse[0]=sample_case.diffuse;
fragment_uniforms.lighting_ambient[0]=sample_case.ambient;fragment_uniforms.lighting_global_ambient=sample_case.global;
fragment_uniforms.lighting_attenuation[0]=vec4(0,.05,0,0);
fragment_uniforms.lighting_spot_direction[0]=vec4(0,0,-1,0);
vec2 pbr_uv=vec2(.5);vec3 pbr_geometric_normal=pica_surface_normal;
vec3 pbr_view=pica_surface_unit(pica_view);vec3 pbr_tangent=vec3(1,0,0),pbr_bitangent=vec3(0,1,0);
bool pbr_valid_frame=false;vec4 combiner_output=vec4(.5,.5,.5,.37);
)glsl";
    std::string body=pbr->ShadingBody;
    body=std::regex_replace(body,std::regex("dFdx\\("),"test_dx(");
    body=std::regex_replace(body,std::regex("dFdy\\("),"test_dy(");
    body=std::regex_replace(body,std::regex("textureGrad\\("),"test_texture_grad(");
    body=std::regex_replace(body,std::regex("texture\\("),"test_texture(");
    const size_t bodyAt=shader.size();
    shader+=body;
    shader+="outputs.values[index]=vec4(pbr_lit,combiner_output.a);}\n";
    std::array<Case,12> cases{};std::array<V4,12> expected{},output{};
    const float albedo=std::pow((.5F+.055F)/1.055F,2.4F),window=1.F-121.F/400.F;
    for(size_t i=0;i<cases.size();++i){
        auto& c=cases[i];auto& s=c.Lighting.Surface;auto& b=c.Lighting.Balance;
        PicaPbrMaterialParameters parameters;parameters.RoughnessFactor=1;parameters.MinimumRoughness=1;
        parameters.MetallicFactor=i==6?.7F:0;parameters.SpecularWeight=i==6?1:0;
        if(i==7)parameters.EmissiveFactor={.2F,.1F,.05F};
        PicaLightingProgram lp;lp.Control[0]=1;lp.Lights[0][0]=0;
        if(i>=9)lp.Lights[0][1]=64; // production native distance LUT
        c.Pbr=BuildPicaPbrUniforms(parameters,0,lp);
        c.Position={0,0,10,0};c.Diffuse={.5F,.5F,.5F,0};c.Ambient={.02F,.02F,.02F,0};c.Global={.04F,.04F,.04F,0};
        c.View={0,0,1,0};c.Normal={0,0,1,0};c.Parameters={i<3?float(i)*.5F:1.F,0,0,0};
        s.Control={0,1,0,2};s.MaterialDiffuse={.5F,.5F,.5F,0};s.MaterialAmbient={.8F,.8F,.8F,0};
        s.ScenePoints[0].PositionRadius={0,0,10,20};s.ScenePoints[0].Color={1,1,1,0};
        for(size_t k=0;k<3;++k)s.ViewToWorldLinear[k][k]=1;
        b.Control={1,8,0,2};b.Parameters[0]=1;b.Gains[0]={1,1,1,4};
        if(i==3){s.Control[1]=0;b.Gains[0]={1,1,1,0};}
        if(i==4){s.MaterialDiffuse={};c.Diffuse={};}
        if(i==5){b.Gains[0]={1,1,1,0};c.Diffuse={.1F,.1F,.1F,0};}
        if(i==8){s.EnvironmentParameters={0,.6F,0,1};b.Parameters[0]=.6F;}
        if(i==10)b.Gains[0][1]=2;
        if(i==11)b.Gains[0][1]=.5F;
        FoldPicaSurfaceMaterialForPbr(s,true);
        if (b.Gains[0][3] >= 4) s.ScenePoints[0].Color={1,1,1,0}; // matched row is a user tint
        const float metal=parameters.MetallicFactor,f0=.04F*(1-metal)+albedo*metal;
        const float brdf=(1-f0)*(1-metal)*albedo+f0*.25F*parameters.SpecularWeight;
        float irradiance=i==3?.5F:i==4?0:.5F*c.Parameters[0];
        if(i==5)irradiance*=window; // unmatched supplemental point still uses its own falloff
        if(i==5)irradiance+=.1F;
        if(i>=9)irradiance*=std::max(0.F,1.F-.55F/b.Gains[0][1]);
        const float ambient=.02F*(i>=9?.45F:1.F)+.04F*b.Parameters[0];
        for(size_t k=0;k<3;++k)expected[i][k]=brdf*irradiance+ambient*albedo*(1-metal)+parameters.EmissiveFactor[k];
        expected[i][3]=.37F;
    }
    RunComputeTest(shader,std::as_bytes(std::span(cases)),std::as_writable_bytes(std::span(output)),cases.size());
    unsigned failed=0;
    for(size_t i=0;i<cases.size();++i)for(size_t k=0;k<4;++k)
        if(!std::isfinite(output[i][k])||std::abs(output[i][k]-expected[i][k])>3e-5F){
            std::cerr<<"GGX case "<<i<<" channel "<<k<<" actual="<<output[i][k]<<" expected="<<expected[i][k]<<'\n';++failed;
        }
    std::cout<<"Native GGX points: "<<cases.size()<<" production GPU cases, failures="<<failed<<'\n';
    options.NativeFragmentLighting=false;options.NativeBalance=false;options.NativeAttenuationLuts=false;options.PreserveNativeUnlit=true;
    const auto unlit=BuildPicaPbrShader(hooks,options);
    if(!unlit)throw std::runtime_error("native unlit PBR contract rejected");
    auto unlitBody=unlit->ShadingBody;
    unlitBody=std::regex_replace(unlitBody,std::regex("dFdx\\("),"test_dx(");
    unlitBody=std::regex_replace(unlitBody,std::regex("dFdy\\("),"test_dy(");
    unlitBody=std::regex_replace(unlitBody,std::regex("textureGrad\\("),"test_texture_grad(");
    unlitBody=std::regex_replace(unlitBody,std::regex("texture\\("),"test_texture(");
    shader.replace(bodyAt,body.size(),unlitBody);
    for(auto& c:cases)c.Lighting.Surface.Control[1]=0; // backend omits points on a TEV-unlit receiver
    RunComputeTest(shader,std::as_bytes(std::span(cases)),std::as_writable_bytes(std::span(output)),cases.size());
    unsigned unlitFailed=0;
    for(size_t i=0;i<cases.size();++i)for(size_t k=0;k<4;++k){
        const float want=k==3?.37F:albedo+(i==7?std::array<float,3>{.2F,.1F,.05F}[k]:0.F);
        if(!std::isfinite(output[i][k])||std::abs(output[i][k]-want)>3e-5F){
            std::cerr<<"Unlit PBR case "<<i<<" channel "<<k<<" actual="<<output[i][k]<<" expected="<<want<<'\n';++unlitFailed;
        }
    }
    std::cout<<"Native unlit PBR: "<<cases.size()<<" production GPU cases, failures="<<unlitFailed<<'\n';
    return failed||unlitFailed?1:0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
