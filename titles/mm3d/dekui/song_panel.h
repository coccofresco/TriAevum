// Ported from mm3d-dekui 4f2692ed src/mm3d/song_panel.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "notebook_prompt.h"
namespace Mm3dDekui::SongPanel {
using namespace NativeInventory;
struct Hook {
    NotebookPrompt::Hook scratch;
    template<class M> U32 Source(M& m,U32 layout) {
        const auto all=scratch.Child(m,layout+8,"all");
        const auto staff=scratch.Child(m,all,"four_line_staff_g");
        const auto pane=scratch.Child(m,staff,"four_line_staff_pane02");
        const auto geo=m.Read(pane+0x28);
        if(!pane||!geo||!Pointer(*geo,0x7c)||m.Read(*geo+4).value_or(0)%65536!=6)return 0;
        // Retail right cap: 44x86, with a plain strip at its far right.
        for(auto [off,v]:{std::pair{0x20u,44.f},std::pair{0x24u,86.f}})
            if(m.Read(*geo+off)!=std::optional<U32>(std::bit_cast<U32>(v)))return 0;
        return pane;
    }
    template<class M> U32 Root(M& m,U32 layout,U32 content,U32 sp,float x,float y,float width,float height,float opacity,float darkness=0.f,std::array<float,3> color={1.f,1.f,1.f},float color_opacity=0.f) {
        scratch.frames.emplace_back();auto& frame=scratch.frames.back();
        const auto source=Source(m,layout);if(!source)return content;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||
           width<16||width>1200||height<16||height>600||!std::isfinite(opacity)||opacity<=0||opacity>1)return content;
        if(!std::isfinite(darkness)||darkness<0||darkness>1)return content;
        if(!std::isfinite(color_opacity)||color_opacity<0||color_opacity>1)return content;
        for(float c:color)if(!std::isfinite(c)||c<0||c>1)return content;
        const U32 geo=*m.Read(source+0x28);
        std::array<float,4> uv{};
        for(unsigned i=0;i<4;++i){auto v=m.Read(geo+0x28+i*4);if(!v)return content;uv[i]=std::bit_cast<float>(*v);if(!std::isfinite(uv[i]))return content;}
        if(uv[2]<=0||uv[3]<=0)return content;
        constexpr U32 Size=0x1600;
        if(sp<0x104000||(sp&7))return content;const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return content;
        std::array<U32,Size/4> words{};
        const auto put=[&](U32 at,U32 v){words[at/4]=v;};
        const auto f=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
        const auto copy=[&](U32 at,U32 from,U32 size){for(U32 j=0;j<size;j+=4){auto v=m.Read(from+j);if(!v)return false;put(at+j,*v);}return true;};
        if(!copy(0,layout+8,0x140))return content;
        put(0x14,base+0x140);put(0x18,4);put(0x20,0);put(0x24,0xffff);put(0x28,0);put(0x140,content);
        // Three full-height strips, with no top/bottom border or corners.
        // Mirror the right edge for the left;
        // use only the last 12 pixels, excluding all staff/clef artwork.
        const float pixel_u=uv[2]/44,pixel_v=uv[3]/86;
        const float border=std::min(3.f,std::min(width,height)/4);
        const float xs[]{x,x+border,x+width-border,x+width};
        const float us[]{uv[0]+uv[2],uv[0]+uv[2]-12*pixel_u,uv[0]+uv[2]-8*pixel_u};
        const float uw[]{-8*pixel_u,4*pixel_u,8*pixel_u};
        for(unsigned col=0;col<3;++col) {
            const unsigned i=col;const U32 n=0x200+i*0x200,g=n+0x140;
            if(!copy(n,source,0x140)||!copy(g,geo,0x7c))return content;
            put(0x144+i*4,base+n);put(n+0x14,0);put(n+0x18,0);put(n+0x20,0);put(n+0x24,0xffff);put(n+0x28,base+g);put(n+0xd4,15);
            put(n+0xdc,words[(n+0xdc)/4]&~0xff00u);
            for(unsigned j=0;j<8;++j)f(n+0xe0+j*4,(j==0||j==5)?1.f:0.f);
            for(unsigned j=0;j<6;++j)f(n+0x110+j*4,(j==0||j==4)?1.f:0.f);
            for(unsigned j=0;j<3;++j)f(n+0x128+j*4,1);f(n+0x134,opacity);
            f(g+0x14,xs[col]);f(g+0x18,y);f(g+0x20,xs[col+1]-xs[col]);f(g+0x24,height);
            f(g+0x28,us[col]);f(g+0x2c,uv[1]+16*pixel_v);f(g+0x30,uw[col]);f(g+0x34,pixel_v);
        }
        if(darkness>0) {
            // Black backing behind the textured panel: darkens the visible
            // world through its transparent fill without tinting the border.
            constexpr U32 n=0x800,g=n+0x140;
            for(U32 j=0;j<0x140;j+=4)put(n+j,words[(0x200+j)/4]);
            for(U32 j=0;j<0x7c;j+=4)put(g+j,words[(0x340+j)/4]);
            put(0x18,5);put(0x150,base+n);put(n+0x28,base+g);
            for(unsigned c=0;c<3;++c)f(n+0x128+c*4,0);f(n+0x134,opacity*darkness);
            put(g+0xc,0x100);put(g+0x10,0);put(g+0x38,0);
            f(g+0x14,x);f(g+0x18,y);f(g+0x20,width);f(g+0x24,height);
            for(unsigned c=0;c<16;++c)f(g+0x3c+c*4,1);
        }
        if(color_opacity>0) {
            // Native children draw back-to-front. Insert after content so this
            // overlays the panel fill, beneath song stones/text and inside edges.
            constexpr U32 n=0xa00,g=n+0x140;
            for(U32 j=0;j<0x140;j+=4)put(n+j,words[(0x200+j)/4]);
            for(U32 j=0;j<0x7c;j+=4)put(g+j,words[(0x340+j)/4]);
            const U32 count=words[0x18/4];
            for(U32 i=count;i>1;--i)put(0x140+i*4,words[(0x140+(i-1)*4)/4]);
            put(0x144,base+n);put(0x18,count+1);put(n+0x28,base+g);
            for(unsigned c=0;c<3;++c)f(n+0x128+c*4,color[c]);
            f(n+0x134,opacity*color_opacity);
            put(g+0xc,0x100);put(g+0x10,0);put(g+0x38,0);
            f(g+0x14,x+border);f(g+0x18,y);f(g+0x20,width-2*border);f(g+0x24,height);
            for(unsigned c=0;c<16;++c)f(g+0x3c+c*4,1);
        }
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
        for(auto w:frame.writes)m.Write(w.address,w.after);frame.sp=sp;return base;
    }
};
}
