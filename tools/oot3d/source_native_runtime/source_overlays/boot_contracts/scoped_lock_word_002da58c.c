#include "oot3d/types.h"

#include <stdint.h>

typedef struct Oot3dRecursiveLock {
    volatile s32 state;
    u32 ownerThread;
    u32 depth;
} Oot3dRecursiveLock;

typedef struct Oot3dScopedLockWord {
    u32 lockAddress;
} Oot3dScopedLockWord;

_Static_assert(sizeof(Oot3dScopedLockWord) == 4,
               "target scoped-lock storage is one 32-bit guest address");
_Static_assert(sizeof(Oot3dRecursiveLock) == 12,
               "target recursive-lock layout is three 32-bit words");

extern u32 oot3d_host_system_arena_current_thread_id(void);
extern void EnterImpl_003351e8(Oot3dRecursiveLock* lock);

static Oot3dScopedLockWord* oot3d_boot_contract_acquire_scoped_lock(
    Oot3dScopedLockWord* scoped, Oot3dRecursiveLock* lock) {
    const u32 threadId = oot3d_host_system_arena_current_thread_id();

    scoped->lockAddress = (u32)(uintptr_t)lock;
    if (threadId != lock->ownerThread) {
        s32 acquired = 0;
        do {
            const s32 observed = lock->state;
            if (observed <= 0) {
                break;
            }
            acquired = __sync_bool_compare_and_swap(
                &lock->state, observed, -observed);
        } while (acquired == 0);

        if (acquired != 0) {
            lock->ownerThread = threadId;
        } else {
            EnterImpl_003351e8(lock);
        }
    }
    ++lock->depth;
    return scoped;
}

Oot3dScopedLockWord* oot3d_boot_contract_scoped_lock_002da58c(
    Oot3dScopedLockWord* scoped, Oot3dRecursiveLock* lock) {
    return oot3d_boot_contract_acquire_scoped_lock(scoped, lock);
}

Oot3dScopedLockWord* oot3d_boot_contract_scoped_lock_0030af40(
    Oot3dScopedLockWord* scoped, Oot3dRecursiveLock* lock) {
    return oot3d_boot_contract_acquire_scoped_lock(scoped, lock);
}
