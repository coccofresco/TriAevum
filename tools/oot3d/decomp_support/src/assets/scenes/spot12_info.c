/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: spot12_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_spot12_info_setup_0_commands[] = {
    { 0x00000115u, 0x010005CCu },
    { 0x00000204u, 0x00000280u },
    { 0x0000010Eu, 0x00000308u },
    { 0x00000019u, 0x0000000Cu },
    { 0x00000003u, 0x00008798u },
    { 0x00001206u, 0x000087C4u },
    { 0x00000107u, 0x00000002u },
    { 0x00001200u, 0x000087E8u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x00008908u },
    { 0x00000C0Fu, 0x00008928u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot12_info_setup_0_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot12_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { -842, 3, -84 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { 200, 333, -2554 }, { 0, -16384, 0 }, 3328 },
    { ACTOR_PLAYER, { 410, 333, -1794 }, { 0, 0, 0 }, 3329 },
    { ACTOR_PLAYER, { 626, 333, -1718 }, { 0, -16384, 0 }, 3330 },
    { ACTOR_PLAYER, { 629, 533, -1401 }, { 0, -16384, 0 }, 3331 },
    { ACTOR_PLAYER, { 222, 333, -1347 }, { 0, -16384, 0 }, 3332 },
    { ACTOR_PLAYER, { 679, 533, -2056 }, { 0, 181, 0 }, 3333 },
    { ACTOR_PLAYER, { 325, 572, -1084 }, { 0, -16384, 0 }, 3334 },
    { ACTOR_PLAYER, { 947, 733, -1207 }, { 0, -32767, 0 }, 3335 },
    { ACTOR_PLAYER, { 931, 733, -1404 }, { 0, -16384, 0 }, 3336 },
    { ACTOR_PLAYER, { 1230, 834, -1769 }, { 0, -16384, 0 }, 3337 },
    { ACTOR_PLAYER, { 763, 640, -2662 }, { 0, -16384, 0 }, 3338 },
    { ACTOR_PLAYER, { 314, 1113, -2982 }, { 0, -14199, 0 }, 3583 },
    { ACTOR_PLAYER, { 1249, 653, -2061 }, { 0, 0, 0 }, 3339 },
    { ACTOR_PLAYER, { 40, 333, -1022 }, { 0, -16384, 0 }, 3340 },
    { ACTOR_PLAYER, { -1786, 12, -3382 }, { 0, 14382, 0 }, 3839 },
    { ACTOR_PLAYER, { 3662, 1413, -507 }, { 0, 16384, 0 }, 3583 },
};

static const Oot3dEntranceEntry oot3d_spot12_info_setup_0_entrances[] = {
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
    { 10u, 0 },
    { 11u, 0 },
    { 12u, 0 },
    { 13u, 0 },
    { 14u, 0 },
    { 15u, 0 },
    { 16u, 1 },
    { 17u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot12_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot12_info_setup_0_light_settings[] = {
    { { 0xA2, 0x04, 0xA6, 0x04, 0xAA, 0x04, 0xAE, 0x04, 0xB2, 0x04, 0x70, 0x05, 0x08, 0x00, 0x30, 0x01, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0xBB, 0x46, 0x08, 0x07, 0x77, 0x68 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x44, 0x23, 0x33, 0x9B, 0x68, 0x23, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x47, 0xE0, 0x06, 0x96, 0x82 } },
    { { 0x68, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x6D, 0x59, 0x3D, 0xCC, 0xA5, 0x63, 0x00, 0x40, 0x9C, 0x46, 0x00, 0xC0, 0xDA, 0x46, 0x0C, 0x07, 0xA0, 0x4F } },
    { { 0x19, 0x48, 0x48, 0x48, 0xFF, 0xB5, 0x33, 0xB8, 0xB8, 0xB8, 0x4F, 0x14, 0x4F, 0x96, 0x3D, 0x3D, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0x20, 0x07, 0x28, 0x54 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x2D, 0x2D, 0x38, 0xB8, 0xB8, 0xB8, 0x4F, 0xB5, 0xFF, 0x00, 0x00, 0x1E, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x33, 0x49 } },
    { { 0x3D, 0x00, 0x00, 0x00, 0x6D, 0xB5, 0x4F, 0x00, 0x00, 0x00, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0xFC, 0x49, 0x82 } },
    { { 0x63, 0x00, 0x00, 0x00, 0xA0, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x3D, 0x33 } },
    { { 0x28, 0x00, 0x00, 0x00, 0x6D, 0x6D, 0x82, 0x00, 0x00, 0x00, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0xFC, 0x0A, 0x28 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0x14, 0x28, 0x63, 0x00, 0x00, 0x00, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x54, 0x54 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD8, 0x06, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xD1, 0xD1, 0xD1, 0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x4C, 0x4C, 0x4C, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x59, 0x49 } },
    { { 0x49, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x3D, 0x33, 0x3A, 0x28, 0x2D, 0x28, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0x3B, 0x46, 0xD8, 0x06, 0x3D, 0x49 } },
};

static const Oot3dExitEntry oot3d_spot12_info_setup_0_exits[] = {
    { 557, 557u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1158, 1158u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1162, 1162u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1166, 1166u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1170, 1170u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1174, 1174u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1178, 1178u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1182, 1182u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot12_info_setup_0_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot12_info_setup_0_sound_settings[] = {
    { 1u, 1484u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot12_info_setup_0_misc_settings[] = {
    { 0u, 0x0000000Cu },
};

static const Oot3dSceneCommand oot3d_spot12_info_setup_1_commands[] = {
    { 0x00000115u, 0x010005CCu },
    { 0x00000204u, 0x00008A78u },
    { 0x0000010Eu, 0x00008B00u },
    { 0x00000019u, 0x0000000Cu },
    { 0x00000003u, 0x00008798u },
    { 0x00001306u, 0x00008B10u },
    { 0x00000107u, 0x00000002u },
    { 0x00001300u, 0x00008B38u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x00008C68u },
    { 0x00000C0Fu, 0x00008C88u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot12_info_setup_1_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot12_info_setup_1_spawns[] = {
    { ACTOR_PLAYER, { -842, 3, -84 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { 200, 333, -2554 }, { 0, -16384, 0 }, 3328 },
    { ACTOR_PLAYER, { 410, 333, -1794 }, { 0, 0, 0 }, 3329 },
    { ACTOR_PLAYER, { 626, 333, -1718 }, { 0, -16384, 0 }, 3330 },
    { ACTOR_PLAYER, { 629, 533, -1401 }, { 0, -16384, 0 }, 3331 },
    { ACTOR_PLAYER, { 222, 333, -1347 }, { 0, -16384, 0 }, 3332 },
    { ACTOR_PLAYER, { 679, 533, -2056 }, { 0, 181, 0 }, 3333 },
    { ACTOR_PLAYER, { 325, 572, -1084 }, { 0, -16384, 0 }, 3334 },
    { ACTOR_PLAYER, { 947, 733, -1207 }, { 0, -32767, 0 }, 3335 },
    { ACTOR_PLAYER, { 931, 733, -1404 }, { 0, -16384, 0 }, 3336 },
    { ACTOR_PLAYER, { 1230, 834, -1769 }, { 0, -16384, 0 }, 3337 },
    { ACTOR_PLAYER, { 763, 640, -2662 }, { 0, -16384, 0 }, 3338 },
    { ACTOR_PLAYER, { 314, 1113, -2982 }, { 0, -14199, 0 }, 3583 },
    { ACTOR_PLAYER, { 1249, 653, -2061 }, { 0, 0, 0 }, 3339 },
    { ACTOR_PLAYER, { 40, 333, -1022 }, { 0, -16384, 0 }, 3340 },
    { ACTOR_PLAYER, { -1786, 12, -3382 }, { 0, 14382, 0 }, 3839 },
    { ACTOR_PLAYER, { 3662, 1413, -507 }, { 0, 16384, 0 }, 3583 },
    { ACTOR_PLAYER, { 188, 733, -2919 }, { 0, 0, 0 }, 3583 },
};

static const Oot3dEntranceEntry oot3d_spot12_info_setup_1_entrances[] = {
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
    { 10u, 0 },
    { 11u, 0 },
    { 12u, 0 },
    { 13u, 0 },
    { 14u, 0 },
    { 15u, 0 },
    { 16u, 1 },
    { 17u, 0 },
    { 18u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot12_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot12_info_setup_1_light_settings[] = {
    { { 0xA2, 0x04, 0xA6, 0x04, 0xAA, 0x04, 0xAE, 0x04, 0xB2, 0x04, 0x70, 0x05, 0x08, 0x00, 0x30, 0x01, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x9C, 0x46, 0x08, 0x07, 0x77, 0x68 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x44, 0x23, 0x33, 0x9B, 0x68, 0x23, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x47, 0xE0, 0x06, 0x96, 0x82 } },
    { { 0x68, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x6D, 0x59, 0x3D, 0xCC, 0xA5, 0x63, 0x00, 0x40, 0x9C, 0x46, 0x00, 0xC0, 0xDA, 0x46, 0x0C, 0x07, 0xA0, 0x4F } },
    { { 0x19, 0x48, 0x48, 0x48, 0xFF, 0xB5, 0x33, 0xB8, 0xB8, 0xB8, 0x4F, 0x14, 0x4F, 0x96, 0x3D, 0x3D, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0x20, 0x07, 0x28, 0x54 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x2D, 0x2D, 0x38, 0xB8, 0xB8, 0xB8, 0x4F, 0xB5, 0xFF, 0x00, 0x00, 0x1E, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x33, 0x49 } },
    { { 0x3D, 0x00, 0x00, 0x00, 0x6D, 0xB5, 0x4F, 0x00, 0x00, 0x00, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0xFC, 0x49, 0x82 } },
    { { 0x63, 0x00, 0x00, 0x00, 0xA0, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x3D, 0x33 } },
    { { 0x28, 0x00, 0x00, 0x00, 0x6D, 0x6D, 0x82, 0x00, 0x00, 0x00, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0xFC, 0x0A, 0x28 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0x14, 0x28, 0x63, 0x00, 0x00, 0x00, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x54, 0x54 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD8, 0x06, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xD1, 0xD1, 0xD1, 0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x4C, 0x4C, 0x4C, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x59, 0x49 } },
    { { 0x49, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x3D, 0x33, 0x3A, 0x28, 0x2D, 0x28, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0x3B, 0x46, 0xD8, 0x06, 0x3D, 0x49 } },
};

static const Oot3dExitEntry oot3d_spot12_info_setup_1_exits[] = {
    { 557, 557u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1158, 1158u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1162, 1162u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1166, 1166u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1170, 1170u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1174, 1174u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1178, 1178u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1182, 1182u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot12_info_setup_1_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot12_info_setup_1_sound_settings[] = {
    { 1u, 1484u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot12_info_setup_1_misc_settings[] = {
    { 0u, 0x0000000Cu },
};

static const Oot3dSceneCommand oot3d_spot12_info_setup_2_commands[] = {
    { 0x00000115u, 0x010005CCu },
    { 0x00000204u, 0x00008DD8u },
    { 0x0000010Eu, 0x00008E60u },
    { 0x00000019u, 0x0000000Cu },
    { 0x00000003u, 0x00008798u },
    { 0x00001306u, 0x00008E70u },
    { 0x00000107u, 0x00000002u },
    { 0x00001300u, 0x00008E98u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x00008FC8u },
    { 0x00000C0Fu, 0x00008FE8u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot12_info_setup_2_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot12_info_setup_2_spawns[] = {
    { ACTOR_PLAYER, { -842, 3, -84 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { 200, 333, -2554 }, { 0, -16384, 0 }, 3328 },
    { ACTOR_PLAYER, { 410, 333, -1794 }, { 0, 0, 0 }, 3329 },
    { ACTOR_PLAYER, { 626, 333, -1718 }, { 0, -16384, 0 }, 3330 },
    { ACTOR_PLAYER, { 629, 533, -1401 }, { 0, -16384, 0 }, 3331 },
    { ACTOR_PLAYER, { 222, 333, -1347 }, { 0, -16384, 0 }, 3332 },
    { ACTOR_PLAYER, { 679, 533, -2056 }, { 0, 181, 0 }, 3333 },
    { ACTOR_PLAYER, { 325, 572, -1084 }, { 0, -16384, 0 }, 3334 },
    { ACTOR_PLAYER, { 947, 733, -1207 }, { 0, -32767, 0 }, 3335 },
    { ACTOR_PLAYER, { 931, 733, -1404 }, { 0, -16384, 0 }, 3336 },
    { ACTOR_PLAYER, { 1230, 834, -1769 }, { 0, -16384, 0 }, 3337 },
    { ACTOR_PLAYER, { 763, 640, -2662 }, { 0, -16384, 0 }, 3338 },
    { ACTOR_PLAYER, { 314, 1113, -2982 }, { 0, -14199, 0 }, 3583 },
    { ACTOR_PLAYER, { 1249, 653, -2061 }, { 0, 0, 0 }, 3339 },
    { ACTOR_PLAYER, { 40, 333, -1022 }, { 0, -16384, 0 }, 3340 },
    { ACTOR_PLAYER, { -1786, 12, -3382 }, { 0, 14382, 0 }, 3839 },
    { ACTOR_PLAYER, { 3662, 1413, -507 }, { 0, 16384, 0 }, 3583 },
    { ACTOR_PLAYER, { 188, 733, -2919 }, { 0, 0, 0 }, 3583 },
};

static const Oot3dEntranceEntry oot3d_spot12_info_setup_2_entrances[] = {
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
    { 10u, 0 },
    { 11u, 0 },
    { 12u, 0 },
    { 13u, 0 },
    { 14u, 0 },
    { 15u, 0 },
    { 16u, 1 },
    { 17u, 0 },
    { 18u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot12_info_setup_2_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot12_info_setup_2_light_settings[] = {
    { { 0xA2, 0x04, 0xA6, 0x04, 0xAA, 0x04, 0xAE, 0x04, 0xB2, 0x04, 0x70, 0x05, 0x08, 0x00, 0x30, 0x01, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0xBB, 0x46, 0x08, 0x07, 0x77, 0x68 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x44, 0x23, 0x33, 0x9B, 0x68, 0x23, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x47, 0xE0, 0x06, 0x96, 0x82 } },
    { { 0x68, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x6D, 0x59, 0x3D, 0xCC, 0xA5, 0x63, 0x00, 0x40, 0x9C, 0x46, 0x00, 0xC0, 0xDA, 0x46, 0x0C, 0x07, 0xA0, 0x4F } },
    { { 0x19, 0x48, 0x48, 0x48, 0xFF, 0xB5, 0x33, 0xB8, 0xB8, 0xB8, 0x4F, 0x14, 0x4F, 0x96, 0x3D, 0x3D, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0x20, 0x07, 0x28, 0x54 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x2D, 0x2D, 0x38, 0xB8, 0xB8, 0xB8, 0x4F, 0xB5, 0xFF, 0x00, 0x00, 0x1E, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x33, 0x49 } },
    { { 0x3D, 0x00, 0x00, 0x00, 0x6D, 0xB5, 0x4F, 0x00, 0x00, 0x00, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0xFC, 0x49, 0x82 } },
    { { 0x63, 0x00, 0x00, 0x00, 0xA0, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x3D, 0x33 } },
    { { 0x28, 0x00, 0x00, 0x00, 0x6D, 0x6D, 0x82, 0x00, 0x00, 0x00, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0xFC, 0x0A, 0x28 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0x14, 0x28, 0x63, 0x00, 0x00, 0x00, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x54, 0x54 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD8, 0x06, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xD1, 0xD1, 0xD1, 0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x4C, 0x4C, 0x4C, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x59, 0x49 } },
    { { 0x49, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x3D, 0x33, 0x3A, 0x28, 0x2D, 0x28, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0x3B, 0x46, 0xD8, 0x06, 0x3D, 0x49 } },
};

static const Oot3dExitEntry oot3d_spot12_info_setup_2_exits[] = {
    { 557, 557u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1158, 1158u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1162, 1162u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1166, 1166u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1170, 1170u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1174, 1174u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1178, 1178u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1182, 1182u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot12_info_setup_2_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot12_info_setup_2_sound_settings[] = {
    { 1u, 1484u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_spot12_info_setup_2_misc_settings[] = {
    { 0u, 0x0000000Cu },
};

static const Oot3dSceneCommand oot3d_spot12_info_setup_3_commands[] = {
    { 0x00130115u, 0x00000000u },
    { 0x00000204u, 0x00009138u },
    { 0x0000010Eu, 0x000091C0u },
    { 0x00000019u, 0x0000000Cu },
    { 0x00000003u, 0x00008798u },
    { 0x00001006u, 0x000091D0u },
    { 0x00000007u, 0x00000002u },
    { 0x00001000u, 0x000091F0u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x000092F0u },
    { 0x0000040Fu, 0x00009310u },
    { 0x00000017u, 0x00009380u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot12_info_setup_3_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot12_info_setup_3_spawns[] = {
    { ACTOR_PLAYER, { 3336, 1447, 191 }, { 0, -32767, 0 }, 4095 },
    { ACTOR_PLAYER, { 60, 347, -1020 }, { 0, -16384, 0 }, 3840 },
    { ACTOR_PLAYER, { 406, 333, -1837 }, { 0, 0, 0 }, 3841 },
    { ACTOR_PLAYER, { 269, 333, -1349 }, { 0, -16384, 0 }, 3841 },
    { ACTOR_PLAYER, { 669, 533, -1402 }, { 0, -16384, 0 }, 3841 },
    { ACTOR_PLAYER, { 377, 572, -1087 }, { 0, -16384, 0 }, 3840 },
    { ACTOR_PLAYER, { 948, 733, -1163 }, { 0, -32767, 0 }, 3840 },
    { ACTOR_PLAYER, { 980, 733, -1399 }, { 0, -16384, 0 }, 3841 },
    { ACTOR_PLAYER, { 676, 533, -2113 }, { 0, 181, 0 }, 3841 },
    { ACTOR_PLAYER, { 1280, 834, -1769 }, { 0, -16384, 0 }, 3841 },
    { ACTOR_PLAYER, { 804, 640, -2656 }, { 0, -16384, 0 }, 3843 },
    { ACTOR_PLAYER, { 680, 333, -1717 }, { 0, -16384, 0 }, 3841 },
    { ACTOR_PLAYER, { 250, 333, -2559 }, { 0, -16384, 0 }, 3843 },
    { ACTOR_PLAYER, { 1244, 653, -2113 }, { 0, 0, 0 }, 3841 },
    { ACTOR_PLAYER, { 359, 1113, -2990 }, { 0, -14199, 0 }, 4095 },
};

static const Oot3dEntranceEntry oot3d_spot12_info_setup_3_entrances[] = {
    { 0u, 1 },
    { 1u, 0 },
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
    { 12u, 0 },
    { 13u, 0 },
    { 14u, 0 },
    { 15u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot12_info_setup_3_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot12_info_setup_3_light_settings[] = {
    { { 0xA2, 0x04, 0xA6, 0x04, 0xAA, 0x04, 0x35, 0x02, 0x08, 0x00, 0xAE, 0x04, 0xB2, 0x04, 0x30, 0x01, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0xBB, 0x46, 0x08, 0x07, 0x77, 0x68 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x44, 0x23, 0x33, 0x9B, 0x68, 0x23, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x47, 0xE0, 0x06, 0x96, 0x82 } },
    { { 0x68, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x6D, 0x59, 0x3D, 0xCC, 0xA5, 0x63, 0x00, 0x40, 0x9C, 0x46, 0x00, 0xC0, 0xDA, 0x46, 0x0C, 0x07, 0xA0, 0x4F } },
    { { 0x19, 0x48, 0x48, 0x48, 0xFF, 0xB5, 0x33, 0xB8, 0xB8, 0xB8, 0x4F, 0x14, 0x4F, 0x96, 0x3D, 0x3D, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0x20, 0x07, 0x28, 0x54 } },
};

static const Oot3dExitEntry oot3d_spot12_info_setup_3_exits[] = {
    { 557, 557u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1158, 1158u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1162, 1162u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1166, 1166u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1170, 1170u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1174, 1174u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1178, 1178u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1182, 1182u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot12_info_setup_3_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot12_info_setup_3_sound_settings[] = {
    { 1u, 0u, 0u, 0u },
};

static const Oot3dCutsceneReference oot3d_spot12_info_setup_3_cutscenes[] = {
    { 0x00009380u, 1u },
};

static const Oot3dMiscSettings oot3d_spot12_info_setup_3_misc_settings[] = {
    { 0u, 0x0000000Cu },
};

static const Oot3dSceneCommand oot3d_spot12_info_setup_4_commands[] = {
    { 0x00130115u, 0x010005D4u },
    { 0x00000204u, 0x00009C80u },
    { 0x0000010Eu, 0x00009D08u },
    { 0x00000019u, 0x0000000Cu },
    { 0x00000003u, 0x00008798u },
    { 0x00000106u, 0x00009D18u },
    { 0x00000007u, 0x00000002u },
    { 0x00000100u, 0x00009D1Cu },
    { 0x00000101u, 0x00009D2Cu },
    { 0x00000011u, 0x00010001u },
    { 0x00000013u, 0x00009D3Cu },
    { 0x0000010Fu, 0x00009D5Cu },
    { 0x00000017u, 0x00009D78u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot12_info_setup_4_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot12_info_setup_4_standard_actors[] = {
    { ACTOR_PLAYER, { 86, 1113, -2676 }, { 0, -32767, 0 }, 255 },
};

static const Oot3dActorEntry oot3d_spot12_info_setup_4_spawns[] = {
    { ACTOR_EN_HOLL, { 1514, 833, -447 }, { -16384, 319, 0 }, 0 },
};

static const Oot3dEntranceEntry oot3d_spot12_info_setup_4_entrances[] = {
    { 0u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot12_info_setup_4_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot12_info_setup_4_light_settings[] = {
    { { 0xA2, 0x04, 0xA6, 0x04, 0xAA, 0x04, 0x35, 0x02, 0x08, 0x00, 0xAE, 0x04, 0xB2, 0x04, 0x30, 0x01, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0x3B, 0x46, 0xC0, 0xFF, 0x6A, 0x58 } },
};

static const Oot3dExitEntry oot3d_spot12_info_setup_4_exits[] = {
    { 400, 400u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { -1134, 64402u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 112, 112u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { -3075, 62461u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 9284, 9284u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 255, 255u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 557, 557u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1158, 1158u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1162, 1162u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1166, 1166u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1170, 1170u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1174, 1174u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1178, 1178u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1182, 1182u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot12_info_setup_4_skybox_settings[] = {
    { 1u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_spot12_info_setup_4_sound_settings[] = {
    { 1u, 1492u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot12_info_setup_4_cutscenes[] = {
    { 0x00009D78u, 1u },
};

static const Oot3dMiscSettings oot3d_spot12_info_setup_4_misc_settings[] = {
    { 0u, 0x0000000Cu },
};

static const Oot3dSceneCommand oot3d_spot12_info_setup_5_commands[] = {
    { 0x00130115u, 0x010005CCu },
    { 0x00000204u, 0x0000A0A8u },
    { 0x0000010Eu, 0x0000A130u },
    { 0x00000019u, 0x0000000Cu },
    { 0x00000003u, 0x00008798u },
    { 0x00000106u, 0x0000A140u },
    { 0x00000107u, 0x00000002u },
    { 0x00000100u, 0x0000A144u },
    { 0x00000011u, 0x00000001u },
    { 0x00000013u, 0x0000A154u },
    { 0x00000C0Fu, 0x0000A174u },
    { 0x00000017u, 0x0000A2C4u },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_spot12_info_setup_5_special_files[] = {
    { 1u, OBJECT_GAMEPLAY_FIELD_KEEP },
};

static const Oot3dActorEntry oot3d_spot12_info_setup_5_spawns[] = {
    { ACTOR_EN_HOLL, { 1514, 833, -447 }, { -16384, 319, 0 }, 31084 },
};

static const Oot3dEntranceEntry oot3d_spot12_info_setup_5_entrances[] = {
    { 0u, 0 },
};

static const Oot3dTransitionActorEntry oot3d_spot12_info_setup_5_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
};

static const Oot3dPicaLightSettingsRecord oot3d_spot12_info_setup_5_light_settings[] = {
    { { 0xA2, 0x04, 0xA6, 0x04, 0xAA, 0x04, 0xAE, 0x04, 0xB2, 0x04, 0x70, 0x05, 0x08, 0x00, 0x30, 0x01, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0xBB, 0x46, 0x08, 0x07, 0x77, 0x68 } },
    { { 0x33, 0x48, 0x48, 0x48, 0xFF, 0xE5, 0x63, 0xB8, 0xB8, 0xB8, 0x44, 0x23, 0x33, 0x9B, 0x68, 0x23, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x47, 0xE0, 0x06, 0x96, 0x82 } },
    { { 0x68, 0x48, 0x48, 0x48, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8, 0xB8, 0x6D, 0x59, 0x3D, 0xCC, 0xA5, 0x63, 0x00, 0x40, 0x9C, 0x46, 0x00, 0xC0, 0xDA, 0x46, 0x0C, 0x07, 0xA0, 0x4F } },
    { { 0x19, 0x48, 0x48, 0x48, 0xFF, 0xB5, 0x33, 0xB8, 0xB8, 0xB8, 0x4F, 0x14, 0x4F, 0x96, 0x3D, 0x3D, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0x20, 0x07, 0x28, 0x54 } },
    { { 0xA0, 0x48, 0x48, 0x48, 0x2D, 0x2D, 0x38, 0xB8, 0xB8, 0xB8, 0x4F, 0xB5, 0xFF, 0x00, 0x00, 0x1E, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x33, 0x49 } },
    { { 0x3D, 0x00, 0x00, 0x00, 0x6D, 0xB5, 0x4F, 0x00, 0x00, 0x00, 0x28, 0x33, 0x77, 0x38, 0x3D, 0x2D, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0x3B, 0x46, 0x28, 0xFC, 0x49, 0x82 } },
    { { 0x63, 0x00, 0x00, 0x00, 0xA0, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x28, 0x33, 0xAA, 0x33, 0x63, 0x72, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x1C, 0x46, 0x28, 0xFC, 0x3D, 0x33 } },
    { { 0x28, 0x00, 0x00, 0x00, 0x6D, 0x6D, 0x82, 0x00, 0x00, 0x00, 0x33, 0x28, 0x8C, 0x33, 0x1E, 0x33, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0xBB, 0x45, 0x28, 0xFC, 0x0A, 0x28 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0x14, 0x28, 0x63, 0x00, 0x00, 0x00, 0x33, 0x82, 0xA0, 0x00, 0x0A, 0x14, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x54, 0x54 } },
    { { 0x4F, 0x00, 0x00, 0x00, 0xB5, 0xB5, 0xA5, 0x00, 0x00, 0x00, 0x33, 0x33, 0x28, 0x3F, 0x3F, 0x33, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x40, 0x9C, 0x46, 0xD8, 0x06, 0x6D, 0x6D } },
    { { 0x6D, 0x00, 0x00, 0x00, 0xD1, 0xD1, 0xD1, 0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x4C, 0x4C, 0x4C, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x00, 0x7A, 0x46, 0xD8, 0x06, 0x59, 0x49 } },
    { { 0x49, 0x00, 0x00, 0x00, 0xB5, 0xA5, 0xA5, 0x00, 0x00, 0x00, 0x3D, 0x33, 0x3A, 0x28, 0x2D, 0x28, 0x00, 0x40, 0x9C, 0x46, 0x00, 0x80, 0x3B, 0x46, 0xD8, 0x06, 0x3D, 0x49 } },
};

static const Oot3dExitEntry oot3d_spot12_info_setup_5_exits[] = {
    { 557, 557u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1158, 1158u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1162, 1162u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1166, 1166u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1170, 1170u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1174, 1174u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1178, 1178u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 1182, 1182u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_spot12_info_setup_5_skybox_settings[] = {
    { 1u, 0u, 0u },
};

static const Oot3dSoundSettings oot3d_spot12_info_setup_5_sound_settings[] = {
    { 1u, 1484u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_spot12_info_setup_5_cutscenes[] = {
    { 0x0000A2C4u, 1u },
};

static const Oot3dMiscSettings oot3d_spot12_info_setup_5_misc_settings[] = {
    { 0u, 0x0000000Cu },
};

static const Oot3dActorEntry oot3d_spot12_info_spot12_0_info_actors[] = {
    { ACTOR_OBJ_KIBAKO2, { -120, 333, -2210 }, { 0, 0, 0 }, -1 },
    { ACTOR_OBJ_KIBAKO2, { -60, 333, -2210 }, { 0, 0, 0 }, -1 },
    { ACTOR_OBJ_KIBAKO2, { 310, 333, -1830 }, { 0, -16384, 0 }, -1 },
    { ACTOR_OBJ_KIBAKO2, { 310, 333, -1770 }, { 0, -16384, 0 }, -1 },
    { ACTOR_OBJ_KIBAKO2, { -4571, -20, -3429 }, { 0, 0, 0 }, -1 },
    { ACTOR_OBJ_KIBAKO2, { 315, 333, -1594 }, { 0, 16384, 0 }, -1 },
    { ACTOR_OBJ_KIBAKO2, { 315, 333, -1534 }, { 0, 16384, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_spot12_info_spot12_0_info_objects[] = {
    { OBJECT_UNSET_3A, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_O_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OE_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HATA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HORSE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HORSE_NORMAL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HNI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_YABUSAME_POINT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KIBAKO2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GE1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_spot12_info_spot12_1_info_actors[] = {
    { ACTOR_EN_G_SWITCH, { 4090, 1450, -2980 }, { 0, 0, 0 }, 12287 },
    { ACTOR_EN_G_SWITCH, { 4090, 1450, -2740 }, { 0, 0, 0 }, 12287 },
    { ACTOR_EN_G_SWITCH, { 4090, 1450, -2500 }, { 0, 0, 0 }, 12287 },
    { ACTOR_EN_G_SWITCH, { 4090, 1450, -2260 }, { 0, 0, 0 }, 12287 },
    { ACTOR_EN_G_SWITCH, { 4090, 1450, -2020 }, { 0, 0, 0 }, 12287 },
    { ACTOR_EN_G_SWITCH, { 4090, 1450, -1780 }, { 0, 0, 0 }, 12287 },
    { ACTOR_EN_G_SWITCH, { 4090, 1450, -1540 }, { 0, 0, 0 }, 12287 },
    { ACTOR_EN_YABUSAME_MARK, { 3380, 1734, -4935 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_YABUSAME_MARK, { 3360, 1734, 485 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_YABUSAME_MARK, { 4490, 1670, -2815 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_YABUSAME_MARK, { 4490, 1670, -1785 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_YABUSAME_MARK, { 4490, 1720, -2295 }, { 0, 0, 0 }, 2 },
    { ACTOR_EN_HORSE_GAME_CHECK, { 3256, 1413, -2571 }, { 0, 0, 0 }, 2 },
    { ACTOR_EN_HORSE, { 3705, 1413, -665 }, { 0, -16384, 0 }, -1 },
    { ACTOR_OBJ_KIBAKO2, { 3303, 1441, -5018 }, { 0, -545, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_spot12_info_spot12_1_info_objects[] = {
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_O_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OE_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HATA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HORSE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HORSE_NORMAL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_HNI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_YABUSAME_POINT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KIBAKO2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GE1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_spot12_info_room_refs[] = {
    { "spot12_0_info.zsi", 0 },
    { "spot12_1_info.zsi", 1 },
};

static const Oot3dSceneSetupIndex oot3d_spot12_info_setups[] = {
    { 0u, oot3d_spot12_info_setup_0_commands, 12u, oot3d_spot12_info_setup_0_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot12_info_setup_0_spawns, 17u, oot3d_spot12_info_setup_0_entrances, 18u, oot3d_spot12_info_setup_0_transition_actors, 1u, oot3d_spot12_info_setup_0_light_settings, 12u, oot3d_spot12_info_setup_0_exits, 8u, oot3d_spot12_info_setup_0_skybox_settings, 1u, oot3d_spot12_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_spot12_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_spot12_info_setup_1_commands, 12u, oot3d_spot12_info_setup_1_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot12_info_setup_1_spawns, 18u, oot3d_spot12_info_setup_1_entrances, 19u, oot3d_spot12_info_setup_1_transition_actors, 1u, oot3d_spot12_info_setup_1_light_settings, 12u, oot3d_spot12_info_setup_1_exits, 8u, oot3d_spot12_info_setup_1_skybox_settings, 1u, oot3d_spot12_info_setup_1_sound_settings, 1u, NULL, 0u, oot3d_spot12_info_setup_1_misc_settings, 1u },
    { 2u, oot3d_spot12_info_setup_2_commands, 12u, oot3d_spot12_info_setup_2_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot12_info_setup_2_spawns, 18u, oot3d_spot12_info_setup_2_entrances, 19u, oot3d_spot12_info_setup_2_transition_actors, 1u, oot3d_spot12_info_setup_2_light_settings, 12u, oot3d_spot12_info_setup_2_exits, 8u, oot3d_spot12_info_setup_2_skybox_settings, 1u, oot3d_spot12_info_setup_2_sound_settings, 1u, NULL, 0u, oot3d_spot12_info_setup_2_misc_settings, 1u },
    { 3u, oot3d_spot12_info_setup_3_commands, 13u, oot3d_spot12_info_setup_3_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot12_info_setup_3_spawns, 15u, oot3d_spot12_info_setup_3_entrances, 16u, oot3d_spot12_info_setup_3_transition_actors, 1u, oot3d_spot12_info_setup_3_light_settings, 4u, oot3d_spot12_info_setup_3_exits, 8u, oot3d_spot12_info_setup_3_skybox_settings, 1u, oot3d_spot12_info_setup_3_sound_settings, 1u, oot3d_spot12_info_setup_3_cutscenes, 1u, oot3d_spot12_info_setup_3_misc_settings, 1u },
    { 4u, oot3d_spot12_info_setup_4_commands, 14u, oot3d_spot12_info_setup_4_special_files, 1u, NULL, 0u, oot3d_spot12_info_setup_4_standard_actors, 1u, oot3d_spot12_info_setup_4_spawns, 1u, oot3d_spot12_info_setup_4_entrances, 1u, oot3d_spot12_info_setup_4_transition_actors, 1u, oot3d_spot12_info_setup_4_light_settings, 1u, oot3d_spot12_info_setup_4_exits, 16u, oot3d_spot12_info_setup_4_skybox_settings, 1u, oot3d_spot12_info_setup_4_sound_settings, 1u, oot3d_spot12_info_setup_4_cutscenes, 1u, oot3d_spot12_info_setup_4_misc_settings, 1u },
    { 5u, oot3d_spot12_info_setup_5_commands, 13u, oot3d_spot12_info_setup_5_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_spot12_info_setup_5_spawns, 1u, oot3d_spot12_info_setup_5_entrances, 1u, oot3d_spot12_info_setup_5_transition_actors, 1u, oot3d_spot12_info_setup_5_light_settings, 12u, oot3d_spot12_info_setup_5_exits, 8u, oot3d_spot12_info_setup_5_skybox_settings, 1u, oot3d_spot12_info_setup_5_sound_settings, 1u, oot3d_spot12_info_setup_5_cutscenes, 1u, oot3d_spot12_info_setup_5_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_spot12_info_rooms[] = {
    { "spot12_0_info.zsi", 0, oot3d_spot12_info_spot12_0_info_objects, 13u, oot3d_spot12_info_spot12_0_info_actors, 7u },
    { "spot12_1_info.zsi", 1, oot3d_spot12_info_spot12_1_info_objects, 12u, oot3d_spot12_info_spot12_1_info_actors, 15u },
};

const Oot3dSceneIndex oot3d_scene_index_spot12_info = {
    "spot12_info.zsi",
    oot3d_spot12_info_room_refs, 2u,
    oot3d_spot12_info_rooms, 2u,
    oot3d_spot12_info_setups, 6u,
};
