#include "oot3d/types.h"

#include <stdint.h>

extern u32 DAT_00416180;
extern u32 DAT_00416184;
extern u32 DAT_00416528;

extern u32 GetAppMemorySize(void);
extern u32 GetUsingMemorySize(void);
extern void SetDeviceMemorySize(u32 size);
extern void SetHeapSize(u32 size);
extern u32 GetHeapSize(void);
extern void InitializeMemoryBlock(u32 address, u32 size);
extern void InitializeAllocator(u32 size);
extern void nnosMemoryBlockAllocate(u32* block, int size);
extern void
Oot3dNativeGraphLoopStateFlowGuardedStatePeer00313Ce0HasExclusiveAccessScopedLockPlusRDEFWDEGlobalAt00416CC4(
    int arenaStart, int arenaSize);
extern void* oot3d_host_resolve_target_function(uintptr_t address);

typedef void (*Oot3dBootCallback)(void);

void oot3d_boot_contract_system_arena_dispatch_004164dc(
    u32 arenaStart, u32 arenaSize) {
    Oot3dNativeGraphLoopStateFlowGuardedStatePeer00313Ce0HasExclusiveAccessScopedLockPlusRDEFWDEGlobalAt00416CC4(
        (int)arenaStart, (int)arenaSize);

    const u32 listAddress = DAT_00416528;
    u32* const listHead = (u32*)(uintptr_t)listAddress;
    u32 relativeOffset = *listHead;
    u32 previousAddress = 0;
    u32 currentAddress = listAddress;

    while (relativeOffset != 0) {
        currentAddress += relativeOffset;
        u32* const entry = (u32*)(uintptr_t)currentAddress;
        const u32 callbackAddress = entry[1];
        if (callbackAddress != 0) {
            Oot3dBootCallback const callback = (Oot3dBootCallback)(uintptr_t)
                oot3d_host_resolve_target_function(callbackAddress);
            callback();
        }
        relativeOffset = entry[0];
        entry[0] = previousAddress;
        previousAddress = currentAddress;
    }
    *listHead = previousAddress;
}

/* Validated startup ABI closure promoted in decomp revision 8f00bb64. */
void oot3d_boot_contract_nninit_startup_004160ec(void) {
    const u32 appMemorySize = GetAppMemorySize();
    const u32 usedMemorySize = GetUsingMemorySize();
    const u32 heapSize =
        ((appMemorySize - usedMemorySize) + 0xfe500000U) & 0xfffff000U;

    SetDeviceMemorySize(0x01b00000U);
    SetHeapSize(heapSize);
    InitializeMemoryBlock(0x08000000U, GetHeapSize());
    InitializeAllocator(0x00080000U);

    u32* const systemArenaBlock = (u32*)(uintptr_t)DAT_00416180;
    nnosMemoryBlockAllocate(systemArenaBlock, (int)(heapSize - 0x006e0000U));
    oot3d_boot_contract_system_arena_dispatch_004164dc(
        systemArenaBlock[2], systemArenaBlock[3]);

    nnosMemoryBlockAllocate(
        (u32*)(uintptr_t)DAT_00416184, 0x00630000);
}
