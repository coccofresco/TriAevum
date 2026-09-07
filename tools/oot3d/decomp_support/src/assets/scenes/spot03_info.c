/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: spot03_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_spot03_info_setup_0_commands[] = {
    { 0x00000215u, 0x01000585u },
    { 0x00000204u, 0x000000E4u },
    { 0x0000010Eu, 0x0000016Cu },
    { 0x00000019u, 0x00000003u },
    { 0x00000003u, 0x00009610u },
    { 0x00000506u, 0x0000963Cu },
    { 0x00000107u, 0x00000002u },
    { 0x0000020Du, 0x00009708u },
    { 0x00000500u, 0x00009718u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x00009768u },
    { 0x00000C0Fu, 0x00009774u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot03_info_setup_0_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot03_info_setup_0_paths_0_points[] = {
    { 18, 0, -27048 },
    { 0, 11, 0 },
    { -26940, 0, 3420 },
    { 542, -1420, 2963 },
    { 542, -1435, 2918 },
    { 527, -552, 2278 },
    { 542, -506, 2278 },
    { 299, -506, 1388 },
    { 237, -381, 1394 },
    { 138, -381, 658 },
    { 132, 13, -5 },
    { 132, 7, -3 },
    { 33, 13, -1140 },
    { 27, 13, -1285 },
    { 33, 144, -1291 },
    { 33, 413, -1289 },
    { -58, 433, -1295 },
    { -52, 1316, -1433 },
};

static const Oot3dVec3s oot3d_spot03_info_setup_0_paths_1_points[] = {
    { -58, 1801, -1965 },
    { -58, 1979, -2444 },
    { -52, 1716, 1764 },
    { 243, -449, 1756 },
    { 226, -1237, 1362 },
    { 243, -1237, 1362 },
    { 120, -1237, 1248 },
    { 120, -1237, 1239 },
    { 120, -269, 1232 },
    { 120, -1105, -105 },
    { 136, -1095, -105 },
};

static const Oot3dPathRecord oot3d_spot03_info_setup_0_paths[] = {
    { 18u, 0u, 0u, 38488u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot03_info_setup_0_paths_0_points },
    { 11u, 0u, 0u, 38596u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot03_info_setup_0_paths_1_points },
};

static const Oot3dActorEntry oot3d_spot03_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { -1506, -20, 1510 }, { 0, 28763, 0 }, 4095 },
    { ACTOR_PLAYER, { 4179, 1441, -1384 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { 4413, 920, -1403 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -1357, -86, 1542 }, { 0, 30948, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot03_info_setup_0_entrances[] = {
    { 0u, 0 },
    { 1u, 1 },
    { 2u, 1 },
    { 3u, 0 },
    { 4u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot03_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot03_info_setup_0_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x0F, 0x81, 0x01, 0x0E, 0x01, 0x08, 0x01, 0x11, 0x03, 0xDA, 0x04, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xA0, 0x0C, 0x47, 0x28, 0x04, 0x68, 0x68 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xE5, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x33, 0x14, 0x8C, 0x68, 0x4F, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x47, 0x90, 0x05, 0x82, 0x82 } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x6D, 0x6D, 0x63, 0x8C, 0xCC, 0xCC, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xA0, 0x0C, 0x47, 0x28, 0x04, 0x8C, 0x3F } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xB5, 0x33, 0xB8, 0xB8, 0xB8, 0x33, 0x14, 0x14, 0x82, 0x33, 0x28, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x00, 0x7A, 0x46, 0x28, 0x04, 0x33, 0x3F } },
    { { 0x82, 0x48, 0x48, 0x48, 0x2D, 0x2D, 0x38, 0xB8, 0xB8, 0xB8, 0x4F, 0xB5, 0xFF, 0x00, 0x05, 0x1E, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x33, 0x49 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0xFC, 0x49, 0x82 } },
    { { 0x63, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x49, 0x49 } },
    { { 0x33, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0xFC, 0x19, 0x38 } },
    { { 0x66, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x00, 0xFA, 0x45, 0xC8, 0x04, 0x59, 0x59 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0xBF, 0xBF, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x90, 0x05, 0x72, 0x72 } },
    { { 0x77, 0x00, 0x00, 0x00, 0xDB, 0xDB, 0xDB, 0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x59, 0x59, 0x5E, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x00, 0xFA, 0x45, 0xC8, 0x04, 0x5E, 0x5E } },
    { { 0x59, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x3D, 0x33, 0x33, 0x4C, 0x3F, 0x3F, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0x04, 0x3D, 0x4C } },
};

static const Oot3dExitEntry oot3d_spot03_info_setup_0_exits[] = {
    { 385, 385u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 270, 270u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 264, 264u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 785, 785u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1242, 1242u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot03_info_setup_0_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot03_info_setup_0_sound_settings[] = {
    { 2u, 1413u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot03_info_setup_0_misc_settings[] = {
    { 0u, 0x00000003u },
};

static const Oot3dSceneCommand oot3d_spot03_info_setup_1_commands[] = {
    { 0x00000215u, 0x01000585u },
    { 0x00000204u, 0x000098C4u },
    { 0x0000010Eu, 0x0000994Cu },
    { 0x00000019u, 0x00000003u },
    { 0x00000003u, 0x00009610u },
    { 0x00000506u, 0x0000995Cu },
    { 0x00000107u, 0x00000002u },
    { 0x0000030Du, 0x00009ADCu },
    { 0x00000500u, 0x00009AF4u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x00009B44u },
    { 0x00000C0Fu, 0x00009B50u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot03_info_setup_1_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot03_info_setup_1_paths_0_points[] = {
    { 11, 0, -26132 },
    { 0, 29, 0 },
    { -26066, 0, 3420 },
    { 542, -1420, 2963 },
    { 542, -1435, 2918 },
    { 527, -552, 2278 },
    { 542, -506, 2278 },
    { 299, -506, 1388 },
    { 237, -381, 1394 },
    { 138, -381, 658 },
    { 132, 13, -5 },
    { 132, 7, -3 },
    { 33, 13, -1140 },
    { 27, 13, -1285 },
    { 33, 144, -1291 },
    { 33, 413, -1289 },
    { -58, 433, -1295 },
    { -52, 1316, -1433 },
};

static const Oot3dVec3s oot3d_spot03_info_setup_1_paths_1_points[] = {
    { -58, 1801, -1965 },
    { -58, 1979, -2444 },
    { -52, 1716, 1764 },
    { 243, -449, 1756 },
    { 226, -1237, 1362 },
    { 243, -1237, 1362 },
    { 120, -1237, 1248 },
    { 120, -1237, 1239 },
    { 120, -269, 1232 },
    { 120, -1105, -105 },
    { 136, -1095, -105 },
};

static const Oot3dVec3s oot3d_spot03_info_setup_1_paths_2_points[] = {
    { 120, -742, -114 },
    { 29, -742, -105 },
    { 38, 19, -730 },
    { 100, -220, -730 },
    { 310, -220, -331 },
    { 371, -225, -112 },
    { 428, -291, 83 },
    { 580, -395, 378 },
    { 642, -457, 530 },
    { 618, -557, 535 },
    { 571, -743, 535 },
    { 386, -743, 803 },
    { 361, -816, 953 },
    { 392, -819, 1288 },
    { 443, -789, 1764 },
    { 537, -575, 2600 },
    { 672, -534, 2828 },
    { 747, -550, 2958 },
    { 890, -675, 2923 },
    { 747, -835, 2888 },
    { 739, -680, 2733 },
    { 705, -560, 1779 },
    { 546, -585, 1287 },
    { 537, -790, 902 },
    { 630, -1145, 574 },
    { 605, -1285, 299 },
    { 563, -1175, -186 },
    { 436, -890, -306 },
    { 411, -325, -551 },
};

static const Oot3dPathRecord oot3d_spot03_info_setup_1_paths[] = {
    { 18u, 0u, 0u, 39296u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot03_info_setup_1_paths_0_points },
    { 11u, 0u, 0u, 39404u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot03_info_setup_1_paths_1_points },
    { 29u, 0u, 0u, 39470u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot03_info_setup_1_paths_2_points },
};

static const Oot3dActorEntry oot3d_spot03_info_setup_1_spawns[] = {
    { ACTOR_PLAYER, { -1509, -20, 1502 }, { 0, 26761, 0 }, 4095 },
    { ACTOR_PLAYER, { 4179, 1441, -1384 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { 4404, 920, -1403 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -1364, -87, 1566 }, { 0, 29127, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot03_info_setup_1_entrances[] = {
    { 0u, 0 },
    { 1u, 1 },
    { 2u, 1 },
    { 3u, 0 },
    { 4u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot03_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot03_info_setup_1_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x0F, 0x81, 0x01, 0x0E, 0x01, 0x08, 0x01, 0x11, 0x03, 0xDA, 0x04, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xA0, 0x0C, 0x47, 0x28, 0x04, 0x68, 0x68 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xE5, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x33, 0x14, 0x8C, 0x68, 0x4F, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x47, 0x90, 0x05, 0x82, 0x82 } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x6D, 0x6D, 0x63, 0x8C, 0xCC, 0xCC, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xA0, 0x0C, 0x47, 0x28, 0x04, 0x8C, 0x3F } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xB5, 0x33, 0xB8, 0xB8, 0xB8, 0x33, 0x14, 0x14, 0x82, 0x33, 0x28, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x00, 0x7A, 0x46, 0x28, 0x04, 0x33, 0x3F } },
    { { 0x82, 0x48, 0x48, 0x48, 0x2D, 0x2D, 0x38, 0xB8, 0xB8, 0xB8, 0x4F, 0xB5, 0xFF, 0x00, 0x05, 0x1E, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x33, 0x49 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0xFC, 0x49, 0x82 } },
    { { 0x63, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x49, 0x49 } },
    { { 0x33, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0xFC, 0x19, 0x38 } },
    { { 0x66, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x00, 0xFA, 0x45, 0xC8, 0x04, 0x59, 0x59 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0xBF, 0xBF, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x90, 0x05, 0x72, 0x72 } },
    { { 0x77, 0x00, 0x00, 0x00, 0xDB, 0xDB, 0xDB, 0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x59, 0x59, 0x5E, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x00, 0xFA, 0x45, 0xC8, 0x04, 0x5E, 0x5E } },
    { { 0x59, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x3D, 0x33, 0x33, 0x4C, 0x3F, 0x3F, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0x04, 0x3D, 0x4C } },
};

static const Oot3dExitEntry oot3d_spot03_info_setup_1_exits[] = {
    { 385, 385u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 270, 270u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 264, 264u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 785, 785u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1242, 1242u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot03_info_setup_1_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot03_info_setup_1_sound_settings[] = {
    { 2u, 1413u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot03_info_setup_1_misc_settings[] = {
    { 0u, 0x00000003u },
};

static const Oot3dActorEntry oot3d_spot03_info_spot03_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 2749, 460, -453 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_RIVER_SOUND, { 555, 180, -1216 }, { 0, 0, 0 }, 256 },
    { ACTOR_EN_OWL, { -1397, 260, 426 }, { 0, 0, 0 }, 447 },
    { ACTOR_EN_FR, { 990, 205, -1220 }, { 0, 5460, 0 }, 0 },
    { ACTOR_EN_OKUTA, { -133, 60, -53 }, { 0, -21844, 0 }, -256 },
    { ACTOR_EN_OKUTA, { 2928, 500, -557 }, { 0, -24576, 0 }, -256 },
    { ACTOR_EN_FR, { 1093, 205, -1069 }, { 0, -26396, 0 }, 1 },
    { ACTOR_EN_GS, { 800, 570, -1070 }, { 0, 0, 0 }, 14605 },
    { ACTOR_EN_ITEM00, { 376, 460, -1237 }, { 0, 8191, 0 }, 1030 },
    { ACTOR_EN_FR, { 1082, 199, -1028 }, { 0, -28217, 0 }, 3 },
    { ACTOR_EN_FR, { 1169, 226, -1021 }, { 0, -24030, 0 }, 4 },
    { ACTOR_DOOR_ANA, { 360, 570, 130 }, { 0, -32767, 0 }, 41 },
    { ACTOR_DOOR_ANA, { -1630, 100, -130 }, { 0, 0, 10 }, 491 },
    { ACTOR_EN_NIW, { -1634, 100, -131 }, { 0, 7645, 0 }, 11 },
    { ACTOR_EN_NIW, { 483, 570, -240 }, { 0, 26213, 0 }, 12 },
    { ACTOR_EN_WONDER_ITEM, { -1296, 19, 242 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -1290, -99, 862 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -1288, -101, 668 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -1288, -99, 1052 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -1287, -100, 457 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -1063, 10, -4 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -860, 26, -38 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -145, 140, -938 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -142, 15, -383 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -141, 18, -588 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 9, 140, -1158 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 158, 84, 6 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 269, 140, -1018 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 353, 83, 9 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 509, 140, -1223 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 520, 81, 12 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 670, 140, 3 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 902, 140, -150 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 1020, 140, -973 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 1021, 140, -224 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 1124, 140, -803 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 1144, 140, -296 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 1204, 140, -608 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 1287, 140, -363 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -147, 18, -736 }, { 0, 0, 1 }, 4735 },
    { ACTOR_EN_WONDER_ITEM, { 786, 140, -74 }, { 0, 0, 1 }, 4735 },
    { ACTOR_EN_WONDER_ITEM, { -141, 12, -218 }, { 0, 0, 1 }, 4799 },
    { ACTOR_EN_TITE, { -122, 180, -1252 }, { 0, 0, 0 }, -2 },
    { ACTOR_EN_TITE, { 330, 180, -1108 }, { 0, -16384, 0 }, -2 },
    { ACTOR_EN_FR, { 1098, 194, -1110 }, { 0, -23665, 0 }, 2 },
    { ACTOR_EN_ISHI, { 2044, 440, -786 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_ISHI, { 2425, 488, -446 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_ISHI, { 2425, 528, -524 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_ISHI, { 2503, 528, -571 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_ISHI, { 2550, 488, -415 }, { 0, 0, 0 }, 512 },
    { ACTOR_OBJ_MURE2, { -1635, 128, -133 }, { 0, 0, 0 }, 514 },
    { ACTOR_OBJ_MURE2, { 668, 598, -370 }, { 0, 0, 0 }, 514 },
    { ACTOR_EN_KUSA, { 231, 460, -1478 }, { 0, 0, 0 }, 528 },
    { ACTOR_OBJ_MURE2, { -1508, 91, 943 }, { 0, 0, 0 }, 513 },
    { ACTOR_EN_WOOD02, { -1690, 100, 554 }, { 0, 5460, 114 }, 513 },
    { ACTOR_OBJ_MURE, { -1636, 164, -131 }, { 0, 0, 0 }, 8996 },
    { ACTOR_OBJ_BEAN, { -730, 100, -220 }, { 0, -16384, 0 }, 7939 },
    { ACTOR_EN_MS, { -717, 100, -312 }, { 0, -16384, 0 }, -1 },
    { ACTOR_EN_WONDER_TALK2, { 1000, 205, -1202 }, { 0, -27125, 0 }, 19387 },
    { ACTOR_EN_FR, { 1122, 230, -980 }, { 0, -29127, 0 }, 5 },
    { ACTOR_OBJ_BOMBIWA, { -1456, 100, 434 }, { 0, -2731, 0 }, 2 },
    { ACTOR_OBJ_BOMBIWA, { 672, 561, -366 }, { 0, 0, 0 }, -32763 },
    { ACTOR_OBJ_BOMBIWA, { -1518, 100, 435 }, { 0, -25850, 0 }, 8 },
    { ACTOR_OBJ_BOMBIWA, { -1576, 100, 430 }, { 0, 16384, 0 }, 9 },
    { ACTOR_OBJ_BOMBIWA, { -1400, 100, 482 }, { 0, 0, 0 }, 10 },
    { ACTOR_DOOR_ANA, { 670, 570, -365 }, { 0, -8191, 6 }, 4326 },
};

static const Oot3dRoomObjectEntry oot3d_spot03_info_spot03_0_info_objects[] = {
    { OBJECT_SPOT03_OBJECT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_NIW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OWL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FR, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WOOD02, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot03_info_spot03_1_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 3207, 500, -1526 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_ITEM00, { 4216, 520, -1463 }, { 0, 0, 0 }, 8194 },
    { ACTOR_EN_ITEM00, { 4171, 520, -1436 }, { 0, 0, 0 }, 8450 },
    { ACTOR_EN_ITEM00, { 4171, 520, -1376 }, { 0, 0, 0 }, 8706 },
    { ACTOR_EN_ITEM00, { 4223, 520, -1338 }, { 0, 0, 0 }, 8962 },
    { ACTOR_EN_GS, { 3930, 640, -1650 }, { 0, 0, 0 }, 14860 },
    { ACTOR_BG_SPOT03_TAKI, { 4226, 660, -1401 }, { 0, 0, 0 }, 56 },
    { ACTOR_EN_ITEM00, { 3585, 848, -1665 }, { 0, 0, 0 }, 2822 },
};

static const Oot3dRoomObjectEntry oot3d_spot03_info_spot03_1_info_objects[] = {
    { OBJECT_SPOT03_OBJECT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HORSE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_spot03_info_room_refs[] = {
    { "spot03_0_info.zsi", 0 },
    { "spot03_1_info.zsi", 1 },
};

static const Oot3dSceneSetupIndex oot3d_spot03_info_setups[] = {
    { 0u, oot3d_spot03_info_setup_0_commands, 13u, oot3d_spot03_info_setup_0_special_files, 1u, oot3d_spot03_info_setup_0_paths, 2u, NULL, 0u, oot3d_spot03_info_setup_0_spawns, 4u, oot3d_spot03_info_setup_0_entrances, 5u, oot3d_spot03_info_setup_0_transition_actors, 1u, oot3d_spot03_info_setup_0_light_settings, 12u, oot3d_spot03_info_setup_0_exits, 6u, oot3d_spot03_info_setup_0_skybox_settings, 1u, oot3d_spot03_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_spot03_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_spot03_info_setup_1_commands, 13u, oot3d_spot03_info_setup_1_special_files, 1u, oot3d_spot03_info_setup_1_paths, 3u, NULL, 0u, oot3d_spot03_info_setup_1_spawns, 4u, oot3d_spot03_info_setup_1_entrances, 5u, oot3d_spot03_info_setup_1_transition_actors, 1u, oot3d_spot03_info_setup_1_light_settings, 12u, oot3d_spot03_info_setup_1_exits, 6u, oot3d_spot03_info_setup_1_skybox_settings, 1u, oot3d_spot03_info_setup_1_sound_settings, 1u, NULL, 0u, oot3d_spot03_info_setup_1_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_spot03_info_rooms[] = {
    { "spot03_0_info.zsi", 0, oot3d_spot03_info_spot03_0_info_objects, 14u, oot3d_spot03_info_spot03_0_info_actors, 66u },
    { "spot03_1_info.zsi", 1, oot3d_spot03_info_spot03_1_info_objects, 6u, oot3d_spot03_info_spot03_1_info_actors, 8u },
};

const Oot3dSceneIndex oot3d_scene_index_spot03_info = {
    "spot03_info.zsi",
    oot3d_spot03_info_room_refs, 2u,
    oot3d_spot03_info_rooms, 2u,
    oot3d_spot03_info_setups, 2u,
};
