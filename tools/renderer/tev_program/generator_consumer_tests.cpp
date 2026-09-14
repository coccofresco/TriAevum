#include "oot3d_native_pica_fragment_shader_gen.h"
#include <shaderc/shaderc.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <vector>

using namespace Oot3dNativeGame;
void Check(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
void CheckStaticTevSlots(const std::string& source) {
    // Dynamic local operand/sampler arrays caused a measured GPU regression.
    // Keep the register values dynamic, but lower their fixed slot domains offline.
    for (unsigned slot = 0; slot < 3; ++slot) {
        const auto index = std::to_string(slot);
        Check(source.find("pica_tev_input(words, stage, " + index + "u,") != std::string::npos,
              "TEV operand slot no longer lowered statically");
    }
    for (unsigned slot = 0; slot < 4; ++slot) {
        const auto index = std::to_string(slot);
        Check(source.find("pica_native_texture(" + index + "u)") != std::string::npos,
              "native texture unit no longer lowered statically");
    }
    for (const char* dynamicIndex : {"colors[input_index]", "alphas[input_index]",
                                    "inputs.textures[selector - 3u]", "pica_native_texture(texture_unit)"}) {
        Check(source.find(dynamicIndex) == std::string::npos,
              "dynamic indexing reintroduced into fixed TEV slots");
    }
}
void CheckUniformOffset(const shaderc::SpvCompilationResult& result, const char* name, uint32_t expected) {
    std::vector<uint32_t> words(result.cbegin(),result.cend());
    uint32_t type=0, member=0;
    for(size_t p=5;p<words.size();p+=words[p]>>16) {
        Check((words[p]>>16)>0,"invalid SPIR-V instruction");
        if((words[p]&0xffff)==6 && (words[p]>>16)>=4 &&
            std::strcmp(reinterpret_cast<const char*>(&words[p+3]),name)==0) {
            type=words[p+1]; member=words[p+2];
        }
    }
    bool matched=false;
    for(size_t p=5;p<words.size();p+=words[p]>>16)
        if((words[p]&0xffff)==72 && (words[p]>>16)==5 && words[p+1]==type &&
            words[p+2]==member && words[p+3]==35) {
            Check(words[p+4]==expected,"SPIR-V offset breaks uniform layout");
            matched=true;
        }
    Check(matched,"missing uniform member offset");
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
            if (n==0) {
                baseline=dynamic.Source;
                CheckStaticTevSlots(baseline);
            }
            Check(dynamic.Source==baseline,"TEV values changed parametric source");
            for (const auto* shader : {&dynamic,&specialized}) {
                auto result=compiler.CompileGlslToSpv(shader->Source,shaderc_fragment_shader,"consumer",options);
                Check(result.GetCompilationStatus()==shaderc_compilation_status_success,result.GetErrorMessage());
                if(shader==&dynamic) {
                    CheckUniformOffset(result,"tev_program",2112);
                    CheckUniformOffset(result,"lighting_program",2224);
                    CheckUniformOffset(result,"fragment_control",2480);
                    CheckUniformOffset(result,"proctex_program",2496);
                }
            }
        }
        for (unsigned lit=0;lit<2;++lit) for (uint8_t type : {0,2,3,5}) {
            packet->Registers[0x08F]=lit;
            packet->Registers[0x080]=1;
            packet->Registers[0x0E0]=5;
            packet->Registers[0x104]=0x31;
            state.Textures[0].Enabled=true;
            state.Textures[0].Type=type;
            packet->Registers[0x083]=uint32_t(type)<<28;
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
        // All structural lighting choices below must be uniform data, not
        // newly compiled source. Sampler/bump interfaces are held fixed.
        packet = std::make_unique<Oot3dPicaDrawPacket>();
        state = {};
        packet->Registers[0x08F] = 1;
        baseline.clear();
        unsigned lightingCases = 0;
        for (unsigned environment : {0,1,2,3,4,5,6,8})
        for (unsigned count=1; count<=8; ++count)
        for (unsigned input=0; input<6; ++input) {
            packet->Registers[0x1C2] = count-1;
            packet->Registers[0x1C3] = (environment<<4) | ((input&3)<<2) | ((input&1)<<27);
            packet->Registers[0x1C4] = input&1 ? 0x000000FF : 0xFF00;
            packet->Registers[0x1D9] = input&1 ? 0x76543210 : 0x01234567;
            packet->Registers[0x1D0] = input&1 ? 0x02222222 : 0;
            packet->Registers[0x1D1] = input*0x01111111;
            packet->Registers[0x1D2] = (input%4)*0x01111111;
            for(unsigned light=0;light<8;++light) packet->Registers[0x149+light*16]=(input+light)&15;
            packet->Registers[0xE0] = input&1 ? 0x10005 : 0;
            packet->Registers[0x104] = ((count-1)<<4) | (input&1);
            Oot3dPicaGeneratedFragmentShader dynamic, specialized;
            std::string error;
            Check(GenerateOot3dPicaFragmentShader(*packet,state,dynamic,&error,
                Oot3dPicaShaderBuildPurpose::OfflineSource,Oot3dPicaTevMode::Parametric),error);
            Check(GenerateOot3dPicaFragmentShader(*packet,state,specialized,&error,
                Oot3dPicaShaderBuildPurpose::OfflineSource),error);
            if(baseline.empty()) baseline=dynamic.Source;
            Check(dynamic.Source==baseline,"lighting/fog/alpha values changed parametric source");
            Check(dynamic.Hooks.Semantics==specialized.Hooks.Semantics,"lighting hooks changed");
            auto uniforms=BuildOot3dPicaFragmentUniformState(*packet);
            Check(uniforms.LightingProgram.Control[0]==count,"active lights lost");
            Check(uniforms.LightingProgram.Control[1]==environment,"environment lost");
            for(unsigned slot=0;slot<count;++slot) {
                auto expected=(packet->Registers[0x1D9]>>(slot*4))&7;
                Check(uniforms.LightingProgram.Lights[slot][0]==expected,"light permutation lost");
            }
            ++lightingCases;
        }
        auto lightingSpv=compiler.CompileGlslToSpv(baseline,shaderc_fragment_shader,"parametric_lights",options);
        Check(lightingSpv.GetCompilationStatus()==shaderc_compilation_status_success,lightingSpv.GetErrorMessage());
        std::cout << lightingCases << " lighting/fog/alpha configurations share one source\n";
        packet = std::make_unique<Oot3dPicaDrawPacket>();
        state = {};
        baseline.clear();
        unsigned textureCases=0;
        for(uint8_t type : {0,3,5,2}) for(unsigned mask=0;mask<8;++mask)
        for(unsigned coordinate=0;coordinate<2;++coordinate) for(unsigned sourceIndex=0;sourceIndex<4;++sourceIndex) {
            if(type==2 && (mask&1)!=0)continue; // active integer interface is a separate family
            packet->Registers[0x080]=mask|(coordinate<<13);
            packet->Registers[0x083]=uint32_t(type)<<28;
            state.Textures[0].Type=type;
            state.Texture2UsesCoordinate1=coordinate!=0;
            for(unsigned t=0;t<3;++t)state.Textures[t].Enabled=(mask&(1u<<t))!=0;
            for(auto base:bases)packet->Registers[base]=0x000F000F;
            packet->Registers[0xC0]=sourceIndex==3 ? 0x00000000 : (sourceIndex+3)*0x00010001;
            Oot3dPicaGeneratedFragmentShader dynamic,specialized;
            std::string error;
            Check(GenerateOot3dPicaFragmentShader(*packet,state,dynamic,&error,
                Oot3dPicaShaderBuildPurpose::RuntimeDraw,Oot3dPicaTevMode::Parametric),error);
            Check(GenerateOot3dPicaFragmentShader(*packet,state,specialized,&error),error);
            if(baseline.empty())baseline=dynamic.Source;
            Check(dynamic.Source==baseline,"texture selection changed float sampler source");
            Check(dynamic.Hooks.SampledTextureMask==specialized.Hooks.SampledTextureMask,"texture hooks changed");
            auto actual=dynamic.Uniforms.TevProgram.Control[1]&mask;
            if(type==5)actual&=~1u;
            Check(actual==specialized.Hooks.SampledTextureMask,"dynamic texture mask differs from native usage");
            ++textureCases;
        }
        auto textureSpv=compiler.CompileGlslToSpv(baseline,shaderc_fragment_shader,"texture_selection",options);
        Check(textureSpv.GetCompilationStatus()==shaderc_compilation_status_success,textureSpv.GetErrorMessage());
        std::cout << textureCases << " texture enable/reference/UV/type configurations share one source\n";
        packet = std::make_unique<Oot3dPicaDrawPacket>(); state={}; baseline.clear();
        for(unsigned n=0;n<120;++n) {
            packet->Registers[0x80]=(n%4)<<8 | ((n&1)<<10);
            packet->Registers[0xA8]=(n%5) | (((n/5)%5)<<3) | ((n%10)<<6) | (((n/3)%10)<<10) |
                ((n&1)<<14) | (((n/2)&1)<<15) | ((n%3)<<16) | (((n/3)%3)<<18);
            packet->Registers[0xAC]=(n%6) | (6<<7) | (128<<11);
            packet->Registers[0xC0]=0x60006;
            packet->ProcTexLuts.Color[n%256]=n*987654;
            Oot3dPicaGeneratedFragmentShader dynamic; std::string error;
            Check(GenerateOot3dPicaFragmentShader(*packet,state,dynamic,&error,
                Oot3dPicaShaderBuildPurpose::OfflineSource,Oot3dPicaTevMode::Parametric),error);
            if(baseline.empty())baseline=dynamic.Source;
            Check(dynamic.Source==baseline,"procedural registers/LUT changed source");
        }
        auto procSpv=compiler.CompileGlslToSpv(baseline,shaderc_fragment_shader,"procedural",options);
        Check(procSpv.GetCompilationStatus()==shaderc_compilation_status_success,procSpv.GetErrorMessage());
        std::cout << "120 procedural register/LUT configurations share one source\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
