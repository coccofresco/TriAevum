// Ported from mm3d-dekui src/mm3d/notebook_prompt.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>
#include "native_inventory.h"
#include "notebook_font.h"
#include "prompt_text.h"

namespace Mm3dDekui::NotebookPrompt {
using namespace NativeInventory;
inline std::atomic<bool> enabled{true};
inline std::atomic<float> icon_scale{1.f};
inline GearMenu::AtomicPose pose, map_pose;
inline std::atomic<bool> map_enabled{true},schedule_enabled{true};
inline std::atomic<float> schedule_y{-40.f};
inline std::atomic<unsigned> status{0};
// Main resource registry, Restoration + HD HUD v110. Its address is unchanged
// from retail; validate the init literal as well as both loaded texture IDs.
constexpr U32 RegistryLiteral=0x0013acc0, Registry=0x7cdc9c; // ref 0x13ace8
constexpr U32 ScratchSize=0x3000; // 4f2692ed: room for translated labels
constexpr std::string_view Label="Schedule details", MapLabel="View map", ScheduleLabel="View schedule";
constexpr std::string_view AlarmLabel="Set alarm", EventsLabel="View events", ScheduleDetailsLabel="Event details";
// Retail accessors 1C10C8/1C6260/18EFD0 relocate independently in the
// installed Restoration build. Guard their complete bodies and literals.
constexpr std::array<std::pair<U32,U32>,12> AvailabilityCode{{
    {0x001c10c8,0xe59f000c},{0x001c10cc,0xe0800081}, // ref 0x1c12d0, 0x1c12d4
    {0x001c10d0,0xe2800b06},{0x001c10d4,0xe28000ac}, // ref 0x1c12d8, 0x1c12dc
    {0x001c10d8,0xe12fff1e},{0x001c10dc,0x7751d8}, // ref 0x1c12e0, 0x1c12e4
    {0x001c6260,0xe0800181},{0x001c6264,0xe2800014},{0x001c6268,0xe12fff1e}, // ref 0x1c6468, 0x1c646c, 0x1c6470
    {0x0018efd0,0xe59f0000},{0x0018efd4,0xe12fff1e},{0x0018efd8,0x72f060} // ref 0x18f12c, 0x18f130, 0x18f134
}};
template<class M> bool AvailabilityCodeMatches(M& m) {
    for(auto [at,word]:AvailabilityCode)if(m.Read(at)!=std::optional<U32>(word))return false;
    return true;
}
template<class M> std::optional<bool> DetailsAvailable(M& m,U32 owner,bool* transitioning=nullptr) {
    if(transitioning)*transitioning=false;
    if(!Pointer(owner,0x258)||!AvailabilityCodeMatches(m))return {};
    const auto byte=[&](U32 at)->std::optional<U32> {
        if(!Pointer(at&~3u))return {};
        auto word=m.Read(at&~3u);
        return word?std::optional<U32>((*word>>((at&3)*8))&255):std::nullopt;
    };
    auto table=m.Read(owner+0x6c),selection=m.Read(owner+0x254);
    if(!table||!selection||!Pointer(*table,128*0x12))return {};
    const auto row=(*selection>>8)&255,column=(*selection>>16)&255;
    // Native indices are signed bytes; reject negative/transitional selections.
    if(row>=128||column>=8)return {};
    auto count=byte(*table+row*0x12);
    if(!count||*count>8)return {};
    // A row change can precede resetting the previous row's sub-event index.
    // Preserve the confirmed opacity until that sub-event index is valid again.
    if(column>=*count){if(transitioning)*transitioning=*count!=0;return false;}
    auto event=byte(*table+row*0x12+column*2+2);
    if(!event)return {};
    auto flags=byte(0x7751d8+0x18ac+*event*2);
    auto details=byte(0x72f060+0x18+*event*8);
    if(!flags||!details)return {};
    return *details!=0&&(*flags&0x80)==0;
}
template<class M> std::optional<bool> ScheduleDetailsAvailable(M& m,U32 owner,U32 layout) {
    if(!Pointer(owner,0x5a0)||m.Read(owner+0x44)!=std::optional<U32>(layout)||
       !LayoutNamed(m,layout,"MenuSchedule"))return {};
    auto selection=m.Read(owner+0x10),event=m.Read(owner+0x59c);
    if(!selection||!event)return {};
    // Native schedule navigation stores the selected timeline event at +59C;
    // empty days clear it. Column zero selects the character biography.
    const auto column=*selection&255;
    if(column==0||column>5||!*event)return false;
    return Pointer(*event)&&m.Read(*event).has_value();
}
struct Hook {
    struct Frame { U32 sp=0; std::vector<NativeInventory::Hook::Write> writes; };
    std::vector<Frame> frames;
    U32 details_owner=0,details_layout=0,details_table=0;
    bool last_details_available=false;
    template<class M> std::optional<bool> EventDetailsAvailable(M& m,U32 owner,U32 layout) {
        const U32 table=m.Read(owner+0x6c).value_or(0);
        if(owner!=details_owner||layout!=details_layout||table!=details_table) {
            details_owner=owner;details_layout=layout;details_table=table;
            last_details_available=false;
        }
        bool transitioning=false;
        auto available=DetailsAvailable(m,owner,&transitioning);
        if(transitioning)return last_details_available;
        if(available)last_details_available=*available;
        return available;
    }
    template<class M> U32 Child(M& m,U32 node,const char* name) {
        auto list=m.Read(node+0x14),count=m.Read(node+0x18);
        if(!list||!count||*count>32||!Pointer(*list,*count*4))return 0;
        for(unsigned i=0;i<*count;++i){auto child=m.Read(*list+4*i);if(child&&Named(m,*child,name))return *child;}
        return 0;
    }
    template<class M> std::optional<float> ScrollbarOpacity(M& m,U32 owner,bool schedule) {
        auto layout=m.Read(owner+(schedule?0x5c0:0x78));
        if(!layout||!LayoutNamed(m,*layout,"ScrollBar"))return {};
        // ScrollBar's in/out animations fade its `all` group independently
        // of the selected event and the description window.
        const auto all=Child(m,*layout+8,"all");
        if(!all)return {};
        auto bits=m.Read(all+0x134);if(!bits)return {};
        const float alpha=std::bit_cast<float>(*bits);
        if(std::isfinite(alpha)&&alpha>=0&&alpha<=1)return alpha;
        return {};
    }
    template<class M> U32 Build(M& m,U32 layout,U32 owner,U32 sp,float alpha) {
        frames.emplace_back();status=1;
        const bool schedule=LayoutNamed(m,layout,"MenuSchedule");
        // Labels follow the active message archive (PromptText, S1); glyph budgets as 4f2692ed.
        const auto detail_label=PromptText::Codes(PromptText::Get(m,schedule?ScheduleDetailsLabel:Label));
        const auto map_label=PromptText::Codes(PromptText::Get(m,schedule?AlarmLabel:MapLabel));
        const auto page_label=PromptText::Codes(PromptText::Get(m,schedule?EventsLabel:ScheduleLabel));
        const auto back_label=PromptText::Codes(PromptText::Get(m,schedule?"Back":"Return"));
        if(detail_label.empty()||detail_label.size()>16||map_label.empty()||map_label.size()>9||
           page_label.empty()||page_label.size()>18||back_label.empty()||back_label.size()>8)return layout+8;
        const bool show_a=enabled.load(),show_map=map_enabled.load(),show_schedule=schedule_enabled.load();
        if((!show_a&&!show_map&&!show_schedule)||alpha<=0)return layout+8;
        const float x=pose.x.load(),y=pose.y.load(),scale=pose.scale.load();
        const float icons=icon_scale.load(),sy=schedule_y.load();
        if(show_schedule&&!std::isfinite(sy))return layout+8;
        if(!std::isfinite(icons)||icons<.5f||icons>2.f)return layout+8;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(scale)||scale<.5f||scale>2.f||
           !std::isfinite(alpha)||alpha>1||sp<0x104000||(sp&7))return layout+8;
        const float mx=map_pose.x.load(),my=map_pose.y.load(),ms=map_pose.scale.load();
        if(show_map&&(!std::isfinite(mx)||!std::isfinite(my)||!std::isfinite(ms)||ms<.5f||ms>2.f))return layout+8;
        if(m.Read(RegistryLiteral)!=std::optional<U32>(Registry))return layout+8;
        // Pictogram has three nodes and a single textured pane. Use the bound
        // A prompt as a read-only template, even when its character is hidden.
        auto prompt=m.Read(owner+0x2a8),anchor=m.Read(owner+0x290);
        if(!prompt||!anchor||!LayoutNamed(m,*prompt,"Pictogram")||
           !Named(m,*anchor,"disp_a_icon_n")||m.Read(*prompt+0x180)!=anchor)return layout+8;
        U32 all=Child(m,*prompt+8,"all"),pict=all?Child(m,all,"pict"):0;
        auto geometry=pict?m.Read(pict+0x28):std::optional<U32>{};
        if(!geometry||!Pointer(*geometry,0x7c)||!Named(m,pict,"pict"))return layout+8;
        auto type=m.Read(*geometry+4),texture=m.Read(*geometry+0x10);
        if(!type||(*type&65535)!=6||!texture)return layout+8;
        // Reuse the font bound to the actual event-description text widget.
        const U32 panel_all=Child(m,layout+8,"all");
        const U32 detail=panel_all?Child(m,panel_all,schedule?"sche_quest_detail":"quest_list_detail"):0;
        NativeInventory::Hook helper;std::vector<NativeInventory::Hook::Write> unused;
        std::vector<U32> nodes;
        if(!detail||!helper.Transform(m,detail,unused,nodes,{1,0,0,1,0,0}))return layout+8;
        U32 text_node=0,text_geometry=0;
        std::vector<NotebookFont::Glyph> glyphs(detail_label.size());
        std::vector<NotebookFont::Glyph> map_glyphs(map_label.size());
        std::vector<NotebookFont::Glyph> schedule_glyphs(page_label.size());
        std::vector<NotebookFont::Glyph> back_glyphs(back_label.size());
        float text_height=0,map_width=0;
        for(auto candidate:nodes) {
            auto geo=m.Read(candidate+0x28);if(!geo||!Pointer(*geo,0x30c))continue;
            auto kind=m.Read(*geo+4),font=m.Read(*geo+0x28);
            if(!kind||(*kind&65535)!=3||!font||!Pointer(*font,0x14))continue;
            float pen=0;bool valid=true;
            for(unsigned i=0;i<detail_label.size();++i) {
                auto glyph=NotebookFont::Lookup(m,*font,unsigned(detail_label[i]),pen);
                if(!glyph){valid=false;break;}
                glyphs[i]=*glyph;pen+=glyph->advance;
            }
            if(valid) {
                const auto lookup=[&](std::u32string_view label,auto& out,float& width) {
                    width=0;
                    for(unsigned i=0;i<label.size();++i) {
                        auto glyph=NotebookFont::Lookup(m,*font,unsigned(label[i]),width);
                        if(!glyph)return false;out[i]=*glyph;width+=glyph->advance;
                    }
                    return true;
                };
                valid=(!show_map||lookup(map_label,map_glyphs,map_width))&&
                    (!show_schedule||lookup(page_label,schedule_glyphs,map_width))&&lookup(back_label,back_glyphs,map_width);
            }
            if(valid){text_node=candidate;text_geometry=*geo;
                text_height=std::bit_cast<float>(glyphs[0].words[9]);break;}
        }
        if(!text_node||text_height<=0)return layout+8;
        status=2;
        // A scoped guest stack reservation is consumed only by the synchronous
        // native draw. No heap objects, texture resources or queue entries are
        // retained. Leave another scratch-sized region of mapped stack for native recursion.
        const U32 scratch=sp-ScratchSize;
        for(U32 at=scratch-ScratchSize;at<sp;at+=4)if(!m.Read(at))return layout+8;
        std::array<U32,ScratchSize/4> words{};
        auto copy=[&](U32 dest,U32 source,U32 size){for(U32 i=0;i<size;i+=4){auto v=m.Read(source+i);if(!v)return false;words[(dest+i)/4]=*v;}return true;};
        auto put=[&](U32 offset,U32 value){words[offset/4]=value;};
        auto number=[&](U32 offset,float value){put(offset,std::bit_cast<U32>(value));};
        auto node=[&](U32 offset,U32 geo,float tx,float ty){
            put(offset+0x14,0);put(offset+0x18,0);put(offset+0x20,0);put(offset+0x24,0xffff);put(offset+0x28,geo);
            put(offset+0xd4,15);
            // DD is the traversal group marker; DF is the computed native
            // drawing depth. Preserve DF and the world Z row from the source.
            put(offset+0xdc,words[(offset+0xdc)/4]&~0xff00u);
            for(U32 i=0;i<8;++i)number(offset+0xe0+i*4,(i==0||i==5)?scale:0.f);
            number(offset+0xec,tx);number(offset+0xfc,ty);
            for(U32 i=0;i<6;++i)number(offset+0x110+i*4,(i==0||i==4)?1.f:0.f);
            for(U32 i=0;i<4;++i)number(offset+0x128+i*4,alpha);
        };
        constexpr U32 Children=0x140,Icon=0x180,IconGeometry=0x2c0,Text=0x340,TextGeometry=0x480,Glyphs=0x1d20;
        static_assert(Glyphs+16*60<=0x2100); // next pane is BackText
        if(!copy(0,layout+8,0x140)||!copy(Icon,pict,0x140)||!copy(IconGeometry,*geometry,0x7c)||
           !copy(Text,text_node,0x140)||!copy(TextGeometry,text_geometry,0x30c))return layout+8;
        node(0,0,0,0);put(0x14,scratch+Children);
        unsigned child_count=0;
        const auto child=[&](U32 offset){put(Children+4*child_count++,scratch+offset);};
        if(show_a){child(Icon);child(Text);}
        // Render at 12 native pixels high, using the textbox font's own advances.
        const float font_scale=12.f/text_height;
        // 4f2692ed: a translated label shrinks to stay on screen; English keeps 12 pixels.
        const auto fit=[&](const auto& gs,float left,float group_scale) {
            float width=0;for(const auto& g:gs)width+=g.advance;
            if(PromptText::Language(m)==PromptText::PromptLanguage::English)return font_scale;
            const float available=std::max(1.f,400.f-left-(16.f*icons+4.f)*group_scale-4.f);
            return std::min(font_scale,available/std::max(1.f,width*group_scale));
        };
        const float left=x;
        node(Icon,scratch+IconGeometry,left,228.f+(y-20.f-8.f*icons)*scale);
        number(Icon+0xe0,scale*icons);number(Icon+0xf4,scale*icons);
        put(IconGeometry+0xc,0x00010101);
        number(IconGeometry+0x14,0);number(IconGeometry+0x18,0);number(IconGeometry+0x1c,0);
        number(IconGeometry+0x20,16);number(IconGeometry+0x24,16);
        number(IconGeometry+0x28,112.f/256);number(IconGeometry+0x2c,0);
        number(IconGeometry+0x30,1.f/16);number(IconGeometry+0x34,1.f/8);put(IconGeometry+0x38,0);
        for(U32 c=0;c<16;++c)number(IconGeometry+0x3c+c*4,1.f);
        node(Text,scratch+TextGeometry,left+(16.f*icons+4.f)*scale,228.f+(y-26.f)*scale);
        number(Text+0xe0,scale*fit(glyphs,x,scale));number(Text+0xf4,scale*fit(glyphs,x,scale));
        // Native text renderer consumes these glyph records directly. Uniform
        // white (color index -1), existing font texture, no message ID changes.
        number(TextGeometry+0x34,0);number(TextGeometry+0x38,0);
        put(TextGeometry+0x118,words[(TextGeometry+0x118)/4]&~0xff00u);
        put(TextGeometry+0x2fc,scratch+Glyphs);
        for(U32 off:{0x300u,0x304u,0x308u})put(TextGeometry+off,detail_label.size());
        for(unsigned i=0;i<detail_label.size();++i)for(unsigned j=0;j<15;++j)put(Glyphs+i*60+j*4,glyphs[i].words[j]);
        if(show_map) {
            constexpr U32 MapText=0xb20,MapGeo=0xc60,MapGlyphs=0xf80;
            constexpr U32 MapIcon=0x11a0,MapIconGeo=0x12e0;
            static_assert(MapGlyphs+9*60<=MapIcon);
            const float left=mx,cy=228.f+(my-20.f)*ms;
            const auto text=[&](U32 at,U32 geo,U32 records,const auto& gs,float tx,float ty,float fs) {
                if(!copy(at,text_node,0x140)||!copy(geo,text_geometry,0x30c))return false;
                node(at,scratch+geo,tx,ty);
                number(at+0xe0,ms*fs);number(at+0xf4,ms*fs);
                number(geo+0x34,0);number(geo+0x38,0);
                put(geo+0x118,words[(geo+0x118)/4]&~0xff00u);
                put(geo+0x2fc,scratch+records);
                for(U32 off:{0x300u,0x304u,0x308u})put(geo+off,gs.size());
                for(unsigned i=0;i<gs.size();++i)for(unsigned j=0;j<15;++j)put(records+i*60+j*4,gs[i].words[j]);
                child(at);return true;
            };
            if(!text(MapText,MapGeo,MapGlyphs,map_glyphs,left+(16.f*icons+4.f)*ms,cy-6.f*ms,fit(map_glyphs,left,ms)))return layout+8;
            // Clone the already prepared A geometry, changing only atlas cell:
            // native parts.ctxb X is x144,y0,16x16 (A is x112).
            for(U32 i=0;i<0x140;i+=4)put(MapIcon+i,words[(Icon+i)/4]);
            for(U32 i=0;i<0x7c;i+=4)put(MapIconGeo+i,words[(IconGeometry+i)/4]);
            node(MapIcon,scratch+MapIconGeo,left,cy-8.f*icons*ms);
            number(MapIcon+0xe0,ms*icons);number(MapIcon+0xf4,ms*icons);
            number(MapIconGeo+0x28,144.f/256.f);
            child(MapIcon);

        }
        if(show_schedule) {
            constexpr U32 MapText=0x1380,MapGeo=0x14c0,MapGlyphs=0x2800;
            constexpr U32 MapIcon=0x1b40,MapIconGeo=0x1c80;
            static_assert(MapGlyphs+18*60<=ScratchSize);
            const float ms=scale;
            const float left=x,cy=228.f+(sy-20.f)*ms;
            const auto text=[&](U32 at,U32 geo,U32 records,const auto& gs,float tx,float ty,float fs) {
                if(!copy(at,text_node,0x140)||!copy(geo,text_geometry,0x30c))return false;
                node(at,scratch+geo,tx,ty);
                number(at+0xe0,ms*fs);number(at+0xf4,ms*fs);
                number(geo+0x34,0);number(geo+0x38,0);
                put(geo+0x118,words[(geo+0x118)/4]&~0xff00u);
                put(geo+0x2fc,scratch+records);
                for(U32 off:{0x300u,0x304u,0x308u})put(geo+off,gs.size());
                for(unsigned i=0;i<gs.size();++i)for(unsigned j=0;j<15;++j)put(records+i*60+j*4,gs[i].words[j]);
                child(at);return true;
            };
            if(!text(MapText,MapGeo,MapGlyphs,schedule_glyphs,left+(16.f*icons+4.f)*ms,cy-6.f*ms,fit(schedule_glyphs,left,ms)))return layout+8;
            // Clone the already prepared A geometry, changing only atlas cell:
            // native parts.ctxb Y is x160,y0,16x16 (A is x112).
            for(U32 i=0;i<0x140;i+=4)put(MapIcon+i,words[(Icon+i)/4]);
            for(U32 i=0;i<0x7c;i+=4)put(MapIconGeo+i,words[(IconGeometry+i)/4]);
            node(MapIcon,scratch+MapIconGeo,left,cy-8.f*icons*ms);
            number(MapIcon+0xe0,ms*icons);number(MapIcon+0xf4,ms*icons);
            number(MapIconGeo+0x28,160.f/256.f); // Y: native View Schedule shortcut
            child(MapIcon);

        }
        // B occupies the former bottom row; the other rows move up one step.
        constexpr U32 BackText=0x2100,BackIcon=0x2240,BackIconGeo=0x2380,BackGeo=0x800,BackGlyphs=0x2400;
        static_assert(BackGlyphs+8*60<=0x2800); // schedule glyphs follow
        if(!copy(BackText,text_node,0x140)||!copy(BackGeo,text_geometry,0x30c))return layout+8;
        const float back_center=228.f+(y-20.f)*scale+(y*scale-my*ms);
        node(BackText,scratch+BackGeo,x+(16.f*icons+4.f)*scale,back_center-6.f*scale);
        number(BackText+0xe0,scale*fit(back_glyphs,x,scale));number(BackText+0xf4,scale*fit(back_glyphs,x,scale));
        number(BackGeo+0x34,0);number(BackGeo+0x38,0);
        put(BackGeo+0x118,words[(BackGeo+0x118)/4]&~0xff00u);
        put(BackGeo+0x2fc,scratch+BackGlyphs);
        for(U32 off:{0x300u,0x304u,0x308u})put(BackGeo+off,back_glyphs.size());
        for(unsigned i=0;i<back_glyphs.size();++i)for(unsigned j=0;j<15;++j)put(BackGlyphs+i*60+j*4,back_glyphs[i].words[j]);
        for(U32 i=0;i<0x140;i+=4)put(BackIcon+i,words[(Icon+i)/4]);
        for(U32 i=0;i<0x7c;i+=4)put(BackIconGeo+i,words[(IconGeometry+i)/4]);
        node(BackIcon,scratch+BackIconGeo,x,back_center-8.f*icons*scale);
        number(BackIcon+0xe0,scale*icons);number(BackIcon+0xf4,scale*icons);
        number(BackIconGeo+0x28,128.f/256.f);
        child(BackIcon);child(BackText);
        // Apply after cloning X/Y so only A and its label receive this factor.
        if(show_a&&(schedule?ScheduleDetailsAvailable(m,owner,layout):EventDetailsAvailable(m,owner,layout))==std::optional<bool>(false))
            for(U32 at:{Icon,Text})for(U32 i=0;i<4;++i)number(at+0x128+i*4,alpha*.4f);
        put(Children+4*child_count++,layout+8);put(0x18,child_count);
        auto& frame=frames.back();frame.sp=sp;
        frame.writes.reserve(words.size());
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({scratch+i*4,*m.Read(scratch+i*4),words[i]});
        for(auto w:frame.writes)m.Write(w.address,w.after);
        status=3;return scratch;
    }
    template<class M> U32 End(M& m) {
        if(frames.empty())return 0;
        auto& frame=frames.back();const U32 sp=frame.sp;
        for(auto i=frame.writes.rbegin();i!=frame.writes.rend();++i)m.Write(i->address,i->before);
        frames.pop_back();return sp;
    }
};
}
