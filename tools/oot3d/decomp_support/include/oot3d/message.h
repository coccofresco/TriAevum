#ifndef OOT3D_MESSAGE_H
#define OOT3D_MESSAGE_H

#include "oot3d/types.h"

typedef struct Oot3dMessageContext Oot3dMessageContext;
typedef struct Oot3dActor Oot3dActor;

#define OOT3D_MESSAGE_OFFSET_PLAY_STATE 0x0008
#define OOT3D_MESSAGE_OFFSET_TEXTBOX_STATE 0x0F38
#define OOT3D_MESSAGE_OFFSET_TEXTBOX_MODE 0x0FA0
#define OOT3D_MESSAGE_OFFSET_TEXTBOX_NEXT_STATE 0x0FA4

static inline u8* oot3d_message_ptr_add(void* base, u32 offset) {
    return (u8*)base + offset;
}

static inline void* oot3d_message_play_state(Oot3dMessageContext* msgCtx) {
    return *(void**)oot3d_message_ptr_add(msgCtx, OOT3D_MESSAGE_OFFSET_PLAY_STATE);
}

static inline void oot3d_message_set_u8(Oot3dMessageContext* msgCtx, u32 offset, u8 value) {
    *oot3d_message_ptr_add(msgCtx, offset) = value;
}

static inline void oot3d_message_set_s32(Oot3dMessageContext* msgCtx, u32 offset, s32 value) {
    *(s32*)oot3d_message_ptr_add(msgCtx, offset) = value;
}

#endif
