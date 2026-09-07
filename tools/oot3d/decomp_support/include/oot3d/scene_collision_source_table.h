#ifndef OOT3D_SCENE_COLLISION_SOURCE_TABLE_H
#define OOT3D_SCENE_COLLISION_SOURCE_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_COLLISION_HEADER_SOURCE_ROW_COUNT = 35,
    OOT3D_SCENE_COLLISION_COMMAND_REF_ROW_COUNT = 175,
    OOT3D_SCENE_COLLISION_WATER_SOURCE_ROW_COUNT = 83,
    OOT3D_SCENE_COLLISION_BGCAM_SOURCE_ROW_COUNT = 187,
    OOT3D_SCENE_COLLISION_SURFACE_TYPE_SOURCE_ROW_COUNT = 1000,
    OOT3D_SCENE_COLLISION_POLYGON_TYPE_USAGE_ROW_COUNT = 1000,
    OOT3D_SCENE_COLLISION_SOURCE_UNMAPPED_INDEX = 0xFFFF,
    OOT3D_SCENE_COLLISION_SOURCE_NO_OFFSET = 0xFFFFFFFF,
};

typedef enum {
    OOT3D_COLLISION_COMMAND_REF_RESOLVED,
    OOT3D_COLLISION_COMMAND_REF_ZERO_STUB_UNPROMOTED,
    OOT3D_COLLISION_COMMAND_REF_MISSING_CANDIDATE,
} Oot3dCollisionCommandRefStatus;

typedef struct {
    u16 collisionCommandRefIndex;
    u16 collisionSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 setupIndex;
    u16 commandIndex;
    u32 commandOffset;
    u32 commandArgument;
    Oot3dCollisionCommandRefStatus status;
    const char* scenePath;
    const char* sourceBasename;
    const char* setupRole;
    const char* validationStatus;
    const char* openQuestions;
    const char* handlerName;
    u32 handlerEntry;
} Oot3dSceneCollisionCommandRefRow;

typedef struct {
    u16 waterSourceIndex;
    u16 collisionSourceIndex;
    u16 waterIndex;
    s16 xMin;
    s16 ySurface;
    s16 zMin;
    s16 xLength;
    s16 zLength;
    u32 properties;
    u8 plausible;
} Oot3dSceneCollisionWaterBoxSourceRow;

typedef struct {
    u16 bgcamSourceIndex;
    u16 collisionSourceIndex;
    u16 bgcamIndex;
    u16 setting;
    u16 count;
    u32 dataOffset;
} Oot3dSceneCollisionBgCamSourceRow;

typedef struct {
    u16 surfaceTypeSourceIndex;
    u16 collisionSourceIndex;
    u16 surfaceTypeIndex;
    u32 offset;
    u32 data1;
    u32 data2;
    u16 referencedPolygonCount;
} Oot3dSceneCollisionSurfaceTypeSourceRow;

typedef struct {
    u16 polygonUsageSourceIndex;
    u16 collisionSourceIndex;
    u16 usageIndex;
    u16 surfaceType;
    u16 polygonCount;
} Oot3dSceneCollisionPolygonTypeUsageRow;

typedef struct {
    u16 collisionSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 setupIndex;
    u16 commandIndex;
    u32 commandOffset;
    u32 commandArgument;
    u32 headerOffset;
    Oot3dVec3s boundsMin;
    Oot3dVec3s boundsMax;
    u16 vertexCount;
    u16 rawPolygonCount;
    u16 effectivePolygonCount;
    u16 surfaceTypeCount;
    u16 bgcamCount;
    u16 waterBoxCount;
    u32 vertexOffset;
    u32 effectiveVertexOffset;
    u32 polygonOffset;
    u32 effectivePolygonOffset;
    u32 surfaceTypeOffset;
    u32 effectiveSurfaceTypeOffset;
    u32 bgcamOffset;
    u32 effectiveBgcamOffset;
    u32 cameraPositionOffset;
    s16 cameraPointerAdjustment;
    u32 waterBoxesOffset;
    u16 polygonMarker;
    u16 commandRefStart;
    u16 commandRefCount;
    u16 waterRefStart;
    u16 waterRefCount;
    u16 bgcamRefStart;
    u16 bgcamRefCount;
    u16 surfaceTypeRefStart;
    u16 surfaceTypeRefCount;
    u16 polygonUsageRefStart;
    u16 polygonUsageRefCount;
    const char* scenePath;
    const char* sourceBasename;
    const char* setupRole;
    const char* validationStatus;
    const char* evidenceText;
    const char* openQuestions;
    const char* handlerName;
    u32 handlerEntry;
} Oot3dSceneCollisionHeaderSourceRow;

extern const Oot3dSceneCollisionCommandRefRow oot3d_scene_collision_command_ref_rows[];
extern const Oot3dSceneCollisionWaterBoxSourceRow oot3d_scene_collision_water_source_rows[];
extern const Oot3dSceneCollisionBgCamSourceRow oot3d_scene_collision_bgcam_source_rows[];
extern const Oot3dSceneCollisionSurfaceTypeSourceRow oot3d_scene_collision_surface_type_source_rows[];
extern const Oot3dSceneCollisionPolygonTypeUsageRow oot3d_scene_collision_polygon_type_usage_rows[];
extern const Oot3dSceneCollisionHeaderSourceRow oot3d_scene_collision_header_source_rows[];
extern const u32 oot3d_scene_collision_command_ref_row_count;
extern const u32 oot3d_scene_collision_water_source_row_count;
extern const u32 oot3d_scene_collision_bgcam_source_row_count;
extern const u32 oot3d_scene_collision_surface_type_source_row_count;
extern const u32 oot3d_scene_collision_polygon_type_usage_row_count;
extern const u32 oot3d_scene_collision_header_source_row_count;

#endif
