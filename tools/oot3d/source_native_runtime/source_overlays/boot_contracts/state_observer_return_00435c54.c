#include "oot3d/types.h"

extern u32 DAT_00435d18;
extern u32 DAT_00435d1c;

/* Validated return-value ABI promoted in decomp revision 8f00bb64. */
u32* oot3d_boot_contract_state_observer_00435c54(u32* outputRecord) {
    for (u32 index = 1; index <= 0x15; ++index) {
        outputRecord[index] = 0;
    }
    for (u32 index = 0x29; index <= 0x2d; ++index) {
        outputRecord[index] = 0;
    }

    outputRecord[0x31] = 0;
    outputRecord[0x34] = 0;
    outputRecord[0x35] = 0;
    outputRecord[0x36] = 0xffffffffU;
    outputRecord[0x28] = DAT_00435d18;
    for (u32 index = 0x38; index <= 0x3c; ++index) {
        outputRecord[index] = 0;
    }
    outputRecord[0x40] = 0;
    outputRecord[0x43] = 0;
    outputRecord[0x44] = 0;
    outputRecord[0x45] = 0xffffffffU;
    outputRecord[0] = DAT_00435d1c;
    outputRecord[0x37] = DAT_00435d18;
    return outputRecord;
}
