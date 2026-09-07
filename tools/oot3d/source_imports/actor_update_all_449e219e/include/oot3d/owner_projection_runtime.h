#ifndef OOT3D_OWNER_PROJECTION_RUNTIME_H
#define OOT3D_OWNER_PROJECTION_RUNTIME_H

#include "oot3d/game_math.h"

void FUN_00368cc0(
    const void* play,
    const Oot3dActorVec3f* world,
    Oot3dActorVec3f* projected,
    float* inverseW
);
void Actor_GetScreenPos(
    const void* play,
    const void* actor,
    s16* x,
    s16* y
);

#endif
