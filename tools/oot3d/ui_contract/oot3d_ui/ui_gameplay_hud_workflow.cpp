#include "oot3d_ui/ui_gameplay_hud_workflow.h"

namespace oot3d::ui {

namespace {

constexpr std::array<UiGameplayHudFunctionDescriptor,
                     kOot3dGameplayHudFunctionCount> kFunctions{{
    {0x002D041C, 136, "Interface_DimButtonAlphas", "Dims six native button-alpha lanes or applies the restriction-aware rising alpha.", UiSemanticMechanic::HudAlpha, UiSeamPhase::Helper, UiGameplayHudNativeRole::InternalMechanicHelper, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::Presentation)},
    {0x002D7974, 256, "Interface_RaiseButtonAlphas", "Raises six native button-alpha lanes while holding disabled buttons at alpha 0x46.", UiSemanticMechanic::HudAlpha, UiSeamPhase::Update, UiGameplayHudNativeRole::ReplaceableMechanic, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::Presentation)},
    {0x002D7A78, 400, "Player_GetEnvironmentalHazard", "Classifies the current environmental hazard and can start its one-shot native warning message.", UiSemanticMechanic::PlayerEnvironmentHazard, UiSeamPhase::Action, UiGameplayHudNativeRole::RetainedActionSink, UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x002D7C28, 1432, "Interface_UpdateHudAlphas", "Dispatches all fourteen native visibility modes across buttons, health, magic, and minimap alpha.", UiSemanticMechanic::HudAlpha, UiSeamPhase::Update, UiGameplayHudNativeRole::ReplaceableMechanic, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::Presentation)},
    {0x002E2E60, 5372, "Gameplay_Update", "Owns the native gameplay-frame order and invokes the magic and interface HUD composites among all gameplay systems.", UiSemanticMechanic::GameplayFrame, UiSeamPhase::Update, UiGameplayHudNativeRole::FrameOwner, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x0034BE04, 40, "Interface_ChangeHudVisibilityMode", "Commits the requested native HUD visibility mode.", UiSemanticMechanic::HudVisibility, UiSeamPhase::Action, UiGameplayHudNativeRole::RetainedActionSink, UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x00353998, 60, "Magic_Fill", "Commits the native magic-fill state transition used by gameplay and the fused 3DS magic overlay.", UiSemanticMechanic::HudMagic, UiSeamPhase::Action, UiGameplayHudNativeRole::RetainedActionSink, UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x0044E0D4, 452, "Gameplay_InitializeViewActorUiAndMapState", "Initializes the gameplay view and ordered HUD, health-meter, and scene-map state.", UiSemanticMechanic::GameplayFrame, UiSeamPhase::Initialize, UiGameplayHudNativeRole::InitializationOwner, UiResponsibilityBit(UiResponsibility::Lifecycle)},
    {0x00457318, 488, "Map_Init", "Initializes scene map indices, compass parameters, room metadata, storage, and native room data.", UiSemanticMechanic::HudMinimap, UiSeamPhase::Initialize, UiGameplayHudNativeRole::RetainedComposite, UiResponsibilityBit(UiResponsibility::Lifecycle) | UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x004596D0, 692, "MagicMeter_DrawOverlay", "Owns native magic-overlay and fill presentation, visibility actions, fade state, and renderer submission.", UiSemanticMechanic::HudMagic, UiSeamPhase::Composite, UiGameplayHudNativeRole::RetainedComposite, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::Presentation) | UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x0045ACCC, 2472, "Interface_Update", "Orders visibility, buttons, minimap, health, rupees, timers, action labels, audio, and transition state.", UiSemanticMechanic::GameplayFrame, UiSeamPhase::Composite, UiGameplayHudNativeRole::RetainedComposite, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x0046345C, 188, "Health_InitMeter", "Initializes health accumulators, oscillators, heart colors, and shared defense-heart color tables.", UiSemanticMechanic::HudHealth, UiSeamPhase::Initialize, UiGameplayHudNativeRole::ReplaceableMechanic, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::Lifecycle)},
    {0x00470DF0, 640, "Map_Update", "Updates floor and room selection, visited-floor progress, compass state, and map transition metadata.", UiSemanticMechanic::HudMinimap, UiSeamPhase::Update, UiGameplayHudNativeRole::RetainedComposite, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x004715C0, 528, "Health_UpdateBeatingHeart", "Advances the beating-heart oscillator and emits the capacity-dependent low-health alarm outside pause and cutscenes.", UiSemanticMechanic::HudHealth, UiSeamPhase::Update, UiGameplayHudNativeRole::ReplaceableMechanic, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::Presentation)},
    {0x00475CA8, 4236, "Interface_UpdateButtonRestrictions", "Reconciles equipped button items and enable states across riding, minigames, hazards, scenes, cutscenes, and transitions.", UiSemanticMechanic::HudButtons, UiSeamPhase::Update, UiGameplayHudNativeRole::RetainedComposite, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x00476E1C, 1528, "Magic_Update", "Dispatches the eleven-state native magic mechanic including capacity, fill, consumption, Lens drain, border flash, and audio.", UiSemanticMechanic::HudMagic, UiSeamPhase::Update, UiGameplayHudNativeRole::RetainedComposite, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::Presentation) | UiResponsibilityBit(UiResponsibility::GameStateAction)},
    {0x0047A940, 760, "Health_UpdateMeter", "Advances the heart-color oscillator and rebuilds normal and double-defense heart color tables.", UiSemanticMechanic::HudHealth, UiSeamPhase::Update, UiGameplayHudNativeRole::ReplaceableMechanic, UiResponsibilityBit(UiResponsibility::Mechanics) | UiResponsibilityBit(UiResponsibility::Presentation)},
}};

constexpr std::array<UiGameplayHudEdgeDescriptor,
                     kOot3dGameplayHudEdgeCount> kEdges{{
    {0x002D0434, 0x1A001D4E, "NE", UiGameplayHudInvocation::DirectTailCall, 0, 0x002D041C, "Interface_DimButtonAlphas", 0x002D7974, "Interface_RaiseButtonAlphas", UiGameplayHudEdgeKind::InternalMechanicHandoff},
    {0x002D7AFC, 0xEB0246BD, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x002D7A78, "Player_GetEnvironmentalHazard", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002D7BEC, 0xEB024022, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x002D7A78, "Player_GetEnvironmentalHazard", 0x00367C7C, "Message_StartTextbox", UiGameplayHudEdgeKind::NativeDependency},
    {0x002D7D68, 0xEBFFE1AB, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x002D7C28, "Interface_UpdateHudAlphas", 0x002D041C, "Interface_DimButtonAlphas", UiGameplayHudEdgeKind::InternalMechanicHandoff},
    {0x002D7E4C, 0xEBFFE172, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x002D7C28, "Interface_UpdateHudAlphas", 0x002D041C, "Interface_DimButtonAlphas", UiGameplayHudEdgeKind::InternalMechanicHandoff},
    {0x002D7E98, 0xEBFFE15F, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x002D7C28, "Interface_UpdateHudAlphas", 0x002D041C, "Interface_DimButtonAlphas", UiGameplayHudEdgeKind::InternalMechanicHandoff},
    {0x002D7F1C, 0xEBFFFE94, "AL", UiGameplayHudInvocation::DirectCall, 3, 0x002D7C28, "Interface_UpdateHudAlphas", 0x002D7974, "Interface_RaiseButtonAlphas", UiGameplayHudEdgeKind::InternalMechanicHandoff},
    {0x002D818C, 0xEBFFE0A2, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x002D7C28, "Interface_UpdateHudAlphas", 0x002D041C, "Interface_DimButtonAlphas", UiGameplayHudEdgeKind::InternalMechanicHandoff},
    {0x002E2EBC, 0xEB0219CD, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x002E2E60, "Gameplay_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E2F44, 0xEB01D3D5, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x002E2E60, "Gameplay_Update", 0x00357EA0, "PlayState_GetField229C", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E2F64, 0xEB0249EC, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x002E2E60, "Gameplay_Update", 0x0037571C, "oot3d_get_flag_22a0", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E2F78, 0xEB00CB71, "AL", UiGameplayHudInvocation::DirectCall, 3, 0x002E2E60, "Gameplay_Update", 0x00315D44, "PlayState_GetField22AC", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E2F90, 0xEB0005A1, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x002E2E60, "Gameplay_Update", 0x002E461C, "ModeState_SetPair", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3068, 0xEB01A365, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x002E2E60, "Gameplay_Update", 0x0034BE04, "Interface_ChangeHudVisibilityMode", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x002E30AC, 0xEB05F770, "AL", UiGameplayHudInvocation::DirectCall, 6, 0x002E2E60, "Gameplay_Update", 0x00460E74, "CollisionAt_IsSentinelUnset", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E30C8, 0xEB05FCD0, "AL", UiGameplayHudInvocation::DirectCall, 7, 0x002E2E60, "Gameplay_Update", 0x00462410, "Gameplay_ResetTransitionAndAudioState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E30EC, 0xEB018063, "AL", UiGameplayHudInvocation::DirectCall, 8, 0x002E2E60, "Gameplay_Update", 0x00343280, "Runtime_Memzero", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3320, 0xEB0004CE, "AL", UiGameplayHudInvocation::DirectCall, 9, 0x002E2E60, "Gameplay_Update", 0x002E4660, "ActorResource_SampleIndexedVec4", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E33A4, 0xEB0004AD, "AL", UiGameplayHudInvocation::DirectCall, 10, 0x002E2E60, "Gameplay_Update", 0x002E4660, "ActorResource_SampleIndexedVec4", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E36A4, 0xEB01382A, "AL", UiGameplayHudInvocation::DirectCall, 11, 0x002E2E60, "Gameplay_Update", 0x00331754, "PauseQuestOptionalPanel_SetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E36C8, 0xEB013821, "AL", UiGameplayHudInvocation::DirectCall, 12, 0x002E2E60, "Gameplay_Update", 0x00331754, "PauseQuestOptionalPanel_SetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E36EC, 0xEB013818, "AL", UiGameplayHudInvocation::DirectCall, 13, 0x002E2E60, "Gameplay_Update", 0x00331754, "PauseQuestOptionalPanel_SetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3710, 0xEB0210A7, "AL", UiGameplayHudInvocation::DirectCall, 14, 0x002E2E60, "Gameplay_Update", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3724, 0xEB021058, "AL", UiGameplayHudInvocation::DirectCall, 15, 0x002E2E60, "Gameplay_Update", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3748, 0xEB0142EF, "AL", UiGameplayHudInvocation::DirectCall, 16, 0x002E2E60, "Gameplay_Update", 0x0033430C, "Gameplay_SetRespawnAndBgmForState3", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E376C, 0xEB0137F8, "AL", UiGameplayHudInvocation::DirectCall, 17, 0x002E2E60, "Gameplay_Update", 0x00331754, "PauseQuestOptionalPanel_SetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3904, 0xEB013792, "AL", UiGameplayHudInvocation::DirectCall, 18, 0x002E2E60, "Gameplay_Update", 0x00331754, "PauseQuestOptionalPanel_SetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3910, 0xEB01378F, "AL", UiGameplayHudInvocation::DirectCall, 19, 0x002E2E60, "Gameplay_Update", 0x00331754, "PauseQuestOptionalPanel_SetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3A10, 0xEB01374F, "AL", UiGameplayHudInvocation::DirectCall, 20, 0x002E2E60, "Gameplay_Update", 0x00331754, "PauseQuestOptionalPanel_SetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3A1C, 0xEB01374C, "AL", UiGameplayHudInvocation::DirectCall, 21, 0x002E2E60, "Gameplay_Update", 0x00331754, "PauseQuestOptionalPanel_SetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3A98, 0xEB024677, "AL", UiGameplayHudInvocation::DirectCall, 22, 0x002E2E60, "Gameplay_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3B38, 0xEB02464F, "AL", UiGameplayHudInvocation::DirectCall, 23, 0x002E2E60, "Gameplay_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3C0C, 0xEB0041F5, "AL", UiGameplayHudInvocation::DirectCall, 24, 0x002E2E60, "Gameplay_Update", 0x002F43E8, "PauseContext_GetInteractionState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3C30, 0xEB020F5F, "AL", UiGameplayHudInvocation::DirectCall, 25, 0x002E2E60, "Gameplay_Update", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3C44, 0xEB020F10, "AL", UiGameplayHudInvocation::DirectCall, 26, 0x002E2E60, "Gameplay_Update", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3C78, 0xEB056163, "AL", UiGameplayHudInvocation::DirectCall, 27, 0x002E2E60, "Gameplay_Update", 0x0043C20C, "MessageContext_GetDisplayClass", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3D88, 0xEB024663, "AL", UiGameplayHudInvocation::DirectCall, 28, 0x002E2E60, "Gameplay_Update", 0x0037571C, "oot3d_get_flag_22a0", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3D9C, 0xEB021A7F, "AL", UiGameplayHudInvocation::DirectCall, 29, 0x002E2E60, "Gameplay_Update", 0x0036A7A0, "Player_InCsMode", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3DAC, 0xEB021611, "AL", UiGameplayHudInvocation::DirectCall, 30, 0x002E2E60, "Gameplay_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3DE4, 0xEB000644, "AL", UiGameplayHudInvocation::DirectCall, 31, 0x002E2E60, "Gameplay_Update", 0x002E56FC, "RequestState_SetValueAndClearStatus", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3DF0, 0xEB00042A, "AL", UiGameplayHudInvocation::DirectCall, 32, 0x002E2E60, "Gameplay_Update", 0x002E4EA0, "Object_UpdateBank", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3E20, 0xEB015557, "AL", UiGameplayHudInvocation::DirectCall, 33, 0x002E2E60, "Gameplay_Update", 0x00339384, "Aeabi_UnsignedDivideMod", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3E40, 0xEB020EDB, "AL", UiGameplayHudInvocation::DirectCall, 34, 0x002E2E60, "Gameplay_Update", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3E50, 0xEB020E8D, "AL", UiGameplayHudInvocation::DirectCall, 35, 0x002E2E60, "Gameplay_Update", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3E70, 0xEB004487, "AL", UiGameplayHudInvocation::DirectCall, 36, 0x002E2E60, "Gameplay_Update", 0x002F5094, "SaveData_UpdateConditionalFlags", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3E98, 0xEB020EC5, "AL", UiGameplayHudInvocation::DirectCall, 37, 0x002E2E60, "Gameplay_Update", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3EAC, 0xEB020E76, "AL", UiGameplayHudInvocation::DirectCall, 38, 0x002E2E60, "Gameplay_Update", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3ECC, 0xEB00C619, "AL", UiGameplayHudInvocation::DirectCall, 39, 0x002E2E60, "Gameplay_Update", 0x00315738, "PlayState_GetField1700", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3EF0, 0xEB020EAF, "AL", UiGameplayHudInvocation::DirectCall, 40, 0x002E2E60, "Gameplay_Update", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3F04, 0xEB020E60, "AL", UiGameplayHudInvocation::DirectCall, 41, 0x002E2E60, "Gameplay_Update", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3F24, 0xEB00C603, "AL", UiGameplayHudInvocation::DirectCall, 42, 0x002E2E60, "Gameplay_Update", 0x00315738, "PlayState_GetField1700", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E3F54, 0xEB00B0D1, "AL", UiGameplayHudInvocation::DirectCall, 43, 0x002E2E60, "Gameplay_Update", 0x003102A0, "GlobalRendererState_IsFloat80PositiveAndIdle", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4004, 0xEB000587, "AL", UiGameplayHudInvocation::DirectCall, 44, 0x002E2E60, "Gameplay_Update", 0x002E5628, "Room_ProcessRoomRequest", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4018, 0xEB05F14A, "AL", UiGameplayHudInvocation::DirectCall, 45, 0x002E2E60, "Gameplay_Update", 0x00460548, "CollisionCheck_AT", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4024, 0xEB05E702, "AL", UiGameplayHudInvocation::DirectCall, 46, 0x002E2E60, "Gameplay_Update", 0x0045DC34, "CollisionCheck_OC", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4030, 0xEB05FA46, "AL", UiGameplayHudInvocation::DirectCall, 47, 0x002E2E60, "Gameplay_Update", 0x00462950, "CollisionCheck_Damage", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E403C, 0xEBFFD0F1, "AL", UiGameplayHudInvocation::DirectCall, 48, 0x002E2E60, "Gameplay_Update", 0x002D8408, "CollisionCheck_ClearContext", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4054, 0x0B05F4BA, "EQ", UiGameplayHudInvocation::DirectCall, 49, 0x002E2E60, "Gameplay_Update", 0x00461344, "Actor_UpdateAll", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4068, 0xEB05D598, "AL", UiGameplayHudInvocation::DirectCall, 50, 0x002E2E60, "Gameplay_Update", 0x004596D0, "MagicMeter_DrawOverlay", UiGameplayHudEdgeKind::RetainedCompositeHandoff},
    {0x002E4074, 0xEB05F285, "AL", UiGameplayHudInvocation::DirectCall, 51, 0x002E2E60, "Gameplay_Update", 0x00460A90, "CutsceneState_UpdateAndDispatchMode", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4080, 0xEB05CC50, "AL", UiGameplayHudInvocation::DirectCall, 52, 0x002E2E60, "Gameplay_Update", 0x004571C8, "EffectContext_UpdateActiveSlots", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E408C, 0xEB05F794, "AL", UiGameplayHudInvocation::DirectCall, 53, 0x002E2E60, "Gameplay_Update", 0x00461EE4, "RenderLightDescriptorList_UpdateAndRecycle", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4094, 0xEB0245A0, "AL", UiGameplayHudInvocation::DirectCall, 54, 0x002E2E60, "Gameplay_Update", 0x0037571C, "oot3d_get_flag_22a0", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E40A8, 0xEB0219BC, "AL", UiGameplayHudInvocation::DirectCall, 55, 0x002E2E60, "Gameplay_Update", 0x0036A7A0, "Player_InCsMode", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E40F8, 0xEB056043, "AL", UiGameplayHudInvocation::DirectCall, 56, 0x002E2E60, "Gameplay_Update", 0x0043C20C, "MessageContext_GetDisplayClass", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E410C, 0xEB05FA70, "AL", UiGameplayHudInvocation::DirectCall, 57, 0x002E2E60, "Gameplay_Update", 0x00462AD4, "LightContext_GetIndexedSignedByte", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4130, 0xEB00756A, "AL", UiGameplayHudInvocation::DirectCall, 58, 0x002E2E60, "Gameplay_Update", 0x003016E0, "PauseContext_SetInteractionState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4178, 0xEB01B5CF, "AL", UiGameplayHudInvocation::DirectCall, 59, 0x002E2E60, "Gameplay_Update", 0x003518BC, "GlobalActionState_GetField38", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4188, 0xEB02151A, "AL", UiGameplayHudInvocation::DirectCall, 60, 0x002E2E60, "Gameplay_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E419C, 0xEB02197F, "AL", UiGameplayHudInvocation::DirectCall, 61, 0x002E2E60, "Gameplay_Update", 0x0036A7A0, "Player_InCsMode", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E41E0, 0xEB0244A5, "AL", UiGameplayHudInvocation::DirectCall, 62, 0x002E2E60, "Gameplay_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4240, 0xEB02448D, "AL", UiGameplayHudInvocation::DirectCall, 63, 0x002E2E60, "Gameplay_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E425C, 0xEB015151, "AL", UiGameplayHudInvocation::DirectCall, 64, 0x002E2E60, "Gameplay_Update", 0x003387A8, "Camera_ChangeDataIdx", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4278, 0xEB01514A, "AL", UiGameplayHudInvocation::DirectCall, 65, 0x002E2E60, "Gameplay_Update", 0x003387A8, "Camera_ChangeDataIdx", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E429C, 0xEB05D207, "AL", UiGameplayHudInvocation::DirectCall, 66, 0x002E2E60, "Gameplay_Update", 0x00458AC0, "GameOver_Update", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E42E0, 0xEB05D05E, "AL", UiGameplayHudInvocation::DirectCall, 67, 0x002E2E60, "Gameplay_Update", 0x00458460, "MessageContext_UpdateStateMachine", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E42E8, 0xEB05DA77, "AL", UiGameplayHudInvocation::DirectCall, 68, 0x002E2E60, "Gameplay_Update", 0x0045ACCC, "Interface_Update", UiGameplayHudEdgeKind::RetainedCompositeHandoff},
    {0x002E42F4, 0xEB00044B, "AL", UiGameplayHudInvocation::DirectCall, 69, 0x002E2E60, "Gameplay_Update", 0x002E5428, "AnimationContext_Update", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4300, 0xEB05F14A, "AL", UiGameplayHudInvocation::DirectCall, 70, 0x002E2E60, "Gameplay_Update", 0x00460830, "AudioSpatialSource_TickUnregisterTimers", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4314, 0xEB05F05E, "AL", UiGameplayHudInvocation::DirectCall, 71, 0x002E2E60, "Gameplay_Update", 0x00460494, "GlobalOscillator_StepByScale", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4328, 0xEB05E2E5, "AL", UiGameplayHudInvocation::DirectCall, 72, 0x002E2E60, "Gameplay_Update", 0x0045CEC4, "FileSelect_StepOverlayAlpha", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E437C, 0xEBFFD050, "AL", UiGameplayHudInvocation::DirectCall, 73, 0x002E2E60, "Gameplay_Update", 0x002D84C4, "Camera_Update", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E43A0, 0xEBFFD047, "AL", UiGameplayHudInvocation::DirectCall, 74, 0x002E2E60, "Gameplay_Update", 0x002D84C4, "Camera_Update", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E43CC, 0xEB05E65F, "AL", UiGameplayHudInvocation::DirectCall, 75, 0x002E2E60, "Gameplay_Update", 0x0045DD50, "Environment_Update", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E43E4, 0xEBFFCFCE, "AL", UiGameplayHudInvocation::DirectCall, 76, 0x002E2E60, "Gameplay_Update", 0x002D8324, "ScaledInteger_StepToward", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E43FC, 0xEBFFCFC8, "AL", UiGameplayHudInvocation::DirectCall, 77, 0x002E2E60, "Gameplay_Update", 0x002D8324, "ScaledInteger_StepToward", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4418, 0xEB022F19, "AL", UiGameplayHudInvocation::DirectCall, 78, 0x002E2E60, "Gameplay_Update", 0x00370084, "Math_SmoothStepToSUpdateRate", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E4424, 0xEBFFCFBB, "AL", UiGameplayHudInvocation::DirectCall, 79, 0x002E2E60, "Gameplay_Update", 0x002D8318, "GlobalRuntimeContext_GetAddress", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E447C, 0xEB05D0BD, "AL", UiGameplayHudInvocation::DirectCall, 80, 0x002E2E60, "Gameplay_Update", 0x00458778, "GlobalFrameMode_SetMaskedValue", UiGameplayHudEdgeKind::NativeDependency},
    {0x002E44E8, 0xEB0243E3, "AL", UiGameplayHudInvocation::DirectCall, 81, 0x002E2E60, "Gameplay_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x0044E0FC, 0xEBFA5E4D, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x0044E0D4, "Gameplay_InitializeViewActorUiAndMapState", 0x002E5A38, "View_Init", UiGameplayHudEdgeKind::NativeDependency},
    {0x0044E224, 0xEB00548C, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x0044E0D4, "Gameplay_InitializeViewActorUiAndMapState", 0x0046345C, "Health_InitMeter", UiGameplayHudEdgeKind::ReplaceableMechanicHandoff},
    {0x0044E230, 0xEB002438, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x0044E0D4, "Gameplay_InitializeViewActorUiAndMapState", 0x00457318, "Map_Init", UiGameplayHudEdgeKind::RetainedCompositeHandoff},
    {0x00457458, 0xEAFB372D, "AL", UiGameplayHudInvocation::DirectTailCall, 0, 0x00457318, "Map_Init", 0x00325114, "Map_InitRoomData", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045977C, 0xEBFC388C, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x004596D0, "MagicMeter_DrawOverlay", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudEdgeKind::NativeDependency},
    {0x00459790, 0xEBFC383D, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x004596D0, "MagicMeter_DrawOverlay", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudEdgeKind::NativeDependency},
    {0x004597BC, 0xEBFB6889, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x004596D0, "MagicMeter_DrawOverlay", 0x003339E8, "EnvironmentRenderer_SubmitViewportConfig", UiGameplayHudEdgeKind::NativeDependency},
    {0x00459828, 0xEBFC3861, "AL", UiGameplayHudInvocation::DirectCall, 3, 0x004596D0, "MagicMeter_DrawOverlay", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045983C, 0xEBFC3812, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x004596D0, "MagicMeter_DrawOverlay", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudEdgeKind::NativeDependency},
    {0x00459868, 0xEBFB685E, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x004596D0, "MagicMeter_DrawOverlay", 0x003339E8, "EnvironmentRenderer_SubmitViewportConfig", UiGameplayHudEdgeKind::NativeDependency},
    {0x00459884, 0xEBFBC95E, "AL", UiGameplayHudInvocation::DirectCall, 6, 0x004596D0, "MagicMeter_DrawOverlay", 0x0034BE04, "Interface_ChangeHudVisibilityMode", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x004598AC, 0xEBFBE839, "AL", UiGameplayHudInvocation::DirectCall, 7, 0x004596D0, "MagicMeter_DrawOverlay", 0x00353998, "Magic_Fill", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x004598F8, 0xEBFC382D, "AL", UiGameplayHudInvocation::DirectCall, 8, 0x004596D0, "MagicMeter_DrawOverlay", 0x003679B4, "StaticInitGuard_TryAcquire", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045990C, 0xEBFC37DE, "AL", UiGameplayHudInvocation::DirectCall, 9, 0x004596D0, "MagicMeter_DrawOverlay", 0x0036788C, "RendererGlobalState_Init", UiGameplayHudEdgeKind::NativeDependency},
    {0x00459938, 0xEBFB682A, "AL", UiGameplayHudInvocation::DirectCall, 10, 0x004596D0, "MagicMeter_DrawOverlay", 0x003339E8, "EnvironmentRenderer_SubmitViewportConfig", UiGameplayHudEdgeKind::NativeDependency},
    {0x00459964, 0xEBFBC926, "AL", UiGameplayHudInvocation::DirectCall, 11, 0x004596D0, "MagicMeter_DrawOverlay", 0x0034BE04, "Interface_ChangeHudVisibilityMode", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x0045ACF8, 0xEBFC3A3E, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x0045ACCC, "Interface_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045AD48, 0xEBFC2E7E, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x0045ACCC, "Interface_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045AD5C, 0xEBFC2E79, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x0045ACCC, "Interface_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045AD88, 0x0B006BC6, "EQ", UiGameplayHudInvocation::DirectCall, 3, 0x0045ACCC, "Interface_Update", 0x00475CA8, "Interface_UpdateButtonRestrictions", UiGameplayHudEdgeKind::RetainedCompositeHandoff},
    {0x0045AE24, 0xEBF9F37F, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x0045ACCC, "Interface_Update", 0x002D7C28, "Interface_UpdateHudAlphas", UiGameplayHudEdgeKind::ReplaceableMechanicHandoff},
    {0x0045AE30, 0xEB0057EE, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x0045ACCC, "Interface_Update", 0x00470DF0, "Map_Update", UiGameplayHudEdgeKind::RetainedCompositeHandoff},
    {0x0045AE80, 0xEBFC697D, "AL", UiGameplayHudInvocation::DirectCall, 6, 0x0045ACCC, "Interface_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045AE9C, 0xEB0059C7, "AL", UiGameplayHudInvocation::DirectCall, 7, 0x0045ACCC, "Interface_Update", 0x004715C0, "Health_UpdateBeatingHeart", UiGameplayHudEdgeKind::ReplaceableMechanicHandoff},
    {0x0045AEA8, 0xEBF9F2F2, "AL", UiGameplayHudInvocation::DirectCall, 8, 0x0045ACCC, "Interface_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x0045AF0C, 0xEBF9F345, "AL", UiGameplayHudInvocation::DirectCall, 9, 0x0045ACCC, "Interface_Update", 0x002D7C28, "Interface_UpdateHudAlphas", UiGameplayHudEdgeKind::ReplaceableMechanicHandoff},
    {0x0045AF5C, 0xEBF9F284, "AL", UiGameplayHudInvocation::DirectCall, 10, 0x0045ACCC, "Interface_Update", 0x002D7974, "Interface_RaiseButtonAlphas", UiGameplayHudEdgeKind::ReplaceableMechanicHandoff},
    {0x0045AFCC, 0xEBF9F2A9, "AL", UiGameplayHudInvocation::DirectCall, 11, 0x0045ACCC, "Interface_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x0045AFE0, 0xEBF9F2A4, "AL", UiGameplayHudInvocation::DirectCall, 12, 0x0045ACCC, "Interface_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x0045B020, 0xEB007E46, "AL", UiGameplayHudInvocation::DirectCall, 13, 0x0045ACCC, "Interface_Update", 0x0047A940, "Health_UpdateMeter", UiGameplayHudEdgeKind::ReplaceableMechanicHandoff},
    {0x0045B040, 0xEBFC396C, "AL", UiGameplayHudInvocation::DirectCall, 14, 0x0045ACCC, "Interface_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045B054, 0xEBFC2DBB, "AL", UiGameplayHudInvocation::DirectCall, 15, 0x0045ACCC, "Interface_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045B094, 0xEBFC69B8, "AL", UiGameplayHudInvocation::DirectCall, 16, 0x0045ACCC, "Interface_Update", 0x0037577C, "Gameplay_InCsMode", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045B14C, 0xEBFC68CA, "AL", UiGameplayHudInvocation::DirectCall, 17, 0x0045ACCC, "Interface_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045B1E4, 0xEBFC3903, "AL", UiGameplayHudInvocation::DirectCall, 18, 0x0045ACCC, "Interface_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045B1F8, 0xEBFC2D52, "AL", UiGameplayHudInvocation::DirectCall, 19, 0x0045ACCC, "Interface_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045B230, 0xEBFC6939, "AL", UiGameplayHudInvocation::DirectCall, 20, 0x0045ACCC, "Interface_Update", 0x0037571C, "oot3d_get_flag_22a0", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045B244, 0xEBFC3D55, "AL", UiGameplayHudInvocation::DirectCall, 21, 0x0045ACCC, "Interface_Update", 0x0036A7A0, "Player_InCsMode", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045B284, 0xEB006EE4, "AL", UiGameplayHudInvocation::DirectCall, 22, 0x0045ACCC, "Interface_Update", 0x00476E1C, "Magic_Update", UiGameplayHudEdgeKind::RetainedCompositeHandoff},
    {0x0045B684, 0xEBFB6497, "AL", UiGameplayHudInvocation::DirectCall, 23, 0x0045ACCC, "Interface_Update", 0x003348E8, "PlayState_ClaimTransitionSlot", UiGameplayHudEdgeKind::NativeDependency},
    {0x0045B6BC, 0xEBFA2EE1, "AL", UiGameplayHudInvocation::DirectCall, 24, 0x0045ACCC, "Interface_Update", 0x002E7248, "AudioScene_SetBgmAndBroadcastTransition", UiGameplayHudEdgeKind::NativeDependency},
    {0x00470E10, 0xEBFBE1F8, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x00470DF0, "Map_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x00471044, 0xEBF97D17, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x00470DF0, "Map_Update", 0x002D04A8, "Map_SetPlayerInitialInfo", UiGameplayHudEdgeKind::NativeDependency},
    {0x00471664, 0xEBFBFE74, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x004715C0, "Health_UpdateBeatingHeart", 0x0037103C, "sins", UiGameplayHudEdgeKind::NativeDependency},
    {0x004716D0, 0xEBFBE432, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x004715C0, "Health_UpdateBeatingHeart", 0x0036A7A0, "Player_InCsMode", UiGameplayHudEdgeKind::NativeDependency},
    {0x004716E0, 0xEBFBDFC4, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x004715C0, "Health_UpdateBeatingHeart", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x00471730, 0xEBFC1011, "AL", UiGameplayHudInvocation::DirectCall, 3, 0x004715C0, "Health_UpdateBeatingHeart", 0x0037577C, "Gameplay_InCsMode", UiGameplayHudEdgeKind::NativeDependency},
    {0x00471774, 0xEBFC0F40, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x004715C0, "Health_UpdateBeatingHeart", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x00471798, 0xEBFC0F37, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x004715C0, "Health_UpdateBeatingHeart", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x00475D5C, 0xEBFBE2C0, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x0036E864, "Flags_GetSwitch", UiGameplayHudEdgeKind::NativeDependency},
    {0x00475DCC, 0xEBFBE2A4, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x0036E864, "Flags_GetSwitch", UiGameplayHudEdgeKind::NativeDependency},
    {0x00475EC8, 0xEBFBE265, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x0036E864, "Flags_GetSwitch", UiGameplayHudEdgeKind::NativeDependency},
    {0x0047601C, 0xEBFBC1C9, "AL", UiGameplayHudInvocation::DirectCall, 3, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudEdgeKind::NativeDependency},
    {0x00476030, 0xEBF98690, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x00476044, 0xEBF9868B, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x00476070, 0xEBF98680, "AL", UiGameplayHudInvocation::DirectCall, 6, 0x00475CA8, "Interface_UpdateButtonRestrictions", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x00476F20, 0xEBFBF955, "AL", UiGameplayHudInvocation::DirectCall, 0, 0x00476E1C, "Magic_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x0047701C, 0xEBFBC75C, "AL", UiGameplayHudInvocation::DirectCall, 1, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudEdgeKind::NativeDependency},
    {0x00477038, 0xEBFBC755, "AL", UiGameplayHudInvocation::DirectCall, 2, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudEdgeKind::NativeDependency},
    {0x0047705C, 0xEBFBC74C, "AL", UiGameplayHudInvocation::DirectCall, 3, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudEdgeKind::NativeDependency},
    {0x00477134, 0xEBFBC92F, "AL", UiGameplayHudInvocation::DirectCall, 4, 0x00476E1C, "Magic_Update", 0x003695F8, "PauseContext_GetState", UiGameplayHudEdgeKind::NativeDependency},
    {0x00477148, 0xEBFBBD7E, "AL", UiGameplayHudInvocation::DirectCall, 5, 0x00476E1C, "Magic_Update", 0x00366748, "AudioRuntime_IsFlagF38Set", UiGameplayHudEdgeKind::NativeDependency},
    {0x00477180, 0xEBFBF97D, "AL", UiGameplayHudInvocation::DirectCall, 6, 0x00476E1C, "Magic_Update", 0x0037577C, "Gameplay_InCsMode", UiGameplayHudEdgeKind::NativeDependency},
    {0x004771A4, 0xEBF98233, "AL", UiGameplayHudInvocation::DirectCall, 7, 0x00476E1C, "Magic_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x004771B8, 0xEBF9822E, "AL", UiGameplayHudInvocation::DirectCall, 8, 0x00476E1C, "Magic_Update", 0x002D7A78, "Player_GetEnvironmentalHazard", UiGameplayHudEdgeKind::RetainedActionHandoff},
    {0x00477224, 0xEBFBF894, "AL", UiGameplayHudInvocation::DirectCall, 9, 0x00476E1C, "Magic_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
    {0x004772DC, 0xEBFBC6AC, "AL", UiGameplayHudInvocation::DirectCall, 10, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudEdgeKind::NativeDependency},
    {0x004772F8, 0xEBFBC6A5, "AL", UiGameplayHudInvocation::DirectCall, 11, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudEdgeKind::NativeDependency},
    {0x0047731C, 0xEBFBC69C, "AL", UiGameplayHudInvocation::DirectCall, 12, 0x00476E1C, "Magic_Update", 0x00368D94, "Runtime_SignedDivMod32", UiGameplayHudEdgeKind::NativeDependency},
    {0x004773F4, 0xEBFBF820, "AL", UiGameplayHudInvocation::DirectCall, 13, 0x00476E1C, "Magic_Update", 0x0037547C, "Audio_PlaySoundGeneral", UiGameplayHudEdgeKind::NativeDependency},
}};

constexpr std::array<UiGameplayHudPresentationRootDescriptor,
                     kOot3dGameplayHudPresentationRootCount> kPresentationRoots{{
    {0x004596D0, 692, "MagicMeter_DrawOverlay", UiSubsystem::GameplayHud, UiSeamPhase::Composite, UiGameplayHudPresentationDisposition::RetainedComposite, "Fused native magic-overlay presentation, actions, fade, and renderer submission."},
    {0x0042DDA8, 384, "PauseTouchButtons_SubmitModels", UiSubsystem::TouchControls, UiSeamPhase::Submit, UiGameplayHudPresentationDisposition::ReplaceableRoot, "Submits the native lower-screen touch-control models."},
    {0x0042DF3C, 5344, "PauseTouchButtons_Update", UiSubsystem::TouchControls, UiSeamPhase::Update, UiGameplayHudPresentationDisposition::ReplaceableRoot, "Updates native lower-screen button mechanics, input, and presentation state."},
    {0x0042F5A8, 436, "PauseTouchButtons_Draw", UiSubsystem::TouchControls, UiSeamPhase::Draw, UiGameplayHudPresentationDisposition::ReplaceableRoot, "Draws the native lower-screen touch-control presentation."},
    {0x0046ACB8, 1012, "PauseTouchButtonPanel_Init", UiSubsystem::TouchControls, UiSeamPhase::Initialize, UiGameplayHudPresentationDisposition::ReplaceableRoot, "Initializes the native lower-screen touch-control resources."},
}};

} // namespace

const std::array<UiGameplayHudFunctionDescriptor,
                 kOot3dGameplayHudFunctionCount>&
Oot3dGameplayHudFunctions() noexcept {
    return kFunctions;
}

const UiGameplayHudFunctionDescriptor* Oot3dGameplayHudFunction(
    std::uint32_t entry) noexcept {
    for (const auto& function : kFunctions) {
        if (function.entry == entry) {
            return &function;
        }
    }
    return nullptr;
}

const std::array<UiGameplayHudEdgeDescriptor,
                 kOot3dGameplayHudEdgeCount>&
Oot3dGameplayHudEdges() noexcept {
    return kEdges;
}

const UiGameplayHudEdgeDescriptor* Oot3dGameplayHudEdgeAt(
    std::uint32_t call_site) noexcept {
    for (const auto& edge : kEdges) {
        if (edge.call_site == call_site) {
            return &edge;
        }
    }
    return nullptr;
}

const std::array<UiGameplayHudPresentationRootDescriptor,
                 kOot3dGameplayHudPresentationRootCount>&
Oot3dGameplayHudPresentationRoots() noexcept {
    return kPresentationRoots;
}

} // namespace oot3d::ui
