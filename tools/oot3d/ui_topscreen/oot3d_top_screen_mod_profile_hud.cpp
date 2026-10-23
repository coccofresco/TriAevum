// TopScreen mod profile: HUD and touch geometry and presentation (health, magic, counters, item
// icons, touch clusters and labels, file select, HUD scale, timer).
#include "oot3d_top_screen_mod_profile_internal.h"

#include "oot3d_gameplay_player.h"

namespace Oot3dNativeGame {

TopScreenHealthGeometry
BuildTopScreenHealthGeometry(std::uint16_t health, std::uint16_t healthCapacity,
                             bool alternateAtlasRow,
                             std::uint8_t pulsePhase) noexcept {
  constexpr std::array<float, 5> kAtlasRows{80.0F, 96.0F, 112.0F, 128.0F,
                                            144.0F};
  TopScreenHealthGeometry result;
  const std::uint32_t visibleHearts =
      (static_cast<std::uint32_t>(healthCapacity) + 15U) / 16U;
  std::uint32_t remainingHealth = health;
  for (std::size_t index = 0; index < result.Positions.size(); ++index) {
    const auto column = static_cast<float>(index % 10U);
    const auto row = static_cast<float>(index / 10U);
    result.Positions[index] =
        index < visibleHearts
            ? TopScreenVec2{8.0F + column * 12.0F, 6.0F + row * 9.75F}
            : TopScreenVec2{400.0F, 400.0F};
    result.Sizes[index] = {9.0F, 9.0F};
    result.AtlasSizes[index] = {12.0F, 12.0F};

    const std::size_t atlasRow = remainingHealth == 0U   ? 0U
                                 : remainingHealth < 5U  ? 1U
                                 : remainingHealth < 9U  ? 2U
                                 : remainingHealth < 13U ? 3U
                                                         : 4U;
    result.AtlasOrigins[index] = {
        240.0F, kAtlasRows[atlasRow] - (alternateAtlasRow ? 80.0F : 0.0F)};
    remainingHealth = remainingHealth > 16U ? remainingHealth - 16U : 0U;
  }

  const std::uint32_t currentHeart =
      health == 0U ? 0U : (static_cast<std::uint32_t>(health) + 15U) / 16U - 1U;
  result.PulseHeart = static_cast<std::uint8_t>(currentHeart);
  std::uint8_t pulseStep = 0U;
  if (health != 0U) {
    if (pulsePhase < 2U || (pulsePhase >= 12U && pulsePhase < 14U)) {
      pulseStep = 1U;
    } else if ((pulsePhase >= 2U && pulsePhase < 4U) ||
               (pulsePhase >= 10U && pulsePhase < 12U)) {
      pulseStep = 2U;
    } else if ((pulsePhase >= 4U && pulsePhase < 6U) ||
               (pulsePhase >= 8U && pulsePhase < 10U)) {
      pulseStep = 3U;
    } else if (pulsePhase >= 6U && pulsePhase < 8U) {
      pulseStep = 4U;
    }
  }
  result.PulseExpansion = static_cast<float>(pulseStep) * 0.75F;
  return result;
}

TopScreenMagicMeterGeometry
BuildTopScreenMagicMeterGeometry(const oot3d::ui::UiHudMagicContent &magic,
                                 std::uint16_t healthCapacity) noexcept {
  TopScreenMagicMeterGeometry result;
  if (!magic.acquired.IsKnown() || !magic.current.IsKnown() ||
      !magic.double_magic_acquired.IsKnown() || !magic.acquired.value) {
    return result;
  }
  result.Visible = true;
  result.AtlasOrigins = {
      {{136.0F, 120.0F}, {142.0F, 120.0F}, {143.0F, 120.0F}, {153.0F, 120.0F}}};
  result.AtlasSizes = {
      {{8.0F, 8.0F}, {1.0F, 8.0F}, {-8.0F, 8.0F}, {6.0F, 4.0F}}};

  const auto capacityHearts =
      (static_cast<std::uint32_t>(healthCapacity) + 15U) / 16U;
  const float y = capacityHearts > 10U ? 25.5F : 15.75F;
  const bool alternate = magic.double_magic_acquired.value;
  result.Positions[0] = {5.0F, y};
  result.Sizes[0] = {6.0F, 6.0F};
  result.Positions[1] = {11.0F, y};
  result.Sizes[1] = {alternate ? 111.0F : 49.5F, 6.0F};
  result.Positions[2] = {alternate ? 122.0F : 60.5F, y};
  result.Sizes[2] = {6.0F, 6.0F};
  result.Positions[3] = {6.5F, y + 1.5F};
  const float progressRange = (alternate ? 2.0F : 1.0F) * 48.0F;
  const float fullWidth = alternate ? 160.0F : 78.0F;
  result.Sizes[3] = {
      fullWidth * 0.75F *
          (static_cast<float>(magic.current.value) / progressRange),
      3.0F};
  return result;
}

TopScreenTouchClusterGeometry BuildTopScreenTouchClusterGeometry(
    const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha, TopScreenHudLayout layout) noexcept {
  // SingleScreen 3.0 hub 0x005DCAD0..0x005DCCB0 (2.1.1 arrays
  // 0x005D33A0/0x005D33B4): the four button circles centred on table
  // 0x00609770/0x00609784, 32 px, custom_ui01 rect 1 (148,3,48,48); the A
  // circle at (248, 24) with rect 0 (97,3,48,48); the D-pad cross centred on
  // (30.5, 67), 28 px, rect 3 drawn as (385,108,29,29). The 2.1.1 drew them
  // from its replaced menu_top_parts00, which TriAevum no longer has.
  constexpr std::array<TopScreenVec2, 4> kNativeCenters{
      {{358.0F, 20.0F}, {334.0F, 44.0F}, {382.0F, 44.0F}, {358.0F, 68.0F}}};
  TopScreenTouchClusterGeometry result;
  for (std::size_t index = 0; index < kNativeCenters.size(); ++index) {
    result.Positions[index] = {kNativeCenters[index].X - 16.0F,
                               kNativeCenters[index].Y - 16.0F +
                                   nativeVerticalOffsets[index]};
    result.Sizes[index] = {32.0F, 32.0F};
    result.AtlasOrigins[index] = {148.0F, 3.0F};
    result.AtlasSizes[index] = {48.0F, 48.0F};
    result.Alpha[index] =
        layout == TopScreenHudLayout::Restoration && index >= 2U ? 0.0F
                                                                 : nativeAlpha;
  }

  result.Positions[4] =
      layout == TopScreenHudLayout::Restoration
          ? TopScreenVec2{342.0F, 52.0F}
          : TopScreenVec2{232.0F, 8.0F};
  result.Sizes[4] = {32.0F, 32.0F};
  result.AtlasOrigins[4] = {97.0F, 3.0F};
  result.AtlasSizes[4] = {48.0F, 48.0F};
  result.Alpha[4] = 1.0F;

  result.Positions[5] = {30.5F - 14.0F, 67.0F - 14.0F};
  result.Sizes[5] = {28.0F, 28.0F};
  result.AtlasOrigins[5] = {385.0F, 108.0F};
  result.AtlasSizes[5] = {29.0F, 29.0F};
  result.Alpha[5] = nativeAlpha;
  return result;
}

bool ReadTopScreenTouchDynamicState(
    NativeA32Memory &memory, const TopScreenExtendedInputFrame &extendedInput,
    TopScreenTouchDynamicState *state, std::string *error) {
  if (state == nullptr) {
    SetError(error, "TopScreen touch dynamic-state output is null");
    return false;
  }
  *state = {};

  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  constexpr std::array<std::uint32_t, 4> kNativeButtonOffsets{0x4CU, 0x50U,
                                                              0x44U, 0x48U};
  std::array<std::uint32_t, 4> nativeButtons{};
  for (std::size_t index = 0; index < nativeButtons.size(); ++index) {
    if (!memory.Read32(kPauseTouchButtonState + kNativeButtonOffsets[index],
                       &nativeButtons[index])) {
      SetError(error, "cannot read native TopScreen touch-button state");
      return false;
    }
  }
  state->VerticalOffsets = {
      nativeButtons[0] != 0U ? 2.0F : 0.0F,
      nativeButtons[1] != 0U ? 2.0F : 0.0F,
      nativeButtons[2] != 0U || extendedInput.ZrHeld ? 2.0F : 0.0F,
      nativeButtons[3] != 0U || extendedInput.ZlHeld ? 2.0F : 0.0F};

  std::uint32_t renderer = 0U;
  std::uint32_t colorStream = 0U;
  if (!memory.Read32(kPauseTouchButtonState + 4U, &renderer) ||
      renderer == 0U || !memory.Read32(renderer + 0x18U, &colorStream) ||
      colorStream == 0U) {
    SetError(error, "native TopScreen touch color stream is unavailable");
    return false;
  }
  // Quad 34 is one of the native HUD controls copied by the payload and uses
  // the same visibility fade as its newly allocated control groups.
  constexpr std::uint32_t kVisibilitySourceQuad = 34U;
  std::array<float, 16> colors{};
  if (!memory.ReadBytes(colorStream + kVisibilitySourceQuad * 0x40U,
                        std::span<std::uint8_t>(
                            reinterpret_cast<std::uint8_t *>(colors.data()),
                            colors.size() * sizeof(float)))) {
    SetError(error, "cannot read native TopScreen touch visibility color");
    return false;
  }
  state->Alpha = std::clamp(
      (colors[3] + colors[7] + colors[11] + colors[15]) * 0.25F, 0.0F, 1.0F);
  return true;
}

bool ReadTopScreenHudCompositorGate(NativeA32Memory &memory,
                                    TopScreenHudCompositorGate *gate,
                                    std::string *error) {
  if (gate == nullptr) {
    SetError(error, "TopScreen HUD compositor gate output is null");
    return false;
  }
  *gate = {};
  constexpr std::uint32_t kPauseRoot = 0x005043D4U;
  constexpr std::uint32_t kSaveContext = 0x00587958U;
  constexpr std::uint32_t kRuntimeState = 0x00588958U;
  constexpr std::array<std::uint32_t, 4> kCompetingOwnerStates{
      0x0050446CU + 0x18U, 0x005066F8U + 0x34U, 0x00506CB0U + 0x38U,
      0x0050A508U + 0x28U};
  std::uint32_t playState = 0U;
  std::uint8_t stateType = 0U;
  std::uint8_t stateSubtype = 0U;
  std::uint16_t health = 0U;
  std::uint16_t playerTransition = 0U;
  std::uint8_t playerHudSuppressed = 0U;
  std::uint32_t runtimeMode = 0U;
  std::uint32_t alternateHudOwner = 0U;
  if (!memory.Read32(kPauseRoot + 0x0CU, &playState) || playState == 0U ||
      !memory.Read8(playState + 0x100U, &stateType) ||
      !memory.Read8(playState + 0x101U, &stateSubtype) ||
      !memory.Read16(kSaveContext + 0x44U, &health) ||
      !memory.Read16(playState + 0x318CU, &playerTransition) ||
      !memory.Read8(playState + 0x7F40U, &playerHudSuppressed) ||
      !memory.Read32(kRuntimeState + 0x4E4U, &runtimeMode) ||
      !memory.Read32(0x004FC648U + 0x48U, &alternateHudOwner)) {
    SetError(error, "cannot read native TopScreen HUD compositor gate");
    return false;
  }
  std::uint16_t playerHudReady = 0U;
  if (!memory.Read16(playState + 0x2E30U, &playerHudReady)) {
    SetError(error, "cannot read native TopScreen player HUD readiness");
    return false;
  }
  // Original TopScreen 005CE554 also suppresses its compositor while the
  // native player's +224E field is nonzero; a missing player is allowed.
  std::uint32_t player = 0U;
  std::uint16_t playerHudState = 0U;
  if (!memory.Read32(playState + 0x20ACU, &player) ||
      (player != 0U && !memory.Read16(player + 0x224EU, &playerHudState))) {
    SetError(error, "cannot read native TopScreen player HUD state");
    return false;
  }
  for (const auto address : kCompetingOwnerStates) {
    std::uint32_t active = 0U;
    if (!memory.Read32(address, &active)) {
      SetError(error, "cannot read native TopScreen competing UI owner");
      return false;
    }
    if (active != 0U) {
      return true;
    }
  }
  gate->Draw = stateType == 3U && stateSubtype == 2U && health != 0U &&
               playerTransition == 0U && runtimeMode == 0U &&
               (playerHudReady != 0U || alternateHudOwner != 0U) &&
               playerHudSuppressed == 0U && playerHudState == 0U;
  // SingleScreen 3.0 0x005DA570 adds 0x005CC87C: no HUD while a player is
  // present and Gameplay_InCsMode (0x0037577C) holds. The native top-screen
  // HUD already hides then; the lower-screen controls this compositor
  // relocates do not.
  if (gate->Draw && player != 0U) {
    bool cutscene = false;
    if (!ReadTopScreenGameplayInCutsceneMode(memory, playState, player, &cutscene, error))
      return false;
    gate->Draw = !cutscene;
  }
  return true;
}

bool ReadTopScreenGameplayInCutsceneMode(NativeA32Memory &memory, std::uint32_t playState,
                                         std::uint32_t player, bool *cutscene,
                                         std::string *error) {
  using oot3d::gameplay::PlayerPlayStateWire;
  using oot3d::gameplay::PlayerWireState;
  if (cutscene == nullptr) {
    SetError(error, "TopScreen cutscene-mode output is null");
    return false;
  }
  // 0x0037577C: the play cutscene state (+0x2298 +8), else Player_InCsMode
  // (0x0036A7A0; mask and global state from its literals 0x0036A834/38).
  constexpr std::uint32_t kPlayCutsceneState = 0x22A0U;
  constexpr std::uint32_t kPlayerCutsceneBlockingMask = 0x20000080U;
  constexpr std::uint32_t kPlayerCutsceneGlobalState = 0x00588E58U;
  std::uint8_t playCutscene = 0U;
  oot3d::gameplay::PlayerCutsceneModeState state;
  std::uint8_t itemAction = 0U;
  if (!memory.Read8(playState + kPlayCutsceneState, &playCutscene) ||
      !memory.Read32(player + offsetof(PlayerWireState, StateFlags), &state.StateFlags) ||
      !memory.Read8(player + offsetof(PlayerWireState, CutsceneActionMode),
                    &state.CutsceneAction) ||
      !memory.Read8(player + offsetof(PlayerWireState, StateFlagsByte), &state.StateFlagsByte) ||
      !memory.Read8(player + offsetof(PlayerWireState, ItemAction), &itemAction) ||
      !memory.Read8(player + offsetof(PlayerWireState, State1749), &state.State1749) ||
      !memory.Read8(playState + offsetof(PlayerPlayStateWire, State5C2D),
                    &state.PlayState5C2D) ||
      !memory.Read16(kPlayerCutsceneGlobalState + 0x80U, &state.GlobalState80)) {
    SetError(error, "cannot read native TopScreen cutscene mode");
    return false;
  }
  state.ItemAction = static_cast<std::int8_t>(itemAction);
  *cutscene = playCutscene != 0U ||
              oot3d::gameplay::PlayerInCutsceneMode(state, {kPlayerCutsceneBlockingMask});
  return true;
}

bool ReadTopScreenQuestPageDrawSuppressed(NativeA32Memory &memory,
                                          bool runtimeSceneLatch, bool *suppressed,
                                          std::string *error) {
  if (suppressed == nullptr) {
    SetError(error, "TopScreen Quest draw gate output is null");
    return false;
  }
  *suppressed = false;
  // 0x002FCAD4, 0x002FCAEC, 0x002FCDD4 and 0x004392B0, as in 0x005C91D0.
  constexpr std::array<std::uint32_t, 4> kPauseOwnerStates{
      0x0050446CU + 0x18U, 0x005066F8U + 0x34U, 0x00506CB0U + 0x38U,
      0x0050A508U + 0x28U};
  for (const auto address : kPauseOwnerStates) {
    std::uint32_t active = 0U;
    if (!memory.Read32(address, &active)) {
      SetError(error, "cannot read native TopScreen pause owner");
      return false;
    }
    if (active != 0U) {
      *suppressed = true;
      return true;
    }
  }
  // 0x005CEE58 (the latch) and the native byte 0x005C3F71.
  std::uint8_t runtimeGate = 0U;
  if (runtimeSceneLatch && !memory.Read8(0x005C3F71U, &runtimeGate)) {
    SetError(error, "cannot read native TopScreen runtime scene gate");
    return false;
  }
  *suppressed = runtimeGate != 0U;
  return true;
}

bool ApplyTopScreenNativeTouchQuad28Layout(
    NativeA32Memory &memory, TopScreenNativeTouchQuad28LayoutStats *stats,
    std::string *error) {
  TopScreenNativeTouchQuad28LayoutStats result;
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  constexpr std::uint32_t kPayloadTargetQuad = 28U;
  constexpr std::uint32_t kPositionStride = 0x30U;
  constexpr std::uint32_t kVertexStride = 0x0CU;
  constexpr std::uint32_t kLayoutAxisOffset = 4U;
  constexpr float kDefaultLowerEdge = 11.0F;
  constexpr float kAlternateLowerEdge = 3.0F;
  constexpr float kExtent = 48.0F;

  std::uint32_t renderer = 0U;
  std::uint32_t touchState = 0U;
  if (!memory.Read32(kPauseTouchButtonState + 4U, &renderer) ||
      !memory.Read32(kPauseTouchButtonState + 0x34U, &touchState)) {
    SetError(error, "cannot read native TopScreen touch-quad owner");
    return false;
  }
  result.RendererAddress = renderer;
  if (renderer == 0U || touchState == 0U) {
    if (stats != nullptr) {
      *stats = result;
    }
    return true;
  }

  std::uint32_t quadCount = 0U;
  std::uint32_t positions = 0U;
  if (!memory.Read32(renderer, &quadCount) ||
      !memory.Read32(renderer + 0x0CU, &positions)) {
    SetError(error, "cannot read native TopScreen touch-quad renderer");
    return false;
  }
  if (quadCount <= 91U || positions == 0U) {
    if (stats != nullptr) {
      *stats = result;
    }
    return true;
  }
  result.Eligible = true;

  float lowerEdge = kDefaultLowerEdge;
  // 2.1.1 005D6760..6844 tests Play+2DD4 through the root PlayState
  // *(005043D4+C). PauseTouchButton+14 is a separate renderer-side object
  // (its +100/+101/+104/+2DD4 are not PlayState fields).
  constexpr std::uint32_t kPlayStateRoot = 0x005043D4U + 0x0CU;
  std::uint32_t playState = 0U;
  if (!memory.Read32(kPlayStateRoot, &playState)) {
    SetError(error, "cannot read native TopScreen touch-quad PlayState owner");
    return false;
  }
  if (playState != 0U) {
    std::uint16_t playerHudMode = 0U;
    if (!memory.Read16(playState + 0x2DD4U, &playerHudMode)) {
      SetError(error, "cannot read native TopScreen touch-quad HUD mode");
      return false;
    }
    if (playerHudMode != 0U) {
      lowerEdge = kAlternateLowerEdge;
    }
  }
  const float upperEdge = lowerEdge + kExtent;

  const std::uint32_t quadPositions =
      positions + kPayloadTargetQuad * kPositionStride;
  std::array<float, 4> axis{};
  for (std::size_t vertex = 0U; vertex < axis.size(); ++vertex) {
    std::uint32_t bits = 0U;
    if (!memory.Read32(quadPositions +
                           static_cast<std::uint32_t>(vertex) * kVertexStride +
                           kLayoutAxisOffset,
                       &bits)) {
      SetError(error, "cannot read native TopScreen touch-quad vertices");
      return false;
    }
    axis[vertex] = std::bit_cast<float>(bits);
  }
  const auto [minimum, maximum] = std::minmax_element(axis.begin(), axis.end());
  const float center = (*minimum + *maximum) * 0.5F;
  for (std::size_t vertex = 0U; vertex < axis.size(); ++vertex) {
    const float value = axis[vertex] < center ? lowerEdge : upperEdge;
    if (!memory.Write32(quadPositions +
                            static_cast<std::uint32_t>(vertex) * kVertexStride +
                            kLayoutAxisOffset,
                        std::bit_cast<std::uint32_t>(value))) {
      SetError(error, "cannot write native TopScreen touch-quad vertices");
      return false;
    }
  }
  result.Transformed = true;
  result.LowerEdge = lowerEdge;
  result.UpperEdge = upperEdge;
  if (stats != nullptr) {
    *stats = result;
  }
  return true;
}

TopScreenPauseEdgeGeometry BuildTopScreenPauseEdgeGeometry(
    bool hasScene, std::uint8_t sceneMode, std::uint32_t runtimeMode,
    std::uint8_t sceneFade, std::int32_t transitionStep, float nativeVisibility,
    float globalColorScale, bool itemsActive, bool dungeonMapActive) noexcept {
  float visibility = nativeVisibility;
  float sceneScale = 1.0F;
  if (hasScene && sceneMode == 3U) {
    if (runtimeMode == 1U || runtimeMode == 2U) {
      visibility = std::min(static_cast<float>(transitionStep) * 0.05F, 1.0F);
    }
    sceneScale =
        std::max(0.0F, 1.0F - static_cast<float>(sceneFade) * (1.0F / 255.0F));
  }

  const float retraction = (1.0F - visibility) * 32.0F;
  TopScreenPauseEdgeGeometry result;
  result.Positions = {{{288.0F + retraction, 0.0F}, {-retraction, 0.0F}}};
  result.Sizes = {{{itemsActive || dungeonMapActive ? 0.0F : 32.0F, 240.0F},
                   {dungeonMapActive ? 0.0F : 32.0F, 240.0F}}};
  result.AtlasOrigins = {{{192.0F, 0.0F}, {256.0F, 0.0F}}};
  result.AtlasSizes = {{{64.0F, 240.0F}, {-64.0F, 240.0F}}};
  const float payloadScale = sceneScale * globalColorScale * visibility;
  result.RgbScale = payloadScale < 1.0F ? payloadScale : 1.0F;
  return result;
}

std::size_t AppendTopScreenHealthPresentation(
    const oot3d::ui::UiHudContentSnapshot &content,
    const oot3d::ui::UiTextureIdentity &pauseTopPage, std::uint8_t pulsePhase,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  using oot3d::ui::HudVisibilityMode;
  if (pauseTopPage.semantic_name.empty() ||
      !content.health.current_units.IsKnown() ||
      !content.health.capacity_units.IsKnown()) {
    return 0U;
  }
  if (content.visibility.current.IsKnown()) {
    const auto visibility = content.visibility.current.value;
    if (visibility == HudVisibilityMode::HUD_VISIBILITY_NOTHING ||
        visibility == HudVisibilityMode::HUD_VISIBILITY_NOTHING_ALT ||
        visibility == HudVisibilityMode::HUD_VISIBILITY_NOTHING_INSTANT) {
      return 0U;
    }
  }

  const auto health = static_cast<std::uint16_t>(
      std::max<std::int32_t>(0, content.health.current_units.value));
  const bool alternateAtlasRow =
      content.health.double_defense_acquired.IsKnown() &&
      content.health.double_defense_acquired.value;
  const auto geometry =
      BuildTopScreenHealthGeometry(health, content.health.capacity_units.value,
                                   alternateAtlasRow, pulsePhase);
  const std::size_t startSize = output.size();
  const std::size_t visibleHearts = std::min<std::size_t>(
      geometry.Positions.size(),
      (static_cast<std::size_t>(content.health.capacity_units.value) + 15U) /
          16U);
  for (std::size_t index = 0; index < visibleHearts; ++index) {
    const float expansion =
        index == geometry.PulseHeart ? geometry.PulseExpansion : 0.0F;
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::Heart;
    primitive.source_quad = static_cast<std::uint32_t>(index + 4U);
    primitive.texture = pauseTopPage;
    primitive.destination = {geometry.Positions[index].X - expansion,
                             geometry.Positions[index].Y - expansion,
                             geometry.Sizes[index].X + expansion * 2.0F,
                             geometry.Sizes[index].Y + expansion * 2.0F};
    primitive.uv = {geometry.AtlasOrigins[index].X / 512.0F,
                    geometry.AtlasOrigins[index].Y / 256.0F,
                    geometry.AtlasSizes[index].X / 512.0F,
                    geometry.AtlasSizes[index].Y / 256.0F};
    primitive.layer = 10U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

std::size_t AppendTopScreenMagicMeterPresentation(
    const TopScreenMagicMeterGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!geometry.Visible || pauseTopPage.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Positions.size(); ++index) {
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = index == 3U ? oot3d::ui::UiPrimitiveRole::MagicFill
                                 : oot3d::ui::UiPrimitiveRole::MagicFrame;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = pauseTopPage;
    primitive.destination = {geometry.Positions[index].X,
                             geometry.Positions[index].Y,
                             geometry.Sizes[index].X, geometry.Sizes[index].Y};
    primitive.uv = {geometry.AtlasOrigins[index].X / 512.0F,
                    geometry.AtlasOrigins[index].Y / 256.0F,
                    geometry.AtlasSizes[index].X / 512.0F,
                    geometry.AtlasSizes[index].Y / 256.0F};
    primitive.layer = 11U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

std::size_t AppendTopScreenTouchClusterPresentation(
    const TopScreenTouchClusterGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (uiAtlas.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Positions.size(); ++index) {
    const float alpha = std::clamp(geometry.Alpha[index], 0.0F, 1.0F);
    if (alpha == 0.0F) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005C9940U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = uiAtlas;
    primitive.destination = {geometry.Positions[index].X,
                             geometry.Positions[index].Y,
                             geometry.Sizes[index].X, geometry.Sizes[index].Y};
    // The host decoder already normalizes the PICA storage orientation.
    primitive.uv = {geometry.AtlasOrigins[index].X / 512.0F,
                    geometry.AtlasOrigins[index].Y / 256.0F,
                    geometry.AtlasSizes[index].X / 512.0F,
                    geometry.AtlasSizes[index].Y / 256.0F};
    primitive.color.alpha = alpha;
    primitive.layer = 12U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

std::size_t AppendTopScreenPauseEdgePresentation(
    const TopScreenPauseEdgeGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &itemPage,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (itemPage.semantic_name.empty())
    return 0U;
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Positions.size(); ++index) {
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::PauseBackground;
    primitive.owner_address = 0x005CD1E0U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = itemPage;
    // Payload 0x005CD1E0 authors these edge quads in the promoted lower-screen
    // 320x240 domain. The native top target expands that domain to 400x240.
    // Host-output fitting belongs to the renderer presentation contract.
    primitive.destination = {
        geometry.Positions[index].X * kPromotedPauseHorizontalScale,
        geometry.Positions[index].Y,
        geometry.Sizes[index].X * kPromotedPauseHorizontalScale,
        geometry.Sizes[index].Y};
    // UiTextureIdentity payloads have already been decoded into top-down host
    // rows. FUN_005C7230's PICA V inversion belongs to the guest texture
    // upload boundary and must not be repeated by the source presenter.
    primitive.uv = {geometry.AtlasOrigins[index].X / 256.0F,
                    geometry.AtlasOrigins[index].Y / 256.0F,
                    geometry.AtlasSizes[index].X / 256.0F,
                    geometry.AtlasSizes[index].Y / 256.0F};
    primitive.color.red = geometry.RgbScale;
    primitive.color.green = geometry.RgbScale;
    primitive.color.blue = geometry.RgbScale;
    primitive.layer = 14U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

bool ReadTopScreenTabHintInputs(NativeA32Memory &memory, TopScreenTabHintInputs *inputs) {
  if (inputs == nullptr) return false;
  // 3.0 0x005D4A0C: Items 0x0050672C, Gear 0x00504484, Map 0x00506CE8.
  std::uint32_t items = 0U, gear = 0U, map = 0U, held = 0U;
  if (!memory.Read32(0x0050672CU, &items) || !memory.Read32(0x00504484U, &gear) ||
      !memory.Read32(0x00506CE8U, &map) || !memory.Read32(0x005655A0U, &held))
    return false;
  inputs->PageOpen = items != 0U || gear != 0U || map != 0U;
  inputs->LeftHeld = (held & 0x200U) != 0U;
  inputs->RightHeld = (held & 0x100U) != 0U;
  return true;
}

std::size_t AppendTopScreenTabHintPresentation(
    const TopScreenTabHintInputs &inputs, const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!inputs.PageOpen || uiAtlas.semantic_name.empty()) return 0U;
  constexpr float kPageOffsetX = 40.0F;
  const std::size_t startSize = output.size();
  // 0x005D1644 binds both models to custom_ui01. 0x005D4A0C draws the
  // 41x42 backings first, using its (401,196,18,18) crop without dimming.
  for (unsigned index = 0; index < 2; ++index) {
    oot3d::ui::UiPrimitive backing;
    backing.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    backing.role = oot3d::ui::UiPrimitiveRole::PauseBackground;
    backing.owner_address = 0x005D4A0CU;
    backing.source_quad = index;
    backing.texture = uiAtlas;
    backing.destination = {(index == 0 ? 25.5F : 253.5F) + kPageOffsetX,
                           206.0F, 41.0F, 42.0F};
    backing.uv = {401.0F / 512.0F, 196.0F / 256.0F, 18.0F / 512.0F, 18.0F / 256.0F};
    backing.layer = 14U;
    output.push_back(std::move(backing));
  }
  // 0x0060A568 rects 20/21 are upright 37x28 glyphs. The payload grows
  // each by 1.05 around its centre, with +1.5/-1.5 X and +1 Y offsets.
  constexpr float kWidth = 37.0F, kHeight = 28.0F, kScale = 1.05F;
  constexpr float kCentreOffset = (1.0F - kScale) * 0.5F;
  struct Glyph { float X, CropX, CropY; bool Held; };
  unsigned glyphIndex = 2;
  for (const Glyph glyph : {Glyph{62.0F - kWidth + 1.5F, 462.0F, 108.0F, inputs.LeftHeld},
                            Glyph{258.0F - 1.5F, 240.0F, 117.0F, inputs.RightHeld}}) {
    oot3d::ui::UiPrimitive hint;
    hint.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    hint.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    hint.owner_address = 0x005D4A0CU;
    hint.source_quad = glyphIndex++;
    hint.texture = uiAtlas;
    hint.destination = {glyph.X + kWidth * kCentreOffset + kPageOffsetX,
                        209.0F + kHeight * kCentreOffset, kWidth * kScale, kHeight * kScale};
    hint.uv = {glyph.CropX / 512.0F, glyph.CropY / 256.0F, kWidth / 512.0F, kHeight / 256.0F};
    const float rgb = glyph.Held ? 0.55F : 1.0F;
    hint.color = {rgb, rgb, rgb, 1.0F};
    hint.layer = 14U;
    output.push_back(std::move(hint));
  }
  return output.size() - startSize;
}

TopScreenFileSelectStripGeometry
BuildTopScreenFileSelectStripGeometry(bool active) noexcept {
  TopScreenFileSelectStripGeometry geometry;
  geometry.Active = active;
  geometry.Quad.Visible = active;
  geometry.Quad.Position = {66.0F, 212.0F};
  geometry.Quad.Size = {252.0F, 28.0F};
  geometry.Quad.AtlasOrigin = {0.0F, 152.0F};
  geometry.Quad.AtlasSize = {289.0F, 46.0F};
  geometry.Quad.Color = {0.74F, 0.74F, 0.74F, 1.0F};
  return geometry;
}

bool ReadTopScreenFileSelectStripActive(NativeA32Memory &memory, bool *active,
                                        std::string *error) {
  if (active == nullptr) {
    SetError(error, "TopScreen file-select strip output is null");
    return false;
  }
  *active = false;
  constexpr std::uint32_t kPauseRoot = 0x005043D4U;
  constexpr std::uint32_t kRuntimeState = 0x00588958U;
  constexpr std::uint32_t kDisplayState = 0x005C3F00U;
  std::uint32_t scene = 0U;
  std::uint32_t runtimeMode = 0U;
  std::uint8_t displayReadyA = 0U;
  std::uint8_t displayReadyB = 0U;
  if (!memory.Read32(kPauseRoot + 0x0CU, &scene) ||
      !memory.Read32(kRuntimeState + 0x4E4U, &runtimeMode) ||
      !memory.Read8(kDisplayState + 0x61U, &displayReadyA) ||
      !memory.Read8(kDisplayState + 0x62U, &displayReadyB)) {
    SetError(error, "cannot read native file-select strip gate");
    return false;
  }
  *active = scene != 0U && runtimeMode == 4U && displayReadyA != 0U &&
            displayReadyB != 0U;
  return true;
}

std::size_t AppendTopScreenFileSelectStripPresentation(
    const TopScreenFileSelectStripGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &fileSelectAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!geometry.Active || !geometry.Quad.Visible ||
      fileSelectAtlas.semantic_name.empty()) {
    return 0U;
  }
  const auto &quad = geometry.Quad;
  oot3d::ui::UiPrimitive primitive;
  primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
  primitive.role = oot3d::ui::UiPrimitiveRole::PauseText;
  primitive.owner_address = 0x005CD64CU;
  primitive.texture = fileSelectAtlas;
  primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                           quad.Size.Y};
  primitive.uv = {quad.AtlasOrigin.X / 512.0F,
                  1.0F - quad.AtlasOrigin.Y / 256.0F, quad.AtlasSize.X / 512.0F,
                  -quad.AtlasSize.Y / 256.0F};
  primitive.color = quad.Color;
  primitive.layer = 22U;
  output.push_back(std::move(primitive));
  return 1U;
}

TopScreenExtendedItemButtonsGeometry
BuildTopScreenExtendedItemButtonsGeometry(bool itemIActive,
                                          bool itemIIActive) noexcept {
  TopScreenExtendedItemButtonsGeometry result;
  result.Quads[0] = {itemIActive,      {2.0F, 60.0F},  {14.0F, 14.0F},
                     {378.0F, 210.0F}, {42.0F, 42.0F}, {}};
  result.Quads[1] = {itemIIActive,     {44.0F, 60.0F}, {14.0F, 14.0F},
                     {420.0F, 210.0F}, {42.0F, 42.0F}, {}};
  return result;
}

std::size_t AppendTopScreenExtendedItemButtonsPresentation(
    const TopScreenExtendedItemButtonsGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (itemIcons.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Quads.size(); ++index) {
    const auto &quad = geometry.Quads[index];
    if (!quad.Visible) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::ActionButton;
    primitive.owner_address = 0x005C9940U;
    primitive.source_quad = static_cast<std::uint32_t>(index + 5U);
    primitive.texture = itemIcons;
    primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                             quad.Size.Y};
    primitive.uv = {quad.AtlasOrigin.X / 512.0F, quad.AtlasOrigin.Y / 256.0F,
                    quad.AtlasSize.X / 512.0F, quad.AtlasSize.Y / 256.0F};
    primitive.color = quad.Color;
    primitive.layer = 13U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

float ResolveTopScreenDpadGroupAlpha(const TopScreenNativeItemOpacity &lanes,
                                     bool nestedOwner) noexcept {
  // 0x0060D22C, the third lane, has no writer in 3.0.
  const float lane = lanes.ItemZr > 0.0F ? lanes.ItemZr : lanes.ItemZl;
  if (lane > 0.0F) return std::min(lane, 1.0F);
  return nestedOwner ? 0.3F : 1.0F;
}

bool ReadTopScreenPlayerNestedOwner(NativeA32Memory &memory, bool *nestedOwner) {
  // 0x005CCFB0: PlayState 0x005043D4+0xC in state 3/2, Player +0x20AC.
  if (nestedOwner == nullptr) return false;
  *nestedOwner = false;
  std::uint32_t play = 0U, player = 0U, nested = 0U;
  std::uint8_t type = 0U, subtype = 0U;
  if (!memory.Read32(0x005043D4U + 0x0CU, &play)) return false;
  if (play == 0U) return true;
  if (!memory.Read8(play + 0x100U, &type) || !memory.Read8(play + 0x101U, &subtype))
    return false;
  if (type != 3U || subtype != 2U) return true;
  if (!memory.Read32(play + 0x20ACU, &player)) return false;
  if (player == 0U) return true;
  if (!memory.Read32(player + 0x12B8U, &nested)) return false;
  *nestedOwner = nested != 0U;
  return true;
}

bool AppendTopScreenNativeItemIconCopies(
    NativeA32Memory &memory, const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha, const oot3d::ui::UiTextureIdentity &itemIcons,
    std::vector<oot3d::ui::UiPrimitive> &output, std::string *error,
    bool renderDpadIcons, TopScreenNativeItemOpacity *itemOpacity) {
  if (itemIcons.semantic_name.empty()) {
    SetError(error, "native TopScreen item-icon texture is unavailable");
    return false;
  }
  // FUN_005c9940 loads the active PlayState through
  // gPauseTouchButtonState+0x14, not gPauseUiRootState.  Keeping this owner
  // relationship is required because the two globals do not alias during
  // gameplay.
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  std::uint32_t playState = 0U;
  std::uint32_t renderer = 0U;
  if (!memory.Read32(kPauseTouchButtonState + 0x14U, &playState) ||
      playState == 0U || !memory.Read32(playState + 0x10CU, &renderer) ||
      renderer == 0U) {
    SetError(error, "native TopScreen PlayState item renderer is unavailable");
    return false;
  }
  std::array<std::uint32_t, 4> streams{};
  constexpr std::array<std::uint32_t, 4> kStreamOffsets{0x0CU, 0x14U, 0x18U,
                                                        0x1CU};
  for (std::size_t index = 0; index < streams.size(); ++index) {
    if (!memory.Read32(renderer + kStreamOffsets[index], &streams[index]) ||
        streams[index] == 0U) {
      SetError(error, "native TopScreen item geometry stream is unavailable");
      return false;
    }
  }
  const auto readFloats = [&memory](std::uint32_t address, auto &values) {
    return memory.ReadBytes(
        address,
        std::span<std::uint8_t>(reinterpret_cast<std::uint8_t *>(values.data()),
                                values.size() * sizeof(float)));
  };
  const float alphaScale = std::clamp(nativeAlpha, 0.0F, 1.0F);
  TopScreenNativeItemOpacity observedOpacity;
  for (std::uint32_t source = 0U; source < 5U; ++source) {
    std::array<float, 12> positions{};
    std::array<float, 8> uvs{};
    std::array<float, 16> colors{};
    std::array<float, 2> translation{};
    if (!readFloats(streams[0] + source * 0x30U, positions) ||
        !readFloats(streams[1] + source * 0x20U, uvs) ||
        !readFloats(streams[2] + source * 0x40U, colors) ||
        !readFloats(streams[3] + source * 0x08U, translation)) {
      SetError(error, "cannot read a native TopScreen item source quad");
      return false;
    }
    const float centerX = translation[0] + (positions[0] + positions[9]) * 0.5F;
    const float centerY =
        translation[1] + (positions[1] + positions[10]) * 0.5F;
    const NativeItemIconRegion *region = nullptr;
    std::size_t regionIndex = 0U;
    for (; regionIndex < kNativeItemIconRegions.size(); ++regionIndex) {
      const auto &candidate = kNativeItemIconRegions[regionIndex];
      if (centerX >= candidate.MinimumX && centerX <= candidate.MaximumX &&
          centerY >= candidate.MinimumY && centerY <= candidate.MaximumY) {
        region = &candidate;
        break;
      }
    }
    // Original 005D6D70/005D6E1C captures region 0/3 alpha, and the
    // region-4 branch captures ocarina alpha before hiding the source lane.
    if (regionIndex == 0U)
      observedOpacity.ItemZr = colors[3];
    else if (regionIndex == 3U)
      observedOpacity.ItemZl = colors[3];
    else if (regionIndex == 4U)
      observedOpacity.Ocarina = colors[3];
    // Region four is the native item/action lane relocated around the D-pad.
    // The remaining regions are face-button items and stay visible when the
    // independent 2.1.1 D-pad presentation option is disabled.
    if (!renderDpadIcons && regionIndex == 4U) {
      continue;
    }

    const float sourceCenterX =
        region != nullptr ? static_cast<float>(region->SourceCenterX) : centerX;
    const float sourceCenterY =
        region != nullptr ? static_cast<float>(region->SourceCenterY) : centerY;
    const float destinationCenterX =
        region != nullptr ? static_cast<float>(region->DestinationCenterX)
                          : 500.0F;
    const float destinationY =
        (region != nullptr ? static_cast<float>(region->DestinationCenterY)
                           : 300.0F) +
        (regionIndex < nativeVerticalOffsets.size()
             ? nativeVerticalOffsets[regionIndex]
             : 0.0F);
    const float scale = region != nullptr ? region->Scale : 1.0F;
    std::array<TopScreenVec2, 4> transformed{};
    for (std::size_t vertex = 0; vertex < transformed.size(); ++vertex) {
      transformed[vertex] = {
          destinationCenterX +
              (positions[vertex * 3U] + translation[0] - sourceCenterX) * scale,
          destinationY +
              (positions[vertex * 3U + 1U] + translation[1] - sourceCenterY) *
                  scale};
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::ActionButton;
    primitive.owner_address = 0x005C9940U;
    primitive.descriptor_address = renderer;
    primitive.source_quad = source;
    primitive.texture = itemIcons;
    primitive.destination = {transformed[0].X, transformed[0].Y,
                             transformed[1].X - transformed[0].X,
                             transformed[2].Y - transformed[0].Y};
    primitive.uv = NativePicaQuadUvToHost(uvs);
    primitive.color = {colors[0], colors[1], colors[2], colors[3] * alphaScale};
    primitive.layer = 13U;
    primitive.visible = primitive.color.alpha != 0.0F;
    output.push_back(std::move(primitive));
  }
  if (itemOpacity != nullptr)
    *itemOpacity = observedOpacity;
  return true;
}

std::size_t AppendTopScreenQuestBButtonCopiesPresentation(
    std::span<const TopScreenQuestCopiedQuad> copies,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    const oot3d::ui::UiTextureIdentity &numberGlyphs,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  const std::size_t startSize = output.size();
  for (std::size_t index = 0U; index < copies.size(); ++index) {
    const auto &copy = copies[index];
    const auto &texture = copy.NumberGlyphs ? numberGlyphs : itemIcons;
    if (texture.semantic_name.empty()) continue;
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = copy.NumberGlyphs ? oot3d::ui::UiPrimitiveRole::AmmoCounter
                                       : oot3d::ui::UiPrimitiveRole::ActionButton;
    primitive.owner_address = 0x0042B9F4U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = texture;
    primitive.destination = {copy.Positions[0].X, copy.Positions[0].Y,
                             copy.Positions[1].X - copy.Positions[0].X,
                             copy.Positions[2].Y - copy.Positions[0].Y};
    primitive.uv = NativePicaQuadUvToHost(copy.Uvs);
    primitive.color = copy.Color;
    // Above the circles (layer 12, appended earlier), under the B label (13).
    primitive.layer = 12U;
    primitive.visible = primitive.color.alpha != 0.0F;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

bool AppendTopScreenNativeCounters(
    NativeA32Memory &memory, const std::array<float, 4> &nativeVerticalOffsets,
    const oot3d::ui::UiTextureIdentity &numberGlyphs,
    std::vector<oot3d::ui::UiPrimitive> &output, std::string *error,
    const TopScreenUiConfig *config,
    const TopScreenExtendedInputFrame *input) {
  if (numberGlyphs.semantic_name.empty()) {
    SetError(error, "native TopScreen number-glyph texture is unavailable");
    return false;
  }
  constexpr std::uint32_t kSaveContext = 0x00587958U;
  std::uint16_t rupeesRaw = 0U;
  std::uint32_t upgrades = 0U, walletMask = 0U;
  std::uint8_t walletShift = 0U;
  std::uint16_t walletCapacity = 0U;
  if (!memory.Read16(kSaveContext + 0x48U, &rupeesRaw) ||
      !memory.Read32(kSaveContext + 0xB8U, &upgrades) ||
      !memory.Read32(0x0053CBD4U, &walletMask) ||
      !memory.Read8(0x0053CB20U, &walletShift)) {
    SetError(error, "cannot read native TopScreen rupee count");
    return false;
  }
  const std::uint32_t walletIndex =
      ((upgrades & walletMask) >> (walletShift & 31U)) & 3U;
  if (!memory.Read16(0x0053CC04U + walletIndex * 2U, &walletCapacity)) {
    SetError(error, "cannot read native TopScreen wallet capacity");
    return false;
  }
  // SingleScreen 3.0 hub 0x005DE318..0x005DE3C8: the digits start at x 24
  // whatever their count (origin 24 - (3 - n) x 8.5) at y 217; a full wallet
  // (count >= the 0x0053CC04 capacity) takes the next glyph row and an empty
  // one draws at alpha 0.3.
  const auto rupees = static_cast<std::uint32_t>(
      std::max<std::int16_t>(static_cast<std::int16_t>(rupeesRaw), 0));
  const std::uint32_t rupeeDigits = rupees < 10U ? 1U : (rupees < 100U ? 2U : 3U);
  const std::size_t rupeeStart = output.size();
  (void)AppendTopScreenCounterDigits(
      0x005C9940U, kSaveContext + 0x48U, rupees,
      walletCapacity <= rupees ? 5U : 4U,
      24.0F - static_cast<float>(3U - rupeeDigits) * 8.5F, 217.0F, 0.85F,
      numberGlyphs, output);
  for (std::size_t index = rupeeStart; index < output.size(); ++index) {
    output[index].color.alpha = rupees == 0U ? 0.3F : 1.0F;
  }

  // 2.1.1 005D8434..8540 gates the key digits on the root PlayState
  // *(005043D4+C): type 3, subtype 2, scene 3..16, signed key count >= 0.
  // The ammo counters below belong to the touch-button owner
  // PauseTouchButton+14, whose +100/+101/+104 are not PlayState fields: the
  // key gate read through it never passed.
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  std::uint32_t playState = 0U;
  std::uint32_t touchOwner = 0U;
  if (!memory.Read32(0x005043D4U + 0x0CU, &playState) ||
      !memory.Read32(kPauseTouchButtonState + 0x14U, &touchOwner)) {
    SetError(error, "cannot read native TopScreen PlayState");
    return false;
  }
  if (touchOwner == 0U) {
    return true;
  }

  std::uint8_t stateType = 0U;
  std::uint8_t stateSubtype = 0U;
  std::uint16_t sceneId = 0U;
  std::uint16_t mapIndex = 0U;
  std::uint8_t keyCountRaw = 0U;
  if ((playState != 0U &&
       (!memory.Read8(playState + 0x100U, &stateType) ||
        !memory.Read8(playState + 0x101U, &stateSubtype) ||
        !memory.Read16(playState + 0x104U, &sceneId))) ||
      !memory.Read16(kSaveContext + 0x1592U, &mapIndex) ||
      !memory.Read8(kSaveContext + 0xD4U + mapIndex, &keyCountRaw)) {
    SetError(error, "cannot read native TopScreen dungeon-key gate");
    return false;
  }
  const auto keyCount = static_cast<std::int8_t>(keyCountRaw);
  if (stateType == 3U && stateSubtype == 2U && sceneId >= 3U &&
      sceneId <= 16U && keyCount >= 0) {
    (void)AppendTopScreenCounterDigits(
        0x005C9940U, kSaveContext + 0xD4U + mapIndex,
        static_cast<std::uint32_t>(keyCount), 4U, 22.0F, 198.0F, 0.85F,
        numberGlyphs, output);
  }

  constexpr std::array<float, 4> kAmmoX{382.0F, 358.0F, 358.0F, 334.0F};
  constexpr std::array<float, 4> kAmmoY{44.0F, 68.0F, 20.0F, 44.0F};
  constexpr std::array<std::uint8_t, 4> kOffsetLane{2U, 3U, 0U, 1U};
  std::uint32_t activeSlots = 0U;
  if (!memory.Read32(touchOwner + 0x224U, &activeSlots)) {
    SetError(error, "cannot read native TopScreen ammo slot count");
    return false;
  }
  for (std::uint32_t index = 0U; index < 4U; ++index) {
    if (config != nullptr &&
        config->HudLayout == TopScreenHudLayout::Restoration) {
      const bool zrHeld = input != nullptr && input->ZrHeld;
      if ((zrHeld && index >= 2U) || (!zrHeld && index < 2U)) {
        continue;
      }
    }
    std::uint8_t visible = 0U;
    std::uint32_t valueRaw = 0U;
    if (!memory.Read8(touchOwner + 0x435U + index, &visible) ||
        !memory.Read32(touchOwner + 0x32CU + index * 4U, &valueRaw)) {
      SetError(error, "cannot read native TopScreen ammo counter state");
      return false;
    }
    const auto value = static_cast<std::int32_t>(valueRaw);
    if (index + 1U > activeSlots || visible == 0U || value < 0) {
      continue;
    }

    std::uint32_t sourceCounter = 0U;
    if (!memory.Read32(touchOwner + 0x10U + index * 4U, &sourceCounter)) {
      SetError(error, "cannot read native TopScreen source counter");
      return false;
    }
    bool copiedSpecialGeometry = false;
    if (sourceCounter != 0U) {
      std::uint32_t type = 0U;
      std::uint32_t digitCount = 0U;
      std::uint32_t geometry = 0U;
      if (!memory.Read32(sourceCounter, &type) ||
          !memory.Read32(sourceCounter + 4U, &digitCount) ||
          !memory.Read32(sourceCounter + 8U, &geometry)) {
        SetError(error, "cannot read native TopScreen source-counter owner");
        return false;
      }
      if (type != 8U && digitCount != 0U && geometry != 0U) {
        std::array<std::uint32_t, 3> streams{};
        constexpr std::array<std::uint32_t, 3> kStreamOffsets{0x14U, 0x18U, 0x1CU};
        bool streamsAvailable = true;
        for (std::size_t stream = 0; stream < streams.size(); ++stream) {
          streamsAvailable &= memory.Read32(geometry + kStreamOffsets[stream],
                                            &streams[stream]) &&
                              streams[stream] != 0U;
        }
        if (streamsAvailable) {
          const std::uint32_t copiedDigits = std::min(digitCount, 2U);
          for (std::uint32_t digit = 0U; digit < copiedDigits; ++digit) {
            std::array<float, 8> uvs{};
            std::array<float, 16> colors{};
            std::array<float, 2> translation{};
            const auto readFloats = [&memory](std::uint32_t address,
                                              auto &values) {
              return memory.ReadBytes(
                  address, std::span<std::uint8_t>(
                               reinterpret_cast<std::uint8_t *>(values.data()),
                               values.size() * sizeof(float)));
            };
            if (!readFloats(streams[0] + digit * 0x20U, uvs) ||
                !readFloats(streams[1] + digit * 0x40U, colors) ||
                !readFloats(streams[2] + digit * 0x08U, translation)) {
              SetError(error,
                       "cannot read native TopScreen special counter quad");
              return false;
            }
            oot3d::ui::UiPrimitive primitive;
            primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
            primitive.role = oot3d::ui::UiPrimitiveRole::AmmoCounter;
            primitive.owner_address = 0x005C9940U;
            primitive.descriptor_address = sourceCounter;
            primitive.source_quad = digit;
            primitive.texture = numberGlyphs;
            // 2.1.1 FUN_005CD254 retains the destination counter's geometry;
            // only UV, color and per-digit translation come from the source.
            // Copying source positions duplicates the mounted B counter nearby.
            // It scales the destination first (0x005CC5F4) and sets the
            // translation last (0x005C778C), so the HUD scale applied to the
            // whole primitive later must leave the translation unscaled.
            const auto offsetLane = kOffsetLane[index];
            constexpr float counterScale = 0.7F;
            const float hudScale =
                config != nullptr && config->HudScale > 0.0F ? config->HudScale : 1.0F;
            primitive.destination = {
                kAmmoX[index] + (1U - digit) * 10.0F * counterScale +
                    translation[0] / hudScale,
                kAmmoY[index] + 3.0F + nativeVerticalOffsets[offsetLane] +
                    translation[1] / hudScale,
                11.0F * counterScale, kCounterGlyphHeight[0] * counterScale};
            primitive.uv = NativePicaQuadUvToHost(uvs);
            primitive.color = {colors[0], colors[1], colors[2], colors[3]};
            primitive.layer = 14U;
            primitive.visible = primitive.color.alpha != 0.0F;
            output.push_back(std::move(primitive));
          }
          copiedSpecialGeometry = true;
        }
      }
    }
    if (!copiedSpecialGeometry) {
      const auto offsetLane = kOffsetLane[index];
      (void)AppendTopScreenCounterDigits(
          0x005C9940U, touchOwner + 0x32CU + index * 4U,
          static_cast<std::uint32_t>(value), 0U, kAmmoX[index],
          kAmmoY[index] + 3.0F + nativeVerticalOffsets[offsetLane], 0.7F,
          numberGlyphs, output);
    }
  }
  return true;
}

TopScreenTouchLabelsGeometry BuildTopScreenTouchLabelsGeometry(
    const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha, TopScreenHudLayout layout) noexcept {
  const float alpha =
      layout == TopScreenHudLayout::Restoration
          ? 0.0F
          : std::clamp(nativeAlpha * 0.9F, 0.0F, 1.0F);
  // SingleScreen 3.0 hub 0x005DD0D8/0x005DD168: ZR at (364, lane 2 + 31)
  // with custom_ui01 rect 15 (47,231), ZL at (340, lane 3 + 55) with rect 14
  // (245,227), both 16 px.
  TopScreenTouchLabelsGeometry result;
  result.Quads[0] = {alpha != 0.0F,  {364.0F, nativeVerticalOffsets[2] + 31.0F},
                     {16.0F, 16.0F}, {47.0F, 231.0F},
                     {16.0F, 16.0F}, {1.0F, 1.0F, 1.0F, alpha}};
  result.Quads[1] = {alpha != 0.0F,  {340.0F, nativeVerticalOffsets[3] + 55.0F},
                     {16.0F, 16.0F}, {245.0F, 227.0F},
                     {16.0F, 16.0F}, {1.0F, 1.0F, 1.0F, alpha}};
  return result;
}

std::size_t AppendTopScreenTouchLabelsPresentation(
    const TopScreenTouchLabelsGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (uiAtlas.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Quads.size(); ++index) {
    const auto &quad = geometry.Quads[index];
    if (!quad.Visible) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005C9940U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = uiAtlas;
    primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                             quad.Size.Y};
    primitive.uv = {quad.AtlasOrigin.X / 512.0F, quad.AtlasOrigin.Y / 256.0F,
                    quad.AtlasSize.X / 512.0F, quad.AtlasSize.Y / 256.0F};
    primitive.color = quad.Color;
    primitive.layer = 13U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

bool ReadTopScreenAuxiliaryTouchInputs(NativeA32Memory &memory,
                                       TopScreenAuxiliaryTouchInputs *inputs,
                                       std::string *error) {
  if (inputs == nullptr) {
    SetError(error, "TopScreen auxiliary-touch input output is null");
    return false;
  }
  *inputs = {};

  constexpr std::uint32_t kPauseRoot = 0x005043D4U;
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  constexpr std::uint32_t kAlternateHudOwner = 0x004FC648U;
  std::uint32_t playState = 0U;
  std::uint32_t alternateRenderer = 0U;
  if (!memory.Read32(kPauseRoot + 0x0CU, &playState) ||
      !memory.Read32(kPauseTouchButtonState + 0x34U, &inputs->PauseState) ||
      !memory.Read32(kPauseTouchButtonState + 0x70U,
                     &inputs->TouchLayoutState) ||
      !memory.Read32(kAlternateHudOwner + 0x40U, &alternateRenderer)) {
    SetError(error, "cannot read native TopScreen auxiliary-touch state");
    return false;
  }
  inputs->AlternateHudRendererActive = alternateRenderer != 0U;
  // Read the actor only when the earlier touch/Navi alternatives do not win.
  const auto readViewActor = [&]() {
    if (inputs->TouchLayoutState == 1U ||
        (inputs->HasPlayState && inputs->StateType == 3U &&
         inputs->StateSubtype == 2U && inputs->PlayerHudMode != 0)) return true;
    // 005D91FC..9220: 20x10 UV(128,56,48,24), extra X +1.
    std::uint32_t viewActor = 0U;
    std::uint16_t viewActorMode = 0U;
    if (!memory.Read32(0x0051B2F4U, &viewActor) ||
        (viewActor != 0U && !memory.Read16(viewActor + 0x4B2U, &viewActorMode))) {
      SetError(error, "cannot read native TopScreen View variant");
      return false;
    }
    inputs->ViewActorMode20 = viewActor != 0U && viewActorMode == 0x20U;
    return true;
  };
  if (playState == 0U) return readViewActor();

  inputs->HasPlayState = true;
  if (!memory.Read8(playState + 0x100U, &inputs->StateType) ||
      !memory.Read8(playState + 0x101U, &inputs->StateSubtype)) {
    SetError(error, "cannot read native TopScreen auxiliary PlayState mode");
    return false;
  }
  if (inputs->StateType == 3U) {
    std::uint32_t sceneOwner = 0U;
    if (!memory.Read32(playState + 0x20ACU, &sceneOwner)) {
      SetError(error, "cannot read native TopScreen nested scene owner");
      return false;
    }
    if (sceneOwner != 0U) {
      std::uint32_t nestedOwner = 0U;
      if (!memory.Read32(sceneOwner + 0x12B8U, &nestedOwner)) {
        SetError(error, "cannot read native TopScreen nested HUD owner");
        return false;
      }
      inputs->NestedSceneOwnerActive = nestedOwner != 0U;
    }
  }
  if (inputs->StateType == 3U && inputs->StateSubtype == 2U) {
    std::uint16_t playerHudMode = 0U;
    if (!memory.Read16(playState + 0x2DD4U, &playerHudMode)) {
      SetError(error, "cannot read native TopScreen player HUD mode");
      return false;
    }
    inputs->PlayerHudMode = static_cast<std::int16_t>(playerHudMode);
  }
  return readViewActor();
}

bool AdvanceTopScreenViewAlpha(NativeA32Memory &memory, float *alpha) {
  if (alpha == nullptr) return false;
  std::uint32_t mode = 0U, play = 0U, player = 0U;
  if (!memory.Read32(0x0050AF68U, &mode) ||
      !memory.Read32(0x005043E0U, &play)) return false;
  float target = 1.0F;
  if (mode == 2U && play != 0U) {
    std::uint8_t type = 0U, subtype = 0U;
    if (!memory.Read8(play + 0x100U, &type) ||
        !memory.Read8(play + 0x101U, &subtype)) return false;
    if (type == 3U && subtype == 2U) {
      if (!memory.Read32(play + 0x20ACU, &player)) return false;
      if (player != 0U) {
        std::uint32_t bits = 0U;
        if (!memory.Read32(player + 0x6CU, &bits)) return false;
        if (std::bit_cast<float>(bits) > 0.1F) target = 0.72F;
      }
    }
  }
  // 005D5204..5218. No per-icon blink or 0.9 multiplier.
  *alpha += (target - *alpha) * 0.2F;
  return true;
}

TopScreenAuxiliaryTouchGeometry BuildTopScreenAuxiliaryTouchGeometry(
    const TopScreenAuxiliaryTouchInputs &inputs,
    const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha, TopScreenHudLayout layout) noexcept {
  constexpr std::array<float, 4> kCentersX{358.0F, 334.0F, 382.0F, 358.0F};
  constexpr std::array<float, 4> kCentersY{20.0F, 44.0F, 44.0F, 68.0F};
  // SingleScreen 3.0 hub 0x005DA708 label model (rects from table
  // 0x00609808): X rect 10 (207,227), Y rect 11 (226,227), B rect 9
  // (363,225) of custom_ui01; the fourth label is never drawn (II, rect 13).
  constexpr std::array<TopScreenVec2, 4> kLabelAtlas{
      {{207.0F, 227.0F}, {226.0F, 227.0F}, {363.0F, 225.0F}, {188.0F, 227.0F}}};
  const float auxiliaryAlpha = std::clamp(nativeAlpha * 0.9F, 0.0F, 1.0F);
  const bool conditionalVisible = inputs.SuppressBCopies;

  TopScreenAuxiliaryTouchGeometry result;
  for (std::size_t index = 0U; index < 4U; ++index) {
    auto &quad = result.Quads[index];
    quad.Visible = auxiliaryAlpha != 0.0F &&
                   (index < 2U || (index == 2U && conditionalVisible));
    quad.Position = {500.0F, 300.0F};
    quad.Size = {16.0F, 16.0F};
    quad.AtlasOrigin = kLabelAtlas[index];
    quad.AtlasSize = {16.0F, 16.0F};
    quad.Color = {1.0F, 1.0F, 1.0F, auxiliaryAlpha};
    if (index < 2U) {
      quad.Position = {kCentersX[index] - 20.0F,
                       kCentersY[index] - 14.0F + nativeVerticalOffsets[index]};
    } else if (index == 2U && conditionalVisible) {
      quad.Position =
          layout == TopScreenHudLayout::Restoration
              ? TopScreenVec2{337.0F, 54.0F}
              : TopScreenVec2{227.0F, 10.0F};
    }
  }

  auto &layoutQuad = result.Quads[4];
  const bool hiddenMode =
      (inputs.PauseState >= 12U && inputs.PauseState <= 19U) ||
      inputs.PauseState == 23U;
  const float viewAlpha = hiddenMode ? 0.0F : std::clamp(nativeAlpha, 0.0F, 1.0F);
  layoutQuad.Visible = viewAlpha > 0.0F;
  layoutQuad.Color = {1.0F, 1.0F, 1.0F, viewAlpha};
  float extraX = 0.0F;
  // Variant priority is touch+70 == 1, then Navi, then actor mode / eye.
  // SingleScreen 3.0 hub 0x005DD200..0x005DD6C8 draws them from custom_ui01:
  // camera rect 6 (320,198,36,21) 10 px high at its own aspect, Navi rect 5
  // (199,3,48,48) 14 px, eye rect 4 (451,73,48,32) 16x10. The native
  // menu_top_parts00 versions carry VIEW/BACK captions that 2.1.1 erased.
  constexpr float kCameraWidth = 10.0F * 36.0F / 21.0F;
  if (inputs.TouchLayoutState == 1U) {
    layoutQuad.Size = {kCameraWidth, 10.0F};
    layoutQuad.AtlasOrigin = {320.0F, 198.0F};
    layoutQuad.AtlasSize = {36.0F, 21.0F};
  } else if (inputs.HasPlayState && inputs.StateType == 3U &&
             inputs.StateSubtype == 2U && inputs.PlayerHudMode != 0) {
    layoutQuad.Size = {14.0F, 14.0F};
    layoutQuad.AtlasOrigin = {199.0F, 3.0F};
    layoutQuad.AtlasSize = {48.0F, 48.0F};
  } else if (inputs.ViewActorMode20) {
    layoutQuad.Size = {kCameraWidth, 10.0F};
    layoutQuad.AtlasOrigin = {320.0F, 198.0F};
    layoutQuad.AtlasSize = {36.0F, 21.0F};
    extraX = 1.0F;
  } else {
    layoutQuad.Size = {16.0F, 10.0F};
    layoutQuad.AtlasOrigin = {451.0F, 73.0F};
    layoutQuad.AtlasSize = {48.0F, 32.0F};
  }
  // Base center, not the Up lane: singleton Navi has no lane correction.
  // AppendTopScreenDpadPresentation adds {1,2,0,0} only for mapped lanes.
  layoutQuad.Position = {30.0F - layoutQuad.Size.X * 0.5F + extraX,
                         45.0F - layoutQuad.Size.Y * 0.5F};
  return result;
}

std::size_t AppendTopScreenAuxiliaryTouchPresentation(
    const TopScreenAuxiliaryTouchGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (uiAtlas.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0U; index < geometry.Quads.size(); ++index) {
    const auto &quad = geometry.Quads[index];
    if (!quad.Visible || quad.Color.alpha == 0.0F) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005C9940U;
    primitive.source_quad = static_cast<std::uint32_t>(34U + index);
    primitive.texture = uiAtlas;
    primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                             quad.Size.Y};
    primitive.uv = {quad.AtlasOrigin.X / 512.0F, quad.AtlasOrigin.Y / 256.0F,
                    quad.AtlasSize.X / 512.0F, quad.AtlasSize.Y / 256.0F};
    primitive.color = quad.Color;
    primitive.layer = 13U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

bool BeginTopScreenTouchCopyUpdate(NativeA32Memory &memory,
                                  TopScreenTouchCopyState *state) {
  if (state == nullptr) return false;
  std::uint32_t mode = 0U, alternate = 0U;
  if (!memory.Read32(0x0050AF68U, &mode) ||
      !memory.Read32(0x004FC688U, &alternate)) return false;
  // 005D4AAC..4AE0: consume the OLD latch BEFORE input transactions.
  state->SuppressBCopies =
      (state->PreviousNestedOwnerActive || mode == 11U) && alternate != 0U;
  return true;
}

bool FinishTopScreenTouchCopyUpdate(NativeA32Memory &memory,
                                   bool retainPrevious,
                                   TopScreenTouchCopyState *state) {
  if (state == nullptr) return false;
  if (retainPrevious) return true;
  // 005D51BC / 005D5340 clear outside gameplay; 005D5778..57A0 samples
  // the player only after input routing. Null player also clears the latch.
  std::uint32_t play = 0U, player = 0U, nested = 0U;
  std::uint8_t type = 0U, subtype = 0U;
  if (!memory.Read32(0x005043E0U, &play)) return false;
  if (play != 0U) {
    if (!memory.Read8(play + 0x100U, &type) ||
        !memory.Read8(play + 0x101U, &subtype)) return false;
    if (type == 3U && subtype == 2U) {
      if (!memory.Read32(play + 0x20ACU, &player)) return false;
      if (player != 0U && !memory.Read32(player + 0x12B8U, &nested))
        return false;
    }
  }
  state->PreviousNestedOwnerActive = nested != 0U;
  return true;
}

bool AppendTopScreenNativeTouchCopies(
    NativeA32Memory &memory, const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output,
    TopScreenNativeTouchCopyStats *stats, std::string *error,
    const TopScreenUiConfig *config, bool suppressBCopies) {
  TopScreenNativeTouchCopyStats result;
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  std::uint32_t touchState = 0U;
  std::uint32_t play = 0U;
  if (!memory.Read32(kPauseTouchButtonState + 0x34U, &touchState) ||
      !memory.Read32(0x005043D4U + 0x0CU, &play)) {
    SetError(error, "cannot read native TopScreen touch-copy visibility state");
    return false;
  }
  // 005D6890..005D6910 / 005D6990..005D69B4: source 27 has a
  // scene/save gate independent of its source alpha and off-screen translation.
  bool source27Visible = false;
  if (play != 0U) {
    std::uint8_t type = 0U, subtype = 0U;
    std::uint16_t scene = 0U;
    if (!memory.Read8(play + 0x100U, &type) ||
        !memory.Read8(play + 0x101U, &subtype) ||
        !memory.Read16(play + 0x104U, &scene)) {
      SetError(error, "cannot read native TopScreen touch-copy scene");
      return false;
    }
    if (type == 3U && subtype == 2U && scene >= 3U && scene <= 16U) {
      std::uint16_t index = 0U;
      std::uint8_t value = 0U;
      if (!memory.Read16(0x00587958U + 0x1592U, &index) ||
          !memory.Read8(0x00587958U + 0xD4U + index, &value)) {
        SetError(error, "cannot read native TopScreen touch-copy save flag");
        return false;
      }
      source27Visible = (value & 0x80U) == 0U;
    }
  }
  std::uint32_t renderer = 0U;
  std::array<std::uint32_t, 4> streams{};
  if (!memory.Read32(kPauseTouchButtonState + 4U, &renderer) ||
      renderer == 0U) {
    SetError(error, "native PauseTouchButton renderer is unavailable");
    return false;
  }
  constexpr std::array<std::uint32_t, 4> kStreamOffsets{0x0CU, 0x14U, 0x18U,
                                                        0x1CU};
  for (std::size_t index = 0; index < streams.size(); ++index) {
    if (!memory.Read32(renderer + kStreamOffsets[index], &streams[index]) ||
        streams[index] == 0U) {
      SetError(error, "native PauseTouchButton geometry stream is unavailable");
      return false;
    }
  }

  const auto readFloats = [&memory](std::uint32_t address, auto &values) {
    return memory.ReadBytes(
        address,
        std::span<std::uint8_t>(reinterpret_cast<std::uint8_t *>(values.data()),
                                values.size() * sizeof(float)));
  };
  for (std::size_t contractIndex = 0U;
       contractIndex < kNativeTouchCopyContracts.size(); ++contractIndex) {
    const auto &contract = kNativeTouchCopyContracts[contractIndex];
    const std::uint32_t source = contract.SourceQuad;
    std::array<float, 12> positions{};
    std::array<float, 8> uvs{};
    std::array<float, 16> colors{};
    std::array<float, 2> translation{};
    if (!readFloats(streams[0] + source * 0x30U, positions) ||
        !readFloats(streams[1] + source * 0x20U, uvs) ||
        !readFloats(streams[2] + source * 0x40U, colors) ||
        !readFloats(streams[3] + source * 0x08U, translation)) {
      SetError(error, "cannot read a native PauseTouchButton source quad");
      return false;
    }
    ++result.SourceQuadsRead;
    const bool horseStamina = source >= 86U && source <= 91U;
    if (horseStamina) {
      ++result.HorseStaminaQuadsRead;
    }

    std::array<TopScreenVec2, 4> transformed{};
    const bool restoration =
        config != nullptr &&
        config->HudLayout == TopScreenHudLayout::Restoration;
    const float destinationX =
        restoration && contractIndex < 2U
            ? 339.0F
            : static_cast<float>(contract.DestinationX);
    const float destinationY =
        restoration && contractIndex < 2U
            ? 55.0F
            : static_cast<float>(contract.DestinationY);
    for (std::size_t vertex = 0; vertex < transformed.size(); ++vertex) {
      transformed[vertex] = {
          destinationX +
              ((positions[vertex * 3U] + translation[0]) -
               static_cast<float>(contract.SourceCenterX)) *
                  contract.Scale,
          destinationY +
              ((positions[vertex * 3U + 1U] + translation[1]) -
               static_cast<float>(contract.SourceCenterY)) *
                  contract.Scale};
    }
    if (source >= 86U) {
      uvs[5] += 1.0F / 256.0F;
      uvs[7] += 1.0F / 256.0F;
    }

    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = horseStamina
                         ? oot3d::ui::UiPrimitiveRole::HorseStamina
                         : oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005D4A8CU;
    primitive.descriptor_address = renderer;
    primitive.source_quad = source;
    primitive.texture = pauseTopPage;
    primitive.destination = {transformed[0].X, transformed[0].Y,
                             transformed[1].X - transformed[0].X,
                             transformed[2].Y - transformed[0].Y};
    // 005D6914..6948 collapses only the first two copies (B label/icon).
    const bool suppressed = contractIndex < 2U && suppressBCopies;
    if (suppressed) primitive.destination = {500.0F, 300.0F, 0.0F, 0.0F};
    primitive.uv = NativePicaQuadUvToHost(uvs);
    primitive.color = {colors[0], colors[1], colors[2], colors[3]};
    // 005D6C50..005D6C88: do not expose stale contextual quads while
    // their native owner is in another mode. Keep source memory untouched.
    if ((contractIndex < 2U &&
         ((touchState >= 7U && touchState <= 9U) || touchState > 11U)) ||
        (source == 27U && !source27Visible)) {
      primitive.color.alpha = 0.0F;
    }
    primitive.layer = 13U;
    primitive.visible = !suppressed && primitive.color.alpha != 0.0F;
    if (primitive.visible) {
      ++result.AlphaVisiblePrimitives;
      if (horseStamina) {
        ++result.AlphaVisibleHorseStaminaPrimitives;
      }
    }
    output.push_back(std::move(primitive));
    ++result.PrimitivesEmitted;
  }
  if (stats != nullptr) {
    *stats = result;
  }
  return true;
}

void ApplyTopScreenGameplayCanvas(
    std::span<oot3d::ui::UiPrimitive> primitives) noexcept {
  const auto clipAxis = [](float minimum, float maximum, float *position,
                           float *extent, float *uvPosition,
                           float *uvExtent) noexcept {
    const float originalPosition = *position;
    const float originalExtent = *extent;
    if (originalExtent <= 0.0F) {
      return false;
    }
    const float clippedStart = std::max(originalPosition, minimum);
    const float clippedEnd =
        std::min(originalPosition + originalExtent, maximum);
    if (clippedEnd <= clippedStart) {
      return false;
    }
    const float startRatio = (clippedStart - originalPosition) / originalExtent;
    const float clippedRatio = (clippedEnd - clippedStart) / originalExtent;
    *position = clippedStart;
    *extent = clippedEnd - clippedStart;
    *uvPosition += *uvExtent * startRatio;
    *uvExtent *= clippedRatio;
    return true;
  };

  for (auto &primitive : primitives) {
    if (!primitive.visible ||
        !clipAxis(0.0F, kNativeTopScreenWidth, &primitive.destination.x,
                  &primitive.destination.width, &primitive.uv.x,
                  &primitive.uv.width) ||
        !clipAxis(0.0F, kNativeTopScreenHeight, &primitive.destination.y,
                  &primitive.destination.height, &primitive.uv.y,
                  &primitive.uv.height)) {
      primitive.visible = false;
      continue;
    }
  }
}

namespace {
bool IsTopScreenDpadPrimitive(const oot3d::ui::UiPrimitive &primitive) noexcept {
  // The D-pad presentation (2.1.1 0x005D4A8C action buttons; that owner's
  // native touch copies, the rupee and key icons among them, are touch
  // controls) and the touch-cluster cross, quad 5 (layer 12; the touch labels
  // share owner and indices on layer 13).
  return (primitive.owner_address == 0x005D4A8CU &&
          primitive.role == oot3d::ui::UiPrimitiveRole::ActionButton) ||
         (primitive.owner_address == 0x005C9940U &&
          primitive.role == oot3d::ui::UiPrimitiveRole::TouchControl &&
          primitive.layer == 12U && primitive.source_quad == 5U);
}
} // namespace

TopScreenButtonGroupPlacement ResolveTopScreenButtonGroupPlacement(
    const TopScreenUiConfig &config) noexcept {
  const float placementScale = config.HudScale > 0.0F ? config.HudScale : 1.0F;
  TopScreenButtonGroupPlacement result;
  result.Scale = static_cast<float>(config.ButtonGroupSizePercent) * 0.01F;
  result.OffsetX =
      std::clamp(static_cast<float>(config.ButtonGroupOffsetX) * placementScale,
                 -20.0F, 10.0F) / placementScale;
  result.OffsetY =
      std::clamp(static_cast<float>(config.ButtonGroupOffsetY) * placementScale,
                 -10.0F, 40.0F) / placementScale;
  return result;
}

void ApplyTopScreenHudScale(
    std::span<oot3d::ui::UiPrimitive> primitives,
    const TopScreenUiConfig &config) noexcept {
  const float scale = config.HudScale;
  const float marginX = static_cast<float>(config.HudMarginX);
  const float marginY = static_cast<float>(config.HudMarginY);
  // SingleScreen 3.0 places the D-pad and the button group before the HUD
  // scale. D-pad, helpers 0x005CA63C / 0x005CA66C: (30, 67) + (p - (30, 67))
  // x size + offset; size modopts+0x2C (hub 0x005DB50C), offset +0x2A/+0x2B.
  // Button group, 0x005C84B8: the X/Y/ZR/ZL cluster scales about the
  // top-right corner like the HUD scale, and its offset (+0x2E/+0x2F) moves it
  // by offset x HUD
  // scale, which the hub (0x005DB6B4..0x005DB75C) clamps to [-20, 10] and
  // [-10, 40] px.
  const float dpadSize = static_cast<float>(config.DpadSizePercent) * 0.01F;
  const float dpadX = static_cast<float>(config.DpadOffsetX);
  const float dpadY = static_cast<float>(config.DpadOffsetY);
  const auto group = ResolveTopScreenButtonGroupPlacement(config);
  const float groupSize = group.Scale;
  const float groupX = group.OffsetX;
  const float groupY = group.OffsetY;
  const bool placed = dpadSize != 1.0F || dpadX != 0.0F || dpadY != 0.0F ||
                      groupSize != 1.0F || groupX != 0.0F || groupY != 0.0F;
  if (!placed && scale == 1.0F && marginX == 0.0F && marginY == 0.0F &&
      config.MagicBarY == 0U) {
    return;
  }

  for (auto &primitive : primitives) {
    auto &destination = primitive.destination;
    const float centerX = destination.x + destination.width * 0.5F;
    const float centerY = destination.y + destination.height * 0.5F;
    if (centerX > 430.0F || centerY > 280.0F) {
      continue;
    }

    const bool left = centerX < 200.0F;
    const bool top = centerY < 120.0F;
    // 2.1.1 005CE554 scales copied stamina quads 28..33 as one row
    // about (200, 240), not as independent left/right HUD groups.
    const bool stamina = primitive.role == oot3d::ui::UiPrimitiveRole::HorseStamina;
    if (IsTopScreenDpadPrimitive(primitive)) {
      destination.x = 30.0F + (destination.x - 30.0F) * dpadSize + dpadX;
      destination.y = 67.0F + (destination.y - 67.0F) * dpadSize + dpadY;
      destination.width *= dpadSize;
      destination.height *= dpadSize;
    } else if (centerX > 300.0F && centerY < 90.0F && !stamina) {
      // 0x005C84B8 moves the native button quads centred in X (330, 398)
      // above y 90 (PICA y 150), and the hub moves their labels with them:
      // X, Y, ZR and ZL with items, counts and labels (the Y label is centred
      // at 322). A (248) and B (237) keep their place.
      destination.x = 400.0F + (destination.x - 400.0F) * groupSize + groupX;
      destination.y = destination.y * groupSize + groupY;
      destination.width *= groupSize;
      destination.height *= groupSize;
    }
    // Its pivot literals 005CF558/005CF55C are (200, 240), with no margins.
    const float pivotX = stamina ? 200.0F : (left ? 0.0F : 400.0F);
    const float pivotY = stamina ? 240.0F : (top ? 0.0F : 240.0F);
    const float insetX = stamina ? 0.0F : (left ? marginX : -marginX);
    const float insetY = stamina ? 0.0F : (top ? marginY : -marginY);
    destination.x =
        pivotX + (destination.x - pivotX) * scale + insetX;
    destination.y =
        pivotY + (destination.y - pivotY) * scale + insetY;
    if (primitive.role == oot3d::ui::UiPrimitiveRole::MagicFrame ||
        primitive.role == oot3d::ui::UiPrimitiveRole::MagicFill) {
      destination.y += static_cast<float>(config.MagicBarY);
    }
    destination.width *= scale;
    destination.height *= scale;
  }
}

bool RelocateTopScreenTimerQuad(std::array<TopScreenVec3, 4> &quad,
                               float hudScale) noexcept {
  float centerY = 0.0F;
  for (const auto &vertex : quad)
    centerY += vertex.Y * 0.25F;
  if (centerY <= 205.0F)
    return false;
  const float offsetY = 18.0F + (hudScale - 0.6F) * 30.0F;
  for (auto &vertex : quad) {
    vertex.X -= 9.0F;
    vertex.Y -= offsetY;
  }
  return true;
}

bool ApplyTopScreenTimerCounterLayout(NativeA32Memory &memory, float hudScale,
                                     std::string *error) {
  constexpr std::uint32_t kTimerCounterBinding = 0x004FC674U;
  std::uint32_t counter = 0U, renderer = 0U, digits = 0U, count = 0U,
                positions = 0U;
  if (!memory.Read32(kTimerCounterBinding, &counter)) {
    SetError(error, "cannot read TopScreen timer counter binding");
    return false;
  }
  if (counter == 0U)
    return true;
  if (!memory.Read32(counter + 8U, &renderer) ||
      !memory.Read32(counter + 4U, &digits)) {
    SetError(error, "cannot read TopScreen timer counter");
    return false;
  }
  if (renderer == 0U || digits == 0U)
    return true;
  if (!memory.Read32(renderer, &count) ||
      !memory.Read32(renderer + 0x0CU, &positions) || count > 0xFFU) {
    SetError(error, "invalid TopScreen timer geometry");
    return false;
  }
  if (count == 0U)
    return true;
  std::vector<std::array<TopScreenVec3, 4>> quads(std::min(digits, count));
  auto bytes = std::as_writable_bytes(std::span(quads));
  if (positions == 0U || !memory.IsWritable(positions, bytes.size()) ||
      !memory.ReadBytes(positions,
          {reinterpret_cast<std::uint8_t *>(bytes.data()), bytes.size()})) {
    SetError(error, "cannot read TopScreen timer positions");
    return false;
  }
  bool changed = false;
  for (auto &quad : quads)
    changed |= RelocateTopScreenTimerQuad(quad, hudScale);
  if (changed && !memory.WriteBytes(positions,
          {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()})) {
    SetError(error, "cannot write TopScreen timer positions");
    return false;
  }
  return true;
}

} // namespace Oot3dNativeGame
