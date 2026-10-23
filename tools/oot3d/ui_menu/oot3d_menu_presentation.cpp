#include "oot3d_menu_presentation.h"

#include "oot3d_menu_font.h"
#include "oot3d_ui/ui_contract_types.h"

#include <algorithm>
#include <cmath>

namespace Oot3dNativeGame::Menu {
namespace {

using oot3d::ui::UiColor;
using oot3d::ui::UiPrimitive;
using oot3d::ui::UiRect;

constexpr std::uint32_t kLayerBackdrop = 0;
constexpr std::uint32_t kLayerWindow = 1;
constexpr std::uint32_t kLayerBorder = 2;
constexpr std::uint32_t kLayerRow = 3;
constexpr std::uint32_t kLayerOutline = 4;
constexpr std::uint32_t kLayerText = 5;
constexpr std::uint32_t kLayerModalBackdrop = 20;
constexpr std::uint32_t kLayerModalBox = 21;
constexpr std::uint32_t kLayerModalOutline = 22;
constexpr std::uint32_t kLayerModalText = 23;

UiColor WithAlpha(UiColor color, float alpha) noexcept {
    color.alpha *= alpha;
    return color;
}

void Quad(std::vector<UiPrimitive>& output, const UiRect& rect, const UiColor& color, std::uint32_t layer) {
    if (rect.width <= 0.0F || rect.height <= 0.0F || color.alpha <= 0.0F) return;
    UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::PauseBackground;
    primitive.destination = rect;
    primitive.color = color;
    primitive.layer = layer;
    output.push_back(std::move(primitive));
}

void Outline(std::vector<UiPrimitive>& output, const UiRect& rect, float thickness, const UiColor& color,
             std::uint32_t layer) {
    Quad(output, {rect.x - thickness, rect.y - thickness, rect.width + 2.0F * thickness, thickness}, color, layer);
    Quad(output, {rect.x - thickness, rect.y + rect.height, rect.width + 2.0F * thickness, thickness}, color, layer);
    Quad(output, {rect.x - thickness, rect.y, thickness, rect.height}, color, layer);
    Quad(output, {rect.x + rect.width, rect.y, thickness, rect.height}, color, layer);
}

// Baseline that centres a line of `size` text in a box from y to y + height.
float CenteredBaseline(FontStyle, float size, float y, float height) noexcept {
    // The visual centre of mixed-case text sits near 0.36 em above the baseline.
    return y + height * 0.5F + size * 0.36F;
}

struct Frame {
    UiRect Window;
    float TabBarHeight = 0.0F;
    UiRect Content;
    UiRect LeftPane;
    UiRect RightPane;
    float RowHeight = 0.0F;
    float RowGap = 0.0F;
    float HintsY = 0.0F;
    float StatusY = 0.0F;
};

Frame ComputeFrame(float canvasWidth, float canvasHeight) noexcept {
    Frame frame;
    const float width = std::min(1088.0F * kDp, canvasWidth - 48.0F * kDp);
    const float height = std::min(768.0F * kDp, canvasHeight - 48.0F * kDp);
    frame.Window = {(canvasWidth - width) * 0.5F, (canvasHeight - height) * 0.5F, width, height};
    frame.TabBarHeight = 64.0F * kDp;
    const float padding = 32.0F * kDp;
    const float hintsHeight = 36.0F * kDp;
    const float statusHeight = 26.0F * kDp;
    frame.HintsY = frame.Window.y + frame.Window.height - hintsHeight - 8.0F * kDp;
    frame.StatusY = frame.HintsY - statusHeight;
    const float contentTop = frame.Window.y + frame.TabBarHeight + 20.0F * kDp;
    frame.Content = {frame.Window.x + padding, contentTop, frame.Window.width - 2.0F * padding,
                     frame.StatusY - 8.0F * kDp - contentTop};
    const float gap = 24.0F * kDp;
    const float leftWidth = std::floor((frame.Content.width - gap) * 0.56F);
    frame.LeftPane = {frame.Content.x, frame.Content.y, leftWidth, frame.Content.height};
    frame.RightPane = {frame.Content.x + leftWidth + gap, frame.Content.y, frame.Content.width - leftWidth - gap,
                       frame.Content.height};
    frame.RowHeight = 40.0F * kDp;
    frame.RowGap = 6.0F * kDp;
    return frame;
}

Rect ToRect(const UiRect& rect) noexcept { return {rect.x, rect.y, rect.width, rect.height}; }

bool Hovered(const Model& model, HitKind kind, std::size_t index) noexcept {
    return model.Hover().Kind == kind && model.Hover().Index == index;
}

void DrawTabBar(const Model& model, const Frame& frame, const Palette& palette, const Hints& hints,
                std::vector<UiPrimitive>& output, Layout& layout) {
    const auto& tabs = model.Doc().Tabs;
    const float size = 18.0F * kDp;
    const float padding = 24.0F * kDp;
    const float baseline = CenteredBaseline(FontStyle::Heading, size, frame.Window.y, frame.TabBarHeight);
    // Tab bar bottom rule.
    Quad(output, {frame.Window.x, frame.Window.y + frame.TabBarHeight - 1.0F * kDp, frame.Window.width, 1.0F * kDp},
         WithAlpha(palette.Border, 0.4F), kLayerBorder);
    float x = frame.Window.x + 32.0F * kDp;
    TextStyle hintStyle{FontStyle::Body, 13.0F * kDp, WithAlpha(palette.Text, 0.5F), TextAlign::Left, false,
                        kLayerText};
    // Shoulder hints at both ends of the bar.
    const std::string tabsHint = hints.Tabs;
    const auto slash = tabsHint.find('/');
    const std::string leftHint = slash == std::string::npos ? tabsHint : tabsHint.substr(0, slash);
    const std::string rightHint = slash == std::string::npos ? "" : tabsHint.substr(slash + 1);
    AppendText(output, hintStyle, x, baseline, leftHint);
    x += MeasureText(FontStyle::Body, hintStyle.Size, leftHint) + padding;
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        const bool selected = index == model.TabIndex();
        const bool hovered = !selected && Hovered(model, HitKind::Tab, index);
        const float width = MeasureText(FontStyle::Heading, size, tabs[index].Title, true);
        TextStyle style{FontStyle::Heading, size, WithAlpha(palette.Text, selected ? 1.0F : hovered ? 0.7F : 0.35F),
                        TextAlign::Left, true, kLayerText};
        AppendText(output, style, x, baseline, tabs[index].Title);
        if (selected) {
            Quad(output,
                 {x - 8.0F * kDp, frame.Window.y + frame.TabBarHeight - 4.0F * kDp, width + 16.0F * kDp, 4.0F * kDp},
                 palette.Accent, kLayerOutline);
        }
        layout.Regions.push_back(
            {{x - padding * 0.5F, frame.Window.y, width + padding, frame.TabBarHeight}, HitKind::Tab, index});
        x += width + padding;
    }
    if (!rightHint.empty()) {
        hintStyle.Align = TextAlign::Left;
        AppendText(output, hintStyle, x, baseline, rightHint);
    }
}

void DrawRows(Model& model, const Frame& frame, const Palette& palette, std::vector<UiPrimitive>& output,
              Layout& layout) {
    const auto& tabs = model.Doc().Tabs;
    if (model.TabIndex() >= tabs.size()) return;
    const auto& rows = tabs[model.TabIndex()].Rows;
    const float stride = frame.RowHeight + frame.RowGap;
    const auto visible = static_cast<std::size_t>(std::max(1.0F, std::floor((frame.LeftPane.height + frame.RowGap) / stride)));
    model.SetVisibleRows(visible);
    const std::size_t first = model.ScrollOffset();
    const std::size_t last = std::min(rows.size(), first + visible);
    const bool rowsFocused = model.FocusPane() == Pane::Rows;
    const float keySize = 18.0F * kDp;
    const float valueSize = 20.0F * kDp;
    const float headingSize = 22.0F * kDp;
    float y = frame.LeftPane.y;
    for (std::size_t index = first; index < last; ++index, y += stride) {
        const auto& row = rows[index];
        const UiRect rect{frame.LeftPane.x, y, frame.LeftPane.width, frame.RowHeight};
        if (row.Kind == RowKind::Heading) {
            TextStyle style{FontStyle::Heading, headingSize, WithAlpha(palette.Text, 0.35F), TextAlign::Left, true,
                            kLayerText};
            AppendText(output, style, rect.x + 4.0F * kDp,
                       CenteredBaseline(FontStyle::Heading, headingSize, rect.y, rect.height), row.Key);
            continue;
        }
        const float inset = 16.0F * kDp;
        if (row.Kind == RowKind::Info) {
            // Read-only text on a bare row: the key dimmed, the text plain.
            TextStyle textStyle{FontStyle::Body, valueSize, WithAlpha(palette.Text, 0.85F), TextAlign::Right, false,
                                kLayerText};
            const std::string text = ElideText(FontStyle::Body, valueSize, row.Text, rect.width * 0.62F);
            const float textWidth = AppendText(output, textStyle, rect.x + rect.width - inset,
                                               CenteredBaseline(FontStyle::Body, valueSize, rect.y, rect.height), text);
            TextStyle keyStyle{FontStyle::Heading, keySize, WithAlpha(palette.Text, 0.6F), TextAlign::Left, true,
                               kLayerText};
            AppendText(output, keyStyle, rect.x + inset, CenteredBaseline(FontStyle::Heading, keySize, rect.y, rect.height),
                       ElideText(FontStyle::Heading, keySize, row.Key, rect.width - 2.0F * inset - textWidth - 8.0F * kDp,
                                 true));
            continue;
        }
        const bool focused = index == model.RowIndex();
        const bool hovered = !focused && Hovered(model, HitKind::Row, index);
        const float alpha = row.Enabled ? 1.0F : 0.35F;
        Quad(output, rect, focused ? palette.Interactive : hovered ? WithAlpha(palette.Interactive, 0.5F) : palette.Control,
             kLayerRow);
        if (focused) Outline(output, rect, 2.0F * kDp, WithAlpha(palette.Accent, rowsFocused ? 1.0F : 0.5F), kLayerOutline);
        layout.Regions.push_back({ToRect(rect), HitKind::Row, index});
        float valueWidth = 0.0F;
        if (row.Kind != RowKind::Action) {
            std::string value = row.ValueText();
            if (row.IsModified) value = "\xE2\x80\xA2 " + value; // • value
            const FontStyle valueFont = row.IsModified ? FontStyle::Heading : FontStyle::Body;
            value = ElideText(valueFont, valueSize, value, rect.width * 0.5F);
            TextStyle style{valueFont, valueSize, WithAlpha(palette.Text, alpha), TextAlign::Right, false, kLayerText};
            valueWidth = AppendText(output, style, rect.x + rect.width - inset,
                                    CenteredBaseline(valueFont, valueSize, rect.y, rect.height), value);
        } else {
            TextStyle style{FontStyle::Body, valueSize, WithAlpha(palette.Accent, alpha), TextAlign::Right, false,
                            kLayerText};
            valueWidth = AppendText(output, style, rect.x + rect.width - inset,
                                    CenteredBaseline(FontStyle::Body, valueSize, rect.y, rect.height),
                                    "\xE2\x80\xBA"); // ›
        }
        const float keyWidth = rect.width - 2.0F * inset - valueWidth - 8.0F * kDp;
        TextStyle keyStyle{FontStyle::Heading, keySize, WithAlpha(palette.Text, alpha), TextAlign::Left, true,
                           kLayerText};
        AppendText(output, keyStyle, rect.x + inset, CenteredBaseline(FontStyle::Heading, keySize, rect.y, rect.height),
                   ElideText(FontStyle::Heading, keySize, row.Key, keyWidth, true));
    }
    // A scrollbar in the gap after the rows when some are hidden: a faint
    // track and an accent thumb sized and placed by the visible window.
    if (rows.size() > visible) {
        const UiRect track{frame.LeftPane.x + frame.LeftPane.width + 8.0F * kDp, frame.LeftPane.y, 3.0F * kDp,
                           frame.LeftPane.height};
        Quad(output, track, WithAlpha(palette.Text, 0.12F), kLayerRow);
        const float thumbHeight =
            std::max(12.0F * kDp, track.height * static_cast<float>(visible) / static_cast<float>(rows.size()));
        const float travel = track.height - thumbHeight;
        const float thumbY = track.y + travel * static_cast<float>(first) / static_cast<float>(rows.size() - visible);
        Quad(output, {track.x, thumbY, track.width, thumbHeight}, WithAlpha(palette.Accent, 0.7F), kLayerOutline);
    }
}

void DrawRightPane(const Model& model, const Frame& frame, const Palette& palette, std::vector<UiPrimitive>& output,
                   Layout& layout) {
    const auto* row = model.FocusedRow();
    if (row == nullptr) return;
    const UiRect& pane = frame.RightPane;
    float y = pane.y;
    const float titleSize = 22.0F * kDp;
    TextStyle title{FontStyle::Heading, titleSize, WithAlpha(palette.Text, 0.35F), TextAlign::Left, true, kLayerText};
    AppendText(output, title, pane.x, CenteredBaseline(FontStyle::Heading, titleSize, y, frame.RowHeight),
               ElideText(FontStyle::Heading, titleSize, row->Key, pane.width, true));
    y += frame.RowHeight + 4.0F * kDp;
    const float helpSize = 16.0F * kDp;
    const auto helpMetrics = MeasureFont(FontStyle::Body, helpSize);
    const float lineHeight = helpMetrics.LineHeight() * 1.1F;
    std::string help = row->Help;
    if (!row->Enabled && !row->UnavailableReason.empty()) help += (help.empty() ? "" : "\n") + row->UnavailableReason;
    for (const auto& line : WrapText(FontStyle::Body, helpSize, help, pane.width)) {
        if (y + lineHeight > pane.y + pane.height) break;
        TextStyle style{FontStyle::Body, helpSize, WithAlpha(palette.Text, 0.85F), TextAlign::Left, false, kLayerText};
        AppendText(output, style, pane.x, y + helpMetrics.Ascent, line);
        y += lineHeight;
    }
    y += 12.0F * kDp;
    if (row->Kind == RowKind::Choice) {
        const bool choicesFocused = model.FocusPane() == Pane::Choices;
        const float itemHeight = 34.0F * kDp;
        const float stride = itemHeight + 4.0F * kDp;
        const float itemSize = 18.0F * kDp;
        const auto count = row->Choices.size();
        const auto fit = static_cast<std::size_t>(std::max(1.0F, std::floor((pane.y + pane.height - y + 4.0F * kDp) / stride)));
        // Keep the cursor (or the current value) visible when the list is long.
        const std::size_t cursor = choicesFocused ? model.ChoiceIndex() : static_cast<std::size_t>(row->Value);
        std::size_t first = 0;
        if (count > fit && cursor >= fit) first = std::min(cursor + 1 - fit, count - fit);
        for (std::size_t index = first; index < std::min(count, first + fit); ++index, y += stride) {
            const UiRect rect{pane.x, y, pane.width, itemHeight};
            const bool current = static_cast<int>(index) == row->Value;
            const bool available = !row->ChoiceAvailable || row->ChoiceAvailable(static_cast<int>(index));
            const bool hovered = !current && Hovered(model, HitKind::Choice, index);
            Quad(output, rect, current ? palette.Selected : hovered ? palette.Interactive : palette.Control, kLayerRow);
            if (choicesFocused && index == model.ChoiceIndex()) {
                Outline(output, rect, 2.0F * kDp, palette.Accent, kLayerOutline);
            }
            layout.Regions.push_back({ToRect(rect), HitKind::Choice, index});
            TextStyle style{current ? FontStyle::Heading : FontStyle::Body, itemSize,
                            WithAlpha(palette.Text, available ? 1.0F : 0.35F), TextAlign::Left, false, kLayerText};
            AppendText(output, style, rect.x + 12.0F * kDp,
                       CenteredBaseline(style.Font, itemSize, rect.y, rect.height),
                       ElideText(style.Font, itemSize, row->Choices[index], rect.width - 24.0F * kDp));
        }
    } else if (row->Kind == RowKind::Number) {
        const float valueSize = 28.0F * kDp;
        TextStyle style{FontStyle::Heading, valueSize, palette.Text, TextAlign::Center, false, kLayerText};
        const float centre = pane.x + pane.width * 0.5F;
        const float baseline = CenteredBaseline(FontStyle::Heading, valueSize, y, frame.RowHeight * 1.2F);
        AppendText(output, style, centre, baseline, row->ValueText());
        const float width = MeasureText(FontStyle::Heading, valueSize, row->ValueText());
        // Each arrow is a button the pointer can hold: a square around the
        // glyph, lit while the pointer is over it.
        const float box = frame.RowHeight * 1.2F;
        const auto arrow = [&](float x, const char* glyph, HitKind kind) {
            const bool hovered = Hovered(model, kind, 0);
            const UiRect rect{x - box * 0.5F, y, box, box};
            if (hovered) Quad(output, rect, palette.Interactive, kLayerRow);
            TextStyle arrows{FontStyle::Body, valueSize * 0.8F, WithAlpha(palette.Accent, hovered ? 1.0F : 0.9F),
                             TextAlign::Center, false, kLayerText};
            AppendText(output, arrows, x, baseline, glyph);
            layout.Regions.push_back({ToRect(rect), kind, 0});
        };
        arrow(centre - width * 0.5F - 24.0F * kDp, "\xE2\x80\xB9", HitKind::NumberDown); // ‹
        arrow(centre + width * 0.5F + 24.0F * kDp, "\xE2\x80\xBA", HitKind::NumberUp);   // ›
    }
}

void DrawStatusAndHints(const Model& model, const Frame& frame, const Palette& palette, const Hints& hints,
                        std::vector<UiPrimitive>& output, Layout& layout) {
    const float size = 15.0F * kDp;
    if (!model.Status().empty()) {
        TextStyle style{FontStyle::Body, size, model.StatusIsError() ? palette.Error : palette.Success, TextAlign::Left,
                        false, kLayerText};
        AppendText(output, style, frame.Content.x, CenteredBaseline(FontStyle::Body, size, frame.StatusY, 26.0F * kDp),
                   ElideText(FontStyle::Body, size, model.Status(), frame.Content.width));
    }
    Quad(output, {frame.Window.x, frame.HintsY - 2.0F * kDp, frame.Window.width, 1.0F * kDp},
         WithAlpha(palette.Border, 0.4F), kLayerBorder);
    const float hintSize = 14.0F * kDp;
    const float baseline = CenteredBaseline(FontStyle::Body, hintSize, frame.HintsY, 36.0F * kDp);
    float x = frame.Content.x;
    const auto hint = [&](const std::string& button, const char* label) {
        TextStyle key{FontStyle::Heading, hintSize, palette.Accent, TextAlign::Left, false, kLayerText};
        x += AppendText(output, key, x, baseline, button) + 6.0F * kDp;
        TextStyle text{FontStyle::Body, hintSize, WithAlpha(palette.Text, 0.7F), TextAlign::Left, false, kLayerText};
        x += AppendText(output, text, x, baseline, label) + 20.0F * kDp;
    };
    const bool choices = model.FocusPane() == Pane::Choices;
    const bool modal = model.FocusPane() == Pane::Modal;
    hint(hints.Confirm, modal ? "Choose" : "Select");
    hint(hints.Back, modal ? "Cancel" : choices ? "Back" : "Close");
    if (!modal) {
        hint(hints.Tabs, "Tabs");
        hint(hints.Change, "Change");
    }
    // The close hint is also the pointer's close button.
    const bool closeHovered = model.Hover().Kind == HitKind::Close;
    TextStyle close{FontStyle::Body, hintSize, WithAlpha(palette.Text, closeHovered ? 1.0F : 0.5F), TextAlign::Right,
                    false, kLayerText};
    const float right = frame.Content.x + frame.Content.width;
    const float closeWidth = AppendText(output, close, right, baseline, hints.Close);
    layout.Regions.push_back(
        {{right - closeWidth - 12.0F * kDp, frame.HintsY, closeWidth + 24.0F * kDp, 36.0F * kDp}, HitKind::Close, 0});
}

void DrawModal(const Model& model, const Frame& frame, const Palette& palette, std::vector<UiPrimitive>& output,
               Layout& layout) {
    const auto& modal = model.CurrentModal();
    if (!modal.has_value()) return;
    Quad(output, frame.Window, WithAlpha(palette.Backdrop, 1.2F), kLayerModalBackdrop);
    const float padding = 24.0F * kDp;
    const float width = std::min(560.0F * kDp, frame.Window.width - 2.0F * padding);
    const float titleSize = 24.0F * kDp;
    const float lineSize = 17.0F * kDp;
    const auto lineMetrics = MeasureFont(FontStyle::Body, lineSize);
    const float lineHeight = lineMetrics.LineHeight() * 1.15F;
    std::vector<std::string> lines;
    for (const auto& text : modal->Lines) {
        for (auto& line : WrapText(FontStyle::Body, lineSize, text, width - 2.0F * padding)) lines.push_back(std::move(line));
    }
    const float buttonHeight = 44.0F * kDp;
    const float height = padding + 36.0F * kDp + 8.0F * kDp + static_cast<float>(lines.size()) * lineHeight + 16.0F * kDp +
                         buttonHeight + padding;
    const UiRect box{frame.Window.x + (frame.Window.width - width) * 0.5F,
                     frame.Window.y + (frame.Window.height - height) * 0.5F, width, height};
    Quad(output, box, WithAlpha(palette.Surface, 1.0F), kLayerModalBox);
    Outline(output, box, 2.0F * kDp, palette.Border, kLayerModalOutline);
    float y = box.y + padding;
    TextStyle title{FontStyle::Heading, titleSize, palette.Text, TextAlign::Left, false, kLayerModalText};
    AppendText(output, title, box.x + padding, CenteredBaseline(FontStyle::Heading, titleSize, y, 36.0F * kDp), modal->Title);
    y += 36.0F * kDp + 8.0F * kDp;
    for (const auto& line : lines) {
        TextStyle style{FontStyle::Body, lineSize, WithAlpha(palette.Text, 0.9F), TextAlign::Left, false, kLayerModalText};
        AppendText(output, style, box.x + padding, y + lineMetrics.Ascent, line);
        y += lineHeight;
    }
    y += 16.0F * kDp;
    const float buttonWidth = (width - 2.0F * padding - 16.0F * kDp) * 0.5F;
    const auto button = [&](float x, const std::string& label, bool highlighted, HitKind kind) {
        const UiRect rect{x, y, buttonWidth, buttonHeight};
        Quad(output, rect, highlighted ? palette.Selected : palette.Control, kLayerModalBox);
        if (highlighted) Outline(output, rect, 2.0F * kDp, palette.Accent, kLayerModalOutline);
        TextStyle style{FontStyle::Heading, 18.0F * kDp, palette.Text, TextAlign::Center, false, kLayerModalText};
        AppendText(output, style, rect.x + rect.width * 0.5F,
                   CenteredBaseline(FontStyle::Heading, style.Size, rect.y, rect.height), label);
        layout.Regions.push_back({ToRect(rect), kind, 0});
    };
    button(box.x + padding, modal->ConfirmLabel, model.ModalConfirmHighlighted(), HitKind::ModalConfirm);
    button(box.x + padding + buttonWidth + 16.0F * kDp, modal->CancelLabel, !model.ModalConfirmHighlighted(),
           HitKind::ModalCancel);
}

} // namespace

std::size_t AppendMenuPresentation(Model& model, float canvasWidth, float canvasHeight,
                                   std::vector<UiPrimitive>& output, const Palette& palette, const Hints& hints) {
    const auto before = output.size();
    if (!model.IsOpen()) return 0;
    const Frame frame = ComputeFrame(canvasWidth, canvasHeight);
    Quad(output, {0.0F, 0.0F, canvasWidth, canvasHeight}, palette.Backdrop, kLayerBackdrop);
    Quad(output, frame.Window, palette.Surface, kLayerWindow);
    Outline(output, frame.Window, 2.0F * kDp, palette.Border, kLayerBorder);
    Layout layout;
    DrawTabBar(model, frame, palette, hints, output, layout);
    DrawRows(model, frame, palette, output, layout);
    DrawRightPane(model, frame, palette, output, layout);
    DrawStatusAndHints(model, frame, palette, hints, output, layout);
    // The modal's buttons go first: they are the only targets while it is up.
    Layout modalLayout;
    DrawModal(model, frame, palette, output, modalLayout);
    modalLayout.Regions.insert(modalLayout.Regions.end(), layout.Regions.begin(), layout.Regions.end());
    model.SetLayout(std::move(modalLayout));
    return output.size() - before;
}

} // namespace Oot3dNativeGame::Menu
