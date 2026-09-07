#ifndef OOT3D_STATIC_INIT_H
#define OOT3D_STATIC_INIT_H

#include "oot3d/types.h"

typedef u32 Oot3dStaticInitGuard;

/*
 * Acquires the target's non-thread-safe, one-word static initializer guard.
 * Returns one only for the first caller and stores one into the guard.
 */
s32 oot3d_static_init_guard_acquire(Oot3dStaticInitGuard* guard);

#endif
