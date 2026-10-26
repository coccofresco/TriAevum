#pragma once

#include "fast/extension/graphics_settings.h"

#include <cstdint>

namespace Fast::Extension {

struct PresentationSettingsValue {
    WindowMode Window = WindowMode::Windowed;
    uint32_t Width = 1280;
    uint32_t Height = 720;
    bool VSync = true;
    // A default output request opens at the default fit of the screen
    // (fast/WindowExtentPolicy.h); a rollback restores it with the size.
    WindowExtentProvenance Provenance = WindowExtentProvenance::Default;

    bool operator==(const PresentationSettingsValue&) const = default;
};

[[nodiscard]] PresentationSettingsValue GetPresentationSettings(
    const GraphicsSettings& settings);
void SetPresentationSettings(
    GraphicsSettings& settings,
    const PresentationSettingsValue& presentation);
[[nodiscard]] bool RequiresPresentationConfirmation(
    const PresentationSettingsValue& currentApplied,
    const PresentationSettingsValue& requested);
// The display a renderer reports applied: the mode, size and VSync. The
// provenance is the request's own attribute, which no display shows.
[[nodiscard]] constexpr bool SamePresentedDisplay(const PresentationSettingsValue& left,
                                                  const PresentationSettingsValue& right) noexcept {
    return left.Window == right.Window && left.Width == right.Width && left.Height == right.Height &&
           left.VSync == right.VSync;
}

enum class PresentationTransactionPhase : uint8_t {
    Idle,
    ApplyRequested,
    AwaitingConfirmation,
    RollbackRequested,
};

struct PresentationTransactionStatus {
    PresentationTransactionPhase Phase =
        PresentationTransactionPhase::Idle;
    PresentationSettingsValue Requested;
    PresentationSettingsValue LastKnownGood;
    uint32_t RemainingMilliseconds = 0;

    [[nodiscard]] bool Active() const {
        return Phase != PresentationTransactionPhase::Idle;
    }
};

class PresentationSettingsTransaction final {
  public:
    explicit PresentationSettingsTransaction(
        uint32_t confirmationTimeoutMilliseconds = 15000U);

    bool Begin(const PresentationSettingsValue& currentApplied,
               const PresentationSettingsValue& requested);
    bool MarkApplied(const PresentationSettingsValue& applied,
                     uint64_t nowMilliseconds, bool waitForVisibleConfirmation = false);
    bool ConfirmationVisible(uint64_t nowMilliseconds);
    bool Confirm();
    bool RequestRollback();
    bool Advance(uint64_t nowMilliseconds);

    [[nodiscard]] PresentationTransactionStatus Status(
        uint64_t nowMilliseconds) const;
    [[nodiscard]] const PresentationSettingsValue& LastKnownGood() const;
    [[nodiscard]] const PresentationSettingsValue& Target() const;

  private:
    void CompleteRollback();

    uint32_t mConfirmationTimeoutMilliseconds = 15000U;
    PresentationTransactionPhase mPhase =
        PresentationTransactionPhase::Idle;
    PresentationSettingsValue mRequested;
    PresentationSettingsValue mLastKnownGood;
    uint64_t mDeadlineMilliseconds = 0;
};

} // namespace Fast::Extension
