#ifndef OOT3D_CAMERA_QUAKE_H
#define OOT3D_CAMERA_QUAKE_H

#include "oot3d/types.h"

enum {
    OOT3D_CAMERA_QUAKE_REQUEST_SLOT_COUNT = 4,
    OOT3D_CAMERA_QUAKE_REQUEST_SIZE = 0x24,
    OOT3D_CAMERA_QUAKE_CALLBACK_COUNT = 7,
};

typedef struct {
    float x;
    float y;
    float z;
} Oot3dCameraQuakeVec3f;

typedef struct {
    float r;
    s16 pitch;
    s16 yaw;
} Oot3dCameraQuakeVecSphGeo;

typedef struct {
    Oot3dCameraQuakeVec3f at;
    Oot3dCameraQuakeVec3f eye;
} Oot3dCameraQuakeCameraView;

typedef struct {
    s16 randIdx;
    s16 countdownMax;
    u32 cameraRef;
    u8 callbackIndex;
    u8 pad09;
    s16 yMagnitude;
    s16 xMagnitude;
    s16 fovMagnitude;
    s16 pitchMagnitude;
    s16 sphPitchOffset;
    s16 sphYawOffset;
    s16 sphUnused;
    s16 speed;
    s16 relativeToCamera;
    s16 countdown;
    s16 cameraSlot;
    u32 field20;
} Oot3dCameraQuakeRequest;

typedef struct {
    s16 globalFlag;
    s16 activeRequestCount;
    Oot3dCameraQuakeRequest requests[OOT3D_CAMERA_QUAKE_REQUEST_SLOT_COUNT];
} Oot3dCameraQuakeRuntime;

typedef struct {
    Oot3dCameraQuakeVec3f atOffset;
    Oot3dCameraQuakeVec3f eyeOffset;
    s16 pitchOffset;
    s16 yawOffset;
    s16 fovOffsetBinang;
    s16 pad1E;
} Oot3dCameraQuakeShakeInfo;

typedef float (*Oot3dCameraQuakeAngleFunc)(s16 angle, void* user);
typedef float (*Oot3dCameraQuakeRandFunc)(void* user);

typedef struct {
    Oot3dCameraQuakeAngleFunc sinS;
    Oot3dCameraQuakeAngleFunc cosS;
    Oot3dCameraQuakeRandFunc randZeroOne;
    void* user;
} Oot3dCameraQuakeMath;

typedef enum {
    OOT3D_CAMERA_QUAKE_OK = 0,
    OOT3D_CAMERA_QUAKE_NULL_REQUEST,
    OOT3D_CAMERA_QUAKE_NULL_SHAKE,
    OOT3D_CAMERA_QUAKE_NULL_RETURN,
    OOT3D_CAMERA_QUAKE_NULL_MATH,
    OOT3D_CAMERA_QUAKE_NULL_MATH_FUNC,
    OOT3D_CAMERA_QUAKE_MISSING_CAMERA_VIEW,
    OOT3D_CAMERA_QUAKE_UNSUPPORTED_CALLBACK,
    OOT3D_CAMERA_QUAKE_INVALID_REQUEST_ID,
} Oot3dCameraQuakeStatus;

void Oot3d_CameraQuakeInitRuntime(Oot3dCameraQuakeRuntime* runtime);
Oot3dCameraQuakeRequest* Oot3d_CameraQuakeGetRequest(Oot3dCameraQuakeRuntime* runtime, s16 requestId);
const Oot3dCameraQuakeRequest* Oot3d_CameraQuakeGetRequestConst(
    const Oot3dCameraQuakeRuntime* runtime,
    s16 requestId
);

Oot3dCameraQuakeStatus Oot3d_CameraQuakeAdd(
    Oot3dCameraQuakeRuntime* runtime,
    u32 cameraRef,
    s16 cameraSlot,
    u8 callbackIndex,
    float randZeroOne,
    s16* outRequestId
);

Oot3dCameraQuakeStatus Oot3d_CameraQuakeRemoveFromIdx(
    Oot3dCameraQuakeRuntime* runtime,
    s16 requestId
);

Oot3dCameraQuakeStatus Oot3d_CameraQuakeSetCountdown(
    Oot3dCameraQuakeRuntime* runtime,
    s16 requestId,
    s16 countdown
);

Oot3dCameraQuakeStatus Oot3d_CameraQuakeSetQuakeValues(
    Oot3dCameraQuakeRuntime* runtime,
    s16 requestId,
    s16 y,
    s16 x,
    s16 fov,
    s16 pitch
);

Oot3dCameraQuakeStatus Oot3d_CameraQuakeSetSpeed(
    Oot3dCameraQuakeRuntime* runtime,
    s16 requestId,
    s16 speed
);

s16 Oot3d_CameraQuakeGetCountdown(const Oot3dCameraQuakeRuntime* runtime, s16 requestId);
void Oot3d_CameraQuakeClearShakeInfo(Oot3dCameraQuakeShakeInfo* shake);

Oot3dCameraQuakeStatus Oot3d_CameraQuakeUpdateShakeInfo(
    const Oot3dCameraQuakeRequest* req,
    const Oot3dCameraQuakeCameraView* cameraView,
    const Oot3dCameraQuakeMath* math,
    float ySample,
    float xSample,
    Oot3dCameraQuakeShakeInfo* shake
);

Oot3dCameraQuakeStatus Oot3d_CameraQuakeRunCallback(
    u8 callbackIndex,
    Oot3dCameraQuakeRequest* req,
    const Oot3dCameraQuakeCameraView* cameraView,
    const Oot3dCameraQuakeMath* math,
    Oot3dCameraQuakeShakeInfo* shake,
    s16* outNativeReturn
);

#endif
