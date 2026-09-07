/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: spot06_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_spot06_info_setup_0_commands[] = {
    { 0x00000215u, 0x01000585u },
    { 0x00000104u, 0x000001CCu },
    { 0x0000020Eu, 0x00000210u },
    { 0x00000019u, 0x00000006u },
    { 0x00000003u, 0x0000D3C4u },
    { 0x00000906u, 0x0000D3F0u },
    { 0x00000107u, 0x00000002u },
    { 0x0000010Du, 0x0000D438u },
    { 0x00000900u, 0x0000D440u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x0000D4D0u },
    { 0x00000C0Fu, 0x0000D4E0u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot06_info_setup_0_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot06_info_setup_0_paths_0_points[] = {
    { 6, 7, 8 },
    { 32256, 7, 0 },
    { -11252, 0, -4445 },
    { -904, 2475, -4029 },
    { -883, 2463, -4009 },
    { -1128, 2443, -3449 },
    { -1128, 3003, -3449 },
};

static const Oot3dPathRecord oot3d_spot06_info_setup_0_paths[] = {
    { 7u, 0u, 0u, 54284u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot06_info_setup_0_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot06_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { -2093, -1037, 705 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -3918, -1142, 2533 }, { 0, 9101, 0 }, 4095 },
    { ACTOR_PLAYER, { -929, -1313, 6555 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { -2093, -1037, 706 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -2422, -1033, 3858 }, { 0, 16384, 0 }, 3839 },
    { ACTOR_PLAYER, { -1045, -1223, 7457 }, { 0, -32767, 0 }, 3583 },
    { ACTOR_PLAYER, { 1322, -1218, 3871 }, { 0, -16384, 0 }, 3839 },
    { ACTOR_PLAYER, { -912, -1544, 3759 }, { 0, 0, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot06_info_setup_0_entrances[] = {
    { 0u, 0 },
    { 1u, 0 },
    { 2u, 0 },
    { 3u, 0 },
    { 4u, 0 },
    { 5u, 0 },
    { 6u, 0 },
    { 7u, 0 },
    { 8u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot06_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 0, -1 }, ACTOR_EN_DOOR, { -2465, -1033, 3857 }, 16384, 447 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot06_info_setup_0_light_settings[] = {
    { { 0x89, 0x01, 0xA5, 0x01, 0x10, 0x00, 0x7A, 0x02, 0x43, 0x00, 0x5F, 0x04, 0x28, 0x03, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x47, 0xC8, 0x04, 0x68, 0x68 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xE5, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x33, 0x14, 0xAF, 0x82, 0x3D, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xC0, 0x5A, 0x47, 0x20, 0x07, 0x82, 0x82 } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x6D, 0x6D, 0x63, 0x82, 0xA5, 0xAF, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x47, 0xC8, 0x04, 0x8C, 0x4F } },
    { { 0x19, 0x00, 0x00, 0x00, 0xE5, 0xB5, 0x33, 0x00, 0x00, 0x00, 0x33, 0x14, 0x14, 0x77, 0x19, 0x33, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xA0, 0x0C, 0x47, 0x28, 0x04, 0x33, 0x49 } },
    { { 0x82, 0x00, 0x00, 0x00, 0x2D, 0x2D, 0x38, 0x00, 0x00, 0x00, 0x4F, 0xB5, 0xFF, 0x00, 0x00, 0x19, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x3D, 0x59 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x0A, 0x19, 0x05, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0xFC, 0x49, 0x82 } },
    { { 0x63, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x04, 0x3F, 0x28, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x4F, 0x3D } },
    { { 0x1E, 0x48, 0x48, 0x48, 0x8C, 0x77, 0x4F, 0xB8, 0xB8, 0xB8, 0x33, 0x59, 0x77, 0x19, 0x14, 0x07, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0xFC, 0x19, 0x38 } },
    { { 0x68, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x05, 0x0C, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xC8, 0x04, 0x54, 0x54 } },
    { { 0x4F, 0x48, 0x48, 0x48, 0xBF, 0xBF, 0xA5, 0xB8, 0xB8, 0xB8, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0xBB, 0x46, 0x90, 0x05, 0x6D, 0x6D } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xDB, 0xDB, 0xDB, 0xB8, 0xB8, 0xB8, 0x63, 0x63, 0x63, 0x4C, 0x4C, 0x4C, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xC8, 0x04, 0x59, 0x4F } },
    { { 0x4F, 0x48, 0x48, 0x48, 0xB5, 0xA5, 0xA5, 0xB8, 0xB8, 0xB8, 0x3D, 0x33, 0x33, 0x3A, 0x28, 0x2D, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0x04, 0x33, 0x4F } },
};

static const Oot3dExitEntry oot3d_spot06_info_setup_0_exits[] = {
    { 393, 393u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 421, 421u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 16, 16u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 634, 634u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 67, 67u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1119, 1119u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 808, 808u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot06_info_setup_0_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot06_info_setup_0_sound_settings[] = {
    { 2u, 1413u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot06_info_setup_0_misc_settings[] = {
    { 0u, 0x00000006u },
};

static const Oot3dSceneCommand oot3d_spot06_info_setup_1_commands[] = {
    { 0x00000215u, 0x01000585u },
    { 0x00000104u, 0x0000D630u },
    { 0x0000020Eu, 0x0000D674u },
    { 0x00000019u, 0x00000006u },
    { 0x00000003u, 0x0000D3C4u },
    { 0x00000A06u, 0x0000D694u },
    { 0x00000107u, 0x00000002u },
    { 0x0000020Du, 0x0000D76Cu },
    { 0x00000A00u, 0x0000D77Cu },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x0000D81Cu },
    { 0x00000C0Fu, 0x0000D82Cu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot06_info_setup_1_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot06_info_setup_1_paths_0_points[] = {
    { 7, 0, -10568 },
    { 0, 23, 0 },
    { -10526, 0, -4445 },
    { -904, 2475, -4029 },
    { -883, 2463, -4009 },
    { -1128, 2443, -3449 },
    { -1128, 3003, -3449 },
};

static const Oot3dVec3s oot3d_spot06_info_setup_1_paths_1_points[] = {
    { -1128, 3595, -3465 },
    { -1288, 3595, -3465 },
    { -1272, 3755, -2603 },
    { -1033, 3617, -2603 },
    { -707, 3617, -2286 },
    { -820, 3483, -1507 },
    { -882, 3353, 1088 },
    { -1212, 3740, 1108 },
    { -1232, 3863, 1026 },
    { -1235, 4206, 320 },
    { -1166, 4929, -149 },
    { -1149, 6162, -754 },
    { -982, 7006, -998 },
    { -982, 6951, -1361 },
    { -1103, 6245, -2199 },
    { -1214, 6126, -2653 },
    { -1282, 6365, -3079 },
    { -1287, 6602, -3754 },
    { -1230, 6416, -3852 },
    { -1037, 6003, -3948 },
    { -932, 5267, -3724 },
    { -839, 4227, -3154 },
    { -816, 3476, -2682 },
};

static const Oot3dPathRecord oot3d_spot06_info_setup_1_paths[] = {
    { 7u, 0u, 0u, 54968u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot06_info_setup_1_paths_0_points },
    { 23u, 0u, 0u, 55010u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot06_info_setup_1_paths_1_points },
};

static const Oot3dActorEntry oot3d_spot06_info_setup_1_spawns[] = {
    { ACTOR_PLAYER, { -2093, -1037, 705 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -3921, -1138, 2533 }, { 0, 9101, 0 }, 4095 },
    { ACTOR_PLAYER, { -917, -2201, 6359 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { -2093, -1037, 706 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -2423, -1033, 3861 }, { 0, 16384, 0 }, 3839 },
    { ACTOR_PLAYER, { -1254, -1243, 7361 }, { 0, 16384, 0 }, 3583 },
    { ACTOR_PLAYER, { 1329, -1218, 3872 }, { 0, -16384, 0 }, 3839 },
    { ACTOR_PLAYER, { -918, -1543, 3761 }, { 0, 0, 0 }, 4095 },
    { ACTOR_PLAYER, { -1045, -1223, 7460 }, { 0, -32767, 0 }, 3583 },
};

static const Oot3dEntranceEntry oot3d_spot06_info_setup_1_entrances[] = {
    { 0u, 0 },
    { 1u, 0 },
    { 2u, 0 },
    { 3u, 0 },
    { 4u, 0 },
    { 5u, 0 },
    { 6u, 0 },
    { 7u, 0 },
    { 8u, 0 },
    { 9u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot06_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 0, -1 }, ACTOR_EN_DOOR, { -2465, -1033, 3857 }, 16384, 447 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot06_info_setup_1_light_settings[] = {
    { { 0x89, 0x01, 0xA5, 0x01, 0x10, 0x00, 0x7A, 0x02, 0x43, 0x00, 0x5F, 0x04, 0x28, 0x03, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x47, 0xC8, 0x04, 0x68, 0x68 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xE5, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x33, 0x14, 0xAF, 0x82, 0x3D, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xC0, 0x5A, 0x47, 0x20, 0x07, 0x82, 0x82 } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x6D, 0x6D, 0x63, 0x82, 0xA5, 0xAF, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x47, 0xC8, 0x04, 0x8C, 0x4F } },
    { { 0x19, 0x00, 0x00, 0x00, 0xE5, 0xB5, 0x33, 0x00, 0x00, 0x00, 0x33, 0x14, 0x14, 0x77, 0x19, 0x33, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xA0, 0x0C, 0x47, 0x28, 0x04, 0x33, 0x49 } },
    { { 0x82, 0x00, 0x00, 0x00, 0x2D, 0x2D, 0x38, 0x00, 0x00, 0x00, 0x4F, 0xB5, 0xFF, 0x00, 0x00, 0x19, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x3D, 0x59 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x0A, 0x19, 0x05, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0xFC, 0x49, 0x82 } },
    { { 0x63, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x04, 0x3F, 0x28, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x4F, 0x3D } },
    { { 0x1E, 0x48, 0x48, 0x48, 0x8C, 0x77, 0x4F, 0xB8, 0xB8, 0xB8, 0x33, 0x59, 0x77, 0x19, 0x14, 0x07, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0xFC, 0x19, 0x38 } },
    { { 0x68, 0x48, 0x48, 0x48, 0x14, 0x28, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x82, 0xA0, 0x00, 0x05, 0x0C, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xC8, 0x04, 0x54, 0x54 } },
    { { 0x4F, 0x48, 0x48, 0x48, 0xBF, 0xBF, 0xA5, 0xB8, 0xB8, 0xB8, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0xBB, 0x46, 0x90, 0x05, 0x6D, 0x6D } },
    { { 0x6D, 0x48, 0x48, 0x48, 0xDB, 0xDB, 0xDB, 0xB8, 0xB8, 0xB8, 0x63, 0x63, 0x63, 0x4C, 0x4C, 0x4C, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xC8, 0x04, 0x59, 0x4F } },
    { { 0x4F, 0x48, 0x48, 0x48, 0xB5, 0xA5, 0xA5, 0xB8, 0xB8, 0xB8, 0x3D, 0x33, 0x33, 0x3A, 0x28, 0x2D, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0x04, 0x33, 0x3F } },
};

static const Oot3dExitEntry oot3d_spot06_info_setup_1_exits[] = {
    { 393, 393u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 421, 421u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 16, 16u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 634, 634u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 67, 67u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1119, 1119u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 808, 808u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot06_info_setup_1_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot06_info_setup_1_sound_settings[] = {
    { 2u, 1413u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot06_info_setup_1_misc_settings[] = {
    { 0u, 0x00000006u },
};

static const Oot3dSceneCommand oot3d_spot06_info_setup_2_commands[] = {
    { 0x00000215u, 0x01000585u },
    { 0x00000104u, 0x0000D97Cu },
    { 0x0000020Eu, 0x0000D9C0u },
    { 0x00000019u, 0x00000006u },
    { 0x00000003u, 0x0000D3C4u },
    { 0x00000106u, 0x0000D9E0u },
    { 0x00000007u, 0x00000002u },
    { 0x0000010Du, 0x0000DA18u },
    { 0x00000100u, 0x0000DA20u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x0000DA30u },
    { 0x0000080Fu, 0x0000DA3Cu },
    { 0x00000017u, 0x0000DB1Cu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot06_info_setup_2_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot06_info_setup_2_paths_0_points[] = {
    { -16384, 447, 0 },
    { 0, 7, 0 },
    { -9748, 0, -4445 },
    { -904, 2475, -4029 },
    { -883, 2463, -4009 },
    { -1128, 2443, -3449 },
    { -1128, 3003, -3449 },
};

static const Oot3dPathRecord oot3d_spot06_info_setup_2_paths[] = {
    { 7u, 0u, 0u, 55788u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot06_info_setup_2_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot06_info_setup_2_spawns[] = {
    { -3465, { -1272, 3755, 0 }, { 7, 0, -9748 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot06_info_setup_2_entrances[] = {
    { 0u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot06_info_setup_2_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 0, -1 }, ACTOR_EN_DOOR, { -2465, -1033, 3857 }, 16384, 447 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot06_info_setup_2_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x02, 0x89, 0x01, 0xA5, 0x01, 0x10, 0x00, 0x7A, 0x02, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x47, 0xC8, 0x04, 0x68, 0x68 } },
    { { 0x3D, 0x48, 0x48, 0x48, 0xE5, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x33, 0x33, 0x14, 0xAF, 0x82, 0x3D, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xC0, 0x5A, 0x47, 0x20, 0x07, 0x82, 0x82 } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x6D, 0x6D, 0x63, 0x82, 0xA5, 0xAF, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x47, 0xC8, 0x04, 0x8C, 0x4F } },
    { { 0x19, 0x00, 0x00, 0x00, 0xE5, 0xB5, 0x33, 0x00, 0x00, 0x00, 0x33, 0x14, 0x14, 0x77, 0x19, 0x33, 0x00, 0x00, 0xFA, 0x46, 0x00, 0xA0, 0x0C, 0x47, 0x28, 0x04, 0x33, 0x49 } },
    { { 0x82, 0x00, 0x00, 0x00, 0x2D, 0x2D, 0x38, 0x00, 0x00, 0x00, 0x4F, 0xB5, 0xFF, 0x00, 0x00, 0x19, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x3D, 0x59 } },
    { { 0x28, 0x48, 0x48, 0x48, 0x6D, 0xB5, 0x4F, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0x77, 0x0A, 0x19, 0x05, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0xFC, 0x49, 0x82 } },
    { { 0x63, 0x48, 0x48, 0x48, 0xA0, 0xC6, 0xC6, 0xB8, 0xB8, 0xB8, 0x28, 0x33, 0xAA, 0x04, 0x3F, 0x28, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x4F, 0x3D } },
    { { 0x1E, 0x48, 0x48, 0x48, 0x8C, 0x77, 0x4F, 0xB8, 0xB8, 0xB8, 0x33, 0x59, 0x77, 0x19, 0x14, 0x07, 0x00, 0x00, 0xFA, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0xFC, 0x19, 0x38 } },
};

static const Oot3dExitEntry oot3d_spot06_info_setup_2_exits[] = {
    { 393, 393u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 421, 421u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 16, 16u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 634, 634u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 924, 924u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot06_info_setup_2_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot06_info_setup_2_sound_settings[] = {
    { 2u, 1413u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot06_info_setup_2_cutscenes[] = {
    { 0x0000DB1Cu, 1u },
};

static const Oot3dMiscSettings oot3d_spot06_info_setup_2_misc_settings[] = {
    { 0u, 0x00000006u },
};

static const Oot3dSceneCommand oot3d_spot06_info_setup_3_commands[] = {
    { 0x00130215u, 0x010005D4u },
    { 0x00000104u, 0x000118CCu },
    { 0x0000020Eu, 0x00011910u },
    { 0x00000019u, 0x00000006u },
    { 0x00000003u, 0x0000D3C4u },
    { 0x00000106u, 0x00011930u },
    { 0x00000007u, 0x00000002u },
    { 0x0000010Du, 0x00011968u },
    { 0x00000100u, 0x00011970u },
    { 0x00000011u, 0x00010001u },
    { 0x00000013u, 0x00011980u },
    { 0x0000010Fu, 0x0001198Cu },
    { 0x00000017u, 0x000119A8u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot06_info_setup_3_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot06_info_setup_3_paths_0_points[] = {
    { -16384, 447, 0 },
    { 0, 7, 0 },
    { 6460, 1, -4445 },
    { -904, 2475, -4029 },
    { -883, 2463, -4009 },
    { -1128, 2443, -3449 },
    { -1128, 3003, -3449 },
};

static const Oot3dPathRecord oot3d_spot06_info_setup_3_paths[] = {
    { 7u, 0u, 0u, 71996u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot06_info_setup_3_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot06_info_setup_3_spawns[] = {
    { -3465, { -1272, 3755, 0 }, { 7, 0, 6460 }, 1 },
};

static const Oot3dEntranceEntry oot3d_spot06_info_setup_3_entrances[] = {
    { 0u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot06_info_setup_3_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 0, -1 }, ACTOR_EN_DOOR, { -2465, -1033, 3857 }, 16384, 447 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot06_info_setup_3_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x00, 0x89, 0x01, 0xA5, 0x01, 0x10, 0x00, 0x7A, 0x02, 0x9C, 0x03, 0x00, 0x00, 0x00, 0xB0, 0xFE, 0x46, 0x00, 0xB0, 0x33, 0x47, 0x1E, 0xFF, 0x81, 0x81 } },
};

static const Oot3dExitEntry oot3d_spot06_info_setup_3_exits[] = {
    { 393, 393u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 421, 421u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 16, 16u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 634, 634u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 924, 924u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot06_info_setup_3_skybox_settings[] = {
    { 1u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot06_info_setup_3_sound_settings[] = {
    { 2u, 1492u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot06_info_setup_3_cutscenes[] = {
    { 0x000119A8u, 1u },
};

static const Oot3dMiscSettings oot3d_spot06_info_setup_3_misc_settings[] = {
    { 0u, 0x00000006u },
};

static const Oot3dActorEntry oot3d_spot06_info_spot06_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { -4217, -873, 2340 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_KAKASI3, { -164, -1293, 2913 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_GS, { -3682, -1038, 3212 }, { 0, -2366, 0 }, 14339 },
    { ACTOR_EN_GS, { -3749, -1336, 7877 }, { 0, 24757, 0 }, 14600 },
    { ACTOR_EN_GS, { 1683, -1340, 8317 }, { 0, -21844, 0 }, 14863 },
    { ACTOR_EN_ITEM00, { -2523, -258, 3991 }, { 0, 0, 0 }, 7686 },
    { ACTOR_BG_SPOT06_OBJECTS, { -916, -2401, 6437 }, { 0, -32767, 0 }, 31 },
    { ACTOR_BG_SPOT06_OBJECTS, { -916, -2176, 6370 }, { 0, -32767, 0 }, 287 },
    { ACTOR_BG_SPOT06_OBJECTS, { -1056, -1313, 5010 }, { 0, 0, 0 }, 767 },
    { ACTOR_BG_SPOT06_OBJECTS, { -916, -1553, 3677 }, { 0, 0, 0 }, 1023 },
    { ACTOR_DOOR_WARP1, { -1045, -1223, 7460 }, { 0, 0, 0 }, 6 },
    { ACTOR_DOOR_ANA, { -3040, -1033, 6075 }, { 0, 0, 4 }, 239 },
    { ACTOR_EN_KAKASI2, { 1187, -1269, 3322 }, { 0, -16384, 20 }, 1339 },
    { ACTOR_EN_KAKASI2, { 638, -1298, 7189 }, { 0, -16384, 20 }, 1660 },
    { ACTOR_EN_KAKASI2, { -2623, -753, 3831 }, { 0, -32767, 20 }, 1341 },
    { ACTOR_EN_CROW, { -2855, -829, 5007 }, { 0, -22390, 0 }, 0 },
    { ACTOR_EN_CROW, { -2781, -19, 3823 }, { 0, -22390, 0 }, 0 },
    { ACTOR_EN_CROW, { -2085, -1034, 6447 }, { 0, -15838, 0 }, 0 },
    { ACTOR_EN_CROW, { 1788, -931, 8220 }, { 0, -18568, 0 }, 0 },
    { ACTOR_EN_A_OBJ, { -1835, -1013, 995 }, { 0, 23848, 0 }, 266 },
    { ACTOR_EN_SW, { -731, -699, 7445 }, { 0, -24211, 0 }, -19696 },
    { ACTOR_EN_WEATHER_TAG, { -906, -1200, 8012 }, { 0, 0, 0 }, 10243 },
    { ACTOR_EN_TITE, { -2842, -1031, 2953 }, { 0, 18751, 0 }, -2 },
    { ACTOR_EN_TITE, { -2567, -1326, 8245 }, { 0, -6372, 0 }, -2 },
    { ACTOR_EN_TITE, { -1746, -1360, 3761 }, { 0, -32767, 0 }, -2 },
    { ACTOR_EN_TITE, { 746, -1453, 3297 }, { 0, -24211, 0 }, -2 },
    { ACTOR_EN_TITE, { -813, -1120, 2543 }, { 0, -5643, 0 }, -2 },
    { ACTOR_EN_TITE, { -628, -1333, 3318 }, { 0, -23119, 0 }, -2 },
    { ACTOR_EN_TITE, { 2223, -1398, 6863 }, { 0, -23665, 0 }, -2 },
    { ACTOR_EN_TITE, { 55, -1313, 8685 }, { 0, 5643, 0 }, -2 },
    { ACTOR_EN_ISHI, { 1221, -1218, 3952 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_KANBAN, { -2300, -1033, 3670 }, { 0, -29127, 0 }, 792 },
    { ACTOR_EN_KUSA, { -661, -1243, 7352 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_KUSA, { -613, -1243, 7369 }, { 0, 0, 0 }, 1792 },
    { ACTOR_OBJ_MURE2, { -2246, -985, 3007 }, { 0, 0, 0 }, 1793 },
    { ACTOR_OBJ_MURE2, { -2071, -906, 2500 }, { 0, 0, 0 }, 1793 },
    { ACTOR_OBJ_MURE2, { -1680, -1014, 2753 }, { 0, 0, 0 }, 1793 },
    { ACTOR_SHOT_SUN, { -575, -1233, 7260 }, { 0, -16384, 0 }, -255 },
    { ACTOR_SHOT_SUN, { 814, -1298, 7319 }, { 0, 0, 0 }, -192 },
    { ACTOR_OBJ_BEAN, { -2602, -1033, 3617 }, { 0, 0, 2 }, 257 },
    { ACTOR_EN_WONDER_TALK2, { -491, -1216, 7259 }, { 0, -16384, 41 }, -30721 },
    { ACTOR_EN_WONDER_TALK2, { 1341, -1180, 3778 }, { 0, -16384, 41 }, -28993 },
    { ACTOR_BG_MJIN, { -1045, -1243, 7457 }, { 0, 0, 0 }, 4 },
    { ACTOR_BG_HAKA, { -3042, -1033, 6074 }, { 0, 16384, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_spot06_info_spot06_0_info_objects[] = {
    { OBJECT_SPOT06_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MJIN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MJIN_ICE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_CROW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HORSE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_M_ARROW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HAKA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WARP1, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_spot06_info_room_refs[] = {
    { "spot06_0_info.zsi", 0 },
};

static const Oot3dSceneSetupIndex oot3d_spot06_info_setups[] = {
    { 0u, oot3d_spot06_info_setup_0_commands, 13u, oot3d_spot06_info_setup_0_special_files, 1u, oot3d_spot06_info_setup_0_paths, 1u, NULL, 0u, oot3d_spot06_info_setup_0_spawns, 8u, oot3d_spot06_info_setup_0_entrances, 9u, oot3d_spot06_info_setup_0_transition_actors, 2u, oot3d_spot06_info_setup_0_light_settings, 12u, oot3d_spot06_info_setup_0_exits, 8u, oot3d_spot06_info_setup_0_skybox_settings, 1u, oot3d_spot06_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_spot06_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_spot06_info_setup_1_commands, 13u, oot3d_spot06_info_setup_1_special_files, 1u, oot3d_spot06_info_setup_1_paths, 2u, NULL, 0u, oot3d_spot06_info_setup_1_spawns, 9u, oot3d_spot06_info_setup_1_entrances, 10u, oot3d_spot06_info_setup_1_transition_actors, 2u, oot3d_spot06_info_setup_1_light_settings, 12u, oot3d_spot06_info_setup_1_exits, 8u, oot3d_spot06_info_setup_1_skybox_settings, 1u, oot3d_spot06_info_setup_1_sound_settings, 1u, NULL, 0u, oot3d_spot06_info_setup_1_misc_settings, 1u },
    { 2u, oot3d_spot06_info_setup_2_commands, 14u, oot3d_spot06_info_setup_2_special_files, 1u, oot3d_spot06_info_setup_2_paths, 1u, NULL, 0u, oot3d_spot06_info_setup_2_spawns, 1u, oot3d_spot06_info_setup_2_entrances, 1u, oot3d_spot06_info_setup_2_transition_actors, 2u, oot3d_spot06_info_setup_2_light_settings, 8u, oot3d_spot06_info_setup_2_exits, 6u, oot3d_spot06_info_setup_2_skybox_settings, 1u, oot3d_spot06_info_setup_2_sound_settings, 1u, oot3d_spot06_info_setup_2_cutscenes, 1u, oot3d_spot06_info_setup_2_misc_settings, 1u },
    { 3u, oot3d_spot06_info_setup_3_commands, 14u, oot3d_spot06_info_setup_3_special_files, 1u, oot3d_spot06_info_setup_3_paths, 1u, NULL, 0u, oot3d_spot06_info_setup_3_spawns, 1u, oot3d_spot06_info_setup_3_entrances, 1u, oot3d_spot06_info_setup_3_transition_actors, 2u, oot3d_spot06_info_setup_3_light_settings, 1u, oot3d_spot06_info_setup_3_exits, 6u, oot3d_spot06_info_setup_3_skybox_settings, 1u, oot3d_spot06_info_setup_3_sound_settings, 1u, oot3d_spot06_info_setup_3_cutscenes, 1u, oot3d_spot06_info_setup_3_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_spot06_info_rooms[] = {
    { "spot06_0_info.zsi", 0, oot3d_spot06_info_spot06_0_info_objects, 15u, oot3d_spot06_info_spot06_0_info_actors, 44u },
};

const Oot3dSceneIndex oot3d_scene_index_spot06_info = {
    "spot06_info.zsi",
    oot3d_spot06_info_room_refs, 1u,
    oot3d_spot06_info_rooms, 1u,
    oot3d_spot06_info_setups, 4u,
};
