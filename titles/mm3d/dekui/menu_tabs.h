// Ported from mm3d-dekui src/mm3d/menu_tabs.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "notebook_prompt.h"
namespace Mm3dDekui::MenuTabs {
using namespace NativeInventory;
inline AtomicButtonPose gear_pose;
inline float SlideClearance(float y,float scale,bool selected) {
    // Start just beyond the bottom edge. A fixed 40px minimum leaves small,
    // low-positioned tabs invisible through most of the four opening steps.
    // The selected face/rim is lifted seven local pixels before scaling.
    return std::max(0.f,242.f-(204.f+y-(selected?7.f*scale:0.f)));
}
struct SlideTiming {
    U32 layout{};
    float previous=1;
    bool opening=true;
    float Apply(U32 next,float remaining) {
        if(layout!=next){layout=next;previous=1;opening=true;}
        remaining=std::clamp(remaining,0.f,1.f);
        if(remaining!=previous)opening=remaining<previous;
        previous=remaining;
        constexpr float speed=1.15f;
        return opening?std::max(0.f,1.f-(1.f-remaining)*speed):std::min(1.f,remaining*speed);
    }
};
// Gear uses native TouchPanel panes; Items/Masks use MenuItem/MenuMask panes.
// Pause tabs include their native selected face and both mirrored rim halves.
// Textures remain in the user's live resource registry; no images are bundled.
inline constexpr std::array<std::array<U32,11>,12> Panes{{
    {0x80000000u,0x42cc0000u,0x80000000u,0x42580000u,0x42100000u,0x3f300000u,0x3dc00000u,0x3dd80007u,0x3e100000u,0xf0000u,0xffffffffu}, // btn_collect_pane
    {0x80000000u,0x42d00000u,0x80000000u,0x42580000u,0x42100000u,0x3f300000u,0x3dc00000u,0x3dd80007u,0x3e100000u,0xf0000u,0xffa0a0a0u}, // base_btn_collect
    {0x80000000u,0x42d00000u,0x80000000u,0x42580000u,0x42100000u,0x3f0f0001u,0x3f5c0000u,0x3dd80007u,0x3e100000u,0xf0000u,0xffffffffu}, // fuchi00_collect
    {0x80000000u,0x42c80000u,0x80000000u,0x42600000u,0x42200000u,0x3f120000u,0x3f080000u,0x3de00000u,0x3e200000u,0xf0000u,0xffffffffu}, // bb00_collect
    {0x42840000u,0x43500000u,0x80000000u,0x42a80000u,0x42000000u,0x3ef80000u,0x3e000000u,0x3ea80000u,0x3e000000u,0x00140000u,0xffffffffu}, // MenuItem.mfl: btn_omen_pane
    {0x42840000u,0x434e0000u,0x80000000u,0x42c80000u,0x42100000u,0x3f000000u,0x3ec80000u,0x3ec80000u,0x3e100000u,0x00140000u,0xffa0a0a0u}, // MenuMask.mfl: btn_omen_press_pane
    {0x42840000u,0x434e0000u,0x80000000u,0x42480000u,0x42100000u,0x00000000u,0x3f580000u,0x3e480000u,0x3e100000u,0x00140000u,0xffffffffu}, // MenuMask.mfl: fuchi00_btn_omen
    {0x42e80000u,0x434e0000u,0x80000000u,0x42480000u,0x42100000u,0x3e480000u,0x3f580000u,0xbe480000u,0x3e100000u,0x00140000u,0xffffffffu}, // MenuMask.mfl: fuchi01_btn_omen
    {0x432a0000u,0x43500000u,0x80000000u,0x42a80000u,0x42000000u,0x3ef80000u,0x00000000u,0x3ea80000u,0x3e000000u,0x00140000u,0xffffffffu}, // MenuMask.mfl: btn_item_pane
    {0x431a0000u,0x434e0000u,0x80000000u,0x42c80000u,0x42100000u,0x3f000000u,0x3e800000u,0x3ec80000u,0x3e100000u,0x00140000u,0xffa0a0a0u}, // MenuItem.mfl: btn_item_press_pane
    {0x431a0000u,0x434e0000u,0x80000000u,0x42480000u,0x42100000u,0x00000000u,0x3f580000u,0x3e480000u,0x3e100000u,0x00140000u,0xffffffffu}, // MenuItem.mfl: fuchi00_item_pane
    {0x434c0000u,0x434e0000u,0x80000000u,0x42480000u,0x42100000u,0x3e480000u,0x3f580000u,0xbe480000u,0x3e100000u,0x00140000u,0xffffffffu}, // MenuItem.mfl: fuchi01_item_pane
}};
struct Hook {
    NotebookPrompt::Hook scratch;
    SlideTiming slide;
    template<class M> U32 Texture(M& m,U32 id) {
        const auto registry=NotebookPrompt::Registry;
        if(m.Read(NotebookPrompt::RegistryLiteral)!=registry)return 0;
        auto groups=m.Read(registry+0x28),entries=m.Read(registry+0x2c);
        if(!groups||!entries||!Pointer(*groups,4)||!Pointer(*entries))return 0;
        auto range=m.Read(*groups);if(!range||(*range&65535)<=id)return 0;
        U32 index=(*range>>16)+id;if(index>4096)return 0;
        auto record=m.Read(*entries+index*4);
        if(!record||!Pointer(*record,0x60)||m.Read(*record+4)!=(id<<16))return 0;
        return *record+0xc;
    }
    template<class M> U32 Root(M& m,NativeInventory::Hook& inventory,U32 layout,U32 sp,U32 content) {
        scratch.frames.emplace_back();auto& frame=scratch.frames.back();
        int selected=LayoutNamed(m,layout,"MenuCollect")?0:LayoutNamed(m,layout,"MenuMask")?1:LayoutNamed(m,layout,"MenuItem")?2:-1;
        if(selected<0||inventory.pass!=3||inventory.panel!=layout||!buttons_enabled||
           !(selected==0?GearMenu::enabled.load():enabled.load())||
           std::find(inventory.routed.begin(),inventory.routed.end(),layout)==inventory.routed.end())return content;
        U32 atlas=Texture(m,15),pause_atlas=Texture(m,20);
        if(!atlas||!pause_atlas)return content;
        std::vector<NativeInventory::Hook::Write> probe;std::vector<U32> nodes;
        if(!inventory.Transform(m,layout+8,probe,nodes,{1,0,0,1,0,0}))return content;
        U32 source=0,all=scratch.Child(m,layout+8,"all"),old_tabs=0;
        for(auto n:nodes){
            if(Named(m,n,"btn_item_omen_g"))old_tabs=n;
            auto geo=m.Read(n+0x28);if(!geo)continue;auto type=m.Read(*geo+4);
            if(!source&&type&&(*type&65535)==6&&Pointer(*geo,0x7c))source=n;
        }
        auto alpha=m.Read((all?all:layout+8)+0x134);if(!source||!alpha)return content;
        float fade=std::bit_cast<float>(*alpha);if(!std::isfinite(fade)||fade<=0||fade>1)return content;
        // Follow the existing menu transition progress rather than starting a
        // second timer. The entire strip moves together, including pressed rims.
        float remaining=inventory.slide_active?inventory.slide_remaining:0.f;
        if(selected==0&&inventory.gear_slide_active)
            remaining=std::clamp(-inventory.gear_left_x/264.f,0.f,1.f);
        float motion_y=0,clearance=0;
        for(unsigned tab=0;tab<3;++tab){
            const auto& pose=tab?buttons[tab]:gear_pose;
            const float y=pose.y.load(),s=pose.scale.load();
            if(!std::isfinite(y)||!std::isfinite(s))return content;
            clearance=std::max(clearance,SlideClearance(y,s,int(tab)==selected));
        }
        if(!std::isfinite(remaining))return content;
        motion_y=clearance*slide.Apply(layout,remaining);
        if(old_tabs){
            auto a=m.Read(old_tabs+0x134);if(!a)return content;
            float native_alpha=std::bit_cast<float>(*a);
            if(!std::isfinite(native_alpha)||native_alpha<0||native_alpha>1)return content;
            fade=std::min(fade,native_alpha);
            // Respect native tab motion when the shared slide override is off.
            if(!inventory.slide_active&&selected!=0){
                auto world=m.Read(old_tabs+0xfc),local=m.Read(old_tabs+0x40);
                if(!world||!local)return content;
                float delta=std::bit_cast<float>(*world)-std::bit_cast<float>(*local);
                if(!std::isfinite(delta))return content;
                motion_y=delta;
            }
        }
        constexpr U32 Size=0x2000; if(sp<0x104000||(sp&7))return content;
        U32 base=sp-Size;
        for(U32 a=base-Size;a<sp;a+=4)if(!m.Read(a))return content;
        std::array<U32,Size/4> words{};
        auto put=[&](U32 a,U32 v){words[a/4]=v;};
        auto f=[&](U32 a,float v){put(a,std::bit_cast<U32>(v));};
        auto copy=[&](U32 a,U32 from,U32 size){for(U32 i=0;i<size;i+=4){auto v=m.Read(from+i);if(!v)return false;put(a+i,*v);}return true;};
        if(!copy(0,content,0x140))return content;
        put(0x14,base+0x140);put(0x20,0);put(0x24,0xffff);put(0x28,0);put(0xd4,15);
        unsigned count=0;
        auto geo=m.Read(source+0x28);if(!geo)return content;
        // Same positions on all three pages. Leave room for Gear before Masks.
        const std::array<float,3> x{0,58,142};
        for(unsigned tab=0;tab<3;++tab){
            const bool down=int(tab)==selected;
            const auto& pose=tab?buttons[tab]:gear_pose;
            const float dx=pose.x.load(),dy=pose.y.load(),scale=pose.scale.load();
            if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(scale)||scale<.25f||scale>2)return content;
            // Keep button faces and pressed rims; omit the black backing on every tab.
            const unsigned layers=down?(tab?3u:2u):1u;
            for(unsigned layer=0;layer<layers;++layer){
                unsigned pane=tab*4+(layer==0?(down?1:0):layer+1);
                const auto& p=Panes[pane];const auto& origin=Panes[tab*4+(down?1:0)];
                // Preserve the authored 82px tab footprint and native aspect ratio.
                const float fit=tab?82.f/std::bit_cast<float>(origin[3]):1.f;
                U32 at=0x200+count*0x200,gp=at+0x140;
                if(!copy(at,source,0x140)||!copy(gp,*geo,0x7c))return content;
                put(0x140+count*4,base+at);++count;
                put(at+0x14,0);put(at+0x18,0);put(at+0x20,0);put(at+0x24,0xffff);put(at+0x28,base+gp);put(at+0xd4,15);
                put(at+0xdc,words[(at+0xdc)/4]&~0xff00u);
                for(unsigned i=0;i<8;++i)f(at+0xe0+4*i,(i==0||i==5)?scale:0);
                f(at+0xec,x[tab]+dx);f(at+0xfc,204+dy+motion_y);
                for(unsigned i=0;i<6;++i)f(at+0x110+4*i,(i==0||i==4)?1:0);
                // Pane RGB is the tint used by the native textured draw path,
                // also used for the menu background darkness and shadows.
                for(unsigned i=0;i<3;++i)f(at+0x128+4*i,down?1.f:.7f);f(at+0x134,fade);
                put(gp+4,(words[(gp+4)/4]&0xffff0000)|6);put(gp+0xc,0x00010101);
                put(gp+0x10,tab?pause_atlas:atlas);
                f(gp+0x14,fit*(std::bit_cast<float>(p[0])-std::bit_cast<float>(origin[0])));
                // Replace the native downward press with a small upward shift.
                const float face_y=std::bit_cast<float>(Panes[tab*4+(down?1:0)][1]);
                f(gp+0x18,fit*(std::bit_cast<float>(p[1])-face_y)+(down?-7.f:0.f));f(gp+0x1c,0);
                for(unsigned i=3;i<9;++i)put(gp+0x20+4*(i-3),p[i]);
                f(gp+0x20,fit*std::bit_cast<float>(p[3]));
                f(gp+0x24,fit*std::bit_cast<float>(p[4]));
                put(gp+0x38,0);
                // Keep vertex colors neutral for selected panes; apply the
                // tab tint once through the pane RGB above.
                for(unsigned i=0;i<16;++i)
                    f(gp+0x3c+i*4,down&&i%4!=3?1.f:float((p[10]>>((i%4)*8))&255)/255);
            }
        }
        put(0x140+count*4,content);put(0x18,count+1);
        if(old_tabs){auto flags=m.Read(old_tabs+0xd4);if(!flags)return content;frame.writes.push_back({old_tabs+0xd4,*flags,*flags&~1u});}
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+i*4,*m.Read(base+i*4),words[i]});
        for(auto w:frame.writes)m.Write(w.address,w.after);frame.sp=sp;return base;
    }
};
}
