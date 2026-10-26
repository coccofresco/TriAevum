#include "oot3d_menu_help.h"

#include <utility>

namespace Oot3dNativeGame::Menu {
namespace {

Row Heading(std::string key) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Heading;
    return row;
}

Row Info(std::string key, std::string text) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Info;
    row.Text = std::move(text);
    return row;
}

Row Action(std::string key, std::string help, std::function<WriteResult()> activate) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Action;
    row.Help = std::move(help);
    row.Activate = std::move(activate);
    return row;
}

} // namespace

std::vector<std::string> HelpCardLines(const HelpContent& content) {
    std::vector<std::string> lines;
    if (content.Controls == HelpControls::Touch) {
        lines.emplace_back("Tap HOME: game menu.");
        lines.emplace_back("Hold HOME: all settings.");
    } else {
        lines.emplace_back("Esc or R3: game menu.");
        lines.emplace_back("F1 or L3+R3: all settings.");
        lines.emplace_back("F5 / F8: save / load a state.");
    }
    lines.emplace_back("The game pauses while its menu is open.");
    if (!content.SaveDataPath.empty()) lines.push_back("Your saves: " + content.SaveDataPath);
    return lines;
}

Modal HelpCard(const HelpContent& content, std::function<void()> onDismiss) {
    Modal card;
    card.Title = "Welcome to TriAevum";
    card.Lines = HelpCardLines(content);
    card.ConfirmLabel = "OK";
    card.CancelLabel.clear();
    card.OnConfirm = std::move(onDismiss);
    card.WidthDp = 720.0F;
    return card;
}

Modal QuitConfirmation(std::function<void()> quit) {
    Modal modal;
    modal.Title = "Quit the game?";
    modal.Lines = {"Your settings are kept. The game's own progress is not saved by quitting: "
                   "save in the game, or save a state, first."};
    modal.ConfirmLabel = "Quit";
    modal.CancelLabel = "Cancel";
    modal.OnConfirm = std::move(quit);
    return modal;
}

Modal SaveFailure(std::string detail, std::function<void()> retry) {
    Modal modal;
    modal.Title = "Could not save";
    modal.Lines = {"The change applies for now but was not written.", std::move(detail)};
    modal.ConfirmLabel = "Retry";
    modal.CancelLabel = "Not now";
    modal.OnConfirm = std::move(retry);
    return modal;
}

Tab BuildHelpTab(const HelpTabActions& actions) {
    Tab tab;
    tab.Title = kHelpTabTitle;
    tab.Rows.push_back(Heading("Controls"));
    if (actions.Content.Controls == HelpControls::Touch) {
        tab.Rows.push_back(Info("Game menu", "Tap HOME"));
        tab.Rows.push_back(Info("All settings", "Hold HOME"));
    } else {
        tab.Rows.push_back(Info("Game menu", "Esc \xC2\xB7 R3"));                 // ·
        tab.Rows.push_back(Info("All settings (F1)", "F1 \xC2\xB7 L3 + R3"));
        tab.Rows.push_back(Info("Save / load a state", "F5 / F8"));
        tab.Rows.push_back(Info("Photo camera", "F6"));
        tab.Rows.push_back(Info("Fullscreen", "F11"));
    }
    if (actions.ShowHelpCard) {
        tab.Rows.push_back(Action("Show the help card", "The card shown at the first start: the menu keys, the "
                                                        "settings keys and where your saves are.",
                                  actions.ShowHelpCard));
    }
    const bool folders = actions.OpenSavesFolder || actions.OpenLogsFolder || actions.ExportSavesInInstaller ||
                         actions.ShareDiagnosticsInInstaller || !actions.InstallerRoute.empty();
    if (folders) tab.Rows.push_back(Heading("Folders"));
    if (actions.OpenSavesFolder) {
        std::string help = "Opens the folder of the game's saves in the file browser.";
        if (!actions.Content.SaveDataPath.empty()) help += "\n" + actions.Content.SaveDataPath;
        tab.Rows.push_back(Action("Open saves folder", std::move(help), actions.OpenSavesFolder));
    }
    if (actions.OpenLogsFolder) {
        tab.Rows.push_back(Action("Open logs folder",
                                  "Opens the folder of the game's logs, to attach them to a report.",
                                  actions.OpenLogsFolder));
    }
    if (actions.ExportSavesInInstaller) {
        tab.Rows.push_back(Action("Export saves", "Opens the installer's Saves page to export or import your saves.",
                                  actions.ExportSavesInInstaller));
    }
    if (actions.ShareDiagnosticsInInstaller) {
        tab.Rows.push_back(Action("Share diagnostics",
                                  "Opens the installer's Diagnostics page to share a report of this device.",
                                  actions.ShareDiagnosticsInInstaller));
    }
    if (!actions.InstallerRoute.empty()) tab.Rows.push_back(Info("Saves and diagnostics", actions.InstallerRoute));
    return tab;
}

} // namespace Oot3dNativeGame::Menu
