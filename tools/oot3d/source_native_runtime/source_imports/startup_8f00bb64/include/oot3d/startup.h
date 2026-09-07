#ifndef OOT3D_STARTUP_H
#define OOT3D_STARTUP_H

#include "oot3d/types.h"

extern u32 __bss_start[];
extern u32 __bss_end[];

void oot3d_clear_bss(void);
void oot3d_call_process_initializers(void);
void oot3d_process_entry(u32 argument0, u32 argument1);

/* Typed boundaries used by generated process-initializer bodies. */
u32 oot3d_startup_duplicate_handle(u32* outHandle, u32 sourceHandle);
u32 oot3d_startup_initialize_zeroed_state(u32 address);
void oot3d_startup_handle_result(u32 result);
u32 oot3d_startup_construct_array_address(
    u32 firstAddress, u32 callbackAddress, s32 stride, u32 count
);
u32 oot3d_startup_initialize_heap_address(
    u32 address, u32 size, u32 storageAddress, u32 storageSize,
    u32 alignment, u32 flags
);

void oot3d_startup_construct_003fd23c(u32 address);
u32 oot3d_startup_construct_00401268_address(u32 address, u32 value);
void oot3d_startup_construct_00401318(u32 address);
void oot3d_startup_construct_00401430(u32 address);
u32 oot3d_startup_construct_00401668_address(u32 address, u32 value);
u32 oot3d_startup_construct_00402fc0(u32 address);
void oot3d_startup_construct_00403078(u32 address);
void oot3d_startup_construct_004031f8(u32 address);
void oot3d_startup_construct_00404480(u32 address);

#define oot3d_startup_construct_array(first, callback, stride, count) \
    oot3d_startup_construct_array_address( \
        (u32)(uint32_t)(first), (u32)(uint32_t)(callback), \
        (s32)(stride), (u32)(count))
#define oot3d_startup_initialize_heap( \
    address, size, storage, storageSize, alignment, flags) \
    oot3d_startup_initialize_heap_address( \
        (u32)(uint32_t)(address), (u32)(size), (u32)(storage), \
        (u32)(storageSize), (u32)(alignment), (u32)(flags))
#define oot3d_startup_construct_00401268(address, value) \
    oot3d_startup_construct_00401268_address( \
        (u32)(uint32_t)(address), (u32)(value))
#define oot3d_startup_construct_00401668(address, value) \
    oot3d_startup_construct_00401668_address( \
        (u32)(uint32_t)(address), (u32)(value))

#endif
