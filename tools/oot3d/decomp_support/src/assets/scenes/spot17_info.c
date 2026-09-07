/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: spot17_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_spot17_info_setup_0_commands[] = {
    { 0x00130415u, 0x01000586u },
    { 0x00000204u, 0x000001B4u },
    { 0x0000010Eu, 0x0000023Cu },
    { 0x00000019u, 0x00000011u },
    { 0x00000003u, 0x00006BD8u },
    { 0x00000506u, 0x00006C04u },
    { 0x00000107u, 0x00000002u },
    { 0x00000500u, 0x00006C10u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x00006C60u },
    { 0x00000C0Fu, 0x00006C68u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot17_info_setup_0_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot17_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { 0, 256, 257 }, { 2, 259, 260 }, 57 },
    { ACTOR_PLAYER, { -1114, 1360, 2065 }, { 0, 20934, 0 }, 4095 },
    { ACTOR_PLAYER, { -1705, 722, 13 }, { 0, 20207, 0 }, 4095 },
    { ACTOR_PLAYER, { 12, -350, -1419 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { -1287, 829, 941 }, { 0, 18203, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot17_info_setup_0_entrances[] = {
    { 0u, 1 },
    { 1u, 1 },
    { 2u, 0 },
    { 3u, 1 },
    { 4u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot17_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot17_info_setup_0_light_settings[] = {
    { { 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0xFF, 0x0D, 0xBD, 0x01, 0xC1, 0x01, 0x65, 0x01, 0xBE, 0x04, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xDC, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xA0, 0x68, 0x28, 0xB8, 0xB8, 0xB8, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xDC, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xA0, 0x68, 0x28, 0xB8, 0xB8, 0xB8, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xDC, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xAF, 0x4F, 0xB8, 0xB8, 0xB8, 0xA0, 0x68, 0x28, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xDC, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xAF, 0x4F, 0xB8, 0xB8, 0xB8, 0xA0, 0x68, 0x28, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x0E, 0xFF, 0x00, 0x00 } },
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x0E, 0xFF, 0x00, 0x00 } },
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x0E, 0xFF, 0x00, 0x00 } },
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x0E, 0xFF, 0x00, 0x00 } },
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xE0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x00, 0x00, 0x00, 0xA0, 0x68, 0x28, 0x00, 0x00, 0x00, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x00, 0x00, 0x00, 0xA0, 0x68, 0x28, 0x00, 0x00, 0x00, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xE0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x00, 0x00, 0x00, 0xFF, 0xAF, 0x4F, 0x00, 0x00, 0x00, 0xA0, 0x68, 0x28, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xF0, 0x06, 0xFF, 0x77 } },
};

static const Oot3dExitEntry oot3d_spot17_info_setup_0_exits[] = {
    { 445, 445u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 449, 449u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 357, 357u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1214, 1214u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot17_info_setup_0_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot17_info_setup_0_sound_settings[] = {
    { 4u, 1414u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot17_info_setup_0_misc_settings[] = {
    { 0u, 0x00000011u },
};

static const Oot3dSceneCommand oot3d_spot17_info_setup_1_commands[] = {
    { 0x00130415u, 0x01000586u },
    { 0x00000204u, 0x00006DB8u },
    { 0x0000010Eu, 0x00006E40u },
    { 0x00000019u, 0x00000011u },
    { 0x00000003u, 0x00006BD8u },
    { 0x00000606u, 0x00006E50u },
    { 0x00000107u, 0x00000002u },
    { 0x0000010Du, 0x00006F00u },
    { 0x00000600u, 0x00006F08u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x00006F68u },
    { 0x00000C0Fu, 0x00006F70u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot17_info_setup_1_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dVec3s oot3d_spot17_info_setup_1_paths_0_points[] = {
    { 2, 259, 260 },
    { 261, 26, 0 },
    { 28260, 0, -127 },
    { 421, -168, -127 },
    { 963, -168, -219 },
    { 995, 206, -524 },
    { 1067, 360, -649 },
    { 1067, 374, -853 },
    { 1099, 254, -830 },
    { 1125, 41, -714 },
    { 1155, -5, -613 },
    { 1131, 129, -640 },
    { 1117, 348, -807 },
    { 1117, 755, -758 },
    { 1272, 933, -594 },
    { 1183, 967, -230 },
    { 1187, 994, -63 },
    { 793, 994, 57 },
    { 796, 994, 317 },
    { 1149, 994, 1012 },
    { 1483, 1103, 1285 },
    { 1558, 912, 1482 },
    { 1087, 40, 1250 },
    { 951, -649, 644 },
    { 815, -913, 232 },
    { 726, -498, -18 },
};

static const Oot3dPathRecord oot3d_spot17_info_setup_1_paths[] = {
    { 26u, 0u, 0u, 28260u, OOT3D_PATH_POINTS_DECODED_VEC3S, oot3d_spot17_info_setup_1_paths_0_points },
};

static const Oot3dActorEntry oot3d_spot17_info_setup_1_spawns[] = {
    { ACTOR_PLAYER, { -1121, 1360, 2076 }, { 0, 20934, 0 }, 4095 },
    { ACTOR_PLAYER, { -1749, 722, 26 }, { 0, 20207, 0 }, 4095 },
    { ACTOR_PLAYER, { 12, -350, -1419 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { -1287, 829, 941 }, { 0, 18203, 0 }, 4095 },
    { ACTOR_PLAYER, { 0, 441, 0 }, { 0, -32767, 0 }, 3583 },
};

static const Oot3dEntranceEntry oot3d_spot17_info_setup_1_entrances[] = {
    { 0u, 1 },
    { 1u, 1 },
    { 2u, 0 },
    { 3u, 1 },
    { 4u, 1 },
    { 5u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot17_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot17_info_setup_1_light_settings[] = {
    { { 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x02, 0xBD, 0x01, 0xC1, 0x01, 0x65, 0x01, 0xBE, 0x04, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xDC, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xA0, 0x68, 0x28, 0xB8, 0xB8, 0xB8, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xDC, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xA0, 0x68, 0x28, 0xB8, 0xB8, 0xB8, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xDC, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xAF, 0x4F, 0xB8, 0xB8, 0xB8, 0xA0, 0x68, 0x28, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xDC, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xAF, 0x4F, 0xB8, 0xB8, 0xB8, 0xA0, 0x68, 0x28, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x0E, 0xFF, 0x00, 0x00 } },
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x0E, 0xFF, 0x00, 0x00 } },
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x0E, 0xFF, 0x00, 0x00 } },
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x0E, 0xFF, 0x00, 0x00 } },
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xE0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x00, 0x00, 0x00, 0xA0, 0x68, 0x28, 0x00, 0x00, 0x00, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x00, 0x00, 0x00, 0xA0, 0x68, 0x28, 0x00, 0x00, 0x00, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xE0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x00, 0x00, 0x00, 0xFF, 0xAF, 0x4F, 0x00, 0x00, 0x00, 0xA0, 0x68, 0x28, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xF0, 0x06, 0xFF, 0x77 } },
};

static const Oot3dExitEntry oot3d_spot17_info_setup_1_exits[] = {
    { 445, 445u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 449, 449u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 357, 357u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1214, 1214u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot17_info_setup_1_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot17_info_setup_1_sound_settings[] = {
    { 4u, 1414u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot17_info_setup_1_misc_settings[] = {
    { 0u, 0x00000011u },
};

static const Oot3dSceneCommand oot3d_spot17_info_setup_2_commands[] = {
    { 0x00130415u, 0x01000586u },
    { 0x00000204u, 0x000070C0u },
    { 0x0000010Eu, 0x00007148u },
    { 0x00000019u, 0x00000011u },
    { 0x00000003u, 0x00006BD8u },
    { 0x00000106u, 0x00007158u },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x0000715Cu },
    { 0x00000011u, 0x00010101u },
    { 0x00000013u, 0x0000716Cu },
    { 0x0000040Fu, 0x00007178u },
    { 0x00000017u, 0x000071E8u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot17_info_setup_2_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot17_info_setup_2_spawns[] = {
    { ACTOR_EN_HOLL, { 13, 515, -1375 }, { 0, 319, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot17_info_setup_2_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot17_info_setup_2_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot17_info_setup_2_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x0F, 0xBD, 0x01, 0xC1, 0x01, 0x65, 0x01, 0xBE, 0x04, 0xF2, 0x04, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xE0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xA0, 0x68, 0x28, 0xB8, 0xB8, 0xB8, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xA0, 0x68, 0x28, 0xB8, 0xB8, 0xB8, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xE0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xAF, 0x4F, 0xB8, 0xB8, 0xB8, 0xA0, 0x68, 0x28, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xF0, 0x06, 0xFF, 0x77 } },
};

static const Oot3dExitEntry oot3d_spot17_info_setup_2_exits[] = {
    { 445, 445u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 449, 449u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 357, 357u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1214, 1214u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1266, 1266u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot17_info_setup_2_skybox_settings[] = {
    { 1u, 1u, 1u },
};

static const Oot3dSoundSettings oot3d_spot17_info_setup_2_sound_settings[] = {
    { 4u, 1414u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot17_info_setup_2_cutscenes[] = {
    { 0x000071E8u, 1u },
};

static const Oot3dMiscSettings oot3d_spot17_info_setup_2_misc_settings[] = {
    { 0u, 0x00000011u },
};

static const Oot3dSceneCommand oot3d_spot17_info_setup_3_commands[] = {
    { 0x00130415u, 0x01000586u },
    { 0x00000204u, 0x0000C578u },
    { 0x0000010Eu, 0x0000C600u },
    { 0x00000019u, 0x00000011u },
    { 0x00000003u, 0x00006BD8u },
    { 0x00000106u, 0x0000C610u },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x0000C614u },
    { 0x00000011u, 0x00010101u },
    { 0x00000013u, 0x0000C624u },
    { 0x0000040Fu, 0x0000C630u },
    { 0x00000017u, 0x0000C6A0u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot17_info_setup_3_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot17_info_setup_3_spawns[] = {
    { ACTOR_EN_HOLL, { 13, 515, -1375 }, { 0, 319, 256 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot17_info_setup_3_entrances[] = {
    { 0u, 1 },
};

static const Oot3dTransitionActorEntry oot3d_spot17_info_setup_3_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot17_info_setup_3_light_settings[] = {
    { { 0x00, 0x00, 0xFF, 0x0F, 0xBD, 0x01, 0xC1, 0x01, 0x65, 0x01, 0xBE, 0x04, 0xF2, 0x04, 0x00, 0x00, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xE0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xA0, 0x68, 0x28, 0xB8, 0xB8, 0xB8, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xA0, 0x68, 0x28, 0xB8, 0xB8, 0xB8, 0xFF, 0xAF, 0x4F, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xE0, 0x06, 0xFF, 0x77 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xAF, 0x4F, 0xB8, 0xB8, 0xB8, 0xA0, 0x68, 0x28, 0x82, 0xA0, 0x44, 0x00, 0x00, 0x48, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xF0, 0x06, 0xFF, 0x77 } },
};

static const Oot3dExitEntry oot3d_spot17_info_setup_3_exits[] = {
    { 445, 445u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 449, 449u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 357, 357u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1214, 1214u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1266, 1266u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot17_info_setup_3_skybox_settings[] = {
    { 1u, 1u, 1u },
};

static const Oot3dSoundSettings oot3d_spot17_info_setup_3_sound_settings[] = {
    { 4u, 1414u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot17_info_setup_3_cutscenes[] = {
    { 0x0000C6A0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot17_info_setup_3_misc_settings[] = {
    { 0u, 0x00000011u },
};

static const Oot3dActorEntry oot3d_spot17_info_spot17_1_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 14, 202, 22 }, { 0, 0, 0 }, 6 },
    { ACTOR_EN_GS, { 1298, 1476, 1666 }, { 0, 32129, 0 }, 14341 },
    { ACTOR_EN_ITEM00, { 4, 749, 1208 }, { 0, 0, 0 }, 518 },
    { ACTOR_EN_ITEM00, { -734, 1120, 132 }, { 0, 0, -181 }, 2054 },
    { ACTOR_OBJ_HAMISHI, { -1331, 829, 921 }, { 0, 0, 0 }, 277 },
    { ACTOR_OBJ_HAMISHI, { -1303, 829, 975 }, { 0, 0, 0 }, 278 },
    { ACTOR_OBJ_HAMISHI, { -1698, 721, -472 }, { 0, 13470, 0 }, 279 },
    { ACTOR_OBJ_HAMISHI, { -1060, 829, 944 }, { 0, -9101, 0 }, 24 },
    { ACTOR_EN_BB, { 162, 247, -780 }, { 0, 0, 0 }, -2 },
    { ACTOR_EN_BB, { 437, 231, -576 }, { 0, 8555, 0 }, -2 },
    { ACTOR_EN_BB, { 925, 247, -579 }, { 0, -7645, 0 }, -2 },
    { ACTOR_EN_BB, { 990, 236, -234 }, { 0, 23848, 0 }, -2 },
    { ACTOR_EN_BB, { 1238, 229, 283 }, { 0, -16384, 0 }, -2 },
    { ACTOR_OBJ_TSUBO, { -1606, 721, -166 }, { 0, 0, 0 }, 16643 },
    { ACTOR_OBJ_TSUBO, { -1641, 721, -127 }, { 0, 0, 0 }, 17160 },
    { ACTOR_OBJ_TSUBO, { -1590, 721, 132 }, { 0, 0, 0 }, 17665 },
    { ACTOR_OBJ_TSUBO, { -1546, 723, 141 }, { 0, 0, 0 }, 18191 },
    { ACTOR_DOOR_WARP1, { 0, 441, 0 }, { 0, 0, 0 }, 6 },
    { ACTOR_OBJ_MURE3, { 1121, 367, 435 }, { 0, 0, 0 }, 16639 },
    { ACTOR_DOOR_ANA, { 40, 1234, 1770 }, { 0, 9101, 0 }, 122 },
    { ACTOR_DOOR_ANA, { -1698, 722, -472 }, { 0, 12743, 4 }, 249 },
    { ACTOR_EN_KAKASI2, { 1169, 367, 448 }, { 0, -32767, 3 }, 2623 },
    { ACTOR_EN_KAKASI2, { 1158, 379, -213 }, { 0, 0, 3 }, 2623 },
    { ACTOR_EN_XC, { -340, 464, -331 }, { 0, -4914, 0 }, 7 },
    { ACTOR_EN_WEATHER_TAG, { 0, 421, 0 }, { 0, 0, 0 }, 10244 },
    { ACTOR_BG_MJIN, { 0, 421, 0 }, { 0, 0, 0 }, 3 },
    { ACTOR_BG_SPOT17_BAKUDANKABE, { 1310, 1476, 1566 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_SPOT17_FUNEN, { -733, 1104, 132 }, { 0, 910, 0 }, -1 },
    { ACTOR_BG_SPOT17_FUNEN, { -733, 1104, 132 }, { 0, 910, 0 }, -1 },
    { ACTOR_BG_SPOT17_FUNEN, { 640, 1148, 20 }, { 0, 727, 0 }, -1 },
    { ACTOR_BG_SPOT17_FUNEN, { 640, 1148, 20 }, { 0, 727, 0 }, -1 },
    { ACTOR_EN_A_OBJ, { -1610, 721, 95 }, { 0, 29127, 0 }, 2826 },
    { ACTOR_EN_A_OBJ, { -1610, 721, 95 }, { 0, 29127, 0 }, 2826 },
    { ACTOR_EN_ISHI, { -50, 476, -714 }, { 0, 0, 0 }, 1792 },
    { ACTOR_EN_ISHI, { -26, 476, -806 }, { 0, 4004, 0 }, 1792 },
    { ACTOR_EN_ISHI, { 61, 476, -763 }, { 0, -4914, 0 }, 1792 },
    { ACTOR_EN_ISHI, { 71, 471, -610 }, { 0, -22026, 0 }, 1792 },
    { ACTOR_EN_ISHI, { 79, 476, -700 }, { 0, -7099, 0 }, 1792 },
    { ACTOR_OBJ_MURE2, { 40, 1234, 1770 }, { 0, 0, 0 }, 1794 },
    { ACTOR_EN_WONDER_ITEM, { -1353, 829, 929 }, { 0, 0, 0 }, 16348 },
    { ACTOR_EN_WONDER_ITEM, { -1343, 829, 951 }, { 0, 0, 0 }, 16348 },
    { ACTOR_EN_WONDER_ITEM, { -1333, 829, 974 }, { 0, 0, 0 }, 16348 },
    { ACTOR_EN_WONDER_ITEM, { -1323, 829, 996 }, { 0, 0, 0 }, 16348 },
    { ACTOR_OBJ_BEAN, { -127, 422, -168 }, { 0, 0, 2 }, 3 },
    { ACTOR_EN_WONDER_TALK2, { -1328, 880, 955 }, { 0, 20753, 42 }, -28964 },
    { ACTOR_OBJ_BOMBIWA, { 236, 1210, 1199 }, { 0, 0, 0 }, 30 },
    { ACTOR_OBJ_BOMBIWA, { -504, 1144, 1070 }, { 0, 0, 0 }, 31 },
    { ACTOR_OBJ_BOMBIWA, { 40, 1234, 1770 }, { 0, 0, 0 }, -32739 },
    { ACTOR_EN_WONDER_ITEM, { 6, 311, -640 }, { 0, 0, 1 }, 4799 },
};

static const Oot3dRoomObjectEntry oot3d_spot17_info_spot17_1_info_objects[] = {
    { OBJECT_SPOT17_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MJIN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MJIN_FLAME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MAMENOKI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KANBAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_XC, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WARP1, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_spot17_info_room_refs[] = {
    { "spot17_0_info.zsi", 0 },
    { "spot17_1_info.zsi", 1 },
};

static const Oot3dSceneSetupIndex oot3d_spot17_info_setups[] = {
    { 0u, oot3d_spot17_info_setup_0_commands, 12u, oot3d_spot17_info_setup_0_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot17_info_setup_0_spawns, 5u, oot3d_spot17_info_setup_0_entrances, 5u, oot3d_spot17_info_setup_0_transition_actors, 1u, oot3d_spot17_info_setup_0_light_settings, 12u, oot3d_spot17_info_setup_0_exits, 4u, oot3d_spot17_info_setup_0_skybox_settings, 1u, oot3d_spot17_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_spot17_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_spot17_info_setup_1_commands, 13u, oot3d_spot17_info_setup_1_special_files, 1u, oot3d_spot17_info_setup_1_paths, 1u, NULL, 0u, oot3d_spot17_info_setup_1_spawns, 5u, oot3d_spot17_info_setup_1_entrances, 6u, oot3d_spot17_info_setup_1_transition_actors, 1u, oot3d_spot17_info_setup_1_light_settings, 12u, oot3d_spot17_info_setup_1_exits, 4u, oot3d_spot17_info_setup_1_skybox_settings, 1u, oot3d_spot17_info_setup_1_sound_settings, 1u, NULL, 0u, oot3d_spot17_info_setup_1_misc_settings, 1u },
    { 2u, oot3d_spot17_info_setup_2_commands, 13u, oot3d_spot17_info_setup_2_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot17_info_setup_2_spawns, 1u, oot3d_spot17_info_setup_2_entrances, 1u, oot3d_spot17_info_setup_2_transition_actors, 1u, oot3d_spot17_info_setup_2_light_settings, 4u, oot3d_spot17_info_setup_2_exits, 6u, oot3d_spot17_info_setup_2_skybox_settings, 1u, oot3d_spot17_info_setup_2_sound_settings, 1u, oot3d_spot17_info_setup_2_cutscenes, 1u, oot3d_spot17_info_setup_2_misc_settings, 1u },
    { 3u, oot3d_spot17_info_setup_3_commands, 13u, oot3d_spot17_info_setup_3_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot17_info_setup_3_spawns, 1u, oot3d_spot17_info_setup_3_entrances, 1u, oot3d_spot17_info_setup_3_transition_actors, 1u, oot3d_spot17_info_setup_3_light_settings, 4u, oot3d_spot17_info_setup_3_exits, 6u, oot3d_spot17_info_setup_3_skybox_settings, 1u, oot3d_spot17_info_setup_3_sound_settings, 1u, oot3d_spot17_info_setup_3_cutscenes, 1u, oot3d_spot17_info_setup_3_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_spot17_info_rooms[] = {
    { "spot17_0_info.zsi", 0, NULL, 0u, NULL, 0u },
    { "spot17_1_info.zsi", 1, oot3d_spot17_info_spot17_1_info_objects, 13u, oot3d_spot17_info_spot17_1_info_actors, 49u },
};

const Oot3dSceneIndex oot3d_scene_index_spot17_info = {
    "spot17_info.zsi",
    oot3d_spot17_info_room_refs, 2u,
    oot3d_spot17_info_rooms, 2u,
    oot3d_spot17_info_setups, 4u,
};
