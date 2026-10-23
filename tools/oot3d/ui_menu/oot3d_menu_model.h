#pragma once

// The in-game menu's content and navigation, apart from any renderer or
// setting owner: tabs of rows (toggles, choices, numbers, actions) read and
// written through callbacks, a focus that a pad or keyboard moves, and an
// optional modal question. The presentation draws it; the host feeds it the
// held inputs once per presented frame.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Oot3dNativeGame::Menu {

// Info: a read-only key and text (a control binding, for instance), skipped
// by the focus like a heading.
enum class RowKind : std::uint8_t { Heading, Toggle, Choice, Number, Action, Info };

struct WriteResult {
    std::string Message; // shown on the status line; empty means "Saved"
    bool Error = false;
};

struct Row {
    std::string Key;
    RowKind Kind = RowKind::Toggle;
    std::string Help;
    std::string Text; // Info: the value shown
    // Choice: one label per value; Number: the range and step.
    std::vector<std::string> Choices;
    int Minimum = 0;
    int Maximum = 0;
    int Step = 1;
    std::function<std::string(int)> Format; // Number: value text
    std::function<int()> Read;               // Toggle 0/1, Choice index, Number value
    std::function<WriteResult(int)> Write;
    std::function<WriteResult()> Activate;   // Action
    std::function<bool()> Available;         // absent: always
    std::string UnavailableReason;
    std::function<bool(int)> ChoiceAvailable; // Choice: a value the owner refuses
    std::function<bool()> Modified;          // differs from the default; absent: never

    // Refreshed by the model from the callbacks.
    int Value = 0;
    bool Enabled = true;
    bool IsModified = false;

    [[nodiscard]] bool Focusable() const noexcept { return Kind != RowKind::Heading && Kind != RowKind::Info; }
    [[nodiscard]] std::string ValueText() const;
};

struct Tab {
    std::string Title;
    std::vector<Row> Rows;
};

struct Document {
    std::vector<Tab> Tabs;
};

// Held state of the menu controls, sampled by the host each frame.
struct Input {
    bool Up = false, Down = false, Left = false, Right = false;
    bool Confirm = false, Back = false;
    bool TabPrevious = false, TabNext = false;
};

// The pointer (a mouse) over the canvas, sampled by the host each frame, in
// canvas units. Absent when it is outside the canvas or there is none.
struct Pointer {
    bool Present = false;
    float X = 0.0F;
    float Y = 0.0F;
    bool Pressed = false; // the primary button held
    float Wheel = 0.0F;   // notches since the previous update, positive up
};

struct Rect {
    float X = 0.0F, Y = 0.0F, Width = 0.0F, Height = 0.0F;
    [[nodiscard]] bool Contains(float x, float y) const noexcept {
        return x >= X && y >= Y && x < X + Width && y < Y + Height;
    }
};

// What the pointer can act on, as the presentation laid it out last frame.
enum class HitKind : std::uint8_t {
    None,
    Tab,          // Index: the tab
    Row,          // Index: the row (focusable rows only)
    Choice,       // Index: the value in the focused row's list
    NumberDown,   // the ‹ of the focused number row
    NumberUp,     // the › of the focused number row
    ModalConfirm,
    ModalCancel,
    Close,        // the close hint
};

struct HitRegion {
    Rect Area;
    HitKind Kind = HitKind::None;
    std::size_t Index = 0;
};

// The kind's name for diagnostics and the dev lab ("row", "tab", ...).
[[nodiscard]] constexpr const char* MenuHitKindName(HitKind kind) noexcept {
    switch (kind) {
    case HitKind::Tab: return "tab";
    case HitKind::Row: return "row";
    case HitKind::Choice: return "choice";
    case HitKind::NumberDown: return "number_down";
    case HitKind::NumberUp: return "number_up";
    case HitKind::ModalConfirm: return "modal_confirm";
    case HitKind::ModalCancel: return "modal_cancel";
    case HitKind::Close: return "close";
    case HitKind::None: break;
    }
    return "none";
}

struct Layout {
    std::vector<HitRegion> Regions; // first match wins: the modal's come first
};

struct Modal {
    std::string Title;
    std::vector<std::string> Lines;
    std::string ConfirmLabel = "OK";
    std::string CancelLabel = "Cancel";
    std::function<void()> OnConfirm;
    std::function<void()> OnCancel;
};

enum class Pane : std::uint8_t { Rows, Choices, Modal };

class Model {
  public:
    static constexpr double kRepeatDelaySeconds = 0.35;
    static constexpr double kRepeatIntervalSeconds = 0.08;

    explicit Model(Document document);

    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept { return mOpen; }

    // Navigation and edits from the held inputs since the previous update.
    void Update(const Input& held, double elapsedSeconds) { Update(held, Pointer{}, elapsedSeconds); }
    // The same, with the pointer acting on the layout of the previous frame:
    // moving over a row, tab or choice focuses it, a click selects or
    // activates it, the number arrows step (and repeat while held), the
    // wheel moves the focus.
    void Update(const Input& held, const Pointer& pointer, double elapsedSeconds);
    // Re-reads every row of the current tab from its callbacks.
    void Refresh();

    // The presentation's hit regions for the frame it just drew.
    void SetLayout(Layout layout) { mLayout = std::move(layout); }
    [[nodiscard]] const Layout& CurrentLayout() const noexcept { return mLayout; }
    // The region under the pointer, for the presentation's hover highlight.
    [[nodiscard]] const HitRegion& Hover() const noexcept { return mHover; }

    void SetModal(std::optional<Modal> modal);
    [[nodiscard]] const std::optional<Modal>& CurrentModal() const noexcept { return mModal; }
    [[nodiscard]] std::optional<Modal>& CurrentModal() noexcept { return mModal; }
    [[nodiscard]] bool ModalConfirmHighlighted() const noexcept { return mModalConfirm; }

    [[nodiscard]] const Document& Doc() const noexcept { return mDocument; }
    [[nodiscard]] Document& Doc() noexcept { return mDocument; }
    [[nodiscard]] std::size_t TabIndex() const noexcept { return mTab; }
    [[nodiscard]] std::size_t RowIndex() const noexcept { return mRow; }
    [[nodiscard]] Pane FocusPane() const noexcept;
    [[nodiscard]] std::size_t ChoiceIndex() const noexcept { return mChoice; }
    [[nodiscard]] const Row* FocusedRow() const noexcept;

    // The presentation tells how many rows fit; the model keeps the focused
    // row inside the window.
    void SetVisibleRows(std::size_t rows) noexcept;
    [[nodiscard]] std::size_t ScrollOffset() const noexcept { return mScroll; }

    [[nodiscard]] const std::string& Status() const noexcept { return mStatus; }
    [[nodiscard]] bool StatusIsError() const noexcept { return mStatusError; }
    void SetStatus(std::string text, bool error);
    void SelectTab(std::size_t index);

  private:
    struct Repeat {
        bool Held = false;
        double Timer = 0.0;
        // True on the press edge and on each auto-repeat while held.
        bool Fire(bool held, double elapsed) noexcept;
    };
    struct Edge {
        bool Held = false;
        bool Fire(bool held) noexcept;
    };

    [[nodiscard]] Tab* CurrentTab() noexcept;
    [[nodiscard]] Row* CurrentRow() noexcept;
    void MoveRow(int direction);
    void Adjust(int direction);
    void Confirm();
    void Back();
    void Apply(Row& row, int value);
    void ChooseValue(Row& row, int value);
    void RefreshRow(Row& row);
    void ClampScroll() noexcept;
    void FocusFirstRow();
    [[nodiscard]] HitRegion HitTest(float x, float y) const noexcept;
    // True when the pointer consumed this update (a modal click, for instance).
    bool UpdatePointer(const Pointer& pointer, double elapsedSeconds);

    Document mDocument;
    bool mOpen = false;
    std::size_t mTab = 0;
    std::size_t mRow = 0;
    std::size_t mScroll = 0;
    std::size_t mVisibleRows = 12;
    bool mInChoices = false;
    std::size_t mChoice = 0;
    std::optional<Modal> mModal;
    bool mModalConfirm = true;
    std::string mStatus;
    bool mStatusError = false;
    Repeat mUp, mDown, mLeft, mRight;
    Edge mConfirm, mBack, mTabPrevious, mTabNext;
    Layout mLayout;
    HitRegion mHover;
    float mPointerX = -1.0F, mPointerY = -1.0F;
    bool mPointerHeld = false;
    Repeat mPointerArrow; // the number arrows repeat while the button is held
};

} // namespace Oot3dNativeGame::Menu
