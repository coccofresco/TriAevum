// Ported from mm3d-dekui src/mm3d/world_map_prompt.h (tgacquin/mm3d-dekui 4f2692ed).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "items_map_prompt.h"
namespace Mm3dDekui::WorldMapPrompt {
using namespace NativeInventory;
// Use emulated time, preserving the current opacity when zoom reverses.
struct ZoomFade {
    U32 owner=0;
    std::int64_t last=0;
    float value=0;
    bool target=false;
    void Reset(){*this={};}
    float Update(U32 current,bool zoomed,std::int64_t now) {
        if(owner!=current||now<last||now-last>500000) {
            Reset();owner=current;
        } else {
            const float step=float(now-last)/150000.f;
            value=std::clamp(value+(target?step:-step),0.f,1.f);
        }
        last=now;target=zoomed;return value;
    }
};
inline AtomicButtonPose pose{0,0,.8f},native_pose;
inline void ApplyNative(const ButtonPose& p){native_pose.x=p.x;native_pose.y=p.y;native_pose.scale=p.scale;}
inline void Apply(const ButtonPose& p){pose.x=p.x;pose.y=p.y;pose.scale=p.scale;}
struct Hook {
    ItemsMapPrompt::Hook source;
    OcarinaPrompt::Hook move_prompt;
    ZoomFade move_fade;
    std::vector<std::vector<NativeInventory::Hook::Write>> native_frames;
    template<class M> void MoveNative(M& m,DungeonMap::Hook& map,U32 layout) {
        const ButtonPose p{native_pose.x.load(),native_pose.y.load(),native_pose.scale.load()};
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||p.scale<.1f||p.scale>3||
           (p.x==0&&p.y==0&&p.scale==1))return;
        std::vector<NativeInventory::Hook::Write> probe,writes;std::vector<U32> nodes,seen;
        if(!map.helper.Transform(m,layout+8,probe,nodes,{1,0,0,1,0,0}))return;
        unsigned found=0;
        for(auto n:nodes)if(Named(m,n,"disp_text_detail")||Named(m,n,"disp_text_whole")) {
            auto xb=m.Read(n+0xec),yb=m.Read(n+0xfc);if(!xb||!yb)return;
            const float x=std::bit_cast<float>(*xb),y=std::bit_cast<float>(*yb);
            if(!std::isfinite(x)||!std::isfinite(y)||!map.helper.Transform(m,n,writes,seen,
                {p.scale,0,0,p.scale,x*(1-p.scale)+p.x,y*(1-p.scale)+p.y}))return;
            ++found;
        }
        if(found!=2)return;
        for(auto w:writes)m.Write(w.address,w.after);native_frames.back()=std::move(writes);
    }
    template<class M> U32 End(M& m) {
        const U32 move_sp=move_prompt.End(m);
        const U32 sp=source.prompt.End(m);
        if(!native_frames.empty()) {
            for(auto it=native_frames.back().rbegin();it!=native_frames.back().rend();++it)m.Write(it->address,it->before);
            native_frames.pop_back();
        }
        return sp?sp:move_sp;
    }
    template<class M> U32 Root(M& m,DungeonMap::Hook& map,U32 layout,U32 sp,U32 content,bool wide,std::int64_t now=0) {
        native_frames.emplace_back();
        auto& prompt=source.prompt;prompt.pass=~0u;move_prompt.pass=~0u;
        auto mode=DungeonMap::Dungeon(m,map.owner);
        const bool current=map.valid&&mode&&m.Read(map.controller+8)==map.owner;
        const bool dungeon=current&&*mode&&map.pass==3&&layout==map.panel&&
            std::find(map.routed.begin(),map.routed.end(),layout)!=map.routed.end();
        const bool world=current&&!*mode&&map.pass==2&&m.Read(map.owner+0x1c)==layout&&
            LayoutNamed(m,layout,"UpWorldMapHud");
        prompt.group=move_prompt.group=dungeon?DungeonMap::PromptGroup():OcarinaPrompt::Group{};
        if(!current||world)move_fade.Reset();
        std::string_view label="Zoom in";
        if(world||dungeon) {
            std::optional<bool> detail;
            if(world) {
                MoveNative(m,map,layout);
                // Native UpWorldMapHud::SetDetail toggles this byte on X.
                auto state=m.Read(layout+0x170);
                if(state&&(*state&255)<=1)detail=(*state&255)!=0;
            } else if(m.Read(DungeonMap::OverviewCursorGuards.back().first)==std::optional<U32>(DungeonMap::MapState)) {
                // Same mode byte read by native IsMapZoomed (0x1B2558, ref 1B274C;
                // its literal 0x1B2570 is the last OverviewCursorGuard).
                // A switches modes1/3; read the accepted state, not the press.
                auto state=m.Read(DungeonMap::MapMode);
                if(state&&(*state>>24)>=1&&(*state>>24)<=3)detail=(*state>>24)!=3;
            }
            if(detail) {
                label=*detail?"Zoom out":"Zoom in";
                prompt.icon=prompt.text=0;prompt.texture=prompt.Parts(m);
                // Both labels' glyphs, so the template font holds the localized
                // label whichever way the zoom toggles (S1/S2 localization rule).
                const std::string alphabet=std::string(PromptText::Get(m,"Zoom in"))+std::string(PromptText::Get(m,"Zoom out"));
                source.Templates(m,map.helper,layout,alphabet);
                if(!prompt.icon||!prompt.text)source.Templates(m,map.helper,map.panel,alphabet);
                auto all=prompt.scratch.Child(m,dungeon?map.backgrounds[1]+8:layout+8,dungeon?"backGround":"all");
                auto fade=dungeon?prompt.scratch.Child(m,all,"map_base"):all;
                auto alpha=fade?m.Read(fade+0x134):std::optional<U32>{};
                if(alpha&&prompt.icon&&prompt.text&&prompt.texture) {
                    const float a=std::bit_cast<float>(*alpha);
                    if(std::isfinite(a)&&a>=0&&a<=1) {
                        // Dungeon hints match the fully opaque native cursor/return
                        // pair, while still following the menu's own scene fade.
                        prompt.native_alpha=a*(dungeon?1.f:ItemsMapPrompt::Opacity());prompt.panel=layout;prompt.pass=3;
                        if(dungeon) {
                            if(prompt.native_alpha<=0)move_fade.Reset();
                            else {
                                move_prompt.native_alpha=prompt.native_alpha*move_fade.Update(map.owner,*detail,now);
                                move_prompt.icon=prompt.icon;move_prompt.text=prompt.text;
                                move_prompt.texture=prompt.texture;move_prompt.panel=layout;move_prompt.pass=3;
                            }
                        }
                    }
                }
            }
        }
        if(dungeon&&prompt.pass!=3)move_fade.Reset();
        const auto shared=DungeonMap::PromptOptions();
        const ButtonPose selected=dungeon?ButtonPose{shared.x,shared.y-shared.spacing*shared.scale,shared.scale}:
            ButtonPose{pose.x.load(),pose.y.load(),pose.scale.load()};
        OcarinaPrompt::SinglePrompt single{selected,content};
        if(dungeon){single.icon_scale=shared.icon_scale;single.dungeon_alignment=true;}
        single.label=label;single.button_u=dungeon?112:144;
        single.width_label=label=="Zoom in"?"Zoom out":"Zoom in";
        auto root=prompt.Root(m,layout,sp,wide,&single);
        if(root==layout+8)root=content;
        OcarinaPrompt::SinglePrompt move{{shared.x,shared.y-3*shared.spacing*shared.scale,shared.scale},root};
        move.icon_scale=shared.icon_scale;move.dungeon_alignment=true;
        // Neutral Circle Pad in the live localized parts atlas (not the D-pad).
        move.label="Move map";move.button_u=208;move.button_v=16;
        const auto result=move_prompt.Root(m,layout,root!=content?root:sp,wide,&move);
        return result==layout+8?root:result;
    }
};
}
