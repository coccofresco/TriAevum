#include "oot3d/types.h"

typedef void (*Oot3dStartupElementCallback)(void* element);

extern void* Oot3dStructuralStridedCallbackLoopAt00350820(
    void* first, Oot3dStartupElementCallback callback, s32 stride, u32 count
);

extern u32 DAT_003FF9F0;
extern u32 DAT_003FF9F4;
extern u32 DAT_00401310;
extern u32 DAT_00401314;

#define OOT3D_STARTUP_NOOP_CALLBACK(entry) \
    void oot3d_startup_callback_##entry(void* element) { (void)element; }

OOT3D_STARTUP_NOOP_CALLBACK(003fb360)
OOT3D_STARTUP_NOOP_CALLBACK(003fb364)
OOT3D_STARTUP_NOOP_CALLBACK(003fb368)
OOT3D_STARTUP_NOOP_CALLBACK(003fb4c8)
OOT3D_STARTUP_NOOP_CALLBACK(003fc088)
OOT3D_STARTUP_NOOP_CALLBACK(003fd238)
OOT3D_STARTUP_NOOP_CALLBACK(003fd780)
OOT3D_STARTUP_NOOP_CALLBACK(0040c198)
OOT3D_STARTUP_NOOP_CALLBACK(0040c19c)
OOT3D_STARTUP_NOOP_CALLBACK(0040c600)
OOT3D_STARTUP_NOOP_CALLBACK(0040c604)
OOT3D_STARTUP_NOOP_CALLBACK(0040c6c0)
OOT3D_STARTUP_NOOP_CALLBACK(0040c6c4)
OOT3D_STARTUP_NOOP_CALLBACK(0040cbe4)
OOT3D_STARTUP_NOOP_CALLBACK(004c9748)

void oot3d_startup_callback_001001c4(void* element) {
    u32* words = element;
    words[0] = 0;
    words[1] = 0;
}

void oot3d_startup_callback_003fcca8(void* element) {
    (void)Oot3dStructuralStridedCallbackLoopAt00350820(
        (u8*)element + 4, oot3d_startup_callback_003fd780, 0x24, 0x10
    );
}

void oot3d_startup_callback_003fcccc(void* element) {
    (void)Oot3dStructuralStridedCallbackLoopAt00350820(
        (u8*)element + 0x10, oot3d_startup_callback_004c9748, 0x28, 0x20
    );
}

void oot3d_startup_callback_003fccf0(void* element) {
    (void)Oot3dStructuralStridedCallbackLoopAt00350820(
        (u8*)element + 0x14F0, oot3d_startup_callback_003fd238, 0x1C, 3
    );
}

void oot3d_startup_callback_003ff970(void* element) {
    u8* bytes = element;
    bytes[0] = 0x7F;
    bytes[1] = 0x7F;
}

void oot3d_startup_callback_003ff9d0(void* element) {
    u32* words = element;
    words[0] = DAT_003FF9F0;
    words[1] = DAT_003FF9F0;
    words[2] = DAT_003FF9F4;
    words[3] = 0;
}

void oot3d_startup_callback_003ff9f8(void* element) {
    u32* words = element;
    words[0] = 0;
    words[1] = 0;
}

void oot3d_startup_callback_003ffce4(void* element) {
    u32* words = element;
    words[0] = 0;
    words[1] = 0;
    words[2] = 0;
}

void oot3d_startup_callback_004012ec(void* element) {
    u32* words = element;
    words[0] = 0xFA;
    words[1] = DAT_00401310;
    words[2] = DAT_00401314;
    ((u8*)element)[0x0C] = 0;
}

void oot3d_startup_callback_004027f4(void* element) {
    ((u32*)element)[0] = 0;
}

void Oot3dIntrusiveListHeaderInitAt0040559C(void* element) {
    u32* words = element;
    u32 sentinel = (u32)(uint32_t)&words[1];
    words[0] = 0;
    words[1] = sentinel;
    words[2] = sentinel;
    words[3] = 1;
}

void oot3d_startup_callback_00448308(void* element) {
    u32* words = element;
    words[0] = 0;
    words[1] = 0;
}
