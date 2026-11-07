// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from mm3d-dekui src/core/arm/dynarmic/arm_dynarmic.cpp.
// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project.
#include "core.h"

namespace Mm3dDekui {
U32 Core::Instruction(Memory& memory,U32 pc,U32 word) {

    if (pc >= 0x007cf000U) return word; // ref appended Restoration range
    if(VegetationDrawDistance::Instruction(pc,word)!=word&&VegetationDrawDistance::CodeMatches(memory,pc))
        return VegetationDrawDistance::Instruction(pc,word);
    if(TatlReminders::Instruction(pc,word)!=word && TatlReminders::CodeMatches(memory))
        return TatlReminders::Instruction(pc,word);
    if(BomberApproach::Instruction(pc,word)!=word && BomberApproach::CodeMatches(memory))
        return BomberApproach::Instruction(pc,word);
    if(OwlIntro::Instruction(pc,word)!=word && OwlIntro::CodeMatches(memory))
        return OwlIntro::Instruction(pc,word);
    if(ActorDrawDistance::Instruction(pc,word)!=word && ActorDrawDistance::CodeMatches(memory,pc))
        return ActorDrawDistance::Instruction(pc,word);
    if(PropDrawDistance::Instruction(pc,word)!=word && PropDrawDistance::CodeMatches(memory,pc))
        return PropDrawDistance::Instruction(pc,word);


        if(Mm3dDekui::MinimapShortcut::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::MinimapShortcut::CodeMatches(code))
                return Mm3dDekui::MinimapShortcut::Instruction(pc,word);
        }
        if(Mm3dDekui::BottleHealing::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::BottleHealing::CodeMatches(code))
                return Mm3dDekui::BottleHealing::Instruction(pc,word);
        }
        if(Mm3dDekui::DeathPenalties::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::DeathPenalties::CodeMatches(code))
                return Mm3dDekui::DeathPenalties::Instruction(pc,word);
        }
        if(Mm3dDekui::Difficulty::SpawnInstruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::Difficulty::SpawnCodeMatches(code,pc))
                return Mm3dDekui::Difficulty::SpawnInstruction(pc,word);
        }
        if(Mm3dDekui::Difficulty::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::Difficulty::CodeMatches(code,pc))
                return Mm3dDekui::Difficulty::Instruction(pc,word);
        }
        if(Mm3dDekui::NativeStartup::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::NativeStartup::CodeMatches(code))
                return Mm3dDekui::NativeStartup::Instruction(pc,word);
        }
        if(Mm3dDekui::ScheduleRows::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::ScheduleRows::CodeMatches(code))
                return Mm3dDekui::ScheduleRows::Instruction(pc,word);
        }
        if(Mm3dDekui::NotebookRows::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::NotebookRows::CodeMatches(code))
                return Mm3dDekui::NotebookRows::Instruction(pc,word);
        }
        if(Mm3dDekui::NativeNotebook::MapInstruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::NativeNotebook::MapCodeMatches(code))
                return Mm3dDekui::NativeNotebook::MapInstruction(pc,word);
        }
        if(Mm3dDekui::NativeNotebook::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::NativeNotebook::CodeMatches(code))
                return Mm3dDekui::NativeNotebook::Instruction(pc,word);
        }
        if(Mm3dDekui::ItemPreview::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::ItemPreview::CodeMatches(code))
                return Mm3dDekui::ItemPreview::Instruction(pc,word);
        }
        if(Mm3dDekui::HudButtons::VisibilityInstruction(pc,word)!=word){
            auto& code=memory;
            if(Mm3dDekui::HudButtons::VisibilityCodeMatches(code))
                return Mm3dDekui::HudButtons::VisibilityInstruction(pc,word);
        }
        if(pc==Mm3dDekui::SongList::NavPC&&word==Mm3dDekui::SongList::NavWord) {
            auto& code=memory;
            if(Mm3dDekui::SongList::CodeMatches(code))return 0xef000000|Mm3dDekui::SongList::NavSVC;
        }
        if(Mm3dDekui::DungeonMap::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::DungeonMap::CodeMatches(code))
                return Mm3dDekui::DungeonMap::Instruction(pc,word);
        }
        if(Mm3dDekui::OptionsMenu::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::OptionsMenu::CodeMatches(code))
                return Mm3dDekui::OptionsMenu::Instruction(pc,word);
        }
        if(Mm3dDekui::MenuShortcuts::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::MenuShortcuts::CodeMatches(code))
                return Mm3dDekui::MenuShortcuts::Instruction(pc,word);
        }
        if(Mm3dDekui::MenuClose::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::MenuClose::CodeMatches(code))
                return Mm3dDekui::MenuClose::Instruction(pc,word);
        }
        if(Mm3dDekui::Music::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::Music::CodeMatches(code))
                return Mm3dDekui::Music::Instruction(pc,word);
        }
        if(Mm3dDekui::MenuSound::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::MenuSound::CodeMatches(code))
                return Mm3dDekui::MenuSound::Instruction(pc,word);
        }
        if(Mm3dDekui::Prices::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::Prices::CodeMatches(code))
                return Mm3dDekui::Prices::Instruction(pc,word);
        }
        if(Mm3dDekui::SingleScreenText::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::SingleScreenText::CodeMatches(code))
                return Mm3dDekui::SingleScreenText::Instruction(pc,word);
        }
        if(Mm3dDekui::MenuMemory::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::MenuMemory::CodeMatches(code))
                return Mm3dDekui::MenuMemory::Instruction(pc,word);
        }
        if(Mm3dDekui::MenuPageSwitch::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::MenuPageSwitch::CodeMatches(code))
                return Mm3dDekui::MenuPageSwitch::Instruction(pc,word);
        }
        if(Mm3dDekui::InventoryNavigation::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::InventoryNavigation::CodeMatches(code))
                return Mm3dDekui::InventoryNavigation::Instruction(pc,word);
        }
        if(Mm3dDekui::NativeItemPresent::IntroInstruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::NativeItemPresent::IntroCodeMatches(code))
                return Mm3dDekui::NativeItemPresent::IntroInstruction(pc,word);
        }
        if(Mm3dDekui::PauseCapture::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::PauseCapture::CodeMatches(code))
                return Mm3dDekui::PauseCapture::Instruction(pc,word);
        }
        if(Mm3dDekui::NativeInventory::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::NativeInventory::CodeMatches(code))
                return Mm3dDekui::NativeInventory::Instruction(pc,word);
        }
        if(Mm3dDekui::DialogueScale::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::DialogueScale::CodeMatches(code))
                return Mm3dDekui::DialogueScale::Instruction(pc,word);
        }
        if (Mm3dDekui::NativeMinimap::Instruction(pc,word)!=word) {
            auto& code=memory;
            if (Mm3dDekui::NativeMinimap::CodeMatches(code))
                return Mm3dDekui::NativeMinimap::Instruction(pc,word);
        }
        if(Mm3dDekui::FormCamera::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::FormCamera::CodeMatches(code))
                return Mm3dDekui::FormCamera::Instruction(pc,word);
        }
        if(Mm3dDekui::CameraLive::ChangeModeInstruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::CameraLive::ChangeModeCodeMatches(code))
                return Mm3dDekui::CameraLive::ChangeModeInstruction(pc,word);
        }
        if(Mm3dDekui::CameraLive::DialogueCameraInstruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::CameraLive::DialogueCameraCodeMatches(code))
                return Mm3dDekui::CameraLive::DialogueCameraInstruction(pc,word);
        }
        if(Mm3dDekui::SongEffect::Instruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::SongEffect::CodeMatches(code))
                return Mm3dDekui::SongEffect::Instruction(pc,word);
        }
        if(Mm3dDekui::CameraLive::ProjectionInstruction(pc,word)!=word) {
            auto& code=memory;
            if(Mm3dDekui::CameraLive::CodeMatches(code,pc))
                return Mm3dDekui::CameraLive::ProjectionInstruction(pc,word);
        }
    return word;
}

void Core::CallSvc(U32 swi,Cpu& cpu,Memory& memory) {

    auto& r=cpu.Regs();
    if(swi>=VegetationDrawDistance::FirstSVC&&swi<VegetationDrawDistance::FirstSVC+VegetationDrawDistance::Hooks.size()) {
        VegetationDrawDistance::Load(memory,swi-VegetationDrawDistance::FirstSVC,r,cpu.ExtRegs(),PropDrawDistance::multiplier.load());return;
    }
    if(swi==Difficulty::DefenseSVC) {
        r[4]=Difficulty::DefenseDelta(r[4],r[1],cpu.Cpsr(),Difficulty::weaker_enhanced_defense.load());return;
    }
    if(swi==TatlReminders::SVC) {r[4]=TatlReminders::Request(memory,r[0],r[1]);return;}
    if(swi==BomberApproach::FirstSVC) {
        const auto word=memory.Read(r[5]+0x8c);
        if(word)r[0]=BomberApproach::State(U32(std::int32_t(std::int16_t(*word&65535))),BomberApproach::Suppress(memory,r[4]));return;
    }
    if(swi==BomberApproach::FirstSVC+1) {
        if(BomberApproach::Suppress(memory,r[4]))r[15]=BomberApproach::ResetPC;else r[1]=15;return;
    }
    if(swi==OwlIntro::FirstSVC) {OwlIntro::Enter(r);return;}
    if(swi==OwlIntro::FirstSVC+1) {
        r[0]=OwlIntro::TatlFlags(memory.Read8(r[0]+0x2d3));return;
    }
    if(swi==ActorDrawDistance::SVC) {
        U32 value=ActorDrawDistance::Apply(memory,r[4],cpu.ExtRegs()[16],ActorDrawDistance::multiplier.load());
        cpu.ExtRegs()[0]=PropDrawDistance::Apply(memory,r[4],value,PropDrawDistance::multiplier.load());return;
    }
    if(swi>=PropDrawDistance::FirstSVC&&swi<PropDrawDistance::FirstSVC+PropDrawDistance::Hooks.size()) {
        PropDrawDistance::Load(swi-PropDrawDistance::FirstSVC,r,cpu.ExtRegs(),PropDrawDistance::multiplier.load());return;
    }


        if(swi==Mm3dDekui::MinimapShortcut::SVC) {
            auto& guest=memory;
            Mm3dDekui::MinimapShortcut::Handle(guest,cpu.Regs());
            return;
        }
        if(swi>=Mm3dDekui::BottleHealing::FirstSVC&&swi<Mm3dDekui::BottleHealing::FirstSVC+3) {
            auto& guest=memory;
            Mm3dDekui::BottleHealing::Apply(guest,cpu.Regs(),swi-Mm3dDekui::BottleHealing::FirstSVC,
                Mm3dDekui::BottleHealing::red_hearts.load(),Mm3dDekui::BottleHealing::blue_hearts.load(),
                Mm3dDekui::BottleHealing::fairy_hearts.load());
            return;
        }
        if(swi==Mm3dDekui::DeathPenalties::FirstSVC||swi==Mm3dDekui::DeathPenalties::FirstSVC+1) {
            auto& guest=memory;
            if(swi==Mm3dDekui::DeathPenalties::FirstSVC)
                Mm3dDekui::DeathPenalties::Death(guest,cpu.Regs(),Mm3dDekui::DeathPenalties::lose_rupees.load());
            else
                Mm3dDekui::DeathPenalties::Continue(guest,cpu.Regs(),Mm3dDekui::DeathPenalties::world_ends.load());
            return;
        }
        if(swi==Mm3dDekui::Difficulty::SpawnSVC) {
            auto& guest=memory;
            Mm3dDekui::Difficulty::Spawn(guest,cpu.Regs(),Mm3dDekui::Difficulty::disable_heart_drops.load(),
                                    Mm3dDekui::Difficulty::disable_rupee_drops.load(),
                                    Mm3dDekui::Difficulty::disable_fairy_drops.load(),
                                    Mm3dDekui::Difficulty::disable_magic_drops.load(),
                                    Mm3dDekui::Difficulty::disable_ammo_pickups.load());
            return;
        }
        if(swi==Mm3dDekui::Difficulty::SVC) {
            auto& regs=cpu.Regs();
            regs[2]=Mm3dDekui::Difficulty::ApplyDamage(regs[2],regs[4],Mm3dDekui::Difficulty::damage_multiplier.load());
            return; // displaced ADD does not alter CPSR or the other registers
        }
        if(swi==Mm3dDekui::NativeStartup::FirstSVC||swi==Mm3dDekui::NativeStartup::FirstSVC+1) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            if(swi==Mm3dDekui::NativeStartup::FirstSVC) {
                startup_hook.Enter(guest,regs[0]);regs[5]=regs[0];
            } else {
                startup_hook.Matrix(guest,regs[5],regs[4]);
                regs[0]=memory.Read32(regs[5]+0xc);
            }
            return;
        }
        if(swi>=Mm3dDekui::ItemPreview::FirstSVC && swi<Mm3dDekui::ItemPreview::FirstSVC+Mm3dDekui::ItemPreview::PCs.size()) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            switch(swi-Mm3dDekui::ItemPreview::FirstSVC) {
            case 0:
                item_preview_hook.Enter(guest,regs[0],regs[1]);
                // Shared Items/Masks entry runs after the complete icon array is built.
                inventory_hook.Capture(guest,regs[0]);
                regs[4]=regs[0]; break;
            case 1: item_preview_hook.Leave(regs[0]); regs[4]=regs[0]; break;
            case 2: item_preview_hook.ModelFrame(regs[0]); dungeon_map_hook.ModelFrame(guest,regs[0]); regs[4]=regs[0]; break;
            case 3: item_preview_hook.ModelBegin(guest,regs[4],regs[6]); dungeon_map_hook.preview.ModelBegin(guest,regs[4],regs[6]); regs[1]=regs[6]; break;
            case 4: dungeon_map_hook.preview.ModelEnd(guest); item_preview_hook.ModelEnd(guest); break; // displaced NOP
            case 5:
                item_preview_hook.EnterGear(guest,regs[0],regs[1]);
                inventory_hook.CaptureGear(guest,regs[0]);
                regs[4]=regs[0]; break;
            case 6:
                item_preview_hook.Leave(regs[0]);
                if(inventory_hook.gear_page==regs[0]) inventory_hook.Capture(guest,0);
                regs[5]=regs[0]; break;
            }
            return;
        }
        if(swi==Mm3dDekui::ScheduleRows::FirstSVC||swi==Mm3dDekui::ScheduleRows::FirstSVC+1) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            if(swi==Mm3dDekui::ScheduleRows::FirstSVC) {
                notebook_hook.schedule_rows.Enter(guest,regs[0]);regs[4]=regs[0];
            } else {
                regs[1]=0; // Native stop/reset path for Schedule vertical scrolling.
                regs[2]=0; // Displaced MOV r2,#0.
            }
            return;
        }
        if(swi==Mm3dDekui::NotebookRows::SyncSVC) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            if(notebook_hook.row_window.ImmediateScroll(guest,regs[0],regs[2],regs[3])) {
                regs[2]|=8; // Populate the changed window now.
                regs[3]=0; // Native reset path, without the deferred scroll animation.
            }
            regs[4]=regs[0];return;
        }
        if(swi==Mm3dDekui::NotebookRows::FirstSVC||swi==Mm3dDekui::NotebookRows::FirstSVC+1) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            if(swi==Mm3dDekui::NotebookRows::FirstSVC){notebook_hook.row_window.Enter(guest,regs[0]);regs[4]=regs[0];}
            else {notebook_hook.row_window.Leave(guest);regs[0]=regs[4];}
            return;
        }
        if(swi==Mm3dDekui::NativeNotebook::MapInputSVC||swi==0xffffd3) {
            auto& guest=memory;
            const auto& regs=cpu.Regs();
            cpu.SetCpsr(swi==0xffffd3 ?
                Mm3dDekui::NativeNotebook::ScheduleInputFlags(guest,regs[4],regs[3],cpu.Cpsr()) :
                Mm3dDekui::NativeNotebook::MapInputFlags(guest,regs[4],regs[3],cpu.Cpsr()));
            return;
        }
        if(swi==Mm3dDekui::NativeNotebook::SVC) {
            auto& guest=memory;
            auto& regs=cpu.Regs();notebook_hook.Capture(guest,regs[0]);regs[4]=regs[0];return;
        }
        if(swi==Mm3dDekui::SongList::NavSVC){
            auto& r=cpu.Regs();
            if(Mm3dDekui::SongList::enabled&&song_hook.panel&&memory.Read32OrNullopt(r[4]+0x14)==std::optional<U32>(song_hook.panel)){
                auto context=memory.Read32OrNullopt(r[7]+4);
                auto input=context?memory.Read32OrNullopt(*context+0x16c):std::optional<U32>{};
                auto a=input?memory.Read32OrNullopt(*input+0x34):std::optional<U32>{};
                auto b=input?memory.Read32OrNullopt(*input+0x8c):std::optional<U32>{};
                auto flags=memory.Read32OrNullopt(r[5]+0x1f8),mask=memory.Read32OrNullopt(r[6]+0x1c);
                if(a&&b&&flags&&mask)r[2]=Mm3dDekui::SongList::Selection(r[1],*a|*b,(*flags&*mask)!=0);
            }
            const U32 a=r[1],b=r[2],d=a-b;
            const U32 flags=(d&0x80000000u)|(d==0?0x40000000u:0)|(a>=b?0x20000000u:0)|(((a^b)&(a^d)&0x80000000u)>>3);
            cpu.SetCpsr((cpu.Cpsr()&0x0fffffffu)|flags);return;
        }
        if(swi==Mm3dDekui::Music::SVC) {
            auto& guest=memory;
            const auto& regs=cpu.Regs();
            Mm3dDekui::Music::Store(guest,regs[4],regs[5],cpu.ExtRegs()[16]);return;
        }
        if(swi==Mm3dDekui::MenuSound::SVC) {
            auto& guest=memory;
            auto& r=cpu.Regs();
            Mm3dDekui::MenuSound::Store(guest,menu_sound_context,r[4],r[0]);return;
        }
        if(swi>=Mm3dDekui::Prices::FirstSVC&&swi<Mm3dDekui::Prices::FirstSVC+Mm3dDekui::Prices::Sites.size()) {
            auto& guest=memory;
            Mm3dDekui::Prices::Execute(guest,cpu.Regs(),swi-Mm3dDekui::Prices::FirstSVC,Mm3dDekui::Prices::enabled.load());
            return;
        }
        if(swi==Mm3dDekui::SingleScreenText::SVC) {
            auto& guest=memory;
            auto& r=cpu.Regs();
            Mm3dDekui::SingleScreenText::Apply(guest,r[0],r[1]);
            if(Mm3dDekui::Prices::CodeMatches(guest))Mm3dDekui::Prices::Message(guest,r[0],r[1],Mm3dDekui::Prices::enabled.load());
            r[3]=memory.Read32(r[0]+4); // original LDR r3,[r0,#4]
            return;
        }
        if(swi>=Mm3dDekui::MenuMemory::FirstSVC&&swi<Mm3dDekui::MenuMemory::FirstSVC+Mm3dDekui::MenuMemory::PCs.size()) {
            auto& guest=memory;
            auto& r=cpu.Regs();
            if(swi==Mm3dDekui::MenuMemory::FirstSVC) {
                if(!menu_memory.Prepare(guest,r)) {
                    r[15]=r[2]&~1u;
                    cpu.SetCpsr((cpu.Cpsr()&~0x20u)|((r[2]&1u)<<5));
                }
            } else if(swi==Mm3dDekui::MenuMemory::FirstSVC+1)menu_memory.Ready(guest,r[4],r[0]);
            else menu_memory.GearOpenCapture(guest,r[0],r[6],r[4],r[7]);
            return;
        }
        if(swi>=Mm3dDekui::MenuPageSwitch::FirstSVC&&swi<Mm3dDekui::MenuPageSwitch::FirstSVC+Mm3dDekui::MenuPageSwitch::PCs.size()) {
            auto& guest=memory;
            auto& r=cpu.Regs();
            switch(swi-Mm3dDekui::MenuPageSwitch::FirstSVC) {
            case 0:
                menu_sound_context=r[0];
                if(Mm3dDekui::MenuMemory::CodeMatches(guest))menu_memory.Tick(guest,r[0]);
                menu_page_switch.Tick(guest,r[0]);r[4]=r[0];break;
            case 1:
                r[14]=Mm3dDekui::MenuPageSwitch::PCs[1]+4;
                if(menu_page_switch.PairPending(guest,r[1]))r[0]=1;
                else {r[15]=r[2]&~1u;cpu.SetCpsr((cpu.Cpsr()&~0x20u)|((r[2]&1u)<<5));}
                break;
            case 2:
                if(menu_page_switch.FinishLayout(guest,r[0]))cpu.ExtRegs()[0]=std::bit_cast<U32>(10.f);
                r[4]=r[0];break;
            case 6: cpu.ExtRegs()[1]=Mm3dDekui::MenuPageSwitch::GearBackdropAlpha();break;
            case 7: menu_page_switch.MapCloseSound(guest,r);break;
            default: menu_page_switch.CaptureCall(guest,r,swi-Mm3dDekui::MenuPageSwitch::FirstSVC);break;
            }
            return;
        }
        if(swi>=Mm3dDekui::InventoryNavigation::FirstSVC&&swi<=Mm3dDekui::InventoryNavigation::FirstSVC+2) {
            auto& guest=memory;
            auto& regs=cpu.Regs();const bool vertical=swi==Mm3dDekui::InventoryNavigation::FirstSVC;
            if(swi==Mm3dDekui::InventoryNavigation::FirstSVC+2) {
                regs[2]|=regs[12];
                if(auto next=Mm3dDekui::InventoryNavigation::GearMove(guest,regs[0],regs[2])) {
                    const auto current=memory.Read8(regs[0]+0x195);
                    regs[2]=*next;regs[15]=*next==current?0x005a5850:0x005a58bc; // ref 0x5a56e0, 0x5a574c
                }
                return;
            }
            if(Mm3dDekui::InventoryNavigation::PresentMove(guest,regs[4],regs[13],vertical,regs[5])||
               Mm3dDekui::InventoryNavigation::Move(guest,regs[4],regs[13],vertical,regs[5]))
                regs[15]=vertical?0x005a1020:0x005a1630; // ref 0x5a0eb0, 0x5a14c0
            else regs[1]=memory.Read8(regs[4]+(vertical?0x19:0x18));
            return;
        }
        if(swi==Mm3dDekui::NativeItemPresent::IntroSVC) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            regs[14]=Mm3dDekui::NativeItemPresent::IntroPC+4;
            if(Mm3dDekui::NativeItemPresent::SkipIntro(guest,regs[5])) {
                regs[0]=1;regs[15]=regs[14]; // Native caller closes the message and advances focus.
            } else regs[15]=0x001ad990; // Replay the original BL for unsupported/disabled cases. // ref 0x1adb90
            return;
        }
        if(swi>=Mm3dDekui::PauseCapture::FirstSVC&&swi<Mm3dDekui::PauseCapture::FirstSVC+3) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            switch(swi-Mm3dDekui::PauseCapture::FirstSVC) {
            case 0: pause_capture_hook.Convert(guest,regs[7],regs[8],regs[4],regs[5]);regs[0]=regs[7]+(regs[8]<<2);break;
            case 1: pause_capture_hook.Bind(guest,regs[3],regs[2]);regs[12]=regs[0]+0x1c;break;
            case 2: pause_capture_hook.Reset();regs[4]=regs[0];break;
            }
            return;
        }
        if(swi>=Mm3dDekui::NativeInventory::FirstSVC && swi<Mm3dDekui::NativeInventory::FirstSVC+4) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            switch(swi-Mm3dDekui::NativeInventory::FirstSVC) {
            case 0: inventory_hook.Capture(guest,regs[0]); regs[4]=regs[0]; break;
            case 1: options_hook.Pass(guest,regs[0],regs[1]); song_reset_hook.Pass(guest,regs[0],regs[1]); double_time_hook.Pass(guest,regs[0],regs[1]); dungeon_map_hook.Pass(guest,regs[0],regs[1]); song_hook.Pass(guest,regs[0],regs[1]); ocarina_hook.Pass(guest,regs[0],regs[1]); item_present_hook.Pass(guest,regs[0],regs[1]); press_hook.Pass(guest,regs[0],regs[1]); inventory_hook.Pass(guest,regs[0],regs[1]); notebook_hook.Pass(guest,regs[0],regs[1]); file_select_hook.Pass(guest,regs[0],regs[1]);
                if(regs[1]==5&&file_select_hook.route_status!=file_select_hook.last_route_status) {
                    HostLog("MM3D FileSelect: active={} stage={} panel={:08x} summary={:08x} popup={:08x}",
                        file_select_hook.active,file_select_hook.route_status,file_select_hook.panel,file_select_hook.summary,file_select_hook.popup);
                    file_select_hook.last_route_status=file_select_hook.route_status;
                }
                banner_hook.Pass(guest,regs[0],regs[1]); regs[4]=regs[0]; break;
            case 2: minigame_hook.Begin(guest,regs[4]); hud_buttons_hook.Begin(guest,regs[4]); song_reset_hook.Begin(guest,regs[4]); banner_hook.Begin(guest,regs[4],WidescreenActive()); double_time_hook.Begin(guest,regs[4]); dungeon_map_hook.Begin(guest,regs[4]); song_hook.Begin(guest,regs[4]); item_present_hook.Begin(guest,regs[4]); loading_icon_hook.Begin(guest,regs[4]); press_hook.Begin(guest,regs[4]); inventory_hook.Begin(guest,regs[4],WidescreenActive()); notebook_hook.Begin(guest,regs[4],WidescreenActive()); file_select_hook.Begin(guest,regs[4],WidescreenActive());
                options_hook.Begin(guest,regs[4]);
                player_hud_hook.Begin(guest,regs[4]);
                regs[2]=notebook_hook.PromptRoot(guest,regs[4],regs[13]);
                {const auto root=ocarina_hook.Root(guest,regs[4],regs[13],WidescreenActive());
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=magic_hook.Root(guest,regs[4],regs[13]);
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=song_hook.ShadowRoot(guest,regs[4],regs[13]);
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=inventory_shadow_hook.Root(guest,inventory_hook,regs[4],regs[13],&item_present_hook);
                 if(root!=regs[4]+8)regs[2]=root;}
                items_map_prompt_hook.ResolveDescription(guest,item_preview_hook.owner,inventory_hook.panel);
                {const auto existing_root=regs[2];
                 const auto prompt_sp=existing_root!=regs[4]+8?existing_root:regs[13];
                 const auto root=items_map_prompt_hook.Root(guest,inventory_hook,regs[4],
                    prompt_sp,existing_root,WidescreenActive(),
                    GlobalTimeUs());
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=dungeon_map_hook.PromptRoot(guest,regs[4],regs[13],WidescreenActive());
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=dungeon_map_hook.PanelRoot(guest,regs[4],regs[13]);
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=dungeon_map_hook.ShadowRoot(guest,regs[4],regs[13]);
                 if(root!=regs[4]+8)regs[2]=root;}
                regs[2]=map_items_prompt_hook.MapRoot(guest,dungeon_map_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2],WidescreenActive(),
                    GlobalTimeUs());
                regs[2]=world_map_prompt_hook.Root(guest,dungeon_map_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2],WidescreenActive());
                regs[2]=menu_tabs_hook.Root(guest,inventory_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2]);
                menu_help_hook.source.ResolveDescription(guest,item_preview_hook.owner,inventory_hook.panel);
                regs[2]=menu_help_hook.Root(guest,inventory_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2]);
                if(!item_present_hook.IsDescription(guest,select_help_hook.source.description))select_help_hook.source.description=0;
                regs[2]=select_help_hook.Root(guest,inventory_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2],&item_present_hook);
                regs[2]=file_select_prompt_hook.Root(guest,file_select_hook,options_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2],WidescreenActive());
                if(host.layout_draw)host.layout_draw(guest,regs[4],regs[2],inventory_hook.pass);
                if(regs[2]!=regs[4]+8)regs[13]=regs[2];
                break;
            case 3:
                if(const auto sp=file_select_prompt_hook.End(guest))regs[13]=sp;
                if(const auto sp=select_help_hook.scratch.End(guest))regs[13]=sp;
                if(const auto sp=menu_help_hook.scratch.End(guest))regs[13]=sp;
                if(const auto sp=menu_tabs_hook.scratch.End(guest))regs[13]=sp;
                if(const auto sp=world_map_prompt_hook.End(guest))regs[13]=sp;
                if(const auto sp=map_items_prompt_hook.prompt.End(guest))regs[13]=sp;
                if(const auto sp=dungeon_map_hook.shadow_scratch.End(guest))regs[13]=sp;
                if(const auto sp=dungeon_map_hook.panel_scratch.End(guest))regs[13]=sp;
                if(const auto sp=dungeon_map_hook.map_prompts.scratch.End(guest))regs[13]=sp;
                if(const auto sp=items_map_prompt_hook.prompt.End(guest))regs[13]=sp;
                if(const auto sp=inventory_shadow_hook.scratch.End(guest))regs[13]=sp;
                if(const auto sp=song_hook.shadow_scratch.End(guest))regs[13]=sp;
                if(const auto sp=magic_hook.End(guest))regs[13]=sp;
                if(const auto sp=ocarina_hook.End(guest))regs[13]=sp;
                if(const auto sp=notebook_hook.prompt_hook.End(guest))regs[13]=sp;
                player_hud_hook.End(guest);
                options_hook.End(guest);
                song_hook.End(guest); item_present_hook.End(guest); file_select_hook.End(guest); notebook_hook.End(guest); inventory_hook.End(guest); press_hook.End(guest); loading_icon_hook.End(guest); dungeon_map_hook.End(guest); double_time_hook.End(guest); banner_hook.End(guest); song_reset_hook.End(guest); hud_buttons_hook.End(guest); minigame_hook.End(guest); guest.Write(regs[6],regs[0]); break;
            }
            return;
        }
        if(swi==Mm3dDekui::DialogueScale::BeforeSVC || swi==Mm3dDekui::DialogueScale::AfterSVC) {
            auto& pose=memory;
            const auto layout=cpu.Regs()[4];
            if(swi==Mm3dDekui::DialogueScale::BeforeSVC) {
                const bool description=item_preview_hook.Description(pose,layout);
                const bool present_description=item_present_hook.IsDescription(pose,layout);
                if(present_description){select_help_hook.source.description=layout;
                    select_help_hook.source.description_panel=item_present_hook.panel;}
                const auto& setting=Mm3dDekui::NativeItemPresent::poses[Mm3dDekui::NativeItemPresent::Description];
                const Mm3dDekui::GearMenu::Pose description_pose{setting.x.load(),setting.y.load(),setting.scale.load()};
                const bool map_description=dungeon_map_hook.IsDescription(pose,layout);
                const auto& map_setting=Mm3dDekui::DungeonMap::poses[Mm3dDekui::DungeonMap::Description];
                const Mm3dDekui::GearMenu::Pose map_pose{map_setting.x.load(),map_setting.y.load(),map_setting.scale.load()};
                const bool reset_description=song_reset_hook.IsDescription(pose,layout);
                const auto& reset_setting=Mm3dDekui::SongReset::poses[Mm3dDekui::SongReset::Description];
                const Mm3dDekui::GearMenu::Pose reset_pose{reset_setting.x.load(),reset_setting.y.load(),reset_setting.scale.load()};
                const bool options_description=options_hook.IsDescription(pose,layout);
                dialogue_hook.Begin(pose,layout,description,description&&item_preview_hook.gear,
                    options_description?&options_hook.frame.description:reset_description?&reset_pose:map_description?&map_pose:present_description?&description_pose:nullptr);
                cpu.Regs()[0]=layout+8; // displaced ADD r0,r4,#8
            } else {
                dialogue_hook.End(pose,layout);
                cpu.Regs()[13]+=8; // displaced ADD sp,sp,#8
            }
            return;
        }
        if(swi>=Mm3dDekui::DungeonMap::FirstSVC&&swi<Mm3dDekui::DungeonMap::FirstSVC+Mm3dDekui::DungeonMap::PCs.size()) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            switch(swi-Mm3dDekui::DungeonMap::FirstSVC) {
            case 0: dungeon_map_hook.Capture(guest,regs[0],regs[1]);regs[4]=regs[0];break;
            case 1:
                dungeon_map_hook.BeforeMap(guest);
                regs[14]=Mm3dDekui::DungeonMap::PCs[1]+4;regs[15]=0x002a3120;break; // ref 0x2a326c
            case 2: dungeon_map_hook.AfterMap(guest);regs[0]=memory.Read8(regs[4]+0x74c);break;
            case 3:
                if(dungeon_map_hook.valid) Mm3dDekui::DungeonMap::LogInput(guest,regs[4],memory.Read8(regs[6]+0xc));
                regs[0]=memory.Read8(regs[6]+0xc);
                regs[0]=Mm3dDekui::DungeonMap::FilterA(guest,regs[4],regs[0]);
                break;
            case 4:
                regs[0]=memory.Read32(regs[4]+0x1c);
                if(dungeon_map_hook.SkipWorld(guest,regs[4]))regs[15]=Mm3dDekui::DungeonMap::WorldEnd;
                break;
            }
            return;
        }
        if(swi==Mm3dDekui::OptionsMenu::DrawSVC||swi==Mm3dDekui::OptionsMenu::SelectSVC) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            if(swi==Mm3dDekui::OptionsMenu::DrawSVC)options_hook.Capture(guest,regs[0]);
            else regs[2]=Mm3dDekui::OptionsMenu::Selection(guest,regs[0],regs[2]);
            regs[4]=regs[0];return;
        }
        if (swi==Mm3dDekui::NativeMinimap::BeforeSVC || swi==Mm3dDekui::NativeMinimap::AfterSVC) {
            auto& map_memory=memory;
            const auto screen=cpu.Regs()[4];
            if (swi==Mm3dDekui::NativeMinimap::BeforeSVC) {
                minimap_hook.Begin(map_memory,screen,Mm3dDekui::Minimap::enabled.load());
            } else {
                minimap_hook.End(map_memory,screen,Mm3dDekui::Minimap::enabled.load(),
                    Mm3dDekui::Minimap::scale.load(),Mm3dDekui::Minimap::opacity.load(),
                    WidescreenActive() ? 400.f+(240.f*16.f/9.f-400.f)/2.f : 400.f);
                // Emulate the displaced LDR r0,[r4,#0x38], including paths which skip map drawing.
                cpu.Regs()[0]=memory.Read32(screen+0x38);
            }
            return;
        }
        if(swi>=Mm3dDekui::SongEffect::FirstSVC&&swi<Mm3dDekui::SongEffect::FirstSVC+Mm3dDekui::SongEffect::Loads.size()) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            Mm3dDekui::SongEffect::Expand(guest,regs[13],Mm3dDekui::CameraLive::desired_fov.load(),
                WidescreenActive(),Mm3dDekui::Widescreen::camera_zoom_out.load());
            regs[1]=regs[13]; // displaced MOV, including guarded fallback
            return;
        }
        if(swi==Mm3dDekui::FormCamera::EntrySVC||swi==Mm3dDekui::FormCamera::RadiusSVC||swi==Mm3dDekui::FormCamera::LockSVC) {
            auto& guest=memory;
            auto& regs=cpu.Regs();
            if(swi==Mm3dDekui::FormCamera::EntrySVC) {
                const bool native_entry_orbit=entry_recovery_orbit.Native(guest,regs[0]);
                form_camera.Entry(guest,regs,native_entry_orbit);
                const auto deku_lens_before=guest.Read(regs[0]+0x110).value_or(0);
                deku_free_camera.Step(guest,regs[0]);
                if(Mm3dDekui::FormCamera::diagnostics.load()) {
                    const auto actor=guest.Read(regs[0]+0xa4).value_or(0);
                    HostLog("Deku free camera: armed={} recovering={} flags3={:08x} ground={:08x} lens_before={} lens_after={}",
                        deku_free_camera.armed,deku_free_camera.recovering,
                        actor?guest.Read(actor+0x11db8).value_or(0):0,
                        actor?guest.Read(actor+0x94).value_or(0):0,
                        std::bit_cast<float>(deku_lens_before),
                        std::bit_cast<float>(guest.Read(regs[0]+0x110).value_or(0)));
                }
                const bool ocarina_before=ocarina_return_lens.pending||ocarina_return_lens.active;
                const auto ocarina_bits=ocarina_before?guest.Read(regs[0]+0x110).value_or(0):0;
                ocarina_return_lens.Step(guest,regs[0]);
                if(ocarina_before&&Mm3dDekui::FormCamera::diagnostics.load())
                    HostLog("Ocarina return lens: cam={:08x} active={} before={} after={}",
                        regs[0],ocarina_return_lens.active,std::bit_cast<float>(ocarina_bits),
                        std::bit_cast<float>(guest.Read(regs[0]+0x110).value_or(0)));
                const bool arrival_before=arrival_lens.active;
                const auto arrival_bits=guest.Read(regs[0]+0x110).value_or(0);
                const auto expected_bits=arrival_lens.last_bits;
                arrival_lens.Step(guest,regs[0]);
                if((arrival_before||arrival_lens.active)&&Mm3dDekui::FormCamera::diagnostics.load())
                    HostLog("Arrival lens step: active={} setting={} normal={} before={} expected={} after={} rate={} orbit={} orbit_target={}",
                        arrival_lens.active,guest.Read(regs[0]+0x154).value_or(0)>>16,
                        arrival_lens.NormalFov(guest,guest.Read(regs[0]+0x154).value_or(0)>>16).value_or(-1.f),
                        std::bit_cast<float>(arrival_bits),std::bit_cast<float>(expected_bits),
                        std::bit_cast<float>(guest.Read(regs[0]+0x110).value_or(0)),
                        std::bit_cast<float>(guest.Read(regs[0]+0xe8).value_or(0)),
                        arrival_lens.radius,arrival_lens.radius_target);
                if(Mm3dDekui::FormCamera::diagnostics.load())
                    HostLog("Form camera entry: cam={:08x} phase={:08x} target={:08x} radius={} saved={} native_entry_orbit={}",
                        regs[4],guest.Read(regs[4]+0x170).value_or(0),guest.Read(regs[4]+0xbc).value_or(0),
                        form_camera.radius,form_camera.saved,native_entry_orbit);
            }
            else if(swi==Mm3dDekui::FormCamera::RadiusSVC) {
                const auto before=guest.Read(regs[13]);
                form_camera.Radius(guest,regs);
                arrival_lens.Radius(guest,regs,form_camera);
                if(Mm3dDekui::FormCamera::diagnostics.load()) {
                    const auto after=guest.Read(regs[13]);
                    HostLog("Form camera orbit: cam={:08x} native={} applied={} saved={}",
                        regs[4],before?std::bit_cast<float>(*before):0.f,
                        after?std::bit_cast<float>(*after):0.f,form_camera.saved);
                }
            } else {
                const auto requested=regs[1];
                const auto cam=regs[0];
                form_camera.LockMode(guest,regs);
                if(Mm3dDekui::FormCamera::diagnostics.load()) {
                    const auto target=form_camera.Target(guest,cam,true);
                    const U32 player=target?(*target)[0]:0;
                    HostLog("Form camera lock request: cam={:08x} requested={} mode={} native_lock_allowed={} camera_flags={:08x} camera_target={:08x} player_flags={:08x} player_target={:08x}",
                        cam,requested,guest.Read(cam+0x158).value_or(0)&65535,
                        regs[15]==regs[14]&&regs[0]==0,
                        guest.Read(cam+0x170).value_or(0),guest.Read(cam+0xbc).value_or(0),
                        player?guest.Read(player+0x11db0).value_or(0):0,
                        player?guest.Read(player+0xdfc).value_or(0):0);
                }
            }
            return;
        }
        if(swi==Mm3dDekui::FormCamera::SVC) {
            auto& guest=memory;
            const auto cam=cpu.Regs()[4];
            const float before_s0=std::bit_cast<float>(cpu.ExtRegs()[0]);
            const float before_s1=std::bit_cast<float>(cpu.ExtRegs()[1]);
            const auto mode=guest.Read(cam+0x158),native=guest.Read(cam+0xf0);
            form_camera.Distance(guest,cam,cpu.ExtRegs());
            Mm3dDekui::FormCamera::DekuFreeCamera::Distance(guest,cam,cpu.ExtRegs());
            const bool capture=Mm3dDekui::FormCamera::diagnostics.load();
            if(capture&&!form_trace_was_active) {form_trace_count=form_projection_count=0;form_last_native=form_last_output=form_last_radius=-1.f;form_last_mode=~0u;}
            form_trace_was_active=capture;
            if(capture) {
                ++form_trace_count;
                HostLog("Form camera distance: cam={:08x} mode={} native_radius={} s0_before={} s1_before={} s0_after={} s1_after={} saved_radius={} saved={}",
                    cam,mode?(*mode&65535):999u,native?std::bit_cast<float>(*native):0.f,
                    before_s0,before_s1,std::bit_cast<float>(cpu.ExtRegs()[0]),
                    std::bit_cast<float>(cpu.ExtRegs()[1]),form_camera.radius,form_camera.saved);
            }
            return;
        }
        if(swi==Mm3dDekui::CameraLive::ChangeModeSVC) {
            auto& guest=memory;
            const auto cam=cpu.Regs()[0];
            const auto requested=cpu.Regs()[1];
            const auto mode_before=guest.Read(cam+0x158);
            const auto effective=form_camera.RequestedMode(guest,cam,requested);
            cpu.Regs()[1]=effective;
            const bool arrival_before=arrival_lens.active;
            arrival_lens.Observe(guest,cam,effective);
            ocarina_return_lens.Observe(guest,cam,effective);
            form_camera.ChangeMode(guest,cam,effective);
            entry_recovery_orbit.Observe(guest,cam,effective);
            deku_free_camera.Observe(cam,effective);
            if(Mm3dDekui::FormCamera::diagnostics.load()) {
                const auto player=guest.Read(cam+0xa4).value_or(0);
                HostLog("Arrival lens mode: before={} active={} setting={} normal={} native={} ground={:08x} requested={}",
                    arrival_before,arrival_lens.active,guest.Read(cam+0x154).value_or(0)>>16,
                    arrival_lens.NormalFov(guest,guest.Read(cam+0x154).value_or(0)>>16).value_or(-1.f),
                    std::bit_cast<float>(guest.Read(cam+0x110).value_or(0)),
                    player?guest.Read(player+0x94).value_or(0):0,effective);
                ++form_trace_count;
                HostLog("Form camera mode: cam={:08x} requested={} mode_before={} effective={} radius={} saved={}",
                    cam,requested,mode_before?(*mode_before&65535):999u,effective,form_camera.radius,form_camera.saved);
            }
            cpu.Regs()[4]=cam+0x100; // displaced ADD; native mode selection retained
            return;
        }
        if(swi==Mm3dDekui::CameraLive::ProjectionSVC) {
            // SVC is a synchronization boundary: R4 and S0 are live here.
            // Emulate the replaced VLDR, changing only its destination register.
            const U32 native=memory.Read32(cpu.Regs()[4]+0x18);
            auto& guest=memory;
            const auto projection=cpu.Regs()[4];
            const float configured=Mm3dDekui::CameraLive::ConfiguredFov(guest,projection);
            const auto output=Mm3dDekui::CameraLive::ProjectionValue(native,configured);
            cpu.ExtRegs()[0]=output;
            // Manual form capture remains available for all supported forms, including Deku.
            if(Mm3dDekui::FormCamera::diagnostics.load()) {
                const auto ctx=memory.Read32(0x72f5c0);
                std::optional<U32> game,player,form;
                if(ctx) game=memory.Read32(ctx+0x16c);
                if(game) player=memory.Read32(*game+0x20dc);
                if(player) form=memory.Read32(*player+0x1fc);
                const bool form_match=form&&(Mm3dDekui::FormCamera::SupportedForm(*form>>24)||(*form>>24)==3);
                if(form_match&&memory.Read32(Mm3dDekui::CameraLive::CameraPointer)==projection) {
                    const auto indices=game?memory.Read32(*game+0xa78):std::optional<U32>{};
                    const unsigned index=indices?(*indices>>16):99;
                    const auto camera=(game&&index<4)?(*game+0x408+index*0x198):0;
                    const auto mode=camera?memory.Read32(camera+0x158):std::optional<U32>{};
                    const auto radius=camera?memory.Read32(camera+0xf0):std::optional<U32>{};
                    const U32 mode_value=mode?(U32(*mode)&65535):999u;
                    const float native_value=std::bit_cast<float>(native),output_value=std::bit_cast<float>(output),
                        radius_value=radius?std::bit_cast<float>(*radius):0.f;
                    std::array<U32,6> pose{};
                    for(unsigned axis=0;axis<6;++axis)pose[axis]=memory.Read32(projection+0x28+axis*4);
                    const bool changed=pose!=form_last_pose||mode_value!=form_last_mode||std::abs(native_value-form_last_native)>.05f||
                        std::abs(output_value-form_last_output)>.05f||std::abs(radius_value-form_last_radius)>.05f;
                    if(changed) {
                        ++form_projection_count;form_last_pose=pose;form_last_mode=mode_value;form_last_native=native_value;
                        form_last_output=output_value;form_last_radius=radius_value;
                        HostLog("Form camera projection: form={} mode={} native_fov={} configured={} output_fov={} radius={} projection={:08x} eye=({},{},{}) at=({},{},{}) target={:08x} flags={:08x} camera={:08x} index={} setting={}",
                            *form>>24,mode_value,native_value,configured,output_value,radius_value,projection,
                            std::bit_cast<float>(pose[0]),std::bit_cast<float>(pose[1]),std::bit_cast<float>(pose[2]),
                            std::bit_cast<float>(pose[3]),std::bit_cast<float>(pose[4]),std::bit_cast<float>(pose[5]),
                            camera?guest.Read(camera+0xbc).value_or(0):0,camera?guest.Read(camera+0x170).value_or(0):0,
                            camera,index,camera?guest.Read(camera+0x154).value_or(0)>>16:0);
                    }
                }
            }
            return;
        }
}

std::vector<U32> Core::CandidatePcs() {
    return {
        0x00104da4U, // ref 0x00104dd0; menu_page_switch
        0x0010fd2cU, // ref 0x0010fd58; menu_memory
        0x0010fd50U, // ref 0x0010fd7c; menu_memory
        0x0010fd78U, // ref 0x0010fda4; menu_page_switch
        0x00111c40U, // ref 0x00111c6c; item_preview
        0x001122b8U, // ref 0x001122e4; item_preview
        0x001122c0U, // ref 0x001122ec; item_preview
        0x00120620U, // ref 0x00120648; pause_capture
        0x001262c8U, // ref 0x001262f0; native_inventory
        0x001262d4U, // ref 0x001262fc; native_inventory
        0x001267ecU, // ref 0x00126814; native_inventory
        0x001391f4U, // ref 0x0013921c; menu_sound
        0x00141d44U, // ref 0x00141d88; pause_capture
        0x001619a4U, // ref 0x00161aec; menu_page_switch
        0x00161b54U, // ref 0x00161c9c; dialogue_scale
        0x00161b5cU, // ref 0x00161ca4; dialogue_scale
        0x0016f8bcU, // ref 0x0016fa18; death_penalties
        0x001754d8U, // update 6e36dfb4
        0x0018d414U, // ref 0x0018d414, 0x0018d570; camera_live
        0x0018d428U, // ref 0x0018d428, 0x0018d584; camera_live
        0x0019870cU, // ref 0x00198868; pause_capture
        0x001e6c70U, // ref 0x001e6e80; bottle_healing
        0x00206134U, // ref 0x00206344; death_penalties
        0x0021e33cU, // update 6e36dfb4
        0x0021e34cU, // ref 0x0021e34c, 0x0021e55c; difficulty
        0x0021f140U, // ref 0x0021f350; bottle_healing
        0x0021f54cU, // update 6e36dfb4
        0x0022053cU, // ref 0x0022053c, 0x0022074c; difficulty
        0x002993f4U, // ref 0x00299558; prices
        0x00299404U, // ref 0x00299568; prices
        0x002ac82cU, // ref 0x002ac978; bottle_healing
        0x002b2be0U, // ref 0x002b2c98; prices
        0x002b2e74U, // ref 0x002b2f2c; prices
        0x002b5170U, // ref 0x002b5228; prices
        0x002b5188U, // ref 0x002b5240; prices
        0x002b9920U, // ref 0x002b99d8; prices
        0x002b9998U, // ref 0x002b9a50; prices
        0x002d2da4U, // ref 0x002d2e5c; prices
        0x002d3118U, // ref 0x002d31d0; prices
        0x002d47a8U, // ref 0x002d4860; prices
        0x002d4a3cU, // ref 0x002d4af4; prices
        0x002d7cb0U, // ref 0x002d7d68; prices
        0x002d7f80U, // ref 0x002d8038; prices
        0x002f5cb8U, // ref 0x002f5d70; prices
        0x002f5dd8U, // ref 0x002f5e90; prices
        0x0030df14U, // ref 0x0030dfc8; prices
        0x0030df98U, // ref 0x0030e04c; prices
        0x00318b98U, // ref 0x00318c4c; prices
        0x00318c58U, // ref 0x00318d0c; prices
        0x0039d538U, // ref 0x0039d570; prices
        0x0039d588U, // ref 0x0039d5c0; prices
        0x003dcf60U, // ref 0x003dce08; prices
        0x003dcfc4U, // ref 0x003dce6c; prices
        0x004291ecU, // ref 0x00429094; prices
        0x00429214U, // ref 0x004290bc; prices
        0x0042c6e4U, // ref 0x0042c58c; prices
        0x0042c710U, // ref 0x0042c5b8; prices
        0x00436d30U, // update vegetation stock
        0x00436d40U, // update vegetation stock
        0x00436d44U, // update vegetation stock
        0x00436d80U, // update vegetation stock
        0x00437018U, // update vegetation stock
        0x00437034U, // update vegetation stock
        0x00437078U, // update vegetation stock
        0x0043fb7cU, // update 6e36dfb4
        0x004476ccU, // ref 0x00447574; prices
        0x004476ecU, // ref 0x00447594; prices
        0x00447724U, // ref 0x004475cc; prices
        0x004479d4U, // ref 0x0044787c; prices
        0x004680e0U, // ref 0x00467f88; song_effect
        0x0046a950U, // ref 0x0046a7f8; prices
        0x0046ef94U, // ref 0x0046ee3c; prices
        0x0046fc30U, // ref 0x0046fad8; form_camera
        0x0046ffc4U, // ref 0x0046fe6c; form_camera
        0x004700a0U, // ref 0x0046ff48; form_camera
        0x00477724U, // ref 0x004775cc; prices
        0x0047b4a0U, // update 6e36dfb4
        0x00489d00U, // ref 0x00489ba8; song_effect
        0x00489f68U, // ref 0x00489e10; song_effect
        0x0048a1c4U, // ref 0x0048a06c; song_effect
        0x0048a3e8U, // ref 0x0048a290; song_effect
        0x0048a600U, // ref 0x0048a4a8; song_effect
        0x004cb240U, // update 6e36dfb4
        0x004cb254U, // update 6e36dfb4
        0x004cb378U, // update 6e36dfb4
        0x0050965cU, // update 6e36dfb4
        0x005097b4U, // update 6e36dfb4
        0x005097f8U, // update 6e36dfb4
        0x00517efcU, // update vegetation stock
        0x00518400U, // update vegetation stock
        0x00518414U, // update vegetation stock
        0x00518470U, // update vegetation stock
        0x00519ad0U, // update vegetation stock
        0x00535014U, // update vegetation stock
        0x00535028U, // update vegetation stock
        0x00535090U, // update vegetation stock
        0x00546cf0U, // ref 0x00546b80; prices
        0x00546d94U, // ref 0x00546c24; prices
        0x00546fc4U, // ref 0x00546e54; prices
        0x00547010U, // ref 0x00546ea0; prices
        0x0056dea0U, // update 6e36dfb4
        0x0056e290U, // update 6e36dfb4
        0x005945f0U, // ref 0x00594480; dungeon_map
        0x00596d94U, // ref 0x00596c24; dungeon_map
        0x00596e28U, // ref 0x00596cb8; dungeon_map
        0x00596e2cU, // ref 0x00596cbc; dungeon_map
        0x00596e5cU, // ref 0x00596cec; dungeon_map
        0x0059747cU, // ref 0x0059730c; menu_page_switch
        0x00599a00U, // ref 0x00599890; menu_close
        0x0059b4b4U, // ref 0x0059b344; native_minimap
        0x0059b4bcU, // ref 0x0059b34c; native_minimap
        0x0059e558U, // ref 0x0059e3e8; native_inventory
        0x005a0ff8U, // ref 0x005a0e88; inventory_navigation
        0x005a1608U, // ref 0x005a1498; inventory_navigation
        0x005a1d84U, // ref 0x005a1c14; options_menu
        0x005a1d88U, // ref 0x005a1c18; options_menu
        0x005a1fa0U, // ref 0x005a1e30; options_menu
        0x005a2e5cU, // ref 0x005a2cec; options_menu
        0x005a3330U, // ref 0x005a31c0; options_menu
        0x005a5830U, // ref 0x005a56c0; inventory_navigation
        0x005a5908U, // ref 0x005a5798; menu_close
        0x005a5924U, // ref 0x005a57b4; menu_close
        0x005a6060U, // ref 0x005a5ef0; menu_page_switch
        0x005a6138U, // ref 0x005a5fc8; item_preview
        0x005a6164U, // ref 0x005a5ff4; menu_page_switch
        0x005a6310U, // ref 0x005a61a0; menu_memory
        0x005a6cb4U, // ref 0x005a6b44; item_preview
        0x005a75ccU, // ref 0x005a745c; song_list
        0x005aad2cU, // ref 0x005aabbc; schedule_rows
        0x005ab494U, // ref 0x005ab324; notebook_rows
        0x005ab9c0U, // ref 0x005ab850; notebook_rows
        0x005abd6cU, // ref 0x005abbfc; schedule_rows
        0x005abd88U, // ref 0x005abc18; schedule_rows
        0x005abd90U, // ref 0x005abc20; schedule_rows
        0x005abdb8U, // ref 0x005abc48; schedule_rows
        0x005af3a0U, // ref 0x005af230; schedule_rows
        0x005b2634U, // ref 0x005b24c4; schedule_rows
        0x005b2768U, // ref 0x005b25f8; schedule_rows
        0x005b2788U, // ref 0x005b2618; schedule_rows
        0x005b2794U, // ref 0x005b2624; schedule_rows
        0x005b3864U, // ref 0x005b36f4; notebook_rows
        0x005b416cU, // ref 0x005b3ffc; native_notebook
        0x005bc900U, // ref 0x005bc790; native_item_present
        0x005c25d8U, // ref 0x005c2468; menu_page_switch
        0x005c267cU, // ref 0x005c250c; item_preview
        0x005c2928U, // ref 0x005c27b8; menu_page_switch
        0x005c294cU, // ref 0x005c27dc; item_preview
        0x005d3fecU, // ref 0x005d3e7c; music
        0x005edc0cU, // ref 0x005eda9c; native_startup
        0x005edc48U, // ref 0x005edad8; native_startup
    };
}
} // namespace Mm3dDekui
