/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: spot07_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_spot07_info_setup_0_commands[] = {
    { 0x00130415u, 0x010005BDu },
    { 0x00000204u, 0x00000150u },
    { 0x0000020Eu, 0x000001D8u },
    { 0x00000019u, 0x00000007u },
    { 0x00000003u, 0x0000866Cu },
    { 0x00000506u, 0x00008698u },
    { 0x00000007u, 0x00000002u },
    { 0x0000020Du, 0x000086CCu },
    { 0x00000500u, 0x000086DCu },
    { 0x00000011u, 0x00000000u },
    { 0x00000013u, 0x0000872Cu },
    { 0x00000C0Fu, 0x00008734u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot07_info_setup_0_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot07_info_setup_0_paths_0_points[] = {
    { 2, 0, -31052 },
    { 0, 2, 0 },
};

static const Oot3dVec3s oot3d_spot07_info_setup_0_paths_1_points[] = {
    { -31040, 0, 452 },
    { 887, -1631, 802 },
};

static const Oot3dPathRecord oot3d_spot07_info_setup_0_paths[] = {
    { 2u, 0u, 0u, 34484u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot07_info_setup_0_paths_0_points },
    { 2u, 0u, 0u, 34496u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot07_info_setup_0_paths_1_points },
};

static const Oot3dActorEntry oot3d_spot07_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { -1112, 210, -160 }, { 0, 16384, 0 }, 4095 },
    { ACTOR_PLAYER, { 540, 996, -2503 }, { 0, 6372, 0 }, 4095 },
    { ACTOR_PLAYER, { 524, 52, 254 }, { 0, 9284, 0 }, 4095 },
    { ACTOR_PLAYER, { 623, 947, -1499 }, { 0, -24940, 0 }, 3583 },
};

static const Oot3dEntranceEntry oot3d_spot07_info_setup_0_entrances[] = {
    { 0u, 1 },
    { 1u, 0 },
    { 2u, 1 },
    { 3u, 0 },
    { 4u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot07_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 1, -1 }, ACTOR_EN_HOLL, { -195, 857, -1350 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot07_info_setup_0_light_settings[] = {
    { { 0x00, 0x00, 0x39, 0x8E, 0x00, 0x00, 0xFF, 0x0F, 0x9D, 0x01, 0x25, 0x02, 0x80, 0x03, 0x60, 0x05, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x44, 0x90, 0x05, 0x63, 0x63 } },
    { { 0x77, 0x48, 0x48, 0x48, 0xC6, 0xDB, 0xDB, 0xB8, 0xB8, 0xB8, 0x05, 0x4F, 0x82, 0x63, 0x7C, 0xAA, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x44, 0x90, 0x05, 0x6D, 0x6D } },
    { { 0x7C, 0x48, 0x48, 0x48, 0xCC, 0xE5, 0xE5, 0xB8, 0xB8, 0xB8, 0x05, 0x4F, 0x82, 0x68, 0x87, 0xB5, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x44, 0x90, 0x05, 0x63, 0x63 } },
    { { 0x77, 0x48, 0x48, 0x48, 0xC6, 0xDB, 0xDB, 0xB8, 0xB8, 0xB8, 0x05, 0x4F, 0x82, 0x63, 0x77, 0xAA, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x44, 0x90, 0x05, 0x5E, 0x5E } },
    { { 0x77, 0x48, 0x48, 0x48, 0xD1, 0xD1, 0xDB, 0xB8, 0xB8, 0xB8, 0x05, 0x4F, 0x82, 0x4F, 0x63, 0xAA, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x45, 0x28, 0xFC, 0x49, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0xEF, 0xFF, 0xB8, 0xB8, 0xB8, 0x14, 0x8C, 0xB5, 0x0F, 0x3D, 0x68, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x45, 0x28, 0xFC, 0x49, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0xEF, 0xFF, 0xB8, 0xB8, 0xB8, 0x14, 0x8C, 0xB5, 0x0F, 0x3D, 0x68, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x45, 0x28, 0xFC, 0x49, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0xEF, 0xFF, 0xB8, 0xB8, 0xB8, 0x14, 0x8C, 0xB5, 0x0F, 0x3D, 0x68, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x45, 0x28, 0xFC, 0x49, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0xEF, 0xFF, 0xB8, 0xB8, 0xB8, 0x14, 0x8C, 0xB5, 0x0F, 0x3D, 0x68, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x00, 0xFA, 0x44, 0x28, 0x00, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xDB, 0xDB, 0xDB, 0x00, 0x00, 0x00, 0x33, 0x33, 0x59, 0xAA, 0xBA, 0xD1, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x00, 0xFA, 0x44, 0x28, 0x00, 0x7C, 0x7C } },
    { { 0x82, 0x00, 0x00, 0x00, 0xE5, 0xE5, 0xE5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x59, 0xB5, 0xC1, 0xDB, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x00, 0xFA, 0x44, 0x28, 0x00, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xDB, 0xDB, 0xDB, 0x00, 0x00, 0x00, 0x33, 0x33, 0x59, 0xAA, 0xBA, 0xD1, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x00, 0xFA, 0x44, 0x28, 0x00, 0x63, 0x63 } },
};

static const Oot3dExitEntry oot3d_spot07_info_setup_0_exits[] = {
    { 413, 413u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 549, 549u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 896, 896u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1376, 1376u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot07_info_setup_0_skybox_settings[] = {
    { 0u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot07_info_setup_0_sound_settings[] = {
    { 4u, 1469u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot07_info_setup_0_misc_settings[] = {
    { 0u, 0x00000007u },
};

static const Oot3dSceneCommand oot3d_spot07_info_setup_1_commands[] = {
    { 0x00130415u, 0x010005BDu },
    { 0x00000204u, 0x00008884u },
    { 0x0000020Eu, 0x0000890Cu },
    { 0x00000019u, 0x00000007u },
    { 0x00000003u, 0x0000866Cu },
    { 0x00000506u, 0x0000892Cu },
    { 0x00000107u, 0x00000002u },
    { 0x0000010Du, 0x0000894Cu },
    { 0x00000500u, 0x00008954u },
    { 0x00000011u, 0x00000000u },
    { 0x00000013u, 0x000089A4u },
    { 0x00000C0Fu, 0x000089ACu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot07_info_setup_1_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot07_info_setup_1_paths_0_points[] = {
    { 258, 3, 260 },
    { 0, 2, 0 },
};

static const Oot3dPathRecord oot3d_spot07_info_setup_1_paths[] = {
    { 2u, 0u, 0u, 35136u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot07_info_setup_1_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot07_info_setup_1_spawns[] = {
    { ACTOR_PLAYER, { -1150, 210, -150 }, { 0, 16384, 0 }, 4095 },
    { ACTOR_PLAYER, { 537, 996, -2501 }, { 0, 6372, 0 }, 4095 },
    { ACTOR_PLAYER, { 520, 52, 248 }, { 0, 9284, 0 }, 3839 },
    { ACTOR_PLAYER, { 617, 947, -1507 }, { 0, -28580, 0 }, 3583 },
};

static const Oot3dEntranceEntry oot3d_spot07_info_setup_1_entrances[] = {
    { 0u, 1 },
    { 1u, 0 },
    { 2u, 1 },
    { 3u, 0 },
    { 4u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot07_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 1, -1 }, ACTOR_EN_HOLL, { -195, 857, -1350 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot07_info_setup_1_light_settings[] = {
    { { 0x00, 0x00, 0x39, 0x8E, 0x00, 0x00, 0xFF, 0x0F, 0x9D, 0x01, 0x25, 0x02, 0x80, 0x03, 0x60, 0x05, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x44, 0x90, 0x05, 0x63, 0x63 } },
    { { 0x77, 0x48, 0x48, 0x48, 0xC6, 0xDB, 0xDB, 0xB8, 0xB8, 0xB8, 0x05, 0x4F, 0x82, 0x63, 0x7C, 0xAA, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x44, 0x90, 0x05, 0x6D, 0x6D } },
    { { 0x7C, 0x48, 0x48, 0x48, 0xCC, 0xE5, 0xE5, 0xB8, 0xB8, 0xB8, 0x05, 0x4F, 0x82, 0x68, 0x87, 0xB5, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x44, 0x90, 0x05, 0x63, 0x63 } },
    { { 0x77, 0x48, 0x48, 0x48, 0xC6, 0xDB, 0xDB, 0xB8, 0xB8, 0xB8, 0x05, 0x4F, 0x82, 0x63, 0x77, 0xAA, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xFA, 0x44, 0x90, 0x05, 0x5E, 0x5E } },
    { { 0x77, 0x48, 0x48, 0x48, 0xD1, 0xD1, 0xDB, 0xB8, 0xB8, 0xB8, 0x05, 0x4F, 0x82, 0x4F, 0x63, 0xAA, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x45, 0x28, 0xFC, 0x49, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0xEF, 0xFF, 0xB8, 0xB8, 0xB8, 0x14, 0x8C, 0xB5, 0x0F, 0x3D, 0x68, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x45, 0x28, 0xFC, 0x49, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0xEF, 0xFF, 0xB8, 0xB8, 0xB8, 0x14, 0x8C, 0xB5, 0x0F, 0x3D, 0x68, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x45, 0x28, 0xFC, 0x49, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0xEF, 0xFF, 0xB8, 0xB8, 0xB8, 0x14, 0x8C, 0xB5, 0x0F, 0x3D, 0x68, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x7A, 0x45, 0x28, 0xFC, 0x49, 0x59 } },
    { { 0x8C, 0x48, 0x48, 0x48, 0x33, 0xEF, 0xFF, 0xB8, 0xB8, 0xB8, 0x14, 0x8C, 0xB5, 0x0F, 0x3D, 0x68, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x00, 0xFA, 0x44, 0x28, 0x00, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xDB, 0xDB, 0xDB, 0x00, 0x00, 0x00, 0x33, 0x33, 0x59, 0xAA, 0xBA, 0xD1, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x00, 0xFA, 0x44, 0x28, 0x00, 0x7C, 0x7C } },
    { { 0x82, 0x00, 0x00, 0x00, 0xE5, 0xE5, 0xE5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x59, 0xB5, 0xC1, 0xDB, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x00, 0xFA, 0x44, 0x28, 0x00, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xDB, 0xDB, 0xDB, 0x00, 0x00, 0x00, 0x33, 0x33, 0x59, 0xAA, 0xBA, 0xD1, 0x00, 0x40, 0xB5, 0x45, 0x00, 0x00, 0xFA, 0x44, 0x28, 0x00, 0x63, 0x63 } },
};

static const Oot3dExitEntry oot3d_spot07_info_setup_1_exits[] = {
    { 413, 413u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 549, 549u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 896, 896u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1376, 1376u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot07_info_setup_1_skybox_settings[] = {
    { 0u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot07_info_setup_1_sound_settings[] = {
    { 4u, 1469u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot07_info_setup_1_misc_settings[] = {
    { 0u, 0x00000007u },
};

static const Oot3dSceneCommand oot3d_spot07_info_setup_2_commands[] = {
    { 0x00130415u, 0x010005D4u },
    { 0x00000204u, 0x00008AFCu },
    { 0x0000020Eu, 0x00008B84u },
    { 0x00000019u, 0x00000007u },
    { 0x00000003u, 0x0000866Cu },
    { 0x00000106u, 0x00008BA4u },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x00008BA8u },
    { 0x00000011u, 0x00010000u },
    { 0x00000013u, 0x00008BB8u },
    { 0x0000040Fu, 0x00008BC0u },
    { 0x00000017u, 0x00008C30u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot07_info_setup_2_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot07_info_setup_2_spawns[] = {
    { ACTOR_EN_HOLL, { 625, 619, -689 }, { 0, 319, 0 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot07_info_setup_2_entrances[] = {
    { 0u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot07_info_setup_2_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 0, -1 }, { 1, -1 }, ACTOR_EN_HOLL, { -195, 857, -1350 }, 0, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot07_info_setup_2_light_settings[] = {
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x9D, 0x01, 0x25, 0x02, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xC3, 0x44, 0x70, 0x04, 0x69, 0x59 } },
    { { 0x59, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xEF, 0xB8, 0xB8, 0xB8, 0x31, 0x31, 0x59, 0x17, 0x4D, 0x64, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x18, 0x07, 0x69, 0x59 } },
    { { 0x59, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xEF, 0xB8, 0xB8, 0xB8, 0x31, 0x31, 0x59, 0x18, 0x64, 0x64, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x18, 0x07, 0x69, 0x59 } },
    { { 0x59, 0x48, 0x48, 0x48, 0x31, 0x31, 0x59, 0xB8, 0xB8, 0xB8, 0xFF, 0xFF, 0xEF, 0x18, 0x64, 0x64, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x18, 0x07, 0x69, 0x59 } },
};

static const Oot3dExitEntry oot3d_spot07_info_setup_2_exits[] = {
    { 413, 413u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 549, 549u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 896, 896u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot07_info_setup_2_skybox_settings[] = {
    { 0u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot07_info_setup_2_sound_settings[] = {
    { 4u, 1492u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot07_info_setup_2_cutscenes[] = {
    { 0x00008C30u, 1u },
};

static const Oot3dMiscSettings oot3d_spot07_info_setup_2_misc_settings[] = {
    { 0u, 0x00000007u },
};

static const Oot3dActorEntry oot3d_spot07_info_spot07_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 641, 856, -1691 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_KZ, { 628, 996, -1780 }, { 0, 0, 0 }, 256 },
    { ACTOR_EN_RU1, { 628, 996, -1800 }, { 0, 5461, 0 }, 5 },
    { ACTOR_BG_SPOT07_TAKI, { 445, 1008, -1741 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_A_OBJ, { 345, 856, -1571 }, { 0, -32767, 0 }, 11018 },
    { ACTOR_OBJ_SYOKUDAI, { 462, 886, -1275 }, { 0, 0, 0 }, 9216 },
    { ACTOR_OBJ_SYOKUDAI, { 778, 886, -1275 }, { 0, 0, 0 }, 9216 },
    { ACTOR_EN_KANBAN, { 720, 890, -1345 }, { 0, 0, 0 }, 804 },
    { ACTOR_EN_DIVING_GAME, { -149, 856, -1020 }, { 0, -23848, 0 }, -1 },
    { ACTOR_OBJ_COMB, { 382, 1173, -1336 }, { 0, 0, 0 }, -254 },
    { ACTOR_OBJ_COMB, { 948, 1216, -1500 }, { 0, 0, 0 }, -254 },
    { ACTOR_OBJ_COMB, { 701, 1250, -2056 }, { 0, -8374, 0 }, -254 },
    { ACTOR_OBJ_SYOKUDAI, { 644, 402, -157 }, { 0, -22755, 0 }, 8192 },
    { ACTOR_EN_GS, { 620, 856, -1600 }, { 0, 0, 0 }, 14345 },
};

static const Oot3dRoomObjectEntry oot3d_spot07_info_spot07_0_info_objects[] = {
    { OBJECT_SPOT07_OBJECT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SYOKUDAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot07_info_spot07_1_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { -189, 17, -941 }, { 0, 0, 0 }, 3 },
    { ACTOR_EN_ZO, { -786, 15, -486 }, { 0, -181, 0 }, -64 },
    { ACTOR_EN_ZO, { 822, 33, 77 }, { 0, -18203, 0 }, -63 },
    { ACTOR_EN_ZO, { 241, -75, -384 }, { 0, -1820, 0 }, -62 },
    { ACTOR_EN_ZO, { -298, -200, -217 }, { 0, -8737, 0 }, -61 },
    { ACTOR_EN_ZO, { -589, -120, -621 }, { 0, 11651, 0 }, -60 },
    { ACTOR_EN_ZO, { -936, -319, -352 }, { 0, -3641, 0 }, -59 },
    { ACTOR_BG_SPOT07_TAKI, { 0, 0, 0 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_A_OBJ, { 217, 178, 150 }, { 0, 12014, 0 }, 15114 },
    { ACTOR_OBJ_MURE, { 653, -10, -485 }, { 0, 0, 0 }, 21282 },
    { ACTOR_OBJ_MURE2, { 462, -20, -776 }, { 0, 0, 0 }, 514 },
    { ACTOR_EN_KANBAN, { -980, 210, -210 }, { 0, -11832, 0 }, 782 },
    { ACTOR_EN_KANBAN, { 477, 60, 318 }, { 0, 7281, 0 }, 819 },
    { ACTOR_EN_DIVING_GAME, { -149, 856, -1020 }, { 0, -23848, 0 }, -1 },
    { ACTOR_DOOR_ANA, { -860, 14, -470 }, { 0, -10922, 0 }, 4607 },
    { ACTOR_EN_BOX, { -210, 8, -1079 }, { 0, -32767, 31 }, -18496 },
    { ACTOR_OBJ_SYOKUDAI, { 644, 402, -157 }, { 0, -22755, 0 }, 8192 },
    { ACTOR_OBJ_SYOKUDAI, { 586, 51, 204 }, { 0, 0, -181 }, 4383 },
    { ACTOR_OBJ_SYOKUDAI, { 552, -20, -910 }, { 0, 0, -181 }, 4383 },
    { ACTOR_OBJ_SYOKUDAI, { -260, 8, -1030 }, { 0, 0, -181 }, 4383 },
    { ACTOR_OBJ_SYOKUDAI, { -130, 8, -1030 }, { 0, 0, -181 }, 4383 },
    { ACTOR_OBJ_TSUBO, { 614, 67, 419 }, { 0, 0, 0 }, 16641 },
    { ACTOR_OBJ_TSUBO, { 289, 114, 415 }, { 0, 0, 0 }, 17155 },
    { ACTOR_OBJ_TSUBO, { 676, 53, 377 }, { 0, 0, 0 }, 17667 },
    { ACTOR_OBJ_TSUBO, { 289, 128, 289 }, { 0, 0, 0 }, 18188 },
    { ACTOR_OBJ_TSUBO, { 220, 130, 384 }, { 0, 0, 0 }, 18701 },
};

static const Oot3dRoomObjectEntry oot3d_spot07_info_spot07_1_info_objects[] = {
    { OBJECT_SPOT07_OBJECT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SYOKUDAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_UNSET_10, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_spot07_info_room_refs[] = {
    { "spot07_0_info.zsi", 0 },
    { "spot07_1_info.zsi", 1 },
};

static const Oot3dSceneSetupIndex oot3d_spot07_info_setups[] = {
    { 0u, oot3d_spot07_info_setup_0_commands, 13u, oot3d_spot07_info_setup_0_special_files, 1u, oot3d_spot07_info_setup_0_paths, 2u, NULL, 0u, oot3d_spot07_info_setup_0_spawns, 4u, oot3d_spot07_info_setup_0_entrances, 5u, oot3d_spot07_info_setup_0_transition_actors, 2u, oot3d_spot07_info_setup_0_light_settings, 12u, oot3d_spot07_info_setup_0_exits, 4u, oot3d_spot07_info_setup_0_skybox_settings, 1u, oot3d_spot07_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_spot07_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_spot07_info_setup_1_commands, 13u, oot3d_spot07_info_setup_1_special_files, 1u, oot3d_spot07_info_setup_1_paths, 1u, NULL, 0u, oot3d_spot07_info_setup_1_spawns, 4u, oot3d_spot07_info_setup_1_entrances, 5u, oot3d_spot07_info_setup_1_transition_actors, 2u, oot3d_spot07_info_setup_1_light_settings, 12u, oot3d_spot07_info_setup_1_exits, 4u, oot3d_spot07_info_setup_1_skybox_settings, 1u, oot3d_spot07_info_setup_1_sound_settings, 1u, NULL, 0u, oot3d_spot07_info_setup_1_misc_settings, 1u },
    { 2u, oot3d_spot07_info_setup_2_commands, 13u, oot3d_spot07_info_setup_2_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot07_info_setup_2_spawns, 1u, oot3d_spot07_info_setup_2_entrances, 1u, oot3d_spot07_info_setup_2_transition_actors, 2u, oot3d_spot07_info_setup_2_light_settings, 4u, oot3d_spot07_info_setup_2_exits, 4u, oot3d_spot07_info_setup_2_skybox_settings, 1u, oot3d_spot07_info_setup_2_sound_settings, 1u, oot3d_spot07_info_setup_2_cutscenes, 1u, oot3d_spot07_info_setup_2_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_spot07_info_rooms[] = {
    { "spot07_0_info.zsi", 0, oot3d_spot07_info_spot07_0_info_objects, 8u, oot3d_spot07_info_spot07_0_info_actors, 14u },
    { "spot07_1_info.zsi", 1, oot3d_spot07_info_spot07_1_info_objects, 8u, oot3d_spot07_info_spot07_1_info_actors, 26u },
};

const Oot3dSceneIndex oot3d_scene_index_spot07_info = {
    "spot07_info.zsi",
    oot3d_spot07_info_room_refs, 2u,
    oot3d_spot07_info_rooms, 2u,
    oot3d_spot07_info_setups, 3u,
};
