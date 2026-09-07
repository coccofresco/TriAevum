#include "oot3d/boss_va.h"

#if defined(__arm__)
#define OOT3D_NAKED __attribute__((naked))
#else
#define OOT3D_NAKED
#endif

extern s32 FUN_003731e0(void* skelAnime);
extern void FUN_0031cb28(Oot3dBossVa* this);
extern s32 FUN_00375a18(s16* value, s32 target, s32 scale, s32 step, s32 minStep);
extern void FUN_00334e70(void* joint, s16* outRot, s32 mode);
extern void FUN_0031b034(Oot3dBossVa* this, Oot3dPlayState* play);
extern u32 FUN_0036ae14(void* skelAnime, s32 animationIndex);
extern void FUN_00375c08(void* skelAnime, s32 animationIndex, s32 mode);
extern void oot3d_anim_change_full(
    void* skelAnime,
    s32 animationIndex,
    s32 mode,
    float playSpeed,
    float startFrame,
    float endFrame,
    float morphFrames) __asm__("FUN_00375c08");
extern void FUN_00371738(void* dst, const void* src, u32 size);
extern float FUN_003759d0(void);
extern void FUN_00375ed8(Oot3dBossVa* this, s32 colorFlag, s32 colorIntensity, s32 bufferFlag, s32 duration);
extern s16 FUN_003758b0(float z, float x);
extern s16 FUN_0037587c(const void* from, const void* to);
extern float FUN_0036e168(float* value, float target, float scale, float step, float minStep);
extern float FUN_003738a8(float range);
extern s32 FUN_00370378(s16* value, s32 target, s32 step);
extern void* FUN_00371478(void* object, s32 arg1);
extern float oot3d_sin_idx8(u32 angle);
extern float oot3d_cos_idx8(u32 angle);
extern void FUN_00375bcc(void* actor, s32 sfxId);
extern void FUN_0031f5a8(Oot3dPlayState* play, Oot3dBossVa* this, s32 count, s32 arg3, s32 arg4, ...);
extern void FUN_0031c7d4(Oot3dPlayState* play, Oot3dBossVa* this, s32 count, s32 arg3, s32 arg4, ...);
extern void FUN_00374428(void* actor);
extern void FUN_003761f0(Oot3dPlayState* play);
extern void FUN_00376168(Oot3dPlayState* play, void* colChkCtx, void* collider);

#if defined(OOT3D_BOSS_VA_STRUCTURED_PORT)
#if defined(__GNUC__)
#define OOT3D_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define OOT3D_FORCE_INLINE static inline
#endif

#define OOT3D_ACTOR_ID_BOOMERANG 0x32
#define OOT3D_ACTOR_OFFSET_NEXT 0x130
#define OOT3D_ACTOR_OFFSET_FOCUS_POS 0x3C
#define OOT3D_EN_BOOM_OFFSET_MOVE_TO 0x228
#define OOT3D_EN_BOOM_OFFSET_RETURN_TIMER 0x26C
#define OOT3D_PLAY_OFFSET_ACTOR_LIST_1 0x20DC
#define OOT3D_PLAY_OFFSET_GAMEPLAY_FRAMES 0x5BF4
#define OOT3D_BOSS_VA_EFFECT_STRIDE 0x5C
#define OOT3D_BOSS_VA_ZAPPER_INTRO_STEP 0x02EE
#define OOT3D_BOSS_VA_ZAPPER_DAMAGED_STEP 0x02EE
#define OOT3D_BOSS_VA_ZAPPER_EFFECTS_BASE ((u8*)0x0057FB4C)
#define OOT3D_BOSS_VA_ZAPPER_REST_VELOCITY ((const Oot3dVec3f*)0x0058434C)

void oot3d_boss_va_zapper_hold(Oot3dBossVa* this, Oot3dPlayState* play);
void oot3d_boss_va_zapper_attack(Oot3dBossVa* this, Oot3dPlayState* play);
void oot3d_boss_va_zapper_enraged(Oot3dBossVa* this, Oot3dPlayState* play);
void oot3d_boss_va_zapper_death(Oot3dBossVa* this, Oot3dPlayState* play);
void oot3d_boss_va_setup_zapper_damaged(Oot3dBossVa* this);

OOT3D_FORCE_INLINE s32 oot3d_boss_va_smooth_abs(s16* value, s32 target, s32 step) {
    return oot3d_abs_s32(FUN_00375a18(value, target, 1, step, 0));
}

OOT3D_FORCE_INLINE void oot3d_boss_va_set_env_spark(Oot3dPlayState* play) {
    s16* env = (s16*)oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_ENV_COLOR);

    env[0] = 10;
    env[1] = 10;
    env[2] = 10;
    env[3] = 0x73;
    env[4] = 0x41;
    env[5] = 100;
    env[6] = 0x78;
    env[7] = 0x78;
    env[8] = 0x46;
}

OOT3D_FORCE_INLINE void oot3d_boss_va_set_death_env(Oot3dPlayState* play) {
    s16* env = (s16*)oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_ENV_COLOR);
    u8* env8 = oot3d_ptr_add(play, 0x3262);

    env[0] = 200;
    env[1] = 200;
    env[2] = 200;
    env[3] = 0xd7;
    env[4] = 0xa5;
    env[5] = 200;
    env[6] = 0xdc;
    env[7] = 0xdc;
    env[8] = 0x96;
    env[9] = 0x100;
    *(u32*)oot3d_ptr_add(play, 0x3214) = 0x3f800000;
    env8[0] = 0xdc;
    env8[1] = 0xdc;
    env8[2] = 0x96;
    env8[3] = 100;
}

OOT3D_FORCE_INLINE void oot3d_boss_va_reset_aim(Oot3dBossVa* this, s32 step) {
    s16 jointRot[3];

    FUN_00375a18(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_E6), 0, 1, step, 0);
    FUN_00375a18(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_EC), 0, 1, step, 0);
    FUN_00375a18(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_EA), 0, 1, step, 0);
    FUN_00375a18(
        oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_F2),
        (s16)(oot3d_boss_va_shape_rot_y(this) - oot3d_boss_va_shape_rot_x(this)),
        1,
        step,
        0);
    FUN_00334e70(oot3d_ptr_add(oot3d_boss_va_joint_table(this), 0x9c), jointRot, 0);
    FUN_00375a18(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_F0), jointRot[2], 1, step, 0);
    FUN_0036e168(oot3d_boss_va_float(this, OOT3D_BOSS_VA_OFFSET_SKEL_PLAY_SPEED), 1.0f, 1.0f, 0.05f, 0.0f);
    oot3d_boss_va_set_burst(this, 0);
}

OOT3D_FORCE_INLINE void oot3d_boss_va_setup_zapper_hold_structured(Oot3dBossVa* this) {
    s32 lastFrame = FUN_0036ae14(oot3d_boss_va_skel_anime(this), 0xc);

    oot3d_anim_change_full(
        oot3d_boss_va_skel_anime(this),
        0xc,
        3,
        0.0f,
        0.0f,
        (float)lastFrame,
        -6.0f);
    oot3d_boss_va_set_burst(this, 0);
    oot3d_boss_va_set_action(this, oot3d_boss_va_zapper_hold);
}

OOT3D_FORCE_INLINE void oot3d_boss_va_setup_zapper_enraged_structured(Oot3dBossVa* this) {
    s32 lastFrame = FUN_0036ae14(oot3d_boss_va_skel_anime(this), 0xd);

    oot3d_anim_change_full(
        oot3d_boss_va_skel_anime(this),
        0xd,
        1,
        1.0f,
        (float)lastFrame - 1.0f,
        (float)lastFrame,
        -6.0f);
    oot3d_boss_va_set_burst(this, 0);
    oot3d_boss_va_set_action(this, oot3d_boss_va_zapper_enraged);
}

OOT3D_FORCE_INLINE void oot3d_boss_va_setup_zapper_death_structured(Oot3dBossVa* this) {
    s32 params;
    s32 timer;
    s32 lastFrame = FUN_0036ae14(oot3d_boss_va_skel_anime(this), 0xd);
    float startFrame = FUN_003759d0() * 3.0f;
    float playSpeed = FUN_003759d0() + 0.25f;

    oot3d_anim_change_full(
        oot3d_boss_va_skel_anime(this),
        0xd,
        1,
        playSpeed,
        startFrame,
        (float)lastFrame,
        -6.0f);
    oot3d_boss_va_set_burst(this, 0);
    params = oot3d_boss_va_params(this);
    timer = (params - OOT3D_BOSS_VA_PARAM_ZAPPER_1) * -6;
    *oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_TIMER2) = (timer < 1) ? (s16)(-55 - timer) : (s16)(55 + timer);
    *oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_DEATH_SPIN) = 0;
    oot3d_boss_va_set_action(this, oot3d_boss_va_zapper_death);
}

OOT3D_FORCE_INLINE void oot3d_boss_va_store_burst_target(Oot3dBossVa* this, const Oot3dVec3f* target) {
    oot3d_vec3f_copy(oot3d_boss_va_vec3f(this, OOT3D_BOSS_VA_OFFSET_BURST_POS), target);
}

OOT3D_FORCE_INLINE s16 oot3d_boss_va_vec3f_yaw(const Oot3dVec3f* from, const Oot3dVec3f* to) {
    return FUN_003758b0(to->z - from->z, to->x - from->x);
}

OOT3D_FORCE_INLINE void* oot3d_boss_va_find_boomerang(Oot3dPlayState* play) {
    u8* actor = *(u8**)oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_ACTOR_LIST_1);

    while (actor != 0) {
        if (*(s16*)actor == OOT3D_ACTOR_ID_BOOMERANG) {
            return actor;
        }
        actor = *(u8**)oot3d_ptr_add(actor, OOT3D_ACTOR_OFFSET_NEXT);
    }

    return 0;
}

OOT3D_FORCE_INLINE float oot3d_boss_va_vec3f_dist_xyz(const Oot3dVec3f* a, const Oot3dVec3f* b) {
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    float dz = b->z - a->z;

    return __builtin_sqrtf((dx * dx) + (dy * dy) + (dz * dz));
}

OOT3D_FORCE_INLINE s32 oot3d_boss_va_predict_attack_target(
    Oot3dPlayState* play,
    void* player,
    Oot3dVec3f* target,
    u32* successThreshold) {
    u8* boomerang = oot3d_boss_va_find_boomerang(play);
    void* boomTarget;
    s16 boomYaw;
    s16 boomPitch;
    s32 i;
    float updateStep;

    if (boomerang == 0) {
        oot3d_vec3f_copy(target, oot3d_actor_world_pos(player));
        target->y += 10.0f;
        *successThreshold = 500;
        return 0x3e80;
    }

    boomTarget = *(void**)oot3d_ptr_add(boomerang, OOT3D_EN_BOOM_OFFSET_MOVE_TO);
    if ((boomTarget == 0) || (boomTarget == player)) {
        oot3d_vec3f_copy(target, oot3d_actor_world_pos(player));
        target->y += 10.0f;
        *successThreshold = 500;
        return 0x3e80;
    }

    updateStep = 0.5f;
    *successThreshold = 0x3e80;
    oot3d_vec3f_copy(target, oot3d_actor_world_pos(boomerang));
    boomYaw = *(s16*)oot3d_ptr_add(boomerang, OOT3D_ACTOR_OFFSET_WORLD_ROT_Y);
    boomPitch = *(s16*)oot3d_ptr_add(boomerang, OOT3D_ACTOR_OFFSET_WORLD_ROT_X);

    for (i = *(u8*)oot3d_ptr_add(boomerang, OOT3D_EN_BOOM_OFFSET_RETURN_TIMER); i >= 3; i--) {
        const Oot3dVec3f* focus = (const Oot3dVec3f*)oot3d_const_ptr_add(boomTarget, OOT3D_ACTOR_OFFSET_FOCUS_POS);
        s16 yaw = oot3d_boss_va_vec3f_yaw(target, focus);
        s16 pitch = FUN_0037587c(target, focus);
        s16 yawDelta = (s16)(boomYaw - yaw);
        s16 pitchDelta = (s16)(boomPitch - pitch);
        float step = (200.0f - oot3d_boss_va_vec3f_dist_xyz(target, focus)) * 0.005f;
        float pitchSin;
        float pitchCos;
        float yawSin;
        float yawCos;

        if (step < 0.12f) {
            step = 0.12f;
        }

        if (yawDelta < 0) {
            yawDelta = -yawDelta;
        }
        if (pitchDelta < 0) {
            pitchDelta = -pitchDelta;
        }

        FUN_00370378(&boomYaw, yaw, (s16)((float)yawDelta * step));
        FUN_00370378(&boomPitch, pitch, (s16)((float)pitchDelta * step));

        pitchSin = oot3d_sin_idx8((u32)(s16)boomPitch);
        pitchCos = oot3d_cos_idx8((u32)(s16)boomPitch);
        yawSin = oot3d_sin_idx8((u32)(s16)boomYaw);
        yawCos = oot3d_cos_idx8((u32)(s16)boomYaw);

        target->x += (yawSin * (pitchCos * 12.0f)) * updateStep;
        target->y += (-pitchSin * 12.0f) * updateStep;
        target->z += (yawCos * (pitchCos * 12.0f)) * updateStep;
    }

    return 0x4650;
}

OOT3D_FORCE_INLINE void oot3d_boss_va_spawn_zapper_charge(
    Oot3dBossVa* this,
    const Oot3dBossVaStateTable* state,
    u8* effects,
    const Oot3dVec3f* restVelocity,
    float speedScale) {
    Oot3dVec3f pos = *oot3d_boss_va_vec3f(this, OOT3D_BOSS_VA_OFFSET_ZAP_HEAD_POS);
    s32 i;

    for (i = 0; i < 200; i++, effects += OOT3D_BOSS_VA_EFFECT_STRIDE) {
        if (*(u8*)oot3d_ptr_add(effects, 0x24) == 0) {
            *(u8*)oot3d_ptr_add(effects, 0x24) = 5;
            *(Oot3dBossVa**)oot3d_ptr_add(effects, 0x54) = this;
            *(Oot3dVec3f*)oot3d_ptr_add(effects, 0x00) = pos;
            *(Oot3dVec3f*)oot3d_ptr_add(effects, 0x18) = *restVelocity;
            *(Oot3dVec3f*)oot3d_ptr_add(effects, 0x0c) = *restVelocity;
            *(s16*)oot3d_ptr_add(effects, 0x28) = 0;
            *(s16*)oot3d_ptr_add(effects, 0x2a) =
                (s16)(*oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_HEAD_ROT_Y) + 0x4000);
            *(s16*)oot3d_ptr_add(effects, 0x2c) = *oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_HEAD_ROT_Z);
            *(s16*)oot3d_ptr_add(effects, 0x26) = (s16)((s16)(FUN_003759d0() * speedScale) + 10);
            *(s16*)oot3d_ptr_add(effects, 0x36) = 0xf0;
            *(float*)oot3d_ptr_add(effects, 0x40) = 1.0f;
            *(void**)oot3d_ptr_add(effects, 0x58) = FUN_00371478(state->object_18, 10);
            break;
        }
    }
}

OOT3D_FORCE_INLINE s32 oot3d_boss_va_track_target(
    Oot3dBossVa* this,
    Oot3dPlayState* play,
    void* player,
    const Oot3dVec3f* target,
    const Oot3dBossVaStateTable* state,
    s32 yawThreshold,
    s32 yawStep,
    s32 headYawStep,
    s32 pitchStep,
    float frameStep,
    u32 successThreshold) {
    s16 joint0[3];
    s16 joint1[3];
    s16 joint2[3];
    s16 yaw;
    s16 aim;
    s32 delta;
    u32 error;
    s16 pitchBase;

    yaw = oot3d_boss_va_vec3f_yaw(target, oot3d_boss_va_const_vec3f(this, OOT3D_BOSS_VA_OFFSET_ZAP_NECK_POS));
    delta = (s16)(yaw - oot3d_boss_va_shape_rot_y(this));
    if ((oot3d_abs_s32(delta) > yawThreshold) && (*oot3d_boss_va_u8(this, OOT3D_BOSS_VA_OFFSET_BURST) == 0)) {
        if ((*oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_TIMER2) < 0) &&
            (oot3d_player_state_flags1(player) & OOT3D_BOSS_VA_PLAYER_SHOCK_FLAG)) {
            FUN_0031f5a8(play, this, 1, 0x1e, 6, 1);
        }
        oot3d_boss_va_reset_aim(this, yawStep);
        return 0;
    }

    if ((state->flags_07 & 0x80) || (oot3d_player_state_flags1(player) & OOT3D_BOSS_VA_PLAYER_SHOCK_FLAG)) {
        if ((*oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_TIMER2) < 0) &&
            (oot3d_player_state_flags1(player) & OOT3D_BOSS_VA_PLAYER_SHOCK_FLAG)) {
            FUN_0031f5a8(play, this, 1, 0x1e, 6, 1);
        }
        oot3d_boss_va_reset_aim(this, yawStep);
        return 0;
    }

    if (*oot3d_boss_va_u8(this, OOT3D_BOSS_VA_OFFSET_BURST) != 0) {
        return 1;
    }

    aim = (s16)delta;
    if (oot3d_abs_s32(aim) > 0x1770) {
        aim = (aim > 0) ? 0x1770 : -0x1770;
    }
    error = oot3d_boss_va_smooth_abs(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_E6), aim, yawStep);

    aim = (s16)(yaw - aim);
    if (oot3d_abs_s32(aim) > 0x1770) {
        aim = (aim > 0) ? 0x1770 : -0x1770;
    }
    error += oot3d_boss_va_smooth_abs(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_EC), aim, yawStep);

    yaw = oot3d_boss_va_vec3f_yaw(oot3d_boss_va_const_vec3f(this, OOT3D_BOSS_VA_OFFSET_ZAP_HEAD_POS), target);
    error += oot3d_boss_va_smooth_abs(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_F2), (s16)(yaw - 0x4000), headYawStep);

    FUN_00334e70(oot3d_boss_va_joint_table(this), joint0, 0);
    FUN_00334e70(oot3d_ptr_add(oot3d_boss_va_joint_table(this), 0x34), joint1, 0);
    FUN_00334e70(oot3d_ptr_add(oot3d_boss_va_joint_table(this), 0x68), joint2, 0);
    pitchBase = (s16)(oot3d_boss_va_shape_rot_x(this) + joint0[0] + joint1[0] + joint2[0]);

    aim = FUN_0037587c(target, oot3d_boss_va_const_vec3f(this, OOT3D_BOSS_VA_OFFSET_ZAP_NECK_POS));
    error += oot3d_boss_va_smooth_abs(
        oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_EA),
        (s16)(aim - pitchBase),
        pitchStep);

    aim = FUN_0037587c(oot3d_boss_va_const_vec3f(this, OOT3D_BOSS_VA_OFFSET_ZAP_HEAD_POS), target);
    error += oot3d_boss_va_smooth_abs(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_F0), (s16)-aim, pitchStep);

    *oot3d_boss_va_float(this, OOT3D_BOSS_VA_OFFSET_SKEL_PLAY_SPEED) = 0.0f;
    if ((FUN_0036e168(oot3d_boss_va_float(this, OOT3D_BOSS_VA_OFFSET_SKEL_CUR_FRAME), 0.0f, 1.0f, frameStep, 0.0f) ==
         0.0f) &&
        (error < successThreshold)) {
        *oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_TIMER2) = 0;
        *oot3d_boss_va_u8(this, OOT3D_BOSS_VA_OFFSET_BURST) += 1;
        oot3d_boss_va_store_burst_target(this, target);
        if (FUN_003759d0() < 0.1f) {
            FUN_00375bcc(this, 0);
        }
    }

    return 1;
}

OOT3D_FORCE_INLINE void oot3d_boss_va_update_burst(
    Oot3dBossVa* this,
    Oot3dPlayState* play,
    const Oot3dBossVaStateTable* state,
    u8* effects,
    const Oot3dVec3f* restVelocity,
    s32 chargeFrame,
    s32 envFrame,
    s32 thunderFrame,
    s32 blastStart,
    s32 doneFrame,
    float chargeSpeedScale) {
    s16* timer = oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_TIMER2);

    if ((*oot3d_boss_va_u8(this, OOT3D_BOSS_VA_OFFSET_BURST) == 0) ||
        (*oot3d_boss_va_u8(this, OOT3D_BOSS_VA_OFFSET_BURST) == 2)) {
        return;
    }

    if (*timer >= blastStart) {
        if (*timer == thunderFrame) {
            FUN_00375bcc(this, 0);
        }
        FUN_0031f5a8(play, this, 2, 0x6e, 3, 1);
        FUN_0031f5a8(play, this, 2, 0x6e, 3, 1);
        FUN_0031f5a8(play, this, 2, 0x6e, 3, 1);
        FUN_003761f0(play);
        FUN_00376168(
            play,
            oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_COLCHK_CTX),
            oot3d_ptr_add(this, OOT3D_BOSS_VA_OFFSET_LIGHTNING_COLLIDER));
    } else {
        FUN_0031f5a8(play, this, 2, 0x32, 5, 1);
        if (*timer == envFrame) {
            oot3d_boss_va_set_env_spark(play);
        }
    }

    if (*timer == chargeFrame) {
        oot3d_boss_va_spawn_zapper_charge(this, state, effects, restVelocity, chargeSpeedScale);
    }

    *timer += 1;
    if (*timer >= doneFrame) {
        oot3d_boss_va_set_burst(this, 0);
    }
}

void oot3d_boss_va_zapper_intro(Oot3dBossVa* this) {
    s16 jointRot[3];
    s32 lastFrame;
    s32 step;
    s32 csState;

    FUN_0031cb28(this);
    csState = OOT3D_BOSS_VA_STATE_TABLE->state_09;
    if (csState == 10) {
        goto update_skel;
    }
    if (csState == 11) {
        goto update_skel;
    }
    if (csState == 12) {
        goto update_skel;
    }
    if (csState == 13) {
        lastFrame = FUN_0036ae14((u8*)this + OOT3D_BOSS_VA_OFFSET_SKEL_ANIME, 0xd);
        oot3d_anim_change_full(
            (u8*)this + OOT3D_BOSS_VA_OFFSET_SKEL_ANIME,
            0xd,
            1,
            1.0f,
            (float)lastFrame - 1.0f,
            (float)lastFrame,
            -6.0f);
        *(u32*)((u8*)this + OOT3D_ACTOR_OFFSET_FLAGS) &= ~1u;
        *(void**)((u8*)this + OOT3D_BOSS_VA_OFFSET_ACTION_FUNC) = oot3d_boss_va_zapper_attack;
    }
    goto reset_aim;

update_skel:
    FUN_003731e0((u8*)this + OOT3D_BOSS_VA_OFFSET_SKEL_ANIME);

reset_aim:
    step = OOT3D_BOSS_VA_ZAPPER_INTRO_STEP;
    FUN_00375a18(
        (s16*)((u8*)this + 0x0F00 + 0xF6),
        (s16)(*(s16*)((u8*)this + OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y) -
              *(s16*)((u8*)this + OOT3D_ACTOR_OFFSET_SHAPE_ROT_X)),
        1,
        step,
        0);
    FUN_00334e70((u8*)*(void**)((u8*)this + OOT3D_BOSS_VA_OFFSET_JOINT_TABLE_PTR) + 0x9c, jointRot, 0);
    FUN_00375a18((s16*)((u8*)this + 0x0C00 + 0x3F4), jointRot[2], 1, step, 0);
}

void oot3d_boss_va_zapper_damaged(Oot3dBossVa* this, Oot3dPlayState* play) {
    s16 jointRot[3];
    u8* base = (u8*)this;
    s32 lastFrame;
    s32 step;

    FUN_0031cb28(this);
    FUN_00375a18((s16*)(base + 0xfea), 0, 1, 4000, 0);
    FUN_00375a18((s16*)(base + 0xfe8), 0, 1, 4000, 0);
    FUN_00375a18((s16*)(base + 0xff0), 0, 1, 4000, 0);
    FUN_00375a18((s16*)(base + 0xfee), 0, 1, 4000, 0);

    step = OOT3D_BOSS_VA_ZAPPER_DAMAGED_STEP;
    FUN_00375a18(
        (s16*)(base + 0xff6),
        (s16)(*(s16*)(base + 0xbe) - *(s16*)(base + 0xbc)),
        1,
        step,
        0);
    FUN_00334e70((u8*)*(void**)(base + 0x21c) + 0x9c, jointRot, 0);
    FUN_00375a18((s16*)(base + 0xff4), jointRot[2], 1, step, 0);

    if (FUN_003731e0(base + 0x1a4) != 0) {
        if (OOT3D_BOSS_VA_STATE_TABLE->phase_08 < 0xf) {
            FUN_0031b034(this, play);
        } else {
            lastFrame = FUN_0036ae14(base + 0x1a4, 0xd);
            oot3d_anim_change_full(
                base + 0x1a4,
                0xd,
                1,
                1.0f,
                (float)lastFrame - 1.0f,
                (float)lastFrame,
                -6.0f);
            *(u8*)(base + 0xf95) = 0;
            *(void**)(base + 0xf90) = oot3d_boss_va_zapper_enraged;
        }
    }
}

void oot3d_boss_va_zapper_attack(Oot3dBossVa* this, Oot3dPlayState* play) {
    void* player = oot3d_play_player_actor(play);
    Oot3dVec3f target;
    u32 successThreshold;
    s32 yawThreshold = oot3d_boss_va_predict_attack_target(play, player, &target, &successThreshold);

    FUN_003731e0(oot3d_boss_va_skel_anime(this));
    FUN_0031cb28(this);

    if (OOT3D_BOSS_VA_STATE_TABLE->phase_08 >= 0xf) {
        oot3d_boss_va_setup_zapper_enraged_structured(this);
        return;
    }

    if (OOT3D_BOSS_VA_STATE_TABLE->flags_07 & 0x7f) {
        oot3d_boss_va_setup_zapper_damaged(this);
        return;
    }

    if (oot3d_actor_speed(oot3d_boss_va_body_actor(this)) != 0.0f) {
        oot3d_boss_va_setup_zapper_hold_structured(this);
        return;
    }

    if (oot3d_boss_va_track_target(
            this,
            play,
            player,
            &target,
            OOT3D_BOSS_VA_STATE_TABLE,
            yawThreshold,
            0x6d6,
            0x9c4,
            0xfa0,
            2.0f,
            successThreshold)) {
        oot3d_boss_va_update_burst(
            this,
            play,
            OOT3D_BOSS_VA_STATE_TABLE,
            OOT3D_BOSS_VA_ZAPPER_EFFECTS_BASE,
            OOT3D_BOSS_VA_ZAPPER_REST_VELOCITY,
            30,
            45,
            48,
            48,
            60,
            70.0f);
    }
}

void oot3d_boss_va_zapper_enraged(Oot3dBossVa* this, Oot3dPlayState* play) {
    void* player = oot3d_play_player_actor(play);
    Oot3dVec3f target;

    oot3d_vec3f_copy(&target, oot3d_actor_world_pos(player));
    target.y += 35.0f;

    FUN_003731e0(oot3d_boss_va_skel_anime(this));
    FUN_0031cb28(this);

    if (OOT3D_BOSS_VA_STATE_TABLE->phase_08 >= 0x12) {
        oot3d_boss_va_setup_zapper_death_structured(this);
        return;
    }

    if (OOT3D_BOSS_VA_STATE_TABLE->flags_07 & 0x7e) {
        oot3d_boss_va_setup_zapper_damaged(this);
        return;
    }

    if (oot3d_boss_va_track_target(
            this,
            play,
            player,
            &target,
            OOT3D_BOSS_VA_STATE_TABLE,
            0x4650,
            0xdac,
            0xea6,
            0x1b58,
            3.0f,
            600)) {
        oot3d_boss_va_update_burst(
            this,
            play,
            OOT3D_BOSS_VA_STATE_TABLE,
            OOT3D_BOSS_VA_ZAPPER_EFFECTS_BASE,
            OOT3D_BOSS_VA_ZAPPER_REST_VELOCITY,
            6,
            21,
            27,
            24,
            36,
            70.0f);
    }
}

void oot3d_boss_va_zapper_death(Oot3dBossVa* this, Oot3dPlayState* play) {
    u8* bossVaF00Base = (u8*)this + 0xf00;
    s16* timer = (s16*)(bossVaF00Base + 0xa0);
    u8* burst = oot3d_boss_va_u8(this, OOT3D_BOSS_VA_OFFSET_BURST);
    s32 csState = OOT3D_BOSS_VA_STATE_TABLE->state_09;
    s32 params = oot3d_boss_va_params(this);
    s32 shouldBurst;

    FUN_0031cb28(this);

    if (((*(u32*)oot3d_ptr_add(play, OOT3D_PLAY_OFFSET_GAMEPLAY_FRAMES) & 0x1f) == 0) && (csState < 0x10)) {
        *(s16*)(bossVaF00Base + 0xec) = (s16)FUN_003738a8(16384.0f);
        *(s16*)(bossVaF00Base + 0xf2) = (s16)FUN_003738a8(16384.0f);
        *(s16*)(bossVaF00Base + 0xf8) =
            (s16)(FUN_003738a8(16384.0f) + oot3d_boss_va_shape_rot_y(this) - oot3d_boss_va_shape_rot_x(this));
    } else {
        FUN_0036e168(oot3d_boss_va_float(this, OOT3D_BOSS_VA_OFFSET_SKEL_PLAY_SPEED), 0.0f, 1.0f, 0.025f, 0.0f);
    }

    FUN_003731e0(oot3d_boss_va_skel_anime(this));
    FUN_00375a18(
        oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_E6),
        *(s16*)(bossVaF00Base + 0xec),
        1,
        (s16)FUN_003738a8(500.0f) + 500,
        0);
    FUN_00375a18(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_E4), 0, 1, 500, 0);
    FUN_00375a18(
        oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_EC),
        *(s16*)(bossVaF00Base + 0xf2),
        1,
        (s16)FUN_003738a8(500.0f) + 500,
        0);
    FUN_00375a18(oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_EA), 0, 1, 500, 0);
    FUN_00375a18(
        oot3d_boss_va_s16(this, OOT3D_BOSS_VA_OFFSET_ROT_F2),
        *(s16*)(bossVaF00Base + 0xf8),
        1,
        (s16)FUN_003738a8(500.0f) + 500,
        0);

    if (csState < 0x10 || csState > 0x12) {
        return;
    }

    if (*burst != 0) {
        *timer -= 1;
        if (*timer == 0) {
            if (params == OOT3D_BOSS_VA_PARAM_ZAPPER_3) {
                OOT3D_BOSS_VA_STATE_TABLE->state_09 = csState + 1;
            }
            FUN_00374428(this);
        }
        return;
    }

    shouldBurst = ((params == OOT3D_BOSS_VA_PARAM_ZAPPER_1) && (*timer >= 24)) ||
                  ((params == OOT3D_BOSS_VA_PARAM_ZAPPER_2) && (*timer >= 36)) ||
                  (params != OOT3D_BOSS_VA_PARAM_ZAPPER_1 && params != OOT3D_BOSS_VA_PARAM_ZAPPER_2 &&
                   params != OOT3D_BOSS_VA_PARAM_ZAPPER_3);
    if (shouldBurst) {
        *burst += 1;
        *oot3d_boss_va_u8(this, OOT3D_BOSS_VA_OFFSET_IS_DEAD) = 1;
        *timer = 48;
        OOT3D_BOSS_VA_STATE_TABLE->state_09 = csState + 1;
        oot3d_boss_va_set_death_env(play);
        FUN_00375bcc(this, 0);
        return;
    }

    if (((*timer % 2) == 0) && (*timer >= 0)) {
        if (*timer < 8) {
            FUN_0031c7d4(play, this, 1, (s16)FUN_003738a8(5.0f) + 0xd, 2, 1);
        } else {
            FUN_0031c7d4(play, this, 1, (s16)FUN_003738a8(5.0f) + 6, 2, 1);
        }
        FUN_0031f5a8(play, this, 2, 0x32, 5, 1);
    }

    *timer += 1;
    if (*timer >= 48) {
        *burst += 1;
        *oot3d_boss_va_u8(this, OOT3D_BOSS_VA_OFFSET_IS_DEAD) = 1;
        oot3d_boss_va_set_death_env(play);
        FUN_00375bcc(this, 0);
    }
}

#undef OOT3D_FORCE_INLINE
#endif
