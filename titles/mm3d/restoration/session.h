// SPDX-License-Identifier: GPL-2.0-or-later
// State for the source port of Project Restoration (8bb2a7b8) and dekui additions.
#pragma once
#include "input.h"
#include "routine.h"
#include <array>
#include <bitset>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Mm3dRestoration {
using DekuiBridge=std::function<void(u32,oot3d::recomp::a32::GuestState&,oot3d::recomp::a32::MemoryBus&)>;
// An edit contains only caller-provided bytes from the user's own language
// resources. No game strings are supplied by the port. Full original/edited
// hashes and exact length must match before writes are staged.
struct MessageEdit {
    u16 id=0;u8 language=0;
    u64 original_hash=0,edited_hash=0;
    std::vector<u8> original,edited;
    bool price=false;
    std::optional<u32> original_metadata,edited_metadata;
};
struct PortCapabilities {bool message_getter=false;bool camera_bridge=false;};
struct Session {
    Input input;
    bool initialised=false,camera_initialised=false,is_swimming=false,use_fast_swim=true,going_back_in_time=false;
    u32 gctx=0,a_press_duration=0;
    std::optional<u8> arrow_action,arrow_slot;
    int magic_timer=-1;
    std::array<u32,5> bomber_data{};
    struct NpcPosition {float last=0;int ticks=0;};
    std::map<u32,NpcPosition> npc_positions;
    std::bitset<16> played_songs;
    bool odolwa_collided=false;
    std::optional<u32> gyorg_inhale_frames;
    struct Twin {s8 red_life=0;u16 hit_counter=0;bool sane=false;};
    std::optional<Twin> twin;
    u32 arena=0,arena_used=0,hud=0;
    // Hud2 initialised: its layout was found and built (Restoration's own
    // layouts are user-supplied RomFS files; without them there is no Hud2).
    bool hud_ready=false;
    unsigned hud_depth=0;
    std::array<u32,17> hud_widgets{};
    std::map<std::string,u32,std::less<>> strings;
    DekuiBridge dekui;
    std::vector<MessageEdit> message_edits;
    // Host-generated extension callbacks, intentionally outside the guest image.
    static constexpr u32 StoneDelay1=0x000fff00,StoneDelay2=0x000fff04;
    static constexpr u32 SotInit=0x000fff08,SotCalc=0x000fff0c,SotDestroy=0x000fff10;
    u32 sot_info=0;
};
} // namespace Mm3dRestoration
