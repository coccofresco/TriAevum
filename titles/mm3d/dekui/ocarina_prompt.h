// Ported from mm3d-dekui src/mm3d/ocarina_prompt.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include "notebook_prompt.h"

namespace Mm3dDekui::OcarinaPrompt {
using namespace NativeInventory;
struct Options { bool enabled=true; float x=0,y=0,scale=1,icon_scale=1,spacing=22; };
inline std::atomic<bool> enabled{true};
inline std::atomic<float> x{0},y{0},scale{1},icon_scale{1},spacing{22};
inline std::atomic<unsigned> status{0};
inline void Apply(const Options& o){enabled=o.enabled;x=o.x;y=o.y;scale=o.scale;icon_scale=o.icon_scale;spacing=o.spacing;}
struct SinglePrompt { ButtonPose pose; U32 content_root; U32 arrow_texture=0; std::array<ButtonPose,3> elements{}; bool reverse=false; std::string_view label{}; U32 button_u=144; };
struct PairPrompt { Options options; U32 content_root; std::array<std::string_view,2> labels; std::array<U32,2> buttons; };
struct Hook {
    NotebookPrompt::Hook scratch;
    U32 pass=~0u,panel=0,icon=0,text=0,texture=0;
    std::chrono::steady_clock::time_point opened{};
    float native_alpha=0;
    bool map_mode=false;
    // Main package, group0 resource11 is the localized parts atlas. Resolve the
    // live record each frame; never retain a resource across package unloading.
    template<class M> U32 Parts(M& m,U32 id=11) {
        constexpr U32 registry=NotebookPrompt::Registry;
        if(m.Read(NotebookPrompt::RegistryLiteral)!=std::optional<U32>(registry))return 0;
        // The resource registry is the manager subobject at +4.
        // Manager+24 is the layout-name table, not the resource groups.
        auto groups=m.Read(registry+4+0x24),entries=m.Read(registry+4+0x28);
        if(!groups||!entries||!Pointer(*groups,8)||!Pointer(*entries))return 0;
        auto range=m.Read(*groups);if(!range||(*range&65535)<=id)return 0;
        const U32 index=(*range>>16)+id;
        if(index>4096)return 0;
        auto record=m.Read(*entries+index*4);
        if(!record||!Pointer(*record,0x60)||m.Read(*record+4)!=std::optional<U32>(id<<16))return 0;
        return *record+0xc;
    }
    template<class M> bool Templates(M& m,U32 layout) {
        icon=text=0;native_alpha=1;texture=Parts(m);if(!texture)return false;
        auto list=m.Read(layout+0x154),count=m.Read(layout+0x158);
        if(!list||!count||*count>256||!Pointer(*list,*count*4))return false;
        unsigned buttons=0;
        for(unsigned i=0;i<*count;++i) {
            auto w=m.Read(*list+4*i);if(!w)continue;
            if(Named(m,*w,"btn_quit_g")||Named(m,*w,"btn_music_g")) {
                auto a=m.Read(*w+0x134),flags=m.Read(*w+0xd4);if(!a||!flags)return false;
                const float alpha=std::bit_cast<float>(*a);if(!std::isfinite(alpha)||alpha<0||alpha>1)return false;
                native_alpha=std::min(native_alpha,(*flags&1)?alpha:0.f);++buttons;
            }
            auto geo=m.Read(*w+0x28);if(!geo||!Pointer(*geo,0x14))continue;
            auto kind=m.Read(*geo+4);if(!kind)continue;
            if(Named(m,*w,"btn_music_pane")&&((*kind&65535)==6||(*kind&65535)==4))icon=*w;
            if(Named(m,*w,"e_message_pane")&&(*kind&65535)==3)text=*w;
        }
        return icon&&text&&buttons==2;
    }
    template<class M> void Pass(M& m,U32 manager,U32 screen) {
        pass=screen;if(screen!=5)return;
        const U32 previous=panel;panel=0;status=0;
        if(!enabled||!Pointer(manager,0x20))return;
        auto bottom=m.Read(manager+28),top=m.Read(manager+20);
        NativeInventory::Hook helper;NativeInventory::Hook::Buckets from,to;
        if(!bottom||!top||*bottom==*top||!helper.ReadQueue(m,*bottom,from)||!helper.ReadQueue(m,*top,to))return;
        U32 candidate=0;
        for(const auto& bucket:from)for(U32 layout:bucket)if(LayoutNamed(m,layout,"MenuOcarina")) {
            if(candidate)return;candidate=layout;
        }
        if(!candidate)return;status=1;
        if(!Templates(m,candidate)||to[15].size()>=32)return;
        for(const auto& bucket:to)if(std::find(bucket.begin(),bucket.end(),candidate)!=bucket.end())return;
        const U32 at=*top+4+15*0x80+U32(to[15].size())*4;
        auto total=m.Read(*top+0x844);if(!total||!m.Read(at))return;
        panel=candidate;if(panel!=previous)opened=std::chrono::steady_clock::now();
        m.Write(at,panel);m.Write(*top+0x804+15*4,U32(to[15].size()+1));m.Write(*top+0x844,*total+1);status=2;
    }
    template<class M> U32 Root(M& m,U32 layout,U32 sp,bool wide,const SinglePrompt* single=nullptr,const PairPrompt* pair=nullptr) {
        scratch.frames.emplace_back();auto& frame=scratch.frames.back();
        if(pass!=3||!panel||layout!=panel)return layout+8;
        // If scratch/font validation fails, suppress the extra top-screen copy.
        const U32 root=pair?pair->content_root:single?single->content_root:layout+8;
        const bool flat=single&&!single->label.empty();
        if(single&&!flat&&!single->arrow_texture)return root;
        auto flags=m.Read(root+0xd4);if(!flags)return root;
        if(!map_mode&&!single&&!pair){frame.writes.push_back({root+0xd4,*flags,*flags&~1u});m.Write(root+0xd4,*flags&~1u);}
        constexpr U32 Size=0x2400;
        if(sp<0x104000||(sp&7))return root;
        const float s=pair?pair->options.scale:single?single->pose.scale:map_mode?NotebookPrompt::pose.scale.load():scale.load(),
            is=pair?pair->options.icon_scale:single?1.f:map_mode?NotebookPrompt::icon_scale.load():icon_scale.load(),dx=pair?pair->options.x:single?single->pose.x:map_mode?0:x.load(),dy=pair?pair->options.y:single?single->pose.y:map_mode?0:y.load(),
            gap=pair?pair->options.spacing:single?0:map_mode?NotebookPrompt::pose.y.load()-NotebookPrompt::map_pose.y.load():spacing.load();
        if(!std::isfinite(s)||s<(single?.25f:.5f)||s>2||!std::isfinite(is)||is<.5f||is>2||!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(gap)||gap<0||gap>60)return root;
        const float fade=std::clamp(std::chrono::duration<float>(std::chrono::steady_clock::now()-opened).count()/.2f,0.f,1.f);
        const float alpha=((map_mode||single||pair)?1.f:fade)*native_alpha;if(alpha<=0)return root;
        const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return root;
        auto ig=m.Read(icon+0x28),tg=m.Read(text+0x28);if(!ig||!tg)return root;
        auto font=m.Read(*tg+0x28);if(!font||!Pointer(*font,0x14))return root;
        const std::array<std::string_view,2> labels=pair?pair->labels:single?std::array<std::string_view,2>{flat?single->label:single->reverse?"Items":"Map",""}:map_mode?
            std::array<std::string_view,2>{"Move cursor","Return"}:std::array<std::string_view,2>{"Song list","Back"};
        std::array<std::vector<NotebookFont::Glyph>,2> glyphs;
        float height=0,width=0;
        for(unsigned row=0;row<(single?1u:2u);++row) {
            float pen=0;
            for(char c:labels[row]) {auto g=NotebookFont::Lookup(m,*font,unsigned(c),pen);if(!g)return root;glyphs[row].push_back(*g);pen+=g->advance;height=std::bit_cast<float>(g->words[9]);}
            width=std::max(width,pen);
        }
        if(height<=0||!std::isfinite(height))return root;
        std::array<U32,Size/4> words{};
        const auto put=[&](U32 at,U32 v){words[at/4]=v;};
        const auto number=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
        const auto copy=[&](U32 to,U32 from,U32 n){for(U32 i=0;i<n;i+=4){auto v=m.Read(from+i);if(!v)return false;put(to+i,*v);}return true;};
        const auto node=[&](U32 at,U32 geo,float px,float py,float factor){
            put(at+0x14,0);put(at+0x18,0);put(at+0x20,0);put(at+0x24,0xffff);put(at+0x28,geo);put(at+0xd4,15);
            put(at+0xdc,words[(at+0xdc)/4]&~0xff00u);
            for(unsigned i=0;i<8;++i)number(at+0xe0+4*i,(i==0||i==5)?factor:0);
            number(at+0xec,px);number(at+0xfc,py);
            for(unsigned i=0;i<6;++i)number(at+0x110+4*i,(i==0||i==4)?1:0);
            for(unsigned i=0;i<3;++i)number(at+0x128+4*i,1);number(at+0x134,alpha);
        };
        if(!copy(0,root,0x140))return root;
        node(0,0,0,0,1);put(0x14,base+0x140);put(0x18,single?(flat?5:7):(map_mode||pair)?9:8);if(single)put(flat?0x150:0x158,root);else if(map_mode||pair)put(0x160,root);
        const float font_scale=12/height;
        const float right=(wide?400.f+(240.f*16/9-400)/2:400.f)-12+dx;
        const float icon_width=16*is,left=right-(single&&!flat?36.f:icon_width+4+width*font_scale)*s;
        for(unsigned row=0;row<(single?1u:2u);++row) {
            const U32 offset=0x180+row*0xa00,igoff=offset+0x140,txt=offset+0x1c0,tgoff=offset+0x300,records=offset+0x610;
            if(!copy(offset,icon,0x140)||!copy(igoff,*ig,0x7c)||!copy(txt,text,0x140)||!copy(tgoff,*tg,0x30c))return root;
            const U32 shadow=0x1600+row*0x500,shadow_geo=shadow+0x140;
            const U32 icon_shadow=0x2000+row*0x1c0,icon_shadow_geo=icon_shadow+0x140;
            // Native emission uses front-first ordering for these equal-depth
            // panes. Foregrounds must precede their shadow copies.
            put(0x140+row*16,base+offset);put(0x144+row*16,base+txt);
            put(0x148+row*16,base+icon_shadow);put(0x14c+row*16,base+shadow);
            const float cy=228+dy-(single&&!flat?18*s:row==0?gap*s:0);
            const float text_x=single&&!flat?left+(36-width*font_scale)*.5f*s:left+(icon_width+4)*s;
            const float text_y=single&&!flat?cy+14*s:cy-6*s;
            node(offset,base+igoff,left,cy-8*is*s,s*is);
            put(igoff+4,(words[(igoff+4)/4]&0xffff0000u)|6);put(igoff+0xc,0x00010101);put(igoff+0x10,texture);
            for(U32 at:{0x14u,0x18u,0x1cu})number(igoff+at,0);
            number(igoff+0x20,16);number(igoff+0x24,16);
            // Localized parts.ctxb: START208,0,16x16; B128,0,16x16.
            number(igoff+0x28,float(pair?pair->buttons[row]:single?(flat?single->button_u:single->reverse?176:192):row?128:map_mode?96:208)/256);number(igoff+0x2c,map_mode&&!single&&!row?16.f/128.f:0.f);
            number(igoff+0x30,16.f/256);number(igoff+0x34,16.f/128);put(igoff+0x38,0);
            for(unsigned i=0;i<16;++i)number(igoff+0x3c+4*i,1);
            node(txt,base+tgoff,text_x,text_y,s*font_scale);
            number(tgoff+0x34,0);number(tgoff+0x38,0);put(tgoff+0x118,words[(tgoff+0x118)/4]&~0xff00u);
            put(tgoff+0x2fc,base+records);for(U32 at:{0x300u,0x304u,0x308u})put(tgoff+at,glyphs[row].size());
            for(unsigned i=0;i<glyphs[row].size();++i)for(unsigned j=0;j<15;++j)put(records+i*60+j*4,glyphs[row][i].words[j]);
            // Draw the same native glyphs in black behind the white label.
            // One native pixel down/right; offset and opacity follow the group.
            for(U32 i=0;i<0x140;i+=4)put(shadow+i,words[(txt+i)/4]);
            for(U32 i=0;i<0x30c;i+=4)put(shadow_geo+i,words[(tgoff+i)/4]);
            node(shadow,base+shadow_geo,text_x+s,text_y+s,s*font_scale);
            for(U32 i=0;i<3;++i)number(shadow+0x128+4*i,0);
            number(shadow+0x134,alpha*.8f);
            for(U32 i=0;i<0x140;i+=4)put(icon_shadow+i,words[(offset+i)/4]);
            for(U32 i=0;i<0x7c;i+=4)put(icon_shadow_geo+i,words[(igoff+i)/4]);
            node(icon_shadow,base+icon_shadow_geo,left+s*is,cy-7*is*s,s*is);
            for(U32 i=0;i<3;++i)number(icon_shadow+0x128+4*i,0);
            number(icon_shadow+0x134,alpha*.8f);
        }
        if(single&&!flat) {
            // menu_common_parts00 (resource8): native right arrow432,104,16x24,
            // used by UpHintMenu and CursorEventCharSeal. Keep its aspect ratio.
            constexpr U32 arrow=0xb80,geo=arrow+0x140,shadow=0xd40,shadow_geo=shadow+0x140;
            for(U32 i=0;i<0x140;i+=4)put(arrow+i,words[(0x180+i)/4]);
            for(U32 i=0;i<0x7c;i+=4)put(geo+i,words[(0x2c0+i)/4]);
            node(arrow,base+geo,left+20*s,228+dy-30*s,s);
            put(geo+0x10,single->arrow_texture);number(geo+0x24,24);
            number(geo+0x28,432.f/512);number(geo+0x2c,(single->reverse?80.f:104.f)/128);
            number(geo+0x30,16.f/512);number(geo+0x34,24.f/128);
            for(U32 i=0;i<0x140;i+=4)put(shadow+i,words[(arrow+i)/4]);
            for(U32 i=0;i<0x7c;i+=4)put(shadow_geo+i,words[(geo+i)/4]);
            node(shadow,base+shadow_geo,left+21*s,228+dy-29*s,s);
            for(U32 i=0;i<3;++i)number(shadow+0x128+4*i,0);
            number(shadow+0x134,alpha*.8f);
            put(0x150,base+arrow);put(0x154,base+shadow);
            // Local adjustments are centered on each element, then carried by
            // the global prompt pose. Shadows use the same transform.
            const std::array<std::array<U32,2>,3> nodes{{{0x180,0x2000},{arrow,shadow},{0x340,0x1600}}};
            const std::array<float,3> centers_x{left+8*s,left+28*s,left+18*s};
            const std::array<float,3> centers_y{228+dy-18*s,228+dy-18*s,228+dy+2*s};
            for(unsigned i=0;i<3;++i) {
                const auto& p=single->elements[i];
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||p.scale<.25f||p.scale>4)return root;
                for(U32 at:nodes[i]) {
                    const auto get=[&](U32 field){return std::bit_cast<float>(words[(at+field)/4]);};
                    number(at+0xec,centers_x[i]+(get(0xec)-centers_x[i])*p.scale+p.x*s);
                    number(at+0xfc,centers_y[i]+(get(0xfc)-centers_y[i])*p.scale+p.y*s);
                    number(at+0xe0,get(0xe0)*p.scale);number(at+0xf4,get(0xf4)*p.scale);
                    if(single->reverse) {
                        const float w=(i==2?width*font_scale:16.f)*s*p.scale;
                        // Mirror the arrangement, never the artwork or text.
                        const float shadow_shift=at==nodes[i][1]?2*s*p.scale:0;
                        number(at+0xec,400-get(0xec)-w+shadow_shift);
                    }
                }
            }
        }
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+i*4,*m.Read(base+i*4),words[i]});
        for(auto w:frame.writes)m.Write(w.address,w.after);frame.sp=sp;status=3;return base;
    }
    template<class M> U32 End(M& m){return scratch.End(m);}
};
}
