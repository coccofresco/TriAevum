/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: ice_doukutu_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dSceneCommand oot3d_ice_doukutu_info_setup_0_commands[] = {
    { 0x00130515u, 0x010005C5u },
    { 0x00000C04u, 0x000000E0u },
    { 0x00000C0Eu, 0x00000410u },
    { 0x00000019u, 0x00000008u },
    { 0x00000003u, 0x00015C88u },
    { 0x00000206u, 0x00015CB4u },
    { 0x00000207u, 0x00000003u },
    { 0x00000200u, 0x00015CB8u },
    { 0x00000011u, 0x00010000u },
    { 0x00000013u, 0x00015CD8u },
    { 0x0000060Fu, 0x00015CDCu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_ice_doukutu_info_setup_0_special_files[] = {
    { 2u, OBJECT_GAMEPLAY_DANGEON_KEEP },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_setup_0_spawns[] = {
    { ACTOR_PLAYER, { 1025, 0, 16 }, { 0, 2727, 0 }, -32767 },
};

static const Oot3dEntranceEntry oot3d_ice_doukutu_info_setup_0_entrances[] = {
    { 0u, 0 },
    { 1u, 4 },
};

static const Oot3dTransitionActorEntry oot3d_ice_doukutu_info_setup_0_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 1, -1 }, { 10, -1 }, ACTOR_EN_HOLL, { -666, 155, 1434 }, -20571, 319 },
    { { 3, -1 }, { 8, -1 }, ACTOR_EN_HOLL, { 902, 172, -777 }, 8010, 319 },
    { { 2, -1 }, { 3, -1 }, ACTOR_EN_HOLL, { 50, 20, 258 }, 17657, 319 },
    { { 1, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -117, 6, 604 }, -16384, 319 },
    { { 1, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { 154, 7, 1899 }, 16384, 319 },
    { { 3, -1 }, { 4, -1 }, ACTOR_EN_HOLL, { -251, 9, -363 }, -16384, 319 },
    { { 4, -1 }, { 5, -1 }, ACTOR_EN_HOLL, { -433, 26, -963 }, -24940, 319 },
    { { 6, -1 }, { 5, -1 }, ACTOR_EN_HOLL, { -1337, 132, -81 }, -25486, 319 },
    { { 8, -1 }, { 9, -1 }, ACTOR_EN_HOLL, { 899, 172, -1560 }, -12924, 319 },
    { { 3, -1 }, { 11, -1 }, ACTOR_EN_HOLL, { 764, -27, 27 }, -5460, 319 },
    { { 7, -1 }, { 10, -1 }, ACTOR_DOOR_SHUTTER, { -866, 80, 1216 }, 8374, 63 },
};

static const Oot3dPicaLightSettingsRecord oot3d_ice_doukutu_info_setup_0_light_settings[] = {
    { { 0x3C, 0x00, 0x49, 0xFC, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0xFF, 0x0D, 0xD4, 0x03, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0x0C, 0x44, 0x28, 0x10, 0x68, 0x68 } },
    { { 0x82, 0x26, 0x7F, 0x26, 0x9B, 0x9B, 0xE5, 0xDA, 0x81, 0xDA, 0x0A, 0x3D, 0xFF, 0xBF, 0xBF, 0xE5, 0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0x2A, 0x44, 0x50, 0x10, 0x68, 0x68 } },
    { { 0x82, 0x26, 0x7F, 0x26, 0xA0, 0xAA, 0xE5, 0xDA, 0x81, 0xDA, 0x0A, 0x4F, 0xFF, 0x9B, 0xBF, 0xE5, 0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0xAF, 0x44, 0x2C, 0x11, 0x77, 0x77 } },
    { { 0x82, 0x26, 0x7F, 0x26, 0xAA, 0xBF, 0xFF, 0xDA, 0x81, 0xDA, 0x33, 0x9B, 0xFF, 0x9B, 0xBF, 0xE5, 0x00, 0x00, 0x48, 0x46, 0x00, 0x80, 0x3B, 0x45, 0x78, 0xFC, 0x4F, 0x6D } },
    { { 0x82, 0x26, 0x7F, 0x26, 0xCC, 0xCC, 0xFF, 0xDA, 0x81, 0xDA, 0x68, 0xAA, 0xFF, 0x33, 0x33, 0x4F, 0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0xFA, 0x44, 0x78, 0x10, 0x77, 0x77 } },
    { { 0x82, 0x26, 0x7F, 0x26, 0xAA, 0xBF, 0xFF, 0xDA, 0x81, 0xDA, 0x33, 0xAA, 0xFF, 0xA0, 0xCC, 0xE5, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x96, 0x44, 0x00, 0xFC, 0x49, 0x59 } },
};

static const Oot3dExitEntry oot3d_ice_doukutu_info_setup_0_exits[] = {
    { 980, 980u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_ice_doukutu_info_setup_0_skybox_settings[] = {
    { 0u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_ice_doukutu_info_setup_0_sound_settings[] = {
    { 5u, 1477u, 0u, 1u },
};

static const Oot3dMiscSettings oot3d_ice_doukutu_info_setup_0_misc_settings[] = {
    { 0u, 0x00000008u },
};

static const Oot3dSceneCommand oot3d_ice_doukutu_info_setup_1_commands[] = {
    { 0x00130315u, 0x010005C5u },
    { 0x00000C04u, 0x00015D84u },
    { 0x00000C0Eu, 0x000160B4u },
    { 0x00000019u, 0x00000000u },
    { 0x00000003u, 0x00015C88u },
    { 0x00000106u, 0x00016174u },
    { 0x00000007u, 0x00000003u },
    { 0x00000100u, 0x00016178u },
    { 0x00000011u, 0x00010000u },
    { 0x00000013u, 0x00016188u },
    { 0x0000040Fu, 0x0001618Cu },
    { 0x00000017u, 0x000161FCu },
    { 0x00000014u, 0x00000000u },
};

static const Oot3dSpecialFiles oot3d_ice_doukutu_info_setup_1_special_files[] = {
    { 0u, OBJECT_GAMEPLAY_DANGEON_KEEP },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_setup_1_spawns[] = {
    { ACTOR_EN_DOOR, { -1340, 280, 729 }, { 8191, 63, 1792 }, 0 },
};

static const Oot3dEntranceEntry oot3d_ice_doukutu_info_setup_1_entrances[] = {
    { 0u, 7 },
};

static const Oot3dTransitionActorEntry oot3d_ice_doukutu_info_setup_1_transition_actors[] = {
    { { 0, 0 }, { 0, 0 }, ACTOR_PLAYER, { 0, 0, 0 }, 0, 0 },
    { { 10, -1 }, { 10, -1 }, ACTOR_EN_HOLL, { -666, 155, 1434 }, -20571, 319 },
    { { 8, -1 }, { 8, -1 }, ACTOR_EN_HOLL, { 902, 172, -777 }, 8010, 319 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { 50, 20, 258 }, 17657, 319 },
    { { 2, -1 }, { 2, -1 }, ACTOR_EN_HOLL, { -117, 6, 604 }, -16384, 319 },
    { { 0, -1 }, { 0, -1 }, ACTOR_EN_HOLL, { 154, 7, 1899 }, 16384, 319 },
    { { 4, -1 }, { 4, -1 }, ACTOR_EN_HOLL, { -251, 9, -363 }, -16384, 319 },
    { { 4, -1 }, { 4, -1 }, ACTOR_EN_HOLL, { -433, 26, -963 }, -24940, 319 },
    { { 6, -1 }, { 6, -1 }, ACTOR_EN_HOLL, { -1337, 132, -81 }, -25486, 319 },
    { { 8, -1 }, { 8, -1 }, ACTOR_EN_HOLL, { 899, 172, -1560 }, -12924, 319 },
    { { 11, -1 }, { 11, -1 }, ACTOR_EN_HOLL, { 764, -27, 27 }, -5460, 319 },
    { { 7, -1 }, { 10, -1 }, ACTOR_EN_DOOR, { -866, 80, 1216 }, 8374, 63 },
};

static const Oot3dPicaLightSettingsRecord oot3d_ice_doukutu_info_setup_1_light_settings[] = {
    { { 0x1D, 0x01, 0x76, 0x03, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0xFF, 0x0F, 0xD4, 0x03, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0xFA, 0x44, 0xF0, 0x06, 0x46, 0x46 } },
    { { 0x59, 0x00, 0x7F, 0x00, 0x77, 0x77, 0xFF, 0x00, 0x81, 0x00, 0x77, 0x77, 0xFF, 0x87, 0x9A, 0x9A, 0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0xFA, 0x44, 0x00, 0x07, 0x31, 0x31 } },
    { { 0x77, 0x00, 0x7F, 0x00, 0x77, 0xA0, 0xDB, 0x00, 0x81, 0x00, 0x95, 0xB3, 0xFF, 0x64, 0x64, 0x77, 0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0xFA, 0x44, 0x08, 0x07, 0x4F, 0x4F } },
    { { 0x64, 0x00, 0x7F, 0x00, 0x64, 0x64, 0xDB, 0x00, 0x81, 0x00, 0x95, 0xB3, 0xFF, 0x3B, 0x3B, 0x4F, 0x00, 0x00, 0x48, 0x46, 0x00, 0x00, 0x48, 0x46, 0x1C, 0x07, 0x46, 0x46 } },
};

static const Oot3dExitEntry oot3d_ice_doukutu_info_setup_1_exits[] = {
    { 980, 980u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
    { 0, 0u, OOT3D_EXIT_DIRECT_TRANSITION_VALUE },
};

static const Oot3dSkyboxSettings oot3d_ice_doukutu_info_setup_1_skybox_settings[] = {
    { 0u, 0u, 1u },
};

static const Oot3dSoundSettings oot3d_ice_doukutu_info_setup_1_sound_settings[] = {
    { 3u, 1477u, 0u, 1u },
};

static const Oot3dCutsceneReference oot3d_ice_doukutu_info_setup_1_cutscenes[] = {
    { 0x000161FCu, 1u },
};

static const Oot3dMiscSettings oot3d_ice_doukutu_info_setup_1_misc_settings[] = {
    { 0u, 0x00000000u },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_0_dd_info_actors[] = {
    { ACTOR_BG_ICE_TURARA, { 324, 3, 2280 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 287, 3, 2346 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_GOROIWA, { 600, 4, 1829 }, { 0, 0, 1 }, 3328 },
    { ACTOR_OBJ_TSUBO, { 248, 10, 2287 }, { 0, 0, 0 }, 16387 },
    { ACTOR_EN_WONDER_ITEM, { 195, 2, 2605 }, { 0, 0, 0 }, 16360 },
    { ACTOR_EN_WONDER_ITEM, { 168, 2, 2533 }, { 0, 0, 0 }, 16360 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_0_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_0_info_actors[] = {
    { ACTOR_BG_ICE_TURARA, { 295, 0, 2386 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 313, 3, 2446 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 400, 4, 2443 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 499, 107, 2229 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_ICE_TURARA, { 536, 124, 2071 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_ICE_SHELTER, { 411, 4, 2332 }, { 0, 0, 0 }, 256 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_0_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_1_dd_info_actors[] = {
    { ACTOR_EN_FZ, { 4, -10, 1058 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_TITE, { -132, -10, 1252 }, { 0, 0, 0 }, -2 },
    { ACTOR_EN_TITE, { 140, -10, 1256 }, { 0, 0, 0 }, -2 },
    { ACTOR_BG_ICE_TURARA, { 204, 420, 910 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_ICE_TURARA, { 268, 420, 980 }, { 0, 0, 0 }, 1 },
    { ACTOR_OBJ_TSUBO, { 51, 11, 718 }, { 0, 0, 0 }, 18947 },
    { ACTOR_OBJ_TSUBO, { 52, 10, 768 }, { 0, 0, 0 }, 19459 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_1_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_1_info_actors[] = {
    { ACTOR_EN_ITEM00, { -105, -10, 854 }, { 0, -32767, 0 }, 8449 },
    { ACTOR_EN_FZ, { -211, -10, 997 }, { 0, 8191, 0 }, -1 },
    { ACTOR_EN_TRAP, { 1, -10, 1195 }, { 0, 0, 0 }, 9760 },
    { ACTOR_BG_ICE_TURARA, { -62, 1, 1642 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -19, 2, 1676 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 36, 1, 1643 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -18, 110, 1760 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_ICE_SHUTTER, { 0, 1, 726 }, { 0, 0, 0 }, 256 },
    { ACTOR_BG_ICE_SHELTER, { -105, -10, 854 }, { 0, 0, 0 }, 259 },
    { ACTOR_BG_ICE_SHELTER, { 119, -10, 856 }, { 0, 0, 0 }, 260 },
    { ACTOR_EN_FZ, { -111, -10, 1277 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_FZ, { 0, -10, 1078 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_FZ, { 138, -10, 1260 }, { 0, -5097, 0 }, 0 },
    { ACTOR_SHOT_SUN, { 4, -10, 1186 }, { 0, 0, 0 }, -191 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_1_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SPOT02_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TRAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_2_dd_info_actors[] = {
    { ACTOR_BG_ICE_TURARA, { -288, 12, 402 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -212, 13, 398 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -351, 11, 423 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -258, 11, 461 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_GOROIWA, { 298, 451, 64 }, { 0, 0, 1 }, 3329 },
    { ACTOR_BG_ICE_TURARA, { -19, 13, 193 }, { 0, 0, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_2_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_2_info_actors[] = {
    { ACTOR_OBJ_TSUBO, { -206, 11, 449 }, { 0, 0, 0 }, 18947 },
    { ACTOR_OBJ_TSUBO, { -203, 10, 492 }, { 0, 0, 0 }, 19459 },
    { ACTOR_BG_ICE_TURARA, { -308, 13, 322 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -272, 13, 348 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -226, 12, 371 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -187, 13, 395 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -237, 129, 472 }, { 0, 0, 0 }, 1 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_2_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TRAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_3_dd_info_actors[] = {
    { ACTOR_BG_ICE_TURARA, { 640, 0, -312 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 614, 0, -292 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 597, 0, -256 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 598, 0, -213 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 607, 0, -170 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_WF, { 271, 0, -401 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_FZ, { -35, 0, -409 }, { 0, 15291, 0 }, -1 },
    { ACTOR_EN_FZ, { 541, 0, -503 }, { 0, -7464, 0 }, -1 },
    { ACTOR_BG_ICE_SHUTTER, { 691, 7, -217 }, { 0, -20934, 0 }, 1280 },
    { ACTOR_BG_ICE_SHELTER, { -134, 0, -462 }, { 0, 0, 0 }, 33 },
    { ACTOR_BG_ICE_SHELTER, { -121, 0, -418 }, { 0, 0, 0 }, 34 },
    { ACTOR_BG_ICE_SHELTER, { -142, 0, -377 }, { 0, 0, 0 }, 35 },
    { ACTOR_OBJ_TSUBO, { 433, 0, -732 }, { 0, 0, 0 }, 19986 },
    { ACTOR_OBJ_TSUBO, { 569, 0, -175 }, { 0, 0, 0 }, 20481 },
    { ACTOR_BG_ICE_SHELTER, { 577, 172, -818 }, { 0, 0, 0 }, 41 },
    { ACTOR_OBJ_TSUBO, { 521, 0, -131 }, { 0, 0, 0 }, 20995 },
    { ACTOR_BG_ICE_SHELTER, { 614, 172, -770 }, { 0, 0, 0 }, 42 },
    { ACTOR_BG_ICE_SHELTER, { 656, 172, -722 }, { 0, 0, 0 }, 43 },
    { ACTOR_OBJ_TSUBO, { 138, 0, -672 }, { 0, 0, 0 }, 22546 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_3_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_3_info_actors[] = {
    { ACTOR_EN_BOX, { -118, 387, -409 }, { 0, -16384, 0 }, 127 },
    { ACTOR_BG_HAKA_SGAMI, { 293, 0, -384 }, { 0, 0, 0 }, 256 },
    { ACTOR_BG_ICE_SHELTER, { 554, 172, -701 }, { 0, -8374, 0 }, 1023 },
    { ACTOR_BG_ICE_SHELTER, { -55, 0, -415 }, { 0, 15838, 0 }, 1023 },
    { ACTOR_BG_ICE_SHELTER, { -134, 0, -415 }, { 0, 15838, 0 }, 1023 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_3_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_XC, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_MELODY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_4_dd_info_actors[] = {
    { ACTOR_BG_ICE_TURARA, { -261, 31, -840 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_GOROIWA, { -638, 449, -1163 }, { 0, 0, 1 }, 3330 },
    { ACTOR_EN_WONDER_ITEM, { -400, 19, -440 }, { 0, 0, 0 }, 16358 },
    { ACTOR_EN_WONDER_ITEM, { -439, 19, -411 }, { 0, 0, 0 }, 16358 },
    { ACTOR_EN_WONDER_ITEM, { -482, 20, -386 }, { 0, 0, 0 }, 16358 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_4_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_4_info_actors[] = {
    { ACTOR_BG_ICE_TURARA, { -518, 23, -601 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -487, 22, -600 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -451, 22, -604 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -414, 22, -610 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -373, 22, -605 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -385, 135, -686 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { -359, 138, -862 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { -314, 122, -768 }, { 0, 0, 0 }, 2 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_4_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_XC, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TRAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_5_dd_info_actors[] = {
    { ACTOR_EN_ICE_HONO, { -1941, 150, -690 }, { 0, 0, 0 }, -1 },
    { ACTOR_OBJ_TIMEBLOCK, { -1810, 0, -695 }, { 0, 0, 0 }, 4607 },
    { ACTOR_EN_KAKASI2, { -1170, 119, -1373 }, { 0, 0, 9 }, 6399 },
    { ACTOR_EN_WF, { -1563, 41, -956 }, { 0, 16384, 0 }, -255 },
    { ACTOR_EN_WF, { -1156, 61, -683 }, { 0, 16565, 0 }, -255 },
    { ACTOR_EN_FIREFLY, { -1726, 341, -748 }, { 0, 16384, 0 }, 4 },
    { ACTOR_EN_FIREFLY, { -1721, 341, -641 }, { 0, 16384, 0 }, 4 },
    { ACTOR_EN_SW, { -1151, 185, -1656 }, { 0, 10376, 0 }, -30207 },
    { ACTOR_EN_SW, { -1393, 0, -233 }, { 0, -32767, 0 }, -30204 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_5_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_5_info_actors[] = {
    { ACTOR_EN_ITEM00, { -1699, 225, -697 }, { 0, 0, 0 }, 11778 },
    { ACTOR_EN_ITEM00, { -1648, 226, -715 }, { 0, 0, 0 }, 12034 },
    { ACTOR_EN_ITEM00, { -1648, 225, -677 }, { 0, 0, 0 }, 12290 },
    { ACTOR_EN_FIREFLY, { -1367, 0, -1017 }, { 0, 0, 0 }, 4 },
    { ACTOR_EN_FIREFLY, { -1359, 0, -749 }, { 0, -1091, 0 }, 4 },
    { ACTOR_EN_FIREFLY, { -1208, 0, -490 }, { 0, 0, 0 }, 4 },
    { ACTOR_EN_SW, { -761, 185, -525 }, { 0, -22390, 0 }, -30207 },
    { ACTOR_OBJ_TIMEBLOCK, { -1681, 120, -695 }, { 0, 0, 3 }, 14394 },
    { ACTOR_OBJ_TIMEBLOCK, { -1820, 122, -695 }, { 0, 0, 3 }, 14648 },
    { ACTOR_OBJ_TIMEBLOCK, { -1760, 122, -695 }, { 0, 0, 3 }, 14649 },
    { ACTOR_EN_G_SWITCH, { -1676, 112, -552 }, { 0, 0, 0 }, 8137 },
    { ACTOR_EN_G_SWITCH, { -1558, 41, -951 }, { 0, 0, 0 }, 8137 },
    { ACTOR_EN_G_SWITCH, { -1294, 113, -899 }, { 0, 0, 0 }, 8137 },
    { ACTOR_EN_G_SWITCH, { -1120, 119, -1577 }, { 0, 0, 0 }, 8137 },
    { ACTOR_EN_G_SWITCH, { -1040, 112, -485 }, { 0, 0, 0 }, 8137 },
    { ACTOR_EN_G_SWITCH, { -1387, 0, -334 }, { 0, 0, 0 }, 329 },
    { ACTOR_EN_ICE_HONO, { -1942, 150, -690 }, { 0, 0, 0 }, -1 },
    { ACTOR_BG_ICE_SHUTTER, { -1388, 120, -164 }, { 0, -32767, 0 }, 2305 },
    { ACTOR_BG_ICE_OBJECTS, { -1060, 0, -900 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_SHELTER, { -1126, 119, -1577 }, { 0, -2731, 0 }, 16 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_5_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_6_dd_info_actors[] = {
    { ACTOR_BG_ICE_TURARA, { -1487, 354, 569 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_ICE_TURARA, { -1583, 403, 609 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_ICE_TURARA, { -1524, 443, 326 }, { 0, 0, 0 }, 1 },
    { ACTOR_EN_FIREFLY, { -1573, 373, 506 }, { 0, -24030, 0 }, 4 },
    { ACTOR_EN_FIREFLY, { -1330, 348, 159 }, { 0, 13107, 0 }, 4 },
    { ACTOR_OBJ_TSUBO, { -1352, 273, 639 }, { 0, 0, 0 }, 21522 },
    { ACTOR_OBJ_TSUBO, { -1396, 267, 596 }, { 0, 0, 0 }, 22030 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_6_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_6_info_actors[] = {
    { ACTOR_OBJ_TSUBO, { -1422, 265, 586 }, { 0, 0, 0 }, 21507 },
    { ACTOR_OBJ_TSUBO, { -1488, 271, 676 }, { 0, 0, 0 }, 22019 },
    { ACTOR_BG_ICE_TURARA, { -1624, 227, 431 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -1595, 248, 600 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -1587, 240, 497 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -1514, 203, 307 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -1507, 226, 443 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -1367, 177, 206 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -1349, 188, 270 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -1288, 172, 184 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { -1571, 473, 422 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_ICE_TURARA, { -1412, 398, 246 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { -1328, 385, 215 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { -1231, 350, 170 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_ICE_SHELTER, { -1459, 267, 625 }, { 0, 0, 0 }, 273 },
    { ACTOR_BG_ICE_SHELTER, { -1488, 271, 676 }, { 0, 0, 0 }, 18 },
    { ACTOR_BG_ICE_SHELTER, { -1422, 265, 586 }, { 0, 0, 0 }, 19 },
    { ACTOR_EN_FZ, { -1513, 246, 560 }, { 0, -32767, 0 }, 0 },
    { ACTOR_EN_FZ, { -1508, 209, 358 }, { 0, 25667, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_6_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_XC, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TRAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_7_dd_info_actors[] = {
    { ACTOR_EN_XC, { -1219, 291, 777 }, { 0, 0, 0 }, 2 },
    { ACTOR_DEMO_KANKYO, { -1194, 290, 828 }, { 0, 0, 0 }, 9 },
    { ACTOR_BG_ICE_SHELTER, { -1005, 280, 1068 }, { 0, -32767, 0 }, 767 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_7_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_XC, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_MELODY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOMA, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_7_info_actors[] = {
    { ACTOR_EN_WF, { -1096, 282, 964 }, { 0, -26032, 0 }, 5121 },
    { ACTOR_EN_XC, { -1108, 290, 660 }, { 0, 0, 0 }, 8 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_7_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_XC, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_8_dd_info_actors[] = {
    { ACTOR_EN_FZ, { 1109, 172, -1213 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_FZ, { 1219, 172, -1076 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_GOROIWA, { 1470, 707, -1243 }, { 0, 0, 1 }, 3331 },
    { ACTOR_EN_GOROIWA, { 897, 687, -1687 }, { 0, 0, 1 }, 3332 },
    { ACTOR_EN_WONDER_ITEM, { 1074, 172, -722 }, { 0, 0, 0 }, 16359 },
    { ACTOR_EN_WONDER_ITEM, { 1110, 172, -656 }, { 0, 0, 0 }, 16359 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_8_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_8_info_actors[] = {
    { ACTOR_BG_ICE_TURARA, { 951, 172, -1217 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1029, 172, -1217 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1187, 172, -1217 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1063, 365, -1300 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { 1088, 351, -1505 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { 1141, 402, -946 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { 1229, 334, -1134 }, { 0, 0, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { 1264, 348, -890 }, { 0, 0, 0 }, 2 },
    { ACTOR_EN_FZ, { 1103, 172, -1216 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_TRAP, { 950, 172, -1061 }, { 0, 16384, 0 }, 1808 },
    { ACTOR_EN_TRAP, { 980, 172, -1390 }, { 0, 16384, 0 }, 2320 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_8_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TRAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_9_dd_info_actors[] = {
    { ACTOR_EN_FZ, { 856, 182, -2411 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_FZ, { 297, 262, -2545 }, { 0, 3822, 0 }, 0 },
    { ACTOR_EN_ICE_HONO, { 602, 226, -2468 }, { 0, 0, 0 }, -1 },
    { ACTOR_OBJ_TIMEBLOCK, { 272, 112, -2065 }, { 0, 2730, 0 }, 4351 },
    { ACTOR_OBJ_TIMEBLOCK, { 277, 112, -2196 }, { 0, 3277, 0 }, 4351 },
    { ACTOR_EN_FZ, { 711, 22, -2305 }, { 0, 0, 0 }, -1 },
    { ACTOR_BG_ICE_TURARA, { 741, 22, -2463 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 783, 22, -2453 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 903, 22, -2353 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 929, 22, -2303 }, { 0, 0, 0 }, 0 },
    { ACTOR_OBJ_TSUBO, { 901, 22, -2720 }, { 0, 0, 0 }, 28171 },
    { ACTOR_OBJ_TSUBO, { 451, 22, -2726 }, { 0, 0, 0 }, 27659 },
    { ACTOR_EN_BOX, { 275, 262, -2607 }, { 0, -28944, 0 }, 2048 },
    { ACTOR_EN_ITEM00, { 1196, 183, -2231 }, { 0, 0, 0 }, 262 },
    { ACTOR_EN_SW, { 376, 213, -2048 }, { 0, 0, 0 }, -30206 },
    { ACTOR_OBJ_SWITCH, { 660, -53, -2220 }, { 0, 0, 0 }, 10003 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_9_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_9_info_actors[] = {
    { ACTOR_EN_FIREFLY, { 429, 356, -2385 }, { 0, 21844, 0 }, 4 },
    { ACTOR_EN_FIREFLY, { 451, 340, -2601 }, { 0, 0, 0 }, 4 },
    { ACTOR_EN_FIREFLY, { 963, 303, -2046 }, { 0, -19842, 0 }, 4 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_9_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TRAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_11_dd_info_actors[] = {
    { ACTOR_BG_ICE_SHELTER, { 1201, -71, 640 }, { 0, 0, 0 }, 767 },
    { ACTOR_EN_ICE_HONO, { 1491, -22, 413 }, { 0, 0, 0 }, -1 },
    { ACTOR_BG_ICE_TURARA, { 1345, -71, 260 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1375, -71, 291 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1343, -71, 332 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1325, -71, 369 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1321, -71, 410 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1326, -71, 442 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1333, -71, 473 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1149, -71, 64 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1170, -71, 97 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_WF, { 1246, -71, 363 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_WF, { 1241, -71, 117 }, { 0, 0, 0 }, -256 },
    { ACTOR_EN_SW, { 1123, 29, 79 }, { 0, 7464, 0 }, 0 },
    { ACTOR_EN_BOX, { 1201, -71, 641 }, { 0, 0, 3 }, -18399 },
    { ACTOR_OBJ_SWITCH, { 1114, -88, 104 }, { 0, 0, 0 }, 771 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_11_dd_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TITE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOROIWA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DEMO_KEKKAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TIMEBLOCK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_TW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ice_doukutu_info_ice_doukutu_11_info_actors[] = {
    { ACTOR_EN_FIREFLY, { 1099, 17, 669 }, { 0, 30400, 0 }, 3 },
    { ACTOR_EN_FIREFLY, { 1216, 68, 671 }, { 0, 30948, 0 }, 3 },
    { ACTOR_EN_FIREFLY, { 1461, -41, 412 }, { 0, -18386, 0 }, 3 },
    { ACTOR_EN_ITEM00, { 1261, -71, 68 }, { 0, 0, 0 }, 262 },
    { ACTOR_EN_SW, { 1338, 143, 186 }, { 0, -29126, 0 }, -30204 },
    { ACTOR_BG_ICE_TURARA, { 1159, -71, 552 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1191, -71, 565 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1223, -71, 195 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1226, -71, 549 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1256, -71, 202 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1266, -71, 559 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1296, -71, 200 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1316, -71, 470 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1321, -71, 404 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1336, -71, 343 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1341, -71, 438 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1348, -71, 382 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1361, -71, 312 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ICE_TURARA, { 1186, 150, 66 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_ICE_TURARA, { 1346, 150, 110 }, { 0, 0, 0 }, 1 },
    { ACTOR_BG_ICE_TURARA, { 1026, 150, 296 }, { 0, -21844, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { 1058, 150, 369 }, { 0, -20024, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { 1091, 150, 274 }, { 0, -16384, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { 1148, 150, 347 }, { 0, -18203, 0 }, 2 },
    { ACTOR_BG_ICE_TURARA, { 1251, 150, 356 }, { 0, -20024, 0 }, 2 },
    { ACTOR_EN_ICE_HONO, { 1493, -22, 413 }, { 0, 0, 0 }, -1 },
    { ACTOR_BG_ICE_SHELTER, { 1201, -72, 643 }, { 0, -16384, 0 }, 14 },
    { ACTOR_BG_ICE_SHELTER, { 1261, -71, 68 }, { 0, 0, 0 }, 15 },
    { ACTOR_EN_BOX, { 1201, -71, 649 }, { 0, 0, 0 }, 2049 },
};

static const Oot3dRoomObjectEntry oot3d_ice_doukutu_info_ice_doukutu_11_info_objects[] = {
    { OBJECT_ICE_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FZ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_FLASH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TRAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FIREFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SUTARU, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_ice_doukutu_info_room_refs[] = {
    { "ice_doukutu_0_info.zsi", 0 },
    { "ice_doukutu_1_info.zsi", 1 },
    { "ice_doukutu_2_info.zsi", 2 },
    { "ice_doukutu_3_info.zsi", 3 },
    { "ice_doukutu_4_info.zsi", 4 },
    { "ice_doukutu_5_info.zsi", 5 },
    { "ice_doukutu_6_info.zsi", 6 },
};

static const Oot3dSceneSetupIndex oot3d_ice_doukutu_info_setups[] = {
    { 0u, oot3d_ice_doukutu_info_setup_0_commands, 12u, oot3d_ice_doukutu_info_setup_0_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_ice_doukutu_info_setup_0_spawns, 1u, oot3d_ice_doukutu_info_setup_0_entrances, 2u, oot3d_ice_doukutu_info_setup_0_transition_actors, 12u, oot3d_ice_doukutu_info_setup_0_light_settings, 6u, oot3d_ice_doukutu_info_setup_0_exits, 2u, oot3d_ice_doukutu_info_setup_0_skybox_settings, 1u, oot3d_ice_doukutu_info_setup_0_sound_settings, 1u, NULL, 0u, oot3d_ice_doukutu_info_setup_0_misc_settings, 1u },
    { 1u, oot3d_ice_doukutu_info_setup_1_commands, 13u, oot3d_ice_doukutu_info_setup_1_special_files, 1u, NULL, 0u, NULL, 0u, oot3d_ice_doukutu_info_setup_1_spawns, 1u, oot3d_ice_doukutu_info_setup_1_entrances, 1u, oot3d_ice_doukutu_info_setup_1_transition_actors, 12u, oot3d_ice_doukutu_info_setup_1_light_settings, 4u, oot3d_ice_doukutu_info_setup_1_exits, 2u, oot3d_ice_doukutu_info_setup_1_skybox_settings, 1u, oot3d_ice_doukutu_info_setup_1_sound_settings, 1u, oot3d_ice_doukutu_info_setup_1_cutscenes, 1u, oot3d_ice_doukutu_info_setup_1_misc_settings, 1u },
};

static const Oot3dRoomIndex oot3d_ice_doukutu_info_rooms[] = {
    { "ice_doukutu_0_dd_info.zsi", 0, oot3d_ice_doukutu_info_ice_doukutu_0_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_0_dd_info_actors, 6u },
    { "ice_doukutu_0_info.zsi", 0, oot3d_ice_doukutu_info_ice_doukutu_0_info_objects, 3u, oot3d_ice_doukutu_info_ice_doukutu_0_info_actors, 6u },
    { "ice_doukutu_1_dd_info.zsi", 1, oot3d_ice_doukutu_info_ice_doukutu_1_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_1_dd_info_actors, 7u },
    { "ice_doukutu_1_info.zsi", 1, oot3d_ice_doukutu_info_ice_doukutu_1_info_objects, 5u, oot3d_ice_doukutu_info_ice_doukutu_1_info_actors, 14u },
    { "ice_doukutu_2_dd_info.zsi", 2, oot3d_ice_doukutu_info_ice_doukutu_2_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_2_dd_info_actors, 6u },
    { "ice_doukutu_2_info.zsi", 2, oot3d_ice_doukutu_info_ice_doukutu_2_info_objects, 3u, oot3d_ice_doukutu_info_ice_doukutu_2_info_actors, 7u },
    { "ice_doukutu_3_dd_info.zsi", 3, oot3d_ice_doukutu_info_ice_doukutu_3_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_3_dd_info_actors, 19u },
    { "ice_doukutu_3_info.zsi", 3, oot3d_ice_doukutu_info_ice_doukutu_3_info_objects, 3u, oot3d_ice_doukutu_info_ice_doukutu_3_info_actors, 5u },
    { "ice_doukutu_4_dd_info.zsi", 4, oot3d_ice_doukutu_info_ice_doukutu_4_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_4_dd_info_actors, 5u },
    { "ice_doukutu_4_info.zsi", 4, oot3d_ice_doukutu_info_ice_doukutu_4_info_objects, 7u, oot3d_ice_doukutu_info_ice_doukutu_4_info_actors, 8u },
    { "ice_doukutu_5_dd_info.zsi", 5, oot3d_ice_doukutu_info_ice_doukutu_5_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_5_dd_info_actors, 9u },
    { "ice_doukutu_5_info.zsi", 5, oot3d_ice_doukutu_info_ice_doukutu_5_info_objects, 7u, oot3d_ice_doukutu_info_ice_doukutu_5_info_actors, 20u },
    { "ice_doukutu_6_dd_info.zsi", 6, oot3d_ice_doukutu_info_ice_doukutu_6_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_6_dd_info_actors, 7u },
    { "ice_doukutu_6_info.zsi", 6, oot3d_ice_doukutu_info_ice_doukutu_6_info_objects, 8u, oot3d_ice_doukutu_info_ice_doukutu_6_info_actors, 19u },
    { "ice_doukutu_7_dd_info.zsi", 7, oot3d_ice_doukutu_info_ice_doukutu_7_dd_info_objects, 4u, oot3d_ice_doukutu_info_ice_doukutu_7_dd_info_actors, 3u },
    { "ice_doukutu_7_info.zsi", 7, oot3d_ice_doukutu_info_ice_doukutu_7_info_objects, 5u, oot3d_ice_doukutu_info_ice_doukutu_7_info_actors, 2u },
    { "ice_doukutu_8_dd_info.zsi", 8, oot3d_ice_doukutu_info_ice_doukutu_8_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_8_dd_info_actors, 6u },
    { "ice_doukutu_8_info.zsi", 8, oot3d_ice_doukutu_info_ice_doukutu_8_info_objects, 3u, oot3d_ice_doukutu_info_ice_doukutu_8_info_actors, 11u },
    { "ice_doukutu_9_dd_info.zsi", 9, oot3d_ice_doukutu_info_ice_doukutu_9_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_9_dd_info_actors, 16u },
    { "ice_doukutu_9_info.zsi", 9, oot3d_ice_doukutu_info_ice_doukutu_9_info_objects, 9u, oot3d_ice_doukutu_info_ice_doukutu_9_info_actors, 3u },
    { "ice_doukutu_10_dd_info.zsi", 10, NULL, 0u, NULL, 0u },
    { "ice_doukutu_10_info.zsi", 10, NULL, 0u, NULL, 0u },
    { "ice_doukutu_11_dd_info.zsi", 11, oot3d_ice_doukutu_info_ice_doukutu_11_dd_info_objects, 13u, oot3d_ice_doukutu_info_ice_doukutu_11_dd_info_actors, 16u },
    { "ice_doukutu_11_info.zsi", 11, oot3d_ice_doukutu_info_ice_doukutu_11_info_objects, 8u, oot3d_ice_doukutu_info_ice_doukutu_11_info_actors, 29u },
};

const Oot3dSceneIndex oot3d_scene_index_ice_doukutu_info = {
    "ice_doukutu_info.zsi",
    oot3d_ice_doukutu_info_room_refs, 7u,
    oot3d_ice_doukutu_info_rooms, 24u,
    oot3d_ice_doukutu_info_setups, 2u,
};
