#include "oot3d/platform_abi.h"
#include "oot3d/startup.h"
#include "oot3d/target_native_prelude.h"

typedef void (*Oot3dStartupElementConstructor)(void* element);

extern u32* Oot3dStateFlowLinearMemclearMemclearR4F6CW6CGlobalReturnAt00303A94(
    u32* outputRecord
);
extern int Oot3dNativeGraphLoopCallFlowGuardedCoprocMovefromUserRThreadAndProcClearExclusiveLocalHasExclusiveAccessPlusRCGlobalReturnAt003351B4(
    u32 result
);
extern void* Oot3dStructuralStridedCallbackLoopAt00350820(
    void* first, Oot3dStartupElementConstructor callback, s32 stride, u32 count
);
extern u64 UserHeapTemplate(
    u32* object, u32 size, u32 storageAddress, u32 storageSize,
    u32 alignment, u32 flags
);

extern u32 DAT_003FD250;
extern u32 DAT_004012A8;
extern u32 DAT_004013B4;
extern u32 DAT_004013B8;
extern u32 DAT_004013BC;
extern u32 DAT_00401564;
extern u32 DAT_00401568;
extern u32 DAT_0040156C;
extern u32 DAT_00401570;
extern u32 DAT_00401574;
extern u32 DAT_004016F8;
extern u32 DAT_00402FEC;
extern u32 DAT_00402FF0;
extern u32 DAT_00402FF4;
extern u32 DAT_00402FF8;
extern u32 DAT_00402FFC;
extern u32 DAT_004030D4;
extern u32 DAT_004030D8;
extern u32 DAT_004030DC;
extern u32 DAT_004030E0;
extern u32 DAT_00403288;
extern u32 DAT_0040328C;
extern u32 DAT_00403290;

u32 oot3d_startup_duplicate_handle(u32* outHandle, u32 sourceHandle) {
    return (u32)Oot3dCtr_DuplicateHandle(outHandle, sourceHandle);
}

u32 oot3d_startup_initialize_zeroed_state(u32 address) {
    u32* result =
        Oot3dStateFlowLinearMemclearMemclearR4F6CW6CGlobalReturnAt00303A94(
            (u32*)(uint32_t)address
        );
    return (u32)(uint32_t)result;
}

void oot3d_startup_handle_result(u32 result) {
    (void)Oot3dNativeGraphLoopCallFlowGuardedCoprocMovefromUserRThreadAndProcClearExclusiveLocalHasExclusiveAccessPlusRCGlobalReturnAt003351B4(
        result
    );
}

u32 oot3d_startup_construct_array_address(
    u32 firstAddress, u32 callbackAddress, s32 stride, u32 count
) {
    (void)Oot3dStructuralStridedCallbackLoopAt00350820(
        (void*)(uint32_t)firstAddress,
        (Oot3dStartupElementConstructor)(uint32_t)callbackAddress,
        stride,
        count
    );
    return firstAddress;
}

u32 oot3d_startup_initialize_heap_address(
    u32 address, u32 size, u32 storageAddress, u32 storageSize,
    u32 alignment, u32 flags
) {
    const u64 result = UserHeapTemplate(
        (u32*)(uint32_t)address, size, storageAddress, storageSize,
        alignment, flags
    );
    return (u32)result;
}

void oot3d_startup_construct_003fd23c(u32 address) {
    u32* object = (u32*)(uint32_t)address;
    object[0] = 0;
    object[3] = DAT_003FD250;
}

u32 oot3d_startup_construct_00401268_address(u32 address, u32 value) {
    u32* object = (u32*)(uint32_t)address;
    const u32 initial = DAT_004012A8;
    object[0] = value;
    object[0x15] = initial;
    object[0x11] = initial;
    object[0x0D] = initial;
    object[0x16] = initial;
    object[0x12] = initial;
    object[0x0E] = initial;
    object[0x17] = initial;
    object[0x13] = initial;
    object[0x0F] = initial;
    object[0x18] = initial;
    object[0x14] = initial;
    object[0x10] = initial;
    return address;
}

void oot3d_startup_construct_00401318(u32 address) {
    u32* object = (u32*)(uint32_t)address;
    u32 index;
    object[0] = DAT_004013B4;
    object[1] = 0xFA;
    object[2] = DAT_004013B8;
    object[3] = DAT_004013BC;
    *(u8*)(object + 4) = 0;
    object[5] = 0;
    object[6] = 0;
    for (index = 0; index < 4; ++index) {
        object[7 + index] = 0;
    }
    object[0x10] = 0;
    object[0x11] = 0;
    object[0x12] = 0x10000;
    object[0x13] = 0;
    *(u8*)((u8*)object + 0x55) = 4;
    *(u8*)((u8*)object + 0x56) = 0;
    for (index = 0; index < 4; ++index) {
        object[0x0B + index] = 0;
    }
}

void oot3d_startup_construct_00401430(u32 address) {
    u32* object = (u32*)(uint32_t)address;
    const u32* source = (const u32*)(uint32_t)DAT_00401570;
    u32 index;
    object[0] = DAT_00401564;
    object[1] = 0x3C;
    object[2] = 4000;
    object[3] = 100;
    object[4] = DAT_00401568;
    object[5] = DAT_0040156C;
    object[6] = DAT_00401570;
    object[7] = DAT_00401574;
    object[8] = DAT_0040156C;
    object[9] = 0;
    object[0x0B] = source[0];
    object[0x0C] = source[1];
    object[0x0D] = source[2];
    for (index = 0x0E; index <= 0x3C; ++index) {
        object[index] = 0;
    }
    object[0x26] = 0xA0;
    object[0x28] = 0xA0;
    object[0x2A] = 0xA0;
    object[0x2B] = 0xA0;
    object[0x30] = 0xA0;
    object[0x3D] = 0xBE0;
    object[0x3E] = 0xE60;
    object[0x3F] = 0x820;
    *(u8*)(object + 0x40) = 0;
}

u32 oot3d_startup_construct_00401668_address(u32 address, u32 value) {
    u32* object = (u32*)(uint32_t)address;
    const u32 initial = DAT_004016F8;
    object[0] = value;
    object[0x1A] = initial;
    object[0x16] = initial;
    object[0x12] = initial;
    object[0x1B] = initial;
    object[0x17] = initial;
    object[0x13] = initial;
    object[0x1C] = initial;
    object[0x18] = initial;
    object[0x14] = initial;
    object[0x1D] = initial;
    object[0x19] = initial;
    object[0x15] = initial;
    object[0x20] = 0;
    object[0x21] = 0;
    object[0x22] = 0xFFFFFFFF;
    (void)Oot3dAtomic_ExchangeU32(object + 0x20, 1);
    object[0x21] = 0;
    object[0x22] = 0;
    *(u16*)(object + 1) = 1;
    *(u16*)(object + 0x1F) |= 0x8000;
    return address;
}

u32 oot3d_startup_construct_00402fc0(u32 address) {
    u32* object = (u32*)(uint32_t)address;
    object[0] = DAT_00402FEC;
    object[1] = DAT_00402FF0;
    object[2] = DAT_00402FF4;
    object[3] = DAT_00402FF8;
    object[4] = DAT_00402FFC;
    return address;
}

void oot3d_startup_construct_00403078(u32 address) {
    u32* object = (u32*)(uint32_t)address;
    object[0] = DAT_004030D4;
    object[1] = DAT_004030D4 + 0x20;
    object[2] = 0;
    object[3] = 0;
    object[4] = address + 0x10;
    object[5] = address + 0x10;
    object[6] = DAT_004030D8;
    object[7] = 0x20;
    object[8] = DAT_004030DC;
    object[9] = DAT_004030E0;
    object[10] = 0;
    object[11] = 0;
    object[12] = 0;
    object[13] = 0;
    *(u8*)(object + 14) = 0;
}

void oot3d_startup_construct_004031f8(u32 address) {
    u32* object = (u32*)(uint32_t)address;
    const u32 zeroLike = DAT_00403288;
    u32 index;
    for (index = 0; index <= 0x14; ++index) {
        object[index] = zeroLike;
    }
    object[0x12] = DAT_0040328C;
    object[0x13] = DAT_0040328C;
    object[0x14] = DAT_0040328C;
    object[0x15] = 0;
    object[0x16] = DAT_00403290;
    object[0x17] = DAT_0040328C;
    *(u8*)(object + 0x18) = 1;
    object[0x19] = 0;
    object[0x1A] = 0;
}

void oot3d_startup_construct_00404480(u32 address) {
    u32* object = (u32*)(uint32_t)address;
    object[0] = 0;
    *(u8*)(object + 1) = 1;
    object[2] = 0;
    object[3] = 0;
    object[4] = 0xFFFFFFFF;
    *(u8*)(object + 5) = 0;
    *((u8*)object + 0x15) = 0;
}
