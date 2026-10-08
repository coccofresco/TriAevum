// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration source/game/states and rst/fixes/ocarina.cpp.
namespace Mm3dRestoration {
Routine Engine::StateInfo(u32 state) {
    namespace SI=Layout::StateInfo;
    const u32 init=g.Read32(state+Layout::State::next_state_init_fn),type=g.Read8(state+Layout::State::type);
    for(u32 i=0;i<10;++i){const auto info=0x6883fc+i*SI::Size;if(init?g.Read32(info+SI::init_fn)==init:g.Read8(info+SI::type)==type)co_return info;}
    if(init?init!=Session::SotInit:type!=10)co_return 0;
    if(!s.sot_info){s.sot_info=co_await Alloc(SI::Size);if(!s.sot_info)co_return 0;
        for(u32 i=0;i<SI::Size;i+=4)g.Write32(s.sot_info+i,0);
        g.Write32(s.sot_info+SI::init_fn,Session::SotInit);g.Write32(s.sot_info+SI::instance_size,Layout::StateSotSave::Size);g.Write8(s.sot_info+SI::type,10);
    }co_return s.sot_info;
}
Routine Engine::Sot(std::string_view action) {
    namespace S=Layout::StateSotSave;const auto self=context.Saved.r[0];if(!self)co_return 0;
    const auto telop=[&]{g.Write8(self+S::status,0);g.Write32(self+S::next_state_init_fn,0);g.Write8(self+S::type,7);};
    if(action=="SotDestroy")co_return 0; // both donor destructors are trivial
    if(action=="SotInit") {
        g.Write32(self+S::calc_fn,Session::SotCalc);g.Write32(self+S::exit_fn,Session::SotDestroy);g.Write8(self+S::status,2);g.Write32(self+S::sotsave_status,0);
        co_await Call(0x15d20c,{{self}});g.Write32(self+S::calc_fn,Session::SotCalc);
        if(!s.going_back_in_time)telop();else s.going_back_in_time=false;co_return 0;
    }
    switch(g.Read32(self+S::sotsave_status)) {
    case 0:
        co_await Call(0x1883f0,{{18}});g.Write32(self+S::message_window,co_await MakeLayout("Joker.Main.MessageWindow"));g.Write32(self+S::ending_status,4);g.Write32(self+S::sotsave_status,1);break;
    case 1:
        if((co_await Call(0x180190,{{Project}})).R0)break;
        g.Write32(self+S::layout,co_await MakeLayout("Joker.Main.Ending.Ending"));g.Write32(self+S::press_start_layout,co_await MakeLayout("Joker.Main.Ending.EndingPressStart"));g.Write32(self+S::sotsave_status,2);break;
    case 2:
        if(g.Read32(self+S::ending_status)==4) {
            co_await Call(0x18cd10,{{self,0x575e}});g.Float(self+S::timer,.3f);
            namespace T=Layout::ui_MessageTextState;const auto text=g.Read32(self+S::text_state),arrow=g.Read32(self+S::branch_arrow);
            const int count=s32(g.Read32(text+T::num_choices));
            if(count>0) {
                const u32 choice=count>1?1:0;
                if(g.Read32(self+S::message_choice)!=choice) {
                    const auto y=s32((co_await Call(0x1c6ad4,{{text,g.Read32(text+T::first_choice_slot)+choice}})).R0)+8;
                    const auto pos=arrow+Layout::ui_Widget::pos;const float value=float(y);
                    if(g.Float(pos+Layout::ui_WidgetPos::translate__y)!=value){g.Float(pos+Layout::ui_WidgetPos::translate__y,value);Bits(pos+Layout::ui_WidgetPos::flags,0x200,true);Bits(pos+Layout::ui_WidgetPos::active_flags,0x200,value==0);}
                    g.Write32(self+S::message_choice,choice);
                }
            }
            g.Write32(self+S::ending_status,g.Read32(self+S::ending_status)+1);
        }
        co_await Call(0x16ec90,{{self}});
        if(g.Read32(self+S::ending_status)==10)co_await Call(0x18cd10,{{self,0xe9b}});
        if(g.Read32(self+S::ending_status)==12)telop();break;
    }co_return 0;
}
Routine Engine::EndOcarina(u32 window) {
    const auto p=Player();if(!s.gctx||!p)co_return 0;
    constexpr u32 durations[]={20,25,25,20,20};const auto form=g.Read8(p+P::active_form);if(form>=5)co_return 0;
    co_await Call(0x4fe0bc,{{0,durations[form]}});co_await Call(0x1d1a18,{{window,1}});
    g.Write32(s.gctx+0x8422,1);g.Write16(s.gctx+G::ocarina_state,u16(Enum::Context::OcarinaState::StoppedPlaying));co_return 0;
}
Routine Engine::OcarinaSong(u32 window,u16 song) {
    if(!s.gctx)co_return 0;
    const bool played=song<16&&s.played_songs.test(song);if(song<16)s.played_songs.set(song);
    using Song=Enum::Context::OcarinaSong;using State=Enum::Context::OcarinaState;using Sc=Enum::Scene::SceneId;
    if(!played||(song!=Value(Song::SongOfSoaring)&&song!=Value(Song::ElegyOfEmptiness)))co_return 0;
    co_await EndOcarina(window);
    constexpr Sc scenes[]={Sc::StoneTower,Sc::StoneTowerInverted,Sc::RoadToIkana,Sc::IkanaCastle,Sc::IgosDuIkanaLair,Sc::IkanaCanyon,Sc::StoneTowerTemple,Sc::StoneTowerTempleInverted,Sc::SecretShrine,Sc::BeneathTheWell,Sc::GhostHut};
    const auto scene=Sc(g.Read16(s.gctx+G::scene));const bool soaring=song==Value(Song::SongOfSoaring);
    if(soaring||std::find(std::begin(scenes),std::end(scenes),scene)!=std::end(scenes)) {
        co_await Call(0x2006e4,{{Value(Enum::Sound::EffectId::NA_SE_SY_TRE_BOX_APPEAR)}});
        g.Write16(s.gctx+G::ocarina_song,song);g.Write16(s.gctx+G::ocarina_state,u16(State::PlayingAndReplayDone));
        if(soaring){g.Write32(window+Layout::ui_MessageWindow::song,song);g.Write8(s.gctx+0x83ec,0);co_await Call(0x1d78f0,{{window}});}
    }else {co_await Call(0x21bafc,{{s.gctx,0x1b95,0}});g.Write16(s.gctx+G::ocarina_state,u16(State::StoppedPlaying));co_await Call(0x1d8c5c,{{0}});}
    co_return 1;
}
Routine Engine::Calc() {
    const auto state=context.Saved.r[0];s.gctx=0;const auto type=g.Read8(state+G::type);
    if(!s.initialised&&type==kStateFirstGame) {
        namespace FP=Layout::act_FormParam;using Form=Enum::Player::FormParamIndex;
        g.Write16(0x7ae9e8+Value(Form::Deku)*FP::Size+FP::run_accel,200);
        g.Write16(0x7ae9e8+Value(Form::Giant)*FP::Size+FP::run_accel,100);g.Write16(0x7ae9e8+Value(Form::Giant)*FP::Size+FP::walk_speed,350);
        co_await Call(0x2006e4,{{NA_SE_SY_QUEST_CLEAR}});s.initialised=true;
    }
    if(type!=kStatePlay)co_return 0;s.gctx=state;
    co_await CalcCamera();co_await LinkCalc();FixTime();co_await Fixes();
    UiGearScreenUpdate(g,state);if(auto close=UiOcarinaScreenUpdate(g,state)){co_await Direct(close->Function,{{close->Layout}});co_await Call(0x2006e4,{{NA_SE_SY_DECIDE}});}
    co_await ScheduleUpdate();co_return 0;
}
} // namespace Mm3dRestoration
