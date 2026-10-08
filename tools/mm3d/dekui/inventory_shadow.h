// Ported from mm3d-dekui src/mm3d/inventory_shadow.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "notebook_prompt.h"
#include "native_item_present.h"

namespace Mm3dDekui::InventoryShadow {
using namespace NativeInventory;
struct Hook {
    NotebookPrompt::Hook scratch;
    template<class M> U32 Root(M& m,NativeInventory::Hook& inventory,U32 layout,U32 sp,
                               NativeItemPresent::Hook* present=nullptr) {
        scratch.frames.emplace_back();auto& frame=scratch.frames.back();const U32 root=layout+8;
        const bool items=LayoutNamed(m,layout,"MenuItem"),masks=LayoutNamed(m,layout,"MenuMask"),
            gear=LayoutNamed(m,layout,"MenuCollect"),giving=LayoutNamed(m,layout,"MenuItemPresen");
        if(giving) {
            if(!present||!NativeItemPresent::enabled||present->pass!=3||layout!=present->panel||
               std::find(present->routed.begin(),present->routed.end(),layout)==present->routed.end())return root;
        } else if(!(items||masks||gear)||!(gear?GearMenu::enabled.load():enabled.load())||
                  inventory.pass!=3||layout!=inventory.panel||
                  std::find(inventory.routed.begin(),inventory.routed.end(),layout)==inventory.routed.end())return root;
        std::vector<NativeInventory::Hook::Write> unused;std::vector<U32> nodes;
        if(!inventory.Transform(m,root,unused,nodes,{1,0,0,1,0,0}))return root;
        const std::array<const char*,3> names=gear?
            std::array<const char*,3>{"left_base_pane","right_base_pane",nullptr}:
            masks?std::array<const char*,3>{"mask_panel_pane00","mask_panel_pane01",nullptr}:
            std::array<const char*,3>{"item_panel_pane00","item_panel_pane01",giving?"right_base_pane":nullptr};
        const unsigned count=giving?3:2;
        std::array<U32,3> panes{};
        for(auto node:nodes)for(unsigned i=0;i<count;++i)if(Named(m,node,names[i])) {
            if(panes[i])return root;panes[i]=node;
        }
        // Use the current world rectangles after the menu's live transforms.
        // Gear / Select Item right strips can be rotated ninety degrees.
        struct Bounds {float left,right,bottom,scale;};
        std::array<Bounds,3> bounds{};std::array<float,2> bottoms{-1e9f,-1e9f};
        const auto group=[&](unsigned i){return gear?i:(i==2?1u:0u);};
        const auto read=[&](U32 at)->std::optional<float>{auto v=m.Read(at);if(!v)return {};
            float n=std::bit_cast<float>(*v);return std::isfinite(n)?std::optional<float>(n):std::nullopt;};
        for(unsigned i=0;i<count;++i) {
            const auto pane=panes[i];if(!pane)return root;
            auto geometry=m.Read(pane+0x28);if(!geometry||!Pointer(*geometry,0x7c))return root;
            auto x=read(pane+0xec),y=read(pane+0xfc),xx=read(pane+0xe0),xy=read(pane+0xe4),
                 yx=read(pane+0xf0),yy=read(pane+0xf4),gx=read(*geometry+0x14),gy=read(*geometry+0x18),
                 w=read(*geometry+0x20),h=read(*geometry+0x24);
            if(!x||!y||!xx||!xy||!yx||!yy||!gx||!gy||!w||!h||*w<=0||*h<=0)return root;
            const bool straight=*xy==0&&*yx==0&&*xx!=0&&*yy!=0;
            const bool rotated=*xx==0&&*yy==0&&*xy!=0&&*yx!=0;
            if(!straight&&!rotated)return root;
            const float x0=*x+*xx**gx+*xy**gy,x1=x0+*xx**w+*xy**h,
                        y0=*y+*yx**gx+*yy**gy,y1=y0+*yx**w+*yy**h;
            bounds[i]={std::min(x0,x1),std::max(x0,x1),std::max(y0,y1),straight?std::abs(*yy):std::abs(*yx)};
            bottoms[group(i)]=std::max(bottoms[group(i)],bounds[i].bottom);
        }
        constexpr U32 Size=0x1800;
        if(sp<0x104000||(sp&7))return root;const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return root;
        std::array<U32,Size/4> words{};
        const auto put=[&](U32 at,U32 v){words[at/4]=v;};
        const auto f=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
        const auto copy=[&](U32 at,U32 from,U32 size){for(U32 j=0;j<size;j+=4){auto v=m.Read(from+j);if(!v)return false;put(at+j,*v);}return true;};
        if(!copy(0,root,0x140))return root;
        put(0x14,base+0x140);put(0x18,3*count+1);put(0x20,0);put(0x24,0xffff);put(0x28,0);
        // Draw the shadow copies before the untouched foreground panel.
        put(0x140+3*count*4,root);
        for(unsigned layer=0;layer<3;++layer)for(unsigned part=0;part<count;++part){
            const U32 source=panes[part],index=layer*count+part,node=0x200+index*0x200,geo=node+0x140;
            auto geometry=m.Read(source+0x28),alpha=m.Read(source+0x134),x=m.Read(source+0xec),y=m.Read(source+0xfc);
            if(!geometry||!alpha||!x||!y||!Pointer(*geometry,0x7c))return root;
            auto type=m.Read(*geometry+4);if(!type||(*type&65535)!=6)return root;
            const float a=std::bit_cast<float>(*alpha),px=std::bit_cast<float>(*x),py=std::bit_cast<float>(*y);
            if(!std::isfinite(a)||a<0||a>1||!std::isfinite(px)||!std::isfinite(py))return root;
            if(!copy(node,source,0x140)||!copy(geo,*geometry,0x7c))return root;
            put(0x140+index*4,base+node);put(node+0x28,base+geo);
            put(node+0x14,0);put(node+0x18,0);put(node+0x20,0);put(node+0x24,0xffff);
            for(unsigned c=0;c<3;++c)f(node+0x128+c*4,0);
            const auto box=bounds[part];const float bottom=bottoms[group(part)];
            const bool bottom_piece=std::abs(box.bottom-bottom)<.01f;
            // Match the existing Items panel: three disjoint two-unit bands,
            // wholly outside the translucent panel, with inherited fade.
            f(node+0x134,bottom_piece?a*(.32f-.09f*layer):0.f);
            f(node+0xe0,1);f(node+0xe4,0);f(node+0xf0,0);f(node+0xf4,1);
            f(node+0xec,box.left);f(node+0xfc,bottom+2*layer*box.scale);
            f(geo+0x14,0);f(geo+0x18,0);
            f(geo+0x20,box.right-box.left);f(geo+0x24,2*box.scale);
        }
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
        for(auto w:frame.writes)m.Write(w.address,w.after);frame.sp=sp;return base;
    }
};
}
