#include "oot3d/types.h"

#include <stdint.h>

typedef u32 (*Oot3dOwnerObjectQuery1)(u32 self);
typedef void (*Oot3dOwnerObjectMethod1)(u32 self);

extern int Free_0030eb68(int allocator, u32* allocation);
extern u32 GetIpcObject(int object);
extern u32* dtor_003111e8(u32* object);
extern s32 CloseArchive(const u32* handle, u32 unused, u32 first, u32 second);
extern u32 DAT_0054A9E4[];
extern void* oot3d_host_resolve_target_function(uintptr_t address);

static u32 OwnerDestructor_ReadWord(const void* object, u32 offset) {
    return *(const u32*)((const u8*)object + offset);
}

static void OwnerDestructor_WriteWord(void* object, u32 offset, u32 value) {
    *(u32*)((u8*)object + offset) = value;
}

static u32 OwnerDestructor_CallQuery1(u32 slotAddress, const void* object) {
    const u32 targetAddress = *(const u32*)(uintptr_t)slotAddress;
    Oot3dOwnerObjectQuery1 const function =
        (Oot3dOwnerObjectQuery1)(uintptr_t)
            oot3d_host_resolve_target_function(targetAddress);
    return function((u32)(uintptr_t)object);
}

static void OwnerDestructor_CallMethod1(u32 slotAddress, const void* object) {
    const u32 targetAddress = *(const u32*)(uintptr_t)slotAddress;
    Oot3dOwnerObjectMethod1 const function =
        (Oot3dOwnerObjectMethod1)(uintptr_t)
            oot3d_host_resolve_target_function(targetAddress);
    function((u32)(uintptr_t)object);
}

int Oot3dMemoryStreamDeletingDestructorAt003FFAB0(u8* object) {
    const u32 vtable = OwnerDestructor_ReadWord(object, 0x00);
    (void)OwnerDestructor_CallQuery1(vtable + 0x24, object);
    const u32 owner = OwnerDestructor_ReadWord(object, 0x04);
    return Free_0030eb68((int)(owner + 0xa0), (u32*)object);
}

u8* Oot3dMemoryStreamDestructorAt003FFB7C(u8* object) {
    return object;
}

u32* Oot3dVtableObjectDtorAt0044DA50(u32* object) {
    OwnerDestructor_WriteWord(object, 0x00, 0x004ec2d4U);
    if (OwnerDestructor_ReadWord(object, 0x24) != 0) {
        OwnerDestructor_WriteWord(object, 0x24, 0);
    }
    return dtor_003111e8(object);
}

int Oot3dAllocatorBackedDeletingDestructorAt003FFD60(u8* object) {
    const u32 vtable = OwnerDestructor_ReadWord(object, 0x00);
    (void)OwnerDestructor_CallQuery1(vtable + 0x2c, object);
    return Free_0030eb68(0x005ae994, (u32*)object);
}

u8* Oot3dArchiveOwnerDestructorAt004000A0(u8* object) {
    OwnerDestructor_WriteWord(object, 0x00, 0x004ec238U);
    const u32 archiveFirst = OwnerDestructor_ReadWord(object, 0x08);
    const u32 archiveSecond = OwnerDestructor_ReadWord(object, 0x0c);
    if (archiveFirst != 0 || archiveSecond != 0) {
        const u32 handle = GetIpcObject((int)(uintptr_t)object);
        (void)CloseArchive(&handle, 0, archiveFirst, archiveSecond);
        OwnerDestructor_WriteWord(object, 0x08, 0);
        OwnerDestructor_WriteWord(object, 0x0c, 0);
    }
    OwnerDestructor_WriteWord(object, 0x04, 0);
    return object;
}

void Oot3dOwnerDeletingDestructor_00400104(u8* object) {
    const u32 vtable = OwnerDestructor_ReadWord(object, 0x00);
    (void)OwnerDestructor_CallQuery1(vtable + 0x2c, object);
    (void)Free_0030eb68((int)DAT_0054A9E4[1], (u32*)object);
}

u8* Oot3dOwnerDestructor_00400134(u8* object) {
    const u32 childAddress = OwnerDestructor_ReadWord(object, 0x04);
    OwnerDestructor_WriteWord(object, 0x00, 0x004ec1c8U);
    if (childAddress != 0) {
        u8* const child = (u8*)(uintptr_t)childAddress;
        const u32 childVtable = OwnerDestructor_ReadWord(child, 0x00);
        OwnerDestructor_CallMethod1(childVtable + 0x20, child);
    }

    u8* const secondBase = object + 0xdc;
    OwnerDestructor_WriteWord(secondBase, 0x00, 0x004ec2d4U);
    if (OwnerDestructor_ReadWord(secondBase, 0x24) != 0) {
        OwnerDestructor_WriteWord(secondBase, 0x24, 0);
    }
    (void)dtor_003111e8((u32*)secondBase);

    u8* const firstBase = object + 0xa0;
    OwnerDestructor_WriteWord(firstBase, 0x00, 0x004ec2d4U);
    if (OwnerDestructor_ReadWord(firstBase, 0x24) != 0) {
        OwnerDestructor_WriteWord(firstBase, 0x24, 0);
    }
    (void)dtor_003111e8((u32*)firstBase);
    return object;
}
