#include "oot3d_menu_model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace Oot3dNativeGame::Menu {

std::string Row::ValueText() const {
    switch (Kind) {
    case RowKind::Toggle:
        return Value != 0 ? "On" : "Off";
    case RowKind::Choice:
        if (Choices.empty()) return {};
        return Choices[static_cast<std::size_t>(std::clamp<int>(Value, 0, static_cast<int>(Choices.size()) - 1))];
    case RowKind::Number:
        return Format ? Format(Value) : std::to_string(Value);
    case RowKind::Info:
        return Text;
    case RowKind::Action:
    case RowKind::Heading:
        break;
    }
    return {};
}

bool Model::Repeat::Fire(bool held, double elapsed) noexcept {
    if (!held) {
        Held = false;
        Timer = 0.0;
        return false;
    }
    if (!Held) {
        Held = true;
        Timer = kRepeatDelaySeconds;
        return true;
    }
    Timer -= elapsed;
    if (Timer <= 0.0) {
        Timer += kRepeatIntervalSeconds;
        if (Timer < 0.0) Timer = kRepeatIntervalSeconds;
        return true;
    }
    return false;
}

bool Model::Edge::Fire(bool held) noexcept {
    const bool fire = held && !Held;
    Held = held;
    return fire;
}

Model::Model(Document document) : mDocument(std::move(document)) {
    FocusFirstRow();
}

void Model::Open() {
    if (mOpen) return;
    mOpen = true;
    mInChoices = false;
    mStatus.clear();
    mStatusError = false;
    // The buttons held at the opening press must not act inside the menu.
    mConfirm.Held = mBack.Held = mTabPrevious.Held = mTabNext.Held = true;
    mUp = mDown = mLeft = mRight = {};
    // The pointer acts once the menu has been drawn and the pointer moved.
    mLayout = {};
    mHover = {};
    mPointerX = mPointerY = -1.0F;
    mPointerHeld = true;
    mPointerArrow = {};
    Refresh();
}

void Model::Close() {
    mOpen = false;
    mInChoices = false;
    mModal.reset();
    mLayout = {};
    mHover = {};
}

Pane Model::FocusPane() const noexcept {
    if (mModal.has_value()) return Pane::Modal;
    return mInChoices ? Pane::Choices : Pane::Rows;
}

Tab* Model::CurrentTab() noexcept {
    return mTab < mDocument.Tabs.size() ? &mDocument.Tabs[mTab] : nullptr;
}

Row* Model::CurrentRow() noexcept {
    auto* tab = CurrentTab();
    return tab != nullptr && mRow < tab->Rows.size() ? &tab->Rows[mRow] : nullptr;
}

const Row* Model::FocusedRow() const noexcept {
    if (mTab >= mDocument.Tabs.size()) return nullptr;
    const auto& rows = mDocument.Tabs[mTab].Rows;
    return mRow < rows.size() ? &rows[mRow] : nullptr;
}

void Model::SetVisibleRows(std::size_t rows) noexcept {
    mVisibleRows = std::max<std::size_t>(1U, rows);
    ClampScroll();
}

void Model::SetStatus(std::string text, bool error) {
    mStatus = std::move(text);
    mStatusError = error;
}

void Model::SetModal(std::optional<Modal> modal) {
    const bool had = mModal.has_value();
    mModal = std::move(modal);
    if (mModal.has_value() && !had) {
        mModalConfirm = true;
        mConfirm.Held = mBack.Held = true;
    }
}

void Model::SelectTab(std::size_t index) {
    if (mDocument.Tabs.empty()) return;
    mTab = index % mDocument.Tabs.size();
    mInChoices = false;
    // The status line belongs to the tab it was written on.
    mStatus.clear();
    mStatusError = false;
    FocusFirstRow();
    Refresh();
}

void Model::FocusFirstRow() {
    mRow = 0;
    mScroll = 0;
    auto* tab = CurrentTab();
    if (tab == nullptr) return;
    for (std::size_t index = 0; index < tab->Rows.size(); ++index) {
        if (tab->Rows[index].Focusable()) {
            mRow = index;
            break;
        }
    }
    ClampScroll();
}

void Model::ClampScroll() noexcept {
    auto* tab = CurrentTab();
    if (tab == nullptr) {
        mScroll = 0;
        return;
    }
    const std::size_t count = tab->Rows.size();
    if (count <= mVisibleRows) {
        mScroll = 0;
        return;
    }
    // Keep a heading just above the focused row visible when there is one.
    std::size_t first = mRow;
    if (first > 0 && !tab->Rows[first - 1].Focusable()) --first;
    if (first < mScroll) mScroll = first;
    if (mRow >= mScroll + mVisibleRows) mScroll = mRow + 1 - mVisibleRows;
    mScroll = std::min(mScroll, count - mVisibleRows);
}

void Model::RefreshRow(Row& row) {
    row.Enabled = !row.Available || row.Available();
    if (row.Read) row.Value = row.Read();
    if (row.Kind == RowKind::Choice && !row.Choices.empty()) {
        row.Value = std::clamp<int>(row.Value, 0, static_cast<int>(row.Choices.size()) - 1);
    } else if (row.Kind == RowKind::Number) {
        row.Value = std::clamp(row.Value, row.Minimum, row.Maximum);
    } else if (row.Kind == RowKind::Toggle) {
        row.Value = row.Value != 0 ? 1 : 0;
    }
    row.IsModified = row.Modified ? row.Modified() : false;
}

void Model::Refresh() {
    auto* tab = CurrentTab();
    if (tab == nullptr) return;
    for (auto& row : tab->Rows) RefreshRow(row);
}

void Model::MoveRow(int direction) {
    auto* tab = CurrentTab();
    if (tab == nullptr || tab->Rows.empty()) return;
    const std::size_t count = tab->Rows.size();
    std::size_t index = mRow;
    for (std::size_t step = 0; step < count; ++step) {
        index = (index + count + static_cast<std::size_t>(direction > 0 ? 1 : count - 1)) % count;
        if (tab->Rows[index].Focusable()) {
            mRow = index;
            break;
        }
    }
    ClampScroll();
}

void Model::Apply(Row& row, int value) {
    if (!row.Write) return;
    WriteResult result = row.Write(value);
    if (result.Message.empty() && !result.Error) result.Message = "Saved";
    SetStatus(std::move(result.Message), result.Error);
    Refresh();
}

void Model::Adjust(int direction) {
    auto* row = CurrentRow();
    if (row == nullptr || !row->Enabled) return;
    switch (row->Kind) {
    case RowKind::Toggle:
        Apply(*row, row->Value != 0 ? 0 : 1);
        break;
    case RowKind::Choice: {
        const int count = static_cast<int>(row->Choices.size());
        if (count == 0) return;
        int value = row->Value;
        for (int step = 0; step < count; ++step) {
            value = (value + direction + count) % count;
            if (!row->ChoiceAvailable || row->ChoiceAvailable(value)) break;
        }
        if (value != row->Value) Apply(*row, value);
        break;
    }
    case RowKind::Number: {
        const int value = std::clamp(row->Value + direction * std::max(1, row->Step), row->Minimum, row->Maximum);
        if (value != row->Value) Apply(*row, value);
        break;
    }
    case RowKind::Action:
    case RowKind::Heading:
        break;
    }
}

void Model::Confirm() {
    auto* row = CurrentRow();
    if (row == nullptr || !row->Enabled) return;
    switch (row->Kind) {
    case RowKind::Toggle:
        Apply(*row, row->Value != 0 ? 0 : 1);
        break;
    case RowKind::Choice:
        if (!row->Choices.empty()) {
            mInChoices = true;
            mChoice = static_cast<std::size_t>(row->Value);
        }
        break;
    case RowKind::Number:
        Adjust(1);
        break;
    case RowKind::Action:
        if (row->Activate) {
            WriteResult result = row->Activate();
            if (result.Message.empty() && !result.Error) result.Message = "Done";
            SetStatus(std::move(result.Message), result.Error);
            Refresh();
        }
        break;
    case RowKind::Heading:
        break;
    }
}

void Model::Back() {
    if (mInChoices) {
        mInChoices = false;
        return;
    }
    Close();
}

// A value picked from the list by its index: applied when the owner takes it,
// refused on the status line otherwise. The list closes on success.
void Model::ChooseValue(Row& row, int value) {
    const bool available = !row.ChoiceAvailable || row.ChoiceAvailable(value);
    if (!available) {
        SetStatus("Not available with the current renderer", true);
        return;
    }
    mInChoices = false;
    if (value != row.Value) Apply(row, value);
}

HitRegion Model::HitTest(float x, float y) const noexcept {
    for (const auto& region : mLayout.Regions) {
        if (region.Area.Contains(x, y)) return region;
    }
    return {};
}

bool Model::UpdatePointer(const Pointer& pointer, double elapsedSeconds) {
    if (!pointer.Present) {
        mHover = {};
        mPointerHeld = pointer.Pressed;
        mPointerArrow.Fire(false, elapsedSeconds);
        return false;
    }
    // The first sample after the opening is where the mouse already rests,
    // not a move: a pad or keyboard user's idle mouse must not steal the
    // first row.
    const bool moved = mPointerX >= 0.0F && (pointer.X != mPointerX || pointer.Y != mPointerY);
    mPointerX = pointer.X;
    mPointerY = pointer.Y;
    const bool click = pointer.Pressed && !mPointerHeld;
    mPointerHeld = pointer.Pressed;
    mHover = HitTest(pointer.X, pointer.Y);
    // The modal takes every pointer action: its buttons highlight under the
    // pointer and a click decides; nothing behind it reacts.
    if (mModal.has_value()) {
        if (mHover.Kind != HitKind::ModalConfirm && mHover.Kind != HitKind::ModalCancel) {
            mHover = {};
            return true;
        }
        if (moved) mModalConfirm = mHover.Kind == HitKind::ModalConfirm;
        if (click) {
            auto modal = std::move(*mModal);
            mModal.reset();
            if (mHover.Kind == HitKind::ModalConfirm && modal.OnConfirm) modal.OnConfirm();
            if (mHover.Kind == HitKind::ModalCancel && modal.OnCancel) modal.OnCancel();
        }
        return true;
    }
    auto* tab = CurrentTab();
    bool overArrow = false;
    switch (mHover.Kind) {
    case HitKind::Row:
        if (tab == nullptr || mHover.Index >= tab->Rows.size() || !tab->Rows[mHover.Index].Focusable()) break;
        // Moving over a row focuses it; while the list is open the rows wait
        // for a click, so the list's cursor is not lost to a passing pointer.
        if ((moved && !mInChoices) || click) {
            if (mRow != mHover.Index) {
                mRow = mHover.Index;
                ClampScroll();
            }
            mInChoices = false;
        }
        if (click) {
            auto* row = CurrentRow();
            // A number row changes through its arrows in the right pane.
            if (row != nullptr && row->Kind != RowKind::Number) Confirm();
        }
        break;
    case HitKind::Tab:
        if (click && mHover.Index != mTab) SelectTab(mHover.Index);
        break;
    case HitKind::Choice: {
        auto* row = CurrentRow();
        if (row == nullptr || row->Kind != RowKind::Choice || mHover.Index >= row->Choices.size()) break;
        if (moved && mInChoices) mChoice = mHover.Index;
        if (click) {
            mChoice = mHover.Index;
            ChooseValue(*row, static_cast<int>(mHover.Index));
        }
        break;
    }
    case HitKind::NumberDown:
    case HitKind::NumberUp:
        overArrow = true;
        if (click) mPointerArrow = {};
        if (mPointerArrow.Fire(pointer.Pressed, elapsedSeconds)) Adjust(mHover.Kind == HitKind::NumberUp ? 1 : -1);
        break;
    case HitKind::Close:
        if (click) Close();
        break;
    case HitKind::ModalConfirm:
    case HitKind::ModalCancel:
    case HitKind::None:
        break;
    }
    if (!overArrow) mPointerArrow.Fire(false, elapsedSeconds);
    // The wheel moves the focus like up and down, after the hover so a move
    // and a notch in one frame end on the notch: the list's cursor while it
    // is open, the rows otherwise.
    if (pointer.Wheel != 0.0F) {
        const int steps = static_cast<int>(pointer.Wheel > 0.0F ? std::ceil(pointer.Wheel) : std::floor(pointer.Wheel));
        for (int step = 0; step < std::abs(steps); ++step) {
            const int direction = steps > 0 ? -1 : 1;
            if (mInChoices) {
                auto* row = CurrentRow();
                const std::size_t count = row != nullptr ? row->Choices.size() : 0;
                if (count != 0) mChoice = (mChoice + static_cast<std::size_t>(direction > 0 ? 1 : count - 1)) % count;
            } else {
                MoveRow(direction);
            }
        }
    }
    // A held arrow keeps the keys out until it is released.
    return overArrow && pointer.Pressed;
}

void Model::Update(const Input& held, const Pointer& pointer, double elapsedSeconds) {
    if (!mOpen) return;
    if (UpdatePointer(pointer, elapsedSeconds)) {
        // Keep the button edges current so a release is not a press later.
        mConfirm.Fire(held.Confirm);
        mBack.Fire(held.Back);
        mTabPrevious.Fire(held.TabPrevious);
        mTabNext.Fire(held.TabNext);
        return;
    }
    if (!mOpen) return;
    const bool up = mUp.Fire(held.Up, elapsedSeconds);
    const bool down = mDown.Fire(held.Down, elapsedSeconds);
    const bool left = mLeft.Fire(held.Left, elapsedSeconds);
    const bool right = mRight.Fire(held.Right, elapsedSeconds);
    const bool confirm = mConfirm.Fire(held.Confirm);
    const bool back = mBack.Fire(held.Back);
    const bool tabPrevious = mTabPrevious.Fire(held.TabPrevious);
    const bool tabNext = mTabNext.Fire(held.TabNext);

    if (mModal.has_value()) {
        if (left || right) mModalConfirm = !mModalConfirm;
        if (confirm) {
            auto modal = std::move(*mModal);
            mModal.reset();
            if (mModalConfirm && modal.OnConfirm) modal.OnConfirm();
            if (!mModalConfirm && modal.OnCancel) modal.OnCancel();
        } else if (back) {
            auto modal = std::move(*mModal);
            mModal.reset();
            if (modal.OnCancel) modal.OnCancel();
        }
        return;
    }

    if (tabPrevious || tabNext) {
        if (!mDocument.Tabs.empty()) {
            const std::size_t count = mDocument.Tabs.size();
            SelectTab((mTab + (tabNext ? 1 : count - 1)) % count);
        }
        return;
    }

    if (mInChoices) {
        auto* row = CurrentRow();
        if (row == nullptr || row->Choices.empty()) {
            mInChoices = false;
            return;
        }
        const std::size_t count = row->Choices.size();
        if (up) mChoice = (mChoice + count - 1) % count;
        if (down) mChoice = (mChoice + 1) % count;
        if (confirm) {
            ChooseValue(*row, static_cast<int>(mChoice));
        } else if (back) {
            Back();
        }
        return;
    }

    if (up) MoveRow(-1);
    if (down) MoveRow(1);
    if (left) Adjust(-1);
    if (right) Adjust(1);
    if (confirm) Confirm();
    if (back) Back();
}

} // namespace Oot3dNativeGame::Menu
