#ifndef OOT3D_SCENE_CUTSCENE_CAMERA_RUNTIME_H
#define OOT3D_SCENE_CUTSCENE_CAMERA_RUNTIME_H

#include "oot3d/scene_cutscene_camera_cmad_apply.h"

enum {
    OOT3D_CUTSCENE_CAMERA_ACTOR_POSE_COPY_SIZE = 0x12,
    OOT3D_CUTSCENE_CAMERA_ATTACH_FLAG_CUTSCENE = 0x0004,
};

typedef struct {
    float worldPosX;
    float worldPosY;
    float worldPosZ;
    s16 shapeRotX;
    s16 shapeRotY;
    s16 shapeRotZ;
} Oot3dCutsceneCameraActorPose;

typedef struct {
    Oot3dCutsceneCameraState csParams;
    u32 field16C;
    u32 field170;
    u16 field174;
    u16 field1A6;
    u32 fieldD8;
    Oot3dCutsceneCameraActorPose fieldDCPose;
    float field120;
    float field128;
    s16 field19E;
    u8 hasActorTrackingFields;
} Oot3dCutsceneCameraRuntimeFrame;

typedef struct {
    float x;
    float y;
    float z;
} Oot3dCutsceneCameraVec3f;

typedef struct {
    float r;
    s16 pitch;
    s16 yaw;
} Oot3dCutsceneCameraVecSphGeo;

typedef struct {
    Oot3dCutsceneCameraVec3f atOffset;
    Oot3dCutsceneCameraVec3f eyeOffset;
    s16 pitchOffset;
    s16 yawOffset;
    s16 fovOffsetBinang;
    float maxMagnitude;
    u8 enabled;
} Oot3dCutsceneCameraViewPerturbation;

typedef struct {
    Oot3dCutsceneCameraVec3f eye;
    Oot3dCutsceneCameraVec3f at;
    Oot3dCutsceneCameraVec3f up;
    Oot3dCutsceneCameraVecSphGeo eyeToAt;
    float fov;
    float viewDistD0;
    float perturbationMagnitude;
    s16 roll;
    u8 hasPerturbation;
} Oot3dCutsceneCameraViewFrame;

typedef enum {
    OOT3D_CUTSCENE_CAMERA_RUNTIME_OK = 0,
    OOT3D_CUTSCENE_CAMERA_RUNTIME_NULL_OUTPUT,
    OOT3D_CUTSCENE_CAMERA_RUNTIME_NULL_STATE,
    OOT3D_CUTSCENE_CAMERA_RUNTIME_NULL_VIEW,
    OOT3D_CUTSCENE_CAMERA_RUNTIME_MISSING_ACTOR_POSE,
    OOT3D_CUTSCENE_CAMERA_RUNTIME_APPLY_FAILED,
} Oot3dCutsceneCameraRuntimeStatus;

Oot3dCutsceneCameraRuntimeStatus Oot3d_CutsceneCameraApplyRuntimeAttachFields(
    Oot3dCutsceneCameraRuntimeFrame* frame,
    u32 csParamsRef,
    u32 actorRef,
    const Oot3dCutsceneCameraActorPose* actorPose,
    u32 modeFlag
);

Oot3dCutsceneCameraRuntimeStatus Oot3d_CutsceneCameraBuildRuntimeFrame(
    u16 cameraBlobSegmentSourceIndex,
    float frameTime,
    u32 csParamsRef,
    u32 actorRef,
    const Oot3dCutsceneCameraActorPose* actorPose,
    u32 modeFlag,
    Oot3dCutsceneCameraRuntimeFrame* outFrame
);

Oot3dCutsceneCameraRuntimeStatus Oot3d_CutsceneCameraProjectView(
    const Oot3dCutsceneCameraState* state,
    const Oot3dCutsceneCameraViewPerturbation* perturbation,
    Oot3dCutsceneCameraViewFrame* outView
);

Oot3dCutsceneCameraRuntimeStatus Oot3d_CutsceneCameraBuildRuntimeView(
    u16 cameraBlobSegmentSourceIndex,
    float frameTime,
    const Oot3dCutsceneCameraViewPerturbation* perturbation,
    Oot3dCutsceneCameraViewFrame* outView
);

#endif
