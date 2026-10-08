// Ported from mm3d-dekui src/mm3d/native_item_present.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"

namespace Mm3dDekui::NativeItemPresent {
using namespace NativeInventory;
enum Part : unsigned { Background, Panel, Right, Prompt, Return, Confirm, Description, Count };
struct Options { ButtonPose help_pose{280,180,.8f};float help_spacing=6; bool enabled=true,horizontal=false; std::array<GearMenu::Pose,Count> poses{}; };
inline AtomicButtonPose help_pose{280,180,.8f};
inline std::atomic<float> help_spacing{6};
inline std::atomic<bool> enabled{true},horizontal{false};
inline std::array<GearMenu::AtomicPose,Count> poses;
inline void Apply(const Options& o) {
    help_pose.x=o.help_pose.x;help_pose.y=o.help_pose.y;help_pose.scale=o.help_pose.scale;help_spacing=o.help_spacing;
    enabled=o.enabled;horizontal=o.horizontal;
    for(unsigned i=0;i<Count;++i){poses[i].x=o.poses[i].x;poses[i].y=o.poses[i].y;poses[i].scale=o.poses[i].scale;}
}
// Exact Restoration v110 Give Item instruction-dismissal call, not global dialogue input.
constexpr U32 IntroPC=0x005bc900,IntroWord=0xebefc422,IntroSVC=0xffffb7; // ref 0x5bc790, 0xebefc4fe
inline U32 IntroInstruction(U32 pc,U32 word) {
    return pc==IntroPC&&word==IntroWord?0xef000000|IntroSVC:word;
}
template<class M> bool IntroCodeMatches(M& m) {
    for(auto [pc,word]:std::array<std::pair<U32,U32>,5>{{
        {IntroPC,IntroWord},{0x005bc8fc,0xe1a00004},{0x005bc904,0xe3500000}, // ref 0x5bc78c, 0x5bc794
        {0x005bc918,0xe3a01001},{0x005bc920,0xe5c5721c}}}) // ref 0x5bc7a8, 0x5bc7b0
        if(m.Read(pc)!=std::optional<U32>(word))return false;
    return true;
}
template<class M> bool SkipIntro(M& m,U32 owner) {
    if(!enabled.load()||!Pointer(owner,0x220))return false;
    auto layout=m.Read(owner+0x28),state=m.Read(owner+0x21c);
    return layout&&state&&(*state&255)==0&&LayoutNamed(m,*layout,"MenuItemPresen");
}
struct Hook {
    NativeInventory::Hook helper;
    using Write=NativeInventory::Hook::Write;
    U32 panel=0,owner=0,background=0,cursor=0,touch=0,pass=~0u;
    unsigned background_draw=0;
    bool transitioning=false;
    float transition_x=0;
    bool upper_background=false;
    std::array<U32,20> icons{};
    unsigned cursor_part=Panel,cursor_slot=0;
    bool frame_horizontal=false;
    std::array<GearMenu::Pose,Count> frame{};
    std::vector<U32> routed;
    std::vector<std::vector<Write>> frames;
    template<class M> void ReadTransition(M& m) {
        transitioning=false;transition_x=0;
        auto player=m.Read(owner+0x34);
        if(!player||!Pointer(*player,0x18))return;
        auto anim=m.Read(*player+0xc),time=m.Read(*player+0x10);
        if(!anim||!time||!Pointer(*anim,0x18))return;
        auto name=m.Read(*anim+8);if(!name)return;
        const bool opening=DialogueScale::Name(m,*name,"in_item_presen_anim");
        if(!opening&&!DialogueScale::Name(m,*name,"out_item_presen_anim"))return;
        const float f=std::bit_cast<float>(*time);
        if(!std::isfinite(f)||f<0||f>6)return;
        float right=0;
        for(unsigned i=0;i<Count;++i) {
            if(i==Description)continue;
            const auto& p=frame[i];
            // Screen-space rest bounds. Off-screen buttons must not accelerate
            // the visible panel merely because the user parked them far away.
            float x0=0,x1=320,y0=0,y1=240;
            if(i==Background){y0=-240;x1+=16*NativeInventory::shadow_width.load();}
            else if(i==Panel){x1=256;y1=200;}
            else if(i==Right){
                if(frame_horizontal){x1=200;y1=56;}
                else{x0=262;x1=318;y1=200;}
            } else if(i==Prompt){y0=200;}
            else if(i==Return||i==Confirm){y0=200;}
            const float l=p.x+x0*p.scale,r=p.x+x1*p.scale;
            const float top=p.y+y0*p.scale,bottom=p.y+y1*p.scale;
            if(top>=240||bottom<=0||l>=427||r<=0)continue;
            right=std::max(right,r);
        }
        transition_x=-(32+right)*(opening?1-f/6.f:f/6.f);
        transitioning=true;
    }
    template<class M> bool Move(M& m,U32 node,unsigned part,std::vector<Write>& writes,std::vector<U32>& seen,int slot=-1) {
        const auto& p=frame[part];
        NativeInventory::Hook::Affine pose{p.scale,0,0,p.scale,p.x,p.y-(part==Background&&upper_background?240*p.scale:0)};
        if(part==Right&&frame_horizontal) {
            if(slot<0)pose={0,p.scale,-p.scale,0,p.x,p.y+318.f*p.scale};
            else {
                const float cy=29.f+47.f*slot;
                pose={p.scale,0,0,p.scale,p.x+p.scale*(cy-290.f),p.y+p.scale*(28.f-cy)};
            }
        }
        if(transitioning&&part!=Description) {
            pose.tx+=transition_x;
            if(part!=Background) {
                auto wx=m.Read(node+0xec),wy=m.Read(node+0xfc),lx=m.Read(node+0x3c),ly=m.Read(node+0x40);
                if(!wx||!wy||!lx||!ly)return false;
                const float dx=std::bit_cast<float>(*wx)-std::bit_cast<float>(*lx);
                const float dy=std::bit_cast<float>(*wy)-std::bit_cast<float>(*ly);
                if(!std::isfinite(dx)||!std::isfinite(dy)||std::abs(dx)>512||std::abs(dy)>512)return false;
                pose.tx-=pose.xx*dx+pose.xy*dy;pose.ty-=pose.yx*dx+pose.yy*dy;
            }
        }
        return helper.Transform(m,node,writes,seen,pose);
    }
    template<class M> bool Prepare(M& m,U32 layout,std::vector<Write>& writes) {
        std::vector<U32> seen;
        if(layout!=panel)return Move(m,layout+8,layout==background?Background:(layout==cursor||layout==touch)?cursor_part:Panel,writes,seen,int(cursor_slot));
        auto list=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
        if(!list||count!=std::optional<U32>(1)||!Pointer(*list))return false;
        auto all=m.Read(*list);if(!all||!Named(m,*all,"all"))return false;
        list=m.Read(*all+0x14);count=m.Read(*all+0x18);
        if(!list||count!=std::optional<U32>(4)||!Pointer(*list,16))return false;
        unsigned found=0;
        constexpr std::array<const char*,4> names{"presen_item_collect","disp_item_name","btn_return_l","btn_decide_g"};
        for(unsigned i=0;i<4;++i) {
            auto child=m.Read(*list+4*i);if(!child)return false;
            int role=-1;for(unsigned j=0;j<4;++j)if(Named(m,*child,names[j]))role=j;
            if(role<0||(found&(1u<<role)))return false;found|=1u<<role;
            if(role) {if(!Move(m,*child,role==1?Prompt:role==2?Return:Confirm,writes,seen))return false;continue;}
            auto sub=m.Read(*child+0x14),n=m.Read(*child+0x18);
            if(!sub||n!=std::optional<U32>(3)||!Pointer(*sub,12))return false;
            unsigned parts=0;
            for(unsigned k=0;k<3;++k) {
                auto node=m.Read(*sub+4*k);if(!node)return false;
                const int part=Named(m,*node,"item_set_pos_g")?0:Named(m,*node,"item_panel_g")?1:Named(m,*node,"right_base_g")?2:-1;
                if(part<0||(parts&(1u<<part)))return false;parts|=1u<<part;
                if(part==2&&frame_horizontal) {
                    auto list=m.Read(*node+0x14),count=m.Read(*node+0x18);
                    if(!list||count!=std::optional<U32>(5)||!Pointer(*list,20))return false;
                    constexpr std::array<const char*,5> strip{"icon_event00_l","icon_event01_l","icon_event02_l","icon_event03_l","right_base_pane"};
                    unsigned found=0;
                    for(unsigned j=0;j<5;++j) {
                        auto child=m.Read(*list+j*4);if(!child)return false;
                        int slot=-1;for(unsigned k=0;k<5;++k)if(Named(m,*child,strip[k]))slot=k;
                        if(slot<0||(found&(1u<<slot)))return false;found|=1u<<slot;
                        if(!Move(m,*child,Right,writes,seen,slot<4?slot:-1))return false;
                    }
                } else if(!Move(m,*node,part==2?Right:Panel,writes,seen))return false;
            }
        }
        return found==15;
    }
    template<class M> void Discover(M& m,U32 manager) {
        panel=owner=background=cursor=touch=0;background_draw=0;transitioning=false;transition_x=0;helper.brown_shadow_steps=0;icons.fill(0);routed.clear();
        if(!enabled||!Pointer(manager,32))return;
        auto q=m.Read(manager+28);NativeInventory::Hook::Buckets entries;
        if(!q||!helper.ReadQueue(m,*q,entries))return;
        for(auto& bucket:entries)for(auto entry:bucket)if(LayoutNamed(m,entry,"MenuItemPresen")) {
            if(panel){panel=0;return;}panel=entry;
        }
        if(!panel)return;
        for(auto& bucket:entries)for(auto entry:bucket)if(LayoutNamed(m,entry,"ItemIconButton")) {
            auto binding=m.Read(entry+0x184);
            if(binding&&*binding>=0x100024&&m.Read(*binding+4)==std::optional<U32>(panel)){owner=*binding-0x24;break;}
        }
        if(!owner){panel=0;return;}
        for(unsigned i=0;i<20;++i) {
            auto icon=m.Read(owner+0x5c+4*i);
            if(!icon||!LayoutNamed(m,*icon,"ItemIconButton")){panel=0;return;}icons[i]=*icon;
        }
        auto c=m.Read(owner+8);if(c&&LayoutNamed(m,*c,"CursorItemCollect"))cursor=*c;
        auto t=m.Read(owner+0xc);if(t&&LayoutNamed(m,*t,"CursorItemCollect"))touch=*t;
        auto state=m.Read(owner+0x18);if(!state){panel=0;return;}
        cursor_part=(*state&255)<5?Panel:Right;cursor_slot=(*state>>8)&255;
        frame_horizontal=horizontal.load();
        if(cursor_part==Right&&cursor_slot>=4){panel=0;return;}
        for(unsigned i=0;i<Count;++i) {
            auto& p=frame[i];p={poses[i].x.load(),poses[i].y.load(),poses[i].scale.load()};
            if(i==Background){p.y=background_pose.y.load();p.scale=background_pose.scale.load();}
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||p.scale<.1f||p.scale>2.f){panel=0;return;}
        }
        ReadTransition(m);
        std::vector<Write> preview;
        if(!Prepare(m,panel,preview)){panel=0;return;}
        for(auto icon:icons){preview.clear();if(!Prepare(m,icon,preview)){panel=0;return;}}
    }
    template<class M> void Pass(M& m,U32 manager,U32 screen) {
        pass=screen;if(screen==4)Discover(m,manager);
        if(!panel||(screen!=4&&screen!=5))return;
        auto src=m.Read(manager+8+4*screen),dst=m.Read(manager+8+4*(screen==4?1:3));
        NativeInventory::Hook::Buckets from,to;
        if(!src||!dst||*src==*dst||!helper.ReadQueue(m,*src,from)||!helper.ReadQueue(m,*dst,to))return;
        std::vector<U32> selected;
        for(unsigned p=0;p<16;++p) {
            std::vector<U32> keep;
            for(auto entry:from[p]) {
                const bool move=screen==4?(m.Read(owner+4)==std::optional<U32>(entry)&&LayoutNamed(m,entry,"CommonBottomBg")):
                    entry==panel||entry==cursor||entry==touch||std::find(icons.begin(),icons.end(),entry)!=icons.end();
                if(!move){keep.push_back(entry);continue;}
                for(auto& bucket:to)if(std::find(bucket.begin(),bucket.end(),entry)!=bucket.end())return;
                if(screen==4){if(background)return;background=entry;}
                std::vector<Write> preview;if(!Prepare(m,entry,preview))return;
                if(screen==4) {
                    if(to[p].size()+2>32)return;
                    const auto& b=frame[Background];helper.frame_background={b.x+transition_x,b.y,b.scale};
                    helper.ConfigureBrownShadow(m,entry,32-to[p].size()-2,shadow_width.load(),shadow_opacity.load());
                    for(unsigned i=0;i<1+helper.brown_shadow_steps;++i)to[p].push_back(entry);
                }
                to[p].push_back(entry);selected.push_back(entry);
            }
            from[p]=std::move(keep);if(to[p].size()>32)return;
        }
        for(auto [q,buckets]:{std::pair{*src,&from},std::pair{*dst,&to}})
            for(unsigned p=0;p<16;++p)for(unsigned i=0;i<(*buckets)[p].size();++i)if(!m.Read(q+4+p*0x80+i*4))return;
        for(auto [q,buckets]:{std::pair{*src,&from},std::pair{*dst,&to}}) {
            U32 total=0;
            for(unsigned p=0;p<16;++p){auto& b=(*buckets)[p];for(unsigned i=0;i<b.size();++i)m.Write(q+4+p*0x80+i*4,b[i]);m.Write(q+0x804+p*4,b.size());total+=b.size();}
            m.Write(q+0x844,total);
        }
        routed.insert(routed.end(),selected.begin(),selected.end());
    }
    template<class M> bool IsDescription(M& m,U32 layout) const {
        if(!enabled.load()||!panel||!owner||
           std::find(routed.begin(),routed.end(),panel)==routed.end()||
           m.Read(owner+0x28)!=std::optional<U32>(panel)||
           !LayoutNamed(m,panel,"MenuItemPresen")||!LayoutNamed(m,layout,"MessageWindow"))return false;
        auto id=m.Read(layout+0x3f4);
        // Item descriptions only; the NPC's instruction and conversation stay put.
        return id&&*id>=0x1700&&*id<0x1800;
    }
    template<class M> void Begin(M& m,U32 layout) {
        frames.emplace_back();if(std::find(routed.begin(),routed.end(),layout)==routed.end()||pass!=(layout==background?1u:3u))return;
        std::vector<Write> writes;
        if(layout==background&&background_draw<helper.brown_shadow_steps) {
            if(!helper.PrepareBrownShadow(m,layout,background_draw++,writes))return;
        } else {
            upper_background=layout==background&&(background_draw++%2==1);
            const bool ok=Prepare(m,layout,writes);upper_background=false;if(!ok)return;
        }
        for(auto w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
    }
    template<class M> void End(M& m) {
        if(frames.empty())return;
        for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);
        frames.pop_back();
    }
};
}
