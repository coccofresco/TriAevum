#include "oot3d_top_screen_camera.h"
#include "oot3d_top_screen_camera_guest.h"
#include "oot3d_top_screen_controls.h"
#include "oot3d_top_screen_gameplay_actions.h"
#include "oot3d_top_screen_item_guest.h"
#include "oot3d_top_screen_mod_profile.h"
#include "oot3d_top_screen_texture_overrides.h"
#include "oot3d_ui/ui_contract_types.h"

#include "oot3d_native_a32_memory.h"
#include <bit>
#include <cmath>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
  std::cerr << "oot3d_top_screen_mod_profile_tests: " << message << '\n';
  std::exit(1);
}

void Require(bool condition, std::string_view message) {
  if (!condition) {
    Fail(message);
  }
}

std::uint64_t TestFnv1a64(std::span<const std::uint8_t> bytes) {
  std::uint64_t value = 0xCBF29CE484222325ULL;
  for (const auto byte : bytes) {
    value ^= byte;
    value *= 0x100000001B3ULL;
  }
  return value;
}

void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void AppendU16(std::vector<std::uint8_t> &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void AppendU64(std::vector<std::uint8_t> &bytes, std::uint64_t value) {
  AppendU32(bytes, static_cast<std::uint32_t>(value));
  AppendU32(bytes, static_cast<std::uint32_t>(value >> 32U));
}

Oot3dNativeGame::NativeA32Memory BuildLayoutFixture() {
  using namespace Oot3dNativeGame;
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"topscreen-layout-fixture",
                            0x004D0000U,
                            0x00040000U,
                            true,
                            false,
                            {}},
                           &error),
          "could not map layout fixture");
  for (const auto &patch : TopScreenVerifiedLayoutPatches()) {
    for (std::uint8_t word = 0; word < patch.WordCount; ++word) {
      Require(
          memory.Write32(patch.Address + static_cast<std::uint32_t>(word) * 4U,
                         patch.Expected[word]),
          "could not seed layout fixture");
    }
  }
  return memory;
}

Oot3dNativeGame::NativeA32Memory BuildWorldMapFixture() {
  using namespace Oot3dNativeGame;
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"topscreen-world-map-fixture",
                            0x004D0000U,
                            0x000C0000U,
                            true,
                            false,
                            {}},
                           &error),
          "could not map world-map fixture");
  const auto writeFloat = [&memory](std::uint32_t address, float value) {
    return memory.Write32(address, std::bit_cast<std::uint32_t>(value));
  };
  constexpr std::uint32_t kWorldMap = 0x005093E4U;
  constexpr std::uint32_t kScene = 0x00520000U;
  constexpr std::uint32_t kMarkerTypes = 0x00521000U;
  Require(memory.Write32(0x005043D4U + 0x0CU, kScene) &&
              memory.Write32(0x0050AF68U, 2U) &&
              memory.Write8(kScene + 0x100U, 3U) &&
              memory.Write32(kWorldMap + 0x14U, 4U) &&
              memory.Write32(kWorldMap + 0x18U, 1U) &&
              memory.Write32(kWorldMap + 0x1CU, 1U) &&
              memory.Write32(kWorldMap + 0x38U, 0U) &&
              memory.Write32(kWorldMap + 0x44U, 1U),
          "could not seed world-map controller state");
  for (std::uint32_t index = 0U; index < 6U; ++index) {
    Require(writeFloat(kWorldMap + 0x68U + index * 8U,
                       10.0F + static_cast<float>(index)) &&
                writeFloat(kWorldMap + 0x6CU + index * 8U,
                           20.0F + static_cast<float>(index)) &&
                writeFloat(kWorldMap + 0x3C8U + index * 8U, 30.0F) &&
                writeFloat(kWorldMap + 0x3CCU + index * 8U, 40.0F) &&
                writeFloat(kWorldMap + 0x728U + index * 8U, 50.0F) &&
                writeFloat(kWorldMap + 0x72CU + index * 8U, 60.0F) &&
                writeFloat(kWorldMap + 0xA88U + index * 8U, 70.0F) &&
                writeFloat(kWorldMap + 0xA8CU + index * 8U, 80.0F),
            "could not seed world-map native geometry arrays");
  }
  constexpr std::uint32_t kDestination = 5U;
  Require(memory.Write32(0x00587A14U, 4U) &&
              memory.Write32(0x0050A3B0U + kDestination * 4U, 2U) &&
              memory.Write32(0x0053C9D4U + 2U * 4U, 4U) &&
              memory.Write32(0x004D53C8U + kDestination * 4U, 2U) &&
              memory.Write32(0x004D541CU + kDestination * 4U, kMarkerTypes) &&
              memory.Write32(kMarkerTypes, 1U) &&
              memory.Write32(kMarkerTypes + 4U, 4U),
          "could not seed world-map destination tables");
  for (std::uint32_t type = 0U; type < 5U; ++type) {
    Require(
        writeFloat(0x0050A1CCU + type * 4U, 5.0F + static_cast<float>(type)) &&
            writeFloat(0x0050A1E0U + type * 4U,
                       90.0F + static_cast<float>(type)),
        "could not seed world-map marker tables");
  }
  return memory;
}

} // namespace

int main() {
  using namespace Oot3dNativeGame;

  Oot3dUiProfile profile = Oot3dUiProfile::Oot3d;
  Require(ParseOot3dUiProfile("topscreen", &profile) &&
              profile == Oot3dUiProfile::TopScreen &&
              std::string_view(Oot3dUiProfileName(profile)) == "topscreen",
          "TopScreen profile parsing failed");
  Require(!ParseOot3dUiProfile("ips", &profile),
          "binary patch profile was unexpectedly accepted");
  Require(
      ShouldPresentNativeBottomFrontend(Oot3dUiProfile::Oot3d, true) &&
          !ShouldPresentNativeBottomFrontend(Oot3dUiProfile::Oot3d, false) &&
          !ShouldPresentNativeBottomFrontend(Oot3dUiProfile::TopScreen, true),
      "lower-screen frontend presentation escaped the native OoT3D profile");
  Require(
      ShouldSuppressTopScreenFrontendBackdrop(Oot3dUiProfile::TopScreen,
                                              true) &&
          !ShouldSuppressTopScreenFrontendBackdrop(
              Oot3dUiProfile::TopScreen, false) &&
          !ShouldSuppressTopScreenFrontendBackdrop(Oot3dUiProfile::Oot3d,
                                                   true),
      "frontend backdrop suppression escaped the TopScreen profile");
  TopScreenUiConfig parsedConfig;
  std::string configError;
  Require(ParseTopScreenUiConfigText(
              R"({"schema":"oot3d_topscreen_ui_v2","hud_layout":"restoration","hud_scale":0.75,"hud_margin_x":8,"hud_margin_y":-2,"magic_bar_y":7,"minimap_visible":false,"render_hud":true,"render_dpad_icons":false,"render_items_hint":false,"select_action":"minimap_toggle","exit_items_to_save_screen":false,"camera_zoom_percent":130,"camera_fov_percent":115,"dpad_child":["view","ocarina","boomerang","slingshot"],"dpad_adult":["view","ocarina","iron_boots","hover_boots"],"free_camera_enabled":true,"free_camera_speed_level":5,"free_camera_smoothing":"heavy","free_camera_invert_x":true,"free_camera_invert_y":false,"c_stick_aim_speed_level":6,"c_stick_aim_invert_x":false,"c_stick_aim_invert_y":true})",
              &parsedConfig, &configError) &&
              parsedConfig.HudLayout == TopScreenHudLayout::Restoration &&
              parsedConfig.HudScale == 0.75F &&
              parsedConfig.HudMarginX == 8 &&
              parsedConfig.HudMarginY == -2 && parsedConfig.MagicBarY == 7U &&
              !parsedConfig.MinimapVisible &&
              parsedConfig.CameraZoomPercent == 130U &&
              parsedConfig.CameraFovPercent == 115U &&
              parsedConfig.SelectAction ==
                  TopScreenSelectAction::MinimapToggle &&
              !parsedConfig.RenderDpadIcons && !parsedConfig.RenderItemsHint &&
              !parsedConfig.ExitItemsToSaveScreen &&
              parsedConfig.FreeCameraEnabled &&
              parsedConfig.FreeCameraSpeedLevel == 5U &&
              parsedConfig.FreeCameraSmoothing ==
                  TopScreenFreeCameraSmoothing::Heavy &&
              parsedConfig.FreeCameraInvertX &&
              !parsedConfig.FreeCameraInvertY &&
              parsedConfig.CStickAimSpeedLevel == 6U &&
              parsedConfig.CStickAimInvertY,
          "TopScreen 2.1.1 external config did not decode");
  TopScreenUiConfig invalidSpeedConfig;
  Require(!ParseTopScreenUiConfigText(
              R"({"schema":"oot3d_topscreen_ui_v2","free_camera_speed_level":7})",
              &invalidSpeedConfig, &configError),
          "TopScreen accepted a free-camera speed outside the official table");
  Require(static_cast<std::uint8_t>(TopScreenDpadAction::IronBoots) == 3U &&
              static_cast<std::uint8_t>(TopScreenDpadAction::MinimapToggle) ==
                  9U &&
              static_cast<std::uint8_t>(TopScreenDpadAction::Boomerang) ==
                  10U,
          "TopScreen 2.1.1 D-pad action ABI diverged from the payload");
  TopScreenUiConfig defaultDpadConfig;
  TopScreenDpadPhysicalState childDpadInput;
  childDpadInput.ChildLink = true;
  childDpadInput.Pressed[2] = true;
  childDpadInput.Held[3] = true;
  const auto childDpadActions =
      ResolveTopScreenDpadActions(defaultDpadConfig, childDpadInput);
  Require(childDpadActions.WasPressed(TopScreenDpadAction::ItemZr) &&
              childDpadActions.IsHeld(TopScreenDpadAction::ItemZl) &&
              !childDpadActions.WasPressed(TopScreenDpadAction::IronBoots),
          "TopScreen 2.1.1 child D-pad defaults are incorrect");
  childDpadInput.ChildLink = false;
  const auto adultDpadActions =
      ResolveTopScreenDpadActions(defaultDpadConfig, childDpadInput);
  Require(adultDpadActions.WasPressed(TopScreenDpadAction::IronBoots) &&
              adultDpadActions.IsHeld(TopScreenDpadAction::HoverBoots) &&
              !adultDpadActions.WasPressed(TopScreenDpadAction::ItemZr),
          "TopScreen 2.1.1 adult D-pad defaults are incorrect");
  TopScreenUiConfig fastAimConfig;
  fastAimConfig.CStickAimSpeedLevel = 6U;
  fastAimConfig.CStickAimInvertX = true;
  const auto fastAimPolicy = ResolveTopScreenCStickAimPolicy(fastAimConfig);
  Require(std::abs(fastAimPolicy.SpeedMultiplier - (16.0F / 6.0F)) <
                  0.0001F &&
              fastAimPolicy.InvertX && !fastAimPolicy.InvertY,
          "TopScreen 2.1.1 C-stick aim policy is incorrect");
  Require(std::abs(ResolveTopScreenCStickSmoothingCoefficient(
                       TopScreenFreeCameraSmoothing::Off) -
                   1.0F) < 0.0001F &&
              std::abs(ResolveTopScreenCStickSmoothingCoefficient(
                           TopScreenFreeCameraSmoothing::Light) -
                       (33.3333F / 93.3333F)) < 0.0001F &&
              std::abs(ResolveTopScreenCStickSmoothingCoefficient(
                           TopScreenFreeCameraSmoothing::Default) -
                       (33.3333F / 223.3333F)) < 0.0001F,
          "TopScreen 2.1.1 C-stick smoothing table is incorrect");
  TopScreenEquipmentActionState equipmentState;
  equipmentState.Eligible = true;
  equipmentState.OwnedEquipment = 0x7766U;
  equipmentState.EquippedEquipment = 0x1122U;
  TopScreenDpadActionState equipmentActions;
  equipmentActions.Pressed[static_cast<std::size_t>(
      TopScreenDpadAction::IronBoots)] = true;
  auto equipmentPlan =
      ResolveTopScreenEquipmentAction(equipmentActions, equipmentState);
  Require(equipmentPlan.Consumed && equipmentPlan.ChangeEquipment.has_value() &&
              equipmentPlan.ChangeEquipment->equipment_type ==
                  oot3d::ui::EquipmentType::EQUIP_TYPE_BOOTS &&
              equipmentPlan.ChangeEquipment->value == 2 &&
              equipmentPlan.RefreshPlayerEquipment,
          "TopScreen 2.1.1 Iron Boots action did not use the typed UI action");
  equipmentActions = {};
  equipmentActions.Pressed[static_cast<std::size_t>(
      TopScreenDpadAction::SwordToggle)] = true;
  equipmentPlan = ResolveTopScreenEquipmentAction(equipmentActions,
                                                   equipmentState);
  Require(equipmentPlan.ChangeEquipment.has_value() &&
              equipmentPlan.ChangeEquipment->equipment_type ==
                  oot3d::ui::EquipmentType::EQUIP_TYPE_SWORD &&
              equipmentPlan.ChangeEquipment->value == 3 &&
              equipmentPlan.SwordButtonItem == 0x3DU,
          "TopScreen 2.1.1 sword toggle plan is incorrect");
  equipmentActions = {};
  equipmentActions.Pressed[static_cast<std::size_t>(
      TopScreenDpadAction::AllBootsToggle)] = true;
  equipmentPlan = ResolveTopScreenEquipmentAction(equipmentActions,
                                                   equipmentState);
  Require(equipmentPlan.ChangeEquipment.has_value() &&
              equipmentPlan.ChangeEquipment->value == 2,
          "TopScreen 2.1.1 all-boots cycle did not select Iron Boots");
  equipmentState.EquippedEquipment = 0x3122U;
  equipmentPlan = ResolveTopScreenEquipmentAction(equipmentActions,
                                                   equipmentState);
  Require(equipmentPlan.ChangeEquipment.has_value() &&
              equipmentPlan.ChangeEquipment->value == 1,
          "TopScreen 2.1.1 all-boots cycle did not wrap to Kokiri Boots");
  equipmentState.Eligible = false;
  Require(!ResolveTopScreenEquipmentAction(equipmentActions, equipmentState)
               .Consumed,
          "TopScreen equipment action escaped its native gameplay gates");

  TopScreenDpadActionState directItemActions;
  directItemActions.Held[static_cast<std::size_t>(
      TopScreenDpadAction::Boomerang)] = true;
  TopScreenDirectItemActionState directItemState;
  directItemState.Eligible = true;
  directItemState.BoomerangAvailable = true;
  directItemState.BoomerangItemAction = 9U;
  directItemState.SlingshotAvailable = true;
  directItemState.SlingshotItemAction = 8U;
  TopScreenDirectItemRuntime directItemRuntime;
  auto directItemPlan = ResolveTopScreenDirectItemAction(
      directItemActions, directItemState, directItemRuntime);
  Require(directItemPlan.Consumed && directItemPlan.TriggerPlayerItemUse &&
              directItemPlan.TransientItemAction == 9U &&
              directItemRuntime.ActiveItemId == kTopScreenBoomerangItemId &&
              directItemRuntime.ActiveFrames == 1U,
          "TopScreen 2.1.1 direct Boomerang action is incorrect");
  directItemActions = {};
  directItemState.CurrentTransientItemAction = 9U;
  directItemState.PlayerHeldItemAction = 9U;
  directItemPlan = ResolveTopScreenDirectItemAction(
      directItemActions, directItemState, directItemRuntime);
  Require(!directItemPlan.TransientItemAction.has_value() &&
              directItemRuntime.PlayerObservedAction,
          "TopScreen direct item did not observe the native Player action");
  directItemState.PlayerHeldItemAction = 0U;
  directItemPlan = ResolveTopScreenDirectItemAction(
      directItemActions, directItemState, directItemRuntime);
  Require(directItemPlan.TransientItemAction == 0U &&
              directItemRuntime.ActiveItemId == 0U,
          "TopScreen direct item did not clear after native action release");
  directItemActions.Held[static_cast<std::size_t>(
      TopScreenDpadAction::Slingshot)] = true;
  directItemState.CurrentTransientItemAction = 0U;
  directItemPlan = ResolveTopScreenDirectItemAction(
      directItemActions, directItemState, directItemRuntime);
  directItemState.OrdinaryItemActivated = true;
  directItemState.CurrentTransientItemAction = 8U;
  directItemPlan = ResolveTopScreenDirectItemAction(
      directItemActions, directItemState, directItemRuntime);
  Require(directItemPlan.TransientItemAction == 0U &&
              !directItemPlan.TriggerPlayerItemUse &&
              directItemRuntime.ActiveItemId == 0U,
          "ordinary item input did not cancel the TopScreen direct item");
  TopScreenUiConfig migratedConfig;
  Require(ParseTopScreenUiConfigText(
              R"({"schema":"oot3d_topscreen_ui_v1","hud_scale":1.25,"n64_camera_zoom":true,"free_camera_speed":12})",
              &migratedConfig, &configError) &&
              migratedConfig.HudScale == 1.20F &&
              migratedConfig.CameraZoomPercent == 130U &&
              migratedConfig.CStickAimSpeedLevel == 5U,
          "TopScreen 1.2 config did not migrate to typed 2.1.1 settings");
  Require(!ParseTopScreenUiConfigText(
              R"({"schema":"oot3d_topscreen_ui_v1","hud_scale":0.59})",
              &parsedConfig, &configError) &&
              configError.find("0.60") != std::string::npos,
          "TopScreen config accepted an out-of-range HUD scale");
  Require(!ParseTopScreenUiConfigText(
              R"({"schema":"oot3d_topscreen_ui_v1","hud_scale":0.77})",
              &parsedConfig, &configError) &&
              configError.find("0.05") != std::string::npos,
          "TopScreen config accepted a non-native HUD scale step");
  Require(!ParseTopScreenUiConfigText(
              R"({"schema":"oot3d_topscreen_ui_v1","free_camera_speed":7})",
              &parsedConfig, &configError) &&
              configError.find("native value") != std::string::npos,
          "TopScreen config accepted a non-native free-camera speed");
  Require(!ParseTopScreenUiConfigText(
              R"({"schema":"oot3d_topscreen_ui_v1","controller_toggle":true})",
              &parsedConfig, &configError) &&
              configError.find("unknown") != std::string::npos,
          "TopScreen config accepted an undeclared gameplay preference");
  std::string serializedConfig;
  Require(SerializeTopScreenUiConfigText(parsedConfig, &serializedConfig,
                                         &configError) &&
              serializedConfig.find("\"hud_layout\": \"restoration\"") !=
                  std::string::npos,
          "TopScreen 2.1.1 external config did not serialize");
  const auto configRoundTripPath =
      std::filesystem::temp_directory_path() /
      "oot3d_top_screen_config_roundtrip_test.json";
  std::error_code removeError;
  std::filesystem::remove(configRoundTripPath, removeError);
  TopScreenUiConfig persistedConfig;
  persistedConfig.HudLayout = TopScreenHudLayout::Restoration;
  persistedConfig.HudScale = 1.20F;
  persistedConfig.MinimapVisible = false;
  persistedConfig.CameraZoomPercent = 130U;
  persistedConfig.CameraFovPercent = 115U;
  persistedConfig.FreeCameraEnabled = true;
  persistedConfig.FreeCameraSpeedLevel = 6U;
  persistedConfig.FreeCameraInvertX = true;
  Require(SaveTopScreenUiConfig(configRoundTripPath, persistedConfig,
                                &configError),
          "TopScreen 2.1.1 external config did not save atomically");
  TopScreenUiConfigRuntime configRuntime(configRoundTripPath, {});
  const auto initialConfigSnapshot = configRuntime.Snapshot();
  Require(configRuntime.Reload(&configError),
          "TopScreen 2.1.1 external config runtime did not reload");
  const auto reloadedConfigSnapshot = configRuntime.Snapshot();
  Require(reloadedConfigSnapshot.Revision ==
                  initialConfigSnapshot.Revision + 1U &&
              reloadedConfigSnapshot.Config.HudLayout ==
                  TopScreenHudLayout::Restoration &&
              reloadedConfigSnapshot.Config.HudScale == 1.20F &&
              !reloadedConfigSnapshot.Config.MinimapVisible &&
              reloadedConfigSnapshot.Config.FreeCameraEnabled &&
              reloadedConfigSnapshot.Config.FreeCameraSpeedLevel == 6U,
          "TopScreen 2.1.1 config runtime reload lost typed settings");
  TopScreenUiConfig liveConfig = reloadedConfigSnapshot.Config;
  liveConfig.HudScale = 0.75F;
  liveConfig.MinimapVisible = true;
  configRuntime.Preview(liveConfig);
  const auto previewConfigSnapshot = configRuntime.Snapshot();
  TopScreenUiConfig diskBeforeSave;
  Require(previewConfigSnapshot.Revision ==
                  reloadedConfigSnapshot.Revision + 1U &&
              previewConfigSnapshot.Config == liveConfig &&
              LoadTopScreenUiConfig(configRoundTripPath, &diskBeforeSave,
                                    &configError) &&
              diskBeforeSave.HudScale == 1.20F &&
              !diskBeforeSave.MinimapVisible,
          "TopScreen live preview did not remain transient");
  persistedConfig.HudLayout = TopScreenHudLayout::Normal;
  persistedConfig.HudScale = 0.60F;
  persistedConfig.MinimapVisible = true;
  Require(configRuntime.Apply(persistedConfig, &configError),
          "TopScreen 2.1.1 config runtime did not persist an edit");
  TopScreenUiConfig savedAgain;
  Require(LoadTopScreenUiConfig(configRoundTripPath, &savedAgain,
                                &configError) &&
              savedAgain.HudLayout == TopScreenHudLayout::Normal &&
              savedAgain.HudScale == 0.60F &&
              savedAgain.MinimapVisible,
          "TopScreen 2.1.1 config runtime edit did not round-trip");
  std::filesystem::remove(configRoundTripPath, removeError);

  auto worldMapMemory = BuildWorldMapFixture();
  TopScreenWorldMapGeometry worldMapGeometry;
  std::string worldMapError;
  Require(ReadTopScreenWorldMapGeometry(worldMapMemory, &worldMapGeometry,
                                        &worldMapError) &&
              worldMapGeometry.Active && worldMapGeometry.Destination == 5U,
          "world-map producer did not activate the native destination");
  Require(
      std::abs(worldMapGeometry.Quads[0].Position.X - 50.0F) < 0.001F &&
          std::abs(worldMapGeometry.Quads[0].Position.Y - 189.0F) < 0.001F &&
          std::abs(worldMapGeometry.Quads[0].Size.Y - 39.0F) < 0.001F &&
          std::abs(worldMapGeometry.Quads[0].AtlasOrigin.Y - 81.0F) < 0.001F &&
          std::abs(worldMapGeometry.Quads[0].AtlasSize.Y - 59.0F) < 0.001F,
      "world-map base-quad transformation is incorrect");
  Require(
      worldMapGeometry.Quads[6].Visible && worldMapGeometry.Quads[7].Visible &&
          !worldMapGeometry.Quads[8].Visible &&
          std::abs(worldMapGeometry.Quads[6].Position.X - 122.0F) < 0.001F &&
          std::abs(worldMapGeometry.Quads[6].Position.Y - 174.0F) < 0.001F &&
          worldMapGeometry.Quads[14].Visible &&
          std::abs(worldMapGeometry.Quads[14].Size.X - 24.0F) < 0.001F,
      "world-map marker or arrow reconstruction is incorrect");
  std::vector<oot3d::ui::UiPrimitive> worldMapPresentation;
  const oot3d::ui::UiTextureIdentity worldMapTexture{
      0x005D0000U, 0x18000000U, "oot3d/native/pause_shared/ocarina_page"};
  Require(AppendTopScreenWorldMapPresentation(worldMapGeometry, worldMapTexture,
                                              worldMapPresentation) == 10U &&
              worldMapPresentation.front().subsystem ==
                  oot3d::ui::UiSubsystem::Map &&
              worldMapPresentation.front().role ==
                  oot3d::ui::UiPrimitiveRole::PauseMap &&
              std::abs(worldMapPresentation.front().color.alpha - 0.68F) <
                  0.001F,
          "world-map presentation did not preserve recovered semantics");
  Require(worldMapMemory.Write32(0x005093E4U + 0x44U, 0U) &&
              ReadTopScreenWorldMapGeometry(worldMapMemory, &worldMapGeometry,
                                            &worldMapError) &&
              !worldMapGeometry.Active,
          "disabled native world-map owner still produced a presentation");
  worldMapMemory = BuildWorldMapFixture();
  Require(worldMapMemory.Write32(0x0050AF68U, 0U) &&
              ReadTopScreenWorldMapGeometry(worldMapMemory, &worldMapGeometry,
                                            &worldMapError) &&
              !worldMapGeometry.Active,
          "persistent world-map controller escaped the native pause root");

  const auto idleNavigation = BuildTopScreenPauseNavigationGeometry(0);
  const auto leftNavigation = BuildTopScreenPauseNavigationGeometry(-1);
  const auto rightNavigation = BuildTopScreenPauseNavigationGeometry(1);
  Require(std::abs(idleNavigation.Quads[0].Color.red - 0.6F) < 0.001F &&
              std::abs(idleNavigation.Quads[1].Color.red - 0.6F) < 0.001F &&
              std::abs(idleNavigation.Quads[0].Position.Y - 191.0F) < 0.001F &&
              std::abs(leftNavigation.Quads[0].Color.red - 1.0F) < 0.001F &&
              std::abs(leftNavigation.Quads[0].Position.Y - 193.0F) < 0.001F &&
              std::abs(leftNavigation.Quads[1].Color.red - 0.6F) < 0.001F &&
              std::abs(rightNavigation.Quads[1].Color.red - 1.0F) < 0.001F &&
              std::abs(rightNavigation.Quads[1].Position.Y - 193.0F) < 0.001F &&
              leftNavigation.Quads[0].Size.X == 26.0F &&
              leftNavigation.Quads[0].AtlasSize.X == -26.0F &&
              rightNavigation.Quads[1].Size.X == 26.0F,
          "pause navigation geometry does not match the payload producer");
  std::vector<oot3d::ui::UiPrimitive> navigationPresentation;
  const oot3d::ui::UiTextureIdentity navigationTexture{
      0x005D1000U, 0x18010000U, "oot3d/native/pause_shared/pause_top_page"};
  Require(
      AppendTopScreenPauseNavigationPresentation(
          leftNavigation, navigationTexture, navigationPresentation) == 2U &&
          navigationPresentation.front().role ==
              oot3d::ui::UiPrimitiveRole::PauseCursor &&
          std::abs(navigationPresentation.front().color.alpha - 0.8F) < 0.001F,
      "pause navigation presentation lost recovered texture semantics");

  const auto fileSelectStrip = BuildTopScreenFileSelectStripGeometry(true);
  Require(fileSelectStrip.Active && fileSelectStrip.Quad.Visible &&
              fileSelectStrip.Quad.Position.X == 66.0F &&
              fileSelectStrip.Quad.Position.Y == 212.0F &&
              fileSelectStrip.Quad.Size.X == 252.0F &&
              fileSelectStrip.Quad.Size.Y == 28.0F &&
              fileSelectStrip.Quad.AtlasOrigin.Y == 152.0F &&
              fileSelectStrip.Quad.AtlasSize.X == 289.0F &&
              std::abs(fileSelectStrip.Quad.Color.red - 0.74F) < 0.001F,
          "file-select strip geometry does not match payload 0x005CD64C");
  std::vector<oot3d::ui::UiPrimitive> fileSelectStripPresentation;
  const oot3d::ui::UiTextureIdentity fileSelectStripTexture{
      0x005D2000U, 0x18020000U, "oot3d/native/localized/file_select_active"};
  Require(AppendTopScreenFileSelectStripPresentation(
              fileSelectStrip, fileSelectStripTexture,
              fileSelectStripPresentation) == 1U &&
              fileSelectStripPresentation.front().owner_address ==
                  0x005CD64CU &&
              fileSelectStripPresentation.front().role ==
                  oot3d::ui::UiPrimitiveRole::PauseText,
          "file-select strip presenter lost its native ownership");

  const auto extendedButtons =
      BuildTopScreenExtendedItemButtonsGeometry(true, false);
  Require(extendedButtons.Quads[0].Visible &&
              !extendedButtons.Quads[1].Visible &&
              extendedButtons.Quads[0].Position.X == 2.0F &&
              extendedButtons.Quads[0].Position.Y == 60.0F &&
              extendedButtons.Quads[0].AtlasOrigin.X == 378.0F &&
              extendedButtons.Quads[1].AtlasOrigin.X == 420.0F,
          "extended item-button geometry diverges from payload 0x005C9940");
  std::vector<oot3d::ui::UiPrimitive> extendedButtonPresentation;
  const oot3d::ui::UiTextureIdentity itemIconsTexture{
      0x005D2100U, 0x18021000U, "oot3d/native/pause/item_icons"};
  Require(AppendTopScreenExtendedItemButtonsPresentation(
              extendedButtons, itemIconsTexture, extendedButtonPresentation) ==
                  1U &&
              extendedButtonPresentation.front().source_quad == 5U &&
              extendedButtonPresentation.front().role ==
                  oot3d::ui::UiPrimitiveRole::ActionButton &&
              extendedButtonPresentation.front().texture.semantic_name ==
                  "oot3d/native/pause/item_icons",
          "extended item-button presenter lost its recovered ownership");

  const auto touchLabels = BuildTopScreenTouchLabelsGeometry(
      std::array<float, 4>{0.0F, 0.0F, 4.0F, -3.0F}, 0.5F);
  Require(touchLabels.Quads[0].Position.X == 362.0F &&
              touchLabels.Quads[0].Position.Y == 37.0F &&
              touchLabels.Quads[1].Position.X == 338.0F &&
              touchLabels.Quads[1].Position.Y == 54.0F &&
              touchLabels.Quads[0].AtlasOrigin.Y == 190.0F &&
              touchLabels.Quads[1].AtlasOrigin.Y == 201.0F &&
              std::abs(touchLabels.Quads[0].Color.alpha - 0.45F) < 0.001F,
          "touch-label geometry diverges from payload 0x005C9940");
  std::vector<oot3d::ui::UiPrimitive> touchLabelPresentation;
  Require(AppendTopScreenTouchLabelsPresentation(
              touchLabels, navigationTexture, touchLabelPresentation) == 2U &&
              touchLabelPresentation.front().role ==
                  oot3d::ui::UiPrimitiveRole::TouchControl,
          "touch-label presenter lost its recovered texture semantics");

  auto memory = BuildLayoutFixture();
  TopScreenLayoutApplyStats stats;
  std::string error;
  Require(ApplyTopScreenVerifiedLayout(memory, &stats, &error),
          "verified layout did not apply");
  Require(stats.ContractsChecked == TopScreenVerifiedLayoutPatches().size() &&
              stats.WordsChecked == stats.WordsWritten &&
              stats.WordsChanged != 0U,
          "layout application statistics are inconsistent");
  for (const auto &patch : TopScreenVerifiedLayoutPatches()) {
    for (std::uint8_t word = 0; word < patch.WordCount; ++word) {
      std::uint32_t value = 0;
      Require(
          memory.Read32(patch.Address + static_cast<std::uint32_t>(word) * 4U,
                        &value) &&
              value == patch.Replacement[word],
          "layout replacement value was not written");
    }
  }
  TopScreenLayoutApplyStats repeatedLayoutStats;
  Require(ApplyTopScreenVerifiedLayout(memory, &repeatedLayoutStats, &error) &&
              repeatedLayoutStats.WordsChanged == 0U,
          "verified layout application is not idempotent");

  auto runtimeGeometry = BuildLayoutFixture();
  Require(runtimeGeometry.MapRegion({"topscreen-runtime-geometry-fixture",
                                     0x08000000U,
                                     0x00060000U,
                                     true,
                                     false,
                                     {}},
                                    &error),
          "could not map runtime geometry fixture");
  std::uint32_t runtimeStreamIndex = 0U;
  for (const auto &contract : TopScreenVerifiedRuntimeQuadStreams()) {
    const std::uint32_t renderBuffer =
        0x08000000U + runtimeStreamIndex * 0x00010000U;
    const std::uint32_t positions = renderBuffer + 0x100U;
    const std::uint32_t texcoords = renderBuffer + 0x4000U;
    Require(runtimeGeometry.Write32(
                contract.OwnerAddress + contract.RenderBufferPointerOffset,
                renderBuffer) &&
                runtimeGeometry.Write32(renderBuffer, contract.QuadCount) &&
                runtimeGeometry.Write32(renderBuffer + 4U,
                                        std::bit_cast<std::uint32_t>(512.0F)) &&
                runtimeGeometry.Write32(renderBuffer + 8U,
                                        std::bit_cast<std::uint32_t>(256.0F)) &&
                runtimeGeometry.Write32(renderBuffer + 0x0CU, positions) &&
                runtimeGeometry.Write32(renderBuffer + 0x14U, texcoords),
            "could not seed runtime geometry binding");
    for (std::uint32_t quad = 0U; quad < contract.QuadCount; ++quad) {
      const std::uint32_t sourceOffset = quad * 8U;
      const auto writeFloat = [&](std::uint32_t address, float value) {
        return runtimeGeometry.Write32(address,
                                       std::bit_cast<std::uint32_t>(value));
      };
      Require(writeFloat(contract.OwnerAddress + contract.RectOriginOffset +
                             sourceOffset,
                         10.0F + static_cast<float>(quad)) &&
                  writeFloat(contract.OwnerAddress + contract.RectOriginOffset +
                                 sourceOffset + 4U,
                             20.0F + static_cast<float>(quad)) &&
                  writeFloat(contract.OwnerAddress + contract.RectExtentOffset +
                                 sourceOffset,
                             30.0F) &&
                  writeFloat(contract.OwnerAddress + contract.RectExtentOffset +
                                 sourceOffset + 4U,
                             40.0F) &&
                  writeFloat(contract.OwnerAddress +
                                 contract.TextureOriginOffset + sourceOffset,
                             static_cast<float>(quad)) &&
                  writeFloat(contract.OwnerAddress +
                                 contract.TextureOriginOffset + sourceOffset +
                                 4U,
                             static_cast<float>(quad * 2U)) &&
                  writeFloat(contract.OwnerAddress +
                                 contract.TextureExtentOffset + sourceOffset,
                             8.0F) &&
                  writeFloat(contract.OwnerAddress +
                                 contract.TextureExtentOffset + sourceOffset +
                                 4U,
                             9.0F),
              "could not seed runtime geometry source arrays");
      for (std::uint32_t vertex = 0U; vertex < 4U; ++vertex) {
        Require(writeFloat(positions + quad * 48U + vertex * 12U + 8U,
                           7.0F),
                "could not seed runtime geometry depth");
      }
    }
    ++runtimeStreamIndex;
  }
  for (const auto &patch : TopScreenVerifiedLayoutPatches()) {
    for (std::uint8_t word = 0; word < patch.WordCount; ++word) {
      Require(runtimeGeometry.Write32(
                  patch.Address + static_cast<std::uint32_t>(word) * 4U,
                  patch.Expected[word]),
              "could not restore runtime fixture layout source");
    }
  }
  Require(ApplyTopScreenVerifiedLayout(runtimeGeometry, nullptr, &error),
          "could not apply layout to initialized runtime fixture");
  TopScreenRuntimeGeometryApplyStats runtimeGeometryStats;
  Require(RebindTopScreenVerifiedRuntimeGeometry(
              runtimeGeometry, &runtimeGeometryStats, &error) &&
              runtimeGeometryStats.ContractsChecked == 5U &&
              runtimeGeometryStats.StreamsRebound == 5U &&
              runtimeGeometryStats.StreamsNotInitialized == 0U &&
              runtimeGeometryStats.QuadsRebound == 236U &&
              runtimeGeometryStats.WordsWritten == 3776U,
          "initialized pause geometry was not rebound completely");
  const auto readFloat = [&runtimeGeometry](std::uint32_t address) {
    std::uint32_t bits = 0U;
    Require(runtimeGeometry.Read32(address, &bits),
            "could not read rebound runtime geometry");
    return std::bit_cast<float>(bits);
  };
  runtimeStreamIndex = 0U;
  for (const auto &contract : TopScreenVerifiedRuntimeQuadStreams()) {
    const std::uint32_t renderBuffer =
        0x08000000U + runtimeStreamIndex * 0x00010000U;
    const std::uint32_t positions = renderBuffer + 0x100U;
    const std::uint32_t texcoords = renderBuffer + 0x4000U;
    for (std::uint32_t quad = 0U; quad < contract.QuadCount; ++quad) {
      const std::uint32_t sourceOffset = quad * 8U;
      const float x = readFloat(contract.OwnerAddress +
                                contract.RectOriginOffset + sourceOffset);
      const float y = readFloat(contract.OwnerAddress +
                                contract.RectOriginOffset + sourceOffset + 4U);
      const float width = readFloat(contract.OwnerAddress +
                                    contract.RectExtentOffset + sourceOffset);
      const float height = readFloat(contract.OwnerAddress +
                                     contract.RectExtentOffset + sourceOffset +
                                     4U);
      const float textureX =
          readFloat(contract.OwnerAddress + contract.TextureOriginOffset +
                    sourceOffset);
      const float textureY =
          readFloat(contract.OwnerAddress + contract.TextureOriginOffset +
                    sourceOffset + 4U);
      const float textureExtentX =
          readFloat(contract.OwnerAddress + contract.TextureExtentOffset +
                    sourceOffset);
      const float textureExtentY =
          readFloat(contract.OwnerAddress + contract.TextureExtentOffset +
                    sourceOffset + 4U);
      const std::uint32_t positionBase = positions + quad * 48U;
      const std::uint32_t texcoordBase = texcoords + quad * 32U;
      Require(readFloat(positionBase) == x &&
                  readFloat(positionBase + 4U) == y &&
                  readFloat(positionBase + 12U) == x + width &&
                  readFloat(positionBase + 28U) == y + height &&
                  readFloat(positionBase + 44U) == 7.0F &&
                  readFloat(texcoordBase) == textureX / 512.0F &&
                  readFloat(texcoordBase + 12U) ==
                      (256.0F - textureY) / 256.0F &&
                  readFloat(texcoordBase + 24U) ==
                      (textureX + textureExtentX) / 512.0F &&
                  readFloat(texcoordBase + 28U) ==
                      (256.0F - (textureY + textureExtentY)) / 256.0F,
              "runtime geometry diverges from native quad-stream semantics");
    }
    ++runtimeStreamIndex;
  }
  Require(RebindTopScreenVerifiedRuntimeGeometry(runtimeGeometry, nullptr,
                                                 &error),
          "runtime geometry rebind is not idempotent");

  auto mismatch = BuildLayoutFixture();
  const auto patches = TopScreenVerifiedLayoutPatches();
  Require(patches.size() > 1U &&
              mismatch.Write32(patches.back().Address, 0xDEADBEEFU),
          "could not create mismatch fixture");
  Require(!ApplyTopScreenVerifiedLayout(mismatch, nullptr, &error) &&
              error.find("mismatch") != std::string::npos,
          "layout mismatch was not rejected");
  std::uint32_t unchanged = 0;
  Require(mismatch.Read32(patches.front().Address, &unchanged) &&
              unchanged == patches.front().Expected[0],
          "failed preflight partially changed guest layout");

  Require(ResolveTopScreenItemLane({false, true, false, 0U, 0x04000000U}),
          "extended item lane was not synthesized");
  Require(
      !ResolveTopScreenItemLane({false, true, false, 0x04000000U, 0x04000000U}),
      "suppressed item lane was synthesized");
  Require(!ResolveTopScreenItemLane({false, false, false, 0U, 0x04000000U}),
          "native item lane result was not retained");
  Require(
      ResolveTopScreenItemLane({false, false, true, 0xFFFFFFFFU, 0x04000000U}),
      "compatibility lane did not retain payload precedence");

  const auto itemContracts = TopScreenVerifiedItemQueryContracts();
  Require(itemContracts.size() == 4U &&
              itemContracts[0].OriginalEntry == 0x00349504U &&
              itemContracts[0].NativeFieldOffset == 0x3CU &&
              itemContracts[3].OriginalEntry == 0x002C3950U &&
              itemContracts[3].NativeFieldOffset == 0x48U,
          "verified item query contracts changed");

  TopScreenItemQueryState itemState;
  itemState.Input.ZrPressed = true;
  Require(
      ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIPressed, itemState),
      "ZR press did not expose Item I");
  Require(!ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIHeld, itemState),
          "ZR press leaked into the held lane");
  itemState.Input.ZrHeld = true;
  Require(ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIHeld, itemState),
          "ZR hold did not expose Item I");
  itemState.SuppressionFlags = 0x04000000U;
  Require(
      !ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIPressed, itemState),
      "Item I suppression mask was ignored");

  itemState = {};
  itemState.Input.ZlPressed = true;
  itemState.Input.ZlHeld = true;
  Require(
      ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIIPressed, itemState) &&
          ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIIHeld, itemState),
      "ZL did not expose both Item II lanes");
  itemState.SuppressionFlags = 0x02000000U;
  Require(
      !ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIIPressed,
                                 itemState) &&
          !ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIIHeld, itemState),
      "Item II suppression mask was ignored");

  TopScreenPauseDrawInputs pauseCloseInputs;
  pauseCloseInputs.AnyPageGateActive = true;
  pauseCloseInputs.ChildStates[1] = 2U;
  auto pauseClose = ResolveTopScreenPauseStartClose(pauseCloseInputs, true);
  Require(pauseClose.Page == TopScreenPauseClosePage::Items &&
              pauseClose.ResetFunction == 0x002EB628U,
          "START did not select the native Items reset");
  pauseCloseInputs.ChildStates[0] = 1U;
  pauseClose = ResolveTopScreenPauseStartClose(pauseCloseInputs, true);
  Require(pauseClose.Page == TopScreenPauseClosePage::Items,
          "START close lost the payload's Items-first priority");
  pauseCloseInputs.ChildStates[1] = 0U;
  pauseClose = ResolveTopScreenPauseStartClose(pauseCloseInputs, true);
  Require(pauseClose.Page == TopScreenPauseClosePage::Gear &&
              pauseClose.ResetFunction == 0x002F8AF4U,
          "START did not select the native Gear reset");
  pauseCloseInputs.ChildStates[0] = 0U;
  pauseCloseInputs.ChildStates[2] = 1U;
  pauseClose = ResolveTopScreenPauseStartClose(pauseCloseInputs, true);
  Require(pauseClose.Page == TopScreenPauseClosePage::DungeonMap &&
              pauseClose.ResetFunction == 0x002F0618U,
          "START did not select the native Dungeon Map reset");
  pauseCloseInputs.ChildStates[2] = 0U;
  pauseCloseInputs.ChildStates[3] = 1U;
  pauseClose = ResolveTopScreenPauseStartClose(pauseCloseInputs, true);
  Require(pauseClose.Page == TopScreenPauseClosePage::SystemMenu &&
              pauseClose.ResetFunction == 0x002E9920U,
          "START did not select the native System Menu close");
  Require(ResolveTopScreenPauseStartClose(pauseCloseInputs, false).Page ==
              TopScreenPauseClosePage::None,
          "pause close dispatched without a START edge");

  TopScreenPauseSystemOpenState systemOpenState;
  TopScreenPauseDrawInputs systemOpenInputs;
  systemOpenInputs.ChildStates[1] = 2U;
  auto systemOpen = ResolveTopScreenPauseBSystemOpen(
      systemOpenInputs, true, systemOpenState);
  Require(systemOpen.Page == TopScreenPauseClosePage::None &&
              systemOpenState.RemainingTicks == 4U,
          "B did not arm the five-tick Items-to-system transition");
  systemOpenInputs.ChildStates[1] = 0xEU;
  systemOpen = ResolveTopScreenPauseBSystemOpen(systemOpenInputs, false,
                                                systemOpenState);
  Require(systemOpen.Page == TopScreenPauseClosePage::Items &&
              systemOpen.ResetFunction == 0x002EB628U &&
              systemOpenState.RemainingTicks == 0U,
          "Items close state did not route to the native system menu");

  systemOpenState = {};
  systemOpenInputs = {};
  systemOpenInputs.ChildStates[0] = 0xAU;
  systemOpen = ResolveTopScreenPauseBSystemOpen(systemOpenInputs, true,
                                                systemOpenState);
  Require(systemOpen.Page == TopScreenPauseClosePage::Gear &&
              systemOpen.ResetFunction == 0x002F8AF4U,
          "Gear close state did not route to the native system menu");

  systemOpenState = {};
  systemOpenInputs = {};
  systemOpenInputs.ChildStates[2] = 8U;
  systemOpen = ResolveTopScreenPauseBSystemOpen(systemOpenInputs, true,
                                                systemOpenState);
  Require(systemOpen.Page == TopScreenPauseClosePage::DungeonMap &&
              systemOpen.ResetFunction == 0x002F0618U,
          "Dungeon Map close state did not route to the native system menu");

  systemOpenState = {};
  systemOpenInputs = {};
  systemOpenInputs.ChildStates[3] = 1U;
  systemOpen = ResolveTopScreenPauseBSystemOpen(systemOpenInputs, true,
                                                systemOpenState);
  Require(systemOpen.Page == TopScreenPauseClosePage::None &&
              systemOpenState.RemainingTicks == 0U,
          "B incorrectly armed a transition from the system menu itself");

  TopScreenExtendedInputFrame compatibilityInput;
  compatibilityInput.DpadLeftHeld = true;
  auto compatibility = BuildTopScreenItemCompatibilityOverrides(
      compatibilityInput, 0x45U, 0x46U);
  Require(compatibility[0] && compatibility[2] && !compatibility[1] &&
              !compatibility[3],
          "D-pad Left did not produce both Item I compatibility lanes");
  compatibilityInput.DpadLeftHeld = false;
  compatibilityInput.DpadRightHeld = true;
  compatibility = BuildTopScreenItemCompatibilityOverrides(compatibilityInput,
                                                           0x45U, 0x46U);
  Require(!compatibility[0] && !compatibility[2] && compatibility[1] &&
              compatibility[3],
          "D-pad Right did not produce both Item II compatibility lanes");
  compatibility = BuildTopScreenItemCompatibilityOverrides(compatibilityInput,
                                                           0x45U, 0x10U);
  Require(!compatibility[1] && !compatibility[3],
          "compatibility lane ignored the native Item II identity gate");
  TopScreenItemQueryState restorationItemState;
  restorationItemState.Input.RestorationLayout = true;
  restorationItemState.Input.ZrPressed = true;
  restorationItemState.Input.XHeld = true;
  Require(ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIPressed,
                                    restorationItemState),
          "Restoration Item I did not accept ZR pressed after X");
  restorationItemState.Input.XHeld = false;
  restorationItemState.Input.YHeld = true;
  Require(ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIIPressed,
                                    restorationItemState),
          "Restoration Item II did not accept ZR pressed after Y");
  restorationItemState.Input = {};
  restorationItemState.Input.RestorationLayout = true;
  restorationItemState.Input.ZrHeld = true;
  restorationItemState.Input.XPressed = true;
  Require(ResolveTopScreenItemQuery(TopScreenItemQuery::ItemIPressed,
                                    restorationItemState),
          "Restoration Item I did not accept X pressed after ZR");

  Require(ResolveTopScreenSlotItemId({3U, 0x22U, true, false}) == 0x45U,
          "Item I compatibility slot identity was not selected");
  Require(ResolveTopScreenSlotItemId({4U, 0x33U, false, true}) == 0x46U,
          "Item II compatibility slot identity was not selected");
  Require(ResolveTopScreenSlotItemId({3U, 0x22U, false, true}) == 0x22U &&
              ResolveTopScreenSlotItemId({4U, 0x33U, true, false}) == 0x33U &&
              ResolveTopScreenSlotItemId({2U, 0x44U, true, true}) == 0x44U &&
              ResolveTopScreenSlotItemId({3U, 0x22U, true, false, true}) ==
                  0x22U,
          "compatibility slot identity leaked into an unrelated lane");

  NativeA32Memory itemGuestMemory;
  constexpr std::uint32_t kItemGlobalActionState = 0x0050AF34U;
  constexpr std::uint32_t kItemGlobalContext = 0x08000000U;
  Require(
      itemGuestMemory.MapRegion(
          {"item-state", 0x00500000U, 0x00090000U, true, false, {}}, &error) &&
          itemGuestMemory.MapRegion(
              {"aim-prompt", 0x004FD000U, 0x2000U, true, false, {}},
              &error) &&
          itemGuestMemory.MapRegion(
              {"global-context", kItemGlobalContext, 0x6000U, true, false, {}},
              &error) &&
          itemGuestMemory.Write8(0x005879FCU, 0x45U) &&
          itemGuestMemory.Write8(0x005879FDU, 0x46U),
      "could not create typed Item I/II guest fixture");

  TopScreenExtendedInputFrame guestInput;
  Require(!HasTopScreenItemQueryOverrideInput(guestInput) &&
              !HasTopScreenSlotItemOverrideInput(guestInput),
          "idle TopScreen input unnecessarily intercepted native item queries");
  guestInput.ZrPressed = true;
  Require(HasTopScreenItemQueryOverrideInput(guestInput) &&
              !HasTopScreenSlotItemOverrideInput(guestInput),
          "TopScreen Item I input did not request a query override");
  auto guestItem =
      ResolveTopScreenItemQueryGuest(itemGuestMemory, 0x00349504U, guestInput);
  Require(guestItem == true,
          "typed guest bridge did not synthesize the Item I press lane");
  guestItem =
      ResolveTopScreenItemQueryGuest(itemGuestMemory, 0x002C3960U, guestInput);
  Require(guestItem == false,
          "typed guest bridge leaked an Item I press into its held lane");
  Require(itemGuestMemory.Write32(kItemGlobalActionState + 0x68U, 0x04000000U),
          "could not seed Item I suppression state");
  guestItem =
      ResolveTopScreenItemQueryGuest(itemGuestMemory, 0x00349504U, guestInput);
  Require(guestItem == false,
          "typed guest bridge ignored native Item I suppression");

  guestInput = {};
  guestInput.DpadLeftHeld = true;
  Require(HasTopScreenItemQueryOverrideInput(guestInput) &&
              HasTopScreenSlotItemOverrideInput(guestInput),
          "TopScreen compatibility input did not request its slot override");
  guestItem =
      ResolveTopScreenItemQueryGuest(itemGuestMemory, 0x00349504U, guestInput);
  Require(guestItem == true, "typed guest bridge lost D-pad Item I precedence");
  Require(itemGuestMemory.Write8(0x005879FCU, 0x10U),
          "could not alter Item I identity gate");
  guestItem =
      ResolveTopScreenItemQueryGuest(itemGuestMemory, 0x00349504U, guestInput);
  Require(guestItem == false,
          "typed guest bridge ignored the Item I identity gate");

  Require(itemGuestMemory.Write8(0x005879FCU, 0x45U) &&
              itemGuestMemory.Write32(kItemGlobalActionState + 0x68U, 0U),
          "could not restore Item I guest fixture");
  const auto itemIOverride = ResolveTopScreenSlotItemOverrideGuest(
      itemGuestMemory, kItemGlobalContext, 3U, guestInput);
  Require(itemIOverride == 0x45U,
          "typed slot bridge did not apply Item I compatibility");
  Require(itemGuestMemory.Write8(kItemGlobalContext + 0x5C75U, 1U),
          "could not seed positive native special state");
  Require(!ResolveTopScreenSlotItemOverrideGuest(
               itemGuestMemory, kItemGlobalContext, 3U, guestInput)
               .has_value(),
          "Item I compatibility bypassed positive native special state");
  Require(itemGuestMemory.Write8(kItemGlobalContext + 0x5C75U, 0xFFU),
          "could not seed negative native special state");
  Require(!ResolveTopScreenSlotItemOverrideGuest(
               itemGuestMemory, kItemGlobalContext, 3U, guestInput)
               .has_value(),
          "Item I compatibility bypassed negative native special state");

  guestInput = {};
  guestInput.DpadRightHeld = true;
  Require(itemGuestMemory.Write8(kItemGlobalContext + 0x5C75U, 0U),
          "could not clear native special state");
  Require(ResolveTopScreenSlotItemOverrideGuest(
              itemGuestMemory, kItemGlobalContext, 4U, guestInput) == 0x46U,
          "typed slot bridge did not apply Item II compatibility");
  Require(!ResolveTopScreenSlotItemOverrideGuest(
               itemGuestMemory, kItemGlobalContext, 2U, guestInput)
                  .has_value() &&
              !ResolveTopScreenItemQueryGuest(itemGuestMemory, 0xDEADBEEFU,
                                              guestInput)
                   .has_value(),
          "typed Item I/II bridge consumed an unrelated native case");

  constexpr std::uint32_t kAimPromptState = 0x004FDA6CU;
  constexpr std::uint32_t kAimSelectorState = 0x00506CB0U;
  constexpr std::uint32_t kPreviousAvailability = 0x0055B634U;
  constexpr std::uint32_t kNextAvailability = 0x0055B63CU;
  Require(itemGuestMemory.Write32(kAimPromptState + 0x50U, 1U) &&
              itemGuestMemory.Write32(kAimSelectorState + 0x38U, 2U) &&
              itemGuestMemory.Write32(kAimSelectorState + 0x30U, 2U) &&
              itemGuestMemory.Write32(kPreviousAvailability + 2U * 4U, 1U),
          "could not seed native aiming selector state");
  TopScreenAimProjectileCycleResult aimCycle;
  Require(ApplyTopScreenAimProjectileCycleGuest(
              itemGuestMemory, {.PreviousPressed = true}, &aimCycle,
              &error) &&
              aimCycle.Eligible && aimCycle.InitialSelector == 2U &&
              aimCycle.FinalSelector == 1U && aimCycle.Updates == 1U &&
              aimCycle.PromptWrites == 1U && aimCycle.LastPrompt == 4U,
          "previous aiming projectile did not follow payload state");
  std::uint32_t aimValue = 0;
  Require(itemGuestMemory.Read32(kAimPromptState + 0x38U, &aimValue) &&
              aimValue == 4U,
          "previous aiming projectile did not update the native prompt");

  Require(itemGuestMemory.Write32(kAimSelectorState + 0x30U, 1U) &&
              itemGuestMemory.Write32(kNextAvailability + 1U * 4U, 1U) &&
              ApplyTopScreenAimProjectileCycleGuest(
                  itemGuestMemory, {.NextPressed = true}, &aimCycle,
                  &error) &&
              aimCycle.FinalSelector == 2U && aimCycle.LastPrompt == 5U,
          "next aiming projectile did not follow payload state");
  Require(itemGuestMemory.Write32(kAimSelectorState + 0x30U, 2U) &&
              itemGuestMemory.Write32(kNextAvailability + 2U * 4U, 0U) &&
              ApplyTopScreenAimProjectileCycleGuest(
                  itemGuestMemory, {.NextPressed = true}, &aimCycle,
                  &error) &&
              aimCycle.Updates == 0U && aimCycle.FinalSelector == 2U,
          "aiming projectile ignored its native availability table");

  Require(itemGuestMemory.Write32(kAimSelectorState + 0x30U, 6U) &&
              ApplyTopScreenAimProjectileCycleGuest(
                  itemGuestMemory,
                  {.PreviousPressed = true, .NextPressed = true}, &aimCycle,
                  &error) &&
              aimCycle.Updates == 2U && aimCycle.FinalSelector == 6U &&
              aimCycle.PromptWrites == 0U,
          "high aiming selector range lost sequential edge semantics");
  Require(itemGuestMemory.Write32(kAimSelectorState + 0x38U, 0U) &&
              ApplyTopScreenAimProjectileCycleGuest(
                  itemGuestMemory, {.PreviousPressed = true}, &aimCycle,
                  &error) &&
              !aimCycle.Eligible && aimCycle.Updates == 0U,
          "aiming projectile consumed input outside native mode 2");

  constexpr std::uint32_t kGameplayRoot = 0x005043D4U;
  constexpr std::uint32_t kPauseContext = 0x0050AF34U;
  constexpr std::uint32_t kSceneExtension =
      kItemGlobalContext + 0x3000U;
  Require(itemGuestMemory.Write32(kGameplayRoot + 0x0CU,
                                  kItemGlobalContext) &&
              itemGuestMemory.Write8(kItemGlobalContext + 0x100U, 3U) &&
              itemGuestMemory.Write8(kItemGlobalContext + 0x101U, 2U) &&
              itemGuestMemory.Write16(kItemGlobalContext + 0x2DD4U,
                                      0x123U) &&
              itemGuestMemory.Write32(kItemGlobalContext + 0x20ACU, 0U) &&
              itemGuestMemory.Write32(kGameplayRoot + 0x18U, 0U) &&
              itemGuestMemory.Write32(kPauseContext + 0x34U, 2U) &&
              itemGuestMemory.Write32(kPauseContext + 0x68U, 0U) &&
              itemGuestMemory.Write32(kPauseContext + 0x70U, 0U),
          "could not seed TopScreen gameplay D-pad state");
  TopScreenGameplayDpadResult gameplayDpad;
  Require(PrepareTopScreenGameplayDpadGuest(
              itemGuestMemory, {.ViewPressed = true}, &gameplayDpad,
              &error) &&
              gameplayDpad.EligibleScene && gameplayDpad.NaviViewActivated &&
              !gameplayDpad.OcarinaQueryRequired,
          "D-pad Up did not activate the native Navi/view transition");
  Require(itemGuestMemory.Read32(kPauseContext + 0x38U, &aimValue) &&
              aimValue == 0x124U &&
              itemGuestMemory.Read32(kPauseContext + 0x34U, &aimValue) &&
              aimValue == 1U,
          "D-pad Up wrote the wrong native Navi/view state");

  Require(itemGuestMemory.Write32(kPauseContext + 0x34U, 2U) &&
              itemGuestMemory.Write32(kPauseContext + 0x70U, 1U) &&
              itemGuestMemory.Write32(0x00565660U, 0U) &&
              PrepareTopScreenGameplayDpadGuest(
                  itemGuestMemory, {.ViewPressed = true}, &gameplayDpad,
                  &error) &&
              gameplayDpad.NaviViewActivated &&
              itemGuestMemory.Read32(0x00565660U, &aimValue) &&
              aimValue == 1U,
          "Navi mode one did not emit its original native signal");

  Require(itemGuestMemory.Write32(kPauseContext + 0x34U, 2U) &&
              itemGuestMemory.Write32(kPauseContext + 0x70U, 0U) &&
              PrepareTopScreenGameplayDpadGuest(
                  itemGuestMemory,
                  {.ViewPressed = true, .OcarinaPressed = true}, &gameplayDpad,
                  &error) &&
              gameplayDpad.NaviViewActivated &&
              !gameplayDpad.OcarinaQueryRequired,
          "simultaneous D-pad edges lost payload Up-before-Down priority");

  Require(itemGuestMemory.Write32(kPauseContext + 0x34U, 2U) &&
              itemGuestMemory.Write32(kItemGlobalContext + 0x20ACU,
                                      kSceneExtension) &&
              itemGuestMemory.Write32(kSceneExtension + 0x12B8U, 1U) &&
              PrepareTopScreenGameplayDpadGuest(
                  itemGuestMemory, {.ViewPressed = true}, &gameplayDpad,
                  &error) &&
              !gameplayDpad.NaviViewActivated,
          "D-pad Up ignored the native Navi special owner");
  Require(itemGuestMemory.Write32(kItemGlobalContext + 0x20ACU, 0U) &&
              PrepareTopScreenGameplayDpadGuest(
                  itemGuestMemory,
                  {.ViewPressed = true,
                   .LeftShoulderHeld = true,
                   .RightShoulderHeld = true},
                  &gameplayDpad, &error) &&
              gameplayDpad.ShoulderChordSuppressed &&
              !gameplayDpad.NaviViewActivated,
          "L+R did not suppress the payload gameplay D-pad route");

  Require(itemGuestMemory.Write32(kPauseContext + 0x34U, 2U) &&
              PrepareTopScreenGameplayDpadGuest(
                  itemGuestMemory, {.OcarinaPressed = true}, &gameplayDpad,
                  &error) &&
              gameplayDpad.OcarinaQueryRequired &&
              CommitTopScreenGameplayOcarinaGuest(
                  itemGuestMemory, 0U, &gameplayDpad, &error) &&
              !gameplayDpad.OcarinaActivated,
          "rejected native ocarina eligibility was not preserved");
  Require(itemGuestMemory.Write32(kPauseContext + 0x34U, 2U) &&
              PrepareTopScreenGameplayDpadGuest(
                  itemGuestMemory, {.OcarinaPressed = true}, &gameplayDpad,
                  &error) &&
              CommitTopScreenGameplayOcarinaGuest(
                  itemGuestMemory, 1U, &gameplayDpad, &error) &&
              gameplayDpad.OcarinaActivated &&
              itemGuestMemory.Read32(kPauseContext + 0x60U, &aimValue) &&
              aimValue == 1U &&
              itemGuestMemory.Read32(kPauseContext + 0x34U, &aimValue) &&
              aimValue == 1U,
          "accepted native ocarina transition was not committed");
  Require(itemGuestMemory.Write32(kPauseContext + 0x34U, 2U) &&
              itemGuestMemory.Write32(kGameplayRoot + 0x18U, 1U) &&
              PrepareTopScreenGameplayDpadGuest(
                  itemGuestMemory, {.OcarinaPressed = true}, &gameplayDpad,
                  &error) &&
              !gameplayDpad.OcarinaQueryRequired,
          "D-pad Down ignored the native gameplay action blocker");

  const auto healthGeometry =
      BuildTopScreenHealthGeometry(0x25U, 0x50U, false, 7U);
  Require(healthGeometry.Positions[0].X == 8.0F &&
              healthGeometry.Positions[0].Y == 6.0F &&
              healthGeometry.Positions[9].X == 400.0F &&
              healthGeometry.Positions[9].Y == 400.0F,
          "health capacity layout is incorrect");
  Require(healthGeometry.AtlasOrigins[0].Y == 144.0F &&
              healthGeometry.AtlasOrigins[1].Y == 144.0F &&
              healthGeometry.AtlasOrigins[2].Y == 112.0F &&
              healthGeometry.PulseHeart == 2U &&
              healthGeometry.PulseExpansion == 3.0F,
          "health atlas or pulse state is incorrect");
  const auto alternateHealth =
      BuildTopScreenHealthGeometry(0U, 0x10U, true, 0U);
  Require(alternateHealth.AtlasOrigins[0].Y == 0.0F &&
              alternateHealth.PulseExpansion == 0.0F,
          "alternate or empty health state is incorrect");

  oot3d::ui::UiHudMagicContent magicContent;
  magicContent.acquired = oot3d::ui::KnownUiContentValue(true);
  magicContent.double_magic_acquired = oot3d::ui::KnownUiContentValue(false);
  magicContent.current = oot3d::ui::KnownUiContentValue<std::int8_t>(24);
  const auto indicator = BuildTopScreenMagicMeterGeometry(magicContent, 0xA0U);
  Require(
      indicator.Visible && indicator.Positions[0].Y == 15.75F &&
          indicator.Sizes[1].X == 49.5F && indicator.Positions[2].X == 60.5F &&
          indicator.Sizes[3].X == 29.25F && indicator.AtlasSizes[2].X == -8.0F,
      "primary magic meter geometry is incorrect");
  magicContent.double_magic_acquired = oot3d::ui::KnownUiContentValue(true);
  magicContent.current = oot3d::ui::KnownUiContentValue<std::int8_t>(48);
  const auto alternateIndicator =
      BuildTopScreenMagicMeterGeometry(magicContent, 0xB0U);
  Require(alternateIndicator.Positions[0].Y == 25.5F &&
              alternateIndicator.Sizes[1].X == 111.0F &&
              alternateIndicator.Positions[2].X == 122.0F &&
              alternateIndicator.Sizes[3].X == 60.0F,
          "double magic meter geometry is incorrect");
  magicContent.acquired = oot3d::ui::KnownUiContentValue(false);
  Require(!BuildTopScreenMagicMeterGeometry(magicContent, 0U).Visible,
          "unacquired magic meter remained visible");
  magicContent.acquired = {};
  Require(!BuildTopScreenMagicMeterGeometry(magicContent, 0U).Visible,
          "unknown magic state produced a meter");

  oot3d::ui::UiHudContentSnapshot hudContent;
  hudContent.health.current_units =
      oot3d::ui::KnownUiContentValue<std::int16_t>(0x25);
  hudContent.health.capacity_units =
      oot3d::ui::KnownUiContentValue<std::uint16_t>(0x50U);
  hudContent.health.double_defense_acquired =
      oot3d::ui::KnownUiContentValue(false);
  oot3d::ui::UiTextureIdentity heartTexture;
  heartTexture.guest_resource_address = 0x00560000U;
  heartTexture.guest_surface_address = 0x14001234U;
  heartTexture.semantic_name = "oot3d/native/pause_shared/pause_top_page";
  std::vector<oot3d::ui::UiPrimitive> heartPrimitives;
  Require(AppendTopScreenHealthPresentation(hudContent, heartTexture, 7U,
                                            heartPrimitives) == 5U &&
              heartPrimitives.size() == 5U,
          "TopScreen health presenter emitted the wrong heart count");
  Require(heartPrimitives[0].texture.guest_surface_address == 0x14001234U &&
              heartPrimitives[0].uv.x == 240.0F / 512.0F &&
              heartPrimitives[2].uv.y == 112.0F / 256.0F &&
              heartPrimitives[2].destination.x == 29.0F &&
              heartPrimitives[2].destination.width == 15.0F,
          "TopScreen health presenter lost native texture or pulse geometry");

  std::vector<oot3d::ui::UiPrimitive> indicatorPrimitives;
  Require(AppendTopScreenMagicMeterPresentation(
              alternateIndicator, heartTexture, indicatorPrimitives) == 4U &&
              indicatorPrimitives.size() == 4U &&
              indicatorPrimitives[2].uv.width == -8.0F / 512.0F &&
              indicatorPrimitives[3].destination.width == 60.0F &&
              indicatorPrimitives[0].role ==
                  oot3d::ui::UiPrimitiveRole::MagicFrame &&
              indicatorPrimitives[3].role ==
                  oot3d::ui::UiPrimitiveRole::MagicFill &&
              indicatorPrimitives[3].layer == 11U,
          "TopScreen magic meter presentation is incorrect");

  const auto touchCluster =
      BuildTopScreenTouchClusterGeometry({0.0F, 1.0F, 2.0F, 3.0F}, 0.5F);
  Require(touchCluster.Positions[0].X == 342.0F &&
              touchCluster.Positions[0].Y == 4.0F &&
              touchCluster.Positions[3].X == 342.0F &&
              touchCluster.Positions[3].Y == 55.0F &&
              touchCluster.Positions[4].X == 232.0F &&
              touchCluster.Positions[4].Y == 8.0F &&
              touchCluster.Positions[5].X == 14.0F &&
              touchCluster.Positions[5].Y == 52.0F &&
              touchCluster.AtlasOrigins[0].X == 386.0F &&
              touchCluster.AtlasOrigins[5].X == 432.0F &&
              touchCluster.AtlasOrigins[5].Y == 218.0F &&
              touchCluster.Alpha[4] == 1.0F,
          "TopScreen touch cluster payload geometry is incorrect");
  std::vector<oot3d::ui::UiPrimitive> touchPrimitives;
  Require(AppendTopScreenTouchClusterPresentation(touchCluster, heartTexture,
                                                  touchPrimitives) == 6U &&
              touchPrimitives[0].owner_address == 0x005C9940U &&
              touchPrimitives[0].uv.x == 386.0F / 512.0F &&
              touchPrimitives[0].uv.y == 201.0F / 256.0F &&
              touchPrimitives[0].uv.height == 48.0F / 256.0F &&
              touchPrimitives[0].color.alpha == 0.5F &&
              touchPrimitives[4].color.alpha == 1.0F &&
              touchPrimitives[5].destination.width == 30.0F,
          "TopScreen touch cluster presentation is incorrect");

  NativeA32Memory touchStateMemory;
  Require(touchStateMemory.MapRegion({"topscreen-touch-state-fixture",
                                      0x00500000U,
                                      0x00040000U,
                                      true,
                                      false,
                                      {}},
                                     &error),
          "could not map TopScreen touch-state fixture");
  constexpr std::uint32_t kTouchRenderer = 0x00520000U;
  constexpr std::uint32_t kTouchColors = 0x00521000U;
  Require(touchStateMemory.Write32(0x0050AF38U, kTouchRenderer) &&
              touchStateMemory.Write32(kTouchRenderer + 0x18U, kTouchColors) &&
              touchStateMemory.Write32(0x0050AF34U + 0x4CU, 1U) &&
              touchStateMemory.Write32(0x0050AF34U + 0x50U, 0U) &&
              touchStateMemory.Write32(0x0050AF34U + 0x44U, 0U) &&
              touchStateMemory.Write32(0x0050AF34U + 0x48U, 0U),
          "could not seed TopScreen touch-state fixture");
  for (std::uint32_t vertex = 0U; vertex < 4U; ++vertex) {
    Require(touchStateMemory.Write32(kTouchColors + 34U * 0x40U +
                                         vertex * 0x10U + 0x0CU,
                                     std::bit_cast<std::uint32_t>(0.6F)),
            "could not seed TopScreen touch alpha");
  }
  TopScreenExtendedInputFrame touchInput;
  touchInput.ZrHeld = true;
  TopScreenTouchDynamicState dynamicTouch;
  Require(ReadTopScreenTouchDynamicState(touchStateMemory, touchInput,
                                         &dynamicTouch, &error) &&
              dynamicTouch.VerticalOffsets ==
                  std::array<float, 4>{2.0F, 0.0F, 2.0F, 0.0F} &&
              std::abs(dynamicTouch.Alpha - 0.6F) < 0.001F,
          "TopScreen touch dynamic state is not sourced from native gameplay");

  TopScreenAuxiliaryTouchInputs auxiliaryInputs;
  auxiliaryInputs.HasPlayState = true;
  auxiliaryInputs.StateType = 3U;
  auxiliaryInputs.StateSubtype = 2U;
  auxiliaryInputs.PlayerHudMode = 1;
  auxiliaryInputs.NestedSceneOwnerActive = true;
  auxiliaryInputs.AlternateHudRendererActive = true;
  const auto auxiliaryGeometry = BuildTopScreenAuxiliaryTouchGeometry(
      auxiliaryInputs, dynamicTouch.VerticalOffsets, dynamicTouch.Alpha);
  Require(auxiliaryGeometry.Quads[0].Visible &&
              auxiliaryGeometry.Quads[0].Position.X == 338.0F &&
              auxiliaryGeometry.Quads[0].Position.Y == 8.0F &&
              auxiliaryGeometry.Quads[2].Visible &&
              auxiliaryGeometry.Quads[2].Position.X == 227.0F &&
              auxiliaryGeometry.Quads[2].AtlasOrigin.Y == 174.0F &&
              !auxiliaryGeometry.Quads[3].Visible &&
              auxiliaryGeometry.Quads[4].Position.X == 22.0F &&
              auxiliaryGeometry.Quads[4].Size.Y == 16.0F &&
              auxiliaryGeometry.Quads[4].AtlasSize.Y == 48.0F,
          "TopScreen group-39 auxiliary geometry is incorrect");
  std::vector<oot3d::ui::UiPrimitive> auxiliaryPrimitives;
  Require(AppendTopScreenAuxiliaryTouchPresentation(
              auxiliaryGeometry, heartTexture, auxiliaryPrimitives) == 4U &&
              auxiliaryPrimitives[0].source_quad == 34U &&
              auxiliaryPrimitives[2].source_quad == 36U &&
              auxiliaryPrimitives[3].source_quad == 38U &&
              std::abs(auxiliaryPrimitives[0].color.alpha - 0.54F) < 0.001F,
          "TopScreen group-39 auxiliary presenter is incomplete");
  const auto restorationTouchCluster = BuildTopScreenTouchClusterGeometry(
      {0.0F, 1.0F, 2.0F, 3.0F}, 0.5F,
      TopScreenHudLayout::Restoration);
  Require(restorationTouchCluster.Alpha[0] == 0.5F &&
              restorationTouchCluster.Alpha[1] == 0.5F &&
              restorationTouchCluster.Alpha[2] == 0.0F &&
              restorationTouchCluster.Alpha[3] == 0.0F &&
              restorationTouchCluster.Positions[4].X == 342.0F &&
              restorationTouchCluster.Positions[4].Y == 52.0F,
          "Restoration touch-cluster geometry is incorrect");
  const auto restorationAuxiliary = BuildTopScreenAuxiliaryTouchGeometry(
      auxiliaryInputs, dynamicTouch.VerticalOffsets, dynamicTouch.Alpha,
      TopScreenHudLayout::Restoration);
  Require(restorationAuxiliary.Quads[2].Position.X == 337.0F &&
              restorationAuxiliary.Quads[2].Position.Y == 54.0F,
          "Restoration auxiliary control geometry is incorrect");
  const auto restorationLabels = BuildTopScreenTouchLabelsGeometry(
      dynamicTouch.VerticalOffsets, dynamicTouch.Alpha,
      TopScreenHudLayout::Restoration);
  Require(!restorationLabels.Quads[0].Visible &&
              !restorationLabels.Quads[1].Visible,
          "Restoration touch labels were not suppressed");
  auxiliaryInputs.TouchLayoutState = 1U;
  const auto fixedAuxiliaryGeometry = BuildTopScreenAuxiliaryTouchGeometry(
      auxiliaryInputs, dynamicTouch.VerticalOffsets, 1.0F);
  Require(fixedAuxiliaryGeometry.Quads[4].Position.X == 21.0F &&
              fixedAuxiliaryGeometry.Quads[4].Position.Y == 43.0F &&
              fixedAuxiliaryGeometry.Quads[4].Size.X == 20.0F &&
              fixedAuxiliaryGeometry.Quads[4].AtlasOrigin.Y == 56.0F &&
              fixedAuxiliaryGeometry.Quads[4].AtlasSize.Y == 24.0F,
          "TopScreen group-39 fixed layout is incorrect");

  NativeA32Memory compositorGateMemory;
  Require(compositorGateMemory.MapRegion({"topscreen-compositor-gate-fixture",
                                          0x004FC000U,
                                          0x00094000U,
                                          true,
                                          false,
                                          {}},
                                         &error),
          "could not map TopScreen compositor-gate fixture");
  constexpr std::uint32_t kGatePlayState = 0x00520000U;
  constexpr std::uint32_t kGateSceneOwner = 0x00540000U;
  Require(compositorGateMemory.Write32(0x005043D4U + 0x0CU, kGatePlayState) &&
              compositorGateMemory.Write8(kGatePlayState + 0x100U, 3U) &&
              compositorGateMemory.Write8(kGatePlayState + 0x101U, 2U) &&
              compositorGateMemory.Write16(0x00587958U + 0x44U, 0x30U) &&
              compositorGateMemory.Write16(kGatePlayState + 0x2E30U, 1U) &&
              compositorGateMemory.Write16(kGatePlayState + 0x2DD4U, 1U) &&
              compositorGateMemory.Write32(kGatePlayState + 0x20ACU,
                                           kGateSceneOwner) &&
              compositorGateMemory.Write32(kGateSceneOwner + 0x12B8U, 1U) &&
              compositorGateMemory.Write32(0x004FC648U + 0x40U, 1U),
          "could not seed TopScreen compositor gate");
  TopScreenHudCompositorGate compositorGate;
  Require(ReadTopScreenHudCompositorGate(compositorGateMemory, &compositorGate,
                                         &error) &&
              compositorGate.Draw,
          "TopScreen compositor rejected the native gameplay state");
  Require(compositorGateMemory.Write32(0x005066F8U + 0x34U, 1U) &&
              ReadTopScreenHudCompositorGate(compositorGateMemory,
                                             &compositorGate, &error) &&
              !compositorGate.Draw,
          "TopScreen compositor ignored a competing native pause owner");
  TopScreenAuxiliaryTouchInputs nativeAuxiliaryInputs;
  Require(ReadTopScreenAuxiliaryTouchInputs(compositorGateMemory,
                                            &nativeAuxiliaryInputs, &error) &&
              nativeAuxiliaryInputs.HasPlayState &&
              nativeAuxiliaryInputs.NestedSceneOwnerActive &&
              nativeAuxiliaryInputs.AlternateHudRendererActive &&
              nativeAuxiliaryInputs.PlayerHudMode == 1,
          "TopScreen group-39 producer is not reading native owner state");

  constexpr std::uint32_t kPlayState = 0x00522000U;
  constexpr std::uint32_t kItemRenderer = 0x00523000U;
  constexpr std::uint32_t kItemPositions = 0x00524000U;
  constexpr std::uint32_t kItemUvs = 0x00525000U;
  constexpr std::uint32_t kItemColors = 0x00526000U;
  constexpr std::uint32_t kItemTranslations = 0x00527000U;
  Require(
      touchStateMemory.Write32(0x0050AF34U + 0x14U, kPlayState) &&
          touchStateMemory.Write32(0x005043D4U + 0x14U, 0U) &&
          touchStateMemory.Write32(kPlayState + 0x10CU, kItemRenderer) &&
          touchStateMemory.Write32(kItemRenderer + 0x0CU, kItemPositions) &&
          touchStateMemory.Write32(kItemRenderer + 0x14U, kItemUvs) &&
          touchStateMemory.Write32(kItemRenderer + 0x18U, kItemColors) &&
          touchStateMemory.Write32(kItemRenderer + 0x1CU, kItemTranslations),
      "could not seed TopScreen native item renderer");
  const std::array<float, 12> itemPositionQuad{256.0F, -8.0F,  0.0F,   276.0F,
                                               -8.0F,  0.0F,   256.0F, 12.0F,
                                               0.0F,   276.0F, 12.0F,  0.0F};
  const std::array<float, 8> itemUvQuad{0.0F, 0.8F, 0.1F, 0.8F,
                                        0.0F, 0.6F, 0.1F, 0.6F};
  std::array<float, 16> itemColorQuad{};
  for (std::size_t vertex = 0; vertex < 4U; ++vertex) {
    itemColorQuad[vertex * 4U] = 1.0F;
    itemColorQuad[vertex * 4U + 1U] = 1.0F;
    itemColorQuad[vertex * 4U + 2U] = 1.0F;
    itemColorQuad[vertex * 4U + 3U] = 0.5F;
  }
  const std::array<float, 2> itemTranslation{};
  for (std::uint32_t quad = 0U; quad < 5U; ++quad) {
    Require(
        touchStateMemory.WriteBytes(
            kItemPositions + quad * 0x30U,
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t *>(itemPositionQuad.data()),
                sizeof(itemPositionQuad))) &&
            touchStateMemory.WriteBytes(
                kItemUvs + quad * 0x20U,
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t *>(itemUvQuad.data()),
                    sizeof(itemUvQuad))) &&
            touchStateMemory.WriteBytes(
                kItemColors + quad * 0x40U,
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t *>(
                        itemColorQuad.data()),
                    sizeof(itemColorQuad))) &&
            touchStateMemory.WriteBytes(
                kItemTranslations + quad * 0x08U,
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t *>(
                        itemTranslation.data()),
                    sizeof(itemTranslation))),
        "could not seed TopScreen native item quad");
  }
  std::vector<oot3d::ui::UiPrimitive> nativeItemCopies;
  Require(AppendTopScreenNativeItemIconCopies(
              touchStateMemory, dynamicTouch.VerticalOffsets,
              dynamicTouch.Alpha, itemIconsTexture, nativeItemCopies, &error) &&
              nativeItemCopies.size() == 5U &&
              nativeItemCopies[0].destination.x == 360.0F &&
              nativeItemCopies[0].destination.y == 24.0F &&
              nativeItemCopies[0].destination.width == 12.0F &&
              nativeItemCopies[0].destination.height == 12.0F &&
              std::abs(nativeItemCopies[0].uv.y - 0.2F) < 0.001F &&
              std::abs(nativeItemCopies[0].uv.height - 0.2F) < 0.001F &&
              std::abs(nativeItemCopies[0].color.alpha - 0.3F) < 0.001F &&
              nativeItemCopies[0].descriptor_address == kItemRenderer,
          "TopScreen native item-copy table is not reproduced exactly");

  const std::array<float, 2> dpadItemTranslation{-264.0F, 184.0F};
  Require(
      touchStateMemory.WriteBytes(
          kItemTranslations + 4U * 0x08U,
          std::span<const std::uint8_t>(
              reinterpret_cast<const std::uint8_t *>(
                  dpadItemTranslation.data()),
              sizeof(dpadItemTranslation))),
      "could not move the native item fixture into the D-pad lane");
  std::vector<oot3d::ui::UiPrimitive> dpadItemVisibleCopies;
  Require(AppendTopScreenNativeItemIconCopies(
              touchStateMemory, dynamicTouch.VerticalOffsets,
              dynamicTouch.Alpha, itemIconsTexture, dpadItemVisibleCopies,
              &error, true) &&
              dpadItemVisibleCopies.size() == 5U,
          "enabled TopScreen D-pad icons suppressed the native D-pad lane");
  std::vector<oot3d::ui::UiPrimitive> dpadItemSuppressedCopies;
  Require(AppendTopScreenNativeItemIconCopies(
              touchStateMemory, dynamicTouch.VerticalOffsets,
              dynamicTouch.Alpha, itemIconsTexture, dpadItemSuppressedCopies,
              &error, false) &&
              dpadItemSuppressedCopies.size() == 4U,
          "disabled TopScreen D-pad icons did not suppress only its lane");

  Require(touchStateMemory.MapRegion({"topscreen-counter-save-fixture",
                                      0x00587000U,
                                      0x00003000U,
                                      true,
                                      false,
                                      {}},
                                     &error),
          "could not map TopScreen counter save fixture");
  Require(touchStateMemory.Write16(0x00587958U + 0x48U, 123U) &&
              touchStateMemory.Write16(0x00587958U + 0x1592U, 0U) &&
              touchStateMemory.Write8(0x00587958U + 0xD4U, 5U) &&
              touchStateMemory.Write8(kPlayState + 0x100U, 3U) &&
              touchStateMemory.Write8(kPlayState + 0x101U, 2U) &&
              touchStateMemory.Write16(kPlayState + 0x104U, 3U) &&
              touchStateMemory.Write32(kPlayState + 0x224U, 4U) &&
              touchStateMemory.Write8(kPlayState + 0x435U, 1U) &&
              touchStateMemory.Write32(kPlayState + 0x32CU, 42U) &&
              touchStateMemory.Write32(kPlayState + 0x10U, 0U),
          "could not seed TopScreen native counters");
  for (std::uint32_t index = 1U; index < 4U; ++index) {
    Require(touchStateMemory.Write8(kPlayState + 0x435U + index, 0U) &&
                touchStateMemory.Write32(kPlayState + 0x32CU + index * 4U,
                                         0xFFFFFFFFU),
            "could not suppress unused TopScreen ammo counter");
  }
  const oot3d::ui::UiTextureIdentity numberGlyphTexture{
      0x005D2200U, 0x18022000U, "oot3d/native/pause/number_glyphs"};
  std::vector<oot3d::ui::UiPrimitive> nativeCounters;
  Require(AppendTopScreenNativeCounters(
              touchStateMemory, dynamicTouch.VerticalOffsets,
              numberGlyphTexture, nativeCounters, &error) &&
              nativeCounters.size() == 6U &&
              nativeCounters[0].uv.x == 36.0F / 256.0F &&
              nativeCounters[0].uv.y == 60.0F / 128.0F &&
              nativeCounters[0].destination.y == 218.0F &&
              nativeCounters[3].descriptor_address == 0x00587958U + 0xD4U &&
              nativeCounters[4].destination.x == 389.0F &&
              nativeCounters[4].destination.y == 49.0F &&
              nativeCounters[4].role ==
                  oot3d::ui::UiPrimitiveRole::CounterDigit,
          "TopScreen native counter reconstruction is incomplete");

  const auto fixedPauseEdges =
      BuildTopScreenPauseEdgeGeometry(true, 5U, 0U, 0U, 0, 1.0F, 1.0F);
  Require(fixedPauseEdges.Positions[0].X == 288.0F &&
              fixedPauseEdges.Positions[1].X == 0.0F &&
              fixedPauseEdges.Sizes[0].X == 32.0F &&
              fixedPauseEdges.Sizes[0].Y == 240.0F &&
              fixedPauseEdges.AtlasOrigins[0].X == 192.0F &&
              fixedPauseEdges.AtlasSizes[1].X == -64.0F &&
              fixedPauseEdges.RgbScale == 1.0F,
          "TopScreen pause-edge fixed geometry is incorrect");
  const auto transitioningPauseEdges =
      BuildTopScreenPauseEdgeGeometry(true, 3U, 1U, 0x7FU, 10, 1.0F, 0.8F);
  Require(transitioningPauseEdges.Positions[0].X == 304.0F &&
              transitioningPauseEdges.Positions[1].X == -16.0F &&
              transitioningPauseEdges.RgbScale > 0.2F &&
              transitioningPauseEdges.RgbScale < 0.21F,
          "TopScreen pause-edge transition math is incorrect");
  std::vector<oot3d::ui::UiPrimitive> pauseEdgePrimitives;
  const oot3d::ui::UiTextureIdentity itemPageTexture{
      0x005D2300U, 0x18023000U, "oot3d/native/pause_shared/item_page"};
  Require(AppendTopScreenPauseEdgePresentation(fixedPauseEdges, itemPageTexture,
                                               pauseEdgePrimitives) == 2U &&
              pauseEdgePrimitives[0].owner_address == 0x005CD1E0U &&
              pauseEdgePrimitives[0].texture.semantic_name ==
                  "oot3d/native/pause_shared/item_page" &&
              pauseEdgePrimitives[0].uv.x == 192.0F / 256.0F &&
              pauseEdgePrimitives[0].uv.y == 0.0F &&
              pauseEdgePrimitives[0].uv.height == 240.0F / 256.0F &&
              pauseEdgePrimitives[1].uv.width == -64.0F / 256.0F &&
              std::abs(pauseEdgePrimitives[0].destination.x -
                       360.0F) < 0.001F &&
              pauseEdgePrimitives[0].destination.width == 40.0F &&
              pauseEdgePrimitives[0].destination.height == 240.0F &&
              std::abs(pauseEdgePrimitives[1].destination.x) < 0.001F,
          "TopScreen pause-edge presentation is incorrect");

  NativeA32Memory nativeTouchMemory;
  Require(nativeTouchMemory.MapRegion(
              {"touch-state", 0x0050A000U, 0x1000U, true, false, {}}, &error) &&
              nativeTouchMemory.MapRegion(
                  {"touch-streams", 0x00600000U, 0x10000U, true, false, {}},
                  &error),
          "could not map native touch-copy fixture");
  constexpr std::uint32_t kRenderer = 0x0050A800U;
  constexpr std::uint32_t kPositions = 0x00600000U;
  constexpr std::uint32_t kUvs = 0x00602000U;
  constexpr std::uint32_t kColors = 0x00604000U;
  constexpr std::uint32_t kTranslations = 0x00608000U;
  Require(nativeTouchMemory.Write32(0x0050AF38U, kRenderer) &&
              nativeTouchMemory.Write32(kRenderer + 0x0CU, kPositions) &&
              nativeTouchMemory.Write32(kRenderer + 0x14U, kUvs) &&
              nativeTouchMemory.Write32(kRenderer + 0x18U, kColors) &&
              nativeTouchMemory.Write32(kRenderer + 0x1CU, kTranslations),
          "could not seed native touch renderer");
  const std::array<float, 12> sourcePositions{0.0F, 0.0F,  0.0F,  10.0F,
                                              0.0F, 0.0F,  0.0F,  20.0F,
                                              0.0F, 10.0F, 20.0F, 0.0F};
  const std::array<float, 8> sourceUvs{0.0F, 0.9F, 0.2F, 0.9F,
                                       0.0F, 0.7F, 0.2F, 0.7F};
  std::array<float, 16> sourceColors{};
  for (std::size_t vertex = 0; vertex < 4U; ++vertex) {
    sourceColors[vertex * 4U + 0U] = 1.0F;
    sourceColors[vertex * 4U + 1U] = 0.5F;
    sourceColors[vertex * 4U + 2U] = 0.25F;
    sourceColors[vertex * 4U + 3U] = 0.75F;
  }
  constexpr std::array<std::array<std::int16_t, 3>, 10> sourceContracts{
      {{{34, 278, 149}},
       {{35, 278, 149}},
       {{26, 2, 111}},
       {{27, 2, 151}},
       {{86, 88, 8}},
       {{87, 88, 8}},
       {{88, 88, 8}},
       {{89, 88, 8}},
       {{90, 88, 8}},
       {{91, 88, 8}}}};
  const auto writeFloats = [&nativeTouchMemory](std::uint32_t address,
                                                const auto &values) {
    return nativeTouchMemory.WriteBytes(
        address, std::span<const std::uint8_t>(
                     reinterpret_cast<const std::uint8_t *>(values.data()),
                     values.size() * sizeof(float)));
  };
  for (const auto &contract : sourceContracts) {
    const auto source = static_cast<std::uint32_t>(contract[0]);
    const float nativeScreenLane =
        source == 27U || source >= 86U ? 400.0F : 0.0F;
    const std::array<float, 2> translation{static_cast<float>(contract[1]) +
                                               nativeScreenLane,
                                           static_cast<float>(contract[2])};
    Require(writeFloats(kPositions + source * 0x30U, sourcePositions) &&
                writeFloats(kUvs + source * 0x20U, sourceUvs) &&
                writeFloats(kColors + source * 0x40U, sourceColors) &&
                writeFloats(kTranslations + source * 0x08U, translation),
            "could not seed a native touch source quad");
  }
  std::vector<oot3d::ui::UiPrimitive> copiedTouchPrimitives;
  TopScreenNativeTouchCopyStats copyStats;
  Require(AppendTopScreenNativeTouchCopies(nativeTouchMemory, heartTexture,
                                           copiedTouchPrimitives, &copyStats,
                                           &error) &&
              copyStats.SourceQuadsRead == 10U &&
              copyStats.PrimitivesEmitted == 10U &&
              copyStats.AlphaVisiblePrimitives == 10U &&
              copyStats.HorseStaminaQuadsRead == 6U &&
              copyStats.AlphaVisibleHorseStaminaPrimitives == 6U &&
              copiedTouchPrimitives[0].source_quad == 34U &&
              copiedTouchPrimitives[0].role ==
                  oot3d::ui::UiPrimitiveRole::TouchControl &&
              copiedTouchPrimitives[4].source_quad == 86U &&
              copiedTouchPrimitives[4].role ==
                  oot3d::ui::UiPrimitiveRole::HorseStamina &&
              copiedTouchPrimitives[0].destination.x == 229.0F &&
              copiedTouchPrimitives[0].destination.y == 11.0F &&
              copiedTouchPrimitives[0].destination.width == 10.0F &&
              copiedTouchPrimitives[0].destination.height == 20.0F &&
              copiedTouchPrimitives[3].destination.x == 404.0F &&
              copiedTouchPrimitives[4].destination.x == 528.0F &&
              std::abs(copiedTouchPrimitives[0].uv.y - 0.1F) < 0.001F &&
              std::abs(copiedTouchPrimitives[0].uv.height - 0.2F) < 0.001F &&
              copiedTouchPrimitives[0].color.alpha == 0.75F &&
              std::abs(copiedTouchPrimitives[4].uv.height -
                       (0.9F - (0.7F + 1.0F / 256.0F))) < 0.001F,
          "native TopScreen touch-copy consumer is incorrect");
  TopScreenUiConfig restorationCopyConfig;
  restorationCopyConfig.HudLayout = TopScreenHudLayout::Restoration;
  std::vector<oot3d::ui::UiPrimitive> restorationTouchCopies;
  Require(AppendTopScreenNativeTouchCopies(
              nativeTouchMemory, heartTexture, restorationTouchCopies, nullptr,
              &error, &restorationCopyConfig) &&
              restorationTouchCopies.size() == 10U &&
              restorationTouchCopies[0].destination.x == 339.0F &&
              restorationTouchCopies[0].destination.y == 55.0F &&
              restorationTouchCopies[2].destination.x == 4.0F,
          "Restoration native touch-copy transform is incorrect");

  ApplyTopScreenGameplayCanvas(copiedTouchPrimitives);
  Require(std::abs(copiedTouchPrimitives[0].destination.x - 229.0F) <
                  0.001F &&
              !copiedTouchPrimitives[3].visible &&
              !copiedTouchPrimitives[4].visible,
          "TopScreen canvas did not preserve native off-screen visibility");
  std::vector<oot3d::ui::UiPrimitive> clippedCanvasPrimitive(1U);
  clippedCanvasPrimitive[0].destination = {390.0F, 230.0F, 20.0F, 20.0F};
  clippedCanvasPrimitive[0].uv = {0.0F, 0.0F, 1.0F, 1.0F};
  ApplyTopScreenGameplayCanvas(clippedCanvasPrimitive);
  Require(std::abs(clippedCanvasPrimitive[0].destination.x - 390.0F) <
                  0.001F &&
              clippedCanvasPrimitive[0].destination.width == 10.0F &&
              clippedCanvasPrimitive[0].destination.height == 10.0F &&
              clippedCanvasPrimitive[0].uv.width == 0.5F &&
              clippedCanvasPrimitive[0].uv.height == 0.5F,
          "TopScreen canvas edge clipping is incorrect");

  std::vector<oot3d::ui::UiPrimitive> scaledHud(4U);
  scaledHud[0].destination = {10.0F, 20.0F, 20.0F, 10.0F};
  scaledHud[1].destination = {370.0F, 20.0F, 20.0F, 10.0F};
  scaledHud[2].destination = {10.0F, 210.0F, 20.0F, 10.0F};
  scaledHud[3].destination = {370.0F, 210.0F, 20.0F, 10.0F};
  TopScreenUiConfig scaledHudConfig;
  scaledHudConfig.HudScale = 0.8F;
  ApplyTopScreenHudScale(scaledHud, scaledHudConfig);
  Require(scaledHud[0].destination.x == 12.0F &&
              scaledHud[0].destination.y == 17.0F &&
              scaledHud[0].destination.width == 16.0F &&
              scaledHud[0].destination.height == 8.0F &&
              scaledHud[1].destination.x == 372.0F &&
              scaledHud[1].destination.y == 17.0F &&
              scaledHud[2].destination.x == 12.0F &&
              scaledHud[2].destination.y == 215.0F &&
              scaledHud[3].destination.x == 372.0F &&
              scaledHud[3].destination.y == 215.0F,
          "TopScreen 2.1.1 quadrant HUD transform is incorrect");

  std::vector<oot3d::ui::UiPrimitive> scaledSpecialHud(4U);
  scaledSpecialHud[0].role = oot3d::ui::UiPrimitiveRole::Timer;
  scaledSpecialHud[0].destination = {224.0F, 8.0F, 32.0F, 32.0F};
  scaledSpecialHud[1].role = oot3d::ui::UiPrimitiveRole::HorseStamina;
  scaledSpecialHud[1].destination = {330.0F, 8.0F, 48.0F, 12.0F};
  scaledSpecialHud[2].role = oot3d::ui::UiPrimitiveRole::ActionButton;
  scaledSpecialHud[2].destination = {342.0F, 4.0F, 32.0F, 32.0F};
  scaledSpecialHud[3].role = oot3d::ui::UiPrimitiveRole::AmmoCounter;
  scaledSpecialHud[3].destination = {382.0F, 44.0F, 14.0F, 14.0F};
  ApplyTopScreenHudScale(scaledSpecialHud, scaledHudConfig);
  const auto near = [](float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
  };
  Require(
      near(scaledSpecialHud[0].destination.x, 255.2F) &&
          near(scaledSpecialHud[0].destination.y, 7.4F) &&
          near(scaledSpecialHud[0].destination.width, 25.6F) &&
          near(scaledSpecialHud[1].destination.x, 340.0F) &&
          near(scaledSpecialHud[1].destination.y, 7.4F) &&
          near(scaledSpecialHud[1].destination.width, 38.4F) &&
          near(scaledSpecialHud[2].destination.x, 349.6F) &&
          near(scaledSpecialHud[2].destination.y, 4.2F) &&
          near(scaledSpecialHud[3].destination.x, 381.6F) &&
          near(scaledSpecialHud[3].destination.y, 36.2F) &&
          near(scaledSpecialHud[3].destination.width, 11.2F),
      "TopScreen timer, Epona B, carrot and item-digit transform diverged");

  std::vector<oot3d::ui::UiPrimitive> marginHud(3U);
  marginHud[0].destination = {10.0F, 20.0F, 20.0F, 10.0F};
  marginHud[1].destination = {370.0F, 210.0F, 20.0F, 10.0F};
  marginHud[2].role = oot3d::ui::UiPrimitiveRole::MagicFrame;
  marginHud[2].destination = {10.0F, 30.0F, 40.0F, 6.0F};
  TopScreenUiConfig marginHudConfig;
  marginHudConfig.HudScale = 1.0F;
  marginHudConfig.HudMarginX = 8;
  marginHudConfig.HudMarginY = -2;
  marginHudConfig.MagicBarY = 7U;
  ApplyTopScreenHudScale(marginHud, marginHudConfig);
  Require(near(marginHud[0].destination.x, 18.0F) &&
              near(marginHud[0].destination.y, 18.0F) &&
              near(marginHud[1].destination.x, 362.0F) &&
              near(marginHud[1].destination.y, 212.0F) &&
              near(marginHud[2].destination.x, 18.0F) &&
              near(marginHud[2].destination.y, 35.0F),
          "TopScreen 2.1.1 HUD margins or magic-bar offset diverged");

  constexpr std::uint32_t kTouchQuadPlayState = 0x00609000U;
  constexpr std::uint32_t kNativeNaviViewEyeQuad = 28U;
  std::array<float, 12> touchQuadPositions{4.0F, 3.0F,  0.0F,  52.0F,
                                           3.0F, 0.0F,  4.0F,  51.0F,
                                           0.0F, 52.0F, 51.0F, 0.0F};
  Require(
      nativeTouchMemory.Write32(kRenderer, 92U) &&
          nativeTouchMemory.Write32(0x0050AF34U + 0x34U, 2U) &&
          nativeTouchMemory.Write32(0x0050AF34U + 0x14U, kTouchQuadPlayState) &&
          nativeTouchMemory.Write16(kTouchQuadPlayState + 0x2DD4U, 0U) &&
          writeFloats(kPositions + kNativeNaviViewEyeQuad * 0x30U,
                      touchQuadPositions),
      "could not seed native TopScreen touch-quad fixture");
  TopScreenNativeTouchQuad28LayoutStats touchQuadStats;
  Require(ApplyTopScreenNativeTouchQuad28Layout(nativeTouchMemory,
                                                &touchQuadStats, &error) &&
              touchQuadStats.Eligible && touchQuadStats.Transformed &&
              touchQuadStats.RendererAddress == kRenderer &&
              touchQuadStats.LowerEdge == 11.0F &&
              touchQuadStats.UpperEdge == 59.0F,
          "native TopScreen touch-quad transform was not applied");
  std::array<float, 12> transformedTouchQuad{};
  Require(
      nativeTouchMemory.ReadBytes(
          kPositions + kNativeNaviViewEyeQuad * 0x30U,
          std::span<std::uint8_t>(
              reinterpret_cast<std::uint8_t *>(transformedTouchQuad.data()),
              sizeof(transformedTouchQuad))) &&
          transformedTouchQuad[0] == 4.0F && transformedTouchQuad[1] == 11.0F &&
          transformedTouchQuad[4] == 11.0F &&
          transformedTouchQuad[7] == 59.0F && transformedTouchQuad[10] == 59.0F,
      "native TopScreen Navi/view eye vertices diverge from payload 0x005C9940");
  Require(nativeTouchMemory.Write16(kTouchQuadPlayState + 0x2DD4U, 1U) &&
              ApplyTopScreenNativeTouchQuad28Layout(nativeTouchMemory,
                                                    &touchQuadStats, &error) &&
              touchQuadStats.LowerEdge == 3.0F &&
              touchQuadStats.UpperEdge == 51.0F,
          "native TopScreen alternate touch-quad layout was not selected");

  std::array<std::array<TopScreenVec3, 4>, 3> questQuads{{
      {{{90.0F, 140.0F, 1.0F},
        {110.0F, 140.0F, 1.0F},
        {90.0F, 160.0F, 1.0F},
        {110.0F, 160.0F, 1.0F}}},
      {{{340.0F, 160.0F, 2.0F},
        {360.0F, 160.0F, 2.0F},
        {340.0F, 180.0F, 2.0F},
        {360.0F, 180.0F, 2.0F}}},
      {{{200.0F, 100.0F, 3.0F},
        {220.0F, 100.0F, 3.0F},
        {200.0F, 120.0F, 3.0F},
        {220.0F, 120.0F, 3.0F}}},
  }};
  const auto questStats = TransformTopScreenQuestGeometry(
      questQuads, {0U, false, false, 0.0F, 0.0F});
  Require(questStats.QuadsVisited == 3U && questStats.QuadsTranslated == 2U &&
              questStats.QuadsScaled == 1U && questStats.QuadsHidden == 1U &&
              questQuads[0][0].X == 490.0F && questQuads[0][0].Y == 290.0F &&
              questQuads[1][0].X == 278.5F && questQuads[1][0].Y == 14.5F &&
              questQuads[1][0].Z == 2.0F && questQuads[2][0].X == 200.0F,
          "TopScreen quest geometry transform is incorrect");

  std::array<std::array<TopScreenVec3, 4>, 1> offsetQuest{
      {{{{80.0F, 140.0F, 0.0F},
         {100.0F, 140.0F, 0.0F},
         {80.0F, 160.0F, 0.0F},
         {100.0F, 160.0F, 0.0F}}}}};
  const auto offsetStats = TransformTopScreenQuestGeometry(
      offsetQuest, {0U, false, false, 7.0F, -3.0F});
  Require(offsetStats.QuadsTranslated == 1U && offsetStats.QuadsHidden == 0U &&
              offsetQuest[0][0].X == 87.0F && offsetQuest[0][0].Y == 137.0F,
          "TopScreen quest dynamic offset is incorrect");
  std::array<std::array<TopScreenVec3, 4>, 1> scaledQuest{
      {{{{10.0F, 20.0F, 0.0F},
         {30.0F, 20.0F, 0.0F},
         {10.0F, 30.0F, 0.0F},
         {30.0F, 30.0F, 0.0F}}}}};
  const auto scaledQuestStats = TransformTopScreenQuestGeometry(
      scaledQuest, {12U, false, false, 0.0F, 0.0F, 0.8F, 4.0F, 1.0F});
  Require(scaledQuestStats.QuadsScaled == 1U &&
              scaledQuest[0][0].X == 12.0F &&
              scaledQuest[0][0].Y == 17.0F &&
              scaledQuest[0][1].X == 28.0F &&
              scaledQuest[0][2].Y == 25.0F,
          "TopScreen quest buffer did not receive 2.1.1 HUD transform");

  NativeA32Memory questMemory;
  Require(questMemory.MapRegion(
              {"quest-buffer", 0x00600000U, 0x1000U, true, false, {}}, &error),
          "cannot map Quest render-buffer fixture");
  constexpr std::uint32_t questBuffer = 0x00600000U;
  constexpr std::uint32_t questPositions = 0x00600100U;
  std::array<std::array<TopScreenVec3, 4>, 8> nativeQuestQuads{};
  nativeQuestQuads[0] = {{{80.0F, 140.0F, 1.0F},
                          {100.0F, 140.0F, 1.0F},
                          {100.0F, 160.0F, 1.0F},
                          {80.0F, 160.0F, 1.0F}}};
  Require(questMemory.Write32(questBuffer, 8U) &&
              questMemory.Write32(questBuffer + 0x10U, questPositions) &&
              questMemory.WriteBytes(questPositions,
                                     std::span<const std::uint8_t>(
                                         reinterpret_cast<const std::uint8_t *>(
                                             nativeQuestQuads.data()),
                                         sizeof(nativeQuestQuads))),
          "cannot seed Quest render-buffer fixture");
  TopScreenQuestRenderBufferStats renderStats;
  Require(TransformTopScreenQuestRenderBuffer(questMemory, questBuffer,
                                              {0U, false, false, 7.0F, -3.0F},
                                              &renderStats, &error) &&
              renderStats.QuadCount == 8U &&
              renderStats.Geometry.QuadsVisited == 8U,
          "native Quest render-buffer transform failed");
  std::uint32_t transformedX = 0U;
  std::uint32_t transformedY = 0U;
  Require(questMemory.Read32(questPositions, &transformedX) &&
              questMemory.Read32(questPositions + 4U, &transformedY) &&
              std::bit_cast<float>(transformedX) == 87.0F &&
              std::bit_cast<float>(transformedY) == 137.0F,
          "native Quest materialized stream was not updated");
  Require(questMemory.Write32(questBuffer, 7U) &&
              !TransformTopScreenQuestRenderBuffer(questMemory, questBuffer, {},
                                                   nullptr, &error),
          "invalid Quest render-buffer count was accepted");

  NativeA32Memory questModelMemory;
  Require(
      questModelMemory.MapRegion(
          {"quest-model-globals-a", 0x004F0000U, 0x00010000U, true, false, {}},
          &error) &&
          questModelMemory.MapRegion(
              {"quest-model-globals-b", 0x00500000U, 0x00090000U, true, false,
               {}},
              &error),
      "cannot map TopScreen 1.2 Quest model fixture");
  constexpr std::uint32_t questModelOwner = 0x00510000U;
  constexpr std::uint32_t questMainModelBuffer = 0x00511000U;
  constexpr std::uint32_t questMainModelPositions = 0x00512000U;
  constexpr std::uint32_t questMainModelTranslations = 0x00513000U;
  constexpr std::uint32_t questChildModelOwner = 0x00514000U;
  constexpr std::uint32_t questChildModelBuffer = 0x00515000U;
  constexpr std::uint32_t questChildModelPositions = 0x00516000U;
  constexpr std::uint32_t questChildModelTranslations = 0x00517000U;
  constexpr std::uint32_t questDrawScene = 0x00518000U;
  const std::array<TopScreenVec3, 8> questModelPositions{{
      {0.0F, 0.0F, 2.0F},
      {20.0F, 0.0F, 2.0F},
      {0.0F, 20.0F, 2.0F},
      {20.0F, 20.0F, 2.0F},
      {0.0F, 0.0F, 3.0F},
      {20.0F, 0.0F, 3.0F},
      {0.0F, 20.0F, 3.0F},
      {20.0F, 20.0F, 3.0F},
  }};
  const std::array<float, 4> questModelTranslations{
      300.0F, 140.0F, 100.0F, 20.0F};
  const auto seedQuestModelBuffer =
      [&questModelMemory, &questModelPositions, &questModelTranslations](
          std::uint32_t buffer, std::uint32_t positions,
          std::uint32_t translations) {
        return questModelMemory.Write32(buffer, 2U) &&
               questModelMemory.Write32(buffer + 0x0CU, positions) &&
               questModelMemory.Write32(buffer + 0x1CU, translations) &&
               questModelMemory.WriteBytes(
                   positions,
                   std::span<const std::uint8_t>(
                       reinterpret_cast<const std::uint8_t *>(
                           questModelPositions.data()),
                       sizeof(questModelPositions))) &&
               questModelMemory.WriteBytes(
                   translations,
                   std::span<const std::uint8_t>(
                       reinterpret_cast<const std::uint8_t *>(
                           questModelTranslations.data()),
                       sizeof(questModelTranslations)));
      };
  Require(seedQuestModelBuffer(questMainModelBuffer, questMainModelPositions,
                               questMainModelTranslations) &&
              seedQuestModelBuffer(questChildModelBuffer,
                                   questChildModelPositions,
                                   questChildModelTranslations),
          "cannot seed TopScreen 1.2 Quest model streams");
  TopScreenModelRegionStats modelRegionStats;
  Require(
      TransformTopScreenModelRegion(
          questModelMemory, questMainModelBuffer,
          {300.0F, 430.0F, 140.0F, 500.0F, 300.0F, 1.0F},
          &modelRegionStats, &error) &&
          modelRegionStats.ContractMatched &&
          modelRegionStats.QuadsVisited == 2U &&
          modelRegionStats.QuadsSelected == 1U &&
          modelRegionStats.VerticesTransformed == 4U,
      "TopScreen 1.2 generic model-region transform rejected native streams");
  std::uint32_t modelPositionXBits = 0U;
  std::uint32_t modelPositionYBits = 0U;
  std::uint32_t modelTranslationXBits = 0U;
  Require(
      questModelMemory.Read32(questMainModelPositions, &modelPositionXBits) &&
          questModelMemory.Read32(questMainModelPositions + 4U,
                                  &modelPositionYBits) &&
          questModelMemory.Read32(questMainModelTranslations,
                                  &modelTranslationXBits) &&
          std::bit_cast<float>(modelPositionXBits) == 190.0F &&
          std::bit_cast<float>(modelPositionYBits) == 150.0F &&
          std::bit_cast<float>(modelTranslationXBits) == 300.0F,
      "TopScreen 1.2 model-region transform did not preserve translations");

  Require(seedQuestModelBuffer(questMainModelBuffer, questMainModelPositions,
                               questMainModelTranslations) &&
              questModelMemory.Write32(0x004FC688U, questModelOwner) &&
              questModelMemory.Write32(questModelOwner + 0x10CU,
                                       questMainModelBuffer) &&
              questModelMemory.Write32(questModelOwner + 0x224U, 1U) &&
              questModelMemory.Write32(questModelOwner + 0x0CU,
                                       questChildModelOwner) &&
              questModelMemory.Write32(questChildModelOwner + 8U,
                                       questChildModelBuffer),
          "cannot seed TopScreen 1.2 Quest model ownership");
  TopScreenUiConfig normalScaledConfig;
  normalScaledConfig.HudScale = 0.9F;
  TopScreenQuestModelTransformStats questModelStats;
  Require(
      ApplyTopScreenQuestSubmitModelTransforms(
          questModelMemory, normalScaledConfig, &questModelStats, &error) &&
          questModelStats.MainModelsTransformed == 1U &&
          questModelStats.ChildModelsVisited == 1U &&
          questModelStats.ChildModelsTransformed == 1U,
      "TopScreen 1.2 Quest submit-model transforms were not applied");
  Require(
      questModelMemory.Read32(questChildModelPositions, &modelPositionXBits) &&
          questModelMemory.Read32(questChildModelPositions + 4U,
                                  &modelPositionYBits) &&
          std::abs((std::bit_cast<float>(modelPositionXBits) + 300.0F +
                    5.4F) -
                   259.2F) < 0.0001F &&
          std::abs((std::bit_cast<float>(modelPositionYBits) + 140.0F +
                    5.4F) -
                   22.6F) < 0.0001F,
      "TopScreen 1.2 Quest child model did not use Normal scale formulas");

  Require(
      seedQuestModelBuffer(questChildModelBuffer, questChildModelPositions,
                           questChildModelTranslations) &&
          questModelMemory.Write8(questDrawScene + 0x100U, 3U) &&
          questModelMemory.Write8(questDrawScene + 0x101U, 2U) &&
          questModelMemory.Write32(questDrawScene + 0x20ACU, 1U) &&
          questModelMemory.Write32(0x005043F0U, 1U) &&
          questModelMemory.Write32(0x0050A530U, 0U) &&
          questModelMemory.Write32(0x0050AF68U, 1U) &&
          questModelMemory.Write32(0x00588E3CU, 0U) &&
          questModelMemory.Write32(0x004FC6A0U, 0U) &&
          questModelMemory.Write32(questModelOwner + 0x428U, 5U),
      "cannot seed TopScreen 1.2 Quest draw-model gates");
  TopScreenUiConfig restorationScaledConfig;
  restorationScaledConfig.HudLayout = TopScreenHudLayout::Restoration;
  restorationScaledConfig.HudScale = 0.8F;
  TopScreenPauseProjectionState questDrawProjection;
  Require(
      ApplyTopScreenQuestDrawModelTransform(
          questModelMemory, questDrawScene, restorationScaledConfig,
          questDrawProjection, &questModelStats, &error) &&
          questModelStats.DrawModelsTransformed == 1U &&
          questDrawProjection.QuestDrawModelAdjusted &&
          questModelMemory.Read32(questChildModelPositions,
                                  &modelPositionXBits) &&
          questModelMemory.Read32(questChildModelPositions + 4U,
                                  &modelPositionYBits) &&
          std::abs(std::bit_cast<float>(modelPositionXBits) - 119.2F) <
              0.0001F &&
          std::abs(std::bit_cast<float>(modelPositionYBits) + 48.6F) <
              0.0001F,
      "TopScreen 1.2 Quest draw-model transform diverged from Restoration");
  const auto oneShotPositionX = modelPositionXBits;
  Require(
      ApplyTopScreenQuestDrawModelTransform(
          questModelMemory, questDrawScene, restorationScaledConfig,
          questDrawProjection, &questModelStats, &error) &&
          questModelStats.DrawModelsTransformed == 0U &&
          questModelMemory.Read32(questChildModelPositions,
                                  &modelPositionXBits) &&
          modelPositionXBits == oneShotPositionX,
      "TopScreen 1.2 Quest draw-model one-shot transform accumulated");

  bool ocarinaUiActive = false;
  Require(
      questModelMemory.Write32(0x005043E0U, questDrawScene) &&
          questModelMemory.Write32(questDrawScene + 0x20ACU, 0U) &&
          questModelMemory.Write16(questDrawScene + 0x2B82U, 0x00FFU) &&
          questModelMemory.Write16(questDrawScene + 0x2B80U, 1U) &&
          ReadTopScreenOcarinaUiActive(questModelMemory, &ocarinaUiActive,
                                      &error) &&
          !ocarinaUiActive,
      "TopScreen 1.2 Ocarina gate blocked the native idle state");
  Require(
      questModelMemory.Write16(questDrawScene + 0x2B80U, 2U) &&
          ReadTopScreenOcarinaUiActive(questModelMemory, &ocarinaUiActive,
                                      &error) &&
          ocarinaUiActive,
      "TopScreen 1.2 Ocarina action did not suppress promoted HUD layers");
  Require(
      questModelMemory.Write16(questDrawScene + 0x2B80U, 1U) &&
          questModelMemory.Write16(questDrawScene + 0x2B82U, 3U) &&
          ReadTopScreenOcarinaUiActive(questModelMemory, &ocarinaUiActive,
                                      &error) &&
          ocarinaUiActive,
      "TopScreen 1.2 Ocarina result state was not decoded");
  constexpr std::uint32_t ocarinaSceneExtension = 0x0051B000U;
  Require(
      questModelMemory.Write16(questDrawScene + 0x2B82U, 0x00FFU) &&
          questModelMemory.Write32(questDrawScene + 0x20ACU,
                                   ocarinaSceneExtension) &&
          questModelMemory.Write32(ocarinaSceneExtension + 0x1714U,
                                   0x01000000U) &&
          ReadTopScreenOcarinaUiActive(questModelMemory, &ocarinaUiActive,
                                      &error) &&
          ocarinaUiActive,
      "TopScreen 1.2 Ocarina extension flag was not decoded");

  NativeA32Memory questStateMemory;
  Require(
      questStateMemory.MapRegion(
          {"quest-roots", 0x004F0000U, 0x00010000U, true, false, {}}, &error) &&
          questStateMemory.MapRegion(
              {"quest-state", 0x00500000U, 0x000E0000U, true, false, {}},
              &error),
      "cannot map Quest state fixture");
  constexpr std::uint32_t sceneOwner = 0x00520000U;
  constexpr std::uint32_t sceneExtension = 0x00524000U;
  Require(questStateMemory.Write32(0x0050AF68U, 9U) &&
              questStateMemory.Write32(0x005043E0U, sceneOwner) &&
              questStateMemory.Write8(sceneOwner + 0x100U, 3U) &&
              questStateMemory.Write8(sceneOwner + 0x101U, 2U) &&
              questStateMemory.Write32(sceneOwner + 0x20ACU, sceneExtension) &&
              questStateMemory.Write32(sceneExtension + 0x12B8U, 1U),
          "cannot seed Quest geometry state fixture");
  TopScreenPauseProjectionState projectionState;
  projectionState.OffsetX = 7.0F;
  projectionState.OffsetY = -3.0F;
  projectionState.NativeQuestGate = true;
  TopScreenQuestGeometryContext decodedContext;
  Require(ReadTopScreenQuestGeometryContext(questStateMemory, &decodedContext,
                                            &projectionState, &error) &&
              decodedContext.PauseState == 9U &&
              decodedContext.PlayerSpecialState &&
              decodedContext.NativePageGateActive &&
              decodedContext.LeftRegionOffsetX == 7.0F &&
              decodedContext.LeftRegionOffsetY == -3.0F,
          "Quest geometry context did not consume typed projection state");
  projectionState.NativeQuestGate = false;
  Require(questStateMemory.Write32(0x00504484U, 1U) &&
              ReadTopScreenQuestGeometryContext(questStateMemory,
                                                &decodedContext,
                                                &projectionState, &error) &&
              decodedContext.NativePageGateActive,
          "Quest native-page gates were not decoded");

  constexpr std::uint32_t projectionOwner = 0x00530000U;
  constexpr std::uint32_t projectionExtents = 0x00531000U;
  constexpr std::uint32_t projectionOffset = 0x00532000U;
  constexpr std::uint32_t indicatorRenderer = 0x00533000U;
  constexpr std::uint32_t indicatorBuffer = 0x00534000U;
  constexpr std::uint32_t indicatorPositions = 0x00535000U;
  Require(
      questStateMemory.Write32(0x004FDA84U, projectionOwner) &&
          questStateMemory.Write32(0x004FDA8CU, indicatorRenderer) &&
          questStateMemory.Write32(indicatorRenderer + 0x08U,
                                   indicatorBuffer) &&
          questStateMemory.Write32(indicatorRenderer + 0x0CU, 2U) &&
          questStateMemory.Write32(indicatorBuffer + 0x1CU,
                                   indicatorPositions) &&
          questStateMemory.Write32(indicatorPositions,
                                   std::bit_cast<std::uint32_t>(10.0F)) &&
          questStateMemory.Write32(indicatorPositions + 4U,
                                   std::bit_cast<std::uint32_t>(20.0F)) &&
          questStateMemory.Write32(indicatorPositions + 8U,
                                   std::bit_cast<std::uint32_t>(30.0F)) &&
          questStateMemory.Write32(indicatorPositions + 12U,
                                   std::bit_cast<std::uint32_t>(40.0F)) &&
          questStateMemory.Write32(projectionOwner + 0x0CU,
                                   projectionExtents) &&
          questStateMemory.Write32(projectionOwner + 0x1CU, projectionOffset) &&
          questStateMemory.Write32(projectionExtents,
                                   std::bit_cast<std::uint32_t>(100.0F)) &&
          questStateMemory.Write32(projectionExtents + 0x24U,
                                   std::bit_cast<std::uint32_t>(200.0F)) &&
          questStateMemory.Write32(projectionOffset,
                                   std::bit_cast<std::uint32_t>(10.0F)) &&
          questStateMemory.Write32(projectionOffset + 4U,
                                   std::bit_cast<std::uint32_t>(100.0F)) &&
          questStateMemory.Write8(0x00587966U, 0U) &&
          questStateMemory.Write32(0x004FC688U, 1U),
      "cannot seed pause projection fixture");
  projectionState = {};
  Require(
      ApplyTopScreenPauseProjection(questStateMemory, projectionState, &error),
      "pause projection adapter rejected native fixture");
  std::uint32_t projectedXBits = 0U;
  std::uint32_t projectedYBits = 0U;
  Require(questStateMemory.Read32(projectionOffset, &projectedXBits) &&
              questStateMemory.Read32(projectionOffset + 4U, &projectedYBits) &&
              std::bit_cast<float>(projectedXBits) == 234.0F &&
              std::bit_cast<float>(projectedYBits) == 100.0F &&
              projectionState.OffsetX == 224.0F &&
              projectionState.OffsetY == 0.0F &&
              projectionState.NativeQuestGate,
          "pause projection did not reproduce payload-derived transforms");
  std::uint32_t projectedIndicatorBits = 0U;
  Require(
      questStateMemory.Read32(indicatorPositions, &projectedIndicatorBits) &&
          std::bit_cast<float>(projectedIndicatorBits) == 234.0F &&
          questStateMemory.Read32(indicatorPositions + 8U,
                                  &projectedIndicatorBits) &&
          std::bit_cast<float>(projectedIndicatorBits) == 254.0F,
      "TopScreen 1.2 minimap indicator streams were not aligned");
  TopScreenUiConfig projectedHud;
  projectedHud.HudMarginX = 12;
  Require(ApplyTopScreenPauseProjection(questStateMemory, projectionState,
                                        &error, &projectedHud) &&
              questStateMemory.Read32(projectionOffset, &projectedXBits) &&
              std::bit_cast<float>(projectedXBits) == 226.0F &&
              projectionState.OffsetX == 216.0F,
          "pause projection ignored the configured right HUD margin");
  Require(ApplyTopScreenPauseProjection(questStateMemory, projectionState,
                                        &error) &&
              questStateMemory.Read32(projectionOffset, &projectedXBits) &&
              questStateMemory.Read32(projectionOffset + 4U, &projectedYBits) &&
              std::bit_cast<float>(projectedXBits) == 234.0F &&
              std::bit_cast<float>(projectedYBits) == 100.0F &&
              projectionState.OffsetX == 224.0F &&
              projectionState.OffsetY == 0.0F,
          "pause projection accumulated its native right-edge transform");
  projectionState.AlternatePage = false;
  Require(!projectionState.AlternatePage,
          "external minimap configuration was not retained");
  Require(ApplyTopScreenPauseProjection(questStateMemory, projectionState,
                                        &error) &&
              questStateMemory.Read32(projectionOffset + 4U, &projectedYBits) &&
              std::bit_cast<float>(projectedYBits) == 3100.0F &&
              projectionState.OffsetY == 3000.0F &&
              questStateMemory.Read32(indicatorPositions,
                                      &projectedIndicatorBits) &&
              std::bit_cast<float>(projectedIndicatorBits) == 400.0F,
          "pause projection did not reproduce the hidden-page displacement");
  projectionState.AlternatePage = true;
  Require(projectionState.AlternatePage,
          "external minimap configuration did not restore the projection");
  Require(questStateMemory.Write32(projectionOffset + 4U,
                                   std::bit_cast<std::uint32_t>(2000.0F)) &&
              ApplyTopScreenPauseProjection(questStateMemory, projectionState,
                                            &error) &&
              questStateMemory.Read32(projectionOffset + 4U, &projectedYBits) &&
              std::bit_cast<float>(projectedYBits) == -1000.0F &&
              projectionState.OffsetY == 0.0F,
          "pause projection did not reproduce the alternate-page threshold");
  Require(
      questStateMemory.Write32(0x004FDABCU, 1U) &&
          questStateMemory.Write32(projectionOffset,
                                   std::bit_cast<std::uint32_t>(10.0F)) &&
          ApplyTopScreenPauseProjection(questStateMemory, projectionState,
                                        &error) &&
          questStateMemory.Read32(projectionOffset, &projectedXBits) &&
          std::bit_cast<float>(projectedXBits) == 221.0F &&
          projectionState.OffsetX == 211.0F,
      "TopScreen 1.2 secondary-layer minimap margin was not decoded");

  constexpr std::uint32_t systemLayerRenderer = 0x00560000U;
  constexpr std::uint32_t systemLayerPositions = 0x00561000U;
  constexpr std::uint32_t systemLayerColors = 0x00562000U;
  const std::array<float, 24> systemLayerVertices{
      0.0F,   0.0F, 0.0F, 200.0F, 0.0F, 0.0F,
      0.0F, 100.0F, 0.0F, 200.0F, 100.0F, 0.0F,
      0.0F,   0.0F, 0.0F, 130.0F, 0.0F, 0.0F,
      0.0F,  80.0F, 0.0F, 130.0F,  80.0F, 0.0F};
  std::array<float, 32> systemLayerColorsFixture{};
  systemLayerColorsFixture.fill(0.25F);
  Require(
      questStateMemory.Write32(0x0050A530U, 1U) &&
          questStateMemory.Write32(0x0050A510U, systemLayerRenderer) &&
          questStateMemory.Write32(systemLayerRenderer, 2U) &&
          questStateMemory.Write32(systemLayerRenderer + 0x0CU,
                                   systemLayerPositions) &&
          questStateMemory.Write32(systemLayerRenderer + 0x18U,
                                   systemLayerColors) &&
          questStateMemory.WriteBytes(
              systemLayerPositions,
              std::span<const std::uint8_t>(
                  reinterpret_cast<const std::uint8_t *>(
                      systemLayerVertices.data()),
                  sizeof(systemLayerVertices))) &&
          questStateMemory.WriteBytes(
              systemLayerColors,
              std::span<const std::uint8_t>(
                  reinterpret_cast<const std::uint8_t *>(
                      systemLayerColorsFixture.data()),
                  sizeof(systemLayerColorsFixture))),
      "cannot seed TopScreen 1.2 system-menu layer fixture");
  TopScreenSystemMenuLayerStats systemLayerStats;
  Require(ApplyTopScreenSystemMenuLayerSuppression(
              questStateMemory, &systemLayerStats, &error) &&
              systemLayerStats.Active &&
              systemLayerStats.RendererAddress == systemLayerRenderer &&
              systemLayerStats.QuadsVisited == 2U &&
              systemLayerStats.QuadsSuppressed == 1U,
          "TopScreen 1.2 system-menu layer suppression was not applied");
  std::array<float, 32> suppressedSystemLayerColors{};
  Require(
      questStateMemory.ReadBytes(
          systemLayerColors,
          std::span<std::uint8_t>(
              reinterpret_cast<std::uint8_t *>(
                  suppressedSystemLayerColors.data()),
              sizeof(suppressedSystemLayerColors))) &&
          suppressedSystemLayerColors[0] == 1.0F &&
          suppressedSystemLayerColors[1] == 1.0F &&
          suppressedSystemLayerColors[2] == 1.0F &&
          suppressedSystemLayerColors[3] == 0.0F &&
          suppressedSystemLayerColors[16] == 0.25F &&
          suppressedSystemLayerColors[31] == 0.25F,
      "TopScreen 1.2 system-menu suppression changed the wrong quads");
  Require(questStateMemory.Write32(0x0050A530U, 0U) &&
              ApplyTopScreenSystemMenuLayerSuppression(
                  questStateMemory, &systemLayerStats, &error) &&
              !systemLayerStats.Active &&
              systemLayerStats.QuadsVisited == 0U,
          "inactive TopScreen system menu mutated renderer layers");

  const auto disabledTarget =
      ResolveTopScreenPauseTargetCommand({false, 0x400U, false}, true);
  const auto inactiveTarget =
      ResolveTopScreenPauseTargetCommand({true, 0x401U, false}, false);
  const auto topTarget =
      ResolveTopScreenPauseTargetCommand({true, 0x400U, false}, true);
  const auto tallTarget =
      ResolveTopScreenPauseTargetCommand({true, 0x401U, false}, true);
  const auto halfTarget =
      ResolveTopScreenPauseTargetCommand({true, 0x401U, true}, true);
  const auto passthroughTarget =
      ResolveTopScreenPauseTargetCommand({true, 0x123U, false}, true);
  const auto pageRedrawTarget =
      ResolveTopScreenPausePageRedrawTargetCommand({true, 0x400U, false});
  Require(!disabledTarget.Handled && !inactiveTarget.Handled &&
              topTarget.Handled && topTarget.BindTopTarget &&
              topTarget.ViewportX == 0U &&
              topTarget.FramebufferBindingOffset == 0x3CU &&
              topTarget.ViewportY == 0U && topTarget.ViewportWidth == 240U &&
              topTarget.ViewportHeight == 320U &&
              tallTarget.FramebufferBindingOffset == 0x38U &&
              tallTarget.ViewportY == 40U && tallTarget.ViewportWidth == 480U &&
              tallTarget.ViewportHeight == 320U &&
              halfTarget.ViewportY == 40U && halfTarget.ViewportWidth == 240U &&
              halfTarget.ViewportHeight == 320U && passthroughTarget.Handled &&
              !passthroughTarget.BindTopTarget &&
              passthroughTarget.StoredCommand == 0x123U &&
              pageRedrawTarget.BindTopTarget &&
              pageRedrawTarget.FramebufferBindingOffset == 0x3CU &&
              pageRedrawTarget.ViewportY == 40U &&
              pageRedrawTarget.ViewportWidth == 480U &&
              pageRedrawTarget.ViewportHeight == 320U,
          "TopScreen pause target command contract is incorrect");

  NativeA32Memory suppressionMemory;
  Require(suppressionMemory.MapRegion(
              {"pause-child-state", 0x00500000U, 0x00010000U, true, false, {}},
              &error),
          "cannot map pause child-state fixture");
  constexpr std::array<std::uint32_t, 6> suppressionAddresses{
      0x00504484U, 0x0050672CU, 0x00506CE8U,
      0x0050A530U, 0x005043F4U, 0x0050AF68U};
  for (std::size_t index = 0; index < suppressionAddresses.size(); ++index) {
    Require(suppressionMemory.Write32(suppressionAddresses[index],
                                      static_cast<std::uint32_t>(index + 1U)),
            "cannot seed pause child-state fixture");
  }
  TopScreenPauseChildState savedChildState;
  Require(BeginTopScreenPauseChildSuppression(
              suppressionMemory,
              TopScreenPauseChildSuppressionSet::AuxiliaryRedraw,
              &savedChildState, &error),
          "cannot begin auxiliary pause child suppression");
  for (const auto address : suppressionAddresses) {
    std::uint32_t value = 1U;
    Require(suppressionMemory.Read32(address, &value) && value == 0U,
            "pause child state was not suppressed");
  }
  Require(EndTopScreenPauseChildSuppression(suppressionMemory, savedChildState,
                                            &error),
          "cannot restore pause child state");
  for (std::size_t index = 0; index < suppressionAddresses.size(); ++index) {
    std::uint32_t value = 0U;
    Require(suppressionMemory.Read32(suppressionAddresses[index], &value) &&
                value == index + 1U,
            "pause child state was not restored exactly");
  }

  Require(BeginTopScreenPauseChildSuppression(
              suppressionMemory, TopScreenPauseChildSuppressionSet::Tail,
              &savedChildState, &error),
          "cannot begin tail pause child suppression");
  for (std::size_t index = 0; index < suppressionAddresses.size(); ++index) {
    std::uint32_t value = 0U;
    Require(suppressionMemory.Read32(suppressionAddresses[index], &value) &&
                value == (index == 4U ? index + 1U : 0U),
            "tail pause suppression used the wrong owner mask");
  }
  Require(EndTopScreenPauseChildSuppression(suppressionMemory, savedChildState,
                                            &error),
          "cannot restore tail pause child state");

  Require(BeginTopScreenPauseChildSuppression(
              suppressionMemory, TopScreenPauseChildSuppressionSet::PageRedraw,
              &savedChildState, &error),
          "cannot begin page-redraw suppression");
  for (std::size_t index = 0; index < suppressionAddresses.size(); ++index) {
    std::uint32_t value = 0U;
    Require(suppressionMemory.Read32(suppressionAddresses[index], &value) &&
                value == (index == 5U ? 0U : index + 1U),
            "page redraw suppressed an owner other than touch");
  }
  Require(EndTopScreenPauseChildSuppression(suppressionMemory, savedChildState,
                                            &error),
          "cannot restore page-redraw child state");

  TopScreenPauseDrawInputs drawInputs;
  drawInputs.HasScene = true;
  drawInputs.SceneMode = 3U;
  drawInputs.SceneVariant = 1U;
  drawInputs.RuntimeMode = 0U;
  drawInputs.SuppressionOverride = true;
  drawInputs.AnyPageGateActive = true;
  TopScreenPauseDrawRoutingState drawState;
  Require(ResolveTopScreenPauseDrawAction(drawInputs, &drawState) ==
              TopScreenPauseDrawAction::SuppressNativeChildren,
          "eligible pause pages must suppress their native child draws");
  drawInputs.AnyPageGateActive = false;
  Require(ResolveTopScreenPauseDrawAction(drawInputs, &drawState) ==
              TopScreenPauseDrawAction::NativeDraw,
          "ordinary pause draws must remain native");
  drawInputs.AnyPageGateActive = true;
  drawInputs.NativeTransitionActive = true;
  drawInputs.SuppressionOverride = true;
  ObserveTopScreenPauseTargetCommand(drawInputs, 0x400U, &drawState);
  drawInputs.SuppressionOverride = false;
  ObserveTopScreenPauseTargetCommand(drawInputs, 0x400U, &drawState);
  Require(drawState.SuppressionDelayArmed &&
              drawState.SuppressionDelayCommands == 179U &&
              ResolveTopScreenPauseDrawAction(drawInputs, &drawState) ==
                  TopScreenPauseDrawAction::NativeDraw,
          "native transition must latch and delay pause suppression");
  drawInputs.NativeTransitionActive = false;
  drawInputs.SceneMode = 0U;
  ResolveTopScreenPauseDrawAction(drawInputs, &drawState);
  Require(!drawState.NativeTransitionLatched,
          "leaving the pause scene must clear the transition latch");
  for (std::uint32_t command = 0; command < 179U; ++command) {
    ObserveTopScreenPauseTargetCommand(drawInputs, 0x400U, &drawState);
  }
  drawInputs.SceneMode = 3U;
  Require(drawState.SuppressionDelayCommands == 0U &&
              ResolveTopScreenPauseDrawAction(drawInputs, &drawState) ==
                  TopScreenPauseDrawAction::SuppressNativeChildren,
          "pause suppression delay must expire by native target commands");
  drawInputs.RuntimeMode = 1U;
  drawInputs.EntranceIndex = 0U;
  Require(!IsTopScreenTitleDemoRuntime(drawInputs) &&
              ResolveTopScreenPauseDrawAction(drawInputs, &drawState) ==
                  TopScreenPauseDrawAction::SuppressNativeChildren,
          "TopScreen 1.2 accepted an ordinary entrance as title demo");
  drawInputs.EntranceIndex = 0xFFF3U;
  Require(IsTopScreenTitleDemoRuntime(drawInputs) &&
              ResolveTopScreenPauseDrawAction(drawInputs, &drawState) ==
                  TopScreenPauseDrawAction::NativeDraw,
          "TopScreen 1.2 title-demo entrance gate was not preserved");
  drawInputs.RuntimeMode = 0U;
  drawInputs.EntranceIndex = 0U;

  TopScreenPausePageRedrawInputs redrawInputs;
  redrawInputs.Pause = drawInputs;
  redrawInputs.Pause.SceneMode = 3U;
  redrawInputs.Pause.SceneSequence = 0U;
  redrawInputs.Pause.RuntimeMode = 0U;
  redrawInputs.Pause.NativeTransitionActive = false;
  redrawInputs.Pause.SuppressionOverride = false;
  redrawInputs.Pause.AlternatePathReady = true;
  redrawInputs.Pause.AlternatePathVisible = false;
  redrawInputs.NativeHealthGate = 16;
  TopScreenPausePageRedrawState redrawState;
  Require(ResolveTopScreenPausePageRedraw(redrawInputs, &redrawState),
          "stable active page must be redrawn on the top viewport");
  redrawInputs.WorldMapControllerState = 1U;
  Require(!ResolveTopScreenPausePageRedraw(redrawInputs, &redrawState),
          "world-map controller must retain ownership of its redraw");
  redrawInputs.WorldMapControllerState = 0U;
  redrawInputs.Pause.SceneSequence = 1U;
  Require(!ResolveTopScreenPausePageRedraw(redrawInputs, &redrawState) &&
              redrawState.DelayCalls == 90U,
          "nonzero scene sequence must arm the native redraw delay");
  redrawInputs.Pause.SceneSequence = 0U;
  for (std::uint32_t call = 0U; call < 89U; ++call) {
    Require(!ResolveTopScreenPausePageRedraw(redrawInputs, &redrawState),
            "page redraw delay expired before its native call count");
  }
  Require(ResolveTopScreenPausePageRedraw(redrawInputs, &redrawState),
          "page redraw delay did not expire on its native call count");

  TopScreenPauseIconBuild ordinaryIcon{1U, 0x44U, 3U, 4U};
  TopScreenUiConfig normalHud;
  normalHud.HudScale = 0.9F; // This fixture tests the 90% layout, not the user default.
  Require(!ResolveTopScreenPauseIconBuild(&ordinaryIcon, normalHud) &&
              ordinaryIcon.Argument2 == 1U && ordinaryIcon.Argument3 == 0x44U &&
              ordinaryIcon.StackArgument0 == 3U &&
              ordinaryIcon.StackArgument1 == 4U,
          "ordinary pause icon build must pass through unchanged");
  TopScreenPauseIconBuild relocatedIcon{1U, 0xC5U, 3U, 4U};
  Require(ResolveTopScreenPauseIconBuild(&relocatedIcon, normalHud) &&
              relocatedIcon.Argument2 == 247U &&
              relocatedIcon.Argument3 == 10U &&
              relocatedIcon.StackArgument0 == 25U &&
              relocatedIcon.StackArgument1 == 25U,
          "TopScreen normal pause icon build transform is incorrect");
  normalHud.HudMarginX = 12;
  normalHud.HudMarginY = 5;
  relocatedIcon = {1U, 0xC5U, 3U, 4U};
  Require(ResolveTopScreenPauseIconBuild(&relocatedIcon, normalHud) &&
              relocatedIcon.Argument2 == 239U &&
              relocatedIcon.Argument3 == 14U,
          "TopScreen pause icon ignored configured HUD margins");
  TopScreenUiConfig restorationHud;
  restorationHud.HudLayout = TopScreenHudLayout::Restoration;
  restorationHud.HudScale = 1.0F;
  relocatedIcon = {1U, 0xC5U, 3U, 4U};
  Require(ResolveTopScreenPauseIconBuild(&relocatedIcon, restorationHud) &&
              relocatedIcon.Argument2 == 340U &&
              relocatedIcon.Argument3 == 55U &&
              relocatedIcon.StackArgument0 == 28U &&
              relocatedIcon.StackArgument1 == 28U,
          "TopScreen restoration pause icon build transform is incorrect");
  Require(!SuppressTopScreenTouchButtonDraw(11U) &&
              SuppressTopScreenTouchButtonDraw(12U) &&
              SuppressTopScreenTouchButtonDraw(19U) &&
              !SuppressTopScreenTouchButtonDraw(20U),
          "TopScreen touch-button draw state interval is incorrect");

  TopScreenPauseDrawInputs viewportInputs;
  viewportInputs.HasScene = true;
  viewportInputs.RuntimeMode = 4U;
  viewportInputs.AlternatePathReady = true;
  viewportInputs.AlternatePathVisible = true;
  Require(UseTopScreenAlternatePauseViewport(viewportInputs),
          "record-42 alternate viewport predicate changed");
  viewportInputs.AlternatePathVisible = false;
  Require(!UseTopScreenAlternatePauseViewport(viewportInputs),
          "record-42 viewport ignored native visibility state");
  viewportInputs.SceneMode = 5U;
  Require(UseTopScreenSceneFiveViewport(viewportInputs),
          "record-47 scene viewport predicate changed");
  viewportInputs.HasScene = false;
  Require(!UseTopScreenSceneFiveViewport(viewportInputs),
          "record-47 viewport accepted a missing scene");

  TopScreenPauseRouteState routeState;
  TopScreenPauseDrawInputs routeInputs;
  routeInputs.RuntimeMode = 4U;
  Require(!ResolveTopScreenPauseRouteActive(routeInputs, &routeState),
          "runtime mode four alone selected the pause route");
  routeInputs.RuntimeMode = 0U;
  Require(ResolveTopScreenPauseRouteActive(routeInputs, &routeState),
          "leaving runtime mode four did not start the route window");
  Require(routeState.TransitionPhase == 1U && routeState.RemainingCalls == 899U,
          "route transition phase or countdown changed");
  routeInputs.HasScene = true;
  routeInputs.SceneVariant = 3U;
  Require(ResolveTopScreenPauseRouteActive(routeInputs, &routeState) &&
              routeState.TransitionPhase == 2U,
          "a non-two scene variant did not advance route phase one");
  routeInputs.SceneVariant = 2U;
  Require(!ResolveTopScreenPauseRouteActive(routeInputs, &routeState) &&
              routeState.TransitionPhase == 0U,
          "variant two did not close route phase two");
  routeInputs.SceneMode = 5U;
  Require(ResolveTopScreenPauseRouteActive(routeInputs, &routeState),
          "scene mode five did not select the recovered route");
  routeInputs.SceneMode = 3U;
  routeInputs.RuntimeMode = 4U;
  routeInputs.AlternatePathReady = true;
  routeInputs.AlternatePathVisible = true;
  Require(ResolveTopScreenPauseRouteActive(routeInputs, &routeState),
          "the native alternate path did not select the recovered route");
  Require(ResolveTopScreenPauseRoutedCommand(0x400U, true) == 0x401U &&
              ResolveTopScreenPauseRoutedCommand(0x401U, true) == 0x400U,
          "TopScreen routed render commands were not exchanged");
  Require(ResolveTopScreenPauseRoutedCommand(0x402U, true) == 0x402U &&
              ResolveTopScreenPauseRoutedCommand(0x400U, false) == 0x400U,
          "TopScreen command routing changed an unowned command");

  TopScreenTouchCoordinateRouteState touchRouteState;
  TopScreenPauseDrawInputs touchRouteInputs;
  touchRouteInputs.HasScene = true;
  touchRouteInputs.SceneMode = 3U;
  touchRouteInputs.RuntimeMode = 1U;
  Require(!ResolveTopScreenTouchCoordinateSuppression(
              touchRouteInputs, false, false, false, &touchRouteState),
          "ordinary mode-3 scene used the obsolete TopScreen 1.1 route");
  touchRouteInputs.EntranceIndex = 0xFFF3U;
  Require(ResolveTopScreenTouchCoordinateSuppression(
              touchRouteInputs, false, false, false, &touchRouteState),
          "TopScreen 1.2 title-demo route did not suppress touch coordinates");
  touchRouteInputs.RuntimeMode = 0U;
  touchRouteInputs.EntranceIndex = 0U;
  touchRouteInputs.SceneVariant = 2U;
  touchRouteInputs.SceneSequence = 4U;
  touchRouteInputs.NativeTransitionActive = true;
  Require(!ResolveTopScreenTouchCoordinateSuppression(
              touchRouteInputs, false, false, false, &touchRouteState) &&
              touchRouteState.RuntimeSceneLatch,
          "record-28 runtime latch did not block its sequence route");
  touchRouteInputs.NativeTransitionActive = false;
  touchRouteInputs.SceneMode = 0U;
  Require(!ResolveTopScreenTouchCoordinateSuppression(
              touchRouteInputs, false, false, false, &touchRouteState) &&
              !touchRouteState.RuntimeSceneLatch,
          "record-28 runtime latch did not clear outside scene mode three");
  touchRouteInputs.SceneMode = 3U;
  Require(ResolveTopScreenTouchCoordinateSuppression(
              touchRouteInputs, false, false, false, &touchRouteState),
          "record-28 sequence route did not suppress touch coordinates");
  touchRouteInputs.SceneSequence = 7U;
  Require(!ResolveTopScreenTouchCoordinateSuppression(
              touchRouteInputs, false, false, false, &touchRouteState) &&
              ResolveTopScreenTouchCoordinateSuppression(
                  touchRouteInputs, true, false, false, &touchRouteState) &&
              ResolveTopScreenTouchCoordinateSuppression(
                  touchRouteInputs, false, true, true, &touchRouteState),
          "record-28 route and secondary-layer predicates changed");

  NativeA32Memory touchCoordinateMemory;
  Require(touchCoordinateMemory.MapRegion(
              {"touch-coordinate-state", 0x0050B000U, 0x1000U, true, false, {}},
              &error),
          "cannot map record-28 touch-coordinate fixture");
  for (std::uint32_t offset : {0x3AU, 0x3CU, 0x3EU, 0x40U, 0x42U, 0x48U}) {
    Require(touchCoordinateMemory.Write16(0x0050BB00U + offset, 0x1234U),
            "cannot seed record-28 touch-coordinate fixture");
  }
  Require(
      ApplyTopScreenTouchCoordinateSuppression(touchCoordinateMemory, &error),
      "cannot apply record-28 touch-coordinate suppression");
  constexpr std::array<std::pair<std::uint32_t, std::uint16_t>, 6>
      touchCoordinateExpected{{{0x3AU, 0x7FFFU},
                               {0x3CU, 0x7FFFU},
                               {0x3EU, 0U},
                               {0x40U, 0U},
                               {0x42U, 0U},
                               {0x48U, 0U}}};
  for (const auto &[offset, expected] : touchCoordinateExpected) {
    std::uint16_t actual = 0U;
    Require(touchCoordinateMemory.Read16(0x0050BB00U + offset, &actual) &&
                actual == expected,
            "record-28 touch-coordinate sentinel is incorrect");
  }

  NativeA32Memory visibilityMemory;
  Require(
      visibilityMemory.MapRegion(
          {"renderer-route-globals", 0x005C4000U, 0x1000U, true, false, {}},
          &error) &&
          visibilityMemory.MapRegion(
              {"renderer-route-root", 0x005C5000U, 0x1000U, true, false, {}},
              &error) &&
          visibilityMemory.MapRegion({"renderer-route-controller",
                                      0x00600000U,
                                      0x2000U,
                                      true,
                                      false,
                                      {}},
                                     &error) &&
          visibilityMemory.MapRegion(
              {"renderer-route-objects", 0x00700000U, 0x3000U, true, false, {}},
              &error),
      "cannot map record-41 renderer-route fixture");
  constexpr std::uint32_t visibilityController = 0x00600000U;
  constexpr std::uint32_t visibilityOwner = 0x00700000U;
  constexpr std::uint32_t visibilityRenderer = 0x00701000U;
  constexpr std::uint32_t visibilityColorA = 0x00702000U;
  constexpr std::uint32_t visibilityColorB = 0x00702100U;
  Require(visibilityMemory.Write32(0x005C588CU, visibilityOwner) &&
              visibilityMemory.Write32(visibilityOwner + 0x6FCU,
                                       visibilityRenderer) &&
              visibilityMemory.Write32(visibilityController + 0x6FCU,
                                       visibilityRenderer) &&
              visibilityMemory.Write32(visibilityRenderer + 0x64U,
                                       visibilityColorA) &&
              visibilityMemory.Write32(visibilityRenderer + 0x68U,
                                       visibilityColorB) &&
              visibilityMemory.Write8(visibilityRenderer + 0x6CU, 1U),
          "cannot seed record-41 renderer-route fixture");
  TopScreenRendererVisibilityInputs visibilityInputs;
  visibilityInputs.ControllerAddress = visibilityController;
  visibilityInputs.HasCallScene = true;
  visibilityInputs.CallSceneMode = 3U;
  visibilityInputs.CallSceneSequence = 4U;
  TopScreenRendererVisibilityRouteState visibilityState;
  TopScreenRendererVisibilityAction visibilityAction;
  for (std::uint32_t call = 0U; call < 60U; ++call) {
    Require(ResolveTopScreenRendererVisibilityRoute(
                visibilityMemory, visibilityInputs, &visibilityState,
                &visibilityAction, &error) &&
                visibilityAction ==
                    TopScreenRendererVisibilityAction::NativeDraw,
            "record-41 delay route failed");
  }
  Require(visibilityState.DelayCalls == 60 && visibilityState.FadeStep == 0 &&
              ResolveTopScreenRendererVisibilityRoute(
                  visibilityMemory, visibilityInputs, &visibilityState,
                  &visibilityAction, &error) &&
              visibilityState.FadeStep == 1,
          "record-41 fade did not start after its exact delay");
  std::uint8_t visibilityAlpha = 0U;
  std::uint32_t visibilityAlphaBits = 0U;
  Require(
      visibilityMemory.Read8(visibilityRenderer + 0xA0U, &visibilityAlpha) &&
          visibilityAlpha == 244U &&
          visibilityMemory.Read32(visibilityColorA + 0xF0U,
                                  &visibilityAlphaBits) &&
          std::bit_cast<float>(visibilityAlphaBits) == 23.0F / 24.0F,
      "record-41 native renderer fade is incorrect");
  visibilityState.FadeStep = 24;
  Require(ResolveTopScreenRendererVisibilityRoute(
              visibilityMemory, visibilityInputs, &visibilityState,
              &visibilityAction, &error) &&
              visibilityAction ==
                  TopScreenRendererVisibilityAction::SuppressNativeDraw &&
              visibilityMemory.Read8(visibilityRenderer + 0x6CU,
                                     &visibilityAlpha) &&
              visibilityAlpha == 0U,
          "record-41 terminal renderer was not suppressed");
  constexpr std::uint32_t visibilityOwnerSlot = visibilityController + 0x6F8U;
  Require(visibilityMemory.Write32(visibilityController + 0xAF8U,
                                   visibilityRenderer) &&
              visibilityMemory.Write32(visibilityOwnerSlot, 0U) &&
              visibilityMemory.Write8(visibilityRenderer + 0x6CU, 1U),
          "cannot seed record-41 scene-mode-7 fixture");
  visibilityInputs.CallSceneMode = 7U;
  visibilityInputs.CallSceneSubmode = 4U;
  visibilityState = {};
  std::uint32_t reconciledOwner = 0U;
  Require(ResolveTopScreenRendererVisibilityRoute(
              visibilityMemory, visibilityInputs, &visibilityState,
              &visibilityAction, &error) &&
              visibilityAction ==
                  TopScreenRendererVisibilityAction::SuppressNativeDraw &&
              visibilityMemory.Read32(visibilityOwnerSlot, &reconciledOwner) &&
              reconciledOwner == visibilityRenderer &&
              !visibilityState.RendererConflict &&
              visibilityMemory.Read8(visibilityRenderer + 0x6CU,
                                     &visibilityAlpha) &&
              visibilityAlpha == 0U,
          "record-41 scene-mode-7 renderer reconciliation failed");
  Require(visibilityMemory.Write32(visibilityOwnerSlot, visibilityColorA) &&
              visibilityMemory.Write8(visibilityRenderer + 0x6CU, 1U) &&
              ResolveTopScreenRendererVisibilityRoute(
                  visibilityMemory, visibilityInputs, &visibilityState,
                  &visibilityAction, &error) &&
              visibilityMemory.Read32(visibilityOwnerSlot, &reconciledOwner) &&
              reconciledOwner == visibilityColorA &&
              visibilityState.RendererConflict,
          "record-41 renderer ownership conflict was not preserved");

  TopScreenPauseControllerInputs controllerInputs;
  controllerInputs.Pause.HasScene = true;
  controllerInputs.Pause.SceneMode = 3U;
  controllerInputs.RendererAddress = visibilityRenderer;
  controllerInputs.RendererPhase = 1U;
  Require(
      visibilityMemory.Write32(visibilityRenderer + 0x5C4U, visibilityColorA) &&
          visibilityMemory.Write32(visibilityRenderer + 0x5C8U,
                                   visibilityColorB),
      "cannot seed record-34 controller fixture");
  TopScreenPauseControllerState controllerState;
  TopScreenPauseControllerAction controllerAction;
  Require(ResolveTopScreenPauseController(visibilityMemory, controllerInputs,
                                          true, &controllerState,
                                          &controllerAction, &error) &&
              controllerAction == TopScreenPauseControllerAction::NativeDraw,
          "record-34 native renderer phase was not retained");
  controllerInputs.RendererPhase = 8U;
  Require(ResolveTopScreenPauseController(visibilityMemory, controllerInputs,
                                          true, &controllerState,
                                          &controllerAction, &error) &&
              controllerAction ==
                  TopScreenPauseControllerAction::SuppressNativeDraw &&
              controllerState.RoutedRendererLatched &&
              controllerState.RoutedFadeStep == 1 &&
              visibilityMemory.Read32(visibilityRenderer + 0x54U,
                                      &visibilityAlphaBits) &&
              std::bit_cast<float>(visibilityAlphaBits) == 0.05F &&
              visibilityMemory.Read32(visibilityColorA + 0xFCU,
                                      &visibilityAlphaBits) &&
              std::bit_cast<float>(visibilityAlphaBits) == 0.05F,
          "record-34 routed renderer fade did not start exactly");
  for (std::uint32_t step = 1U; step < 20U; ++step) {
    Require(ResolveTopScreenPauseController(visibilityMemory, controllerInputs,
                                            true, &controllerState,
                                            &controllerAction, &error),
            "record-34 routed renderer fade progression failed");
  }
  Require(controllerState.RoutedFadeStep == 20 &&
              visibilityMemory.Read32(visibilityRenderer + 0x450U,
                                      &visibilityAlphaBits) &&
              std::bit_cast<float>(visibilityAlphaBits) == 1.0F,
          "record-34 routed renderer fade endpoint is incorrect");
  controllerInputs.Pause.RuntimeMode = 4U;
  controllerInputs.Pause.AlternatePathReady = true;
  controllerInputs.Pause.AlternatePathVisible = true;
  Require(
      ResolveTopScreenPauseController(visibilityMemory, controllerInputs, false,
                                      &controllerState, &controllerAction,
                                      &error) &&
          !controllerState.RoutedRendererLatched &&
          controllerState.RoutedFadeStep == 0 &&
          controllerAction ==
              TopScreenPauseControllerAction::NativeDrawThenAlternateOverlay,
      "record-34 alternate transition route is incorrect");
  controllerInputs.Pause.SceneMode = 7U;
  Require(ResolveTopScreenPauseController(visibilityMemory, controllerInputs,
                                          false, &controllerState,
                                          &controllerAction, &error) &&
              controllerAction ==
                  TopScreenPauseControllerAction::SuppressNativeDraw,
          "record-34 scene-mode-7 suppression is incorrect");
  constexpr std::uint32_t alternateRendererB = 0x00701100U;
  Require(visibilityMemory.Write32(0x005C4CD8U, visibilityRenderer) &&
              visibilityMemory.Write32(0x005C4CE0U, alternateRendererB) &&
              visibilityMemory.Write32(visibilityRenderer + 0x80U,
                                       std::bit_cast<std::uint32_t>(10.0F)) &&
              visibilityMemory.Write32(visibilityRenderer + 0x84U,
                                       std::bit_cast<std::uint32_t>(20.0F)) &&
              visibilityMemory.Write32(alternateRendererB + 0x80U,
                                       std::bit_cast<std::uint32_t>(30.0F)) &&
              visibilityMemory.Write32(alternateRendererB + 0x84U,
                                       std::bit_cast<std::uint32_t>(40.0F)) &&
              visibilityMemory.Write8(visibilityRenderer + 0x6CU, 1U) &&
              visibilityMemory.Write8(alternateRendererB + 0x6CU, 1U),
          "cannot seed record-34 alternate-renderer fixture");
  std::array<std::uint32_t, 2> alternateRenderers{};
  std::uint32_t alternatePositionBits = 0U;
  Require(
      PrepareTopScreenPauseAlternateRenderers(
          visibilityMemory, &controllerState, &alternateRenderers, &error) &&
          alternateRenderers[0] == visibilityRenderer &&
          alternateRenderers[1] == alternateRendererB &&
          visibilityMemory.Read32(visibilityRenderer + 0x80U,
                                  &alternatePositionBits) &&
          std::bit_cast<float>(alternatePositionBits) == 55.0F &&
          visibilityMemory.Read32(alternateRendererB + 0x80U,
                                  &alternatePositionBits) &&
          std::bit_cast<float>(alternatePositionBits) == 5.0F &&
          PrepareTopScreenPauseAlternateRenderers(
              visibilityMemory, &controllerState, &alternateRenderers,
              &error) &&
          visibilityMemory.Read32(visibilityRenderer + 0x80U,
                                  &alternatePositionBits) &&
          std::bit_cast<float>(alternatePositionBits) == 55.0F,
      "record-34 alternate-renderer positions drifted");

  NativeA32Memory alternateRendererMemory;
  Require(
      alternateRendererMemory.MapRegion({"alternate-renderer-pointers",
                                         0x005C4000U,
                                         0x1000U,
                                         true,
                                         false,
                                         {}},
                                        &error) &&
          alternateRendererMemory.MapRegion(
              {"alternate-renderers", 0x00600000U, 0x1000U, true, false, {}},
              &error),
      "cannot map record-44 renderer fixture");
  constexpr std::array<std::uint32_t, 4> rendererPointerAddresses{
      0x005C4CD4U, 0x005C4CD8U, 0x005C4CDCU, 0x005C4CE0U};
  constexpr std::array<std::uint32_t, 4> renderers{0x00600000U, 0x00600100U, 0U,
                                                   0x00600300U};
  constexpr std::array<std::uint8_t, 4> rendererVisibility{1U, 3U, 0U, 7U};
  for (std::size_t index = 0; index < renderers.size(); ++index) {
    Require(alternateRendererMemory.Write32(rendererPointerAddresses[index],
                                            renderers[index]),
            "cannot seed record-44 renderer pointer");
    if (renderers[index] != 0U) {
      Require(alternateRendererMemory.Write8(renderers[index] + 0x6CU,
                                             rendererVisibility[index]),
              "cannot seed record-44 renderer visibility");
    }
  }
  TopScreenAlternateRendererState savedRendererState;
  Require(BeginTopScreenAlternateRendererSuppression(
              alternateRendererMemory, &savedRendererState, &error),
          "cannot begin record-44 renderer suppression");
  for (const auto renderer : renderers) {
    std::uint8_t value = 0U;
    Require(renderer == 0U ||
                (alternateRendererMemory.Read8(renderer + 0x6CU, &value) &&
                 value == 0U),
            "record-44 renderer was not suppressed");
  }
  Require(EndTopScreenAlternateRendererSuppression(alternateRendererMemory,
                                                   savedRendererState, &error),
          "cannot restore record-44 renderer state");
  for (std::size_t index = 0; index < renderers.size(); ++index) {
    std::uint8_t value = 0U;
    Require(renderers[index] == 0U || (alternateRendererMemory.Read8(
                                           renderers[index] + 0x6CU, &value) &&
                                       value == rendererVisibility[index]),
            "record-44 renderer state was not restored exactly");
  }

  const auto flowContracts = TopScreenVerifiedControlFlowContracts();
  Require(flowContracts.size() == 10U,
          "TopScreen control-flow contract count is incorrect");
  for (const auto &contract : flowContracts) {
    std::uint32_t target = 0U;
    Require(ResolveTopScreenControlFlow(contract.Entry, &target) &&
                target == contract.Target,
            "TopScreen control-flow contract resolution failed");
  }
  std::uint32_t gameplayCompositionTarget = 0U;
  Require(
      ResolveTopScreenControlFlow(0x002E27C0U, &gameplayCompositionTarget) &&
          gameplayCompositionTarget == 0x002E2A04U,
      "record-91 branch contract does not match the IPS patch site");
  Require(!ResolveTopScreenControlFlow(0x002E278CU, nullptr),
          "record-91 contract still intercepts before the IPS patch site");
  for (const std::uint32_t callsite :
       {0x004848F0U, 0x00484944U, 0x004849B0U, 0x00324638U, 0x0032464CU,
        0x00324658U, 0x00324664U}) {
    std::uint32_t nopTarget = 0U;
    Require(ResolveTopScreenControlFlow(callsite, &nopTarget) &&
                nopTarget == callsite + 4U,
            "records 104-110 do not preserve exact NOP continuation");
  }
  Require(!ResolveTopScreenControlFlow(0x00100000U, nullptr),
          "TopScreen control-flow accepted an unrelated entry");

  const auto floatLoads = TopScreenVerifiedFloatLoadContracts();
  Require(floatLoads.size() == 2U,
          "TopScreen float-load contract count is incorrect");
  for (const auto &contract : floatLoads) {
    std::uint8_t lane = 0xFFU;
    std::uint32_t bits = 0U;
    Require(ResolveTopScreenFloatLoad(contract.Entry, &lane, &bits) &&
                lane == contract.VfpLane && bits == contract.ValueBits,
            "TopScreen float-load contract resolution failed");
  }
  Require(!ResolveTopScreenFloatLoad(0x00100000U, nullptr, nullptr),
          "TopScreen float-load accepted an unrelated entry");

  NativeA32Memory titleLogoMemory;
  Require(
      titleLogoMemory.MapRegion(
          {"title-logo-code", 0x001DAD00U, 0x100U, true, false, {}}, &error) &&
          titleLogoMemory.MapRegion(
              {"title-logo-global", 0x0050BB00U, 0x100U, true, false, {}},
              &error) &&
          titleLogoMemory.MapRegion(
              {"title-logo-actor", 0x08000000U, 0x1000U, true, false, {}},
              &error) &&
          titleLogoMemory.Write32(0x001DAD08U, 0x0050BB50U) &&
          titleLogoMemory.Write32(0x0050BB50U, 0x12345678U),
      "could not seed TopScreen title-logo fixture");
  TopScreenTitleLogoFadeHoldResult titleLogoHold;
  constexpr std::uint32_t kTitleActor = 0x08000000U;
  constexpr std::uint32_t kTitleActorState = kTitleActor + 0x100U;
  constexpr std::uint32_t kClampedAlpha = 0x437F0000U;
  Require(ApplyTopScreenTitleLogoFadeHold(titleLogoMemory, kTitleActor,
                                          kTitleActorState, kClampedAlpha,
                                          &titleLogoHold, &error),
          "TopScreen title-logo fade hold did not apply");
  std::uint32_t titleAlpha = 0U;
  std::uint32_t effectAlpha = 0U;
  std::uint16_t fadeSubstate = 0U;
  std::uint16_t fadeTimer = 0U;
  Require(titleLogoMemory.Read32(kTitleActor + 0x1D0U, &titleAlpha) &&
              titleLogoMemory.Read32(kTitleActor + 0x1DCU, &effectAlpha) &&
              titleLogoMemory.Read16(kTitleActorState + 0xC4U, &fadeSubstate) &&
              titleLogoMemory.Read16(kTitleActorState + 0xC6U, &fadeTimer) &&
              titleAlpha == kClampedAlpha && effectAlpha == kClampedAlpha &&
              fadeSubstate == 3U && fadeTimer == 1U &&
              titleLogoHold.AnimationOwner == 0x12345678U &&
              titleLogoHold.Argument1 == 1U &&
              titleLogoHold.NextPc == 0x001DAFF8U,
          "TopScreen title-logo records 111-113 diverge from the IPS");

  const auto nativeCameraScalar = ResolveTopScreenCameraNormal1Scalar(
      std::bit_cast<std::uint32_t>(60.0F), 100U);
  const auto extendedCameraScalar = ResolveTopScreenCameraNormal1Scalar(
      std::bit_cast<std::uint32_t>(60.0F), 130U);
  Require(std::bit_cast<float>(nativeCameraScalar.NativeValueBits) == 60.0F &&
              std::bit_cast<float>(nativeCameraScalar.ContinuedValueBits) ==
                  60.0F &&
              std::bit_cast<float>(extendedCameraScalar.NativeValueBits) ==
                  60.0F &&
              std::bit_cast<float>(extendedCameraScalar.ContinuedValueBits) ==
                  78.0F,
          "TopScreen Camera_Normal1 scalar contract is incorrect");

  const auto mainCameraFovOwner = ResolveTopScreenCameraFovOwnership(
      {0x08001000U, 0x13, 0});
  const auto secondaryCameraFovOwner = ResolveTopScreenCameraFovOwnership(
      {0x08001000U, 0x13, 1});
  const auto blockedCameraFovOwner = ResolveTopScreenCameraFovOwnership(
      {0x08001000U, 0x14, 0});
  Require(mainCameraFovOwner.Active &&
              mainCameraFovOwner.ValueAddress == 0x08001198U &&
              !secondaryCameraFovOwner.Active &&
              !blockedCameraFovOwner.Active &&
              std::abs(ResolveTopScreenCameraFovDegrees(60.0F, 115U) -
                       69.0F) < 0.001F &&
              ResolveTopScreenCameraFovDegrees(10.0F, 70U) == 20.0F &&
              ResolveTopScreenCameraFovDegrees(100.0F, 140U) == 110.0F,
          "TopScreen 2.1.1 main-camera FOV contract is incorrect");

  Require(IsTopScreenFreeCameraGameplayAllowed(0, 0, 0) &&
              !IsTopScreenFreeCameraGameplayAllowed(0, 0, 0xFFFF) &&
              !IsTopScreenFreeCameraGameplayAllowed(1, 0x0629, 0xFFEF) &&
              !IsTopScreenFreeCameraGameplayAllowed(1, 0x0629, 0xFFFF) &&
              !IsTopScreenFreeCameraGameplayAllowed(1, 0x0100, 0xFFFF) &&
              !IsTopScreenFreeCameraGameplayAllowed(2, 0, 0),
          "TopScreen free-camera gameplay gate is incorrect");
  Require(IsTopScreenFreeCameraSettingBlocked(0x14) &&
              IsTopScreenFreeCameraSettingBlocked(0x23) &&
              IsTopScreenFreeCameraSettingBlocked(0x46) &&
              !IsTopScreenFreeCameraSettingBlocked(0x13) &&
              !IsTopScreenFreeCameraSettingBlocked(0x24),
          "TopScreen free-camera setting gate is incorrect");

  TopScreenFreeCameraOwnershipInput blockerInput;
  blockerInput.Scene = 0x45;
  blockerInput.CameraStatus = 3;
  blockerInput.CameraSetting = 0x14;
  const auto blockers = ResolveTopScreenFreeCameraBlockers(blockerInput);
  Require((blockers & TopScreenFreeCameraBlockerScene) != 0U &&
              (blockers & TopScreenFreeCameraBlockerSecondaryCamera) != 0U &&
              (blockers & TopScreenFreeCameraBlockerMissingPlayer) != 0U &&
              (blockers & TopScreenFreeCameraBlockerStatus) != 0U &&
              (blockers & TopScreenFreeCameraBlockerSetting) != 0U,
          "TopScreen free-camera blocker diagnostics are incomplete");

  TopScreenFreeCameraState cameraOptions;
  TopScreenUiConfig externalCameraConfig;
  externalCameraConfig.CameraZoomPercent = 130U;
  externalCameraConfig.CameraFovPercent = 115U;
  externalCameraConfig.FreeCameraEnabled = true;
  externalCameraConfig.FreeCameraSpeedLevel = 5U;
  externalCameraConfig.FreeCameraInvertX = true;
  externalCameraConfig.FreeCameraInvertY = true;
  ApplyTopScreenFreeCameraConfig(cameraOptions, externalCameraConfig);
  Require(cameraOptions.Enabled && cameraOptions.N64StyleZoom &&
              cameraOptions.SpeedOption == 5U &&
              cameraOptions.Speed == 12U && cameraOptions.Inversion == 3U &&
              cameraOptions.SpeedNotificationFrames == 0U &&
              cameraOptions.InversionNotificationFrames == 0U,
          "external config did not own every persistent camera preference");
  cameraOptions = {};
  UpdateTopScreenFreeCameraOptions(cameraOptions,
                                   {true, true, false, false, true});
  Require(cameraOptions.SpeedOption == 4U && cameraOptions.Speed == 8U &&
              cameraOptions.Inversion == 1U,
          "TopScreen free-camera option increment is incorrect");
  UpdateTopScreenFreeCameraOptions(cameraOptions,
                                   {true, false, true, true, false});
  Require(cameraOptions.SpeedOption == 3U && cameraOptions.Speed == 6U &&
              cameraOptions.Inversion == 0U &&
              cameraOptions.SpeedNotificationFrames == 29U &&
              cameraOptions.InversionNotificationFrames == 29U,
          "TopScreen free-camera option decrement is incorrect");
  UpdateTopScreenFreeCameraOptions(cameraOptions,
                                   {.N64StyleZoomTogglePressed = true});
  Require(cameraOptions.N64StyleZoom &&
              std::bit_cast<float>(ResolveTopScreenCameraNormal1Scalar(
                                       std::bit_cast<std::uint32_t>(60.0F),
                                       cameraOptions.ZoomPercent)
                                       .ContinuedValueBits) == 78.0F,
          "TopScreen R+D-pad Up camera zoom toggle is incorrect");
  UpdateTopScreenFreeCameraOptions(cameraOptions,
                                   {.N64StyleZoomTogglePressed = true});
  Require(!cameraOptions.N64StyleZoom,
          "TopScreen camera zoom toggle did not return to native scale");
  std::vector<oot3d::ui::UiPrimitive> cameraOptionPrimitives;
  Require(AppendTopScreenFreeCameraOptionPresentation(
              cameraOptions, cameraOptionPrimitives) == 13U &&
              cameraOptionPrimitives.size() == 13U &&
              cameraOptionPrimitives.front().texture.semantic_name ==
                  "oot3d/topscreen/camera_option_glyphs" &&
              cameraOptionPrimitives.front().destination.y == 10.0F &&
              cameraOptionPrimitives[6].destination.y == 20.0F &&
              std::abs(cameraOptionPrimitives.front().destination.x -
                       10.0F) < 0.001F &&
              cameraOptionPrimitives.front().uv.x == 112.0F / 128.0F &&
              cameraOptionPrimitives.front().uv.y == 64.0F / 256.0F &&
              cameraOptionPrimitives.front().uv.width == 6.0F / 128.0F &&
              cameraOptionPrimitives.front().uv.height == 10.0F / 256.0F,
          "TopScreen free-camera option feedback does not match the payload");
  for (std::uint32_t frame = 0; frame < 29U; ++frame) {
    UpdateTopScreenFreeCameraOptions(cameraOptions, {});
  }
  cameraOptionPrimitives.clear();
  Require(cameraOptions.SpeedNotificationFrames == 0U &&
              cameraOptions.InversionNotificationFrames == 0U &&
              AppendTopScreenFreeCameraOptionPresentation(
                  cameraOptions, cameraOptionPrimitives) == 0U,
          "TopScreen free-camera option feedback did not expire");

  TopScreenFreeCameraState cameraState;
  cameraState.Enabled = true;
  TopScreenFreeCameraOwnershipInput ownership;
  ownership.IsMainCamera = true;
  ownership.HasPlayer = true;
  ownership.CameraStatus = 7;
  ownership.CameraPitch = 0x100;
  ownership.CameraYaw = 0x200;
  ownership.CameraDistance = 150.0F;
  ownership.RightStickX = 31;
  TopScreenFreeCameraState disabledCameraState;
  Require(!UpdateTopScreenFreeCameraOwnership(disabledCameraState,
                                               ownership) &&
              !disabledCameraState.Active,
          "disabled TopScreen free camera acquired camera ownership");
  Require(UpdateTopScreenFreeCameraOwnership(cameraState, ownership) &&
              cameraState.Pitch == 0x100 && cameraState.Yaw == 0x200 &&
              cameraState.Distance == 150.0F,
          "TopScreen free-camera activation contract is incorrect");
  TopScreenFreeCameraState relativeCameraState;
  relativeCameraState.Enabled = true;
  auto relativeOwnership = ownership;
  relativeOwnership.RightStickX = 1;
  relativeOwnership.RelativeInput = true;
  Require(UpdateTopScreenFreeCameraOwnership(relativeCameraState,
                                              relativeOwnership),
          "relative mouse input was rejected by the analog stick dead zone");
  ownership.RightStickX = 0;
  ownership.CameraSetting = 0x14;
  Require(!UpdateTopScreenFreeCameraOwnership(cameraState, ownership),
          "TopScreen free-camera blocked setting retained ownership");

  cameraState = {};
  cameraState.Active = true;
  cameraState.Distance = 150.0F;
  const auto orbit = BuildTopScreenFreeCameraOrbit(
      cameraState, {{10.0F, 20.0F, 30.0F}, {}, {}, false, false, false, 0, 0});
  Require(std::abs(orbit.At.X - 10.0F) < 0.001F &&
              std::abs(orbit.At.Y - 70.0F) < 0.001F &&
              std::abs(orbit.At.Z - 30.0F) < 0.001F &&
              std::abs(orbit.IntendedEye.X - 10.0F) < 0.01F &&
              std::abs(orbit.IntendedEye.Y - 70.0F) < 0.01F &&
              std::abs(orbit.IntendedEye.Z + 145.0F) < 0.1F,
          "TopScreen free-camera orbit contract is incorrect");
  cameraState.Distance = 150.0F;
  cameraState.ZoomPercent = 130U;
  const auto zoomedOrbit = BuildTopScreenFreeCameraOrbit(
      cameraState, {{10.0F, 20.0F, 30.0F}, {}, {}, false, false, false, 0, 0});
  Require(std::abs(zoomedOrbit.IntendedEye.Z + 175.0F) < 0.1F,
          "TopScreen camera zoom did not compose with free-camera orbit");

  NativeA32Memory cameraMemory;
  Require(cameraMemory.MapRegion({"topscreen-camera-globals",
                                  0x004F0000U,
                                  0x000B0000U,
                                  true,
                                  false,
                                  {}},
                                 &error) &&
              cameraMemory.MapRegion({"topscreen-camera-runtime",
                                      0x08000000U,
                                      0x00120000U,
                                      true,
                                      false,
                                      {}},
                                     &error),
          "cannot map TopScreen camera guest fixture");
  constexpr std::uint32_t globalContext = 0x08000000U;
  constexpr std::uint32_t cameraAddress = globalContext + 0x364U;
  constexpr std::uint32_t playerAddress = 0x08010000U;
  constexpr std::uint32_t outAddress = 0x08000300U;
  constexpr std::uint32_t stackAddress = 0x08110000U;
  const auto writeFloat = [&cameraMemory](std::uint32_t address, float value) {
    return cameraMemory.Write32(address, std::bit_cast<std::uint32_t>(value));
  };
  Require(cameraMemory.Write32(cameraAddress + 0xD4U, globalContext) &&
              cameraMemory.Write32(cameraAddress + 0xD8U, playerAddress) &&
              cameraMemory.Write16(globalContext + 0x104U, 1U) &&
              cameraMemory.Write16(cameraAddress + 0x188U, 7U) &&
              cameraMemory.Write16(cameraAddress + 0x18AU, 0U) &&
              cameraMemory.Write16(cameraAddress + 0x182U, 0U) &&
              cameraMemory.Write16(cameraAddress + 0x184U, 0U) &&
              writeFloat(cameraAddress + 0x80U, 0.0F) &&
              writeFloat(cameraAddress + 0x84U, 50.0F) &&
              writeFloat(cameraAddress + 0x88U, 0.0F) &&
              writeFloat(cameraAddress + 0x8CU, 0.0F) &&
              writeFloat(cameraAddress + 0x90U, 50.0F) &&
              writeFloat(cameraAddress + 0x94U, 150.0F) &&
              writeFloat(cameraAddress + 0x144U, 60.0F) &&
              cameraMemory.Write32(0x00587958U, 0U) &&
              cameraMemory.Write32(0x0058795CU, 0U) &&
              cameraMemory.Write32(0x00587960U, 0U) &&
              cameraMemory.Write32(0x00588E3CU, 0U) &&
              cameraMemory.Write32(playerAddress + 0x1710U, 0U) &&
              cameraMemory.Write32(playerAddress + 0x1714U, 0U) &&
              cameraMemory.Write32(playerAddress + 0x7CU, 0x08020000U) &&
              writeFloat(playerAddress + 0x28U, 10.0F) &&
              writeFloat(playerAddress + 0x2CU, 20.0F) &&
              writeFloat(playerAddress + 0x30U, 30.0F) &&
              writeFloat(playerAddress + 0x84U, 20.0F),
          "cannot seed TopScreen camera guest fixture");
  TopScreenFreeCameraGuestRuntime guestCamera;
  guestCamera.Camera.Enabled = true;
  guestCamera.Camera.FovPercent = 115U;
  bool guestCameraActive = false;
  Require(BeginTopScreenFreeCameraGuestUpdate(
              cameraMemory, outAddress, cameraAddress, stackAddress, 31, 0,
              false,
              &guestCamera, &guestCameraActive, &error) &&
              guestCameraActive &&
              guestCamera.Phase == TopScreenCameraGuestPhase::Water &&
              ApplyTopScreenFreeCameraEnvironment(cameraMemory, &guestCamera,
                                                  &error) &&
              PrepareTopScreenFreeCameraCollision(cameraMemory, &guestCamera,
                                                  &error) &&
              guestCamera.Phase == TopScreenCameraGuestPhase::Collision &&
              CommitTopScreenFreeCameraCollision(cameraMemory, &guestCamera,
                                                 &error) &&
              ApplyTopScreenFreeCameraQuake(cameraMemory, &guestCamera, false,
                                            &error) &&
              ApplyTopScreenFreeCameraData(cameraMemory, &guestCamera, -1,
                                           &error) &&
              guestCamera.Phase == TopScreenCameraGuestPhase::Idle,
          "TopScreen staged native camera guest contract failed");
  std::uint32_t freeCameraFovBits = 0U;
  Require(cameraMemory.Read32(globalContext + 0x198U,
                              &freeCameraFovBits) &&
              std::abs(std::bit_cast<float>(freeCameraFovBits) - 69.0F) <
                  0.001F,
          "TopScreen FOV control did not compose with free-camera ownership");

  const auto zrSelection =
      ResolveTopScreenItemsSelectionBegin({true, false, 2U});
  const auto zlSelection =
      ResolveTopScreenItemsSelectionBegin({false, true, 2U});
  Require(zrSelection.Active && zrSelection.Selection == 5U &&
              zlSelection.Active && zlSelection.Selection == 0x17U &&
              !ResolveTopScreenItemsSelectionBegin({true, false, 1U}).Active,
          "TopScreen Items selection begin contract is incorrect");
  std::uint32_t completedSelection = 0U;
  std::uint16_t completedCursorY = 0U;
  Require(CompleteTopScreenItemsSelection(5U, 0x0BU, &completedSelection,
                                          &completedCursorY) &&
              completedSelection == 5U && completedCursorY == 7U &&
              CompleteTopScreenItemsSelection(0x17U, 0x0BU, &completedSelection,
                                              &completedCursorY) &&
              completedSelection == 0x17U && completedCursorY == 0xBFU &&
              !CompleteTopScreenItemsSelection(5U, 10U, &completedSelection,
                                               &completedCursorY),
          "TopScreen Items selection completion contract is incorrect");

  std::vector<std::uint8_t> originalPayload{1U, 2U, 3U};
  const std::vector<std::uint8_t> replacementPayload{4U, 5U, 6U};
  std::vector<std::uint8_t> overridePack{'O', '3', 'T', 'U'};
  AppendU32(overridePack, 1U);
  AppendU32(overridePack, 1U);
  AppendU64(overridePack, TestFnv1a64(originalPayload));
  AppendU64(overridePack, TestFnv1a64(replacementPayload));
  AppendU32(overridePack,
            static_cast<std::uint32_t>(replacementPayload.size()));
  overridePack.insert(overridePack.end(), replacementPayload.begin(),
                      replacementPayload.end());
  TopScreenTextureOverridePack textureOverrides;
  Require(textureOverrides.LoadBytes(overridePack, &error) &&
              textureOverrides.Apply(originalPayload) ==
                  TopScreenTextureOverrideResult::Applied &&
              originalPayload == replacementPayload &&
              textureOverrides.Apply(originalPayload) ==
                  TopScreenTextureOverrideResult::AlreadyApplied,
          "TopScreen native CTXB payload override contract is incorrect");

  constexpr std::string_view profileTextureSemantic =
      "oot3d/topscreen/2.1.1/test_atlas";
  const std::vector<std::uint8_t> profileTexturePayload(256U, 0x7FU);
  std::vector<std::uint8_t> profilePack{'O', '3', 'T', 'U'};
  AppendU32(profilePack, 2U);
  AppendU32(profilePack, 0U);
  AppendU32(profilePack, 1U);
  AppendU32(profilePack,
            static_cast<std::uint32_t>(profileTextureSemantic.size()));
  AppendU16(profilePack, 8U);
  AppendU16(profilePack, 8U);
  profilePack.push_back(0U);
  profilePack.insert(profilePack.end(), 3U, 0U);
  AppendU32(profilePack,
            static_cast<std::uint32_t>(profileTexturePayload.size()));
  AppendU64(profilePack, TestFnv1a64(profileTexturePayload));
  profilePack.insert(profilePack.end(), profileTextureSemantic.begin(),
                     profileTextureSemantic.end());
  profilePack.insert(profilePack.end(), profileTexturePayload.begin(),
                     profileTexturePayload.end());
  TopScreenTextureOverridePack profileTextures;
  const auto *profileTexture =
      profileTextures.LoadBytes(profilePack, &error)
          ? profileTextures.FindProfileTexture(profileTextureSemantic)
          : nullptr;
  Require(profileTexture != nullptr && profileTexture->Width == 8U &&
              profileTexture->Height == 8U &&
              profileTexture->NativePicaFormat == 0U &&
              profileTexture->EncodedPayload == profileTexturePayload,
          "TopScreen 2.1.1 named profile CTXB contract is incorrect");

  std::cout << "oot3d_top_screen_mod_profile_tests: ok\n";
  return 0;
}
