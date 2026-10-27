// The in-game menu apart from the game: model navigation and edits, the baked
// fonts, the button prompts and their glyphs, and the presentation's
// primitives.
#include "oot3d_menu_audio.h"
#include "oot3d_menu_button_glyphs.h"
#include "oot3d_menu_font.h"
#include "oot3d_menu_font_atlas_data.h"
#include "oot3d_menu_glyphs.h"
#include "oot3d_menu_help.h"
#include "oot3d_menu_model.h"
#include "oot3d_menu_presentation.h"
#include "oot3d_menu_prompts.h"
#include "oot3d_menu_textures.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

using namespace Oot3dNativeGame::Menu;

void Expect(bool condition, const char* what) {
    if (!condition) throw std::runtime_error(what);
}

struct Store {
    int Toggle = 0;
    int Choice = 1;
    int Number = 10;
    int Actions = 0;
    bool FailWrites = false;
};

Document BuildDocument(Store& store) {
    Document document;
    Tab video;
    video.Title = "Video";
    Row heading;
    heading.Key = "Display";
    heading.Kind = RowKind::Heading;
    video.Rows.push_back(heading);
    Row toggle;
    toggle.Key = "VSync";
    toggle.Kind = RowKind::Toggle;
    toggle.Help = "Waits for the display.";
    toggle.Read = [&store] { return store.Toggle; };
    toggle.Write = [&store](int value) {
        if (store.FailWrites) return WriteResult{"Could not save: disk full", true};
        store.Toggle = value;
        return WriteResult{};
    };
    toggle.Modified = [&store] { return store.Toggle != 0; };
    video.Rows.push_back(toggle);
    Row choice;
    choice.Key = "Anti-aliasing";
    choice.Kind = RowKind::Choice;
    choice.Choices = {"Off", "FXAA", "MSAA", "TAA"};
    choice.Read = [&store] { return store.Choice; };
    choice.Write = [&store](int value) {
        store.Choice = value;
        return WriteResult{};
    };
    choice.ChoiceAvailable = [](int value) { return value != 2; };
    video.Rows.push_back(choice);
    Row number;
    number.Key = "Scene FOV";
    number.Kind = RowKind::Number;
    number.Minimum = 10;
    number.Maximum = 15;
    number.Step = 1;
    number.Format = [](int value) { return std::to_string(value) + "0%"; };
    number.Read = [&store] { return store.Number; };
    number.Write = [&store](int value) {
        store.Number = value;
        return WriteResult{};
    };
    video.Rows.push_back(number);
    document.Tabs.push_back(video);
    Tab game;
    game.Title = "Game";
    Row action;
    action.Key = "Save state";
    action.Kind = RowKind::Action;
    action.Activate = [&store] {
        ++store.Actions;
        return WriteResult{"State saved", false};
    };
    game.Rows.push_back(action);
    Row unavailable;
    unavailable.Key = "Free camera";
    unavailable.Kind = RowKind::Toggle;
    unavailable.Read = [] { return 0; };
    unavailable.Write = [](int) { return WriteResult{}; };
    unavailable.Available = [] { return false; };
    unavailable.UnavailableReason = "Only during gameplay.";
    game.Rows.push_back(unavailable);
    Row info;
    info.Key = "Move";
    info.Kind = RowKind::Info;
    info.Text = "WASD";
    game.Rows.push_back(info);
    document.Tabs.push_back(game);
    return document;
}

Input Press(bool Input::*field) {
    Input input;
    input.*field = true;
    return input;
}

void Tap(Model& model, bool Input::*field) {
    model.Update(Press(field), 0.016);
    model.Update(Input{}, 0.016);
}

void TestModelNavigation() {
    Store store;
    Model model(BuildDocument(store));
    Expect(!model.IsOpen(), "closed at start");
    model.Open();
    Expect(model.IsOpen(), "open");
    Expect(model.RowIndex() == 1, "the first focusable row is focused, not the heading");
    Expect(model.FocusedRow()->Value == 0, "values are read on open");
    // Confirm held at the opening press does nothing.
    model.Update(Press(&Input::Confirm), 0.016);
    Expect(store.Toggle == 0, "the opening press is not a confirm");
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Confirm);
    Expect(store.Toggle == 1 && model.FocusedRow()->Value == 1, "confirm toggles and refreshes");
    Expect(model.Status() == "Saved" && !model.StatusIsError(), "a successful write says Saved");
    Expect(model.FocusedRow()->IsModified, "modified mark");
    Tap(model, &Input::Right);
    Expect(store.Toggle == 0, "right toggles back");
    Tap(model, &Input::Down);
    Expect(model.RowIndex() == 2, "down moves to the choice row");
    Tap(model, &Input::Right);
    Expect(store.Choice == 3, "right skips the unavailable choice (MSAA)");
    Tap(model, &Input::Right);
    Expect(store.Choice == 0, "choices wrap");
    Tap(model, &Input::Left);
    Expect(store.Choice == 3, "left wraps backwards");
    Tap(model, &Input::Confirm);
    Expect(model.FocusPane() == Pane::Choices && model.ChoiceIndex() == 3, "confirm opens the choice pane at the current value");
    Tap(model, &Input::Up);
    Tap(model, &Input::Up);
    Expect(model.ChoiceIndex() == 1, "up moves the choice cursor");
    Tap(model, &Input::Confirm);
    Expect(store.Choice == 1 && model.FocusPane() == Pane::Rows, "confirm applies the choice and returns");
    Tap(model, &Input::Confirm);
    Tap(model, &Input::Down);
    Expect(model.ChoiceIndex() == 2, "cursor on the unavailable choice");
    Tap(model, &Input::Confirm);
    Expect(store.Choice == 1 && model.FocusPane() == Pane::Choices && model.StatusIsError(),
           "an unavailable choice is refused with an error status");
    Tap(model, &Input::Back);
    Expect(model.FocusPane() == Pane::Rows && model.IsOpen(), "back leaves the choice pane");
    Tap(model, &Input::Down);
    Expect(model.RowIndex() == 3, "number row");
    Tap(model, &Input::Right);
    Tap(model, &Input::Right);
    Expect(store.Number == 12, "number steps");
    Expect(model.FocusedRow()->ValueText() == "120%", "number formatting");
    for (int step = 0; step < 10; ++step) Tap(model, &Input::Right);
    Expect(store.Number == 15, "number clamps at the maximum");
    Tap(model, &Input::Down);
    Expect(model.RowIndex() == 1, "down wraps to the first focusable row");
    Tap(model, &Input::Up);
    Expect(model.RowIndex() == 3, "up wraps to the last row");
    // Auto-repeat while held: one step at the press, then after the delay.
    model.Update(Press(&Input::Left), 0.016);
    Expect(store.Number == 14, "press edge steps once");
    model.Update(Press(&Input::Left), 0.2);
    Expect(store.Number == 14, "no repeat before the delay");
    model.Update(Press(&Input::Left), 0.2);
    Expect(store.Number == 13, "repeat after the delay");
    model.Update(Press(&Input::Left), 0.1);
    Expect(store.Number == 12, "repeat at the interval");
    model.Update(Input{}, 0.016);
    Tap(model, &Input::TabNext);
    Expect(model.TabIndex() == 1 && model.RowIndex() == 0, "next tab, first row");
    Tap(model, &Input::Confirm);
    Expect(store.Actions == 1 && model.Status() == "State saved", "actions run and report");
    Tap(model, &Input::Down);
    Expect(!model.FocusedRow()->Enabled, "unavailable row is disabled");
    Tap(model, &Input::Confirm);
    Tap(model, &Input::Right);
    Expect(model.Status() == "State saved", "a disabled row ignores edits");
    Tap(model, &Input::Down);
    Expect(model.RowIndex() == 0, "down skips the info row and wraps to the first row");
    Expect(model.Doc().Tabs[1].Rows[2].ValueText() == "WASD" && !model.Doc().Tabs[1].Rows[2].Focusable(),
           "an info row shows its text and takes no focus");
    Tap(model, &Input::TabNext);
    Expect(model.TabIndex() == 0, "tabs wrap");
    Tap(model, &Input::TabPrevious);
    Expect(model.TabIndex() == 1, "previous tab wraps backwards");
    store.FailWrites = true;
    Tap(model, &Input::TabPrevious);
    Tap(model, &Input::Confirm);
    Expect(model.StatusIsError() && model.Status() == "Could not save: disk full", "write errors reach the status line");
    Tap(model, &Input::Back);
    Expect(!model.IsOpen(), "back closes the menu");
}

void TestModal() {
    Store store;
    Model model(BuildDocument(store));
    model.Open();
    int confirmed = 0, cancelled = 0;
    Modal modal;
    modal.Title = "Keep display settings?";
    modal.Lines = {"Reverting in 10 seconds"};
    modal.OnConfirm = [&] { ++confirmed; };
    modal.OnCancel = [&] { ++cancelled; };
    model.SetModal(modal);
    Expect(model.FocusPane() == Pane::Modal && model.ModalConfirmHighlighted(), "modal focus, confirm first");
    model.Update(Press(&Input::Confirm), 0.016); // still held from before the modal
    Expect(confirmed == 0, "the press that was held when the modal appeared does not confirm");
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Down);
    Expect(model.RowIndex() == 1, "rows do not move under a modal");
    Tap(model, &Input::Right);
    Expect(!model.ModalConfirmHighlighted(), "right highlights cancel");
    Tap(model, &Input::Confirm);
    Expect(cancelled == 1 && confirmed == 0 && !model.CurrentModal().has_value(), "confirm on cancel cancels");
    model.SetModal(modal);
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Back);
    Expect(cancelled == 2 && !model.CurrentModal().has_value(), "back cancels");
    model.SetModal(modal);
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Confirm);
    Expect(confirmed == 1 && model.IsOpen(), "confirm confirms and the menu stays open");
}

// The pointer against the layout of a drawn frame: hover focuses, clicks
// select and activate, the arrows step and repeat, the wheel moves, the
// close hint closes, and a modal takes every click.
void TestPointer() {
    Store store;
    Model model(BuildDocument(store));
    model.Open();
    std::vector<oot3d::ui::UiPrimitive> output;
    const auto draw = [&] {
        output.clear();
        AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output);
    };
    const auto region = [&](HitKind kind, std::size_t index) {
        for (const auto& candidate : model.CurrentLayout().Regions) {
            if (candidate.Kind == kind && candidate.Index == index) return candidate;
        }
        throw std::runtime_error(std::string("region not laid out: ") + MenuHitKindName(kind));
    };
    const auto at = [&](HitKind kind, std::size_t index, bool pressed = false, float wheel = 0.0F) {
        const auto found = region(kind, index);
        Pointer pointer;
        pointer.Present = true;
        pointer.X = found.Area.X + found.Area.Width * 0.5F;
        pointer.Y = found.Area.Y + found.Area.Height * 0.5F;
        pointer.Pressed = pressed;
        pointer.Wheel = wheel;
        return pointer;
    };
    const auto click = [&](HitKind kind, std::size_t index) {
        model.Update(Input{}, at(kind, index, true), 0.016);
        model.Update(Input{}, at(kind, index, false), 0.016);
        draw();
    };
    draw();
    // Two tabs, three focusable rows and the close hint; the choice list and
    // the number arrows appear only with such a row focused.
    Expect(model.CurrentLayout().Regions.size() == 6, "the drawn frame lays out its targets");
    Expect(std::none_of(model.CurrentLayout().Regions.begin(), model.CurrentLayout().Regions.end(),
                        [](const auto& r) { return r.Kind == HitKind::Row && r.Index == 0; }),
           "a heading is not a target");
    // Hovering a row focuses it; a stationary pointer does not fight the keys.
    model.Update(Input{}, at(HitKind::Row, 1), 0.016); // where the mouse rests at the opening
    model.Update(Input{}, at(HitKind::Row, 3), 0.016);
    Expect(model.RowIndex() == 3 && model.Hover().Kind == HitKind::Row, "hover focuses the row under the pointer");
    Tap(model, &Input::Up);
    Expect(model.RowIndex() == 2, "keys still move the focus while the pointer rests");
    draw();
    model.Update(Input{}, at(HitKind::Row, 3), 0.016);
    Expect(model.RowIndex() == 2, "a pointer that did not move does not refocus");
    // A click on the toggle row flips it.
    click(HitKind::Row, 1);
    Expect(store.Toggle == 1 && model.RowIndex() == 1, "click toggles the row it is on");
    // A click on the choice row opens its list; a click on a value applies it.
    click(HitKind::Row, 2);
    Expect(model.FocusPane() == Pane::Choices, "click opens the choice list");
    model.Update(Input{}, at(HitKind::Choice, 3), 0.016);
    Expect(model.ChoiceIndex() == 3, "hovering a value moves the list cursor");
    click(HitKind::Choice, 3);
    Expect(store.Choice == 3 && model.FocusPane() == Pane::Rows, "clicking a value applies it and closes the list");
    click(HitKind::Choice, 2);
    Expect(store.Choice == 3 && model.StatusIsError(), "an unavailable value is refused by the click too");
    // Number arrows step and repeat while held; a click on the number row only focuses it.
    click(HitKind::Row, 3);
    Expect(store.Number == 10 && model.RowIndex() == 3, "clicking a number row focuses without stepping");
    click(HitKind::NumberUp, 0);
    Expect(store.Number == 11, "the › arrow steps up");
    model.Update(Input{}, at(HitKind::NumberDown, 0, true), 0.016);
    Expect(store.Number == 10, "the ‹ arrow steps down on the press");
    model.Update(Input{}, at(HitKind::NumberDown, 0, true), 0.2);
    model.Update(Input{}, at(HitKind::NumberDown, 0, true), 0.2);
    Expect(store.Number == 10, "held at the minimum it clamps");
    model.Update(Input{}, at(HitKind::NumberDown, 0, false), 0.016);
    store.Number = 15;
    model.Refresh();
    model.Update(Input{}, at(HitKind::NumberDown, 0, true), 0.016);
    model.Update(Input{}, at(HitKind::NumberDown, 0, true), 0.4);
    Expect(store.Number == 13, "held, the arrow repeats after the delay");
    model.Update(Input{}, at(HitKind::NumberDown, 0, false), 0.016);
    // The wheel moves the focus, over an arrow too; over the list it moves the cursor.
    draw();
    model.Update(Input{}, at(HitKind::NumberUp, 0, false, 1.0F), 0.016);
    Expect(model.RowIndex() == 2, "wheel up over an arrow moves to the previous row");
    draw();
    model.Update(Input{}, at(HitKind::Row, 2, false, -1.0F), 0.016);
    Expect(model.RowIndex() == 3, "wheel down moves to the next row");
    model.Update(Input{}, at(HitKind::Row, 2, false, 1.0F), 0.016);
    Expect(model.RowIndex() == 2, "wheel up moves to the previous row");
    model.Update(Input{}, at(HitKind::Row, 2, false, -2.0F), 0.016);
    Expect(model.RowIndex() == 1, "wheel down moves on, wrapping");
    // A click on a tab selects it.
    draw();
    click(HitKind::Tab, 1);
    Expect(model.TabIndex() == 1 && model.RowIndex() == 0, "clicking a tab selects it");
    click(HitKind::Row, 0);
    Expect(store.Actions == 1, "clicking an action runs it");
    // A modal takes the pointer: hover highlights, click decides, rows stay.
    int confirmed = 0, cancelled = 0;
    Modal modal;
    modal.Title = "Keep display settings?";
    modal.OnConfirm = [&] { ++confirmed; };
    modal.OnCancel = [&] { ++cancelled; };
    model.SetModal(modal);
    draw();
    Expect(model.CurrentLayout().Regions.front().Kind == HitKind::ModalConfirm, "the modal's buttons come first");
    model.Update(Input{}, at(HitKind::ModalCancel, 0), 0.016);
    Expect(!model.ModalConfirmHighlighted(), "hovering the cancel button highlights it");
    click(HitKind::Row, 0);
    Expect(store.Actions == 1 && model.CurrentModal().has_value(), "rows under a modal ignore clicks");
    draw();
    click(HitKind::ModalConfirm, 0);
    Expect(confirmed == 1 && cancelled == 0 && !model.CurrentModal().has_value(), "clicking Keep confirms");
    // The close hint closes the menu (and the layout goes with it).
    draw();
    auto closePointer = at(HitKind::Close, 0, true);
    model.Update(Input{}, closePointer, 0.016);
    Expect(!model.IsOpen() && model.CurrentLayout().Regions.empty(), "clicking the close hint closes the menu");
    closePointer.Pressed = false;
    model.Update(Input{}, closePointer, 0.016);
    // A pointer held when the menu opens is not a click, and where it rests
    // is not a move: the first row keeps the focus until the mouse moves.
    // (The Game tab is current: row 0 the action, row 1 the disabled toggle.)
    model.Open();
    draw();
    model.Update(Input{}, at(HitKind::Row, 1, true), 0.016);
    Expect(store.Actions == 1 && model.RowIndex() == 0, "a button held at the opening is not a click, nor a move");
    model.Update(Input{}, at(HitKind::Row, 1, false), 0.016);
    Expect(model.RowIndex() == 0, "a resting mouse does not take the focus on opening");
    auto nudged = at(HitKind::Row, 1);
    nudged.X += 1.0F;
    model.Update(Input{}, nudged, 0.016);
    Expect(model.RowIndex() == 1, "once it moves it focuses the row under it");
    Pointer away;
    model.Update(Input{}, away, 0.016);
    Expect(model.Hover().Kind == HitKind::None, "no hover without a pointer");
}

void TestFonts() {
    for (const auto font : {FontStyle::Body, FontStyle::Heading}) {
        const auto metrics = MeasureFont(font, 4.0F);
        Expect(metrics.Ascent > 3.0F && metrics.Descent > 0.5F && metrics.Descent < metrics.Ascent, "font metrics");
        const float hello = MeasureText(font, 4.0F, "Hello");
        Expect(hello > 6.0F && hello < 14.0F, "measure");
        Expect(MeasureText(font, 8.0F, "Hello") > hello * 1.9F, "measure scales with size");
        Expect(MeasureText(font, 4.0F, "\xC3\xA8\xC3\xA9 \xE2\x80\xA2 \xE2\x80\xBA") > 0.0F, "latin-1 and symbols");
        Expect(MeasureText(font, 4.0F, "ab", true) == MeasureText(font, 4.0F, "AB"), "uppercase measure");
    }
    // The menu's symbols (‹ › • … and the arrows) are glyphs both atlases
    // hold, never the .notdef box of a code point Fira Sans lacks.
    for (const auto* atlas : {&Oot3dNativeGame::MenuFontData::kBodyAtlas, &Oot3dNativeGame::MenuFontData::kHeadingAtlas}) {
        for (const std::uint32_t codepoint : {0x2039U, 0x203AU, 0x2022U, 0x2026U, 0x2190U, 0x2192U}) {
            const bool present = std::any_of(atlas->Glyphs.begin(), atlas->Glyphs.end(),
                                             [&](const auto& glyph) { return glyph.Codepoint == codepoint; });
            Expect(present, "menu symbol in the atlas");
        }
        const bool triangle = std::any_of(atlas->Glyphs.begin(), atlas->Glyphs.end(),
                                          [](const auto& glyph) { return glyph.Codepoint == 0x25B6U; });
        Expect(!triangle, "no triangle placeholder in the atlas");
    }
    Expect(UppercaseUtf8("caf\xC3\xA9 x") == "CAF\xC3\x89 X", "uppercase keeps UTF-8");
    const auto lines = WrapText(FontStyle::Body, 4.0F, "one two three four five six seven eight nine ten", 30.0F);
    Expect(lines.size() >= 3, "wrap splits long text");
    for (const auto& line : lines) Expect(MeasureText(FontStyle::Body, 4.0F, line) <= 30.0F, "wrapped lines fit");
    const auto paragraphs = WrapText(FontStyle::Body, 4.0F, "first\nsecond", 100.0F);
    Expect(paragraphs.size() == 2 && paragraphs[1] == "second", "newline breaks");
    const auto elided = ElideText(FontStyle::Body, 4.0F, "a very long value that cannot fit", 20.0F);
    Expect(elided.ends_with("\xE2\x80\xA6") && MeasureText(FontStyle::Body, 4.0F, elided) <= 20.0F, "elide");

    std::vector<oot3d::ui::UiPrimitive> output;
    TextStyle style;
    style.Size = 4.0F;
    style.Align = TextAlign::Right;
    const float width = AppendText(output, style, 100.0F, 50.0F, "Hi!");
    Expect(output.size() == 3 && width > 0.0F, "one quad per visible glyph");
    for (const auto& primitive : output) {
        Expect(primitive.texture.semantic_name == kBodyFontTexture && primitive.texture_alpha_only, "font texture");
        Expect(primitive.destination.x >= 100.0F - width - 0.5F && primitive.destination.x + primitive.destination.width <= 100.5F,
               "right alignment");
        Expect(primitive.uv.width > 0.0F && primitive.uv.height > 0.0F && primitive.uv.x + primitive.uv.width <= 1.0F &&
                   primitive.uv.y + primitive.uv.height <= 1.0F,
               "uv inside the atlas");
    }

    MenuFontTextureProvider provider(nullptr);
    oot3d::ui::UiTexturePixels pixels;
    std::string error;
    Expect(provider.Resolve({0, 0, std::string(kHeadingFontTexture)}, pixels, &error), error.c_str());
    Expect(pixels.width == 1024 && pixels.height > 0 && pixels.rgba8.size() == pixels.width * pixels.height * 4U, "atlas size");
    std::size_t covered = 0;
    for (std::size_t texel = 3; texel < pixels.rgba8.size(); texel += 4) covered += pixels.rgba8[texel] != 0 ? 1 : 0;
    Expect(covered > 10000 && pixels.rgba8[0] == 0xFF, "coverage in alpha, white colour");
    Expect(!provider.Resolve({0, 0, "oot3d/unknown"}, pixels, &error) && !error.empty(), "other names need the next provider");
}

void TestPresentation() {
    Store store;
    Model model(BuildDocument(store));
    std::vector<oot3d::ui::UiPrimitive> output;
    Expect(AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output) == 0 && output.empty(), "closed menu draws nothing");
    model.Open();
    const auto count = AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output);
    Expect(count > 40 && count == output.size(), "an open menu draws its parts");
    std::size_t text = 0, solid = 0;
    for (const auto& primitive : output) {
        Expect(primitive.destination.width > 0.0F && primitive.destination.height > 0.0F, "no empty quads");
        Expect(primitive.destination.x >= -0.5F && primitive.destination.y >= -0.5F &&
                   primitive.destination.x + primitive.destination.width <= 1280.0F / 3.0F + 0.5F &&
                   primitive.destination.y + primitive.destination.height <= 240.5F,
               "inside the canvas");
        if (primitive.texture.semantic_name.empty()) {
            ++solid;
        } else {
            Expect(MenuFontTextureProvider::IsMenuTexture(primitive.texture.semantic_name), "text uses the menu fonts");
            ++text;
        }
    }
    Expect(text > 20 && solid > 5, "text and panels");
    Expect(model.ScrollOffset() == 0, "no scroll for a short tab");
    // The right pane shows the choice list when a choice row is focused.
    Tap(model, &Input::Down);
    output.clear();
    const auto withChoices = AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output);
    Expect(withChoices > count, "choice rows add their list");
    Modal modal;
    modal.Title = "Keep display settings?";
    modal.Lines = {"Reverting in 9 seconds"};
    model.SetModal(modal);
    output.clear();
    Expect(AppendMenuPresentation(model, 400.0F, 240.0F, output) > withChoices, "the modal draws on top");
    Expect(std::any_of(output.begin(), output.end(), [](const auto& p) { return p.layer >= 20; }), "modal layers");
}

const HitRegion* FindRegion(const Model& model, HitKind kind) {
    for (const auto& region : model.CurrentLayout().Regions)
        if (region.Kind == kind) return &region;
    return nullptr;
}

// A notice has one button: confirm and Back both dismiss it with its answer,
// and left / right do not move to a missing Cancel.
void TestSingleButtonModal() {
    Store store;
    Model model(BuildDocument(store));
    model.Open();
    int dismissed = 0;
    Modal notice;
    notice.Title = "Notice";
    notice.CancelLabel.clear();
    notice.OnConfirm = [&] { ++dismissed; };
    model.SetModal(notice);
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Right);
    Expect(model.ModalConfirmHighlighted(), "a notice keeps its only button highlighted");
    Tap(model, &Input::Back);
    Expect(dismissed == 1 && !model.CurrentModal() && model.IsOpen(), "back dismisses a notice like OK");
    model.SetModal(notice);
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Confirm);
    Expect(dismissed == 2, "confirm dismisses a notice");
    std::vector<oot3d::ui::UiPrimitive> output;
    model.SetModal(notice);
    AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output);
    Expect(FindRegion(model, HitKind::ModalConfirm) != nullptr && FindRegion(model, HitKind::ModalCancel) == nullptr,
           "a notice draws one button");
}

// The first-play help card: the menu open with the card alone (no window,
// tabs or rows behind it, the game held), dismissed by its button, Back or
// the menu key, which closes the menu and runs the dismissal once.
void TestHelpCard() {
    Store store;
    Model model(BuildDocument(store));
    int remembered = 0;
    HelpContent content;
    content.SaveDataPath = "D:/Games/TriAevum/data/savedata";
    const auto key = [](const char* label) { return GlyphFrameText(GlyphFrame::Key, label); };
    const auto lines = HelpCardLines(content);
    Expect(lines.size() == 5 && lines[0] == key("Esc") + ": game menu." && lines[1] == key("F1") + ": all settings." &&
               lines[2] == key("F5") + " / " + key("F8") + ": save / load a state." &&
               lines[4] == "Your saves: D:/Games/TriAevum/data/savedata",
           "the keyboard card names the menu, the settings, the state keys and the saves");
    HelpContent touch;
    touch.Prompts = PromptSet::Touch;
    const auto home = GlyphIconText(GlyphIcon::Home);
    const auto touchLines = HelpCardLines(touch);
    Expect(touchLines.size() == 3 && touchLines[0] == "Tap " + home + ": game menu." &&
               touchLines[1] == "Hold " + home + ": all settings.",
           "the touch card names HOME, without an empty saves line");
    HelpContent pad;
    pad.Prompts = PromptSet::PlayStation;
    const auto stick = [](const char* label) { return GlyphFrameText(GlyphFrame::Stick, label); };
    const auto padLines = HelpCardLines(pad);
    Expect(padLines.size() == 4 && padLines[0] == stick("R3") + ": game menu." &&
               padLines[1] == stick("L3") + " + " + stick("R3") + ": all settings." &&
               padLines[2].find("Game tab") != std::string::npos,
           "the pad card names its stick clicks and where the save states are");
    model.OpenCard(HelpCard(content, [&] { ++remembered; }));
    Expect(model.IsOpen() && model.CardOnly() && model.FocusPane() == Pane::Modal, "the card opens alone");
    std::vector<oot3d::ui::UiPrimitive> output;
    AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output);
    Expect(FindRegion(model, HitKind::Tab) == nullptr && FindRegion(model, HitKind::Row) == nullptr &&
               FindRegion(model, HitKind::Close) == nullptr && FindRegion(model, HitKind::ModalConfirm) != nullptr,
           "the card draws no menu behind it");
    model.Update(Press(&Input::Confirm), 0.016);
    Expect(remembered == 0 && model.IsOpen(), "the press held when the card appeared does not dismiss it");
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Confirm);
    Expect(remembered == 1 && !model.IsOpen() && !model.CardOnly(), "OK dismisses the card and closes the menu");
    model.OpenCard(HelpCard(content, [&] { ++remembered; }));
    model.DismissCard();
    Expect(remembered == 2 && !model.IsOpen(), "the menu key dismisses the card the same way");
    // Re-opened from the menu, the card is a notice over the menu.
    model.Open();
    model.SetModal(HelpCard(content, nullptr));
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Confirm);
    Expect(model.IsOpen() && !model.CurrentModal(), "the card over the menu returns to the menu");
    // A click on its button dismisses it too.
    model.Close();
    model.OpenCard(HelpCard(content, [&] { ++remembered; }));
    output.clear();
    AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output);
    const auto* ok = FindRegion(model, HitKind::ModalConfirm);
    Expect(ok != nullptr, "the card's button is a pointer target");
    const float okX = ok->Area.X + ok->Area.Width * 0.5F;
    const float okY = ok->Area.Y + ok->Area.Height * 0.5F;
    model.Update(Input{}, Pointer{true, okX, okY, false, 0.0F}, 0.016);
    model.Update(Input{}, Pointer{true, okX, okY, true, 0.0F}, 0.016);
    Expect(remembered == 3 && !model.IsOpen(), "a click on OK dismisses the card");
}

// Resume is the footer's persistent action on every tab.
void TestResumeFooter() {
    Store store;
    Model model(BuildDocument(store));
    model.Open();
    for (std::size_t tab = 0; tab < model.Doc().Tabs.size(); ++tab) {
        model.SelectTab(tab);
        std::vector<oot3d::ui::UiPrimitive> output;
        AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output);
        Expect(FindRegion(model, HitKind::Close) != nullptr, "every tab has the Resume button");
    }
    const auto area = FindRegion(model, HitKind::Close)->Area;
    model.Update(Input{}, Pointer{true, area.X + 1.0F, area.Y + 1.0F, false, 0.0F}, 0.016);
    model.Update(Input{}, Pointer{true, area.X + 1.0F, area.Y + 1.0F, true, 0.0F}, 0.016);
    Expect(!model.IsOpen(), "Resume closes the menu");
}

// The Help & folders tab, the quit question and a failed save's retry.
void TestHelpTabAndSession() {
    int cards = 0, saves = 0, logs = 0;
    HelpTabActions actions;
    actions.Content.SaveDataPath = "D:/Games/TriAevum/data/savedata";
    actions.ShowHelpCard = [&] { ++cards; return WriteResult{"Help card shown", false}; };
    actions.OpenSavesFolder = [&] { ++saves; return WriteResult{"Opened the saves folder", false}; };
    actions.OpenLogsFolder = [&] { ++logs; return WriteResult{"Opened the logs folder", false}; };
    Document document;
    document.Tabs.push_back(BuildHelpTab(actions));
    const auto& rows = document.Tabs[0].Rows;
    const auto find = [&](const char* key) {
        return std::find_if(rows.begin(), rows.end(), [&](const Row& row) { return row.Key == key; });
    };
    Expect(document.Tabs[0].Title == "Help & folders", "the tab's title");
    Expect(find("Game menu") != rows.end() &&
               find("Game menu")->Text == PromptText(PromptSet::Keyboard, PromptAction::Menu) &&
               find("All settings (F1)") != rows.end() && find("Save / load a state") != rows.end(),
           "the keyboard controls summary");
    Expect(find("Open saves folder") != rows.end() &&
               find("Open saves folder")->Help.find("D:/Games/TriAevum/data/savedata") != std::string::npos,
           "the saves row names the resolved folder");
    Model model(std::move(document));
    model.Open();
    Expect(model.FocusedRow()->Key == std::string("Show the help card"), "the first action is the help card");
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Confirm);
    Tap(model, &Input::Down);
    Tap(model, &Input::Confirm);
    Tap(model, &Input::Down);
    Tap(model, &Input::Confirm);
    Expect(cards == 1 && saves == 1 && logs == 1 && model.Status() == "Opened the logs folder",
           "the help card and folder rows ask the host");
    // Without a file browser the folder rows are left out and the installer
    // route is named instead; with the installer hook they are actions.
    HelpTabActions android;
    android.Content.Prompts = PromptSet::Touch;
    android.InstallerRoute = "Close the game, then the TriAevum installer: Saves, Diagnostics";
    const auto androidTab = BuildHelpTab(android);
    Expect(std::none_of(androidTab.Rows.begin(), androidTab.Rows.end(),
                        [](const Row& row) { return row.Key == "Open saves folder" || row.Key == "Open logs folder"; }) &&
               std::any_of(androidTab.Rows.begin(), androidTab.Rows.end(),
                           [](const Row& row) { return row.Key == "Saves and diagnostics" && row.Kind == RowKind::Info; }) &&
               std::any_of(androidTab.Rows.begin(), androidTab.Rows.end(),
                           [](const Row& row) {
                               return row.Key == "Game menu" && row.Text == "Tap " + GlyphIconText(GlyphIcon::Home);
                           }),
           "the touch tab names HOME and the installer route");
    HelpTabActions xbox;
    xbox.Content.Prompts = PromptSet::Xbox;
    const auto xboxTab = BuildHelpTab(xbox);
    Expect(std::any_of(xboxTab.Rows.begin(), xboxTab.Rows.end(),
                       [](const Row& row) {
                           return row.Key == "Switch tabs" && row.Text == GlyphFrameText(GlyphFrame::Solid, "LB") + " / " +
                                                                              GlyphFrameText(GlyphFrame::Solid, "RB");
                       }) &&
               std::none_of(xboxTab.Rows.begin(), xboxTab.Rows.end(),
                            [](const Row& row) { return row.Key == "Fullscreen"; }),
           "a pad's summary names its buttons and leaves the keyboard keys out");
    android.ExportSavesInInstaller = [] { return WriteResult{}; };
    const auto hooked = BuildHelpTab(android);
    Expect(std::any_of(hooked.Rows.begin(), hooked.Rows.end(),
                       [](const Row& row) { return row.Key == "Export saves" && row.Kind == RowKind::Action; }),
           "the installer hook becomes an action row");

    // Quit asks first; Back keeps the game.
    int quits = 0;
    Store store;
    Model menu(BuildDocument(store));
    menu.Open();
    menu.SetModal(QuitConfirmation([&] { ++quits; }));
    menu.Update(Input{}, 0.016);
    Expect(menu.CurrentModal()->Title == "Quit the game?" && menu.CurrentModal()->ConfirmLabel == "Quit",
           "the quit question");
    Expect(menu.CurrentModal()->Lines[0].find("not saved by quitting") != std::string::npos,
           "the question says the game's progress is not saved by quitting");
    Tap(menu, &Input::Back);
    Expect(quits == 0 && menu.IsOpen(), "back keeps the game");
    menu.SetModal(QuitConfirmation([&] { ++quits; }));
    menu.Update(Input{}, 0.016);
    Tap(menu, &Input::Confirm);
    Expect(quits == 1, "Quit quits");
    // A failed save offers Retry through the owner.
    int retries = 0;
    menu.SetModal(SaveFailure("disk full", [&] { ++retries; }));
    menu.Update(Input{}, 0.016);
    Expect(menu.CurrentModal()->ConfirmLabel == "Retry", "a failed save offers Retry");
    Tap(menu, &Input::Confirm);
    Expect(retries == 1, "Retry asks the owner again");
    Expect(std::string(kStatusSaved) == "Saved" && std::string(kStatusSessionOnly) == "Session only" &&
               std::string(kStatusPreview) == "Preview: press Save in F1 to keep" &&
               std::string(kStatusSaveFailed) == "Could not save: Retry",
           "the status vocabulary");
}

std::size_t CountTexture(const std::vector<oot3d::ui::UiPrimitive>& output, std::string_view texture) {
    return static_cast<std::size_t>(std::count_if(output.begin(), output.end(), [&](const auto& primitive) {
        return primitive.texture.semantic_name == texture;
    }));
}

// The glyph atlas and the prompts inside text: measured, wrapped, elided and
// drawn with the letters.
void TestGlyphText() {
    // The menu draws from the 64-texel button glyph atlas (its cells are
    // checked in TestButtonGlyphs); the PlayStation cross is a black mark
    // on a white disc.
    const auto& atlas = ButtonGlyphAtlasForCell(kGlyphCell);
    Expect(ButtonGlyphTextureName(kGlyphCell) == kGlyphTexture, "the menu's glyph texture");
    const auto at = [&](int x, int y) {
        return &atlas.Rgba8[(static_cast<std::size_t>(y) * atlas.Width + static_cast<std::size_t>(x)) * 4U];
    };
    Expect(at(32, 32)[3] == 255 && at(32, 32)[0] == 0 && at(32, 14)[0] == 255 && at(32, 14)[3] == 255,
           "the cross is black on its white disc");

    // Codepoints.
    const auto codes = DecodeUtf8(GlyphFrameText(GlyphFrame::Solid, "LB"));
    Expect(codes.size() == 4 && GlyphFrameOpenFromCodepoint(codes[0]) == GlyphFrame::Solid && codes[1] == 'L' &&
               IsGlyphFrameClose(codes[3]),
           "a frame is its opening, its label and its closing");
    Expect(GlyphIconFromCodepoint(DecodeUtf8(GlyphIconText(GlyphIcon::Tap))[0]) == GlyphIcon::Tap, "an icon codepoint");
    Expect(!GlyphIconFromCodepoint('A') && !GlyphFrameOpenFromCodepoint(kGlyphFrameBase + 1U), "plain letters");

    // Measured with the text; a pill grows with its label, a disc does not
    // shrink below its height.
    const float size = 4.0F;
    const float a = MeasureText(FontStyle::Body, size, GlyphFrameText(GlyphFrame::Solid, "A"));
    const float lb = MeasureText(FontStyle::Body, size, GlyphFrameText(GlyphFrame::Solid, "LB"));
    const float backspace = MeasureText(FontStyle::Body, size, GlyphFrameText(GlyphFrame::Key, "Ctrl+Alt+Del"));
    const float icon = MeasureText(FontStyle::Body, size, GlyphIconText(GlyphIcon::PsCross));
    Expect(a > 0.9F * size && icon > 0.9F * size && lb > a && backspace > 2.0F * lb, "prompt widths");
    const std::string sentence = GlyphFrameText(GlyphFrame::Key, "Esc") + ": game menu.";
    Expect(std::abs(MeasureText(FontStyle::Body, size, sentence) -
                    (MeasureText(FontStyle::Body, size, GlyphFrameText(GlyphFrame::Key, "Esc")) +
                     MeasureText(FontStyle::Body, size, ": game menu."))) < 0.001F,
           "a prompt adds its width to the line");

    // Drawn: the frame's three slices from the glyph atlas, then its label
    // in the heading font, dark on a filled button.
    std::vector<oot3d::ui::UiPrimitive> output;
    TextStyle style{FontStyle::Body, size, {0.8F, 0.7F, 0.2F, 1.0F}, TextAlign::Left, false, 5};
    const float drawn = AppendText(output, style, 10.0F, 50.0F, GlyphFrameText(GlyphFrame::Solid, "LB"));
    Expect(std::abs(drawn - lb) < 0.001F, "drawn width is the measured width");
    Expect(CountTexture(output, kGlyphTexture) == 3 && CountTexture(output, kHeadingFontTexture) == 2, "slices and label");
    Expect(!output[0].texture_alpha_only && output[0].color.red == 1.0F && output[0].color.blue == 1.0F &&
               output.back().color.red == 0.0F && output.back().color.green == 0.0F && output.back().layer == 5,
           "the button is white whatever the text colour, its label black");
    for (const auto& primitive : output) {
        Expect(primitive.uv.x >= 0.0F && primitive.uv.y >= 0.0F && primitive.uv.x + primitive.uv.width <= 1.0F &&
                   primitive.uv.y + primitive.uv.height <= 1.0F && primitive.destination.x >= 10.0F &&
                   primitive.destination.x + primitive.destination.width <= 10.0F + lb + 0.001F,
               "inside the atlas and inside its advance");
    }
    // The label is centred on the button.
    const auto& slice = output[0];
    const float buttonCentre = (output[0].destination.x + output[2].destination.x + output[2].destination.width) * 0.5F;
    const float labelLeft = output[3].destination.x;
    const float labelRight = output.back().destination.x + output.back().destination.width;
    Expect(std::abs((labelLeft + labelRight) * 0.5F - buttonCentre) < 0.15F * size &&
               output[3].destination.y > slice.destination.y &&
               output[3].destination.y + output[3].destination.height < slice.destination.y + slice.destination.height,
           "the label sits inside the button");
    output.clear();
    AppendText(output, style, 10.0F, 50.0F, GlyphIconText(GlyphIcon::DpadHorizontal));
    Expect(output.size() == 1 && output[0].texture.semantic_name == kGlyphTexture, "an icon is one quad");

    // Wrapping keeps a prompt on its line; eliding never cuts into one.
    const std::string longLine = sentence + " " + GlyphFrameText(GlyphFrame::Key, "F1") + ": all settings, and more words";
    for (const auto& line : WrapText(FontStyle::Body, size, longLine, 40.0F)) {
        Expect(MeasureText(FontStyle::Body, size, line) <= 40.0F + 0.001F, "wrapped prompt lines fit");
    }
    const auto elided = ElideText(FontStyle::Body, size, GlyphFrameText(GlyphFrame::Key, "Ctrl+Alt+Del") + " back", backspace * 0.8F);
    const auto elidedCodes = DecodeUtf8(elided);
    Expect(std::none_of(elidedCodes.begin(), elidedCodes.end(),
                        [](std::uint32_t code) { return GlyphFrameOpenFromCodepoint(code).has_value(); }),
           "a prompt that does not fit is left out whole");

    MenuFontTextureProvider provider(nullptr);
    oot3d::ui::UiTexturePixels pixels;
    std::string error;
    Expect(MenuFontTextureProvider::IsMenuTexture(kGlyphTexture), "the glyph atlas is a menu texture");
    Expect(provider.Resolve({0, 0, std::string(kGlyphTexture)}, pixels, &error), error.c_str());
    Expect(pixels.width == atlas.Width && pixels.height == atlas.Height && pixels.rgba8 == atlas.Rgba8,
           "the provider serves the button glyph atlas");
}

// Each prompt family's footer: the controls a pad, the keyboard or touch has.
void TestPromptSets() {
    const auto solid = [](const char* label) { return GlyphFrameText(GlyphFrame::Solid, label); };
    const auto xbox = HintsFor(PromptSet::Xbox);
    Expect(xbox.Confirm == solid("A") && xbox.Back == solid("B") && xbox.TabPrevious == solid("LB") &&
               xbox.TabNext == solid("RB") && xbox.Close == GlyphFrameText(GlyphFrame::Stick, "RS") &&
               xbox.Change == GlyphIconText(GlyphIcon::DpadHorizontal),
           "Xbox prompts");
    const auto playStation = HintsFor(PromptSet::PlayStation);
    Expect(playStation.Confirm == GlyphIconText(GlyphIcon::PsCross) &&
               playStation.Back == GlyphIconText(GlyphIcon::PsCircle) && playStation.TabPrevious == solid("L1") &&
               playStation.Close == GlyphFrameText(GlyphFrame::Stick, "R3"),
           "PlayStation prompts: cross confirms, circle goes back");
    const auto nintendo = HintsFor(PromptSet::Nintendo);
    Expect(nintendo.Confirm == solid("A") && nintendo.TabPrevious == solid("L") && nintendo.TabNext == solid("R"),
           "Nintendo prompts follow the printed labels");
    const auto deck = HintsFor(PromptSet::SteamDeck);
    Expect(deck.Confirm == solid("A") && deck.TabPrevious == solid("L1") && deck.TabNext == solid("R1"),
           "Steam Deck prompts");
    const auto generic = HintsFor(PromptSet::Generic);
    Expect(generic.Confirm == solid("A") && generic.Back == solid("B"),
           "generic prompts name the positions with SDL's letters");
    const auto keyboard = HintsFor(PromptSet::Keyboard);
    Expect(keyboard.Confirm == GlyphIconText(GlyphIcon::KeyEnter) &&
               keyboard.Back == GlyphIconText(GlyphIcon::KeyBackspace) &&
               keyboard.Change == GlyphIconText(GlyphIcon::KeyLeft) + " " + GlyphIconText(GlyphIcon::KeyRight) &&
               keyboard.TabPrevious == GlyphFrameText(GlyphFrame::Key, "Q") &&
               keyboard.Close == GlyphFrameText(GlyphFrame::Key, "Esc"),
           "keyboard prompts");
    const auto touch = HintsFor(PromptSet::Touch);
    Expect(touch.Confirm == GlyphIconText(GlyphIcon::Tap) && touch.Back.empty() && touch.TabPrevious.empty() &&
               touch.TabNext.empty() && touch.Change.empty() && touch.Close == GlyphIconText(GlyphIcon::Home),
           "touch: a tap does it, HOME closes");
    Expect(PromptText(PromptSet::Xbox, PromptAction::Fullscreen).empty() &&
               !PromptText(PromptSet::Keyboard, PromptAction::Fullscreen).empty() && IsPadPromptSet(PromptSet::Generic) &&
               !IsPadPromptSet(PromptSet::Touch),
           "keyboard-only keys are not pad prompts");

    // Every family's footer fits the window, draws its glyphs and keeps the
    // Resume button.
    for (int set = 0; set <= static_cast<int>(PromptSet::Touch); ++set) {
        Store store;
        Model model(BuildDocument(store));
        model.Open();
        std::vector<oot3d::ui::UiPrimitive> output;
        AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output, {}, HintsFor(static_cast<PromptSet>(set)));
        Expect(CountTexture(output, kGlyphTexture) >= 2, "the footer draws the family's glyphs");
        const auto* close = FindRegion(model, HitKind::Close);
        Expect(close != nullptr, "Resume stays");
        for (const auto& primitive : output) {
            if (primitive.texture.semantic_name != kGlyphTexture) continue;
            Expect(primitive.destination.x >= 0.0F && primitive.destination.x + primitive.destination.width <= 1280.0F / 3.0F,
                   "glyphs inside the canvas");
        }
        // The hints on the left end before the Resume button.
        float hintsRight = 0.0F;
        for (const auto& primitive : output) {
            if (primitive.texture.semantic_name.empty() || primitive.destination.y < close->Area.Y ||
                primitive.destination.x >= close->Area.X)
                continue;
            hintsRight = std::max(hintsRight, primitive.destination.x + primitive.destination.width);
        }
        Expect(hintsRight < close->Area.X, "the hints do not run under the Resume button");
    }
}

// The device changes while the menu is open: the Help tab is rebuilt for the
// new family and the focus stays on its row; an open help card follows.
void TestPromptsMidMenu() {
    int cards = 0;
    HelpTabActions actions;
    actions.Content.Prompts = PromptSet::Keyboard;
    actions.ShowHelpCard = [&] { ++cards; return WriteResult{"Help card shown", false}; };
    actions.OpenLogsFolder = [] { return WriteResult{"Opened the logs folder", false}; };
    Store store;
    Document document = BuildDocument(store);
    document.Tabs.push_back(BuildHelpTab(actions));
    const std::size_t help = document.Tabs.size() - 1;
    Model model(std::move(document));
    model.Open();
    model.SelectTab(help);
    model.Update(Input{}, 0.016);
    Tap(model, &Input::Down);
    Expect(model.FocusedRow()->Key == std::string("Open logs folder"), "focus on the second action");
    model.SetStatus("Opened the logs folder", false);
    const auto summary = [&](const char* key) {
        const auto& rows = model.Doc().Tabs[help].Rows;
        const auto found = std::find_if(rows.begin(), rows.end(), [&](const Row& row) { return row.Key == key; });
        return found != rows.end() ? found->Text : std::string{};
    };
    Expect(summary("Game menu") == PromptText(PromptSet::Keyboard, PromptAction::Menu), "keyboard first");
    // Keyboard -> pad -> touch -> keyboard, as InGameMenu rebuilds the tab.
    for (const auto set : {PromptSet::PlayStation, PromptSet::Touch, PromptSet::Keyboard}) {
        actions.Content.Prompts = set;
        model.ReplaceTab(help, BuildHelpTab(actions));
        Expect(summary("Game menu").find(PromptText(set, PromptAction::Menu)) != std::string::npos,
               "the summary follows the device");
        Expect(model.TabIndex() == help && model.FocusedRow()->Key == std::string("Open logs folder") &&
                   model.Status() == "Opened the logs folder",
               "the focus and the status line stay");
        std::vector<oot3d::ui::UiPrimitive> output;
        AppendMenuPresentation(model, 1280.0F / 3.0F, 240.0F, output, {}, HintsFor(set));
        Expect(FindRegion(model, HitKind::Close) != nullptr, "the rebuilt tab draws");
    }
    Tap(model, &Input::Up);
    Tap(model, &Input::Confirm);
    Expect(cards == 1, "the rebuilt rows act");
    // A focused row the new tab lacks: the first focusable row.
    HelpTabActions without = actions;
    without.OpenLogsFolder = nullptr;
    Tap(model, &Input::Down);
    model.ReplaceTab(help, BuildHelpTab(without));
    Expect(model.FocusedRow() != nullptr && model.FocusedRow()->Key == std::string("Show the help card"),
           "a missing row gives the first one");
    // Another tab's replacement does not move the focus.
    model.ReplaceTab(0, model.Doc().Tabs[0]);
    Expect(model.TabIndex() == help, "replacing another tab keeps the current one");
    // The open help card's lines follow the device too.
    HelpContent content;
    model.SetModal(HelpCard(content, nullptr));
    Expect(IsHelpCard(*model.CurrentModal()), "the help card is recognised");
    content.Prompts = PromptSet::Xbox;
    model.CurrentModal()->Lines = HelpCardLines(content);
    Expect(model.CurrentModal()->Lines[0] == GlyphFrameText(GlyphFrame::Stick, "RS") + ": game menu.",
           "the card names the pad's button");
}

} // namespace

// Game tab > Audio (oot3d_menu_audio.h): the rows a host binds to its audio
// output policy. Volume in 5 % steps, clamped; Mute keeps the volume and makes
// the volume row unavailable; the focus mute row only when bound; a failed
// write reaches the status line.
void TestAudioRows() {
    struct AudioStore {
        int Volume = 100;
        bool Muted = false;
        bool Unfocused = false;
        bool Fail = false;
        int Writes = 0;
    } store;
    AudioRowBindings bindings;
    bindings.Volume = [&store] { return store.Volume; };
    bindings.SetVolume = [&store](int value) {
        ++store.Writes;
        if (store.Fail) return WriteResult{"Could not save: Retry", true};
        store.Volume = value;
        return WriteResult{"Saved", false};
    };
    bindings.Muted = [&store] { return store.Muted; };
    bindings.SetMuted = [&store](bool value) {
        ++store.Writes;
        store.Muted = value;
        return WriteResult{"Saved", false};
    };
    Tab without;
    AppendAudioRows(without, bindings);
    Expect(without.Rows.size() == 3U && without.Rows[0].Kind == RowKind::Heading && without.Rows[0].Key == "Audio",
           "audio heading and two rows without the focus mute binding");
    Expect(without.Rows[1].Key == "Master volume" && without.Rows[1].Kind == RowKind::Number &&
               without.Rows[2].Key == "Mute" && without.Rows[2].Kind == RowKind::Toggle,
           "audio rows");
    Tab none;
    AppendAudioRows(none, AudioRowBindings{});
    Expect(none.Rows.empty(), "no rows without an owner");

    bindings.MuteWhenUnfocused = [&store] { return store.Unfocused; };
    bindings.SetMuteWhenUnfocused = [&store](bool value) {
        store.Unfocused = value;
        return WriteResult{"Saved", false};
    };
    Document document;
    Tab game;
    game.Title = "Game";
    AppendAudioRows(game, bindings);
    Expect(game.Rows.size() == 4U && game.Rows[3].Key == "Mute when unfocused", "focus mute row when bound");
    document.Tabs.push_back(game);
    Model model(document);
    model.Open();
    model.Update(Input{}, 0.016);
    Expect(model.FocusedRow()->Key == "Master volume" && model.FocusedRow()->ValueText() == "100%",
           "volume row focused first, as a percentage");
    Expect(!model.FocusedRow()->IsModified, "100 % is the default");
    Tap(model, &Input::Right);
    Expect(store.Volume == 100, "volume clamps at 100 %");
    Tap(model, &Input::Left);
    Tap(model, &Input::Left);
    Expect(store.Volume == 90 && model.FocusedRow()->ValueText() == "90%" && model.FocusedRow()->IsModified,
           "volume steps by 5 %");
    for (int step = 0; step < 25; ++step) Tap(model, &Input::Left);
    Expect(store.Volume == 0, "volume clamps at 0 %");
    for (int step = 0; step < 10; ++step) Tap(model, &Input::Right);
    Expect(store.Volume == 50, "volume back to 50 %");
    Tap(model, &Input::Down);
    Tap(model, &Input::Confirm);
    Expect(store.Muted && model.FocusedRow()->Value == 1 && model.FocusedRow()->IsModified, "mute toggles");
    Tap(model, &Input::Up);
    Expect(!model.FocusedRow()->Enabled, "volume row unavailable while muted");
    const int writes = store.Writes;
    Tap(model, &Input::Right);
    Expect(store.Volume == 50 && store.Writes == writes, "muted volume row ignores edits");
    Tap(model, &Input::Down);
    Tap(model, &Input::Confirm);
    Expect(!store.Muted, "unmute");
    Tap(model, &Input::Down);
    Tap(model, &Input::Confirm);
    Expect(store.Unfocused, "focus mute toggles");
    Tap(model, &Input::Down);
    store.Fail = true;
    Tap(model, &Input::Right);
    Expect(model.StatusIsError() && model.Status() == "Could not save: Retry" && store.Volume == 50,
           "a failed volume write reaches the status line");
}

// Graphics tab > Textures (oot3d_menu_textures.h, UX plan WP-07): "Custom
// textures" toggles the owner's LoadCustomTextures, asks the owner to keep
// the graphics preset (presets preserve texture packs), is modified against
// the value the menu opened with, and reports a failed write.
void TestTextureRows() {
    struct GraphicsOwner {
        bool LoadCustomTextures = false;
        bool Baseline = false;
        int Preset = 2; // a built-in preset; 0 stands for Custom
        bool Fail = false;
        int Writes = 0;
    } owner;
    TextureRowBindings bindings;
    bindings.LoadCustomTextures = [&owner] { return owner.LoadCustomTextures; };
    bindings.BaselineLoadCustomTextures = [&owner] { return owner.Baseline; };
    bindings.SetLoadCustomTextures = [&owner](bool value, bool keepPreset) {
        ++owner.Writes;
        if (owner.Fail) return WriteResult{"Could not save: Retry", true};
        owner.LoadCustomTextures = value;
        if (!keepPreset) owner.Preset = 0;
        return WriteResult{"Saved", false};
    };
    Tab none;
    AppendTextureRows(none, TextureRowBindings{});
    Expect(none.Rows.empty(), "no texture rows without an owner");

    Document document;
    Tab graphics;
    graphics.Title = "Graphics";
    AppendTextureRows(graphics, bindings);
    Expect(graphics.Rows.size() == 2U && graphics.Rows[0].Kind == RowKind::Heading &&
               graphics.Rows[0].Key == "Textures" && graphics.Rows[1].Key == "Custom textures" &&
               graphics.Rows[1].Kind == RowKind::Toggle && !graphics.Rows[1].Help.empty(),
           "Textures heading and the Custom textures toggle");
    document.Tabs.push_back(graphics);
    Model model(document);
    model.Open();
    model.Update(Input{}, 0.016);
    Expect(model.FocusedRow()->Key == "Custom textures" && model.FocusedRow()->Value == 0 &&
               !model.FocusedRow()->IsModified,
           "Custom textures focused, off and unmodified");
    Tap(model, &Input::Confirm);
    Expect(owner.LoadCustomTextures && owner.Writes == 1 && model.FocusedRow()->Value == 1,
           "Custom textures turns the owner's LoadCustomTextures on");
    Expect(owner.Preset == 2, "Custom textures keeps the graphics preset");
    Expect(model.FocusedRow()->IsModified, "Custom textures differs from the value the menu opened with");
    Tap(model, &Input::Confirm);
    Expect(!owner.LoadCustomTextures && owner.Preset == 2 && !model.FocusedRow()->IsModified,
           "Custom textures turns the pack off again");
    owner.Fail = true;
    Tap(model, &Input::Confirm);
    Expect(model.StatusIsError() && model.Status() == "Could not save: Retry" && !owner.LoadCustomTextures,
           "a failed Custom textures write reaches the status line");
}

// The HUD's button glyphs: each family's look for a pad button, keycaps for
// keys, a mouse for its buttons, nothing for an unbound device or touch, and
// the three atlas sizes the provider serves.
void TestButtonGlyphs() {
    using ThreeDsRecomp::Input::GamepadButton;
    using ThreeDsRecomp::Input::HostBinding;
    using ThreeDsRecomp::Input::KeyboardKey;
    using ThreeDsRecomp::Input::MouseButton;
    using ThreeDsRecomp::Input::PromptSet;
    const auto framed = [](ButtonGlyphFrame frame, const char* label) { return ButtonGlyph::Framed(frame, label); };
    const auto icon = [](ButtonGlyphIcon value) { return ButtonGlyph::Symbol(value); };
    const auto button = [&](const char* label) { return framed(ButtonGlyphFrame::Button, label); };

    struct Case {
        PromptSet Set;
        GamepadButton Pad;
        ButtonGlyph Expected;
    };
    const std::vector<Case> pads{
        {PromptSet::Xbox, GamepadButton::A, button("A")},
        {PromptSet::Xbox, GamepadButton::Y, button("Y")},
        {PromptSet::Xbox, GamepadButton::LeftShoulder, button("LB")},
        {PromptSet::Xbox, GamepadButton::RightTrigger, button("RT")},
        {PromptSet::Xbox, GamepadButton::LeftStick, framed(ButtonGlyphFrame::Stick, "LS")},
        {PromptSet::PlayStation, GamepadButton::A, icon(ButtonGlyphIcon::PsCross)},
        {PromptSet::PlayStation, GamepadButton::B, icon(ButtonGlyphIcon::PsCircle)},
        {PromptSet::PlayStation, GamepadButton::X, icon(ButtonGlyphIcon::PsSquare)},
        {PromptSet::PlayStation, GamepadButton::Y, icon(ButtonGlyphIcon::PsTriangle)},
        {PromptSet::PlayStation, GamepadButton::RightShoulder, button("R1")},
        {PromptSet::PlayStation, GamepadButton::LeftTrigger, button("L2")},
        {PromptSet::PlayStation, GamepadButton::RightStick, framed(ButtonGlyphFrame::Stick, "R3")},
        // SDL maps a Nintendo pad by its printed labels.
        {PromptSet::Nintendo, GamepadButton::B, button("B")},
        {PromptSet::Nintendo, GamepadButton::LeftShoulder, button("L")},
        {PromptSet::Nintendo, GamepadButton::RightTrigger, button("ZR")},
        {PromptSet::SteamDeck, GamepadButton::X, button("X")},
        {PromptSet::SteamDeck, GamepadButton::LeftShoulder, button("L1")},
        {PromptSet::SteamDeck, GamepadButton::RightTrigger, button("R2")},
        {PromptSet::Generic, GamepadButton::A, button("A")},
        {PromptSet::Generic, GamepadButton::Y, button("Y")},
        {PromptSet::SteamDeck, GamepadButton::B, button("B")},
        {PromptSet::Generic, GamepadButton::RightShoulder, button("R")},
        {PromptSet::Generic, GamepadButton::LeftTrigger, button("L2")},
        {PromptSet::Xbox, GamepadButton::DpadLeft, icon(ButtonGlyphIcon::DpadLeft)},
        {PromptSet::PlayStation, GamepadButton::DpadUp, icon(ButtonGlyphIcon::DpadUp)},
    };
    for (const auto& item : pads) {
        HostBinding binding;
        binding.Gamepad = item.Pad;
        binding.KeyboardPrimary = KeyboardKey::Space;
        const auto glyph = ResolveButtonGlyph(item.Set, binding);
        Expect(glyph.has_value() && *glyph == item.Expected, "a pad binding's glyph in its family");
    }

    HostBinding keys;
    keys.KeyboardPrimary = KeyboardKey::Space;
    keys.KeyboardSecondary = KeyboardKey::Z;
    keys.Mouse = MouseButton::Left;
    keys.Gamepad = GamepadButton::A;
    Expect(ResolveButtonGlyph(PromptSet::Keyboard, keys) == icon(ButtonGlyphIcon::KeySpace),
           "the keyboard shows the first key, as its symbol");
    keys.KeyboardPrimary = KeyboardKey::None;
    Expect(ResolveButtonGlyph(PromptSet::Keyboard, keys) == framed(ButtonGlyphFrame::Key, "Z"),
           "the keyboard shows the second key without a first");
    keys.KeyboardSecondary = KeyboardKey::None;
    Expect(ResolveButtonGlyph(PromptSet::Keyboard, keys) == icon(ButtonGlyphIcon::MouseLeft),
           "the keyboard family shows the mouse button without keys");
    keys.Mouse = MouseButton::Back;
    Expect(ResolveButtonGlyph(PromptSet::Keyboard, keys) == icon(ButtonGlyphIcon::MouseBack), "mouse back");
    keys.Mouse = MouseButton::None;
    Expect(!ResolveButtonGlyph(PromptSet::Keyboard, keys).has_value(), "no key: the label stays");
    HostBinding padless;
    padless.KeyboardPrimary = KeyboardKey::C;
    for (const auto set : {PromptSet::Xbox, PromptSet::PlayStation, PromptSet::Nintendo, PromptSet::SteamDeck,
                           PromptSet::Generic})
        Expect(!ResolveButtonGlyph(set, padless).has_value(), "no pad button: the label stays");
    Expect(!ResolveButtonGlyph(PromptSet::Touch, keys).has_value() &&
               !ResolveButtonGlyph(PromptSet::Touch, padless).has_value(),
           "touch keeps the labels");

    struct KeyCase {
        KeyboardKey Key;
        const char* Label;
    };
    for (const auto& item : {KeyCase{KeyboardKey::Q, "Q"}, KeyCase{KeyboardKey::Num1, "1"},
                             KeyCase{KeyboardKey::Escape, "Esc"}, KeyCase{KeyboardKey::Numpad8, "Num8"},
                             KeyCase{KeyboardKey::Comma, ","}, KeyCase{KeyboardKey::Control, "Ctrl"}})
        Expect(KeyboardKeyGlyph(item.Key) == framed(ButtonGlyphFrame::Key, item.Label), "a key's printed name");
    struct SymbolCase {
        KeyboardKey Key;
        ButtonGlyphIcon Icon;
    };
    // Keys with a conventional symbol draw it, never their name.
    for (const auto& item : {SymbolCase{KeyboardKey::Space, ButtonGlyphIcon::KeySpace},
                             SymbolCase{KeyboardKey::Shift, ButtonGlyphIcon::KeyShift},
                             SymbolCase{KeyboardKey::RightShift, ButtonGlyphIcon::KeyShift},
                             SymbolCase{KeyboardKey::Enter, ButtonGlyphIcon::KeyEnter},
                             SymbolCase{KeyboardKey::Backspace, ButtonGlyphIcon::KeyBackspace},
                             SymbolCase{KeyboardKey::Tab, ButtonGlyphIcon::KeyTab},
                             SymbolCase{KeyboardKey::ArrowUp, ButtonGlyphIcon::KeyUp},
                             SymbolCase{KeyboardKey::ArrowDown, ButtonGlyphIcon::KeyDown},
                             SymbolCase{KeyboardKey::ArrowLeft, ButtonGlyphIcon::KeyLeft},
                             SymbolCase{KeyboardKey::ArrowRight, ButtonGlyphIcon::KeyRight}})
        Expect(KeyboardKeyGlyph(item.Key) == icon(item.Icon), "a key's conventional symbol");
    // Every bindable key has a symbol or a short label in the heading font.
    for (int value = 0; value <= 336; ++value) {
        const auto key = static_cast<KeyboardKey>(value);
        const std::string name = ThreeDsRecomp::Input::KeyboardKeyName(key);
        if (key == KeyboardKey::None || name == "unknown") continue;
        const auto glyph = KeyboardKeyGlyph(key);
        Expect(!glyph.Labelled || (glyph.Label != "?" && glyph.Label.size() <= 4U &&
                                   MeasureText(FontStyle::Heading, 10.0F, glyph.Label) > 0.0F),
               "a key without a compact label");
    }

    // A glyph is at least as wide as high; a one-letter button is a disc.
    Expect(ButtonGlyphWidth(button("A"), 12.0F) == 12.0F, "one letter: a disc");
    Expect(ButtonGlyphWidth(framed(ButtonGlyphFrame::Key, "Ctrl"), 12.0F) > 12.5F, "a word widens its key");
    Expect(ButtonGlyphCellForPixels(20.0F) == 32U && ButtonGlyphCellForPixels(33.0F) == 64U &&
               ButtonGlyphCellForPixels(99.0F) == 128U && ButtonGlyphCellForPixels(400.0F) == 128U,
           "the atlas size follows the drawn size");

    const ButtonGlyphColors colors;
    std::vector<oot3d::ui::UiPrimitive> output;
    AppendButtonGlyph(output, icon(ButtonGlyphIcon::PsCross), 100.0F, 50.0F, 11.0F, 0.9F, 13U, 64U, colors);
    Expect(output.size() == 1U && output[0].texture.semantic_name == ButtonGlyphTextureName(64U) &&
               !output[0].texture_alpha_only && output[0].layer == 13U && output[0].color.alpha == 0.9F &&
               std::abs(output[0].destination.x - 94.5F) < 1e-4F && output[0].destination.width == 11.0F,
           "an icon is one quad centred on its place, with the label's alpha");
    output.clear();
    AppendButtonGlyph(output, framed(ButtonGlyphFrame::Key, "Ctrl"), 100.0F, 50.0F, 11.0F, 1.0F, 13U, 32U, colors);
    Expect(CountTexture(output, ButtonGlyphTextureName(32U)) == 3U && CountTexture(output, kHeadingFontTexture) == 4U,
           "a keycap is three slices and its label");
    output.clear();
    AppendButtonGlyph(output, button("A"), 100.0F, 50.0F, 11.0F, 0.0F, 13U, 32U, colors);
    Expect(output.empty(), "a hidden label draws nothing");

    // The atlases: built once, every cell holds a shape with clear borders,
    // dark edges and a light body; served by the menu's provider.
    for (const std::uint32_t cell : {32U, 64U, 128U}) {
        const auto& atlas = ButtonGlyphAtlasForCell(cell);
        Expect(&ButtonGlyphAtlasForCell(cell) == &atlas && atlas.Cell == cell && atlas.Width == 8U * cell &&
                   atlas.Height == static_cast<std::uint32_t>(kButtonGlyphIconRows + 1) * cell && atlas.Rgba8.size() == static_cast<std::size_t>(atlas.Width) * atlas.Height * 4U,
               "button glyph atlas size");
        const auto texel = [&](std::uint32_t x, std::uint32_t y) { return &atlas.Rgba8[(y * atlas.Width + x) * 4U]; };
        const auto inspect = [&](std::uint32_t left, std::uint32_t top, std::uint32_t width) {
            std::size_t dark = 0, light = 0, border = 0;
            for (std::uint32_t y = 0; y < cell; ++y)
                for (std::uint32_t x = 0; x < width; ++x) {
                    const auto* value = texel(left + x, top + y);
                    if (value[3] > 200 && value[0] < 40) ++dark;
                    if (value[3] > 200 && value[0] > 215) ++light;
                    if (x == 0 || y == 0 || x == width - 1 || y == cell - 1) border += value[3];
                }
            Expect(dark > cell / 2U && light > cell && border == 0,
                   "a glyph cell with an edge, a body and a clear border");
            // Black and white only: every texel is a grey (no tint).
            for (std::uint32_t y = 0; y < cell; ++y)
                for (std::uint32_t x = 0; x < width; ++x) {
                    const auto* value = texel(left + x, top + y);
                    Expect(value[0] == value[1] && value[1] == value[2], "a tinted glyph texel");
                }
        };
        for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(ButtonGlyphIcon::Count); ++index)
            inspect((index % 8U) * cell, (index / 8U) * cell, cell);
        for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(ButtonGlyphFrame::Count); ++index)
            inspect(index * 2U * cell, static_cast<std::uint32_t>(kButtonGlyphIconRows) * cell, 2U * cell);
        MenuFontTextureProvider provider(nullptr);
        oot3d::ui::UiTexturePixels pixels;
        std::string error;
        Expect(MenuFontTextureProvider::IsMenuTexture(ButtonGlyphTextureName(cell)) &&
                   provider.Resolve({0, 0, ButtonGlyphTextureName(cell)}, pixels, &error) && pixels.rgba8 == atlas.Rgba8,
               "the provider serves the button glyph atlas");
    }
    Expect(!ButtonGlyphCellFromTextureName("triaevum/button_glyphs/48").has_value() &&
               !ButtonGlyphCellFromTextureName("triaevum/menu/glyphs").has_value(),
           "only the three atlas sizes are button glyph textures");
}

// `--dump-button-glyphs DIR`: writes each button glyph atlas as a PAM image
// (DIR/button_glyphs_<cell>.pam) for reviewing the art.
int DumpButtonGlyphs(const std::string& directory) {
    for (const std::uint32_t cell : {32U, 64U, 128U}) {
        const auto& atlas = ButtonGlyphAtlasForCell(cell);
        const auto path = directory + "/button_glyphs_" + std::to_string(cell) + ".pam";
        std::ofstream stream(path, std::ios::binary);
        stream << "P7\nWIDTH " << atlas.Width << "\nHEIGHT " << atlas.Height
               << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
        stream.write(reinterpret_cast<const char*>(atlas.Rgba8.data()), static_cast<std::streamsize>(atlas.Rgba8.size()));
        if (!stream) {
            std::cerr << "cannot write " << path << '\n';
            return EXIT_FAILURE;
        }
        std::cout << path << '\n';
    }
    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--dump-button-glyphs") return DumpButtonGlyphs(argv[2]);
    try {
        TestAudioRows();
        TestTextureRows();
        TestModelNavigation();
        TestModal();
        TestPointer();
        TestFonts();
        TestPresentation();
        TestSingleButtonModal();
        TestHelpCard();
        TestResumeFooter();
        TestHelpTabAndSession();
        TestGlyphText();
        TestPromptSets();
        TestPromptsMidMenu();
        TestButtonGlyphs();
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_menu_tests: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "oot3d_menu_tests: ok\n";
    return EXIT_SUCCESS;
}
