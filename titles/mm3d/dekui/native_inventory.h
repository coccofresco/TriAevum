// Ported from mm3d-dekui src/mm3d/native_inventory.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
#include "dialogue_scale.h"
#include "gear_menu.h"

namespace Mm3dDekui::NativeInventory {
using U32 = std::uint32_t;
inline std::atomic<bool> enabled{true};
inline std::atomic<float> x{0.f}, y{32.f}, scale{.8f};
inline std::atomic<float> masks_x{0.f},masks_y{0.f},masks_scale{1.f};
inline std::atomic<bool> bar_enabled{true};
inline std::atomic<bool> shared_slide{true};
inline std::atomic<float> bar_x{0.f},bar_y{20.f},bar_scale{.65f},bar_spacing{64.f};
struct ButtonPose { float x=0.f,y=0.f,scale=1.f; };
struct AtomicButtonPose { std::atomic<float> x{0.f},y{0.f},scale{1.f}; };
inline std::atomic<bool> buttons_enabled{true};
inline std::array<AtomicButtonPose,3> buttons; // Back, Masks, Items
inline std::atomic<bool> background_enabled{true};
inline AtomicButtonPose background_pose;
inline std::atomic<float> shadow_width{1.f},shadow_opacity{1.f};
inline std::atomic<unsigned> status{0}, routed_icons{0};
// Restoration + HD HUD v110; instruction substitutions leave guest code unchanged.
constexpr std::array<U32,4> PCs{0x0059e558,0x001267ec,0x001262c8,0x001262d4}; // ref 0x59e3e8, 0x126814, 0x1262f0, 0x1262fc
constexpr std::array<U32,4> Words{0xe1a04000,0xe1a04000,0xe2842008,0xe5860000};
constexpr U32 FirstSVC=0xfffff0;
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<PCs.size();++i)
        if(pc==PCs[i] && word==Words[i]) return 0xef000000|(FirstSVC+i);
    return word;
}
template<class Memory> bool CodeMatches(Memory& m) {
    for(unsigned i=0;i<PCs.size();++i) if(m.Read(PCs[i])!=std::optional<U32>(Words[i])) return false;
    return m.Read(0x001262d0)==std::optional<U32>(0xeb003ab0) && // ref 0x1262f8
        m.Read(0x0059e564)==std::optional<U32>(0xe59410c0); // ref 0x59e3f4
}
inline bool Pointer(U32 p,U32 bytes=4) {
    return p>=0x100000 && !(p&3) && p<0x40000000 && bytes<=0x40000000-p;
}
template<class Memory> bool Named(Memory& m,U32 widget,const char* name) {
    if(!Pointer(widget,0x140)) return false;
    auto p=m.Read(widget+0x10);
    return p && DialogueScale::Name(m,*p,name);
}
template<class Memory> bool LayoutNamed(Memory& m,U32 layout,const char* name) {
    if(!Pointer(layout,0x16c)) return false;
    auto resource=m.Read(layout+4);
    if(!resource || !Pointer(*resource,0x1c)) return false;
    auto p=m.Read(*resource+0x18);
    return p && DialogueScale::Name(m,*p,name);
}
struct Hook {
    U32 panel=0, pass=~0u,inventory_owner=0;
    std::array<U32,2> flying{};
    bool slide_active=false;
    float slide_x=0,bar_slide_y=0,bar_native_x=0;
    bool inventory_page=false;
    U32 transition_header=0;
    float header_native_y=0;
    float slide_remaining=0;
    unsigned background_draw=0;
    unsigned brown_shadow_steps=0;
    bool upper_background=false;
    U32 shadow_layout=0,shadow_group=0,shadow_pane=0;
    unsigned shadow_draw=0;
    float frame_shadow_width=1,frame_shadow_opacity=1;
    bool gear=false,gear_slide_active=false;
    float gear_slide_x=0,gear_left_x=0,gear_right_x=0;
    U32 gear_page=0;
    unsigned gear_cursor_part=GearMenu::Panel;
    unsigned gear_cursor_slot=0;
    bool gear_horizontal=false;
    std::array<GearMenu::Pose,4> gear_frame{};
    U32 background=0,routed_background=0;
    U32 background_pass=0;
    ButtonPose frame_background{};
    std::array<U32,24> icons{}; // Items: 20; Masks: 24
    std::array<U32,2> cursors{};
    std::array<U32,4> bar_icons{};
    bool bar_ready=false,frame_bar=false;
    bool frame_buttons=false;
    std::array<ButtonPose,3> frame_button_poses{};
    float row_x=0,row_y=20,row_scale=.65f,row_spacing=64;
    struct Affine { float xx,xy,yx,yy,tx,ty; };
    // ItemIconSet2.mfl item_set2_pos00..03: verified independent icon anchors.
    static constexpr std::array<std::array<float,2>,4> Anchors{{{292,28},{293,95},{283,146},{292,211}}};
    Affine Slide(Affine pose,float native_x=0) const {
        if(slide_active) { pose.tx+=slide_x-pose.xx*native_x;pose.ty-=pose.yx*native_x; }
        return pose;
    }
    Affine GridTransform(float native_x=0) const {
        return Slide({frame_scale,0,0,frame_scale,frame_x,frame_y},native_x);
    }
    template<class M> float NativeShift(M& m,U32 widget) const {
        if(!slide_active||slide_x==0)return 0;
        auto world=m.Read(widget+0xec),local=m.Read(widget+0x3c);
        if(!world||!local)return 0;
        float dx=std::bit_cast<float>(*world)-std::bit_cast<float>(*local);
        return std::isfinite(dx)&&std::abs(dx)<=512.f?dx:0.f;
    }
    template<class M> void ReadTransition(M& m) {
        slide_active=false;slide_remaining=0;slide_x=bar_slide_y=bar_native_x=0;
        if(gear||!enabled.load()||!shared_slide.load()||!bar_enabled.load()||!bar_ready||
           !(LayoutNamed(m,panel,"MenuItem")||LayoutNamed(m,panel,"MenuMask")))return;
        const bool masks=LayoutNamed(m,panel,"MenuMask");
        auto players=m.Read(panel+0x14c),count=m.Read(panel+0x150);
        if(!players||!count||*count>64||!Pointer(*players,*count*4))return;
        for(unsigned i=0;i<*count;++i) {
            auto player=m.Read(*players+i*4);if(!player||!Pointer(*player,0x18))continue;
            auto name=m.Read(*player+4),anim=m.Read(*player+0xc),frame=m.Read(*player+0x10);
            if(!name||!DialogueScale::Name(m,*name,"leftPaneTransition")||!anim||!Pointer(*anim,0x18)||!frame)continue;
            auto anim_name=m.Read(*anim+8);if(!anim_name)return;
            const bool opening=DialogueScale::Name(m,*anim_name,masks?"in_menu_mask_anim":"in_menu_item_anim");
            if(!opening&&!DialogueScale::Name(m,*anim_name,masks?"out_menu_mask_anim":"out_menu_item_anim"))return;
            const float f=std::bit_cast<float>(*frame);
            if(!std::isfinite(f)||f<0||f>6.f)return;
            // Capture: movement occupies frames0..4; native state holds through6.
            const float progress=std::clamp(f/4.f,0.f,1.f);
            const float remaining=opening?1.f-progress:progress;
            const std::array<float,8> poses{x.load(),scale.load(),bar_x.load(),bar_scale.load(),
                bar_spacing.load(),background_pose.x.load(),background_pose.scale.load(),bar_y.load()};
            for(float v:poses)if(!std::isfinite(v))return;
            const float mx=masks?masks_x.load():0.f,ms=masks?masks_scale.load():1.f;
            if(!std::isfinite(mx)||!std::isfinite(ms))return;
            const float grid_right=std::clamp(poses[0],-320.f,320.f)+std::clamp(mx,-320.f,320.f)+
                320*std::clamp(poses[1],.5f,1.5f)*std::clamp(ms,.5f,1.5f);
            const float strip_bottom=std::clamp(poses[7],-240.f,240.f)+64*std::clamp(poses[3],.35f,1.5f);
            const float bg_right=background_enabled.load()?std::clamp(poses[5],-400.f,400.f)+
                400*std::clamp(poses[6],.25f,2.f):0.f;
            // Conservative bounds plus margin clear the top viewport at all GUI poses.
            const float distance=64+std::max({0.f,grid_right,bg_right});
            auto widgets=m.Read(panel+0x154),n=m.Read(panel+0x158);
            if(!widgets||!n||*n>256||!Pointer(*widgets,*n*4))return;
            for(unsigned j=0;j<*n;++j) {
                auto node=m.Read(*widgets+j*4);
                if(!node||!Named(m,*node,"right_item_set_l"))continue;
                auto wx=m.Read(*node+0xec),lx=m.Read(*node+0x3c);
                if(!wx||!lx)return;
                const float dx=std::bit_cast<float>(*wx)-std::bit_cast<float>(*lx);
                if(!std::isfinite(dx)||dx<-.01f||dx>80.01f)return;
                slide_remaining=remaining;bar_native_x=dx;slide_x=-distance*remaining;
                bar_slide_y=-(64+std::max(0.f,strip_bottom))*remaining;
                slide_active=true;return;
            }
            return;
        }
    }
    Affine ButtonTransform(unsigned index) const {
        // Native MenuItem anchors, including each button's backing artwork.
        constexpr std::array<std::array<float,2>,3> anchors{{{0,202},{64,206},{154,204}}};
        const auto& p=frame_button_poses[index];
        return {p.scale,0,0,p.scale,p.x+anchors[index][0]*(1-p.scale),p.y+anchors[index][1]*(1-p.scale)};
    }
    Affine BarTransform(int slot=-1) const {
        Affine pose=slot<0?Affine{0,row_scale*(64+3*row_spacing)/240,-row_scale,0,
            row_x,row_y+320*row_scale}:
            Affine{row_scale,0,0,row_scale,row_x+row_scale*(32+slot*row_spacing-Anchors[slot][0]),
            row_y+row_scale*(32-Anchors[slot][1])};
        if(slide_active) {
            pose.tx-=pose.xx*bar_native_x;
            pose.ty+=bar_slide_y-pose.yx*bar_native_x;
        }
        return pose;
    }
    std::vector<U32> routed;
    float frame_x=0,frame_y=0,frame_scale=1;
    struct Write { U32 address,before,after; };
    std::vector<std::vector<Write>> frames;
    template<class Memory> void Capture(Memory& m,U32 page) {
        gear=false;gear_slide_active=false;gear_slide_x=0;inventory_page=false;gear_page=0;inventory_owner=0;flying.fill(0);
        slide_active=false;slide_remaining=0;slide_x=bar_slide_y=bar_native_x=0;
        panel=0;background=0; icons.fill(0); cursors.fill(0);bar_icons.fill(0);bar_ready=false;
        if(!Pointer(page,0x130)) return;
        auto layout=m.Read(page+0xc0);
        if(!layout) return;
        const bool masks=LayoutNamed(m,*layout,"MenuMask");
        if(!masks&&!LayoutNamed(m,*layout,"MenuItem")) return;
        std::array<U32,24> found{};
        for(unsigned i=0;i<(masks?24u:20u);++i) {
            auto icon=m.Read(page+0xd0+i*4);
            if(!icon || !LayoutNamed(m,*icon,"ItemIconButton")) return;
            found[i]=*icon;
        }
        panel=*layout;inventory_page=true; icons=found;inventory_owner=page;
        auto bg=m.Read(page+4);
        if(bg&&LayoutNamed(m,*bg,"CommonBottomBg")) background=*bg;
        // The shared item-collection base owns primary/replacement cursors at +8/+c.
        // They are independent layouts, submitted at priorities 6 and 5.
        for(unsigned i=0;i<cursors.size();++i) {
            auto cursor=m.Read(page+8+i*4);
            if(cursor && LayoutNamed(m,*cursor,"CursorItemCollect")) cursors[i]=*cursor;
        }
        bar_ready=true;
        for(unsigned i=0;i<bar_icons.size();++i) {
            auto icon=m.Read(page+0x2c+i*4);
            if(!icon||!LayoutNamed(m,*icon,"ItemIconButton")) {bar_ready=false;break;}
            bar_icons[i]=*icon;
        }
    }
    template<class Memory> void CaptureGear(Memory& m,U32 page) {
        Capture(m,0);
        if(!Pointer(page,0x198)) return;
        auto layout=m.Read(page+0x14),cursor=m.Read(page+0x130),bg=m.Read(page+0x18);
        if(!layout||!LayoutNamed(m,*layout,"MenuCollect")||!cursor||!LayoutNamed(m,*cursor,"CursorItemCollect")) return;
        gear=true;gear_page=page;panel=*layout;cursors[0]=*cursor;
        if(bg&&LayoutNamed(m,*bg,"CommonBottomBg")) background=*bg;
    }
    template<class M> void ReadGearTransition(M& m) {
        gear_slide_active=false;gear_slide_x=gear_left_x=gear_right_x=0;
        if(!gear||!GearMenu::enabled.load()||!LayoutNamed(m,panel,"MenuCollect"))return;
        auto players=m.Read(panel+0x14c),count=m.Read(panel+0x150);
        if(!players||!count||*count>64||!Pointer(*players,*count*4))return;
        bool valid=false;
        for(unsigned i=0;i<*count;++i) {
            auto player=m.Read(*players+i*4);if(!player||!Pointer(*player,0x18))continue;
            auto anim=m.Read(*player+0xc);if(!anim||!Pointer(*anim,0x18))continue;
            auto name=m.Read(*anim+8);
            if(name&&(DialogueScale::Name(m,*name,"in_menu_collect_anim")||DialogueScale::Name(m,*name,"out_menu_collect_anim")))valid=true;
        }
        if(!valid)return;
        auto list=m.Read(panel+0x154),n=m.Read(panel+0x158);
        if(!list||!n||*n>256||!Pointer(*list,*n*4))return;
        unsigned found=0;
        for(unsigned i=0;i<*n;++i) {
            auto node=m.Read(*list+i*4);if(!node)continue;
            const bool left=Named(m,*node,"left_base_g"),right=Named(m,*node,"right_base_g");
            if(!left&&!right)continue;
            auto world=m.Read(*node+0xec),local=m.Read(*node+0x3c);if(!world||!local)return;
            const float dx=std::bit_cast<float>(*world)-std::bit_cast<float>(*local);
            if(!std::isfinite(dx)||(left?(dx< -264.01f||dx>.01f):(dx<-.01f||dx>64.01f)))return;
            if(left){gear_left_x=dx;found|=1;}else{gear_right_x=dx;found|=2;}
        }
        if(found!=3)return;
        float edge=0;
        for(unsigned part:{unsigned(GearMenu::Panel),unsigned(GearMenu::Right),unsigned(GearMenu::Background)}) {
            auto p=GearMenu::Get(part);
            if(part==GearMenu::Background){p.x=background_pose.x.load();p.y=background_pose.y.load();p.scale=background_pose.scale.load();}
            float l=p.x,r=p.x+264*p.scale,top=p.y,bottom=p.y+240*p.scale;
            if(part==GearMenu::Background){top-=240*p.scale;r=p.x+(320+16*shadow_width.load())*p.scale;}
            if(part==GearMenu::Right){l+=260;r=l+(GearMenu::horizontal.load()?200:58)*p.scale;bottom=p.y+(GearMenu::horizontal.load()?56:200)*p.scale;}
            if(!std::isfinite(l)||!std::isfinite(r)||!std::isfinite(top)||!std::isfinite(bottom))return;
            if(l<427&&r>0&&top<240&&bottom>0)edge=std::max(edge,r);
        }
        gear_slide_x=-(32+edge)*std::clamp(-gear_left_x/264.f,0.f,1.f);gear_slide_active=true;
    }
    Affine GearSlide(Affine pose,unsigned part) const {
        if(gear_slide_active&&(part==GearMenu::Panel||part==GearMenu::Right)) {
            const float native=part==GearMenu::Panel?gear_left_x:gear_right_x;
            pose.tx+=gear_slide_x-pose.xx*native;pose.ty-=pose.yx*native;
        }
        return pose;
    }
    Affine GearTransform(unsigned part,int slot=-1) const {
        const auto& p=gear_frame[part];
        if(part==GearMenu::Right&&gear_horizontal) {
            // MenuCollect: background (262,0,56,200), icons at (290,29+47*i).
            if(slot<0) return GearSlide({0,p.scale,-p.scale,0,260.f+p.x,p.y+318.f*p.scale},part);
            const float cy=29.f+47.f*slot;
            return GearSlide({p.scale,0,0,p.scale,260.f+p.x+p.scale*(cy-290.f),p.y+p.scale*(28.f-cy)},part);
        }
        // Keep the right strip anchored at its native left edge (x=260).
        const float anchor=part==GearMenu::Right?260.f:0.f;
        return GearSlide({p.scale,0,0,p.scale,p.x+anchor*(1-p.scale),p.y},part);
    }
    template<class Memory> bool SamePosition(Memory& m,U32 a,U32 b) {
        for(U32 offset:{0x44u,0x48u,0x4cu}) {
            auto av=m.Read(a+offset),bv=m.Read(b+offset);
            if(!av||!bv) return false;
            const float af=std::bit_cast<float>(*av),bf=std::bit_cast<float>(*bv);
            if(!std::isfinite(af)||!std::isfinite(bf)||std::abs(af-bf)>.001f) return false;
        }
        return true;
    }
    template<class Memory> int BarSlot(Memory& m,U32 layout) {
        if(!frame_bar||!layout) return -1;
        const bool cursor=std::find(cursors.begin(),cursors.end(),layout)!=cursors.end();
        for(unsigned i=0;i<bar_icons.size();++i)
            if(layout==bar_icons[i]||(cursor&&SamePosition(m,layout,bar_icons[i]))) return i;
        return -1;
    }
    template<class Memory> bool GridCursor(Memory& m,U32 layout) {
        if(!layout || std::find(cursors.begin(),cursors.end(),layout)==cursors.end()) return false;
        if(gear) return true;
        // Native cursor setters copy the selected slot's position into root.pos.
        // Compare unmodified poses so sidebar/button focus stays on the bottom.
        for(U32 icon:icons) {
            if(!icon) continue;
            bool same=true;
            for(U32 offset:{0x44u,0x48u,0x4cu}) {
                auto a=m.Read(layout+offset),b=m.Read(icon+offset);
                if(!a || !b) { same=false;break; }
                const float av=std::bit_cast<float>(*a),bv=std::bit_cast<float>(*b);
                if(!std::isfinite(av)||!std::isfinite(bv)||std::abs(av-bv)>.001f) { same=false;break; }
            }
            if(same) return true;
        }
        return false;
    }
    using Buckets=std::array<std::vector<U32>,16>;
    template<class Memory> bool ReadQueue(Memory& m,U32 queue,Buckets& result) {
        if(!Pointer(queue,0x848)) return false;
        U32 total=0;
        for(unsigned p=0;p<16;++p) {
            auto count=m.Read(queue+0x804+p*4);
            if(!count || *count>32) return false;
            for(unsigned i=0;i<*count;++i) {
                auto entry=m.Read(queue+4+p*0x80+i*4);
                if(!entry || !Pointer(*entry,0x140)) return false;
                result[p].push_back(*entry);
            }
            total+=*count;
        }
        return m.Read(queue+0x844)==std::optional<U32>(total);
    }
    template<class Memory> void ReadHeaderTransition(Memory& m,U32 manager) {
        transition_header=0;header_native_y=0;
        if(!slide_active||!Pointer(manager,0x24))return;
        for(unsigned screen=0;screen<4;++screen) {
            auto queue=m.Read(manager+8+screen*4);Buckets entries;
            if(!queue||!ReadQueue(m,*queue,entries))continue;
            for(const auto& bucket:entries)for(U32 layout:bucket) {
                if(!LayoutNamed(m,layout,"SubMenuTitle"))continue;
                auto widgets=m.Read(layout+0x154),count=m.Read(layout+0x158);
                if(!widgets||!count||*count>256||!Pointer(*widgets,*count*4))continue;
                for(unsigned i=0;i<*count;++i) {
                    auto node=m.Read(*widgets+i*4);
                    if(!node||!Named(m,*node,"menu_title_base"))continue;
                    auto world=m.Read(*node+0xfc),local=m.Read(*node+0x40);
                    if(!world||!local)continue;
                    const float dy=std::bit_cast<float>(*world)-std::bit_cast<float>(*local);
                    if(!std::isfinite(dy)||dy>0.01f||dy< -40.01f)continue;
                    transition_header=layout;header_native_y=dy;
                    const float remaining=std::clamp(-dy/40.f,0.f,1.f);
                    const float bottom=std::clamp(bar_y.load(),-240.f,240.f)+64*std::clamp(bar_scale.load(),.35f,1.5f);
                    bar_slide_y=-(64+std::max(0.f,bottom))*remaining;
                    return;
                }
            }
        }
    }
    template<class Memory> bool CurrentPanel(Memory& m) const {
        // Menu allocations can be reused after closing a page. Queue membership
        // alone does not prove that the captured address still owns our layout.
        return gear ? LayoutNamed(m,panel,"MenuCollect") :
            inventory_page && (LayoutNamed(m,panel,"MenuItem")||LayoutNamed(m,panel,"MenuMask"));
    }
    template<class Memory> void Pass(Memory& m,U32 manager,U32 screen) {
        pass=screen;
        if(screen==4||screen==5) {ReadTransition(m);ReadGearTransition(m);ReadHeaderTransition(m,manager);}
        if(screen==4) { BackgroundPass(m,manager);return; }
        if(screen!=5) return;
        routed.clear(); routed_icons.store(0); status.store(1);
        if(!(gear?GearMenu::enabled.load():enabled.load()) || !CurrentPanel(m) || !Pointer(manager,0x24)) return;
        if(gear) {
            gear_horizontal=GearMenu::horizontal.load();
            for(unsigned i=0;i<gear_frame.size();++i) {
                auto p=GearMenu::Get(i);
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)) return;
                p.x=std::clamp(p.x,-400.f,400.f);p.y=std::clamp(p.y,-240.f,240.f);p.scale=std::clamp(p.scale,.25f,2.f);
                gear_frame[i]=p;
            }
            auto state=m.Read(gear_page+0x194);if(!state) return;
            gear_cursor_slot=(*state>>8)&255;
            gear_cursor_part=gear_cursor_slot<4?GearMenu::Right:GearMenu::Panel;
        }
        frame_buttons=buttons_enabled.load();
        for(unsigned i=0;i<buttons.size();++i) {
            auto& p=frame_button_poses[i];p={buttons[i].x.load(),buttons[i].y.load(),buttons[i].scale.load()};
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)) return;
            p.x=std::clamp(p.x,-400.f,400.f);p.y=std::clamp(p.y,-240.f,240.f);p.scale=std::clamp(p.scale,.25f,2.f);
        }
        frame_bar=bar_enabled.load()&&bar_ready;
        row_x=bar_x.load();row_y=bar_y.load();row_scale=bar_scale.load();row_spacing=bar_spacing.load();
        if(!std::isfinite(row_x)||!std::isfinite(row_y)||!std::isfinite(row_scale)||!std::isfinite(row_spacing)) return;
        row_x=std::clamp(row_x,-320.f,320.f);row_y=std::clamp(row_y,-240.f,240.f);
        row_scale=std::clamp(row_scale,.35f,1.5f);row_spacing=std::clamp(row_spacing,40.f,120.f);
        auto bottom=m.Read(manager+8+5*4),top=m.Read(manager+8+3*4);
        Buckets from,to;
        status.store(3);
        if(!bottom || !top || *bottom==*top || !ReadQueue(m,*bottom,from) || !ReadQueue(m,*top,to)) return;
        const auto has=[&](const Buckets& q,U32 value) {
            unsigned count=0;
            for(const auto& bucket:q) count+=std::count(bucket.begin(),bucket.end(),value);
            return count;
        };
        if(has(from,panel)==0) { status.store(1); return; }
        if(has(from,panel)!=1 || has(to,panel)!=0) return;
        flying.fill(0);
        if(!gear&&frame_bar&&inventory_owner&&(LayoutNamed(m,panel,"MenuItem")||LayoutNamed(m,panel,"MenuMask"))) {
            for(unsigned i=0;i<2;++i) {
                auto moving=m.Read(inventory_owner+0xb4+4*i);if(!moving||!*moving)continue;
                bool owned=false;
                // Button assignment (patched 0x5c116c) copies the selected item
                // into the standalone ItemIcon at +6c, then stores it in +b4.
                // Stylus swaps instead animate the existing button layouts.
                if(m.Read(inventory_owner+0x6c)==moving&&LayoutNamed(m,*moving,"ItemIcon"))owned=true;
                for(auto icon:icons)if(icon&&(*moving==icon||m.Read(icon+0x1c0)==moving))owned=true;
                for(auto icon:bar_icons)if(icon&&(*moving==icon||m.Read(icon+0x1c0)==moving))owned=true;
                if(owned&&Pointer(*moving,0x16c))flying[i]=*moving;
            }
        }
        std::vector<U32> selected;
        for(unsigned p=0;p<16;++p) {
            std::vector<U32> retained;
            for(U32 entry:from[p]) {
                const bool flight=entry&&std::find(flying.begin(),flying.end(),entry)!=flying.end();
                const bool grid=entry==panel;
                const bool icon=std::find(icons.begin(),icons.end(),entry)!=icons.end();
                const bool cursor=GridCursor(m,entry);
                const bool bar=BarSlot(m,entry)>=0;
                if(grid || icon || cursor || bar || flight) {
                    if(flight) {
                        bool later=false;
                        for(unsigned q=p+1;q<16;++q)later|=std::find(from[q].begin(),from[q].end(),entry)!=from[q].end();
                        if(later)continue; // retain the native priority7 flying draw
                    }
                    const bool already=std::find(selected.begin(),selected.end(),entry)!=selected.end();
                    if(already&&flight)continue;
                    if(has(to,entry)||already)return;
                    selected.push_back(entry); to[p].push_back(entry);
                }
                // Keep the panel's other branches on the bottom screen.
                if(!icon && !cursor && !bar && !flight) retained.push_back(entry);
            }
            from[p]=std::move(retained);
            if(to[p].size()>32) return;
        }
        const float sx=x.load(),sy=y.load(),ss=scale.load();
        if(!std::isfinite(sx)||!std::isfinite(sy)||!std::isfinite(ss)) return;
        frame_x=std::clamp(sx,-320.f,320.f);frame_y=std::clamp(sy,-240.f,240.f);
        frame_scale=std::clamp(ss,.5f,1.5f);
        if(LayoutNamed(m,panel,"MenuMask")) {
            const float mx=masks_x.load(),my=masks_y.load(),ms=masks_scale.load();
            if(!std::isfinite(mx)||!std::isfinite(my)||!std::isfinite(ms)) return;
            frame_x+=std::clamp(mx,-320.f,320.f);
            frame_y+=std::clamp(my,-240.f,240.f);
            frame_scale*=std::clamp(ms,.5f,1.5f);
        }
        // Confirm the complete render trees before moving anything off the bottom.
        for(U32 layout:selected) {
            std::vector<Write> preview;
            if(!Prepare(m,layout,3,preview)) { status.store(4);return; }
        }
        // Validate every destination before changing either queue.
        for(auto [address,queue]:{std::pair{*bottom,&from},std::pair{*top,&to}})
            for(unsigned p=0;p<16;++p) for(unsigned i=0;i<(*queue)[p].size();++i)
                if(!m.Read(address+4+p*0x80+i*4)) return;
        for(auto [address,queue]:{std::pair{*bottom,&from},std::pair{*top,&to}}) {
            U32 total=0;
            for(unsigned p=0;p<16;++p) {
                const auto& bucket=(*queue)[p];
                for(unsigned i=0;i<bucket.size();++i) m.Write(address+4+p*0x80+i*4,bucket[i]);
                m.Write(address+0x804+p*4,static_cast<U32>(bucket.size())); total+=bucket.size();
            }
            m.Write(address+0x844,total);
        }
        routed=std::move(selected);
        routed_icons.store(std::count_if(routed.begin(),routed.end(),[&](U32 entry) {
            return std::find(icons.begin(),icons.end(),entry)!=icons.end();
        })); status.store(2);
    }
    // Gear has no ItemIconSet2, but its shadow texture is in the same live
    // Main.Play resource14 atlas. Recreate shadow_pane01 without another page.
    template<class M> bool PrepareGearShadow(M& m,U32 layout,unsigned draw,std::vector<Write>& writes) {
        if(draw>1||m.Read(0x0013acc0)!=0x7cdc9c)return false; // ref 0x13ace8
        auto groups=m.Read(0x7cdc9c+0x28),entries=m.Read(0x7cdc9c+0x2c);
        if(!groups||!entries||!Pointer(*groups,4)||!Pointer(*entries))return false;
        auto range=m.Read(*groups);if(!range||(*range&65535)<=14)return false;
        const U32 index=(*range>>16)+14;if(index>4096)return false;
        auto record=m.Read(*entries+index*4);
        if(!record||!Pointer(*record,0x60)||m.Read(*record+4)!=(14u<<16))return false;
        std::vector<Write> probe;std::vector<U32> nodes;
        if(!Transform(m,layout+8,probe,nodes,{1,0,0,1,0,0}))return false;
        U32 left=0,right=0;
        for(auto n:nodes){if(Named(m,n,"common_bottom_bg_pane0"))left=n;if(Named(m,n,"common_bottom_bg_pane1"))right=n;}
        if(!left||!right)return false;
        auto flags=m.Read(left+0xd4),geo=m.Read(right+0x28),alpha=m.Read(right+0x134);
        if(!flags||!geo||!Pointer(*geo,0x7c)||!alpha)return false;
        auto kind=m.Read(*geo+4);if(!kind||(*kind&65535)!=6)return false;
        const float a=std::bit_cast<float>(*alpha);if(!std::isfinite(a)||a<0||a>1)return false;
        const auto f=[&](U32 at,float v){return Stage(m,writes,at,std::bit_cast<U32>(v));};
        const auto b=frame_background;const float w=b.scale*frame_shadow_width;
        if(!Stage(m,writes,left+0xd4,*flags&~1u))return false;
        // Exact native shadow rectangle/UV, mirrored beyond the brown edge.
        for(unsigned i=0;i<8;++i)if(!f(right+0xe0+4*i,i==0?-w:i==5?b.scale:0))return false;
        if(!f(right+0xec,b.x+320*b.scale+256*w)||!f(right+0xfc,b.y-(draw?240*b.scale:0)))return false;
        if(!Stage(m,writes,*geo+0x10,*record+0xc)||!Stage(m,writes,*geo+0xc,0x10101))return false;
        constexpr std::array<float,9> geometry{240,0,0,16,240,.0625f,.96875f,-.0625f,0};
        for(unsigned i=0;i<geometry.size();++i)if(!f(*geo+0x14+i*4,geometry[i]))return false;
        if(!Stage(m,writes,*geo+0x38,0))return false;
        for(unsigned i=0;i<16;++i)if(!f(*geo+0x3c+i*4,1))return false;
        for(unsigned i=0;i<3;++i)if(!f(right+0x128+i*4,frame_shadow_opacity))return false;
        return f(right+0x134,a*frame_shadow_opacity);
    }
    // Background-only pages lack ItemIconSet2. Feather a black-tinted native
    // background pane in narrow bands; no dependency on another page's lifetime.
    template<class M> bool PrepareBrownShadow(M& m,U32 layout,unsigned draw,std::vector<Write>& writes) {
        if(gear&&brown_shadow_steps==2)return PrepareGearShadow(m,layout,draw,writes);
        auto list=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
        if(!list||count!=std::optional<U32>(1))return false;
        auto all=m.Read(*list);if(!all||!Named(m,*all,"all"))return false;
        list=m.Read(*all+0x14);count=m.Read(*all+0x18);
        if(!list||count!=std::optional<U32>(2)||!Pointer(*list,8))return false;
        U32 left=0,right=0;
        for(unsigned i=0;i<2;++i) {
            auto node=m.Read(*list+4*i);if(!node)return false;
            if(Named(m,*node,"common_bottom_bg_pane0"))left=*node;
            if(Named(m,*node,"common_bottom_bg_pane1"))right=*node;
        }
        if(!left||!right)return false;
        auto flags=m.Read(left+0xd4),alpha=m.Read(right+0x134);
        if(!flags||!alpha)return false;
        const float a=std::bit_cast<float>(*alpha);
        if(!std::isfinite(a)||a<0||a>1)return false;
        const unsigned band=draw/2;const bool upper=draw%2;
        const float t=(band+.5f)/12.f;
        const auto& b=frame_background;
        const float width=16*b.scale*frame_shadow_width/12.f;
        std::vector<U32> seen;
        if(!Stage(m,writes,left+0xd4,*flags&~1u)||
           !Transform(m,right,writes,seen,{width/160,0,0,b.scale,
               b.x+320*b.scale+band*width-width,b.y-(upper?240*b.scale:0)}))return false;
        for(U32 offset:{0x128u,0x12cu,0x130u})if(!Stage(m,writes,right+offset,0))return false;
        return Stage(m,writes,right+0x134,std::bit_cast<U32>(a*frame_shadow_opacity*.5f*(1-t)*(1-t)));
    }
    template<class M> void ConfigureBrownShadow(M& m,U32 layout,unsigned available,float width,float opacity) {
        brown_shadow_steps=0;
        frame_shadow_width=width;frame_shadow_opacity=opacity;
        if(available<2||!std::isfinite(frame_shadow_width)||frame_shadow_width<=0||frame_shadow_width>3||
           !std::isfinite(frame_shadow_opacity)||frame_shadow_opacity<=0||frame_shadow_opacity>1)return;
        std::vector<Write> preview;
        if(gear&&PrepareGearShadow(m,layout,0,preview)){brown_shadow_steps=2;return;}
        preview.clear();
        if(available>=24&&PrepareBrownShadow(m,layout,0,preview))brown_shadow_steps=24;
    }
    template<class M> bool PrepareShadow(M& m,bool upper,std::vector<Write>& writes) {
        auto count=m.Read(shadow_group+0x18),list=m.Read(shadow_group+0x14);
        if(!count||!list||*count>64||!Pointer(*list,*count*4))return false;
        bool found=false;
        for(unsigned i=0;i<*count;++i) {
            auto child=m.Read(*list+4*i);if(!child)return false;
            auto flags=m.Read(*child+0xd4);if(!flags)return false;
            if(*child==shadow_pane)found=true;
            else if(!Stage(m,writes,*child+0xd4,*flags&~1u))return false;
        }
        if(!found)return false;
        auto wx=m.Read(shadow_pane+0xec),wy=m.Read(shadow_pane+0xfc);
        if(!wx||!wy)return false;
        const auto& b=frame_background;
        const float width=b.scale*frame_shadow_width;
        // Native shadow spans X240..256, dark at256. Mirror it beyond X320.
        Affine pose{-width,0,0,b.scale,
            b.x+320*b.scale+256*width+width*std::bit_cast<float>(*wx),
            b.y-(upper?240*b.scale:0)-b.scale*std::bit_cast<float>(*wy)};
        std::vector<U32> seen;
        if(!Transform(m,shadow_pane,writes,seen,Slide(pose)))return false;
        for(U32 at:{0x128u,0x12cu,0x130u,0x134u}) {
            auto bits=m.Read(shadow_pane+at);if(!bits)return false;
            const float alpha=std::bit_cast<float>(*bits);
            if(!std::isfinite(alpha)||alpha<0||alpha>1)return false;
            if(!Stage(m,writes,shadow_pane+at,std::bit_cast<U32>(alpha*frame_shadow_opacity)))return false;
        }
        return true;
    }
    template<class M> void FindShadow(M& m) {
        shadow_layout=shadow_group=shadow_pane=0;shadow_draw=0;
        if(!inventory_page)return;
        auto list=m.Read(panel+0x154),count=m.Read(panel+0x158);
        if(!list||!count||*count>256||!Pointer(*list,*count*4))return;
        for(unsigned i=0;i<*count;++i) {
            auto node=m.Read(*list+4*i);if(!node||!Named(m,*node,"right_item_set_l"))continue;
            auto layout=m.Read(*node+0x20);
            if(!layout||!LayoutNamed(m,*layout,"ItemIconSet2"))return;
            auto children=m.Read(*layout+8+0x14),n=m.Read(*layout+8+0x18);
            if(!children||n!=std::optional<U32>(1))return;
            auto group=m.Read(*children);if(!group||!Named(m,*group,"all"))return;
            auto panes=m.Read(*group+0x14),total=m.Read(*group+0x18);
            if(!panes||!total||*total>64||!Pointer(*panes,*total*4))return;
            for(unsigned j=0;j<*total;++j) {
                auto pane=m.Read(*panes+4*j);if(!pane||!Named(m,*pane,"shadow_pane01"))continue;
                shadow_layout=*layout;shadow_group=*group;shadow_pane=*pane;return;
            }
        }
    }
    template<class Memory> void BackgroundPass(Memory& m,U32 manager) {
        routed_background=0;background_draw=0;shadow_layout=0;brown_shadow_steps=0;
        if(!(gear?GearMenu::enabled.load():(enabled.load()&&background_enabled.load()))||!CurrentPanel(m)||!background||!Pointer(manager,0x24)) return;
        // Gear retains the live scene: pass 0 is covered by that scene. Pass 1
        // is after scene rendering and before the separate preview-model layer.
        background_pass=gear?1u:0u;
        auto bottom=m.Read(manager+8+4*4),top=m.Read(manager+8+background_pass*4),foreground=m.Read(manager+8+5*4);
        Buckets from,to,items;
        if(!bottom||!top||!foreground||*bottom==*top||!ReadQueue(m,*bottom,from)||
           !ReadQueue(m,*top,to)||!ReadQueue(m,*foreground,items)) return;
        unsigned occurrences=0;bool active=false;
        for(unsigned p=0;p<16;++p) {
            occurrences+=std::count(from[p].begin(),from[p].end(),background);
            if(std::find(to[p].begin(),to[p].end(),background)!=to[p].end()) return;
            active|=std::find(items[p].begin(),items[p].end(),panel)!=items[p].end();
        }
        // The shared background also belongs to other menus. Require the Items
        // panel to be submitted in this frame, rather than trusting a stale pointer.
        const unsigned layers=(inventory_page||gear)?2u:1u;
        if(!active||occurrences!=1||to[15].size()+layers>32||!LayoutNamed(m,background,"CommonBottomBg")) return;
        frame_background={background_pose.x.load(),background_pose.y.load(),background_pose.scale.load()};
        if(gear)frame_background.x+=gear_slide_x;
        auto& b=frame_background;
        if(!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.scale)) return;
        b.x=std::clamp(b.x,-400.f,400.f);b.y=std::clamp(b.y,-240.f,240.f);b.scale=std::clamp(b.scale,.25f,2.f);
        std::vector<Write> preview;
        if(!Prepare(m,background,background_pass,preview)) return;
        frame_shadow_width=shadow_width.load();frame_shadow_opacity=shadow_opacity.load();
        if(std::isfinite(frame_shadow_width)&&std::isfinite(frame_shadow_opacity)&&
           frame_shadow_width>0&&frame_shadow_width<=3&&frame_shadow_opacity>0&&frame_shadow_opacity<=1) {
            FindShadow(m);
            std::vector<Write> shadow_preview;
            if(shadow_layout&&(to[15].size()+layers+2>32||!PrepareShadow(m,false,shadow_preview)))shadow_layout=0;
        }
        if(gear)ConfigureBrownShadow(m,background,32-to[15].size()-layers,shadow_width.load(),shadow_opacity.load());
        for(auto& bucket:from) bucket.erase(std::remove(bucket.begin(),bucket.end(),background),bucket.end());
        // Last in the selected backdrop pass, before preview-model rendering.
        if(shadow_layout) {to[15].push_back(shadow_layout);to[15].push_back(shadow_layout);}
        for(unsigned i=0;i<layers+brown_shadow_steps;++i)to[15].push_back(background);
        for(auto [address,queue]:{std::pair{*bottom,&from},std::pair{*top,&to}})
            for(unsigned p=0;p<16;++p) for(unsigned i=0;i<(*queue)[p].size();++i)
                if(!m.Read(address+4+p*0x80+i*4)) return;
        for(auto [address,queue]:{std::pair{*bottom,&from},std::pair{*top,&to}}) {
            U32 total=0;
            for(unsigned p=0;p<16;++p) {
                for(unsigned i=0;i<(*queue)[p].size();++i) m.Write(address+4+p*0x80+i*4,(*queue)[p][i]);
                m.Write(address+0x804+p*4,static_cast<U32>((*queue)[p].size()));total+=(*queue)[p].size();
            }
            m.Write(address+0x844,total);
        }
        routed_background=background;
    }
    template<class Memory> bool Stage(Memory& m,std::vector<Write>& writes,U32 at,U32 value) {
        auto before=m.Read(at); if(!before) return false;
        writes.push_back({at,*before,value}); return true;
    }
    // Follow exactly the branches consumed by native Widget rendering (0x134dc0).
    template<class Memory> int WidgetSlot(Memory& m,U32 node) {
        constexpr std::array<std::array<const char*,4>,4> names{{
            {"item_set2_pos00","disp_1_pane","btn_corner01_g","bb00_cbtn01"},
            {"item_set2_pos01","disp_x_pane","btn_x_g","bb00_xbtn"},
            {"item_set2_pos02","disp_y_pane","btn_y_g","bb00_ybtn"},
            {"item_set2_pos03","disp_2_pane","btn_corner03_g","bb00_cbtn03"}}};
        for(unsigned i=0;i<names.size();++i) for(auto name:names[i]) if(Named(m,node,name)) return i;
        return -1;
    }
    template<class Memory> bool Transform(Memory& m,U32 node,std::vector<Write>& writes,
                                          std::vector<U32>& seen,const Affine& mapping,
                                          bool bar=false,int slot=-1,unsigned depth=0) {
        if(!Pointer(node,0x140)||depth>64||seen.size()>=4096) return false;
        if(std::find(seen.begin(),seen.end(),node)!=seen.end()) return false;
        seen.push_back(node);
        if(bar) { const int child_slot=WidgetSlot(m,node);if(child_slot>=0)slot=child_slot; }
        const Affine transform=bar?BarTransform(slot):mapping;
        for(unsigned row=0;row<2;++row) for(unsigned col=0;col<4;++col) {
            const U32 at=node+0xe0+row*16+col*4;
            auto bits=m.Read(at),xb=m.Read(node+0xe0+col*4),yb=m.Read(node+0xf0+col*4);
            if(!bits||!xb||!yb) return false;
            const float xv=std::bit_cast<float>(*xb),yv=std::bit_cast<float>(*yb);
            const float result=row==0?transform.xx*xv+transform.xy*yv+(col==3?transform.tx:0.f):
                transform.yx*xv+transform.yy*yv+(col==3?transform.ty:0.f);
            if(!std::isfinite(xv)||!std::isfinite(yv)||!std::isfinite(result)) return false;
            writes.push_back({at,*bits,std::bit_cast<U32>(result)});
        }
        auto embedded=m.Read(node+0x20), group=m.Read(node+0x24);
        if(!embedded || !group) return false;
        if(*embedded) return Pointer(*embedded,0x148) && Transform(m,*embedded+8,writes,seen,mapping,bar,slot,depth+1);
        if((*group&0xffff)!=0xffff) {
            auto owner=m.Read(node+4);
            if(!owner||!Pointer(*owner,0x14c)) return false;
            auto count=m.Read(*owner+0x148),list=m.Read(*owner+0x144);
            if(!count||!list||(*group&0xffff)>=*count||!Pointer(*list,*count*4)||*count>1024) return false;
            auto target=m.Read(*list+(*group&0xffff)*4);
            return target && (!*target || (Pointer(*target,0x144)&&Transform(m,*target+4,writes,seen,mapping,bar,slot,depth+1)));
        }
        auto count=m.Read(node+0x18),list=m.Read(node+0x14);
        if(!count||!list||*count>256||(*count&&!Pointer(*list,*count*4))) return false;
        for(unsigned i=0;i<*count;++i) {
            auto child=m.Read(*list+i*4);
            if(!child || (*child&&!Transform(m,*child,writes,seen,mapping,bar,slot,depth+1))) return false;
        }
        return true;
    }
    template<class Memory> bool GridBranch(Memory& m,U32 node) {
        for(const char* name:{"item_panel_g","item_set_pos_g","mask_panel_g","mask_set_pos_g","set_frame0_g","set_frame1_g","set_frame2_g","set_frame3_g"})
            if(Named(m,node,name)) return true;
        return false;
    }
    template<class Memory> bool TransformMenuButtons(Memory& m,U32 group,std::vector<Write>& writes,
                                                    std::vector<U32>& seen) {
        auto count=m.Read(group+0x18),list=m.Read(group+0x14);
        if(!count||*count!=4||!list||!Pointer(*list,16)) return false;
        float native_y=0,down=0;
        if(slide_active) {
            auto world=m.Read(group+0xfc),local=m.Read(group+0x40);
            if(!world||!local)return false;
            native_y=std::bit_cast<float>(*world)-std::bit_cast<float>(*local);
            if(!std::isfinite(native_y)||native_y<-.01f||native_y>40.01f)return false;
            const auto& a=frame_button_poses[1];const auto& b=frame_button_poses[2];
            // Shared distance preserves spacing and clears even moved/scaled buttons.
            const float top=160+std::min(a.y,b.y)-64*std::max(a.scale,b.scale);
            down=(64+std::max(0.f,240-top))*slide_remaining;
        }
        unsigned found=0;
        for(unsigned i=0;i<4;++i) {
            auto child=m.Read(*list+i*4);if(!child) return false;
            int role=-1;
            constexpr std::array<const char*,4> names{"btn_omen_l","bb_item_omen_pane00","btn_item_l","bb_item_omen_pane01"};
            for(unsigned j=0;j<names.size();++j) if(Named(m,*child,names[j])) role=j;
            if(role<0||(found&(1u<<role))) return false;
            found|=1u<<role;
            auto pose=ButtonTransform(role<2?1:2);
            pose.ty+=down-pose.yy*native_y;
            if(!Transform(m,*child,writes,seen,pose)) return false;
        }
        return found==15;
    }
    template<class M> bool Flight(M& m,U32 layout,std::vector<Write>& writes) {
        auto state=m.Read(inventory_owner+0x18);if(!state)return false;
        const unsigned equipped_column=LayoutNamed(m,panel,"MenuMask")?6u:5u;
        std::array<float,2> nx{},ny{},tx{},ty{},sc{};
        for(unsigned i=0;i<2;++i) {
            auto xb=m.Read(inventory_owner+0x84+i*12),yb=m.Read(inventory_owner+0x88+i*12);
            if(!xb||!yb)return false;nx[i]=std::bit_cast<float>(*xb);ny[i]=std::bit_cast<float>(*yb);
            unsigned c=(*state>>(i*16))&255,r=(*state>>(i*16+8))&255;
            if(c>equipped_column||r>=4||!std::isfinite(nx[i])||!std::isfinite(ny[i]))return false;
            if(c==equipped_column){sc[i]=row_scale;tx[i]=row_x+row_scale*(32+r*row_spacing);ty[i]=row_y+32*row_scale+bar_slide_y;}
            else{sc[i]=frame_scale;tx[i]=frame_x+frame_scale*nx[i]+slide_x;ty[i]=frame_y+frame_scale*ny[i];}
        }
        auto xb=m.Read(layout+0x44),yb=m.Read(layout+0x48);if(!xb||!yb)return false;
        const float px=std::bit_cast<float>(*xb),py=std::bit_cast<float>(*yb);
        const float dx=nx[1]-nx[0],dy=ny[1]-ny[0],length=dx*dx+dy*dy;
        if(!std::isfinite(px)||!std::isfinite(py)||!std::isfinite(length))return false;
        const float t=length>0.0001f?std::clamp(((px-nx[0])*dx+(py-ny[0])*dy)/length,0.f,1.f):0.f;
        const float scale=sc[0]+t*(sc[1]-sc[0]);
        std::vector<U32> seen;
        return Transform(m,layout+8,writes,seen,{scale,0,0,scale,
            tx[0]+t*(tx[1]-tx[0])-scale*px,ty[0]+t*(ty[1]-ty[0])-scale*py});
    }
    template<class Memory> bool Prepare(Memory& m,U32 layout,U32 screen,std::vector<Write>& writes) {
        std::vector<U32> seen;
        if(layout&&std::find(flying.begin(),flying.end(),layout)!=flying.end())return screen==3&&Flight(m,layout,writes);
        if(layout==background&&background) {
            const auto& b=frame_background;
            return screen==background_pass&&Transform(m,layout+8,writes,seen,Slide({b.scale,0,0,b.scale,b.x,b.y-(upper_background?240*b.scale:0)}));
        }
        if(layout==panel) {
            const U32 root=layout+8;
            auto list=m.Read(root+0x14);
            if(!list||!Pointer(*list)||m.Read(root+0x18)!=std::optional<U32>(1)) return false;
            auto all=m.Read(*list);
            if(!all||!Named(m,*all,"all")) return false;
            auto count=m.Read(*all+0x18),children=m.Read(*all+0x14);
            if(!count||!children||*count!=(gear?5u:9u)||!Pointer(*children,*count*4)) return false;
            if(gear) {
                unsigned found=0;
                constexpr std::array<const char*,5> names{"btn_return_top_g","left_base_g","right_base_g","btn_option_g","btn_return_l"};
                for(unsigned i=0;i<5;++i) {
                    auto child=m.Read(*children+i*4);if(!child) return false;
                    int role=-1;for(unsigned j=0;j<5;++j) if(Named(m,*child,names[j])) role=j;
                    if(role<0||(found&(1u<<role))) return false;found|=1u<<role;
                    if(screen==5||role==3) { // Gear Options is no longer exposed.
                        auto flags=m.Read(*child+0xd4);
                        if(!flags||!Stage(m,writes,*child+0xd4,*flags&~1u)) return false;
                    } else if(screen==3) {
                        const unsigned part=role==1?GearMenu::Panel:role==2?GearMenu::Right:GearMenu::Buttons;
                        if(part==GearMenu::Right&&gear_horizontal) {
                            if(!TransformGearStrip(m,*child,writes,seen)) return false;
                        } else if(!Transform(m,*child,writes,seen,GearTransform(part))) return false;
                    } else return false;
                }
                return found==31;
            }
            for(unsigned i=0;i<*count;++i) {
                auto child=m.Read(*children+i*4);
                if(!child||!Pointer(*child,0x140)) return false;
                const bool grid=GridBranch(m,*child),bar=frame_bar&&Named(m,*child,"right_item_set_l");
                const bool back=frame_buttons&&Named(m,*child,"btn_return_l");
                const bool menu_buttons=frame_buttons&&Named(m,*child,"btn_item_omen_g");
                if((grid||bar||back||menu_buttons)!=(screen==3)) {
                    auto flags=m.Read(*child+0xd4);
                    if(!flags||!Stage(m,writes,*child+0xd4,*flags&~1u)) return false;
                } else if(screen==3) {
                    if(menu_buttons) {
                        if(!TransformMenuButtons(m,*child,writes,seen)) return false;
                    } else if(!Transform(m,*child,writes,seen,back?ButtonTransform(0):GridTransform(NativeShift(m,*child)),bar)) return false;
                }
            }
        } else {
            const int slot=BarSlot(m,layout);
            if(screen!=3 || !Transform(m,layout+8,writes,seen,gear?GearTransform(gear_cursor_part,int(gear_cursor_slot)):slot>=0?BarTransform(slot):GridTransform(NativeShift(m,layout+8)))) return false;
        }
        return true;
    }
    template<class Memory> bool TransformGearStrip(Memory& m,U32 group,std::vector<Write>& writes,std::vector<U32>& seen) {
        auto count=m.Read(group+0x18),list=m.Read(group+0x14);
        if(!count||*count!=5||!list||!Pointer(*list,20)) return false;
        constexpr std::array<const char*,5> names{"icon_event00_l","icon_event01_l","icon_event02_l","icon_event03_l","right_base_pane"};
        unsigned found=0;
        for(unsigned i=0;i<5;++i) {
            auto child=m.Read(*list+i*4);if(!child) return false;
            int role=-1;for(unsigned j=0;j<5;++j) if(Named(m,*child,names[j])) role=j;
            if(role<0||(found&(1u<<role))) return false;found|=1u<<role;
            if(!Transform(m,*child,writes,seen,GearTransform(GearMenu::Right,role<4?role:-1))) return false;
        }
        return found==31;
    }
    template<class Memory> bool PrepareMenuBackdrop(Memory& m,U32 layout,std::vector<Write>& writes) {
        // UpCommonBG owns the left/right captured-scene panes used behind Items
        // and Masks. Keep native color/opacity and stereo visibility untouched.
        if(!LayoutNamed(m,layout,"UpCommonBG")) return false;
        auto list=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
        if(!list||!Pointer(*list)||!count||*count!=1) return false;
        auto all=m.Read(*list);if(!all||!Named(m,*all,"all")) return false;
        auto groups=m.Read(*all+0x14),n=m.Read(*all+0x18);
        if(!groups||!Pointer(*groups,8)||!n||*n!=2) return false;
        unsigned found=0;
        for(unsigned i=0;i<2;++i) {
            auto group=m.Read(*groups+i*4);if(!group) return false;
            if(Named(m,*group,"picture_left_g")) found|=1;
            else if(Named(m,*group,"picture_right_g")) found|=2;
            else return false;
        }
        if(found!=3) return false;
        std::vector<U32> seen;
        constexpr float factor=16.f/15.f;
        return Transform(m,layout+8,writes,seen,{factor,0,0,1,200.f*(1.f-factor),0});
    }
    template<class Memory> void Begin(Memory& m,U32 layout,bool widescreen=false) {
        frames.emplace_back();
        if(widescreen&&pass==0&&LayoutNamed(m,layout,"UpCommonBG")) {
            std::vector<Write> writes;
            if(!PrepareMenuBackdrop(m,layout,writes)) return;
            for(const auto& w:writes) m.Write(w.address,w.after);
            frames.back()=std::move(writes);
            return;
        }
        if(layout==shadow_layout&&shadow_layout&&pass==background_pass) {
            std::vector<Write> writes;
            if(!PrepareShadow(m,shadow_draw++%2==1,writes))return;
            for(const auto& w:writes)m.Write(w.address,w.after);
            frames.back()=std::move(writes);return;
        }
        if(slide_active&&layout==transition_header&&pass<4) {
            std::vector<Write> writes;std::vector<U32> seen;
            if(!Transform(m,layout+8,writes,seen,{1,0,0,1,0,bar_slide_y-header_native_y}))return;
            for(const auto& w:writes)m.Write(w.address,w.after);
            frames.back()=std::move(writes);return;
        }
        const bool bg=layout==routed_background&&routed_background&&pass==background_pass;
        if(!bg&&(std::find(routed.begin(),routed.end(),layout)==routed.end() || (pass!=3 && pass!=5))) return;
        std::vector<Write> writes;
        if(bg&&background_draw<brown_shadow_steps) {
            if(!PrepareBrownShadow(m,layout,background_draw++,writes))return;
            for(const auto& w:writes)m.Write(w.address,w.after);
            frames.back()=std::move(writes);return;
        }
        upper_background=bg&&(inventory_page||gear)&&(background_draw++%2==1);
        const bool prepared=Prepare(m,layout,pass,writes);upper_background=false;
        if(!prepared) { status.store(4);return; }
        for(const auto& w:writes) m.Write(w.address,w.after);
        frames.back()=std::move(writes);
    }
    template<class Memory> void End(Memory& m) {
        if(frames.empty()) return;
        for(const auto& w:frames.back()) m.Write(w.address,w.before);
        frames.pop_back();
    }
};
}
