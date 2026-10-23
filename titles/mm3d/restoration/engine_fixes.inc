// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration source/rst/camera.cpp, fixes.cpp and fixes/time.cpp.
namespace Mm3dRestoration {
Routine Engine::CalcCamera() {
    namespace Cam=Layout::Camera;namespace CS=Layout::CameraStateInfo;namespace CM=Layout::CameraModeInfo;
    constexpr auto ocarina=0x667894+Value(Enum::Camera::CameraState::ITEM1)*CS::Size;
    if(!s.camera_initialised) {
        const auto mode=g.Read32(ocarina+CS::modes)+31*CM::Size;
        g.Write16(mode+CM::handler_fn_idx,0x3d);g.Write16(mode+CM::field_2,2);g.Write32(mode+CM::field_4,0x6645c8);s.camera_initialised=true;
    }
    const u32 idx=g.Read16(s.gctx+G::camera_idx),camera=s.gctx+G::main_camera+idx*Cam::Size;
    const bool use=idx==1&&g.Read16(camera+Cam::state)==Value(Enum::Camera::CameraState::ITEM1);
    Bits(ocarina+CS::allowed_modes,1u<<31,use);
    const float x=g.Float(s.gctx+G::pad_state__c_stick__x_raw_last),y=g.Float(s.gctx+G::pad_state__c_stick__y_raw_last);
    if(use&&g.Read16(camera+Cam::mode)==0&&x*x+y*y>=5.198f)co_await Call(0x18b31c,{{camera,31}});
    co_return 0;
}
Routine Engine::ChangeCamera() {
    namespace Cam=Layout::Camera;auto& cpu=context.Saved;
    const auto camera=cpu.r[0],requested=cpu.r[1],arg=cpu.r[2];bool locked=false;
    if(s.dekui) {
        auto probe=cpu;probe.r[14]=Session::StoneDelay1;probe.r[15]=0;
        s.dekui(0xffffaf,probe,g.Bus());locked=probe.r[15]==probe.r[14];
    }
    // dekui DialogueCamera changes the range 9..11 to 9..10 (TALK excluded).
    bool change=!locked&&(requested==31||requested==9||requested==10);
    const auto game=g.Read32(camera+Cam::gctx);
    if(!game||g.Read32(game+G::pad_state__input__new_buttons)&Button::L)change=false;
    if(change&&g.Read16(camera+Cam::mode)!=31) {
        const float x=g.Float(game+G::pad_state__c_stick__x_raw_last),y=g.Float(game+G::pad_state__c_stick__y_raw_last);change=x*x+y*y>=5.198f;
    }
    cpu.r[4]=camera;cpu.r[5]=change?31:requested;cpu.r[6]=arg;
    cpu.r[0]=camera;cpu.r[1]=cpu.r[5];cpu.r[2]=arg;
    // CMP result,#1 in the donor trampoline; the bridge then replays ADD r4.
    const u32 value=change?1:0;const u32 result=value-1;
    cpu.cpsr=(cpu.cpsr&0x0fffffff)|(result&0x80000000)|(result==0?0x40000000:0)|(value>=1?0x20000000:0);
    if(s.dekui)s.dekui(0xffffb3,cpu,g.Bus());else cpu.r[4]=camera+0x100;
    co_return 0;
}
bool Engine::SlowTime() {
    if(!s.gctx||s32(g.Read32(Common+C::save__extra_time_speed))!=-1)return false;
    const auto entrance=g.Read32(Common+C::sub1__entrance)&0xff00;
    if(entrance==0xd600&&Find(Value(Enum::Actor::Type::Npc),Value(Enum::Actor::Id::NpcOldLady)))return false;
    const auto time=g.Read16(Common+C::save__time);
    if(entrance==0x6400&&g.Read32(Common+C::save__day)==1&&time>=u16(65536.*2/24)&&time<=u16(65536.*5.25/24))return false;
    if(auto kafei=Find(Value(Enum::Actor::Type::Npc),Value(Enum::Actor::Id::NpcKafei)))if(g.Read32(kafei+A::draw_fn))return false;
    return true;
}
void Engine::UpdateTime() {
    if(SlowTime()&&g.Read32(s.gctx+G::frame_counter)%3==0)g.Write16(Common+C::save__time,u16(g.Read16(Common+C::save__time)-1));
    g.Write16(Common+C::time_copy,g.Read16(Common+C::save__time));
}
float Engine::MoveNpc(u32 pointer,float speed) {
    if(!pointer)return speed;auto& entry=s.npc_positions[pointer];const float value=g.Float(pointer);
    if(entry.last<value||value==0)entry.last=value;entry.ticks=0;
    const s32 base=g.Signed16(Static+Layout::StaticContext::time_speed),extra=s32(g.Read32(Common+C::save__extra_time_speed));
    return float(base+extra)==speed&&SlowTime()?base*(1.f/3.f):speed;
}
void Engine::FixTime() {
    if(g.Read16(s.gctx+G::ocarina_state)==Value(Enum::Context::OcarinaState::GoingBackInTime))s.going_back_in_time=true;
    for(auto it=s.npc_positions.begin();it!=s.npc_positions.end();) {
        auto& [address,entry]=*it;if(entry.ticks>=60){it=s.npc_positions.erase(it);continue;}
        const auto value=g.Float(address);if(std::isfinite(entry.last)&&entry.last>=float(INT32_MIN)&&entry.last<float(INT32_MAX)&&value!=entry.last&&value==int(entry.last))g.Float(address,entry.last);
        ++entry.ticks;++it;
    }
}
Routine Engine::Fixes() {
    using Id=Enum::Actor::Id;using Type=Enum::Actor::Type;
    for(auto a=First(Value(Type::Prop)),n=0u;a&&n<4096;a=g.Read32(a+A::next),++n)if(g.Read16(a+A::id)==Value(Id::BgSeaFreezablePoint))g.Float(a+A::pos__pos__y,-100000.f);
    for(auto a=First(1),n=0u;a&&n<4096;a=g.Read32(a+A::next),++n)if(g.Read16(a+A::id)==Value(Id::ObjRailLift)&&g.Read32(a+0x20c)==Code(0x3113c8))g.Write32(a+0x20c,Code(0x58f1f4));
    const auto rosa=Find(Value(Type::Npc),Value(Id::NpcRosaSisters)),player=Player();bool near=false;
    if(rosa&&player){float distance=0;for(unsigned i=0;i<3;++i){const float d=g.Float(player+A::pos__pos+4*i)-g.Float(rosa+A::pos__pos+4*i);distance+=d*d;}near=distance<=40000;}
    bool bombers=false;namespace B=Layout::act_NpcBombers;
    for(auto a=First(Value(Type::Npc)),n=0u;a&&n<4096;a=g.Read32(a+A::next),++n)if(g.Read16(a+A::id)==Value(Id::NpcBombers)) {
        bombers=true;const auto number=g.Read16(a+B::number);if(number>=s.bomber_data.size())continue;
        auto& old=s.bomber_data[number];const auto ptr=g.Read32(a+B::notebook_data);
        if((near&&!old)||(!near&&old&&!ptr)){g.Write32(a+B::notebook_data,old);old=ptr;}
    }
    if(!bombers)s.bomber_data.fill(0);
    if(auto stone=Find(Value(Type::Prop),Value(Id::NpcHintStone)))if(!HasItem(Value(Enum::Items::ItemId::DekuMask),true))for(u32 field:{0x4acu,0x4c8u,0x4ccu})g.Float(stone+field,0);
    const u32 camera=s.gctx+G::main_camera;
    if(player&&g.Read16(camera+Layout::Camera::mode)==31&&(g.Read32(s.gctx+G::pad_state__input__buttons)&Button::L)) {
        using F=Enum::Player::Flag3;using M=Enum::Camera::CameraMode;std::optional<M> mode;
        if(Flag(player,P::flags3,Value(F::ZoraFastSwimming)))mode=Flag(player,P::flags1,Value(Enum::Player::Flag1::InWater))?M::GORONDASH:M::FREEFALL;
        else if(Flag(player,P::flags3,Value(F::GoronRolling)))mode=(g.Read16(player+A::flags_94)&Value(Enum::Actor::Flag94::Grounded))?M::GORONDASH:M::GORONJUMP;
        if(mode)co_await Call(0x18b31c,{{camera,Value(*mode)}});
    }co_return 0;
}
Routine Engine::Owl() {
    if(!s.gctx)co_return 0;const auto p=Player(),owl=Find(Value(Enum::Actor::Type::Item),Value(Enum::Actor::Id::ObjOwlStatue));if(!p||!owl)co_return 0;
    namespace O=Layout::act_ObjOwlStatue;int timer=g.Signed8(owl+O::activation_timer);if(timer>1)--timer;
    const bool activated=(g.Read16(Common+C::save__player__owl_statue_flags)&(1u<<(g.Read16(owl+A::params)&15)))!=0;
    const u8 flags=g.Read8(owl+O::col_body__info__flags1);g.Write8(owl+O::col_body__info__flags1,flags&~2);
    if(!activated&&(flags&2)&&timer==0)timer=5;
    if(timer==1){if(g.Signed8(p+P::pending_cutscene_id)>=0)g.Write8(p+P::pending_cutscene_id,0xff);g.Write32(p+P::talk_actor,owl);co_await Call(0x2209a8,{{s.gctx,owl,0}});timer=-1;}
    g.Write8(owl+O::activation_timer,u8(timer));co_return 0;
}
} // namespace Mm3dRestoration
