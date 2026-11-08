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
            gear=LayoutNamed(m,layout,"MenuCollect"),giving_masks=LayoutNamed(m,layout,"MenuMaskPresen"),
            giving=giving_masks||LayoutNamed(m,layout,"MenuItemPresen");
        if(giving) {
            if(!present||!present->Enabled()||present->pass!=3||layout!=present->panel||
               std::find(present->routed.begin(),present->routed.end(),layout)==present->routed.end())return root;
        } else if(!(items||masks||gear)||!(gear?GearMenu::enabled.load():enabled.load())||
                  inventory.pass!=3||layout!=inventory.panel||
                  std::find(inventory.routed.begin(),inventory.routed.end(),layout)==inventory.routed.end())return root;
        std::vector<NativeInventory::Hook::Write> unused;std::vector<U32> nodes;
        if(!inventory.Transform(m,root,unused,nodes,{1,0,0,1,0,0}))return root;
        const std::array<const char*,3> names=gear?
            std::array<const char*,3>{"left_base_pane","right_base_pane",nullptr}:
            (masks||giving_masks)?std::array<const char*,3>{"mask_panel_pane00","mask_panel_pane01",giving_masks?"mask_panel_pane02":nullptr}:
            std::array<const char*,3>{"item_panel_pane00","item_panel_pane01",giving?"right_base_pane":nullptr};
        const unsigned count=giving?3:2;
        std::array<U32,3> panes{};
        for(auto node:nodes)for(unsigned i=0;i<count;++i)if(Named(m,node,names[i])) {
            if(panes[i])return root;panes[i]=node;
        }
        // Use the current world rectangles after the menu's live transforms.
        // Gear / Select Item right strips can be rotated ninety degrees.
        struct Bounds {float left,right,bottom,scale,top;};
        std::array<Bounds,3> bounds{};std::array<float,2> bottoms{-1e9f,-1e9f};
        const auto group=[&](unsigned i){return gear?i:(!giving_masks&&i==2?1u:0u);};
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
            bounds[i]={std::min(x0,x1),std::max(x0,x1),std::max(y0,y1),straight?std::abs(*yy):std::abs(*yx),std::min(y0,y1)};
            bottoms[group(i)]=std::max(bottoms[group(i)],bounds[i].bottom);
        }
        if(giving) {
            // Items have two physical panels; masks have one made of three
            // texture pieces. Shade only the exterior of each complete panel.
            constexpr U32 Size=0xc400;
            if(sp<0x11a000||(sp&7))return root;
            const U32 base=sp-Size;
            for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return root;
            std::array<U32,Size/4> words{};
            const auto put=[&](U32 at,U32 v){words[at/4]=v;};
            const auto f=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
            const auto copy=[&](U32 to,U32 from,U32 n){for(U32 j=0;j<n;j+=4){auto v=m.Read(from+j);if(!v)return false;put(to+j,*v);}return true;};
            if(!copy(0,root,0x140))return root;
            put(0x14,base+0x140);put(0x20,0);put(0x24,0xffff);put(0x28,0);
            unsigned emitted=0;
            for(unsigned panel=0;panel<(giving_masks?1u:2u);++panel) {
                const unsigned part=panel?2:0;const U32 source=panes[part];
                auto box=bounds[part];
                if(!panel)for(unsigned i=1;i<(giving_masks?3u:2u);++i) {
                    box.left=std::min(box.left,bounds[i].left);box.right=std::max(box.right,bounds[i].right);
                    box.top=std::min(box.top,bounds[i].top);box.bottom=std::max(box.bottom,bounds[i].bottom);
                }
                auto geometry=m.Read(source+0x28),alpha=m.Read(source+0x134),flags=m.Read(source+0xd4);
                if(!geometry||!alpha||!flags||(m.Read(*geometry+4).value_or(0)&65535)!=6)return root;
                const float opacity=std::bit_cast<float>(*alpha);
                if(!std::isfinite(opacity)||opacity<0||opacity>1)return root;
                if(!(*flags&1)||opacity==0)continue;
                // The wide mask grid needs a broader, bottom-weighted shadow
                // to separate it from the world, even at small editor scales.
                const float step=box.scale*(giving_masks?2.5f:1.f);
                const float top_step=step*(giving_masks?.7f:1.f);
                const float bottom_step=step*(giving_masks?1.4f:1.f);
                const auto emit=[&](float x,float y,float w,float h,float a) {
                    if(emitted>=96)return false;
                    const U32 node=0x300+emitted*0x200,geo=node+0x140;
                    if(!copy(node,source,0x140)||!copy(geo,*geometry,0x7c))return false;
                    put(0x140+emitted++*4,base+node);put(node+0x28,base+geo);
                    put(node+0x14,0);put(node+0x18,0);put(node+0x20,0);put(node+0x24,0xffff);
                    for(unsigned c=0;c<3;++c)f(node+0x128+c*4,0);
                    f(node+0x134,opacity*a);f(node+0xe0,1);f(node+0xe4,0);f(node+0xf0,0);f(node+0xf4,1);
                    f(node+0xec,x);f(node+0xfc,y);f(geo+0x14,0);f(geo+0x18,0);f(geo+0x20,w);f(geo+0x24,h);
                    return true;
                };
                const auto fade=[&](float distance){const float t=std::max(0.f,1.f-distance/3.f);return (giving_masks?.6f:.38f)*t*t;};
                if(giving_masks) {
                    // The old three broad bands and corner cells became
                    // visible blocks at high output resolution. Use a finely
                    // sampled continuous falloff, with disjoint exterior rings.
                    // Each ring owns its corners exactly once: translucent
                    // overlaps must not darken or shimmer at their joins.
                    constexpr unsigned bands=24;
                    for(unsigned band=0;band<bands;++band) {
                        const float inner=3.f*band/bands,outer=3.f*(band+1)/bands;
                        const float l0=box.left-inner*step,r0=box.right+inner*step,
                                    t0=box.top-inner*top_step,b0=box.bottom+inner*bottom_step;
                        const float l1=box.left-outer*step,r1=box.right+outer*step,
                                    t1=box.top-outer*top_step,b1=box.bottom+outer*bottom_step;
                        const float a=fade((inner+outer)*.5f);
                        if(!emit(l1,t0,l0-l1,b0-t0,a)||!emit(r0,t0,r1-r0,b0-t0,a)||
                           !emit(l1,t1,r1-l1,t0-t1,a)||!emit(l1,b0,r1-l1,b1-b0,a))return root;
                    }
                    continue;
                }
                for(unsigned band=0;band<3;++band) {
                    const float d=band*step,a=fade(band+.5f);
                    if(!emit(box.left-step-d,box.top,step,box.bottom-box.top,a)||
                       !emit(box.right+d,box.top,step,box.bottom-box.top,a)||
                       !emit(box.left,box.top-(band+1)*top_step,box.right-box.left,top_step,a)||
                       !emit(box.left,box.bottom+band*bottom_step,box.right-box.left,bottom_step,a))return root;
                }
                // Radial falloff around each corner joins adjacent edges;
                // distant corner cells fade to zero instead of a square cutoff.
                for(unsigned cy=0;cy<3;++cy)for(unsigned cx=0;cx<3;++cx) {
                    const float a=fade(std::hypot(cx+.5f,cy+.5f));if(a<=0)continue;
                    for(bool right:{false,true})for(bool bottom:{false,true})
                        if(!emit(right?box.right+cx*step:box.left-(cx+1)*step,
                                 bottom?box.bottom+cy*bottom_step:box.top-(cy+1)*top_step,
                                 step,bottom?bottom_step:top_step,a))return root;
                }
            }
            if(!emitted)return root;
            put(0x140+emitted*4,root);put(0x18,emitted+1);
            for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
            for(auto w:frame.writes)m.Write(w.address,w.after);frame.sp=sp;return base;
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
