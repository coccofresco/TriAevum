#include "oot3d_menu_prompts.h"

#include "oot3d_menu_glyphs.h"

namespace Oot3dNativeGame::Menu {
namespace {

std::string Solid(const char* label) { return GlyphFrameText(GlyphFrame::Solid, label); }
std::string Key(const char* label) { return GlyphFrameText(GlyphFrame::Key, label); }
std::string Stick(const char* label) { return GlyphFrameText(GlyphFrame::Stick, label); }
std::string Icon(GlyphIcon icon) { return GlyphIconText(icon); }

// The pad's labels. SDL maps a Nintendo pad by its printed labels (its
// default, SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS), so the menu's confirm
// button is the one printed A there too.
struct PadLabels {
    const char* Shoulders[2];
    const char* Sticks[2];
};

PadLabels LabelsFor(PromptSet set) noexcept {
    switch (set) {
    case PromptSet::Xbox: return {{"LB", "RB"}, {"LS", "RS"}};
    case PromptSet::PlayStation: return {{"L1", "R1"}, {"L3", "R3"}};
    case PromptSet::Nintendo: return {{"L", "R"}, {"LS", "RS"}};
    case PromptSet::SteamDeck: return {{"L1", "R1"}, {"L3", "R3"}};
    default: return {{"L", "R"}, {"L3", "R3"}};
    }
}

std::string Face(PromptSet set, bool south) {
    switch (set) {
    case PromptSet::PlayStation: return Icon(south ? GlyphIcon::PsCross : GlyphIcon::PsCircle);
    default: return Solid(south ? "A" : "B");
    }
}

} // namespace

bool IsPadPromptSet(PromptSet set) noexcept { return set != PromptSet::Keyboard && set != PromptSet::Touch; }

std::string PromptText(PromptSet set, PromptAction action) {
    if (set == PromptSet::Keyboard) {
        switch (action) {
        case PromptAction::Confirm: return Icon(GlyphIcon::KeyEnter);
        case PromptAction::Back: return Icon(GlyphIcon::KeyBackspace);
        case PromptAction::TabPrevious: return Key("Q");
        case PromptAction::TabNext: return Key("E");
        case PromptAction::Change: return Icon(GlyphIcon::KeyLeft) + " " + Icon(GlyphIcon::KeyRight);
        case PromptAction::Menu: return Key("Esc");
        case PromptAction::AllSettings: return Key("F1");
        case PromptAction::SaveState: return Key("F5");
        case PromptAction::LoadState: return Key("F8");
        case PromptAction::PhotoCamera: return Key("F6");
        case PromptAction::Fullscreen: return Key("F11");
        }
        return {};
    }
    if (set == PromptSet::Touch) {
        switch (action) {
        case PromptAction::Confirm: return Icon(GlyphIcon::Tap);
        // Tap HOME opens and closes the menu, hold HOME opens F1.
        case PromptAction::Menu:
        case PromptAction::AllSettings: return Icon(GlyphIcon::Home);
        default: return {};
        }
    }
    const auto labels = LabelsFor(set);
    switch (action) {
    case PromptAction::Confirm: return Face(set, true);
    case PromptAction::Back: return Face(set, false);
    case PromptAction::TabPrevious: return Solid(labels.Shoulders[0]);
    case PromptAction::TabNext: return Solid(labels.Shoulders[1]);
    case PromptAction::Change: return Icon(GlyphIcon::DpadHorizontal);
    case PromptAction::Menu: return Stick(labels.Sticks[1]);
    case PromptAction::AllSettings: return Stick(labels.Sticks[0]) + " + " + Stick(labels.Sticks[1]);
    // Keyboard keys only (the menu's Game tab has the rows).
    case PromptAction::SaveState:
    case PromptAction::LoadState:
    case PromptAction::PhotoCamera:
    case PromptAction::Fullscreen: return {};
    }
    return {};
}

Hints HintsFor(PromptSet set) {
    Hints hints;
    hints.Confirm = PromptText(set, PromptAction::Confirm);
    hints.Back = PromptText(set, PromptAction::Back);
    hints.TabPrevious = PromptText(set, PromptAction::TabPrevious);
    hints.TabNext = PromptText(set, PromptAction::TabNext);
    hints.Change = PromptText(set, PromptAction::Change);
    hints.Close = PromptText(set, PromptAction::Menu);
    return hints;
}

} // namespace Oot3dNativeGame::Menu
