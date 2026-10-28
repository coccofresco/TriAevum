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
    float Dp = kDp;
    bool Touch = false;
    UiRect Window;
    float TabBarHeight = 0.0F;
    UiRect Content;
    UiRect LeftPane;
    UiRect RightPane;
    float RowHeight = 0.0F;
    float RowGap = 0.0F;
    float ChoiceHeight = 0.0F;
    float HintsY = 0.0F;
    float HintsHeight = 0.0F;
    float StatusY = 0.0F;
};

// Below this many dp of canvas height (a phone, a handheld, a small window)
// the window fills the canvas and its margins tighten so the rows keep room.
constexpr float kCompactHeightDp = 700.0F;

Frame ComputeFrame(float canvasWidth, float canvasHeight, const Metrics& metrics) noexcept {
    Frame frame;
    const float dp = metrics.Dp;
    frame.Dp = dp;
    frame.Touch = metrics.Touch;
    const bool compact = canvasHeight / dp < kCompactHeightDp;
    const float margin = compact ? 24.0F * dp : 48.0F * dp;
    const float width = std::min(1088.0F * dp, canvasWidth - margin);
    const float height = std::min(768.0F * dp, canvasHeight - margin);
    frame.Window = {(canvasWidth - width) * 0.5F, (canvasHeight - height) * 0.5F, width, height};
    frame.TabBarHeight = (compact ? 52.0F : 64.0F) * dp;
    const float padding = (compact ? 20.0F : 32.0F) * dp;
    // A finger needs a taller footer for the Resume button.
    frame.HintsHeight = (metrics.Touch ? 44.0F : 36.0F) * dp;
    const float statusHeight = (compact ? 22.0F : 26.0F) * dp;
    frame.HintsY = frame.Window.y + frame.Window.height - frame.HintsHeight - 8.0F * dp;
    frame.StatusY = frame.HintsY - statusHeight;
    const float contentTop = frame.Window.y + frame.TabBarHeight + (compact ? 10.0F : 20.0F) * dp;
    frame.Content = {frame.Window.x + padding, contentTop, frame.Window.width - 2.0F * padding,
                     frame.StatusY - 8.0F * dp - contentTop};
    const float gap = (compact ? 16.0F : 24.0F) * dp;
    const float leftWidth = std::floor((frame.Content.width - gap) * (compact ? 0.58F : 0.56F));
    frame.LeftPane = {frame.Content.x, frame.Content.y, leftWidth, frame.Content.height};
    frame.RightPane = {frame.Content.x + leftWidth + gap, frame.Content.y, frame.Content.width - leftWidth - gap,
                       frame.Content.height};
    // Touch targets of at least 44-48 dp, as the platforms recommend.
    frame.RowHeight = (metrics.Touch ? 48.0F : 40.0F) * dp;
    frame.RowGap = 6.0F * dp;
    frame.ChoiceHeight = (metrics.Touch ? 44.0F : 34.0F) * dp;
    return frame;
}

Rect ToRect(const UiRect& rect) noexcept { return {rect.x, rect.y, rect.width, rect.height}; }

bool Hovered(const Model& model, HitKind kind, std::size_t index) noexcept {
    return model.Hover().Kind == kind && model.Hover().Index == index;
}

void DrawTabBar(const Model& model, const Frame& frame, const Palette& palette, const Hints& hints,
                std::vector<UiPrimitive>& output, Layout& layout) {
    const float dp = frame.Dp;
    const auto& tabs = model.Doc().Tabs;
    const float size = 18.0F * dp;
    const float padding = 24.0F * dp;
    const float baseline = CenteredBaseline(FontStyle::Heading, size, frame.Window.y, frame.TabBarHeight);
    // Tab bar bottom rule.
    Quad(output, {frame.Window.x, frame.Window.y + frame.TabBarHeight - 1.0F * dp, frame.Window.width, 1.0F * dp},
         WithAlpha(palette.Border, 0.4F), kLayerBorder);
    float x = frame.Window.x + 32.0F * dp;
    TextStyle hintStyle{FontStyle::Body, 15.0F * dp, WithAlpha(palette.Text, 0.6F), TextAlign::Left, false,
                        kLayerText};
    // Shoulder hints at both ends of the bar (none on touch: a tap selects).
    const std::string& leftHint = hints.TabPrevious;
    const std::string& rightHint = hints.TabNext;
    if (!leftHint.empty()) {
        AppendText(output, hintStyle, x, baseline, leftHint);
        x += MeasureText(FontStyle::Body, hintStyle.Size, leftHint) + padding;
    }
    // When the titles are wider than the bar (a narrow or a scaled-up menu)
    // the strip slides to keep the current tab and the next one in view (a
    // tap on it moves on: touch has no shoulder buttons); a tab partly
    // outside the strip is left out.
    std::vector<float> widths;
    float total = 0.0F;
    float selectedEnd = 0.0F;
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        widths.push_back(MeasureText(FontStyle::Heading, size, tabs[index].Title, true));
        if (index != 0) total += padding;
        total += widths.back();
        if (index == std::min(model.TabIndex() + 1, tabs.size() - 1)) selectedEnd = total;
    }
    const float rightHintWidth = rightHint.empty() ? 0.0F : MeasureText(FontStyle::Body, hintStyle.Size, rightHint);
    const float limit =
        frame.Window.x + frame.Window.width - 32.0F * dp - (rightHint.empty() ? 0.0F : rightHintWidth + padding);
    const float stripLeft = x;
    const bool overflow = x + total > limit;
    if (overflow) x -= std::max(0.0F, x + selectedEnd - limit);
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        const float width = widths[index];
        if (overflow && (x < stripLeft - 0.5F * dp || x + width > limit + 0.5F * dp)) {
            x += width + padding;
            continue;
        }
        const bool selected = index == model.TabIndex();
        const bool hovered = !selected && Hovered(model, HitKind::Tab, index);
        TextStyle style{FontStyle::Heading, size, WithAlpha(palette.Text, selected ? 1.0F : hovered ? 0.7F : 0.35F),
                        TextAlign::Left, true, kLayerText};
        AppendText(output, style, x, baseline, tabs[index].Title);
        if (selected) {
            Quad(output,
                 {x - 8.0F * dp, frame.Window.y + frame.TabBarHeight - 4.0F * dp, width + 16.0F * dp, 4.0F * dp},
                 palette.Accent, kLayerOutline);
        }
        layout.Regions.push_back(
            {{x - padding * 0.5F, frame.Window.y, width + padding, frame.TabBarHeight}, HitKind::Tab, index});
        x += width + padding;
    }
    if (!rightHint.empty()) {
        hintStyle.Align = TextAlign::Left;
        AppendText(output, hintStyle, overflow ? limit + padding : x, baseline, rightHint);
    }
}

void DrawRows(Model& model, const Frame& frame, const Palette& palette, std::vector<UiPrimitive>& output,
              Layout& layout) {
    const float dp = frame.Dp;
    const auto& tabs = model.Doc().Tabs;
    if (model.TabIndex() >= tabs.size()) return;
    const auto& rows = tabs[model.TabIndex()].Rows;
    const float stride = frame.RowHeight + frame.RowGap;
    const auto visible = static_cast<std::size_t>(std::max(1.0F, std::floor((frame.LeftPane.height + frame.RowGap) / stride)));
    model.SetVisibleRows(visible);
    // A finger drags the rows by this stride.
    layout.RowsArea = ToRect(frame.LeftPane);
    layout.RowStride = stride;
    const std::size_t first = model.ScrollOffset();
    const std::size_t last = std::min(rows.size(), first + visible);
    const bool rowsFocused = model.FocusPane() == Pane::Rows;
    const float keySize = 18.0F * dp;
    const float valueSize = 20.0F * dp;
    const float headingSize = 22.0F * dp;
    float y = frame.LeftPane.y;
    for (std::size_t index = first; index < last; ++index, y += stride) {
        const auto& row = rows[index];
        const UiRect rect{frame.LeftPane.x, y, frame.LeftPane.width, frame.RowHeight};
        if (row.Kind == RowKind::Heading) {
            TextStyle style{FontStyle::Heading, headingSize, WithAlpha(palette.Text, 0.35F), TextAlign::Left, true,
                            kLayerText};
            AppendText(output, style, rect.x + 4.0F * dp,
                       CenteredBaseline(FontStyle::Heading, headingSize, rect.y, rect.height), row.Key);
            continue;
        }
        const float inset = 16.0F * dp;
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
                       ElideText(FontStyle::Heading, keySize, row.Key, rect.width - 2.0F * inset - textWidth - 8.0F * dp,
                                 true));
            continue;
        }
        const bool focused = index == model.RowIndex();
        const bool hovered = !focused && Hovered(model, HitKind::Row, index);
        const float alpha = row.Enabled ? 1.0F : 0.35F;
        Quad(output, rect, focused ? palette.Interactive : hovered ? WithAlpha(palette.Interactive, 0.5F) : palette.Control,
             kLayerRow);
        if (focused) Outline(output, rect, 2.0F * dp, WithAlpha(palette.Accent, rowsFocused ? 1.0F : 0.5F), kLayerOutline);
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
        const float keyWidth = rect.width - 2.0F * inset - valueWidth - 8.0F * dp;
        TextStyle keyStyle{FontStyle::Heading, keySize, WithAlpha(palette.Text, alpha), TextAlign::Left, true,
                           kLayerText};
        AppendText(output, keyStyle, rect.x + inset, CenteredBaseline(FontStyle::Heading, keySize, rect.y, rect.height),
                   ElideText(FontStyle::Heading, keySize, row.Key, keyWidth, true));
    }
    // A scrollbar in the gap after the rows when some are hidden: a faint
    // track and an accent thumb sized and placed by the visible window.
    if (rows.size() > visible) {
        const UiRect track{frame.LeftPane.x + frame.LeftPane.width + 8.0F * dp, frame.LeftPane.y, 3.0F * dp,
                           frame.LeftPane.height};
        Quad(output, track, WithAlpha(palette.Text, 0.12F), kLayerRow);
        const float thumbHeight =
            std::max(12.0F * dp, track.height * static_cast<float>(visible) / static_cast<float>(rows.size()));
        const float travel = track.height - thumbHeight;
        const float thumbY = track.y + travel * static_cast<float>(first) / static_cast<float>(rows.size() - visible);
        Quad(output, {track.x, thumbY, track.width, thumbHeight}, WithAlpha(palette.Accent, 0.7F), kLayerOutline);
    }
}

void DrawRightPane(const Model& model, const Frame& frame, const Palette& palette, std::vector<UiPrimitive>& output,
                   Layout& layout) {
    const float dp = frame.Dp;
    const auto* row = model.FocusedRow();
    if (row == nullptr) return;
    const UiRect& pane = frame.RightPane;
    float y = pane.y;
    const float titleSize = 22.0F * dp;
    TextStyle title{FontStyle::Heading, titleSize, WithAlpha(palette.Text, 0.35F), TextAlign::Left, true, kLayerText};
    AppendText(output, title, pane.x, CenteredBaseline(FontStyle::Heading, titleSize, y, frame.RowHeight),
               ElideText(FontStyle::Heading, titleSize, row->Key, pane.width, true));
    y += frame.RowHeight + 4.0F * dp;
    const float helpSize = 16.0F * dp;
    const auto helpMetrics = MeasureFont(FontStyle::Body, helpSize);
    const float lineHeight = helpMetrics.LineHeight() * 1.1F;
    const float itemHeight = frame.ChoiceHeight;
    const float stride = itemHeight + 4.0F * dp;
    // A long help text gives way to the controls below it on a short pane:
    // up to three of the values, or the number with its arrows, stay visible.
    const float reserve = row->Kind == RowKind::Choice
                              ? 12.0F * dp + static_cast<float>(std::min<std::size_t>(row->Choices.size(), 3U)) * stride
                          : row->Kind == RowKind::Number ? 12.0F * dp + frame.RowHeight * 1.2F
                                                         : 0.0F;
    std::string help = row->Help;
    if (!row->Enabled && !row->UnavailableReason.empty()) help += (help.empty() ? "" : "\n") + row->UnavailableReason;
    for (const auto& line : WrapText(FontStyle::Body, helpSize, help, pane.width)) {
        if (y + lineHeight > pane.y + pane.height - reserve) break;
        TextStyle style{FontStyle::Body, helpSize, WithAlpha(palette.Text, 0.85F), TextAlign::Left, false, kLayerText};
        AppendText(output, style, pane.x, y + helpMetrics.Ascent, line);
        y += lineHeight;
    }
    y += 12.0F * dp;
    if (row->Kind == RowKind::Choice) {
        const bool choicesFocused = model.FocusPane() == Pane::Choices;
        const float itemSize = 18.0F * dp;
        const auto count = row->Choices.size();
        const auto fit = static_cast<std::size_t>(std::max(1.0F, std::floor((pane.y + pane.height - y + 4.0F * dp) / stride)));
        // Keep the cursor (or the current value) visible when the list is long.
        const std::size_t cursor = choicesFocused ? model.ChoiceIndex() : static_cast<std::size_t>(row->Value);
        std::size_t first = 0;
        if (count > fit && cursor >= fit) first = std::min(cursor + 1 - fit, count - fit);
        if (count > fit) {
            layout.ChoicesArea = {pane.x, y, pane.width, static_cast<float>(fit) * stride};
            layout.ChoiceStride = stride;
        }
        for (std::size_t index = first; index < std::min(count, first + fit); ++index, y += stride) {
            const UiRect rect{pane.x, y, pane.width, itemHeight};
            const bool current = static_cast<int>(index) == row->Value;
            const bool available = !row->ChoiceAvailable || row->ChoiceAvailable(static_cast<int>(index));
            const bool hovered = !current && Hovered(model, HitKind::Choice, index);
            Quad(output, rect, current ? palette.Selected : hovered ? palette.Interactive : palette.Control, kLayerRow);
            if (choicesFocused && index == model.ChoiceIndex()) {
                Outline(output, rect, 2.0F * dp, palette.Accent, kLayerOutline);
            }
            layout.Regions.push_back({ToRect(rect), HitKind::Choice, index});
            TextStyle style{current ? FontStyle::Heading : FontStyle::Body, itemSize,
                            WithAlpha(palette.Text, available ? 1.0F : 0.35F), TextAlign::Left, false, kLayerText};
            AppendText(output, style, rect.x + 12.0F * dp,
                       CenteredBaseline(style.Font, itemSize, rect.y, rect.height),
                       ElideText(style.Font, itemSize, row->Choices[index], rect.width - 24.0F * dp));
        }
    } else if (row->Kind == RowKind::Number) {
        const float valueSize = 28.0F * dp;
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
        arrow(centre - width * 0.5F - 24.0F * dp, "\xE2\x80\xB9", HitKind::NumberDown); // ‹
        arrow(centre + width * 0.5F + 24.0F * dp, "\xE2\x80\xBA", HitKind::NumberUp);   // ›
    }
}

void DrawStatusAndHints(const Model& model, const Frame& frame, const Palette& palette, const Hints& hints,
                        std::vector<UiPrimitive>& output, Layout& layout) {
    const float dp = frame.Dp;
    const float size = 15.0F * dp;
    if (!model.Status().empty()) {
        TextStyle style{FontStyle::Body, size, model.StatusIsError() ? palette.Error : palette.Success, TextAlign::Left,
                        false, kLayerText};
        AppendText(output, style, frame.Content.x,
                   CenteredBaseline(FontStyle::Body, size, frame.StatusY, frame.HintsY - frame.StatusY),
                   ElideText(FontStyle::Body, size, model.Status(), frame.Content.width));
    }
    Quad(output, {frame.Window.x, frame.HintsY - 2.0F * dp, frame.Window.width, 1.0F * dp},
         WithAlpha(palette.Border, 0.4F), kLayerBorder);
    const float hintSize = 14.0F * dp;
    const float baseline = CenteredBaseline(FontStyle::Body, hintSize, frame.HintsY, frame.HintsHeight);
    // Resume, a persistent action of the footer on every tab: a button for
    // the pointer, with the keys that close the menu beside its label.
    const bool resumeHovered = model.Hover().Kind == HitKind::Close;
    const float labelSize = 15.0F * dp;
    const float labelWidth = MeasureText(FontStyle::Heading, labelSize, kResumeLabel);
    const float keysWidth = MeasureText(FontStyle::Body, hintSize, hints.Close);
    const float inner = 12.0F * dp;
    const float buttonWidth = inner + labelWidth + (keysWidth > 0.0F ? 8.0F * dp + keysWidth : 0.0F) + inner;
    const float buttonHeight = (frame.Touch ? 36.0F : 28.0F) * dp;
    const float right = frame.Content.x + frame.Content.width;
    const UiRect button{right - buttonWidth, frame.HintsY + (frame.HintsHeight - buttonHeight) * 0.5F, buttonWidth,
                        buttonHeight};
    float x = frame.Content.x;
    // Each hint: the control (button prompts, a little larger than the words)
    // and what it does; a control the device lacks is left out, and so is a
    // hint that would run into the Resume button on a narrow menu.
    bool full = false;
    const auto hint = [&](const std::string& control, const char* label) {
        if (control.empty() || full) return;
        const float width = MeasureText(FontStyle::Heading, 16.0F * dp, control) + 6.0F * dp +
                            MeasureText(FontStyle::Body, hintSize, label);
        if (x + width > button.x - 12.0F * dp) {
            full = true;
            return;
        }
        TextStyle key{FontStyle::Heading, 16.0F * dp, palette.Accent, TextAlign::Left, false, kLayerText};
        x += AppendText(output, key, x, baseline, control) + 6.0F * dp;
        TextStyle text{FontStyle::Body, hintSize, WithAlpha(palette.Text, 0.7F), TextAlign::Left, false, kLayerText};
        x += AppendText(output, text, x, baseline, label) + 20.0F * dp;
    };
    const bool choices = model.FocusPane() == Pane::Choices;
    const bool modal = model.FocusPane() == Pane::Modal;
    hint(hints.Confirm, modal ? "Choose" : "Select");
    hint(hints.Back, modal ? "Cancel" : choices ? "Back" : "Resume");
    if (!modal) {
        const std::string tabs = hints.TabPrevious.empty() || hints.TabNext.empty()
                                     ? hints.TabPrevious + hints.TabNext
                                     : hints.TabPrevious + " " + hints.TabNext;
        hint(tabs, "Tabs");
        hint(hints.Change, "Change");
    }
    Quad(output, button, resumeHovered ? palette.Selected : palette.Interactive, kLayerRow);
    Outline(output, button, 1.0F * dp, WithAlpha(palette.Accent, resumeHovered ? 1.0F : 0.6F), kLayerOutline);
    TextStyle label{FontStyle::Heading, labelSize, palette.Text, TextAlign::Left, false, kLayerText};
    const float labelEnd = button.x + inner + AppendText(output, label, button.x + inner,
                                                         CenteredBaseline(FontStyle::Heading, labelSize, button.y,
                                                                          button.height),
                                                         kResumeLabel);
    TextStyle keys{FontStyle::Body, hintSize, WithAlpha(palette.Text, resumeHovered ? 0.9F : 0.6F), TextAlign::Left,
                   false, kLayerText};
    AppendText(output, keys, labelEnd + 8.0F * dp,
               CenteredBaseline(FontStyle::Body, hintSize, button.y, button.height), hints.Close);
    layout.Regions.push_back({ToRect(button), HitKind::Close, 0});
}

// The modal question over `area` (the menu's window, or the whole canvas for
// a card shown without the menu).
void DrawModal(const Model& model, const Frame& frame, const UiRect& area, const Palette& palette,
               std::vector<UiPrimitive>& output, Layout& layout) {
    const float dp = frame.Dp;
    const auto& modal = model.CurrentModal();
    if (!modal.has_value()) return;
    if (!model.CardOnly()) Quad(output, area, WithAlpha(palette.Backdrop, 1.2F), kLayerModalBackdrop);
    const float padding = 24.0F * dp;
    const float width = std::min(std::max(240.0F, modal->WidthDp) * dp, area.width - 2.0F * padding);
    const float titleSize = 24.0F * dp;
    const float lineSize = 17.0F * dp;
    const auto lineMetrics = MeasureFont(FontStyle::Body, lineSize);
    const float lineHeight = lineMetrics.LineHeight() * 1.15F;
    std::vector<std::string> lines;
    for (const auto& text : modal->Lines) {
        for (auto& line : WrapText(FontStyle::Body, lineSize, text, width - 2.0F * padding)) lines.push_back(std::move(line));
    }
    const float buttonHeight = (frame.Touch ? 52.0F : 44.0F) * dp;
    const float height = padding + 36.0F * dp + 8.0F * dp + static_cast<float>(lines.size()) * lineHeight + 16.0F * dp +
                         buttonHeight + padding;
    const UiRect box{area.x + (area.width - width) * 0.5F, area.y + std::max(0.0F, (area.height - height) * 0.5F),
                     width, height};
    Quad(output, box, WithAlpha(palette.Surface, 1.0F), kLayerModalBox);
    Outline(output, box, 2.0F * dp, palette.Border, kLayerModalOutline);
    float y = box.y + padding;
    TextStyle title{FontStyle::Heading, titleSize, palette.Text, TextAlign::Left, false, kLayerModalText};
    AppendText(output, title, box.x + padding, CenteredBaseline(FontStyle::Heading, titleSize, y, 36.0F * dp), modal->Title);
    y += 36.0F * dp + 8.0F * dp;
    for (const auto& line : lines) {
        TextStyle style{FontStyle::Body, lineSize, WithAlpha(palette.Text, 0.9F), TextAlign::Left, false, kLayerModalText};
        AppendText(output, style, box.x + padding, y + lineMetrics.Ascent, line);
        y += lineHeight;
    }
    y += 16.0F * dp;
    const float buttonWidth = (width - 2.0F * padding - 16.0F * dp) * 0.5F;
    const auto button = [&](float x, const std::string& label, bool highlighted, HitKind kind) {
        const UiRect rect{x, y, buttonWidth, buttonHeight};
        Quad(output, rect, highlighted ? palette.Selected : palette.Control, kLayerModalBox);
        if (highlighted) Outline(output, rect, 2.0F * dp, palette.Accent, kLayerModalOutline);
        TextStyle style{FontStyle::Heading, 18.0F * dp, palette.Text, TextAlign::Center, false, kLayerModalText};
        AppendText(output, style, rect.x + rect.width * 0.5F,
                   CenteredBaseline(FontStyle::Heading, style.Size, rect.y, rect.height), label);
        layout.Regions.push_back({ToRect(rect), kind, 0});
    };
    if (modal->SingleButton()) {
        // A notice: its one button, centred.
        button(box.x + (width - buttonWidth) * 0.5F, modal->ConfirmLabel, true, HitKind::ModalConfirm);
        return;
    }
    button(box.x + padding, modal->ConfirmLabel, model.ModalConfirmHighlighted(), HitKind::ModalConfirm);
    button(box.x + padding + buttonWidth + 16.0F * dp, modal->CancelLabel, !model.ModalConfirmHighlighted(),
           HitKind::ModalCancel);
}

} // namespace

float MenuDensityScale(float heightMillimetres) noexcept {
    // 1080 dp at 0.15 mm each: a picture shorter than 162 mm (a phone, a
    // handheld, a small tablet) scales up, at most to a 2.6x (a phone's
    // 62 mm landscape height); taller ones (laptops, monitors) keep 1.
    constexpr float kMillimetresPerDp = 0.15F;
    if (!std::isfinite(heightMillimetres) || heightMillimetres < 20.0F || heightMillimetres > 2000.0F) return 1.0F;
    return std::clamp(1080.0F * kMillimetresPerDp / heightMillimetres, 1.0F, 2.6F);
}

std::size_t AppendMenuPresentation(Model& model, float canvasWidth, float canvasHeight,
                                   std::vector<UiPrimitive>& output, const Palette& palette, const Hints& hints,
                                   const Metrics& metrics) {
    const auto before = output.size();
    if (!model.IsOpen()) return 0;
    const Frame frame = ComputeFrame(canvasWidth, canvasHeight, metrics);
    Quad(output, {0.0F, 0.0F, canvasWidth, canvasHeight}, palette.Backdrop, kLayerBackdrop);
    if (model.CardOnly()) {
        // The card alone over the dimmed game, centred on the canvas.
        Layout cardLayout;
        DrawModal(model, frame, {0.0F, 0.0F, canvasWidth, canvasHeight}, palette, output, cardLayout);
        model.SetLayout(std::move(cardLayout));
        return output.size() - before;
    }
    Quad(output, frame.Window, palette.Surface, kLayerWindow);
    Outline(output, frame.Window, 2.0F * frame.Dp, palette.Border, kLayerBorder);
    Layout layout;
    DrawTabBar(model, frame, palette, hints, output, layout);
    DrawRows(model, frame, palette, output, layout);
    DrawRightPane(model, frame, palette, output, layout);
    DrawStatusAndHints(model, frame, palette, hints, output, layout);
    // The modal's buttons go first: they are the only targets while it is up.
    Layout modalLayout;
    DrawModal(model, frame, frame.Window, palette, output, modalLayout);
    modalLayout.Regions.insert(modalLayout.Regions.end(), layout.Regions.begin(), layout.Regions.end());
    if (!model.CurrentModal().has_value()) {
        modalLayout.RowsArea = layout.RowsArea;
        modalLayout.RowStride = layout.RowStride;
        modalLayout.ChoicesArea = layout.ChoicesArea;
        modalLayout.ChoiceStride = layout.ChoiceStride;
    }
    model.SetLayout(std::move(modalLayout));
    return output.size() - before;
}

} // namespace Oot3dNativeGame::Menu
