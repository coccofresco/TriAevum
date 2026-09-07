#ifndef OOT3D_OWNER_CAMERA_MODE_RUNTIME_H
#define OOT3D_OWNER_CAMERA_MODE_RUNTIME_H

#include "oot3d/types.h"

#if defined(OOT3D_HOST_OWNER_CAMERA_GLOBAL_RESOLVER)
void* oot3d_host_owner_camera_global_resolve(u32 address);
#endif

#if defined(OOT3D_HOST_OWNER_CAMERA_PTR32_RESOLVER)
void* oot3d_host_owner_camera_ptr32_resolve(u32 address);
#endif

void FUN_002c0998(s32 camera);
u32 Camera_CheckWater(s32 camera);

s16 FUN_002d064c(void* context, s32 dataIndex, u32 category);
void Camera_InitPlayerSettings(void* camera, void* player);
u32 Camera_ChangeMode(void* camera, u32 mode);
u32 Camera_ChangeModeFlags(void* camera, u32 mode, s32 flags);
s32 Camera_ChangeSettingFlags(void* camera, s32 setting, u32 flags);
u32 Camera_ChangeDataIdx(void* camera, u32 dataIndex);

#endif
