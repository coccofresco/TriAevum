#include "oot3d/types.h"

#include <stdint.h>

typedef void (*Oot3dElementCallback)(void* element);

extern u32 DAT_003FB518;
extern u32 DAT_003FB51C;
extern u32 DAT_00402710;
extern u32 DAT_00402714;
extern u32 DAT_00402718;
extern u32 DAT_0040271C;
extern u32 DAT_00402CC4;
extern u32 DAT_00402CC8;

extern void* Oot3dStructuralStridedCallbackLoopAt00350820(
    void* first, Oot3dElementCallback callback, s32 stride, u32 count
);

/* Validated constructor ABI closure promoted in decomp revision 8f00bb64. */
u32* oot3d_boot_contract_constructor_004026c4(u32* outputRecord) {
    outputRecord[0] = DAT_00402710;
    outputRecord[1] = 0;
    u8* const array = Oot3dStructuralStridedCallbackLoopAt00350820(
        outputRecord + 2,
        (Oot3dElementCallback)(uintptr_t)DAT_00402714,
        0x10,
        4
    );
    outputRecord = (u32*)(array - 8);
    *(u32*)(array + 0x40) = DAT_00402718;
    *(u32*)(array + 0x44) = DAT_00402718;
    *(u32*)(array + 0x48) = DAT_0040271C;
    array[0x4c] = 0;
    array[0x4d] = 1;
    return outputRecord;
}

u32* oot3d_boot_contract_constructor_00402c60(u32* outputRecord) {
    u32* const result =
        oot3d_boot_contract_constructor_004026c4(outputRecord);
    result[0] = DAT_00402CC4;
    result[0x16] = DAT_00402CC4 + 0x24U;
    result[0x17] = 0;
    result[0x18] = 0;
    result[0x19] = 0;
    for (u32 index = 0x1a; index <= 0x1f; ++index) {
        result[index] = DAT_00402CC8;
    }
    ((u8*)result)[0x80] = 1;
    ((u8*)result)[0x81] = 0;
    ((u8*)result)[0x82] = 1;
    return result;
}

u32* oot3d_boot_contract_constructor_003fb4cc(u32* outputRecord) {
    u32* const result =
        oot3d_boot_contract_constructor_00402c60(outputRecord);
    result[0] = DAT_003FB518;
    result[0x16] = DAT_003FB518 + 0x24U;
    result[0x27] = 0;
    ((u8*)result)[0x83] = 0;
    result[0x21] = 0;
    result[0x22] = 0;
    result[0x23] = 0;
    result[0x24] = 0;
    result[0x25] = DAT_003FB51C;
    ((u8*)result)[0x98] = 0;
    ((u8*)result)[0x99] = 0;
    ((u8*)result)[0x9a] = 0;
    return result;
}
