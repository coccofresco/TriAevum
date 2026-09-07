#ifndef OOT3D_MATRIX_H
#define OOT3D_MATRIX_H

#include "oot3d/types.h"
#include "oot3d/game_math.h"

typedef struct Oot3dMtxF {
    float m[3][4];
} Oot3dMtxF;

typedef struct Oot3dMtxVec3f {
    float x;
    float y;
    float z;
} Oot3dMtxVec3f;

typedef enum Oot3dMatrixMode {
    OOT3D_MTXMODE_NEW = 0,
    OOT3D_MTXMODE_APPLY = 1,
} Oot3dMatrixMode;

float Oot3d_CosF(float angle);
float Oot3d_SinF(float angle);

void FUN_0036c258(float* sinOut, float* cosOut, float angle);
Oot3dMtxF* FUN_003625f8(
    Oot3dMtxF* matrix,
    const Oot3dMtxVec3f* axis,
    float angle
);
Oot3dMtxF* FUN_0036c174(
    Oot3dMtxF* destination,
    const Oot3dMtxF* left,
    const Oot3dMtxF* right
);
void FUN_00373598(
    const Oot3dMtxF* matrix,
    const Oot3dMtxVec3f* source,
    Oot3dMtxVec3f* destination
);

void Matrix_RotateX(
    Oot3dMtxF* matrix,
    float angle,
    Oot3dMatrixMode mode
);
void Matrix_RotateY(
    Oot3dMtxF* matrix,
    float angle,
    Oot3dMatrixMode mode
);
void Matrix_RotateZ(
    Oot3dMtxF* matrix,
    float angle,
    Oot3dMatrixMode mode
);
void Matrix_Scale(
    Oot3dMtxF* matrix,
    float x,
    float y,
    float z,
    Oot3dMatrixMode mode
);
void Matrix_MtxFToYXZRotS(
    const Oot3dMtxF* matrix,
    Oot3dVec3s* rotation,
    s32 normalizeAxes
);

#endif
