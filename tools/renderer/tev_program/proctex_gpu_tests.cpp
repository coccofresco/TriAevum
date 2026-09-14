#include "vulkan_compute_test.h"
#include "oot3d_native_pica_proctex.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <iomanip>

using namespace Oot3dNativeGame;
using namespace Fast::Renderer3ds;
int main() {
    try {
        {
            PicaProcTexProgram dummy;
            dummy.Registers[0][0]=1024;
            dummy.Registers[1][1]=5|(7<<3)|(7<<7)|(128<<11)|(0x3C<<19);
            dummy.Lut[(384+254)/4][(384+254)%4]=0x80402010;
            std::vector<std::array<float,4>> decoded(4096);
            const std::string source="#version 450\n"+std::string(PicaProcTexDeclaration)+R"glsl(
layout(set=0,binding=0,std430) readonly buffer Input {PicaProcTexProgram proctex_program;} fragment_uniforms;
layout(set=0,binding=1,std430) writeonly buffer Output {vec4 results[];};
)glsl"+std::string(PicaProcTexCode)+R"glsl(
layout(local_size_x=1) in; void main(){uint n=gl_GlobalInvocationID.x;
vec4 endpoint=pica_proctex_evaluate(vec2(0.25,0.75),vec2(1.0));
results[n]=vec4(pt_unorm(n&255u,8u),pt_unorm(n,12u),endpoint.r,endpoint.a);}
)glsl";
            RunComputeTest(source,std::as_bytes(std::span(&dummy,1)),std::as_writable_bytes(std::span(decoded)),4096);
            for(unsigned n=0;n<4096;++n) {
                Require(decoded[n][0]==float(n&255)/255.0F,"8-bit UNORM decode differs");
                Require(decoded[n][1]==float(n)/4095.0F,"12-bit UNORM decode differs");
                Require(decoded[n][2]==16.0F/255.0F && decoded[n][3]==128.0F/255.0F,"terminal mip must not sample nonexistent level 8");
            }
            std::cout<<"All 8-bit and 12-bit UNORM values match CPU decode exactly\n";
        }
        constexpr unsigned configs=6, samples=32;
        unsigned failures=0, byteFailures=0;float maximum=0;
        for(unsigned batch=0;batch<10;++batch) {
        std::vector<PicaProcTexProgram> inputs(configs);
        std::vector<std::array<float,4>> outputs(configs*samples*2);
        auto packet=std::make_unique<Oot3dPicaDrawPacket>();
        std::ostringstream canonical,dispatch;
        const char* symbols[]={"pica_proctex_value_lut","pica_proctex_color_lut","pica_proctex_color_diff_lut",
            "pica_proctex_lookup_value","pica_proctex_noise_f","pica_proctex_noise_a","pica_proctex_noise_p",
            "pica_proctex_noise_rand_1d","pica_proctex_noise_rand_2d","pica_proctex_noise_coef",
            "pica_sample_proctex_color","pica_sample_proctex"};
        for(unsigned local=0;local<configs;++local) {
            unsigned n=batch*configs+local;
            packet->Registers[0x80]=1024;
            packet->Registers[0xA8]=(n%5)|(((n/5)%5)<<3)|((n%10)<<6)|(((n/3)%10)<<10)|
                ((n&1)<<14)|(((n/6)&1)<<15)|((n%3)<<16)|(((n/3)%3)<<18);
            // Zero, positive, negative and fractional native biases; signed noise.
            packet->Registers[0xA9]=0x340003FF;packet->Registers[0xAA]=0xB000FC01;
            packet->Registers[0xAB]=0x38003C00;
            constexpr unsigned biases[]{0,0x3C00,0xBC00,0x3800};
            packet->Registers[0xA8]|=(biases[n%4]&255)<<20;
            packet->Registers[0xAC]=(n%6)|(6<<7)|(128<<11)|((biases[n%4]>>8)<<19);
            packet->Registers[0xAD]=0xE0C08000;
            for(unsigned i=0;i<128;++i) {
                packet->ProcTexLuts.Noise[i]=((i*31+n)%4096)|(((i&1)?4090:11)<<12);
                packet->ProcTexLuts.ColorMap[i]=((i*29+n*13)%4096)|(((i&1)?4081:17)<<12);
                packet->ProcTexLuts.AlphaMap[i]=((i*19+n*7)%4096)|(((i&1)?4071:23)<<12);
            }
            for(unsigned i=0;i<256;++i){packet->ProcTexLuts.Color[i]=i*9876543u+n*1234567u;packet->ProcTexLuts.ColorDifference[i]=i*11111111u;}
            inputs[local]=BuildOot3dPicaProcTexProgram(*packet);
            std::string source,error;
            Require(GenerateOot3dPicaProceduralTextureSampler(*packet,source,&error),error.c_str());
            // Namespacing the unmodified canonical emitter is test-only.
            for(auto symbol:symbols)canonical<<"#define "<<symbol<<' '<<symbol<<'_'<<n<<'\n';
            canonical<<source;
            for(auto symbol:symbols)canonical<<"#undef "<<symbol<<'\n';
            dispatch<<"case "<<local<<"u: expected=pica_sample_proctex_"<<n<<"();break;\n";
        }
        const std::string shader="#version 450\n"+std::string(PicaProcTexDeclaration)+R"glsl(
struct Uniforms { PicaProcTexProgram proctex_program; };
layout(set=0,binding=0,std430) readonly buffer Input {Uniforms programs[];};
layout(set=0,binding=1,std430) writeonly buffer Output {vec4 results[];};
#define fragment_uniforms programs[gl_GlobalInvocationID.x/32u]
vec2 test_uv,test_duv;
#define pica_texcoord0 test_uv
#define dFdx(x) test_duv
#define dFdy(x) test_duv
)glsl"+canonical.str()+std::string(PicaProcTexCode)+R"glsl(
layout(local_size_x=1) in;
void main() {
    uint i=gl_GlobalInvocationID.x, cfg=i/32u, sample_index=i%32u;
    test_uv=vec2(float(sample_index)*0.173-1.5,float(sample_index)*0.279-0.5);
    test_duv=vec2(exp2(float(sample_index%16u)*0.43-9.0));
    vec4 expected;
    switch(cfg) {
)glsl"+dispatch.str()+R"glsl(
    }
    results[i*2u]=expected;
    results[i*2u+1u]=pica_proctex_evaluate(test_uv,test_duv);
}
)glsl";
        RunComputeTest(shader,std::as_bytes(std::span(inputs)),std::as_writable_bytes(std::span(outputs)),configs*samples);
        for(unsigned i=0;i<configs*samples;++i)for(unsigned c=0;c<4;++c){
            float a=outputs[i*2][c],b=outputs[i*2+1][c],error=std::abs(a-b);
            maximum=std::max(maximum,error);
            // Retain the lighting differential's float tolerance, and additionally
            // require identical clamped 8-bit output for these samples.
            if(!std::isfinite(a)||!std::isfinite(b)||error>1e-5F){
                if(failures<8)std::cerr<<"case "<<i<<" channel "<<c<<" expected "<<a<<" got "<<b<<'\n';
                ++failures;
            }
            const auto byte=[](float v){return std::lround(std::clamp(v,0.0F,1.0F)*255.0F);};
            if(std::isfinite(a)&&std::isfinite(b)&&byte(a)!=byte(b)) {
                std::cerr<<std::setprecision(10)<<"byte mismatch config "<<batch*configs+i/samples<<" sample "<<i%samples
                    <<" channel "<<c<<" expected "<<a<<" got "<<b<<'\n';
                ++byteFailures;
            }
        }
        std::cout<<"batch "<<batch<<" complete\n"<<std::flush;
        }
        std::cout<<configs*10<<" configurations, "<<configs*samples*10<<" samples, max error "<<maximum<<", float failures "<<failures<<", byte failures "<<byteFailures<<'\n';
        Require(failures==0 && byteFailures==0,"procedural GPU differential failed");
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
