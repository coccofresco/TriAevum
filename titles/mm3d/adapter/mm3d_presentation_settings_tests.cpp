#include "mm3d_presentation_settings.h"
#include "../dekui/core.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <limits>
using namespace Oot3dNativeGame;
namespace {
void Check(bool v,const char* s){if(!v){std::cerr<<s<<'\n';std::exit(1);}}
void Near(float a,float b){Check(std::abs(a-b)<0.0001f,"projection value differs");}
struct Memory:Mm3dDekui::Memory {
 std::map<uint32_t,uint32_t> words;unsigned writes=0;
 std::optional<uint32_t> Read(uint32_t a)override{auto i=words.find(a);return i==words.end()?std::nullopt:std::optional<uint32_t>(i->second);}
 void Write(uint32_t a,uint32_t v)override{++writes;words[a]=v;}
};
struct Cpu:Mm3dDekui::Cpu {
 std::array<uint32_t,16> r{};std::array<uint32_t,64> s{};uint32_t flags=0;
 auto Regs()->std::array<uint32_t,16>& override{return r;}
 auto ExtRegs()->std::array<uint32_t,64>& override{return s;}
 uint32_t Cpsr()override{return flags;}void SetCpsr(uint32_t v)override{flags=v;}
};
}
int main(){
 namespace C=Mm3dDekui::CameraLive;
 const auto bits=[](float f){return std::bit_cast<uint32_t>(f);};
 for(float native:{30.f,45.f,60.f})Check(C::PresentedProjectionValue(bits(native),false,1,1)==bits(native),"Off exact replay");
 Near(std::bit_cast<float>(C::PresentedProjectionValue(bits(45.f),false,1.1f,1)),49.5f);
 Near(std::bit_cast<float>(C::PresentedProjectionValue(bits(45.f),true,1.f,1)),49.f);
 Near(std::bit_cast<float>(C::PresentedProjectionValue(bits(45.f),true,1.1f,1)),53.9f);
 const auto nan=bits(std::numeric_limits<float>::quiet_NaN());Check(C::PresentedProjectionValue(nan,false,1.1f,1)==nan,"invalid native preserved");
 ApplyMm3dPresentationSettings({1.1f,1.2f,1.f,true});
 Check(C::renderer_owned&&Mm3dDekui::Widescreen::Active(),"one product owner");
 Near(Mm3dDekui::ActorDrawDistance::multiplier,3.f);Near(Mm3dDekui::PropDrawDistance::multiplier,3.f);
 Near(Mm3dDekui::Widescreen::camera_zoom_out,1.f);Near(C::desired_fov,45.f);Near(C::desired_cutscene_fov,49.f);
 Memory m;Cpu cpu;Mm3dDekui::Core core;
 constexpr uint32_t P=0x08045B40,V=P+0x1F8;
 m.words={{C::CameraPointer,V},{P+0x13C,0x402},{P+0x4A8,P},{V+4,0},{V+8,400},{V+12,0},{V+16,240},{V+0x14,bits(400.f/240.f)},{V+0x18,bits(45.f)}};
 for(const auto& [pc,word]:C::SceneProjectionGuards)m.words[pc]=word;
 cpu.r[4]=V;cpu.s[1]=bits(400.f/240.f);cpu.s[2]=bits(80.f);cpu.s[3]=bits(1000.f);
 core.CallSvc(C::ProjectionSVC,cpu,m);
 Near(std::bit_cast<float>(cpu.s[0]),49.5f);Near(std::bit_cast<float>(cpu.s[1]),2.f);
 Check(m.words[V+0x14]==cpu.s[1]&&m.writes==1,"both native projection calls get picture aspect");
 Check(cpu.s[2]==bits(80.f)&&cpu.s[3]==bits(1000.f),"near/far preserved");
 ApplyMm3dPresentationSettings({});m.words[V+0x14]=bits(400.f/240.f);m.writes=0;
 core.CallSvc(C::ProjectionSVC,cpu,m);Check(cpu.s[0]==bits(45.f)&&!m.writes,"native settings preserve load and aspect");
 Near(Mm3dDekui::ActorDrawDistance::multiplier,1.f);Near(Mm3dDekui::PropDrawDistance::multiplier,1.f);
 Check(!Mm3dDekui::Widescreen::Active(),"native picture not widescreen");
 ApplyMm3dPresentationSettings({1.1f,1.2f,1.f,true});m.words[P+0x13C]=0x602;m.writes=0;cpu.s[1]=bits(400.f/240.f);
 core.CallSvc(C::ProjectionSVC,cpu,m);Check(cpu.s[0]==bits(45.f)&&cpu.s[1]==bits(400.f/240.f)&&!m.writes,"frontend retains native projection");
 m.words[P+0x13C]=0x402;
 for(const auto& [pc,word]:C::SceneProjectionGuards){
   m.words[pc]=word^1U;Check(!C::PrimaryGameplayProjection(m,V),"corrupt aspect consumer rejected");
   m.words.erase(pc);Check(!C::PrimaryGameplayProjection(m,V),"unmapped aspect consumer rejected");m.words[pc]=word;
 }
 std::cout<<"MM3D presentation ownership passed\n";
}
