#include "oot3d/pause.h"

extern void FUN_002f0618(void);
extern void* FUN_00313ce0(void* object, s32 arg1);
extern void FUN_00480bd8(void);

extern void* oot3d_pause_icon_group_init(void* object, s32 arg1, s32 group, s32 arg3);
extern void oot3d_pause_dungeon_item_panel_init(void);

#define OOT3D_PAUSE_DUNGEON_ITEM_GROUP_ARG ((void*)0x00000474u)
#define OOT3D_PAUSE_DUNGEON_ITEM_PAGE_STATE ((void*)0x00506CB0u)

void oot3d_pause_dungeon_item_page_init(void) {
    register s32 zero asm("r4");
    register s32 arg1 asm("r1");
    register void* state asm("r1");
    register s32 clear asm("r0");
    register s32 group asm("r0");

    oot3d_pause_dungeon_item_panel_init();
    FUN_00480bd8();
    group = (s32)OOT3D_PAUSE_DUNGEON_ITEM_GROUP_ARG;
    __asm__ volatile("" : "+r"(group));
    arg1 = zero;
    group = (s32)FUN_00313ce0((void*)group, arg1);
    __asm__ goto("cmp %0, #0\n\tbeq %l[store_group]" : : "r"(group) : "cc" : store_group);
    group = (s32)oot3d_pause_icon_group_init((void*)group, 1, 8, 0);

store_group:
    state = OOT3D_PAUSE_DUNGEON_ITEM_PAGE_STATE;
    __asm__ volatile("" : "+r"(state));
    oot3d_pause_set_s32(state, 0x24, group);
    __asm__ volatile("" ::: "memory");
    clear = 0;
    __asm__ volatile("" : "+r"(clear));
    oot3d_pause_set_s32(state, 0x3c, clear);
    __asm__ volatile("" ::: "memory");
    FUN_002f0618();
}
