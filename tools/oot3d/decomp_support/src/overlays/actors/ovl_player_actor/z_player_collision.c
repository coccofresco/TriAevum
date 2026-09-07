#include "oot3d/player.h"

#if defined(__GNUC__)
#define OOT3D_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define OOT3D_FORCE_INLINE static inline
#endif

extern void FUN_00376340(Oot3dPlayState* play, Oot3dPlayer* player, float wallCheckHeight, float wallCheckRadius,
                         float ceilingCheckHeight, u32 flags);
extern u8 FUN_0035ea4c(void* colCtx, void* poly, u8 bgId);
extern s32 oot3d_surface_type_get_sfx_semantic(void* colCtx, void* poly, u8 bgId);
extern s8 FUN_00496914(void* colCtx, void* poly, u8 bgId);
extern void FUN_00495b54(s32 reverb);
extern s32 FUN_002c1e10(void* colCtx, void* poly, u8 bgId);
extern void FUN_0032b13c(Oot3dPlayState* play, s32 lightSetting);
extern void FUN_00496c44(void* colCtx);
extern s32 FUN_003232a4(void* colCtx, void* poly, u8 bgId);
extern s16 FUN_00496774(void* colCtx, void* poly, u8 bgId);
extern s16 FUN_00357d54(void* colCtx, void* poly, u8 bgId);
extern void FUN_003365b0(Oot3dPlayState* play, Oot3dPlayer* player, void* floorPoly, u8 floorBgId);
extern s32 FUN_003679b4(const void* object);
extern s32 FUN_002c1d18(Oot3dPlayState* play, Oot3dPlayer* player, Oot3dVec3f* pos, void** outPoly, u8* outBgId,
                        const void* actor);
extern s32 FUN_0035fee8(void* colCtx, void* poly, u8 bgId);
extern s16 FUN_003758b0(float z, float x);
extern float FUN_003586a4(void* colCtx, void** outPoly, Oot3dVec3f* pos);
extern s32 FUN_00496a9c(void* colCtx, void* poly, void* bgId);
extern s32 FUN_00496ac8(void* colCtx, void* poly, u8 bgId);
extern s32 FUN_002c1ba4(Oot3dPlayer* player);
extern s32 FUN_0035ea34(void* colCtx, void* poly, u8 bgId);
extern void FUN_00496c60(void* colCtx);
extern s32 FUN_0035db20(Oot3dPlayState* play, Oot3dPlayer* player);
extern s32 FUN_00331030(void* colCtx, void* poly, u8 bgId);
extern void FUN_0036055c(Oot3dPlayState* play, Oot3dPlayer* player, const void* action, s32 arg3);
extern void FUN_0036b0fc(Oot3dPlayState* play, Oot3dPlayer* player);
extern void FUN_0036b02c(Oot3dPlayState* play, Oot3dPlayer* player);
extern void FUN_00360190(void* skelAnime, Oot3dPlayState* play, const void* animation, s32 mode, float playSpeed,
                         float startFrame, float endFrame, float morphFrames);
extern s32 FUN_003705a0(float* value, float target, float step);
extern float oot3d_sin_idx8(s16 angle);
extern float oot3d_cos_idx8(s16 angle);
extern float VectorSignedToFloat(s32 value, u32 roundingMode);

#define OOT3D_PLAYER_COLLISION_RUNTIME_BASE ((u8*)0x0053A07Cu)
#define OOT3D_PLAYER_COLLISION_PARAM_TABLE_PTR ((u8**)0x0051B2F4u)
#define OOT3D_PLAYER_COLLISION_CRAWL_WALL_RADIUS 0x1.400000p+3f
#define OOT3D_PLAYER_COLLISION_CRAWL_WALL_HEIGHT 0x1.e00000p+3f
#define OOT3D_PLAYER_COLLISION_CRAWL_CEILING_HEIGHT 0x1.e00000p+4f
#define OOT3D_PLAYER_COLLISION_LOW_CRAWL_WALL_HEIGHT 0x1.800000p+3f
#define OOT3D_PLAYER_COLLISION_LOW_CRAWL_CEILING_HEIGHT 0x1.b00000p+4f
#define OOT3D_PLAYER_COLLISION_STAND_WALL_HEIGHT 0x1.a00000p+4f
#define OOT3D_PLAYER_COLLISION_SCENE_FLAG_BASE ((u8*)0x00587958u)
#define OOT3D_PLAY_OFFSET_PLAYER_COLLISION_SCENE_FLAG 0x00004C30
#define OOT3D_PLAYER_COLLISION_SCENE_SPEED_THRESHOLD 0x1.47ae14p-7f
#define OOT3D_PLAYER_ACTION_00492A5C ((const void*)0x00492A5Cu)
#define OOT3D_PLAYER_OFFSET_COLLISION_STORED_FLOOR_Y 0x0000249C
#define OOT3D_PLAYER_ACTION_00495E1C ((const void*)0x00495E1Cu)
#define OOT3D_PLAYER_ACTION_00496458 ((const void*)0x00496458u)
#define OOT3D_PLAYER_COLLISION_ZERO 0.0f
#define OOT3D_PLAYER_COLLISION_WATER_DEPTH_THRESHOLD 0x1.400000p+4f
#define OOT3D_PLAYER_COLLISION_WALL_SPEED_SCALE 0x1.47ae14p-7f
#define OOT3D_PLAYER_COLLISION_NORMAL_SCALE 0x1.000200p-15f
#define OOT3D_PLAYER_COLLISION_LEDGE_CHECK_Y 0x1.200000p+4f
#define OOT3D_PLAYER_COLLISION_CHECK_POS ((float*)0x005A3224u)
#define OOT3D_PLAYER_COLLISION_LEDGE_HELPER_ARG ((const void*)0x0053A168u)
#define OOT3D_PLAYER_COLLISION_BG_ACTOR_ARG ((const void*)0x005A5328u)
#define OOT3D_PLAYER_COLLISION_WALL_YAW_SCALE 0x1.4f8b58p-14f
#define OOT3D_PLAYER_COLLISION_MIN_WALL_SPEED_LIMIT 0x1.99999ap-4f
#define OOT3D_PLAYER_COLLISION_LEDGE_INVALID_Y 0x1.8ff5c4p+8f
#define OOT3D_PLAYER_COLLISION_LEDGE_CLEARANCE 0x1.400000p+2f
#define OOT3D_PLAYER_ACTION_00495FD8 ((const void*)0x00495FD8u)
#define OOT3D_PLAYER_COLLISION_LEDGE_ANIMS ((const void**)0x0053A25Cu)

OOT3D_FORCE_INLINE float oot3d_player_colpoly_normal(s16 normal) {
    return VectorSignedToFloat(normal, 0) * OOT3D_PLAYER_COLLISION_NORMAL_SCALE;
}

OOT3D_FORCE_INLINE s16 oot3d_player_poly_yaw(const void* poly) {
    return FUN_003758b0(
        oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(poly, 0x0e)),
        oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(poly, 0x0a)));
}

OOT3D_FORCE_INLINE u32 oot3d_player_bg_check_flags(Oot3dPlayer* player) {
    u32 stateFlags1 = *oot3d_player_u32(player, OOT3D_PLAYER_OFFSET_STATE_FLAGS1);
    u32 flags;

    if ((stateFlags1 & OOT3D_PLAYER_STATE1_29_OR_31) == 0) {
        if ((*oot3d_player_u32(player, OOT3D_PLAYER_OFFSET_BG_UPDATE_FLAGS) & 0x800000) != 0) {
            flags = OOT3D_UPDBGCHECKINFO_FLAG_ALL;
        } else {
            flags = OOT3D_UPDBGCHECKINFO_FLAG_ALL;
        }
    } else if ((stateFlags1 & OOT3D_PLAYER_STATE1_31) != 0) {
        *oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) &= ~OOT3D_BGCHECKFLAG_GROUND;
        flags = OOT3D_UPDBGCHECKINFO_FLAG_345;
    } else if ((stateFlags1 & OOT3D_PLAYER_STATE1_0) != 0) {
        s16 storedFloorY = *(s16*)oot3d_player_ptr_add(player, OOT3D_PLAYER_OFFSET_COLLISION_STORED_FLOOR_Y);

        if ((storedFloorY - (s32)*oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 4)) >= 100) {
            flags = OOT3D_UPDBGCHECKINFO_FLAG_0345;
        } else {
            flags = OOT3D_UPDBGCHECKINFO_FLAG_ALL;
        }
    } else {
        const void* actionFunc = *oot3d_player_ptr(player, OOT3D_PLAYER_OFFSET_ACTION_FUNC);

        if ((actionFunc == OOT3D_PLAYER_ACTION_00495E1C) || (actionFunc == OOT3D_PLAYER_ACTION_00496458)) {
            *oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) &=
                ~(OOT3D_BGCHECKFLAG_WALL | OOT3D_BGCHECKFLAG_PLAYER_WALL_INTERACT);
            flags = OOT3D_UPDBGCHECKINFO_FLAG_2345;
        } else {
            flags = OOT3D_UPDBGCHECKINFO_FLAG_ALL;
        }
    }

    if ((*oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_STATE_FLAGS3) & OOT3D_PLAYER_STATE3_0) != 0) {
        flags &= ~(OOT3D_UPDBGCHECKINFO_FLAG_1 | OOT3D_UPDBGCHECKINFO_FLAG_2);
    }
    if ((flags & OOT3D_UPDBGCHECKINFO_FLAG_2) != 0) {
        *oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_STATE_FLAGS3) |= OOT3D_PLAYER_STATE3_4;
    }

    return flags;
}

OOT3D_FORCE_INLINE void oot3d_player_update_bg_check_info(Oot3dPlayState* play, Oot3dPlayer* player) {
    void* ageProperties = *oot3d_player_ptr(player, OOT3D_PLAYER_OFFSET_AGE_PROPERTIES);
    u32 stateFlags1 = *oot3d_player_u32(player, OOT3D_PLAYER_OFFSET_STATE_FLAGS1);
    u32 stateFlags2 = *oot3d_player_u32(player, OOT3D_PLAYER_OFFSET_STATE_FLAGS2);
    float wallCheckHeight;
    float wallCheckRadius;
    float ceilingCheckHeight;
    u32 flags;

    if ((stateFlags2 & OOT3D_PLAYER_STATE2_CRAWLING) != 0) {
        s16 sceneSetup = *(s16*)oot3d_player_const_ptr_add(*OOT3D_PLAYER_COLLISION_PARAM_TABLE_PTR, 0x110);

        wallCheckRadius = OOT3D_PLAYER_COLLISION_CRAWL_WALL_RADIUS;
        wallCheckHeight = OOT3D_PLAYER_COLLISION_CRAWL_WALL_HEIGHT;
        ceilingCheckHeight = OOT3D_PLAYER_COLLISION_CRAWL_CEILING_HEIGHT;
        if (sceneSetup <= 2) {
            wallCheckHeight = OOT3D_PLAYER_COLLISION_LOW_CRAWL_WALL_HEIGHT;
            ceilingCheckHeight = OOT3D_PLAYER_COLLISION_LOW_CRAWL_CEILING_HEIGHT;
        }
    } else {
        s16 sceneId = *(s16*)oot3d_player_ptr_add(play, OOT3D_PLAY_OFFSET_SCENE_ID);

        wallCheckHeight = OOT3D_PLAYER_COLLISION_STAND_WALL_HEIGHT;
        wallCheckRadius = *(float*)oot3d_player_ptr_add(ageProperties, OOT3D_AGE_PROPERTIES_OFFSET_WALL_CHECK_RADIUS);
        ceilingCheckHeight = *(float*)ageProperties;

        if (sceneId == 0x57) {
            if (((stateFlags1 & OOT3D_PLAYER_STATE1_SCENE_BG_OFFSET) != 0) &&
                (*oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_CURRENT_BOOTS) != OOT3D_PLAYER_BOOTS_IRON) &&
                (*(u32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_SCENE_FLAG_BASE, 4) == 0)) {
                wallCheckHeight += *(float*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0xe0);
                ceilingCheckHeight += *(float*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0xe0);
            }
        } else if ((sceneId == 0x0b) &&
                   (*(u8*)oot3d_player_ptr_add(play, OOT3D_PLAY_OFFSET_PLAYER_COLLISION_SCENE_FLAG) == 5)) {
            wallCheckHeight += *(float*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0xe4);
            ceilingCheckHeight += *(float*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0xe4);
        }

        if ((sceneId <= 2) &&
            (*oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_SPEED_XZ) >
             OOT3D_PLAYER_COLLISION_SCENE_SPEED_THRESHOLD)) {
            wallCheckHeight -= *(float*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0xe8);
            ceilingCheckHeight -= *(float*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0xe8);
        }

        if (*oot3d_player_ptr(player, OOT3D_PLAYER_OFFSET_ACTION_FUNC) == OOT3D_PLAYER_ACTION_00492A5C) {
            wallCheckHeight -= *(float*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0xdc);
        }
    }

    flags = oot3d_player_bg_check_flags(player);
    FUN_00376340(play, player, wallCheckHeight, wallCheckRadius, ceilingCheckHeight, flags);
}

OOT3D_FORCE_INLINE s32 oot3d_player_floor_sfx(Oot3dPlayer* player, void* colCtx, void* floorPoly, u8 floorBgId) {
    if ((*oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) & OOT3D_BGCHECKFLAG_WATER) != 0) {
        if (*oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_DEPTH_IN_WATER) <
            OOT3D_PLAYER_COLLISION_WATER_DEPTH_THRESHOLD) {
            return 4;
        }
        return 5;
    }

    if ((*oot3d_player_u32(player, OOT3D_PLAYER_OFFSET_STATE_FLAGS2) &
         OOT3D_PLAYER_STATE2_FORCE_SAND_FLOOR_SOUND) != 0) {
        return 1;
    }

    return oot3d_surface_type_get_sfx_semantic(colCtx, floorPoly, floorBgId);
}

OOT3D_FORCE_INLINE void oot3d_player_update_floor_surface(Oot3dPlayState* play, Oot3dPlayer* player, void* colCtx,
                                                          void* floorPoly, u8 floorBgId) {
    s32 conveyorSpeed;
    s16 isFloorConveyor;

    *oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_FLOOR_PROPERTY) = FUN_0035ea4c(colCtx, floorPoly, floorBgId);
    *oot3d_player_s32(player, OOT3D_PLAYER_OFFSET_PREV_FLOOR_SFX_OFFSET) =
        *oot3d_player_s32(player, OOT3D_PLAYER_OFFSET_FLOOR_SFX_OFFSET);
    *oot3d_player_s32(player, OOT3D_PLAYER_OFFSET_FLOOR_SFX_OFFSET) =
        oot3d_player_floor_sfx(player, colCtx, floorPoly, floorBgId);

    if (*oot3d_player_s8(player, OOT3D_ACTOR_OFFSET_CATEGORY) == OOT3D_ACTORCAT_PLAYER) {
        FUN_00495b54(FUN_00496914(colCtx, floorPoly, floorBgId));
        if (floorBgId == OOT3D_BGCHECK_SCENE) {
            FUN_0032b13c(play, FUN_002c1e10(colCtx, floorPoly, OOT3D_BGCHECK_SCENE));
        } else {
            FUN_00496c44(colCtx);
        }
    }

    conveyorSpeed = FUN_003232a4(colCtx, floorPoly, floorBgId);
    *(s32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x38) = conveyorSpeed;
    if (conveyorSpeed == 0) {
        return;
    }

    isFloorConveyor = FUN_00496774(colCtx, floorPoly, floorBgId);
    *(s16*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0) = isFloorConveyor;
    if (isFloorConveyor == 0) {
        if ((*oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_DEPTH_IN_WATER) <=
             OOT3D_PLAYER_COLLISION_WATER_DEPTH_THRESHOLD) ||
            (*oot3d_player_s8(player, OOT3D_PLAYER_OFFSET_CURRENT_BOOTS) == OOT3D_PLAYER_BOOTS_IRON)) {
            *(s32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x38) = 0;
            return;
        }
    } else if ((*oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) & OOT3D_BGCHECKFLAG_GROUND) == 0) {
        *(s32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x38) = 0;
        return;
    }

    *(s16*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 2) =
        FUN_00357d54(colCtx, floorPoly, floorBgId) << 10;
}

OOT3D_FORCE_INLINE u8 oot3d_player_update_wall_and_ledge(Oot3dPlayState* play, Oot3dPlayer* player, void* colCtx,
                                                         void* floorPoly, u8 floorBgId) {
    u8 nextLedgeClimbType = OOT3D_PLAYER_LEDGE_CLIMB_NONE;
    u16 bgFlags = *oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS);

    *oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) =
        bgFlags & ~OOT3D_BGCHECKFLAG_PLAYER_WALL_INTERACT;
    if ((bgFlags & OOT3D_BGCHECKFLAG_WALL) == 0) {
        *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_WALL_SPEED_LIMIT) =
            VectorSignedToFloat(*(s16*)oot3d_player_const_ptr_add(*OOT3D_PLAYER_COLLISION_PARAM_TABLE_PTR, 0x6e), 0) *
            OOT3D_PLAYER_COLLISION_WALL_SPEED_SCALE;
        *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE) = OOT3D_PLAYER_COLLISION_ZERO;
        *oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_LEDGE_DELAY_TIMER) = 0;
        return nextLedgeClimbType;
    }

    OOT3D_PLAYER_COLLISION_CHECK_POS[0] = OOT3D_PLAYER_COLLISION_ZERO;
    OOT3D_PLAYER_COLLISION_CHECK_POS[1] = OOT3D_PLAYER_COLLISION_LEDGE_CHECK_Y;
    OOT3D_PLAYER_COLLISION_CHECK_POS[2] =
        oot3d_age_property_f32(player, OOT3D_AGE_PROPERTIES_OFFSET_WALL_CHECK_RADIUS) +
        OOT3D_PLAYER_COLLISION_CRAWL_WALL_RADIUS;

    if (((*(u32*)oot3d_player_const_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x76) & 1) == 0) &&
        FUN_003679b4(OOT3D_PLAYER_COLLISION_LEDGE_HELPER_ARG)) {
        OOT3D_PLAYER_COLLISION_CHECK_POS[0] = OOT3D_PLAYER_COLLISION_ZERO;
        OOT3D_PLAYER_COLLISION_CHECK_POS[1] = OOT3D_PLAYER_COLLISION_LEDGE_CHECK_Y;
        OOT3D_PLAYER_COLLISION_CHECK_POS[2] = OOT3D_PLAYER_COLLISION_ZERO;
    }

    if (((*oot3d_player_u32(player, OOT3D_PLAYER_OFFSET_STATE_FLAGS2) & OOT3D_PLAYER_STATE2_CRAWLING) == 0) &&
        FUN_002c1d18(play, player, (Oot3dVec3f*)OOT3D_PLAYER_COLLISION_CHECK_POS,
                     oot3d_player_ptr(player, OOT3D_ACTOR_OFFSET_WALL_POLY),
                     oot3d_player_u8(player, OOT3D_ACTOR_OFFSET_WALL_BG_ID),
                     OOT3D_PLAYER_COLLISION_BG_ACTOR_ARG)) {
        *oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) |= OOT3D_BGCHECKFLAG_PLAYER_WALL_INTERACT;
        *oot3d_player_s16(player, OOT3D_ACTOR_OFFSET_WALL_YAW) =
            oot3d_player_poly_yaw(*oot3d_player_ptr(player, OOT3D_ACTOR_OFFSET_WALL_POLY));
    }

    {
        s16 wallYaw = *oot3d_player_s16(player, OOT3D_ACTOR_OFFSET_WALL_YAW);
        s32 shapeYawToWall = (s16)(*oot3d_player_s16(player, OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y) -
                                   (s16)(wallYaw - 0x8000));
        s32 worldYawToWall =
            (s16)(*oot3d_player_s16(player, OOT3D_PLAYER_OFFSET_YAW) - (s16)(wallYaw - 0x8000));
        float worldScale;

        if (shapeYawToWall < 0) {
            shapeYawToWall = -shapeYawToWall;
        }
        if (worldYawToWall < 0) {
            worldYawToWall = -worldYawToWall;
        }

        *(s32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x34) =
            FUN_0035fee8(colCtx, *oot3d_player_ptr(player, OOT3D_ACTOR_OFFSET_WALL_POLY),
                         *oot3d_player_u8(player, OOT3D_ACTOR_OFFSET_WALL_BG_ID));
        *(s32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x44) = shapeYawToWall;
        *(s32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x48) = worldYawToWall;

        worldScale = VectorSignedToFloat(worldYawToWall, 0) * OOT3D_PLAYER_COLLISION_WALL_YAW_SCALE;
        if (((*oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) & OOT3D_BGCHECKFLAG_GROUND) == 0) ||
            (worldScale >= 1.0f)) {
            *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_WALL_SPEED_LIMIT) =
                VectorSignedToFloat(*(s16*)oot3d_player_const_ptr_add(*OOT3D_PLAYER_COLLISION_PARAM_TABLE_PTR, 0x6e),
                                    0) *
                OOT3D_PLAYER_COLLISION_WALL_SPEED_SCALE;
        } else {
            float speedLimit =
                VectorSignedToFloat(*(s16*)oot3d_player_const_ptr_add(*OOT3D_PLAYER_COLLISION_PARAM_TABLE_PTR, 0x6e),
                                    0) *
                OOT3D_PLAYER_COLLISION_WALL_SPEED_SCALE * worldScale;

            *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_WALL_SPEED_LIMIT) =
                (speedLimit < OOT3D_PLAYER_COLLISION_MIN_WALL_SPEED_LIMIT) ?
                    OOT3D_PLAYER_COLLISION_MIN_WALL_SPEED_LIMIT :
                    speedLimit;
        }

        if (((*oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) &
              OOT3D_BGCHECKFLAG_PLAYER_WALL_INTERACT) != 0) &&
            (shapeYawToWall < 0x3000)) {
            void* wallPoly = *oot3d_player_ptr(player, OOT3D_ACTOR_OFFSET_WALL_POLY);

            if (oot3d_abs_s32(*(s16*)oot3d_player_const_ptr_add(wallPoly, 0x0c)) < 600) {
                float normalX = oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(wallPoly, 0x0a));
                float normalY = oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(wallPoly, 0x0c));
                float normalZ = oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(wallPoly, 0x0e));
                float ledgeOffset;
                float ledgeY;
                Oot3dVec3f ledgeCheckPos;
                void* ledgeFloorPoly;
                void* poly;
                u8 ceilingBgId;
                s32 wallYawDiff;

                *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_DIST_TO_WALL) =
                    ((normalX * *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 0)) +
                     (normalY * *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 4)) +
                     (normalZ * *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 8))) -
                    VectorSignedToFloat(*(s16*)oot3d_player_const_ptr_add(wallPoly, 0x08), 0);

                ledgeOffset =
                    *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_DIST_TO_WALL) +
                    OOT3D_PLAYER_COLLISION_CRAWL_WALL_RADIUS;
                ledgeCheckPos.x = *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 0) - (ledgeOffset * normalX);
                ledgeCheckPos.y = *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 4) +
                                  oot3d_age_property_f32(player, OOT3D_AGE_PROPERTIES_OFFSET_UNK_0C);
                ledgeCheckPos.z = *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 8) - (ledgeOffset * normalZ);

                ledgeY = FUN_003586a4(colCtx, &ledgeFloorPoly, &ledgeCheckPos);
                *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE) =
                    ledgeY - *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 4);

                if (*oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE) <
                    OOT3D_PLAYER_COLLISION_LEDGE_CHECK_Y) {
                    *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE) =
                        OOT3D_PLAYER_COLLISION_LEDGE_INVALID_Y;
                } else {
                    OOT3D_PLAYER_COLLISION_CHECK_POS[1] =
                        (ledgeY + OOT3D_PLAYER_COLLISION_LEDGE_CLEARANCE) -
                        *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 4);
                    if (FUN_002c1d18(play,
                                      player,
                                      (Oot3dVec3f*)OOT3D_PLAYER_COLLISION_CHECK_POS,
                                      &poly,
                                      &ceilingBgId,
                                      OOT3D_PLAYER_COLLISION_BG_ACTOR_ARG)) {
                        wallYawDiff =
                            *oot3d_player_s16(player, OOT3D_ACTOR_OFFSET_WALL_YAW) - oot3d_player_poly_yaw(poly);
                        if ((oot3d_abs_s32(wallYawDiff) < 0x4000) && (FUN_00496a9c(colCtx, poly, &ceilingBgId) == 0)) {
                            *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE) =
                                OOT3D_PLAYER_COLLISION_LEDGE_INVALID_Y;
                        }
                    } else if (FUN_00496ac8(colCtx, wallPoly, *oot3d_player_u8(player, OOT3D_ACTOR_OFFSET_WALL_BG_ID)) ==
                               0) {
                        if (oot3d_age_property_f32(player, OOT3D_AGE_PROPERTIES_OFFSET_UNK_1C) <=
                            *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE)) {
                            if (oot3d_abs_s32(*(s16*)oot3d_player_const_ptr_add(ledgeFloorPoly, 0x0c)) > 0x6d60) {
                                if (oot3d_age_property_f32(player, OOT3D_AGE_PROPERTIES_OFFSET_UNK_14) <=
                                    *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE)) {
                                    nextLedgeClimbType = OOT3D_PLAYER_LEDGE_CLIMB_4;
                                } else if (oot3d_age_property_f32(player, OOT3D_AGE_PROPERTIES_OFFSET_UNK_18) <=
                                           *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE)) {
                                    nextLedgeClimbType = OOT3D_PLAYER_LEDGE_CLIMB_3;
                                } else {
                                    nextLedgeClimbType = OOT3D_PLAYER_LEDGE_CLIMB_2;
                                }
                            }
                        } else {
                            nextLedgeClimbType = OOT3D_PLAYER_LEDGE_CLIMB_1;
                        }
                    }
                }
            }
        }
    }

    (void)floorPoly;
    (void)floorBgId;
    return nextLedgeClimbType;
}

OOT3D_FORCE_INLINE s32 oot3d_player_handle_slopes(Oot3dPlayState* play, Oot3dPlayer* player, void* colCtx,
                                                  void* floorPoly, u8 floorBgId) {
    float normalX;
    float normalY;
    float normalZ;
    s16 playerVelYaw;
    s16 downwardSlopeYaw;
    s32 velYawToDownwardSlope;

    if (FUN_0035db20(play, player) != 0) {
        return 0;
    }
    if (*oot3d_player_ptr(player, OOT3D_PLAYER_OFFSET_ACTION_FUNC) == OOT3D_PLAYER_ACTION_00495FD8) {
        return 0;
    }
    if (FUN_00331030(colCtx, floorPoly, floorBgId) != 1) {
        return 0;
    }

    normalX = oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(floorPoly, 0x0a));
    normalY = oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(floorPoly, 0x0c));
    normalZ = oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(floorPoly, 0x0e));
    playerVelYaw = FUN_003758b0(
        *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_VELOCITY_Z),
        *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_VELOCITY_X));
    downwardSlopeYaw = FUN_003758b0(normalZ, normalX);
    velYawToDownwardSlope = (s16)(downwardSlopeYaw - playerVelYaw);
    if (velYawToDownwardSlope < 0) {
        velYawToDownwardSlope = -velYawToDownwardSlope;
    }

    if (velYawToDownwardSlope > 0x3e80) {
        float slopeSlowdownSpeed = (1.0f - normalY) * 40.0f;
        float slopeSlowdownStep = (slopeSlowdownSpeed * slopeSlowdownSpeed) * 0.015f;

        if (slopeSlowdownStep < 1.2f) {
            slopeSlowdownStep = 1.2f;
        }
        *oot3d_player_s16(player, OOT3D_PLAYER_OFFSET_LEDGE_YAW_TARGET) = downwardSlopeYaw;
        FUN_003705a0(oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_LEDGE_TARGET), slopeSlowdownSpeed,
                     slopeSlowdownStep);
    } else {
        s8 facingUpSlope;

        FUN_0036055c(play, player, OOT3D_PLAYER_ACTION_00495FD8, 0);
        FUN_0036b0fc(play, player);
        FUN_0036b02c(play, player);
        if (*(s16*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 4) >= 0) {
            *oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_LEDGE_SIDE) = 1;
        }
        facingUpSlope = *oot3d_player_s8(player, OOT3D_PLAYER_OFFSET_LEDGE_SIDE);
        FUN_00360190(
            oot3d_player_ptr_add(player, OOT3D_PLAYER_OFFSET_SKEL_ANIME),
            play,
            OOT3D_PLAYER_COLLISION_LEDGE_ANIMS[facingUpSlope],
            0,
            1.0f,
            OOT3D_PLAYER_COLLISION_ZERO,
            OOT3D_PLAYER_COLLISION_ZERO,
            8.0f);
        *oot3d_player_f32(player, OOT3D_PLAYER_OFFSET_SPEED_XZ) = __builtin_sqrtf(
            (*oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_VELOCITY_X) *
             *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_VELOCITY_X)) +
            (*oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_VELOCITY_Z) *
             *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_VELOCITY_Z)));
        *oot3d_player_s16(player, OOT3D_PLAYER_OFFSET_YAW) = playerVelYaw;
        return 1;
    }

    return 0;
}

OOT3D_FORCE_INLINE void oot3d_player_update_ground_state(Oot3dPlayState* play, Oot3dPlayer* player, void* colCtx,
                                                         void* floorPoly, u8 floorBgId) {
    s32 floorType;
    s32 hoverBootsActive;

    if ((*oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) & OOT3D_BGCHECKFLAG_GROUND) == 0) {
        (void)FUN_002c1ba4(player);
        return;
    }

    floorType = FUN_0035ea34(colCtx, floorPoly, floorBgId);
    *(s32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x28) = floorType;

    hoverBootsActive = FUN_002c1ba4(player);
    if (hoverBootsActive == 0) {
        float normalX;
        float invNormalY;
        float normalZ;
        float sin;
        float cos;
        s16 floorYaw;

        if (floorBgId != OOT3D_BGCHECK_SCENE) {
            FUN_00496c60(colCtx);
        }

        normalX = oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(floorPoly, 0x0a));
        invNormalY = 1.0f / oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(floorPoly, 0x0c));
        normalZ = oot3d_player_colpoly_normal(*(s16*)oot3d_player_const_ptr_add(floorPoly, 0x0e));

        sin = oot3d_sin_idx8(*oot3d_player_s16(player, OOT3D_PLAYER_OFFSET_YAW));
        cos = oot3d_cos_idx8(*oot3d_player_s16(player, OOT3D_PLAYER_OFFSET_YAW));
        *oot3d_player_s16(player, OOT3D_PLAYER_OFFSET_FLOOR_PITCH) =
            FUN_003758b0(1.0f, (-(normalX * sin) - (normalZ * cos)) * invNormalY);
        *oot3d_player_s16(player, OOT3D_PLAYER_OFFSET_FLOOR_PITCH_ALT) =
            FUN_003758b0(1.0f, (-(normalX * cos) - (normalZ * sin)) * invNormalY);

        sin = oot3d_sin_idx8(*oot3d_player_s16(player, OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y));
        cos = oot3d_cos_idx8(*oot3d_player_s16(player, OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y));
        floorYaw = FUN_003758b0(1.0f, (-(normalX * sin) - (normalZ * cos)) * invNormalY);
        *(s16*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 4) = floorYaw;

        (void)oot3d_player_handle_slopes(play, player, colCtx, floorPoly, floorBgId);
    }

    if (*oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_PREV_FLOOR_TYPE) == (u8)floorType) {
        *oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_FLOOR_TYPE_TIMER) += 1;
    } else {
        *oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_PREV_FLOOR_TYPE) = (u8)floorType;
        *oot3d_player_u8(player, OOT3D_PLAYER_OFFSET_FLOOR_TYPE_TIMER) = 0;
    }

    (void)play;
}

void oot3d_player_process_scene_collision(Oot3dPlayState* play, Oot3dPlayer* player) {
    u8* player2000Base = (u8*)player + 0x2000;
    void* colCtx = oot3d_play_colctx(play);
    void* floorPoly;
    u8 floorBgId;
    u8 nextLedgeClimbType;
    u8 ledgeClimbType;

#if defined(__arm__)
    __asm__ volatile(
        ""
        :
        : "r"(play), "r"(player), "r"(oot3d_player_ptr_add(player, 0x2000)),
          "r"(oot3d_player_ptr_add(player, 0x1000)), "r"(OOT3D_PLAYER_COLLISION_RUNTIME_BASE)
        : "memory");
#endif

    *(u32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x40) =
        *(u8*)(player2000Base + 0x48a);

    oot3d_player_update_bg_check_info(play, player);

    if ((*oot3d_player_u16(player, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS) & OOT3D_BGCHECKFLAG_CEILING) != 0) {
        *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_VELOCITY_Y) = OOT3D_PLAYER_COLLISION_ZERO;
    }

    *(float*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x3c) =
        *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_WORLD_POS + 4) -
        *oot3d_player_f32(player, OOT3D_ACTOR_OFFSET_FLOOR_HEIGHT);
    *(s32*)oot3d_player_ptr_add(OOT3D_PLAYER_COLLISION_RUNTIME_BASE, 0x38) = 0;

    floorPoly = *oot3d_player_ptr(player, OOT3D_ACTOR_OFFSET_FLOOR_POLY);
    floorBgId = *oot3d_player_u8(player, OOT3D_ACTOR_OFFSET_FLOOR_BG_ID);
    if (floorPoly != 0) {
        oot3d_player_update_floor_surface(play, player, colCtx, floorPoly, floorBgId);
    }

    FUN_003365b0(play, player, floorPoly, floorBgId);
    nextLedgeClimbType = oot3d_player_update_wall_and_ledge(play, player, colCtx, floorPoly, floorBgId);

    ledgeClimbType = *(u8*)(player2000Base + 0x278);
    if (nextLedgeClimbType == ledgeClimbType) {
        if ((*(float*)(player2000Base + 0x21c) != OOT3D_PLAYER_COLLISION_ZERO) &&
            (*(u8*)(player2000Base + 0x279) < 100)) {
            *(u8*)(player2000Base + 0x279) += 1;
        }
    } else {
        *(u8*)(player2000Base + 0x278) = nextLedgeClimbType;
        *(u8*)(player2000Base + 0x279) = 0;
    }

    oot3d_player_update_ground_state(play, player, colCtx, floorPoly, floorBgId);
}
