#ifndef OOT3D_PLAYER_H
#define OOT3D_PLAYER_H

#include "oot3d/types.h"

typedef struct Oot3dPlayState Oot3dPlayState;
typedef struct Oot3dPlayer Oot3dPlayer;
typedef struct Oot3dCollisionPoly Oot3dCollisionPoly;

typedef struct {
    float x;
    float y;
    float z;
} Oot3dVec3f;

/*
 * OOT3D offsets observed in the 0032eeb4 target body. They track the N64
 * Player_ProcessSceneCollision fields, but the Player tail moved heavily in
 * the 3DS build.
 */
#define OOT3D_PLAY_OFFSET_COLCTX 0x0A98
#define OOT3D_PLAY_OFFSET_SCENE_ID 0x0104

#define OOT3D_ACTOR_OFFSET_CATEGORY 0x0002
#define OOT3D_ACTOR_OFFSET_WORLD_POS 0x0028
#define OOT3D_ACTOR_OFFSET_VELOCITY_X 0x0060
#define OOT3D_ACTOR_OFFSET_VELOCITY_Y 0x0064
#define OOT3D_ACTOR_OFFSET_VELOCITY_Z 0x0068
#define OOT3D_ACTOR_OFFSET_SPEED_XZ 0x006C
#define OOT3D_ACTOR_OFFSET_WALL_POLY 0x0078
#define OOT3D_ACTOR_OFFSET_FLOOR_POLY 0x007C
#define OOT3D_ACTOR_OFFSET_WALL_BG_ID 0x0080
#define OOT3D_ACTOR_OFFSET_FLOOR_BG_ID 0x0081
#define OOT3D_ACTOR_OFFSET_WALL_YAW 0x0082
#define OOT3D_ACTOR_OFFSET_FLOOR_HEIGHT 0x0084
#define OOT3D_ACTOR_OFFSET_DEPTH_IN_WATER 0x0088
#define OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS 0x0090
#define OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y 0x00BE

#define OOT3D_PLAYER_OFFSET_CURRENT_BOOTS 0x01A7
#define OOT3D_PLAYER_OFFSET_SKEL_ANIME 0x0254
#define OOT3D_PLAYER_OFFSET_ACTION_FUNC 0x1708
#define OOT3D_PLAYER_OFFSET_AGE_PROPERTIES 0x170C
#define OOT3D_PLAYER_OFFSET_STATE_FLAGS1 0x1710
#define OOT3D_PLAYER_OFFSET_STATE_FLAGS2 0x1714
#define OOT3D_PLAYER_OFFSET_STATE_FLAGS3 0x172A
#define OOT3D_PLAYER_OFFSET_SPEED_XZ 0x221C
#define OOT3D_PLAYER_OFFSET_YAW 0x2220
#define OOT3D_PLAYER_OFFSET_LEDGE_SIDE 0x2237
#define OOT3D_PLAYER_OFFSET_WALL_SPEED_LIMIT 0x226C
#define OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE 0x2270
#define OOT3D_PLAYER_OFFSET_DIST_TO_WALL 0x2274
#define OOT3D_PLAYER_OFFSET_LEDGE_CLIMB_TYPE 0x2278
#define OOT3D_PLAYER_OFFSET_LEDGE_DELAY_TIMER 0x2279
#define OOT3D_PLAYER_OFFSET_FLOOR_PITCH 0x2284
#define OOT3D_PLAYER_OFFSET_FLOOR_PITCH_ALT 0x2286
#define OOT3D_PLAYER_OFFSET_FLOOR_SFX_OFFSET 0x228C
#define OOT3D_PLAYER_OFFSET_LEDGE_TARGET 0x229C
#define OOT3D_PLAYER_OFFSET_LEDGE_YAW_TARGET 0x22A0
#define OOT3D_PLAYER_OFFSET_FLOOR_TYPE_TIMER 0x2489
#define OOT3D_PLAYER_OFFSET_FLOOR_PROPERTY 0x248A
#define OOT3D_PLAYER_OFFSET_PREV_FLOOR_TYPE 0x248B
#define OOT3D_PLAYER_OFFSET_PREV_FLOOR_SFX_OFFSET 0x2494
#define OOT3D_PLAYER_OFFSET_BG_UPDATE_FLAGS 0x29B8

#define OOT3D_AGE_PROPERTIES_OFFSET_UNK_0C 0x000C
#define OOT3D_AGE_PROPERTIES_OFFSET_UNK_14 0x0014
#define OOT3D_AGE_PROPERTIES_OFFSET_UNK_18 0x0018
#define OOT3D_AGE_PROPERTIES_OFFSET_UNK_1C 0x001C
#define OOT3D_AGE_PROPERTIES_OFFSET_WALL_CHECK_RADIUS 0x0038

#define OOT3D_ACTORCAT_PLAYER 2

#define OOT3D_BGCHECKFLAG_GROUND 0x0001
#define OOT3D_BGCHECKFLAG_WALL 0x0008
#define OOT3D_BGCHECKFLAG_CEILING 0x0010
#define OOT3D_BGCHECKFLAG_WATER 0x0020
#define OOT3D_BGCHECKFLAG_PLAYER_WALL_INTERACT 0x0200

#define OOT3D_PLAYER_STATE1_0 0x00000001u
#define OOT3D_PLAYER_STATE1_SCENE_BG_OFFSET 0x08000000u
#define OOT3D_PLAYER_STATE1_29 0x20000000u
#define OOT3D_PLAYER_STATE1_31 0x80000000u
#define OOT3D_PLAYER_STATE1_29_OR_31 (OOT3D_PLAYER_STATE1_29 | OOT3D_PLAYER_STATE1_31)
#define OOT3D_PLAYER_STATE2_CRAWLING 0x00040000u
#define OOT3D_PLAYER_STATE2_FORCE_SAND_FLOOR_SOUND 0x00000200u
#define OOT3D_PLAYER_STATE3_0 0x01
#define OOT3D_PLAYER_STATE3_4 0x10

#define OOT3D_UPDBGCHECKINFO_FLAG_0 0x01
#define OOT3D_UPDBGCHECKINFO_FLAG_1 0x02
#define OOT3D_UPDBGCHECKINFO_FLAG_2 0x04
#define OOT3D_UPDBGCHECKINFO_FLAG_3 0x08
#define OOT3D_UPDBGCHECKINFO_FLAG_4 0x10
#define OOT3D_UPDBGCHECKINFO_FLAG_5 0x20
#define OOT3D_UPDBGCHECKINFO_FLAG_345 \
    (OOT3D_UPDBGCHECKINFO_FLAG_3 | OOT3D_UPDBGCHECKINFO_FLAG_4 | OOT3D_UPDBGCHECKINFO_FLAG_5)
#define OOT3D_UPDBGCHECKINFO_FLAG_0345 (OOT3D_UPDBGCHECKINFO_FLAG_0 | OOT3D_UPDBGCHECKINFO_FLAG_345)
#define OOT3D_UPDBGCHECKINFO_FLAG_2345 (OOT3D_UPDBGCHECKINFO_FLAG_2 | OOT3D_UPDBGCHECKINFO_FLAG_345)
#define OOT3D_UPDBGCHECKINFO_FLAG_ALL \
    (OOT3D_UPDBGCHECKINFO_FLAG_0 | OOT3D_UPDBGCHECKINFO_FLAG_1 | OOT3D_UPDBGCHECKINFO_FLAG_2 | \
     OOT3D_UPDBGCHECKINFO_FLAG_3 | OOT3D_UPDBGCHECKINFO_FLAG_4 | OOT3D_UPDBGCHECKINFO_FLAG_5)

#define OOT3D_PLAYER_LEDGE_CLIMB_NONE 0
#define OOT3D_PLAYER_LEDGE_CLIMB_1 1
#define OOT3D_PLAYER_LEDGE_CLIMB_2 2
#define OOT3D_PLAYER_LEDGE_CLIMB_3 3
#define OOT3D_PLAYER_LEDGE_CLIMB_4 4

#define OOT3D_PLAYER_BOOTS_IRON 1
#define OOT3D_BGCHECK_SCENE 0x32

static inline u8* oot3d_player_ptr_add(void* base, u32 offset) {
    return (u8*)base + offset;
}

static inline const u8* oot3d_player_const_ptr_add(const void* base, u32 offset) {
    return (const u8*)base + offset;
}

static inline void* oot3d_play_colctx(Oot3dPlayState* play) {
    return oot3d_player_ptr_add(play, OOT3D_PLAY_OFFSET_COLCTX);
}

static inline u8* oot3d_player_u8(Oot3dPlayer* player, u32 offset) {
    return oot3d_player_ptr_add(player, offset);
}

static inline s8* oot3d_player_s8(Oot3dPlayer* player, u32 offset) {
    return (s8*)oot3d_player_ptr_add(player, offset);
}

static inline u16* oot3d_player_u16(Oot3dPlayer* player, u32 offset) {
    return (u16*)oot3d_player_ptr_add(player, offset);
}

static inline s16* oot3d_player_s16(Oot3dPlayer* player, u32 offset) {
    return (s16*)oot3d_player_ptr_add(player, offset);
}

static inline u32* oot3d_player_u32(Oot3dPlayer* player, u32 offset) {
    return (u32*)oot3d_player_ptr_add(player, offset);
}

static inline s32* oot3d_player_s32(Oot3dPlayer* player, u32 offset) {
    return (s32*)oot3d_player_ptr_add(player, offset);
}

static inline float* oot3d_player_f32(Oot3dPlayer* player, u32 offset) {
    return (float*)oot3d_player_ptr_add(player, offset);
}

static inline void** oot3d_player_ptr(Oot3dPlayer* player, u32 offset) {
    return (void**)oot3d_player_ptr_add(player, offset);
}

static inline Oot3dVec3f* oot3d_player_vec3f(Oot3dPlayer* player, u32 offset) {
    return (Oot3dVec3f*)oot3d_player_ptr_add(player, offset);
}

static inline float oot3d_age_property_f32(Oot3dPlayer* player, u32 offset) {
    void* ageProperties = *oot3d_player_ptr(player, OOT3D_PLAYER_OFFSET_AGE_PROPERTIES);
    return *(float*)oot3d_player_ptr_add(ageProperties, offset);
}

static inline s32 oot3d_abs_s32(s32 value) {
    return value < 0 ? -value : value;
}

#endif
