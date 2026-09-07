#ifndef OOT3D_RUNTIME_STRUCTURAL_FAMILIES_H
#define OOT3D_RUNTIME_STRUCTURAL_FAMILIES_H

#include "oot3d/actor.h"
#include "oot3d/types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct Oot3dPlayState Oot3dPlayState;
typedef struct Oot3dSkelAnime Oot3dSkelAnime;

#if defined(OOT3D_HOST_RUNTIME_STRUCTURAL_PTR32_RESOLVER)
void* oot3d_host_runtime_structural_ptr32_resolve(u32 address);
#endif

void EnGe2_WaitLookAtPlayer(Oot3dActor* actor, Oot3dPlayState* play);
void EnGe3_WaitLookAtPlayer(Oot3dActor* actor, Oot3dPlayState* play);
void EnFu_WaitForPlayback(Oot3dActor* actor, Oot3dPlayState* play);

void EnStream_Draw(Oot3dActor* actor);
void BgJya1flift_Draw(Oot3dActor* actor, Oot3dPlayState* play);
void BgJyaZurerukabe_Draw(Oot3dActor* actor, Oot3dPlayState* play);
void BgSpot05Soko_Draw(Oot3dActor* actor);
void BgSpot00Break_Draw(Oot3dActor* actor);
void BgGndNisekabe_Draw(Oot3dActor* actor);
void BgMenkuriNisekabe_Draw(Oot3dActor* actor);

void BgHidanSyoku_Update(Oot3dActor* actor, Oot3dPlayState* play);
void BgHidanFslift_Update(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_4d(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_2b(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_8_0037cc94(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_9_0037cd38(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_a_0037cddc(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_b_0037ce80(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_c_0037cf24(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_d_0037cfc8(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_e_0037d06c(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0012bffc(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0012c144(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0012c260(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0012c5f4(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0012c710(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_003264c8(Oot3dActor* actor);
void FUN_00330370(Oot3dActor* actor);
void FUN_0033292c(Oot3dActor* actor);
void FUN_0033398c(Oot3dActor* actor);
void FUN_0033526c(Oot3dActor* actor);
void* FUN_0040dbe4(void* archive);
void* FUN_0040dc48(void* archive);
void* FUN_0040f5f4(void* archive);
void* FUN_0040f660(void* archive);
void* FUN_0048bb64(void* archive);
void* FUN_0048c1b8(void* archive);
void* FUN_0048c21c(void* archive);
void* FUN_0048c288(void* archive);
void* FUN_0048c2ec(void* archive);
void FUN_00222acc(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002230cc(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002231a8(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002445c8(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_00244760(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002449cc(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_00244a88(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_00244cdc(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0037cb5c(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0016cbf8(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0016d4d0(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_001e1ac0(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002148a4(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_00345a08(
    void* state,
    Oot3dPlayState* play,
    s32 duration,
    float firstTarget,
    float secondTarget
);
void FUN_003524ec(
    void* state,
    Oot3dPlayState* play,
    s32 duration,
    float firstTarget,
    float secondTarget
);
void FUN_0035b3b0(
    void* state,
    Oot3dPlayState* play,
    s32 duration,
    float firstTarget,
    float secondTarget
);
void FUN_0035b494(
    void* state,
    Oot3dPlayState* play,
    s32 duration,
    float firstTarget,
    float secondTarget
);
void caseD_0_0026dc48(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_0_0027cfd4(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_0_0027de44(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_0_0027dfe8(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_1_0024cdf4(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_1_0026db7c(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_1_0027df24(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_f_0028e374(
    Oot3dActor* actor,
    Oot3dPlayState* play,
    u32 unused2,
    u32 unused3
);
void caseD_10_0028e4a0(
    Oot3dActor* actor,
    Oot3dPlayState* play,
    u32 unused2,
    u32 unused3
);
void caseD_11_0028e5cc(
    Oot3dActor* actor,
    Oot3dPlayState* play,
    u32 unused2,
    u32 unused3
);
void FUN_0024481c(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002448f4(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_00244d98(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_f_002b5b2c(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_13_002b5cdc(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_11_00392fb8(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0025a7d8(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_00275218(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002890a8(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0030b790(void* object, u8 value);
void FUN_0030c49c(void* object, u8 value);
void FUN_004042b8(void* object, u8 value);
u32 FUN_0035fe90(const void* manager, const void* flags, u32 slot);
u32 FUN_00496774(const void* manager, const void* flags, u32 slot);
u32 FUN_004c931c(const void* manager, const void* flags, u32 slot);
void EnGe1_TalkOfferPlay_Archery(
    Oot3dActor* actor,
    Oot3dPlayState* play
);

bool SetSoundOutputMode(void* audioState, u16 mode);
void FUN_00342230(void);
u32 FUN_00369f3c(void);

s32 FUN_002bfc24(
    const Oot3dActorVec3f* v0,
    const Oot3dActorVec3f* v1,
    const Oot3dActorVec3f* v2,
    float* yIntersect,
    float nx,
    float ny,
    float nz,
    float originDistance,
    float z,
    float x
);

void EnHs_Update(Oot3dActor* actor, Oot3dPlayState* play);
void EnHs2_Update(Oot3dActor* actor, Oot3dPlayState* play);

void EnDh_SpawnDebris(
    Oot3dPlayState* play,
    Oot3dActor* actor,
    const Oot3dActorVec3f* spawnPosition,
    float spread,
    s16 effectParameter,
    float accelerationXZ,
    float scale
);
void EnPeehat_SpawnDust(
    Oot3dPlayState* play,
    Oot3dActor* actor,
    const Oot3dActorVec3f* spawnPosition,
    float spread,
    s16 effectParameter,
    float accelerationXZ,
    float scale
);

typedef void (*Oot3dRuntimeEffectPrimitiveSpawn)(
    uintptr_t play,
    u32 type,
    uintptr_t position,
    uintptr_t velocity,
    uintptr_t acceleration,
    uintptr_t primaryColor,
    uintptr_t environmentColor,
    s16 scale,
    s16 scaleStep,
    s16 life,
    u8 flags
);

void FUN_0034035c(
    uintptr_t play,
    uintptr_t position,
    uintptr_t velocity,
    uintptr_t acceleration,
    uintptr_t primaryColor,
    uintptr_t environmentColor,
    uintptr_t scale,
    uintptr_t scaleStep,
    s32 life
);
void FUN_00343798(
    uintptr_t play,
    uintptr_t position,
    uintptr_t velocity,
    uintptr_t acceleration,
    uintptr_t primaryColor,
    uintptr_t environmentColor,
    uintptr_t scale,
    uintptr_t scaleStep,
    s32 life
);
void FUN_00365d20(
    uintptr_t play,
    uintptr_t position,
    uintptr_t velocity,
    uintptr_t acceleration,
    uintptr_t primaryColor,
    uintptr_t environmentColor,
    uintptr_t scale,
    uintptr_t scaleStep,
    s32 life
);
void FUN_0038f278(
    uintptr_t play,
    uintptr_t position,
    uintptr_t velocity,
    uintptr_t acceleration,
    uintptr_t primaryColor,
    uintptr_t environmentColor,
    uintptr_t scale,
    uintptr_t scaleStep,
    s32 life
);
void FUN_0039a7b0(
    uintptr_t play,
    uintptr_t position,
    uintptr_t velocity,
    uintptr_t acceleration,
    uintptr_t primaryColor,
    uintptr_t environmentColor,
    uintptr_t scale,
    uintptr_t scaleStep,
    s32 life
);

void FUN_0034e568(
    Oot3dPlayState* play,
    const Oot3dActorVec3f* position,
    u32 intensity
);
void FUN_003661a8(
    Oot3dPlayState* play,
    const Oot3dActorVec3f* position,
    u32 intensity
);

s32 EnNiw_OverrideLimbDraw(
    Oot3dPlayState* play,
    s32 limbIndex,
    void* transform,
    const void* actor
);
s32 FUN_00389c20(
    Oot3dPlayState* play,
    s32 limbIndex,
    void* transform,
    const void* actor
);

void FUN_004aa624(const void* input, void* output, s32 rowCount);
void FUN_004b0c24(const void* input, void* output, s32 rowCount);
void FUN_004b7224(const void* input, void* output, s32 rowCount);
void FUN_004aa79c(const void* input, void* output, s32 rowCount);
void FUN_004b0d9c(const void* input, void* output, s32 rowCount);
void FUN_004b739c(const void* input, void* output, s32 rowCount);
void FUN_004aa914(const void* input, void* output, s32 rowCount);
void FUN_004b0f14(const void* input, void* output, s32 rowCount);
void FUN_004b7514(const void* input, void* output, s32 rowCount);
void FUN_004aaaf0(const void* input, void* output, s32 rowCount);
void FUN_004b10f0(const void* input, void* output, s32 rowCount);
void FUN_004b76f0(const void* input, void* output, s32 rowCount);
void FUN_004aac64(const void* input, void* output, s32 rowCount);
void FUN_004b1264(const void* input, void* output, s32 rowCount);
void FUN_004b7864(const void* input, void* output, s32 rowCount);
void FUN_004aae30(const void* input, void* output, s32 rowCount);
void FUN_004b1430(const void* input, void* output, s32 rowCount);
void FUN_004b7a30(const void* input, void* output, s32 rowCount);
void FUN_004aaf7c(const void* input, void* output, s32 rowCount);
void FUN_004b157c(const void* input, void* output, s32 rowCount);
void FUN_004b7b7c(const void* input, void* output, s32 rowCount);

typedef struct Oot3dRuntimeVideoServiceRequest {
    u32 command;
    u32 argument1;
    u32 argument2;
    u16 argument3;
    u16 reserved0;
    u16 argument4;
    u16 reserved1;
    u32 response;
    u32 resource;
} Oot3dRuntimeVideoServiceRequest;

typedef s32 (*Oot3dRuntimeVideoServiceHook)(
    Oot3dRuntimeVideoServiceRequest* request,
    void* userData
);

void Oot3dRuntimeVideo_SetServiceHook(
    Oot3dRuntimeVideoServiceHook hook,
    void* userData
);
s32 SetSendingU_004bb11c(
    u32 resource,
    u32 argument1,
    u32 argument2,
    u16 argument3,
    u16 argument4
);
s32 SetSendingV_004bb174(
    u32 resource,
    u32 argument1,
    u32 argument2,
    u16 argument3,
    u16 argument4
);
s32 SetSendingY_004bb1cc(
    u32 resource,
    u32 argument1,
    u32 argument2,
    u16 argument3,
    u16 argument4
);
s32 SetReceiving_004bb224(
    u32 resource,
    u32 argument1,
    u32 argument2,
    u16 argument3,
    u16 argument4
);

s32 FUN_003079d0(u32 channel);
s32 FUN_00307a48(u32 channel);
u32 FUN_004c7eb8(u32 value);
u32 FUN_004c7f08(u32 value);
void FUN_00417458(void* object);
void FUN_0041aea8(void* object);
void FUN_0041749c(void* object);
void FUN_004225bc(void* object);
void FUN_0041757c(void* object);
void FUN_004227c0(void* object);

void FUN_002d75dc(
    float matrix[12],
    float x,
    float y,
    float z
);
void FUN_003332b4(
    float matrix[12],
    float x,
    float y,
    float z
);

void Oot3dRuntimeRandom_SetStatePointers(
    void* firstState,
    void* secondState
);
float FUN_003655ec(void);
float FUN_003f9060(void);

void* FUN_0040f214(void* header, u32 index);
void* FUN_0040f4ec(void* header, u32 index);

typedef struct Oot3dRuntimeServiceOutputRequest {
    u32 command;
    u32 input;
    u32 count;
    u32 output0;
    u32 output1;
    u32 output2;
    u32 output3;
    u32 resource;
    u32 serviceParameter;
} Oot3dRuntimeServiceOutputRequest;

typedef s32 (*Oot3dRuntimeServiceOutputHook)(
    Oot3dRuntimeServiceOutputRequest* request,
    void* userData
);

void Oot3dRuntimeService_SetOutputHook(
    Oot3dRuntimeServiceOutputHook hook,
    void* userData
);
s32 FUN_0044b188(
    u32* output0,
    u32 input,
    u32* output1,
    u32 resource,
    u32 count,
    u32* output2,
    u32* output3
);
s32 FUN_00493ea0(
    u32* output0,
    u32 input,
    u32* output1,
    u32 resource,
    u32 count,
    u32* output2,
    u32* output3
);

s32 FUN_00409640(s32 value);
s32 FUN_004096c0(s32 value);

typedef void (*Oot3dRuntimeSyncWakeHook)(
    void* control,
    void* userData
);

void Oot3dRuntimeSync_SetWakeHook(
    Oot3dRuntimeSyncWakeHook hook,
    void* userData
);
void* FUN_002da528(void* reference);
void* dtor_0030aedc(void* reference);

void FUN_00309f1c(void* object, s32 value);
void FUN_0030a074(void* object, s32 value);
void FUN_0040f454(void);
float FUN_0048bcc0(const void* object);
s32 FUN_002e1ce8(u16 value);
s32 FUN_0044a1c8(u16 value);
void FUN_002e7054(
    const void* manager,
    const void* source,
    s32 count,
    s32 index
);
void FUN_002f9430(
    const void* manager,
    const void* source,
    s32 count,
    s32 index
);
void FUN_002ccfdc(void* object);
void FUN_0040bb2c(void* object);
u32 FUN_0036e5e0(
    const void* object,
    float phase,
    float step
);

void FUN_0030ed80(void* object, u32 value, float scalar);
void FUN_00404b20(void* object, u32 value, float scalar);
void FUN_00404d38(void* object, u32 value);
void FUN_00408374(void* object, u32 value);
void FUN_00404e58(void* object, u8 value);
void FUN_00408458(void* object, u8 value);

void caseD_47(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_25(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_46(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_24_0039a91c(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_42(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_20(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_e_0027da2c(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_f_0027dbb0(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_c_002a12f8(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_d_002a13a0(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_7_0037f7e0(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_7_00385804(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0016e5a4(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_001944c0(Oot3dActor* actor, Oot3dPlayState* play);

void FUN_00274fa4(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0037989c(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002883b8(Oot3dActor* actor);
void FUN_0033be60(Oot3dActor* actor);
void FUN_00138500(Oot3dActor* actor);
void FUN_00138560(Oot3dActor* actor);
void FUN_00259e98(Oot3dActor* actor);
void FUN_00288594(Oot3dActor* actor);
void FUN_00207200(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0021de8c(Oot3dActor* actor, Oot3dPlayState* play);
void* FUN_0030c4f4(void);
void* FUN_0030c550(void);
void FUN_0040bc68(
    void* owner,
    u32 unused1,
    u32 unused2,
    u32 value
);
void FUN_0040bcf8(
    void* owner,
    u32 unused1,
    u32 unused2,
    u32 value
);
void FUN_00131184(void* object, u32 argument);
void FUN_00287c5c(void* object, u32 argument);
u32 FUN_00353e78(
    void* archive,
    Oot3dPlayState* play,
    void* skelAnime,
    u32 cmb,
    u32 modelResource,
    u32 animationResource,
    u32 argument6,
    u32 argument7,
    u32 argument8
);
u32 FUN_00358ea8(
    void* archive,
    Oot3dPlayState* play,
    void* skelAnime,
    u32 cmb,
    u32 modelResource,
    u32 animationResource,
    u32 argument6,
    u32 argument7,
    u32 argument8
);
void FUN_002d0190(
    s32 index,
    u32 value,
    u32 unused,
    u32 handle
);
void FUN_00356018(
    s32 index,
    u32 value,
    u32 unused,
    u32 handle
);
void FUN_0021d098(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0026f1f4(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002cfcf0(
    u32 descriptor,
    u32 unused1,
    u32 unused2,
    u32 handle
);
void FUN_002cfe00(
    u32 descriptor,
    u32 unused1,
    u32 unused2,
    u32 handle
);
void caseD_40(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_3b(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_2_00243f54(Oot3dActor* actor);
void caseD_2_00245298(Oot3dActor* actor);
void* FUN_0044b4e0(void* object);
void* FUN_0044b510(void* object);
void FUN_0037100c(Oot3dPlayState* play, const u32 init[3]);
void FUN_004c8994(Oot3dPlayState* play, const u32 init[3]);
void FUN_0014ef94(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_002a8e5c(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_003067e4(void* object);
void FUN_004983e8(void* object);
u32 FUN_00465464(u32 resourceId);
u32 FUN_0047d7e0(u32 resourceId);
void FUN_00406648(void* object);
void FUN_004077f8(void* object);
void FUN_0013532c(Oot3dActor* actor, Oot3dPlayState* play);
u32 FUN_0034ad10(void);

void FUN_002055b4(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_0021c8c4(Oot3dActor* actor, Oot3dPlayState* play);
s32 FUN_003059b0(const u32 timestamp[2]);
s32 FUN_00305aa4(const u32 timestamp[2]);
void caseD_e_002969bc(Oot3dActor* actor, Oot3dPlayState* play);
void caseD_10_002a76b8(Oot3dActor* actor, Oot3dPlayState* play);
void FUN_00330710(Oot3dActor* actor);
void FUN_00330988(Oot3dActor* actor);
void FUN_00359b0c(
    uintptr_t play,
    uintptr_t position,
    uintptr_t velocity,
    uintptr_t acceleration,
    uintptr_t scale,
    uintptr_t scaleStep
);
void FUN_00363ec4(
    uintptr_t play,
    uintptr_t position,
    uintptr_t velocity,
    uintptr_t acceleration,
    uintptr_t scale,
    uintptr_t scaleStep
);
s32 FUN_0047dc00(void* owner, s32 index, u16 value);
s32 FUN_004a39a0(void* owner, s32 index, u16 value);
void FUN_0030b4ac(void* list, u32 argument);
void FUN_0030cb90(void* list, u32 argument);
void FUN_00359aa0(
    Oot3dSkelAnime* skelAnime,
    Oot3dPlayState* play,
    s32 animationIndex
);

#endif
