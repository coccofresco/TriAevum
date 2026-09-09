#pragma once

#include "oot3d_ui/ui_backend_state.h"
#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_ui/ui_frontend_actions.h"
#include "oot3d_ui/ui_native_actions.h"
#include "oot3d_ui/ui_primitives.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace oot3d::ui {

enum class UiSeamPhase : std::uint8_t {
    Update,
    Draw,
    Submit,
    Initialize,
    Reset,
    Input,
    Composite,
    Helper,
    Action,
    Query,
};

enum class UiNativeDisposition : std::uint8_t {
    Replaceable,
    RetainActionSink,
    RetainQuery,
    RetainComposite,
    RetainOrderingOwner,
};

enum class UiResponsibility : std::uint8_t {
    Content = 1U << 0U,
    Mechanics = 1U << 1U,
    Input = 1U << 2U,
    Presentation = 1U << 3U,
    Lifecycle = 1U << 4U,
    GameStateAction = 1U << 5U,
};

constexpr std::uint8_t UiResponsibilityBit(UiResponsibility responsibility) noexcept {
    return static_cast<std::uint8_t>(responsibility);
}

struct UiNativeFunctionContract {
    std::uint32_t guest_entry = 0;
    std::uint32_t guest_size = 0;
    UiSubsystem subsystem = UiSubsystem::GameplayHud;
    UiSemanticMechanic mechanic = UiSemanticMechanic::HudVisibility;
    UiSeamPhase phase = UiSeamPhase::Update;
    UiNativeDisposition disposition = UiNativeDisposition::Replaceable;
    std::uint8_t responsibilities = 0;
};

inline constexpr std::size_t kUiNativeFunctionContractCount = 63;

enum class UiExecutionOwner : std::uint8_t {
    Oot3dGuest,
    HostBackend,
};

struct UiSubsystemRoute {
    UiSubsystem subsystem = UiSubsystem::GameplayHud;
    UiExecutionOwner content = UiExecutionOwner::Oot3dGuest;
    UiExecutionOwner mechanics = UiExecutionOwner::Oot3dGuest;
    UiExecutionOwner input = UiExecutionOwner::Oot3dGuest;
    UiExecutionOwner presentation = UiExecutionOwner::Oot3dGuest;
};

struct UiBackendProfile {
    std::string id;
    std::array<UiSubsystemRoute, kUiSubsystemCount> routes;
};

struct UiFramePlan {
    bool use_guest_content = true;
    bool run_guest_mechanics = true;
    bool forward_input_to_guest = true;
    bool run_guest_presentation = true;
    bool run_host_content = false;
    bool run_host_mechanics = false;
    bool route_input_to_host = false;
    bool run_host_presentation = false;
};

enum class UiInputKind : std::uint8_t {
    None,
    NavigateLeft,
    NavigateRight,
    NavigateUp,
    NavigateDown,
    Confirm,
    Cancel,
    Touch,
};

struct UiInputEvent {
    UiSubsystem subsystem = UiSubsystem::GameplayHud;
    UiInputKind kind = UiInputKind::None;
    std::int32_t x = 0;
    std::int32_t y = 0;
};

enum class UiBackendIntentKind : std::uint8_t {
    None,
    ClosePause,
    OpenPage,
    MoveFocus,
    NativeActionRequest,
    FrontendNativeActionRequest,
    Count,
};

struct ClosePauseIntent {};

struct OpenPausePageIntent {
    PausePage page = PausePage::None;
};

struct MoveUiFocusIntent {
    UiSubsystem subsystem = UiSubsystem::GameplayHud;
    std::int32_t column_delta = 0;
    std::int32_t row_delta = 0;
};

using UiBackendIntent = std::variant<
    std::monostate,
    ClosePauseIntent,
    OpenPausePageIntent,
    MoveUiFocusIntent,
    UiBackendNativeActionRequest,
    UiFrontendNativeActionRequest>;

struct UiInputRouteResult {
    bool forward_to_guest = true;
    bool has_host_intent = false;
    UiBackendIntent host_intent;
};

class UiBackend {
public:
    virtual ~UiBackend() = default;

    virtual const UiBackendProfile& Profile() const noexcept = 0;
    virtual UiFramePlan PlanFrame(UiSubsystem subsystem) const noexcept = 0;
    virtual void ObserveState(const UiBackendStateView& state) noexcept = 0;
    virtual UiBackendIntent HandleInput(const UiInputEvent& input) noexcept = 0;
    virtual void AppendPresentation(UiSubsystem subsystem, const UiBackendStateView& state,
                                    std::vector<UiPrimitive>& output) const = 0;
};

// Identity adapter for the current game. It never consumes host input, emits
// host presentation, or suppresses an OoT3D update/draw path.
class Oot3dNativeUiBackend final : public UiBackend {
public:
    Oot3dNativeUiBackend();

    const UiBackendProfile& Profile() const noexcept override;
    UiFramePlan PlanFrame(UiSubsystem subsystem) const noexcept override;
    void ObserveState(const UiBackendStateView& state) noexcept override;
    UiBackendIntent HandleInput(const UiInputEvent& input) noexcept override;
    void AppendPresentation(UiSubsystem subsystem, const UiBackendStateView& state,
                            std::vector<UiPrimitive>& output) const override;

private:
    UiBackendProfile profile_;
};

class UiRuntimeRouter {
public:
    explicit UiRuntimeRouter(UiBackend& backend) noexcept;

    UiFramePlan PlanFrame(UiSubsystem subsystem) const noexcept;
    void ObserveState(const Oot3dUiSemanticState& state) noexcept;
    UiInputRouteResult RouteInput(const UiInputEvent& input) noexcept;
    std::vector<UiPrimitive> BuildPresentation(UiSubsystem subsystem,
                                               const Oot3dUiSemanticState& state) const;

private:
    UiBackend& backend_;
};

UiBackendProfile BuildOot3dNativeUiProfile();
UiFramePlan BuildFramePlan(const UiBackendProfile& profile, UiSubsystem subsystem) noexcept;
bool ValidateUiBackendProfile(const UiBackendProfile& profile, std::string* error);
bool HasUiBackendIntent(const UiBackendIntent& intent) noexcept;
UiBackendIntentKind UiBackendIntentKindOf(const UiBackendIntent& intent) noexcept;
const std::array<UiNativeFunctionContract, kUiNativeFunctionContractCount>&
Oot3dNativeUiFunctionContracts() noexcept;
const char* UiSubsystemName(UiSubsystem subsystem) noexcept;
const char* UiSemanticMechanicName(UiSemanticMechanic mechanic) noexcept;
const char* UiSeamPhaseName(UiSeamPhase phase) noexcept;
const char* UiNativeDispositionName(UiNativeDisposition disposition) noexcept;

} // namespace oot3d::ui
