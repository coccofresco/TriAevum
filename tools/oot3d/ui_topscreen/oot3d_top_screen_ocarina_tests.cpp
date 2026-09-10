#include "oot3d_top_screen_ocarina.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d_ui/ui_contract_types.h"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << "TopScreen ocarina: " << message << '\n'; std::exit(1); }
}
Oot3dNativeGame::NativeA32Memory BuildOcarinaFixture() {
  using namespace Oot3dNativeGame;
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"topscreen-ocarina-fixture",
                            0x004D0000U,
                            0x000C0000U,
                            true,
                            false,
                            {}},
                           &error),
          "could not map ocarina fixture");
  const auto writeFloat = [&memory](std::uint32_t address, float value) {
    return memory.Write32(address, std::bit_cast<std::uint32_t>(value));
  };
  constexpr std::uint32_t kOcarina = 0x005093E4U;
  constexpr std::uint32_t kScene = 0x00520000U;
  constexpr std::uint32_t kNoteTypes = 0x00521000U;
  Require(memory.Write32(0x005043D4U + 0x0CU, kScene) &&
              memory.Write32(0x0050AF68U, 2U) &&
              memory.Write8(kScene + 0x100U, 3U) &&
              memory.Write16(kScene + 0x2B82U, 0xFFU) &&
              memory.Write16(kScene + 0x2B80U, 1U) &&
              memory.Write32(kOcarina + 0x14U, 4U) &&
              memory.Write32(kOcarina + 0x18U, 1U) &&
              memory.Write32(kOcarina + 0x1CU, 1U) &&
              memory.Write32(kOcarina + 0x38U, 0U) &&
              memory.Write32(kOcarina + 0x44U, 1U),
          "could not seed ocarina controller state");
  for (const std::uint32_t index : {0U, 1U, 2U, 35U, 36U, 37U}) {
    Require(writeFloat(kOcarina + 0x68U + index * 8U,
                       10.0F + static_cast<float>(index)) &&
                writeFloat(kOcarina + 0x6CU + index * 8U,
                           20.0F + static_cast<float>(index)) &&
                writeFloat(kOcarina + 0x3C8U + index * 8U, 30.0F) &&
                writeFloat(kOcarina + 0x3CCU + index * 8U, 40.0F) &&
                writeFloat(kOcarina + 0x728U + index * 8U, 50.0F) &&
                writeFloat(kOcarina + 0x72CU + index * 8U, 60.0F) &&
                writeFloat(kOcarina + 0xA88U + index * 8U, 70.0F) &&
                writeFloat(kOcarina + 0xA8CU + index * 8U, 80.0F),
            "could not seed ocarina native geometry arrays");
  }
  constexpr std::uint32_t kDestination = 5U;
  Require(memory.Write32(0x00587A14U, 4U) &&
              memory.Write32(0x0050A3B0U + kDestination * 4U, 100U) &&
              memory.Write32(0x0053C9D4U + 100U * 4U, 4U) &&
              memory.Write32(0x004D53C8U + kDestination * 4U, 2U) &&
              memory.Write32(0x004D541CU + kDestination * 4U, kNoteTypes) &&
              memory.Write32(kNoteTypes, 1U) &&
              memory.Write32(kNoteTypes + 4U, 4U),
          "could not seed ocarina destination tables");
  for (std::uint32_t type = 0U; type < 5U; ++type) {
    Require(
        writeFloat(0x0050A1CCU + type * 4U, 5.0F + static_cast<float>(type)) &&
            writeFloat(0x0050A1E0U + type * 4U,
                       90.0F + static_cast<float>(type)),
        "could not seed ocarina marker tables");
  }
  return memory;
}

} // namespace

void RunTopScreenOcarinaTests() {
  using namespace Oot3dNativeGame;
  auto ocarinaMemory = BuildOcarinaFixture();
  TopScreenOcarinaBrowser ocarinaBrowser;
  TopScreenOcarinaGeometry ocarinaGeometry;
  std::string ocarinaError;
  Require(ReadTopScreenOcarinaGeometry(ocarinaMemory, ocarinaBrowser, &ocarinaGeometry,
                                        &ocarinaError) &&
              ocarinaGeometry.Active && ocarinaGeometry.Song == 5U,
          "ocarina producer did not activate the native destination");
  Require(
      std::abs(ocarinaGeometry.Quads[0].Position.X - 50.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[0].Position.Y - 189.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[0].Size.Y - 39.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[0].AtlasOrigin.Y - 81.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[0].AtlasSize.Y - 59.0F) < 0.001F,
      "ocarina base-quad transformation is incorrect");
  Require(
      ocarinaGeometry.Quads[6].Visible && ocarinaGeometry.Quads[7].Visible &&
          !ocarinaGeometry.Quads[8].Visible &&
          std::abs(ocarinaGeometry.Quads[6].Position.X - 122.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[6].Position.Y - 174.0F) < 0.001F &&
          ocarinaGeometry.Quads[14].Visible &&
          std::abs(ocarinaGeometry.Quads[14].Size.X - 24.0F) < 0.001F,
      "ocarina marker or arrow reconstruction is incorrect");
  std::vector<oot3d::ui::UiPrimitive> ocarinaPresentation;
  const oot3d::ui::UiTextureIdentity ocarinaTexture{
      0x005D0000U, 0x18000000U, "oot3d/native/pause_shared/ocarina_page"};
  Require(AppendTopScreenOcarinaPresentation(ocarinaGeometry, ocarinaTexture,
                                              ocarinaPresentation) == 10U &&
              ocarinaPresentation.front().subsystem ==
                  oot3d::ui::UiSubsystem::TouchControls &&
              ocarinaPresentation.front().role ==
                  oot3d::ui::UiPrimitiveRole::TouchControl &&
              std::abs(ocarinaPresentation.front().color.alpha - 0.68F) <
                  0.001F,
          "ocarina presentation did not preserve recovered semantics");
  Require(ocarinaMemory.Write32(0x005093E4U + 0x38U, 1U) &&
              ReadTopScreenOcarinaGeometry(ocarinaMemory, ocarinaBrowser, &ocarinaGeometry,
                                            &ocarinaError) &&
              !ocarinaGeometry.Active,
          "transitioning ocarina owner still produced a presentation");
  ocarinaMemory = BuildOcarinaFixture();
  Require(ocarinaMemory.Write32(0x0050AF68U, 0U) &&
              ReadTopScreenOcarinaGeometry(ocarinaMemory, ocarinaBrowser, &ocarinaGeometry,
                                            &ocarinaError) &&
              ocarinaGeometry.Active,
          "ocarina presentation incorrectly requires a paused game");
  Require(ocarinaGeometry.Quads[3].Position.X == 85.0F &&
              ocarinaGeometry.Quads[3].Position.Y == -127.0F &&
              ocarinaGeometry.Quads[0].Color.red == 1.0F &&
              ocarinaGeometry.Quads[6].Color.red == 1.0F,
          "ocarina band source indices or white tint regressed");
  Require(ocarinaPresentation[0].uv.y == 81.0F / 256.0F &&
              ocarinaPresentation[0].uv.height > 0,
          "native PICA V inversion leaked into host UI primitives");
  Require(ocarinaMemory.Write32(0x00587A14U, 0) &&
              ReadTopScreenOcarinaGeometry(ocarinaMemory, ocarinaBrowser,
                                           &ocarinaGeometry, &ocarinaError) &&
              !ocarinaGeometry.SongLearned && ocarinaGeometry.UnknownSong.Visible &&
              !ocarinaGeometry.Quads[6].Visible,
          "unlearned song must show the mod marker without revealing notes");
  const auto generationBefore = ocarinaMemory.WriteGeneration();
  TopScreenOcarinaState browserState{true, 12, 0};
  ocarinaBrowser.Advance(browserState, false, true);
  Require(ocarinaBrowser.SelectedSong() == 0, "first right must select song zero");
  for (unsigned i = 0; i < 11; ++i) ocarinaBrowser.Advance(browserState, false, true);
  Require(ocarinaBrowser.SelectedSong() == 0, "song repeat occurred before 12 updates");
  ocarinaBrowser.Advance(browserState, false, true);
  Require(ocarinaBrowser.SelectedSong() == 1, "song repeat missing at update 12");
  for (unsigned i = 0; i < 5; ++i) ocarinaBrowser.Advance(browserState, false, true);
  Require(ocarinaBrowser.SelectedSong() == 2, "song repeat missing after five updates");
  ocarinaBrowser.Advance(browserState, true, false);
  Require(ocarinaBrowser.SelectedSong() == 1, "direction reversal must select immediately");
  ocarinaBrowser.Advance({}, false, false);
  ocarinaBrowser.Advance(browserState, true, false);
  Require(ocarinaBrowser.SelectedSong() == 11, "first left must select song eleven");
  ocarinaBrowser.Advance(browserState, true, false, true, false);
  Require(ocarinaBrowser.SelectedSong() == 10,
          "release/repress between native updates must not become held repeat");
  ocarinaBrowser.Reset();
  ocarinaBrowser.Advance(browserState, true, true);
  Require(ocarinaBrowser.SelectedSong() == 0 && ocarinaBrowser.Direction() == 1,
          "right must have priority in both selection and arrow presentation");
  Require(ocarinaMemory.WriteGeneration() == generationBefore,
          "guide navigation must not modify native pause or quest state");

  const auto idleNavigation = BuildTopScreenOcarinaNavigationGeometry(0);
  const auto leftNavigation = BuildTopScreenOcarinaNavigationGeometry(-1);
  const auto rightNavigation = BuildTopScreenOcarinaNavigationGeometry(1);
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
      AppendTopScreenOcarinaNavigationPresentation(
          leftNavigation, navigationTexture, navigationPresentation) == 2U &&
          navigationPresentation.front().role ==
              oot3d::ui::UiPrimitiveRole::TouchControl &&
          std::abs(navigationPresentation.front().color.alpha - 0.8F) < 0.001F,
      "pause navigation presentation lost recovered texture semantics");

  std::cout << "TopScreen ocarina contracts passed\n";
}
