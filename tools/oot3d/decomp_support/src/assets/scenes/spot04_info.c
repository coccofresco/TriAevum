/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: spot04_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_spot04_info_setup_0_commands[] = {
    { 0x00040115u, 0x010005A9u },
    { 0x00000304u, 0x0000059Cu },
    { 0x0000020Eu, 0x00000668u },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000C06u, 0x00016D38u },
    { 0x00000107u, 0x00000002u },
    { 0x0000030Du, 0x00016DA0u },
    { 0x00000C00u, 0x00016DB8u },
    { 0x00000011u, 0x0000001Du },
    { 0x00000013u, 0x00016E78u },
    { 0x00000C0Fu, 0x00016E90u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_0_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot04_info_setup_0_paths_0_points[] = {
    { 2, 0, 28020 },
    { 1, 5, 0 },
};

static const Oot3dVec3s oot3d_spot04_info_setup_0_paths_1_points[] = {
    { 28032, 1, -1474 },
    { -80, -295, -1416 },
};

static const Oot3dVec3s oot3d_spot04_info_setup_0_paths_2_points[] = {
    { -74, -138, 1522 },
    { 0, 105, 1412 },
    { 0, 211, -247 },
    { 120, 1869, -247 },
    { 120, 1538, -575 },
};

static const Oot3dPathRecord oot3d_spot04_info_setup_0_paths[] = {
    { 2u, 0u, 0u, 93544u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot04_info_setup_0_paths_0_points },
    { 2u, 0u, 0u, 93556u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot04_info_setup_0_paths_1_points },
    { 5u, 0u, 0u, 93568u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot04_info_setup_0_paths_2_points },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { -68, -80, 941 }, { 0, 25486, 0 }, 4095 },
    { ACTOR_PLAYER, { 3896, -153, -1184 }, { 0, -9101, 0 }, 3845 },
    { ACTOR_PLAYER, { -1413, -74, -283 }, { 0, 16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -31, 100, 1073 }, { 0, -32767, 0 }, 3583 },
    { ACTOR_PLAYER, { 854, 0, -272 }, { 0, 0, 0 }, 3587 },
    { ACTOR_PLAYER, { -1034, 120, 394 }, { 0, 12743, 0 }, 3584 },
    { ACTOR_PLAYER, { -314, 380, -1362 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -40, 344, 1244 }, { 0, 26396, 0 }, 3583 },
    { ACTOR_PLAYER, { 1036, 0, 524 }, { 0, -27307, 0 }, 3594 },
    { ACTOR_PLAYER, { -445, 0, -486 }, { 0, 0, 0 }, 3586 },
    { ACTOR_PLAYER, { 516, 0, 629 }, { 0, -32767, 0 }, 3585 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_0_entrances[] = {
    { 0u, 0 },
    { 1u, 1 },
    { 2u, 0 },
    { 3u, 0 },
    { 4u, 0 },
    { 5u, 0 },
    { 6u, 0 },
    { 7u, 0 },
    { 8u, 0 },
    { 9u, 0 },
    { 10u, 0 },
    { 11u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_0_light_settings[] = {
    { { 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0xEE, 0x00, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x00, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0xA0, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x8C, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x44, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x3F, 0x3F, 0x63, 0xB8, 0xB8, 0xB8, 0x63, 0xAA, 0xDB, 0x33, 0x3D, 0x63, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x44, 0x54 } },
    { { 0x44, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x54, 0x8C } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x59, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x23, 0x3F } },
    { { 0x68, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x7A, 0x45, 0xC8, 0x04, 0x72, 0x72 } },
    { { 0x68, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0x9B, 0x00, 0x00, 0x00, 0x19, 0x33, 0x38, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x80, 0x89, 0x45, 0xC8, 0x04, 0x8C, 0x8C } },
    { { 0xA0, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x44, 0x44, 0x59, 0xB5, 0xB5, 0x9B, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x61, 0x45, 0xC8, 0x04, 0x82, 0x72 } },
    { { 0x72, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x48, 0x45, 0xC8, 0x04, 0x4F, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_0_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1504, 1504u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 626, 626u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_0_skybox_settings[] = {
    { 29u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_0_sound_settings[] = {
    { 1u, 1449u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_0_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_1_commands[] = {
    { 0x00040115u, 0x010005A9u },
    { 0x00000304u, 0x00016FE0u },
    { 0x0000020Eu, 0x000170ACu },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000D06u, 0x000170CCu },
    { 0x00000107u, 0x00000002u },
    { 0x0000010Du, 0x0001714Cu },
    { 0x00000D00u, 0x00017154u },
    { 0x00000011u, 0x0000001Du },
    { 0x00000013u, 0x00017224u },
    { 0x00000C0Fu, 0x0001723Cu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_1_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot04_info_setup_1_paths_0_points[] = {
    { 10, 11, 268 },
    { 0, 15, 0 },
    { 28912, 1, 1190 },
    { 0, -480, 1182 },
    { 97, -228, 683 },
    { 187, 475, 468 },
    { 244, 570, 359 },
    { 214, 734, -748 },
    { 237, -680, -528 },
    { 271, -880, -390 },
    { 269, -722, -296 },
    { 221, -507, -46 },
    { 176, -255, 425 },
    { 151, -181, 781 },
    { 208, -396, 1044 },
};

static const Oot3dPathRecord oot3d_spot04_info_setup_1_paths[] = {
    { 15u, 0u, 0u, 94448u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot04_info_setup_1_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_1_spawns[] = {
    { ACTOR_PLAYER, { -68, -80, 941 }, { 0, 25486, 0 }, 4095 },
    { ACTOR_PLAYER, { 3844, -161, -1080 }, { 0, -8191, 0 }, 3845 },
    { ACTOR_PLAYER, { -1413, -74, -283 }, { 0, 16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -31, 100, 1073 }, { 0, -32767, 0 }, 3583 },
    { ACTOR_PLAYER, { 854, 0, -272 }, { 0, 0, 0 }, 3587 },
    { ACTOR_PLAYER, { -1034, 120, 394 }, { 0, 12743, 0 }, 3584 },
    { ACTOR_PLAYER, { -314, 380, -1362 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -40, 344, 1244 }, { 0, 26396, 0 }, 3583 },
    { ACTOR_PLAYER, { 1036, 0, 524 }, { 0, -27307, 0 }, 3594 },
    { ACTOR_PLAYER, { -445, 0, -486 }, { 0, 0, 0 }, 3586 },
    { ACTOR_PLAYER, { 516, 0, 629 }, { 0, -32767, 0 }, 3585 },
    { ACTOR_PLAYER, { 1790, 0, 135 }, { 0, -17840, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_1_entrances[] = {
    { 0u, 0 },
    { 1u, 1 },
    { 2u, 0 },
    { 3u, 0 },
    { 4u, 0 },
    { 5u, 0 },
    { 6u, 0 },
    { 7u, 0 },
    { 8u, 0 },
    { 9u, 0 },
    { 10u, 0 },
    { 11u, 0 },
    { 12u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_1_light_settings[] = {
    { { 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0xEE, 0x00, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x00, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0xA0, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x8C, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x44, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x3F, 0x3F, 0x63, 0xB8, 0xB8, 0xB8, 0x63, 0xAA, 0xDB, 0x33, 0x3D, 0x63, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x44, 0x54 } },
    { { 0x44, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x54, 0x8C } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x59, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x23, 0x3F } },
    { { 0x68, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x7A, 0x45, 0xC8, 0x04, 0x72, 0x72 } },
    { { 0x68, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0x9B, 0x00, 0x00, 0x00, 0x19, 0x33, 0x38, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x80, 0x89, 0x45, 0xC8, 0x04, 0x8C, 0x8C } },
    { { 0xA0, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x44, 0x44, 0x59, 0xB5, 0xB5, 0x9B, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x61, 0x45, 0xC8, 0x04, 0x82, 0x72 } },
    { { 0x72, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x48, 0x45, 0xC8, 0x04, 0x4F, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_1_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1504, 1504u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 626, 626u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_1_skybox_settings[] = {
    { 29u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_1_sound_settings[] = {
    { 1u, 1449u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_1_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_2_commands[] = {
    { 0x00040115u, 0x010005A9u },
    { 0x00000304u, 0x0001738Cu },
    { 0x0000020Eu, 0x00017458u },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000D06u, 0x00017478u },
    { 0x00000107u, 0x00000002u },
    { 0x0000010Du, 0x000174F8u },
    { 0x00000D00u, 0x00017500u },
    { 0x00000011u, 0x0000001Du },
    { 0x00000013u, 0x000175D0u },
    { 0x00000C0Fu, 0x000175E8u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_2_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot04_info_setup_2_paths_0_points[] = {
    { 10, 11, 268 },
    { 0, 15, 0 },
    { 29852, 1, 1190 },
    { 0, -480, 1182 },
    { 97, -228, 683 },
    { 187, 475, 468 },
    { 244, 570, 359 },
    { 214, 734, -748 },
    { 237, -680, -528 },
    { 271, -880, -390 },
    { 269, -722, -296 },
    { 221, -507, -46 },
    { 176, -255, 425 },
    { 151, -181, 781 },
    { 208, -396, 1044 },
};

static const Oot3dPathRecord oot3d_spot04_info_setup_2_paths[] = {
    { 15u, 0u, 0u, 95388u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot04_info_setup_2_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_2_spawns[] = {
    { ACTOR_PLAYER, { -68, -80, 941 }, { 0, 25486, 0 }, 4095 },
    { ACTOR_PLAYER, { 3844, -161, -1080 }, { 0, -8191, 0 }, 3845 },
    { ACTOR_PLAYER, { -1413, -74, -283 }, { 0, 16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -31, 100, 1073 }, { 0, -32767, 0 }, 3583 },
    { ACTOR_PLAYER, { 854, 0, -272 }, { 0, 0, 0 }, 3587 },
    { ACTOR_PLAYER, { -1034, 120, 394 }, { 0, 12743, 0 }, 3584 },
    { ACTOR_PLAYER, { -314, 380, -1362 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -40, 344, 1244 }, { 0, 26396, 0 }, 3583 },
    { ACTOR_PLAYER, { 1036, 0, 524 }, { 0, -27307, 0 }, 3594 },
    { ACTOR_PLAYER, { -445, 0, -486 }, { 0, 0, 0 }, 3586 },
    { ACTOR_PLAYER, { 516, 0, 629 }, { 0, -32767, 0 }, 3585 },
    { ACTOR_PLAYER, { 1790, 0, 135 }, { 0, -17840, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_2_entrances[] = {
    { 0u, 0 },
    { 1u, 1 },
    { 2u, 0 },
    { 3u, 0 },
    { 4u, 0 },
    { 5u, 0 },
    { 6u, 0 },
    { 7u, 0 },
    { 8u, 0 },
    { 9u, 0 },
    { 10u, 0 },
    { 11u, 0 },
    { 12u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_2_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_2_light_settings[] = {
    { { 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0xEE, 0x00, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x00, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0xA0, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x8C, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x44, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x3F, 0x3F, 0x63, 0xB8, 0xB8, 0xB8, 0x63, 0xAA, 0xDB, 0x33, 0x3D, 0x63, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x44, 0x54 } },
    { { 0x44, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x54, 0x8C } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x59, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x23, 0x3F } },
    { { 0x68, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x7A, 0x45, 0xC8, 0x04, 0x72, 0x72 } },
    { { 0x68, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0x9B, 0x00, 0x00, 0x00, 0x19, 0x33, 0x38, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x80, 0x89, 0x45, 0xC8, 0x04, 0x8C, 0x8C } },
    { { 0xA0, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x44, 0x44, 0x59, 0xB5, 0xB5, 0x9B, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x61, 0x45, 0xC8, 0x04, 0x82, 0x72 } },
    { { 0x72, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x48, 0x45, 0xC8, 0x04, 0x4F, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_2_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1504, 1504u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 626, 626u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_2_skybox_settings[] = {
    { 29u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_2_sound_settings[] = {
    { 1u, 1449u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_2_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_3_commands[] = {
    { 0x00130115u, 0x010005BAu },
    { 0x00000304u, 0x00017738u },
    { 0x0000020Eu, 0x00017804u },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x00017824u },
    { 0x00000007u, 0x00000002u },
    { 0x00000100u, 0x00017828u },
    { 0x00000011u, 0x00010001u },
    { 0x00000013u, 0x00017838u },
    { 0x0000050Fu, 0x00017844u },
    { 0x00000017u, 0x000178D0u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_3_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_3_spawns[] = {
    { ACTOR_EN_HOLL, { 2160, -1, -148 }, { 0, 319, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_3_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_3_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_3_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x00, 0xEE, 0x00, 0x00, 0x00, 0x85, 0x01, 0xBB, 0x00, 0xC1, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0xBC, 0xFE, 0x4F, 0x4F } },
    { { 0x4F, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x46, 0x46, 0x59, 0xC8, 0xC8, 0x95, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0xBC, 0xFE, 0x13, 0x13 } },
    { { 0x28, 0xE6, 0x92, 0x37, 0x90, 0xA5, 0xFF, 0xD4, 0x2C, 0x92, 0x69, 0x4F, 0xAA, 0x0F, 0x0A, 0x05, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0xBC, 0x16, 0x34, 0x56 } },
    { { 0x02, 0xEE, 0x77, 0x27, 0x90, 0xA5, 0xFF, 0xD4, 0x2C, 0x92, 0x69, 0x4F, 0xAA, 0x0F, 0x0A, 0x05, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0xBC, 0x06, 0x7A, 0x75 } },
    { { 0xA3, 0xEE, 0x77, 0x27, 0xC3, 0xF4, 0xFF, 0xD4, 0x2C, 0x92, 0xFF, 0xFF, 0xFF, 0x9C, 0x93, 0x79, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0xBC, 0x0A, 0x71, 0x59 } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_3_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 389, 389u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 187, 187u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 193, 193u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 252, 252u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_3_skybox_settings[] = {
    { 1u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_3_sound_settings[] = {
    { 1u, 1466u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_3_cutscenes[] = {
    { 0x000178D0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_3_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_4_commands[] = {
    { 0x00130115u, 0x010005B8u },
    { 0x00000304u, 0x00017E10u },
    { 0x0000020Eu, 0x00017EDCu },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x00017EFCu },
    { 0x00000007u, 0x00000002u },
    { 0x00000100u, 0x00017F00u },
    { 0x00000011u, 0x0001001Du },
    { 0x00000013u, 0x00017F10u },
    { 0x0000040Fu, 0x00017F20u },
    { 0x00000017u, 0x00017F90u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_4_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_4_spawns[] = {
    { ACTOR_EN_HOLL, { 2160, -1, -148 }, { 0, 319, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_4_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_4_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_4_light_settings[] = {
    { { 0xEE, 0x00, 0x00, 0x00, 0x85, 0x01, 0x72, 0x02, 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0x9C, 0x00, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0xA0, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x8C, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x44, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_4_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 389, 389u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 626, 626u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 193, 193u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 201, 201u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 286, 286u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 156, 156u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_4_skybox_settings[] = {
    { 29u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_4_sound_settings[] = {
    { 1u, 1464u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_4_cutscenes[] = {
    { 0x00017F90u, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_4_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_5_commands[] = {
    { 0x00130115u, 0x010005B8u },
    { 0x00000304u, 0x00018DF0u },
    { 0x0000020Eu, 0x00018EBCu },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x00018EDCu },
    { 0x00000007u, 0x00000002u },
    { 0x00000100u, 0x00018EE0u },
    { 0x00000011u, 0x0001001Du },
    { 0x00000013u, 0x00018EF0u },
    { 0x0000040Fu, 0x00018F00u },
    { 0x00000017u, 0x00018F70u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_5_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_5_spawns[] = {
    { ACTOR_EN_HOLL, { 2160, -1, -148 }, { 0, 319, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_5_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_5_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_5_light_settings[] = {
    { { 0xEE, 0x00, 0x00, 0x00, 0x85, 0x01, 0x72, 0x02, 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0x9C, 0x00, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x6B, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x8C, 0xA5 } },
    { { 0x59, 0x48, 0x48, 0x48, 0xCC, 0xFF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xD1, 0xD8, 0x72, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_5_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 389, 389u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 626, 626u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 193, 193u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 201, 201u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 286, 286u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 156, 156u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_5_skybox_settings[] = {
    { 29u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_5_sound_settings[] = {
    { 1u, 1464u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_5_cutscenes[] = {
    { 0x00018F70u, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_5_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_6_commands[] = {
    { 0x00130115u, 0x010005B8u },
    { 0x00000304u, 0x0001BBE0u },
    { 0x0000020Eu, 0x0001BCACu },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x0001BCCCu },
    { 0x00000007u, 0x00000002u },
    { 0x0000020Du, 0x0001BCF8u },
    { 0x00000100u, 0x0001BD08u },
    { 0x00000011u, 0x0000001Du },
    { 0x00000013u, 0x0001BD18u },
    { 0x0000080Fu, 0x0001BD2Cu },
    { 0x00000017u, 0x0001BE0Cu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_6_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot04_info_setup_6_paths_0_points[] = {
    { 2, 0, -17184 },
    { 1, 2, 0 },
};

static const Oot3dVec3s oot3d_spot04_info_setup_6_paths_1_points[] = {
    { -17172, 1, -1474 },
    { -80, -295, -1416 },
};

static const Oot3dPathRecord oot3d_spot04_info_setup_6_paths[] = {
    { 2u, 0u, 0u, 113888u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot04_info_setup_6_paths_0_points },
    { 2u, 0u, 0u, 113900u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot04_info_setup_6_paths_1_points },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_6_spawns[] = {
    { ACTOR_EN_TEST, { 0, -17184, 1 }, { 2, 0, -17172 }, 1 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_6_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_6_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_6_light_settings[] = {
    { { 0x85, 0x01, 0x72, 0x02, 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0xA0, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x8C, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x44, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x3F, 0x3F, 0x63, 0xB8, 0xB8, 0xB8, 0x63, 0xAA, 0xDB, 0x33, 0x3D, 0x63, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x44, 0x54 } },
    { { 0x44, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x54, 0x8C } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x59, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x23, 0x3F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_6_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_6_skybox_settings[] = {
    { 29u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_6_sound_settings[] = {
    { 1u, 1464u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_6_cutscenes[] = {
    { 0x0001BE0Cu, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_6_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_7_commands[] = {
    { 0x00040115u, 0x0000007Fu },
    { 0x00000304u, 0x0001F1DCu },
    { 0x0000030Eu, 0x0001F2A8u },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x0001F2D8u },
    { 0x00000007u, 0x00000002u },
    { 0x0000010Du, 0x0001F2F0u },
    { 0x00000100u, 0x0001F2F8u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x0001F308u },
    { 0x0000080Fu, 0x0001F31Cu },
    { 0x00000017u, 0x0001F3FCu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_7_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot04_info_setup_7_paths_0_points[] = {
    { 14957, 29487, 25955 },
    { 25966, 29487, 28528 },
    { 12404, 24372, 24369 },
    { 28265, 28518, 31278 },
    { 26995, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 28530, 14957, 29487 },
    { 25955, 25966, 29487 },
    { 28528, 12404, 24372 },
    { 24370, 28265, 28518 },
    { 31278, 26995, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, -256, -254 },
    { 35, -785, 120 },
    { 1219, 0, 319 },
    { -255, -256, 35 },
    { 2160, -1, -148 },
    { 0, 319, -280 },
    { 60, -816, -525 },
    { 60, -935, -766 },
    { 60, -815, -322 },
    { 60, -677, 119 },
    { 327, -990, 488 },
    { 320, -1157, -241 },
    { 380, -1406, -92 },
    { 380, -970, -539 },
    { 380, -1136, -500 },
    { 380, -1414, -738 },
    { 380, -1169, 100 },
    { 60, -509, -22 },
    { 60, -772, 262 },
    { 60, -528, -92 },
    { 200, -970, -24 },
    { 191, -772, 259 },
    { 320, -526, -538 },
    { 60, -1127, -731 },
    { 60, -1171, -1440 },
    { 60, -849, -642 },
    { 0, -325, -766 },
    { 0, -815, -1440 },
    { 0, -849, -324 },
    { 0, -677, 179 },
    { 0, -315, 100 },
    { 0, -509, 221 },
    { 0, -439, 262 },
    { 0, -528, 1068 },
    { 0, -682, 706 },
    { 0, -725, 652 },
    { 0, -333, 602 },
    { 0, -448, 511 },
    { 0, -552, 337 },
    { -27, -496, 471 },
    { -23, -486, 255 },
    { -79, 1275, 198 },
    { -78, 781, -200 },
    { -78, 762, -380 },
    { -79, 1178, -35 },
    { -79, 1476, -427 },
    { 120, 1018, -756 },
    { 119, 176, -972 },
    { 120, 864, -554 },
    { 120, 202, -209 },
    { 120, 776, -202 },
    { 120, 499, -1472 },
    { 120, -8, 258 },
    { 0, 499, -202 },
    { 0, 499, -519 },
    { 1, -117, -684 },
    { 0, -182, 180 },
    { 0, -29, -554 },
    { 0, 202, 292 },
    { 0, 31, 1388 },
    { 0, 872, 198 },
    { 0, 781, 255 },
    { 0, 1275, 564 },
    { 0, -51, 344 },
    { -60, -92, 480 },
    { 0, 61, 370 },
    { -61, -211, 400 },
    { -60, -399, 1756 },
    { 367, -246, 1378 },
    { 316, -321, 1529 },
    { 267, 23, 1502 },
    { 221, 198, 1465 },
    { 192, 310, 1777 },
    { 288, 360, -1472 },
    { -80, -8, -1468 },
    { -79, -371, 1756 },
    { 385, -716, 1391 },
    { 200, 876, -525 },
    { 201, -935, -538 },
    { 201, -1127, -282 },
    { 191, -816, 1916 },
    { 0, 223, 2050 },
    { 0, 42, 1916 },
    { 0, 63, 2221 },
    { 0, 186, 509 },
    { 320, -552, 726 },
    { 320, -756, 1516 },
    { 0, 23, 1516 },
    { 0, 183, 2263 },
    { 0, -193, 2050 },
    { 0, -189, 2055 },
    { 213, 42, 1943 },
    { 224, 60, 1926 },
    { 204, 129, 1940 },
    { 224, 220, 1449 },
    { 375, -571, 1284 },
    { 338, -470, 1054 },
    { 461, -682, 1343 },
    { 413, -711, 1206 },
    { 366, -594, 1206 },
    { 240, -594, 1284 },
    { 240, -470, 1449 },
    { 254, -571, 1343 },
    { 254, -711, 903 },
    { -23, 5, 1053 },
    { -59, 39, 1053 },
    { -58, -61, 914 },
    { 0, -38, 679 },
    { -3, -2, 910 },
    { -23, 62, 544 },
    { -23, -5, 613 },
    { 0, 89, 876 },
    { 0, 146, 640 },
    { -23, 29, 923 },
    { -38, 164, 1053 },
    { -60, 119, 983 },
    { -48, 179, 1173 },
    { 0, 119, 983 },
    { 0, 179, 1123 },
    { 0, 179, 1053 },
    { 0, 119, 923 },
    { -40, 269, 983 },
    { -48, 279, 900 },
    { 0, 375, 1305 },
    { -59, 211, 1123 },
    { -60, 279, 1382 },
    { -60, 282, 1123 },
    { -60, 179, 1232 },
    { -60, 179, 1173 },
    { -59, 119, 1292 },
    { -57, -20, 1093 },
    { -60, -61, 1232 },
    { -60, 0, 1173 },
    { -60, 39, 1093 },
    { -58, -101, 1414 },
    { -57, -255, 1158 },
    { 0, -261, 1053 },
    { -46, -101, 872 },
    { 0, 269, 1053 },
    { 0, -101, 1053 },
    { 0, -61, 1093 },
    { 0, -61, 1093 },
    { 0, -101, 1053 },
    { 0, 39, 1173 },
    { 0, 39, 923 },
    { 0, 269, 923 },
    { 0, 164, 983 },
    { 0, 279, 1123 },
    { 0, 279, 1232 },
    { -24, 0, 1232 },
    { -24, 179, 1292 },
    { 0, -20, 1305 },
    { 0, 211, 1414 },
    { 0, -255, 1382 },
    { 1, 282, 1123 },
    { 0, 379, 1023 },
    { 0, 379, 1023 },
    { 0, 419, 1123 },
    { 0, 419, 1398 },
    { 4, 399, 1123 },
    { -40, 379, 1023 },
    { -40, 379, 255 },
    { 274, 1275, 1391 },
    { 280, 876, -1458 },
    { 120, 751, -1458 },
    { 299, 751, -858 },
    { 299, 1054, -861 },
    { 120, 1054, 726 },
    { 547, -756, 488 },
    { 566, -1157, -215 },
    { 581, -1423, -731 },
    { 500, -1171, -491 },
    { 582, -1416, -1435 },
    { 415, -849, -35 },
    { 245, 1476, -383 },
    { 120, 1178, -380 },
    { 287, 1178, -1466 },
    { 51, -197, -1467 },
    { -27, -196, -1471 },
    { -79, -215, -1455 },
    { 48, -404, -1466 },
    { -27, -402, -1543 },
    { 330, -294, -1457 },
    { 133, -302, -1463 },
    { 111, -366, -1464 },
    { 107, -226, -1456 },
    { 329, -16, -387 },
    { 380, -1403, -408 },
    { 424, -1396, -417 },
    { 511, -1376, -376 },
    { 567, -1408, -309 },
    { 590, -1408, -237 },
    { 560, -1408, -204 },
    { 508, -1372, -212 },
    { 418, -1405, 1911 },
    { 145, 82, 1908 },
    { 145, 204, 1916 },
    { 120, 223, 2227 },
    { 211, 186, 2269 },
    { 211, -193, 1916 },
    { 120, 63, 1519 },
    { 111, 183, 1524 },
    { 146, 153, 1522 },
    { 150, 74, 1535 },
    { 99, 34, 405 },
    { 268, -540, 365 },
};

static const Oot3dPathRecord oot3d_spot04_info_setup_7_paths[] = {
    { 228u, 242u, 1u, 1522u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot04_info_setup_7_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_7_spawns[] = {
    { ACTOR_EN_BB, { 1412, 0, 211 }, { 2, 0, -3356 }, 1 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_7_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_7_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1159 }, -32767, 319 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1279 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_7_light_settings[] = {
    { { 0x85, 0x01, 0x72, 0x02, 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x1C, 0xFF, 0x46, 0x2D } },
    { { 0x38, 0x48, 0x48, 0x48, 0xB3, 0x9A, 0x89, 0xB8, 0xB8, 0xB8, 0x13, 0x13, 0x3B, 0x1D, 0x0A, 0x0A, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x1C, 0xFF, 0x4F, 0x4F } },
    { { 0x4F, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x46, 0x46, 0x59, 0xC8, 0xC8, 0x95, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x1C, 0xFF, 0x77, 0x59 } },
    { { 0x00, 0x48, 0x48, 0x48, 0xF9, 0x87, 0x31, 0xB8, 0xB8, 0xB8, 0x1D, 0x1D, 0x3B, 0x1C, 0x13, 0x00, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x08, 0xFF, 0x1D, 0x28 } },
    { { 0x46, 0x48, 0x48, 0x48, 0x31, 0x31, 0x64, 0xB8, 0xB8, 0xB8, 0x64, 0x64, 0xA5, 0x13, 0x28, 0x3B, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x0E, 0xFF, 0x3B, 0x28 } },
    { { 0x46, 0x48, 0x48, 0x48, 0x4F, 0x1D, 0x3B, 0xB8, 0xB8, 0xB8, 0x4F, 0x31, 0x95, 0x46, 0x2A, 0x2D, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x0E, 0xFF, 0x4B, 0x59 } },
    { { 0x64, 0x48, 0x48, 0x48, 0x36, 0xFF, 0xEF, 0xB8, 0xB8, 0xB8, 0x0A, 0x95, 0xBD, 0x13, 0x59, 0x6E, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x0E, 0xFF, 0x3B, 0x28 } },
    { { 0x4F, 0x48, 0x48, 0x48, 0x3B, 0x4B, 0x95, 0xB8, 0xB8, 0xB8, 0x3B, 0x36, 0x95, 0x31, 0x1D, 0x1D, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x0E, 0xFF, 0x00, 0x28 } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_7_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_7_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_7_sound_settings[] = {
    { 1u, 127u, 0u, 0u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_7_cutscenes[] = {
    { 0x0001F3FCu, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_7_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_8_commands[] = {
    { 0x00040115u, 0x0000007Fu },
    { 0x00000304u, 0x0001F97Cu },
    { 0x0000030Eu, 0x0001FA48u },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x0001FA78u },
    { 0x00000007u, 0x00000002u },
    { 0x0000010Du, 0x0001FA90u },
    { 0x00000100u, 0x0001FA98u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x0001FAA8u },
    { 0x0000080Fu, 0x0001FABCu },
    { 0x00000017u, 0x0001FB9Cu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_8_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot04_info_setup_8_paths_0_points[] = {
    { 14957, 29487, 25955 },
    { 25966, 29487, 28528 },
    { 12404, 24372, 24369 },
    { 28265, 28518, 31278 },
    { 26995, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 28530, 14957, 29487 },
    { 25955, 25966, 29487 },
    { 28528, 12404, 24372 },
    { 24370, 28265, 28518 },
    { 31278, 26995, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, -256, -254 },
    { 35, -785, 120 },
    { 1219, 0, 319 },
    { -255, -256, 35 },
    { 2160, -1, -148 },
    { 0, 319, -280 },
    { 60, -816, -525 },
    { 60, -935, -766 },
    { 60, -815, -322 },
    { 60, -677, 119 },
    { 327, -990, 488 },
    { 320, -1157, -241 },
    { 380, -1406, -92 },
    { 380, -970, -539 },
    { 380, -1136, -500 },
    { 380, -1414, -738 },
    { 380, -1169, 100 },
    { 60, -509, -22 },
    { 60, -772, 262 },
    { 60, -528, -92 },
    { 200, -970, -24 },
    { 191, -772, 259 },
    { 320, -526, -538 },
    { 60, -1127, -731 },
    { 60, -1171, -1440 },
    { 60, -849, -642 },
    { 0, -325, -766 },
    { 0, -815, -1440 },
    { 0, -849, -324 },
    { 0, -677, 179 },
    { 0, -315, 100 },
    { 0, -509, 221 },
    { 0, -439, 262 },
    { 0, -528, 1068 },
    { 0, -682, 706 },
    { 0, -725, 652 },
    { 0, -333, 602 },
    { 0, -448, 511 },
    { 0, -552, 337 },
    { -27, -496, 471 },
    { -23, -486, 255 },
    { -79, 1275, 198 },
    { -78, 781, -200 },
    { -78, 762, -380 },
    { -79, 1178, -35 },
    { -79, 1476, -427 },
    { 120, 1018, -756 },
    { 119, 176, -972 },
    { 120, 864, -554 },
    { 120, 202, -209 },
    { 120, 776, -202 },
    { 120, 499, -1472 },
    { 120, -8, 258 },
    { 0, 499, -202 },
    { 0, 499, -519 },
    { 1, -117, -684 },
    { 0, -182, 180 },
    { 0, -29, -554 },
    { 0, 202, 292 },
    { 0, 31, 1388 },
    { 0, 872, 198 },
    { 0, 781, 255 },
    { 0, 1275, 564 },
    { 0, -51, 344 },
    { -60, -92, 480 },
    { 0, 61, 370 },
    { -61, -211, 400 },
    { -60, -399, 1756 },
    { 367, -246, 1378 },
    { 316, -321, 1529 },
    { 267, 23, 1502 },
    { 221, 198, 1465 },
    { 192, 310, 1777 },
    { 288, 360, -1472 },
    { -80, -8, -1468 },
    { -79, -371, 1756 },
    { 385, -716, 1391 },
    { 200, 876, -525 },
    { 201, -935, -538 },
    { 201, -1127, -282 },
    { 191, -816, 1916 },
    { 0, 223, 2050 },
    { 0, 42, 1916 },
    { 0, 63, 2221 },
    { 0, 186, 509 },
    { 320, -552, 726 },
    { 320, -756, 1516 },
    { 0, 23, 1516 },
    { 0, 183, 2263 },
    { 0, -193, 2050 },
    { 0, -189, 2055 },
    { 213, 42, 1943 },
    { 224, 60, 1926 },
    { 204, 129, 1940 },
    { 224, 220, 1449 },
    { 375, -571, 1284 },
    { 338, -470, 1054 },
    { 461, -682, 1343 },
    { 413, -711, 1206 },
    { 366, -594, 1206 },
    { 240, -594, 1284 },
    { 240, -470, 1449 },
    { 254, -571, 1343 },
    { 254, -711, 903 },
    { -23, 5, 1053 },
    { -59, 39, 1053 },
    { -58, -61, 914 },
    { 0, -38, 679 },
    { -3, -2, 910 },
    { -23, 62, 544 },
};

static const Oot3dPathRecord oot3d_spot04_info_setup_8_paths[] = {
    { 132u, 250u, 1u, 1522u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot04_info_setup_8_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_8_spawns[] = {
    { ACTOR_EN_BB, { 1412, 0, 211 }, { 2, 0, -1404 }, 1 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_8_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_8_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1159 }, -32767, 319 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1279 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_8_light_settings[] = {
    { { 0x85, 0x01, 0x72, 0x02, 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x1C, 0xFF, 0x46, 0x2D } },
    { { 0x38, 0x48, 0x48, 0x48, 0xB3, 0x9A, 0x89, 0xB8, 0xB8, 0xB8, 0x13, 0x13, 0x3B, 0x1D, 0x0A, 0x0A, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x1C, 0xFF, 0x4F, 0x4F } },
    { { 0x4F, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x46, 0x46, 0x59, 0xC8, 0xC8, 0x95, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x1C, 0xFF, 0x77, 0x59 } },
    { { 0x00, 0x48, 0x48, 0x48, 0xF9, 0x87, 0x31, 0xB8, 0xB8, 0xB8, 0x1D, 0x1D, 0x3B, 0x1C, 0x13, 0x00, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x08, 0xFF, 0x1D, 0x28 } },
    { { 0x46, 0x48, 0x48, 0x48, 0x31, 0x31, 0x64, 0xB8, 0xB8, 0xB8, 0x64, 0x64, 0xA5, 0x13, 0x28, 0x3B, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x0E, 0xFF, 0x3B, 0x28 } },
    { { 0x46, 0x48, 0x48, 0x48, 0x4F, 0x1D, 0x3B, 0xB8, 0xB8, 0xB8, 0x4F, 0x31, 0x95, 0x46, 0x2A, 0x2D, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x0E, 0xFF, 0x4B, 0x59 } },
    { { 0x64, 0x48, 0x48, 0x48, 0x36, 0xFF, 0xEF, 0xB8, 0xB8, 0xB8, 0x0A, 0x95, 0xBD, 0x13, 0x59, 0x6E, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x0E, 0xFF, 0x3B, 0x28 } },
    { { 0x4F, 0x48, 0x48, 0x48, 0x3B, 0x4B, 0x95, 0xB8, 0xB8, 0xB8, 0x3B, 0x36, 0x95, 0x31, 0x1D, 0x1D, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x40, 0xB5, 0x45, 0x0E, 0xFF, 0x00, 0x28 } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_8_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_8_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_8_sound_settings[] = {
    { 1u, 127u, 0u, 0u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_8_cutscenes[] = {
    { 0x0001FB9Cu, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_8_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_9_commands[] = {
    { 0x00130115u, 0x010005D4u },
    { 0x00000304u, 0x0001FD4Cu },
    { 0x0000020Eu, 0x0001FE18u },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x0001FE38u },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x0001FE3Cu },
    { 0x00000011u, 0x0001001Du },
    { 0x00000013u, 0x0001FE4Cu },
    { 0x0000040Fu, 0x0001FE60u },
    { 0x00000017u, 0x0001FED0u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_9_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_9_spawns[] = {
    { ACTOR_EN_HOLL, { 2160, -1, -148 }, { 0, 319, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_9_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_9_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_9_light_settings[] = {
    { { 0xDE, 0x04, 0x72, 0x02, 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0xA0, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x8C, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x44, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_9_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_9_skybox_settings[] = {
    { 29u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_9_sound_settings[] = {
    { 1u, 1492u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_9_cutscenes[] = {
    { 0x0001FED0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_9_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_10_commands[] = {
    { 0x00130115u, 0x010005D4u },
    { 0x00000304u, 0x00020210u },
    { 0x0000020Eu, 0x000202DCu },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x000202FCu },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x00020300u },
    { 0x00000011u, 0x0001001Du },
    { 0x00000013u, 0x00020310u },
    { 0x0000040Fu, 0x00020324u },
    { 0x00000017u, 0x00020394u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_10_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_10_spawns[] = {
    { ACTOR_EN_HOLL, { 2160, -1, -148 }, { 0, 319, 0 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_10_entrances[] = {
    { 0u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_10_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_10_light_settings[] = {
    { { 0xDE, 0x04, 0x72, 0x02, 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0xA0, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x8C, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x44, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_10_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_10_skybox_settings[] = {
    { 29u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_10_sound_settings[] = {
    { 1u, 1492u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_10_cutscenes[] = {
    { 0x00020394u, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_10_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_11_commands[] = {
    { 0x00040115u, 0x0000007Fu },
    { 0x00000304u, 0x00020744u },
    { 0x0000020Eu, 0x00020810u },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x00020830u },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x00020834u },
    { 0x00000011u, 0x0001001Du },
    { 0x00000013u, 0x00020844u },
    { 0x00000C0Fu, 0x00020858u },
    { 0x00000017u, 0x000209A8u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_11_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_11_spawns[] = {
    { ACTOR_EN_HOLL, { 2160, -1, -148 }, { 0, 319, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_11_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_11_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1219 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_11_light_settings[] = {
    { { 0xDE, 0x04, 0x72, 0x02, 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0xA0, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x8C, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x44, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x3F, 0x3F, 0x63, 0xB8, 0xB8, 0xB8, 0x63, 0xAA, 0xDB, 0x33, 0x3D, 0x63, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x44, 0x54 } },
    { { 0x44, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x54, 0x8C } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x59, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x23, 0x3F } },
    { { 0x68, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x7A, 0x45, 0xC8, 0x04, 0x72, 0x72 } },
    { { 0x68, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0x9B, 0x00, 0x00, 0x00, 0x19, 0x33, 0x38, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x80, 0x89, 0x45, 0xC8, 0x04, 0x8C, 0x8C } },
    { { 0xA0, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x44, 0x44, 0x59, 0xB5, 0xB5, 0x9B, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x61, 0x45, 0xC8, 0x04, 0x82, 0x72 } },
    { { 0x72, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x48, 0x45, 0xC8, 0x04, 0x4F, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_11_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_11_skybox_settings[] = {
    { 29u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_11_sound_settings[] = {
    { 1u, 127u, 0u, 0u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_11_cutscenes[] = {
    { 0x000209A8u, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_11_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dSceneCommand oot3d_spot04_info_setup_12_commands[] = {
    { 0x00040115u, 0x010005A9u },
    { 0x00000304u, 0x00022948u },
    { 0x0000030Eu, 0x00022A14u },
    { 0x00000019u, 0x00000004u },
    { 0x00000003u, 0x00016D0Cu },
    { 0x00000106u, 0x00022A44u },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x00022A48u },
    { 0x00000011u, 0x0001001Du },
    { 0x00000013u, 0x00022A58u },
    { 0x00000C0Fu, 0x00022A6Cu },
    { 0x00000017u, 0x00022BBCu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot04_info_setup_12_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot04_info_setup_12_spawns[] = {
    { ACTOR_EN_HOLL, { 2160, -1, -148 }, { 0, 319, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot04_info_setup_12_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot04_info_setup_12_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1159 }, -32767, 319 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -785, 120, 1279 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot04_info_setup_12_light_settings[] = {
    { { 0xDE, 0x04, 0x72, 0x02, 0xC1, 0x00, 0xC9, 0x00, 0x1E, 0x01, 0x9C, 0x00, 0x33, 0x04, 0x37, 0x04, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0xF8, 0xFE, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xF4, 0xEF, 0x82, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0xA0, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x8C, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x44, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x0C, 0xFF, 0x33, 0x4F } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x3F, 0x3F, 0x63, 0xB8, 0xB8, 0xB8, 0x63, 0xAA, 0xDB, 0x33, 0x3D, 0x63, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x44, 0x54 } },
    { { 0x44, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x54, 0x8C } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x59, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x23, 0x3F } },
    { { 0x68, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x7A, 0x45, 0xC8, 0x04, 0x72, 0x72 } },
    { { 0x68, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0x9B, 0x00, 0x00, 0x00, 0x19, 0x33, 0x38, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x80, 0x89, 0x45, 0xC8, 0x04, 0x8C, 0x8C } },
    { { 0xA0, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x44, 0x44, 0x59, 0xB5, 0xB5, 0x9B, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x61, 0x45, 0xC8, 0x04, 0x82, 0x72 } },
    { { 0x72, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x48, 0x45, 0xC8, 0x04, 0x4F, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot04_info_setup_12_exits[] = {
    { 238, 238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot04_info_setup_12_skybox_settings[] = {
    { 29u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot04_info_setup_12_sound_settings[] = {
    { 1u, 1449u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot04_info_setup_12_cutscenes[] = {
    { 0x00022BBCu, 1u },
};

static const Oot3dMiscSettings oot3d_spot04_info_setup_12_misc_settings[] = {
    { 0u, 0x00000004u },
};

static const Oot3dActorEntry oot3d_spot04_info_spot04_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 398, -29, -483 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_ITEM00, { -501, 1, 160 }, { 0, 0, 181 }, 25600 },
    { ACTOR_EN_ITEM00, { -424, 1, 213 }, { 0, 0, 0 }, 26368 },
    { ACTOR_EN_ITEM00, { 24, 1, -473 }, { 0, 0, 0 }, 25856 },
    { ACTOR_EN_ITEM00, { 137, 1, -453 }, { 0, 0, 0 }, 26112 },
    { ACTOR_EN_ITEM00, { -379, 59, -823 }, { 0, 0, 0 }, 4609 },
    { ACTOR_EN_ITEM00, { 2, 180, -45 }, { 0, 0, 0 }, 4353 },
    { ACTOR_OBJECT_KANKYO, { 355, 1, -150 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_KO, { -292, 0, -430 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_KO, { 45, 0, -272 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KO, { -608, 120, 1021 }, { 0, -24576, 0 }, -254 },
    { ACTOR_EN_KO, { -1471, -80, -294 }, { 0, 16384, 0 }, 3 },
    { ACTOR_EN_KO, { 669, 0, 521 }, { 0, -30948, 0 }, -252 },
    { ACTOR_EN_KO, { 853, 100, -311 }, { 0, 0, 0 }, -251 },
    { ACTOR_EN_KO, { -678, 1, -179 }, { 0, 14199, 0 }, -250 },
    { ACTOR_EN_GS, { -622, 380, -1223 }, { 0, 16384, 0 }, 14366 },
    { ACTOR_EN_KO, { -10, 180, -22 }, { 0, 12379, 0 }, -244 },
    { ACTOR_EN_MD, { 1521, 0, 105 }, { 0, -17476, 0 }, 256 },
    { ACTOR_DOOR_ANA, { -512, 380, -1224 }, { 0, 16384, 0 }, 300 },
    { ACTOR_EN_ITEM00, { 453, 216, 822 }, { 0, 0, 0 }, 7171 },
    { ACTOR_EN_ITEM00, { 514, 215, 742 }, { 0, 0, 0 }, 7683 },
    { ACTOR_EN_ITEM00, { 566, 213, 842 }, { 0, 0, 0 }, 7427 },
    { ACTOR_EN_A_OBJ, { -1008, 120, 479 }, { 0, 23665, 0 }, 15626 },
    { ACTOR_EN_A_OBJ, { -924, 120, 928 }, { 0, 13470, 0 }, 17162 },
    { ACTOR_EN_A_OBJ, { -779, 121, 424 }, { 0, -13653, 0 }, 4106 },
    { ACTOR_EN_A_OBJ, { -512, 0, -459 }, { 0, 6735, 0 }, 15370 },
    { ACTOR_EN_A_OBJ, { -170, 370, -1335 }, { 0, 27488, 0 }, 5130 },
    { ACTOR_EN_A_OBJ, { 436, 0, 601 }, { 0, -9647, 0 }, 16138 },
    { ACTOR_EN_A_OBJ, { 728, 0, -195 }, { 0, 7281, 0 }, 7690 },
    { ACTOR_EN_A_OBJ, { 1089, 0, 473 }, { 0, -21844, 0 }, 15882 },
    { ACTOR_EN_SW, { -1380, 166, 412 }, { 0, -20929, 0 }, -21246 },
    { ACTOR_OBJ_MAKEKINSUTA, { 1190, 0, -480 }, { 0, 0, 0 }, 19713 },
    { ACTOR_EN_WONDER_ITEM, { -488, 140, 600 }, { 0, -19843, 0 }, 6739 },
    { ACTOR_EN_WONDER_ITEM, { 1074, 0, 178 }, { 0, 0, 2 }, 10851 },
    { ACTOR_EN_WONDER_ITEM, { 1069, 0, 406 }, { 0, 0, 0 }, 14307 },
    { ACTOR_EN_WONDER_ITEM, { 1074, 0, -80 }, { 0, 0, 1 }, 14307 },
    { ACTOR_EN_WONDER_ITEM, { 188, 3, -198 }, { 0, 0, 1 }, 4064 },
    { ACTOR_EN_WONDER_ITEM, { 548, 3, -158 }, { 0, 0, 0 }, 4064 },
    { ACTOR_EN_WONDER_ITEM, { 364, 0, 28 }, { 0, 0, 2 }, 608 },
    { ACTOR_EN_WONDER_ITEM, { -746, 165, 951 }, { 0, 0, 1 }, 4628 },
    { ACTOR_EN_WONDER_ITEM, { -698, 166, 830 }, { 0, 0, 1 }, 4629 },
    { ACTOR_EN_WONDER_ITEM, { -676, 166, 899 }, { 0, 0, 1 }, 4694 },
    { ACTOR_OBJ_HANA, { -915, 120, 871 }, { 0, 0, 0 }, 1 },
    { ACTOR_OBJ_HANA, { -896, 120, 826 }, { 0, 0, 0 }, 1 },
    { ACTOR_OBJ_HANA, { -584, 120, 963 }, { 0, 0, 0 }, 1 },
    { ACTOR_OBJ_HANA, { -292, 0, -415 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_ISHI, { -1388, 120, 72 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_ISHI, { -671, 0, -623 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_ISHI, { 337, 0, 542 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_ISHI, { 726, 0, 961 }, { 0, 0, 0 }, 512 },
    { ACTOR_OBJ_MURE2, { -292, 0, -415 }, { 0, 0, 0 }, 514 },
    { ACTOR_EN_KANBAN, { -1431, -66, -426 }, { 0, 16384, 0 }, 800 },
    { ACTOR_EN_KANBAN, { -845, 120, 1018 }, { 0, -32767, 0 }, 823 },
    { ACTOR_EN_KANBAN, { -784, 120, 1675 }, { 0, -32767, 0 }, 832 },
    { ACTOR_EN_KANBAN, { -538, 120, 718 }, { 0, -19661, 0 }, 824 },
    { ACTOR_EN_KANBAN, { -494, 120, 598 }, { 0, -19478, 0 }, 822 },
    { ACTOR_EN_KANBAN, { 49, -80, 966 }, { 0, -32767, 0 }, 799 },
    { ACTOR_EN_KANBAN, { 607, 0, -80 }, { 0, 12378, 0 }, 833 },
    { ACTOR_EN_KANBAN, { 871, 0, 311 }, { 0, -14564, 0 }, 786 },
    { ACTOR_OBJ_HANA, { 668, 0, 500 }, { 0, 1820, 0 }, 2 },
    { ACTOR_EN_KUSA, { -835, 120, 605 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { -823, 120, 666 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { -756, 120, 708 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { -748, 120, 632 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { -671, 120, 671 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { -612, 120, 736 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { -523, 120, 771 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { -498, 120, 696 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { 296, 0, 659 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { 572, 0, 603 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { 594, 0, 542 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { 678, 0, 596 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_SA, { 18, -80, 873 }, { 0, -7281, 0 }, 0 },
    { ACTOR_OBJ_BEAN, { 1190, 0, -480 }, { 0, 0, 0 }, 7945 },
    { ACTOR_EN_WONDER_TALK2, { 861, 34, -340 }, { 0, 0, 3 }, 17951 },
    { ACTOR_EN_WONDER_TALK2, { -915, 130, 871 }, { 0, 11832, 0 }, -1 },
    { ACTOR_EN_WONDER_TALK2, { -896, 130, 826 }, { 0, 11832, 0 }, -1 },
    { ACTOR_EN_WONDER_TALK2, { -584, 130, 963 }, { 0, -20024, 0 }, -1 },
    { ACTOR_OBJECT_KANKYO, { 390, -12, -498 }, { 0, 0, 0 }, 6 },
};

static const Oot3dRoomObjectEntry oot3d_spot04_info_spot04_0_info_objects[] = {
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { 404, OOT3D_ROOM_OBJECT_UNKNOWN_OOT3D_ID },
};

static const Oot3dActorEntry oot3d_spot04_info_spot04_1_info_actors[] = {
    { ACTOR_OBJECT_KANKYO, { 3408, -143, -818 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_KAREBABA, { 2108, -1, -317 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_KAREBABA, { 2236, -1, -291 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_KAREBABA, { 2293, -1, -496 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_KAREBABA, { 3074, -149, -1770 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_KAREBABA, { 4433, -149, -600 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_TREEMOUTH, { 3882, -171, -1161 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_GS, { 3792, -149, -2185 }, { 0, -12743, 0 }, 14623 },
    { ACTOR_EN_GS, { 4983, -149, -978 }, { 0, 25486, 0 }, 14880 },
};

static const Oot3dRoomObjectEntry oot3d_spot04_info_spot04_1_info_objects[] = {
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SPOT04_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEKUBABA, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot04_info_spot04_2_info_actors[] = {
    { ACTOR_EN_ITEM00, { -1009, 120, 1556 }, { 0, 0, 0 }, 3841 },
    { ACTOR_EN_ITEM00, { -711, 120, 1856 }, { 0, 0, 0 }, 3585 },
    { ACTOR_EN_KUSA, { -995, 120, 1531 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { -701, 120, 1881 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { -295, 160, 2296 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_WONDER_ITEM, { -579, 120, 1649 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { -572, 120, 1786 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_KANBAN, { -784, 120, 1675 }, { 0, -32767, 0 }, 832 },
    { ACTOR_EN_KANBAN, { -273, 160, 2173 }, { 0, 24576, 0 }, 837 },
    { ACTOR_EN_KUSA, { -756, 120, 708 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_GOROIWA, { -245, 120, 1870 }, { 0, 0, 1 }, 3074 },
    { ACTOR_EN_BOX, { -232, 178, 2245 }, { 0, 0, 0 }, 1248 },
};

static const Oot3dRoomObjectEntry oot3d_spot04_info_spot04_2_info_objects[] = {
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_spot04_info_room_refs[] = {
    { "spot04_0_info.zsi", 0 },
    { "spot04_1_info.zsi", 1 },
    { "spot04_2_info.zsi", 2 },
};

static const Oot3dSceneSetupIndex oot3d_spot04_info_setups[] = {
    { 0u, oot3d_spot04_info_setup_0_commands, 13u, oot3d_spot04_info_setup_0_special_files, 1u, oot3d_spot04_info_setup_0_paths, 3u, NULL, 0u, oot3d_spot04_info_setup_0_spawns, 11u, oot3d_spot04_info_setup_0_entrances, 12u, oot3d_spot04_info_setup_0_transition_actors, 2u, oot3d_spot04_info_setup_0_light_settings, 12u, oot3d_spot04_info_setup_0_exits, 4u, oot3d_spot04_info_setup_0_skybox_settings, 1u, oot3d_spot04_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_spot04_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_spot04_info_setup_1_commands, 13u, oot3d_spot04_info_setup_1_special_files, 1u, oot3d_spot04_info_setup_1_paths, 1u, NULL, 0u, oot3d_spot04_info_setup_1_spawns, 12u, oot3d_spot04_info_setup_1_entrances, 13u, oot3d_spot04_info_setup_1_transition_actors, 2u, oot3d_spot04_info_setup_1_light_settings, 12u, oot3d_spot04_info_setup_1_exits, 4u, oot3d_spot04_info_setup_1_skybox_settings, 1u, oot3d_spot04_info_setup_1_sound_settings, 1u, NULL, 0u, oot3d_spot04_info_setup_1_misc_settings, 1u },
    { 2u, oot3d_spot04_info_setup_2_commands, 13u, oot3d_spot04_info_setup_2_special_files, 1u, oot3d_spot04_info_setup_2_paths, 1u, NULL, 0u, oot3d_spot04_info_setup_2_spawns, 12u, oot3d_spot04_info_setup_2_entrances, 13u, oot3d_spot04_info_setup_2_transition_actors, 2u, oot3d_spot04_info_setup_2_light_settings, 12u, oot3d_spot04_info_setup_2_exits, 4u, oot3d_spot04_info_setup_2_skybox_settings, 1u, oot3d_spot04_info_setup_2_sound_settings, 1u, NULL, 0u, oot3d_spot04_info_setup_2_misc_settings, 1u },
    { 3u, oot3d_spot04_info_setup_3_commands, 13u, oot3d_spot04_info_setup_3_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot04_info_setup_3_spawns, 1u, oot3d_spot04_info_setup_3_entrances, 1u, oot3d_spot04_info_setup_3_transition_actors, 2u, oot3d_spot04_info_setup_3_light_settings, 5u, oot3d_spot04_info_setup_3_exits, 6u, oot3d_spot04_info_setup_3_skybox_settings, 1u, oot3d_spot04_info_setup_3_sound_settings, 1u, oot3d_spot04_info_setup_3_cutscenes, 1u, oot3d_spot04_info_setup_3_misc_settings, 1u },
    { 4u, oot3d_spot04_info_setup_4_commands, 13u, oot3d_spot04_info_setup_4_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot04_info_setup_4_spawns, 1u, oot3d_spot04_info_setup_4_entrances, 1u, oot3d_spot04_info_setup_4_transition_actors, 2u, oot3d_spot04_info_setup_4_light_settings, 4u, oot3d_spot04_info_setup_4_exits, 8u, oot3d_spot04_info_setup_4_skybox_settings, 1u, oot3d_spot04_info_setup_4_sound_settings, 1u, oot3d_spot04_info_setup_4_cutscenes, 1u, oot3d_spot04_info_setup_4_misc_settings, 1u },
    { 5u, oot3d_spot04_info_setup_5_commands, 13u, oot3d_spot04_info_setup_5_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot04_info_setup_5_spawns, 1u, oot3d_spot04_info_setup_5_entrances, 1u, oot3d_spot04_info_setup_5_transition_actors, 2u, oot3d_spot04_info_setup_5_light_settings, 4u, oot3d_spot04_info_setup_5_exits, 8u, oot3d_spot04_info_setup_5_skybox_settings, 1u, oot3d_spot04_info_setup_5_sound_settings, 1u, oot3d_spot04_info_setup_5_cutscenes, 1u, oot3d_spot04_info_setup_5_misc_settings, 1u },
    { 6u, oot3d_spot04_info_setup_6_commands, 14u, oot3d_spot04_info_setup_6_special_files, 1u, oot3d_spot04_info_setup_6_paths, 2u, NULL, 0u, oot3d_spot04_info_setup_6_spawns, 1u, oot3d_spot04_info_setup_6_entrances, 1u, oot3d_spot04_info_setup_6_transition_actors, 2u, oot3d_spot04_info_setup_6_light_settings, 8u, oot3d_spot04_info_setup_6_exits, 2u, oot3d_spot04_info_setup_6_skybox_settings, 1u, oot3d_spot04_info_setup_6_sound_settings, 1u, oot3d_spot04_info_setup_6_cutscenes, 1u, oot3d_spot04_info_setup_6_misc_settings, 1u },
    { 7u, oot3d_spot04_info_setup_7_commands, 14u, oot3d_spot04_info_setup_7_special_files, 1u, oot3d_spot04_info_setup_7_paths, 1u, NULL, 0u, oot3d_spot04_info_setup_7_spawns, 1u, oot3d_spot04_info_setup_7_entrances, 1u, oot3d_spot04_info_setup_7_transition_actors, 3u, oot3d_spot04_info_setup_7_light_settings, 8u, oot3d_spot04_info_setup_7_exits, 2u, oot3d_spot04_info_setup_7_skybox_settings, 1u, oot3d_spot04_info_setup_7_sound_settings, 1u, oot3d_spot04_info_setup_7_cutscenes, 1u, oot3d_spot04_info_setup_7_misc_settings, 1u },
    { 8u, oot3d_spot04_info_setup_8_commands, 14u, oot3d_spot04_info_setup_8_special_files, 1u, oot3d_spot04_info_setup_8_paths, 1u, NULL, 0u, oot3d_spot04_info_setup_8_spawns, 1u, oot3d_spot04_info_setup_8_entrances, 1u, oot3d_spot04_info_setup_8_transition_actors, 3u, oot3d_spot04_info_setup_8_light_settings, 8u, oot3d_spot04_info_setup_8_exits, 2u, oot3d_spot04_info_setup_8_skybox_settings, 1u, oot3d_spot04_info_setup_8_sound_settings, 1u, oot3d_spot04_info_setup_8_cutscenes, 1u, oot3d_spot04_info_setup_8_misc_settings, 1u },
    { 9u, oot3d_spot04_info_setup_9_commands, 13u, oot3d_spot04_info_setup_9_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot04_info_setup_9_spawns, 1u, oot3d_spot04_info_setup_9_entrances, 1u, oot3d_spot04_info_setup_9_transition_actors, 2u, oot3d_spot04_info_setup_9_light_settings, 4u, oot3d_spot04_info_setup_9_exits, 2u, oot3d_spot04_info_setup_9_skybox_settings, 1u, oot3d_spot04_info_setup_9_sound_settings, 1u, oot3d_spot04_info_setup_9_cutscenes, 1u, oot3d_spot04_info_setup_9_misc_settings, 1u },
    { 10u, oot3d_spot04_info_setup_10_commands, 13u, oot3d_spot04_info_setup_10_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot04_info_setup_10_spawns, 1u, oot3d_spot04_info_setup_10_entrances, 1u, oot3d_spot04_info_setup_10_transition_actors, 2u, oot3d_spot04_info_setup_10_light_settings, 4u, oot3d_spot04_info_setup_10_exits, 2u, oot3d_spot04_info_setup_10_skybox_settings, 1u, oot3d_spot04_info_setup_10_sound_settings, 1u, oot3d_spot04_info_setup_10_cutscenes, 1u, oot3d_spot04_info_setup_10_misc_settings, 1u },
    { 11u, oot3d_spot04_info_setup_11_commands, 13u, oot3d_spot04_info_setup_11_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot04_info_setup_11_spawns, 1u, oot3d_spot04_info_setup_11_entrances, 1u, oot3d_spot04_info_setup_11_transition_actors, 2u, oot3d_spot04_info_setup_11_light_settings, 12u, oot3d_spot04_info_setup_11_exits, 2u, oot3d_spot04_info_setup_11_skybox_settings, 1u, oot3d_spot04_info_setup_11_sound_settings, 1u, oot3d_spot04_info_setup_11_cutscenes, 1u, oot3d_spot04_info_setup_11_misc_settings, 1u },
    { 12u, oot3d_spot04_info_setup_12_commands, 13u, oot3d_spot04_info_setup_12_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot04_info_setup_12_spawns, 1u, oot3d_spot04_info_setup_12_entrances, 1u, oot3d_spot04_info_setup_12_transition_actors, 3u, oot3d_spot04_info_setup_12_light_settings, 12u, oot3d_spot04_info_setup_12_exits, 2u, oot3d_spot04_info_setup_12_skybox_settings, 1u, oot3d_spot04_info_setup_12_sound_settings, 1u, oot3d_spot04_info_setup_12_cutscenes, 1u, oot3d_spot04_info_setup_12_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_spot04_info_rooms[] = {
    { "spot04_0_info.zsi", 0, oot3d_spot04_info_spot04_0_info_objects, 12u, oot3d_spot04_info_spot04_0_info_actors, 79u },
    { "spot04_1_info.zsi", 1, oot3d_spot04_info_spot04_1_info_objects, 5u, oot3d_spot04_info_spot04_1_info_actors, 9u },
    { "spot04_2_info.zsi", 2, oot3d_spot04_info_spot04_2_info_objects, 4u, oot3d_spot04_info_spot04_2_info_actors, 12u },
};

const Oot3dSceneIndex oot3d_scene_index_spot04_info = {
    "spot04_info.zsi",
    oot3d_spot04_info_room_refs, 3u,
    oot3d_spot04_info_rooms, 3u,
    oot3d_spot04_info_setups, 13u,
};
