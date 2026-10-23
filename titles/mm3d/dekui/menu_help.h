// Ported from mm3d-dekui src/mm3d/menu_help.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "items_map_prompt.h"
#include "hud_adjustments.h"
#include "native_item_present.h"
namespace Mm3dDekui::MenuHelp {
using namespace NativeInventory;
inline constexpr std::array<const char*,5> Labels{"Change tab","Choose","Return","Equip to I","Equip to II"};
struct Options {
    bool enabled=true;
    ButtonPose pose{280,148,.8f};
    bool horizontal=false;
    float spacing=6,button_gap=4,button_scale=1,text_scale=1,opacity=1;
};
inline std::atomic<bool> enabled{true};
inline AtomicButtonPose pose{280,148,.8f};
inline std::atomic<bool> horizontal{false};
inline std::atomic<float> spacing{6},button_gap{4},button_scale{1},text_scale{1};
inline void Apply(const Options& o) {
    enabled=o.enabled;pose.x=o.pose.x;pose.y=o.pose.y;pose.scale=o.pose.scale;
    ItemsMapPrompt::opacity=o.opacity;
    horizontal=o.horizontal;spacing=o.spacing;button_gap=o.button_gap;button_scale=o.button_scale;text_scale=o.text_scale;
}
struct Hook {
    ItemsMapPrompt::Hook source;
    NotebookPrompt::Hook scratch;
    template<class M> U32 Root(M& m,NativeInventory::Hook& inventory,U32 layout,U32 sp,U32 content,NativeItemPresent::Hook* present=nullptr) {
        scratch.frames.emplace_back();auto& frame=scratch.frames.back();
        const bool gear=LayoutNamed(m,layout,"MenuCollect");
        if(present) {
            if(!NativeItemPresent::enabled||present->pass!=3||layout!=present->panel||
               !LayoutNamed(m,layout,"MenuItemPresen")||
               std::find(present->routed.begin(),present->routed.end(),layout)==present->routed.end())return content;
        } else {
        if(HudAdjustments::hide_menu_prompts||!enabled||!(gear?GearMenu::enabled.load():NativeInventory::enabled.load())||
           inventory.pass!=3||layout!=inventory.panel||
           !(gear||LayoutNamed(m,layout,"MenuItem")||LayoutNamed(m,layout,"MenuMask"))||
           std::find(inventory.routed.begin(),inventory.routed.end(),layout)==inventory.routed.end())return content;
        }
        auto& prompt=source.prompt;prompt.icon=prompt.text=0;prompt.texture=prompt.Parts(m);
        const std::string_view alphabet=present?"ChooseCancel":"Change tabChooseReturnEquip to IIZR +/";
        source.Templates(m,inventory,layout,alphabet);
        if(!prompt.text&&source.description_panel==layout&&LayoutNamed(m,source.description,"MessageWindow"))
            source.Templates(m,inventory,source.description,alphabet);
        if(!prompt.icon||!prompt.text||!prompt.texture)return content;
        auto all=scratch.Child(m,layout+8,"all");auto alpha=m.Read((all?all:layout+8)+0x134);
        auto ig=m.Read(prompt.icon+0x28),tg=m.Read(prompt.text+0x28);
        if(!alpha||!ig||!tg)return content;auto font=m.Read(*tg+0x28);if(!font)return content;
        const auto& group=present?NativeItemPresent::help_pose:pose;
        const float a=std::bit_cast<float>(*alpha)*ItemsMapPrompt::Opacity(),scale=group.scale.load(),
            x=group.x.load()+(present?present->transition_x:0),y=group.y.load();
        if(!std::isfinite(a)||a<=0||a>1||!std::isfinite(scale)||scale<.1f||scale>2||!std::isfinite(x)||!std::isfinite(y))return content;
        const bool across=!present&&horizontal.load();
        const float gap=present?NativeItemPresent::help_spacing.load():spacing.load(),
            inner=present?4:button_gap.load(),bs=present?1:button_scale.load(),ts=present?1:text_scale.load();
        if(!std::isfinite(gap)||gap<0||gap>100||!std::isfinite(inner)||inner<0||inner>40||
           !std::isfinite(bs)||bs<.25f||bs>3||!std::isfinite(ts)||ts<.25f||ts>3)return content;
        const float row_height=std::max(16*bs,12*ts);
        constexpr U32 Size=0x8000;
        if(sp<0x110000||(sp&7))return content;const U32 base=sp-Size;
        for(U32 at=base;at<sp;at+=4)if(!m.Read(at))return content;
        std::vector<U32> words(Size/4);
        const auto put=[&](U32 at,U32 v){words[at/4]=v;};
        const auto f=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
        const auto copy=[&](U32 at,U32 from,U32 size){for(U32 j=0;j<size;j+=4){auto v=m.Read(from+j);if(!v)return false;put(at+j,*v);}return true;};
        if(!copy(0,content,0x140))return content;
        put(0x14,base+0x140);put(0x20,0);put(0x24,0xffff);put(0x28,0);
        unsigned children=0;U32 next=0x200;
        float row_alpha=a;
        const auto node=[&](U32 at,U32 geo,float px,float py,float s,bool shadow){
            put(at+0x14,0);put(at+0x18,0);put(at+0x20,0);put(at+0x24,0xffff);put(at+0x28,base+geo);put(at+0xd4,15);
            put(at+0xdc,words[(at+0xdc)/4]&~0xff00u);
            for(unsigned j=0;j<8;++j)f(at+0xe0+j*4,(j==0||j==5)?s:0);
            f(at+0xec,px);f(at+0xfc,py);
            for(unsigned j=0;j<6;++j)f(at+0x110+j*4,(j==0||j==4)?1:0);
            for(unsigned j=0;j<3;++j)f(at+0x128+j*4,shadow?0:1);f(at+0x134,row_alpha*(shadow?.8f:1.f));
            put(0x140+4*children++,base+at);
        };
        float cursor_x=x,cursor_y=y;
        for(unsigned row=0;row<(present?2u:5u);++row) {
            // Gear has no Choose/equip action; keep navigation and Return full.
            row_alpha=a*((gear&&(row==1||row>=3))?.5f:1.f);
            const float s=scale,py=cursor_y;float px=cursor_x;
            const auto icon=[&](unsigned u)->bool {
                const U32 at=next;next+=0x380;if(next>Size)return false;
                for(unsigned shadow=0;shadow<2;++shadow){const U32 n=at+shadow*0x1c0,g=n+0x140;
                    if(!copy(n,prompt.icon,0x140)||!copy(g,*ig,0x7c))return false;
                    node(n,g,px+shadow*s*bs,py+(row_height-16*bs)*s/2+shadow*s*bs,s*bs,shadow);
                    put(g+4,(words[(g+4)/4]&0xffff0000u)|6);put(g+0xc,0x10101);put(g+0x10,prompt.texture);
                    for(U32 off:{0x14u,0x18u,0x1cu})f(g+off,0);
                    f(g+0x20,16);f(g+0x24,16);f(g+0x28,float(u)/256);f(g+0x2c,0);
                    f(g+0x30,16.f/256);f(g+0x34,16.f/128);put(g+0x38,0);
                    for(unsigned j=0;j<16;++j)f(g+0x3c+j*4,1);
                }px+=(16*bs+inner)*s;return true;
            };
            const auto text=[&](std::string_view label)->bool {
                std::vector<NotebookFont::Glyph> glyphs;float pen=0,height=0;
                for(char c:label){auto g=NotebookFont::Lookup(m,*font,c,pen);if(!g)return false;glyphs.push_back(*g);pen+=g->advance;height=std::bit_cast<float>(g->words[9]);}
                if(height<=0||!std::isfinite(height)||glyphs.size()>16)return false;
                const float fs=12*ts/height;const U32 at=next;next+=0xb00;if(next>Size)return false;
                const U32 records=at+0x900;
                // Glyph records follow the two0x450-byte pane+geometry copies.
                // Reserve a larger tail for labels up to16 glyphs.
                next=std::max(next,records+U32(glyphs.size())*60);next=(next+7)&~7u;if(next>Size)return false;
                for(unsigned j=0;j<glyphs.size();++j)for(unsigned k=0;k<15;++k)put(records+j*60+k*4,glyphs[j].words[k]);
                for(unsigned shadow=0;shadow<2;++shadow){const U32 n=at+shadow*0x450,g=n+0x140;
                    if(!copy(n,prompt.text,0x140)||!copy(g,*tg,0x30c))return false;
                    node(n,g,px+shadow*s*ts,py+(row_height-12*ts)*s/2+shadow*s*ts,s*fs,shadow);
                    f(g+0x34,0);f(g+0x38,0);put(g+0x118,words[(g+0x118)/4]&~0xff00u);
                    put(g+0x2fc,base+records);for(U32 off:{0x300u,0x304u,0x308u})put(g+off,glyphs.size());
                }px+=(pen*fs+inner)*s;return true;
            };
            if(present){if(!icon(row==0?112:128))return content;}
            else if(row==0){if(!icon(176)||!text("/")||!icon(192))return content;}
            else if(row<3){if(!icon(row==1?112:128))return content;}
            else {if(!text("ZR +")||!icon(row==3?144:160))return content;}
            if(!text(present?(row==0?"Choose":"Cancel"):Labels[row]))return content;
            if(across)cursor_x=px+(gap-inner)*s;
            else cursor_y+=(row_height+gap)*s;
        }
        put(0x140+4*children++,content);put(0x18,children);
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
        for(auto w:frame.writes)m.Write(w.address,w.after);frame.sp=sp;return base;
    }
};
}
