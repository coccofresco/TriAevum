// Ported from mm3d-dekui 4f2692ed src/mm3d/song_header.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "notebook_prompt.h"
namespace Mm3dDekui::SongHeader {
using namespace NativeInventory;
struct Hook {
    NotebookPrompt::Hook scratch;
    U32 source_layout=0;
    template<class M> U32 Root(M& m,U32 layout,U32 content,U32 sp,float x,float y,float width,float height,float opacity) {
        scratch.frames.emplace_back();auto& frame=scratch.frames.back();
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||
           width<2||width>1800||height<1||height>120||!std::isfinite(opacity)||opacity<=0||opacity>1)return content;
        if(!LayoutNamed(m,source_layout,"MenuOcarina"))return content;
        const U32 all=scratch.Child(m,source_layout+8,"all");
        const U32 message=scratch.Child(m,all,"message_g");
        const U32 text_group=scratch.Child(m,message,"e_message_pane_g");
        const U32 text=scratch.Child(m,text_group,"e_message_pane");
        const auto tg_opt=m.Read(text+0x28);
        if(!text||!tg_opt||!Pointer(*tg_opt,0x30c)||
           !m.Read(*tg_opt+4)||(*m.Read(*tg_opt+4)&65535)!=3)return content;
        const U32 tg=*tg_opt;
        auto font=m.Read(tg+0x28);if(!font)return content;
        const auto label=PromptText::Codes(PromptText::Get(m,"Songs"));
        if(label.empty()||label.size()>14)return content;
        std::vector<NotebookFont::Glyph> glyphs(label.size());float pen=0,glyph_height=0;
        unsigned character=0;
        for(char32_t c:label) {
            auto g=NotebookFont::Lookup(m,*font,c,pen);if(!g)return content;
            glyphs[character++]=*g;pen+=g->advance;
            glyph_height=std::bit_cast<float>(g->words[9]);
        }
        struct Pane {U32 node,geometry;float x,y,w,h;};
        std::array<Pane,3> panes{};
        float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;
        unsigned index=0;
        // Reuse all three original decorated banner panes, including their
        // texture, UVs, vertex colors and native layer order. No drawn substitute.
        for(const char* name:{"bb_message_pane02","bb_message_pane01","bb_message_pane00"}) {
            const U32 n=scratch.Child(m,message,name);auto geo=m.Read(n+0x28);
            if(!n||!geo||!Pointer(*geo,0x7c)||!m.Read(*geo+4)||(*m.Read(*geo+4)&65535)!=6)return content;
            Pane p{n,*geo};float* values[]{&p.x,&p.y,&p.w,&p.h};
            for(unsigned i=0;i<4;++i) {
                auto v=m.Read(*geo+(i<2?0x14+i*4:0x20+(i-2)*4));if(!v)return content;
                *values[i]=std::bit_cast<float>(*v);if(!std::isfinite(*values[i]))return content;
            }
            if(p.w<=0||p.h<=0)return content;
            left=std::min(left,p.x);top=std::min(top,p.y);
            right=std::max(right,p.x+p.w);bottom=std::max(bottom,p.y+p.h);panes[index++]=p;
        }
        if(!std::isfinite(glyph_height)||glyph_height<=0||pen<=0||right<=left||bottom<=top)return content;
        constexpr U32 Size=0x1c00,Text=0x1400,TextGeo=0x1540,Glyphs=0x1880;
        if(sp<0x104000||(sp&7))return content;const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return content;
        std::array<U32,Size/4> words{};
        const auto put=[&](U32 at,U32 v){words[at/4]=v;};
        const auto f=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
        const auto copy=[&](U32 at,U32 from,U32 count){for(U32 i=0;i<count;i+=4){auto v=m.Read(from+i);if(!v)return false;put(at+i,*v);}return true;};
        const auto node=[&](U32 at,U32 geo,float dx,float dy,float scale) {
            put(at+0x14,0);put(at+0x18,0);put(at+0x20,0);put(at+0x24,0xffff);put(at+0x28,geo);put(at+0xd4,15);
            put(at+0xdc,words[(at+0xdc)/4]&~0xff00u);
            for(unsigned i=0;i<8;++i)f(at+0xe0+i*4,(i==0||i==5)?scale:0);
            f(at+0xec,dx);f(at+0xfc,dy);
            for(unsigned i=0;i<6;++i)f(at+0x110+i*4,(i==0||i==4)?1:0);
            for(unsigned i=0;i<3;++i)f(at+0x128+i*4,1);f(at+0x134,opacity);
        };
        if(!copy(0,layout+8,0x140))return content;
        node(0,0,0,0,1);put(0x14,base+0x140);put(0x18,5);put(0x140,content);
        const float sx=width/(right-left),sy=height/(bottom-top);
        for(unsigned i=0;i<panes.size();++i) {
            const U32 n=0x200+i*0x200,g=n+0x140;const auto& source=panes[i];
            if(!copy(n,source.node,0x140)||!copy(g,source.geometry,0x7c))return content;
            put(0x148+i*4,base+n);
            node(n,base+g,x-left*sx,y-top*sy,1);
            f(n+0xe0,sx);f(n+0xf4,sy);
            for(unsigned c=0;c<3;++c) {
                auto color=m.Read(source.node+0x128+c*4);if(!color)return content;
                put(n+0x128+c*4,*color);
            }
        }
        if(!copy(Text,text,0x140)||!copy(TextGeo,tg,0x30c))return content;
        const float scale=std::min(height*.5f/glyph_height,(width*.9f)/pen);
        node(Text,base+TextGeo,x+(width-pen*scale)/2,y+(height-glyph_height*scale)/2,scale);
        // Native message_g visits text before the three background panes.
        // Preserve that order for its depth-tested glyph/background rendering.
        put(0x144,base+Text);f(TextGeo+0x34,0);f(TextGeo+0x38,0);
        put(TextGeo+0x118,words[(TextGeo+0x118)/4]&~0xff00u);put(TextGeo+0x2fc,base+Glyphs);
        for(U32 off:{0x300u,0x304u,0x308u})put(TextGeo+off,glyphs.size());
        // The source prompt can retain a dim default text color when hidden.
        // Select an explicit white palette entry; node alpha still follows the list.
        for(unsigned c=0;c<4;++c)f(TextGeo+0x78+c*4,1.f);
        for(unsigned i=0;i<glyphs.size();++i) {
            glyphs[i].words[14]=0;
            for(unsigned j=0;j<15;++j)put(Glyphs+i*60+j*4,glyphs[i].words[j]);
        }
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
        for(auto w:frame.writes)m.Write(w.address,w.after);frame.sp=sp;return base;
    }
};
}
