// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration source/rst/main.cpp, message.cpp and game/ui.cpp, message.cpp.
namespace Mm3dRestoration {
bool Engine::Name(u32 at,std::string_view name) {
    if(!at)return false;
    for(unsigned i=0;i<name.size();++i)if(g.Read8(at+i)!=u8(name[i]))return false;
    return g.Read8(at+u32(name.size()))==0;
}
u32 Engine::Widget(u32 root,std::string_view name,unsigned depth) {
    if(!root||depth>64)return 0;
    if(Name(g.Read32(root+Layout::ui_Widget::name),name))return root;
    const u32 list=g.Read32(root+Layout::ui_Widget::widgets__data),count=g.Read32(root+Layout::ui_Widget::widgets__size);
    if(count>1024)return 0;
    for(u32 i=0;i<count;++i)if(auto w=Widget(g.Read32(list+4*i),name,depth+1))return w;
    return 0;
}
void Engine::Visible(u32 widget,bool value) {
    if(!widget)return;
    const u32 pos=widget+Layout::ui_Widget::pos;
    if(bool(g.Read32(pos+Layout::ui_WidgetPos::flags)&1)==value)return;
    Bits(pos+Layout::ui_WidgetPos::flags,1,value);
    Bits(pos+Layout::ui_WidgetPos::flags,0x20000,true);
    Bits(pos+Layout::ui_WidgetPos::active_flags,0x20000,value);
}
void Engine::Opacity(u32 widget,float value) {
    if(!widget)return;
    const u32 pos=widget+Layout::ui_Widget::pos;
    if(g.Float(pos+Layout::ui_WidgetPos::color+12)==value)return;
    g.Float(pos+Layout::ui_WidgetPos::color+12,value);
    Bits(pos+Layout::ui_WidgetPos::flags,0x4000000,true);
    Bits(pos+Layout::ui_WidgetPos::active_flags,0x4000000,value==1);
}
void Engine::AddOpacity(u32 widget,float delta,float minimum,float maximum) {
    if(widget)Opacity(widget,std::clamp(g.Float(widget+Layout::ui_Widget::pos+Layout::ui_WidgetPos::color+12)+delta,minimum,maximum));
}
Routine Engine::Alloc(u32 size) {
    // Same allocator as game::Allocator; objects have process/session lifetime,
    // matching the donor's static Hud2 and extension StateInfo storage.
    co_return (co_await Call(0x1dfb88,{{0x72f7f0,size,0}})).R0;
}
Routine Engine::String(std::string_view text) {
    if(auto it=s.strings.find(text);it!=s.strings.end())co_return it->second;
    const u32 at=co_await Alloc(u32(text.size()+1));if(!at)co_return 0;
    for(u32 i=0;i<text.size();++i)g.Write8(at+i,u8(text[i]));g.Write8(at+u32(text.size()),0);
    s.strings.emplace(text,at);co_return at;
}
Routine Engine::MakeLayout(std::string_view name) {
    const auto str=co_await String(name);if(!str)co_return 0;
    const u32 id=(co_await Call(0x16a014,{{Project,str}})).R0;
    // Not in the mounted RomFS (Restoration's own layouts are user-supplied):
    // no layout, as the native index would read past its table.
    if(id==kNoResource)co_return 0;
    // util::GetInstance: initialize the existing native singleton lazily.
    constexpr u32 manager=0x7cdc9c;
    if(!g.Read8(0x6b0afc)) {
        g.Write8(0x6b0afc,1);
        co_await Call(0x16963c,{{manager}});
    }
    if(!manager)co_return 0;
    co_return (co_await Call(0x16962c,{{manager,id,0x30000}})).R0;
}
Routine Engine::Frame(u32 player,float frame) {
    if(player){CallMachine::Arguments a{{player}};a.S[0]=std::bit_cast<u32>(frame);co_await Call(0x192ae0,a);}co_return 0;
}
Routine Engine::Play(u32 player,u32 anim,float frame,bool loop) {
    if(!player)co_return 0;
    namespace AP=Layout::ui_AnimPlayer;
    g.Write32(player+AP::m_anim,anim);g.Write8(player+AP::m_loop,loop);
    co_await Frame(player,frame);
    if(anim)g.Write8(player+AP::m_playing,1);g.Write8(player+AP::m_xa,1);co_return 0;
}
Routine Engine::OpenScreen(unsigned screen) {
    if(!s.gctx||g.Read8(s.gctx+G::type)!=kStatePlay||screen>7)co_return 0;
    g.Write32(kScreenContext+12,g.Read32(0x656b00+4*screen));
    g.Write16(s.gctx+G::pause_flags,1);g.Write8(s.gctx+G::enable_letterbox,0);
    if(g.Read16(Static+Layout::StaticContext::field_D38)) {
        g.Write16(Static+Layout::StaticContext::field_D38,0);g.Write32(s.gctx+G::field_CAB0,0xa0a0a000);
    }
    if(!g.Read8(s.gctx+G::field_CAAD))g.Write32(s.gctx+G::field_CAB0,0);
    if(const auto fn=g.Read32(s.gctx+G::some_fn2)) {
        if(!(co_await Direct(fn,{{s.gctx+G::some_ptr}})).R0)
            if(const auto other=g.Read32(s.gctx+G::some_fn1))co_await Direct(other,{{s.gctx+G::some_ptr,0}});
    }
    co_return 1;
}
Routine Engine::ScheduleTrigger() {
    if(!s.gctx||g.Read8(s.gctx+G::type)!=kStatePlay)co_return 0;
    const u32 held=g.Read32(s.gctx+G::pad_state__input__buttons),pressed=g.Read32(s.gctx+G::pad_state__input__new_buttons);
    const bool zr=(held&Button::ZR)!=0,start=(pressed&Button::Start)!=0,select=(pressed&Button::Select)!=0;
    if(!zr&&select)co_await OpenScreen(4);
    if(!zr&&start&&(g.Read8(Common+C::save__inventory__collect_register+2)&4))co_await OpenScreen(2);
    // dekui MenuShortcuts removes ZR+Start. MinimapShortcut takes precedence
    // at ref 0x7d06c4 and preserves the ZR+Select test at ref 0x7d06dc.
    if(zr&&select) {
        const u32 ui=g.Read32(0x72f5c0),main=g.Read32(0x656b04);
        const bool supported=ui&&g.Read32(ui+0x16c)==s.gctx&&main&&g.Read32(0x72f5c4)==main&&
            g.Read32(0x72f5c8)==0&&(g.Read32(s.gctx+0x13c)&0xffff)==0x0402;
        if(supported) {
            Mm3dDekui::HudAdjustments::ToggleMinimap();
            co_await Call(0x2006e4,{{0x0100077f}});
        } else {
            g.Write8(g.Read32(kMapScreen)+0x78e,0);co_await OpenScreen(5);
        }
        Bits(s.gctx+G::pad_state__input__buttons,Button::Select,false);
        Bits(s.gctx+G::pad_state__input__new_buttons,Button::Select,false);
    }
    co_return 0;
}
Routine Engine::ScheduleUpdate() {
    if(!s.gctx||!ScreenActive(g,kScheduleScreen))co_return 0;
    namespace S=Layout::ui_ScheduleScreen;
    const u32 screen=g.Read32(kScheduleScreen),pressed=g.Read32(s.gctx+G::pad_state__input__new_buttons);
    Bits(screen+S::btn_press_flags,pressed&(Button::X|Button::Y),true);
    const u32 ap=g.Read32(screen+S::btn_map_anim_player);
    // NativeNotebook ref 0x7d04b0 preserves Y-only schedule input. At
    // ref 0x7d04c4 Event Notes uses X for Map; other notebook views use Select.
    const bool map_input=(Mm3dDekui::NativeNotebook::MapInputFlags(g,screen,pressed,0)&0x40000000u)==0;
    if(map_input&&ap&&g.Read32(ap+Layout::ui_AnimPlayer::m_anim)==g.Read32(screen+S::disp_btn_map_anim)) {
        const u32 handler=screen+S::touch_handler,vtable=g.Read32(handler);
        for(u32 field:{S::btn_map_l,S::base1_btn_map_l}) {
            const auto button=g.Read32(screen+field);
            // TouchHandler has exactly three virtuals and no virtual destructor.
            // AAPCS-VFP places the two float coordinates in S0/S1.
            CallMachine::Arguments a{{handler,button,kScreenContext,0}};a.S[0]=0;a.S[1]=0;
            co_await Direct(g.Read32(vtable),a); // UpdateState
            a.R={handler,button,0,kScreenContext};
            co_await Direct(g.Read32(vtable+4),a); // OnActivate
            a.R={handler,button,0,0};a.Stack={kScreenContext};a.StackWords=1;
            co_await Direct(g.Read32(vtable+8),a); // OnRelease
        }
    }
    co_return 0;
}
void Engine::EditMessage(u32 self,u16 id) {
    const u32 base=g.Read32(self+4),entries=g.Read32(self+8),size=g.Read32(self+12),stride=g.Read32(self+16);
    if(!base||entries!=base+16||size<16||size>0x1000000||g.Read32(base)!=0x47534d47||g.Read32(base+4)!=1)return;
    const u32 languages=g.Read32(base+12),count=g.Read32(base+8);
    if(!count||count>20000||(languages&~0x3fffu)||stride!=12u+8u*std::popcount(languages)||16+u64(count)*stride>size)return;
    for(const auto& edit:s.message_edits) {
        if(edit.id!=id||edit.language>=14||!(languages&(1u<<edit.language))||edit.original.size()!=edit.edited.size())continue;
        const bool active=edit.price?Mm3dDekui::Prices::enabled.load():Mm3dDekui::SingleScreenText::enabled.load();
        for(u32 i=0;i<count;++i) {
            const u32 entry=entries+i*stride;if(g.Read16(entry)!=id)continue;
            const u32 language_index=std::popcount(languages&((1u<<edit.language)-1));
            const u32 offset=g.Read32(entry+12+8*language_index),length=g.Read32(entry+16+8*language_index);
            if(offset<16+count*stride||offset>size||length>size-offset||length!=edit.original.size())break;
            u64 hash=0xcbf29ce484222325ULL;bool readable=true;
            for(u32 n=0;n<length;++n){u8 b;if(!g.Bus().Read8(base+offset+n,&b)){readable=false;break;}hash=(hash^b)*0x100000001b3ULL;}
            const auto before=active?edit.original_metadata:edit.edited_metadata,after=active?edit.edited_metadata:edit.original_metadata;
            if(readable&&hash==(active?edit.original_hash:edit.edited_hash)&&(!before||g.Read32(entry+4)==*before)) {
                const auto& bytes=active?edit.edited:edit.original;
                for(u32 n=0;n<length;++n)g.Write8(base+offset+n,bytes[n]);
                if(after)g.Write32(entry+4,*after);
            }
            break;
        }
    }
}
Routine Engine::MessageGet(u32 self,u16 id,u32 out) {
    namespace M=Layout::Message;
    const u16 lookup=id==0x195e?0x1962:id;
    if(s.dekui){auto cpu=context.Saved;cpu.r[0]=self;cpu.r[1]=lookup;s.dekui(0xfffe90,cpu,g.Bus());}
    EditMessage(self,lookup);
    const u32 base=g.Read32(self+4),entries=g.Read32(self+8),size=g.Read32(self+12),stride=g.Read32(self+16);
    if(!out||!base||size<16||size>0x1000000||g.Read32(base)!=0x47534d47||g.Read32(base+4)!=1)co_return 0;
    const u32 count=g.Read32(base+8),languages=g.Read32(base+12);
    if(!count||count>20000||(languages&~0x3fffu)||stride!=12u+8u*std::popcount(languages)||entries!=base+16||16+u64(count)*stride>size)co_return 0;
    u32 lo=0,hi=count,entry=0;
    while(lo<hi) {const u32 mid=lo+(hi-lo)/2,at=entries+stride*mid;const auto candidate=g.Read16(at);
        if(candidate<lookup)lo=mid+1;else if(candidate>lookup)hi=mid;else{entry=at;break;}}
    if(!entry)co_return 0;
    const u32 flags=g.Read32(entry+8),field=g.Read32(entry+4);
    g.Write32(out+M::msgid,id);g.Write8(out+M::is_flag1,bool(flags&1));g.Write8(out+M::is_flag1000,bool(flags&0x1000));g.Write8(out+M::is_flag2000,bool(flags&0x2000));
    g.Write32(out+M::flag10,(flags>>4)&15);g.Write32(out+M::flag100,(flags>>8)&3);g.Write32(out+M::field_10,g.Read16(entry+2));
    const u32 x14=(flags>>16)&255,x18=field&1023,x1c=(field>>10)&1023,x20=(field>>20)&0xfffff3ff;
    g.Write32(out+M::field_14,x14==255?~0u:x14);g.Write32(out+M::field_18,x18==1023?~0u:x18);
    g.Write32(out+M::field_1C,x1c==1023?~0u:x1c);g.Write32(out+M::field_20,x20==1023?~0u:x20);
    for(unsigned language=0,index=0;language<14;++language) {
        const u32 text=out+M::texts+language*Layout::MessageText::Size;
        g.Write32(text,(co_await Call(0x1c519c,{{language}})).R0);
        u32 ptr=0,length=0;
        if(languages&(1u<<language)) {const auto offset=g.Read32(entry+12+8*index);length=g.Read32(entry+16+8*index);++index;
            if(offset>size||length>size-offset)co_return 0;ptr=base+offset;}
        g.Write32(text+4,ptr);g.Write32(text+8,length);
    }
    co_return 1;
}
} // namespace Mm3dRestoration
