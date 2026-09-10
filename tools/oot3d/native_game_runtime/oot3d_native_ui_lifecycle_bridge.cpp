#include "oot3d_native_ui_lifecycle_bridge.h"

#include "oot3d_native_a32_input.h"
#include "oot3d_top_screen_items_hint_consumer.h"
#include "oot3d_top_screen_mod_profile.h"
#include "oot3d_ui/ui_gameplay_hud_inline_semantics.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kRendererBuiltinCtxbSources = 0x0055B490U;
constexpr uint32_t kCtxbRuntimeTextureGuestAddressOffset = 0x4CU;
constexpr std::string_view kPauseSharedSemanticPrefix =
    "oot3d/native/pause_shared/";

class NativeA32UiMemoryReader final : public oot3d::ui::GuestMemoryReader {
public:
  explicit NativeA32UiMemoryReader(const NativeA32Memory &memory) noexcept
      : mMemory(memory) {}

  bool Read(uint32_t address, void *destination,
            std::size_t size) const noexcept override {
    if (destination == nullptr && size != 0U) {
      return false;
    }
    return mMemory.ReadBytes(
        address, std::span<uint8_t>(static_cast<uint8_t *>(destination), size));
  }

private:
  const NativeA32Memory &mMemory;
};

std::size_t SeamPhaseIndex(oot3d::ui::UiSeamPhase phase) noexcept {
  return static_cast<std::size_t>(phase);
}

bool RouteGuestForPhase(oot3d::ui::UiSeamPhase phase,
                        const oot3d::ui::UiFramePlan &plan,
                        oot3d::ui::UiNativeDisposition disposition) noexcept {
  if (disposition != oot3d::ui::UiNativeDisposition::Replaceable) {
    return true;
  }
  switch (phase) {
  case oot3d::ui::UiSeamPhase::Update:
  case oot3d::ui::UiSeamPhase::Initialize:
  case oot3d::ui::UiSeamPhase::Reset:
  case oot3d::ui::UiSeamPhase::Helper:
    return plan.run_guest_mechanics;
  case oot3d::ui::UiSeamPhase::Draw:
  case oot3d::ui::UiSeamPhase::Submit:
  case oot3d::ui::UiSeamPhase::Composite:
    return plan.run_guest_presentation;
  case oot3d::ui::UiSeamPhase::Input:
    return plan.forward_input_to_guest;
  case oot3d::ui::UiSeamPhase::Action:
  case oot3d::ui::UiSeamPhase::Query:
    return true;
  }
  return true;
}

} // namespace

bool BeginsNativeGameplayUiPresentation(
    const NativeUiLifecycleObservation &observation) noexcept {
  if (!observation.matched || observation.contract == nullptr ||
      observation.contract->subsystem != oot3d::ui::UiSubsystem::GameplayHud) {
    return false;
  }
  const auto phase = observation.contract->phase;
  const bool presentationPhase = phase == oot3d::ui::UiSeamPhase::Draw ||
                                 phase == oot3d::ui::UiSeamPhase::Submit ||
                                 phase == oot3d::ui::UiSeamPhase::Composite;
  const bool ownsPresentation =
      (observation.contract->responsibilities &
       oot3d::ui::UiResponsibilityBit(
           oot3d::ui::UiResponsibility::Presentation)) != 0U;
  return presentationPhase && ownsPresentation;
}

Oot3dNativeUiLifecycleBridge::Oot3dNativeUiLifecycleBridge(
    NativeA32Memory &memory)
    : mMemory(memory), mRoots(oot3d::ui::BuildVerifiedOot3dUiGuestRoots(
                           oot3d::ui::kOot3dSaveContextAddress)),
      mRouter(mRuntime) {}

void Oot3dNativeUiLifecycleBridge::BeginHostFrame(uint32_t hostFrame) noexcept {
  ++mHostFrameGeneration;
  mStats.current_host_frame = hostFrame;
}

void Oot3dNativeUiLifecycleBridge::BeginObservationFrameIfNeeded() noexcept {
  if (mObservationFrameGeneration == mHostFrameGeneration) {
    return;
  }
  mObservedSubsystems.fill(false);
  mLastObservedSubsystem.reset();
  mObservationFrameGeneration = mHostFrameGeneration;
}

void Oot3dNativeUiLifecycleBridge::ResetAfterStateLoad(
    uint32_t hostFrame) noexcept {
  mRuntime = oot3d::ui::N64IntegratedUiRuntime{};
  mStats = {};
  mStats.current_host_frame = hostFrame;
  mHostFrameGeneration = 0;
  mObservationFrameGeneration = UINT64_MAX;
  mCapturedGeneration = UINT64_MAX;
  mLatestState = {};
  mHasLatestState = false;
  mObservedSubsystems.fill(false);
  mLastObservedSubsystem.reset();
  mTopScreenPauseProjection = {};
  mTopScreenPauseProjection.AlternatePage = mTopScreenConfig.MinimapVisible;
  mTopScreenPauseEdgeGeometry.reset();
  mTopScreenPausePageRedrawEdgeGeometry.reset();
  mTopScreenItemsHint = {};
  mTopScreenOcarina.Reset();
}

NativeUiLifecycleObservation
Oot3dNativeUiLifecycleBridge::ObserveGuestEntry(uint32_t guestEntry) {
  NativeUiLifecycleObservation result;
  const auto &entries = GuestEntryPoints();
  if (!std::binary_search(entries.begin(), entries.end(), guestEntry)) {
    return result;
  }
  const auto &contracts = oot3d::ui::Oot3dNativeUiFunctionContracts();
  const auto found = std::find_if(
      contracts.begin(), contracts.end(),
      [guestEntry](const oot3d::ui::UiNativeFunctionContract &contract) {
        return contract.guest_entry == guestEntry;
      });
  if (found == contracts.end()) {
    return result;
  }

  BeginObservationFrameIfNeeded();

  result.matched = true;
  result.contract = &*found;
  result.frame_plan = mRouter.PlanFrame(found->subsystem);
  mObservedSubsystems[static_cast<std::size_t>(found->subsystem)] = true;
  mLastObservedSubsystem = found->subsystem;
  result.route_to_guest =
      RouteGuestForPhase(found->phase, result.frame_plan, found->disposition);
  ++mStats.matched_entries;
  mStats.last_guest_entry = guestEntry;
  ++mStats.calls_by_subsystem[static_cast<std::size_t>(found->subsystem)];
  const std::size_t phaseIndex = SeamPhaseIndex(found->phase);
  if (phaseIndex < mStats.calls_by_phase.size()) {
    ++mStats.calls_by_phase[phaseIndex];
  }
  if (result.route_to_guest) {
    ++mStats.guest_routed_entries;
  } else {
    ++mStats.host_routed_entries;
  }

  if (mCapturedGeneration != mHostFrameGeneration) {
    CaptureState();
    mCapturedGeneration = mHostFrameGeneration;
  }
  return result;
}

void Oot3dNativeUiLifecycleBridge::ObserveGameplayComposition() noexcept {
  BeginObservationFrameIfNeeded();
  mObservedSubsystems[static_cast<std::size_t>(
      oot3d::ui::UiSubsystem::GameplayHud)] = true;
  mLastObservedSubsystem = oot3d::ui::UiSubsystem::GameplayHud;
}

bool Oot3dNativeUiLifecycleBridge::ApplyTopScreenGuestHook(uint32_t guestPc,
                                                           uint32_t guestR0) {
  if (guestPc == kTopScreenQuestSubmitModelsHook) {
    ++mStats.topscreen_quest_hook_calls;
    TopScreenQuestModelTransformStats stats;
    std::string error;
    if (!ApplyTopScreenQuestSubmitModelTransforms(mMemory, mTopScreenConfig,
                                                  &stats, &error)) {
      ++mStats.topscreen_quest_hook_failures;
      return true;
    }
    mStats.topscreen_quest_transforms +=
        stats.MainModelsTransformed + stats.ChildModelsTransformed;
    return true;
  }
  if (guestPc == kTopScreenQuestDrawHook) {
    ++mStats.topscreen_quest_hook_calls;
    TopScreenQuestModelTransformStats stats;
    std::string error;
    if (!ApplyTopScreenQuestDrawModelTransform(
            mMemory, guestR0, mTopScreenConfig, mTopScreenPauseProjection,
            &stats, &error)) {
      ++mStats.topscreen_quest_hook_failures;
      return true;
    }
    mStats.topscreen_quest_transforms += stats.DrawModelsTransformed;
    return true;
  }
  if (guestPc != kTopScreenQuestMaterializedHook)
    return false;

  ++mStats.topscreen_quest_hook_calls;
  constexpr uint32_t kQuestRenderBufferBinding = 0x004FC660U;
  uint32_t renderBuffer = 0U;
  TopScreenQuestGeometryContext context;
  std::string error;
  if (!mMemory.Read32(kQuestRenderBufferBinding, &renderBuffer) ||
      renderBuffer == 0U ||
      !ReadTopScreenQuestGeometryContext(mMemory, &context,
                                         &mTopScreenPauseProjection, &error)) {
    ++mStats.topscreen_quest_hook_failures;
    return true;
  }
  if (context.NativePageGateActive) {
    return true;
  }
  context.HudScale = mTopScreenConfig.HudScale;
  context.HudMarginX = static_cast<float>(mTopScreenConfig.HudMarginX);
  context.HudMarginY = static_cast<float>(mTopScreenConfig.HudMarginY);
  TopScreenQuestRenderBufferStats stats;
  if (!TransformTopScreenQuestRenderBuffer(mMemory, renderBuffer, context,
                                           &stats, &error)) {
    ++mStats.topscreen_quest_hook_failures;
    return true;
  }
  ++mStats.topscreen_quest_transforms;
  return true;
}

std::vector<oot3d::ui::UiPrimitive>
Oot3dNativeUiLifecycleBridge::BuildShadowPresentation(
    oot3d::ui::UiSubsystem subsystem) {
  std::vector<oot3d::ui::UiPrimitive> output;
  const std::size_t index = static_cast<std::size_t>(subsystem);
  if (!mHasLatestState || index >= mObservedSubsystems.size() ||
      !mObservedSubsystems[index]) {
    return output;
  }
  if (subsystem == oot3d::ui::UiSubsystem::GameplayHud) {
    oot3d::ui::Oot3dHudTextureSources sources;
    if (auto identity = NativePauseSharedTextureIdentity(
            oot3d::ui::UiPauseSharedTextureSlot::PauseTopPage)) {
      sources.pause_top_page = std::move(*identity);
    }
    if (auto identity = NativePauseSharedTextureIdentity(
            oot3d::ui::UiPauseSharedTextureSlot::ItemIcons)) {
      sources.item_icons = std::move(*identity);
    }
    if (auto identity = NativePauseSharedTextureIdentity(
            oot3d::ui::UiPauseSharedTextureSlot::NumberGlyphs)) {
      sources.number_glyphs = std::move(*identity);
    }
    mRuntime.SetHudTextureSources(std::move(sources));
  }
  mRuntime.AppendPresentation(
      subsystem, oot3d::ui::UiBackendStateView(mLatestState), output);
  if (!output.empty()) {
    ++mStats.shadow_presentation_frames;
    mStats.shadow_presentation_primitives += output.size();
  }
  return output;
}

std::vector<oot3d::ui::UiPrimitive>
Oot3dNativeUiLifecycleBridge::BuildTopScreenPresentation(
    oot3d::ui::UiSubsystem subsystem) {
  std::vector<oot3d::ui::UiPrimitive> output;
  const std::size_t index = static_cast<std::size_t>(subsystem);
  const bool retainedPauseEdge =
      subsystem == oot3d::ui::UiSubsystem::GameplayHud &&
      (mTopScreenPauseEdgeGeometry.has_value() ||
       mTopScreenPausePageRedrawEdgeGeometry.has_value());
  const bool ocarinaLane = subsystem == oot3d::ui::UiSubsystem::TouchControls;
  if (index >= mObservedSubsystems.size() ||
      (!ocarinaLane && (!mHasLatestState ||
       (!mObservedSubsystems[index] && !retainedPauseEdge)))) {
    return output;
  }
  if (ocarinaLane) {
    const auto ocarinaTexture = NativePauseSharedTextureIdentity(
        oot3d::ui::UiPauseSharedTextureSlot::OcarinaPage);
    const auto pauseTexture = NativePauseSharedTextureIdentity(
        oot3d::ui::UiPauseSharedTextureSlot::PauseTopPage);
    if (!ocarinaTexture.has_value()) {
      return output;
    }
    TopScreenOcarinaGeometry geometry;
    std::string ocarinaError;
    if (ReadTopScreenOcarinaGeometry(mMemory, mTopScreenOcarina, &geometry, &ocarinaError)) {
      oot3d::ui::UiTextureIdentity menuTexture;
      menuTexture.semantic_name = kTopScreen211MenuAtlasSemantic;
      (void)AppendTopScreenOcarinaPresentation(geometry, *ocarinaTexture, output, menuTexture);
      if (geometry.Active && pauseTexture.has_value()) {
        (void)AppendTopScreenOcarinaNavigationPresentation(
            BuildTopScreenOcarinaNavigationGeometry(mTopScreenOcarina.Direction()), *pauseTexture,
            output);
      }
    } else {
      ++mStats.topscreen_ocarina_failures;
      mStats.topscreen_ocarina_error = ocarinaError;
    }
    if (!output.empty()) {
      ++mStats.topscreen_ocarina_frames;
      mStats.topscreen_ocarina_primitives += output.size();
      ++mStats.shadow_presentation_frames;
      mStats.shadow_presentation_primitives += output.size();
    }
    return output;
  }
  if (subsystem != oot3d::ui::UiSubsystem::GameplayHud) {
    return output;
  }
  TopScreenHudCompositorGate compositorGate;
  std::string compositorGateError;
  const bool compositorGateKnown = ReadTopScreenHudCompositorGate(
      mMemory, &compositorGate, &compositorGateError);
  const auto texture = NativePauseSharedTextureIdentity(
      oot3d::ui::UiPauseSharedTextureSlot::PauseTopPage);
  if (!texture.has_value()) {
    return output;
  }
  if (compositorGateKnown && compositorGate.Draw &&
      mTopScreenConfig.RenderHud) {
    std::uint32_t pulsePhase = 0U;
    (void)mMemory.Read32(0x0050AF8CU, &pulsePhase);
    const auto content = oot3d::ui::BuildOot3dUiHudContent(mLatestState);
    (void)AppendTopScreenHealthPresentation(
        content, *texture, static_cast<std::uint8_t>(pulsePhase), output);
    TopScreenTouchDynamicState touchState;
    std::string touchStateError;
    if (ReadTopScreenTouchDynamicState(mMemory, mTopScreenInput, &touchState,
                                       &touchStateError)) {
      auto touchGeometry = BuildTopScreenTouchClusterGeometry(
          touchState.VerticalOffsets, touchState.Alpha,
          mTopScreenConfig.HudLayout);
      if (!mTopScreenConfig.RenderDpadIcons) {
        touchGeometry.Alpha[5] = 0.0F;
      }
      (void)AppendTopScreenTouchClusterPresentation(touchGeometry, *texture,
                                                    output);
      TopScreenAuxiliaryTouchInputs auxiliaryInputs;
      std::string auxiliaryInputError;
      if (ReadTopScreenAuxiliaryTouchInputs(mMemory, &auxiliaryInputs,
                                            &auxiliaryInputError)) {
        (void)AppendTopScreenAuxiliaryTouchPresentation(
            BuildTopScreenAuxiliaryTouchGeometry(
                auxiliaryInputs, touchState.VerticalOffsets, touchState.Alpha,
                mTopScreenConfig.HudLayout),
            *texture, output);
      }
      (void)AppendTopScreenTouchLabelsPresentation(
          BuildTopScreenTouchLabelsGeometry(touchState.VerticalOffsets,
                                            touchState.Alpha,
                                            mTopScreenConfig.HudLayout),
          *texture, output);
      if (const auto itemIcons = NativePauseSharedTextureIdentity(
              oot3d::ui::UiPauseSharedTextureSlot::ItemIcons);
          itemIcons.has_value()) {
        std::string itemCopyError;
        (void)AppendTopScreenNativeItemIconCopies(
            mMemory, touchState.VerticalOffsets, touchState.Alpha, *itemIcons,
            output, &itemCopyError, mTopScreenConfig.RenderDpadIcons);
      }
      if (const auto numberGlyphs = NativePauseSharedTextureIdentity(
              oot3d::ui::UiPauseSharedTextureSlot::NumberGlyphs);
          numberGlyphs.has_value()) {
        std::string counterError;
        (void)AppendTopScreenNativeCounters(
            mMemory, touchState.VerticalOffsets, *numberGlyphs, output,
            &counterError, &mTopScreenConfig, &mTopScreenInput);
      }
    }
    std::uint8_t itemISlotIdentity = 0U;
    std::uint8_t itemIISlotIdentity = 0U;
    if (mTopScreenConfig.RenderDpadIcons &&
        mTopScreenConfig.HudLayout == TopScreenHudLayout::Normal &&
        mMemory.Read8(0x005879FCU, &itemISlotIdentity) &&
        mMemory.Read8(0x005879FDU, &itemIISlotIdentity)) {
      if (const auto itemIcons = NativePauseSharedTextureIdentity(
              oot3d::ui::UiPauseSharedTextureSlot::ItemIcons);
          itemIcons.has_value()) {
        (void)AppendTopScreenExtendedItemButtonsPresentation(
            BuildTopScreenExtendedItemButtonsGeometry(
                itemISlotIdentity == 0x45U, itemIISlotIdentity == 0x46U),
            *itemIcons, output);
      }
    }
    TopScreenNativeTouchCopyStats nativeTouchStats;
    std::string nativeTouchError;
    const std::size_t nativeTouchStart = output.size();
    (void)AppendTopScreenNativeTouchCopies(mMemory, *texture, output,
                                           &nativeTouchStats, &nativeTouchError,
                                           &mTopScreenConfig);
    const std::size_t nativeTouchEnd = output.size();
    if (nativeTouchStats.SourceQuadsRead != 0U) {
      ++mStats.topscreen_native_touch_copy_frames;
      mStats.topscreen_native_touch_copy_primitives +=
          nativeTouchStats.PrimitivesEmitted;
    }
    if (content.health.capacity_units.IsKnown()) {
      const auto magic = BuildTopScreenMagicMeterGeometry(
          content.magic, content.health.capacity_units.value);
      (void)AppendTopScreenMagicMeterPresentation(magic, *texture, output);
    }
    ApplyTopScreenHudScale(output, mTopScreenConfig);
    ApplyTopScreenGameplayCanvas(output);
    std::uint64_t visibleNativeTouchCopies = 0U;
    std::uint64_t visibleHorseStamina = 0U;
    for (std::size_t index = nativeTouchStart; index < nativeTouchEnd;
         ++index) {
      const auto &primitive = output[index];
      if (primitive.visible) {
        ++visibleNativeTouchCopies;
      }
      if (primitive.visible &&
          primitive.role == oot3d::ui::UiPrimitiveRole::HorseStamina) {
        ++visibleHorseStamina;
      }
    }
    mStats.topscreen_native_touch_visible_primitives +=
        visibleNativeTouchCopies;
    if (visibleHorseStamina != 0U) {
      ++mStats.topscreen_horse_stamina_visible_frames;
      mStats.topscreen_horse_stamina_visible_primitives += visibleHorseStamina;
    }
  }
  const auto *pauseEdge = mTopScreenPausePageRedrawEdgeGeometry.has_value()
                              ? &*mTopScreenPausePageRedrawEdgeGeometry
                              : (mTopScreenPauseEdgeGeometry.has_value()
                                     ? &*mTopScreenPauseEdgeGeometry
                                     : nullptr);
  if (pauseEdge != nullptr) {
    if (const auto itemPage = NativePauseSharedTextureIdentity(
            oot3d::ui::UiPauseSharedTextureSlot::ItemPage);
        itemPage.has_value()) {
      (void)AppendTopScreenPauseEdgePresentation(*pauseEdge, *itemPage, output);
    }
  }
  oot3d::ui::UiTextureIdentity itemsHintAtlas;
  itemsHintAtlas.semantic_name = kTopScreen211MenuAtlasSemantic;
  (void)AppendTopScreenItemsHintPresentation(
      BuildTopScreenItemsHintGeometry(mTopScreenItemsHint,
                                      mTopScreenConfig.RenderItemsHint),
      itemsHintAtlas, output);
  bool fileSelectStripActive = false;
  std::string fileSelectStripError;
  if (ReadTopScreenFileSelectStripActive(mMemory, &fileSelectStripActive,
                                         &fileSelectStripError) &&
      fileSelectStripActive) {
    if (const auto fileSelectTexture = NativeActiveFileSelectTextureIdentity();
        fileSelectTexture.has_value()) {
      (void)AppendTopScreenFileSelectStripPresentation(
          BuildTopScreenFileSelectStripGeometry(true), *fileSelectTexture,
          output);
    }
  }
  if (!output.empty()) {
    ++mStats.shadow_presentation_frames;
    mStats.shadow_presentation_primitives += output.size();
  }
  return output;
}

void Oot3dNativeUiLifecycleBridge::SetTopScreenPauseEdgePresentation(
    std::optional<TopScreenPauseEdgeGeometry> geometry) noexcept {
  mTopScreenPauseEdgeGeometry = std::move(geometry);
}

void Oot3dNativeUiLifecycleBridge::SetTopScreenPausePageRedrawEdgePresentation(
    std::optional<TopScreenPauseEdgeGeometry> geometry) noexcept {
  mTopScreenPausePageRedrawEdgeGeometry = std::move(geometry);
}

void Oot3dNativeUiLifecycleBridge::SetTopScreenInputFrame(
    const TopScreenExtendedInputFrame &input) noexcept {
  mTopScreenInput = input;
  TopScreenOcarinaState state;
  if (ReadTopScreenOcarinaState(mMemory, &state)) {
    mTopScreenOcarina.Advance(state, input.DpadLeftHeld || input.DpadLeftPressed,
                            input.DpadRightHeld || input.DpadRightPressed,
                            input.DpadLeftPressed, input.DpadRightPressed);
  }
}

void Oot3dNativeUiLifecycleBridge::SetTopScreenConfig(
    const TopScreenUiConfig &config) noexcept {
  mTopScreenConfig = config;
  mTopScreenPauseProjection.AlternatePage = config.MinimapVisible;
}

bool Oot3dNativeUiLifecycleBridge::NativePresentationActive(
    oot3d::ui::UiSubsystem subsystem) const noexcept {
  if (!mHasLatestState) {
    return false;
  }
  switch (subsystem) {
  case oot3d::ui::UiSubsystem::FileSelect:
    return mLatestState.file_select.controller_state.known &&
           mLatestState.file_select.controller_state.value != 0;
  case oot3d::ui::UiSubsystem::NameEntry:
    return mLatestState.name_entry.controller_state.known &&
           mLatestState.name_entry.controller_state.value != 0;
  default:
    return false;
  }
}

bool Oot3dNativeUiLifecycleBridge::NativeGameplayPresentationActive()
    const noexcept {
  return mObservedSubsystems[static_cast<std::size_t>(
      oot3d::ui::UiSubsystem::GameplayHud)];
}

bool Oot3dNativeUiLifecycleBridge::NativeTouchPresentationActive()
    const noexcept {
  // Pause pages retain OoT3D's touch-owned tabs and widgets after their
  // TopScreen relocation. Physical pointer input must therefore stay mapped
  // to the native 320x240 touch domain for the complete pause session.
  if (mHasLatestState && mLatestState.pause.open.known &&
      mLatestState.pause.open.value) {
    return true;
  }
  return NativeFrontendPresentationActive();
}

bool Oot3dNativeUiLifecycleBridge::NativeFrontendPresentationActive()
    const noexcept {
  // FileSelect owns the out-of-game options and normal/Master Quest
  // selectors as well as the slot list. NameEntry is its separate native
  // controller.
  // Gameplay and frontend callbacks share a global update chain, so callback
  // order is not ownership. SaveContext.game_mode is the persistent native
  // discriminator: 0/1 are gameplay, while frontend modes are greater.
  if (!mHasLatestState || !mLatestState.save.game_mode.known ||
      IsTopScreenGameplayInputMode(
          static_cast<uint32_t>(mLatestState.save.game_mode.value))) {
    return false;
  }
  return NativePresentationActive(oot3d::ui::UiSubsystem::FileSelect) ||
         NativePresentationActive(oot3d::ui::UiSubsystem::NameEntry);
}

std::optional<uint32_t>
Oot3dNativeUiLifecycleBridge::NativePauseSharedTextureGuestAddress(
    oot3d::ui::UiPauseSharedTextureSlot slot) const {
  const auto identity = NativePauseSharedTextureIdentity(slot);
  return identity.has_value()
             ? std::optional<uint32_t>(identity->guest_surface_address)
             : std::nullopt;
}

std::optional<oot3d::ui::UiTextureIdentity>
Oot3dNativeUiLifecycleBridge::NativePauseSharedTextureIdentity(
    oot3d::ui::UiPauseSharedTextureSlot slot) const {
  const std::size_t slotIndex = static_cast<std::size_t>(slot);
  if (slotIndex >= oot3d::ui::kOot3dPauseSharedTextureCount) {
    return std::nullopt;
  }

  // RendererTextureDescriptor_GetBuiltin returns these decoded CTXB source
  // objects. The native descriptor binding copies their payload pointer,
  // dimensions, and PICA format/type from +0x4c/+0x2c..+0x32.
  uint32_t source = 0;
  if (!mMemory.Read32(kRendererBuiltinCtxbSources +
                          static_cast<uint32_t>(slotIndex * sizeof(uint32_t)),
                      &source) ||
      source == 0U) {
    return std::nullopt;
  }
  uint32_t guestTextureAddress = 0;
  if (!mMemory.Read32(source + kCtxbRuntimeTextureGuestAddressOffset,
                      &guestTextureAddress) ||
      guestTextureAddress == 0U) {
    return std::nullopt;
  }

  const auto kind = oot3d::ui::Oot3dPauseSharedTextureKind(slot);
  const auto *descriptor =
      oot3d::ui::Oot3dLocalizedMenuTextureDescriptorFor(kind);
  if (descriptor == nullptr || descriptor->semantic_name == nullptr) {
    return std::nullopt;
  }
  oot3d::ui::UiTextureIdentity identity;
  identity.guest_resource_address = source;
  identity.guest_surface_address = guestTextureAddress;
  identity.semantic_name =
      std::string(kPauseSharedSemanticPrefix) + descriptor->semantic_name;
  return identity;
}

std::optional<oot3d::ui::UiTextureIdentity>
Oot3dNativeUiLifecycleBridge::NativeActiveFileSelectTextureIdentity() const {
  // Payload 0x005CD64C expresses this as FileSelect globals 0x00504000 plus
  // 0xFA0. The original loader at 0x00481390 and disposer at 0x002EE468 both
  // own the resulting CTXB pointer at this exact absolute address.
  constexpr std::uint32_t kFileSelectTextureOwner = 0x00504FA0U;
  std::uint32_t source = 0U;
  std::uint32_t guestTextureAddress = 0U;
  if (!mMemory.Read32(kFileSelectTextureOwner, &source) || source == 0U ||
      !mMemory.Read32(source + kCtxbRuntimeTextureGuestAddressOffset,
                      &guestTextureAddress) ||
      guestTextureAddress == 0U) {
    return std::nullopt;
  }
  return oot3d::ui::UiTextureIdentity{
      source, guestTextureAddress, "oot3d/native/localized/file_select_active"};
}

void Oot3dNativeUiLifecycleBridge::CaptureState() {
  const NativeA32UiMemoryReader reader(mMemory);
  const auto capture = oot3d::ui::CaptureOot3dUiState(reader, mRoots);
  ++mStats.state_captures;
  mStats.fields_read += capture.fields_read;
  mStats.fields_missing += capture.fields_missing;
  mLatestState = capture.state;
  mHasLatestState = true;
  mRouter.ObserveState(capture.state);
  TopScreenItemsHintCursorBounds itemsHintCursor;
  std::string itemsHintError;
  if (!ReadTopScreenItemsHintCursorBounds(mMemory, &itemsHintCursor,
                                          &itemsHintError)) {
    itemsHintCursor = {};
  }
  AdvanceTopScreenItemsHintState(mTopScreenItemsHint, itemsHintCursor);
}

const NativeUiLifecycleBridgeStats &
Oot3dNativeUiLifecycleBridge::Stats() const noexcept {
  return mStats;
}

const oot3d::ui::N64IntegratedUiRuntime &
Oot3dNativeUiLifecycleBridge::Runtime() const noexcept {
  return mRuntime;
}

TopScreenPauseProjectionState &
Oot3dNativeUiLifecycleBridge::TopScreenPauseProjection() noexcept {
  return mTopScreenPauseProjection;
}

const std::vector<uint32_t> &Oot3dNativeUiLifecycleBridge::GuestEntryPoints() {
  static const std::vector<uint32_t> entries = [] {
    std::vector<uint32_t> result;
    const auto &contracts = oot3d::ui::Oot3dNativeUiFunctionContracts();
    result.reserve(contracts.size());
    for (const auto &contract : contracts) {
      result.push_back(contract.guest_entry);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
  }();
  return entries;
}

} // namespace Oot3dNativeGame
