#include "oot3d_native_pica_fragment_shader_gen.h"
#include <shaderc/shaderc.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <vector>

using namespace Oot3dNativeGame;
void Check(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
void CheckTevOffset(const shaderc::SpvCompilationResult& result) {
    std::vector<uint32_t> words(result.cbegin(),result.cend());
    uint32_t type=0, member=0;
    for(size_t p=5;p<words.size();p+=words[p]>>16) {
        Check((words[p]>>16)>0,"invalid SPIR-V instruction");
        if((words[p]&0xffff)==6 && (words[p]>>16)>=4 &&
            std::strcmp(reinterpret_cast<const char*>(&words[p+3]),"tev_program")==0) {
            type=words[p+1]; member=words[p+2];
        }
    }
    bool matched=false;
    for(size_t p=5;p<words.size();p+=words[p]>>16)
        if((words[p]&0xffff)==72 && (words[p]>>16)==5 && words[p+1]==type &&
            words[p+2]==member && words[p+3]==35) {
            Check(words[p+4]==2112,"SPIR-V TEV offset breaks legacy uniform prefix");
            matched=true;
        }
    Check(matched,"missing TEV uniform member offset");
}
int main() {
    try {
        auto packet = std::make_unique<Oot3dPicaDrawPacket>();
        Oot3dPicaDecodedDrawState state{};
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
        constexpr unsigned bases[]{0xC0,0xC8,0xD0,0xD8,0xF0,0xF8};
        std::string baseline;
        for (unsigned n=0;n<16;++n) {
            for (auto base : bases) {
                packet->Registers[base] = 0x000E000E;
                packet->Registers[base+1] = (n&1) ? 0x1001 : 0;
                packet->Registers[base+2] = (n%6) | ((n%6)<<16);
                packet->Registers[base+4] = (n%3) | ((n%3)<<16);
            }
            packet->Registers[0xE0] = (n&1) ? 0xFF00 : 0;
            Oot3dPicaGeneratedFragmentShader dynamic, specialized;
            std::string error;
            Check(GenerateOot3dPicaFragmentShader(*packet,state,dynamic,&error,
                Oot3dPicaShaderBuildPurpose::RuntimeDraw,Oot3dPicaTevMode::Parametric),error);
            Check(GenerateOot3dPicaFragmentShader(*packet,state,specialized,&error),error);
            Check(dynamic.StateKey != specialized.StateKey,"mode key collision");
            Check(dynamic.Source.find("tev_inputs.primary = rounded_primary_color") != std::string::npos,
                  "authorized primary lighting hook bypassed");
            Check(dynamic.Uniforms.TevProgram.Stages[0][2] == packet->Registers[0xC2],"draw program lost");
            Check(dynamic.Hooks.SampledTextureMask == specialized.Hooks.SampledTextureMask,"texture hooks changed");
            Check(dynamic.Hooks.Semantics == specialized.Hooks.Semantics,"effect semantics changed");
            if (n==0) baseline=dynamic.Source;
            Check(dynamic.Source==baseline,"TEV values changed parametric source");
            for (const auto* shader : {&dynamic,&specialized}) {
                auto result=compiler.CompileGlslToSpv(shader->Source,shaderc_fragment_shader,"consumer",options);
                Check(result.GetCompilationStatus()==shaderc_compilation_status_success,result.GetErrorMessage());
                if(shader==&dynamic) CheckTevOffset(result);
            }
        }
        for (unsigned lit=0;lit<2;++lit) for (uint8_t type : {0,2,3,5}) {
            packet->Registers[0x08F]=lit;
            packet->Registers[0x080]=1;
            packet->Registers[0x0E0]=5;
            packet->Registers[0x104]=0x31;
            state.Textures[0].Enabled=true;
            state.Textures[0].Type=type;
            state.Textures[0].Width=64;
            state.Textures[0].Height=64;
            for(auto base:bases) {
                packet->Registers[base]=0x00030003;
                packet->Registers[base+1]=0;
                packet->Registers[base+2]=0;
                packet->Registers[base+4]=0;
            }
            for(auto mode:{Oot3dPicaTevMode::Specialized,Oot3dPicaTevMode::Parametric}) {
                Oot3dPicaGeneratedFragmentShader shader;
                std::string error;
                Check(GenerateOot3dPicaFragmentShader(*packet,state,shader,&error,
                    Oot3dPicaShaderBuildPurpose::OfflineSource,mode),error);
                auto result=compiler.CompileGlslToSpv(shader.Source,shaderc_fragment_shader,"sampler_lighting",options);
                Check(result.GetCompilationStatus()==shaderc_compilation_status_success,result.GetErrorMessage());
            }
        }
        std::cout << "16 stable-source material programs; 8 sampler/lighting/fog/alpha cases; 48 SPIR-V compilations passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
