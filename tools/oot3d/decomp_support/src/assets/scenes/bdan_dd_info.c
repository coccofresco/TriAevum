/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: bdan_dd_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_bdan_dd_info_setup_0_commands[] = {
    { 0x00130315u, 0x01000594u },
    { 0x00001004u, 0x000000E0u },
    { 0x0000160Eu, 0x00000520u },
    { 0x00000019u, 0x00000000u },
    { 0x00000003u, 0x00019A5Cu },
    { 0x00000206u, 0x00019A88u },
    { 0x00000207u, 0x00000003u },
    { 0x00000200u, 0x00019A8Cu },
    { 0x00000011u, 0x00010001u },
    { 0x00000013u, 0x00019AACu },
    { 0x0000040Fu, 0x00019AB0u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_bdan_dd_info_setup_0_special_files[] = {
    { 2u, OBJECT_GAMEPLAY_DANGEON_KEEP },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { 3585, 0, -18 }, { -235, 323, 0 }, -32767 },
};

static const Oot3dEntranceEntry oot3d_bdan_dd_info_setup_0_entrances[] = {
    { 0u, 0 },
    { 1u, 14 },
};

static const Oot3dTransitionActorEntry oot3d_bdan_dd_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 15, -1 }, { 5, -1 }, ACTOR_DOOR_SHUTTER, { 1360, -240, -2119 }, 0, 148 },
    { { 1, -1 }, { 0, -1 }, ACTOR_DOOR_SHUTTER, { 0, -240, -1127 }, 0, 149 },
    { { 2, -1 }, { 1, -1 }, ACTOR_DOOR_SHUTTER, { 0, -260, -2443 }, 0, 153 },
    { { 4, -1 }, { 6, -1 }, ACTOR_DOOR_SHUTTER, { -1360, 160, -2284 }, -32767, 160 },
    { { 7, -1 }, { 2, -1 }, ACTOR_DOOR_SHUTTER, { 0, -260, -3983 }, 0, 147 },
    { { 7, -1 }, { 12, -1 }, ACTOR_DOOR_SHUTTER, { -660, -260, -5183 }, -32767, 63 },
    { { 8, -1 }, { 7, -1 }, ACTOR_DOOR_SHUTTER, { 0, -260, -5303 }, 0, 141 },
    { { 7, -1 }, { 11, -1 }, ACTOR_DOOR_SHUTTER, { 660, -260, -5183 }, -32767, 129 },
    { { 1, -1 }, { 4, -1 }, ACTOR_DOOR_SHUTTER, { -620, 160, -1703 }, -16384, 146 },
    { { 5, -1 }, { 1, -1 }, ACTOR_DOOR_SHUTTER, { 620, -240, -1703 }, -16384, 63 },
    { { 10, -1 }, { 7, -1 }, ACTOR_DOOR_SHUTTER, { -1060, -260, -4683 }, 16384, 63 },
    { { 9, -1 }, { 7, -1 }, ACTOR_DOOR_SHUTTER, { 1060, -260, -4683 }, -16384, 63 },
    { { 14, -1 }, { 3, -1 }, ACTOR_DOOR_SHUTTER, { 460, -1033, -3223 }, -16384, 188 },
    { { 3, -1 }, { 13, -1 }, ACTOR_DOOR_SHUTTER, { -581, -1033, -2742 }, -8920, 187 },
    { { 14, -1 }, { 1, -1 }, ACTOR_DOOR_SHUTTER, { 313, -1153, -1703 }, -16384, 63 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -40, -638, -3083 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { 40, -638, -3323 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -260, -638, -3403 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -300, -638, -2863 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { 220, -638, -3203 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -60, -638, -3543 }, 16384, 191 },
};

static const Oot3dPicaLightSettingsRecord oot3d_bdan_dd_info_setup_0_light_settings[] = {
    { { 0xC0, 0xFE, 0x7E, 0xF6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x0D, 0x21, 0x02, 0x01, 0x03, 0x00, 0x00, 0xFA, 0x45, 0x00, 0x00, 0x48, 0x45, 0x28, 0x04, 0x77, 0x68 } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xFF, 0xA0, 0x3D, 0xB8, 0xB8, 0xB8, 0xFF, 0x33, 0x00, 0x8C, 0x8C, 0x00, 0x00, 0x60, 0x2E, 0x45, 0x00, 0x60, 0x2E, 0x45, 0xF2, 0x06, 0x13, 0x13 } },
    { { 0x13, 0x48, 0x48, 0x48, 0x72, 0x72, 0x64, 0x00, 0x81, 0x00, 0x1D, 0x13, 0x13, 0x00, 0x1D, 0x2D, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1A, 0x07, 0x77, 0x59 } },
    { { 0x00, 0x48, 0x48, 0x48, 0xF9, 0x87, 0x31, 0xB8, 0xB8, 0xB8, 0x1D, 0x1D, 0x3B, 0x1C, 0x13, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x19, 0x07, 0x28, 0x28 } },
};

static const Oot3dExitEntry oot3d_bdan_dd_info_setup_0_exits[] = {
    { 545, 545u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 769, 769u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_bdan_dd_info_setup_0_skybox_settings[] = {
    { 1u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_bdan_dd_info_setup_0_sound_settings[] = {
    { 3u, 1428u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_bdan_dd_info_setup_0_misc_settings[] = {
    { 0u, 0x00000000u },
};

static const Oot3dSceneCommand oot3d_bdan_dd_info_setup_1_commands[] = {
    { 0x00000315u, 0x01000594u },
    { 0x00001004u, 0x00019B20u },
    { 0x0000160Eu, 0x00019F60u },
    { 0x00000019u, 0x00000000u },
    { 0x00000003u, 0x00019A5Cu },
    { 0x00000206u, 0x0001A0C0u },
    { 0x00000007u, 0x00000003u },
    { 0x00000200u, 0x0001A0C4u },
    { 0x00000011u, 0x00010000u },
    { 0x00000013u, 0x0001A0E4u },
    { 0x0000040Fu, 0x0001A0E8u },
    { 0x00000017u, 0x0001A158u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_bdan_dd_info_setup_1_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_DANGEON_KEEP },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_setup_1_spawns[] = {
    { ACTOR_DOOR_SHUTTER, { -1001, -935, -3343 }, { -16384, 160, 1536 }, 1281 },
    { ACTOR_PLAYER, { -1196, -1025, -3506 }, { 0, -32767, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_bdan_dd_info_setup_1_entrances[] = {
    { 0u, 6 },
    { 1u, 5 },
};

static const Oot3dTransitionActorEntry oot3d_bdan_dd_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 1, -1 }, { 0, -1 }, ACTOR_DOOR_SHUTTER, { 0, -240, -1127 }, 0, 63 },
    { { 2, -1 }, { 1, -1 }, ACTOR_DOOR_SHUTTER, { 0, -260, -2443 }, 0, 63 },
    { { 6, -1 }, { 4, -1 }, ACTOR_DOOR_SHUTTER, { -1360, 160, -2284 }, 0, 63 },
    { { 7, -1 }, { 2, -1 }, ACTOR_DOOR_SHUTTER, { 0, -260, -3983 }, 0, 63 },
    { { 12, -1 }, { 7, -1 }, ACTOR_DOOR_SHUTTER, { -660, -260, -5183 }, 0, 63 },
    { { 8, -1 }, { 7, -1 }, ACTOR_DOOR_SHUTTER, { 0, -260, -5303 }, 0, 63 },
    { { 11, -1 }, { 7, -1 }, ACTOR_DOOR_SHUTTER, { 660, -260, -5183 }, 0, 63 },
    { { 4, -1 }, { 1, -1 }, ACTOR_DOOR_SHUTTER, { -620, 160, -1703 }, 16384, 63 },
    { { 1, -1 }, { 5, -1 }, ACTOR_DOOR_SHUTTER, { 620, -240, -1703 }, 16384, 63 },
    { { 10, -1 }, { 7, -1 }, ACTOR_DOOR_SHUTTER, { -1060, -260, -4683 }, 16384, 63 },
    { { 7, -1 }, { 9, -1 }, ACTOR_DOOR_SHUTTER, { 1060, -260, -4683 }, 16384, 63 },
    { { 14, -1 }, { 3, -1 }, ACTOR_DOOR_SHUTTER, { 460, -1033, -3223 }, -16384, 63 },
    { { 3, -1 }, { 15, -1 }, ACTOR_DOOR_SHUTTER, { -376, -1033, -2815 }, -8920, 63 },
    { { 1, -1 }, { 14, -1 }, ACTOR_DOOR_SHUTTER, { 313, -1153, -1703 }, 16384, 63 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -40, -638, -3083 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { 40, -638, -3323 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -260, -638, -3403 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -220, -638, -2963 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { 220, -638, -3203 }, 16384, 191 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -60, -638, -3543 }, 16384, 191 },
    { { -1, -1 }, { 5, -1 }, ACTOR_DOOR_SHUTTER, { 1360, -240, -2514 }, 0, 191 },
};

static const Oot3dPicaLightSettingsRecord oot3d_bdan_dd_info_setup_1_light_settings[] = {
    { { 0xC0, 0xFE, 0x7E, 0xF6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x0F, 0x21, 0x02, 0x01, 0x03, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1C, 0x07, 0x6E, 0x3B } },
    { { 0x28, 0x48, 0x48, 0x48, 0xD6, 0x72, 0x46, 0x00, 0x81, 0x00, 0x1D, 0x13, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1C, 0x07, 0x69, 0x59 } },
    { { 0x59, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xEF, 0xB8, 0xB8, 0xB8, 0x31, 0x31, 0x59, 0x64, 0x64, 0x77, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1A, 0x07, 0x77, 0x59 } },
    { { 0x00, 0x48, 0x48, 0x48, 0xF9, 0x87, 0x31, 0xB8, 0xB8, 0xB8, 0x1D, 0x1D, 0x3B, 0x1C, 0x13, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x19, 0x07, 0x28, 0x28 } },
};

static const Oot3dExitEntry oot3d_bdan_dd_info_setup_1_exits[] = {
    { 545, 545u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 769, 769u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_bdan_dd_info_setup_1_skybox_settings[] = {
    { 0u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_bdan_dd_info_setup_1_sound_settings[] = {
    { 3u, 1428u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_bdan_dd_info_setup_1_cutscenes[] = {
    { 0x0001A158u, 1u },
};

static const Oot3dMiscSettings oot3d_bdan_dd_info_setup_1_misc_settings[] = {
    { 0u, 0x00000000u },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_0_dd_info_actors[] = {
    { ACTOR_EN_OKUTA, { -2, -330, -224 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_KUSA, { 213, -330, -398 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { -219, -340, -61 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_COW, { 232, -211, -573 }, { 0, -6554, 0 }, 0 },
    { ACTOR_EN_COW, { -224, -211, -581 }, { 0, 6554, 0 }, 0 },
    { ACTOR_BG_BDAN_SWITCH, { -2, -341, -305 }, { 0, 0, 0 }, 15618 },
    { ACTOR_OBJ_BOMBIWA, { -1, -340, -296 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_BUBBLE, { -219, -240, -408 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -265, -99, -359 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -251, -190, -267 }, { 0, 0, 0 }, -1 },
    { ACTOR_ELF_MSG2, { 197, -182, -527 }, { 0, 25, 1 }, 16128 },
    { ACTOR_ELF_MSG2, { -203, -182, -537 }, { 0, 22, 1 }, 16128 },
    { ACTOR_EN_BOX, { 200, -240, -240 }, { 0, 16384, 24 }, -32699 },
    { ACTOR_EN_WONDER_ITEM, { -201, -188, -538 }, { 0, 0, 4 }, 6421 },
    { ACTOR_EN_WONDER_ITEM, { 199, -188, -524 }, { 0, 0, 4 }, 6424 },
    { ACTOR_OBJ_TSUBO, { -183, -330, -449 }, { 0, 0, 0 }, 30724 },
    { ACTOR_OBJ_TSUBO, { 202, -340, -62 }, { 0, 0, 0 }, 25612 },
    { ACTOR_EN_BOX, { 0, -340, -111 }, { 0, 0, 61 }, -18397 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_0_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SIOFUKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBF, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_0_info_actors[] = {
    { ACTOR_EN_OKUTA, { -133, -330, -257 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_OKUTA, { 110, -330, -260 }, { 0, 0, 0 }, -256 },
    { ACTOR_BG_BDAN_SWITCH, { -4, -112, -729 }, { 0, 0, -32767 }, 15108 },
    { ACTOR_EN_BUBBLE, { 17, -241, -993 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -28, -257, -900 }, { 0, 0, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_0_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_1_dd_info_actors[] = {
    { ACTOR_EN_ITEM00, { -390, -1553, -1695 }, { 0, 0, 0 }, 256 },
    { ACTOR_EN_ITEM00, { -487, -1490, -1701 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_ITEM00, { -549, -1408, -1708 }, { 0, 0, 0 }, 768 },
    { ACTOR_EN_BILI, { 146, -708, -1788 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BILI, { -134, -72, -1613 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BROB, { -172, -340, -1553 }, { 0, 9101, 0 }, 511 },
    { ACTOR_EN_BROB, { -233, -330, -1706 }, { 0, 0, 0 }, 511 },
    { ACTOR_EN_BROB, { -177, -340, -1858 }, { 0, -6918, 0 }, 511 },
    { ACTOR_EN_ITEM00, { -217, -326, -1620 }, { 0, 0, 0 }, 9987 },
    { ACTOR_EN_ITEM00, { -206, -334, -1796 }, { 0, 0, 0 }, 10243 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_1_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BROB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MIZU_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SIOFUKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_1_info_actors[] = {
    { ACTOR_EN_OKUTA, { -118, -1248, -1700 }, { 0, 15291, 0 }, -256 },
    { ACTOR_EN_BILI, { -4, -212, -1481 }, { 0, 0, 0 }, -1 },
    { ACTOR_BG_BDAN_OBJECTS, { 0, -771, -1703 }, { 0, 0, 0 }, -255 },
    { ACTOR_BG_BDAN_SWITCH, { 477, -321, -1703 }, { 0, 0, 0 }, 14848 },
    { ACTOR_BG_BDAN_OBJECTS, { 120, 80, -1583 }, { 0, 0, 0 }, 7427 },
    { ACTOR_EN_SHOPNUTS, { -737, -1233, -1701 }, { 0, 16384, 0 }, 0 },
    { ACTOR_OBJ_KIBAKO, { -189, -340, -1925 }, { 0, 4368, 0 }, -1 },
    { ACTOR_OBJ_KIBAKO, { -141, -340, -1945 }, { 0, 4368, 0 }, -253 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_1_info_objects[] = {
    { OBJECT_UNSET_5, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOPNUTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_2_dd_info_actors[] = {
    { ACTOR_EN_KUSA, { -192, -340, -3209 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { 350, -340, -3534 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_BILI, { -61, -160, -3267 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BILI, { -261, -187, -3066 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BROB, { 0, -400, -3715 }, { 0, 0, 0 }, 255 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_2_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SIOFUKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BROB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_3_dd_info_actors[] = {
    { ACTOR_EN_KUSA, { 305, -1206, -3481 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { 231, -1206, -3575 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { -91, -1206, -2815 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_BILI, { -10, -1141, -3330 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_COW, { -383, -943, -3158 }, { 0, 17476, 0 }, 0 },
    { ACTOR_EN_COW, { -296, -885, -3566 }, { 0, 727, 0 }, 0 },
    { ACTOR_EN_RU1, { 222, -1113, -3270 }, { 0, 0, 0 }, 515 },
    { ACTOR_BG_BDAN_SWITCH, { 44, -1113, -3596 }, { 0, 0, 0 }, 15360 },
    { ACTOR_BG_BDAN_SWITCH, { -15, -1204, -3330 }, { 0, 0, 0 }, 14850 },
    { ACTOR_EN_SIOFUKI, { -12, -1400, -3321 }, { 0, 0, 3 }, 3775 },
    { ACTOR_EN_BUBBLE, { 26, -1074, -3480 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BOX, { 270, -833, -3532 }, { 0, 24211, 3 }, -32696 },
    { ACTOR_EN_BOX, { -149, -764, -2832 }, { 0, -3458, 7 }, -32668 },
    { ACTOR_EN_WONDER_ITEM, { -289, -891, -3530 }, { 0, 0, 4 }, 6531 },
    { ACTOR_EN_WONDER_ITEM, { -282, -874, -3541 }, { 0, 0, 4 }, 6531 },
    { ACTOR_EN_WONDER_ITEM, { -302, -871, -3527 }, { 0, 0, 4 }, 6531 },
    { ACTOR_EN_WONDER_ITEM, { -361, -946, -3152 }, { 0, 0, 4 }, 6535 },
    { ACTOR_EN_WONDER_ITEM, { -352, -935, -3162 }, { 0, 0, 4 }, 6535 },
    { ACTOR_EN_WONDER_ITEM, { -364, -949, -3168 }, { 0, 0, 4 }, 6535 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_3_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SIOFUKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BROB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_3_info_actors[] = {
    { ACTOR_EN_BX, { -260, -1015, -3401 }, { 0, 0, 0 }, 2561 },
    { ACTOR_EN_BILI, { -69, -990, -3530 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BILI, { 69, -1039, -3380 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BILI, { -98, -1022, -3044 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BILI, { 112, -916, -2936 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_RU1, { 222, -1113, -3270 }, { 0, 0, 0 }, 515 },
    { ACTOR_EN_SW, { -84, -977, -3657 }, { 0, 0, 0 }, -31999 },
    { ACTOR_EN_SW, { -325, -925, -3509 }, { 0, 11105, 0 }, -31998 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_3_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_4_dd_info_actors[] = {
    { ACTOR_OBJ_HSBLOCK, { -1381, -17, -2115 }, { 0, 0, 0 }, 15809 },
    { ACTOR_OBJ_HSBLOCK, { -1341, -17, -2115 }, { 0, 0, 0 }, 15809 },
    { ACTOR_OBJ_HSBLOCK, { -1237, -81, -1585 }, { 0, 16384, 16384 }, 15809 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_4_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_D_HSBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RR, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BROB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_D_LIFT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_2, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_4_info_actors[] = {
    { ACTOR_EN_OKUTA, { -1223, -40, -1479 }, { 0, -32586, 0 }, -256 },
    { ACTOR_EN_BROB, { -1104, -40, -1700 }, { 0, -16384, 0 }, 255 },
    { ACTOR_EN_BROB, { -1363, -40, -1700 }, { 0, -32586, 0 }, 255 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_4_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BROB, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_5_dd_info_actors[] = {
    { ACTOR_OBJ_HSBLOCK, { 1455, -251, -1156 }, { 0, 0, 0 }, 16065 },
    { ACTOR_EN_KUSA, { 1477, -440, -1366 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { 1691, -440, -1576 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_RR, { 1345, 387, -1448 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_RR, { 1642, 480, -1686 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_COW, { 1456, -242, -1156 }, { 0, -32767, 0 }, 2 },
    { ACTOR_EN_COW, { 1898, -155, -1688 }, { 0, -16384, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_5_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RR, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_D_HSBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_5_info_actors[] = {
    { ACTOR_ELF_MSG, { 1547, -80, -1518 }, { 0, 0, 0 }, -21447 },
    { ACTOR_EN_BILI, { 1374, -440, -1323 }, { 0, -28217, 0 }, -1 },
    { ACTOR_EN_BILI, { 1626, -420, -1829 }, { 0, -17657, 0 }, -1 },
    { ACTOR_EN_BILI, { 1622, -203, -1619 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BILI, { 1352, -197, -1467 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BILI, { 1253, -85, -1438 }, { 0, 0, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_5_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BROB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_6_dd_info_actors[] = {
    { ACTOR_EN_VALI, { -1367, 411, -2490 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_COW, { -1713, 207, -3353 }, { 0, 16384, 0 }, 0 },
    { ACTOR_EN_RU1, { -1360, -980, -3342 }, { 0, 16384, 0 }, 4 },
    { ACTOR_BG_BDAN_OBJECTS, { -1360, -1025, -3343 }, { 0, -16384, 0 }, 7936 },
    { ACTOR_EN_KUSA, { -1086, 80, -3346 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { -1360, 80, -3606 }, { 0, 0, 0 }, -255 },
    { ACTOR_DEMO_EFFECT, { -1360, -948, -3343 }, { 0, 16384, 0 }, 4117 },
    { ACTOR_EN_WONDER_ITEM, { -1633, 225, -3351 }, { 0, 0, 4 }, 6432 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_6_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BIGOKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_JEWEL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_6_info_actors[] = {
    { ACTOR_EN_BILI, { -1360, 151, -2740 }, { 0, -32586, 0 }, -1 },
    { ACTOR_EN_BILI, { -1368, 170, -2546 }, { 0, -32586, 0 }, -1 },
    { ACTOR_EN_RU1, { -1360, -980, -3342 }, { 0, 16384, 0 }, 4 },
    { ACTOR_BG_BDAN_OBJECTS, { -1360, -1025, -3343 }, { 0, -16384, 0 }, 7936 },
    { ACTOR_DEMO_EFFECT, { -1360, -948, -3343 }, { 0, 16384, 0 }, 4117 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_6_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BIGOKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_JEWEL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_7_dd_info_actors[] = {
    { ACTOR_EN_ZF, { 0, -340, -4846 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_RU1, { 6, -340, -4354 }, { 0, -32767, 0 }, 518 },
    { ACTOR_BG_BDAN_SWITCH, { 1, -341, -5178 }, { 0, 0, 0 }, 3328 },
    { ACTOR_OBJ_BOMBIWA, { -37, -340, -5173 }, { 0, 0, 0 }, 28 },
    { ACTOR_OBJ_BOMBIWA, { 31, -340, -5177 }, { 0, 23484, 0 }, 28 },
    { ACTOR_OBJ_KIBAKO, { -666, -340, -4671 }, { 0, 0, 0 }, -1 },
    { ACTOR_OBJ_KIBAKO, { -512, -340, -4592 }, { 0, 0, 0 }, -1 },
    { ACTOR_BG_YDAN_SP, { 827, -340, -4687 }, { 0, -16384, 0 }, 8150 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_7_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_YDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZF, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_7_info_actors[] = {
    { ACTOR_EN_TP, { -79, -340, -4437 }, { 0, 12014, 0 }, -1 },
    { ACTOR_EN_TP, { 69, -340, -4440 }, { 0, -11105, 0 }, -1 },
    { ACTOR_EN_TP, { -4, -340, -4517 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_TP, { -414, -340, -4557 }, { 0, 11105, 0 }, -1 },
    { ACTOR_EN_TP, { 396, -340, -4557 }, { 0, -10741, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_7_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TP, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_8_dd_info_actors[] = {
    { ACTOR_EN_BA, { 1, 20, -5801 }, { 0, 0, 0 }, 16383 },
    { ACTOR_EN_BILI, { -181, -262, -5982 }, { 0, 8737, 0 }, -1 },
    { ACTOR_EN_BILI, { -184, -271, -5623 }, { 0, 24030, 0 }, -1 },
    { ACTOR_EN_BILI, { 184, -253, -5621 }, { 0, -23665, 0 }, -1 },
    { ACTOR_EN_BILI, { 182, -275, -5978 }, { 0, -8191, 0 }, -1 },
    { ACTOR_EN_BOX, { -11, -340, -6006 }, { 0, -32767, 0 }, 4288 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_8_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_8_info_actors[] = {
    { ACTOR_EN_BA, { 1, 20, -5801 }, { 0, 0, 0 }, 2561 },
    { ACTOR_EN_BILI, { -181, -262, -5982 }, { 0, 8737, 0 }, -1 },
    { ACTOR_EN_BILI, { -184, -271, -5623 }, { 0, 24030, 0 }, -1 },
    { ACTOR_EN_BILI, { 184, -253, -5621 }, { 0, -23665, 0 }, -1 },
    { ACTOR_EN_BILI, { 182, -275, -5978 }, { 0, -8191, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_8_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_9_dd_info_actors[] = {
    { ACTOR_EN_EIYER, { 1544, -340, -4836 }, { 0, -8191, 0 }, 0 },
    { ACTOR_EN_EIYER, { 1562, -340, -4533 }, { 0, -23119, 0 }, 0 },
    { ACTOR_EN_BOX, { 1549, -340, -4681 }, { 0, -16384, 0 }, 6177 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_9_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_9_info_actors[] = {
    { ACTOR_EN_EIYER, { 1640, -340, -4827 }, { 0, -16384, 0 }, 10 },
    { ACTOR_EN_EIYER, { 1640, -340, -4561 }, { 0, 16384, 0 }, 10 },
    { ACTOR_EN_EIYER, { 1420, -340, -4692 }, { 0, 0, 0 }, 10 },
    { ACTOR_EN_BOX, { 1548, -340, -4681 }, { 0, 16384, 0 }, 4289 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_9_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_10_dd_info_actors[] = {
    { ACTOR_EN_BA, { -1561, 20, -4681 }, { 0, 16384, 0 }, 16383 },
    { ACTOR_EN_BOX, { -1762, -340, -4680 }, { 0, 16384, 0 }, 6146 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_10_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_10_info_actors[] = {
    { ACTOR_EN_BA, { -1561, 20, -4681 }, { 0, 16384, 0 }, 2816 },
    { ACTOR_EN_BOX, { -1793, -340, -4680 }, { 0, -16384, 0 }, 6178 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_10_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_11_dd_info_actors[] = {
    { ACTOR_OBJ_HSBLOCK, { 933, 302, -5669 }, { 0, 0, 0 }, 9665 },
    { ACTOR_OBJ_HSBLOCK, { 933, 302, -5629 }, { 0, 0, 0 }, 9665 },
    { ACTOR_OBJ_HSBLOCK, { 386, 301, -5669 }, { 0, 0, 0 }, 10177 },
    { ACTOR_OBJ_HSBLOCK, { 386, 301, -5630 }, { 0, 0, 0 }, 10177 },
    { ACTOR_EN_KUSA, { 652, -340, -5687 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_RR, { 1003, 224, -5647 }, { 0, -16384, 0 }, -1 },
    { ACTOR_EN_RR, { 324, 207, -5652 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_COW, { 334, -202, -5645 }, { 0, 21299, 0 }, 0 },
    { ACTOR_EN_COW, { 988, -233, -5665 }, { 0, -12924, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_11_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RR, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_D_HSBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SYOKUDAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_11_info_actors[] = {
    { ACTOR_EN_BA, { 660, 20, -5680 }, { 0, 0, 0 }, 16383 },
    { ACTOR_EN_BOX, { 661, -340, -5876 }, { 0, -32767, 0 }, 1988 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_11_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BXA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_12_dd_info_actors[] = {
    { ACTOR_EN_SW, { -887, -309, -5912 }, { 0, 8191, 0 }, 0 },
    { ACTOR_EN_TP, { -551, -340, -5752 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_TP, { -766, -340, -5754 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_TP, { -662, -340, -5555 }, { 0, 0, 0 }, -1 },
    { ACTOR_BG_BDAN_SWITCH, { -655, -341, -5684 }, { 0, 910, 0 }, 15618 },
    { ACTOR_OBJ_TIMEBLOCK, { -492, -216, -5680 }, { 0, 0, 0 }, 14591 },
    { ACTOR_EN_SIOFUKI, { -655, -549, -5683 }, { 0, 0, 3 }, 3967 },
    { ACTOR_OBJ_BOMBIWA, { -411, -106, -5682 }, { 0, 0, 0 }, 14 },
    { ACTOR_OBJ_BOMBIWA, { -885, -340, -5907 }, { 0, 0, 0 }, 16 },
    { ACTOR_EN_BUBBLE, { -530, -55, -5841 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -826, -168, -5577 }, { 0, 0, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_12_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SIOFUKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_12_info_actors[] = {
    { ACTOR_OBJ_ROOMTIMER, { -651, -340, -5767 }, { 0, 0, 0 }, 30760 },
    { ACTOR_EN_BOX, { -669, -340, -5681 }, { 0, -32767, 30 }, -18428 },
    { ACTOR_EN_BUBBLE, { -776, -340, -5894 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -560, -340, -5746 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -552, -340, -5606 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -749, -47, -5771 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -744, -83, -5579 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -643, -340, -5552 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -445, -138, -5823 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -786, -340, -5667 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { -852, -340, -5480 }, { 0, 0, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_12_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_13_dd_info_actors[] = {
    { ACTOR_OBJ_HSBLOCK, { -1044, -1265, -2332 }, { 0, 7464, 0 }, 1473 },
    { ACTOR_EN_FIREFLY, { -1055, -1113, -2154 }, { 0, 0, 0 }, -32765 },
    { ACTOR_EN_FIREFLY, { -1300, -974, -2390 }, { 0, 15838, 0 }, -32765 },
    { ACTOR_EN_FIREFLY, { -1094, -973, -2637 }, { 0, 7099, 0 }, -32765 },
    { ACTOR_BG_BDAN_SWITCH, { -1128, -1234, -2518 }, { 0, 0, 0 }, 15106 },
    { ACTOR_EN_WEIYER, { -987, -1203, -2604 }, { 0, -7281, 0 }, 0 },
    { ACTOR_EN_WEIYER, { -871, -1163, -2316 }, { 0, -23665, 0 }, 0 },
    { ACTOR_OBJ_ROOMTIMER, { -992, -1233, -2378 }, { 0, 0, 0 }, 6143 },
    { ACTOR_BG_YDAN_SP, { -1147, -1113, -2243 }, { 0, 24030, 0 }, 8174 },
    { ACTOR_BG_YDAN_SP, { -1149, -1113, -2242 }, { 0, -8737, 0 }, 8174 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_13_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_D_HSBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_YDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_13_info_actors[] = {
    { ACTOR_EN_OKUTA, { -804, -1140, -2302 }, { 0, 27307, 0 }, -256 },
    { ACTOR_EN_OKUTA, { -1058, -1140, -2579 }, { 0, 20571, 0 }, -256 },
    { ACTOR_EN_BROB, { -944, -1233, -2426 }, { 0, 23665, 0 }, 255 },
    { ACTOR_OBJ_TSUBO, { -1150, -1113, -2248 }, { 0, 23665, 0 }, 16402 },
    { ACTOR_OBJ_TSUBO, { -1127, -1113, -2271 }, { 0, 24940, 0 }, 16897 },
    { ACTOR_OBJ_TSUBO, { -1178, -1113, -2272 }, { 0, 23665, 0 }, 17410 },
    { ACTOR_OBJ_TSUBO, { -1131, -1113, -2221 }, { 0, 23665, 0 }, 17922 },
    { ACTOR_OBJ_TSUBO, { -1173, -1113, -2227 }, { 0, 23848, 0 }, 18433 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_13_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BROB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MJIN_WIND, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_14_dd_info_actors[] = {
    { ACTOR_EN_EIYER, { 1080, -1283, -2261 }, { 0, -32767, 0 }, 10 },
    { ACTOR_EN_EIYER, { 923, -1293, -2355 }, { 0, 0, 0 }, 10 },
    { ACTOR_EN_KUSA, { 1228, -1193, -2647 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_RR, { 1246, -1293, -2470 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_ZF, { 928, -1153, -3216 }, { 0, -14382, 0 }, -1 },
    { ACTOR_BG_BDAN_SWITCH, { 1240, -1293, -2383 }, { 0, 0, 0 }, 14594 },
    { ACTOR_BG_BDAN_OBJECTS, { 1100, -1283, -2383 }, { 0, 0, 0 }, 14594 },
    { ACTOR_OBJ_TIMEBLOCK, { 941, -1293, -2563 }, { 0, 0, 0 }, -18177 },
    { ACTOR_EN_BOX, { 1288, -1193, -2638 }, { 0, 0, 0 }, 20545 },
    { ACTOR_EN_SW, { 940, -1293, -2583 }, { 0, 0, 0 }, -31999 },
    { ACTOR_OBJ_TSUBO, { 696, -909, -2318 }, { 0, 0, 0 }, 26116 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_14_dd_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RR, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MIZU_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SIOFUKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_bdan_dd_info_bdan_14_info_actors[] = {
    { ACTOR_EN_EIYER, { 1100, -1293, -2521 }, { 0, 0, 0 }, 10 },
    { ACTOR_EN_EIYER, { 936, -1283, -2276 }, { 0, 0, 0 }, 10 },
    { ACTOR_EN_EIYER, { 1264, -1293, -2281 }, { 0, 0, 0 }, 10 },
    { ACTOR_BG_BDAN_SWITCH, { 651, -1071, -1603 }, { 0, 0, -32767 }, 15364 },
    { ACTOR_BG_BDAN_SWITCH, { 1098, -1293, -2386 }, { 0, 0, 0 }, 14594 },
    { ACTOR_BG_BDAN_OBJECTS, { 1100, -1283, -2383 }, { 0, 0, 0 }, 14594 },
    { ACTOR_EN_SW, { 801, -1129, -2380 }, { 0, 16384, 0 }, -31992 },
    { ACTOR_OBJ_TSUBO, { 645, -1073, -2408 }, { 0, 0, 0 }, 18960 },
    { ACTOR_OBJ_TSUBO, { 703, -1073, -2371 }, { 0, 0, 0 }, 19474 },
    { ACTOR_OBJ_TSUBO, { 650, -1073, -2343 }, { 0, 0, 0 }, 19984 },
    { ACTOR_EN_BUBBLE, { 668, -1085, -3238 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { 770, -1099, -3202 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { 1091, -1176, -1770 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { 1048, -1185, -1618 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { 881, -1105, -3171 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { 999, -1105, -3192 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BUBBLE, { 889, -1185, -1551 }, { 0, 0, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_dd_info_bdan_14_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_VALI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_bdan_dd_info_room_refs[] = {
    { "bdan_0_dd_info.zsi", 0 },
    { "bdan_1_dd_info.zsi", 1 },
    { "bdan_2_dd_info.zsi", 2 },
    { "bdan_3_dd_info.zsi", 3 },
    { "bdan_4_dd_info.zsi", 4 },
    { "bdan_5_dd_info.zsi", 5 },
    { "bdan_6_dd_info.zsi", 6 },
};

static const Oot3dSceneSetupIndex oot3d_bdan_dd_info_setups[] = {
    { 0u, oot3d_bdan_dd_info_setup_0_commands, 12u, oot3d_bdan_dd_info_setup_0_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_bdan_dd_info_setup_0_spawns, 1u, oot3d_bdan_dd_info_setup_0_entrances, 2u, oot3d_bdan_dd_info_setup_0_transition_actors, 22u, oot3d_bdan_dd_info_setup_0_light_settings, 4u, oot3d_bdan_dd_info_setup_0_exits, 2u, oot3d_bdan_dd_info_setup_0_skybox_settings, 1u, oot3d_bdan_dd_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_bdan_dd_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_bdan_dd_info_setup_1_commands, 13u, oot3d_bdan_dd_info_setup_1_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_bdan_dd_info_setup_1_spawns, 2u, oot3d_bdan_dd_info_setup_1_entrances, 2u, oot3d_bdan_dd_info_setup_1_transition_actors, 22u, oot3d_bdan_dd_info_setup_1_light_settings, 4u, oot3d_bdan_dd_info_setup_1_exits, 2u, oot3d_bdan_dd_info_setup_1_skybox_settings, 1u, oot3d_bdan_dd_info_setup_1_sound_settings, 1u, oot3d_bdan_dd_info_setup_1_cutscenes, 1u, oot3d_bdan_dd_info_setup_1_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_bdan_dd_info_rooms[] = {
    { "bdan_0_dd_info.zsi", 0, oot3d_bdan_dd_info_bdan_0_dd_info_objects, 11u, oot3d_bdan_dd_info_bdan_0_dd_info_actors, 18u },
    { "bdan_0_info.zsi", 0, oot3d_bdan_dd_info_bdan_0_info_objects, 7u, oot3d_bdan_dd_info_bdan_0_info_actors, 5u },
    { "bdan_1_dd_info.zsi", 1, oot3d_bdan_dd_info_bdan_1_dd_info_objects, 11u, oot3d_bdan_dd_info_bdan_1_dd_info_actors, 10u },
    { "bdan_1_info.zsi", 1, oot3d_bdan_dd_info_bdan_1_info_objects, 12u, oot3d_bdan_dd_info_bdan_1_info_actors, 8u },
    { "bdan_2_dd_info.zsi", 2, oot3d_bdan_dd_info_bdan_2_dd_info_objects, 14u, oot3d_bdan_dd_info_bdan_2_dd_info_actors, 5u },
    { "bdan_2_info.zsi", 2, NULL, 0u, NULL, 0u },
    { "bdan_3_dd_info.zsi", 3, oot3d_bdan_dd_info_bdan_3_dd_info_objects, 14u, oot3d_bdan_dd_info_bdan_3_dd_info_actors, 19u },
    { "bdan_3_info.zsi", 3, oot3d_bdan_dd_info_bdan_3_info_objects, 10u, oot3d_bdan_dd_info_bdan_3_info_actors, 8u },
    { "bdan_4_dd_info.zsi", 4, oot3d_bdan_dd_info_bdan_4_dd_info_objects, 10u, oot3d_bdan_dd_info_bdan_4_dd_info_actors, 3u },
    { "bdan_4_info.zsi", 4, oot3d_bdan_dd_info_bdan_4_info_objects, 8u, oot3d_bdan_dd_info_bdan_4_info_actors, 3u },
    { "bdan_5_dd_info.zsi", 5, oot3d_bdan_dd_info_bdan_5_dd_info_objects, 10u, oot3d_bdan_dd_info_bdan_5_dd_info_actors, 7u },
    { "bdan_5_info.zsi", 5, oot3d_bdan_dd_info_bdan_5_info_objects, 10u, oot3d_bdan_dd_info_bdan_5_info_actors, 6u },
    { "bdan_6_dd_info.zsi", 6, oot3d_bdan_dd_info_bdan_6_dd_info_objects, 11u, oot3d_bdan_dd_info_bdan_6_dd_info_actors, 8u },
    { "bdan_6_info.zsi", 6, oot3d_bdan_dd_info_bdan_6_info_objects, 10u, oot3d_bdan_dd_info_bdan_6_info_actors, 5u },
    { "bdan_7_dd_info.zsi", 7, oot3d_bdan_dd_info_bdan_7_dd_info_objects, 9u, oot3d_bdan_dd_info_bdan_7_dd_info_actors, 8u },
    { "bdan_7_info.zsi", 7, oot3d_bdan_dd_info_bdan_7_info_objects, 9u, oot3d_bdan_dd_info_bdan_7_info_actors, 5u },
    { "bdan_8_dd_info.zsi", 8, oot3d_bdan_dd_info_bdan_8_dd_info_objects, 6u, oot3d_bdan_dd_info_bdan_8_dd_info_actors, 6u },
    { "bdan_8_info.zsi", 8, oot3d_bdan_dd_info_bdan_8_info_objects, 10u, oot3d_bdan_dd_info_bdan_8_info_actors, 5u },
    { "bdan_9_dd_info.zsi", 9, oot3d_bdan_dd_info_bdan_9_dd_info_objects, 5u, oot3d_bdan_dd_info_bdan_9_dd_info_actors, 3u },
    { "bdan_9_info.zsi", 9, oot3d_bdan_dd_info_bdan_9_info_objects, 9u, oot3d_bdan_dd_info_bdan_9_info_actors, 4u },
    { "bdan_10_dd_info.zsi", 10, oot3d_bdan_dd_info_bdan_10_dd_info_objects, 5u, oot3d_bdan_dd_info_bdan_10_dd_info_actors, 2u },
    { "bdan_10_info.zsi", 10, oot3d_bdan_dd_info_bdan_10_info_objects, 10u, oot3d_bdan_dd_info_bdan_10_info_actors, 2u },
    { "bdan_11_dd_info.zsi", 11, oot3d_bdan_dd_info_bdan_11_dd_info_objects, 13u, oot3d_bdan_dd_info_bdan_11_dd_info_actors, 9u },
    { "bdan_11_info.zsi", 11, oot3d_bdan_dd_info_bdan_11_info_objects, 4u, oot3d_bdan_dd_info_bdan_11_info_actors, 2u },
    { "bdan_12_dd_info.zsi", 12, oot3d_bdan_dd_info_bdan_12_dd_info_objects, 11u, oot3d_bdan_dd_info_bdan_12_dd_info_actors, 11u },
    { "bdan_12_info.zsi", 12, oot3d_bdan_dd_info_bdan_12_info_objects, 9u, oot3d_bdan_dd_info_bdan_12_info_actors, 11u },
    { "bdan_13_dd_info.zsi", 13, oot3d_bdan_dd_info_bdan_13_dd_info_objects, 8u, oot3d_bdan_dd_info_bdan_13_dd_info_actors, 10u },
    { "bdan_13_info.zsi", 13, oot3d_bdan_dd_info_bdan_13_info_objects, 10u, oot3d_bdan_dd_info_bdan_13_info_actors, 8u },
    { "bdan_14_dd_info.zsi", 14, oot3d_bdan_dd_info_bdan_14_dd_info_objects, 15u, oot3d_bdan_dd_info_bdan_14_dd_info_actors, 11u },
    { "bdan_14_info.zsi", 14, oot3d_bdan_dd_info_bdan_14_info_objects, 10u, oot3d_bdan_dd_info_bdan_14_info_actors, 17u },
    { "bdan_15_dd_info.zsi", 15, NULL, 0u, NULL, 0u },
    { "bdan_15_info.zsi", 15, NULL, 0u, NULL, 0u },
};

const Oot3dSceneIndex oot3d_scene_index_bdan_dd_info = {
    "bdan_dd_info.zsi",
    oot3d_bdan_dd_info_room_refs, 7u,
    oot3d_bdan_dd_info_rooms, 32u,
    oot3d_bdan_dd_info_setups, 2u,
};
