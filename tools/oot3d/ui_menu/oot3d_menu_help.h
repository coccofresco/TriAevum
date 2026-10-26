#pragma once

// The in-game menu's everyday help, apart from any host: the first-play help
// card, the Help & folders tab, the quit question and the status vocabulary
// every row reports with. The host supplies the actions (open a folder, open
// F1, quit) and the resolved paths; nothing here touches a file or a setting.

#include "oot3d_menu_model.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Oot3dNativeGame::Menu {

// The status line's words for the outcome of a change (one vocabulary for
// every owner the menu writes through).
inline constexpr const char* kStatusSaved = "Saved";
inline constexpr const char* kStatusPreview = "Preview: press Save in F1 to keep";
inline constexpr const char* kStatusSessionOnly = "Session only";
inline constexpr const char* kStatusSaveFailed = "Could not save: Retry";

// The controls the card and the Help tab describe.
enum class HelpControls : std::uint8_t { KeyboardAndController, Touch };

struct HelpContent {
    HelpControls Controls = HelpControls::KeyboardAndController;
    // The resolved save-data folder, shown at runtime only; empty: left out.
    std::string SaveDataPath;
};

// The card's lines, in order: what opens the game menu and all settings,
// the save-state keys, and where the saves are.
[[nodiscard]] std::vector<std::string> HelpCardLines(const HelpContent& content);

// The first-play help card: a notice with one button. `onDismiss` runs when
// it is dismissed (OK or Back), for example to remember it was shown.
[[nodiscard]] Modal HelpCard(const HelpContent& content, std::function<void()> onDismiss);

// The quit question: the game's own progress is not saved by quitting.
[[nodiscard]] Modal QuitConfirmation(std::function<void()> quit);

// A save that failed: the error, and Retry calls the owner again.
[[nodiscard]] Modal SaveFailure(std::string detail, std::function<void()> retry);

// The host actions of the Help & folders tab. An absent folder action leaves
// its row out (a platform without a file browser). Android hands saves and
// diagnostics to the installer's Saves and Diagnostics pages: with the
// installer actions present they become rows, otherwise `InstallerRoute`
// tells where they are.
struct HelpTabActions {
    HelpContent Content;
    std::function<WriteResult()> ShowHelpCard;
    std::function<WriteResult()> OpenSavesFolder;
    std::function<WriteResult()> OpenLogsFolder;
    std::function<WriteResult()> ExportSavesInInstaller;
    std::function<WriteResult()> ShareDiagnosticsInInstaller;
    std::string InstallerRoute;
};

inline constexpr const char* kHelpTabTitle = "Help & folders";

[[nodiscard]] Tab BuildHelpTab(const HelpTabActions& actions);

} // namespace Oot3dNativeGame::Menu
