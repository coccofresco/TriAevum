// SPDX-License-Identifier: GPL-2.0-or-later
// Project Restoration source/game/*.h; offsets compiled for i686 (donor size assertions pass).
// Reproduce: W/analysis/dekui/restoration_layout_probe.py + restoration_generate_layout.py.
#pragma once
#include <cstdint>
namespace Mm3dRestoration::Layout {
namespace ui_MessageTextState {
inline constexpr std::uint32_t Size=0x2f8;
inline constexpr std::uint32_t first_choice_slot=0x2d4; // int first_choice_slot
inline constexpr std::uint32_t num_choices=0x2d8; // int num_choices
}
namespace ui_MessageWindow {
inline constexpr std::uint32_t Size=0xa34;
inline constexpr std::uint32_t song=0x428; // u32 song
}
namespace CollisionInfoCylinder {
inline constexpr std::uint32_t Size=0x50;
}
namespace ui_LayoutClass {
inline constexpr std::uint32_t Size=0x24;
inline constexpr std::uint32_t num_anims=0x12; // u16 num_anims
inline constexpr std::uint32_t anims=0x14; // Anim * anims
}
namespace ui_Context {
inline constexpr std::uint32_t Size=0x178;
inline constexpr std::uint32_t game_state=0x16c; // State * game_state
}
namespace act_OdolwaDamageReaction {
inline constexpr std::uint32_t Size=0x8;
}
namespace State {
inline constexpr std::uint32_t Size=0x148;
inline constexpr std::uint32_t calc_fn=0x104; // void (*)(State *) calc_fn
inline constexpr std::uint32_t next_state_init_fn=0x10c; // void (*)(State *) next_state_init_fn
inline constexpr std::uint32_t status=0x13c; // Status status
inline constexpr std::uint32_t type=0x13d; // StateType type
}
namespace StateInfo {
inline constexpr std::uint32_t Size=0x20;
inline constexpr std::uint32_t init_fn=0x4; // void (*)(State *) init_fn
inline constexpr std::uint32_t instance_size=0x18; // u32 instance_size
inline constexpr std::uint32_t type=0x1c; // StateType type
}
namespace StateSotSave {
inline constexpr std::uint32_t Size=0x194;
inline constexpr std::uint32_t calc_fn=0x104; // void (*)(State *) calc_fn
inline constexpr std::uint32_t exit_fn=0x108; // void (*)(State *) exit_fn
inline constexpr std::uint32_t next_state_init_fn=0x10c; // void (*)(State *) next_state_init_fn
inline constexpr std::uint32_t status=0x13c; // Status status
inline constexpr std::uint32_t type=0x13d; // StateType type
inline constexpr std::uint32_t layout=0x16c; // ui::Layout * layout
inline constexpr std::uint32_t press_start_layout=0x170; // ui::Layout * press_start_layout
inline constexpr std::uint32_t message_window=0x174; // ui::MessageWindow * message_window
inline constexpr std::uint32_t text_state=0x178; // ui::MessageTextState * text_state
inline constexpr std::uint32_t branch_arrow=0x17c; // ui::Widget * branch_arrow
inline constexpr std::uint32_t ending_status=0x180; // int ending_status
inline constexpr std::uint32_t message_choice=0x184; // int message_choice
inline constexpr std::uint32_t timer=0x188; // float timer
inline constexpr std::uint32_t sotsave_status=0x190; // Status sotsave_status
}
namespace ActorList {
inline constexpr std::uint32_t Size=0xc;
inline constexpr std::uint32_t first=0x4; // act::Actor * first
inline constexpr std::uint32_t need_to_update_lists=0x8; // _Bool need_to_update_lists
}
namespace ActorLists {
inline constexpr std::uint32_t Size=0xa0;
inline constexpr std::uint32_t lists=0x10; // class std::array<struct game::ActorList, 12> lists
}
namespace GlobalContext {
inline constexpr std::uint32_t Size=0x11030;
inline constexpr std::uint32_t pad_state=0x28; // struct game::pad::State pad_state
inline constexpr std::uint32_t pad_state__input__buttons=0x30; // class rst::Flags<enum game::pad::Button> buttons
inline constexpr std::uint32_t pad_state__input__new_buttons=0x34; // class rst::Flags<enum game::pad::Button> new_buttons
inline constexpr std::uint32_t pad_state__c_stick__x_raw_last=0x80; // float x_raw_last
inline constexpr std::uint32_t pad_state__c_stick__y_raw_last=0x84; // float y_raw_last
inline constexpr std::uint32_t frame_counter=0x138; // u32 frame_counter
inline constexpr std::uint32_t type=0x13d; // StateType type
inline constexpr std::uint32_t scene=0x148; // SceneId scene
inline constexpr std::uint32_t main_camera=0x408; // struct game::Camera main_camera
inline constexpr std::uint32_t camera_idx=0xa7a; // u16 camera_idx
inline constexpr std::uint32_t pause_flags=0xaac; // class rst::Flags<enum game::PauseFlag> pause_flags
inline constexpr std::uint32_t actors=0x20b0; // struct game::ActorLists actors
inline constexpr std::uint32_t actors__field_4=0x20b4; // u8 field_4
inline constexpr std::uint32_t actors__lists=0x20c0; // class std::array<struct game::ActorList, 12> lists
inline constexpr std::uint32_t elegy_statues=0x2394; // class std::array<class game::act::ObjElegyStatue *, 5> elegy_statues
inline constexpr std::uint32_t hide_hud=0x825e; // u8 hide_hud
inline constexpr std::uint32_t ocarina_state=0x8366; // OcarinaState ocarina_state
inline constexpr std::uint32_t ocarina_song=0x836a; // OcarinaSong ocarina_song
inline constexpr std::uint32_t hud_state__num_carrots=0x8666; // u8 num_carrots
inline constexpr std::uint32_t hud_state__field_244=0x866c; // u16 field_244
inline constexpr std::uint32_t hud_state__item_btn_opacity=0x8676; // u16[5] item_btn_opacity
inline constexpr std::uint32_t hud_state__prohibit_gear_or_bottles=0x869b; // _Bool prohibit_gear_or_bottles
inline constexpr std::uint32_t hud_state__prohibit_masks=0x86a1; // _Bool prohibit_masks
inline constexpr std::uint32_t hud_state__prohibit_regular_items=0x86a3; // _Bool prohibit_regular_items
inline constexpr std::uint32_t field_C529_one_to_clear_input=0xc529; // u8 field_C529_one_to_clear_input
inline constexpr std::uint32_t field_C531=0xc531; // u8 field_C531
inline constexpr std::uint32_t field_C85A=0xc85a; // u8 field_C85A
inline constexpr std::uint32_t some_ptr=0xc860; // void * some_ptr
inline constexpr std::uint32_t some_fn1=0xcaa0; // void (*)(void *, u32) some_fn1
inline constexpr std::uint32_t some_fn2=0xcaa8; // _Bool (*)(void *) some_fn2
inline constexpr std::uint32_t field_CAAD=0xcaad; // u8 field_CAAD
inline constexpr std::uint32_t field_CAB0=0xcab0; // u32 field_CAB0
inline constexpr std::uint32_t enable_letterbox=0xccac; // _Bool enable_letterbox
inline constexpr std::uint32_t dim_overlay_alpha=0xccb0; // float dim_overlay_alpha
inline constexpr std::uint32_t undim_heart_status=0xccb8; // u8 undim_heart_status
}
namespace Camera {
inline constexpr std::uint32_t Size=0x198;
inline constexpr std::uint32_t gctx=0xa0; // GlobalContext * gctx
inline constexpr std::uint32_t state=0x156; // CameraState state
inline constexpr std::uint32_t mode=0x158; // CameraMode mode
}
namespace CameraStateInfo {
inline constexpr std::uint32_t Size=0xc;
inline constexpr std::uint32_t allowed_modes=0x0; // class rst::BitSet<32, unsigned int, enum game::CameraMode> allowed_modes
inline constexpr std::uint32_t modes=0x8; // std::array<CameraModeInfo, NumCameraModes> * modes
}
namespace CameraModeInfo {
inline constexpr std::uint32_t Size=0x8;
inline constexpr std::uint32_t handler_fn_idx=0x0; // u16 handler_fn_idx
inline constexpr std::uint32_t field_2=0x2; // u16 field_2
inline constexpr std::uint32_t field_4=0x4; // uintptr_t field_4
}
namespace CommonData {
inline constexpr std::uint32_t Size=0x140f8;
inline constexpr std::uint32_t save__extra_time_speed=0x10; // int extra_time_speed
inline constexpr std::uint32_t save__day=0x14; // int day
inline constexpr std::uint32_t save__time=0x20; // u16 time
inline constexpr std::uint32_t save__player_form=0x26; // act::Player::Form player_form
inline constexpr std::uint32_t save__anonymous_12=0x2e; // char anonymous_12
inline constexpr std::uint32_t save__anonymous_13=0x2f; // char anonymous_13
inline constexpr std::uint32_t save__player__magic=0x13f; // char magic
inline constexpr std::uint32_t save__player__magic_num_upgrades=0x147; // s8 magic_num_upgrades
inline constexpr std::uint32_t save__player__owl_statue_flags=0x14c; // u16 owl_statue_flags
inline constexpr std::uint32_t save__equipment__data=0x152; // FormEquipmentData[4] data
inline constexpr std::uint32_t save__inventory__items=0x17c; // class std::array<enum game::ItemId, 24> items
inline constexpr std::uint32_t save__inventory__masks=0x194; // class std::array<enum game::ItemId, 24> masks
inline constexpr std::uint32_t save__inventory__collect_register=0x1f8; // union game::InventoryData::CollectRegister collect_register
inline constexpr std::uint32_t save__anonymous_72=0x1258; // char anonymous_72
inline constexpr std::uint32_t save__anonymous_130=0x12a2; // char anonymous_130
inline constexpr std::uint32_t save__anonymous_138=0x12aa; // char anonymous_138
inline constexpr std::uint32_t sub1__entrance=0x3518; // int entrance
inline constexpr std::uint32_t magic_max=0x368a; // s16 magic_max
inline constexpr std::uint32_t magic_cost=0x368e; // s16 magic_cost
inline constexpr std::uint32_t field_3696=0x3696; // u16 field_3696
inline constexpr std::uint32_t time_copy=0x36a2; // u16 time_copy
inline constexpr std::uint32_t setup2=0x13620; // int setup2
inline constexpr std::uint32_t usable_btns=0x13740; // ButtonIsUsable[9] usable_btns
}
namespace HudState {
inline constexpr std::uint32_t Size=0x280;
inline constexpr std::uint32_t a_btn_opacity=0x24a; // u16 a_btn_opacity
inline constexpr std::uint32_t b_btn_opacity=0x24c; // u16 b_btn_opacity
inline constexpr std::uint32_t item_btn_opacity=0x24e; // u16[5] item_btn_opacity
inline constexpr std::uint32_t camera_opacity=0x258; // u16 camera_opacity
}
namespace StaticContext {
inline constexpr std::uint32_t Size=0x15d4;
inline constexpr std::uint32_t time_speed=0x32; // s16 time_speed
inline constexpr std::uint32_t field_D38=0xd38; // u16 field_D38
}
namespace Message {
inline constexpr std::uint32_t Size=0xcc;
inline constexpr std::uint32_t msgid=0x0; // u32 msgid
inline constexpr std::uint32_t is_flag1=0x4; // _Bool is_flag1
inline constexpr std::uint32_t is_flag1000=0x5; // _Bool is_flag1000
inline constexpr std::uint32_t is_flag2000=0x6; // _Bool is_flag2000
inline constexpr std::uint32_t flag10=0x8; // u32 flag10
inline constexpr std::uint32_t flag100=0xc; // u32 flag100
inline constexpr std::uint32_t field_10=0x10; // int field_10
inline constexpr std::uint32_t field_14=0x14; // int field_14
inline constexpr std::uint32_t field_18=0x18; // int field_18
inline constexpr std::uint32_t field_1C=0x1c; // int field_1C
inline constexpr std::uint32_t field_20=0x20; // int field_20
inline constexpr std::uint32_t texts=0x24; // MessageText[14] texts
}
namespace MessageText {
inline constexpr std::uint32_t Size=0xc;
}
namespace pad_State {
inline constexpr std::uint32_t Size=0x6c;
inline constexpr std::uint32_t input__buttons=0x8; // class rst::Flags<enum game::pad::Button> buttons
inline constexpr std::uint32_t input__new_buttons=0xc; // class rst::Flags<enum game::pad::Button> new_buttons
inline constexpr std::uint32_t input__released_buttons=0x10; // class rst::Flags<enum game::pad::Button> released_buttons
}
namespace act_Actor {
inline constexpr std::uint32_t Size=0x1f8;
inline constexpr std::uint32_t id=0x0; // Id id
inline constexpr std::uint32_t actor_type=0x2; // Type actor_type
inline constexpr std::uint32_t flags=0x4; // class rst::Flags<enum game::act::Actor::Flag> flags
inline constexpr std::uint32_t params=0x1c; // u16 params
inline constexpr std::uint32_t pos__pos=0x24; // struct TVec3<> pos
inline constexpr std::uint32_t pos__pos__y=0x28; // float y
inline constexpr std::uint32_t ztarget_pos__pos=0x3c; // struct TVec3<> pos
inline constexpr std::uint32_t flags_94=0x94; // class rst::Flags<enum game::act::Actor::Flag94> flags_94
inline constexpr std::uint32_t distance_to_link=0x9c; // float distance_to_link
inline constexpr std::uint32_t damage_table=0xa4; // DamageTable * damage_table
inline constexpr std::uint32_t life=0xbb; // s8 life
inline constexpr std::uint32_t damage=0xbc; // u8 damage
inline constexpr std::uint32_t damage_type=0xbd; // DamageType damage_type
inline constexpr std::uint32_t next=0x134; // Actor * next
inline constexpr std::uint32_t calc_fn=0x140; // MainFunc * calc_fn
inline constexpr std::uint32_t draw_fn=0x144; // MainFunc * draw_fn
}
namespace act_Player {
inline constexpr std::uint32_t Size=0x12cd0;
inline constexpr std::uint32_t angle=0xc2; // u16 angle
inline constexpr std::uint32_t item_btn_slot=0x1fa; // u8 item_btn_slot
inline constexpr std::uint32_t current_action=0x1fb; // Action current_action
inline constexpr std::uint32_t action=0x1fe; // Action action
inline constexpr std::uint32_t active_form=0x1ff; // Form active_form
inline constexpr std::uint32_t transform_mask_action=0x200; // Action transform_mask_action
inline constexpr std::uint32_t active_mask_id=0x20b; // MaskId active_mask_id
inline constexpr std::uint32_t projectile_actor=0x8e4; // Actor * projectile_actor
inline constexpr std::uint32_t state_handler_fn=0xe14; // void (*)(Player *, GlobalContext *) state_handler_fn
inline constexpr std::uint32_t flags1=0x11db0; // class rst::Flags<enum game::act::Player::Flag1> flags1
inline constexpr std::uint32_t flags3=0x11db8; // class rst::Flags<enum game::act::Player::Flag3> flags3
inline constexpr std::uint32_t pending_cutscene_id=0x11dce; // s8 pending_cutscene_id
inline constexpr std::uint32_t talk_actor=0x11dd0; // Actor * talk_actor
inline constexpr std::uint32_t action_type=0x11df1; // ActionType action_type
inline constexpr std::uint32_t timer=0x11e4e; // u16 timer
inline constexpr std::uint32_t zora_swim_a_press_duration=0x11efc; // u16 zora_swim_a_press_duration
inline constexpr std::uint32_t controller_info__state=0x129cc; // State * state
}
namespace act_FormParam {
inline constexpr std::uint32_t Size=0x28;
inline constexpr std::uint32_t run_accel=0x0; // u16 run_accel
inline constexpr std::uint32_t walk_speed=0x16; // u16 walk_speed
}
namespace act_BossOdolwa {
inline constexpr std::uint32_t Size=0x28f4;
inline constexpr std::uint32_t cycle_life=0x1fb; // s8 cycle_life
inline constexpr std::uint32_t call_bug_timer=0x1fc; // int call_bug_timer
inline constexpr std::uint32_t timer=0x202; // u16 timer
inline constexpr std::uint32_t taunting_timer=0x204; // u16 taunting_timer
inline constexpr std::uint32_t field_208=0x208; // float field_208
inline constexpr std::uint32_t field_20C=0x20c; // float field_20C
inline constexpr std::uint32_t invincibility_timer=0x210; // u16 invincibility_timer
inline constexpr std::uint32_t command=0x216; // u8 command
inline constexpr std::uint32_t actor_util=0x21c; // struct game::as::ActorUtil actor_util
inline constexpr std::uint32_t actor_util__state__position=0x264; // float position
inline constexpr std::uint32_t anim_duration=0x348; // float anim_duration
inline constexpr std::uint32_t do_not_use_shield=0x35b; // u8 do_not_use_shield
inline constexpr std::uint32_t shield_timer_2=0x360; // u16 shield_timer_2
inline constexpr std::uint32_t call_bug_counter=0x363; // u8 call_bug_counter
inline constexpr std::uint32_t field_364=0x364; // float field_364
inline constexpr std::uint32_t field_368=0x368; // float field_368
inline constexpr std::uint32_t odolwa_calc=0x13d4; // void (*)(BossOdolwa *, GlobalContext *) odolwa_calc
inline constexpr std::uint32_t odolwa_calc_prev=0x13d8; // void (*)(BossOdolwa *, GlobalContext *) odolwa_calc_prev
inline constexpr std::uint32_t collision__info=0x157c; // typename game::CollisionBodyCylinderCollection::InfoType[11] info
inline constexpr std::uint32_t intro_timer=0x1b20; // u32 intro_timer
inline constexpr std::uint32_t intro_state=0x1b24; // u16 intro_state
inline constexpr std::uint32_t field_1C35=0x1c35; // u8 field_1C35
inline constexpr std::uint32_t field_30C=0x230c; // s8 field_30C
inline constexpr std::uint32_t field_30C_delta=0x230d; // s8 field_30C_delta
inline constexpr std::uint32_t timer2=0x2318; // u32 timer2
}
namespace act_BossGoht {
inline constexpr std::uint32_t Size=0x3ac0;
inline constexpr std::uint32_t collision__info=0xff4; // typename game::CollisionBodyCylinderCollection::InfoType[20] info
inline constexpr std::uint32_t goht_flags=0x3990; // class rst::Flags<enum game::act::BossGoht::Flag> goht_flags
inline constexpr std::uint32_t field_3998=0x3998; // char field_3998
inline constexpr std::uint32_t eye_scale=0x39ac; // struct TVec3<> eye_scale
}
namespace act_BossGyorg {
inline constexpr std::uint32_t Size=0xf28;
inline constexpr std::uint32_t gyorg_calc=0x1f8; // void (*)(BossGyorg *, GlobalContext *) gyorg_calc
inline constexpr std::uint32_t eyeball_flags=0xa6a; // u8 eyeball_flags
inline constexpr std::uint32_t eyeball_scale=0xa6c; // struct TVec3<> eyeball_scale
inline constexpr std::uint32_t field_F24=0xf24; // int field_F24
}
namespace act_BossTwinmold {
inline constexpr std::uint32_t Size=0x9628;
inline constexpr std::uint32_t status=0x1fc; // Status status
inline constexpr std::uint32_t hit_counter=0x90e4; // u16 hit_counter
}
namespace act_ObjOwlStatue {
inline constexpr std::uint32_t Size=0x290;
inline constexpr std::uint32_t col_body__info__flags1=0x23a; // class rst::Flags<enum game::CollisionInfo::Flag1> flags1
inline constexpr std::uint32_t activation_timer=0x28d; // s8 activation_timer
}
namespace act_NpcBombers {
inline constexpr std::uint32_t Size=0xc0c;
inline constexpr std::uint32_t notebook_data=0x974; // void * notebook_data
inline constexpr std::uint32_t number=0xbaa; // u16 number
}
namespace act_ObjElegyStatue {
inline constexpr std::uint32_t Size=0x258;
inline constexpr std::uint32_t timer=0x251; // u8 timer
inline constexpr std::uint32_t opacity=0x252; // u16 opacity
}
namespace act_ObjStoneTowerBlock {
inline constexpr std::uint32_t Size=0x228;
inline constexpr std::uint32_t action_fn=0x220; // ActionFunc<ObjStoneTowerBlock> * action_fn
inline constexpr std::uint32_t timer=0x226; // u16 timer
}
namespace CollisionInfo {
inline constexpr std::uint32_t Size=0x28;
inline constexpr std::uint32_t attack_type=0x0; // class rst::BitSet<32, unsigned int, enum game::AttackType> attack_type
inline constexpr std::uint32_t flags1=0x16; // class rst::Flags<enum game::CollisionInfo::Flag1> flags1
inline constexpr std::uint32_t colliding_info=0x24; // CollisionInfo * colliding_info
}
namespace as_ActorUtil {
inline constexpr std::uint32_t Size=0x8c;
inline constexpr std::uint32_t state__id=0x3c; // u32 id
inline constexpr std::uint32_t field_89=0x89; // u8 field_89
}
namespace ui_MainScreen {
inline constexpr std::uint32_t Size=0x1f0;
inline constexpr std::uint32_t vtable_4=0x4; // void * vtable_4
inline constexpr std::uint32_t iconKeyPane=0xc; // Widget * iconKeyPane
inline constexpr std::uint32_t gaugeYellow_g=0x18; // Widget * gaugeYellow_g
inline constexpr std::uint32_t carrotStatus=0x1c; // Widget * carrotStatus
inline constexpr std::uint32_t camChangePane=0x20; // Widget * camChangePane
inline constexpr std::uint32_t camPicturePane=0x24; // Widget * camPicturePane
inline constexpr std::uint32_t camReturn=0x28; // Widget * camReturn
inline constexpr std::uint32_t playTouchPanel=0x38; // Layout * playTouchPanel
inline constexpr std::uint32_t icon_c_btn02=0x7c; // ItemIcon * icon_c_btn02
inline constexpr std::uint32_t rank_a_l=0x80; // NumAll * rank_a_l
inline constexpr std::uint32_t rank_ten_l=0x84; // NumAll * rank_ten_l
inline constexpr std::uint32_t rank_hundred_l=0x88; // NumAll * rank_hundred_l
inline constexpr std::uint32_t num_key_l=0x8c; // NumAll * num_key_l
inline constexpr std::uint32_t num_key_rank_ten_l=0x90; // NumAll * num_key_rank_ten_l
inline constexpr std::uint32_t transition=0x94; // AnimPlayer * transition
inline constexpr std::uint32_t magic_gauge_position=0xa4; // AnimPlayer * magic_gauge_position
inline constexpr std::uint32_t magic_gauge_value=0xa8; // AnimPlayer * magic_gauge_value
inline constexpr std::uint32_t key_change=0xb0; // AnimPlayer * key_change
inline constexpr std::uint32_t magic_use=0xb4; // AnimPlayer * magic_use
inline constexpr std::uint32_t magic_use_ani=0xbc; // Anim * magic_use_ani
inline constexpr std::uint32_t carrot_anims=0x1b0; // AnimPlayerAndAnim[6] carrot_anims
inline constexpr std::uint32_t numSmallKeys=0x1e6; // u8 numSmallKeys
inline constexpr std::uint32_t magic_max=0x1e7; // s8 magic_max
inline constexpr std::uint32_t magic=0x1e8; // s8 magic
inline constexpr std::uint32_t has_magic=0x1e9; // s8 has_magic
inline constexpr std::uint32_t magic_num_upgrades=0x1ea; // s8 magic_num_upgrades
inline constexpr std::uint32_t magic_unlimited=0x1eb; // u8 magic_unlimited
inline constexpr std::uint32_t num_carrots=0x1ed; // u8 num_carrots
}
namespace ui_ScheduleScreen {
inline constexpr std::uint32_t Size=0x7b0;
inline constexpr std::uint32_t touch_handler=0x4; // class game::ui::TouchHandler touch_handler
inline constexpr std::uint32_t btn_press_flags=0x1c; // u32 btn_press_flags
inline constexpr std::uint32_t btn_map_l=0x26c; // Layout * btn_map_l
inline constexpr std::uint32_t base1_btn_map_l=0x6f0; // Layout * base1_btn_map_l
inline constexpr std::uint32_t btn_map_anim_player=0x764; // AnimPlayer * btn_map_anim_player
inline constexpr std::uint32_t disp_btn_map_anim=0x768; // Anim * disp_btn_map_anim
}
namespace ui_Layout {
inline constexpr std::uint32_t Size=0x170;
inline constexpr std::uint32_t cl=0x4; // LayoutClass * cl
inline constexpr std::uint32_t root_widget=0x8; // class game::ui::Widget root_widget
}
namespace ui_Widget {
inline constexpr std::uint32_t Size=0x13c;
inline constexpr std::uint32_t name=0x10; // const char * name
inline constexpr std::uint32_t widgets__data=0x14; // class game::ui::Widget ** data
inline constexpr std::uint32_t widgets__size=0x18; // size_t size
inline constexpr std::uint32_t layout=0x20; // Layout * layout
inline constexpr std::uint32_t pos=0x3c; // struct game::ui::WidgetPos pos
}
namespace ui_WidgetPos {
inline constexpr std::uint32_t Size=0x50;
inline constexpr std::uint32_t translate__y=0x4; // float y
inline constexpr std::uint32_t color=0x38; // class Eigen::Matrix<float, 4, 1, 2> color
inline constexpr std::uint32_t flags=0x48; // class rst::Flags<enum game::ui::WidgetPos::Flag> flags
inline constexpr std::uint32_t active_flags=0x4c; // class rst::Flags<enum game::ui::WidgetPos::Flag> active_flags
}
namespace ui_AnimPlayer {
inline constexpr std::uint32_t Size=0x18;
inline constexpr std::uint32_t m_playing=0x8; // _Bool m_playing
inline constexpr std::uint32_t m_loop=0x9; // _Bool m_loop
inline constexpr std::uint32_t m_xa=0xa; // _Bool m_xa
inline constexpr std::uint32_t m_anim=0xc; // Anim * m_anim
inline constexpr std::uint32_t m_frame=0x10; // float m_frame
}
namespace ui_Anim {
inline constexpr std::uint32_t Size=0x18;
inline constexpr std::uint32_t name=0x8; // const char * name
}
namespace ui_PlayHud {
inline constexpr std::uint32_t Size=0x23c;
inline constexpr std::uint32_t tatl_state=0x19d; // TatlHudState tatl_state
}
namespace ui_PlayBActionIcon {
inline constexpr std::uint32_t Size=0x19c;
inline constexpr std::uint32_t a_action_text=0x170; // Widget * a_action_text
inline constexpr std::uint32_t a_action_num=0x174; // Widget * a_action_num
inline constexpr std::uint32_t text_num=0x190; // u16 text_num
inline constexpr std::uint32_t a_action_icon=0x194; // Widget * a_action_icon
inline constexpr std::uint32_t a_action_icon_l=0x198; // ItemIcon * a_action_icon_l
}
namespace ui_ItemIcon {
inline constexpr std::uint32_t Size=0x1cc;
inline constexpr std::uint32_t icon_id=0x174; // int icon_id
inline constexpr std::uint32_t icon_number=0x17c; // Widget * icon_number
inline constexpr std::uint32_t item_icon_player=0x184; // AnimPlayer * item_icon_player
}
namespace ui_NumAll {
inline constexpr std::uint32_t Size=0x188;
inline constexpr std::uint32_t color_player=0x178; // AnimPlayer * color_player
}
} // namespace Mm3dRestoration::Layout
