/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: spot18_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_spot18_info_setup_0_commands[] = {
    { 0x00130315u, 0x0100059Eu },
    { 0x00000404u, 0x000001C4u },
    { 0x0000030Eu, 0x000002D4u },
    { 0x00000019u, 0x00000012u },
    { 0x00000003u, 0x00009E78u },
    { 0x00000406u, 0x00009EA4u },
    { 0x00000107u, 0x00000002u },
    { 0x0000010Du, 0x00009F08u },
    { 0x00000400u, 0x00009F10u },
    { 0x00000011u, 0x00010000u },
    { 0x00000013u, 0x00009F50u },
    { 0x0000040Fu, 0x00009F58u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot18_info_setup_0_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot18_info_setup_0_paths_0_points[] = {
    { 768, 257, 770 },
    { 771, 14, 0 },
    { -24908, 0, 520 },
    { 399, 565, 45 },
    { 400, 767, -259 },
    { 397, 556, -495 },
    { 400, 552, -435 },
    { 397, 230, -568 },
    { 400, -22, -434 },
    { 397, -194, -478 },
    { 400, -662, -185 },
    { 399, -662, -69 },
    { 401, -874, 401 },
    { 397, -530, 570 },
};

static const Oot3dPathRecord oot3d_spot18_info_setup_0_paths[] = {
    { 14u, 0u, 0u, 40628u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot18_info_setup_0_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot18_info_setup_0_spawns[] = {
    { ACTOR_OBJ_SWITCH, { 520, 399, 565 }, { 14, 0, -24908 }, 0 },
    { ACTOR_PLAYER, { 56, 600, 1104 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { 90, 40, -1416 }, { 0, 0, 0 }, 3328 },
    { ACTOR_PLAYER, { -134, -3, -42 }, { 0, 19661, 0 }, 3583 },
};

static const Oot3dEntranceEntry oot3d_spot18_info_setup_0_entrances[] = {
    { 0u, 3 },
    { 1u, 1 },
    { 2u, 3 },
    { 3u, 3 },
};

static const Oot3dTransitionActorEntry oot3d_spot18_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -332, 375, 935 }, 31675, 319 },
    { { 3, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { -1058, 600, -178 }, -16384, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot18_info_setup_0_light_settings[] = {
    { { 0x00, 0x00, 0xCE, 0x8C, 0x00, 0x00, 0xFF, 0x0D, 0xB9, 0x01, 0x46, 0x02, 0x7C, 0x03, 0xD6, 0x04, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xC8, 0x44, 0x78, 0x10, 0x7F, 0x77 } },
    { { 0x72, 0x3F, 0x7F, 0x3F, 0xBF, 0xB2, 0x99, 0xC1, 0x81, 0xC1, 0x11, 0x0F, 0x19, 0x1E, 0x13, 0x11, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xE1, 0x44, 0xC8, 0x10, 0x91, 0x8C } },
    { { 0x63, 0x3F, 0x7F, 0x3F, 0xE5, 0xCC, 0xB2, 0xC1, 0x81, 0xC1, 0x1E, 0x19, 0x59, 0x33, 0x28, 0x1E, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xAA, 0x44, 0x78, 0x10, 0x99, 0x77 } },
    { { 0x66, 0x00, 0x3F, 0x0C, 0x8C, 0x59, 0x33, 0x00, 0xC1, 0xF4, 0xCC, 0x66, 0x4C, 0x3F, 0x2B, 0x0C, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xC8, 0x44, 0x78, 0x10, 0x91, 0x8C } },
};

static const Oot3dExitEntry oot3d_spot18_info_setup_0_exits[] = {
    { 441, 441u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 582, 582u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 892, 892u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1238, 1238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot18_info_setup_0_skybox_settings[] = {
    { 0u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot18_info_setup_0_sound_settings[] = {
    { 3u, 1438u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot18_info_setup_0_misc_settings[] = {
    { 0u, 0x00000012u },
};

static const Oot3dSceneCommand oot3d_spot18_info_setup_1_commands[] = {
    { 0x00130315u, 0x0100059Eu },
    { 0x00000404u, 0x00009FC8u },
    { 0x0000030Eu, 0x0000A0D8u },
    { 0x00000019u, 0x00000012u },
    { 0x00000003u, 0x00009E78u },
    { 0x00000406u, 0x0000A108u },
    { 0x00000107u, 0x00000002u },
    { 0x0000010Du, 0x0000A15Cu },
    { 0x00000400u, 0x0000A164u },
    { 0x00000011u, 0x00010000u },
    { 0x00000013u, 0x0000A1A4u },
    { 0x0000040Fu, 0x0000A1ACu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot18_info_setup_1_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot18_info_setup_1_paths_0_points[] = {
    { 768, 257, 770 },
    { 771, 11, 0 },
    { -24296, 0, 252 },
    { 397, -408, 190 },
    { 400, -791, -173 },
    { 400, -831, -453 },
    { 398, -539, -515 },
    { 398, -89, -442 },
    { 400, 609, -46 },
    { 399, 736, 514 },
    { 399, 544, 516 },
};

static const Oot3dPathRecord oot3d_spot18_info_setup_1_paths[] = {
    { 11u, 0u, 0u, 41240u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot18_info_setup_1_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot18_info_setup_1_spawns[] = {
    { ACTOR_BG_JYA_COBRA, { 397, -408, 0 }, { 11, 0, -24296 }, 0 },
    { ACTOR_PLAYER, { 56, 600, 1104 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { 47, 40, -1523 }, { 0, -6007, 0 }, 3328 },
    { ACTOR_PLAYER, { -134, -3, -42 }, { 0, 19661, 0 }, 3583 },
};

static const Oot3dEntranceEntry oot3d_spot18_info_setup_1_entrances[] = {
    { 0u, 3 },
    { 1u, 1 },
    { 2u, 3 },
    { 3u, 3 },
};

static const Oot3dTransitionActorEntry oot3d_spot18_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -332, 375, 935 }, 31675, 319 },
    { { 3, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { -1058, 600, -178 }, -16384, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot18_info_setup_1_light_settings[] = {
    { { 0x00, 0x00, 0xCE, 0x8C, 0x00, 0x00, 0xFF, 0x0D, 0xB9, 0x01, 0x46, 0x02, 0x7C, 0x03, 0xD6, 0x04, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xC8, 0x44, 0x78, 0x10, 0x7F, 0x77 } },
    { { 0x72, 0x3F, 0x7F, 0x3F, 0xBF, 0xB2, 0x99, 0xC1, 0x81, 0xC1, 0x11, 0x0F, 0x19, 0x1E, 0x13, 0x11, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xE1, 0x44, 0xC8, 0x10, 0x91, 0x8C } },
    { { 0x63, 0x3F, 0x7F, 0x3F, 0xE5, 0xCC, 0xB2, 0xC1, 0x81, 0xC1, 0x1E, 0x19, 0x59, 0x33, 0x28, 0x1E, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xAA, 0x44, 0x78, 0x10, 0x99, 0x77 } },
    { { 0x66, 0x00, 0x3F, 0x0C, 0x8C, 0x59, 0x33, 0x00, 0xC1, 0xF4, 0xCC, 0x66, 0x4C, 0x3F, 0x2B, 0x0C, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xC8, 0x44, 0x78, 0x10, 0x91, 0x8C } },
};

static const Oot3dExitEntry oot3d_spot18_info_setup_1_exits[] = {
    { 441, 441u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 582, 582u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 892, 892u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1238, 1238u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot18_info_setup_1_skybox_settings[] = {
    { 0u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot18_info_setup_1_sound_settings[] = {
    { 3u, 1438u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot18_info_setup_1_misc_settings[] = {
    { 0u, 0x00000012u },
};

static const Oot3dSceneCommand oot3d_spot18_info_setup_2_commands[] = {
    { 0x00130315u, 0x0000007Fu },
    { 0x00000404u, 0x0000A21Cu },
    { 0x0000030Eu, 0x0000A32Cu },
    { 0x00000019u, 0x00000012u },
    { 0x00000003u, 0x00009E78u },
    { 0x00000106u, 0x0000A35Cu },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x0000A360u },
    { 0x00000101u, 0x0000A370u },
    { 0x00000011u, 0x00010000u },
    { 0x00000013u, 0x0000A380u },
    { 0x0000040Fu, 0x0000A388u },
    { 0x00000017u, 0x0000A3F8u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot18_info_setup_2_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot18_info_setup_2_standard_actors[] = {
    { ACTOR_PLAYER, { 90, 40, -1416 }, { 0, 0, 0 }, 3583 },
};

static const Oot3dActorEntry oot3d_spot18_info_setup_2_spawns[] = {
    { ACTOR_EN_HOLL, { -69, 31, -589 }, { -32767, 63, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot18_info_setup_2_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot18_info_setup_2_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -332, 375, 935 }, 31675, 319 },
    { { 3, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { -1058, 600, -178 }, -16384, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot18_info_setup_2_light_settings[] = {
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xB9, 0x01, 0x46, 0x02, 0x7C, 0x03, 0xD2, 0x04, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xC8, 0x44, 0x78, 0x10, 0x7F, 0x77 } },
    { { 0x72, 0x3F, 0x7F, 0x3F, 0xBF, 0xB2, 0x99, 0xC1, 0x81, 0xC1, 0x11, 0x0F, 0x19, 0x1E, 0x13, 0x11, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xE1, 0x44, 0xC8, 0x10, 0x91, 0x8C } },
    { { 0x63, 0x3F, 0x7F, 0x3F, 0xE5, 0xCC, 0xB2, 0xC1, 0x81, 0xC1, 0x1E, 0x19, 0x59, 0x33, 0x28, 0x1E, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xAA, 0x44, 0x78, 0x10, 0x99, 0x77 } },
    { { 0x66, 0x00, 0x3F, 0x0C, 0x8C, 0x59, 0x33, 0x00, 0xC1, 0xF4, 0xCC, 0x66, 0x4C, 0x3F, 0x2B, 0x0C, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0xC8, 0x44, 0x78, 0x10, 0x91, 0x8C } },
};

static const Oot3dExitEntry oot3d_spot18_info_setup_2_exits[] = {
    { 452, 452u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { -70, 65466u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { -5, 65531u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { -454, 65082u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot18_info_setup_2_skybox_settings[] = {
    { 0u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot18_info_setup_2_sound_settings[] = {
    { 3u, 127u, 0u, 0u },
};

static const Oot3dCutsceneReference oot3d_spot18_info_setup_2_cutscenes[] = {
    { 0x0000A3F8u, 1u },
};

static const Oot3dMiscSettings oot3d_spot18_info_setup_2_misc_settings[] = {
    { 0u, 0x00000012u },
};

static const Oot3dSceneCommand oot3d_spot18_info_setup_3_commands[] = {
    { 0x00130315u, 0x010005D4u },
    { 0x00000404u, 0x0000CC08u },
    { 0x0000030Eu, 0x0000CD18u },
    { 0x00000019u, 0x00000012u },
    { 0x00000003u, 0x00009E78u },
    { 0x00000106u, 0x0000CD48u },
    { 0x00000107u, 0x00000003u },
    { 0x00000100u, 0x0000CD4Cu },
    { 0x00000011u, 0x00010000u },
    { 0x00000013u, 0x0000CD5Cu },
    { 0x0000010Fu, 0x0000CD64u },
    { 0x00000017u, 0x0000CD80u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot18_info_setup_3_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_DANGEON_KEEP },
};

static const Oot3dActorEntry oot3d_spot18_info_setup_3_spawns[] = {
    { ACTOR_EN_HOLL, { -69, 31, -589 }, { -32767, 63, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot18_info_setup_3_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot18_info_setup_3_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { -332, 375, 935 }, 31675, 319 },
    { { 3, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { -1058, 600, -178 }, -16384, 319 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot18_info_setup_3_light_settings[] = {
    { { 0x00, 0x00, 0x82, 0x4D, 0x00, 0x00, 0xFF, 0x00, 0xB9, 0x01, 0x46, 0x02, 0x7C, 0x03, 0xD2, 0x04, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x0D, 0x07, 0x4F, 0x4B } },
};

static const Oot3dExitEntry oot3d_spot18_info_setup_3_exits[] = {
    { 441, 441u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 582, 582u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 892, 892u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1234, 1234u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot18_info_setup_3_skybox_settings[] = {
    { 0u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot18_info_setup_3_sound_settings[] = {
    { 3u, 1492u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot18_info_setup_3_cutscenes[] = {
    { 0x0000CD80u, 1u },
};

static const Oot3dMiscSettings oot3d_spot18_info_setup_3_misc_settings[] = {
    { 0u, 0x00000012u },
};

static const Oot3dActorEntry oot3d_spot18_info_spot18_0_info_actors[] = {
    { ACTOR_OBJ_BOMBIWA, { -1393, 520, -1238 }, { 0, -16565, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1398, 520, -916 }, { 0, -20024, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1533, 534, -833 }, { 0, -32767, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1308, 520, -971 }, { 0, -4733, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1320, 520, -1123 }, { 0, -2912, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1131, 520, -1164 }, { 0, 14564, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1363, 520, -1176 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1169, 520, -1045 }, { 0, -32586, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1194, 520, -1104 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1546, 520, -1131 }, { 0, -7645, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1261, 520, -1201 }, { 0, 4368, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1306, 520, -1246 }, { 0, 23848, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1353, 520, -1279 }, { 0, 3641, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1168, 520, -1201 }, { 0, -4914, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1206, 520, -1195 }, { 0, 7645, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1151, 520, -1086 }, { 0, -4551, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1251, 520, -1113 }, { 0, -22209, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1470, 541, -795 }, { 0, -3095, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1466, 520, -974 }, { 0, 20753, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1160, 534, -836 }, { 0, -27488, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1393, 520, -981 }, { 0, -29673, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1233, 520, -943 }, { 0, 5643, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1155, 524, -891 }, { 0, -24211, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1540, 520, -964 }, { 0, -8555, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1486, 520, -1199 }, { 0, 16020, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1268, 520, -1041 }, { 0, 10741, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1528, 524, -891 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1475, 520, -1079 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1426, 520, -1198 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1325, 520, -1328 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1443, 531, -853 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1450, 520, -1136 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1211, 524, -889 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1233, 534, -833 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1361, 532, -846 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1303, 528, -866 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1376, 520, -1119 }, { 0, 22390, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1328, 520, -911 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1393, 520, -1036 }, { 0, -6735, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1438, 520, -1021 }, { 0, 6554, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1473, 521, -906 }, { 0, -3822, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1321, 520, -1048 }, { 0, 7464, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1268, 523, -896 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1144, 520, -979 }, { 0, -5460, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1220, 520, -1028 }, { 0, -32767, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1551, 520, -1194 }, { 0, -4733, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1548, 520, -1061 }, { 0, -8737, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1493, 520, -1038 }, { 0, 6918, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1406, 537, -816 }, { 0, -11287, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1540, 546, -769 }, { 0, -32767, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1126, 545, -774 }, { 0, -11832, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1199, 541, -795 }, { 0, -12014, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1283, 538, -811 }, { 0, 14382, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1338, 542, -788 }, { 0, -3822, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1435, 546, -769 }, { 0, 8737, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1386, 551, -741 }, { 0, -9101, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1498, 553, -730 }, { 0, 3822, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1540, 560, -690 }, { 0, 14928, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1164, 551, -741 }, { 0, -5824, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1230, 550, -744 }, { 0, 3641, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1303, 551, -741 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1446, 556, -711 }, { 0, -3095, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1269, 559, -695 }, { 0, -20024, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1343, 559, -691 }, { 0, 3458, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1401, 563, -669 }, { 0, 26032, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1488, 566, -656 }, { 0, 4733, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1533, 575, -606 }, { 0, -2912, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1448, 572, -622 }, { 0, 9466, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1306, 566, -655 }, { 0, -3458, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1204, 561, -681 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1134, 561, -683 }, { 0, 6007, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1161, 571, -625 }, { 0, -7464, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1231, 569, -639 }, { 0, 7099, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1351, 572, -622 }, { 0, 3277, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1401, 579, -583 }, { 0, -7828, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1491, 580, -576 }, { 0, -5460, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1531, 592, -510 }, { 0, 0, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1433, 586, -543 }, { 0, -4187, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1131, 583, -557 }, { 0, -5278, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1199, 581, -571 }, { 0, -5824, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1265, 577, -594 }, { 0, 4004, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1316, 582, -562 }, { 0, 10741, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1241, 587, -534 }, { 0, -4187, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1176, 593, -501 }, { 0, -5278, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1465, 594, -496 }, { 0, 6735, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1373, 591, -513 }, { 0, 3641, 0 }, 255 },
    { ACTOR_OBJ_BOMBIWA, { -1293, 594, -494 }, { 0, 0, 0 }, 255 },
    { ACTOR_EN_BOX, { -1476, 520, -1290 }, { 0, -32767, 0 }, 32 },
    { ACTOR_EN_BOX, { -1191, 520, -1290 }, { 0, -32767, 0 }, 33 },
    { ACTOR_EN_BOX, { -1191, 520, -1290 }, { 0, -32767, 0 }, 33 },
};

static const Oot3dRoomObjectEntry oot3d_spot18_info_spot18_0_info_objects[] = {
    { OBJECT_SPOT18_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OF1D_MAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot18_info_spot18_1_info_actors[] = {
    { ACTOR_BOSS_GOMA, { -1531, 0, -181 }, { 0, -256, 185 }, 92 },
    { ACTOR_EN_BUTTE, { -1580, 0, 0 }, { 0, 1, 152 }, 91 },
    { ACTOR_EN_ZL1, { -1461, 0, 0 }, { 0, -1, 273 }, 262 },
    { ACTOR_BG_HIDAN_SIMA, { -1210, 0, 0 }, { 0, 32575, 273 }, 261 },
    { ACTOR_BG_HIDAN_SIMA, { -1254, 0, 0 }, { 0, 32575, 273 }, 262 },
    { ACTOR_BG_HIDAN_SIMA, { -1386, 0, 0 }, { 0, 32575, 94 }, 222 },
    { ACTOR_BOSS_GOMA, { -1473, 0, 0 }, { 0, 9216, 94 }, -52 },
    { ACTOR_BOSS_GOMA, { -1473, 0, 0 }, { 0, 9216, 175 }, 300 },
};

static const Oot3dRoomObjectEntry oot3d_spot18_info_spot18_1_info_objects[] = {
    { OBJECT_SPOT18_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OF1D_MAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_BOSSKEY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MORI_HINERI1, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot18_info_spot18_2_info_actors[] = {
    { ACTOR_BG_BREAKWALL, { -363, 400, 1206 }, { 0, 30765, 0 }, 8194 },
    { ACTOR_BG_BREAKWALL, { -404, 400, 1318 }, { 0, 25121, 0 }, 8196 },
    { ACTOR_BG_BREAKWALL, { -508, 400, 1366 }, { 0, 17111, 0 }, 8197 },
    { ACTOR_BG_BREAKWALL, { -609, 400, 1351 }, { 0, 15109, 0 }, 8198 },
    { ACTOR_OBJ_TSUBO, { -694, 430, 1196 }, { 0, 0, 0 }, 32575 },
};

static const Oot3dRoomObjectEntry oot3d_spot18_info_spot18_2_info_objects[] = {
    { OBJECT_HUMAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SPOT18_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GM, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OF1D_MAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot18_info_spot18_3_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 447, 196, 1139 }, { 0, 0, 0 }, 13 },
    { ACTOR_EN_GO2, { -454, 116, 699 }, { 0, 18751, 0 }, -22 },
    { ACTOR_EN_GO2, { 237, 197, 420 }, { 0, -25303, 0 }, -21 },
    { ACTOR_EN_GO2, { 84, -3, -314 }, { 0, -7281, 0 }, -24 },
    { ACTOR_EN_KANBAN, { 333, 398, -684 }, { 0, -7281, 0 }, 805 },
    { ACTOR_EN_WONDER_TALK2, { 0, 535, 175 }, { 0, 0, 0 }, -30465 },
    { ACTOR_EN_GO2, { 43, 522, 224 }, { 0, -6007, 0 }, -23 },
    { ACTOR_EN_BOMBF, { -881, 280, -188 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BOMBF, { -378, 400, 695 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BOMBF, { -231, 399, 730 }, { 0, 0, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_spot18_info_spot18_3_info_objects[] = {
    { OBJECT_SPOT18_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OF1D_MAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BWALL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SYOKUDAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_D_HSBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SD, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_spot18_info_room_refs[] = {
    { "spot18_0_info.zsi", 0 },
    { "spot18_1_info.zsi", 1 },
    { "spot18_2_info.zsi", 2 },
    { "spot18_3_info.zsi", 3 },
};

static const Oot3dSceneSetupIndex oot3d_spot18_info_setups[] = {
    { 0u, oot3d_spot18_info_setup_0_commands, 13u, oot3d_spot18_info_setup_0_special_files, 1u, oot3d_spot18_info_setup_0_paths, 1u, NULL, 0u, oot3d_spot18_info_setup_0_spawns, 4u, oot3d_spot18_info_setup_0_entrances, 4u, oot3d_spot18_info_setup_0_transition_actors, 3u, oot3d_spot18_info_setup_0_light_settings, 4u, oot3d_spot18_info_setup_0_exits, 4u, oot3d_spot18_info_setup_0_skybox_settings, 1u, oot3d_spot18_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_spot18_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_spot18_info_setup_1_commands, 13u, oot3d_spot18_info_setup_1_special_files, 1u, oot3d_spot18_info_setup_1_paths, 1u, NULL, 0u, oot3d_spot18_info_setup_1_spawns, 4u, oot3d_spot18_info_setup_1_entrances, 4u, oot3d_spot18_info_setup_1_transition_actors, 3u, oot3d_spot18_info_setup_1_light_settings, 4u, oot3d_spot18_info_setup_1_exits, 4u, oot3d_spot18_info_setup_1_skybox_settings, 1u, oot3d_spot18_info_setup_1_sound_settings, 1u, NULL, 0u, oot3d_spot18_info_setup_1_misc_settings, 1u },
    { 2u, oot3d_spot18_info_setup_2_commands, 14u, oot3d_spot18_info_setup_2_special_files, 1u, NULL, 0u, oot3d_spot18_info_setup_2_standard_actors, 1u, oot3d_spot18_info_setup_2_spawns, 1u, oot3d_spot18_info_setup_2_entrances, 1u, oot3d_spot18_info_setup_2_transition_actors, 3u, oot3d_spot18_info_setup_2_light_settings, 4u, oot3d_spot18_info_setup_2_exits, 4u, oot3d_spot18_info_setup_2_skybox_settings, 1u, oot3d_spot18_info_setup_2_sound_settings, 1u, oot3d_spot18_info_setup_2_cutscenes, 1u, oot3d_spot18_info_setup_2_misc_settings, 1u },
    { 3u, oot3d_spot18_info_setup_3_commands, 13u, oot3d_spot18_info_setup_3_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot18_info_setup_3_spawns, 1u, oot3d_spot18_info_setup_3_entrances, 1u, oot3d_spot18_info_setup_3_transition_actors, 3u, oot3d_spot18_info_setup_3_light_settings, 1u, oot3d_spot18_info_setup_3_exits, 4u, oot3d_spot18_info_setup_3_skybox_settings, 1u, oot3d_spot18_info_setup_3_sound_settings, 1u, oot3d_spot18_info_setup_3_cutscenes, 1u, oot3d_spot18_info_setup_3_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_spot18_info_rooms[] = {
    { "spot18_0_info.zsi", 0, oot3d_spot18_info_spot18_0_info_objects, 8u, oot3d_spot18_info_spot18_0_info_actors, 90u },
    { "spot18_1_info.zsi", 1, oot3d_spot18_info_spot18_1_info_objects, 10u, oot3d_spot18_info_spot18_1_info_actors, 8u },
    { "spot18_2_info.zsi", 2, oot3d_spot18_info_spot18_2_info_objects, 9u, oot3d_spot18_info_spot18_2_info_actors, 5u },
    { "spot18_3_info.zsi", 3, oot3d_spot18_info_spot18_3_info_objects, 13u, oot3d_spot18_info_spot18_3_info_actors, 10u },
};

const Oot3dSceneIndex oot3d_scene_index_spot18_info = {
    "spot18_info.zsi",
    oot3d_spot18_info_room_refs, 4u,
    oot3d_spot18_info_rooms, 4u,
    oot3d_spot18_info_setups, 4u,
};
