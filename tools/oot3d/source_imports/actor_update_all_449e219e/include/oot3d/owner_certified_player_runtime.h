#ifndef OOT3D_OWNER_CERTIFIED_PLAYER_RUNTIME_H
#define OOT3D_OWNER_CERTIFIED_PLAYER_RUNTIME_H

#include "oot3d/types.h"

#if defined(OOT3D_HOST_OWNER_PLAYER_GLOBAL_RESOLVER)
void* oot3d_host_owner_player_global_resolve(u32 address);
#endif

#if defined(OOT3D_HOST_OWNER_PLAYER_PTR32_RESOLVER)
void* oot3d_host_owner_player_ptr32_resolve(u32 address);
#endif

s32 Player_InCsMode(void* play);
s32 Player_InBlockingCsMode(void* play, const void* player);
void Player_SetModels(void* player, s32 modelGroup);

#endif
