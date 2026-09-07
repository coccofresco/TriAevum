#include "oot3d/platform_abi.h"
#include "oot3d/startup.h"

extern void nninitLocale(void);
extern void nninitSystem(u32 unusedStartupRegister);
extern void nninitStartUp(void);
extern void nninitCallStaticInitializers(void);
extern int nninitSetup(void);
extern void nnMain(u32 argument0, u32 argument1);
extern u32 Oot3dStateQueryLinearReturnAt00416188(void);

/* Native entrypoint 0x00100000, represented without the final branch thunk. */
void oot3d_process_entry(u32 argument0, u32 argument1) {
    oot3d_clear_bss();
    (void)Oot3dStateQueryLinearReturnAt00416188();
    nninitLocale();
    nninitSystem(0);
    nninitStartUp();
    oot3d_call_process_initializers();
    nninitCallStaticInitializers();
    (void)nninitSetup();
    nnMain(argument0, argument1);
    Oot3dCtr_ExitProcess();
}
