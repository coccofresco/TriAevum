// Ported from mm3d-dekui src/mm3d/items_map_prompt.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ocarina_prompt.h"
#include "dungeon_map.h"
#include <cstdio>

namespace Mm3dDekui::ItemsMapPrompt {
using namespace NativeInventory;
inline std::atomic<float> opacity{1.f}; // Shared with the menu button legend.
inline float Opacity(){const float a=opacity.load();return std::isfinite(a)?std::clamp(a,0.f,1.f):1.f;}
inline constexpr ButtonPose DefaultPose{9.f,-89.6f,.6f};
inline constexpr std::array<ButtonPose,3> DefaultElements{{{6.f,-1.8f,1.f},{0,0,.71f},{0,-5.4f,1.f}}};
inline AtomicButtonPose pose{9.f,-89.6f,.6f};
inline std::array<AtomicButtonPose,3> elements{{{6.f,-1.8f,1.f},{0,0,.71f},{0,-5.4f,1.f}}}; // R, arrow, label
inline constexpr ButtonPose ReturnDefaultPose{-DefaultPose.x,DefaultPose.y,DefaultPose.scale};
inline constexpr std::array<ButtonPose,3> ReturnDefaultElements{{{-6.f,-1.8f,1.f},{0,0,.71f},{0,-5.4f,1.f}}};
struct ReturnOptions {
    ButtonPose pose=ReturnDefaultPose;
    std::array<ButtonPose,3> elements=ReturnDefaultElements;
};
inline AtomicButtonPose return_pose{-9.f,-89.6f,.6f};
inline std::array<AtomicButtonPose,3> return_elements{{{-6.f,-1.8f,1.f},{0,0,.71f},{0,-5.4f,1.f}}};
inline void ApplyReturn(const ReturnOptions& o) {
    return_pose.x=o.pose.x;return_pose.y=o.pose.y;return_pose.scale=o.pose.scale;
    for(unsigned i=0;i<3;++i) {
        return_elements[i].x=o.elements[i].x;return_elements[i].y=o.elements[i].y;
        return_elements[i].scale=o.elements[i].scale;
    }
}
inline float BobY(std::int64_t guest_us) {
    // MessageWindow/keyWait, waitIcon Y:3 ->5 ->3 at frames0/15/30 (30Hz).
    // Use only its vertical displacement, relative to the saved prompt pose.
    const float phase=float(std::max<std::int64_t>(0,guest_us)%1000000)/1000000.f;
    return phase<=.5f?4.f*phase:4.f*(1.f-phase);
}
struct Hook {
    OcarinaPrompt::Hook prompt;
    U32 description=0,description_panel=0;
    unsigned last_diagnostic=~0u;
    // Resolve the persistent MessageWindow before it draws, including on an
    // empty inventory opened immediately after loading a save/state. Its font
    // is usable even when there is no active item-description message.
    template<class M> void ResolveDescription(M& m,U32 owner,U32 panel) {
        description=description_panel=0;
        if(!Pointer(owner,0x170))return;
        auto context=m.Read(owner+0x16c);
        if(!context||!Pointer(*context))return;
        // Restoration v110 MessageMgr::Instance return and singleton literal.
        constexpr U32 manager=0x006b3700; // ref 0x6b36f8
        if(m.Read(0x001c5074)!=0xe59f0004u||m.Read(0x001c5078)!=0xe8bd8070u|| // ref 0x1c527c, 0x1c5280
           m.Read(0x001c5080)!=manager)return; // ref 0x1c5288
        auto window=m.Read(manager+0x2c);
        if(!window||!LayoutNamed(m,*window,"MessageWindow")||
           m.Read(*window+0x444)!=context)return;
        description=*window;description_panel=panel;
    }
    template<class M> void Templates(M& m,NativeInventory::Hook& inventory,U32 layout,std::string_view label="Map") {
        std::vector<NativeInventory::Hook::Write> probe;std::vector<U32> nodes;
        if(!inventory.Transform(m,layout+8,probe,nodes,{1,0,0,1,0,0}))return;
        for(auto node:nodes) {
            auto geo=m.Read(node+0x28);if(!geo||!Pointer(*geo,8))continue;
            auto kind=m.Read(*geo+4);if(!kind)continue;
            if(!prompt.icon&&(*kind&65535)==6&&Pointer(*geo,0x7c))prompt.icon=node;
            if(!prompt.text&&(*kind&65535)==3&&Pointer(*geo,0x30c)) {
                auto font=m.Read(*geo+0x28);
                // The font must hold every glyph of the label in the active archive's language.
                const auto codes=PromptText::Codes(PromptText::Get(m,label));
                if(font&&!codes.empty()&&std::all_of(codes.begin(),codes.end(),[&](char32_t c){return bool(NotebookFont::Lookup(m,*font,c,0));}))prompt.text=node;
            }
        }
    }
    template<class M,class MapHook> U32 MapRoot(M& m,MapHook& map,U32 layout,U32 sp,U32 content,bool wide,std::int64_t guest_us=0) {
        prompt.pass=~0u;
        auto mode=DungeonMap::Dungeon(m,map.owner);
        const auto region=mode?m.Read(map.owner+0x74c):std::optional<U32>{};
        const bool no_world_map=mode&&!*mode&&region&&(*region&255)==6;
        // Native Map Draw submits UpWorldMapHud in top pass2; the
        // relocated dungeon panel uses top pass3. Neither is a bottom draw.
        // Region6 (the early-game no-map view) skips UpWorldMapHud entirely,
        // but still submits owner+20 SubMenuTitle in pass2. Attach there only
        // in that state so navigation survives without duplicating the prompt.
        const bool active=enabled&&map.valid&&mode&&map.pass==(*mode?3u:2u)&&
            m.Read(map.controller+8)==map.owner&&
            (*mode?(layout==map.panel&&std::find(map.routed.begin(),map.routed.end(),layout)!=map.routed.end()):
                   no_world_map?(m.Read(map.owner+0x20)==layout&&LayoutNamed(m,layout,"SubMenuTitle")):
                   (m.Read(map.owner+0x1c)==layout&&LayoutNamed(m,layout,"UpWorldMapHud")));
        if(active) {
            prompt.icon=prompt.text=0;prompt.texture=prompt.Parts(m);
            Templates(m,map.helper,layout,"Items");
            if(!prompt.icon||!prompt.text)Templates(m,map.helper,map.panel,"Items");
            U32 fade=prompt.scratch.Child(m,layout+8,"all");
            if(*mode) {
                auto group=prompt.scratch.Child(m,map.backgrounds[1]+8,"backGround");
                fade=group?prompt.scratch.Child(m,group,"map_base"):0;
            }
            auto alpha=fade?m.Read(fade+0x134):std::optional<U32>{};
            if(alpha&&prompt.icon&&prompt.text&&prompt.texture) {
                const float a=std::bit_cast<float>(*alpha);
                if(std::isfinite(a)&&a>=0&&a<=1) {prompt.native_alpha=a*Opacity();prompt.panel=layout;prompt.pass=3;}
            }
        }
        // Negate offsets before mirroring so positive editor X moves right.
        OcarinaPrompt::SinglePrompt single{{-return_pose.x.load(),return_pose.y.load(),return_pose.scale.load()},content,prompt.Parts(m,8)};
        single.reverse=true;
        for(unsigned i=0;i<return_elements.size();++i)
            single.elements[i]={-return_elements[i].x.load(),return_elements[i].y.load(),return_elements[i].scale.load()};
        for(unsigned i=0;i<2;++i)single.elements[i].y+=BobY(guest_us);
        auto root=prompt.Root(m,layout,sp,wide,&single);
        return root==layout+8?content:root;
    }
    template<class M> U32 Root(M& m,NativeInventory::Hook& inventory,U32 layout,U32 sp,U32 content,bool wide,std::int64_t guest_us=0) {
        prompt.pass=~0u;
        const bool active=enabled&&inventory.inventory_page&&inventory.pass==3&&layout==inventory.panel&&
            LayoutNamed(m,layout,"MenuItem")&&
            std::find(inventory.routed.begin(),inventory.routed.end(),layout)!=inventory.routed.end();
        if(active) {
            prompt.icon=prompt.text=0;prompt.texture=prompt.Parts(m);
            Templates(m,inventory,layout);
            // Items tab labels are textures. The actual text/font lives in the
            // separately rendered item-description MessageWindow.
            if(!prompt.text&&description_panel==layout&&LayoutNamed(m,description,"MessageWindow"))
                Templates(m,inventory,description);
            const U32 all=prompt.scratch.Child(m,layout+8,"all");
            auto alpha=m.Read((all?all:layout+8)+0x134);
            if(alpha&&prompt.icon&&prompt.text&&prompt.texture) {
                const float a=std::bit_cast<float>(*alpha);
                if(std::isfinite(a)&&a>=0&&a<=1) {
                    prompt.native_alpha=a*Opacity();prompt.panel=layout;prompt.pass=3;
                }
            }
        }
        OcarinaPrompt::SinglePrompt single{{pose.x.load(),pose.y.load(),pose.scale.load()},content,prompt.Parts(m,8)};
        for(unsigned i=0;i<elements.size();++i)
            single.elements[i]={elements[i].x.load(),elements[i].y.load(),elements[i].scale.load()};
        // Keep the label stationary; native font positioning snaps small motion.
        for(unsigned i=0;i<2;++i)single.elements[i].y+=BobY(guest_us);
        const auto root=prompt.Root(m,layout,sp,wide,&single);
        if(active) {
            const unsigned state=(prompt.icon?1:0)|(prompt.text?2:0)|(prompt.texture?4:0)|
                (root!=content&&root!=layout+8?8:0);
            if(state!=last_diagnostic) {
                std::fprintf(stderr,"[MM3D MapPrompt] top-pass icon=%08x text=%08x description=%08x atlas=%08x built=%u\n",
                    prompt.icon,prompt.text,description,prompt.texture,(state>>3)&1);
                last_diagnostic=state;
            }
        }
        return root==layout+8?content:root;
    }
};
}
