#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "oot3d_top_screen_config.h"
#include "oot3d_ui/ui_hud_content.h"
#include "oot3d_ui/ui_primitives.h"

namespace Oot3dNativeGame {

class NativeA32Memory;

enum class Oot3dUiProfile : std::uint8_t {
  Oot3d,
  TopScreen,
};

const char *Oot3dUiProfileName(Oot3dUiProfile profile) noexcept;
bool ParseOot3dUiProfile(std::string_view value,
                         Oot3dUiProfile *profile) noexcept;

// Frontend menus keep their native mechanics and render data. Profile policy
// only decides which native surface owns the one-screen presentation.
// The unchanged OoT3D profile needs the complete lower-screen frontend
// presented on a single-screen host. TopScreen owns its relocated frontend
// directly and must never receive a cloned lower framebuffer.
bool ShouldPresentNativeBottomFrontend(
    Oot3dUiProfile profile, bool nativePresentationActive) noexcept;

// TopScreen retains native frontend renderers on the upper target but removes
// the lower-screen clear and CommonBackground00 backdrop so the existing upper
// sky remains visible.
bool ShouldSuppressTopScreenFrontendBackdrop(
    Oot3dUiProfile profile, bool nativePresentationActive) noexcept;

struct TopScreenLayoutPatch {
  std::uint32_t Address = 0;
  std::array<std::uint32_t, 2> Expected{};
  std::array<std::uint32_t, 2> Replacement{};
  std::uint8_t WordCount = 0;
  std::string_view Semantic;
};

struct TopScreenLayoutApplyStats {
  std::uint32_t ContractsChecked = 0;
  std::uint32_t WordsChecked = 0;
  std::uint32_t WordsChanged = 0;
  std::uint32_t WordsWritten = 0;
};

// These are only scalar contracts whose owners are known from the recovered
// OoT3D code inventory. The IPS and its injected ARM payload are never loaded.
std::span<const TopScreenLayoutPatch> TopScreenVerifiedLayoutPatches() noexcept;

bool ApplyTopScreenVerifiedLayout(NativeA32Memory &memory,
                                  TopScreenLayoutApplyStats *stats = nullptr,
                                  std::string *error = nullptr);

struct TopScreenRuntimeQuadStreamContract {
  std::uint32_t OwnerAddress = 0;
  std::uint16_t RenderBufferPointerOffset = 0;
  std::uint16_t RectOriginOffset = 0;
  std::uint16_t RectExtentOffset = 0;
  std::uint16_t TextureOriginOffset = 0;
  std::uint16_t TextureExtentOffset = 0;
  std::uint16_t QuadCount = 0;
  std::string_view Semantic;
};

struct TopScreenRuntimeGeometryApplyStats {
  std::uint32_t ContractsChecked = 0;
  std::uint32_t StreamsRebound = 0;
  std::uint32_t StreamsNotInitialized = 0;
  std::uint32_t QuadsRebound = 0;
  std::uint32_t WordsWritten = 0;
};

// Pause-page constructors copy their scalar geometry tables into heap-backed
// render streams once. A savestate captured before selecting TopScreen already
// contains those streams, so rebuild their base positions and texcoords from
// the verified tables without touching per-frame offsets or depth.
std::span<const TopScreenRuntimeQuadStreamContract>
TopScreenVerifiedRuntimeQuadStreams() noexcept;

bool RebindTopScreenVerifiedRuntimeGeometry(
    NativeA32Memory &memory,
    TopScreenRuntimeGeometryApplyStats *stats = nullptr,
    std::string *error = nullptr);

struct TopScreenItemLaneQuery {
  bool NativeResult = false;
  bool ExtendedButtonHeld = false;
  bool CompatibilityModeActive = false;
  std::uint32_t SuppressionFlags = 0;
  std::uint32_t SuppressionMask = 0;
};

enum class TopScreenItemQuery : std::uint8_t {
  ItemIPressed,
  ItemIIPressed,
  ItemIHeld,
  ItemIIHeld,
};

struct TopScreenExtendedInputFrame {
  bool ZrPressed = false;
  bool ZlPressed = false;
  bool ZrHeld = false;
  bool ZlHeld = false;
  bool DpadLeftHeld = false;
  bool DpadRightHeld = false;
  bool XPressed = false;
  bool YPressed = false;
  bool XHeld = false;
  bool YHeld = false;
  bool RestorationLayout = false;
};

bool HasTopScreenItemQueryOverrideInput(
    const TopScreenExtendedInputFrame &input) noexcept;
bool HasTopScreenSlotItemOverrideInput(
    const TopScreenExtendedInputFrame &input) noexcept;

struct TopScreenItemQueryState {
  std::array<bool, 4> NativeResults{};
  std::array<bool, 4> CompatibilityOverrides{};
  TopScreenExtendedInputFrame Input;
  std::uint32_t SuppressionFlags = 0;
};

std::array<bool, 4> BuildTopScreenItemCompatibilityOverrides(
    const TopScreenExtendedInputFrame &input, std::uint8_t itemISlotIdentity,
    std::uint8_t itemIISlotIdentity) noexcept;

struct TopScreenItemQueryContract {
  TopScreenItemQuery Query = TopScreenItemQuery::ItemIPressed;
  std::uint32_t OriginalEntry = 0;
  std::uint32_t NativeFieldOffset = 0;
  std::uint32_t SuppressionMask = 0;
  std::string_view Semantic;
};

// Contracts recovered from the four original GlobalActionState getters
// replaced by the EUR mod.
std::span<const TopScreenItemQueryContract>
TopScreenVerifiedItemQueryContracts() noexcept;

bool ResolveTopScreenItemQuery(TopScreenItemQuery query,
                               const TopScreenItemQueryState &state) noexcept;

struct TopScreenSlotItemQuery {
  std::uint8_t Slot = 0;
  std::uint8_t NativeItemId = 0xFFU;
  bool ItemICompatibilityOverride = false;
  bool ItemIICompatibilityOverride = false;
  bool NativeSpecialStateActive = false;
};

// Source-level delta applied by the mod's replacement for
// GlobalActionState_GetSlotItemId. All ordinary slot resolution remains native.
std::uint8_t
ResolveTopScreenSlotItemId(const TopScreenSlotItemQuery &query) noexcept;

struct TopScreenVec2 {
  float X = 0.0F;
  float Y = 0.0F;
};

struct TopScreenHealthGeometry {
  std::array<TopScreenVec2, 20> Positions{};
  std::array<TopScreenVec2, 20> Sizes{};
  std::array<TopScreenVec2, 20> AtlasOrigins{};
  std::array<TopScreenVec2, 20> AtlasSizes{};
  std::uint8_t PulseHeart = 0;
  float PulseExpansion = 0.0F;
};

// Exact geometry math recovered from the mod's health builder at 0x005C7E88.
TopScreenHealthGeometry
BuildTopScreenHealthGeometry(std::uint16_t health, std::uint16_t healthCapacity,
                             bool alternateAtlasRow,
                             std::uint8_t pulsePhase) noexcept;

struct TopScreenMagicMeterGeometry {
  bool Visible = false;
  std::array<TopScreenVec2, 4> Positions{};
  std::array<TopScreenVec2, 4> Sizes{};
  std::array<TopScreenVec2, 4> AtlasOrigins{};
  std::array<TopScreenVec2, 4> AtlasSizes{};
};

struct TopScreenTouchClusterGeometry {
  std::array<TopScreenVec2, 6> Positions{};
  std::array<TopScreenVec2, 6> Sizes{};
  std::array<TopScreenVec2, 6> AtlasOrigins{};
  std::array<TopScreenVec2, 6> AtlasSizes{};
  std::array<float, 6> Alpha{};
};

struct TopScreenTouchDynamicState {
  std::array<float, 4> VerticalOffsets{};
  float Alpha = 0.0F;
};

struct TopScreenHudCompositorGate {
  bool Draw = false;
};

// Exact native-state gate shared by payload draw paths 0x005CBF40 and
// 0x005CC1A8. It excludes pause-page owners and player states that already own
// the underlying native controls.
bool ReadTopScreenHudCompositorGate(NativeA32Memory &memory,
                                    TopScreenHudCompositorGate *gate,
                                    std::string *error = nullptr);

struct TopScreenNativeTouchQuad28LayoutStats {
  bool Eligible = false;
  bool Transformed = false;
  std::uint32_t RendererAddress = 0;
  float LowerEdge = 0.0F;
  float UpperEdge = 0.0F;
};

// Exact transform of the native Navi/view eye quad performed by payload
// 0x005C9940 before the PauseTouchButtons submit. Quad 28 is identified by its
// native menu_top_parts00 atlas rectangle (128, 0, 48, 48); it is independent
// of the gameplay minimap renderer.
bool ApplyTopScreenNativeTouchQuad28Layout(
    NativeA32Memory &memory,
    TopScreenNativeTouchQuad28LayoutStats *stats = nullptr,
    std::string *error = nullptr);

// Reads the live button depression and visibility values used by the native
// PauseTouchButton renderer. Extended ZL/ZR state is supplied by the host HID
// path because those buttons do not exist in the original OoT3D HID frame.
bool ReadTopScreenTouchDynamicState(
    NativeA32Memory &memory, const TopScreenExtendedInputFrame &extendedInput,
    TopScreenTouchDynamicState *state, std::string *error = nullptr);

struct TopScreenPauseEdgeGeometry {
  std::array<TopScreenVec2, 2> Positions{};
  std::array<TopScreenVec2, 2> Sizes{};
  std::array<TopScreenVec2, 2> AtlasOrigins{};
  std::array<TopScreenVec2, 2> AtlasSizes{};
  float RgbScale = 1.0F;
};

// Exact two-quad geometry and color math from payload producer 0x005CD1E0.
// The caller supplies values owned by its recovered route/controller.
TopScreenPauseEdgeGeometry BuildTopScreenPauseEdgeGeometry(
    bool hasScene, std::uint8_t sceneMode, std::uint32_t runtimeMode,
    std::uint8_t sceneFade, std::int32_t transitionStep, float nativeVisibility,
    float globalColorScale) noexcept;

// The ordinary gameplay producer at payload 0x005C9940 reuses the native
// PauseTouchButton renderer. These are its exact six primary quads; the first
// four retain their independently animated native vertical offsets.
TopScreenTouchClusterGeometry BuildTopScreenTouchClusterGeometry(
    const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha,
    TopScreenHudLayout layout = TopScreenHudLayout::Normal) noexcept;

// Exact four-quad magic meter generated at payload 0x005C8244. The typed
// content fields map to the original SaveContext +0x4E/+0x50/+0x47 inputs.
TopScreenMagicMeterGeometry
BuildTopScreenMagicMeterGeometry(const oot3d::ui::UiHudMagicContent &magic,
                                 std::uint16_t healthCapacity) noexcept;

std::size_t AppendTopScreenHealthPresentation(
    const oot3d::ui::UiHudContentSnapshot &content,
    const oot3d::ui::UiTextureIdentity &pauseTopPage, std::uint8_t pulsePhase,
    std::vector<oot3d::ui::UiPrimitive> &output);

std::size_t AppendTopScreenMagicMeterPresentation(
    const TopScreenMagicMeterGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output);

std::size_t AppendTopScreenTouchClusterPresentation(
    const TopScreenTouchClusterGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output);

std::size_t AppendTopScreenPauseEdgePresentation(
    const TopScreenPauseEdgeGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &itemPage,
    std::vector<oot3d::ui::UiPrimitive> &output);

struct TopScreenWorldMapQuad {
  bool Visible = false;
  TopScreenVec2 Position{};
  TopScreenVec2 Size{};
  TopScreenVec2 AtlasOrigin{};
  TopScreenVec2 AtlasSize{};
  oot3d::ui::UiColor Color{};
};

struct TopScreenWorldMapGeometry {
  bool Active = false;
  std::uint8_t Destination = 0;
  std::array<TopScreenWorldMapQuad, 16> Quads{};
};

// Source reconstruction of payload producer 0x005CE57C's world-map lane.
// All mutable inputs come from the original PauseWorldMap arrays, save flags,
// scene state and destination tables.
bool ReadTopScreenWorldMapGeometry(NativeA32Memory &memory,
                                   TopScreenWorldMapGeometry *geometry,
                                   std::string *error = nullptr);

std::size_t AppendTopScreenWorldMapPresentation(
    const TopScreenWorldMapGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &ocarinaPage,
    std::vector<oot3d::ui::UiPrimitive> &output);

struct TopScreenPauseNavigationGeometry {
  std::array<TopScreenWorldMapQuad, 2> Quads{};
};

// Exact pause navigation arrows produced by payload 0x005C887C/0x005C9040.
// Direction is derived from the native OoT3D input frame: -1 left, +1 right.
TopScreenPauseNavigationGeometry
BuildTopScreenPauseNavigationGeometry(std::int8_t direction) noexcept;

std::size_t AppendTopScreenPauseNavigationPresentation(
    const TopScreenPauseNavigationGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output);

struct TopScreenFileSelectStripGeometry {
  bool Active = false;
  TopScreenWorldMapQuad Quad{};
};

// Exact visible quad from payload 0x005CD64C. Its second allocated quad has
// zero extent and is intentionally not emitted.
TopScreenFileSelectStripGeometry
BuildTopScreenFileSelectStripGeometry(bool active) noexcept;

bool ReadTopScreenFileSelectStripActive(NativeA32Memory &memory, bool *active,
                                        std::string *error = nullptr);

std::size_t AppendTopScreenFileSelectStripPresentation(
    const TopScreenFileSelectStripGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &fileSelectAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output);

struct TopScreenExtendedItemButtonsGeometry {
  std::array<TopScreenWorldMapQuad, 2> Quads{};
};

// Exact quads 5/6 of the seven-quad renderer produced by payload 0x005C9940.
// Their native 0x45/0x46 slot identities remain the only visibility source.
TopScreenExtendedItemButtonsGeometry
BuildTopScreenExtendedItemButtonsGeometry(bool itemIActive,
                                          bool itemIIActive) noexcept;

std::size_t AppendTopScreenExtendedItemButtonsPresentation(
    const TopScreenExtendedItemButtonsGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    std::vector<oot3d::ui::UiPrimitive> &output);

// Copies the first five live item/action quads from the native PlayState
// renderer through payload table 0x005D33C8. The source renderer remains the
// authority for UVs, colors and per-item animation.
bool AppendTopScreenNativeItemIconCopies(
    NativeA32Memory &memory, const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha, const oot3d::ui::UiTextureIdentity &itemIcons,
    std::vector<oot3d::ui::UiPrimitive> &output, std::string *error = nullptr,
    bool renderDpadIcons = true);

// Reconstructs the six PauseCounter instances allocated by payload
// 0x005C9474: rupees, dungeon keys and four action-item ammo counters. Native
// special counter geometry is copied when present; ordinary digits use the
// original NumberGlyphs atlas and counter metrics.
bool AppendTopScreenNativeCounters(
    NativeA32Memory &memory, const std::array<float, 4> &nativeVerticalOffsets,
    const oot3d::ui::UiTextureIdentity &numberGlyphs,
    std::vector<oot3d::ui::UiPrimitive> &output,
    std::string *error = nullptr, const TopScreenUiConfig *config = nullptr,
    const TopScreenExtendedInputFrame *input = nullptr);

struct TopScreenTouchLabelsGeometry {
  std::array<TopScreenWorldMapQuad, 2> Quads{};
};

// Exact two-quad label group produced by payload 0x005C9940. Vertical offsets
// share the same native animation lanes as touch-cluster buttons 2 and 3.
TopScreenTouchLabelsGeometry BuildTopScreenTouchLabelsGeometry(
    const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha,
    TopScreenHudLayout layout = TopScreenHudLayout::Normal) noexcept;

std::size_t AppendTopScreenTouchLabelsPresentation(
    const TopScreenTouchLabelsGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output);

struct TopScreenAuxiliaryTouchInputs {
  bool HasPlayState = false;
  std::uint8_t StateType = 0;
  std::uint8_t StateSubtype = 0;
  std::int16_t PlayerHudMode = 0;
  std::uint32_t TouchLayoutState = 0;
  std::uint32_t PauseState = 0;
  bool NestedSceneOwnerActive = false;
  bool AlternateHudRendererActive = false;
};

struct TopScreenAuxiliaryTouchGeometry {
  std::array<TopScreenWorldMapQuad, 5> Quads{};
};

// Exact group-39 quads 34..38 produced by payload 0x005C9940. The visibility
// flag formerly stored at payload BSS 0x005D4118 is derived directly from its
// native PlayState, pause-state and alternate-renderer owners.
bool ReadTopScreenAuxiliaryTouchInputs(NativeA32Memory &memory,
                                       TopScreenAuxiliaryTouchInputs *inputs,
                                       std::string *error = nullptr);
TopScreenAuxiliaryTouchGeometry BuildTopScreenAuxiliaryTouchGeometry(
    const TopScreenAuxiliaryTouchInputs &inputs,
    const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha,
    TopScreenHudLayout layout = TopScreenHudLayout::Normal) noexcept;
std::size_t AppendTopScreenAuxiliaryTouchPresentation(
    const TopScreenAuxiliaryTouchGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output);

struct TopScreenNativeTouchCopyStats {
  std::uint32_t SourceQuadsRead = 0;
  std::uint32_t PrimitivesEmitted = 0;
  std::uint32_t AlphaVisiblePrimitives = 0;
  std::uint32_t HorseStaminaQuadsRead = 0;
  std::uint32_t AlphaVisibleHorseStaminaPrimitives = 0;
};

// Source equivalent of the live quad-copy loop in payload 0x005C9940. The
// native renderer remains the authority for positions, UVs, colors and
// animation; only the payload's ten destination transforms are reproduced.
bool AppendTopScreenNativeTouchCopies(
    NativeA32Memory &memory, const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output,
    TopScreenNativeTouchCopyStats *stats = nullptr,
    std::string *error = nullptr, const TopScreenUiConfig *config = nullptr);

// Clips native gameplay HUD primitives to OoT3D's 400x240 top surface.
// Renderer presentation maps that logical surface to the host output;
// off-screen native translations remain authoritative visibility state.
void ApplyTopScreenGameplayCanvas(
    std::span<oot3d::ui::UiPrimitive> primitives) noexcept;

// Source equivalent of the TopScreen 2.1.1 generic HUD transform. Gameplay
// geometry is scaled around the nearest native-screen corner, then receives
// the configured signed edge margins. Pause/frontend presentation is
// intentionally outside this transform.
void ApplyTopScreenHudScale(
    std::span<oot3d::ui::UiPrimitive> primitives,
    const TopScreenUiConfig &config) noexcept;

struct TopScreenVec3 {
  float X = 0.0F;
  float Y = 0.0F;
  float Z = 0.0F;
};

struct TopScreenQuestGeometryContext {
  std::uint32_t PauseState = 0;
  bool NativePageGateActive = false;
  bool PlayerSpecialState = false;
  float LeftRegionOffsetX = 0.0F;
  float LeftRegionOffsetY = 0.0F;
  float HudScale = 1.0F;
  float HudMarginX = 0.0F;
  float HudMarginY = 0.0F;
};

struct TopScreenPauseProjectionState {
  struct TrackedPosition {
    std::uint32_t Address = 0;
    std::uint32_t OriginalBits = 0;
    std::uint32_t LastWrittenBits = 0;
    bool Valid = false;
  };

  // The host configuration replaces payload storage 0x005D66AC. The false
  // state moves both the native minimap and its indicator streams outside the
  // visible canvas.
  bool AlternatePage = true;
  float OffsetX = 0.0F;
  float OffsetY = 0.0F;
  bool NativeQuestGate = false;
  bool QuestDrawModelAdjusted = false;
  TrackedPosition MapX;
  TrackedPosition MapY;
  std::array<TrackedPosition, 4U * 64U> IconX;
};

struct TopScreenSystemMenuLayerStats {
  bool Active = false;
  std::uint32_t RendererAddress = 0;
  std::uint32_t QuadsVisited = 0;
  std::uint32_t QuadsSuppressed = 0;
};

// TopScreen 1.2 payload helper 0x005CDB14 suppresses the native lower-screen
// background layers while the system menu is active. The desktop compositor
// has no lower display, so only its portable render-buffer operation remains:
// quads larger than 130x80 receive the payload's (1,1,1,0) vertex color.
bool ApplyTopScreenSystemMenuLayerSuppression(
    NativeA32Memory &memory, TopScreenSystemMenuLayerStats *stats = nullptr,
    std::string *error = nullptr);

// TopScreen 1.2 payload helper 0x005C9058 reads the native PlayState
// message/Ocarina fields and blocks promoted HUD layers while that UI owns the
// presentation surface.
bool ReadTopScreenOcarinaUiActive(NativeA32Memory &memory, bool *active,
                                  std::string *error = nullptr);

struct TopScreenModelRegionTransform {
  float MinimumX = 0.0F;
  float MaximumX = 0.0F;
  float MinimumY = 0.0F;
  float TargetX = 0.0F;
  float TargetY = 0.0F;
  float Scale = 1.0F;
};

struct TopScreenModelRegionStats {
  bool ContractMatched = false;
  std::uint32_t QuadsVisited = 0;
  std::uint32_t QuadsSelected = 0;
  std::uint32_t VerticesTransformed = 0;
};

// Source equivalent of TopScreen 1.2 payload helper 0x005CD8CC. It operates
// on the native model render-buffer position and per-quad translation streams,
// selects a region by materialized quad center, then recenters and scales the
// selected group around its aggregate center.
bool TransformTopScreenModelRegion(
    NativeA32Memory &memory, std::uint32_t renderBufferAddress,
    const TopScreenModelRegionTransform &transform,
    TopScreenModelRegionStats *stats = nullptr,
    std::string *error = nullptr);

struct TopScreenQuestModelTransformStats {
  std::uint32_t MainModelsTransformed = 0;
  std::uint32_t ChildModelsVisited = 0;
  std::uint32_t ChildModelsTransformed = 0;
  std::uint32_t DrawModelsTransformed = 0;
};

// Typed ports of the model transforms added by the TopScreen 1.2 replacements
// for PauseQuestPage_SubmitModels and PauseQuestPage_Draw.
bool ApplyTopScreenQuestSubmitModelTransforms(
    NativeA32Memory &memory, const TopScreenUiConfig &config,
    TopScreenQuestModelTransformStats *stats = nullptr,
    std::string *error = nullptr);
bool ApplyTopScreenQuestDrawModelTransform(
    NativeA32Memory &memory, std::uint32_t sceneContextAddress,
    const TopScreenUiConfig &config, TopScreenPauseProjectionState &state,
    TopScreenQuestModelTransformStats *stats = nullptr,
    std::string *error = nullptr);

bool ReadTopScreenQuestGeometryContext(
    NativeA32Memory &memory, TopScreenQuestGeometryContext *context,
    const TopScreenPauseProjectionState *projection = nullptr,
    std::string *error = nullptr);

bool ApplyTopScreenPauseProjection(NativeA32Memory &memory,
                                   TopScreenPauseProjectionState &state,
                                   std::string *error = nullptr,
                                   const TopScreenUiConfig *config = nullptr);

struct TopScreenQuestGeometryStats {
  std::uint32_t QuadsVisited = 0;
  std::uint32_t QuadsTranslated = 0;
  std::uint32_t QuadsScaled = 0;
  std::uint32_t QuadsHidden = 0;
};

TopScreenQuestGeometryStats TransformTopScreenQuestGeometry(
    std::span<std::array<TopScreenVec3, 4>> quads,
    const TopScreenQuestGeometryContext &context) noexcept;

struct TopScreenQuestRenderBufferStats {
  std::uint32_t QuadCount = 0;
  TopScreenQuestGeometryStats Geometry{};
};

bool TransformTopScreenQuestRenderBuffer(
    NativeA32Memory &memory, std::uint32_t renderBufferAddress,
    const TopScreenQuestGeometryContext &context,
    TopScreenQuestRenderBufferStats *stats = nullptr,
    std::string *error = nullptr);

struct TopScreenPauseTargetCommand {
  bool RendererEnabled = false;
  std::uint32_t NativeCommand = 0;
  bool HalfHeightMode = false;
};

struct TopScreenPauseTargetPlan {
  bool Handled = false;
  bool BindTopTarget = false;
  std::uint32_t FramebufferBindingOffset = 0;
  std::uint32_t StoredCommand = 0;
  std::uint32_t ViewportX = 0;
  std::uint32_t ViewportY = 0;
  std::uint32_t ViewportWidth = 0;
  std::uint32_t ViewportHeight = 0;
};

TopScreenPauseTargetPlan
ResolveTopScreenPauseTargetCommand(const TopScreenPauseTargetCommand &command,
                                   bool routeActive) noexcept;
TopScreenPauseTargetPlan ResolveTopScreenPausePageRedrawTargetCommand(
    const TopScreenPauseTargetCommand &command) noexcept;

struct TopScreenPauseChildState {
  std::array<std::uint32_t, 6> Values{};
  std::uint8_t SuppressedMask = 0;
};

enum class TopScreenPauseChildSuppressionSet : std::uint8_t {
  // Wrapper 0x005CFEF0 suppresses the four page owners and touch owner before
  // the ordinary lower-screen PauseUi_Draw. The selector lane remains live.
  Tail,
  // Helper 0x005C8678 suppresses all six owners around its auxiliary redraw.
  AuxiliaryRedraw,
  // The stable page path in 0x005CE57C suppresses only the touch owner before
  // redrawing the complete active page in the top-screen viewport.
  PageRedraw,
};

struct TopScreenAlternateRendererState {
  std::array<std::uint8_t, 4> Visibility{};
};

enum class TopScreenPauseDrawAction : std::uint8_t {
  NativeDraw,
  SuppressNativeChildren,
};

struct TopScreenPauseDrawInputs {
  bool HasScene = false;
  std::uint8_t SceneMode = 0;
  std::uint8_t SceneVariant = 0;
  std::uint16_t SceneSequence = 0;
  std::uint32_t RuntimeMode = 0;
  std::uint32_t EntranceIndex = 0;
  bool NativeTransitionActive = false;
  bool AlternatePathReady = false;
  bool AlternatePathVisible = false;
  bool SuppressionOverride = false;
  bool AnyPageGateActive = false;
  std::array<std::uint32_t, 6> ChildStates{};
};

enum class TopScreenPauseClosePage : std::uint8_t {
  None,
  Items,
  Gear,
  DungeonMap,
  SystemMenu,
};

struct TopScreenPauseClosePlan {
  TopScreenPauseClosePage Page = TopScreenPauseClosePage::None;
  std::uint32_t ResetFunction = 0;
};

// Source form of the START-close dispatch in payload controller 0x005C9940.
// The returned function is an original OoT3D page reset, never payload code.
TopScreenPauseClosePlan
ResolveTopScreenPauseStartClose(const TopScreenPauseDrawInputs &inputs,
                                bool startPressed) noexcept;

struct TopScreenPauseSystemOpenState {
  // Payload 0x005C9940 gives the active page five update ticks to reach its
  // native close state before opening Save/Quit.
  std::uint8_t RemainingTicks = 0;
};

struct TopScreenPauseSystemOpenPlan {
  TopScreenPauseClosePage Page = TopScreenPauseClosePage::None;
  std::uint32_t ResetFunction = 0;
};

// Source form of the B-to-Save/Quit transition in payload controller
// 0x005C9940. The page-state values and reset functions belong to the
// original OoT3D pause controllers; payload-private state is host-transient.
TopScreenPauseSystemOpenPlan ResolveTopScreenPauseBSystemOpen(
    const TopScreenPauseDrawInputs &inputs, bool bPressed,
    TopScreenPauseSystemOpenState &state) noexcept;

struct TopScreenPauseDrawRoutingState {
  bool NativeTransitionLatched = false;
  bool SuppressionDelayArmed = false;
  std::uint16_t SuppressionDelayCommands = 0;
};

struct TopScreenPausePageRedrawInputs {
  TopScreenPauseDrawInputs Pause{};
  std::int16_t NativeHealthGate = 0;
  std::uint32_t WorldMapControllerState = 0;
  bool RuntimeSceneLatch = false;
};

struct TopScreenPausePageRedrawState {
  std::uint16_t DelayCalls = 0;
};

// Transient state owned by payload helper 0x005CCC48. It routes native pause
// draw calls while the 3DS display mode changes; it is not save-game state.
struct TopScreenPauseRouteState {
  std::uint32_t PreviousRuntimeMode = 0;
  std::uint8_t TransitionPhase = 0;
  std::uint16_t RemainingCalls = 0;
};

// Payload helper 0x005C8E0C retains this latch while the scene owner remains
// in mode 3. It is transient UI routing state, never serialized game state.
struct TopScreenTouchCoordinateRouteState {
  bool RuntimeSceneLatch = false;
};

struct TopScreenRendererVisibilityRouteState {
  std::int32_t FadeStep = 0;
  std::int32_t DelayCalls = 0;
  bool RendererConflict = false;
};

struct TopScreenRendererVisibilityInputs {
  std::uint32_t ControllerAddress = 0;
  bool HasCallScene = false;
  std::uint8_t CallSceneMode = 0;
  std::uint32_t CallSceneSubmode = 0;
  std::uint16_t CallSceneSequence = 0;
  bool GlobalSceneRuntimeRoute = false;
  bool RuntimeSceneLatch = false;
  bool NativeRuntimeActive = false;
  std::int16_t NativeFadeGate = 0;
};

struct TopScreenPauseControllerState {
  bool RoutedRendererLatched = false;
  std::int32_t RoutedFadeStep = 0;
  bool AlternateRendererPositionsCaptured = false;
  std::array<std::uint32_t, 2> AlternateRendererAddresses{};
  std::array<float, 2> AlternateRendererOriginalX{};
  std::array<float, 2> AlternateRendererOriginalY{};
};

struct TopScreenPauseControllerInputs {
  TopScreenPauseDrawInputs Pause;
  std::uint32_t RendererAddress = 0;
  std::uint8_t RendererPhase = 0;
};

enum class TopScreenPauseControllerAction : std::uint8_t {
  NativeDraw,
  SuppressNativeDraw,
  NativeDrawThenAlternateOverlay,
};

enum class TopScreenRendererVisibilityAction : std::uint8_t {
  NativeDraw,
  SuppressNativeDraw,
};

bool ReadTopScreenPauseDrawInputs(NativeA32Memory &memory,
                                  TopScreenPauseDrawInputs *inputs,
                                  std::string *error = nullptr);
bool ReadTopScreenPausePageRedrawInputs(NativeA32Memory &memory,
                                        bool runtimeSceneLatch,
                                        TopScreenPausePageRedrawInputs *inputs,
                                        std::string *error = nullptr);
bool ResolveTopScreenPausePageRedraw(
    const TopScreenPausePageRedrawInputs &inputs,
    TopScreenPausePageRedrawState *state) noexcept;
bool UseTopScreenAlternatePauseViewport(
    const TopScreenPauseDrawInputs &inputs) noexcept;
bool IsTopScreenTitleDemoRuntime(
    const TopScreenPauseDrawInputs &inputs) noexcept;
bool UseTopScreenSceneFiveViewport(
    const TopScreenPauseDrawInputs &inputs) noexcept;
bool ResolveTopScreenPauseRouteActive(const TopScreenPauseDrawInputs &inputs,
                                      TopScreenPauseRouteState *state) noexcept;
std::uint32_t ResolveTopScreenPauseRoutedCommand(std::uint32_t nativeCommand,
                                                 bool routeActive) noexcept;
bool ResolveTopScreenTouchCoordinateSuppression(
    const TopScreenPauseDrawInputs &inputs, bool routeActive,
    bool pauseCameraControlActive, bool secondaryLayerActive,
    TopScreenTouchCoordinateRouteState *state) noexcept;
bool ApplyTopScreenTouchCoordinateSuppression(NativeA32Memory &memory,
                                              std::string *error = nullptr);
bool ResolveTopScreenRendererVisibilityRoute(
    NativeA32Memory &memory, const TopScreenRendererVisibilityInputs &inputs,
    TopScreenRendererVisibilityRouteState *state,
    TopScreenRendererVisibilityAction *action, std::string *error = nullptr);
bool ReadTopScreenPauseControllerInputs(NativeA32Memory &memory,
                                        TopScreenPauseControllerInputs *inputs,
                                        std::string *error = nullptr);
bool ResolveTopScreenPauseController(
    NativeA32Memory &memory, const TopScreenPauseControllerInputs &inputs,
    bool routeActive, TopScreenPauseControllerState *state,
    TopScreenPauseControllerAction *action, std::string *error = nullptr);
bool PrepareTopScreenPauseAlternateRenderers(
    NativeA32Memory &memory, TopScreenPauseControllerState *state,
    std::array<std::uint32_t, 2> *visibleRenderers,
    std::string *error = nullptr);
bool BeginTopScreenAlternateRendererSuppression(
    NativeA32Memory &memory, TopScreenAlternateRendererState *saved,
    std::string *error = nullptr);
bool EndTopScreenAlternateRendererSuppression(
    NativeA32Memory &memory, const TopScreenAlternateRendererState &saved,
    std::string *error = nullptr);
TopScreenPauseDrawAction
ResolveTopScreenPauseDrawAction(const TopScreenPauseDrawInputs &inputs,
                                TopScreenPauseDrawRoutingState *state) noexcept;
void ObserveTopScreenPauseTargetCommand(
    const TopScreenPauseDrawInputs &inputs, std::uint32_t nativeCommand,
    TopScreenPauseDrawRoutingState *state) noexcept;

struct TopScreenPauseIconBuild {
  std::uint32_t Argument2 = 0;
  std::uint32_t Argument3 = 0;
  std::uint32_t StackArgument0 = 0;
  std::uint32_t StackArgument1 = 0;
};

// Exact source equivalent of TopScreen 1.2 payload wrapper 0x005D2738. It
// changes only native icon selector 0xC5 and derives its geometry from the
// externally selected layout and scale.
bool ResolveTopScreenPauseIconBuild(TopScreenPauseIconBuild *build,
                                    const TopScreenUiConfig &config) noexcept;

// Payload wrapper 0x005D005C skips the lower-screen touch-button callback for
// the contiguous native pause states 12..19.
bool SuppressTopScreenTouchButtonDraw(std::uint32_t pauseState) noexcept;

struct TopScreenControlFlowContract {
  std::uint32_t Entry = 0;
  std::uint32_t Target = 0;
  std::string_view Semantic;
};

// Recovered unconditional flow deltas, including exact callsite NOPs. The
// runtime branches at these entries without modifying guest code or registers.
std::span<const TopScreenControlFlowContract>
TopScreenVerifiedControlFlowContracts() noexcept;
bool ResolveTopScreenControlFlow(std::uint32_t entry,
                                 std::uint32_t *target) noexcept;

struct TopScreenFloatLoadContract {
  std::uint32_t Entry = 0;
  std::uint8_t VfpLane = 0;
  std::uint32_t ValueBits = 0;
  std::string_view Semantic;
};

std::span<const TopScreenFloatLoadContract>
TopScreenVerifiedFloatLoadContracts() noexcept;
bool ResolveTopScreenFloatLoad(std::uint32_t entry, std::uint8_t *vfpLane,
                               std::uint32_t *valueBits) noexcept;

struct TopScreenTitleLogoFadeHoldResult {
  std::uint32_t AnimationOwner = 0;
  std::uint32_t Argument1 = 1;
  std::uint32_t NextPc = 0x001DAFF8U;
};

// Source equivalent of records 111-113 inside EnMag_Update. The consumer
// enters at the containing native block 0x001DADCC, retains its two alpha
// stores and native owner load, then applies the mod's fade-substate/timer
// writes instead of calling the animation-state transition.
bool ApplyTopScreenTitleLogoFadeHold(NativeA32Memory &memory,
                                     std::uint32_t actorAddress,
                                     std::uint32_t actorStateAddress,
                                     std::uint32_t clampedAlphaBits,
                                     TopScreenTitleLogoFadeHoldResult *result,
                                     std::string *error = nullptr);

struct TopScreenCameraNormal1Scalar {
  std::uint32_t NativeValueBits = 0;
  std::uint32_t ContinuedValueBits = 0;
};

// Source equivalent of record 26 and payload entry 0x005D001C. The original
// scalar is retained by Camera_Normal1; only the value consumed by the
// remaining native function is scaled while the R+D-pad Up option is enabled.
TopScreenCameraNormal1Scalar
ResolveTopScreenCameraNormal1Scalar(std::uint32_t nativeValueBits,
                                    std::uint8_t zoomPercent) noexcept;

struct TopScreenItemsSelectionBegin {
  bool ZrPressed = false;
  bool ZlPressed = false;
  std::uint32_t NativePageState = 0;
};

struct TopScreenItemsSelectionPlan {
  bool Active = false;
  std::uint32_t Selection = 0;
};

TopScreenItemsSelectionPlan ResolveTopScreenItemsSelectionBegin(
    const TopScreenItemsSelectionBegin &input) noexcept;
bool CompleteTopScreenItemsSelection(std::uint32_t pendingSelection,
                                     std::uint32_t nativeSelectionState,
                                     std::uint32_t *selection,
                                     std::uint16_t *cursorY) noexcept;

bool BeginTopScreenPauseChildSuppression(NativeA32Memory &memory,
                                         TopScreenPauseChildSuppressionSet set,
                                         TopScreenPauseChildState *saved,
                                         std::string *error = nullptr);
bool EndTopScreenPauseChildSuppression(NativeA32Memory &memory,
                                       const TopScreenPauseChildState &saved,
                                       std::string *error = nullptr);

// Source-level equivalent of the four small item query wrappers in the mod.
// Host input supplies the semantic ZL/ZR state; no 3DS IR memory is consulted.
bool ResolveTopScreenItemLane(const TopScreenItemLaneQuery &query) noexcept;

} // namespace Oot3dNativeGame
