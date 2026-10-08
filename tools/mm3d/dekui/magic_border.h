// Ported from mm3d-dekui src/mm3d/magic_border.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "notebook_prompt.h"

namespace Mm3dDekui::MagicBorder {
using namespace NativeInventory;
struct Options { bool enabled=true; float thickness=1; std::array<float,3> color{1,1,1}; bool shadow_enabled=true; float shadow_size=2.5f,shadow_opacity=.8f; };
inline std::atomic<bool> enabled{true};
inline std::atomic<float> thickness{1},red{1},green{1},blue{1};
inline std::atomic<bool> shadow_enabled{true};
inline std::atomic<float> shadow_size{2.5f},shadow_opacity{.8f};
inline void Apply(const Options& o){enabled=o.enabled;thickness=o.thickness;red=o.color[0];green=o.color[1];blue=o.color[2];shadow_enabled=o.shadow_enabled;shadow_size=o.shadow_size;shadow_opacity=o.shadow_opacity;}
struct Hook {
    NotebookPrompt::Hook scratch;
    template<class M> U32 Root(M& m,U32 layout,U32 sp) {
        scratch.frames.emplace_back();auto& frame=scratch.frames.back();
        const U32 root=layout+8;
        if(!LayoutNamed(m,layout,"Hud2"))return root;
        // Verified native mode5 stage: RGB replaces with previous/constant,
        // alpha multiplies previous alpha by texture alpha. Color word varies.
        if(m.Read(0x6561ac)!=std::optional<U32>(0x0e3f0eef)||
           m.Read(0x6561b0)!=std::optional<U32>(0x804f00c8)||
           m.Read(0x6561b4)!=std::optional<U32>(0)||
           m.Read(0x6561b8)!=std::optional<U32>(0x10000))return root;
        const float t=thickness.load();const std::array<float,3> color{red.load(),green.load(),blue.load()};
        if(!std::isfinite(t)||t<1||t>3)return root;
        for(float c:color)if(!std::isfinite(c)||c<0||c>1)return root;
        std::array<U32,3> panes{};
        std::array<float,4> bar_left{},bar_right{},bar_scale{};
        std::vector<U32> pending{root},visited;
        while(!pending.empty()) {
            const U32 n=pending.back();pending.pop_back();
            if(!Pointer(n,0x140)||visited.size()>512||std::find(visited.begin(),visited.end(),n)!=visited.end())return root;
            visited.push_back(n);
            auto flags=m.Read(n+0xd4),alpha=m.Read(n+0x134);
            if(!flags||!alpha)return root;
            if(!(*flags&1)||std::bit_cast<float>(*alpha)<=0)continue;
            if(Named(m,n,"gauge_base00"))panes[0]=n;
            if(Named(m,n,"gauge_base01_pane"))panes[1]=n;
            if(Named(m,n,"gauge_base02_pane"))panes[2]=n;
            auto list=m.Read(n+0x14),count=m.Read(n+0x18);if(!list||!count||*count>256)return root;
            for(unsigned i=0;i<*count;++i){auto child=m.Read(*list+4*i);if(!child)return root;if(*child)pending.push_back(*child);}
        }
        if(!panes[0]||!panes[1]||!panes[2])return root;
        const float distance=shadow_size.load(),opacity=shadow_opacity.load();
        const bool draw_shadow=shadow_enabled&&distance>0&&opacity>0;
        if(!std::isfinite(distance)||distance<0||distance>8||!std::isfinite(opacity)||opacity<0||opacity>1)return root;
        constexpr U32 Size=0x2400;
        if(sp<0x106000||(sp&7))return root;
        const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return root;
        std::array<U32,Size/4> words{};
        const auto put=[&](U32 at,U32 v){words[at/4]=v;};
        const auto number=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
        const auto copy=[&](U32 at,U32 src,U32 size){for(U32 i=0;i<size;i+=4){auto v=m.Read(src+i);if(!v)return false;put(at+i,*v);}return true;};
        const auto f=[&](U32 at)->std::optional<float>{auto v=m.Read(at);if(!v)return {};float x=std::bit_cast<float>(*v);if(!std::isfinite(x))return {};return x;};
        if(!copy(0,root,0x140))return root;
        put(0x14,base+0x140);put(0x18,(enabled?4:0)+1+(draw_shadow?8:0));put(0x20,0);put(0x24,0xffff);put(0x28,0);
        // Foreground first at equal native depth, then the unchanged HUD tree.
        unsigned child=0;
        if(enabled)for(unsigned i=0;i<4;++i)put(0x140+4*child++,base+0x200+i*0x200);
        put(0x140+4*child++,root);
        for(unsigned i=0;i<4;++i) {
            const U32 src=panes[i==0?0:i==3?2:1];auto gp=m.Read(src+0x28);if(!gp||!Pointer(*gp,0x7c))return root;
            auto type=m.Read(*gp+4);if(!type||(*type&65535)!=6)return root;
            auto px=f(*gp+0x14),py=f(*gp+0x18),w=f(*gp+0x20),h=f(*gp+0x24);
            auto xx=f(src+0xe0),yy=f(src+0xf4),xy=f(src+0xe4),yx=f(src+0xf0),tx=f(src+0xec),ty=f(src+0xfc);
            if(!px||!py||!w||!h||!xx||!yy||!xy||!yx||!tx||!ty||*xx<=0||*yy<=0||*h!=9||*w<=0||*xy!=0||*yx!=0)return root;
            const U32 node=0x200+i*0x200,geo=node+0x140;
            if(!copy(node,src,0x140)||!copy(geo,*gp,0x7c))return root;
            put(node+0x28,base+geo);
            put(node+0x14,0);put(node+0x18,0);put(node+0x20,0);put(node+0x24,0xffff);
            // Native pane material mode1: constant RGB, texture alpha. It is
            // the game's own TEV setup, not a texture replacement or PC overlay.
            put(geo+0xc,(words[(geo+0xc)/4]&~0x03000000u)|0x01000000u);
            for(unsigned c=0;c<3;++c)number(node+0x128+4*c,color[c]);
            // Preserve animation/visibility alpha, depth, UV transforms and Z.
            float x=*tx+*xx**px,y=*ty+*yy**py,width=*xx**w,height=*yy**h;
            if(i==0||i==3) {
                if(*w!=2)return root;
                const float cap=2*std::max(*xx,*yy)*t;
                if(i==0)x+=width-cap;
                width=cap;
            } else {
                auto v=f(*gp+0x2c),vh=f(*gp+0x34);if(!v||!vh)return root;
                const float edge=std::min(height*.45f,*yy*t);
                if(i==2){y+=height-edge;number(geo+0x2c,*v+*vh*8.f/9.f);}
                number(geo+0x34,*vh/9.f);height=edge;
            }
            number(geo+0x14,0);number(geo+0x18,0);number(geo+0x20,width);number(geo+0x24,height);
            number(node+0xe0,1);number(node+0xe4,0);number(node+0xf0,0);number(node+0xf4,1);
            number(node+0xec,x);number(node+0xfc,y);
            bar_left[i]=x;bar_right[i]=x+width;bar_scale[i]=*xx;
        }
        // Draw the shadow only outside the meter. A full silhouette behind
        // the translucent interior would make the black background opaque again.
        if(draw_shadow) {
            const float left=std::min(bar_left[0],bar_left[3]);
            const float right=std::max(bar_right[0],bar_right[3]);
            const U32 src=panes[1],gpv=m.Read(src+0x28).value_or(0);
            if(!gpv)return root;
            auto y=f(src+0xfc),sy=f(src+0xf4),py=f(gpv+0x18),height=f(gpv+0x24),alpha=f(src+0x134);
            if(!y||!sy||!py||!height||!alpha||*alpha<0||*alpha>1)return root;
            const float top=*y+*sy**py,bottom=top+*sy**height;
            for(unsigned layer=0;layer<4;++layer)for(unsigned side=0;side<2;++side) {
                const U32 node=0xa00+(layer*2+side)*0x200,geo=node+0x140;
                if(!copy(node,src,0x140)||!copy(geo,gpv,0x7c))return root;
                put(0x140+4*child++,base+node);put(node+0x28,base+geo);
                put(node+0x14,0);put(node+0x18,0);put(node+0x20,0);put(node+0x24,0xffff);
                put(geo+0xc,(words[(geo+0xc)/4]&~0x03000000u)|0x01000000u);
                for(unsigned c=0;c<3;++c)number(node+0x128+4*c,0);
                const float offset=distance*(layer+1)/4.f;
                const float dx=offset*bar_scale[1],dy=offset**sy;
                number(node+0xec,side?right:left+dx);
                number(node+0xfc,side?top+std::min(dy,bottom-top):bottom);
                number(node+0xe0,1);number(node+0xe4,0);number(node+0xf0,0);number(node+0xf4,1);
                number(geo+0x14,0);number(geo+0x18,0);
                number(geo+0x20,side?dx:right-left);
                number(geo+0x24,side?std::max(0.f,bottom-top-dy):dy);
                number(node+0x134,*alpha*opacity*.4f*(1.f-layer/5.f));
            }
        }
        // Fade only the original frame/background panes. The green/yellow fill
        // and cloned border retain their own native fade alpha.
        for(auto pane:panes) {
            auto alpha=f(pane+0x134);if(!alpha||*alpha<0||*alpha>1)return root;
            frame.writes.push_back({pane+0x134,*m.Read(pane+0x134),std::bit_cast<U32>(*alpha*.7f)});
        }
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+i*4,*m.Read(base+i*4),words[i]});
        for(auto w:frame.writes)m.Write(w.address,w.after);
        frame.sp=sp;return base;
    }
    template<class M> U32 End(M& m){return scratch.End(m);}
};
}
