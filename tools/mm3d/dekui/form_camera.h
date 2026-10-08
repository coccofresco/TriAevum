// Ported from mm3d-dekui src/mm3d/form_camera.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "camera_live.h"

namespace Mm3dDekui::FormCamera {
inline std::atomic<bool> diagnostics{false};
using CameraLive::U32;
inline bool SupportedForm(U32 form){return form==1||form==2||form==4;}
// Restoration v110 free-camera radius smoothing, before native collision.
constexpr U32 PC=0x0046ffc4,Word=0xedd40a3c,SVC=0xffffb2; // ref 0x46fe6c
constexpr U32 EntryPC=0x0046fc30,EntryWord=0xe1a04000,EntrySVC=0xffffb1; // ref 0x46fad8
constexpr U32 RadiusPC=0x004700a0,RadiusWord=0xe1a0200d,RadiusSVC=0xffffb0; // ref 0x46ff48
constexpr U32 LockPC=0x007cf16c,LockWord=0xe2413009,LockSVC=0xffffaf; // ref 0x7cf16c (Restoration source port)
inline U32 Instruction(U32 pc,U32 word){
    if(pc==PC&&word==Word)return 0xef000000|SVC;
    if(pc==EntryPC&&word==EntryWord)return 0xef000000|EntrySVC;
    if(pc==RadiusPC&&word==RadiusWord)return 0xef000000|RadiusSVC;
    if(pc==LockPC&&word==LockWord)return 0xef000000|LockSVC;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    constexpr std::array<U32,8> code{0xeeb40ac9,0xeef1fa10,0xeef01a4b,0xeeb01a6a,
        0x3eb00a49,Word,0xeb02a152,0xed8d0a00}; // ref 0xeb02a14c
    for(unsigned i=0;i<code.size();++i)if(m.Read(0x0046ffb0+4*i)!=code[i])return false; // ref 0x46fe58
    for(auto [at,word]:std::array<std::pair<U32,U32>,7>{{
        {0x0046fc2c,0xe92d4ff0},{EntryPC,EntryWord},{0x0046fc34,0xe2805074}, // ref 0x46fad4, 0x46fadc
        {RadiusPC,RadiusWord},{0x004700a4,0xe1a0100a},{0x004700a8,0xe28d0044}, // ref 0x46ff4c, 0x46ff50
        {0x004700ac,0xebf5007a}}}) // ref 0x46ff54, 0xebf50150, 0x7cf188 (Restoration source port)
        if(m.Read(at)!=word)return false;
    if constexpr(requires { m.RestorationCameraPort(); })
        if(m.RestorationCameraPort())return true;
    if(m.Read(LockPC)!=LockWord||m.Read(0x007cf188)!=0xe12fff1e)return false;
    return CameraLive::ChangeModeCodeMatches(m)&&CameraLive::DialogueCameraCodeMatches(m);
}
struct Hook {
    U32 camera=0,player=0,form=0;
    float radius=0;
    bool saved=false,release_target=false;
    template<class M> std::optional<std::array<U32,2>> Target(M& m,U32 cam,bool include_deku=false) {
        auto context=m.Read(0x72f5c0);if(!context||!*context)return {};
        auto game=m.Read(*context+0x16c);if(!game||!*game)return {};
        auto type=m.Read(*game+0x13c),indices=m.Read(*game+0xa78);
        if(!type||((*type>>8)&255)!=4||!indices||(*indices>>16)>3||
           cam!=*game+0x408+(*indices>>16)*0x198||m.Read(cam+0xa0)!=game)return {};
        auto actor=m.Read(*game+0x20dc);if(!actor||!*actor||m.Read(cam+0xa4)!=actor)return {};
        auto id=m.Read(*actor),f=m.Read(*actor+0x1fc);
        if(!id||(*id&0xffffff)!=0x020000||!f||(!SupportedForm(*f>>24)&&!(include_deku&&(*f>>24)==3)))return {};
        return std::array<U32,2>{*actor,*f>>24};
    }
    template<class M> U32 RequestedMode(M& m,U32 cam,U32 requested) {
        if(requested!=31)return requested;
        // Restoration CalcCamera adds FREECAMERA to the ocarina's ITEM1
        // subcamera. Keep its native NORMAL mode while the instrument is out.
        // This uses camera ownership/state, not pad filtering, so note input
        // and the main gameplay camera remain untouched.
        const auto context=m.Read(0x72f5c0);
        const auto game=context&&*context?m.Read(*context+0x16c):std::optional<U32>{};
        if(game&&*game&&cam==*game+0x408+0x198&&m.Read(cam+0xa0)==game) {
            const auto type=m.Read(*game+0x13c),indices=m.Read(*game+0xa78),state=m.Read(cam+0x154);
            if(type&&((*type>>8)&255)==4&&indices&&(*indices>>16)==1&&
               state&&(*state>>16)==0x13)return 0;
        }
        const auto target=Target(m,cam);if(!target)return requested;
        const auto mode=m.Read(cam+0x158),flags=m.Read(cam+0x170),enemy=m.Read(cam+0xbc);
        // A fresh enemy lock can be requested as FREECAMERA by the native
        // caller when already orbiting. Route it through native BATTLE setup
        // and retain BATTLE while the stick is held during that lock, otherwise
        // modes 14/31 alternate and restart the targeting sound every other frame.
        // NORMAL must still release the stale target flag in Entry.
        if(!mode||((*mode&65535)!=31&&(*mode&65535)!=14)||
           !flags||!(*flags&8)||!enemy||!*enemy)return requested;
        // Capture confirms Flag1::ZTargeting (0x8000), not 0x10000, stays
        // set during enemy lock-on. Both it and Player::target_actor clear on
        // release while the camera's own target/bit3 can still be stale.
        const auto player_flags=m.Read((*target)[0]+0x11db0);
        const auto player_target=m.Read((*target)[0]+0xdfc);
        if(!player_flags||!(*player_flags&0x8000)||player_target!=enemy)return requested;
        const auto actor=m.Read(*enemy);if(!actor)return requested;
        const U32 category=(*actor>>16)&255;
        return category==5||category==9?14u:requested;
    }
    template<class M> void ChangeMode(M& m,U32 cam,U32 requested) {
        if(cam==camera&&requested!=31)*this={};
        // Ordinary mode has already released lock-on, but Camera+170 bit3
        // can survive until the end of the next update. FREECAMERA reads it
        // earlier and otherwise aims at the old +BC target for one frame.
        if(const auto target=Target(m,cam,true);requested==31&&target) {
            const auto mode=m.Read(cam+0x158);
            const auto player_flags=m.Read((*target)[0]+0x11db0);
            const auto player_target=m.Read((*target)[0]+0xdfc);
            // With the stick held, native requests go straight from battle to
            // free camera, skipping NORMAL. Apply the same stale-aim cleanup.
            release_target=mode&&((*mode&65535)==0||((*mode&65535)==14&&
                player_flags&&!(*player_flags&0x8000)&&player_target&&!*player_target)||
                ((*mode&65535)==11&&player_target&&!*player_target));
        }
    }
    template<class M,class R> void Entry(M& m,R& regs,bool native_orbit=false) {
        const U32 cam=regs[0];regs[4]=cam; // displaced MOV
        const auto target=Target(m,cam,true);auto mode=m.Read(cam+0x158),phase=m.Read(cam+0x170);
        if(!target||!mode||(*mode&65535)!=31||!phase){*this={};return;}
        if(release_target&&(*phase&8))m.Write(cam+0x170,*phase&~8u);
        release_target=false;
        // Deku shares stale lock-on flags (e.g. the Clock Town dog), but its
        // native walking radius and separate flower-flight orbit stay intact.
        if(!SupportedForm((*target)[1])){*this={};return;}
        // A door/dialogue/first-person takeover inherits a temporary close orbit. Let native
        // FREECAMERA recover it instead of capturing it as a permanent radius.
        if(native_orbit){*this={};return;}
        if(saved&&camera==cam&&player==(*target)[0]&&form==(*target)[1]&&(*phase>>16)!=0)return;
        *this={};
        // Use the orbit already visible at handler entry, before either native
        // free-camera branch changes its target/radius. +74 is the desired eye;
        // +5C is the wall-adjusted eye, which must not become the orbit baseline.
        float squared=0;
        for(unsigned axis=0;axis<3;++axis){
            auto eye=m.Read(cam+0x74+4*axis),at=m.Read(cam+0x50+4*axis);
            if(!eye||!at)return;
            const float d=std::bit_cast<float>(*eye)-std::bit_cast<float>(*at);
            if(!std::isfinite(d))return;squared+=d*d;
        }
        const float distance=std::sqrt(squared);
        if(!std::isfinite(distance)||distance<30||distance>1000)return;
        camera=cam;player=(*target)[0];form=(*target)[1];radius=distance;saved=true;
    }
    template<class M,class R> void Radius(M& m,R& regs) {
        regs[2]=regs[13]; // displaced MOV r2,sp
        const auto target=Target(m,regs[4]);auto mode=m.Read(regs[4]+0x158);
        if(!target||!mode||(*mode&65535)!=31||!saved||camera!=regs[4]||
           player!=(*target)[0]||form!=(*target)[1])return;
        // Both native radius branches meet here. Only replace the radius in
        // their temporary spherical vector, before eye reconstruction/collision.
        const auto old=m.Read(regs[13]);if(!old||!std::isfinite(std::bit_cast<float>(*old)))return;
        m.Write(regs[13],std::bit_cast<U32>(radius));
    }
    template<class M,class R> void LockMode(M& m,R& regs) {
        // Restoration normally preserves FREECAMERA even for KEEPON. A real
        // target request must reach native ChangeMode for its bars/sound/state.
        if(regs[1]==9&&Target(m,regs[0])){regs[0]=0;regs[15]=regs[14];return;}
        regs[3]=regs[1]-9; // displaced SUB, without flag changes
    }
    template<class M,class R> void Distance(M& m,U32 cam,R& floats) {
        // Diagnostic-only: exact replay of VLDR s1,[r4,#0xf0].
        if(auto bits=m.Read(cam+0xf0))floats[1]=*bits;
    }
};
// Finish an interrupted NORMAL arrival at the actual FREECAMERA handler,
// after subcamera handover. Keep its settled orbit for this free-camera session.
struct ArrivalLens {
    U32 camera=0,player=0,form=0,setting=0,last_bits=0;
    float target=0,radius=0,radius_target=0;
    bool active=false,close_return=false;
    template<class M> std::optional<U32> NormalParams(M& m,U32 state) {
        if(state==0||state>=0x5b)return {};
        const auto modes=m.Read(0x667894+state*12+8);
        if(!modes||!*modes)return {};
        const auto info=m.Read(*modes),params=m.Read(*modes+4);
        // NORMAL1/NORMAL3: ten tagged parameters, maximum radius at2, FOV at7.
        if(!info||(*info!=0xa0002&&*info!=0xa0018)||!params||!*params)return {};
        return params;
    }
    template<class M> std::optional<float> NormalFov(M& m,U32 state) {
        const auto params=NormalParams(m,state);if(!params)return {};
        const auto fov=m.Read(*params+7*4);
        if(!fov||(*fov>>16)!=7)return {};
        const float value=float(*fov&65535);
        if(value<30||value>=60)return {};
        return value;
    }
    template<class M> std::optional<float> NormalRadius(M& m,U32 state,U32 actor,U32 f) {
        const auto params=NormalParams(m,state);if(!params)return {};
        const auto maximum=m.Read(*params+8),flags1=m.Read(actor+0x11db0),flags3=m.Read(actor+0x11db8);
        if(!maximum||(*maximum>>16)!=2||!flags1||!flags3)return {};
        // Native Player_GetHeight (Restoration1F2E1C), then NORMAL1/3 scaling:
        // height * .01 * (.8 - (68 / height) * -.2).
        float height=f==1?((*flags3&0x1000)?34.f:80.f):f==2?74.f:f==3?36.f:f==4?44.f:0.f;
        if(height==0||(*flags1&0x00800000))return {}; // mounted camera is separate // ref 0x800000 (Restoration source port)
        const float scale=height*.01f*(.8f-(68.f/height)*-.2f);
        const float result=float(*maximum&65535)*scale;
        if(result<30||result>1000)return {};
        return result;
    }
    template<class M> void Observe(M& m,U32 cam,U32 requested) {
        if(camera==cam&&requested!=31)*this={};
        if(requested!=31||active||close_return)return;
        Hook validator;const auto identity=validator.Target(m,cam,true);
        const auto game=m.Read(cam+0xa0),mode=m.Read(cam+0x158),state=m.Read(cam+0x154);
        const auto bits=m.Read(cam+0x110);
        if(!identity||!game||cam!=*game+0x408||!mode||!state||!bits)return;
        const U32 previous=*mode&65535;
        if(previous!=0&&previous!=10&&previous!=11)return;
        const auto normal=NormalFov(m,*state>>16);
        const auto enemy=m.Read((*identity)[0]+0xdfc);
        const float current=std::bit_cast<float>(*bits);
        if(!normal||!enemy||*enemy||!std::isfinite(current)||current<30||current>=*normal-.1f)return;
        camera=cam;player=(*identity)[0];form=(*identity)[1];close_return=true;
    }
    template<class M> void Step(M& m,U32 cam) {
        Hook validator;const auto identity=validator.Target(m,cam,true);
        const auto mode=m.Read(cam+0x158),state=m.Read(cam+0x154),bits=m.Read(cam+0x110),phase=m.Read(cam+0x170);
        if(!identity||!mode||(*mode&65535)!=31||!state||!bits||!phase){*this={};return;}
        const auto normal=NormalFov(m,*state>>16);
        const auto maximum=NormalRadius(m,*state>>16,(*identity)[0],(*identity)[1]);
        if(!normal||!maximum){*this={};return;}
        if((*identity)[1]==3) {
            const auto flags=m.Read((*identity)[0]+0x11db8);
            if(!flags||(*flags&0x2300)){*this={};return;}
        }
        const float current=std::bit_cast<float>(*bits);
        if(!std::isfinite(current)||current<(close_return?30.f:*normal)||current>60.01f){*this={};return;}
        if(close_return&&(camera!=cam||player!=(*identity)[0]||form!=(*identity)[1]||
           m.Read(player+0xdfc)!=std::optional<U32>(0)||(*phase&8))){*this={};return;}
        if(active&&(camera!=cam||player!=(*identity)[0]||form!=(*identity)[1]||
           target!=*normal||last_bits!=*bits)){*this={};return;}
        if(!active) {
            // First native FREECAMERA update, not merely its earlier request.
            // Never turn an ordinary settled camera or flower flight into an arrival.
            if((*phase>>16)!=0||(*phase&8)||(!close_return&&current<=*normal+.1f))return;
            float squared=0;
            for(unsigned axis=0;axis<3;++axis) {
                const auto eye=m.Read(cam+0x74+axis*4),at=m.Read(cam+0x50+axis*4);
                if(!eye||!at)return;
                const float d=std::bit_cast<float>(*eye)-std::bit_cast<float>(*at);
                if(!std::isfinite(d))return;squared+=d*d;
            }
            const float distance=std::sqrt(squared);
            if(!std::isfinite(distance)||distance<30||distance>1000)return;
            camera=cam;player=(*identity)[0];form=(*identity)[1];target=*normal;
            radius=distance;radius_target=std::min(distance,*maximum);active=true;
        }
        setting=*state>>16;
        // The uninterrupted capture settles5% per game update. Reusing +E8
        // froze its startup value at.0158, making early takeover recover too slowly.
        const auto ease=[](float from,float to){return std::abs(from-to)<.1f?to:from+(to-from)*.05f;};
        const float next=ease(current,target);radius=ease(radius,radius_target);
        last_bits=std::bit_cast<U32>(next);m.Write(cam+0x110,last_bits);
    }
    template<class M,class R> void Radius(M& m,R& regs,Hook& orbit) {
        if(!active||camera!=regs[4])return;
        Hook validator;const auto identity=validator.Target(m,camera,true);
        const auto mode=m.Read(camera+0x158),old=m.Read(regs[13]);
        if(!identity||(*identity)[0]!=player||(*identity)[1]!=form||!mode||
           (*mode&65535)!=31||!old||!std::isfinite(std::bit_cast<float>(*old))){*this={};return;}
        // Lens recovery must never replace the native distance. The radius
        // handler is intentionally left untouched after validation.
    }
};

// Observe DOORC, TALK, or first-person/item aiming before returning to NORMAL.
// Keep native distance for the resulting free-camera session; do not choose
// a guessed distance or start a second interpolation on top of the game.
struct EntryRecoveryOrbit {
    U32 camera=0,player=0,form=0;
    bool pending=false,active=false;
    template<class M> bool Owned(M& m,U32 cam) {
        Hook validator;const auto identity=validator.Target(m,cam);
        const auto game=m.Read(cam+0xa0);
        return identity&&game&&cam==*game+0x408&&cam==camera&&
            (*identity)[0]==player&&(*identity)[1]==form;
    }
    template<class M> void Observe(M& m,U32 cam,U32 requested) {
        Hook validator;const auto identity=validator.Target(m,cam);
        const auto game=m.Read(cam+0xa0),state=m.Read(cam+0x154),phase=m.Read(cam+0x170),mode=m.Read(cam+0x158);
        if(!identity||!game||cam!=*game+0x408||!state||!phase||!mode){*this={};return;}
        const U32 previous=*mode&65535;
        const bool aiming=previous==6||previous==12||previous==13; // SUBJECT/PACHINCO/BOWARROW
        if((*state>>16)==8||((previous==11||aiming)&&
           (requested==previous||requested==0||requested==31))) {
            // The hook runs before ChangeMode: TALK/FIRSTPERSON is still visible here,
            // even when the requested FREECAMERA handler runs later as31.
            camera=cam;player=(*identity)[0];form=(*identity)[1];pending=true;active=false;
            return;
        }
        if(!Owned(m,cam)){*this={};return;}
        if(pending) {
            if(requested==31){pending=false;active=true;}
            else if(requested!=0)*this={};
            else if((*phase>>16)==1) {
                // Phase1 alone is insufficient: the lens and orbit can still
                // be recovering for several updates after a textbox closes.
                ArrivalLens normal;
                const auto lens=normal.NormalFov(m,*state>>16);
                const auto radius=normal.NormalRadius(m,*state>>16,player,form);
                const auto lens_bits=m.Read(cam+0x110),radius_bits=m.Read(cam+0xf0);
                if(lens&&radius&&lens_bits&&radius_bits&&
                   std::abs(std::bit_cast<float>(*lens_bits)-*lens)<.1f&&
                   std::isfinite(std::bit_cast<float>(*radius_bits))&&
                   std::bit_cast<float>(*radius_bits)>=*radius-.1f)*this={};
            }
        } else if(active&&requested!=31)*this={};
    }
    template<class M> bool Native(M& m,U32 cam) {
        const auto mode=m.Read(cam+0x158);
        if(!Owned(m,cam)||!mode||(*mode&65535)!=31){*this={};return false;}
        if(pending){pending=false;active=true;}
        return active;
    }
};

// Ocarina's ITEM1 subcamera copies its lens back to the main camera, even
// when the main camera resumes an already-initialized FREECAMERA session.
// Recover that lens only after observing this particular handover. Its saved
// orbit is already correct; do not run arrival-radius correction here.
struct OcarinaReturnLens {
    U32 camera=0,player=0,form=0,last_bits=0;
    float target=0;
    bool pending=false,active=false;
    template<class M> void Observe(M& m,U32 cam,U32 requested) {
        const auto context=m.Read(0x72f5c0);
        const auto game=context&&*context?m.Read(*context+0x16c):std::optional<U32>{};
        if(!game||!*game||cam!=*game+0x408)return;
        const auto indices=m.Read(*game+0xa78);
        if(!indices){*this={};return;}
        const U32 index=*indices>>16;
        if(index==0){if(requested!=31)*this={};return;}
        const U32 subcamera=cam+0x198;
        Hook validator;const auto identity=validator.Target(m,subcamera,true);
        const auto state=m.Read(subcamera+0x154),mode=m.Read(cam+0x158);
        // Restoration CalcCamera identifies ocarina as index1, ITEM1 (0x13).
        if(index!=1||!identity||!state||(*state>>16)!=0x13||!mode||
           (*mode&65535)!=31||requested!=31||m.Read(cam+0xa0)!=game||
           m.Read(cam+0xa4)!=std::optional<U32>((*identity)[0])){*this={};return;}
        camera=cam;player=(*identity)[0];form=(*identity)[1];pending=true;active=false;
    }
    template<class M> void Step(M& m,U32 cam) {
        if((!pending&&!active)||cam!=camera)return;
        Hook validator;const auto identity=validator.Target(m,cam,true);
        const auto mode=m.Read(cam+0x158),state=m.Read(cam+0x154),bits=m.Read(cam+0x110),phase=m.Read(cam+0x170);
        ArrivalLens normal;const auto fov=state?normal.NormalFov(m,*state>>16):std::optional<float>{};
        if(!identity||(*identity)[0]!=player||(*identity)[1]!=form||!mode||
           (*mode&65535)!=31||!bits||!phase||(*phase&8)||!fov){*this={};return;}
        if(form==3) {
            const auto flags=m.Read(player+0x11db8);
            if(!flags||(*flags&0x2300)){*this={};return;}
        }
        const float current=std::bit_cast<float>(*bits);
        if(!std::isfinite(current)||current<*fov||current>70.01f||
           (active&&(last_bits!=*bits||target!=*fov))){*this={};return;}
        pending=false;active=true;target=*fov;
        const float next=current-target<.1f?target:current+(target-current)*.05f;
        last_bits=std::bit_cast<U32>(next);m.Write(cam+0x110,last_bits);
        if(next==target)*this={};
    }
};

// Repair only NUTSFLY -> FREECAMERA: the latter has a generic orbit and no
// lens update. Distance comes from current native parameters, never a latch.
// The small ownership record authorizes lens recovery after petals release;
// it contains no saved distance, presentation FOV, or timed blend.
struct DekuFreeCamera {
    struct State { U32 player,setting,scene,flags; bool grounded; };
    U32 camera=0,player=0,setting=0,scene=0,last_lens=0;
    bool armed=false,recovering=false;
    template<class M> static std::optional<State> Read(M& m,U32 cam) {
        Hook validator;const auto identity=validator.Target(m,cam,true);
        const auto game=m.Read(cam+0xa0),mode=m.Read(cam+0x158),phase=m.Read(cam+0x170),style=m.Read(cam+0x154);
        if(!identity||(*identity)[1]!=3||!game||cam!=*game+0x408||!mode||
           (*mode&65535)!=31||!phase||(*phase&8)||!style)return {};
        const U32 actor=(*identity)[0];
        const auto flags=m.Read(actor+0x11db8),flags1=m.Read(actor+0x11db0),ground=m.Read(actor+0x94),room=m.Read(*game+0x148);
        if(!flags||(*flags&0x100)||!flags1||(*flags1&0x00808000)||!ground||!room|| // ref 0x808000 (Restoration source port)
           m.Read(actor+0xdfc)!=std::optional<U32>(0))return {};
        return State{actor,*style>>16,*room&65535,*flags,bool(*ground&1)};
    }
    template<class M> static std::optional<float> Parameter(M& m,U32 state,U32 mode,U32 index) {
        if(!state||state>=0x5b)return {};
        const auto modes=m.Read(0x667894+state*12+8);
        if(!modes||!*modes)return {};
        const auto info=m.Read(*modes+mode*8),params=m.Read(*modes+mode*8+4);
        if(!info||(*info!=0xa0002&&*info!=0xa0018)||!params||!*params)return {};
        const auto value=m.Read(*params+index*4);
        if(!value||(*value>>16)!=index)return {};
        return float(*value&65535);
    }
    static bool Flying(const State& s){return !s.grounded&&(s.flags&0x2200);}
    void Observe(U32 cam,U32 requested) {if(cam==camera&&requested!=31)*this={};}
    template<class M> void Step(M& m,U32 cam) {
        const auto s=Read(m,cam);
        if(!s){*this={};return;}
        if(camera!=cam||player!=s->player||setting!=s->setting||scene!=s->scene) {
            *this={};camera=cam;player=s->player;setting=s->setting;scene=s->scene;
        }
        if(Flying(*s)){armed=true;recovering=false;return;}
        if(!armed)return;
        const auto normal=Parameter(m,setting,s->grounded?0:1,7);
        const auto bits=m.Read(cam+0x110);
        if(!normal||*normal<30||*normal>=60||!bits){*this={};return;}
        const float current=std::bit_cast<float>(*bits);
        if(!std::isfinite(current)||current<*normal||current>80.01f||
           (recovering&&*bits!=last_lens)){*this={};return;}
        // Observed native fall: 80 -> 78.25 -> ... ->45, at 30 updates/sec.
        // Run once at handler entry, independent of how many projections draw.
        const float next=current-*normal<.1f?*normal:current+(*normal-current)*.05f;
        last_lens=std::bit_cast<U32>(next);m.Write(cam+0x110,last_lens);
        recovering=true;
        if(next==*normal)*this={};
    }
    template<class M,class R> static void Distance(M& m,U32 cam,R& floats) {
        const auto s=Read(m,cam);if(!s||!Flying(*s))return;
        const auto maximum=Parameter(m,s->setting,5,2);if(!maximum)return;
        // NORMAL1 NUTSFLY parameters use Player_GetHeight(Deku)=36 and
        // height*.01*(.8 - (68/height)*-.2). 320 yields135.68, as captured.
        const float limit=*maximum*(36.f*.01f*(.8f-(68.f/36.f)*-.2f));
        const float requested=std::bit_cast<float>(floats[0]);
        if(!std::isfinite(requested)||requested<30||limit<30||limit>1000)return;
        // S0 is the target immediately before native smoothing; S1, pitch,
        // yaw, eye reconstruction, and collision still belong to the game.
        floats[0]=std::bit_cast<U32>(std::min(requested,limit));
    }
};

}
