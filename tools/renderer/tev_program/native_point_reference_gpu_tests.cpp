#include "fast/renderer3ds/pica_native_point_replacement.h"
#include "fast/renderer3ds/pica_lighting_program.h"
#include "vulkan_compute_test.h"
#include <regex>
#include <iostream>

using namespace Fast::Renderer3ds;
using V4 = std::array<float, 4>;
struct Case { PicaBalancedSurfaceUniforms Lighting; V4 Parameters; };
struct Result { V4 Primary, Secondary; };

int main() try {
    std::string shader = "#version 450\nlayout(local_size_x=1) in;\nvec3 pica_view;vec4 pica_primary_color;float visibility;\n";
    shader += PicaLightingProgramDeclaration;
    shader += R"glsl(
struct NativeUniforms {
 vec4 lighting_diffuse[8]; vec4 lighting_ambient[8]; vec4 lighting_specular0[8]; vec4 lighting_specular1[8];
 vec4 lighting_position[8]; vec4 lighting_spot_direction[8]; vec4 lighting_attenuation[8];
 vec4 lighting_global_ambient; PicaLightingProgram lighting_program;
};
NativeUniforms fragment_uniforms;
// The measured EUR torch LUT is linear, with native packed quantization.
// Both the reference and conversion execute the same sampler contract.
float pica_lighting_lut_unsigned(int table, float x) {return max(0.0, 1.0-x);}
float pica_lighting_lut_signed(int table, float x) {return 1.0;}
)glsl";
    shader += PicaLightingArithmeticFunctions;
    shader += PicaLightingProgramFunctions;
    auto declarations = std::regex_replace(PicaSurfaceLightingDeclarations(true,true), std::regex(R"(layout\([^)]*\)\s*)"), "");
    declarations = std::regex_replace(declarations, std::regex(R"(\bin\s+)"), "");
    shader += std::regex_replace(declarations, std::regex(R"(\buniform\b)"), "struct");
    shader += R"glsl(
struct Case {PicaSurfaceLighting lighting; vec4 parameters;};
struct Result {vec4 primary; vec4 secondary;};
layout(set=0,binding=0,std430) readonly buffer Cases {Case values[];} inputs;
layout(set=0,binding=1,std430) writeonly buffer Results {Result values[];} outputs;
vec3 pica_surface_shadow(int slot) {return vec3(1);}
float pica_point_shadow(float slot,vec3 delta,float radius) {return visibility;}
void main() {
 uint index=gl_GlobalInvocationID.x;
 pica_surface=inputs.values[index].lighting;
 float distance=inputs.values[index].parameters.x;
 visibility=inputs.values[index].parameters.y;
 pica_view=vec3(0,0,1);
 pica_surface_normal=vec3(0,0,1);
 pica_surface_tint=vec4(1);
 if (inputs.values[index].parameters.z != 0.0) {
     // Off's production legacy vertex-colour fallback. Native FPRI/TEV is
     // downstream; the VTX factor alone must remain .25 at native ownership.
     outputs.values[index]=Result(vec4(pica_surface_primary(vec3(.25),pica_surface_normal),.37),vec4(0,0,0,.63));
     return;
 }
 vec3 pica_lighting_normal=vec3(0,0,1), pica_lighting_tangent=vec3(1,0,0), pica_normalized_view=vec3(0,0,1);
 vec4 pica_shadow_factor=vec4(1);
 fragment_uniforms.lighting_program.control=uvec4(1,0,0,0);
 fragment_uniforms.lighting_program.lights[0]=uvec4(0,64,0,0);
 for(int j=0;j<7;++j)fragment_uniforms.lighting_program.luts[j]=vec4(0);
 // Clock Town floor trace slot 0: actual material-folded native radiance.
 fragment_uniforms.lighting_diffuse[0]=vec4(86.0/255.0,60.0/255.0,0,0);
 fragment_uniforms.lighting_ambient[0]=vec4(.01,.02,.03,0);
 fragment_uniforms.lighting_specular0[0]=vec4(.06,.03,0,0);
 fragment_uniforms.lighting_specular1[0]=vec4(0);
 fragment_uniforms.lighting_position[0]=vec4(0,0,distance-1,0);
 fragment_uniforms.lighting_spot_direction[0]=vec4(0,0,-1,0);
 fragment_uniforms.lighting_attenuation[0]=vec4(0,0.00399971008,0,0);
 fragment_uniforms.lighting_global_ambient=vec4(.04,.05,.06,0);
 vec4 pica_diffuse_sum=vec4(0,0,0,.37),pica_specular_sum=vec4(0,0,0,.63);
)glsl";
    shader += kPicaFragmentBalanceCaptureBegin;
    shader += PicaLightingBodyBeforeDiffuse;
    shader += kPicaFragmentBalanceCaptureDiffuse;
    shader += PicaLightingBodyBeforeSpecular;
    shader += kPicaFragmentBalanceCaptureSpecular;
    shader += PicaLightingBodyAfterSpecular;
    shader += R"glsl(
 vec4 primary_fragment_color=vec4(clamp(pica_diffuse_sum.rgb+fragment_uniforms.lighting_global_ambient.rgb,0,1),pica_diffuse_sum.a);
 vec4 secondary_fragment_color=clamp(pica_specular_sum,0,1);
 bool surface_water=false;
)glsl";
    shader += kPicaFragmentBalanceBody;
    shader += "outputs.values[index]=Result(primary_fragment_color,secondary_fragment_color);}\n";

    std::vector<Case> cases;
    std::vector<Result> expected;
    // On must equal native Off at reference distance 70 and across the entire
    // native reach, including outside it. Radius expansion must also recover
    // native radiance that was zero at the original radius (no divide by zero).
    for (float distance : {1.F,70.F,125.F,200.F,249.F,300.F})
      for (unsigned mode=0;mode<8;++mode) {
        Case c{}; auto& s=c.Lighting.Surface; auto& b=c.Lighting.Balance;
        PicaFragmentLightingLayout layout; layout.SchemaVersion=kPicaFragmentLightingLayoutSchemaVersion;
        layout.ActiveLightCount=1;layout.LightPermutation[0]=0;
        PicaNativeLightBinding binding;
        binding.Sources[0].Kind=PicaNativeLightSource::Type::Point;
        binding.Sources[0].SourceIdentity=42;binding.Sources[0].Origin=PicaNativeLightOrigin::Actor;
        auto points=std::make_shared<PicaScenePointLights>();points->Count=1;
        points->Lights[0]={42,{0,0,distance-1},{.01F,.007F,0},250,2}; // deliberately faded candidate
        points->Lights[0].ReplacesNativeLocal=true;binding.ScenePoints=points;
        PicaEnvironmentLighting e;e.PointLights[2].Enabled=mode!=0;
        if(mode==4){e.PointLights[2].Intensity=.4F;e.PointLights[2].ColorTint={.5F,1,1};}
        if(mode==5)e.PointLights[2].RadiusScale=2;
        if(mode>=6) {
            e.Enabled=true;e.NativeAmbientStrength=.6F;e.PreserveSceneBrightness=false;
            e.KeyDirection=mode==6?PicaEnvironmentKeyDirection::Authored:PicaEnvironmentKeyDirection::SceneMain;
            points->Count=2;points->Lights[1]={43,{0,0,69},{.1F,.2F,.3F},250,3};
            points->Lights[1].IndependentEmission=true;
        }
        const auto replacement=BuildPicaNativePointReplacement(true,layout,binding,e,
            mode>=6?BuildPicaFragmentBalanceUniforms(true,layout,binding,e):std::nullopt);
        b.Control={1,8,0,0};b.Parameters[0]=1;b.Gains[0]={1,1,1,0};
        s.MaterialDiffuse={.498F,.498F,.498F,0};
        for(unsigned k=0;k<3;++k)s.ViewToWorldLinear[k][k]=1;
        if(replacement){
            b=replacement->Balance;s.Control={0,1,0,2};
            s.ScenePoints[0].PositionRadius={0,0,distance-1,250*e.PointLights[2].RadiusScale};
            s.ScenePoints[0].Color={.01F,.007F,0,0};
            if(mode>=6) {
                s.Control[1]=2;s.MaterialAmbient={1,1,1,0};
                s.ScenePoints[1].PositionRadius={0,0,69,250};
                s.ScenePoints[1].Color={.1F,.2F,.3F,-1};
                s.EnvironmentParameters={1,.6F,1,1};
                s.EnvironmentAmbient={.02F,.03F,.04F,0};
                s.EnvironmentDirections[0]={0,0,1,1};
                s.EnvironmentColors[0]={.03F,.04F,.05F,0};
            }
            FoldPicaSurfaceMaterialForPbr(s,true);
            PreparePicaFragmentProfileSurface(s,b,e);
            ApplyPicaNativePointReplacementControls(s,*replacement,e);
        }
        const float visibility=mode==2?0.F:mode==3?.5F:1.F;
        c.Parameters={distance,visibility,0,0};cases.push_back(c);
        const float nativeAtt=std::max(0.F,1.F-distance*.00399971008F);
        const float convertedAtt=std::max(0.F,1.F-distance*.00399971008F/(mode==5?2.F:1.F));
        Result want{};
        for(unsigned k=0;k<3;++k){
            const float diffuse=V4{86.F/255,60.F/255,0,0}[k],spec=V4{.06F,.03F,0,0}[k];
            const float factor=visibility*e.PointLights[2].Intensity*e.PointLights[2].ColorTint[k];
            want.Primary[k]=diffuse*convertedAtt*factor+V4{.01F,.02F,.03F,0}[k]*nativeAtt+V4{.04F,.05F,.06F,0}[k]*(mode>=6?.6F:1.F);
            if(mode>=6) want.Primary[k]+=V4{.02F,.03F,.04F,0}[k]+
                V4{.1F,.2F,.3F,0}[k]*.498F*(1.F-70.F*70.F/(250.F*250.F))+
                (mode==6?V4{.03F,.04F,.05F,0}[k]*.498F:0.F);
            want.Secondary[k]=spec*convertedAtt*factor;
        }
        want.Primary[3]=.37F;want.Secondary[3]=.63F;expected.push_back(want);
      }
    for (bool corrected : {false, true}) {
        Case c{};auto& s=c.Lighting.Surface;
        s.Control={0,0,2,0};
        s.SceneDirections[0]={0,0,1,0};s.SceneDirections[1]={0,0,-1,0};
        s.SceneDirectionalColors[0]={.2F,.1F,0,0};s.SceneDirectionalColors[1]={.4F,.3F,.2F,0};
        if (corrected) PreservePicaNativeFragmentSceneReference(s,true,true);
        c.Parameters={70,1,1,0};cases.push_back(c);
        expected.push_back({corrected?V4{.25F,.25F,.25F,.37F}:V4{.45F,.35F,.25F,.37F},V4{0,0,0,.63F}});
    }
    std::vector<Result> output(cases.size());
    RunComputeTest(shader,std::as_bytes(std::span(cases)),std::as_writable_bytes(std::span(output)),uint32_t(cases.size()));
    unsigned failed=0;
    for(size_t i=0;i<cases.size();++i)for(unsigned k=0;k<4;++k)
      if(std::abs(output[i].Primary[k]-expected[i].Primary[k])>2e-5F ||
         std::abs(output[i].Secondary[k]-expected[i].Secondary[k])>2e-5F ||
         !std::isfinite(output[i].Primary[k]) || !std::isfinite(output[i].Secondary[k])) {
          std::cerr<<"reference case="<<i<<" channel="<<k<<" primary="<<output[i].Primary[k]<<" expected="<<expected[i].Primary[k]<<'\n';++failed;
      }
    std::cout<<"Native point reference conservation: "<<cases.size()<<" GPU cases, failures="<<failed<<'\n';
    return failed?1:0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
