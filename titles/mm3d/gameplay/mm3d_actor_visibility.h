#pragma once
#include "../adapter/mm3d_hook_sites.h"
#include "../dekui/actor_draw_distance.h"
#include "triaevum_guest_transaction.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace Oot3dNativeGame::Mm3dVisibility {
// EUR 3DS tri-state semantics, NOT MM64's boolean or OoT3D's return ABI.
struct Sample {
    float X=0, Y=0, Z=0, W=0, Forward=0, Scale=0, Downward=0;
    float Fovy=60, DiagonalX=1, DiagonalY=1;
};
struct Volume { float ForwardMultiplier=1, Horizontal=1, Vertical=1; };
inline bool Valid(const Sample& s) {
    for(float f:{s.X,s.Y,s.Z,s.W,s.Forward,s.Scale,s.Downward,s.Fovy,s.DiagonalX,s.DiagonalY})
        if(!std::isfinite(f)||std::abs(f)>1.0e7f)return false;
    return s.Forward>=0 && s.Scale>=0 && s.Downward>=0 && s.DiagonalX>0 && s.DiagonalY>0;
}
inline unsigned Classify(const Sample& s, Volume v={}) {
    // Actual EUR instructions at190D48. 3DS uses sqrt(3) and (5/3)*sqrt(3)
    // for padding, not MM64's reciprocal constants. Keep strict boundary tests.
    const float far=(s.Forward+s.Scale)*v.ForwardMultiplier;
    if(s.Z>=far)return 2;
    if(s.Z<=-s.Scale)return 1;
    float x=s.Scale, y=s.Scale, down=s.Downward;
    if(s.Fovy!=60.f) {
        const float sy=s.DiagonalY*std::bit_cast<float>(0x3FDDB3D8U);
        x=(s.Scale*s.DiagonalX)*std::bit_cast<float>(0x4038C089U);
        y=s.Scale*sy; down=sy*s.Downward;
    }
    const float w=std::max(1.f,s.W);
    return std::abs(s.X)-x<w*v.Horizontal && s.Y+down>-w*v.Vertical &&
           s.Y-y<w*v.Vertical ? 0 : 1;
}
inline float PresentedExtent(float cullingScale,float presentedScale) {
    if(!std::isfinite(cullingScale)||!std::isfinite(presentedScale)||
       cullingScale<=0||presentedScale<=0)return 1;
    const float ratio=cullingScale/presentedScale;
    // Rigid view multiplication introduces rounding; do not widen an equal view.
    return std::isfinite(ratio)&&ratio>1.0001f ? std::min(ratio,4.f) : 1.f;
}
inline uint8_t NativeFade(unsigned classification,uint8_t fade) {
    return classification==2 && fade ? uint8_t(fade-1) : 0;
}
inline constexpr std::array Sites{
    Mm3dHookSite{"actor walk",0x175094,{0xE92D4FF3,0xE1A05000,0xE2800903,0xE2851A02}},
    Mm3dHookSite{"culling arguments",0x190D48,{0xEE300A20,0xED912A02,0xEEB42AC0,0xEEF1FA10}},
    Mm3dHookSite{"actor culling result",0x1754E0,{0xE21000FF,0xE5C40124,0x0A00000A,0xE3500002}},
    Mm3dHookSite{"next actor",0x175650,{0xE5944134,0xE3540000,0x1AFFFF38,0xE28BB001}},
    Mm3dHookSite{"actor walk end",0x17566C,{0xE5950100,0xEB0035CB,0xE1A00005,0xE320F000}},
    Mm3dHookSite{"grass group result",0x436DC0,{0xE3500000,0xE320F000,0x0A000072,0xE5D410FE}},
    Mm3dHookSite{"wood offspring result",0x529C60,{0xE3500000,0xE320F000,0x1A000026,0xE5DBC0F8}},
};
inline constexpr std::array<uint32_t,46> FadeCode{
    0xE21000FF,0xE5C40124,0x0A00000A,0xE3500002,0x15C461EE,0x0A00001D,
    0xE5940004,0xE3C00040,0xE5840004,0xE5940138,0xE5C46125,0xE3500000,
    0x1A00004E,0xEA00001F,0xE5D401EF,0xE5941004,0xE3500000,0xE3811040,
    0x03A0000A,0xE3580001,0xE5841004,0x0A000001,0xE3590000,0x0A000003,
    0xE5D411F0,0xE3510000,0x05C401EE,0xEAFFFFEC,0xE5D411EE,0xE2811001,
    0xE20110FF,0xE1510000,0xE5C411EE,0xC5C401EE,0xC5C461F0,0xEAFFFFE4,
    0xE5D401EE,0xE3500000,0x0AFFFFDE,0xE2400001,0xE21000FF,0xE5C401EE,
    0x15940004,0x13800040,0x0AFFFFD8,0xEAFFFFD9};

class Source {
    struct Call { Sample Input; Volume Bounds; uint32_t Play=0,Actor=0,Sp=0,Return=0; bool Valid=false; } mCall;
    struct Restore { uint32_t Actor=0,Play=0,Identity=0,Update=0,Draw=0; uint8_t Class=0,Fade=0,Hold=0; } mRestore;
    struct VisualFade {uint32_t Identity=0,Update=0,Draw=0;uint64_t Epoch=0;uint8_t Fade=0,Hold=0;};
    std::unordered_map<uint32_t,VisualFade> mVisual;
    uint32_t mWalk=0,mLastPlay=0,mLastFrame=0,mLastScene=0;
    uint64_t mEpoch=0;
    bool mVerified=false,mExtended=false;
    template<class M> static bool Float(M& m,uint32_t p,float& f) {
        uint32_t w=0;if(!m.Read32(p,&w))return false;f=std::bit_cast<float>(w);return std::isfinite(f);
    }
    static bool Pointer(uint32_t p,uint32_t size) {return p>=0x08000000 && !(p&3) && p<=0x40000000-size;}
    template<class M> static Volume Presented(M& m,uint32_t play,const Sample& s) {
        std::array<float,6> rows{};
        for(unsigned i=0;i<6;++i)if(!Float(m,play+0xC474+4*(i<3?i:i+1),rows[i]))return {};
        return {1,PresentedExtent(std::hypot(rows[0],rows[1],rows[2]),s.DiagonalX),
                  PresentedExtent(std::hypot(rows[3],rows[4],rows[5]),s.DiagonalY)};
    }
    template<class M> void RestoreNative(M& m) {
        if(!mRestore.Actor)return;
        const auto saved=mRestore;mRestore={};
        uint32_t identity=0,update=0,draw=0,flags=0;
        if(!m.Read32(saved.Actor,&identity)||identity!=saved.Identity||
           !m.Read32(saved.Actor+0x140,&update)||update!=saved.Update||
           !m.Read32(saved.Actor+0x144,&draw)||draw!=saved.Draw) {
            mVisual.erase(saved.Actor);
            ++mStats.IdentityChanges;return; // freed/reused actor must not be written
        }
        uint8_t visualFade=0,visualHold=0;
        if(!m.Read32(saved.Actor+4,&flags)||!m.Read8(saved.Actor+0x1EE,&visualFade)||!m.Read8(saved.Actor+0x1F0,&visualHold))
            throw std::runtime_error("MM3D culling restore unreadable");
        mVisual[saved.Actor]={identity,update,draw,mEpoch,visualFade,visualHold};
        flags=(flags&~0x40U)|(saved.Fade?0x40U:0U);
        GuestByteAccess guest(m);GuestWriteTransaction tx(guest);
        if(!tx.Add(saved.Actor+0x124,saved.Class)||!tx.Add(saved.Actor+0x1EE,saved.Fade)||
           !tx.Add(saved.Actor+0x1F0,saved.Hold)||!tx.Add(saved.Actor+4,flags)||!tx.Commit())
            throw std::runtime_error("MM3D culling restore failed");
        ++mStats.Restored;
    }
public:
    struct Statistics {uint64_t Calls=0,Results=0,NativeInside=0,ActorGrants=0,GrassGrants=0,WoodGrants=0;
        uint64_t Restored=0,Invalid=0,NativeMismatch=0,UnknownCallers=0,IdentityChanges=0;
        uint64_t NativeModeResults=0,ExtendedModeResults=0;
        float MaxHorizontal=1,MaxVertical=1;} mStats;
    bool Verified()const{return mVerified;}
    void SetExtended(bool value){mExtended=value;if(!value)mVisual.clear();}
    void Reset(){mCall={};mRestore={};mWalk=0;mEpoch=0;mVisual.clear();mLastPlay=mLastFrame=mLastScene=0;}
    template<class M,class HasBlock> bool Verify(M& m,HasBlock&& hasBlock) {
        const auto read=[&](uint32_t p,uint32_t& w){return m.Read32(p,&w);};
        mVerified=true;
        for(const auto& site:Sites)mVerified=Mm3dHookSiteMatches(site,read)&&hasBlock(site.Pc)&&mVerified;
        for(unsigned i=0;i<FadeCode.size();++i){uint32_t w=0;mVerified=m.Read32(0x1754E0+4*i,&w)&&w==FadeCode[i]&&mVerified;}
        for(unsigned i=0;i<Mm3dDekui::ActorDrawDistance::Cull.size();++i){uint32_t w=0;
            mVerified=m.Read32(0x190D48+4*i,&w)&&w==Mm3dDekui::ActorDrawDistance::Cull[i]&&mVerified;}
        Reset();return mVerified;
    }
    template<class CPU,class M> void Observe(uint32_t pc,CPU& cpu,M& m,bool lightArenaVerified) {
        if(!mVerified)return;
        if(pc==0x175650) {
            if(mRestore.Actor && (mRestore.Actor!=cpu.r[4]||mRestore.Play!=cpu.r[5]))
                throw std::runtime_error("MM3D culling restore boundary mismatch");
            RestoreNative(m);return;
        }
        if(pc==0x17566C||pc==0x175094) {
            if(mRestore.Actor)throw std::runtime_error("MM3D culling missed next-actor boundary");
            if(pc==0x175094) {
                ++mEpoch;
                uint32_t frame=0,scene=0;
                const auto play=cpu.r[0];
                if(!Pointer(play,0x11030)||!m.Read32(play+0x138,&frame)||!m.Read32(play+0x148,&scene)||
                   play!=mLastPlay||(scene&0xFFFF)!=(mLastScene&0xFFFF)||frame<mLastFrame||mVisual.size()>255)
                    mVisual.clear();
                mLastPlay=play;mLastFrame=frame;mLastScene=scene;
                std::erase_if(mVisual,[&](const auto& entry){return entry.second.Epoch+1<mEpoch;});
            }
            mWalk=pc==0x175094 && cpu.r[14]==0x15EF68 && Pointer(cpu.r[0],0x11030) &&
                cpu.r[1]==cpu.r[0]+0x20B0 ? cpu.r[0] : 0;
            return;
        }
        if(pc==0x190D48) {
            mCall={};
            const auto lr=cpu.r[14];
            if(lr!=0x1754E0&&lr!=0x436DC0&&lr!=0x529C60){++mStats.UnknownCallers;return;}
            ++mStats.Calls;
            const auto play=cpu.r[0];if(!Pointer(play,0x11030))return;
            // Capture AFTER the native 10000 override, BEFORE the leaf changes S0..S6.
            Sample s;s.Forward=std::bit_cast<float>(cpu.vfp[0]);s.Scale=std::bit_cast<float>(cpu.vfp[1]);
            s.Downward=std::bit_cast<float>(cpu.vfp[2]);s.W=std::bit_cast<float>(cpu.vfp[3]);
            const auto pos=cpu.r[1];
            if(!pos||pos>UINT32_MAX-12||!Float(m,pos,s.X)||!Float(m,pos+4,s.Y)||!Float(m,pos+8,s.Z)||
               !Float(m,play+0x210,s.Fovy)||!Float(m,play+0x2B4,s.DiagonalX)||!Float(m,play+0x2C8,s.DiagonalY)||!Valid(s)) {
                ++mStats.Invalid;return;
            }
            auto volume=Presented(m,play,s);
            mStats.MaxHorizontal=std::max(mStats.MaxHorizontal,volume.Horizontal);
            mStats.MaxVertical=std::max(mStats.MaxVertical,volume.Vertical);
            // Obj_Grass already gets its extended forward distance from the
            // host-owned vegetation module. Never multiply that argument twice.
            if(mExtended&&lr!=0x436DC0)volume.ForwardMultiplier=3;
            mCall={s,volume,play,cpu.r[4],cpu.r[13],lr,true};return;
        }
        if(pc!=0x1754E0&&pc!=0x436DC0&&pc!=0x529C60)return;
        const auto call=mCall;mCall={};
        if(!call.Valid||call.Return!=pc||call.Sp!=cpu.r[13])return;
        ++mStats.Results;
        if(mExtended)++mStats.ExtendedModeResults;else ++mStats.NativeModeResults;
        const unsigned native=cpu.r[0]&255;
        if(native!=Classify(call.Input)){++mStats.NativeMismatch;return;}
        if(native==0){++mStats.NativeInside;return;}
        if(Classify(call.Input,call.Bounds)!=0)return;
        // EDD grants require the verified non-aliasing overflow protection.
        // Without EDD the native matrix already presents the wider view; do not
        // introduce extra records while its overflow owner is disabled.
        if(!mExtended||!lightArenaVerified)return;
        if(pc==0x1754E0) {
            const uint32_t actor=cpu.r[4];
            uint32_t identity=0,init=0,update=0,draw=0,flags=0;uint8_t fade=0,hold=0;
            if(call.Play!=mWalk||cpu.r[5]!=mWalk||actor!=call.Actor||!Pointer(actor,0x1F8)||
               !m.Read32(actor,&identity)||!m.Read32(actor+0x138,&init)||init||
               !m.Read32(actor+0x140,&update)||!update||!m.Read32(actor+0x144,&draw)||!draw||
               !m.Read32(actor+4,&flags)||!m.Read8(actor+0x1EE,&fade)||!m.Read8(actor+0x1F0,&hold))return;
            // Native unconditional draw/cutscene actors need no extension.
            if(flags&0x200020U)return;
            if(mRestore.Actor)throw std::runtime_error("MM3D nested culling extension");
            // Keep presentation fade history separate from gameplay's native
            // history, otherwise a far actor would restart at alpha1 every draw.
            if(const auto i=mVisual.find(actor);i!=mVisual.end()) {
                const auto& visual=i->second;
                if(visual.Identity==identity&&visual.Update==update&&visual.Draw==draw&&visual.Epoch+1==mEpoch) {
                    GuestByteAccess guest(m);GuestWriteTransaction tx(guest);
                    if(!tx.Add(actor+0x1EE,visual.Fade)||!tx.Add(actor+0x1F0,visual.Hold)||!tx.Commit()) {
                        ++mStats.Invalid;return;
                    }
                }
            }
            mRestore={actor,mWalk,identity,update,draw,uint8_t(native),NativeFade(native,fade),hold};
            ++mStats.ActorGrants;
        } else if(pc==0x529C60) {
            uint8_t count=0;
            if(!m.Read8(call.Play+0x20BE,&count)||count>=165)return;
            ++mStats.WoodGrants;
        } else ++mStats.GrassGrants;
        cpu.r[0]=0; // original store, fade, room/lens/cutscene gates still execute
    }
};
} // namespace Oot3dNativeGame::Mm3dVisibility
