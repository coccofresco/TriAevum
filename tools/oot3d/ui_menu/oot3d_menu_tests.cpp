// The in-game menu apart from the game: model navigation and edits, the baked
// fonts, and the presentation's primitives.
#include "oot3d_menu_font.h"
#include "oot3d_menu_font_atlas_data.h"
#include "oot3d_menu_model.h"
#include "oot3d_menu_presentation.h"

#include <algorithm>
#include <cstdlib>
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

} // namespace

int main() {
    try {
        TestModelNavigation();
        TestModal();
        TestPointer();
        TestFonts();
        TestPresentation();
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_menu_tests: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "oot3d_menu_tests: ok\n";
    return EXIT_SUCCESS;
}
