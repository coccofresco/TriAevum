#ifndef OOT3D_TITLE_INTRO_PLAYER_RESOURCE_VISIBILITY_H
#define OOT3D_TITLE_INTRO_PLAYER_RESOURCE_VISIBILITY_H

#include "oot3d/types.h"

#define OOT3D_TITLE_INTRO_PLAYER_RESOURCE_VISIBILITY_MAX_IDS 8u

typedef struct Oot3dTitleIntroPlayerResourceVisibilityRow {
    u8 valid;
    u8 ageIndex;
    u8 initialActionIndex;
    u8 modelGroup;
    u8 swordValue;
    u8 shieldValue;
    u8 activeResourceIdCount;
    u8 reserved;
    u16 debugSaveEquipmentWord;
    u8 activeResourceIds[OOT3D_TITLE_INTRO_PLAYER_RESOURCE_VISIBILITY_MAX_IDS];
    const char* source;
} Oot3dTitleIntroPlayerResourceVisibilityRow;

extern const Oot3dTitleIntroPlayerResourceVisibilityRow gOot3dTitleIntroPlayerResourceVisibilityRow;

u32 Oot3d_TitleIntroPlayerBuildResourceVisibility(
    const Oot3dTitleIntroPlayerResourceVisibilityRow* row,
    u8* resourceVisibility,
    u32 resourceCount);

#endif
