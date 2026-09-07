#ifndef OOT3D_STARTUP_H
#define OOT3D_STARTUP_H

#include "oot3d/types.h"

extern u32 __bss_start[];
extern u32 __bss_end[];

void oot3d_clear_bss(void);

#endif
