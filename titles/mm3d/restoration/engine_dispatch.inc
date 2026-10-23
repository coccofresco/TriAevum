// SPDX-License-Identifier: GPL-2.0-or-later
// Register/stack contracts from Project Restoration source/rst/trampolines.s.
namespace Mm3dRestoration {
inline void Compare(oot3d::recomp::a32::GuestState& cpu,u32 a,u32 b) {
    const u32 result=a-b;const bool overflow=((a^b)&(a^result)&0x80000000)!=0;
    cpu.cpsr=(cpu.cpsr&0x0fffffff)|(result&0x80000000)|(result==0?0x40000000:0)|(a>=b?0x20000000:0)|(overflow?0x10000000:0);
}
Routine Engine::Run(std::string_view action) {
    auto& cpu=context.Saved;auto& r=cpu.r;
    if(action=="Calc")co_await Calc();
    else if(action=="UpdatePadState")s.input.UpdatePadState(g,s.is_swimming);
    else if(action=="UpdatePadStateForOcarina")Input::UpdatePadStateForOcarina(g);
    else if(action=="PreActorCalcHook")co_await Owl();
    else if(action=="PostActorCalcHook")co_await Bosses();
    else if(action=="TriggerItemUseHook")co_await FastTransform();
    else if(action=="UiScheduleTriggerHook")co_await ScheduleTrigger();
    else if(action.starts_with("MainScreen_"))co_await Hud(action);
    else if(action=="BActionIconUpdate")co_await BIcon(r[0],r[1]);
    else if(action=="MessageDataGet")r[0]=co_await MessageGet(r[0],u16(r[1]),r[2]);
    else if(action=="PlayerGetArrowInfo")r[0]=(co_await ArrowInfo(r[0],r[1],r[2],r[3]))&1;
    else if(action=="PlayerStateSpawningElegyStatue")co_await Elegy(r[0],r[1]);
    else if(action=="UpdateTimeHook")UpdateTime();
    else if(action=="rst_MoveScheduledNpcHook") {r[4]=r[1];cpu.vfp[0]=std::bit_cast<u32>(MoveNpc(r[2],std::bit_cast<float>(cpu.vfp[0])));}
    else if(action=="rst_CameraChangeModeExHook")co_await ChangeCamera();
    else if(action=="rst_GameStateGetNextStateInfo") {r[8]=co_await StateInfo(r[6]);r[0]=r[6];}
    else if(action=="rst_ui_items_IsItemAssignRequested")Compare(cpu,s.input.IsItemAssignRequested(g)?1:0,0);
    else if(action=="rst_ui_items_GetItemAssignIndex")r[3]=u32(s.input.GetItemAssignIndex(g));
    else if(action=="rst_link_ShouldUseZoraFastSwim") {
        bool use=false;
        if(s.gctx){const auto held=g.Read32(s.gctx+G::pad_state__input__buttons),pressed=g.Read32(s.gctx+G::pad_state__input__new_buttons);
            if(held&Button::A){if(pressed&(Button::Up|Button::Down|Button::ZL))s.use_fast_swim=!s.use_fast_swim;use=held&Button::R?true:held&Button::ZR?false:s.use_fast_swim;}}
        r[0]=use;
    } else if(action=="rst_link_ShouldEndGoronRoll") {
        const auto pad=g.Read32(r[0]+P::controller_info__state);
        r[0]=pad&&((g.Read32(pad+Layout::pad_State::input__new_buttons)&Button::A)||((g.Read32(pad+Layout::pad_State::input__released_buttons)&Button::A)&&s.a_press_duration>=15));
    } else if(action=="rst_link_SwitchToZoraFastSwim") {
        const auto p=r[1];const bool ready=g.Read16(p+P::zora_swim_a_press_duration)>=7||Flag(p,P::flags1,Value(Enum::Player::Flag1::IsUsingZoraBarrier));
        r[0]=ready?(co_await Call(0x220ea0,{{r[0],p,r[2]}})).R0:0;
    } else if(action=="rst_HandleOcarinaSong") {
        const auto result=co_await OcarinaSong(r[4],u16(r[0]));Compare(cpu,result,0);
        if(result) {
            // Entry SP - 4 (trampoline's saved LR), +0x70, then POP r4-r11,pc.
            const auto stack=r[13]+0x6c;for(unsigned i=4;i<=11;++i)r[i]=g.Read32(stack+4*(i-4));
            context.Resume=g.Read32(stack+32);r[13]=stack+36;
        }else Compare(cpu,r[0],0x16);
    } else if(action=="OdolwaHandleRegularCollision")co_await OdolwaCollision(r[0]);
    else if(action=="rst_OdolwaCheckIsInvincible") {const auto old=r[0];r[0]=r[4];const auto result=co_await BossHook(action);r[0]=old;Compare(cpu,result,0);}
    else if(action=="rst_IsGohtCollided")Compare(cpu,co_await BossHook(action),1);
    else if(action=="rst_GetGyorgCollisionResponse") {r[10]=co_await BossHook(action);r[14]=36;}
    else if(action.starts_with("rst_Odolwa"))r[0]=co_await BossHook(action);
    else if(action=="LoadPackages") {const auto name=co_await String("Joker.Main.New.gar");if(name){const auto id=(co_await Call(0x15a86c,{{Project,name}})).R0;if(id!=kNoResource)co_await Call(0x15141c,{{Project,id,1}});}}
    else if(action.starts_with("Sot"))co_await Sot(action);
    else if(action=="StoneDelay1"||action=="StoneDelay2") {
        namespace St=Layout::act_ObjStoneTowerBlock;
        if(action=="StoneDelay1"){g.Write16(r[0]+St::timer,6);g.Write32(r[0]+St::action_fn,Session::StoneDelay2);}
        else{const auto timer=u16(g.Read16(r[0]+St::timer)-1);g.Write16(r[0]+St::timer,timer);if(!timer)g.Write32(r[0]+St::action_fn,Code(0x2ac52c));}
    }else throw std::logic_error("Unimplemented Restoration hook: "+std::string(action));
    co_return 0;
}
} // namespace Mm3dRestoration
