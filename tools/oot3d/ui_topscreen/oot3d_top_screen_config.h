#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace Oot3dNativeGame {

inline constexpr std::string_view kTopScreenUiConfigSchema =
    "oot3d_topscreen_ui_v2";
inline constexpr std::string_view kTopScreenUiLegacyConfigSchema =
    "oot3d_topscreen_ui_v1";
inline constexpr float kTopScreenHudScaleMinimum = 0.60F;
inline constexpr float kTopScreenHudScaleMaximum = 1.20F;
inline constexpr float kTopScreenHudScaleStep = 0.05F;
// The two HUD sizes F1 offers: the game's own size and the size that fits the
// HUD to the single wide screen (the default).
inline constexpr float kTopScreenHudScaleNative = 1.00F;
inline constexpr float kTopScreenHudScaleOptimized = 0.70F;
inline constexpr float kTopScreenHudScaleDefault = kTopScreenHudScaleOptimized;
inline constexpr std::int8_t kTopScreenHudMarginMinimum = -3;
inline constexpr std::int8_t kTopScreenHudMarginMaximum = 16;
// SingleScreen 3.0 D-pad Placement and Button Group Placement (settings
// table 0x0060A99C..0x0060AA3C): offsets in px, size in 5 percent steps.
inline constexpr std::int8_t kTopScreenDpadOffsetXMinimum = -50;
inline constexpr std::int8_t kTopScreenDpadOffsetXMaximum = 80;
inline constexpr std::int8_t kTopScreenDpadOffsetYMinimum = -10;
inline constexpr std::int8_t kTopScreenDpadOffsetYMaximum = 18;
inline constexpr std::int8_t kTopScreenButtonGroupOffsetXMinimum = -20;
inline constexpr std::int8_t kTopScreenButtonGroupOffsetXMaximum = 10;
inline constexpr std::int8_t kTopScreenButtonGroupOffsetYMinimum = -10;
inline constexpr std::int8_t kTopScreenButtonGroupOffsetYMaximum = 40;
inline constexpr std::uint8_t kTopScreenPlacementSizeMinimum = 50U;
inline constexpr std::uint8_t kTopScreenPlacementSizeMaximum = 150U;
inline constexpr std::uint8_t kTopScreenCameraZoomMinimum = 75U;
inline constexpr std::uint8_t kTopScreenCameraZoomMaximum = 170U;
inline constexpr std::uint8_t kTopScreenCameraFovMinimum = 70U;
inline constexpr std::uint8_t kTopScreenCameraFovMaximum = 140U;
// SingleScreen 3.0 Rain Density: 25..300 percent in 25 percent steps.
inline constexpr std::uint16_t kTopScreenRainDensityMinimum = 25U;
inline constexpr std::uint16_t kTopScreenRainDensityMaximum = 300U;
inline constexpr std::uint16_t kTopScreenRainDensityStep = 25U;

enum class TopScreenHudLayout {
  Normal,
  Restoration,
};

const char *TopScreenHudLayoutName(TopScreenHudLayout layout) noexcept;
bool ParseTopScreenHudLayout(std::string_view value,
                             TopScreenHudLayout *layout) noexcept;

enum class TopScreenSelectAction : std::uint8_t {
  SaveScreen,
  MinimapToggle,
};

const char *TopScreenSelectActionName(TopScreenSelectAction action) noexcept;
bool ParseTopScreenSelectAction(std::string_view value,
                                TopScreenSelectAction *action) noexcept;

enum class TopScreenDpadAction : std::uint8_t {
  None = 0,
  View = 1,
  Ocarina = 2,
  IronBoots = 3,
  HoverBoots = 4,
  ItemZr = 5,
  ItemZl = 6,
  SwordToggle = 7,
  AllBootsToggle = 8,
  MinimapToggle = 9,
  Boomerang = 10,
  Slingshot = 11,
  TunicToggle = 12,
  ShieldToggle = 13,
};

const char *TopScreenDpadActionName(TopScreenDpadAction action) noexcept;
bool ParseTopScreenDpadAction(std::string_view value,
                              TopScreenDpadAction *action) noexcept;

enum class TopScreenFreeCameraSmoothing : std::uint8_t {
  Off = 0,
  Light = 1,
  Medium = 2,
  Default = 3,
  Heavy = 4,
};

const char *TopScreenFreeCameraSmoothingName(
    TopScreenFreeCameraSmoothing smoothing) noexcept;
bool ParseTopScreenFreeCameraSmoothing(
    std::string_view value, TopScreenFreeCameraSmoothing *smoothing) noexcept;

inline constexpr std::array<TopScreenDpadAction, 4>
    kTopScreenChildDpadDefaults{TopScreenDpadAction::View,
                                TopScreenDpadAction::Ocarina,
                                TopScreenDpadAction::ItemZr,
                                TopScreenDpadAction::ItemZl};
inline constexpr std::array<TopScreenDpadAction, 4>
    kTopScreenAdultDpadDefaults{TopScreenDpadAction::View,
                                TopScreenDpadAction::Ocarina,
                                TopScreenDpadAction::IronBoots,
                                TopScreenDpadAction::HoverBoots};

// The defaults are TriAevum's standard TopScreen profile. F1 offers only the
// HUD size; the layout fields keep these values unless a developer
// configuration file sets them.
struct TopScreenUiConfig {
  TopScreenHudLayout HudLayout = TopScreenHudLayout::Normal;
  float HudScale = kTopScreenHudScaleDefault;
  std::int8_t HudMarginX = 4;
  std::int8_t HudMarginY = 1;
  std::uint8_t MagicBarY = 2U;
  std::int8_t DpadOffsetX = 0;
  std::int8_t DpadOffsetY = -1;
  std::uint8_t DpadSizePercent = 100U;
  std::int8_t ButtonGroupOffsetX = 0;
  std::int8_t ButtonGroupOffsetY = 0;
  std::uint8_t ButtonGroupSizePercent = 100U;
  bool MinimapVisible = true;
  bool RenderHud = true;
  bool RenderDpadIcons = true;
  bool RenderItemsHint = true;
  TopScreenSelectAction SelectAction = TopScreenSelectAction::SaveScreen;
  bool ExitItemsToSaveScreen = true;
  std::uint8_t CameraZoomPercent = 100U;
  std::uint8_t CameraFovPercent = 100U;
  std::array<TopScreenDpadAction, 4> ChildDpad =
      kTopScreenChildDpadDefaults;
  std::array<TopScreenDpadAction, 4> AdultDpad =
      kTopScreenAdultDpadDefaults;
  bool FreeCameraEnabled = false;
  std::uint8_t FreeCameraSpeedLevel = 3U;
  TopScreenFreeCameraSmoothing FreeCameraSmoothing =
      TopScreenFreeCameraSmoothing::Default;
  bool FreeCameraInvertX = false;
  bool FreeCameraInvertY = false;
  std::uint8_t CStickAimSpeedLevel = 3U;
  bool CStickAimInvertX = false;
  bool CStickAimInvertY = false;
  // SingleScreen 3.0 gameplay options; the standard profile turns them all on.
  bool QuickPutAway = true;
  bool LandingRollFix = true;
  bool RollDash = true;
  bool AimingReticle = true;
  bool Rain3d = true;
  std::uint16_t Rain3dDensityPercent = 100U;
  // SingleScreen 3.0 Items shortcut column: the four action rows in display
  // order (config+0x34, a permutation of 0..3; identity is the 3.0 default).
  std::array<std::uint8_t, 4> ItemsShortcutOrder{0U, 1U, 2U, 3U};

  bool operator==(const TopScreenUiConfig &) const = default;
};

bool ParseTopScreenUiConfigText(std::string_view text,
                                TopScreenUiConfig *config,
                                std::string *error = nullptr);
bool LoadTopScreenUiConfig(const std::filesystem::path &path,
                           TopScreenUiConfig *config,
                           std::string *error = nullptr);
bool SerializeTopScreenUiConfigText(const TopScreenUiConfig &config,
                                    std::string *text,
                                    std::string *error = nullptr);
bool SaveTopScreenUiConfig(const std::filesystem::path &path,
                           const TopScreenUiConfig &config,
                           std::string *error = nullptr);

struct TopScreenUiConfigSnapshot {
  TopScreenUiConfig Config;
  std::uint64_t Revision = 0U;
};

class TopScreenUiConfigRuntime final {
public:
  TopScreenUiConfigRuntime(std::filesystem::path path,
                           TopScreenUiConfig initial);

  [[nodiscard]] TopScreenUiConfigSnapshot Snapshot() const;
  [[nodiscard]] const std::filesystem::path &Path() const noexcept;
  [[nodiscard]] bool Persistent() const noexcept;
  void Preview(const TopScreenUiConfig &config);
  bool Apply(const TopScreenUiConfig &config, std::string *error = nullptr);
  bool Reload(std::string *error = nullptr);

private:
  std::filesystem::path mPath;
  mutable std::mutex mMutex;
  TopScreenUiConfig mConfig;
  std::uint64_t mRevision = 1U;
};

} // namespace Oot3dNativeGame
