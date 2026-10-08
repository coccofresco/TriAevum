// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration source/rst/fixes/boss.cpp and game/{as,collision,sound}.cpp.
namespace Mm3dRestoration {
namespace O=Layout::act_BossOdolwa;
namespace CI=Layout::CollisionInfo;
inline void OdolwaState(Guest& g,u32 boss,u32 ref){g.Write32(boss+O::odolwa_calc_prev,g.Read32(boss+O::odolwa_calc));g.Write32(boss+O::odolwa_calc,Code(ref));}
inline u32 Collision(Guest& g,u32 first,unsigned count){for(unsigned i=0;i<count;++i){const auto at=first+i*Layout::CollisionInfoCylinder::Size;if(g.Read8(at+CI::flags1)&2)return at;}return 0;}
Routine Engine::DamageFlash(u32 boss,u32 a,u32 b,u32 c,u32 d) {CallMachine::Arguments args{{boss,a,b,c}};args.Stack={d};args.StackWords=1;co_await Call(0x51e214,args);co_return 0;}
Routine Engine::ActorAnim(u32 util,u32 id,float parameter,bool full) {CallMachine::Arguments args{{util,id}};args.S[0]=std::bit_cast<u32>(parameter);co_await Call(full?0x5153c8:0x51689c,args);co_return 0;}
Routine Engine::OdolwaJump(u32 boss){co_await Call(0x2fe074,{{boss,s.gctx,2}});co_return 0;}
Routine Engine::OdolwaFreeze(u32 boss) {
    OdolwaState(g,boss,0x56655c);g.Write16(boss+O::timer,60);
    if(!g.Read8(boss+O::field_30C_delta)&&!g.Read8(boss+O::field_30C))g.Write8(boss+O::field_30C_delta,2);
    co_await DamageFlash(boss,0,0x78,0,0x3c);g.Write16(boss+O::invincibility_timer,23);
    co_await Call(0x226218,{{boss+A::pos__pos,Value(Enum::Sound::EffectId::NA_SE_EN_COMMON_FREEZE)}});g.Write8(boss+O::do_not_use_shield,0);co_return 0;
}
Routine Engine::OdolwaCollision(u32 boss) {
    if(!boss||!s.gctx||g.Read32(boss+O::odolwa_calc)==Code(0x27aa2c))co_return 0;
    const auto info=Collision(g,boss+O::collision__info,11);if(!info)co_return 0;
    g.Write8(info+CI::flags1,g.Read8(info+CI::flags1)&~2);
    using D=Enum::Actor::DamageType;using At=Enum::Collision::AttackType;using Sfx=Enum::Sound::EffectId;
    const auto type=D(g.Read8(boss+A::damage_type));
    if(type==D::Type1||type==D::IceArrow||type==D::TypeB) {
        if(type==D::IceArrow)g.Write8(boss+O::field_1C35,10);if(type==D::TypeB)g.Write8(boss+O::field_1C35,40);co_await OdolwaFreeze(boss);co_return 0;
    }
    if(type==D::FireArrow)g.Write8(boss+O::field_1C35,1);
    if(type==D::Type4||type==D::TypeC) {
        g.Write8(boss+O::field_1C35,type==D::Type4?20:30);
        CallMachine::Arguments args{{s.gctx+G::actors,s.gctx,Value(Enum::Actor::Id::ClearTag),0}};
        args.Stack={0,type==D::Type4?0u:3u,4};args.StackWords=3;
        for(unsigned i=0;i<3;++i)args.S[i]=g.Read32(boss+A::ztarget_pos__pos+4*i);co_await Call(0x22074c,args);
    }
    const auto moth=g.Read32(s.gctx+0xf004);if(moth)g.Write16(moth+0x1fe,0);
    const auto hit=g.Read32(info+CI::colliding_info),attack=g.Read32(hit+CI::attack_type);int damage=g.Read8(boss+A::damage);
    if(type==D::Hookshot)damage=0;
    else if(attack) {
        const auto reaction=0x65e49c+std::countr_zero(attack)*Layout::act_OdolwaDamageReaction::Size;
        const auto mode=g.Read32(reaction);const auto value=g.Float(reaction+4);
        if(mode==1)damage=int(damage*value);else if(mode==2)damage=int(damage+value);else if(mode==3)damage=int(value);
    }
    g.Write8(boss+A::life,u8(g.Signed8(boss+A::life)-damage));g.Write8(boss+O::cycle_life,u8(g.Signed8(boss+O::cycle_life)-damage));
    const auto util=boss+O::actor_util;
    if(g.Signed8(boss+A::life)<=0) {
        co_await ActorAnim(util,14,g.Float(boss+O::field_208),true);
        g.Float(boss+O::anim_duration,float(s32((co_await Call(0x240dd8,{{util,14}})).R0)));
        g.Write8(util+Layout::as_ActorUtil::field_89,0);g.Write32(boss+O::timer2,0);OdolwaState(g,boss,0x2a4fac);
        co_await Call(0x226218,{{boss+A::pos__pos,Value(Sfx::NA_SE_EN_FANTOM_DEAD)}});Bits(boss+A::flags,Value(Enum::Actor::Flag::Targetable),false);
        g.Write16(boss+O::shield_timer_2,0x5dc);g.Write32(boss+O::intro_timer,0);g.Write16(boss+O::intro_state,0);
        if(!g.Read8(0x6b0a3c)){g.Write8(0x6b0a3c,1);co_await Call(0x1e11f8,{{0x7cb49c}});}
        co_await Call(0x1dc2f0,{{0x7cb49c,Value(Enum::Sound::StreamPlayer::DEFAULT_PLAYER),1,1}});
        if(moth){g.Write8(moth+0x1f8,0xfa);const auto category=g.Read8(moth+A::actor_type);if(category<12)g.Write8(s.gctx+G::actors__lists+category*Layout::ActorList::Size+Layout::ActorList::need_to_update_lists,1);g.Write8(moth+A::actor_type,u8(Enum::Actor::Type::Boss));}
        co_await Call(0x226218,{{boss+A::pos__pos,Value(Sfx::NA_SE_EN_MIBOSS_DEAD)}});co_await Call(0x389ac0,{{s.gctx,boss}});co_await Call(0x51297c,{{s.gctx,info,0}});co_return 0;
    }
    co_await Call(0x51297c,{{s.gctx,info,damage==0?1u:0u}});
    if(damage&&!(attack&(1u<<Value(At::Arrow))))co_await DamageFlash(boss,0x400000,0x64,0,0x17);
    g.Float(boss+O::field_364,g.Float(boss+O::field_364)*.5f);g.Float(boss+O::field_368,g.Float(boss+O::field_368)*.5f);
    if(g.Signed8(boss+O::cycle_life)<=0) {
        co_await OdolwaJump(boss);g.Write16(boss+O::invincibility_timer,8);g.Write8(boss+O::cycle_life,3);
        co_await Call(0x226218,{{boss+A::pos__pos,Value(Sfx::NA_SE_EN_MIBOSS_DAMAGE)}});
    } else if(type==D::DekuNut) {
        co_await DamageFlash(boss,0,0x80,0,0x17);co_await ActorAnim(util,0x13,g.Float(boss+O::field_20C));g.Write8(util+Layout::as_ActorUtil::field_89,0);
        if(!g.Read8(boss+O::field_30C_delta)&&!g.Read8(boss+O::field_30C))g.Write8(boss+O::field_30C_delta,2);
        if(g.Read32(boss+O::odolwa_calc)!=Code(0x27e194)){g.Write16(boss+O::timer,0x69);OdolwaState(g,boss,0x27e194);}
        g.Write8(boss+O::do_not_use_shield,0);co_await Call(0x226218,{{boss+A::pos__pos,Value(Sfx::NA_SE_EN_MIBOSS_DAMAGE)}});g.Write16(boss+O::invincibility_timer,0x17);
    } else if(type!=D::Hookshot) {
        if((attack&(1u<<Value(At::DekuFlower)))&&g.Read32(boss+O::odolwa_calc)==Code(0x24c5a4)) {
            co_await ActorAnim(util,0x2d,-3,true);g.Float(boss+O::anim_duration,float(s32((co_await Call(0x240dd8,{{util,0x2d}})).R0)));OdolwaState(g,boss,0x2fbc68);
        } else {
            if(!g.Read8(boss+O::field_30C_delta)&&!g.Read8(boss+O::field_30C))g.Write8(boss+O::field_30C_delta,2);
            const bool weakened=g.Read32(boss+O::odolwa_calc)==Code(0x27e194);const auto anim=g.Read32(util+Layout::as_ActorUtil::state__id);
            if(!weakened||anim==27){if((attack&(1u<<Value(At::Arrow)))||(!weakened&&anim!=27))g.Write16(boss+O::timer,60);co_await ActorAnim(util,0x13,0);g.Write8(util+Layout::as_ActorUtil::field_89,0);OdolwaState(g,boss,0x27e194);}
        }
        co_await Call(0x226218,{{boss+A::pos__pos,Value(Sfx::NA_SE_EN_MIBOSS_DAMAGE)}});g.Write16(boss+O::invincibility_timer,8);
    }
    g.Write8(boss+O::do_not_use_shield,0);g.Write16(boss+O::taunting_timer,1800);
    for(unsigned i=0;i<11;++i){const auto flags=boss+O::collision__info+i*Layout::CollisionInfoCylinder::Size+CI::flags1;g.Write8(flags,g.Read8(flags)&~2);}co_return 0;
}
Routine Engine::BossHook(std::string_view name) {
    const auto boss=context.Saved.r[0];if(!boss)co_return 0;
    if(name=="rst_OdolwaGetWaitDuration") {
        if(g.Read32(boss+O::odolwa_calc_prev)==Code(0x308484))co_return 90;
        const float random=std::bit_cast<float>((co_await Call(0x20a8f8)).S0);co_return (random<=.25f?15:0)+(g.Read8(boss+O::call_bug_counter)==0?60:15);
    }
    if(name=="rst_OdolwaGetChargeDuration") {
        if(g.Read8(boss+O::command)==0){const float chance=g.Read8(boss+O::call_bug_counter)==0?.9f:.25f;co_return std::bit_cast<float>((co_await Call(0x20a8f8)).S0)<=chance?88:0;}
        co_return u32(2*int(g.Float(boss+O::anim_duration)*1.5f+.5f));
    }
    if(name=="rst_OdolwaShouldMoveToPhase2")co_return !g.Read8(boss+O::call_bug_counter)&&(s32(g.Read32(boss+O::call_bug_timer))>1500||g.Signed8(boss+A::life)<=10);
    if(name=="rst_OdolwaCheckIsInvincible") {
        if(!g.Read16(boss+O::invincibility_timer))co_return 0;const bool collided=Collision(g,boss+O::collision__info,11)!=0;const bool result=s.odolwa_collided||!collided;s.odolwa_collided=collided;co_return result;
    }
    if(name=="rst_IsGohtCollided")co_return (g.Read8(boss+Layout::act_BossGoht::goht_flags)&0x40)&&!g.Read8(boss+Layout::act_BossGoht::field_3998)&&Collision(g,boss+Layout::act_BossGoht::collision__info,20);
    if(name=="rst_GetGyorgCollisionResponse") {const auto fn=g.Read32(context.Saved.r[4]+Layout::act_BossGyorg::gyorg_calc);co_return fn==Code(0x2802c8)||fn==Code(0x5560c0)||fn==Code(0x27aad0)?2:1;}
    co_return 0;
}
Routine Engine::Bosses() {
    if(!s.gctx)co_return 0;
    using Id=Enum::Actor::Id;const auto find=[&](Id id){return Find(Value(Enum::Actor::Type::Boss),Value(id));};
    if(auto boss=find(Id::BossOdolwa)) {
        if(auto table=g.Read32(boss+A::damage_table))g.Write8(table+Value(Enum::Collision::AttackType::Arrow),g.Read8(table+Value(Enum::Collision::AttackType::Arrow))&0xf0);
        if(g.Read16(boss+O::intro_state)==3) {
            auto timer=g.Read32(boss+O::intro_timer);if(timer==0)co_await Call(0x15d198,{{Value(Enum::Sound::StreamId::NA_BGM_BOSS00)}});
            if(timer==2){g.Float(boss+O::actor_util__state__position,32.666653f);timer=49;g.Write32(boss+O::intro_timer,timer);}
            if(timer==60||timer==114||timer==176)co_await Call(0x226218,{{boss+A::pos__pos,Value(Enum::Sound::EffectId::NA_SE_EN_MIBOSS_SWORD)}});
        } else {
            const auto fn=g.Read32(boss+O::odolwa_calc),p=Player();const bool preparing=fn==Code(0x24fe24)&&g.Read8(boss+O::command)==0;
            if((preparing||fn==Code(0x5725e4))&&g.Float(boss+A::distance_to_link)<=120&&g.Read32(boss+O::odolwa_calc_prev)!=Code(0x2a6ff8)&&p&&!Flag(p,P::flags3,Value(Enum::Player::Flag3::DekuInFlower)))co_await OdolwaJump(boss);
        }
    }
    if(auto goht=find(Id::BossGoht)) {
        namespace Go=Layout::act_BossGoht;for(unsigned i=0;i<3;++i)g.Float(goht+Go::eye_scale+4*i,i==2?.05f:.8f);g.Write8(goht+Go::goht_flags,g.Read8(goht+Go::goht_flags)|0x40);
    }
    if(auto gyorg=find(Id::BossGyorg)) {
        namespace Gy=Layout::act_BossGyorg;if(s.gyorg_inhale_frames)++*s.gyorg_inhale_frames;
        const bool eating=g.Read32(gyorg+Gy::gyorg_calc)==Code(0x557900);if(eating)s.gyorg_inhale_frames=0;
        Bits(gyorg+Gy::field_F24,1,true);g.Write8(gyorg+Gy::eyeball_flags,0);for(unsigned i=0;i<3;++i)g.Float(gyorg+Gy::eyeball_scale+4*i,0);
        if(auto player=Player())if((eating&&g.Read32(player+P::state_handler_fn)==Code(0x20d96c))||(s.gyorg_inhale_frames&&*s.gyorg_inhale_frames<=30))Bits(player+P::flags1,Value(Enum::Player::Flag1::FreezeLink),false);
    }else s.gyorg_inhale_frames.reset();
    if(auto red=find(Id::BossTwinmold)) {
        namespace T=Layout::act_BossTwinmold;
        if(s.twin) {
            if(s.twin->red_life>g.Signed8(red+A::life))g.Write8(red+A::life,u8(g.Signed8(red+A::life)-8));
            const auto hit=g.Read16(red+T::hit_counter);if(hit==8)s.twin->sane=true;
            if(s.twin->sane&&hit==9&&s.twin->hit_counter!=9&&g.Read16(red+T::status)!=15)g.Write16(red+T::hit_counter,s.twin->hit_counter);
        }else s.twin.emplace();
        s.twin->red_life=g.Signed8(red+A::life);if(s.twin->sane)s.twin->hit_counter=g.Read16(red+T::hit_counter);
    }else s.twin.reset();co_return 0;
}
} // namespace Mm3dRestoration
