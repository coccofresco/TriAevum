#include "oot3d/types.h"

#include <stdint.h>

extern u32 DAT_00436320;
extern u32 DAT_00436324;

extern void InitList(void* listObject, u16 capacity);
extern u32 GetNextListObject(u32* listObject, int currentObject);
extern u32 FindContainHeap(u32 listObject, u32 objectAddress);

/*
 * Target 0x00436270. Ghidra identifies DAT_00436320 as a char pointer.
 * The baseline source widened that byte read to a target word, so the
 * adjacent 0xffffff sentinel suppressed initialization of the heap list.
 */
void oot3d_boot_contract_heap_registry_00436270(
    u32* state, u32 signature, u32 rangeBegin, u32 rangeEnd, u32 option) {
    const u32 stateAddress = (u32)(uintptr_t)state;
    u8* const initialized = (u8*)(uintptr_t)DAT_00436320;
    u32* list = (u32*)(uintptr_t)DAT_00436324;

    state[0] = signature;
    state[8] = option & 0xffU;
    state[6] = rangeBegin;
    state[7] = rangeEnd;
    InitList(state + 3, 4);

    if (*initialized == 0) {
        InitList(list, 4);
        *initialized = 1;
    }

    u32 containingHeap = 0;
    u32 candidate = 0;
    for (;;) {
        candidate = GetNextListObject(list, (int)candidate);
        if (candidate == 0) {
            break;
        }
        const u32 candidateBegin =
            *(const u32*)(uintptr_t)(candidate + 0x18U);
        const u32 candidateEnd =
            *(const u32*)(uintptr_t)(candidate + 0x1cU);
        if (stateAddress > candidateBegin && stateAddress < candidateEnd) {
            containingHeap = candidate;
            break;
        }
    }

    if (containingHeap != 0) {
        const u32 nested =
            FindContainHeap(containingHeap + 0x0cU, stateAddress);
        if (nested != 0) {
            containingHeap = nested;
        }
        list = (u32*)(uintptr_t)(containingHeap + 0x0cU);
    }

    const u16 linkOffset = *(const u16*)((const u8*)list + 0x0aU);
    u32* const links = (u32*)(uintptr_t)(stateAddress + linkOffset);
    links[1] = 0;
    if (list[0] != 0) {
        links[0] = list[1];
        *(u32*)(uintptr_t)(list[1] + linkOffset + 4U) = stateAddress;
        list[1] = stateAddress;
    } else {
        links[0] = 0;
        list[0] = stateAddress;
        list[1] = stateAddress;
    }
    ++*(u16*)((u8*)list + 8U);
}
