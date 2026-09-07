#include "oot3d/pause.h"

extern void FUN_002eb4e4(void);
extern void FUN_002e9a00(void);
extern void FUN_002f74a4(s32 mode);
extern void FUN_002f7af4(void* item, float x, float y);
extern void FUN_002f9a1c(void* object);
extern void FUN_00343270(void* object);

extern void oot3d_pause_icon_refresh_widget_state(void* widget);
extern void oot3d_pause_item_equip_update_selected_detail(void* object, s32 arg1);
extern void oot3d_pause_item_inventory_sync_and_refresh(s32 mode);

#define OOT3D_PAUSE_ITEM_PAGE_STATE ((u8*)0x005066F8u)
#define OOT3D_PAUSE_ITEM_PAGE_ROOT ((u8*)0x005067A8u)

void oot3d_pause_item_page_set_mode(s32 mode) {
    register s32 modeReg asm("r0") = mode;
    register void* state asm("r4");
    register s32 zero asm("r5");

    __asm__ goto(
        "cmp %2, #0\n\t"
        "ldr %0, =0x005066F8\n\t"
        "mov %1, #0\n\t"
        "beq %l[mode_zero]"
        : "=r"(state), "=r"(zero)
        : "r"(modeReg)
        : "cc"
        : mode_zero);

    __asm__ goto("cmp %0, #1\n\tbne %l[page_common]" : : "r"(modeReg) : "cc" : page_common);

    __asm__ volatile(
        "mov r0, #0xd\n\t"
        "str r0, [%0, #0x34]\n\t"
        "mov r0, #0x5\n\t"
        "str r0, [%0, #0x60]"
        :
        : "r"(state)
        : "r0", "memory");
    FUN_002eb4e4();
    FUN_002f9a1c(*(void**)oot3d_pause_ptr_add(state, 0x24));

page_common:
    __asm__ volatile(
        "mvn r0, #0\n\t"
        "str r0, [%0, #0x88]\n\t"
        "str r0, [%0, #0x8c]"
        :
        : "r"(state)
        : "r0", "memory");
    {
        register void* itemRoot asm("r0") = OOT3D_PAUSE_ITEM_PAGE_ROOT;

        __asm__ volatile("" : "+r"(itemRoot));
        FUN_002f7af4(*(void**)oot3d_pause_ptr_add(itemRoot, 4), 400.0f, 240.0f);
    }
    oot3d_pause_set_s32(state, 0xa4, zero);
    oot3d_pause_set_s32(state, 0xa8, zero);
    FUN_002e9a00();
    oot3d_pause_item_equip_update_selected_detail(*(void**)oot3d_pause_ptr_add(state, 0x64), 1);
    {
        register s32 zeroArg asm("r0") = 0;

        __asm__ volatile("nop" : "+r"(zeroArg));
        FUN_002f74a4(zeroArg);
    }
    {
        register s32 one asm("r0") = 1;

        __asm__ volatile("nop" : "+r"(one));
        oot3d_pause_item_inventory_sync_and_refresh(one);
    }
    oot3d_pause_icon_refresh_widget_state(*(void**)oot3d_pause_ptr_add(state, 0x3c));
    return;

mode_zero:
    __asm__ volatile(
        "mov r0, #0x1\n\t"
        "str r0, [%0, #0x34]"
        :
        : "r"(state)
        : "r0", "memory");
    FUN_00343270(*(void**)oot3d_pause_ptr_add(state, 0x24));
    oot3d_pause_set_s32(state, 0x60, zero);
    oot3d_pause_set_s32(state, 0x84, zero);
    goto page_common;
}
