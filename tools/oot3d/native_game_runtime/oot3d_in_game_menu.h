#pragma once

// The in-game menu (Esc, or R3 on a pad): a controller-friendly subset of the
// settings, drawn by the integrated UI renderer and acting only through the
// owners of each setting (the graphics runtime, the controls configuration,
// the TopScreen configuration, the game language, Photo mode and the
// save-state library). The TopScreen rows are the ones F1 offers (HUD size,
// the gameplay camera, C-stick aiming, the SELECT action and the gameplay
// options); everything without a row here stays in F1, which this menu never
// changes.

#include "oot3d_dev_free_camera.h"
#include "oot3d_game_language.h"
#include "oot3d_menu_font.h"
#include "oot3d_menu_help.h"
#include "oot3d_menu_model.h"
#include "oot3d_menu_presentation.h"
#include "oot3d_native_control_config.h"
#include "oot3d_photo_mode_panel.h"
#include "oot3d_savestate_library.h"
#include "oot3d_top_screen_config.h"
#include "oot3d_ui/ui_primitives.h"

#include "fast/extension/graphics_settings_runtime.h"
#include "ship/audio/AudioOutputPolicy.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

// Held state of the host controls the menu reads, sampled with the game input.
struct InGameMenuHostInput {
  bool Up = false, Down = false, Left = false, Right = false;
  bool Confirm = false, Back = false;
  bool TabPrevious = false, TabNext = false;
  // Esc or R3: opens and closes the menu.
  bool Toggle = false;
  // The mouse over the window, in window pixels, with the presentation
  // picture (the aspect ratio's letterbox the menu is drawn in) in the same
  // pixels; the menu maps the pointer onto its canvas through the picture, so
  // the window's shape never moves a hit. `PointerWheel` is notches this
  // frame.
  bool PointerPresent = false;
  float PointerX = 0.0F;
  float PointerY = 0.0F;
  std::uint32_t PictureX = 0U;
  std::uint32_t PictureY = 0U;
  std::uint32_t PictureWidth = 0U;
  std::uint32_t PictureHeight = 0U;
  // The picture's physical height on the display (0: unknown); the menu
  // grows on a small screen held close (Menu::MenuDensityScale).
  float PictureHeightMillimetres = 0.0F;
  bool PointerPressed = false;
  float PointerWheel = 0.0F;
};

// What the window host does for the menu's session rows: its folders, the
// help card's "shown once" record, F1 and quitting. Each action is optional;
// an absent one leaves its row out. The host owns all of it; the menu only
// asks.
struct InGameMenuHost {
  Menu::HelpControls Controls = Menu::HelpControls::KeyboardAndController;
  std::filesystem::path SaveDataDirectory;
  std::filesystem::path LogsDirectory;
  // Opens a folder in the platform's file browser (desktop); null where the
  // platform has none.
  std::function<bool(const std::filesystem::path &, std::string *)> OpenFolder;
  // Where the saves and the diagnostics are handled instead (Android: the
  // installer's Saves and Diagnostics pages, which run while the game is
  // closed).
  std::string InstallerRoute;
  // Android: asks for the installer's page ("saves" for SavesActivity,
  // "diagnostics" for DiagnosticsActivity) and quits the game. Those pages
  // run in the installer's process and refuse a save import while the game's
  // process lives, so the activity opens the page once the game has ended
  // (ports/android/runtime/android_host.cpp). The menu asks before calling
  // it. While null, the Help tab names the route.
  std::function<bool(std::string_view page, std::string *error)> OpenInstallerPage;
  // The help card's record in the runtime configuration (Help.Shown).
  std::function<bool()> HelpCardShown;
  std::function<bool(std::string *)> RememberHelpCardShown;
  // Opens F1 (which closes this menu).
  std::function<void()> OpenAdvancedSettings;
  // Ends the session normally: pending host settings are written through
  // their owners and the window closes (the launch marker is cleared).
  std::function<void()> QuitGame;
};

struct InGameMenuOwners {
  std::shared_ptr<NativeControlConfigRuntime> Controls;
  // Null unless the TopScreen profile is active: the TopScreen rows then stay out.
  std::shared_ptr<TopScreenUiConfigRuntime> TopScreen;
  std::shared_ptr<GameLanguageSettings> Language;
  std::shared_ptr<SavestateLibrary> Savestates;
  std::shared_ptr<DevFreeCamera> FreeCamera;
  std::shared_ptr<PhotoModeState> PhotoMode;
  InGameMenuHost Host;
  // The host audio service's output policy (master volume, mute); null
  // leaves the Audio rows out.
  std::shared_ptr<Ship::AudioOutputPolicy> Audio;
};

class InGameMenu {
public:
  explicit InGameMenu(InGameMenuOwners owners);

  // Once per presented frame. `hostGuiOwnsInput` is the ImGui surface (F1)
  // being open: the menu closes and ignores its controls while it is.
  void Update(const InGameMenuHostInput &input, bool hostGuiOwnsInput, double elapsedSeconds);
  [[nodiscard]] bool IsOpen() const noexcept { return mModel.IsOpen(); }
  // The game holds while the menu (or the help card) is up: the window host
  // advances no guest frame then (decision 3b of the UX plan).
  [[nodiscard]] bool HoldsGame() const noexcept { return mModel.IsOpen(); }
  // The help card: on its own over the game (`firstPlay`, remembered once it
  // is dismissed), or over the menu from its Help & folders tab.
  void ShowHelpCard(bool firstPlay);
  // The first-play card unless the runtime configuration records it was
  // already shown (Help.Shown); true when it is shown now.
  bool ShowFirstPlayHelpCard();
  // The frame's primitives on the menu's canvas (canvas units): 240 units
  // high and as wide as the presentation picture's shape, so a phone's long
  // screen is used whole (oot3d::ui::N64UiCanvasMode::RegionShape).
  [[nodiscard]] std::vector<oot3d::ui::UiPrimitive> BuildPresentation();
  [[nodiscard]] float CanvasWidth() const noexcept { return mCanvasWidth; }
  static constexpr float kCanvasHeight = 240.0F;

  [[nodiscard]] Menu::Model &Model() noexcept { return mModel; }
  [[nodiscard]] const Menu::Model &Model() const noexcept { return mModel; }
  // Re-reads every owner and the rows of the current tab.
  void Refresh();

private:
  Menu::Document BuildDocument();
  void BuildVideoTab(Menu::Document &document);
  void BuildGraphicsTab(Menu::Document &document);
  void BuildControlsTab(Menu::Document &document);
  void BuildPhotoModeTab(Menu::Document &document);
  void BuildGameTab(Menu::Document &document);
  void BuildHelpTab(Menu::Document &document);
  [[nodiscard]] Menu::HelpContent HelpContent() const;
  void RefreshSnapshots();
  // The status line's word for the graphics owner's save state; a failed
  // save also asks whether to retry.
  Menu::WriteResult DescribeGraphicsSave();
  Menu::WriteResult SaveFailed(std::string detail, std::function<bool(std::string *)> retry);
  Menu::WriteResult ApplyGraphics(Fast::Extension::GraphicsSettings settings, bool keepPreset);
  Menu::WriteResult ApplyControls(const NativeControlConfig &config);
  Menu::WriteResult ApplyTopScreen(const TopScreenUiConfig &config);
  // A change through the audio policy; `preview`: a volume change, heard as
  // the host's preview tone while the game is held.
  Menu::WriteResult ApplyAudio(const Ship::AudioOutputSettings &settings, bool preview);
  void UpdateDisplayConfirmation();
  void UpdateSavestateOutcome();
  // The device in use, from the controls owner (Input): the footer, the help
  // card and the Help tab show its button prompts, changing with it while
  // the menu is open.
  [[nodiscard]] Menu::PromptSet CurrentPromptSet() const;
  void UpdatePrompts();

  InGameMenuOwners mOwners;
  Menu::PromptSet mPrompts = Menu::PromptSet::Keyboard;
  // "Modified" compares with the product's default graphics, owned by the
  // graphics runtime (ProductDefaultGraphicsSettings), not the struct
  // defaults (the Authentic look).
  Fast::Extension::GraphicsSettings mBaseline;
  Fast::Extension::GraphicsSettings mGraphics;
  Fast::Extension::GraphicsCapabilities mCapabilities;
  NativeControlConfig mControls;
  TopScreenUiConfig mTopScreen;
  Menu::Model mModel;
  // The canvas and the dp scale of the last update's picture, and whether a
  // finger drives the menu (larger targets, tap on release, drag to scroll).
  float mCanvasWidth = 1280.0F / 3.0F;
  Menu::Metrics mMetrics;
  bool mToggleHeld = false;
  bool mConfirmationModal = false;
  double mRefreshTimer = 0.0;
  std::optional<std::chrono::steady_clock::time_point> mSeenSavestateOutcome;
};

} // namespace Oot3dNativeGame
