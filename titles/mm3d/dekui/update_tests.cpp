// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic replay fixtures for the dekui 6e36dfb4 native gameplay slice.
// Contracts derived from tgacquin/mm3d-dekui tools/mm3d/*_check.cpp.
#include "core.h"
#include <fstream>
#include <iostream>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
using namespace Mm3dDekui;
namespace {
unsigned checks=0;
void Check(bool ok,const char* what) {++checks;if(!ok)throw std::runtime_error(what);}
struct Mem : Memory {
    std::map<U32,U32> words;
    std::optional<U32> Read(U32 at) override {auto i=words.find(at);return i==words.end()?std::nullopt:std::optional<U32>(i->second);}
    void Write(U32 at,U32 v) override {words[at]=v;}
};
struct CPU : Cpu {
    std::array<U32,16> r{};std::array<U32,64> s{};U32 flags=0;
    std::array<U32,16>& Regs() override{return r;}
    std::array<U32,64>& ExtRegs() override{return s;}
    U32 Cpsr() override{return flags;}void SetCpsr(U32 v) override{flags=v;}
    CPU(){for(unsigned i=0;i<16;++i)r[i]=0x9000000+i*4;for(unsigned i=0;i<64;++i)s[i]=0xabcd0000+i;flags=0xa1234567;}
};
void Tatl() {
    Core core;Mem m;constexpr U32 ctx=0x9000000;
    for(bool disabled:{false,true})for(U32 current:{0u,0x2au,0x2bu,0x34u})
        for(U32 request:{0u,0x2au,0x2bu,0x2cu,0x34u,0x35u}) {
            CPU c;c.r[0]=ctx;c.r[1]=request;const auto before=c;
            TatlReminders::disabled=disabled;m.words[ctx+0x8658]=0xbeef0000|current;
            const bool suppress=disabled&&TatlReminders::Regular(request);
            core.CallSvc(TatlReminders::SVC,c,m);
            auto expected=before.r;expected[4]=suppress?0:request;
            Check(c.r==expected&&c.s==before.s&&c.flags==before.flags,"Tatl register replay");
            Check(m.words[ctx+0x8658]==(suppress&&TatlReminders::Regular(current)?0xbeef0000:(0xbeef0000|current)),"queued alarm and upper half preserved");
        }
    TatlReminders::disabled=false;
}
void Bomber() {
    Core core;Mem m;constexpr U32 actor=0x9000000,state=0x9100000;
    // Source action address is independently guarded by its native callback slot.
    for(bool disabled:{false,true})for(bool same_action:{false,true})for(bool same_id:{false,true})
        for(U32 value:{0u,1u,2u,3u,4u,5u,0xffffu}) {
            m.words[actor]=same_id?0x4020f:0x40210;
            m.words[actor+0xb68]=same_action?BomberApproach::Guards[0].first:0x123456;
            m.words[state+0x8c]=0xcafe0000|value;
            BomberApproach::disabled=disabled;CPU c;c.r[4]=actor;c.r[5]=state;const auto before=c;
            const U32 original=U32(std::int32_t(std::int16_t(value)));
            auto expected=before.r;expected[0]=(disabled&&same_action&&same_id&&(value==2||value==3))?4:original;
            core.CallSvc(BomberApproach::FirstSVC,c,m);
            Check(c.r==expected&&c.s==before.s&&c.flags==before.flags,"Bomber state signed halfword replay/isolation");
            c=before;expected=before.r;
            if(disabled&&same_action&&same_id)expected[15]=BomberApproach::ResetPC;else expected[1]=15;
            const auto words=m.words;core.CallSvc(BomberApproach::FirstSVC+1,c,m);
            Check(c.r==expected&&c.s==before.s&&c.flags==before.flags&&m.words==words,"Bomber approach exact suppressed/native replay");
        }
    BomberApproach::disabled=false;
}
void Owl() {
    Core core;Mem m;
    for(bool disabled:{false,true}) {
        OwlIntro::disabled=disabled;CPU c;const auto before=c;
        core.CallSvc(OwlIntro::FirstSVC,c,m);auto expected=before.r;expected[4]=before.r[0];
        if(disabled){expected[14]=OwlIntro::IntroReturn;expected[15]=OwlIntro::Idle;}
        Check(c.r==expected&&c.s==before.s&&c.flags==before.flags,"owl native idle/pop continuation preserves stack");
        for(U32 flags=0;flags<256;++flags) {
            c=before;m.words[(c.r[0]+0x2d3)&~3u]=flags<<24;
            expected=before.r;expected[0]=disabled?flags|0x10:flags;
            core.CallSvc(OwlIntro::FirstSVC+1,c,m);
            Check(c.r==expected&&c.s==before.s&&c.flags==before.flags,"owl only the specific Tatl gate changes");
        }
    }
    OwlIntro::disabled=false;
}
void Defense() {
    Core core;Mem m;
    for(bool weaker:{false,true})for(U32 nzcv=0;nzcv<16;++nzcv)
        for(int damage:{-32768,-96,-16,-8,-3,-2,-1,0,1,16}) {
            CPU c;c.flags=(nzcv<<28)|0x1234567;c.r[1]=U32(damage);c.r[4]=0x123;const auto before=c;
            Difficulty::weaker_enhanced_defense=weaker;
            const bool lt=((nzcv>>3)&1)!=(nzcv&1);
            U32 delta=before.r[4];
            if(lt)delta=weaker&&damage<0?U32(-std::max(1,(-damage*2+1)/3)):U32(damage>>1);
            auto expected=before.r;expected[4]=delta;
            core.CallSvc(Difficulty::DefenseSVC,c,m);
            Check(c.r==expected&&c.s==before.s&&c.flags==before.flags,"defense condition/signed rounding/flags");
            if(lt&&damage<0)for(int multiplier=1;multiplier<=16;++multiplier) {
                const U32 result=Difficulty::ApplyDamage(320,delta,multiplier);
                Check(multiplier==1?result==320+delta:result<=320,"native 1x replay; amplified defense never wraps healing");
            }
        }
    Difficulty::weaker_enhanced_defense=false;
}
void DrawDistance() {
    Core core;Mem m;constexpr U32 actor=0x9000000;
    for(float factor:{1.f,1.5f,3.f})for(unsigned category=0;category<12;++category) {
        CPU c;c.r[4]=actor;c.s[16]=std::bit_cast<U32>(100.f);const auto before=c;
        m.words[actor]=(category<<16)|1;ActorDrawDistance::multiplier=factor;PropDrawDistance::multiplier=1.f;
        core.CallSvc(ActorDrawDistance::SVC,c,m);auto expected=before.s;
        expected[0]=std::bit_cast<U32>(100.f*((category==4||category==5)?factor:1.f));
        Check(c.r==before.r&&c.flags==before.flags&&c.s==expected,"actor category override affects only S0");
    }
    for(unsigned index=0;index<PropDrawDistance::Hooks.size();++index)for(float factor:{1.f,2.f,3.f}) {
        CPU c;const auto before=c;PropDrawDistance::multiplier=factor;const auto& h=PropDrawDistance::Hooks[index];
        core.CallSvc(PropDrawDistance::FirstSVC+index,c,m);
        auto r=before.r;auto s=before.s;const U32 value=std::bit_cast<U32>(h.value+1200.f*(factor-1));
        (h.floating?s[h.reg]:r[h.reg])=value;
        Check(c.r==r&&c.s==s&&c.flags==before.flags,"prop displaced integer/VFP constant load");
    }
    for(U32 header:{0x60065u,0x6010au,0x60078u,0x6007cu,0x1011bu})Check(PropDrawDistance::Eligible(header),"specific prop profile");
    for(U32 header:{0x60001u,0x50065u,0x6011bu})Check(!PropDrawDistance::Eligible(header),"other props unchanged");
    for(unsigned index=0;index<VegetationDrawDistance::Hooks.size();++index)
        for(float factor:{1.f,2.f,3.f})for(bool cluster:{false,true}) {
            const auto& h=VegetationDrawDistance::Hooks[index];CPU c;
            c.r[1]=0x9200000;c.r[4]=actor;m.words[c.r[1]]=std::bit_cast<U32>(400.f);
            m.words[actor]=cluster?0x6007f:0x60001;m.words[actor+0x1c]=1;
            const auto before=c;PropDrawDistance::multiplier=factor;
            core.CallSvc(VegetationDrawDistance::FirstSVC+index,c,m);
            auto r=before.r;auto s=before.s;U32 value;
            if(h.distance!=0) {
                const float distance=h.distance+h.shift_base*(factor-1);
                value=std::bit_cast<U32>(h.squared?distance*distance:distance);
                if(h.negate)value=0-value;
            } else value=std::bit_cast<U32>(400.f*(cluster?factor*factor:1.f));
            (h.floating?s[h.reg]:r[h.reg])=value;
            Check(c.r==r&&c.s==s&&c.flags==before.flags,"vegetation exact native load/squared radius/profile isolation");
        }
    ActorDrawDistance::multiplier=PropDrawDistance::multiplier=1.f;
}
void MusicBeep() {
    Mem m;constexpr U32 sound=0x9000000,command=0x9100000;
    for(bool muted:{false,true})for(bool beep:{false,true})for(U32 id:{Music::LowHealthAlarm,0x10007dfu,0x123u}) {
        Music::muted=muted;Music::disable_low_health_beep=beep;m.words[sound+0xac]=id;
        const U32 native=0x3f123456;Music::Store(m,sound,command,native);
        Check(m.words[command+0x14]==((beep&&id==Music::LowHealthAlarm)?0:native),"low-health beep versus ordinary sounds");
    }
    Music::muted=false;Music::disable_low_health_beep=false;
}
void CameraFov() {
    Mem m;constexpr U32 projection=0x9000000,context=0x9100000,game=0x9200000,camera=game+0x408;
    m.words[CameraLive::CameraPointer]=projection;
    m.words[projection+4]=0;m.words[projection+8]=400;m.words[projection+12]=0;m.words[projection+16]=240;
    m.words[0x72f5c0]=context;m.words[context+0x16c]=game;m.words[game+0x13c]=0x0402;
    m.words[game+0xa78]=0;m.words[camera+0xa0]=game;
    CameraLive::SetFov(65);CameraLive::SetCutsceneFov(49);
    for(U32 setting:{0u,8u,9u,10u,11u}) {
        m.words[camera+0x154]=setting<<16;
        Check(CameraLive::ConfiguredFov(m,projection)==((setting==9||setting==10)?49.f:65.f),"scripted FOV only for verified active camera settings");
        Check(CameraLive::ConfiguredFov(m,projection+4)==65.f,"other projections retain gameplay FOV selection");
    }
    CameraLive::SetCutsceneFov(100);Check(CameraLive::desired_cutscene_fov==70,"scripted FOV upper bound");
    CameraLive::SetCutsceneFov(0);Check(CameraLive::desired_cutscene_fov==45,"scripted FOV lower bound");
    CameraLive::SetCutsceneFov(std::numeric_limits<float>::quiet_NaN());
    Check(CameraLive::desired_cutscene_fov==49,"scripted FOV finite fallback");
    CameraLive::Stop();Check(CameraLive::desired_cutscene_fov==45&&CameraLive::desired_fov==45,"camera Stop resets both configurable FOVs");
}
}
int main() {
    try {Tatl();Bomber();Owl();Defense();DrawDistance();MusicBeep();CameraFov();std::cout<<"PASS "<<checks<<" update replay checks\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
