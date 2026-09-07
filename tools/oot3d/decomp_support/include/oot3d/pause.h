#ifndef OOT3D_PAUSE_H
#define OOT3D_PAUSE_H

#include "oot3d/types.h"

typedef struct Oot3dPauseState Oot3dPauseState;

static inline u8* oot3d_pause_ptr_add(void* base, u32 offset) {
    return (u8*)base + offset;
}

static inline s32 oot3d_pause_s32(const void* base, u32 offset) {
    return *(const s32*)((const u8*)base + offset);
}

static inline void oot3d_pause_set_s32(void* base, u32 offset, s32 value) {
    *(s32*)oot3d_pause_ptr_add(base, offset) = value;
}

static inline u8 oot3d_pause_u8(const void* base, u32 offset) {
    return *(const u8*)((const u8*)base + offset);
}

#endif
