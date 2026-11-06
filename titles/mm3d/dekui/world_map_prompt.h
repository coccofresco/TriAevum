// Ported from mm3d-dekui src/mm3d/world_map_prompt.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "items_map_prompt.h"
namespace Mm3dDekui::WorldMapPrompt {
using namespace NativeInventory;
inline AtomicButtonPose pose{0,0,.8f},native_pose;
inline void ApplyNative(const ButtonPose& p){native_pose.x=p.x;native_pose.y=p.y;native_pose.scale=p.scale;}
inline void Apply(const ButtonPose& p){pose.x=p.x;pose.y=p.y;pose.scale=p.scale;}
struct Hook {
    ItemsMapPrompt::Hook source;
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
        const U32 sp=source.prompt.End(m);
        if(!native_frames.empty()) {
            for(auto it=native_frames.back().rbegin();it!=native_frames.back().rend();++it)m.Write(it->address,it->before);
            native_frames.pop_back();
        }
        return sp;
    }
    template<class M> U32 Root(M& m,DungeonMap::Hook& map,U32 layout,U32 sp,U32 content,bool wide) {
        native_frames.emplace_back();
        auto& prompt=source.prompt;prompt.pass=~0u;
        auto mode=DungeonMap::Dungeon(m,map.owner);
        const bool active=map.valid&&mode&&!*mode&&map.pass==2&&m.Read(map.controller+8)==map.owner&&
            m.Read(map.owner+0x1c)==layout&&LayoutNamed(m,layout,"UpWorldMapHud");
        std::string_view label="Zoom In";
        if(active) {
            MoveNative(m,map,layout);
            // Native UpWorldMapHud::SetDetail toggles this byte on accepted X.
            // Read state, not input edges, so save-state loads stay synchronized.
            auto detail=m.Read(layout+0x170);
            if(detail&&(*detail&255)<=1) {
                label=(*detail&255)?"Zoom Out":"Zoom In";
                prompt.icon=prompt.text=0;prompt.texture=prompt.Parts(m);
                // Both labels' glyphs (the same set as "Zoom InOut" in English).
                const std::string alphabet=std::string(PromptText::Get(m,"Zoom In"))+std::string(PromptText::Get(m,"Zoom Out"));
                source.Templates(m,map.helper,layout,alphabet);
                if(!prompt.icon||!prompt.text)source.Templates(m,map.helper,map.panel,alphabet);
                auto all=prompt.scratch.Child(m,layout+8,"all");auto alpha=all?m.Read(all+0x134):std::optional<U32>{};
                if(alpha&&prompt.icon&&prompt.text&&prompt.texture) {
                    const float a=std::bit_cast<float>(*alpha);
                    if(std::isfinite(a)&&a>=0&&a<=1) {
                        prompt.native_alpha=a*ItemsMapPrompt::Opacity();prompt.panel=layout;prompt.pass=3;
                    }
                }
            }
        }
        OcarinaPrompt::SinglePrompt single{{pose.x.load(),pose.y.load(),pose.scale.load()},content};
        single.label=label;single.button_u=144;
        auto root=prompt.Root(m,layout,sp,wide,&single);
        return root==layout+8?content:root;
    }
};
}
