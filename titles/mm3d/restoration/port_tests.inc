// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic guest-memory and coroutine fixtures; never runs the game.
#include "engine.h"
#include "sites.h"
#include "substitute.h"
#include "hud_adjustments.h"
#include "prices.h"
#include "single_screen_text.h"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
namespace RestorationPortTests {
using namespace Mm3dRestoration;using namespace Oot3dNativeGame;
namespace a32=oot3d::recomp::a32;
inline void Check(bool condition,std::string_view message){if(!condition)throw std::runtime_error(std::string(message));}
struct Memory:a32::MemoryBus {
    std::map<u32,u8> bytes;std::set<u32> missing;unsigned writes=0;
    bool Read32(u32 at,u32* value) override {if(missing.contains(at))return false;*value=0;for(unsigned i=0;i<4;++i){auto it=bytes.find(at+i);if(it!=bytes.end())*value|=u32(it->second)<<(8*i);}return true;}
    bool Write32(u32 at,u32 value) override {if(missing.contains(at))return false;for(unsigned i=0;i<4;++i)bytes[at+i]=u8(value>>(8*i));++writes;return true;}
};
struct Fixture {
    static Mm3dGuestCallContext& Setup(Mm3dGuestCallContext& ctx,Memory& memory){ctx.Memory=&memory;return ctx;}
    Memory memory;Guest g{memory};Session session;CallMachine machine;Mm3dGuestCallContext ctx;Engine e{session,machine,Setup(ctx,memory)};
    std::vector<CallMachine::Request> calls;std::function<CallMachine::Result(const CallMachine::Request&)> reply;
    u32 allocated=0x09000000;static constexpr u32 game=0x08100000,player=0x08200000,pad=0x08300000;
    Fixture(){ctx.Memory=&memory;ctx.Saved.r[13]=0x0ff00000;ctx.Saved.r[14]=0x123456;ctx.Resume=0x123460;}
    void Play(){session.gctx=game;g.Write8(game+G::type,kStatePlay);g.Write32(game+G::actors__lists+2*Layout::ActorList::Size+Layout::ActorList::first,player);g.Write32(player+P::controller_info__state,pad);g.Write8(player+P::active_form,4);g.Write8(Common+C::save__player_form,4);g.Write32(CommonLayouts+8,0x08400000);for(unsigned i=0;i<24;++i){g.Write8(Common+C::save__inventory__items+i,255);g.Write8(Common+C::save__inventory__masks+i,255);}}
    CallMachine::Result Default(const CallMachine::Request& req) {
        CallMachine::Result r{};
        if(req.entry==Code(0x1dfb88)){r.R0=allocated;allocated+=(req.args.R[1]+15)&~15u;}
        if(req.entry==Code(0x20a8f8))r.S0=std::bit_cast<u32>(.5f);
        if(req.entry==Code(0x1e0390)||req.entry==Code(0x224ef8)||req.entry==Code(0x220ea0))r.R0=1;
        if(req.entry==Code(0x240dd8))r.R0=30;
        if(req.entry==Code(0x1c6ad4))r.R0=20;
        if(req.entry==Code(0x22caac))r.R0=0x08f00000;
        return r;
    }
    u32 Run(Routine routine) {
        machine.leaf=routine.handle;unsigned steps=0;
        while(!routine.Done()){Check(++steps<10000,"coroutine did not terminate");machine.pending.reset();machine.leaf.resume();if(machine.pending){const auto req=*machine.pending;Check(req.entry!=0,"unmapped guest call");calls.push_back(req);machine.result=reply?reply(req):Default(req);}else Check(routine.Done(),"invalid routine suspension");}
        return routine.Result();
    }
    void Run(std::string_view action){Run(e.Run(action));}
    u32 Actor(Enum::Actor::Id id,Enum::Actor::Type type,u32 address=0x08700000){g.Write16(address+A::id,u16(id));g.Write32(game+G::actors__lists+Value(type)*Layout::ActorList::Size+Layout::ActorList::first,address);return address;}
};
inline void Schedule() {
    Fixture f;f.Play();auto& g=f.g;
    g.Write32(kItemsScreen,0x08500000);g.Write32(kScheduleScreen,0x08501000);g.Write32(kMapScreen,0x08502000);
    g.Write32(Fixture::game+G::pad_state__input__new_buttons,Button::Select);f.Run("UiScheduleTriggerHook");Check(g.Read32(kScreenContext+12)==0x08500000,"Select Items shortcut");
    g.Write32(kScreenContext+12,0);g.Write32(Fixture::game+G::pad_state__input__new_buttons,Button::Start);f.Run("UiScheduleTriggerHook");Check(g.Read32(kScreenContext+12)==0,"notebook ownership required");
    g.Write32(Common+C::save__inventory__collect_register,1u<<18);f.Run("UiScheduleTriggerHook");Check(g.Read32(kScreenContext+12)==0x08501000,"Start notebook shortcut");
    g.Write32(kScreenContext+12,0);g.Write32(Fixture::game+G::pad_state__input__buttons,Button::ZR);f.Run("UiScheduleTriggerHook");Check(g.Read32(kScreenContext+12)==0,"ZR Start remains disabled");
    g.Write32(Fixture::game+G::pad_state__input__buttons,Button::ZR|Button::Select);g.Write32(Fixture::game+G::pad_state__input__new_buttons,Button::Select);
    g.Write32(0x72f5c0,0x08600000);g.Write32(0x0860016c,Fixture::game);g.Write32(0x656b04,0x08800000);g.Write32(0x72f5c4,0x08800000);g.Write8(Fixture::game+Layout::State::status,2);
    f.Run("UiScheduleTriggerHook");Check(g.Read32(kScreenContext+12)==0&&f.calls.back().args.R[0]==0x0100077f,"minimap precedence and sound");Check(!(g.Read32(Fixture::game+G::pad_state__input__buttons)&Button::Select)&&g.Read32(Fixture::game+G::pad_state__input__new_buttons)==0,"Select consumed once");
    g.Write32(0x72f5c0,0);g.Write32(Fixture::game+G::pad_state__input__new_buttons,Button::Select);f.Run("UiScheduleTriggerHook");Check(g.Read32(kScreenContext+12)==0x08502000,"unsupported minimap context uses map");
    g.Write32(kScreenContext+8,0x08501000);g.Write32(kScreenContext+12,0);const auto screen=0x08501000u;
    g.Write32(Fixture::game+G::pad_state__input__new_buttons,Button::Y);f.Run(f.e.ScheduleUpdate());Check(g.Read32(screen+Layout::ui_ScheduleScreen::btn_press_flags)==Button::Y,"notebook Y flag");
    // NativeNotebook's Event Notes layout switches the map trigger to X.
    g.Write32(screen+0x40,0x08a00000);g.Write32(0x08a00000+4,0x08a01000);g.Write32(0x08a01000+0x18,0x08b00000);
    const std::string name="MenuQuestlist";for(unsigned i=0;i<=name.size();++i)g.Write8(0x08b00000+i,u8(i<name.size()?name[i]:0));
    g.Write32(screen+Layout::ui_ScheduleScreen::btn_map_anim_player,0x08c00000);g.Write32(0x08c00000+Layout::ui_AnimPlayer::m_anim,0x08d00000);g.Write32(screen+Layout::ui_ScheduleScreen::disp_btn_map_anim,0x08d00000);
    g.Write32(screen+Layout::ui_ScheduleScreen::touch_handler,0x08e00000);for(unsigned i=0;i<3;++i)g.Write32(0x08e00000+4*i,0x100000+4*i);
    g.Write32(Fixture::game+G::pad_state__input__new_buttons,Button::X);const auto before=f.calls.size();f.Run(f.e.ScheduleUpdate());Check(f.calls.size()==before+6,"Event Notes X map callbacks");
    Check(f.calls[before].entry==0x100000&&f.calls[before].args.S[0]==0&&f.calls[before+2].args.StackWords==1&&f.calls[before+2].args.Stack[0]==kScreenContext,"touch callback AAPCS registers");
}
inline u64 Hash(const std::vector<u8>& bytes){u64 h=0xcbf29ce484222325ULL;for(auto b:bytes)h=(h^b)*0x100000001b3ULL;return h;}
inline void PreparedMessages(const char* path) {
    std::ifstream input(path,std::ios::binary);
    Check(bool(input),"prepared message input missing");
    const std::string json((std::istreambuf_iterator<char>(input)),{});
    std::string error;
    const auto edits=Mm3dRestorationPort::ReadMessageEdits(json,&error);
    Check(edits.has_value(),error);
    Mm3dGuestCalls calls;Mm3dRestorationPort port(calls);
    Check(port.SetMessageEdits(*edits),"prepared message guards rejected");
    Check(edits->size()>=795,"five-locale dialogue/price table incomplete");
    for(const auto& edit:*edits) {
        Fixture f;auto& g=f.g;f.session.message_edits={edit};
        const u32 self=0x08001000,base=0x08002000,payload=base+36;
        g.Write32(self+4,base);g.Write32(self+8,base+16);g.Write32(self+12,36+u32(edit.original.size()));g.Write32(self+16,20);
        g.Write32(base,0x47534d47);g.Write32(base+4,1);g.Write32(base+8,1);g.Write32(base+12,1u<<edit.language);
        g.Write16(base+16,edit.id);g.Write32(base+20,edit.original_metadata.value_or(0));
        g.Write32(base+28,36);g.Write32(base+32,u32(edit.original.size()));
        for(size_t i=0;i<edit.original.size();++i)g.Write8(payload+u32(i),edit.original[i]);
        const auto bytesEqual=[&](const std::vector<u8>& expected){for(size_t i=0;i<expected.size();++i)if(g.Read8(payload+u32(i))!=expected[i])return false;return true;};
        Mm3dDekui::Prices::enabled=false;Mm3dDekui::SingleScreenText::enabled=true;f.e.EditMessage(self,edit.id);
        Check(bytesEqual(edit.price?edit.original:edit.edited),"price/single-screen toggle independence");
        Mm3dDekui::Prices::enabled=true;f.e.EditMessage(self,edit.id);f.e.EditMessage(self,edit.id);
        Check(bytesEqual(edit.edited),"prepared text apply/idempotence");
        if(edit.edited_metadata)Check(g.Read32(base+20)==*edit.edited_metadata,"prepared metadata apply");
        Mm3dDekui::Prices::enabled=false;Mm3dDekui::SingleScreenText::enabled=false;f.e.EditMessage(self,edit.id);
        Check(bytesEqual(edit.original),"prepared text restore");
        if(edit.original_metadata)Check(g.Read32(base+20)==*edit.original_metadata,"prepared metadata restore");
        g.Write32(base+12,1u<<((edit.language+1)%14));Mm3dDekui::Prices::enabled=true;Mm3dDekui::SingleScreenText::enabled=true;f.e.EditMessage(self,edit.id);
        Check(bytesEqual(edit.original),"foreign archive language modified");
    }
    Mm3dDekui::Prices::enabled=false;Mm3dDekui::SingleScreenText::enabled=false;
    std::cout<<"prepared message toggle/apply/restore/language fixtures: "<<edits->size()<<'\n';
}
inline void Messages() {
    Fixture f;const u32 self=0x08001000,base=0x08002000,out=0x08003000;auto& g=f.g;
    g.Write32(self+4,base);g.Write32(self+8,base+16);g.Write32(self+12,64);g.Write32(self+16,28);
    g.Write32(base,0x47534d47);g.Write32(base+4,1);g.Write32(base+8,1);g.Write32(base+12,6); // languages 1 and 2
    g.Write16(base+16,0x1962);g.Write16(base+18,7);g.Write32(base+20,0xffffffff);g.Write32(base+24,0xff3121);
    g.Write32(base+28,44);g.Write32(base+32,4);g.Write32(base+36,48);g.Write32(base+40,4);
    MessageEdit edit;edit.id=0x1962;edit.language=2;edit.original={1,2,3,4};edit.edited={5,6,7,8};edit.original_hash=Hash(edit.original);edit.edited_hash=Hash(edit.edited);
    for(unsigned i=0;i<4;++i){g.Write8(base+44+i,9);g.Write8(base+48+i,edit.original[i]);}
    Mm3dGuestCalls calls;Mm3dRestorationPort port(calls);Check(port.SetMessageEdits({edit}),"validated caller-owned edit");auto bad=edit;bad.edited_hash=0;Check(!port.SetMessageEdits({bad}),"incorrect edit hash rejected");Check(!port.SetMessageEdits({edit,edit}),"duplicate edit rejected");
    f.session.message_edits={edit};Mm3dDekui::SingleScreenText::enabled=true;
    Check(f.Run(f.e.MessageGet(self,0x195e,out))==1,"message remap and lookup");Check(g.Read32(out+Layout::Message::msgid)==0x195e,"original ID preserved");
    Check(g.Read32(out+Layout::Message::texts+2*Layout::MessageText::Size+4)==base+48,"language bitmask pointer");Check(g.Read32(base+48)==0x08070605&&g.Read32(base+44)==0x09090909,"only selected language edited");Check(f.calls.size()==14,"all language readers called");
    Mm3dDekui::SingleScreenText::enabled=false;f.Run(f.e.MessageGet(self,0x195e,out));Check(g.Read32(base+48)==0x04030201,"disabled setting restores original");
    g.Write8(base+48,99);Mm3dDekui::SingleScreenText::enabled=true;f.Run(f.e.MessageGet(self,0x195e,out));Check(g.Read8(base+48)==99,"mismatched message left intact");
    // Prepared prices change and restore BOTH text and native choice metadata.
    // A stale or foreign metadata word must prevent all writes.
    edit.price=true; edit.original_metadata=0xffffffff; edit.edited_metadata=0xfff00014;
    f.session.message_edits={edit}; g.Write32(base+48,0x04030201);
    g.Write32(base+20,0x12345678); Mm3dDekui::Prices::enabled=true;
    f.Run(f.e.MessageGet(self,0x195e,out));
    Check(g.Read32(base+48)==0x04030201&&g.Read32(base+20)==0x12345678,"foreign price metadata unchanged");
    g.Write32(base+20,*edit.original_metadata); f.Run(f.e.MessageGet(self,0x195e,out));
    Check(g.Read32(base+48)==0x08070605&&g.Read32(base+20)==*edit.edited_metadata,"price bytes and metadata applied");
    f.Run(f.e.MessageGet(self,0x195e,out));
    Check(g.Read32(base+48)==0x08070605,"price application idempotent");
    Mm3dDekui::Prices::enabled=false; f.Run(f.e.MessageGet(self,0x195e,out));
    Check(g.Read32(base+48)==0x04030201&&g.Read32(base+20)==*edit.original_metadata,"price bytes and metadata restored");
    g.Write32(self+16,20);Check(f.Run(f.e.MessageGet(self,0x195e,out))==0,"bad language stride rejected");
    Mm3dDekui::SingleScreenText::enabled=false;
}
inline void PlayerAndCamera() {
    Fixture f;f.Play();auto& g=f.g;auto& r=f.ctx.Saved.r;
    g.Write32(Fixture::game+G::pad_state__input__buttons,Button::A);f.Run("rst_link_ShouldUseZoraFastSwim");Check(r[0]==1,"default fast swim");
    g.Write32(Fixture::game+G::pad_state__input__new_buttons,Button::ZL);f.Run("rst_link_ShouldUseZoraFastSwim");Check(r[0]==0,"fast swim toggle");
    g.Write32(Fixture::game+G::pad_state__input__buttons,Button::A|Button::R|Button::ZR);f.Run("rst_link_ShouldUseZoraFastSwim");Check(r[0]==1,"R override precedes ZR");
    r[0]=Fixture::player;f.session.a_press_duration=14;g.Write32(Fixture::pad+Layout::pad_State::input__released_buttons,Button::A);f.Run("rst_link_ShouldEndGoronRoll");Check(r[0]==0,"short roll release ignored");r[0]=Fixture::player;f.session.a_press_duration=15;f.Run("rst_link_ShouldEndGoronRoll");Check(r[0]==1,"held roll release ends");
    r[0]=Fixture::game;r[1]=Fixture::player;r[2]=1;g.Write16(Fixture::player+P::zora_swim_a_press_duration,7);f.Run("rst_link_SwitchToZoraFastSwim");Check(r[0]==1&&f.calls.back().entry==Code(0x220ea0),"delayed swim switch");
    using I=Enum::Items::ItemId;g.Write8(Common+C::save__inventory__masks, u8(I::GoronMask));g.Write32(Fixture::pad+Layout::pad_State::input__new_buttons,Button::Up);f.Run("TriggerItemUseHook");Check(g.Read8(Fixture::player+P::action)==Value(Enum::Items::Action::GoronMask),"fast Goron transform");
    g.Write32(Fixture::player+P::flags1,Value(Enum::Player::Flag1::InWater));g.Write8(Fixture::player+P::action,0);f.Run("TriggerItemUseHook");Check(g.Read8(Fixture::player+P::action)==0,"Goron transform blocked in water");
    g.Write32(Fixture::player+P::flags1,0);g.Write8(Common+C::save__inventory__items,1);g.Write8(Common+C::save__inventory__items+1,2);g.Write8(Common+C::save__player__magic,8);
    g.Write8(Fixture::player+P::current_action,9);g.Write8(Fixture::player+P::action_type,3);g.Write8(Fixture::player+P::item_btn_slot,2);g.Write32(Fixture::game+G::pad_state__input__new_buttons,Button::ZL);
    f.Run(f.e.FastArrow(Fixture::player));Check(f.session.arrow_action==10&&g.Read8(Common+C::save__equipment__data+2)==2,"fast arrow changes exact equipped slot");
    const u32 camera=Fixture::game+G::main_camera;g.Write32(camera+Layout::Camera::gctx,Fixture::game);g.Write16(camera+Layout::Camera::mode,31);g.Write32(Fixture::game+G::pad_state__input__new_buttons,0);
    r[0]=camera;r[1]=11;r[2]=3;f.Run("rst_CameraChangeModeExHook");Check(r[1]==11&&r[4]==camera+0x100,"dialogue mode excluded from free camera");
    std::vector<u32> svc;f.session.dekui=[&](u32 id,a32::GuestState& cpu,a32::MemoryBus&){svc.push_back(id);if(id==0xffffaf){cpu.r[0]=0;cpu.r[15]=cpu.r[14];}else cpu.r[4]=cpu.r[0]+0x100;};
    r[0]=camera;r[1]=9;f.Run("rst_CameraChangeModeExHook");Check(r[1]==9&&svc==std::vector<u32>({0xffffaf,0xffffb3}),"shared camera bridge order and lock");
}
inline void TimeAndActors() {
    Fixture f;f.Play();auto& g=f.g;g.Write32(Common+C::save__extra_time_speed,~0u);g.Write16(Static+Layout::StaticContext::time_speed,2);g.Write16(Common+C::save__time,100);g.Write32(Fixture::game+G::frame_counter,3);f.Run("UpdateTimeHook");Check(g.Read16(Common+C::save__time)==99&&g.Read16(Common+C::time_copy)==99,"one-third inverted time");
    g.Float(0x08001000,4.5f);Check(std::abs(f.e.MoveNpc(0x08001000,1)-2.f/3)<.0001f,"scheduled NPC speed");g.Float(0x08001000,4);f.e.FixTime();Check(g.Float(0x08001000)==4.5f,"NPC truncated fraction restored");for(int i=0;i<60;++i)f.e.FixTime();Check(f.session.npc_positions.empty(),"stale NPC entry expires");
    using Id=Enum::Actor::Id;using Ty=Enum::Actor::Type;
    auto ice=f.Actor(Id::BgSeaFreezablePoint,Ty::Prop);f.Run(f.e.Fixes());Check(g.Float(ice+A::pos__pos__y)==-100000,"ice sparkles hidden");
    auto platform=f.Actor(Id::ObjRailLift,Ty::Background,0x08701000);g.Write32(platform+0x20c,Code(0x3113c8));f.Run(f.e.Fixes());Check(g.Read32(platform+0x20c)==Code(0x58f1f4),"Deku platform moves without rider");
    auto owl=f.Actor(Id::ObjOwlStatue,Ty::Item,0x08702000);g.Write8(owl+Layout::act_ObjOwlStatue::col_body__info__flags1,2);g.Write8(Fixture::player+P::pending_cutscene_id,7);for(unsigned i=0;i<5;++i)f.Run("PreActorCalcHook");Check(g.Signed8(owl+Layout::act_ObjOwlStatue::activation_timer)==-1&&g.Signed8(Fixture::player+P::pending_cutscene_id)==-1,"owl delay and cutscene clear");
    f.ctx.Saved.r[0]=platform;f.Run("StoneDelay1");Check(g.Read16(platform+Layout::act_ObjStoneTowerBlock::timer)==6,"stone delay initialized");for(unsigned i=0;i<6;++i)f.Run("StoneDelay2");Check(g.Read32(platform+Layout::act_ObjStoneTowerBlock::action_fn)==Code(0x2ac52c),"stone movement callback");
}
inline void BossesAndSaving() {
    Fixture f;f.Play();auto& g=f.g;using Id=Enum::Actor::Id;using Ty=Enum::Actor::Type;
    auto boss=f.Actor(Id::BossOdolwa,Ty::Boss);f.ctx.Saved.r[0]=boss;
    g.Write32(boss+Layout::act_BossOdolwa::odolwa_calc_prev,Code(0x308484));f.Run("rst_OdolwaGetWaitDuration");Check(f.ctx.Saved.r[0]==90,"Odolwa intro wait");
    f.ctx.Saved.r[0]=boss;g.Write8(boss+A::life,10);f.Run("rst_OdolwaShouldMoveToPhase2");Check(f.ctx.Saved.r[0]==1,"Odolwa phase transition");
    const auto collision=boss+Layout::act_BossOdolwa::collision__info;g.Write8(collision+Layout::CollisionInfo::flags1,2);g.Write8(boss+A::damage_type,u8(Enum::Actor::DamageType::IceArrow));
    f.Run(f.e.OdolwaCollision(boss));Check(g.Read16(boss+Layout::act_BossOdolwa::timer)==60&&g.Read32(boss+Layout::act_BossOdolwa::odolwa_calc)==Code(0x56655c),"Odolwa ice freeze");
    boss=f.Actor(Id::BossGoht,Ty::Boss);f.Run("PostActorCalcHook");Check(g.Float(boss+Layout::act_BossGoht::eye_scale+8)==.05f&&(g.Read8(boss+Layout::act_BossGoht::goht_flags)&0x40),"Goht eye and cutscene fixes");
    f.ctx.Saved.r[0]=boss;g.Write8(boss+Layout::act_BossGoht::collision__info+Layout::CollisionInfo::flags1,2);f.Run("rst_IsGohtCollided");Check(f.ctx.Saved.cpsr&0x40000000,"Goht collision CMP result");
    boss=f.Actor(Id::BossGyorg,Ty::Boss);g.Write32(boss+Layout::act_BossGyorg::gyorg_calc,Code(0x557900));g.Write32(Fixture::player+P::state_handler_fn,Code(0x20d96c));g.Write32(Fixture::player+P::flags1,Value(Enum::Player::Flag1::FreezeLink));f.Run("PostActorCalcHook");Check(!(g.Read32(Fixture::player+P::flags1)&Value(Enum::Player::Flag1::FreezeLink))&&g.Read8(boss+Layout::act_BossGyorg::eyeball_flags)==0,"Gyorg revival recovery");
    boss=f.Actor(Id::BossTwinmold,Ty::Boss);g.Write8(boss+A::life,12);g.Write16(boss+Layout::act_BossTwinmold::hit_counter,9);f.Run("PostActorCalcHook");g.Write16(boss+Layout::act_BossTwinmold::hit_counter,8);f.Run("PostActorCalcHook");g.Write16(boss+Layout::act_BossTwinmold::hit_counter,9);g.Write8(boss+A::life,10);f.Run("PostActorCalcHook");Check(g.Signed8(boss+A::life)==2&&g.Read16(boss+Layout::act_BossTwinmold::hit_counter)==8,"Twinmold damage and persistent hit count");
    f.ctx.Saved.r[6]=Fixture::game;g.Write8(Fixture::game+G::type,10);f.Run("rst_GameStateGetNextStateInfo");Check(f.ctx.Saved.r[8]==f.session.sot_info&&g.Read32(f.session.sot_info+Layout::StateInfo::init_fn)==Session::SotInit,"extended save state info");
    f.ctx.Saved.r[0]=0x08900000;f.session.going_back_in_time=false;f.Run("SotInit");Check(g.Read8(0x08900000+Layout::State::type)==7,"ordinary telop bypasses save");
    f.session.going_back_in_time=true;f.Run("SotInit");Check(!f.session.going_back_in_time&&g.Read32(0x08900000+Layout::State::calc_fn)==Session::SotCalc,"song-of-time custom calc retained");
    f.Run("SotCalc");Check(g.Read32(0x08900000+Layout::StateSotSave::sotsave_status)==1,"save initialization advances");
    f.Run("SotCalc");Check(g.Read32(0x08900000+Layout::StateSotSave::sotsave_status)==2,"save package load advances");
    const auto text=0x08a00000u,arrow=0x08b00000u;g.Write32(0x08900000+Layout::StateSotSave::text_state,text);g.Write32(0x08900000+Layout::StateSotSave::branch_arrow,arrow);g.Write32(text+Layout::ui_MessageTextState::num_choices,2);
    f.Run("SotCalc");Check(g.Read32(0x08900000+Layout::StateSotSave::message_choice)==1&&g.Float(arrow+Layout::ui_Widget::pos+Layout::ui_WidgetPos::translate__y)==28,"save prompt defaults to do not save");
    g.Write32(0x08900000+Layout::StateSotSave::ending_status,12);f.Run("SotCalc");Check(g.Read8(0x08900000+Layout::State::type)==7,"saved prompt closes to telop");
}
inline void HudLifecycle() {
    Fixture f;f.Play();auto& g=f.g;
    const u32 old_layout=0x08500000,touch=0x08600000,hud=0x08700000,set=0x08800000,dpad=0x08900000;
    u32 storage=0x0a000000;
    const auto tree=[&](u32 layout,std::initializer_list<const char*> names) {
        std::map<std::string,u32> result;const auto root=layout+Layout::ui_Layout::root_widget,list=storage;storage+=0x100;
        g.Write32(root+Layout::ui_Widget::widgets__data,list);g.Write32(root+Layout::ui_Widget::widgets__size,u32(names.size()));
        unsigned index=0;
        for(const char* name:names){const auto widget=storage;storage+=0x1000;const auto text=widget+0x800;
            g.Write32(list+4*index++,widget);g.Write32(widget+Layout::ui_Widget::name,text);
            for(unsigned n=0;n<=std::char_traits<char>::length(name);++n)g.Write8(text+n,u8(name[n]));
            g.Float(widget+Layout::ui_Widget::pos+Layout::ui_WidgetPos::color+12,1);result.emplace(name,widget);
        }return result;
    };
    const auto t=tree(touch,{"item_button_g","non_essential_g","heart_g","item_icon_set","icon_item_oca_g","hud_dpad"});
    tree(hud,{"action_button_g","timer_g","a_action_button","b_action_button"});
    tree(set,{"icon_item_1_g","icon_item_2_g","icon_item_x_g","icon_item_y_g"});tree(dpad,{"icon_l","icon_r","icon_u","icon_d"});
    g.Write32(t.at("item_icon_set")+Layout::ui_Widget::layout,set);g.Write32(t.at("hud_dpad")+Layout::ui_Widget::layout,dpad);
    // hud_dpad is nested below item_button_g in the donor layout.
    g.Write32(t.at("item_button_g")+Layout::ui_Widget::widgets__data,0x0be00000);g.Write32(0x0be00000,t.at("hud_dpad"));g.Write32(t.at("item_button_g")+Layout::ui_Widget::widgets__size,1);
    g.Write32(CommonLayouts,old_layout);g.Write32(CommonLayouts+8,hud);f.ctx.Saved.r[0]=0x0bf00000;f.ctx.Saved.r[1]=kScreenContext;
    g.Write32(0x0bf00000,0x00650000);g.Write32(0x0bf00000+H::vtable_4,0x00651000);
    unsigned native_inits=0;
    f.reply=[&](const CallMachine::Request& req){auto result=f.Default(req);
        if(req.entry==Code(0x16a014))result.R0=19;
        if(req.entry==Code(0x16962c)){Check(req.args.R[0]==0x7cdc9c&&req.args.R[1]==19&&req.args.R[2]==0x30000,"HUD layout construction contract");result.R0=touch;}
        if(req.entry==Code(0x599d20)){
            ++native_inits;Check(g.Read32(CommonLayouts)==touch&&req.args.R[0]==f.session.hud&&req.args.R[1]==kScreenContext,"HUD init temporarily binds its layout");
            g.Write32(req.args.R[0]+H::playTouchPanel,touch);
            const auto outer=f.machine.leaf;const auto calls=f.calls.size();f.Run("MainScreen_Open");f.machine.leaf=outer;Check(calls==f.calls.size(),"HUD native helper reentrancy bypass");
        }return result;
    };
    f.Run("MainScreen_Init");Check(native_inits==1&&f.session.hud&&g.Read32(CommonLayouts)==old_layout,"HUD allocation and layout restore");
    Check(g.Read32(f.session.hud)==0x00650000&&g.Read8(f.session.hud+H::magic)==255,"HUD constructor defaults");
    for(auto widget:f.session.hud_widgets)Check(widget!=0,"HUD named widget resolved");
    Check(f.session.hud_widgets[3]==t.at("heart_g")&&f.session.hud_widgets[12]==t.at("hud_dpad"),"HUD hierarchy ownership");
    for(unsigned i=0;i<5;++i)Check(g.Float(f.session.hud_widgets[i]+Layout::ui_Widget::pos+Layout::ui_WidgetPos::color+12)==0,"HUD initial fade");
    const auto object=f.session.hud;f.Run("MainScreen_Init");Check(f.session.hud==object&&native_inits==2,"HUD storage reused across scene init");
    f.Run("MainScreen_Open");Check(f.calls.back().entry==Code(0x59b4d4)&&f.calls.back().args.R[0]==object,"HUD native open");
    f.Run("MainScreen_Draw");Check(f.calls.back().entry==Code(0x17ec58)&&f.calls.back().args.R[1]==touch&&f.calls.back().args.R[2]==2&&f.calls.back().args.R[3]==10,"HUD top draw priority");
    f.Run("MainScreen_CalcClose");Check(f.calls.back().entry==Code(0x59b3cc),"HUD native close");
    // Without Restoration's layouts in the RomFS: no Hud2, and its native
    // lifecycle is never called on the unbuilt object.
    f.reply=[&](const CallMachine::Request& req){auto result=f.Default(req);
        if(req.entry==Code(0x16a014))result.R0=kNoResource;
        Check(req.entry!=Code(0x16962c),"a missing layout was constructed");return result;};
    f.Run("MainScreen_Init");const auto before=f.calls.size();
    for(const char* action:{"MainScreen_Open","MainScreen_Calc","MainScreen_Draw","MainScreen_CalcClose"})f.Run(action);
    Check(!f.session.hud_ready&&f.calls.size()==before,"HUD without its layout stays closed");
}
inline void HudAndSong() {
    Fixture f;f.Play();auto& g=f.g;f.session.hud=0x08800000;f.session.hud_ready=true;for(unsigned i=0;i<17;++i)f.session.hud_widgets[i]=0x08900000+i*0x1000;
    g.Write32(kScreenContext+4,0x08b00000);g.Write32(0x08b00000+Layout::ui_Context::game_state,Fixture::game);
    for(auto w:f.session.hud_widgets){g.Write32(w+Layout::ui_Widget::layout,w+0x200);g.Float(w+Layout::ui_Widget::pos+Layout::ui_WidgetPos::color+12,1);}
    g.Write32(f.session.hud+H::playTouchPanel,0x08c00000);g.Write32(Fixture::game+G::pad_state__input__buttons,Button::ZR);f.ctx.Saved.r[1]=kScreenContext;
    f.Run("MainScreen_Calc");for(unsigned i=5;i<=8;++i)Check(g.Read32(f.session.hud_widgets[i]+Layout::ui_Widget::pos+Layout::ui_WidgetPos::flags)&1,"all four HUD icons visible");
    Check(!(g.Read32(f.session.hud_widgets[9]+Layout::ui_Widget::pos+Layout::ui_WidgetPos::flags)&1)&&(g.Read32(f.session.hud_widgets[10]+Layout::ui_Widget::pos+Layout::ui_WidgetPos::flags)&1),"ZR does not replace A display");
    const auto player=f.session.hud+H::magic_use;g.Write32(player,0x08d00000);g.Write32(f.session.hud+H::magic_use_ani,0x08d10000);g.Write32(f.session.hud+H::gaugeYellow_g,0x08d20000);
    f.Run("MainScreen_MagicStartUseAnim");Check(g.Read8(0x08d00000+Layout::ui_AnimPlayer::m_playing)&&g.Read8(0x08d00000+Layout::ui_AnimPlayer::m_loop),"HUD magic looping animation");
    f.Run("MainScreen_MagicPlayAndHideYellowGauge");Check(f.calls.back().args.S[0]==std::bit_cast<u32>(std::numeric_limits<float>::infinity()),"magic hide plays last frame");
    const auto window=0x08e00000u;const auto song=u16(Enum::Context::OcarinaSong::SongOfSoaring);
    Check(f.Run(f.e.OcarinaSong(window,song))==0,"first song replay retained");Check(f.Run(f.e.OcarinaSong(window,song))==1&&g.Read16(Fixture::game+G::ocarina_state)==Value(Enum::Context::OcarinaState::PlayingAndReplayDone),"repeat soaring skips replay");
    f.ctx.Saved.r[4]=window;f.ctx.Saved.r[0]=song;const auto sp=f.ctx.Saved.r[13];for(unsigned i=0;i<9;++i)g.Write32(sp+0x6c+4*i,0xabc000+i);f.Run("rst_HandleOcarinaSong");Check(f.ctx.Resume==0xabc008&&f.ctx.Saved.r[4]==0xabc000&&f.ctx.Saved.r[13]==sp+0x90,"song trampoline stack unwind");
}
inline void EveryHook() {
    std::set<std::string_view> actions;
    for(const auto& site:SourceSites) {
        if(site.kind==SiteKind::Patch) {
            Fixture f;for(unsigned i=0;i<16;++i)f.ctx.Saved.r[i]=0x09000000+0x1000*i;
            for(u32 flags:{0u,0x40000000u,0xf0000000u}){f.ctx.Saved.cpsr=flags;Check(Substitute(site.replacement,site.pc,f.ctx.Saved,f.memory).has_value(),"HKS replacement not executable");}
            continue;
        }
        if(!actions.insert(site.action).second)continue;
        Fixture f;f.Play();f.ctx.Saved.r[0]=Fixture::player;f.ctx.Saved.r[1]=Fixture::game;f.ctx.Saved.r[4]=Fixture::player;f.ctx.Saved.r[6]=Fixture::game;
        if(site.action=="Calc")f.ctx.Saved.r[0]=Fixture::game;
        if(site.action=="BActionIconUpdate"){f.g.Write32(Fixture::player,0x08f00000);f.g.Write32(0x08f00000+40,0x00100024);}
        if(site.action=="rst_HandleOcarinaSong")f.ctx.Saved.r[0]=0;
        f.Run(site.action);
    }
    Fixture pre;pre.ctx.Saved.r[0]=0x12345678;pre.ctx.Saved.r[9]=0x08001000;Substitute(0xe1c90ab2,0x172908,pre.ctx.Saved,pre.memory);Check(pre.g.Read16(0x080010a2)==0x5678,"pre-hook displaced STRH");
    std::cout<<"Restoration action fixtures: "<<actions.size()<<"; all 108 instruction sites executed or dispatched\n";
}
inline void Registry(const char* image_path) {
    Memory memory;Guest g(memory);Mm3dGuestCalls calls;Mm3dRestorationPort port(calls);Mm3dGuestPatches patches;calls.Install(patches);
    for(const auto& site:SourceSites)for(unsigned i=0;i<site.guard_count;++i)g.Write32(site.guards[i].first,site.guards[i].second);
    Check(!port.Capabilities(memory).message_getter,"unregistered port is not a capability");port.Register(patches);
    Check(!port.Capabilities(memory).message_getter,"unverified port is not a capability");
    Check(patches.Verify(memory)==SourceSites.size()+6,"all synthetic guards enabled");unsigned mutations=0;
    for(const auto& site:SourceSites)for(unsigned i=0;i<site.guard_count;++i) {
        const auto [at,value]=site.guards[i];g.Write32(at,value^1);patches.Verify(memory);auto stats=patches.Statistics();const auto it=std::find_if(stats.begin(),stats.end(),[&](const auto& s){return s.Pc==site.pc;});Check(it!=stats.end()&&!it->Enabled,"corrupt guard accepted");g.Write32(at,value);++mutations;
        memory.missing.insert(at);patches.Verify(memory);stats=patches.Statistics();const auto missing=std::find_if(stats.begin(),stats.end(),[&](const auto& s){return s.Pc==site.pc;});Check(!missing->Enabled,"missing guard accepted");memory.missing.erase(at);++mutations;
    }
    patches.Verify(memory);Check(port.Capabilities(memory).message_getter&&!port.Capabilities(memory).camera_bridge,"camera capability needs bridge");port.SetDekuiBridge([](u32,a32::GuestState&,a32::MemoryBus&){});Check(port.Capabilities(memory).camera_bridge,"bound camera contract");
    auto expected=port.RegistryBoundaries(),actual=patches.BlockBoundaries();Check(expected==actual,"registry boundaries match runtime");
    for(const auto& d:DataPatches)for(unsigned i=0;i<d.size;++i)g.Write8(d.pc+i,d.original[i]);Check(port.ApplyDataPatches(memory)==15,"all guarded data patches");Check(port.ApplyDataPatches(memory)==0,"data patches idempotent");
    for(const auto& site:SourceSites)if(site.action=="MessageDataGet"){g.Write32(site.pc,0);Check(!port.Capabilities(memory).message_getter,"message capability retains native proof");g.Write32(site.pc,site.word);}
    std::ifstream image(image_path?image_path:"",std::ios::binary);if(image){const std::vector<u8> bytes{std::istreambuf_iterator<char>(image),{}};Check(bytes.size()>0x500000,"original code image too short");
        for(const auto& site:SourceSites)for(unsigned i=0;i<site.guard_count;++i){const auto [at,value]=site.guards[i];Check(at>=0x100000&&u64(at-0x100000)+4<=bytes.size(),"guard outside image");u32 word=0;for(unsigned n=0;n<4;++n)word|=u32(bytes[at-0x100000+n])<<(8*n);Check(word==value,"native image guard mismatch");}
        for(const auto& d:DataPatches)for(unsigned i=0;i<d.size;++i)Check(bytes[d.pc-0x100000+i]==d.original[i],"original data guard mismatch");
        std::cout<<"Original EUR image: all 108 sites / 15 data guards match\n";
    }else std::cout<<"SKIP original image (not supplied)\n";
    std::cout<<"Guard mutations/missing: "<<mutations<<"; registry boundaries: "<<expected.size()<<"\n";
}
inline void All(const char* image){Schedule();Messages();PlayerAndCamera();TimeAndActors();BossesAndSaving();HudLifecycle();HudAndSong();EveryHook();Registry(image);}
} // namespace RestorationPortTests
