// Ported from mm3d-dekui src/mm3d/native_notebook.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
#include "notebook_rows.h"
#include "schedule_rows.h"
#include "schedule_detail.h"
#include "notebook_prompt.h"

namespace Mm3dDekui::NativeNotebook {
using namespace NativeInventory;
constexpr unsigned Count=11; // Slot 6 is retained for legacy saved down-arrow settings.
struct Options {
    bool enabled=true;
    bool expanded_rows=true;
    bool event_prompt=true,map_prompt=true,schedule_prompt=true;
    float map_prompt_y=-20,schedule_prompt_y=-40;
    float prompt_x=0,prompt_y=0,prompt_scale=1,prompt_icon_scale=1;
    float popup_width=1.f,popup_height=1.f;
    std::array<GearMenu::Pose,Count> poses{{{173.6f,60.8f,.7f},{0,20,.5f},{0,20,.5f},{0,30,.6f},{240,30,.5f},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{240,50,.5f}}};
};
inline std::atomic<bool> enabled{true};
inline std::atomic<unsigned> schedule_status{0};
inline std::atomic<float> popup_width{1.f},popup_height{1.f};
inline std::array<GearMenu::AtomicPose,Count> poses;
inline void Apply(const Options& o) {
    enabled=o.enabled;NotebookRows::enabled=o.expanded_rows;
    popup_width=o.popup_width;popup_height=o.popup_height;
    NotebookPrompt::enabled=o.event_prompt;
    NotebookPrompt::icon_scale=o.prompt_icon_scale;
    NotebookPrompt::map_enabled=o.map_prompt;
    NotebookPrompt::schedule_enabled=o.schedule_prompt;NotebookPrompt::schedule_y=o.schedule_prompt_y;
    NotebookPrompt::map_pose.x=o.prompt_x;NotebookPrompt::map_pose.y=o.map_prompt_y;NotebookPrompt::map_pose.scale=o.prompt_scale;
    NotebookPrompt::pose.x=o.prompt_x;NotebookPrompt::pose.y=o.prompt_y;NotebookPrompt::pose.scale=o.prompt_scale;
    for(unsigned i=0;i<Count;++i){poses[i].x=o.poses[i].x;poses[i].y=o.poses[i].y;poses[i].scale=o.poses[i].scale;}
}
constexpr U32 PC=0x005b416c,Word=0xe1a04000,SVC=0xffffd8; // ref 0x5b3ffc
inline U32 Instruction(U32 pc,U32 word){return pc==PC&&word==Word?0xef000000|SVC:word;}
template<class Memory> bool CodeMatches(Memory& m) {
    return m.Read(PC)==std::optional<U32>(Word)&&m.Read(PC+4)==std::optional<U32>(0xe5910004)&&
        m.Read(PC+8)==std::optional<U32>(0xe1a05001)&&m.Read(PC+12)==std::optional<U32>(0xe590016c);
}
// Restoration UiScheduleScreenUpdate: TST r3,#Select immediately before
// checking the map button's display animation and invoking its touch callback.
constexpr U32 MapInputPC=0x007d04c4,MapInputWord=0xe3130004,MapInputSVC=0xffffd2; // ref 0x7d04c4 (Restoration source port)
inline U32 MapInstruction(U32 pc,U32 word) {
    if(pc==0x007d04b0&&word==0xe3130b02)return 0xefffffd3; // ref 0x7d04b0 (Restoration source port)
    return pc==MapInputPC&&word==MapInputWord?0xef000000|MapInputSVC:word;
}
template<class M> bool MapCodeMatches(M& m) {
    return m.Read(0x007d04b0)==std::optional<U32>(0xe3130b02)&& // ref 0x7d04b0 (Restoration source port)
        m.Read(0x007d04b8)==std::optional<U32>(0x13833b02)&& // ref 0x7d04b8 (Restoration source port)
        m.Read(MapInputPC)==std::optional<U32>(MapInputWord)&&
        m.Read(0x007d04c8)==std::optional<U32>(0x0affffd4)&& // ref 0x7d04c8 (Restoration source port)
        m.Read(0x007d04cc)==std::optional<U32>(0xe5942764)&& // ref 0x7d04cc (Restoration source port)
        m.Read(0x007d04d0)==std::optional<U32>(0xe5943768)&& // ref 0x7d04d0 (Restoration source port)
        m.Read(0x007d04ec)==std::optional<U32>(0xe594126c)&& // ref 0x7d04ec (Restoration source port)
        m.Read(0x007d04f0)==std::optional<U32>(0xe59436f0); // ref 0x7d04f0 (Restoration source port)
}
template<class M> U32 MapInputFlags(M& m,U32 owner,U32 buttons,U32 cpsr) {
    auto state=m.Read(owner+0xc),layout=m.Read(owner+0x40);
    const bool event_notes=state&&(*state&255)==0&&layout&&LayoutNamed(m,*layout,"MenuQuestlist");
    const U32 result=buttons&(event_notes?0x400u:4u);
    // Emulate the replaced TST's N/Z. Its unrotated immediate preserves C;
    // V and all control/status bits also remain intact. No input memory writes.
    return (cpsr&~0xc0000000u)|(result?0u:0x40000000u);
}
// Restoration forwards Y into the native View Schedule touch-button flag.
// Preserve its Y-only input; R must not synthesize the View Schedule flag.
template<class M> U32 ScheduleInputFlags(M&,U32,U32 buttons,U32 cpsr) {
    const U32 result=buttons&0x800u;
    // Original TST #0x800 uses a rotated immediate: N=0,C=0; preserve V.
    return (cpsr&~0xe0000000u)|(result?0u:0x40000000u);
}
struct Hook {
    NativeInventory::Hook helper;
    NotebookPrompt::Hook prompt_hook;
    NotebookRows::Hook row_window;
    ScheduleRows::Hook schedule_rows;
    U32 panel=0,pass=~0u,notebook_owner=0;
    float detail_opacity=1.f;
    bool active=false,schedule=false;
    U32 schedule_panel=0;
    std::vector<U32> schedule_list,schedule_bar;
    std::vector<U32> list,bar,routed;
    std::array<GearMenu::Pose,Count> frame{};
    std::vector<std::vector<NativeInventory::Hook::Write>> frames;
    template<class Memory> void Capture(Memory& m,U32 owner) {
        notebook_owner=owner;
        panel=0;list.clear();bar.clear();active=false;routed.clear();schedule=false;
        schedule_panel=0;schedule_list.clear();schedule_bar.clear();
        if(Pointer(owner,0x6b4)) {
            auto sp=m.Read(owner+0x44),up=m.Read(owner+0x38);
            auto cursor=m.Read(owner+0x508),pointer=m.Read(owner+0x514),touch=m.Read(owner+0x6b0);
            if(sp&&up&&cursor&&pointer&&touch&&LayoutNamed(m,*sp,"MenuSchedule")&&
               LayoutNamed(m,*up,"UpSchedule")&&LayoutNamed(m,*cursor,"CursorScheFrame")&&
               LayoutNamed(m,*pointer,"CursorPointer")&&LayoutNamed(m,*touch,"CursorTouch")) {
                schedule_panel=*sp;schedule_list={*up,*cursor,*pointer};schedule_bar={*touch};
            }
        }
        if(!Pointer(owner,0x354)) return;
        auto p=m.Read(owner+0x40);if(!p||!LayoutNamed(m,*p,"MenuQuestlist")) return;
        const auto add=[&](U32 offset,const char* name,std::vector<U32>& out) {
            auto value=m.Read(owner+offset);if(!value||!LayoutNamed(m,*value,name)) return false;
            out.push_back(*value);return true;
        };
        if(!add(0x34,"UpQuestlist",list)) return;
        for(unsigned i=0;i<9;++i) if(!add(0x7c+i*4,"QuestPlate",list)) return;
        for(unsigned i=0;i<3;++i) if(!add(0x1c8+i*4,"QuestCategoryPlate",list)) return;
        if(!add(0x1d4,"CursorQuestPlate",list)) return;
        for(unsigned i=0;i<6;++i) if(!add(0x278+i*4,"ChrIconButton",bar)) return;
        if(!add(0x350,"CursorEventCharSeal",bar)) return;
        // Button prompts are standalone layouts bound to character anchors.
        // Validate the binding rather than depending on the shared prompt name.
        for(unsigned i=0;i<6;++i) {
            auto prompt=m.Read(owner+0x2a8+4*i),anchor=m.Read(owner+0x290+4*i);
            if(!prompt||!anchor||!Pointer(*prompt,0x184)||!Named(m,*anchor,"disp_a_icon_n"))continue;
            if(m.Read(*prompt+0x180)==anchor)bar.push_back(*prompt);
        }
        panel=*p;
    }
    NativeInventory::Hook::Affine Mapping(unsigned role) const {
        const auto& p=frame[role];return {p.scale,0,0,p.scale,p.x,p.y};
    }
    template<class M> void MapFade(M& m) {
        detail_opacity=1.f;
        // The notebook has no owner until it is opened. Do not probe fields
        // through a null/stale address on every gameplay frame.
        if(!Pointer(notebook_owner,0x5c))return;
        auto map=m.Read(notebook_owner+0x54),state=m.Read(notebook_owner+0x4c),player=m.Read(notebook_owner+0x58);
        if(!map||!state||!player||!LayoutNamed(m,*map,"UpEventMap"))return;
        if(!Pointer(*player,0xc))return;
        auto playing=m.Read(*player+8);if(!playing)return;
        if(*state==255&&!(*playing&255))return;
        auto list=m.Read(*map+8+0x14),count=m.Read(*map+8+0x18);
        if(!list||!count||*count!=1)return;
        auto all=m.Read(*list);if(!all||!Named(m,*all,"all"))return;
        list=m.Read(*all+0x14);count=m.Read(*all+0x18);if(!list||!count||*count>32)return;
        float opacity=0;
        for(unsigned i=0;i<*count;++i) {
            auto node=m.Read(*list+i*4);if(!node)return;
            if(!Named(m,*node,"blackboard_g")&&!Named(m,*node,"event_map_g"))continue;
            auto alpha=m.Read(*node+0x134);if(!alpha)return;
            float a=std::bit_cast<float>(*alpha);if(!std::isfinite(a))return;
            opacity=std::max(opacity,std::clamp(a,0.f,1.f));
        }
        detail_opacity=1.f-opacity;
    }
    template<class M> bool DetailTransform(M& m,U32 node,std::vector<NativeInventory::Hook::Write>& writes,
                                         std::vector<U32>& seen,const NativeInventory::Hook::Affine& mapping) {
        const auto start=seen.size();
        if(!helper.Transform(m,node,writes,seen,mapping))return false;
        if(detail_opacity==1.f)return true;
        for(auto i=start;i<seen.size();++i) {
            // Four computed corner opacities consumed by native pane emission.
            for(U32 offset:{0x128u,0x12cu,0x130u,0x134u}) {
                auto bits=m.Read(seen[i]+offset);if(!bits)return false;
                const float alpha=std::bit_cast<float>(*bits);
                if(!std::isfinite(alpha)||alpha<0||alpha>1)return false;
                if(!helper.Stage(m,writes,seen[i]+offset,std::bit_cast<U32>(alpha*detail_opacity)))return false;
            }
        }
        return true;
    }
    template<class M> bool Popup(M& m,U32 node,std::vector<NativeInventory::Hook::Write>& writes,std::vector<U32>& seen) {
        const auto mapping=Mapping(10);
        const auto start=writes.size();
        if(!helper.Transform(m,node,writes,seen,mapping))return false;
        const float width=std::clamp(popup_width.load(),.5f,3.f),height=std::clamp(popup_height.load(),.5f,3.f);
        if(!std::isfinite(width)||!std::isfinite(height))return false;
        if(width==1.f&&height==1.f)return true;
        // Resize the backing box about its center, leaving text and A glyph
        // at the ordinary popup scale. Compose with the staged screen pose.
        auto children=m.Read(node+0x14),count=m.Read(node+0x18);
        if(!children||!count||*count>16)return false;
        U32 box=0;
        for(unsigned i=0;i<*count;++i){auto child=m.Read(*children+4*i);if(!child)return false;if(Named(m,*child,"popup_window00"))box=*child;}
        if(!box)return false;
        for(auto i=start;i<writes.size();++i)m.Write(writes[i].address,writes[i].after);
        auto x=m.Read(box+0xec),y=m.Read(box+0xfc);
        std::vector<NativeInventory::Hook::Write> size;std::vector<U32> visited;
        const bool ok=x&&y&&helper.Transform(m,box,size,visited,{width,0,0,height,
            std::bit_cast<float>(*x)*(1-width),std::bit_cast<float>(*y)*(1-height)});
        for(auto i=writes.size();i>start;--i)m.Write(writes[i-1].address,writes[i-1].before);
        if(!ok)return false;
        writes.insert(writes.end(),size.begin(),size.end());return true;
    }
    template<class Memory> bool Prepare(Memory& m,U32 layout,U32 screen,std::vector<NativeInventory::Hook::Write>& writes) {
        std::vector<U32> seen;
        if(layout!=panel) {
            const bool event=std::find(list.begin(),list.end(),layout)!=list.end();
            auto mapping=Mapping(schedule?(event?3:4):(event?0:2));
            if(schedule&&!event&&screen==3) {
                // The separately submitted detail pointer needs the same
                // horizontal normalization as its attached character timeline.
                std::vector<NativeInventory::Hook::Write> detail;
                if(ScheduleDetail::Prepare(m,panel,schedule_rows.owner,helper,detail)&&detail.size()>=4) {
                    const auto& x=detail[3]; // first transformed node's world X
                    mapping.tx+=mapping.xx*(std::bit_cast<float>(x.after)-std::bit_cast<float>(x.before));
                }
            }
            return screen<4&&(event?helper.Transform(m,layout+8,writes,seen,mapping):DetailTransform(m,layout+8,writes,seen,mapping));
        }
        auto children=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
        if(!children||!Pointer(*children)||!count||*count!=1)return false;
        auto all=m.Read(*children);if(!all||!Named(m,*all,"all"))return false;
        children=m.Read(*all+0x14);count=m.Read(*all+0x18);
        if(schedule) {
            constexpr std::array<const char*,15> names{"current_time01","elf_seal_free","elf_seal_fix",
                "char_seal_effect","char_seal_g","alarm_set_anim_g","event_time_g","popup_window00_g",
                "black_screen_g","sche_base_anim","sche_quest_detail","under_tub_g",
                "touch_rect_alarm_l","touch_rect_window00_l","touch_rect_center_l"};
            if(!children||!Pointer(*children,60)||!count||*count!=15)return false;
            unsigned found=0;
            for(unsigned i=0;i<15;++i) {
                auto child=m.Read(*children+i*4);if(!child)return false;
                int role=-1;for(unsigned j=0;j<15;++j)if(Named(m,*child,names[j]))role=j;
                if(role<0||(found&(1u<<role)))return false;found|=1u<<role;
                // Caption, portrait, and their associated native effects/time labels.
                const bool moved=role<=7||role==9||role==10||role==13;
                if(moved!=(screen==3)) {
                    auto flags=m.Read(*child+0xd4);
                    if(!flags||!helper.Stage(m,writes,*child+0xd4,*flags&~1u))return false;
                } else if(screen==3) {
                    if(role==7) {if(!Popup(m,*child,writes,seen))return false;}
                    else if(!DetailTransform(m,*child,writes,seen,Mapping(4)))return false;
                }
            }
            return found==32767;
        }
        if(!children||!Pointer(*children,32)||!count||*count!=8)return false;
        constexpr std::array<const char*,8> names{"attention_effect_g","touch_rect_window00_l","event_item_g","quest_list_detail","btn_return_l","btn_map_g","line_base_g","bb_btn_schedule_g"};
        unsigned found=0;
        for(unsigned i=0;i<8;++i) {
            auto child=m.Read(*children+i*4);if(!child)return false;
            int role=-1;for(unsigned j=0;j<8;++j)if(Named(m,*child,names[j]))role=j;
            if(role<0||(found&(1u<<role)))return false;found|=1u<<role;
            const bool moved=role<4||role==6;
            if(moved!=(screen==3)) {
                auto flags=m.Read(*child+0xd4);
                if(!flags||!helper.Stage(m,writes,*child+0xd4,*flags&~1u))return false;
            } else if(screen==3&&!DetailTransform(m,*child,writes,seen,Mapping(1)))return false;
        }
        return found==255;
    }
    template<class Memory> void Pass(Memory& m,U32 manager,U32 screen) {
        pass=screen;if(screen!=5)return;
        active=false;routed.clear();schedule_status=0;NotebookPrompt::status=0;if(!enabled.load())return;
        for(unsigned i=0;i<Count;++i) {
            auto& p=frame[i];p={poses[i].x.load(),poses[i].y.load(),poses[i].scale.load()};
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale))return;
            p.x=std::clamp(p.x,-512.f,512.f);p.y=std::clamp(p.y,-512.f,512.f);p.scale=std::clamp(p.scale,.1f,2.f);
        }
        frame[2]=frame[1]; // One pose for caption, icons, cursor and A prompts.
        // Share the Event Notes pose without an extra Schedule-only offset.
        frame[4]=frame[1];
        MapFade(m);
        auto bottom=m.Read(manager+28),top=m.Read(manager+20);
        NativeInventory::Hook::Buckets from,to;
        if(!bottom||!top||*bottom==*top||!helper.ReadQueue(m,*bottom,from)||!helper.ReadQueue(m,*top,to))return;
        // Discover the active page from actual submissions. Optional cursors and
        // dormant Events layouts must not gate the Schedule page.
        U32 submitted_schedule=0;
        for(const auto& bucket:from)for(auto entry:bucket)if(LayoutNamed(m,entry,"MenuSchedule")) {
            if(submitted_schedule)return;submitted_schedule=entry;
        }
        if(submitted_schedule) {
            schedule_status=1;schedule=true;panel=submitted_schedule;list.clear();bar.clear();
            auto upper=m.Read(manager+16);NativeInventory::Hook::Buckets up;
            if(!upper||!helper.ReadQueue(m,*upper,up))return;
            bool grid=false;
            for(const auto& bucket:up)for(auto entry:bucket) {
                const bool is_grid=LayoutNamed(m,entry,"UpSchedule");
                if(is_grid||LayoutNamed(m,entry,"CursorScheFrame")||LayoutNamed(m,entry,"CursorPointer")) {
                    list.push_back(entry);grid|=is_grid;
                }
            }
            if(!grid)return;
            auto touch=schedule_rows.owner?m.Read(schedule_rows.owner+0x6b0):std::optional<U32>{};
            auto owner_panel=schedule_rows.owner?m.Read(schedule_rows.owner+0x44):std::optional<U32>{};
            for(const auto& bucket:from)for(auto entry:bucket)
                if(LayoutNamed(m,entry,"CursorTouch")||
                   (touch&&owner_panel&&*owner_panel==panel&&entry==*touch&&Pointer(entry,0x16c)))bar.push_back(entry);
            schedule_status=2;
        } else if(schedule) { schedule=false;panel=0;list.clear();bar.clear(); }
        if(!panel)return;
        unsigned panels=0;std::vector<U32> selected;
        for(unsigned i=0;i<16;++i) {
            std::vector<U32> keep;
            for(auto entry:from[i]) {
                if(entry==panel||std::find(bar.begin(),bar.end(),entry)!=bar.end()) {
                    if(std::find(selected.begin(),selected.end(),entry)!=selected.end())return;
                    for(const auto& bucket:to)if(std::find(bucket.begin(),bucket.end(),entry)!=bucket.end())return;
                    selected.push_back(entry);to[i].push_back(entry);
                    if(entry==panel){++panels;keep.push_back(entry);}
                } else keep.push_back(entry);
            }
            from[i]=std::move(keep);if(to[i].size()>32)return;
        }
        if(panels!=1)return;
        for(auto layout:selected){std::vector<NativeInventory::Hook::Write> w;if(!Prepare(m,layout,3,w)){if(schedule)schedule_status=3;return;}}
        for(auto layout:list){std::vector<NativeInventory::Hook::Write> w;if(!Prepare(m,layout,2,w)){if(schedule)schedule_status=4;return;}}
        for(auto [address,q]:{std::pair{*bottom,&from},std::pair{*top,&to}})
            for(unsigned p=0;p<16;++p)for(unsigned i=0;i<(*q)[p].size();++i)if(!m.Read(address+4+p*0x80+i*4))return;
        for(auto [address,q]:{std::pair{*bottom,&from},std::pair{*top,&to}}) {
            U32 total=0;
            for(unsigned p=0;p<16;++p){for(unsigned i=0;i<(*q)[p].size();++i)m.Write(address+4+p*0x80+i*4,(*q)[p][i]);
                m.Write(address+0x804+p*4,U32((*q)[p].size()));total+=(*q)[p].size();}
            m.Write(address+0x844,total);
        }
        routed=std::move(selected);active=true;if(schedule)schedule_status=5;
    }
    template<class Memory> bool Arrows(Memory& m,U32 layout,std::vector<NativeInventory::Hook::Write>& writes) {
        const bool events=LayoutNamed(m,layout,"UpQuestlist");
        if(!events&&!LayoutNamed(m,layout,"UpSchedule"))return true;
        auto children=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
        if(!children||!count||*count!=1)return false;
        auto all=m.Read(*children);if(!all||!Named(m,*all,"all"))return false;
        children=m.Read(*all+0x14);count=m.Read(*all+0x18);
        if(!children||!count||*count>32)return false;
        std::vector<U32> seen;
        for(unsigned i=0;i<*count;++i) {
            auto node=m.Read(*children+i*4);if(!node)return false;
            if(!Named(m,*node,"direct_up")&&!Named(m,*node,"direct_down"))continue;
            const auto& p=frame[events?5:9];
            const float scale=frame[events?0:3].scale;
            // Offsets in final top-screen coordinates, independent of list scale.
            if(!helper.Transform(m,*node,writes,seen,{1,0,0,1,p.x/scale,p.y/scale}))return false;
        }
        return true;
    }
    template<class Memory> bool ScrollbarPosition(Memory& m,U32 layout,std::vector<NativeInventory::Hook::Write>& writes) {
        const bool events=LayoutNamed(m,layout,"UpQuestlist");
        if(!events&&!LayoutNamed(m,layout,"UpSchedule"))return true;
        auto children=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
        if(!children||!count||*count!=1)return false;
        auto all=m.Read(*children);if(!all||!Named(m,*all,"all"))return false;
        children=m.Read(*all+0x14);count=m.Read(*all+0x18);
        if(!children||!count||*count>64)return false;
        for(unsigned i=0;i<*count;++i) {
            auto node=m.Read(*children+4*i);if(!node)return false;
            if(!Named(m,*node,"scroll_bar_l"))continue;
            // Normalize the complete rendered track to the Schedule pose.
            // Measure after row expansion; the two pages use different heights
            // and list scales. Their thumb fractions remain independent.
            std::vector<U32> seen;std::vector<NativeInventory::Hook::Write> probe;
            if(!helper.Transform(m,*node,probe,seen,{1,0,0,1,0,0}))return false;
            float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;
            float native_left=1e9f,native_top=1e9f,native_right=-1e9f,native_bottom=-1e9f;
            unsigned found=0;
            const auto f=[&](U32 at)->std::optional<float> {
                auto bits=m.Read(at);if(!bits)return {};
                float v=std::bit_cast<float>(*bits);return std::isfinite(v)?std::optional<float>(v):std::nullopt;
            };
            for(auto pane:seen) {
                const bool lower=Named(m,pane,"bb_pane02");
                if(!lower&&!Named(m,pane,"bb_pane00"))continue;
                auto geometry=m.Read(pane+0x28);if(!geometry||!Pointer(*geometry,0x28))return false;
                auto x=f(*geometry+0x14),y=f(*geometry+0x18),w=f(*geometry+0x20),h=f(*geometry+0x24);
                auto sx=f(pane+0xe0),sy=f(pane+0xf4),tx=f(pane+0xec),ty=f(pane+0xfc);
                if(!x||!y||!w||!h||!sx||!sy||!tx||!ty||*w<=0||*h<=0||*sx<=0||*sy<=0)return false;
                left=std::min(left,*tx+*sx**x);right=std::max(right,*tx+*sx*(*x+*w));
                top=std::min(top,*ty+*sy**y);bottom=std::max(bottom,*ty+*sy*(*y+*h));
                native_left=std::min(native_left,*x);native_right=std::max(native_right,*x+*w);
                const float extended_y=*y+(lower?120.f:0.f);
                native_top=std::min(native_top,extended_y);native_bottom=std::max(native_bottom,extended_y+*h);
                ++found;
            }
            if(found!=2||right<=left||bottom<=top)return false;
            const auto& target=frame[3];const auto& offset=frame[8];
            const float list_scale=frame[events?0:3].scale;
            if(list_scale<=0||target.scale<=0)return false;
            const float ax=(native_right-native_left)*target.scale/(list_scale*(right-left));
            const float ay=(native_bottom-native_top)*target.scale/(list_scale*(bottom-top));
            const float target_x=target.x+offset.x+native_left*target.scale;
            const float target_y=target.y+offset.y+native_top*target.scale;
            const auto& list_pose=frame[events?0:3];
            seen.clear();
            return helper.Transform(m,*node,writes,seen,{ax,0,0,ay,
                (target_x-list_pose.x)/list_scale-ax*left,
                (target_y-list_pose.y)/list_scale-ay*top});
        }
        return false;
    }
    template<class Memory> void Begin(Memory& m,U32 layout,bool widescreen=false) {
        frames.emplace_back();
        if(widescreen&&pass==2&&LayoutNamed(m,layout,"CommonUpBg")) {
            // Shared notebook backdrop is a 400x240 pane, independent of
            // event/schedule panel controls. Extend around the screen center.
            std::vector<NativeInventory::Hook::Write> writes;std::vector<U32> seen;
            constexpr float factor=16.f/15.f;
            if(!helper.Transform(m,layout+8,writes,seen,{factor,0,0,1,200.f*(1.f-factor),0}))return;
            for(const auto& w:writes)m.Write(w.address,w.after);
            frames.back()=std::move(writes);return;
        }
        // Schedule row geometry also applies when panel relocation is disabled.
        std::vector<NativeInventory::Hook::Write> schedule_writes;
        if(pass==2&&schedule_rows.Expand(m,layout,helper,schedule_writes)) {
            for(auto w:schedule_writes)m.Write(w.address,w.after);
            frames.back()=std::move(schedule_writes);
        }
        if(!active)return;
        const bool upper=pass<4&&std::find(list.begin(),list.end(),layout)!=list.end();
        const bool moved=(pass==3||pass==5)&&std::find(routed.begin(),routed.end(),layout)!=routed.end();
        if(!upper&&!moved)return;
        std::vector<NativeInventory::Hook::Write> extra;
        if(!schedule&&pass<4&&!row_window.Expand(m,layout,helper,extra))extra.clear();
        if(schedule&&pass==3&&layout==panel) {
            std::vector<NativeInventory::Hook::Write> detail;
            if(ScheduleDetail::Prepare(m,panel,schedule_rows.owner,helper,detail))
                extra.insert(extra.end(),detail.begin(),detail.end());
        }
        if(pass==2) {
            std::vector<NativeInventory::Hook::Write> arrows;
            if(Arrows(m,layout,arrows))extra.insert(extra.end(),arrows.begin(),arrows.end());
        }
        for(const auto& w:extra)m.Write(w.address,w.after);
        // Compose after the row hooks have resized/repositioned the track and
        // thumb, so translation cannot replace their extended geometry.
        if(pass==2) {
            std::vector<NativeInventory::Hook::Write> scrollbar;
            if(ScrollbarPosition(m,layout,scrollbar)) {
                for(auto w:scrollbar)m.Write(w.address,w.after);
                extra.insert(extra.end(),scrollbar.begin(),scrollbar.end());
            }
        }
        std::vector<NativeInventory::Hook::Write> writes;
        if(!Prepare(m,layout,pass,writes)) {
            for(auto i=extra.rbegin();i!=extra.rend();++i)m.Write(i->address,i->before);
            return;
        }
        frames.back().insert(frames.back().end(),extra.begin(),extra.end());
        for(const auto& w:writes)m.Write(w.address,w.after);frames.back().insert(frames.back().end(),writes.begin(),writes.end());
    }
    template<class Memory> U32 PromptRoot(Memory& m,U32 layout,U32 sp) {
        if(active&&pass==3&&layout==panel&&LayoutNamed(m,layout,schedule?"MenuSchedule":"MenuQuestlist")) {
            // Keep the explicit map overlay fade, but follow the page scrollbar
            // rather than a description that disappears on empty selections.
            const float opacity=prompt_hook.ScrollbarOpacity(m,notebook_owner,schedule).value_or(1.f)*detail_opacity;
            return prompt_hook.Build(m,layout,notebook_owner,sp,opacity);
        }
        prompt_hook.frames.emplace_back();return layout+8;
    }
    template<class Memory> void End(Memory& m) {
        if(frames.empty())return;
        for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);frames.pop_back();
    }
};
}
