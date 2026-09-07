/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: shop_face_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_shop_face_info_shop_face_0_info_actors[] = {
    { ACTOR_EN_OSSAN, { 0, 1, -68 }, { 0, 0, 0 }, 10 },
    { ACTOR_EN_WONDER_TALK2, { -112, 30, 31 }, { 0, 3641, 42 }, -32257 },
    { ACTOR_EN_GUEST, { 83, 0, 66 }, { 0, -23665, 0 }, -1 },
    { ACTOR_EN_TANA, { 0, 0, -40 }, { 0, 0, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_shop_face_info_shop_face_0_info_objects[] = {
    { OBJECT_OS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOP_DUNGEN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_KI_TAN_MASK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_REDEAD_MASK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SKJ_MASK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_RABIT_MASK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_TRUTH_MASK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_GOLONMASK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_ZORAMASK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_GERUDOMASK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DOG, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_shop_face_info_room_refs[] = {
    { "shop_face_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_shop_face_info_rooms[] = {
    { "shop_face_0_info.zsi", 0, oot3d_shop_face_info_shop_face_0_info_objects, 13u, oot3d_shop_face_info_shop_face_0_info_actors, 4u },
};

const Oot3dSceneIndex oot3d_scene_index_shop_face_info = {
    "shop_face_info.zsi",
    oot3d_shop_face_info_room_refs, 1u,
    oot3d_shop_face_info_rooms, 1u,
    NULL, 0u,
};
