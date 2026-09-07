/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: hakaana_ouke_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_hakaana_ouke_info_setup_0_commands[] = {
    { 0x00130315u, 0x01000586u },
    { 0x00000404u, 0x00000134u },
    { 0x0000030Eu, 0x00000244u },
    { 0x00000019u, 0x00000002u },
    { 0x00000003u, 0x00005CC0u },
    { 0x00000206u, 0x00005CECu },
    { 0x00000200u, 0x00005CF0u },
    { 0x00000011u, 0x00010000u },
    { 0x00000013u, 0x00005D10u },
    { 0x0000040Fu, 0x00005D14u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dActorEntry oot3d_hakaana_ouke_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { 513, 0, -2 }, { 200, 2226, 0 }, -32767 },
};

static const Oot3dEntranceEntry oot3d_hakaana_ouke_info_setup_0_entrances[] = {
    { 0u, 0 },
    { 1u, 2 },
};

static const Oot3dTransitionActorEntry oot3d_hakaana_ouke_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 1, -1 }, ACTOR_EN_HOLL, { 0, 0, -420 }, 0, 63 },
    { { 1, -1 }, { 3, -1 }, ACTOR_DOOR_SHUTTER, { 0, 120, 1100 }, 0, 65 },
};

static const Oot3dPicaLightSettingsRecord oot3d_hakaana_ouke_info_setup_0_light_settings[] = {
    { { 0x3C, 0x00, 0x81, 0xFB, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0xFF, 0x0D, 0x0B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x2F, 0x45, 0xC8, 0xFC, 0x80, 0x8D } },
    { { 0x99, 0x00, 0x81, 0x20, 0x80, 0xB3, 0xFF, 0x60, 0x04, 0xA0, 0x00, 0x00, 0x80, 0x03, 0x33, 0x80, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0x20, 0x99, 0x80 } },
    { { 0x80, 0x00, 0x51, 0x60, 0x4D, 0x66, 0x99, 0x00, 0x81, 0xF0, 0x1A, 0xCC, 0x66, 0x00, 0x80, 0x5A, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x2F, 0x45, 0xC8, 0x10, 0x99, 0x80 } },
    { { 0x80, 0x00, 0x51, 0x60, 0xC8, 0xC8, 0x95, 0x00, 0xAF, 0xA0, 0x64, 0x77, 0x64, 0x00, 0x80, 0x5A, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x19, 0x13, 0x28, 0x28 } },
};

static const Oot3dExitEntry oot3d_hakaana_ouke_info_setup_0_exits[] = {
    { 1291, 1291u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_hakaana_ouke_info_setup_0_skybox_settings[] = {
    { 0u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_hakaana_ouke_info_setup_0_sound_settings[] = {
    { 3u, 1414u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_hakaana_ouke_info_setup_0_misc_settings[] = {
    { 0u, 0x00000002u },
};

static const Oot3dSceneCommand oot3d_hakaana_ouke_info_setup_1_commands[] = {
    { 0x00130315u, 0x01000586u },
    { 0x00000404u, 0x00005D84u },
    { 0x0000020Eu, 0x00005E94u },
    { 0x00000019u, 0x00000000u },
    { 0x00000003u, 0x00005CC0u },
    { 0x00000106u, 0x00005EB4u },
    { 0x00000100u, 0x00005EB8u },
    { 0x00000011u, 0x00000000u },
    { 0x00000013u, 0x00005EC8u },
    { 0x0000040Fu, 0x00005ECCu },
    { 0x00000017u, 0x00005F3Cu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dActorEntry oot3d_hakaana_ouke_info_setup_1_spawns[] = {
    { ACTOR_EN_HOLL, { 0, 0, -420 }, { 0, 63, 512 }, 0 },
};

static const Oot3dEntranceEntry oot3d_hakaana_ouke_info_setup_1_entrances[] = {
    { 0u, 2 },
};

static const Oot3dTransitionActorEntry oot3d_hakaana_ouke_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 1, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { 0, 60, 943 }, 0, 63 },
};

static const Oot3dPicaLightSettingsRecord oot3d_hakaana_ouke_info_setup_1_light_settings[] = {
    { { 0x28, 0x00, 0xD9, 0xFB, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0xFF, 0x0D, 0x0B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x19, 0x07, 0x46, 0x2D } },
    { { 0x38, 0x48, 0x48, 0x48, 0xB3, 0x9A, 0x89, 0xB8, 0xB8, 0xB8, 0x13, 0x13, 0x3B, 0x1D, 0x0A, 0x0A, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1C, 0x07, 0x69, 0x59 } },
    { { 0x59, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xEF, 0xB8, 0xB8, 0xB8, 0x31, 0x31, 0x59, 0x64, 0x64, 0x77, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1A, 0x07, 0x77, 0x59 } },
    { { 0x00, 0x48, 0x48, 0x48, 0xF9, 0x87, 0x31, 0xB8, 0xB8, 0xB8, 0x1D, 0x1D, 0x3B, 0x1C, 0x13, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x19, 0x07, 0x28, 0x28 } },
};

static const Oot3dExitEntry oot3d_hakaana_ouke_info_setup_1_exits[] = {
    { 1291, 1291u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_hakaana_ouke_info_setup_1_skybox_settings[] = {
    { 0u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_hakaana_ouke_info_setup_1_sound_settings[] = {
    { 3u, 1414u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_hakaana_ouke_info_setup_1_cutscenes[] = {
    { 0x00005F3Cu, 1u },
};

static const Oot3dMiscSettings oot3d_hakaana_ouke_info_setup_1_misc_settings[] = {
    { 0u, 0x00000000u },
};

static const Oot3dSceneCommand oot3d_hakaana_ouke_info_setup_2_commands[] = {
    { 0x00130315u, 0x01000586u },
    { 0x00000404u, 0x00006C0Cu },
    { 0x0000020Eu, 0x00006D1Cu },
    { 0x00000019u, 0x00000000u },
    { 0x00000003u, 0x00005CC0u },
    { 0x00000106u, 0x00006D3Cu },
    { 0x00000100u, 0x00006D40u },
    { 0x00000011u, 0x00000000u },
    { 0x00000013u, 0x00006D50u },
    { 0x0000040Fu, 0x00006D54u },
    { 0x00000017u, 0x00006DC4u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dActorEntry oot3d_hakaana_ouke_info_setup_2_spawns[] = {
    { ACTOR_EN_HOLL, { 0, 0, -420 }, { 0, 63, 512 }, 0 },
};

static const Oot3dEntranceEntry oot3d_hakaana_ouke_info_setup_2_entrances[] = {
    { 0u, 2 },
};

static const Oot3dTransitionActorEntry oot3d_hakaana_ouke_info_setup_2_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 1, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { 0, 60, 943 }, 0, 63 },
};

static const Oot3dPicaLightSettingsRecord oot3d_hakaana_ouke_info_setup_2_light_settings[] = {
    { { 0x28, 0x00, 0xD9, 0xFB, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0xFF, 0x0D, 0x0B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x19, 0x07, 0x46, 0x2D } },
    { { 0x38, 0x48, 0x48, 0x48, 0xB3, 0x9A, 0x89, 0xB8, 0xB8, 0xB8, 0x13, 0x13, 0x3B, 0x1D, 0x0A, 0x0A, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1C, 0x07, 0x69, 0x59 } },
    { { 0x59, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xEF, 0xB8, 0xB8, 0xB8, 0x31, 0x31, 0x59, 0x64, 0x64, 0x77, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1A, 0x07, 0x77, 0x59 } },
    { { 0x00, 0x48, 0x48, 0x48, 0xF9, 0x87, 0x31, 0xB8, 0xB8, 0xB8, 0x1D, 0x1D, 0x3B, 0x1C, 0x13, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x19, 0x07, 0x28, 0x28 } },
};

static const Oot3dExitEntry oot3d_hakaana_ouke_info_setup_2_exits[] = {
    { 1291, 1291u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_hakaana_ouke_info_setup_2_skybox_settings[] = {
    { 0u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_hakaana_ouke_info_setup_2_sound_settings[] = {
    { 3u, 1414u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_hakaana_ouke_info_setup_2_cutscenes[] = {
    { 0x00006DC4u, 1u },
};

static const Oot3dMiscSettings oot3d_hakaana_ouke_info_setup_2_misc_settings[] = {
    { 0u, 0x00000000u },
};

static const Oot3dActorEntry oot3d_hakaana_ouke_info_hakaana_ouke_1_info_actors[] = {
    { ACTOR_EN_RD, { 130, 0, 177 }, { 0, -16384, 0 }, 32512 },
    { ACTOR_EN_RD, { 215, 0, 446 }, { 0, 0, 0 }, 32512 },
    { ACTOR_EN_RD, { -196, 0, 428 }, { 0, 16384, 0 }, 32512 },
};

static const Oot3dRoomObjectEntry oot3d_hakaana_ouke_info_hakaana_ouke_1_info_objects[] = {
    { OBJECT_HAKACH_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SYOKUDAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OUKE_HAKA, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_hakaana_ouke_info_hakaana_ouke_2_info_actors[] = {
    { ACTOR_UNSET_7F, { -1200, 0, 0 }, { 0, 6143, 94 }, 60 },
    { ACTOR_EN_HORSE_NORMAL, { -1112, 0, 0 }, { 0, 9216, 94 }, -60 },
    { ACTOR_EN_HORSE_NORMAL, { -1112, 0, 0 }, { 0, 9216, 43 }, 152 },
};

static const Oot3dRoomObjectEntry oot3d_hakaana_ouke_info_hakaana_ouke_2_info_objects[] = {
    { OBJECT_HAKACH_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SYOKUDAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OUKE_HAKA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_JEWEL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_COIN, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_hakaana_ouke_info_hakaana_ouke_3_info_actors[] = {
    { ACTOR_EN_FIREFLY, { -280, 112, 1445 }, { 0, 16383, 0 }, 3 },
    { ACTOR_EN_FIREFLY, { 281, 81, 1496 }, { 0, -16383, 0 }, 3 },
    { ACTOR_EN_WONDER_TALK2, { 286, 40, 1430 }, { 0, -16384, 0 }, 1784 },
    { ACTOR_EN_FIREFLY, { 54, 73, 1253 }, { 0, 0, 0 }, 3 },
    { ACTOR_EN_FIREFLY, { -63, 27, 1256 }, { 0, 0, 0 }, 3 },
    { ACTOR_OBJ_SYOKUDAI, { -56, 120, 1208 }, { 0, 0, 0 }, 4256 },
    { ACTOR_OBJ_SYOKUDAI, { 55, 120, 1206 }, { 0, 0, 0 }, 4256 },
    { ACTOR_EN_BOX, { 1, 244, 1473 }, { 0, 0, 32 }, -32736 },
    { ACTOR_SHOT_SUN, { -210, 5, 1476 }, { 0, 0, 0 }, -192 },
    { ACTOR_EN_WONDER_TALK2, { -247, 20, 1446 }, { 0, 8920, 0 }, 1721 },
};

static const Oot3dRoomObjectEntry oot3d_hakaana_ouke_info_hakaana_ouke_3_info_objects[] = {
    { OBJECT_HAKACH_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SYOKUDAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OUKE_HAKA, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_hakaana_ouke_info_room_refs[] = {
    { "hakaana_ouke_0_info.zsi", 0 },
    { "hakaana_ouke_1_info.zsi", 1 },
    { "hakaana_ouke_2_info.zsi", 2 },
    { "hakaana_ouke_3_info.zsi", 3 },
};

static const Oot3dSceneSetupIndex oot3d_hakaana_ouke_info_setups[] = {
    { 0u, oot3d_hakaana_ouke_info_setup_0_commands, 11u, NULL, 0u, NULL, 0u, NULL, 0u, oot3d_hakaana_ouke_info_setup_0_spawns, 1u, oot3d_hakaana_ouke_info_setup_0_entrances, 2u, oot3d_hakaana_ouke_info_setup_0_transition_actors, 3u, oot3d_hakaana_ouke_info_setup_0_light_settings, 4u, oot3d_hakaana_ouke_info_setup_0_exits, 2u, oot3d_hakaana_ouke_info_setup_0_skybox_settings, 1u, oot3d_hakaana_ouke_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_hakaana_ouke_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_hakaana_ouke_info_setup_1_commands, 12u, NULL, 0u, NULL, 0u, NULL, 0u, oot3d_hakaana_ouke_info_setup_1_spawns, 1u, oot3d_hakaana_ouke_info_setup_1_entrances, 1u, oot3d_hakaana_ouke_info_setup_1_transition_actors, 2u, oot3d_hakaana_ouke_info_setup_1_light_settings, 4u, oot3d_hakaana_ouke_info_setup_1_exits, 2u, oot3d_hakaana_ouke_info_setup_1_skybox_settings, 1u, oot3d_hakaana_ouke_info_setup_1_sound_settings, 1u, oot3d_hakaana_ouke_info_setup_1_cutscenes, 1u, oot3d_hakaana_ouke_info_setup_1_misc_settings, 1u },
    { 2u, oot3d_hakaana_ouke_info_setup_2_commands, 12u, NULL, 0u, NULL, 0u, NULL, 0u, oot3d_hakaana_ouke_info_setup_2_spawns, 1u, oot3d_hakaana_ouke_info_setup_2_entrances, 1u, oot3d_hakaana_ouke_info_setup_2_transition_actors, 2u, oot3d_hakaana_ouke_info_setup_2_light_settings, 4u, oot3d_hakaana_ouke_info_setup_2_exits, 2u, oot3d_hakaana_ouke_info_setup_2_skybox_settings, 1u, oot3d_hakaana_ouke_info_setup_2_sound_settings, 1u, oot3d_hakaana_ouke_info_setup_2_cutscenes, 1u, oot3d_hakaana_ouke_info_setup_2_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_hakaana_ouke_info_rooms[] = {
    { "hakaana_ouke_0_info.zsi", 0, NULL, 0u, NULL, 0u },
    { "hakaana_ouke_1_info.zsi", 1, oot3d_hakaana_ouke_info_hakaana_ouke_1_info_objects, 7u, oot3d_hakaana_ouke_info_hakaana_ouke_1_info_actors, 3u },
    { "hakaana_ouke_2_info.zsi", 2, oot3d_hakaana_ouke_info_hakaana_ouke_2_info_objects, 9u, oot3d_hakaana_ouke_info_hakaana_ouke_2_info_actors, 3u },
    { "hakaana_ouke_3_info.zsi", 3, oot3d_hakaana_ouke_info_hakaana_ouke_3_info_objects, 7u, oot3d_hakaana_ouke_info_hakaana_ouke_3_info_actors, 10u },
};

const Oot3dSceneIndex oot3d_scene_index_hakaana_ouke_info = {
    "hakaana_ouke_info.zsi",
    oot3d_hakaana_ouke_info_room_refs, 4u,
    oot3d_hakaana_ouke_info_rooms, 4u,
    oot3d_hakaana_ouke_info_setups, 3u,
};
