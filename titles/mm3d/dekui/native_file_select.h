// Ported from mm3d-dekui src/mm3d/native_file_select.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
#include "native_startup.h"

namespace Mm3dDekui::NativeFileSelect {
using namespace NativeInventory;
enum Part : unsigned { Summary, Files, Prompt, Actions, Popup, Count };
struct HeaderBackground {float x=0,y=0,scale_x=1,scale_y=1;};
struct HeaderTitle {float x=0,y=0,scale=1;};
struct Options {
    bool enabled=true;
    float stone_spacing=0.f;
    float entrance_ease=2.f;
    HeaderBackground header;
    HeaderTitle title;
    std::array<GearMenu::Pose,Count> poses{{{170,32,.65f},{0,35,.55f},{0,32,.55f},{0,35,.55f},{40,0,1.f}}};
};
inline std::atomic<bool> enabled{true};
inline std::atomic<float> stone_spacing{0.f};
inline std::atomic<float> entrance_ease{2.f};
inline std::atomic<float> header_x{0},header_y{0},header_scale_x{1},header_scale_y{1};
inline std::atomic<float> title_x{0},title_y{0},title_scale{1};
inline std::array<GearMenu::AtomicPose,Count> poses;
inline void Apply(const Options& o) {
    enabled=o.enabled;stone_spacing=o.stone_spacing;entrance_ease=o.entrance_ease;
    header_x=o.header.x;header_y=o.header.y;header_scale_x=o.header.scale_x;header_scale_y=o.header.scale_y;
    title_x=o.title.x;title_y=o.title.y;title_scale=o.title.scale;
    for(unsigned i=0;i<Count;++i){poses[i].x=o.poses[i].x;poses[i].y=o.poses[i].y;poses[i].scale=o.poses[i].scale;}
}
struct Hook {
    NativeInventory::Hook helper;
    U32 panel=0,summary=0,popup=0,pass=~0u;
    unsigned selected_slot=3;
    bool active=false,wide=false;
    bool header_active=false;
    const char* route_status="not observed";
    const char* last_route_status=nullptr;
    float visibility=1.f,frame_spacing=0;
    float prompt_opacity=1.f;
    std::array<float,3> stone_y{},stone_native_x{};
    bool presentation=false,stone_anchors=false;
    bool entrance_active=false,entrance_pending=false;
    U32 entrance_panel=0;
    float entrance_first_time=0,entrance_last_time=-1;
    std::array<float,3> stone_slide{};
    float detail_progress=0,files_slide=0,summary_slide=0,actions_slide=0,start_native_x=0;
    std::array<GearMenu::Pose,Count> frame{};
    using Write=NativeInventory::Hook::Write;
    std::vector<std::vector<Write>> frames;
    template<class M> void ReadPresentation(M& m) {
        presentation=false;detail_progress=files_slide=summary_slide=actions_slide=start_native_x=0;
        entrance_active=entrance_pending=false;stone_slide={};
        auto widgets=m.Read(panel+0x154),count=m.Read(panel+0x158);
        if(!widgets||!count||*count>512||!Pointer(*widgets,*count*4))return;
        bool cards_hidden=false;
        for(unsigned i=0;i<*count;++i) {
            auto node=m.Read(*widgets+4*i);if(!node||!Named(m,*node,"null_plate_g"))continue;
            auto flags=m.Read(*node+0xd4);if(!flags)return;
            cards_hidden=!(*flags&1);
        }
        unsigned found=0;
        for(unsigned i=0;i<*count;++i) {
            auto node=m.Read(*widgets+4*i);if(!node)continue;
            for(unsigned slot=0;slot<3;++slot) {
                const char* names[]={"plate_x_n","plate_y_n","plate_z_n"};
                if(!Named(m,*node,names[slot]))continue;
                if(cards_hidden){stone_native_x[slot]=-320;found|=1u<<slot;continue;}
                auto wx=m.Read(*node+0xec);if(!wx)return;
                float x=std::bit_cast<float>(*wx);if(!std::isfinite(x)||x< -306.1f||x>334.1f)return;
                stone_native_x[slot]=x-14;found|=1u<<slot;
            }
            if(Named(m,*node,"start_g")) {
                auto wx=m.Read(*node+0xec);if(!wx)return;
                start_native_x=std::bit_cast<float>(*wx);if(!std::isfinite(start_native_x))return;
            }
        }
        if(found!=7)return;
        // Options returns to the selected-file view with the card group hidden.
        // Its dormant matrices cannot identify the active presentation endpoint.
        // Endpoint fallback also handles an inactive/unbound choose-file player.
        detail_progress=std::clamp(-stone_native_x[0]/320.f,0.f,1.f);
        bool entrance=false,selection=false;
        float entrance_time=0;
        auto players=m.Read(panel+0x14c),n=m.Read(panel+0x150);
        if(players&&n&&*n<=64&&Pointer(*players,*n*4))for(unsigned i=0;i<*n;++i) {
            auto player=m.Read(*players+4*i);if(!player||!Pointer(*player,0x18))continue;
            auto anim=m.Read(*player+0xc),time=m.Read(*player+0x10);
            if(!anim||!time||!Pointer(*anim,0x18))continue;
            auto name=m.Read(*anim+8);if(!name)continue;
            if(DialogueScale::Name(m,*name,"in_menu_fileselect_ani")) {
                const float f=std::bit_cast<float>(*time);
                if(std::isfinite(f)&&f>=0&&f<=10) {entrance=true;entrance_time=f;}
            }
            for(unsigned slot=0;slot<3;++slot) {
                const char* ins[]={"in_f1_to_start","in_f2_to_start","in_f3_to_start"};
                const char* outs[]={"out_f1_to_start","out_f2_to_start","out_f3_to_start"};
                const bool enter=DialogueScale::Name(m,*name,ins[slot]);
                if(!enter&&!DialogueScale::Name(m,*name,outs[slot]))continue;
                const float f=std::bit_cast<float>(*time),duration=enter&&slot==0?7.f:6.f;
                if(!std::isfinite(f)||f<0||f>duration)continue;
                const float t=std::clamp(f/duration,0.f,1.f),ease=1-(1-t)*(1-t)*(1-t);
                selected_slot=slot;
                selection=true;detail_progress=enter?ease:1-ease;
            }
        }
        if(entrance&&!selection) {
            entrance_active=true;detail_progress=0;
            // The original three tracks run at frames 0..6, 2..8 and 4..10.
            // Seed the first submitted pose offscreen even when the guest has
            // already advanced its animation before the first draw.
            if(entrance_panel!=panel||entrance_last_time<0||entrance_time<entrance_last_time)
                entrance_first_time=std::min(entrance_time,5.f);
            entrance_panel=panel;entrance_last_time=entrance_time;
            float ease=entrance_ease.load();
            ease=std::isfinite(ease)?std::clamp(ease,1.f,3.f):2.f;
            const auto& p=frame[Files];
            // Includes the widened right edge and margin for the plate rim.
            // Travel is independent of the user-scaled native 320px movement.
            const float distance=std::max(0.f,448.f-p.x+32.f*p.scale);
            for(unsigned i=0;i<3;++i) {
                const float begin=entrance_first_time+2*i,end=6.f+2*i;
                const float t=std::clamp((entrance_time-begin)/(end-begin),0.f,1.f);
                stone_slide[i]=distance*std::pow(1-t,ease);
            }
            entrance_pending=entrance_time<10;
        } else {entrance_panel=0;entrance_last_time=-1;}
        files_slide=-(32+std::max(0.f,frame[Files].x+320*frame[Files].scale))*detail_progress;
        summary_slide=-(32+std::max(0.f,frame[Summary].x+400*frame[Summary].scale))*(1-detail_progress);
        actions_slide=(459-std::min(0.f,frame[Actions].x))*(1-detail_progress);
        presentation=true;
    }
    NativeInventory::Hook::Affine Mapping(unsigned role) const {
        const auto& p=frame[role];
        // Before the native entrance settles (including out-of-range stone X),
        // the summary must already be offscreen, never at its configured destination.
        const float dx=role==Summary?(presentation?summary_slide:
            -(32+std::max(0.f,p.x+400*p.scale))):
            (presentation&&role==Files?files_slide:0);
        return {p.scale,0,0,p.scale,p.x+dx,p.y};
    }
    template<class M> bool StoneAnchors(M& m,U32 group) {
        route_status="file plate anchors";
        stone_anchors=false;
        auto flags=m.Read(group+0xd4);if(!flags)return false;
        // out_for_option/in_for_option hide null_plate_g. Native calculation
        // can leave its dormant cards with stale/unordered transforms; they
        // must not veto the visible summary, prompt and action-button layout.
        if(!(*flags&1))return true;
        auto count=m.Read(group+0x18),list=m.Read(group+0x14);
        if(!count||*count!=3||!list||!Pointer(*list,12))return false;
        for(unsigned i=0;i<3;++i) {
            auto child=m.Read(*list+i*4);if(!child)return false;
            auto layout=m.Read(*child+0x20),y=m.Read(*child+0xfc);
            if(!layout||!LayoutNamed(m,*layout,"FilePlate")||!y)return false;
            stone_y[i]=std::bit_cast<float>(*y);
            if(!std::isfinite(stone_y[i])||(i&&stone_y[i]<=stone_y[i-1]))return false;
        }
        stone_anchors=true;return true;
    }
    template<class M> bool Stones(M& m,U32 group,std::vector<Write>& writes,std::vector<U32>& seen) {
        auto flags=m.Read(group+0xd4);if(!flags)return false;
        if(!(*flags&1))return true;
        if(frame_spacing==0&&!presentation)return helper.Transform(m,group,writes,seen,Mapping(Files));
        auto list=m.Read(group+0x14);if(!list)return false;
        for(unsigned i=0;i<3;++i) {
            auto child=m.Read(*list+i*4);if(!child)return false;
            auto pose=Mapping(Files);pose.ty+=i*frame_spacing*pose.yy;
            if(presentation)pose.tx-=pose.xx*stone_native_x[i];
            if(entrance_active)pose.tx+=stone_slide[i];
            if(!helper.Transform(m,*child,writes,seen,pose))return false;
        }
        return true;
    }
    template<class M> std::optional<float> CursorTop(M& m,U32 group) {
        auto list=m.Read(group+0x14),count=m.Read(group+0x18);
        if(!list||!count||*count>32||!Pointer(*list,*count*4))return {};
        for(unsigned i=0;i<*count;++i) {
            auto base=m.Read(*list+i*4);if(!base||!Named(m,*base,"cursor_base"))continue;
            auto panes=m.Read(*base+0x14),n=m.Read(*base+0x18);
            if(!panes||!n||!*n||*n>32||!Pointer(*panes,*n*4))return {};
            std::optional<float> top;
            for(unsigned j=0;j<*n;++j) {
                auto pane=m.Read(*panes+j*4);if(!pane)return {};
                auto rect=m.Read(*pane+0x28);if(!rect||!Pointer(*rect,0x28))return {};
                std::array<float,7> v{};
                const std::array<U32,7> at{*rect+0x14,*rect+0x18,*rect+0x20,*rect+0x24,*pane+0xf0,*pane+0xf4,*pane+0xfc};
                for(unsigned k=0;k<at.size();++k){auto word=m.Read(at[k]);if(!word)return {};v[k]=std::bit_cast<float>(*word);if(!std::isfinite(v[k]))return {};}
                for(float x:{v[0],v[0]+v[2]})for(float y:{v[1],v[1]+v[3]}) {
                    const float cy=v[4]*x+v[5]*y+v[6];top=top?std::min(*top,cy):cy;
                }
            }
            return top;
        }
        return {};
    }
    template<class Memory> bool Cursors(Memory& m,U32 group,std::vector<Write>& writes,std::vector<U32>& seen) {
        route_status="cursor branches";
        auto count=m.Read(group+0x18),list=m.Read(group+0x14);
        if(!count||*count!=4||!list||!Pointer(*list,16))return false;
        constexpr std::array<const char*,4> names{"cursor_option_g","cursor_copy_del_g","cursor_start_g","cursor_plate_g"};
        unsigned found=0;
        for(unsigned i=0;i<4;++i) {
            auto child=m.Read(*list+i*4);if(!child)return false;
            int role=-1;for(unsigned j=0;j<4;++j)if(Named(m,*child,names[j]))role=j;
            if(role<0||(found&(1u<<role)))return false;found|=1u<<role;
            if(role==3&&entrance_pending) {
                auto flags=m.Read(*child+0xd4);
                if(!flags||!helper.Stage(m,writes,*child+0xd4,*flags&~1u))return false;
                continue;
            }
            auto pose=Mapping(role==3?Files:Actions);
            if(role==3&&frame_spacing>0&&stone_anchors) {
                auto y=m.Read(*child+0xfc);if(!y)return false;
                const float cy=CursorTop(m,*child).value_or(std::bit_cast<float>(*y));
                if(!std::isfinite(cy))return false;
                unsigned index=0;
                for(unsigned j=1;j<3;++j)if(std::abs(cy-stone_y[j])<std::abs(cy-stone_y[index]))index=j;
                pose.ty+=index*frame_spacing*pose.yy;
            }
            if(presentation&&role==2)pose.tx+=actions_slide-pose.xx*start_native_x;
            if(!helper.Transform(m,*child,writes,seen,pose))return false;
        }
        return found==15;
    }
    template<class Memory> bool Prepare(Memory& m,U32 layout,U32 screen,std::vector<Write>& writes) {
        route_status=layout==summary?"summary geometry":layout==popup?"popup geometry":"file panel structure";
        std::vector<U32> seen;
        if(layout==summary&&summary)return screen<4&&helper.Transform(m,layout+8,writes,seen,Mapping(Summary));
        if(layout==popup&&popup) {
            if(screen==5) {
                auto flags=m.Read(layout+8+0xd4);
                return flags&&helper.Stage(m,writes,layout+8+0xd4,*flags&~1u);
            }
            if(screen!=3)return false;
            auto list=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
            if(!list||!Pointer(*list)||!count||*count!=1)return false;
            auto all=m.Read(*list);if(!all||!Named(m,*all,"all"))return false;
            list=m.Read(*all+0x14);count=m.Read(*all+0x18);
            if(!list||!count||*count!=13||!Pointer(*list,52))return false;
            for(unsigned i=0;i<*count;++i) {
                auto child=m.Read(*list+i*4);if(!child)return false;
                if(Named(m,*child,"black_screen_g")) {
                    // Native bottom dimmer is 320x240. Top spans 0..400,
                    // or -13.333..413.333 with the widescreen projection.
                    const float width=wide?240.f*16.f/9.f:400.f;
                    if(!helper.Transform(m,*child,writes,seen,
                        {width/320.f,0,0,1,(400.f-width)*.5f,0}))return false;
                    continue;
                }
                if(!helper.Transform(m,*child,writes,seen,Mapping(Popup)))return false;
            }
            return true;
        }
        if(layout!=panel||(screen!=3&&screen!=5))return false;
        auto children=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
        if(!children||!Pointer(*children)||!count||*count!=1)return false;
        auto all=m.Read(*children);if(!all||!Named(m,*all,"all"))return false;
        children=m.Read(*all+0x14);count=m.Read(*all+0x18);
        if(!children||!Pointer(*children,40)||!count||*count!=10)return false;
        constexpr std::array<const char*,10> names{"cursor_g","black_screen","popup_window_g","message_g","delete_g","copy_g","start_g","plate_detail_g","null_plate_g","btn_return_l"};
        if(frame_spacing>0||presentation) {
            bool found=false;
            for(unsigned i=0;i<10;++i) {
                auto node=m.Read(*children+4*i);
                if(node&&Named(m,*node,"null_plate_g")){if(!StoneAnchors(m,*node))return false;found=true;}
            }
            if(!found)return false;
        }
        unsigned found=0;float local_visibility=1.f;
        for(unsigned i=0;i<10;++i) {
            auto child=m.Read(*children+i*4);if(!child)return false;
            int role=-1;for(unsigned j=0;j<10;++j)if(Named(m,*child,names[j]))role=j;
            if(role<0||(found&(1u<<role)))return false;found|=1u<<role;
            route_status=names[role];
            if(role==1&&screen==3) {
                // out_menu_fileselect_ani / out_name_check_ani animate this
                // local cover, not the drawable manager's full-screen quad.
                auto flags=m.Read(*child+0xd4);if(!flags)return false;
                if(*flags&1u) {
                    float cover=0.f;
                    for(U32 offset:{0x128u,0x12cu,0x130u,0x134u}) {
                        auto bits=m.Read(*child+offset);if(!bits)return false;
                        const float alpha=std::bit_cast<float>(*bits);
                        if(!std::isfinite(alpha)||alpha<0||alpha>1)return false;
                        cover=std::max(cover,alpha);
                    }
                    local_visibility=1.f-cover;
                }
            }
            const bool moved=role==0||role==3||role==4||role==5||role==6||role==8;
            if(moved!=(screen==3)) {
                auto flags=m.Read(*child+0xd4);
                if(!flags||!helper.Stage(m,writes,*child+0xd4,*flags&~1u))return false;
            } else if(screen==3) {
                if(role==0){if(!Cursors(m,*child,writes,seen))return false;}
                else if(role==8){if(!Stones(m,*child,writes,seen))return false;}
                else {
                    auto pose=Mapping(role==3?Prompt:Actions);
                    if(presentation&&role==6)pose.tx+=actions_slide-pose.xx*start_native_x;
                    if(!helper.Transform(m,*child,writes,seen,pose))return false;
                }
            }
        }
        // These branches moved above the native bottom-screen fade quad.
        // Follow that same cover, including embedded FilePlate layouts.
        const float opacity=visibility*local_visibility;
        // Capture before Begin hides the local cover on the relocated top copy.
        // Appended prompts must follow the same native exit/entrance fade.
        if(screen==3)prompt_opacity=opacity;
        if(screen==3&&opacity!=1.f)for(auto node:seen)
            for(U32 offset:{0x128u,0x12cu,0x130u,0x134u}) {
                auto bits=m.Read(node+offset);if(!bits)return false;
                const float alpha=std::bit_cast<float>(*bits);
                if(!std::isfinite(alpha)||alpha<0||alpha>1)return false;
                if(!helper.Stage(m,writes,node+offset,std::bit_cast<U32>(alpha*opacity)))return false;
            }
        return found==1023;
    }
    template<class Memory> void Pass(Memory& m,U32 manager,U32 screen) {
        // The title strip is shared by gameplay menus. Resolve ownership from
        // this frame's queues on every pass, including the first frame on exit.
        header_active=false;
        if(screen<4&&Pointer(manager,0x20)) {
            auto bottom=m.Read(manager+28);NativeInventory::Hook::Buckets entries;
            if(bottom&&helper.ReadQueue(m,*bottom,entries)) {
                bool file=false,options=false;
                for(const auto& bucket:entries)for(U32 entry:bucket) {
                    file|=LayoutNamed(m,entry,"FileSelect");
                    options|=LayoutNamed(m,entry,"MenuOption");
                }
                header_active=file&&!options;
            }
        }
        pass=screen;if(screen!=5)return;
        route_status="disabled / invalid manager";
        active=false;panel=summary=popup=0;if(!enabled.load()||!Pointer(manager,0x20))return;
        route_status="editor settings";
        frame_spacing=stone_spacing.load();
        if(!std::isfinite(frame_spacing)||frame_spacing<0||frame_spacing>80)return;
        for(unsigned i=0;i<Count;++i) {
            auto& p=frame[i];p={poses[i].x.load(),poses[i].y.load(),poses[i].scale.load()};
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale))return;
            p.x=std::clamp(p.x,-512.f,512.f);p.y=std::clamp(p.y,-512.f,512.f);p.scale=std::clamp(p.scale,.1f,2.f);
        }
        route_status="draw queues";
        auto bottom=m.Read(manager+28),top=m.Read(manager+20),detail=m.Read(manager+16);
        NativeInventory::Hook::Buckets from,to,up;
        if(!bottom||!top||!detail||*bottom==*top||*detail==*top||*detail==*bottom||!helper.ReadQueue(m,*bottom,from)||
           !helper.ReadQueue(m,*top,to)||!helper.ReadQueue(m,*detail,up))return;
        route_status="file panel missing or duplicate";
        unsigned priority=0;
        for(unsigned i=0;i<16;++i)for(auto entry:from[i])if(LayoutNamed(m,entry,"FileSelect")) {
            if(panel)return;panel=entry;priority=i;
        }
        if(!panel)return;
        route_status="panel already on top";
        for(const auto& bucket:to)if(std::find(bucket.begin(),bucket.end(),panel)!=bucket.end())return;
        route_status="duplicate summary";
        for(const auto& bucket:up)for(auto entry:bucket)if(LayoutNamed(m,entry,"UpPlateDetail")) {
            if(summary)return;summary=entry;
        }
        if(summary)for(const auto& bucket:to)
            if(std::find(bucket.begin(),bucket.end(),summary)!=bucket.end())return;
        const unsigned added=summary?2:1;
        route_status="top queue capacity";if(to[priority].size()+added>32)return;
        route_status="duplicate popup";
        for(const auto& bucket:from)for(auto entry:bucket)if(LayoutNamed(m,entry,"PopupWindow")) {
            if(popup)return;popup=entry;
        }
        route_status="popup queue";
        if(popup) {
            if(to[15].size()+(priority==15?added:0)>=32)return;
            for(const auto& bucket:to)if(std::find(bucket.begin(),bucket.end(),popup)!=bucket.end())return;
        }
        ReadPresentation(m);
        visibility=NativeStartup::BottomVisibility(m);
        std::vector<Write> preview;
        if(!Prepare(m,panel,3,preview))return;
        if(summary&&!Prepare(m,summary,3,preview))return;
        if(popup&&!Prepare(m,popup,3,preview))return;
        route_status="queue destination";
        // Draw the summary after the stones so it covers them throughout the
        // selection transition. Keep bottom instances for Back/native input.
        to[priority].push_back(panel);
        if(summary) {
            to[priority].push_back(summary);
            for(auto& bucket:up)std::erase(bucket,summary);
        }
        if(popup)to[15].push_back(popup);
        std::vector<Write> queue_writes;
        const auto stage_queue=[&](U32 queue,const NativeInventory::Hook::Buckets& buckets) {
            U32 total=0;
            for(unsigned p=0;p<16;++p) {
                const auto& bucket=buckets[p];total+=U32(bucket.size());
                for(unsigned i=0;i<bucket.size();++i)
                    if(!helper.Stage(m,queue_writes,queue+4+p*0x80+i*4,bucket[i]))return false;
                if(!helper.Stage(m,queue_writes,queue+0x804+p*4,U32(bucket.size())))return false;
            }
            return helper.Stage(m,queue_writes,queue+0x844,total);
        };
        if(!stage_queue(*top,to)||(summary&&!stage_queue(*detail,up)))return;
        for(const auto& w:queue_writes)m.Write(w.address,w.after);
        active=true;route_status="active";
    }
    template<class Memory> void Begin(Memory& m,U32 layout,bool widescreen=false) {
        wide=widescreen;
        frames.emplace_back();
        if(header_active&&LayoutNamed(m,layout,"SubMenuTitle")) {
            const HeaderBackground p{header_x.load(),header_y.load(),header_scale_x.load(),header_scale_y.load()};
            const HeaderTitle t{title_x.load(),title_y.load(),title_scale.load()};
            for(float v:{p.x,p.y,p.scale_x,p.scale_y,t.x,t.y,t.scale})if(!std::isfinite(v))return;
            if(std::abs(p.x)>512||std::abs(p.y)>512||p.scale_x<.01f||p.scale_x>3||p.scale_y<.01f||p.scale_y>3||
               std::abs(t.x)>512||std::abs(t.y)>512||t.scale<.01f||t.scale>3)return;
            if(p.x==0&&p.y==0&&p.scale_x==1&&p.scale_y==1&&t.x==0&&t.y==0&&t.scale==1)return;
            auto list=m.Read(layout+0x154),count=m.Read(layout+0x158);
            if(!list||!count||*count>256||!Pointer(*list,*count*4))return;
            std::vector<Write> writes;std::vector<U32> seen;unsigned found=0;
            for(unsigned i=0;i<*count;++i) {
                auto node=m.Read(*list+4*i);if(!node)return;
                const bool plate=Named(m,*node,"menu_title_base"),shadow=Named(m,*node,"ber_shadow");
                const bool title=Named(m,*node,"title_text")||Named(m,*node,"title_text_shadow")||
                    Named(m,*node,"title_text_hint_menu");
                if(!plate&&!shadow&&!title)continue;
                if(plate) { if(found&1)return; found|=1; }
                if(shadow) { if(found&2)return; found|=2; }
                // Scale around the screen's horizontal center and top edge.
                // The shadow follows the plate; title and prompt are siblings.
                const bool is_title=Named(m,*node,"title_text")||Named(m,*node,"title_text_shadow")||
                    Named(m,*node,"title_text_hint_menu");
                const float sx=is_title?p.scale_x*t.scale:p.scale_x;
                const float sy=is_title?p.scale_y*t.scale:p.scale_y;
                const float tx=is_title?200*(1-sx)+p.x+t.x:200*(1-p.scale_x)+p.x;
                const float ty=is_title?p.y+t.y:p.y;
                if(!helper.Transform(m,*node,writes,seen,{sx,0,0,sy,tx,ty}))return;
            }
            if((found&3)!=3)return;
            for(const auto& w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
            return;
        }
        if(!active||!(((layout==panel||(popup&&layout==popup))&&(pass==3||pass==5))||(summary&&layout==summary&&pass<4)))return;
        std::vector<Write> writes;if(!Prepare(m,layout,pass,writes))return;
        for(const auto& w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
    }
    template<class Memory> void End(Memory& m) {
        if(frames.empty())return;
        for(const auto& w:frames.back())m.Write(w.address,w.before);frames.pop_back();
    }
};
}
