// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration source/rst and source/game (8bb2a7b8).
#pragma once
#include "session.h"
#include "layout.h"
#include "enums.h"
#include "main.h"
#include "v110_address.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>

namespace Mm3dRestoration {
namespace G=Layout::GlobalContext;
namespace P=Layout::act_Player;
namespace A=Layout::act_Actor;
namespace C=Layout::CommonData;
namespace H=Layout::ui_MainScreen;
template<class E> constexpr u32 Value(E value) {return static_cast<u32>(value);}
constexpr u32 Code(u32 reference) {return Mm3dDekui::V110(reference);}
// Data and BSS addresses are the v1.1 ones: CommonData, StaticContext and
// the layout project keep their v1.0 places, while ui::GetCommonLayouts()
// (v1.0 0x6b7b8c) moved by 8 and the LayoutMgr instance guard (v1.0 0x6b0afc)
// by 4; each is the literal v1.1's own code loads.
constexpr u32 Common=0x7751d8,Static=0x7892d0,CommonLayouts=0x6b7b94,Project=0x7cdca0;
constexpr u32 LayoutMgrInstanceGuard=0x6b0b00,LayoutMgrInstance=0x7cdc9c;
// The resource id the project's lookups by name (0x16a014, 0x15a86c) return for
// a name the mounted RomFS lacks.
constexpr u32 kNoResource=0xffff;
class Engine {
public:
    Session& s;Guest g;CallMachine& machine;Oot3dNativeGame::Mm3dGuestCallContext& context;
    Engine(Session& state,CallMachine& calls,Oot3dNativeGame::Mm3dGuestCallContext& ctx):s(state),g(*ctx.Memory),machine(calls),context(ctx) {}
    auto Call(u32 ref,CallMachine::Arguments args={}) {return machine.Call(Code(ref),std::move(args));}
    auto Direct(u32 pc,CallMachine::Arguments args={}) {return machine.Call(pc,std::move(args));}
    u32 Player() {return s.gctx?g.Read32(s.gctx+G::actors+Layout::ActorLists::lists+2*Layout::ActorList::Size+Layout::ActorList::first):0;}
    u32 First(unsigned type) {return s.gctx?g.Read32(s.gctx+G::actors+Layout::ActorLists::lists+type*Layout::ActorList::Size+Layout::ActorList::first):0;}
    u32 Find(unsigned type,u32 id) {for(u32 a=First(type),n=0;a&&n<4096;a=g.Read32(a+A::next),++n)if(g.Read16(a+A::id)==id)return a;return 0;}
    bool Flag(u32 base,u32 offset,u32 flag) {return base&&(g.Read32(base+offset)&flag)!=0;}
    void Bits(u32 at,u32 mask,bool enabled) {const auto old=g.Read32(at);g.Write32(at,enabled?old|mask:old&~mask);}
    bool HasItem(u32 id,bool mask=false);
    Routine CanUseItem(u32 id);
    Routine Alloc(u32 size);
    Routine String(std::string_view text);
    Routine MakeLayout(std::string_view name);
    u32 Widget(u32 root,std::string_view name,unsigned depth=0);
    u32 LayoutWidget(u32 layout,std::string_view name) {return layout?Widget(layout+Layout::ui_Layout::root_widget,name):0;}
    bool Name(u32 address,std::string_view name);
    void Visible(u32 widget,bool value);
    void Opacity(u32 widget,float value);
    void AddOpacity(u32 widget,float delta,float minimum=0,float maximum=1);
    u32 Anim(u32 layout,std::string_view name);
    Routine Play(u32 player,u32 anim,float frame=0,bool loop=false);
    Routine Frame(u32 player,float frame);
    Routine OpenScreen(unsigned screen);
    Routine ScheduleTrigger();
    Routine ScheduleUpdate();
    Routine MessageGet(u32 self,u16 id,u32 out);
    void EditMessage(u32 self,u16 id);
    Routine Calc();
    Routine CalcCamera();
    Routine ChangeCamera();
    Routine LinkCalc();
    Routine FastTransform();
    Routine FastArrow(u32 player);
    Routine ArrowInfo(u32 game,u32 player,u32 item_out=0,u32 param_out=0);
    Routine SpawnArrow(u32 player);
    Routine Elegy(u32 player,u32 game);
    void FixTime();bool SlowTime();void UpdateTime();float MoveNpc(u32 pointer,float speed);
    Routine Fixes();Routine Owl();Routine Bosses();Routine OdolwaCollision(u32 boss);
    Routine OdolwaFreeze(u32 boss);Routine OdolwaJump(u32 boss);Routine BossHook(std::string_view name);
    Routine DamageFlash(u32 boss,u32 a,u32 b,u32 c,u32 d);
    Routine ActorAnim(u32 actor_util,u32 id,float parameter,bool full=false);
    Routine Hud(std::string_view action);
    Routine HudCalc(u32 screen_context);
    Routine HudDpad(u32 game);
    Routine HudMagic(std::string_view action);
    Routine BIcon(u32 icon,u32 game);
    Routine Icon(u32 icon,u32 value,bool item=false);
    Routine OcarinaSong(u32 window,u16 song);
    Routine EndOcarina(u32 window);
    Routine StateInfo(u32 state);
    Routine Sot(std::string_view action);
    Routine Run(std::string_view action);
};
} // namespace Mm3dRestoration
