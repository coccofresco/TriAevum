#include "oot3d_ui/ui_gameplay_hud_composite_split.h"

namespace oot3d::ui {

namespace {

constexpr std::array<UiGameplayHudCompositeFunctionDescriptor,
                     kOot3dGameplayHudCompositeFunctionCount> kFunctions{{
    {0x00457318, 488, "Map_Init", 0, 1, 0, 1, 0, "Expose minimap projection initialization to the selected HUD backend.", "Preserve scene and room indices, compass parameters, native storage, and room-data initialization."},
    {0x004596D0, 692, "MagicMeter_DrawOverlay", 1, 12, 9, 3, 0, "Move the native magic-meter presentation dependencies and replaceable meter behavior together.", "Preserve magic-fill and HUD-visibility state transitions in their observed order."},
    {0x0045ACCC, 2472, "Interface_Update", 13, 25, 5, 20, 0, "Route alpha and health mechanics through the selected backend after the child composites are split.", "Preserve gameplay gates, environmental warnings, transition and audio actions, and the native frame order."},
    {0x00470DF0, 640, "Map_Update", 38, 2, 0, 2, 0, "Expose current minimap floor and room presentation to the selected backend.", "Preserve pause gating, visited-map progress, and player map-state mutation."},
    {0x00475CA8, 4236, "Interface_UpdateButtonRestrictions", 40, 7, 0, 7, 0, "Expose six-lane button availability and restriction mechanics to the selected backend.", "Preserve switch and runtime gates plus native environmental-hazard queries and warnings."},
    {0x00476E1C, 1528, "Magic_Update", 47, 14, 0, 8, 6, "Expose the eleven-state magic mechanic and meter-border state to the selected backend.", "Preserve Lens hazard and gameplay gates, native audio, and state-machine order."},
}};

constexpr std::array<UiGameplayHudCompositeSeamDescriptor,
                     kOot3dGameplayHudCompositeSeamCount> kSeams{{
    {0x00457458, 0xEAFB372D, "AL", UiGameplayHudInvocation::DirectTailCall, 0, 0x00457318, "Map_Init", 0x00325114, "Map_InitRoomData", UiGameplayHudCompositeDisposition::RetainStateMutation, UiGameplayHudCompositeLane::HudMinimap, "Initializes native room-map data and storage.", "Retain native room-data initialization before backend projection."},
    {0x0045977C, 0xEBFC388C, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x004596D0, "MagicMeter_DrawOverlay", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudCompositeDisposition::ReplaceablePresentation, UiGameplayHudCompositeLane::PresentationRuntime, "Acquires the native static renderer initialization guard.", "Omit only together with replacement of the guarded native presentation."},
    {0x00459790, 0xEBFC383D, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x004596D0, "MagicMeter_DrawOverlay", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudCompositeDisposition::ReplaceablePresentation, UiGameplayHudCompositeLane::PresentationRuntime, "Initializes renderer-global state used by the native magic overlay.", "Omit only together with replacement of the dependent native presentation."},
    {0x004597BC, 0xEBFB6889, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x004596D0, "MagicMeter_DrawOverlay", 0x003339E8, "EnvironmentRenderer_SubmitViewportConfig", UiGameplayHudCompositeDisposition::ReplaceablePresentation, UiGameplayHudCompositeLane::PresentationRuntime, "Submits the native viewport configuration used by the magic overlay.", "Omit only when the replacement backend owns the corresponding presentation."},
    {0x00459828, 0xEBFC3861, "AL", UiGameplayHudInvocation::DirectCall, 3, 0x004596D0, "MagicMeter_DrawOverlay", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudCompositeDisposition::ReplaceablePresentation, UiGameplayHudCompositeLane::PresentationRuntime, "Acquires the native static renderer initialization guard.", "Omit only together with replacement of the guarded native presentation."},
    {0x0045983C, 0xEBFC3812, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x004596D0, "MagicMeter_DrawOverlay", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudCompositeDisposition::ReplaceablePresentation, UiGameplayHudCompositeLane::PresentationRuntime, "Initializes renderer-global state used by the native magic overlay.", "Omit only together with replacement of the dependent native presentation."},
    {0x00459868, 0xEBFB685E, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x004596D0, "MagicMeter_DrawOverlay", 0x003339E8, "EnvironmentRenderer_SubmitViewportConfig", UiGameplayHudCompositeDisposition::ReplaceablePresentation, UiGameplayHudCompositeLane::PresentationRuntime, "Submits the native viewport configuration used by the magic overlay.", "Omit only when the replacement backend owns the corresponding presentation."},
    {0x00459884, 0xEBFBC95E, "AL", UiGameplayHudInvocation::DirectCall, 6, 0x004596D0, "MagicMeter_DrawOverlay", 0x0034BE04, "Interface_ChangeHudVisibilityMode", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::HudVisibility, "Commits a native HUD visibility-mode transition.", "Keep the native visibility action even when presentation is replaced."},
    {0x004598AC, 0xEBFBE839, "AL", UiGameplayHudInvocation::DirectCall, 7, 0x004596D0, "MagicMeter_DrawOverlay", 0x00353998, "Magic_Fill", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::HudMagic, "Commits the native magic-fill state transition.", "Keep the native magic action before backend content projection."},
    {0x004598F8, 0xEBFC382D, "AL", UiGameplayHudInvocation::DirectCall, 8, 0x004596D0, "MagicMeter_DrawOverlay", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudCompositeDisposition::ReplaceablePresentation, UiGameplayHudCompositeLane::PresentationRuntime, "Acquires the native static renderer initialization guard.", "Omit only together with replacement of the guarded native presentation."},
    {0x0045990C, 0xEBFC37DE, "AL", UiGameplayHudInvocation::DirectCall, 9, 0x004596D0, "MagicMeter_DrawOverlay", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudCompositeDisposition::ReplaceablePresentation, UiGameplayHudCompositeLane::PresentationRuntime, "Initializes renderer-global state used by the native magic overlay.", "Omit only together with replacement of the dependent native presentation."},
    {0x00459938, 0xEBFB682A, "AL", UiGameplayHudInvocation::DirectCall, 10, 0x004596D0, "MagicMeter_DrawOverlay", 0x003339E8, "EnvironmentRenderer_SubmitViewportConfig", UiGameplayHudCompositeDisposition::ReplaceablePresentation, UiGameplayHudCompositeLane::PresentationRuntime, "Submits the native viewport configuration used by the magic overlay.", "Omit only when the replacement backend owns the corresponding presentation."},
    {0x00459964, 0xEBFBC926, "AL", UiGameplayHudInvocation::DirectCall, 11, 0x004596D0, "MagicMeter_DrawOverlay", 0x0034BE04, "Interface_ChangeHudVisibilityMode", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::HudVisibility, "Commits a native HUD visibility-mode transition.", "Keep the native visibility action even when presentation is replaced."},
    {0x0045ACF8, 0xEBFC3A3E, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x0045ACCC, "Interface_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native pause-context state.", "Read native state before the dependent HUD branch."},
    {0x0045AD48, 0xEBFC2E7E, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x0045ACCC, "Interface_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native runtime audio and transition gate.", "Read native state before the dependent HUD branch."},
    {0x0045AD5C, 0xEBFC2E79, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x0045ACCC, "Interface_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native runtime audio and transition gate.", "Read native state before the dependent HUD branch."},
    {0x0045AD88, 0x0B006BC6, "EQ", UiGameplayHudInvocation::DirectCall, 3, 0x0045ACCC, "Interface_Update", 0x00475CA8, "Interface_UpdateButtonRestrictions", UiGameplayHudCompositeDisposition::SplitComposite, UiGameplayHudCompositeLane::HudButtons, "Hands off to the mixed native button-restriction update.", "Apply the button-restriction split contract before replacing its mechanics."},
    {0x0045AE24, 0xEBF9F37F, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x0045ACCC, "Interface_Update", 0x002D7C28, "Interface_UpdateHudAlphas", UiGameplayHudCompositeDisposition::ReplaceableMechanic, UiGameplayHudCompositeLane::HudAlpha, "Updates native HUD alpha for all fourteen visibility modes.", "Provide whole-body alpha parity in the selected HUD backend."},
    {0x0045AE30, 0xEB0057EE, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x0045ACCC, "Interface_Update", 0x00470DF0, "Map_Update", UiGameplayHudCompositeDisposition::SplitComposite, UiGameplayHudCompositeLane::HudMinimap, "Hands off to the mixed native minimap update.", "Apply the Map_Update split contract before replacing its presentation."},
    {0x0045AE80, 0xEBFC697D, "AL", UiGameplayHudInvocation::DirectCall, 6, 0x0045ACCC, "Interface_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::Audio, "Emits native HUD or magic-meter audio feedback.", "Keep the native audio side effect in its observed state-machine position."},
    {0x0045AE9C, 0xEB0059C7, "AL", UiGameplayHudInvocation::DirectCall, 7, 0x0045ACCC, "Interface_Update", 0x004715C0, "Health_UpdateBeatingHeart", UiGameplayHudCompositeDisposition::ReplaceableMechanic, UiGameplayHudCompositeLane::HudHealth, "Updates the beating-heart oscillator and low-health state.", "Provide whole-body parity in the selected HUD backend."},
    {0x0045AEA8, 0xEBF9F2F2, "AL", UiGameplayHudInvocation::DirectCall, 8, 0x0045ACCC, "Interface_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::GameplayHazard, "Classifies the current environmental hazard and may start its one-shot native warning.", "Keep the native hazard result and warning side effect in their observed order."},
    {0x0045AF0C, 0xEBF9F345, "AL", UiGameplayHudInvocation::DirectCall, 9, 0x0045ACCC, "Interface_Update", 0x002D7C28, "Interface_UpdateHudAlphas", UiGameplayHudCompositeDisposition::ReplaceableMechanic, UiGameplayHudCompositeLane::HudAlpha, "Updates native HUD alpha for all fourteen visibility modes.", "Provide whole-body alpha parity in the selected HUD backend."},
    {0x0045AF5C, 0xEBF9F284, "AL", UiGameplayHudInvocation::DirectCall, 10, 0x0045ACCC, "Interface_Update", 0x002D7974, "Interface_RaiseButtonAlphas", UiGameplayHudCompositeDisposition::ReplaceableMechanic, UiGameplayHudCompositeLane::HudAlpha, "Raises the six native OoT3D button-alpha lanes.", "Provide whole-body parity in the selected HUD backend."},
    {0x0045AFCC, 0xEBF9F2A9, "AL", UiGameplayHudInvocation::DirectCall, 11, 0x0045ACCC, "Interface_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::GameplayHazard, "Classifies the current environmental hazard and may start its one-shot native warning.", "Keep the native hazard result and warning side effect in their observed order."},
    {0x0045AFE0, 0xEBF9F2A4, "AL", UiGameplayHudInvocation::DirectCall, 12, 0x0045ACCC, "Interface_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::GameplayHazard, "Classifies the current environmental hazard and may start its one-shot native warning.", "Keep the native hazard result and warning side effect in their observed order."},
    {0x0045B020, 0xEB007E46, "AL", UiGameplayHudInvocation::DirectCall, 13, 0x0045ACCC, "Interface_Update", 0x0047A940, "Health_UpdateMeter", UiGameplayHudCompositeDisposition::ReplaceableMechanic, UiGameplayHudCompositeLane::HudHealth, "Updates native heart-color oscillator tables.", "Provide whole-body parity in the selected HUD backend."},
    {0x0045B040, 0xEBFC396C, "AL", UiGameplayHudInvocation::DirectCall, 14, 0x0045ACCC, "Interface_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native pause-context state.", "Read native state before the dependent HUD branch."},
    {0x0045B054, 0xEBFC2DBB, "AL", UiGameplayHudInvocation::DirectCall, 15, 0x0045ACCC, "Interface_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native runtime audio and transition gate.", "Read native state before the dependent HUD branch."},
    {0x0045B094, 0xEBFC69B8, "AL", UiGameplayHudInvocation::DirectCall, 16, 0x0045ACCC, "Interface_Update", 0x0037577C, "Gameplay_InCsMode", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Tests the native gameplay cutscene mode.", "Read native state before the dependent HUD branch."},
    {0x0045B14C, 0xEBFC68CA, "AL", UiGameplayHudInvocation::DirectCall, 17, 0x0045ACCC, "Interface_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::Audio, "Emits native HUD or magic-meter audio feedback.", "Keep the native audio side effect in its observed state-machine position."},
    {0x0045B1E4, 0xEBFC3903, "AL", UiGameplayHudInvocation::DirectCall, 18, 0x0045ACCC, "Interface_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native pause-context state.", "Read native state before the dependent HUD branch."},
    {0x0045B1F8, 0xEBFC2D52, "AL", UiGameplayHudInvocation::DirectCall, 19, 0x0045ACCC, "Interface_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native runtime audio and transition gate.", "Read native state before the dependent HUD branch."},
    {0x0045B230, 0xEBFC6939, "AL", UiGameplayHudInvocation::DirectCall, 20, 0x0045ACCC, "Interface_Update", 0x0037571C, "oot3d_get_flag_22a0", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native gameplay flag at field 0x22A0.", "Read native state before the dependent HUD branch."},
    {0x0045B244, 0xEBFC3D55, "AL", UiGameplayHudInvocation::DirectCall, 21, 0x0045ACCC, "Interface_Update", 0x0036A7A0, "Player_InCsMode", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Tests the native Player cutscene mode.", "Read native state before the dependent HUD branch."},
    {0x0045B284, 0xEB006EE4, "AL", UiGameplayHudInvocation::DirectCall, 22, 0x0045ACCC, "Interface_Update", 0x00476E1C, "Magic_Update", UiGameplayHudCompositeDisposition::SplitComposite, UiGameplayHudCompositeLane::HudMagic, "Hands off to the mixed native Magic_Update state machine.", "Apply the magic-state split contract before replacing its mechanics or border presentation."},
    {0x0045B684, 0xEBFB6497, "AL", UiGameplayHudInvocation::DirectCall, 23, 0x0045ACCC, "Interface_Update", 0x003348E8, "PlayState_ClaimTransitionSlot", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::GameplayState, "Claims the native gameplay transition slot.", "Keep the native transition-state mutation in its observed order."},
    {0x0045B6BC, 0xEBFA2EE1, "AL", UiGameplayHudInvocation::DirectCall, 24, 0x0045ACCC, "Interface_Update", 0x002E7248, "AudioScene_SetBgmAndBroadcastTransition", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::Audio, "Commits the native BGM and broadcast transition.", "Keep the native audio transition in the observed frame order."},
    {0x00470E10, 0xEBFBE1F8, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x00470DF0, "Map_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native pause-context state.", "Read native state before the dependent HUD branch."},
    {0x00471044, 0xEBF97D17, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x00470DF0, "Map_Update", 0x002D04A8, "Map_SetPlayerInitialInfo", UiGameplayHudCompositeDisposition::RetainStateMutation, UiGameplayHudCompositeLane::HudMinimap, "Commits the native initial player map information.", "Retain the original map-state mutation before projecting minimap content."},
    {0x00475D5C, 0xEBFBE2C0, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x0036E864, "Flags_GetSwitch", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads a native scene switch used by button restrictions.", "Read native switch state before deriving button availability."},
    {0x00475DCC, 0xEBFBE2A4, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x0036E864, "Flags_GetSwitch", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads a native scene switch used by button restrictions.", "Read native switch state before deriving button availability."},
    {0x00475EC8, 0xEBFBE265, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x0036E864, "Flags_GetSwitch", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads a native scene switch used by button restrictions.", "Read native switch state before deriving button availability."},
    {0x0047601C, 0xEBFBC1C9, "AL", UiGameplayHudInvocation::DirectCall, 3, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native runtime audio and transition gate.", "Read native state before the dependent HUD branch."},
    {0x00476030, 0xEBF98690, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::GameplayHazard, "Classifies the current environmental hazard and may start its one-shot native warning.", "Keep the native hazard result and warning side effect in their observed order."},
    {0x00476044, 0xEBF9868B, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::GameplayHazard, "Classifies the current environmental hazard and may start its one-shot native warning.", "Keep the native hazard result and warning side effect in their observed order."},
    {0x00476070, 0xEBF98680, "AL", UiGameplayHudInvocation::DirectCall, 6, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::GameplayHazard, "Classifies the current environmental hazard and may start its one-shot native warning.", "Keep the native hazard result and warning side effect in their observed order."},
    {0x00476F20, 0xEBFBF955, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x00476E1C, "Magic_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::Audio, "Emits native HUD or magic-meter audio feedback.", "Keep the native audio side effect in its observed state-machine position."},
    {0x0047701C, 0xEBFBC75C, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudCompositeDisposition::PureHelper, UiGameplayHudCompositeLane::PureRuntime, "Performs signed division and remainder for magic-border interpolation.", "May remain internal to either behaviorally equivalent implementation."},
    {0x00477038, 0xEBFBC755, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudCompositeDisposition::PureHelper, UiGameplayHudCompositeLane::PureRuntime, "Performs signed division and remainder for magic-border interpolation.", "May remain internal to either behaviorally equivalent implementation."},
    {0x0047705C, 0xEBFBC74C, "AL", UiGameplayHudInvocation::DirectCall, 3, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudCompositeDisposition::PureHelper, UiGameplayHudCompositeLane::PureRuntime, "Performs signed division and remainder for magic-border interpolation.", "May remain internal to either behaviorally equivalent implementation."},
    {0x00477134, 0xEBFBC92F, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x00476E1C, "Magic_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native pause-context state.", "Read native state before the dependent HUD branch."},
    {0x00477148, 0xEBFBBD7E, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x00476E1C, "Magic_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Reads the native runtime audio and transition gate.", "Read native state before the dependent HUD branch."},
    {0x00477180, 0xEBFBF97D, "AL", UiGameplayHudInvocation::DirectCall, 6, 0x00476E1C, "Magic_Update", 0x0037577C, "Gameplay_InCsMode", UiGameplayHudCompositeDisposition::RetainQuery, UiGameplayHudCompositeLane::GameplayGate, "Tests the native gameplay cutscene mode.", "Read native state before the dependent HUD branch."},
    {0x004771A4, 0xEBF98233, "AL", UiGameplayHudInvocation::DirectCall, 7, 0x00476E1C, "Magic_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::GameplayHazard, "Classifies the current environmental hazard and may start its one-shot native warning.", "Keep the native hazard result and warning side effect in their observed order."},
    {0x004771B8, 0xEBF9822E, "AL", UiGameplayHudInvocation::DirectCall, 8, 0x00476E1C, "Magic_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::GameplayHazard, "Classifies the current environmental hazard and may start its one-shot native warning.", "Keep the native hazard result and warning side effect in their observed order."},
    {0x00477224, 0xEBFBF894, "AL", UiGameplayHudInvocation::DirectCall, 9, 0x00476E1C, "Magic_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::Audio, "Emits native HUD or magic-meter audio feedback.", "Keep the native audio side effect in its observed state-machine position."},
    {0x004772DC, 0xEBFBC6AC, "AL", UiGameplayHudInvocation::DirectCall, 10, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudCompositeDisposition::PureHelper, UiGameplayHudCompositeLane::PureRuntime, "Performs signed division and remainder for magic-border interpolation.", "May remain internal to either behaviorally equivalent implementation."},
    {0x004772F8, 0xEBFBC6A5, "AL", UiGameplayHudInvocation::DirectCall, 11, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudCompositeDisposition::PureHelper, UiGameplayHudCompositeLane::PureRuntime, "Performs signed division and remainder for magic-border interpolation.", "May remain internal to either behaviorally equivalent implementation."},
    {0x0047731C, 0xEBFBC69C, "AL", UiGameplayHudInvocation::DirectCall, 12, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudCompositeDisposition::PureHelper, UiGameplayHudCompositeLane::PureRuntime, "Performs signed division and remainder for magic-border interpolation.", "May remain internal to either behaviorally equivalent implementation."},
    {0x004773F4, 0xEBFBF820, "AL", UiGameplayHudInvocation::DirectCall, 13, 0x00476E1C, "Magic_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudCompositeDisposition::RetainAction, UiGameplayHudCompositeLane::Audio, "Emits native HUD or magic-meter audio feedback.", "Keep the native audio side effect in its observed state-machine position."},
}};

} // namespace

const std::array<UiGameplayHudCompositeFunctionDescriptor,
                 kOot3dGameplayHudCompositeFunctionCount>&
Oot3dGameplayHudCompositeFunctions() noexcept {
    return kFunctions;
}

const UiGameplayHudCompositeFunctionDescriptor* Oot3dGameplayHudCompositeFunction(
    std::uint32_t entry) noexcept {
    for (const auto& function : kFunctions) {
        if (function.entry == entry) {
            return &function;
        }
    }
    return nullptr;
}

const std::array<UiGameplayHudCompositeSeamDescriptor,
                 kOot3dGameplayHudCompositeSeamCount>&
Oot3dGameplayHudCompositeSeams() noexcept {
    return kSeams;
}

const UiGameplayHudCompositeSeamDescriptor* Oot3dGameplayHudCompositeSeamAt(
    std::uint32_t call_site) noexcept {
    for (const auto& seam : kSeams) {
        if (seam.call_site == call_site) {
            return &seam;
        }
    }
    return nullptr;
}

const char* UiGameplayHudCompositeDispositionName(
    UiGameplayHudCompositeDisposition disposition) noexcept {
    switch (disposition) {
    case UiGameplayHudCompositeDisposition::ReplaceableMechanic: return "replaceable_mechanic";
    case UiGameplayHudCompositeDisposition::ReplaceablePresentation: return "replaceable_presentation";
    case UiGameplayHudCompositeDisposition::RetainAction: return "retain_action";
    case UiGameplayHudCompositeDisposition::RetainQuery: return "retain_query";
    case UiGameplayHudCompositeDisposition::RetainStateMutation: return "retain_state_mutation";
    case UiGameplayHudCompositeDisposition::SplitComposite: return "split_composite";
    case UiGameplayHudCompositeDisposition::PureHelper: return "pure_helper";
    }
    return "unknown";
}

const char* UiGameplayHudCompositeLaneName(
    UiGameplayHudCompositeLane lane) noexcept {
    switch (lane) {
    case UiGameplayHudCompositeLane::GameplayGate: return "gameplay_gate";
    case UiGameplayHudCompositeLane::GameplayState: return "gameplay_state";
    case UiGameplayHudCompositeLane::GameplayHazard: return "gameplay_hazard";
    case UiGameplayHudCompositeLane::HudAlpha: return "hud_alpha";
    case UiGameplayHudCompositeLane::HudButtons: return "hud_buttons";
    case UiGameplayHudCompositeLane::HudHealth: return "hud_health";
    case UiGameplayHudCompositeLane::HudMinimap: return "hud_minimap";
    case UiGameplayHudCompositeLane::HudMagic: return "hud_magic";
    case UiGameplayHudCompositeLane::HudVisibility: return "hud_visibility";
    case UiGameplayHudCompositeLane::Audio: return "audio";
    case UiGameplayHudCompositeLane::PresentationRuntime: return "presentation_runtime";
    case UiGameplayHudCompositeLane::PureRuntime: return "pure_runtime";
    }
    return "unknown";
}

} // namespace oot3d::ui
