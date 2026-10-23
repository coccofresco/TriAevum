// Ported from mm3d-dekui src/mm3d/native_startup.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"

namespace Mm3dDekui::NativeStartup {
using namespace NativeInventory;
struct Options { GearMenu::Pose logo{0,0,1}, copyright{0,0,1}, press_start{0,0,1}, loading_icon{0,0,1}; };
inline GearMenu::AtomicPose logo, copyright, press_start, loading_icon;
inline std::atomic<unsigned> status{0}, press_status{0};
inline void Apply(const Options& o) {
    press_start.x=o.press_start.x;press_start.y=o.press_start.y;press_start.scale=o.press_start.scale;
    loading_icon.x=o.loading_icon.x;loading_icon.y=o.loading_icon.y;loading_icon.scale=o.loading_icon.scale;
    logo.x=o.logo.x;logo.y=o.logo.y;logo.scale=o.logo.scale;
    copyright.x=o.copyright.x;copyright.y=o.copyright.y;copyright.scale=o.copyright.scale;
}
// Restoration + HD HUD v110: native skeleton model-view matrix builder.
// Original 0x5edc08. Each output is freshly rebuilt from unmodified world matrices.
constexpr U32 EnterPC=0x005edc0c,MatrixPC=0x005edc48,FirstSVC=0xffffd0; // ref 0x5eda9c, 0x5edad8
inline U32 Instruction(U32 pc,U32 word) {
    if(pc==EnterPC&&word==0xe1a05000)return 0xef000000|FirstSVC;
    if(pc==MatrixPC&&word==0xe595000c)return 0xef000000|(FirstSVC+1);
    return word;
}
template<class M> bool CodeMatches(M& m) {
    constexpr std::array<U32,23> words{
        0xe92d4070,0xe1a05000,0xe590000c,0xe1a06001,0xe3a04000,
        0xe5900000,0xe5900008,0xe3500000,0xda00000c,0xe5950024,
        0xe595101c,0xe0842084,0xe0800202,0xe0812202,0xe1a01006,
        0xebf0b67d,0xe595000c,0xe2844001,0xe5900000,0xe5900008, // ref 0xebf0b75d
        0xe1500004,0xcafffff2,0xe8bd8070};
    for(unsigned i=0;i<words.size();++i)
        if(m.Read(0x005edc08+4*i)!=std::optional<U32>(words[i]))return false; // ref 0x5eda98
    return true;
}
// zelda2_mag.gar / Model/title_logo.cmb: bone1 is copy_nintendo_G,
// bones3..12 drive the eleven logo meshes, including animated effects.
// Verify the complete skeleton identity, not a texture address or menu name.
constexpr std::array<U32,13> BoneHashes{
    0x6dcd9dbe,0x6203468c,0x1a81d08f,0x976c7263,0x976c7200,
    0x976c75f8,0x976c751e,0x976c4f30,0x0eb7295e,0xc7a02135,
    0x0ec2fe30,0x4d3f46ff,0x68702d0a};
constexpr std::array<unsigned,13> Parents{65535,0,0,2,2,2,2,2,0,0,0,0,0};
struct Hook {
    U32 skeleton=0,matrices=0;
    std::array<GearMenu::Pose,2> poses{};
    float anchor_x=0,anchor_y=0;
    template<class M> void Enter(M& m,U32 candidate) {
        skeleton=matrices=0;
        if(!Pointer(candidate,0x28))return;
        auto wrapper=m.Read(candidate+0xc),out=m.Read(candidate+0x24);
        if(!wrapper||!out||!Pointer(*wrapper,4)||!Pointer(*out,13*48))return;
        auto data=m.Read(*wrapper);
        if(!data||!Pointer(*data,0x24c)||m.Read(*data)!=std::optional<U32>(0x206c6b73)||
           m.Read(*data+4)!=std::optional<U32>(0x24c)||
           m.Read(*data+8)!=std::optional<U32>(13))return;
        for(unsigned i=0;i<13;++i) {
            const U32 bone=*data+16+44*i;
            if(m.Read(bone)!=std::optional<U32>(i|(Parents[i]<<16))||
               m.Read(bone+40)!=std::optional<U32>(BoneHashes[i]))return;
        }
        poses={GearMenu::Pose{logo.x.load(),logo.y.load(),logo.scale.load()},
               GearMenu::Pose{copyright.x.load(),copyright.y.load(),copyright.scale.load()}};
        for(const auto& p:poses)
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||
               std::abs(p.x)>400||std::abs(p.y)>240||p.scale<.1f||p.scale>3)return;
        skeleton=candidate;matrices=*out;status=1;
    }
    template<class M> void Matrix(M& m,U32 candidate,U32 index) {
        if(!skeleton||candidate!=skeleton||index>=13)return;
        const U32 at=matrices+48*index;
        std::array<float,8> v{};
        for(unsigned i=0;i<v.size();++i) {
            auto word=m.Read(at+4*i);if(!word)return;
            v[i]=std::bit_cast<float>(*word);if(!std::isfinite(v[i]))return;
        }
        if(index==0){anchor_x=v[3];anchor_y=v[7];return;}
        const auto& p=poses[index==1?1:0];
        if(p.x==0&&p.y==0&&p.scale==1)return;
        // Screen-plane scale retains the model's depth and native layer ordering.
        // Logo branches share the root pivot; copyright uses its own origin.
        const float ax=index==1?v[3]:anchor_x,ay=index==1?v[7]:anchor_y;
        for(unsigned row=0;row<2;++row)
            for(unsigned col=0;col<3;++col)v[row*4+col]*=p.scale;
        v[3]=ax+(v[3]-ax)*p.scale+p.x*.1f;
        v[7]=ay+(v[7]-ay)*p.scale-p.y*.1f;
        for(unsigned i=0;i<v.size();++i)m.Write(at+4*i,std::bit_cast<U32>(v[i]));
        status=2;
    }
};
// Native full-screen color quads are built by original0x1799e4 (patched0x179b40).
// Queue7 draws after bottom UI. Read only this frame's queued pool entries;
// allocated-but-unused quads retain stale colors and must never drive a fade.
constexpr U32 FadeManager=0x71b000;
constexpr std::array<std::pair<U32,U32>,7> FadeGuards{{
    {0x00175ad4,FadeManager},{0x001799e4,0xe92d47f0}, // ref 0x175c30, 0x179b40
    {0x00179a94,0xecc00a02},{0x00179aa0,0xe5841160}, // ref 0x179bf0, 0x179bfc
    {0x00179aa8,0xe5841164},{0x00179ab0,0xe5841168},{0x00179ab8,0xe584116c}}}; // ref 0x179c04, 0x179c0c, 0x179c14
template<class M> bool FadeCodeMatches(M& m) {
    for(auto [at,word]:FadeGuards)if(m.Read(at)!=std::optional<U32>(word))return false;
    return true;
}
template<class M> float BottomVisibility(M& m) {
    if(!FadeCodeMatches(m))return 1.f;
    auto count=m.Read(FadeManager+0x30),used=m.Read(FadeManager+8);
    if(!count||*count>0x180||!used||*used>48)return 1.f;
    float visibility=1.f;
    for(unsigned i=0;i<*count;++i) {
        auto entry=m.Read(FadeManager+0x2f78+4*i);if(!entry)return 1.f;
        bool pooled=false;
        for(unsigned j=0;j<*used;++j) {
            auto quad=m.Read(FadeManager+0x338+4*j);if(!quad)return 1.f;
            pooled|=*quad==*entry;
        }
        if(!pooled||!Pointer(*entry,0x170))continue;
        // Only full-screen black covers, not icons, models or partial dimmers.
        if(m.Read(*entry+0xc)!=std::optional<U32>(std::bit_cast<U32>(320.f))||
           m.Read(*entry+0x10)!=std::optional<U32>(std::bit_cast<U32>(240.f)))continue;
        bool black=true;
        for(unsigned c=0;c<3;++c)black&=m.Read(*entry+0x160+4*c)==std::optional<U32>(0);
        if(!black)continue;
        auto bits=m.Read(*entry+0x16c);if(!bits)return 1.f;
        const float alpha=std::bit_cast<float>(*bits);
        if(!std::isfinite(alpha)||alpha<0||alpha>1)return 1.f;
        visibility*=1.f-alpha;
    }
    return visibility;
}
// Joker.gar / Throbber.mfl has root > all > activity_pane (32x32).
// Apply a reversible screen-space affine during its native layout emission.
struct LoadingIconHook {
    NativeInventory::Hook helper;
    using Write=NativeInventory::Hook::Write;
    std::vector<std::vector<Write>> frames;
    template<class M> void Begin(M& m,U32 layout) {
        frames.emplace_back();
        if(!LayoutNamed(m,layout,"Throbber"))return;
        const GearMenu::Pose p{loading_icon.x.load(),loading_icon.y.load(),loading_icon.scale.load()};
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||
           std::abs(p.x)>400||std::abs(p.y)>240||p.scale<.1f||p.scale>3.f)return;
        const U32 root=layout+8;
        auto xb=m.Read(root+0xec),yb=m.Read(root+0xfc);
        if(!xb||!yb)return;
        const float ax=std::bit_cast<float>(*xb),ay=std::bit_cast<float>(*yb);
        if(!std::isfinite(ax)||!std::isfinite(ay))return;
        std::vector<Write> writes;std::vector<U32> seen;
        if(!helper.Transform(m,root,writes,seen,{p.scale,0,0,p.scale,
            ax*(1.f-p.scale)+p.x,ay*(1.f-p.scale)+p.y}))return;
        for(const auto& w:writes)m.Write(w.address,w.after);
        frames.back()=std::move(writes);
    }
    template<class M> void End(M& m) {
        if(frames.empty())return;
        for(const auto& w:frames.back())m.Write(w.address,w.before);
        frames.pop_back();
    }
};
// MenuStartup.mfl: both Press Start branches share native anchor (160,128).
// Copy its queue entry to top pass 3, retaining HOME and native input below.
struct PressHook {
    NativeInventory::Hook helper;
    U32 panel=0,pass=~0u;
    bool active=false;
    GearMenu::Pose frame{0,0,1};
    float visibility=1.f;
    using Write=NativeInventory::Hook::Write;
    std::vector<std::vector<Write>> frames;
    template<class M> bool Prepare(M& m,U32 layout,U32 screen,std::vector<Write>& writes) {
        if(!LayoutNamed(m,layout,"MenuStartup")||(screen!=3&&screen!=5))return false;
        auto list=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
        if(!list||!Pointer(*list)||count!=std::optional<U32>(1))return false;
        auto all=m.Read(*list);if(!all||!Named(m,*all,"all"))return false;
        list=m.Read(*all+0x14);count=m.Read(*all+0x18);
        if(!list||!Pointer(*list,12)||count!=std::optional<U32>(3))return false;
        constexpr std::array<const char*,3> names{"logo_press_start_fx_g","logo_press_start_g","home_menu_text_g"};
        unsigned found=0;std::vector<U32> seen;
        for(unsigned i=0;i<3;++i) {
            auto child=m.Read(*list+4*i);if(!child)return false;
            int role=-1;for(unsigned j=0;j<3;++j)if(Named(m,*child,names[j]))role=j;
            if(role<0||(found&(1u<<role)))return false;found|=1u<<role;
            if((role<2)!=(screen==3)) {
                auto flags=m.Read(*child+0xd4);
                if(!flags||!helper.Stage(m,writes,*child+0xd4,*flags&~1u))return false;
            } else if(screen==3&&!helper.Transform(m,*child,writes,seen,
                {frame.scale,0,0,frame.scale,200+frame.x-160*frame.scale,190+frame.y-128*frame.scale}))return false;
        }
        if(screen==3&&visibility!=1.f)for(auto node:seen) {
            for(U32 offset:{0x128u,0x12cu,0x130u,0x134u}) {
                auto bits=m.Read(node+offset);if(!bits)return false;
                const float alpha=std::bit_cast<float>(*bits);
                if(!std::isfinite(alpha)||alpha<0||alpha>1)return false;
                if(!helper.Stage(m,writes,node+offset,std::bit_cast<U32>(alpha*visibility)))return false;
            }
        }
        return found==7;
    }
    template<class M> void Pass(M& m,U32 manager,U32 screen) {
        pass=screen;if(screen!=5)return;
        active=false;panel=0;press_status=0;
        frame={press_start.x.load(),press_start.y.load(),press_start.scale.load()};
        if(!std::isfinite(frame.x)||!std::isfinite(frame.y)||!std::isfinite(frame.scale)||
           std::abs(frame.x)>400||std::abs(frame.y)>240||frame.scale<.1f||frame.scale>3||!Pointer(manager,32))return;
        auto bottom=m.Read(manager+28),top=m.Read(manager+20);
        NativeInventory::Hook::Buckets from,to;
        if(!bottom||!top||*bottom==*top||!helper.ReadQueue(m,*bottom,from)||!helper.ReadQueue(m,*top,to))return;
        unsigned priority=0;
        for(unsigned i=0;i<16;++i)for(auto entry:from[i])if(LayoutNamed(m,entry,"MenuStartup")) {
            if(panel)return;panel=entry;priority=i;
        }
        if(!panel||to[priority].size()>=32)return;
        for(const auto& bucket:to)if(std::find(bucket.begin(),bucket.end(),panel)!=bucket.end())return;
        visibility=BottomVisibility(m);
        std::vector<Write> preview;
        if(!Prepare(m,panel,3,preview)||!Prepare(m,panel,5,preview))return;
        const U32 at=*top+4+priority*0x80+U32(to[priority].size())*4;
        auto total=m.Read(*top+0x844);if(!total||!m.Read(at))return;
        m.Write(at,panel);m.Write(*top+0x804+priority*4,U32(to[priority].size()+1));
        m.Write(*top+0x844,*total+1);active=true;press_status=2;
    }
    template<class M> void Begin(M& m,U32 layout) {
        frames.emplace_back();if(!active||layout!=panel||(pass!=3&&pass!=5))return;
        std::vector<Write> writes;if(!Prepare(m,layout,pass,writes))return;
        for(const auto& w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
    }
    template<class M> void End(M& m) {
        if(frames.empty())return;
        for(const auto& w:frames.back())m.Write(w.address,w.before);frames.pop_back();
    }
};

}
