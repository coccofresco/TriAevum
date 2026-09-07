/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: spot10_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_spot10_info_setup_0_commands[] = {
    { 0x00130915u, 0x010005ABu },
    { 0x00000A04u, 0x00000148u },
    { 0x0000090Eu, 0x000003F0u },
    { 0x00000019u, 0x0000000Au },
    { 0x00000003u, 0x000133D0u },
    { 0x00000A06u, 0x000133FCu },
    { 0x00000107u, 0x00000002u },
    { 0x00000A00u, 0x00013410u },
    { 0x00000011u, 0x0000001Du },
    { 0x00000013u, 0x000134B0u },
    { 0x00000C0Fu, 0x000134C4u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot10_info_setup_0_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot10_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { -3, 0, 295 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { 802, 0, -2740 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -304, 0, 1 }, { 0, 16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -2, 0, -308 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { 300, 0, -1 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { 1876, 0, -2396 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { 794, -26, -1112 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { 2188, -260, -855 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -1509, -200, 1603 }, { 0, 15109, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot10_info_setup_0_entrances[] = {
    { 0u, 0 },
    { 1u, 8 },
    { 2u, 0 },
    { 3u, 0 },
    { 4u, 0 },
    { 5u, 7 },
    { 6u, 2 },
    { 7u, 3 },
    { 8u, 5 },
    { 9u, 5 },
};

static const Oot3dTransitionActorEntry oot3d_spot10_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 4, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { 1600, 0, -1200 }, 0, 63 },
    { { 3, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { 1200, 0, -800 }, -16384, 63 },
    { { 2, -1 }, { 1, -1 }, ACTOR_EN_HOLL, { 800, 0, -400 }, 0, 63 },
    { { 1, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { 400, 0, 0 }, -16384, 63 },
    { { 9, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { -400, 0, 0 }, 16384, 63 },
    { { 6, -1 }, { 4, -1 }, ACTOR_EN_HOLL, { 1200, 0, -1600 }, 16384, 63 },
    { { 5, -1 }, { 9, -1 }, ACTOR_EN_HOLL, { -1200, 0, 600 }, -32767, 63 },
    { { 7, -1 }, { 4, -1 }, ACTOR_EN_HOLL, { 1600, 0, -2000 }, 0, 63 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot10_info_setup_0_light_settings[] = {
    { { 0x1E, 0x01, 0xAD, 0x01, 0xB1, 0x01, 0xC6, 0x04, 0xE2, 0x04, 0xDD, 0x01, 0x85, 0x01, 0x0D, 0x02, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x40, 0xB5, 0x45, 0x20, 0xFF, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x40, 0xB5, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xE5, 0xE5, 0x77, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x40, 0xB5, 0x45, 0x20, 0xFF, 0xB5, 0x59 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x96, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x4F, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x40, 0xB5, 0x45, 0xC8, 0xFC, 0x33, 0x59 } },
    { { 0xB5, 0x48, 0x48, 0x48, 0x3F, 0x59, 0x4F, 0xB8, 0xB8, 0xB8, 0x63, 0xAA, 0xDB, 0x33, 0x3D, 0x63, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x44, 0x54 } },
    { { 0x44, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x54, 0x8C } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x59, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x23, 0x3F } },
    { { 0x68, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x7A, 0x45, 0xC8, 0x04, 0x72, 0x72 } },
    { { 0x68, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0x9B, 0x00, 0x00, 0x00, 0x19, 0x33, 0x38, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x80, 0x89, 0x45, 0xC8, 0x04, 0x8C, 0x8C } },
    { { 0xA0, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x44, 0x44, 0x59, 0xB5, 0xB5, 0x9B, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x61, 0x45, 0xC8, 0x04, 0x82, 0x72 } },
    { { 0x72, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x48, 0x45, 0xC8, 0x04, 0x4F, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot10_info_setup_0_exits[] = {
    { 646, 646u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 252, 252u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot10_info_setup_0_skybox_settings[] = {
    { 29u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot10_info_setup_0_sound_settings[] = {
    { 9u, 1451u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot10_info_setup_0_misc_settings[] = {
    { 0u, 0x0000000Au },
};

static const Oot3dSceneCommand oot3d_spot10_info_setup_1_commands[] = {
    { 0x00130915u, 0x010005ABu },
    { 0x00000A04u, 0x00013614u },
    { 0x0000090Eu, 0x000138BCu },
    { 0x00000019u, 0x0000000Au },
    { 0x00000003u, 0x000133D0u },
    { 0x00000A06u, 0x0001394Cu },
    { 0x00000107u, 0x00000002u },
    { 0x0000030Du, 0x000139E0u },
    { 0x00000A00u, 0x000139F8u },
    { 0x00000011u, 0x0000001Du },
    { 0x00000013u, 0x00013A98u },
    { 0x00000C0Fu, 0x00013AACu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot10_info_setup_1_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot10_info_setup_1_paths_0_points[] = {
    { 3, 0, 14724 },
    { 1, 12, 0 },
};

static const Oot3dVec3s oot3d_spot10_info_setup_1_paths_1_points[] = {
    { 14742, 1, 1599 },
    { 0, -979, 1719 },
    { 0, -951, 610 },
};

static const Oot3dVec3s oot3d_spot10_info_setup_1_paths_2_points[] = {
    { 0, -1770, 608 },
    { 210, -1770, 610 },
    { 0, -1770, -1220 },
    { 0, 935, -1220 },
    { 0, 1098, -1241 },
    { -248, 1279, -1160 },
    { -79, 1409, -1236 },
    { -80, 1682, -1211 },
    { -80, 1877, -1278 },
    { -2, 2089, -1277 },
    { -176, 2089, -1208 },
    { 0, 1877, -1181 },
};

static const Oot3dPathRecord oot3d_spot10_info_setup_1_paths[] = {
    { 2u, 0u, 0u, 80248u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot10_info_setup_1_paths_0_points },
    { 3u, 0u, 0u, 80260u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot10_info_setup_1_paths_1_points },
    { 12u, 0u, 0u, 80278u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot10_info_setup_1_paths_2_points },
};

static const Oot3dActorEntry oot3d_spot10_info_setup_1_spawns[] = {
    { ACTOR_PLAYER, { -1, 0, 305 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { 802, 0, -2589 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -1023, 0, -129 }, { 0, -16384, 0 }, 3583 },
    { ACTOR_PLAYER, { -1, 0, -296 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { 292, 0, 0 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { 1908, 0, -2387 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { 797, -15, -1091 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { 2134, -196, -851 }, { 0, -16384, 0 }, 4095 },
    { ACTOR_PLAYER, { -1502, -200, 1600 }, { 0, 15109, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot10_info_setup_1_entrances[] = {
    { 0u, 0 },
    { 1u, 8 },
    { 2u, 9 },
    { 3u, 0 },
    { 4u, 0 },
    { 5u, 7 },
    { 6u, 2 },
    { 7u, 3 },
    { 8u, 5 },
    { 9u, 5 },
};

static const Oot3dTransitionActorEntry oot3d_spot10_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 4, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { 1600, 0, -1200 }, 0, 63 },
    { { 3, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { 1200, 0, -800 }, -16384, 63 },
    { { 2, -1 }, { 1, -1 }, ACTOR_EN_HOLL, { 800, 0, -400 }, 0, 63 },
    { { 1, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { 400, 0, 0 }, -16384, 63 },
    { { 9, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { -400, 0, 0 }, 16384, 63 },
    { { 6, -1 }, { 4, -1 }, ACTOR_EN_HOLL, { 1200, 0, -1600 }, 16384, 63 },
    { { 5, -1 }, { 9, -1 }, ACTOR_EN_HOLL, { -1200, 0, 600 }, -32767, 63 },
    { { 7, -1 }, { 4, -1 }, ACTOR_EN_HOLL, { 1600, 0, -2000 }, 0, 63 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot10_info_setup_1_light_settings[] = {
    { { 0x1E, 0x01, 0xAD, 0x01, 0xB1, 0x01, 0xC6, 0x04, 0xE2, 0x04, 0xDD, 0x01, 0x85, 0x01, 0x0D, 0x02, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x40, 0xB5, 0x45, 0x20, 0xFF, 0x77, 0x77 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xEF, 0xEF, 0x63, 0xB8, 0xB8, 0xB8, 0x4F, 0x4F, 0x33, 0xC6, 0xC6, 0x4F, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x40, 0xB5, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xDB, 0xB8, 0xB8, 0xB8, 0x6D, 0x63, 0x4F, 0xE5, 0xE5, 0x77, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x40, 0xB5, 0x45, 0x20, 0xFF, 0xB5, 0x59 } },
    { { 0x28, 0x48, 0x48, 0x48, 0xEF, 0x96, 0x3D, 0xB8, 0xB8, 0xB8, 0x63, 0x3D, 0x4F, 0xE5, 0x63, 0x28, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x40, 0xB5, 0x45, 0xC8, 0xFC, 0x33, 0x59 } },
    { { 0xB5, 0x48, 0x48, 0x48, 0x3F, 0x59, 0x4F, 0xB8, 0xB8, 0xB8, 0x63, 0xAA, 0xDB, 0x33, 0x3D, 0x63, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x44, 0x54 } },
    { { 0x44, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x54, 0x8C } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x59, 0x44 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0x6D, 0x82, 0xB8, 0xB8, 0xB8, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x16, 0x45, 0x00, 0xFC, 0x23, 0x3F } },
    { { 0x68, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x7A, 0x45, 0xC8, 0x04, 0x72, 0x72 } },
    { { 0x68, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0x9B, 0x00, 0x00, 0x00, 0x19, 0x33, 0x38, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x80, 0x89, 0x45, 0xC8, 0x04, 0x8C, 0x8C } },
    { { 0xA0, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x44, 0x44, 0x59, 0xB5, 0xB5, 0x9B, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x61, 0x45, 0xC8, 0x04, 0x82, 0x72 } },
    { { 0x72, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x82, 0x82, 0x68, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x00, 0x48, 0x45, 0xC8, 0x04, 0x4F, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot10_info_setup_1_exits[] = {
    { 646, 646u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 252, 252u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot10_info_setup_1_skybox_settings[] = {
    { 29u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot10_info_setup_1_sound_settings[] = {
    { 9u, 1451u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot10_info_setup_1_misc_settings[] = {
    { 0u, 0x0000000Au },
};

static const Oot3dSceneCommand oot3d_spot10_info_setup_2_commands[] = {
    { 0x00040715u, 0x0000007Fu },
    { 0x00000A04u, 0x00013BFCu },
    { 0x0000090Eu, 0x00013EA4u },
    { 0x00000019u, 0x0000000Au },
    { 0x00000003u, 0x000133D0u },
    { 0x00000106u, 0x00013F34u },
    { 0x00000107u, 0x00000000u },
    { 0x00000100u, 0x00013F38u },
    { 0x00000011u, 0x0001001Du },
    { 0x00000013u, 0x00013F48u },
    { 0x0000010Fu, 0x00013F5Cu },
    { 0x00000017u, 0x00013F78u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot10_info_setup_2_special_files[] = {
    { 1u, OBJECT_INVALID },
};

static const Oot3dActorEntry oot3d_spot10_info_setup_2_spawns[] = {
    { ACTOR_EN_HOLL, { 1200, 0, -2400 }, { 16384, 63, 1280 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot10_info_setup_2_entrances[] = {
    { 0u, 5 },
};

static const Oot3dTransitionActorEntry oot3d_spot10_info_setup_2_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 4, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { 1600, 0, -1200 }, 0, 63 },
    { { 3, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { 1200, 0, -800 }, -16384, 63 },
    { { 2, -1 }, { 1, -1 }, ACTOR_EN_HOLL, { 800, 0, -400 }, 0, 63 },
    { { 1, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { 400, 0, 0 }, -16384, 63 },
    { { 9, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { -400, 0, 0 }, 16384, 63 },
    { { 6, -1 }, { 4, -1 }, ACTOR_EN_HOLL, { 1200, 0, -1600 }, 16384, 63 },
    { { 5, -1 }, { 9, -1 }, ACTOR_EN_HOLL, { -1200, 0, 600 }, -32767, 63 },
    { { 7, -1 }, { 4, -1 }, ACTOR_EN_HOLL, { 1600, 0, -2000 }, 0, 63 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot10_info_setup_2_light_settings[] = {
    { { 0x1E, 0x01, 0xAD, 0x01, 0xB1, 0x01, 0xC6, 0x04, 0xE2, 0x04, 0xDD, 0x01, 0x85, 0x01, 0x0D, 0x02, 0x00, 0x80, 0x3B, 0x46, 0x00, 0x40, 0xB5, 0x45, 0x20, 0xFF, 0xB5, 0xB5 } },
};

static const Oot3dExitEntry oot3d_spot10_info_setup_2_exits[] = {
    { 646, 646u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 252, 252u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot10_info_setup_2_skybox_settings[] = {
    { 29u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot10_info_setup_2_sound_settings[] = {
    { 7u, 127u, 0u, 0u },
};

static const Oot3dCutsceneReference oot3d_spot10_info_setup_2_cutscenes[] = {
    { 0x00013F78u, 1u },
};

static const Oot3dMiscSettings oot3d_spot10_info_setup_2_misc_settings[] = {
    { 0u, 0x0000000Au },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 287, 0, -2 }, { 0, 0, 0 }, 12 },
    { ACTOR_OBJECT_KANKYO, { 52, 0, -52 }, { 0, 0, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_0_info_objects[] = {
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_1_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 796, 0, -274 }, { 0, 0, 0 }, 12 },
    { ACTOR_EN_SKJ, { 1123, -140, 340 }, { 0, 7645, 0 }, 7167 },
    { ACTOR_EN_SKJ, { 1265, -94, 420 }, { 0, -22938, 0 }, 2047 },
    { ACTOR_EN_SKJ, { 1183, -72, 476 }, { 0, -26761, 0 }, 3071 },
    { ACTOR_EN_DNT_NOMAL, { 1368, 80, -173 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_WONDER_ITEM, { 1256, -152, 194 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 1038, -180, 557 }, { 0, 0, 1 }, 4671 },
    { ACTOR_EN_WONDER_ITEM, { 1371, -180, 497 }, { 0, 0, 1 }, 4735 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_1_info_objects[] = {
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOPNUTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DNK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_YABUSAME_POINT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OWL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HINTNUTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_DEKUPOUCH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_2_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 1061, 0, -803 }, { 0, 0, 0 }, 12 },
    { ACTOR_OBJECT_KANKYO, { 798, 0, -897 }, { 0, 0, 0 }, 0 },
    { ACTOR_DOOR_ANA, { 915, 0, -925 }, { 0, -8191, 0 }, 20 },
    { ACTOR_EN_WEATHER_TAG, { 798, -80, -1220 }, { 0, 0, 0 }, 772 },
    { ACTOR_EN_KUSA, { 645, 0, -638 }, { 0, -16384, 0 }, 1792 },
    { ACTOR_EN_KUSA, { 676, 0, -651 }, { 0, -16384, 0 }, 1792 },
    { ACTOR_EN_KUSA, { 633, 0, -676 }, { 0, -16384, 0 }, 1792 },
    { ACTOR_OBJ_BOMBIWA, { 915, -5, -925 }, { 0, 0, 0 }, -32751 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_2_info_objects[] = {
    { OBJECT_LINK_BOY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_3_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 1596, 0, -1067 }, { 0, 0, 0 }, 12 },
    { ACTOR_OBJECT_KANKYO, { 1735, 0, -792 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_OKUTA, { 2114, -20, -847 }, { 0, -16384, 0 }, -256 },
    { ACTOR_EN_MD, { 1599, 0, -980 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_WEATHER_TAG, { 2135, -190, -847 }, { 0, 0, 0 }, 514 },
    { ACTOR_SHOT_SUN, { 1930, 0, -795 }, { 0, 0, 0 }, -191 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_3_info_objects[] = {
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_4_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 1594, 0, -1880 }, { 0, 0, 0 }, 12 },
    { ACTOR_OBJECT_KANKYO, { 1588, 0, -1581 }, { 0, 0, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_4_info_objects[] = {
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_5_info_actors[] = {
    { ACTOR_OBJECT_KANKYO, { -1191, -220, 1626 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_SHOPNUTS, { -1136, -200, 2180 }, { 0, -16384, 0 }, 9 },
    { ACTOR_OBJ_MAKEKINSUTA, { -1220, 0, 935 }, { 0, 0, 0 }, 19969 },
    { ACTOR_OBJ_BEAN, { -1220, 0, 935 }, { 0, 0, 0 }, 7940 },
    { ACTOR_EN_GS, { -1305, -230, 2300 }, { 0, -32767, 0 }, 14365 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_5_info_objects[] = {
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOPNUTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DNK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_YABUSAME_POINT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OWL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HINTNUTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_DEKUPOUCH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AM, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_6_info_actors[] = {
    { ACTOR_DOOR_ANA, { 80, -20, -1600 }, { 0, 16384, 12 }, 243 },
    { ACTOR_EN_SW, { 772, 210, -1808 }, { 0, -8191, 0 }, -20988 },
    { ACTOR_OBJ_BEAN, { 610, 0, -1770 }, { 0, 0, 0 }, 274 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_6_info_objects[] = {
    { OBJECT_DODONGO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_BEAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_7_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 1348, 0, -2389 }, { 0, 0, 0 }, 12 },
    { ACTOR_EN_ITEM00, { 1720, 0, -2510 }, { 0, 0, 0 }, 4865 },
    { ACTOR_EN_KUSA, { 1445, 0, -2238 }, { 0, -16384, 0 }, 1792 },
    { ACTOR_EN_KUSA, { 1476, 0, -2251 }, { 0, -16384, 0 }, 1792 },
    { ACTOR_EN_KUSA, { 1433, 0, -2276 }, { 0, -16384, 0 }, 1792 },
    { ACTOR_OBJ_BOMBIWA, { 1720, 0, -2510 }, { 0, 0, 0 }, 30 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_7_info_objects[] = {
    { OBJECT_GOL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_8_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 793, 0, -2657 }, { 0, 0, 0 }, 12 },
    { ACTOR_EN_RIVER_SOUND, { 799, 0, -3155 }, { 0, 0, 0 }, 12 },
    { ACTOR_OBJECT_KANKYO, { 815, 0, -2368 }, { 0, 0, 0 }, 0 },
    { ACTOR_DOOR_ANA, { 670, 0, -2520 }, { 0, 8191, 7 }, 245 },
    { ACTOR_EN_KUSA, { 952, 0, -2275 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_KUSA, { 965, 0, -2241 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_KUSA, { 926, 0, -2232 }, { 0, 0, 0 }, 1792 },
    { ACTOR_OBJ_BOMBIWA, { 670, 0, -2520 }, { 0, 0, 0 }, -32737 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_8_info_objects[] = {
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot10_info_spot10_9_info_actors[] = {
    { ACTOR_EN_HS, { -1075, 0, -130 }, { 0, 16384, 0 }, 0 },
    { ACTOR_EN_SKJ, { -1132, 80, -128 }, { 0, 8920, 0 }, -1 },
    { ACTOR_EN_KO, { -1075, 0, -130 }, { 0, 16384, 0 }, -244 },
};

static const Oot3dRoomObjectEntry oot3d_spot10_info_spot10_9_info_objects[] = {
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SKJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_spot10_info_room_refs[] = {
    { "spot10_0_info.zsi", 0 },
    { "spot10_1_info.zsi", 1 },
    { "spot10_2_info.zsi", 2 },
    { "spot10_3_info.zsi", 3 },
    { "spot10_4_info.zsi", 4 },
    { "spot10_5_info.zsi", 5 },
    { "spot10_6_info.zsi", 6 },
};

static const Oot3dSceneSetupIndex oot3d_spot10_info_setups[] = {
    { 0u, oot3d_spot10_info_setup_0_commands, 12u, oot3d_spot10_info_setup_0_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot10_info_setup_0_spawns, 9u, oot3d_spot10_info_setup_0_entrances, 10u, oot3d_spot10_info_setup_0_transition_actors, 9u, oot3d_spot10_info_setup_0_light_settings, 12u, oot3d_spot10_info_setup_0_exits, 2u, oot3d_spot10_info_setup_0_skybox_settings, 1u, oot3d_spot10_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_spot10_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_spot10_info_setup_1_commands, 13u, oot3d_spot10_info_setup_1_special_files, 1u, oot3d_spot10_info_setup_1_paths, 3u, NULL, 0u, oot3d_spot10_info_setup_1_spawns, 9u, oot3d_spot10_info_setup_1_entrances, 10u, oot3d_spot10_info_setup_1_transition_actors, 9u, oot3d_spot10_info_setup_1_light_settings, 12u, oot3d_spot10_info_setup_1_exits, 2u, oot3d_spot10_info_setup_1_skybox_settings, 1u, oot3d_spot10_info_setup_1_sound_settings, 1u, NULL, 0u, oot3d_spot10_info_setup_1_misc_settings, 1u },
    { 2u, oot3d_spot10_info_setup_2_commands, 13u, oot3d_spot10_info_setup_2_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot10_info_setup_2_spawns, 1u, oot3d_spot10_info_setup_2_entrances, 1u, oot3d_spot10_info_setup_2_transition_actors, 9u, oot3d_spot10_info_setup_2_light_settings, 1u, oot3d_spot10_info_setup_2_exits, 2u, oot3d_spot10_info_setup_2_skybox_settings, 1u, oot3d_spot10_info_setup_2_sound_settings, 1u, oot3d_spot10_info_setup_2_cutscenes, 1u, oot3d_spot10_info_setup_2_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_spot10_info_rooms[] = {
    { "spot10_0_info.zsi", 0, oot3d_spot10_info_spot10_0_info_objects, 12u, oot3d_spot10_info_spot10_0_info_actors, 2u },
    { "spot10_1_info.zsi", 1, oot3d_spot10_info_spot10_1_info_objects, 11u, oot3d_spot10_info_spot10_1_info_actors, 8u },
    { "spot10_2_info.zsi", 2, oot3d_spot10_info_spot10_2_info_objects, 13u, oot3d_spot10_info_spot10_2_info_actors, 8u },
    { "spot10_3_info.zsi", 3, oot3d_spot10_info_spot10_3_info_objects, 12u, oot3d_spot10_info_spot10_3_info_actors, 6u },
    { "spot10_4_info.zsi", 4, oot3d_spot10_info_spot10_4_info_objects, 12u, oot3d_spot10_info_spot10_4_info_actors, 2u },
    { "spot10_5_info.zsi", 5, oot3d_spot10_info_spot10_5_info_objects, 12u, oot3d_spot10_info_spot10_5_info_actors, 5u },
    { "spot10_6_info.zsi", 6, oot3d_spot10_info_spot10_6_info_objects, 14u, oot3d_spot10_info_spot10_6_info_actors, 3u },
    { "spot10_7_info.zsi", 7, oot3d_spot10_info_spot10_7_info_objects, 13u, oot3d_spot10_info_spot10_7_info_actors, 6u },
    { "spot10_8_info.zsi", 8, oot3d_spot10_info_spot10_8_info_objects, 12u, oot3d_spot10_info_spot10_8_info_actors, 8u },
    { "spot10_9_info.zsi", 9, oot3d_spot10_info_spot10_9_info_objects, 12u, oot3d_spot10_info_spot10_9_info_actors, 3u },
};

const Oot3dSceneIndex oot3d_scene_index_spot10_info = {
    "spot10_info.zsi",
    oot3d_spot10_info_room_refs, 7u,
    oot3d_spot10_info_rooms, 10u,
    oot3d_spot10_info_setups, 3u,
};
