#ifndef OOT3D_BOSS_VA_H
#define OOT3D_BOSS_VA_H

#include "oot3d/types.h"

typedef struct Oot3dBossVa Oot3dBossVa;
typedef struct Oot3dPlayState Oot3dPlayState;

typedef struct {
    float x;
    float y;
    float z;
} Oot3dVec3f;

typedef struct {
    u8 unk_00[7];
    u8 flags_07;
    u8 phase_08;
    s8 state_09;
    u8 unk_0A[0x0A];
    s16 counter_14;
    u8 unk_16[2];
    void* object_18;
} Oot3dBossVaStateTable;

#define OOT3D_BOSS_VA_OFFSET_PARAMS 0x001C
#define OOT3D_BOSS_VA_OFFSET_BODY_ACTOR_PTR 0x0124
#define OOT3D_BOSS_VA_OFFSET_SKEL_ANIME 0x01A4
#define OOT3D_BOSS_VA_OFFSET_DAMAGE_POSE_COPY 0x01D4
#define OOT3D_BOSS_VA_OFFSET_SKEL_CUR_FRAME 0x01E0
#define OOT3D_BOSS_VA_OFFSET_SKEL_PLAY_SPEED 0x01E4
#define OOT3D_BOSS_VA_OFFSET_JOINT_TABLE_PTR 0x021C
#define OOT3D_BOSS_VA_OFFSET_ACTION_FUNC 0x0F90
#define OOT3D_BOSS_VA_OFFSET_BURST 0x0F95
#define OOT3D_BOSS_VA_OFFSET_IS_DEAD 0x0F98
#define OOT3D_BOSS_VA_OFFSET_TIMER2 0x0FA0
#define OOT3D_BOSS_VA_OFFSET_DEATH_SPIN 0x0FB4
#define OOT3D_BOSS_VA_OFFSET_ZAP_NECK_POS 0x0FC4
#define OOT3D_BOSS_VA_OFFSET_ZAP_HEAD_POS 0x0FD0
#define OOT3D_BOSS_VA_OFFSET_BURST_POS 0x0FDC
#define OOT3D_BOSS_VA_OFFSET_ROT_E4 0x0FE8
#define OOT3D_BOSS_VA_OFFSET_ROT_E6 0x0FEA
#define OOT3D_BOSS_VA_OFFSET_ROT_E8_TARGET 0x0FEC
#define OOT3D_BOSS_VA_OFFSET_ROT_EA 0x0FEE
#define OOT3D_BOSS_VA_OFFSET_ROT_EC 0x0FF0
#define OOT3D_BOSS_VA_OFFSET_ROT_EE_TARGET 0x0FF2
#define OOT3D_BOSS_VA_OFFSET_ROT_F0 0x0FF4
#define OOT3D_BOSS_VA_OFFSET_ROT_F2 0x0FF6
#define OOT3D_BOSS_VA_OFFSET_ROT_F4_TARGET 0x0FF8
#define OOT3D_BOSS_VA_OFFSET_HEAD_ROT_Y 0x0FFA
#define OOT3D_BOSS_VA_OFFSET_HEAD_ROT_Z 0x0FFC
#define OOT3D_BOSS_VA_OFFSET_LIGHTNING_COLLIDER 0x1158

#define OOT3D_ACTOR_OFFSET_FLAGS 0x0004
#define OOT3D_ACTOR_OFFSET_WORLD_ROT_X 0x0034
#define OOT3D_ACTOR_OFFSET_WORLD_ROT_Y 0x0036
#define OOT3D_ACTOR_OFFSET_SHAPE_ROT_X 0x00BC
#define OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y 0x00BE
#define OOT3D_ACTOR_OFFSET_SPEED 0x006C
#define OOT3D_ACTOR_OFFSET_WORLD_POS 0x0028
#define OOT3D_BOSS_VA_BODY_OFFSET_ON_CEILING 0x0F94
#define OOT3D_PLAYER_OFFSET_STATE_FLAGS1 0x1710
#define OOT3D_PLAY_OFFSET_PLAYER_ACTOR 0x20AC
#define OOT3D_PLAY_OFFSET_ACTOR_LIST_1 0x20DC
#define OOT3D_PLAY_OFFSET_ENV_COLOR 0x31FC
#define OOT3D_PLAY_OFFSET_ENV_BLEND_SCALE 0x3214
#define OOT3D_PLAY_OFFSET_ENV_COLOR_BYTES 0x3262
#define OOT3D_PLAY_OFFSET_GAMEPLAY_FRAMES 0x5BF4
#define OOT3D_PLAY_OFFSET_COLCHK_CTX 0x5C78

#define OOT3D_BOSS_VA_ZAPPER_HOLD_STEP 0x0FA0
#define OOT3D_BOSS_VA_DAMAGE_POSE_COPY_SIZE 0x44
#define OOT3D_BOSS_VA_PARAM_ZAPPER_1 3
#define OOT3D_BOSS_VA_PARAM_ZAPPER_2 4
#define OOT3D_BOSS_VA_PARAM_ZAPPER_3 5
#define OOT3D_BOSS_VA_PLAYER_SHOCK_FLAG 0x04000000u
#define OOT3D_BOSS_VA_STATE_TABLE_ADDR 0x00516B0C
#define OOT3D_BOSS_VA_STATE_TABLE ((Oot3dBossVaStateTable*)OOT3D_BOSS_VA_STATE_TABLE_ADDR)

struct Oot3dPlayState {
    u8 pad_0000[OOT3D_PLAY_OFFSET_PLAYER_ACTOR];
    u32 player_actor_20AC;
    u8 pad_20B0[OOT3D_PLAY_OFFSET_ACTOR_LIST_1 - 0x20B0];
    u32 actor_list_1_20DC;
    u8 pad_20E0[OOT3D_PLAY_OFFSET_ENV_COLOR - 0x20E0];
    s16 env_color_31FC[10];
    u8 pad_3210[OOT3D_PLAY_OFFSET_ENV_BLEND_SCALE - 0x3210];
    float env_blend_scale_3214;
    u8 pad_3218[OOT3D_PLAY_OFFSET_ENV_COLOR_BYTES - 0x3218];
    u8 env_color_bytes_3262[4];
    u8 pad_3266[OOT3D_PLAY_OFFSET_GAMEPLAY_FRAMES - 0x3266];
    u32 gameplay_frames_5BF4;
    u8 pad_5BF8[OOT3D_PLAY_OFFSET_COLCHK_CTX - 0x5BF8];
    u8 colchk_ctx_5C78[1];
};

struct Oot3dBossVa {
    u8 pad_0000[OOT3D_ACTOR_OFFSET_FLAGS];
    u32 actor_flags_0004;
    u8 pad_0008[OOT3D_BOSS_VA_OFFSET_PARAMS - 0x0008];
    s16 params_001C;
    u8 pad_001E[OOT3D_ACTOR_OFFSET_WORLD_POS - 0x001E];
    Oot3dVec3f world_pos_0028;
    s16 world_rot_x_0034;
    s16 world_rot_y_0036;
    u8 pad_0038[OOT3D_ACTOR_OFFSET_SPEED - 0x0038];
    float speed_006C;
    u8 pad_0070[OOT3D_ACTOR_OFFSET_SHAPE_ROT_X - 0x0070];
    s16 shape_rot_x_00BC;
    s16 shape_rot_y_00BE;
    u8 pad_00C0[OOT3D_BOSS_VA_OFFSET_BODY_ACTOR_PTR - 0x00C0];
    u32 body_actor_ptr_0124;
    u8 pad_0128[OOT3D_BOSS_VA_OFFSET_SKEL_ANIME - 0x0128];
    u8 skel_anime_01A4[OOT3D_BOSS_VA_OFFSET_SKEL_CUR_FRAME - OOT3D_BOSS_VA_OFFSET_SKEL_ANIME];
    float skel_cur_frame_01E0;
    float skel_play_speed_01E4;
    u8 pad_01E8[OOT3D_BOSS_VA_OFFSET_JOINT_TABLE_PTR - 0x01E8];
    u32 joint_table_ptr_021C;
    u8 pad_0220[OOT3D_BOSS_VA_OFFSET_ACTION_FUNC - 0x0220];
    u32 action_func_0F90;
    u8 unk_0F94;
    u8 burst_0F95;
    u8 pad_0F96[OOT3D_BOSS_VA_OFFSET_IS_DEAD - 0x0F96];
    u8 is_dead_0F98;
    u8 pad_0F99[OOT3D_BOSS_VA_OFFSET_TIMER2 - 0x0F99];
    s16 timer2_0FA0;
    u8 pad_0FA2[OOT3D_BOSS_VA_OFFSET_DEATH_SPIN - 0x0FA2];
    s16 death_spin_0FB4;
    u8 pad_0FB6[OOT3D_BOSS_VA_OFFSET_ZAP_NECK_POS - 0x0FB6];
    Oot3dVec3f zap_neck_pos_0FC4;
    Oot3dVec3f zap_head_pos_0FD0;
    Oot3dVec3f burst_pos_0FDC;
    s16 rot_e4_0FE8;
    s16 rot_e6_0FEA;
    s16 rot_e8_target_0FEC;
    s16 rot_ea_0FEE;
    s16 rot_ec_0FF0;
    s16 rot_ee_target_0FF2;
    s16 rot_f0_0FF4;
    s16 rot_f2_0FF6;
    s16 rot_f4_target_0FF8;
    s16 head_rot_y_0FFA;
    s16 head_rot_z_0FFC;
    u8 pad_0FFE[OOT3D_BOSS_VA_OFFSET_LIGHTNING_COLLIDER - 0x0FFE];
    u8 lightning_collider_1158[1];
};

#if defined(__arm__)
#define OOT3D_BOSS_VA_OFFSET_ASSERT(field, offset) \
    typedef char oot3d_boss_va_offset_assert_##field[(offsetof(Oot3dBossVa, field) == (offset)) ? 1 : -1]

OOT3D_BOSS_VA_OFFSET_ASSERT(actor_flags_0004, OOT3D_ACTOR_OFFSET_FLAGS);
OOT3D_BOSS_VA_OFFSET_ASSERT(params_001C, OOT3D_BOSS_VA_OFFSET_PARAMS);
OOT3D_BOSS_VA_OFFSET_ASSERT(world_pos_0028, OOT3D_ACTOR_OFFSET_WORLD_POS);
OOT3D_BOSS_VA_OFFSET_ASSERT(world_rot_x_0034, OOT3D_ACTOR_OFFSET_WORLD_ROT_X);
OOT3D_BOSS_VA_OFFSET_ASSERT(world_rot_y_0036, OOT3D_ACTOR_OFFSET_WORLD_ROT_Y);
OOT3D_BOSS_VA_OFFSET_ASSERT(speed_006C, OOT3D_ACTOR_OFFSET_SPEED);
OOT3D_BOSS_VA_OFFSET_ASSERT(shape_rot_x_00BC, OOT3D_ACTOR_OFFSET_SHAPE_ROT_X);
OOT3D_BOSS_VA_OFFSET_ASSERT(shape_rot_y_00BE, OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y);
OOT3D_BOSS_VA_OFFSET_ASSERT(body_actor_ptr_0124, OOT3D_BOSS_VA_OFFSET_BODY_ACTOR_PTR);
OOT3D_BOSS_VA_OFFSET_ASSERT(skel_anime_01A4, OOT3D_BOSS_VA_OFFSET_SKEL_ANIME);
OOT3D_BOSS_VA_OFFSET_ASSERT(skel_cur_frame_01E0, OOT3D_BOSS_VA_OFFSET_SKEL_CUR_FRAME);
OOT3D_BOSS_VA_OFFSET_ASSERT(skel_play_speed_01E4, OOT3D_BOSS_VA_OFFSET_SKEL_PLAY_SPEED);
OOT3D_BOSS_VA_OFFSET_ASSERT(joint_table_ptr_021C, OOT3D_BOSS_VA_OFFSET_JOINT_TABLE_PTR);
OOT3D_BOSS_VA_OFFSET_ASSERT(action_func_0F90, OOT3D_BOSS_VA_OFFSET_ACTION_FUNC);
OOT3D_BOSS_VA_OFFSET_ASSERT(burst_0F95, OOT3D_BOSS_VA_OFFSET_BURST);
OOT3D_BOSS_VA_OFFSET_ASSERT(is_dead_0F98, OOT3D_BOSS_VA_OFFSET_IS_DEAD);
OOT3D_BOSS_VA_OFFSET_ASSERT(timer2_0FA0, OOT3D_BOSS_VA_OFFSET_TIMER2);
OOT3D_BOSS_VA_OFFSET_ASSERT(death_spin_0FB4, OOT3D_BOSS_VA_OFFSET_DEATH_SPIN);
OOT3D_BOSS_VA_OFFSET_ASSERT(zap_neck_pos_0FC4, OOT3D_BOSS_VA_OFFSET_ZAP_NECK_POS);
OOT3D_BOSS_VA_OFFSET_ASSERT(zap_head_pos_0FD0, OOT3D_BOSS_VA_OFFSET_ZAP_HEAD_POS);
OOT3D_BOSS_VA_OFFSET_ASSERT(burst_pos_0FDC, OOT3D_BOSS_VA_OFFSET_BURST_POS);
OOT3D_BOSS_VA_OFFSET_ASSERT(rot_e4_0FE8, OOT3D_BOSS_VA_OFFSET_ROT_E4);
OOT3D_BOSS_VA_OFFSET_ASSERT(rot_e6_0FEA, OOT3D_BOSS_VA_OFFSET_ROT_E6);
OOT3D_BOSS_VA_OFFSET_ASSERT(rot_e8_target_0FEC, OOT3D_BOSS_VA_OFFSET_ROT_E8_TARGET);
OOT3D_BOSS_VA_OFFSET_ASSERT(rot_ea_0FEE, OOT3D_BOSS_VA_OFFSET_ROT_EA);
OOT3D_BOSS_VA_OFFSET_ASSERT(rot_ec_0FF0, OOT3D_BOSS_VA_OFFSET_ROT_EC);
OOT3D_BOSS_VA_OFFSET_ASSERT(rot_ee_target_0FF2, OOT3D_BOSS_VA_OFFSET_ROT_EE_TARGET);
OOT3D_BOSS_VA_OFFSET_ASSERT(rot_f0_0FF4, OOT3D_BOSS_VA_OFFSET_ROT_F0);
OOT3D_BOSS_VA_OFFSET_ASSERT(rot_f2_0FF6, OOT3D_BOSS_VA_OFFSET_ROT_F2);
OOT3D_BOSS_VA_OFFSET_ASSERT(rot_f4_target_0FF8, OOT3D_BOSS_VA_OFFSET_ROT_F4_TARGET);
OOT3D_BOSS_VA_OFFSET_ASSERT(head_rot_y_0FFA, OOT3D_BOSS_VA_OFFSET_HEAD_ROT_Y);
OOT3D_BOSS_VA_OFFSET_ASSERT(head_rot_z_0FFC, OOT3D_BOSS_VA_OFFSET_HEAD_ROT_Z);
OOT3D_BOSS_VA_OFFSET_ASSERT(lightning_collider_1158, OOT3D_BOSS_VA_OFFSET_LIGHTNING_COLLIDER);
#undef OOT3D_BOSS_VA_OFFSET_ASSERT

#define OOT3D_PLAYSTATE_OFFSET_ASSERT(field, offset) \
    typedef char oot3d_playstate_offset_assert_##field[(offsetof(Oot3dPlayState, field) == (offset)) ? 1 : -1]

OOT3D_PLAYSTATE_OFFSET_ASSERT(player_actor_20AC, OOT3D_PLAY_OFFSET_PLAYER_ACTOR);
OOT3D_PLAYSTATE_OFFSET_ASSERT(actor_list_1_20DC, OOT3D_PLAY_OFFSET_ACTOR_LIST_1);
OOT3D_PLAYSTATE_OFFSET_ASSERT(env_color_31FC, OOT3D_PLAY_OFFSET_ENV_COLOR);
OOT3D_PLAYSTATE_OFFSET_ASSERT(env_blend_scale_3214, OOT3D_PLAY_OFFSET_ENV_BLEND_SCALE);
OOT3D_PLAYSTATE_OFFSET_ASSERT(env_color_bytes_3262, OOT3D_PLAY_OFFSET_ENV_COLOR_BYTES);
OOT3D_PLAYSTATE_OFFSET_ASSERT(gameplay_frames_5BF4, OOT3D_PLAY_OFFSET_GAMEPLAY_FRAMES);
OOT3D_PLAYSTATE_OFFSET_ASSERT(colchk_ctx_5C78, OOT3D_PLAY_OFFSET_COLCHK_CTX);
#undef OOT3D_PLAYSTATE_OFFSET_ASSERT
#endif

static inline u8* oot3d_ptr_add(void* base, u32 offset) {
    return (u8*)base + offset;
}

static inline const u8* oot3d_const_ptr_add(const void* base, u32 offset) {
    return (const u8*)base + offset;
}

static inline s16* oot3d_boss_va_s16(Oot3dBossVa* this, u32 offset) {
    return (s16*)oot3d_ptr_add(this, offset);
}

static inline u8* oot3d_boss_va_u8(Oot3dBossVa* this, u32 offset) {
    return oot3d_ptr_add(this, offset);
}

static inline float* oot3d_boss_va_float(Oot3dBossVa* this, u32 offset) {
    return (float*)oot3d_ptr_add(this, offset);
}

static inline Oot3dVec3f* oot3d_boss_va_vec3f(Oot3dBossVa* this, u32 offset) {
    return (Oot3dVec3f*)oot3d_ptr_add(this, offset);
}

static inline const Oot3dVec3f* oot3d_boss_va_const_vec3f(const Oot3dBossVa* this, u32 offset) {
    return (const Oot3dVec3f*)oot3d_const_ptr_add(this, offset);
}

static inline s16 oot3d_boss_va_params(const Oot3dBossVa* this) {
    return *(const s16*)oot3d_const_ptr_add(this, OOT3D_BOSS_VA_OFFSET_PARAMS);
}

static inline s16 oot3d_boss_va_shape_rot_y(const Oot3dBossVa* this) {
    return *(const s16*)oot3d_const_ptr_add(this, OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y);
}

static inline s16 oot3d_boss_va_shape_rot_x(const Oot3dBossVa* this) {
    return *(const s16*)oot3d_const_ptr_add(this, OOT3D_ACTOR_OFFSET_SHAPE_ROT_X);
}

static inline u32* oot3d_boss_va_actor_flags(Oot3dBossVa* this) {
    return (u32*)oot3d_ptr_add(this, OOT3D_ACTOR_OFFSET_FLAGS);
}

static inline void* oot3d_boss_va_skel_anime(Oot3dBossVa* this) {
    return oot3d_ptr_add(this, OOT3D_BOSS_VA_OFFSET_SKEL_ANIME);
}

static inline void* oot3d_boss_va_joint_table(Oot3dBossVa* this) {
    return *(void**)oot3d_ptr_add(this, OOT3D_BOSS_VA_OFFSET_JOINT_TABLE_PTR);
}

static inline void* oot3d_boss_va_body_actor(Oot3dBossVa* this) {
    return *(void**)oot3d_ptr_add(this, OOT3D_BOSS_VA_OFFSET_BODY_ACTOR_PTR);
}

static inline Oot3dVec3f* oot3d_actor_world_pos(void* actor) {
    return (Oot3dVec3f*)oot3d_ptr_add(actor, OOT3D_ACTOR_OFFSET_WORLD_POS);
}

static inline void* oot3d_play_player_actor(Oot3dPlayState* play) {
    return *(void**)oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_PLAYER_ACTOR);
}

static inline u8* oot3d_play_actor_list_1(Oot3dPlayState* play) {
    return *(u8**)oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_ACTOR_LIST_1);
}

static inline s16* oot3d_play_env_color(Oot3dPlayState* play) {
    return (s16*)oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_ENV_COLOR);
}

static inline float* oot3d_play_env_blend_scale(Oot3dPlayState* play) {
    return (float*)oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_ENV_BLEND_SCALE);
}

static inline u8* oot3d_play_env_color_bytes(Oot3dPlayState* play) {
    return oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_ENV_COLOR_BYTES);
}

static inline u32 oot3d_play_gameplay_frames(const Oot3dPlayState* play) {
    return *(const u32*)oot3d_const_ptr_add(play, OOT3D_PLAY_OFFSET_GAMEPLAY_FRAMES);
}

static inline void* oot3d_play_colchk_ctx(Oot3dPlayState* play) {
    return oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_COLCHK_CTX);
}

static inline u32 oot3d_player_state_flags1(const void* player) {
    return *(const u32*)oot3d_const_ptr_add(player, OOT3D_PLAYER_OFFSET_STATE_FLAGS1);
}

static inline void oot3d_boss_va_set_action(Oot3dBossVa* this, const void* action) {
    *(const void**)oot3d_ptr_add(this, OOT3D_BOSS_VA_OFFSET_ACTION_FUNC) = action;
}

static inline void oot3d_boss_va_set_burst(Oot3dBossVa* this, u8 burst) {
    *oot3d_ptr_add(this, OOT3D_BOSS_VA_OFFSET_BURST) = burst;
}

static inline float oot3d_actor_speed(const void* actor) {
    return *(const float*)oot3d_const_ptr_add(actor, OOT3D_ACTOR_OFFSET_SPEED);
}

static inline s32 oot3d_abs_s32(s32 value) {
    return value < 0 ? -value : value;
}

static inline void oot3d_vec3f_copy(Oot3dVec3f* dst, const Oot3dVec3f* src) {
    dst->x = src->x;
    dst->y = src->y;
    dst->z = src->z;
}

#endif
