#ifndef OOT3D_RUNTIME_MASS_RECOVERY_H
#define OOT3D_RUNTIME_MASS_RECOVERY_H

#include "oot3d/actor.h"
#include "oot3d/actor_bgcheck.h"
#include "oot3d/matrix.h"
#include "oot3d/skel_anime.h"

s32 FUN_003723c0(
    void*, const Oot3dActorVec3f*, const Oot3dActorVec3f*,
    Oot3dActorVec3f*, u32*, s32, s32, s32, s32, s32*
);
s32 FUN_003aa580(
    void*, const Oot3dActorVec3f*, const Oot3dActorVec3f*,
    Oot3dActorVec3f*, u32*, s32, s32, s32, s32, s32*
);
void FUN_0031a3dc(void* object);
void FUN_00322e80(void* object);
u32 FUN_0034fe20(
    Oot3dActor*, Oot3dPlayState*, void*, s32, s32, void*, void*, s32
);
u32 FUN_00353c9c(
    Oot3dActor*, Oot3dPlayState*, void*, s32, s32, void*, void*, s32
);
void FUN_00104118(void* object);
void FUN_001310a0(void* object);
void FUN_0040fa1c(const u32* resourceCell, float frame, float output[4]);
void FUN_0040fa94(const u32* resourceCell, float frame, float output[4]);
u32 FUN_00494654(u32 argument);
u32 FUN_00494c7c(u32 argument);
void FUN_003404a8(Oot3dSkelAnime*, Oot3dPlayState*, s32, float);
void FUN_00358dfc(Oot3dSkelAnime*, Oot3dPlayState*, s32, float);
void caseD_3_003a5da0(Oot3dActor*, Oot3dPlayState*);
void caseD_1_003a7ae4(Oot3dActor*, Oot3dPlayState*);
float FUN_00372300(void*, u32*, s32*, Oot3dActorVec3f*);
s32 FUN_00438600(void);
s32 FUN_004452b8(void);
void FUN_00244b44(Oot3dActor*, Oot3dPlayState*);
void FUN_0037ca9c(Oot3dActor*, Oot3dPlayState*);
void FUN_0027f34c(
    uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, s32
);
void FUN_00357a84(
    uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, s32
);
void caseD_1e(Oot3dActor*, Oot3dPlayState*);
void caseD_2e(Oot3dActor*, Oot3dPlayState*);
void FUN_0033b608(void);
void caseD_49(Oot3dActor*, Oot3dPlayState*);
void caseD_27_003a26f4(Oot3dActor*, Oot3dPlayState*);
void caseD_3_003a24d0(Oot3dActor*, Oot3dPlayState*);
void caseD_17_003a3b0c(Oot3dActor*, Oot3dPlayState*);
void FUN_00285d84(Oot3dActor*, Oot3dPlayState*);
void FUN_00285e5c(Oot3dActor*, Oot3dPlayState*);
void FUN_002ed908(s32 scalar);
void FUN_002edb18(s32 scalar);
void caseD_44(Oot3dActor*, Oot3dPlayState*);
void caseD_22_002b2390(Oot3dActor*, Oot3dPlayState*);
void FUN_0012adb4(Oot3dActor*);
void FUN_0012b7e8(Oot3dActor*);
void FUN_00314fe4(Oot3dPlayState*, Oot3dActor*);
void FUN_00357680(Oot3dPlayState*, Oot3dActor*);
void FUN_00257f7c(Oot3dPlayState*, s32, u32, float);
void FUN_00379514(Oot3dPlayState*, s32, u32, float);
s32 FUN_002bf520(
    const Oot3dActorVec3f*, const Oot3dActorVec3f*,
    const Oot3dActorVec3f*, float*,
    float, float, float, float, float, float, float
);
void FUN_003373c8(
    Oot3dPlayState*, const Oot3dActorVec3f*, u32, u32,
    s16, s16, s16, s16
);
void FUN_002ad270(Oot3dActor*);
void FUN_00408aac(void*);
void FUN_00222e00(Oot3dActor*, Oot3dPlayState*);
void FUN_00223284(Oot3dActor*, Oot3dPlayState*);
void FUN_00222ef4(Oot3dActor*, Oot3dPlayState*);
void FUN_00222fe0(Oot3dActor*, Oot3dPlayState*);
void FUN_002eb4e4(void);
void FUN_002f88e0(void);
void caseD_10(Oot3dActor*, Oot3dPlayState*);
void caseD_13(Oot3dActor*, Oot3dPlayState*);
void caseD_41(Oot3dActor*, Oot3dPlayState*);
void caseD_1f(Oot3dActor*, Oot3dPlayState*);
void caseD_4_002aeda4(Oot3dActor*, Oot3dPlayState*);
void caseD_5_002aeea8(Oot3dActor*, Oot3dPlayState*);
void FUN_001f285c(
    void*, const Oot3dActorVec3f*, const Oot3dActorVec3f*,
    const Oot3dActorVec3f*, float
);
void FUN_00335814(
    void*, const Oot3dActorVec3f*, const Oot3dActorVec3f*,
    const Oot3dActorVec3f*, float
);
void FUN_00230d84(Oot3dActor*, Oot3dPlayState*, const void*);
void FUN_003cf3c4(Oot3dActor*, Oot3dPlayState*, const void*);
void FUN_002e12d8(void);
void FUN_002e1478(void);
void FUN_002e6ed0(void*);
void FUN_002f0c84(const u32[3]);
void FUN_002fc950(void*);
void FUN_002f0b2c(const u32[3]);
void FUN_00441ffc(const u32[3]);
void FUN_003510f0(Oot3dPlayState*, s32, float);
void FUN_0035123c(Oot3dPlayState*, s32, float);
void FUN_0033a754(
    Oot3dMtxF*, s32, s32, s32,
    float, float, float, float, float, float
);
void FUN_00358188(
    Oot3dMtxF*, s32, s32, s32,
    float, float, float, float, float, float
);
s32 FUN_00113378(Oot3dPlayState*, s32, Oot3dMtxF*, void*);
s32 FUN_0011348c(Oot3dPlayState*, s32, Oot3dMtxF*, void*);
struct MassRecoveryScopedLock;
struct MassRecoveryRecursiveLock;
struct MassRecoveryScopedLock* FUN_002da58c(
    struct MassRecoveryScopedLock*,
    struct MassRecoveryRecursiveLock*
);
struct MassRecoveryScopedLock* ScopedLock_0030af40(
    struct MassRecoveryScopedLock*,
    struct MassRecoveryRecursiveLock*
);
void caseD_7_002a0fd8(void*, void*);
void caseD_8_002a1130(void*, void*);
s32 SaveVramSysArea(void);
s32 FUN_00485ab0(void);
void FUN_00345cb4(void*, s32, s32);
void FUN_0035a050(void*, s32, s32);
void FUN_0013e408(void*);
void EnShopnuts_Wait(void*);
void FUN_00438c58(void);
void FUN_004453e8(void);
void FUN_00440ac4(void*);
void FUN_00440d6c(void*);

#endif
