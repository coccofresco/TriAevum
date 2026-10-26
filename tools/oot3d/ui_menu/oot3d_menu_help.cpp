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
    const auto prompt = [&](PromptAction action) { return PromptText(content.Prompts, action); };
    std::vector<std::string> lines;
    if (content.Prompts == PromptSet::Touch) {
        lines.push_back("Tap " + prompt(PromptAction::Menu) + ": game menu.");
        lines.push_back("Hold " + prompt(PromptAction::AllSettings) + ": all settings.");
    } else if (content.Prompts == PromptSet::Keyboard) {
        lines.push_back(prompt(PromptAction::Menu) + ": game menu.");
        lines.push_back(prompt(PromptAction::AllSettings) + ": all settings.");
        lines.push_back(prompt(PromptAction::SaveState) + " / " + prompt(PromptAction::LoadState) +
                        ": save / load a state.");
    } else {
        lines.push_back(prompt(PromptAction::Menu) + ": game menu.");
        lines.push_back(prompt(PromptAction::AllSettings) + ": all settings.");
        lines.emplace_back("Save states: the game menu's Game tab.");
    }
    lines.emplace_back("The game pauses while its menu is open.");
    if (!content.SaveDataPath.empty()) lines.push_back("Your saves: " + content.SaveDataPath);
    return lines;
}

Modal HelpCard(const HelpContent& content, std::function<void()> onDismiss) {
    Modal card;
    card.Title = kHelpCardTitle;
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
    // The controls of the device in use, with its button prompts.
    const auto set = actions.Content.Prompts;
    const auto prompt = [set](PromptAction action) { return PromptText(set, action); };
    tab.Rows.push_back(Heading("Controls"));
    if (set == PromptSet::Touch) {
        tab.Rows.push_back(Info("Game menu", "Tap " + prompt(PromptAction::Menu)));
        tab.Rows.push_back(Info("All settings", "Hold " + prompt(PromptAction::AllSettings)));
    } else if (set == PromptSet::Keyboard) {
        tab.Rows.push_back(Info("Game menu", prompt(PromptAction::Menu)));
        tab.Rows.push_back(Info("All settings (F1)", prompt(PromptAction::AllSettings)));
        tab.Rows.push_back(Info("Save / load a state",
                                prompt(PromptAction::SaveState) + " / " + prompt(PromptAction::LoadState)));
        tab.Rows.push_back(Info("Photo camera", prompt(PromptAction::PhotoCamera)));
        tab.Rows.push_back(Info("Fullscreen", prompt(PromptAction::Fullscreen)));
    } else {
        tab.Rows.push_back(Info("Game menu", prompt(PromptAction::Menu)));
        tab.Rows.push_back(Info("All settings (F1)", prompt(PromptAction::AllSettings)));
        tab.Rows.push_back(Info("Select / back", prompt(PromptAction::Confirm) + " / " + prompt(PromptAction::Back)));
        tab.Rows.push_back(Info("Switch tabs", prompt(PromptAction::TabPrevious) + " / " + prompt(PromptAction::TabNext)));
        tab.Rows.push_back(Info("Save states", "Game tab"));
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
