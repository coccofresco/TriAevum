#ifndef OOT3D_MATH_H
#define OOT3D_MATH_H

#include "oot3d/types.h"

#define OOT3D_MATH_GET_ATAN2_TBL_ENTRY 0x002BC718u
#define OOT3D_MATH_ATAN2S_ENTRY 0x003758B0u
#define OOT3D_MATH_ATAN2_TABLE_ADDR 0x0050C0E4u
#define OOT3D_MATH_ATAN2_TABLE_COUNT 1025u
#define OOT3D_MATH_ATAN2_INDEX_SCALE 1024.0f
#define OOT3D_MATH_ATAN2_INDEX_BIAS 0.5f

#ifdef __cplusplus
extern "C" {
#endif

extern const u16 gOot3dMathAtan2Table[OOT3D_MATH_ATAN2_TABLE_COUNT];

u16 Oot3d_MathGetAtan2Tbl(float arg0, float arg1);
s16 Oot3d_MathAtan2S(float arg0, float arg1);

#ifdef __cplusplus
}
#endif

#endif
