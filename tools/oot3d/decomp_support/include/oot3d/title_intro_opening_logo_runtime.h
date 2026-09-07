#ifndef OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_H
#define OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_H

#include "oot3d/title_intro_opening_actor_runtime.h"
#include "oot3d/title_intro_opening_orchestration.h"
#include "oot3d/title_intro_source_table.h"
#include "oot3d/types.h"

#define OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NO_INDEX 0xFFFFu
#define OOT3D_TITLE_INTRO_OPENING_LOGO_COMPONENT_CAPACITY 3u
#define OOT3D_TITLE_INTRO_OPENING_LOGO_DRAW_CAPACITY 3u

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_OK = 0,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_OUTPUT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_STATE,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_MISSING_ORCHESTRATION,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_INDEX_OUT_OF_RANGE,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NO_MATCHING_STEP,
} Oot3dTitleIntroOpeningLogoRuntimeStatus;

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_DEFAULTS = 0,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_FORCE_RISING_BUTTON,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ENV3_TO_FADE_IN,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_BIND_MAIN_CSAB,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_COUNTDOWN_TO_NEXT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_MAIN_ALPHA_STEP,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_TITLE_TEXT_EFFECT_STEP,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_COPYRIGHT_ALPHA_STEP,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ENV4_TO_FADE_OUT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_BUTTON_TO_TRANSITION_DELAY,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_TRANSITION_DELAY_TO_FADE_OUT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_SUBTRACT_FADEOUT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_SUBTRACT_FADEOUT_TRANSITION_GUARD,
} Oot3dTitleIntroOpeningLogoUpdateOp;

typedef struct Oot3dTitleIntroOpeningLogoUpdateRuntimeRow {
    u16 updateRuntimeIndex;
    u16 orchestrationIndex;
    u16 sourceUpdateIndex;
    u16 op;
    u16 state;
    u16 substate;
    u16 nextState;
    u16 nextSubstate;
    u16 timerFieldOffset;
    s16 timerInitialValue;
    u16 flagId;
    s16 copyrightAlphaStepDefault;
    s16 fadeOutAlphaStepDefault;
    s16 transitionCopyrightAlphaStep;
    s16 transitionFadeOutAlphaStep;
    float literalValue;
    float minAlpha;
    float maxAlpha;
    float mainAlphaStep;
    float titleTextEffectAlphaStep;
    float copyrightAlphaClamp;
    const char* phaseRole;
    const char* alphaFieldOffsets;
    const char* alphaOperation;
    const char* nativeBasis;
} Oot3dTitleIntroOpeningLogoUpdateRuntimeRow;

typedef struct Oot3dTitleIntroOpeningLogoState {
    u16 state;
    u16 substate;
    s16 delayTimer;
    s16 timer;
    u16 pendingTransition;
    s16 copyrightAlphaStep;
    s16 fadeOutAlphaStep;
    u16 selectedMainCsabIndex;
    u8 mainCsabBound;
    u8 titleAnimStateSet;
    u8 transitionRequested;
    float titleTextAlpha;
    float mainLogoAlpha;
    float copyrightAlpha;
    float effectAlpha;
} Oot3dTitleIntroOpeningLogoState;

typedef struct Oot3dTitleIntroOpeningLogoStepInput {
    u8 envFlag3;
    u8 envFlag4;
    u8 buttonPressed;
} Oot3dTitleIntroOpeningLogoStepInput;

typedef struct Oot3dTitleIntroOpeningLogoStepResult {
    u8 applied;
    u8 delayTimerDecremented;
    u8 stateChanged;
    u8 boundMainCsab;
    u8 titleAnimStateSet;
    u8 transitionRequested;
    u16 updateRuntimeIndex;
    u16 sourceUpdateIndex;
    const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* runtimeRow;
    const Oot3dTitleIntroLogoUpdateRow* sourceUpdateRow;
} Oot3dTitleIntroOpeningLogoStepResult;

typedef struct Oot3dTitleIntroOpeningLogoDrawComponentState {
    u16 drawIndex;
    u16 componentIndex;
    u16 handleFieldOffset;
    u16 alphaFieldOffset;
    u16 effectAlphaFieldOffset;
    u8 visible;
    float alpha;
    float alphaNormalized;
    float effectAlpha;
    float effectAlphaNormalized;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
    float matrix[16];
    const Oot3dTitleIntroLogoDrawRow* sourceDrawRow;
    const Oot3dTitleIntroLogoComponentRow* sourceComponentRow;
} Oot3dTitleIntroOpeningLogoDrawComponentState;

typedef struct Oot3dTitleIntroOpeningLogoDrawSnapshot {
    u16 orchestrationIndex;
    u16 drawCount;
    Oot3dTitleIntroOpeningLogoDrawComponentState draws[OOT3D_TITLE_INTRO_OPENING_LOGO_DRAW_CAPACITY];
} Oot3dTitleIntroOpeningLogoDrawSnapshot;

extern const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow gOot3dTitleIntroOpeningLogoUpdateRuntimeRows[];
extern const u32 gOot3dTitleIntroOpeningLogoUpdateRuntimeRowCount;

const Oot3dTitleIntroActorInitSourceRow* Oot3d_TitleIntroOpeningLogoRuntimeGetActorInitSourceRow(u16 orchestrationIndex);
const Oot3dTitleIntroLogoComponentRow* Oot3d_TitleIntroOpeningLogoRuntimeGetComponentSourceRow(u16 orchestrationIndex, u16 localComponentIndex);
const Oot3dTitleIntroLogoDrawRow* Oot3d_TitleIntroOpeningLogoRuntimeGetDrawSourceRow(u16 orchestrationIndex, u16 localDrawIndex);
const Oot3dTitleIntroLogoUpdateRow* Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateSourceRow(u16 orchestrationIndex, u16 localUpdateIndex);
const Oot3dTitleIntroLogoDrawContextRow* Oot3d_TitleIntroOpeningLogoRuntimeGetDrawContextSourceRow(u16 contextIndex);
const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(u16 updateRuntimeIndex);
u8 Oot3d_TitleIntroOpeningLogoRuntimeIsTitleLogoBinding(
    const Oot3dTitleIntroOpeningActorBindingRow* binding
);
const char* Oot3d_TitleIntroOpeningLogoRuntimeGetActorBindingStatus(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    const Oot3dTitleIntroOpeningActorBindingRow* binding
);
const char* Oot3d_TitleIntroOpeningLogoRuntimeGetDecodeStatus(
    const Oot3dTitleIntroActorInitSourceRow* actorInitRow,
    u16 componentRefCount
);
const char* Oot3d_TitleIntroOpeningLogoRuntimeComponentSourceRowMissingStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeComponentCmbNameMissingStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeComponentArchiveMissingStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeComponentLoadedFromNativeZarStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeLoadedFromOpeningActorBindingStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeComponentAssetsIncompleteStatus(void);
u8 Oot3d_TitleIntroOpeningLogoRuntimeIsUsVariant(const char* variant);
u8 Oot3d_TitleIntroOpeningLogoRuntimeIsDrawRouteDecoded(u16 orchestrationIndex);
u8 Oot3d_TitleIntroOpeningLogoRuntimeIsDrawContextDecoded(void);
u8 Oot3d_TitleIntroOpeningLogoRuntimeIsUpdateRouteDecoded(u16 orchestrationIndex);
const char* Oot3d_TitleIntroOpeningLogoRuntimeGetDrawContextStatus(u8 decoded);
const char* Oot3d_TitleIntroOpeningLogoRuntimeGetDrawStatus(u8 drawRouteDecoded, u8 drawContextDecoded);
const char* Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateStatus(u8 updateRouteDecoded);
const char* Oot3d_TitleIntroOpeningLogoRuntimeUpdateRouteNotDecodedStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeInitFailedStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeStepFailedStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDiagnosticEnv3ScheduleSource(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDiagnosticAlphaStateStatus(u8 displayReached);
const char* Oot3d_TitleIntroOpeningLogoRuntimeOpeningFrameEnv3ScheduleSource(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeOpeningFrameUsedForRenderStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderNotProcessedStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderStepNotValidStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderComponentsNotLoadedStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderRouteOrContextNotDecodedStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderSnapshotNotOkStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderProjectionBridgeFailedStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderUnresolvedComponentRowsStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderAllComponentsInvisibleStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderBoundVisibleComponentsStatus(void);
const char* Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderPartiallyBoundStatus(void);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeInitDefault(Oot3dTitleIntroOpeningLogoState* state);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeInitForceRisingButton(Oot3dTitleIntroOpeningLogoState* state);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeApplyUpdateRow(
    Oot3dTitleIntroOpeningLogoState* state,
    const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* row,
    Oot3dTitleIntroOpeningLogoStepResult* outResult
);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeStep(
    Oot3dTitleIntroOpeningLogoState* state,
    const Oot3dTitleIntroOpeningLogoStepInput* input,
    Oot3dTitleIntroOpeningLogoStepResult* outResult
);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeBuildDrawSnapshot(
    u16 orchestrationIndex,
    const Oot3dTitleIntroOpeningLogoState* state,
    Oot3dTitleIntroOpeningLogoDrawSnapshot* outSnapshot
);

#endif
