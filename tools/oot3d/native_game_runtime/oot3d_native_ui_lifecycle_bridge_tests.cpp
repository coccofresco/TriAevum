#include "oot3d_native_ui_lifecycle_bridge.h"
#include "oot3d_top_screen_gameplay_action_consumer.h"
#include "oot3d_top_screen_items_hint_consumer.h"
#include "oot3d_top_screen_mod_profile.h"

#include <algorithm>
#include <array>
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
          std::abs(selectedItemBounds.CenterX - 120.0F) < 0.0001F &&
          std::abs(selectedItemBounds.MinimumY - 50.0F) < 0.0001F &&
          std::abs(selectedItemBounds.MaximumY - 80.0F) < 0.0001F &&
          std::abs(selectedItemBounds.Width - 40.0F) < 0.0001F,
      "the Items hint adapter did not reproduce FUN_005CA124/005CA270");
  Require(memory.Write32(0x005066F8U + 0x34U, 0U) &&
              ReadTopScreenItemsHintCursorBounds(memory, &selectedItemBounds,
                                                 &error) &&
              !selectedItemBounds.Active,
          "the inactive native Items controller retained hint geometry");

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
  // This ordinary gameplay HUD fixture seeds no ocarina performance fields
  // (scene +0x2B80/+0x2B82 stay zero), so the predicate must read inactive.
  // It previously "activated" only through the inverted `result != 0x00FF`
  // branch treating the zero idle value as a song result.
  bool ocarinaUiActive = true;
  Require(ReadTopScreenOcarinaUiActive(gameplayHudMemory, &ocarinaUiActive,
                                       &error) &&
              !ocarinaUiActive,
          "ordinary gameplay HUD fixture must not report the Ocarina UI as "
          "active");
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

  std::cout << "oot3d_native_ui_lifecycle_bridge_tests: ok\n";
  return 0;
}
