/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: spot09_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_spot09_info_setup_0_commands[] = {
    { 0x00000115u, 0x010005CCu },
    { 0x00000104u, 0x00000210u },
    { 0x00000019u, 0x00000009u },
    { 0x00000003u, 0x000083B4u },
    { 0x00000506u, 0x000083E0u },
    { 0x00000107u, 0x00000002u },
    { 0x0000010Du, 0x00008420u },
    { 0x00000500u, 0x00008428u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x00008478u },
    { 0x00000C0Fu, 0x00008484u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot09_info_setup_0_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot09_info_setup_0_paths_0_points[] = {
    { 2, 3, 4 },
    { 0, 7, 0 },
    { -31756, 0, 142 },
    { -2200, -2414, 6 },
    { -2200, -175, 24 },
    { -2776, -175, 37 },
    { -2776, 1900, 24 },
};

static const Oot3dPathRecord oot3d_spot09_info_setup_0_paths[] = {
    { 7u, 0u, 0u, 33780u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot09_info_setup_0_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot09_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { 2664, -269, 778 }, { 0, -29127, 0 }, 4095 },
    { ACTOR_PLAYER, { 187, -2828, 2447 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { -3804, 354, -1227 }, { 0, 16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -3262, 239, -761 }, { 0, 7281, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot09_info_setup_0_entrances[] = {
    { 0u, 0 },
    { 1u, 0 },
    { 2u, 0 },
    { 3u, 0 },
    { 4u, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot09_info_setup_0_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x0F, 0x8D, 0x01, 0x19, 0x02, 0x30, 0x01, 0x29, 0x01, 0xA0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0xBB, 0x46, 0x00, 0x07, 0x77, 0x68 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x44, 0x23, 0x33, 0x82, 0x54, 0x23, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x46, 0xD8, 0x06, 0x96, 0x82 } },
    { { 0x68, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x6D, 0x59, 0x3D, 0x96, 0x82, 0x63, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0xBB, 0x46, 0xF8, 0x06, 0x82, 0x49 } },
    { { 0x19, 0x48, 0x48, 0x48, 0xFF, 0xB5, 0x33, 0xB8, 0xB8, 0xB8, 0x4F, 0x14, 0x4F, 0x96, 0x3F, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0xA0, 0x8C, 0x46, 0x20, 0x07, 0x33, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0x33, 0x38, 0xB8, 0xB8, 0xB8, 0x4F, 0xB5, 0xFF, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x00, 0xFF, 0x33, 0x49 } },
    { { 0x3D, 0x00, 0x00, 0x00, 0x6D, 0xB5, 0x4F, 0x00, 0x00, 0x00, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0x3B, 0x46, 0xD8, 0xFE, 0x49, 0x82 } },
    { { 0x63, 0x00, 0x00, 0x00, 0xA0, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x1C, 0x46, 0xF8, 0xFE, 0x49, 0x49 } },
    { { 0x33, 0x00, 0x00, 0x00, 0x6D, 0x6D, 0x82, 0x00, 0x00, 0x00, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x20, 0xFF, 0x19, 0x38 } },
    { { 0x68, 0x00, 0x00, 0x00, 0x14, 0x28, 0x63, 0x00, 0x00, 0x00, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x54, 0x54 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD8, 0x06, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xD1, 0xD1, 0xD1, 0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x4C, 0x4C, 0x4C, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x59, 0x49 } },
    { { 0x49, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x3D, 0x33, 0x33, 0x3A, 0x28, 0x2D, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0x3B, 0x46, 0xD8, 0x06, 0x3D, 0x49 } },
};

static const Oot3dExitEntry oot3d_spot09_info_setup_0_exits[] = {
    { 397, 397u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 537, 537u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 304, 304u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 297, 297u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 928, 928u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot09_info_setup_0_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot09_info_setup_0_sound_settings[] = {
    { 1u, 1484u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot09_info_setup_0_misc_settings[] = {
    { 0u, 0x00000009u },
};

static const Oot3dSceneCommand oot3d_spot09_info_setup_1_commands[] = {
    { 0x00000115u, 0x010005CCu },
    { 0x00000104u, 0x000085D4u },
    { 0x00000019u, 0x00000009u },
    { 0x00000003u, 0x000083B4u },
    { 0x00000506u, 0x00008618u },
    { 0x00000107u, 0x00000002u },
    { 0x0000030Du, 0x00008738u },
    { 0x00000500u, 0x00008750u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x000087A0u },
    { 0x00000C0Fu, 0x000087ACu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot09_info_setup_1_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot09_info_setup_1_paths_0_points[] = {
    { 23, 0, -31130 },
    { 0, 12, 0 },
    { -30992, 0, 142 },
    { -2200, -2414, 6 },
    { -2200, -175, 24 },
    { -2776, -175, 37 },
    { -2776, 1900, 24 },
};

static const Oot3dVec3s oot3d_spot09_info_setup_1_paths_1_points[] = {
    { -3200, 1875, 437 },
    { -3215, 3018, 1336 },
    { -3240, 4167, -515 },
    { -2051, 110, 85 },
    { -1934, 372, 85 },
    { -2033, 526, 74 },
    { -2177, 618, 73 },
    { -2177, 615, 73 },
    { -2177, 615, 74 },
    { -2177, 618, 74 },
    { -2348, 654, 74 },
    { -2497, 636, 69 },
    { -2585, 555, 69 },
    { -2662, 454, 85 },
    { -2680, 314, -33 },
    { -2662, -169, -33 },
    { -2574, -295, -63 },
    { -2071, -304, 52 },
    { -2061, -2312, 52 },
    { -2000, -2517, 52 },
    { -1923, -2610, 36 },
    { -1616, -2638, -195 },
    { -1829, -108, -505 },
};

static const Oot3dVec3s oot3d_spot09_info_setup_1_paths_2_points[] = {
    { -2021, 74, -505 },
    { -2021, 74, -516 },
    { -2051, 111, 314 },
    { -1015, -2804, 314 },
    { -1094, -2672, 311 },
    { -2487, -2565, 288 },
    { -2546, -2439, 288 },
    { -2550, -2341, 237 },
    { -2511, -1263, 194 },
    { -2170, -304, 164 },
    { -2913, -241, -38 },
    { -3000, 433, -291 },
};

static const Oot3dPathRecord oot3d_spot09_info_setup_1_paths[] = {
    { 7u, 0u, 0u, 34364u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot09_info_setup_1_paths_0_points },
    { 23u, 0u, 0u, 34406u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot09_info_setup_1_paths_1_points },
    { 12u, 0u, 0u, 34544u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot09_info_setup_1_paths_2_points },
};

static const Oot3dActorEntry oot3d_spot09_info_setup_1_spawns[] = {
    { ACTOR_PLAYER, { 2664, -269, 778 }, { 0, -29127, 0 }, 4095 },
    { ACTOR_PLAYER, { -1025, 15, -355 }, { 0, 20753, 0 }, 2047 },
    { ACTOR_PLAYER, { -3804, 354, -1227 }, { 0, 16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -3264, 239, -757 }, { 0, 7281, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot09_info_setup_1_entrances[] = {
    { 0u, 0 },
    { 1u, 0 },
    { 2u, 0 },
    { 3u, 0 },
    { 4u, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot09_info_setup_1_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x0F, 0x8D, 0x01, 0x19, 0x02, 0x30, 0x01, 0x29, 0x01, 0xA0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0xBB, 0x46, 0x00, 0x07, 0x77, 0x68 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x44, 0x23, 0x33, 0x82, 0x54, 0x23, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x46, 0xD8, 0x06, 0x96, 0x82 } },
    { { 0x68, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x6D, 0x59, 0x3D, 0x96, 0x82, 0x63, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0xBB, 0x46, 0xF8, 0x06, 0x82, 0x49 } },
    { { 0x19, 0x48, 0x48, 0x48, 0xFF, 0xB5, 0x33, 0xB8, 0xB8, 0xB8, 0x4F, 0x14, 0x4F, 0x96, 0x3F, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0xA0, 0x8C, 0x46, 0x20, 0x07, 0x33, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0x33, 0x38, 0xB8, 0xB8, 0xB8, 0x4F, 0xB5, 0xFF, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x00, 0xFF, 0x33, 0x49 } },
    { { 0x3D, 0x00, 0x00, 0x00, 0x6D, 0xB5, 0x4F, 0x00, 0x00, 0x00, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0x3B, 0x46, 0xD8, 0xFE, 0x49, 0x82 } },
    { { 0x63, 0x00, 0x00, 0x00, 0xA0, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x1C, 0x46, 0xF8, 0xFE, 0x49, 0x49 } },
    { { 0x33, 0x00, 0x00, 0x00, 0x6D, 0x6D, 0x82, 0x00, 0x00, 0x00, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x20, 0xFF, 0x19, 0x38 } },
    { { 0x68, 0x00, 0x00, 0x00, 0x14, 0x28, 0x63, 0x00, 0x00, 0x00, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x54, 0x54 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD8, 0x06, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xD1, 0xD1, 0xD1, 0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x4C, 0x4C, 0x4C, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x59, 0x49 } },
    { { 0x49, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x3D, 0x33, 0x33, 0x3A, 0x28, 0x2D, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0x3B, 0x46, 0xD8, 0x06, 0x3D, 0x49 } },
};

static const Oot3dExitEntry oot3d_spot09_info_setup_1_exits[] = {
    { 397, 397u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 537, 537u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 304, 304u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 297, 297u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 928, 928u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot09_info_setup_1_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot09_info_setup_1_sound_settings[] = {
    { 1u, 1484u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot09_info_setup_1_misc_settings[] = {
    { 0u, 0x00000009u },
};

static const Oot3dSceneCommand oot3d_spot09_info_setup_2_commands[] = {
    { 0x00130115u, 0x010005BAu },
    { 0x00000104u, 0x000088FCu },
    { 0x00000019u, 0x00000009u },
    { 0x00000003u, 0x000083B4u },
    { 0x00000106u, 0x00008940u },
    { 0x00000007u, 0x00000002u },
    { 0x0000010Du, 0x00008964u },
    { 0x00000100u, 0x0000896Cu },
    { 0x00000011u, 0x00010001u },
    { 0x00000013u, 0x0000897Cu },
    { 0x0000030Fu, 0x00008984u },
    { 0x00000017u, 0x000089D8u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot09_info_setup_2_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot09_info_setup_2_paths_0_points[] = {
    { 0, 0, 0 },
    { 0, 4, 0 },
    { -30388, 0, 142 },
    { -2761, -2414, 6 },
};

static const Oot3dPathRecord oot3d_spot09_info_setup_2_paths[] = {
    { 4u, 0u, 0u, 35148u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot09_info_setup_2_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot09_info_setup_2_spawns[] = {
    { 822, { 368, -2784, 3130 }, { 4, 0, -30388 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot09_info_setup_2_entrances[] = {
    { 0u, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot09_info_setup_2_light_settings[] = {
    { { 0x00, 0x00, 0x54, 0x55, 0x00, 0x00, 0xFF, 0x00, 0x8D, 0x01, 0x19, 0x02, 0x23, 0x01, 0x29, 0x01, 0x00, 0x00, 0xC8, 0x42, 0x00, 0x00, 0xC8, 0x42, 0xBC, 0xFE, 0x03, 0x0A } },
    { { 0x08, 0xD8, 0x22, 0x8D, 0xFF, 0xED, 0xC8, 0x5B, 0x54, 0x0D, 0x6D, 0x5B, 0x49, 0xBE, 0xA8, 0x7D, 0x00, 0x18, 0xA6, 0x45, 0x00, 0x18, 0xA6, 0x45, 0xBC, 0x06, 0x03, 0x0A } },
    { { 0x08, 0xD8, 0x22, 0x8D, 0xFF, 0xED, 0xC8, 0x5B, 0x54, 0x0D, 0x6D, 0x5B, 0x49, 0xBE, 0xA8, 0x7D, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1C, 0xFF, 0x59, 0x46 } },
};

static const Oot3dExitEntry oot3d_spot09_info_setup_2_exits[] = {
    { 397, 397u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 537, 537u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 291, 291u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 297, 297u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot09_info_setup_2_skybox_settings[] = {
    { 1u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot09_info_setup_2_sound_settings[] = {
    { 1u, 1466u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot09_info_setup_2_cutscenes[] = {
    { 0x000089D8u, 1u },
};

static const Oot3dMiscSettings oot3d_spot09_info_setup_2_misc_settings[] = {
    { 0u, 0x00000009u },
};

static const Oot3dSceneCommand oot3d_spot09_info_setup_3_commands[] = {
    { 0x00130115u, 0x010005BAu },
    { 0x00000104u, 0x000091C8u },
    { 0x00000019u, 0x00000009u },
    { 0x00000003u, 0x000083B4u },
    { 0x00000106u, 0x0000920Cu },
    { 0x00000007u, 0x00000002u },
    { 0x0000010Du, 0x00009230u },
    { 0x00000100u, 0x00009238u },
    { 0x00000011u, 0x00010001u },
    { 0x00000013u, 0x00009248u },
    { 0x0000020Fu, 0x00009250u },
    { 0x00000017u, 0x00009288u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot09_info_setup_3_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot09_info_setup_3_paths_0_points[] = {
    { 0, 0, 0 },
    { 114, 4, 0 },
    { -28136, 0, 142 },
    { -2761, -2414, 6 },
};

static const Oot3dPathRecord oot3d_spot09_info_setup_3_paths[] = {
    { 4u, 0u, 0u, 37400u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot09_info_setup_3_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot09_info_setup_3_spawns[] = {
    { 822, { 368, -2784, 3130 }, { 4, 0, -28136 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot09_info_setup_3_entrances[] = {
    { 0u, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot09_info_setup_3_light_settings[] = {
    { { 0x00, 0x00, 0x54, 0x55, 0x00, 0x00, 0xFF, 0x00, 0x8D, 0x01, 0x19, 0x02, 0x23, 0x01, 0x29, 0x01, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x47, 0xC8, 0xFE, 0x59, 0x46 } },
    { { 0x00, 0xAD, 0xEF, 0x5C, 0xFF, 0xD6, 0x46, 0x46, 0xCD, 0xA6, 0x8C, 0x8C, 0x0A, 0xBE, 0xA8, 0x7D, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1C, 0xFF, 0x03, 0x0A } },
};

static const Oot3dExitEntry oot3d_spot09_info_setup_3_exits[] = {
    { 397, 397u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 537, 537u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 291, 291u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 297, 297u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot09_info_setup_3_skybox_settings[] = {
    { 1u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot09_info_setup_3_sound_settings[] = {
    { 1u, 1466u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot09_info_setup_3_cutscenes[] = {
    { 0x00009288u, 1u },
};

static const Oot3dMiscSettings oot3d_spot09_info_setup_3_misc_settings[] = {
    { 0u, 0x00000009u },
};

static const Oot3dSceneCommand oot3d_spot09_info_setup_4_commands[] = {
    { 0x00130115u, 0x010005D4u },
    { 0x00000104u, 0x00009AA8u },
    { 0x00000019u, 0x00000009u },
    { 0x00000003u, 0x000083B4u },
    { 0x00000106u, 0x00009AECu },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x00009AF0u },
    { 0x00000011u, 0x00010001u },
    { 0x00000013u, 0x00009B00u },
    { 0x0000010Fu, 0x00009B0Cu },
    { 0x00000017u, 0x00009B28u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot09_info_setup_4_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot09_info_setup_4_spawns[] = {
    { ACTOR_PLAYER, { 0, 0, 0 }, { 0, 0, 0 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot09_info_setup_4_entrances[] = {
    { 0u, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot09_info_setup_4_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x0F, 0x8D, 0x01, 0x19, 0x02, 0x30, 0x01, 0x29, 0x01, 0xA0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0xE8, 0x00, 0x47, 0x1C, 0xFF, 0x3A, 0x40 } },
};

static const Oot3dExitEntry oot3d_spot09_info_setup_4_exits[] = {
    { 397, 397u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 537, 537u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 304, 304u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 297, 297u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 928, 928u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot09_info_setup_4_skybox_settings[] = {
    { 1u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot09_info_setup_4_sound_settings[] = {
    { 1u, 1492u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot09_info_setup_4_cutscenes[] = {
    { 0x00009B28u, 1u },
};

static const Oot3dMiscSettings oot3d_spot09_info_setup_4_misc_settings[] = {
    { 0u, 0x00000009u },
};

static const Oot3dActorEntry oot3d_spot09_info_spot09_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 138, -2528, -2396 }, { 0, 0, 0 }, 3 },
    { ACTOR_EN_RIVER_SOUND, { 527, -3122, 3582 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_RIVER_SOUND, { 1686, -138, -220 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_OKUTA, { -169, -2800, 796 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_OKUTA, { -70, -2200, -1436 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_OKUTA, { -59, -2800, 1603 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_OKUTA, { -48, -2200, -498 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_OKUTA, { -37, -2200, -1071 }, { 0, 0, 0 }, -256 },
    { ACTOR_BG_SPOT09_OBJ, { -1105, 15, -746 }, { 0, 3277, 0 }, 3 },
    { ACTOR_BG_SPOT09_OBJ, { 0, 0, 0 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_SPOT09_OBJ, { 0, 0, 0 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_SPOT09_OBJ, { 0, 0, 0 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_SPOT09_OBJ, { 0, 0, 0 }, { 0, 0, 0 }, 4 },
    { ACTOR_EN_ITEM00, { 35, -1630, -2780 }, { 0, 0, 0 }, 262 },
    { ACTOR_EN_ITEM00, { -350, -555, 1480 }, { 0, 0, 0 }, 518 },
    { ACTOR_OBJ_HAMISHI, { -1351, 69, 766 }, { 0, 0, 0 }, 11 },
    { ACTOR_OBJ_HAMISHI, { 861, 81, -778 }, { 0, 0, 0 }, 4 },
    { ACTOR_OBJ_HAMISHI, { 735, 5, 375 }, { 0, 0, 0 }, 5 },
    { ACTOR_OBJ_HAMISHI, { -1695, 70, -350 }, { 0, 0, 0 }, 6 },
    { ACTOR_OBJ_HAMISHI, { -1001, 52, 637 }, { 0, 0, 0 }, 7 },
    { ACTOR_OBJ_HAMISHI, { -1291, 65, 786 }, { 0, 0, 0 }, 9 },
    { ACTOR_OBJ_HAMISHI, { -1416, 59, 778 }, { 0, 0, 0 }, 13 },
    { ACTOR_OBJ_HAMISHI, { -1256, 55, 856 }, { 0, 0, 0 }, 16 },
    { ACTOR_DOOR_ANA, { 280, -555, 1470 }, { 0, -32767, 6 }, 242 },
    { ACTOR_DOOR_ANA, { -1323, 15, -969 }, { 0, -27307, 10 }, 496 },
    { ACTOR_EN_ISHI, { 280, -555, 1470 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_TORYO, { -855, 15, -391 }, { 0, 0, 0 }, -1 },
    { ACTOR_OBJ_BOMBIWA, { 545, 27, -510 }, { 0, 0, 0 }, 10 },
    { ACTOR_OBJ_BOMBIWA, { -954, 35, 577 }, { 0, 0, 0 }, 12 },
    { ACTOR_OBJ_BOMBIWA, { 751, -20, 569 }, { 0, 0, 0 }, 15 },
    { ACTOR_OBJ_KIBAKO2, { -350, -555, 1480 }, { 0, 0, 0 }, -1 },
    { ACTOR_OBJ_BEAN, { -515, -2051, 110 }, { 0, -16384, 2 }, 259 },
    { ACTOR_EN_GOROIWA, { 314, -1015, -2803 }, { 0, 0, 1 }, 3074 },
    { ACTOR_EN_ISHI, { 1558, -202, -63 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_ISHI, { 1605, -202, 26 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_ISHI, { 1686, -202, -33 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_ISHI, { -666, 12, -899 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_ISHI, { -526, 10, -890 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_ISHI, { -607, 16, -791 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_ISHI, { -458, 14, -781 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_WONDER_ITEM, { -45, -2365, -298 }, { 0, 0, 1 }, 4799 },
    { ACTOR_EN_WONDER_ITEM, { 47, -1793, -2620 }, { 0, 0, 1 }, 4799 },
    { ACTOR_EN_ISHI, { 2737, -235, 297 }, { 0, 0, 0 }, 2816 },
    { ACTOR_EN_ISHI, { 2715, -235, 316 }, { 0, 0, 0 }, 2816 },
    { ACTOR_EN_ISHI, { 2698, -226, 275 }, { 0, 0, 0 }, 2816 },
    { ACTOR_EN_GS, { -510, -2030, -2340 }, { 0, 0, 0 }, 14353 },
};

static const Oot3dRoomObjectEntry oot3d_spot09_info_spot09_0_info_objects[] = {
    { OBJECT_SPOT09_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TORYO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HORSE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OKUTA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KIBAKO2, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_spot09_info_room_refs[] = {
    { "spot09_0_info.zsi", 0 },
};

static const Oot3dSceneSetupIndex oot3d_spot09_info_setups[] = {
    { 0u, oot3d_spot09_info_setup_0_commands, 12u, oot3d_spot09_info_setup_0_special_files, 1u, oot3d_spot09_info_setup_0_paths, 1u, NULL, 0u, oot3d_spot09_info_setup_0_spawns, 4u, oot3d_spot09_info_setup_0_entrances, 5u, NULL, 0u, oot3d_spot09_info_setup_0_light_settings, 12u, oot3d_spot09_info_setup_0_exits, 6u, oot3d_spot09_info_setup_0_skybox_settings, 1u, oot3d_spot09_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_spot09_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_spot09_info_setup_1_commands, 12u, oot3d_spot09_info_setup_1_special_files, 1u, oot3d_spot09_info_setup_1_paths, 3u, NULL, 0u, oot3d_spot09_info_setup_1_spawns, 4u, oot3d_spot09_info_setup_1_entrances, 5u, NULL, 0u, oot3d_spot09_info_setup_1_light_settings, 12u, oot3d_spot09_info_setup_1_exits, 6u, oot3d_spot09_info_setup_1_skybox_settings, 1u, oot3d_spot09_info_setup_1_sound_settings, 1u, NULL, 0u, oot3d_spot09_info_setup_1_misc_settings, 1u },
    { 2u, oot3d_spot09_info_setup_2_commands, 13u, oot3d_spot09_info_setup_2_special_files, 1u, oot3d_spot09_info_setup_2_paths, 1u, NULL, 0u, oot3d_spot09_info_setup_2_spawns, 1u, oot3d_spot09_info_setup_2_entrances, 1u, NULL, 0u, oot3d_spot09_info_setup_2_light_settings, 3u, oot3d_spot09_info_setup_2_exits, 4u, oot3d_spot09_info_setup_2_skybox_settings, 1u, oot3d_spot09_info_setup_2_sound_settings, 1u, oot3d_spot09_info_setup_2_cutscenes, 1u, oot3d_spot09_info_setup_2_misc_settings, 1u },
    { 3u, oot3d_spot09_info_setup_3_commands, 13u, oot3d_spot09_info_setup_3_special_files, 1u, oot3d_spot09_info_setup_3_paths, 1u, NULL, 0u, oot3d_spot09_info_setup_3_spawns, 1u, oot3d_spot09_info_setup_3_entrances, 1u, NULL, 0u, oot3d_spot09_info_setup_3_light_settings, 2u, oot3d_spot09_info_setup_3_exits, 4u, oot3d_spot09_info_setup_3_skybox_settings, 1u, oot3d_spot09_info_setup_3_sound_settings, 1u, oot3d_spot09_info_setup_3_cutscenes, 1u, oot3d_spot09_info_setup_3_misc_settings, 1u },
    { 4u, oot3d_spot09_info_setup_4_commands, 12u, oot3d_spot09_info_setup_4_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot09_info_setup_4_spawns, 1u, oot3d_spot09_info_setup_4_entrances, 1u, NULL, 0u, oot3d_spot09_info_setup_4_light_settings, 1u, oot3d_spot09_info_setup_4_exits, 6u, oot3d_spot09_info_setup_4_skybox_settings, 1u, oot3d_spot09_info_setup_4_sound_settings, 1u, oot3d_spot09_info_setup_4_cutscenes, 1u, oot3d_spot09_info_setup_4_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_spot09_info_rooms[] = {
    { "spot09_0_info.zsi", 0, oot3d_spot09_info_spot09_0_info_objects, 12u, oot3d_spot09_info_spot09_0_info_actors, 46u },
};

const Oot3dSceneIndex oot3d_scene_index_spot09_info = {
    "spot09_info.zsi",
    oot3d_spot09_info_room_refs, 1u,
    oot3d_spot09_info_rooms, 1u,
    oot3d_spot09_info_setups, 5u,
};
