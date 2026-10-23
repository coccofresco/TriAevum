// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration source/game/items.cpp, player.cpp and rst/link.cpp.
namespace Mm3dRestoration {
bool Engine::HasItem(u32 id,bool mask) {
    const u32 at=Common+(mask?C::save__inventory__masks:C::save__inventory__items);
    for(unsigned i=0;i<24;++i)if(g.Read8(at+i)==id)return true;return false;
}
Routine Engine::CanUseItem(u32 item) {
    using I=Enum::Items::ItemId;using Sc=Enum::Scene::SceneId;using F=Enum::Player::Flag1;
    if(item==Value(I::Ocarina)||item==Value(I::PictographBox))co_return g.Read8(Common+C::usable_btns+(item==Value(I::Ocarina)?6:7))!=255;
    const auto p=Player();if(!s.gctx||!p||g.Read8(p+P::action_type)==2)co_return 0;
    const int mode=int((co_await Call(0x1e827c,{{s.gctx}})).R0);
    const auto scene=Sc(g.Read16(s.gctx+G::scene));
    const auto has=[&](u32 field,u8 bits){return (g.Read8(Common+field)&bits)!=0;};
    const auto between=[&](I first,I last){return Value(first)<=item&&item<=Value(last);};
    const bool nontransform=between(I::MaskOfTruth,I::MaskOfScents),transform=between(I::DekuMask,I::FierceDeityMask),bottled=between(I::Bottle,I::MysteryMilkSpoiled);
    const bool boss=scene==Sc::OdolwaLair||scene==Sc::GohtLair||scene==Sc::GyorgLair||scene==Sc::TwinmoldLair||scene==Sc::MajoraLair;
    if(Flag(p,P::flags1,Value(F::Riding))||has(C::save__anonymous_72,1)||(!has(C::save__anonymous_13,2)&&g.Read8(s.gctx+G::field_C531)>=2))co_return 0;
    if(scene==Sc::SwampFishingHole||scene==Sc::OceanFishingHole)co_return item==Value(I::ZoraMask)||item==Value(I::FierceDeityMask)||nontransform||(transform&&mode<2);
    const auto entrance=g.Read32(Common+C::sub1__entrance);
    if(((g.Read16(Common+C::field_3696)==1&&entrance==0x8e10)||entrance==0xd010)&&!g.Read8(s.gctx+G::field_C529_one_to_clear_input)&&!g.Read8(s.gctx+G::field_C85A))co_return 0;
    if(has(C::save__anonymous_13,2)||has(C::save__anonymous_138,0x20)||has(C::save__anonymous_130,8)||(scene==Sc::Cutscenes&&g.Read32(Common+C::setup2)==6))co_return 0;
    const bool nuts=Flag(p,P::flags3,Value(Enum::Player::Flag3::DekuNutsOnB));
    if((has(C::save__anonymous_12,0x10)&&!nuts)||nuts)co_return 0;
    if(mode>=2&&mode<=4)co_return item==Value(I::ZoraMask)||(item==Value(I::FierceDeityMask)&&boss)||nontransform||(mode==2&&item>=Value(I::Bottle)&&bottled);
    if(Flag(p,P::flags1,Value(F::Unk200000)))co_return item==Value(I::LensOfTruth);
    if(Flag(p,P::flags1,Value(F::Unk2000))&&g.Read8(Common+C::usable_btns)!=255)co_return 0;
    const auto form=g.Read8(Common+C::save__player_form);
    if(form>=ItemUsabilityTable.size()||item>=0x72||!ItemUsabilityTable[form][item]||g.Read16(p+A::id)!=Value(Enum::Actor::Id::Player))co_return 0;
    if(g.Read8(p+P::active_mask_id)==Value(Enum::Items::MaskId::GiantMask))co_return item==Value(I::GiantMask);
    if(item==Value(I::GiantMask))co_return scene==Sc::TwinmoldLair;
    if(item==Value(I::FierceDeityMask))co_return boss;
    if(g.Read8(s.gctx+G::actors__field_4)&&between(I::FireArrow,I::LightArrow))co_return 0;
    if(between(I::MoonTear,I::PendantOfMemories)||bottled)co_return !g.Read8(s.gctx+G::hud_state__prohibit_gear_or_bottles);
    if(between(I::DekuMask,I::GiantMask))co_return !g.Read8(s.gctx+G::hud_state__prohibit_masks);
    co_return !g.Read8(s.gctx+G::hud_state__prohibit_regular_items);
}
Routine Engine::FastTransform() {
    const u32 p=Player();if(!p)co_return 0;const u32 pad=g.Read32(p+P::controller_info__state);if(!pad)co_return 0;
    const u32 held=g.Read32(pad+Layout::pad_State::input__buttons),pressed=g.Read32(pad+Layout::pad_State::input__new_buttons);
    using I=Enum::Items::ItemId;using Ac=Enum::Items::Action;
    struct Action {u32 button;I item;Ac action;bool water,zr;};
    constexpr Action actions[]={{Button::Left,I::ZoraMask,Ac::ZoraMask,true,false},{Button::Up,I::GoronMask,Ac::GoronMask,false,false},{Button::Down,I::DekuMask,Ac::DekuMask,false,false},{Button::Up,I::FierceDeityMask,Ac::FierceDeityMask,false,true}};
    for(const auto& action:actions) {
        if(bool(held&Button::ZR)!=action.zr||!(pressed&action.button)||(Flag(p,P::flags1,Value(Enum::Player::Flag1::InWater))&&!action.water))continue;
        if(g.Read8(p+P::active_mask_id)==Value(Enum::Items::MaskId::GiantMask)||!HasItem(Value(action.item),true)||!(co_await CanUseItem(Value(action.item))))co_return 0;
        if(!(co_await Call(0x1e0390,{{p,0,Value(action.action)}})).R0)co_return 0;
        g.Write8(p+P::action,u8(action.action));g.Write8(p+P::action_type,u8(Enum::Player::ActionType::OcarinaOrTransformation));g.Write8(p+P::transform_mask_action,u8(action.action));break;
    }co_return 0;
}
Routine Engine::LinkCalc() {
    s.is_swimming=false;const auto p=Player();if(!p||(g.Read16(s.gctx+G::pause_flags)&3))co_return 0;
    const auto pad=g.Read32(p+P::controller_info__state);
    if(pad){++s.a_press_duration;if(!(g.Read32(pad+Layout::pad_State::input__buttons)&Button::A))s.a_press_duration=0;}
    s.is_swimming=Flag(p,P::flags1,Value(Enum::Player::Flag1::InWater))&&(g.Read16(p+A::flags_94)&Value(Enum::Actor::Flag94::Grounded))==0;
    co_await FastArrow(p);
    const auto hud=g.Read32(CommonLayouts+8);
    if(hud&&g.Read8(hud+Layout::ui_PlayHud::tatl_state)==0&&g.Read8(Common+C::save__inventory__items)==Value(Enum::Items::ItemId::Ocarina)&&g.Read16(s.gctx+G::hud_state__item_btn_opacity+8)==255&&pad&&(g.Read32(pad+Layout::pad_State::input__new_buttons)&Button::Right)) {
        g.Write8(p+P::action_type,u8(Enum::Player::ActionType::OcarinaOrTransformation));g.Write8(p+P::action,u8(Enum::Items::Action::Ocarina));
    }co_return 0;
}
inline constexpr unsigned ArrowCosts[]={0,4,4,8};
Routine Engine::FastArrow(u32 p) {
    const auto action=g.Read8(p+P::current_action),slot=g.Read8(p+P::item_btn_slot);
    const auto reset=[&]{s.arrow_action.reset();s.arrow_slot.reset();s.magic_timer=-1;};
    if(action<9||action>12||(g.Read8(p+P::action_type)!=3&&!g.Read32(p+P::projectile_actor))){reset();co_return 0;}
    if(s.arrow_slot!=slot)reset();s.arrow_slot=slot;
    if(s.magic_timer>0)--s.magic_timer;
    if(s.arrow_action&&s.magic_timer==0){co_await Call(0x2264cc,{{s.gctx,ArrowCosts[*s.arrow_action-9],0,1}});s.magic_timer=-1;}
    if(!s.arrow_action)s.arrow_action=action;
    if(!(g.Read32(s.gctx+G::pad_state__input__new_buttons)&Button::ZL))co_return 0;
    auto can=[&](unsigned i)->Routine {co_return HasItem(i+1)&&g.Signed8(Common+C::save__player__magic)>=int(ArrowCosts[i])&&(co_await CanUseItem(i+1));};
    unsigned idx=*s.arrow_action-9;if(!(co_await can(idx)))co_return 0;
    do {idx=(idx+1)%4;}while(!(co_await can(idx)));
    s.arrow_action=u8(idx+9);g.Write8(p+P::current_action,*s.arrow_action);g.Write8(p+P::action,*s.arrow_action);
    if(slot<5)g.Write8(Common+C::save__equipment__data+slot,u8(idx+1));
    if(auto arrow=g.Read32(p+P::projectile_actor)) {
        // Actor::Free clears both calc and draw function pointers.
        g.Write32(arrow+A::calc_fn,0);g.Write32(arrow+A::draw_fn,0);Bits(arrow+A::flags,Value(Enum::Actor::Flag::Targetable),false);g.Write32(p+P::projectile_actor,0);co_await SpawnArrow(p);
    }co_return 0;
}
Routine Engine::ArrowInfo(u32 game,u32 p,u32 item_out,u32 param_out) {
    using I=Enum::Items::ItemId;u32 item,param;
    const bool deku=g.Read8(p+P::active_form)==Value(Enum::Player::Form::Deku);
    if(g.Read8(p+P::current_action)==Value(Enum::Items::Action::DekuBubble)){item=Value(I::DekuNuts);param=deku?7:6;}
    else{item=Value(I::Arrow);param=u32(s.arrow_action.value_or(g.Read8(p+P::current_action)))-7;if(Flag(p,P::flags1,Value(Enum::Player::Flag1::Riding)))param=1;}
    if(item_out)g.Write8(item_out,u8(item));if(param_out)g.Write32(param_out,param);
    bool can;
    if(deku)can=g.Signed8(Common+C::save__player__magic)>=2||((g.Read8(Common+C::save__anonymous_72)&1)&&g.Read16(game+G::scene)==Value(Enum::Scene::SceneId::HoneyAndDarling));
    else can=Flag(p,P::flags3,Value(Enum::Player::Flag3::DekuStuffMaybe))||(g.Read16(Common+C::field_3696)==1&&g.Read16(game+G::hud_state__field_244))||g.Read8(game+G::field_C531)||(co_await Call(0x224ef8,{{item}})).R0;
    // Packed private result for SpawnArrow; public hook returns only bit 0.
    co_return u32(can)|(param<<8)|(item<<24);
}
Routine Engine::SpawnArrow(u32 p) {
    const auto info=co_await ArrowInfo(s.gctx,p);if(!(info&1))co_return 0;
    u32 param=(info>>8)&0xffff;int type=int(param)-2;
    // Fix donor's one-past-the-end check before accessing the four-element table.
    if(type<0||type>=4)co_return 0;
    if(g.Signed8(Common+C::save__player__magic)<int(ArrowCosts[type])){type=0;param=2;}
    CallMachine::Arguments args{{s.gctx+G::actors,p,s.gctx,Value(Enum::Actor::Id::Arrow)}};
    args.Stack={0,g.Read16(p+P::angle),0,param};args.StackWords=4;
    for(unsigned i=0;i<3;++i)args.S[i]=g.Read32(p+A::pos__pos+4*i);
    g.Write32(p+P::projectile_actor,(co_await Call(0x22caac,args)).R0);g.Write16(Common+C::magic_cost,0);if(type)s.magic_timer=2;co_return 0;
}
Routine Engine::Elegy(u32 p,u32 game) {
    g.Write32(p+P::controller_info__state,game+G::pad_state);
    const auto timer=u16(g.Read16(p+P::timer)+1);g.Write16(p+P::timer,timer);
    const auto form=g.Read8(p+P::active_form);if(form>=5)co_return 0;
    if(timer==1){co_await Call(0x1f0758,{{game,p}});if(auto statue=g.Read32(game+G::elegy_statues+4*form))g.Write8(statue+Layout::act_ObjElegyStatue::timer,0);}
    else if(timer>5) {
        const auto statue=g.Read32(game+G::elegy_statues+4*form);bool ready=!statue;
        if(statue){ready=true;for(unsigned i=0;i<3;++i)ready&=g.Float(statue+A::pos__pos+4*i)==g.Float(p+A::pos__pos+4*i);}
        if(timer>135||ready){g.Write16(game+G::ocarina_state,u16(Enum::Context::OcarinaState::StoppedPlaying));CallMachine::Arguments args{{p,game}};args.S[0]=std::bit_cast<u32>(-6.f);co_await Call(0x1e6500,args);}
        else g.Write16(statue+Layout::act_ObjElegyStatue::opacity,u16(std::max(int(g.Read16(statue+Layout::act_ObjElegyStatue::opacity))-24,0)));
    }co_return 0;
}
} // namespace Mm3dRestoration
