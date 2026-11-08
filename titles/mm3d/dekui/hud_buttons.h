// Ported from mm3d-dekui src/mm3d/hud_buttons.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "double_time.h"
namespace Mm3dDekui::HudButtons {
using namespace NativeInventory;
constexpr unsigned Count=6;
using Layout=std::array<GearMenu::Pose,Count>;
inline Layout Default(){Layout result;for(auto& p:result)p={0,0,1};result[4].y=48;result[5].y=48;return result;}
struct Diamond {bool enabled=false;float x=350,y=55,spacing=24,rotation=0;};
struct Options {float flip_peak=1.2f;std::array<float,3> preset_flip_peaks{1.2f,1.2f,1.2f};Diamond diamond;std::array<Diamond,3> preset_diamonds;float scale=1;std::array<float,3> preset_scales{1,1,1};Layout buttons=Default();std::array<Layout,3> presets{Default(),Default(),Default()};std::array<bool,3> saved{};};
inline std::array<GearMenu::AtomicPose,Count> buttons;
inline std::atomic<float> scale{1},flip_peak{1.2f},center_x{350},center_y{55},spacing{24},rotation{0};
inline std::atomic<bool> diamond_enabled{false};
// TriAevum: touch-control placement composed AFTER dekui's own pose/diamond
// (world' = touch o dekui), owned by the MM3D touch-HUD plan while touch
// prompts are active (Mm3dButtonOverlay::ApplyTouchHud); inactive otherwise,
// so dekui's layout settings stay the sole owner of the controller HUD.
struct TouchPlacement {std::atomic<bool> active{false};std::atomic<float> scale{1},x{0},y{0};};
inline std::array<TouchPlacement,Count> touch;
inline NativeInventory::Hook::Affine ComposeTouch(unsigned i,const NativeInventory::Hook::Affine& d){
    if(i>=Count||!touch[i].active.load())return d;
    const float s=touch[i].scale.load(),x=touch[i].x.load(),y=touch[i].y.load();
    if(!std::isfinite(s)||!std::isfinite(x)||!std::isfinite(y)||!(s>0))return d;
    return {s*d.xx,s*d.xy,s*d.yx,s*d.yy,s*d.tx+x,s*d.ty+y};
}
inline std::array<float,2> DiamondPoint(unsigned i,const Diamond& d){
    float x=i==2?-d.spacing:i==4?d.spacing:0;
    float y=i==1?-d.spacing:i==5?d.spacing:0;
    const float angle=d.rotation*3.14159265358979323846f/180;
    return {d.x+x*std::cos(angle)-y*std::sin(angle),d.y+x*std::sin(angle)+y*std::cos(angle)};
}
inline void Apply(const Options& o){flip_peak=o.flip_peak;diamond_enabled=o.diamond.enabled;center_x=o.diamond.x;center_y=o.diamond.y;spacing=o.diamond.spacing;rotation=o.diamond.rotation;scale=o.scale;for(unsigned i=0;i<Count;++i){buttons[i].x=o.buttons[i].x;buttons[i].y=o.buttons[i].y;buttons[i].scale=o.buttons[i].scale;}}
inline void SaveSlot(Options& o,unsigned slot){if(slot<3){o.preset_flip_peaks[slot]=o.flip_peak;o.preset_diamonds[slot]=o.diamond;o.presets[slot]=o.buttons;o.preset_scales[slot]=o.scale;o.saved[slot]=true;}}
inline bool LoadSlot(Options& o,unsigned slot){if(slot>=3||!o.saved[slot])return false;o.flip_peak=o.preset_flip_peaks[slot];o.diamond=o.preset_diamonds[slot];o.buttons=o.presets[slot];o.scale=o.preset_scales[slot];return true;}
// Installed Restoration UpdateActionButtons: change only the two visibility
// loops before world calculation. The two additional display-only ZR tests
// below keep the ordinary A/ocarina and D-pad presentation. Guest input stays intact.
constexpr std::array<U32,6> VisibilityPCs{0x007d42fc,0x007d430c,0x007d4348,0x007d4374,0x007d4380,0x007d43bc}; // ref 0x7d42fc (Restoration source port), 0x7d430c (Restoration source port), 0x7d4348 (Restoration source port), 0x7d4374 (Restoration source port), 0x7d4380 (Restoration source port), 0x7d43bc (Restoration source port)
constexpr std::array<U32,6> VisibilityWords{0xe155000c,0xe3550000,0xe155000c,0xe15c0000,0xe3500000,0xe15c0000};
// Ghidra, installed v110 UpdateActionButtons (0x7d40e8): both reads
// consume button bit15 from context+0x30 solely for HUD presentation.
// First controls A/ocarina visibility; second controls D-pad item/alpha selection.
constexpr std::array<U32,2> DisplayPCs{0x007d42c8,0x007d44e4}; // ref 0x7d42c8 (Restoration source port), 0x7d44e4 (Restoration source port)
constexpr std::array<U32,2> DisplayWords{0xe2033902,0x12033001};
constexpr std::array<U32,2> DisplayReplacements{0xe3a03000,0x13a03000};
inline U32 VisibilityInstruction(U32 pc,U32 word){
    for(unsigned i=0;i<DisplayPCs.size();++i)
        if(pc==DisplayPCs[i]&&word==DisplayWords[i])return DisplayReplacements[i];
    for(unsigned i=0;i<VisibilityPCs.size();++i)if(pc==VisibilityPCs[i]&&word==VisibilityWords[i])
        // ip is existing visibility bit 0/1. Compare with1 to skip if visible;
        // compare with2 in update block to take native SetVisible(true) branch.
        return i==1||i==4?0xe35c0002:0xe35c0001;
    return word;
}
template<class M> bool VisibilityCodeMatches(M& m){
    for(unsigned i=0;i<DisplayPCs.size();++i)
        if(m.Read(DisplayPCs[i])!=std::optional<U32>(DisplayWords[i]))return false;
    for(auto [pc,word]:std::array<std::pair<U32,U32>,6>{{
        {0x007d40e8,0xe92d4ff0},{0x007d42c4,0xe5963030},{0x007d42cc,0xe3530000}, // ref 0x7d40e8 (Restoration source port), 0x7d42c4 (Restoration source port), 0x7d42cc (Restoration source port)
        {0x007d44bc,0x15963030},{0x007d44e0,0x11a037a3},{0x007d44e8,0x158d3000}}}) // ref 0x7d44bc (Restoration source port), 0x7d44e0 (Restoration source port), 0x7d44e8 (Restoration source port)
        if(m.Read(pc)!=std::optional<U32>(word))return false;
    for(unsigned i=0;i<VisibilityPCs.size();++i)if(m.Read(VisibilityPCs[i])!=std::optional<U32>(VisibilityWords[i]))return false;
    for(auto [pc,word]:std::array<std::pair<U32,U32>,8>{{
        {0x007d42e0,0xe5943204},{0x007d42f8,0xe202c001},{0x007d4344,0xe202c001}, // ref 0x7d42e0 (Restoration source port), 0x7d42f8 (Restoration source port), 0x7d4344 (Restoration source port)
        {0x007d4358,0xe594320c},{0x007d4370,0xe202c001},{0x007d43b8,0xe202c001}, // ref 0x7d4358 (Restoration source port), 0x7d4370 (Restoration source port), 0x7d43b8 (Restoration source port)
        {0x007d42c8,0xe2033902},{0x007d43dc,0xe3550000}}})if(m.Read(pc)!=std::optional<U32>(word))return false; // ref 0x7d42c8 (Restoration source port), 0x7d43dc (Restoration source port)
    return true;
}
struct Hook {
    DoubleTime::Hook helper;
    using Write=NativeInventory::Hook::Write;
    std::vector<std::vector<Write>> frames;
    // Native display tracks animate local X/Y scale from1 to1.2. Reduce only
    // that excess, using current values so the original timing/rotation remain.
    static float PulseRatio(float native,float peak){
        if(!std::isfinite(native)||native<1||native>1.2001f)return 1;
        return (1+(native-1)*(peak-1)/.2f)/native;
    }
    template<class M> void AdjustPulse(M& m,U32 all){
        const float peak=flip_peak.load();if(!std::isfinite(peak)||peak<1||peak>=1.2f)return;
        std::vector<Write> writes;
        for(const char* button:{"a_action_button","b_action_button"}){
            auto icon=helper.helper.Child(m,all,button);if(!icon)return;
            auto root=helper.EmbeddedRoot(m,icon);if(!root)return;
            for(const char* name:{"a_action_num","a_action_text","a_button_base","a_action_icon"}){
                auto node=helper.helper.Child(m,root,name);if(!node)continue;
                auto sx=m.Read(node+0x48),sy=m.Read(node+0x4c),x=m.Read(node+0xec),y=m.Read(node+0xfc);
                if(!sx||!sy||!x||!y)return;
                const float rx=PulseRatio(std::bit_cast<float>(*sx),peak),ry=PulseRatio(std::bit_cast<float>(*sy),peak);
                if(rx==1&&ry==1)continue;
                const float cx=std::bit_cast<float>(*x),cy=std::bit_cast<float>(*y);
                std::vector<U32> seen;
                if(!helper.helper.helper.Transform(m,node,writes,seen,{rx,0,0,ry,cx*(1-rx),cy*(1-ry)}))return;
            }
        }
        for(auto w:writes)m.Write(w.address,w.after);
        frames.back().insert(frames.back().end(),writes.begin(),writes.end());
    }
    template<class M> void Begin(M& m,U32 layout){
        const bool nested=std::any_of(frames.begin(),frames.end(),[](const auto& f){return !f.empty();});
        frames.emplace_back();if(nested)return;
        U32 root=0;bool actions=false;
        if(LayoutNamed(m,layout,"HudItemIconSet"))root=layout+8;
        else if(LayoutNamed(m,layout,"Hud2")){
            auto all=helper.helper.Child(m,layout+8,"all");
            auto group=helper.helper.Child(m,all,"item_button_g");
            auto set=helper.helper.Child(m,group,"item_icon_set");
            if(set)root=helper.EmbeddedRoot(m,set);
        }
        else if(LayoutNamed(m,layout,"Hud")){root=layout+8;actions=true;}
        if(!root)return;
        auto all=helper.helper.Child(m,root,"all");if(!all)return;
        if(actions)all=helper.helper.Child(m,all,"action_button_g");
        if(!all)return;
        constexpr const char* icons[]{"a_action_button","icon_item_x_g","icon_item_y_g","b_action_button","icon_item_1_g","icon_item_2_g"};
        constexpr const char* bases[]{"btn_corner01_g","btn_x_g","btn_y_g","btn_corner03_g","btn_corner01_g","btn_corner03_g"};
        std::vector<Write> writes;
        for(unsigned i=0;i<Count;++i){
            if(actions!=(i==0||i==3))continue;
            auto icon=helper.helper.Child(m,all,icons[i]);
            auto base=actions?0:helper.helper.Child(m,all,bases[i]);if(!icon||(!actions&&!base))return;
            const unsigned slot=i; // StickControls::ItemLayoutIndex(i,4) arrives in S5 (identity while it is off)
            const auto& a=buttons[slot];const GearMenu::Pose p{a.x.load(),a.y.load(),scale.load()};
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||p.scale<.1f||p.scale>3)return;
            auto x=m.Read(icon+0xec),y=m.Read(icon+0xfc);if(!x||!y)return;
            const float cx=std::bit_cast<float>(*x),cy=std::bit_cast<float>(*y);
            if(!std::isfinite(cx)||!std::isfinite(cy))return;
            float target_x=cx,target_y=cy;
            if(slot!=i) {
                const auto other=helper.helper.Child(m,all,icons[slot]);if(!other)return;
                const auto ox=m.Read(other+0xec),oy=m.Read(other+0xfc);if(!ox||!oy)return;
                target_x=std::bit_cast<float>(*ox);target_y=std::bit_cast<float>(*oy);
                if(!std::isfinite(target_x)||!std::isfinite(target_y))return;
            }
            float tx=p.x+cx*(1-p.scale),ty=p.y+cy*(1-p.scale);
            if(slot!=i){tx=p.x+target_x-cx*p.scale;ty=p.y+target_y-cy*p.scale;}
            if(!actions&&diamond_enabled){
                const Diamond d{true,center_x.load(),center_y.load(),spacing.load(),rotation.load()};
                for(float v:{d.x,d.y,d.spacing,d.rotation})if(!std::isfinite(v))return;
                if(d.spacing<0||d.spacing>160)return;
                const auto target=DiamondPoint(slot,d);
                // Common root translation retains native HUD entrance/exit motion.
                auto ax=m.Read(all+0xec),ay=m.Read(all+0xfc);if(!ax||!ay)return;
                const float ox=std::bit_cast<float>(*ax),oy=std::bit_cast<float>(*ay);
                if(!std::isfinite(ox)||!std::isfinite(oy))return;
                tx=target[0]+ox-p.scale*cx;ty=target[1]+oy-p.scale*cy;
            }
            const auto mapping=ComposeTouch(i,NativeInventory::Hook::Affine{p.scale,0,0,p.scale,tx,ty});
            std::vector<U32> seen;
            for(U32 node:{icon,base})if(node&&!helper.helper.helper.Transform(m,node,writes,seen,mapping))return;
        }
        for(auto w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
        if(actions)AdjustPulse(m,all);
    }
    template<class M> void End(M& m){if(frames.empty())return;
        for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);frames.pop_back();}
};
}
