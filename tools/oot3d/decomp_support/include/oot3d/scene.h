#ifndef OOT3D_SCENE_H
#define OOT3D_SCENE_H

#include "oot3d/types.h"

enum {
    OOT3D_ACTOR_ENTRY_SIZE = 0x10,
    OOT3D_PATH_RECORD_SIZE = 0x08,
    OOT3D_LIGHT_SETTINGS_RECORD_SIZE = 0x1C,
};

typedef struct {
    s16 x;
    s16 y;
    s16 z;
} Oot3dVec3s;

typedef struct {
    s16 id;
    Oot3dVec3s pos;
    Oot3dVec3s rot;
    s16 params;
} Oot3dActorEntry;

typedef enum {
    OOT3D_ROOM_OBJECT_KNOWN_ID,
    OOT3D_ROOM_OBJECT_UNKNOWN_OOT3D_ID,
} Oot3dRoomObjectSemanticStatus;

typedef struct {
    s16 id;
    Oot3dRoomObjectSemanticStatus semanticStatus;
} Oot3dRoomObjectEntry;

typedef struct {
    s8 room;
    s8 effect;
} Oot3dTransitionSide;

typedef struct {
    Oot3dTransitionSide front;
    Oot3dTransitionSide back;
    s16 id;
    Oot3dVec3s pos;
    s16 rotY;
    s16 params;
} Oot3dTransitionActorEntry;

typedef struct {
    u8 spawn;
    s8 room;
} Oot3dEntranceEntry;

typedef enum {
    OOT3D_EXIT_DIRECT_TRANSITION_VALUE,
    OOT3D_EXIT_HIGH_REMAP_0X7FF9_TO_0X7FFE,
    OOT3D_EXIT_SPECIAL_0X7FFF,
    OOT3D_EXIT_UNHANDLED_SIGNED_VALUE,
} Oot3dExitCategory;

typedef struct {
    u8 raw[OOT3D_LIGHT_SETTINGS_RECORD_SIZE];
} Oot3dPicaLightSettingsRecord;

typedef struct {
    u8 cUpElfMessageFile;
    s16 keepObjectId;
} Oot3dSpecialFiles;

typedef enum {
    OOT3D_PATH_POINTS_DECODED_VEC3S,
    OOT3D_PATH_POINTS_EMPTY,
    OOT3D_PATH_POINTS_INVALID_OFFSET,
} Oot3dPathPointsStatus;

typedef struct {
    u8 pointCount;
    u8 unk_01;
    u16 unk_02;
    u32 rawPointsOffset;
    Oot3dPathPointsStatus pointsStatus;
    const Oot3dVec3s* points;
} Oot3dPathRecord;

typedef struct {
    s16 value;
    u16 rawU16;
    Oot3dExitCategory category;
} Oot3dExitEntry;

typedef struct {
    u8 skyboxId;
    u8 weatherOrUnk05;
    u8 indoors;
} Oot3dSkyboxSettings;

typedef struct {
    u8 specId;
    u8 natureAmbienceId;
    u8 data3;
    u32 bgmSoundId;
} Oot3dSoundSettings;

typedef struct {
    u32 offset;
    u8 inFile;
} Oot3dCutsceneReference;

typedef struct {
    u8 cameraOrWorldMapArea;
    u32 rawArgument;
} Oot3dMiscSettings;

typedef struct {
    u32 word;
    u32 argument;
} Oot3dSceneCommand;

typedef struct {
    const char* path;
    s32 roomIndex;
} Oot3dRoomReference;

typedef struct {
    const char* roomPath;
    s32 roomIndex;
    const Oot3dRoomObjectEntry* objects;
    u32 objectCount;
    const Oot3dActorEntry* actors;
    u32 actorCount;
} Oot3dRoomIndex;

typedef struct {
    u32 setupIndex;
    const Oot3dSceneCommand* commands;
    u32 commandCount;
    const Oot3dSpecialFiles* specialFiles;
    u32 specialFileCount;
    const Oot3dPathRecord* paths;
    u32 pathCount;
    const Oot3dActorEntry* standardActors;
    u32 standardActorCount;
    const Oot3dActorEntry* spawns;
    u32 spawnCount;
    const Oot3dEntranceEntry* entrances;
    u32 entranceCount;
    const Oot3dTransitionActorEntry* transitions;
    u32 transitionCount;
    const Oot3dPicaLightSettingsRecord* lightSettings;
    u32 lightSettingsCount;
    const Oot3dExitEntry* exits;
    u32 exitCount;
    const Oot3dSkyboxSettings* skyboxSettings;
    u32 skyboxSettingsCount;
    const Oot3dSoundSettings* soundSettings;
    u32 soundSettingsCount;
    const Oot3dCutsceneReference* cutscenes;
    u32 cutsceneCount;
    const Oot3dMiscSettings* miscSettings;
    u32 miscSettingsCount;
} Oot3dSceneSetupIndex;

typedef struct {
    const char* scenePath;
    const Oot3dRoomReference* roomRefs;
    u32 roomRefCount;
    const Oot3dRoomIndex* rooms;
    u32 roomCount;
    const Oot3dSceneSetupIndex* setups;
    u32 setupCount;
} Oot3dSceneIndex;

typedef struct {
    const char* sceneStem;
    const char* scenePath;
    const char* sourceBasename;
    const Oot3dSceneIndex* index;
} Oot3dSceneIndexRegistryEntry;

#endif
