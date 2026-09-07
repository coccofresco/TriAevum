#include "oot3d/types.h"

extern s32 svcCloseHandle(u32 handle);

/*
 * The owning object stores its CTR handle in the second target word. This
 * entrypoint is referenced by a vtable, but was not indexed as a function by
 * the baseline Ghidra export.
 */
u32* Oot3dCtrHandleFieldClose_003fff90(u32* object) {
    u32* handle = &object[1];

    if (*handle != 0) {
        (void)svcCloseHandle(*handle);
        *handle = 0;
    }
    return object;
}
