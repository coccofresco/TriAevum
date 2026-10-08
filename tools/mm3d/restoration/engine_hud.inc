// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration source/rst/large_screen.cpp and game/ui.
#include "item_tables.h"
namespace Mm3dRestoration {
u32 Engine::Anim(u32 layout,std::string_view name) {
    const auto cl=g.Read32(layout+Layout::ui_Layout::cl);if(!cl)return 0;
    const u32 data=g.Read32(cl+Layout::ui_LayoutClass::anims),count=g.Read16(cl+Layout::ui_LayoutClass::num_anims);
    for(u32 i=0;i<count;++i){const u32 a=data+i*Layout::ui_Anim::Size;if(Name(g.Read32(a+Layout::ui_Anim::name),name))return a;}
    return 0;
}
Routine Engine::Icon(u32 icon,u32 value,bool item) {
    if(!icon)co_return 0;
    namespace I=Layout::ui_ItemIcon;
    if(item)value=value<std::size(item_id_to_icon_id_map)?item_id_to_icon_id_map[value]:255;
    if(value!=255)co_await Frame(g.Read32(icon+I::item_icon_player),float(value));
    Visible(icon+Layout::ui_Layout::root_widget,value!=255);g.Write32(icon+I::icon_id,value);
    Visible(g.Read32(icon+I::icon_number),false);co_return 0;
}
Routine Engine::Hud(std::string_view action) {
    if(s.hud_depth)co_return 0;
    struct Guard {unsigned& depth;Guard(unsigned& d):depth(d){++depth;}~Guard(){--depth;}} guard(s.hud_depth);
    const u32 ctx=context.Saved.r[1];
    if(action=="MainScreen_Init") {
        s.hud_ready=false;
        if(!s.hud){s.hud=co_await Alloc(H::Size);if(!s.hud)co_return 0;
            for(u32 i=0;i<H::Size;++i)g.Write8(s.hud+i,0);
            // Static Hud2's base constructor defaults. Its lifecycle is called
            // explicitly; the two native interface vptrs serve native helpers.
            g.Write32(s.hud,g.Read32(context.Saved.r[0]));
            g.Write32(s.hud+H::vtable_4,g.Read32(context.Saved.r[0]+H::vtable_4));
            for(u32 field:{H::magic_max,H::magic,H::has_magic,H::magic_num_upgrades,H::magic_unlimited})g.Write8(s.hud+field,255);
        }
        const u32 old=g.Read32(CommonLayouts),layout=co_await MakeLayout("Joker.Main.New.Hud2");if(!layout)co_return 0;
        g.Write32(CommonLayouts,layout);co_await Call(0x599d20,{{s.hud,ctx}});g.Write32(CommonLayouts,old);
        const auto touch=g.Read32(s.hud+H::playTouchPanel),hud=g.Read32(CommonLayouts+8);
        auto& w=s.hud_widgets;
        w[0]=LayoutWidget(touch,"item_button_g");w[1]=LayoutWidget(hud,"action_button_g");
        w[2]=LayoutWidget(touch,"non_essential_g");w[3]=LayoutWidget(touch,"heart_g");w[4]=LayoutWidget(hud,"timer_g");
        const auto set=g.Read32(LayoutWidget(touch,"item_icon_set")+Layout::ui_Widget::layout);
        w[5]=LayoutWidget(set,"icon_item_1_g");w[6]=LayoutWidget(set,"icon_item_2_g");
        w[7]=LayoutWidget(set,"icon_item_x_g");w[8]=LayoutWidget(set,"icon_item_y_g");
        w[9]=LayoutWidget(touch,"icon_item_oca_g");w[10]=LayoutWidget(hud,"a_action_button");w[11]=LayoutWidget(hud,"b_action_button");
        w[12]=Widget(w[0],"hud_dpad");const auto dpad=g.Read32(w[12]+Layout::ui_Widget::layout);
        w[13]=LayoutWidget(dpad,"icon_l");w[14]=LayoutWidget(dpad,"icon_r");w[15]=LayoutWidget(dpad,"icon_u");w[16]=LayoutWidget(dpad,"icon_d");
        for(unsigned i=0;i<5;++i)Opacity(w[i],0);
        s.hud_ready=true;
    } else if(s.hud&&s.hud_ready) {
        if(action=="MainScreen_Open")co_await Call(0x59b4d4,{{s.hud,ctx}});
        else if(action=="MainScreen_Calc")co_await HudCalc(ctx);
        else if(action=="MainScreen_Draw") {
            const auto mgr=(co_await Call(0x169824)).R0;
            co_await Call(0x17ec58,{{mgr,g.Read32(s.hud+H::playTouchPanel),2,10}});
        } else if(action=="MainScreen_PrepareClose")co_await Play(g.Read32(s.hud+H::transition),Anim(g.Read32(s.hud+H::playTouchPanel),"out_menu_top_anim"));
        else if(action=="MainScreen_CalcClose")co_await Call(0x59b3cc,{{s.hud,ctx}});
        else if(action=="MainScreen_UpdateButtonUsability") {
            namespace HS=Layout::HudState;
            const bool minimum=g.Read16(ctx+HS::a_btn_opacity)>=128&&g.Read16(ctx+HS::b_btn_opacity)>=128;
            const unsigned indices[]={8,7,5,6,9};
            for(unsigned i=0;i<5;++i)Opacity(s.hud_widgets[indices[i]],std::max(g.Read16(ctx+HS::item_btn_opacity+2*i)/255.f,minimum?.5f:0.f));
            for(u32 field:{H::camChangePane,H::camPicturePane,H::camReturn})Opacity(g.Read32(s.hud+field),g.Read16(ctx+HS::camera_opacity)/255.f);
        } else co_await HudMagic(action);
    }
    co_return 0;
}
Routine Engine::HudCalc(u32 ctx) {
    // Donor GetState is an unchecked cast; reject other states here.
    const auto manager=g.Read32(ctx+4),state=manager?g.Read32(manager+Layout::ui_Context::game_state):0;
    const u32 game=state&&g.Read8(state+Layout::State::type)==kStatePlay?state:0;
    const u32 old_context=s.gctx;s.gctx=game;
    const auto player=Player();bool hide=!game||g.Float(game+G::dim_overlay_alpha)>=.5f;
    if(player)hide|=Flag(player,P::flags1,Value(Enum::Player::Flag1::FreezeLink)|Value(Enum::Player::Flag1::Locked))||g.Read8(player+P::action_type)==2;
    auto& w=s.hud_widgets;
    for(unsigned i=0;i<5;++i){bool h=hide;if(i==3)h&=!game||!g.Read8(game+G::undim_heart_status);if(i<2)h|=!game||g.Read8(game+G::hide_hud);AddOpacity(w[i],h?-.125f:.125f);}
    // Eight appended HudButtons substitutions: both icon banks visible; the
    // presentation's second level is false for A/ocarina and the D-pad only.
    for(unsigned i=5;i<=8;++i)Visible(w[i],true);Visible(w[9],false);Visible(w[10],true);
    co_await HudDpad(game);
    if(game) {
        using Sc=Enum::Scene::SceneId;const auto scene=Sc(g.Read16(game+G::scene));float frame=0;
        if(scene==Sc::SnowheadTemple||scene==Sc::StoneTowerTemple||scene==Sc::StoneTowerTempleInverted||scene==Sc::WoodfallTemple||scene==Sc::GreatBayTemple)frame=1;
        if(scene==Sc::SwampSpiderHouse||scene==Sc::OceansideSpiderHouse)frame=2;
        co_await Frame(g.Read32(s.hud+H::key_change),frame-1);
        Visible(g.Read32(s.hud+H::iconKeyPane),frame!=0);
        for(u32 field:{H::num_key_l,H::num_key_rank_ten_l})if(auto n=g.Read32(s.hud+field))Visible(n+Layout::ui_Layout::root_widget,frame!=0&&(field==H::num_key_l||g.Read8(s.hud+H::numSmallKeys)>=10));
        if(player) {
            Visible(g.Read32(s.hud+H::carrotStatus),Flag(player,P::flags1,Value(Enum::Player::Flag1::Riding)));
            const u32 before=std::min<u32>(g.Read8(s.hud+H::num_carrots),6),after=std::min<u32>(g.Read8(game+G::hud_state__num_carrots),6);
            for(u32 i=before;i<after;++i){auto ap=g.Read32(s.hud+H::carrot_anims+8*i);co_await Frame(ap,0);g.Write8(ap+Layout::ui_AnimPlayer::m_playing,0);g.Write8(ap+Layout::ui_AnimPlayer::m_xa,1);}
            for(u32 i=after;i<before;++i)co_await Play(g.Read32(s.hud+H::carrot_anims+8*i),g.Read32(s.hud+H::carrot_anims+8*i+4));
            g.Write8(s.hud+H::num_carrots,u8(after));
        }
    }
    co_await Call(0x59a874,{{s.hud,ctx}});co_await Call(0x59ab38,{{s.hud}});
    co_await Call(0x59b040,{{s.hud,manager}});co_await Call(0x59aed0,{{s.hud}});co_await Call(0x59acbc,{{s.hud}});
    for(u32 field:{H::rank_a_l,H::rank_ten_l,H::rank_hundred_l,H::num_key_l,H::num_key_rank_ten_l}) {
        const auto num=g.Read32(s.hud+field),ap=num?g.Read32(num+Layout::ui_NumAll::color_player):0;
        if(ap&&int(g.Float(ap+Layout::ui_AnimPlayer::m_frame))==2)co_await Frame(ap,0);
    }
    CallMachine::Arguments a{{g.Read32(s.hud+H::playTouchPanel)}};a.S[0]=std::bit_cast<u32>(.033333f);co_await Call(0x161ae8,a);
    s.gctx=old_context;co_return 0;
}
Routine Engine::HudDpad(u32 game) {
    using I=Enum::Items::ItemId;auto& w=s.hud_widgets;
    constexpr unsigned indices[]={13,15,16};constexpr I masks[]={I::ZoraMask,I::GoronMask,I::DekuMask};bool fade=true;
    for(unsigned i=0;i<3;++i) {
        const auto widget=w[indices[i]],icon=g.Read32(widget+Layout::ui_Widget::layout);const auto old=g.Read32(icon+Layout::ui_ItemIcon::icon_id);
        const bool can=co_await CanUseItem(Value(masks[i]));co_await Icon(icon,Value(masks[i]),true);
        Visible(widget,HasItem(Value(masks[i]),true));fade&=!HasItem(Value(masks[i]),true);
        if(old!=g.Read32(icon+Layout::ui_ItemIcon::icon_id))Opacity(widget,can?1:.3f);else AddOpacity(widget,can?.25f:-.25f,.3f);
    }
    const auto hud=g.Read32(CommonLayouts+8);
    if(g.Read8(hud+Layout::ui_PlayHud::tatl_state)!=0){Opacity(w[14],0);fade=false;}
    else if(game&&g.Read8(Common+C::save__inventory__items)==Value(I::Ocarina)) {
        const bool can=co_await CanUseItem(Value(I::Ocarina));AddOpacity(w[14],can?.25f:-.25f,.3f);
        const auto icon=g.Read32(w[14]+Layout::ui_Widget::layout),source=g.Read32(s.hud+H::icon_c_btn02);
        co_await Icon(icon,g.Read32(source+Layout::ui_ItemIcon::icon_id));fade=false;
    }else AddOpacity(w[14],-.25f);
    AddOpacity(w[12],fade?-.25f:.25f);co_return 0;
}
Routine Engine::HudMagic(std::string_view action) {
    if(g.Read8(s.hud+H::magic_unlimited))co_return 0;
    const auto use=g.Read32(s.hud+H::magic_use),yellow=g.Read32(s.hud+H::gaugeYellow_g);
    if(action=="MainScreen_MagicPlayAndHideYellowGauge") {co_await Play(use,g.Read32(s.hud+H::magic_use_ani),std::numeric_limits<float>::infinity());Visible(yellow,false);}
    else if(action=="MainScreen_MagicHideYellowGauge")Visible(yellow,false);
    else if(action=="MainScreen_MagicStartUseAnim"&&!g.Read8(use+Layout::ui_AnimPlayer::m_playing))co_await Play(use,g.Read32(s.hud+H::magic_use_ani),0,true);
    else if(action=="MainScreen_MagicUpdateCost") {
        const float multiplier=g.Read8(Common+C::save__player__magic_num_upgrades)==0?.5f:1.f;
        const float max=float(g.Signed16(Common+C::magic_max));
        co_await Frame(g.Read32(s.hud+H::magic_gauge_position),multiplier*g.Signed8(Common+C::save__player__magic)/max);
        co_await Frame(g.Read32(s.hud+H::magic_gauge_value),multiplier*g.Signed16(Common+C::magic_cost)/max);Visible(yellow,true);
    }
    co_return 0;
}
Routine Engine::BIcon(u32 icon,u32 game) {
    namespace B=Layout::ui_PlayBActionIcon;
    // Update is the last virtual after Layout's destructor and nine methods.
    const auto vtable=g.Read32(icon);co_await Direct(g.Read32(vtable+40),{{icon,game}});
    if(g.Read16(icon+B::text_num)!=10||Flag(g.Read32(icon+B::a_action_icon),Layout::ui_Widget::pos+Layout::ui_WidgetPos::flags,1))co_return 0;
    const auto old=s.gctx;s.gctx=game;const auto player=Player();s.gctx=old;
    if(!player||g.Read8(player+P::active_form)!=Value(Enum::Player::Form::Human))co_return 0;
    Visible(g.Read32(icon+B::a_action_text),false);Visible(g.Read32(icon+B::a_action_num),false);Visible(g.Read32(icon+B::a_action_icon),true);
    co_await Icon(g.Read32(icon+B::a_action_icon_l),g.Read8(Common+C::save__equipment__data),true);co_return 0;
}
} // namespace Mm3dRestoration
