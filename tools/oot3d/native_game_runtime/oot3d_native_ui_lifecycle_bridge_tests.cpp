#include "oot3d_native_ui_lifecycle_bridge.h"
#include "oot3d_top_screen_gameplay_action_consumer.h"
#include "oot3d_top_screen_items_hint_consumer.h"
#include "oot3d_top_screen_mod_profile.h"
#include "oot3d_top_screen_wide_items_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

[[noreturn]] void Fail(std::string_view message) {
  std::cerr << "oot3d_native_ui_lifecycle_bridge_tests: " << message << '\n';
  std::exit(1);
}

void Require(bool condition, std::string_view message) {
  if (!condition) {
    Fail(message);
  }
}

} // namespace

int main() {
  using namespace Oot3dNativeGame;

  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion(
              {"ui-state", 0x004F0000U, 0x00110000U, true, false, {}}, &error),
          "cannot map UI state fixture");
  Require(memory.Write32(0x00504FB4U, 2U) && memory.Write32(0x00504FB0U, 7U) &&
              memory.Write32(0x00588E3CU, 2U) &&
              memory.Write32(0x00587958U + 0x04U, 1U) &&
              memory.Write32(0x0055B490U, 0x00560000U) &&
              memory.Write32(0x0056004CU, 0x14001234U) &&
              memory.Write32(0x0055B498U, 0x00560200U) &&
              memory.Write32(0x0056024CU, 0x1400789AU) &&
              memory.Write32(0x0055B49CU, 0x00560300U) &&
              memory.Write32(0x0056034CU, 0x14009ABCU) &&
              memory.Write32(0x00504FA0U, 0x00560100U) &&
              memory.Write32(0x0056014CU, 0x14005678U),
          "cannot seed typed file-select fields");
  bool childLink = false;
  Require(ReadTopScreenChildLink(memory, &childLink, &error) && childLink,
          "the TopScreen runtime adapter did not decode native Link age");

  constexpr std::uint32_t kSelectedItemModel = 0x005D0000U;
  constexpr std::uint32_t kSelectedItemFirstOwner = 0x005D0100U;
  constexpr std::uint32_t kSelectedItemStreamOwner = 0x005D0200U;
  constexpr std::uint32_t kSelectedItemPositions = 0x005D1000U;
  std::array<float, 4U * 4U * 3U> selectedItemVertices{};
  for (std::size_t vertex = 0U; vertex < 16U; ++vertex) {
    selectedItemVertices[vertex * 3U] = 400.0F;
  }
  selectedItemVertices[0U] = 100.0F;
  selectedItemVertices[1U] = 50.0F;
  selectedItemVertices[3U] = 140.0F;
  selectedItemVertices[4U] = 50.0F;
  selectedItemVertices[6U] = 100.0F;
  selectedItemVertices[7U] = 80.0F;
  selectedItemVertices[9U] = 140.0F;
  selectedItemVertices[10U] = 80.0F;
  Require(
      memory.Write32(0x005066F8U + 0x34U, 2U) &&
          memory.Write32(0x005066F8U + 0xB0U, kSelectedItemModel) &&
          memory.Write32(kSelectedItemModel + 4U, kSelectedItemFirstOwner) &&
          memory.Write32(kSelectedItemFirstOwner + 4U,
                         kSelectedItemStreamOwner) &&
          memory.Write32(kSelectedItemStreamOwner + 0x124U, 2U) &&
          memory.Write32(kSelectedItemStreamOwner + 0x1A8U,
                         kSelectedItemPositions) &&
          memory.WriteBytes(kSelectedItemPositions,
                            std::span<const std::uint8_t>(
                                reinterpret_cast<const std::uint8_t *>(
                                    selectedItemVertices.data()),
                                sizeof(selectedItemVertices))),
      "cannot seed the native Items hint selected-model fixture");
  TopScreenItemsHintCursorBounds selectedItemBounds;
  Require(
      ReadTopScreenItemsHintCursorBounds(memory, &selectedItemBounds, &error) &&
          selectedItemBounds.Active &&
          std::abs(selectedItemBounds.CenterX - 204.0F) < 0.0001F &&
          std::abs(selectedItemBounds.MinimumY - 50.0F) < 0.0001F &&
          std::abs(selectedItemBounds.MaximumY - 80.0F) < 0.0001F &&
          std::abs(selectedItemBounds.Width - 40.0F) < 0.0001F,
      "the Items hint adapter did not reproduce 3.0 cursor bounds/projection");
  // At X 256 the icon group retains projection 0.6 (native X+80).
  for (std::size_t vertex = 0U; vertex < 4U; ++vertex)
    selectedItemVertices[vertex * 3U] += 156.0F;
  Require(memory.WriteBytes(kSelectedItemPositions,
                            std::span<const std::uint8_t>(
                                reinterpret_cast<const std::uint8_t *>(selectedItemVertices.data()),
                                sizeof(selectedItemVertices))) &&
              ReadTopScreenItemsHintCursorBounds(memory, &selectedItemBounds, &error) &&
              selectedItemBounds.Active && std::abs(selectedItemBounds.CenterX - 356.0F) < 0.0001F,
          "right-column hint used the grid cursor projection");
  // A hidden quad is skipped as a unit, even if its later vertices are visible.
  selectedItemVertices[0U] = 400.0F;
  Require(memory.WriteBytes(kSelectedItemPositions,
                            std::span<const std::uint8_t>(
                                reinterpret_cast<const std::uint8_t *>(selectedItemVertices.data()),
                                sizeof(selectedItemVertices))) &&
              ReadTopScreenItemsHintCursorBounds(memory, &selectedItemBounds, &error) &&
              !selectedItemBounds.Active,
          "hidden first vertex did not suppress the whole cursor quad");
  Require(memory.Write32(0x005066F8U + 0x34U, 0U) &&
              ReadTopScreenItemsHintCursorBounds(memory, &selectedItemBounds,
                                                 &error) &&
              !selectedItemBounds.Active,
          "the inactive native Items controller retained hint geometry");
  // 3.0 0x005E2BA8: the same scan of the Gear cursor owner, at projection 0.8.
  for (std::size_t vertex = 0U; vertex < 4U; ++vertex)
    selectedItemVertices[vertex * 3U] = vertex % 2U == 0U ? 100.0F : 140.0F;
  Require(memory.WriteBytes(kSelectedItemPositions,
                            std::span<const std::uint8_t>(
                                reinterpret_cast<const std::uint8_t *>(selectedItemVertices.data()),
                                sizeof(selectedItemVertices))) &&
              memory.Write32(0x00504480U, kSelectedItemModel) &&
              ReadTopScreenGearCursorBounds(memory, &selectedItemBounds, &error) &&
              selectedItemBounds.Active && std::abs(selectedItemBounds.CenterX - 160.0F) < 0.0001F &&
              std::abs(selectedItemBounds.MaximumY - 80.0F) < 0.0001F &&
              std::abs(selectedItemBounds.ScreenOffsetX - 40.0F) < 0.0001F &&
              memory.Write32(0x00504480U, 0U) &&
              ReadTopScreenGearCursorBounds(memory, &selectedItemBounds, &error) &&
              !selectedItemBounds.Active,
          "the Gear cursor bounds differ from 0x005CB298 at projection 0.8");
  // 3.0 0x005E0F80: the A-hold ring beside Gear slot 4 of 0x00504BD4.
  constexpr std::array<float, 4> gearSlot{10.0F, 20.0F, 30.0F, 40.0F};
  for (std::uint32_t lane = 0U; lane < 4U; ++lane)
    Require(memory.Write32(0x00504BD4U + (lane * 27U + 4U) * 4U, std::bit_cast<std::uint32_t>(gearSlot[lane])),
            "cannot seed the Gear slot layout");
  float ringX = 0.0F, ringY = 0.0F;
  Require(ReadTopScreenGearHoldRingCenter(memory, 4U, &ringX, &ringY) &&
              std::abs(ringX - 90.133333F) < 0.0001F && std::abs(ringY - 40.0F) < 0.0001F &&
              !ReadTopScreenGearHoldRingCenter(memory, 27U, &ringX, &ringY),
          "the Gear A-hold ring is not beside the native slot");
  for (std::uint32_t lane = 0U; lane < 4U; ++lane)
    Require(memory.Write32(0x00504BD4U + (lane * 27U + 4U) * 4U, 0U), "cannot clear the Gear slot layout");

  Oot3dNativeUiLifecycleBridge bridge(memory);
  const auto backgroundIdentity = bridge.NativePauseSharedTextureIdentity(
      oot3d::ui::UiPauseSharedTextureSlot::CommonBackground00);
  Require(backgroundIdentity.has_value() &&
              backgroundIdentity->guest_resource_address == 0x00560000U &&
              backgroundIdentity->guest_surface_address == 0x14001234U &&
              backgroundIdentity->semantic_name ==
                  "oot3d/native/pause_shared/common_background00",
          "native CTXB source identity was not preserved");
  Require(bridge.NativePauseSharedTextureGuestAddress(
              oot3d::ui::UiPauseSharedTextureSlot::CommonBackground00) ==
              0x14001234U,
          "native shared-background descriptor was not resolved");
  const auto fileSelectIdentity =
      bridge.NativeActiveFileSelectTextureIdentity();
  Require(fileSelectIdentity.has_value() &&
              fileSelectIdentity->guest_resource_address == 0x00560100U &&
              fileSelectIdentity->guest_surface_address == 0x14005678U &&
              fileSelectIdentity->semantic_name ==
                  "oot3d/native/localized/file_select_active",
          "active native file-select CTXB identity was not preserved");
  Require(!bridge
               .NativePauseSharedTextureGuestAddress(
                   oot3d::ui::UiPauseSharedTextureSlot::HudAtlas)
               .has_value(),
          "an absent native shared texture was reported as available");
  Require(!bridge.NativeFrontendPresentationActive(),
          "frontend presentation was active before semantic capture");
  Require(!bridge.NativeGameplayPresentationActive(),
          "gameplay presentation was active before lifecycle observation");
  const auto &entries = bridge.GuestEntryPoints();
  Require(entries.size() == oot3d::ui::kUiNativeFunctionContractCount,
          "UI lifecycle entry inventory drifted");

  constexpr uint32_t kFileSelectUpdate = 0x0042F9A8U;
  bridge.BeginHostFrame(10U);
  const auto first = bridge.ObserveGuestEntry(kFileSelectUpdate);
  const auto repeated = bridge.ObserveGuestEntry(kFileSelectUpdate);
  Require(first.matched && repeated.matched && first.route_to_guest &&
              first.contract != nullptr &&
              first.contract->subsystem == oot3d::ui::UiSubsystem::FileSelect,
          "file-select lifecycle was not routed through the typed bridge");
  Require(!BeginsNativeGameplayUiPresentation(first),
          "frontend lifecycle was classified as gameplay HUD composition");
  const auto &firstStats = bridge.Stats();
  if (firstStats.matched_entries != 2U || firstStats.state_captures != 1U ||
      firstStats.fields_read == 0U || firstStats.fields_missing != 0U) {
    std::cerr << "matched=" << firstStats.matched_entries
              << " captures=" << firstStats.state_captures
              << " read=" << firstStats.fields_read
              << " missing=" << firstStats.fields_missing << '\n';
  }
  Require(firstStats.matched_entries == 2U && firstStats.state_captures == 1U &&
              firstStats.fields_read != 0U && firstStats.fields_missing == 0U,
          "UI bridge did not deduplicate semantic capture per host frame");
  Require(bridge.NativeFrontendPresentationActive(),
          "active file select did not enable frontend presentation");
  constexpr uint32_t kGameplayHudDraw = 0x004596D0U;
  const auto gameplayOwner = bridge.ObserveGuestEntry(kGameplayHudDraw);
  Require(gameplayOwner.matched && gameplayOwner.contract != nullptr &&
              gameplayOwner.contract->subsystem ==
                  oot3d::ui::UiSubsystem::GameplayHud &&
              bridge.NativeFrontendPresentationActive(),
          "a global gameplay callback displaced the active native frontend");
  Require(BeginsNativeGameplayUiPresentation(gameplayOwner),
          "typed gameplay HUD composition seam was not recognized");
  Require(bridge.NativeGameplayPresentationActive(),
          "gameplay lifecycle did not arm TopScreen START routing");
  bridge.ObserveGuestEntry(kFileSelectUpdate);
  Require(bridge.NativeFrontendPresentationActive(),
          "the active native frontend was not stable across callback order");
  bridge.ObserveGameplayComposition();
  Require(bridge.NativeFrontendPresentationActive(),
          "a materialized global gameplay callback displaced the frontend");
  bridge.BeginHostFrame(11U);
  Require(bridge.NativeFrontendPresentationActive(),
          "frontend ownership did not survive until the next scanout");
  bridge.ObserveGuestEntry(kFileSelectUpdate);
  Require(bridge.NativeFrontendPresentationActive(),
          "file-select did not regain ownership on a new frontend frame");
  Require(!bridge.NativeGameplayPresentationActive(),
          "file-select lifecycle retained TopScreen gameplay START routing");
  const auto &runtimeStats = bridge.Runtime().Stats();
  Require(runtimeStats.observed_states == 2U &&
              runtimeStats.selected_slot_known &&
              runtimeStats.selected_slot == 2 &&
              runtimeStats.controller_state_known &&
              runtimeStats.controller_state == 7,
          "N64 UI runtime did not receive the guest semantic state");
  const auto preview =
      bridge.BuildShadowPresentation(oot3d::ui::UiSubsystem::FileSelect);
  Require(!preview.empty() && bridge.Stats().shadow_presentation_frames == 1U &&
              bridge.Stats().shadow_presentation_primitives == preview.size(),
          "UI bridge did not build the observed typed shadow preview");

  bridge.BeginHostFrame(12U);
  const auto persisted =
      bridge.BuildShadowPresentation(oot3d::ui::UiSubsystem::FileSelect);
  Require(!persisted.empty() && bridge.Stats().shadow_presentation_frames == 2U,
          "UI bridge did not preserve guest state between 30 Hz updates");
  bridge.SetTopScreenPauseEdgePresentation(
      BuildTopScreenPauseEdgeGeometry(true, 5U, 0U, 0U, 0, 1.0F, 1.0F));
  const auto retainedPauseEdges =
      bridge.BuildTopScreenPresentation(oot3d::ui::UiSubsystem::GameplayHud);
  std::size_t retainedPauseEdgeCount = 0U;
  for (const auto &primitive : retainedPauseEdges) {
    retainedPauseEdgeCount += primitive.owner_address == 0x005CD1E0U ? 1U : 0U;
  }
  Require(retainedPauseEdgeCount == 2U,
          "TopScreen pause edges did not persist between HUD observations");
  bridge.SetTopScreenPauseEdgePresentation(std::nullopt);
  bridge.SetTopScreenPausePageRedrawEdgePresentation(
      BuildTopScreenPauseEdgeGeometry(true, 3U, 0U, 0U, 0, 1.0F, 1.0F));
  const auto retainedPageRedrawEdges =
      bridge.BuildTopScreenPresentation(oot3d::ui::UiSubsystem::GameplayHud);
  std::size_t retainedPageRedrawEdgeCount = 0U;
  for (const auto &primitive : retainedPageRedrawEdges) {
    retainedPageRedrawEdgeCount +=
        primitive.owner_address == 0x005CD1E0U ? 1U : 0U;
  }
  Require(retainedPageRedrawEdgeCount == 2U,
          "TopScreen page-redraw edges did not retain independent ownership");
  // System omits both redraw edges and the retained alternate-edge fallback.
  bridge.SetTopScreenPauseEdgePresentation(
      BuildTopScreenPauseEdgeGeometry(true, 5U, 0U, 0U, 0, 1.0F, 1.0F));
  // No cursor/equipment bindings: 3.0 hints use the profile atlas directly.
  Require(memory.Write32(0x0055B4C4U, 0U) && memory.Write32(0x0055B4A0U, 0U) &&
              memory.Write32(0x0050672CU, 0U), "cannot clear retired hint atlases");
  for (const auto page : {0x0050672CU, 0x00504484U, 0x00506CE8U}) {
    Require(memory.Write32(page, 2U), "cannot open Items/Gear/Map for tab hints");
    const auto pageHints = bridge.BuildTopScreenPresentation(oot3d::ui::UiSubsystem::GameplayHud);
    Require(std::count_if(pageHints.begin(), pageHints.end(), [](const auto &primitive) {
              return primitive.owner_address == 0x005D4A0CU;
            }) == 4, "Items/Gear/Map did not emit both 3.0 tab-hint lanes");
    for (const auto &primitive : pageHints) {
      if (primitive.owner_address == 0x005D4A0CU)
        Require(primitive.texture.semantic_name == "oot3d/topscreen/3.0/ui_atlas",
                "lifecycle bound tab hints to a retired native atlas");
    }
    Require(memory.Write32(page, 0U), "cannot close tab-hint page");
  }
  const auto closedHints = bridge.BuildTopScreenPresentation(oot3d::ui::UiSubsystem::GameplayHud);
  Require(std::none_of(closedHints.begin(), closedHints.end(), [](const auto &primitive) {
            return primitive.owner_address == 0x005D4A0CU;
          }), "lifecycle retained tab hints after the pages closed");
  Require(memory.Write32(0x0050672CU, 2U) && memory.Write32(0x0050A530U, 2U),
          "cannot activate System over the Items gate");
  const auto systemHints = bridge.BuildTopScreenPresentation(oot3d::ui::UiSubsystem::GameplayHud);
  Require(std::none_of(systemHints.begin(), systemHints.end(), [](const auto &primitive) {
            return primitive.owner_address == 0x005D4A0CU;
          }), "native System state retained tab hints");
  bridge.SetTopScreenSystemPagePresentation(true);
  for (const bool redrawEdges : {true, false}) {
    if (!redrawEdges) {
      bridge.SetTopScreenPausePageRedrawEdgePresentation(std::nullopt);
      Require(memory.Write32(0x0050A530U, 0U), "cannot suppress System in the tail");
    }
    const auto systemPresentation = bridge.BuildTopScreenPresentation(oot3d::ui::UiSubsystem::GameplayHud);
    for (const auto &primitive : systemPresentation)
      Require(primitive.owner_address != 0x005CD1E0U && primitive.owner_address != 0x005D4A0CU,
              "System retained pause edges or tab hints");
  }
  Require(memory.Write32(0x0050A530U, 0U) && memory.Write32(0x0050672CU, 0U),
          "cannot leave System");
  bridge.SetTopScreenSystemPagePresentation(false);
  const auto returnedEdges = bridge.BuildTopScreenPresentation(oot3d::ui::UiSubsystem::GameplayHud);
  Require(std::count_if(returnedEdges.begin(), returnedEdges.end(), [](const auto &primitive) {
            return primitive.owner_address == 0x005CD1E0U;
          }) == 2, "System suppressed subsequent ordinary page edges");
  bridge.SetTopScreenPauseEdgePresentation(std::nullopt);
  bridge.SetTopScreenPausePageRedrawEdgePresentation(std::nullopt);
  Require(memory.Write32(0x00504FB0U, 0U),
          "cannot deactivate the typed file-select fixture");
  bridge.ObserveGuestEntry(kFileSelectUpdate);
  Require(bridge.Stats().state_captures == 3U &&
              bridge.Runtime().Stats().observed_states == 3U,
          "UI bridge did not refresh state on the next host frame");
  Require(bridge.BuildShadowPresentation(oot3d::ui::UiSubsystem::FileSelect)
              .empty(),
          "UI bridge retained presentation after guest deactivation");
  Require(!bridge.NativeFrontendPresentationActive(),
          "frontend presentation remained active after guest deactivation");
  Require(memory.Write32(0x00504FB0U, 7U) && memory.Write32(0x00588E3CU, 0U) &&
              memory.Write32(0x005043D4U + 0x14U, 2U),
          "cannot activate the native gameplay pause fixture");
  bridge.BeginHostFrame(13U);
  bridge.ObserveGuestEntry(kGameplayHudDraw);
  Require(bridge.NativeTouchPresentationActive() &&
              !bridge.NativeFrontendPresentationActive(),
          "open pause did not enable touch independently of lower composition");
  Require(memory.Write32(0x005043D4U + 0x14U, 0U),
          "cannot deactivate the native pause fixture");
  bridge.BeginHostFrame(14U);
  bridge.ObserveGuestEntry(kGameplayHudDraw);
  Require(!bridge.NativeTouchPresentationActive(),
          "pause touch ownership remained active after close");
  Require(!bridge.ObserveGuestEntry(0x00100000U).matched,
          "non-UI guest entry was claimed by the UI bridge");

  NativeA32Memory gameplayHudMemory;
  Require(gameplayHudMemory.MapRegion(
              {"gameplay-hud-state", 0x004F0000U, 0x00110000U, true, false, {}},
              &error),
          "cannot map gameplay HUD fixture");
  constexpr std::uint32_t kGameplayPlayState = 0x00520000U;
  constexpr std::uint32_t kPauseTopPageResource = 0x00560000U;
  Require(
      gameplayHudMemory.Write32(0x005043D4U + 0x0CU, kGameplayPlayState) &&
          gameplayHudMemory.Write8(kGameplayPlayState + 0x100U, 3U) &&
          gameplayHudMemory.Write8(kGameplayPlayState + 0x101U, 2U) &&
          gameplayHudMemory.Write16(kGameplayPlayState + 0x2E30U, 1U) &&
          gameplayHudMemory.Write16(0x00587958U + 0x42U, 0x30U) &&
          gameplayHudMemory.Write16(0x00587958U + 0x44U, 0x30U) &&
          gameplayHudMemory.Write32(0x0055B490U + 2U * sizeof(uint32_t),
                                    kPauseTopPageResource) &&
          gameplayHudMemory.Write32(kPauseTopPageResource + 0x4CU, 0x14001234U),
      "cannot seed gameplay HUD fixture");
  bool ocarinaUiActive = false;
  Require(ReadTopScreenOcarinaUiActive(gameplayHudMemory, &ocarinaUiActive,
                                       &error) &&
              ocarinaUiActive,
          "gameplay HUD regression fixture did not activate the local Ocarina "
          "predicate");
  Oot3dNativeUiLifecycleBridge gameplayHudBridge(gameplayHudMemory);
  gameplayHudBridge.BeginHostFrame(1U);
  Require(gameplayHudBridge.ObserveGuestEntry(kGameplayHudDraw).matched,
          "gameplay HUD fixture was not observed");
  const auto gameplayHud = gameplayHudBridge.BuildTopScreenPresentation(
      oot3d::ui::UiSubsystem::GameplayHud);
  const auto gameplayHeart =
      std::find_if(gameplayHud.begin(), gameplayHud.end(),
                   [](const oot3d::ui::UiPrimitive &primitive) {
                     return primitive.role == oot3d::ui::UiPrimitiveRole::Heart;
                   });
  Require(gameplayHeart != gameplayHud.end(),
          "a lane-local Ocarina predicate suppressed the complete gameplay "
          "HUD");

  gameplayHudBridge.BeginHostFrame(2U);
  gameplayHudBridge.ObserveGuestEntry(kFileSelectUpdate);
  const auto interleavedHud = gameplayHudBridge.BuildTopScreenPresentation(
      oot3d::ui::UiSubsystem::GameplayHud);
  Require(std::any_of(interleavedHud.begin(), interleavedHud.end(),
                     [](const auto &primitive) {
                       return primitive.role == oot3d::ui::UiPrimitiveRole::Heart;
                     }),
          "unrelated callback hid HUD despite the live native compositor gate");
  gameplayHudBridge.BeginHostFrame(3U);
  Require(!gameplayHudBridge.BuildTopScreenPresentation(
               oot3d::ui::UiSubsystem::GameplayHud).empty(),
          "presentation-only frame lost the native HUD");
  Require(gameplayHudMemory.Write8(kGameplayPlayState + 0x7F40U, 1U),
          "cannot suppress native HUD");
  Require(gameplayHudBridge.BuildTopScreenPresentation(
              oot3d::ui::UiSubsystem::GameplayHud).empty(),
          "live native HUD suppression retained stale presentation");
  Require(gameplayHudMemory.Write8(kGameplayPlayState + 0x7F40U, 0U),
          "cannot restore native HUD");
  gameplayHudBridge.BeginHostFrame(4U);
  gameplayHudBridge.ObserveGuestEntry(kGameplayHudDraw);
  Require(gameplayHudMemory.Write16(0x00587958U + 0x42U, 0x40U),
          "cannot update capacity after lifecycle entry");
  const auto updatedHud = gameplayHudBridge.BuildTopScreenPresentation(
      oot3d::ui::UiSubsystem::GameplayHud);
  Require(std::count_if(updatedHud.begin(), updatedHud.end(),
                        [](const auto &primitive) {
                          return primitive.role == oot3d::ui::UiPrimitiveRole::Heart;
                        }) == 4,
          "presentation mixed pre-update health with current native streams");

  constexpr uint32_t kQuestRenderBuffer = 0x005E0000U;
  constexpr uint32_t kQuestPositions = 0x005E0100U;
  std::array<std::array<TopScreenVec3, 4>, 8> questQuads{};
  questQuads[0] = {{{80.0F, 140.0F, 1.0F},
                    {100.0F, 140.0F, 1.0F},
                    {100.0F, 160.0F, 1.0F},
                    {80.0F, 160.0F, 1.0F}}};
  Require(memory.Write32(0x004FC660U, kQuestRenderBuffer) &&
              memory.Write32(kQuestRenderBuffer, 8U) &&
              memory.Write32(kQuestRenderBuffer + 0x10U, kQuestPositions) &&
              memory.WriteBytes(
                  kQuestPositions,
                  std::span<const uint8_t>(
                      reinterpret_cast<const uint8_t *>(questQuads.data()),
                      sizeof(questQuads))),
          "cannot seed live TopScreen Quest hook fixture");
  Require(!bridge.ApplyTopScreenGuestHook(0x00100000U) &&
              bridge.ApplyTopScreenGuestHook(kTopScreenQuestMaterializedHook) &&
              bridge.Stats().topscreen_quest_hook_calls == 1U &&
              bridge.Stats().topscreen_quest_transforms == 1U &&
              bridge.Stats().topscreen_quest_hook_failures == 0U,
          "live TopScreen Quest hook did not transform native geometry");

  bridge.TopScreenPauseProjection().QuestDrawModelAdjusted = true;
  bridge.BeginHostFrame(30U);
  Require(bridge.TopScreenPauseProjection().QuestDrawModelAdjusted,
          "presentation must not reset native counter geometry guards");
  Require(bridge.ApplyTopScreenGuestHook(kTopScreenQuestSubmitModelsHook) &&
              !bridge.TopScreenPauseProjection().QuestDrawModelAdjusted,
          "native counter refresh must rearm its draw transform");

  for (std::uint8_t phase = 0U; phase <= 10U; ++phase) {
    Require(IsNativeVersionChoicePresented({true, 0x005F0000U, phase, 0U, 1U}) ==
                (phase >= 3U && phase <= 8U),
            "version choice phases outside the panels owned the lower screen");
  }
  Require(!IsNativeVersionChoicePresented({false, 0x005F0000U, 8U, 0U, 1U}) &&
              !IsNativeVersionChoicePresented({true, 0U, 8U, 0U, 1U}) &&
              !IsNativeVersionChoicePresented({true, 0x005F0000U, 8U, 0U, 2U}),
          "a disabled, destroyed or single-version selector was presented");

  // The title's normal / Master Quest choice runs in game mode 1, as a lane
  // of the root pause UI rather than a FileSelect state.
  NativeA32Memory selectorMemory;
  Require(selectorMemory.MapRegion(
              {"selector-state", 0x004F0000U, 0x00110000U, true, false, {}},
              &error),
          "cannot map version selector fixture");
  constexpr std::uint32_t kSelectorOwner = 0x005F0000U;
  constexpr std::uint32_t kSelectorBackdropResource = 0x005F1000U;
  constexpr std::uint32_t kSelectorBackdropTexture = 0x1495AA00U;
  Require(selectorMemory.Write32(0x00588E3CU, 1U) &&
              selectorMemory.Write32(kOmoteUraSelectorLaneEnabled, 1U) &&
              selectorMemory.Write32(kOmoteUraSelectorOwner, kSelectorOwner) &&
              selectorMemory.Write8(kSelectorOwner + 0x04U, 1U) &&
              selectorMemory.Write8(kSelectorOwner + 0x06U, 1U) &&
              selectorMemory.Write32(kSelectorOwner + 0x498U,
                                     kSelectorBackdropResource) &&
              selectorMemory.Write32(kSelectorBackdropResource + 0x4CU,
                                     kSelectorBackdropTexture),
          "cannot seed version selector fixture");
  Oot3dNativeUiLifecycleBridge selectorBridge(selectorMemory);
  const auto observeSelectorFrame = [&](std::uint32_t hostFrame) {
    selectorBridge.BeginHostFrame(hostFrame);
    Require(selectorBridge.ObserveGuestEntry(kGameplayHudDraw).matched,
            "version selector fixture was not observed");
  };
  observeSelectorFrame(1U);
  Require(!selectorBridge.NativeFrontendPresentationActive() &&
              !selectorBridge.NativeTouchPresentationActive() &&
              !selectorBridge.NativeVersionChoiceBackdropGuestAddress() &&
              selectorBridge.Stats().version_choice_captures == 0U &&
              selectorBridge.Stats().version_choice_last_selection == -1,
          "the title prompt displaced the upper title screen");
  Require(selectorMemory.Write8(kSelectorOwner + 0x04U, 8U) &&
              selectorMemory.Write8(kSelectorOwner + 0x05U, 1U),
          "cannot open the version choice fixture");
  observeSelectorFrame(2U);
  Require(selectorBridge.NativeFrontendPresentationActive() &&
              selectorBridge.NativeTouchPresentationActive() &&
              selectorBridge.Stats().version_choice_captures == 1U &&
              selectorBridge.Stats().version_choice_last_selection == 1,
          "the version choice did not own the frontend presentation");
  Require(selectorBridge.NativeVersionChoiceBackdropGuestAddress() ==
              kSelectorBackdropTexture,
          "the version choice background texture was not resolved");
  Require(selectorMemory.Write8(kSelectorOwner + 0x06U, 2U),
          "cannot select the single-version variant");
  observeSelectorFrame(3U);
  Require(!selectorBridge.NativeFrontendPresentationActive() &&
              !selectorBridge.NativeVersionChoiceBackdropGuestAddress(),
          "the single-version prompt was presented as a choice");
  Require(selectorMemory.Write8(kSelectorOwner + 0x06U, 1U) &&
              selectorMemory.Write32(kSelectorOwner + 0x498U, 0U),
          "cannot drop the version choice background resource");
  observeSelectorFrame(30U);
  Require(selectorBridge.NativeFrontendPresentationActive() &&
              !selectorBridge.NativeVersionChoiceBackdropGuestAddress(),
          "an unbuilt background changed the version choice presentation");
  Require(selectorMemory.Write32(kSelectorOwner + 0x498U,
                                 kSelectorBackdropResource),
          "cannot restore the version choice background resource");
  Require(selectorMemory.Write8(kSelectorOwner + 0x06U, 1U) &&
              selectorMemory.Write32(kOmoteUraSelectorLaneEnabled, 0U),
          "cannot disable the version selector lane");
  observeSelectorFrame(4U);
  Require(!selectorBridge.NativeFrontendPresentationActive(),
          "a disabled selector lane kept the frontend presentation");
  Require(selectorMemory.Write32(kOmoteUraSelectorLaneEnabled, 1U) &&
              selectorMemory.Write8(kSelectorOwner + 0x04U, 9U),
          "cannot close the version choice fixture");
  observeSelectorFrame(5U);
  Require(!selectorBridge.NativeFrontendPresentationActive() &&
              selectorBridge.Stats().version_choice_captures == 2U,
          "a closed version choice kept the frontend presentation");
  Require(selectorMemory.Write32(kOmoteUraSelectorOwner, 0U),
          "cannot destroy the version selector fixture");
  observeSelectorFrame(6U);
  Require(!selectorBridge.NativeFrontendPresentationActive(),
          "a destroyed selector owner kept the frontend presentation");

  // One screen: the game slides the cards in by itself, so the choice stays
  // withheld (title visible, no touch ownership) until the player reveals it,
  // and the reveal ends with the choice.
  selectorBridge.SetVersionChoiceRevealRequired(true);
  Require(selectorMemory.Write32(kOmoteUraSelectorOwner, kSelectorOwner) &&
              selectorMemory.Write8(kSelectorOwner + 0x04U, 8U),
          "cannot reopen the version choice fixture");
  observeSelectorFrame(7U);
  Require(selectorBridge.NativeVersionChoiceAwaitingReveal() &&
              !selectorBridge.NativeFrontendPresentationActive() &&
              !selectorBridge.NativeTouchPresentationActive() &&
              !selectorBridge.NativeVersionChoiceBackdropGuestAddress(),
          "an unrevealed version choice covered the title");
  selectorBridge.RevealNativeVersionChoice();
  Require(!selectorBridge.NativeVersionChoiceAwaitingReveal() &&
              selectorBridge.NativeFrontendPresentationActive() &&
              selectorBridge.NativeVersionChoiceBackdropGuestAddress() ==
                  kSelectorBackdropTexture &&
              selectorBridge.Stats().version_choice_reveals == 1U,
          "the reveal did not present the version choice");
  observeSelectorFrame(8U);
  Require(selectorBridge.NativeFrontendPresentationActive(),
          "the reveal did not last for the choice");
  Require(selectorMemory.Write8(kSelectorOwner + 0x04U, 9U),
          "cannot close the revealed version choice");
  observeSelectorFrame(9U);
  Require(selectorMemory.Write8(kSelectorOwner + 0x04U, 8U),
          "cannot reopen the version choice fixture");
  observeSelectorFrame(10U);
  Require(selectorBridge.NativeVersionChoiceAwaitingReveal(),
          "a closed choice kept its reveal for the next title");
  // A press on the title prompt counts as the reveal and survives the
  // prompt's own phases into the panels.
  Require(selectorMemory.Write8(kSelectorOwner + 0x04U, 9U),
          "cannot close the version choice fixture");
  observeSelectorFrame(11U);
  Require(selectorMemory.Write8(kSelectorOwner + 0x04U, 1U),
          "cannot open the title prompt fixture");
  observeSelectorFrame(12U);
  Require(selectorBridge.NativeVersionChoicePrompting() &&
              !selectorBridge.NativeVersionChoiceAwaitingReveal() &&
              !selectorBridge.NativeFrontendPresentationActive(),
          "the title prompt was not recognised");
  selectorBridge.RevealNativeVersionChoice();
  Require(selectorMemory.Write8(kSelectorOwner + 0x04U, 2U),
          "cannot advance the title prompt fixture");
  observeSelectorFrame(13U);
  Require(selectorMemory.Write8(kSelectorOwner + 0x04U, 3U),
          "cannot slide the panels in");
  observeSelectorFrame(14U);
  Require(!selectorBridge.NativeVersionChoiceAwaitingReveal() &&
              selectorBridge.NativeFrontendPresentationActive() &&
              selectorBridge.Stats().version_choice_reveals == 2U,
          "a prompt press did not reveal the entering panels");

  NativeA32Memory ringMemory;
  Require(ringMemory.MapRegion(
              {"hold-ring-state", 0x004F0000U, 0x00110000U, true, false, {}}, &error),
          "cannot map hold-ring fixture");
  Require(ringMemory.Write32(0x0050672CU, 2) && ringMemory.Write32(0x0058795CU, 1) &&
              ringMemory.Write32(0x0055B498U, 0x00560200U) &&
              ringMemory.Write32(0x0056024CU, 0x1400789AU) &&
              ringMemory.Write32(0x005067A8U, kSelectedItemModel) &&
              ringMemory.Write32(kSelectedItemModel + 4, kSelectedItemFirstOwner) &&
              ringMemory.Write32(kSelectedItemFirstOwner + 4, kSelectedItemStreamOwner) &&
              ringMemory.Write32(kSelectedItemStreamOwner + 0x124, 2) &&
              ringMemory.Write32(kSelectedItemStreamOwner + 0x1A8, kSelectedItemPositions),
          "cannot seed hold-ring cursor and texture owners");
  for (unsigned vertex = 0; vertex < 16; ++vertex) selectedItemVertices[vertex * 3] = 400;
  for (unsigned vertex = 0; vertex < 4; ++vertex) {
    selectedItemVertices[vertex * 3] = 100.0F + (vertex % 2) * 40.0F;
    selectedItemVertices[vertex * 3 + 1] = 50.0F + (vertex / 2) * 30.0F;
  }
  const auto uploadRingBounds = [&] {
    Require(ringMemory.WriteBytes(kSelectedItemPositions, std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t *>(selectedItemVertices.data()),
                sizeof(selectedItemVertices))), "cannot update live hold-ring bounds");
  };
  uploadRingBounds();
  TopScreenWideItemsRuntime ringSource(ringMemory);
  Oot3dNativeUiLifecycleBridge ringBridge(ringMemory);
  ringBridge.SetTopScreenItemsShortcutSource(&ringSource);
  // Seed an observed producer snapshot; actual input advancement is covered
  // by the wide Items runtime suite, which links the native-call adapter.
  auto &ringShortcuts = const_cast<TopScreenItemsShortcutState &>(ringSource.Shortcuts());
  auto &ringDraw = const_cast<TopScreenItemsShortcutDrawState &>(ringSource.ShortcutDraw());
  auto &ringPicker = const_cast<TopScreenDpadPickerState &>(ringSource.DpadPicker());
  ringShortcuts.SelectedRow = 1;
  ringShortcuts.HoldFrames = 7;
  ringDraw.Active = ringDraw.Child = true;
  const auto presentRing = [&] {
    return ringBridge.BuildTopScreenPresentation(oot3d::ui::UiSubsystem::GameplayHud);
  };
  const auto aGlyphs = [&](const auto &primitives) {
    return std::count_if(primitives.begin(), primitives.end(), [](const auto &p) {
      return p.owner_address == 0x005E34A4U;
    });
  };
  Require(aGlyphs(presentRing()) == 1 && ringBridge.TopScreenItemsShortcutCursor().Count == 36 &&
              ringBridge.TopScreenDpadPickerCursor().Count == 0 &&
              std::abs(ringBridge.TopScreenItemsShortcutCursor().CenterX - 234.133333F) < 0.0001F &&
              std::abs(ringBridge.TopScreenItemsShortcutCursor().CenterY - 65.0F) < 0.0001F &&
              ringBridge.TopScreenItemsShortcutCursor().TextureAlphaScale == 2,
          "lifecycle did not expose the native-bound A ring and its glyph");
  presentRing();
  Require(ringShortcuts.HoldFrames == 7 && ringBridge.TopScreenItemsShortcutCursor().Count == 36,
          "host redraw advanced A-hold time");
  for (unsigned vertex = 0; vertex < 4; ++vertex) selectedItemVertices[vertex * 3] += 10;
  uploadRingBounds();
  presentRing();
  Require(std::abs(ringBridge.TopScreenItemsShortcutCursor().CenterX - 244.133333F) < 0.0001F,
          "A ring did not follow a newly uploaded native cursor");
  Require(ringMemory.Write32(0x005067A8U, 0), "cannot hide native cursor");
  Require(aGlyphs(presentRing()) == 0 && ringBridge.TopScreenItemsShortcutCursor().Count == 0,
          "missing native cursor retained an A ring/glyph");
  Require(ringMemory.Write32(0x005067A8U, kSelectedItemModel), "cannot restore native cursor");
  ringPicker.Phase = TopScreenDpadPickerPhase::Choosing;
  ringPicker.Child = true;
  ringPicker.ClearHoldFrames = 2;
  Require(aGlyphs(presentRing()) == 0 && ringBridge.TopScreenItemsShortcutCursor().Count == 0 &&
              ringBridge.TopScreenDpadPickerCursor().Count == 26 &&
              ringBridge.TopScreenDpadPickerCursor().TextureAlphaScale == 2,
          "modal/clear ring did not replace the pre-picker A ring");
  ringPicker = {};
  ringShortcuts.HoldFrames = -1;
  Require(aGlyphs(presentRing()) == 0 && ringBridge.TopScreenDpadPickerCursor().Count == 0,
          "released buttons retained cursor output");
  for (const auto page : {0U, 1U, 3U}) {
    ringShortcuts.HoldFrames = 7;
    Require(ringMemory.Write32(0x0050672CU, page), "cannot switch hold-ring page");
    Require(aGlyphs(presentRing()) == 0 && ringBridge.TopScreenItemsShortcutCursor().Count == 0 &&
                ringShortcuts.HoldFrames == -1, "page change retained pre-picker progress");
  }
  Require(ringMemory.Write32(0x0050672CU, 2) && ringMemory.Write32(0x0058795CU, 0),
          "cannot change hold-ring age");
  ringShortcuts.HoldFrames = 7;
  Require(aGlyphs(presentRing()) == 0 && ringShortcuts.HoldFrames == -1,
          "age change retained pre-picker progress");
  Require(ringMemory.Write32(0x0058795CU, 1), "cannot restore hold-ring age");
  ringShortcuts.HoldFrames = 7;
  ringBridge.SetTopScreenSystemPagePresentation(true);
  Require(aGlyphs(presentRing()) == 0 && ringShortcuts.HoldFrames == -1,
          "System presentation retained pre-picker progress");
  ringBridge.SetTopScreenSystemPagePresentation(false);
  ringShortcuts.HoldFrames = 7;
  presentRing();
  ringBridge.ResetAfterStateLoad(50);
  Require(ringBridge.TopScreenItemsShortcutCursor().Count == 0 &&
              ringBridge.TopScreenDpadPickerCursor().Count == 0 && ringShortcuts.HoldFrames == -1,
          "savestate reset retained hold cursors or progress");
  ringShortcuts.HoldFrames = 7;
  presentRing();
  ringBridge.SetTopScreenItemsShortcutSource(nullptr);
  Require(ringBridge.TopScreenItemsShortcutCursor().Count == 0 && aGlyphs(presentRing()) == 0,
          "detaching the producer retained the A ring/glyph");

  std::cout << "oot3d_native_ui_lifecycle_bridge_tests: ok\n";
  return 0;
}
