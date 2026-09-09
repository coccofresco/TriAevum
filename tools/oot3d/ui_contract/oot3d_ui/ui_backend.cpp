#include "oot3d_ui/ui_backend.h"

namespace oot3d::ui {

namespace {

constexpr std::size_t ToIndex(UiSubsystem subsystem) noexcept {
    return static_cast<std::size_t>(subsystem);
}

bool IsOwnerValid(UiExecutionOwner owner) noexcept {
    return owner == UiExecutionOwner::Oot3dGuest || owner == UiExecutionOwner::HostBackend;
}

constexpr std::uint8_t R(UiResponsibility responsibility) noexcept {
    return UiResponsibilityBit(responsibility);
}

constexpr std::array<UiNativeFunctionContract, kUiNativeFunctionContractCount>
    kOot3dNativeUiFunctionContracts{{
        {0x0034BE04, 40, UiSubsystem::GameplayHud, UiSemanticMechanic::HudVisibility,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x0041E968, 692, UiSubsystem::PauseShell, UiSemanticMechanic::PauseSession,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable, R(UiResponsibility::Mechanics)},
        {0x0041EC50, 668, UiSubsystem::PauseShell, UiSemanticMechanic::PauseSession,
         UiSeamPhase::Draw, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x0045598C, 972, UiSubsystem::PauseShell, UiSemanticMechanic::PauseSession,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x00469B70, 32, UiSubsystem::PauseShell, UiSemanticMechanic::PauseSession,
         UiSeamPhase::Reset, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x002EB05C, 116, UiSubsystem::Items, UiSemanticMechanic::ItemSelection,
         UiSeamPhase::Reset, UiNativeDisposition::Replaceable, R(UiResponsibility::Mechanics)},
        {0x002EBA9C, 2288, UiSubsystem::Items, UiSemanticMechanic::ItemSelection,
         UiSeamPhase::Input, UiNativeDisposition::Replaceable, R(UiResponsibility::Input)},
        {0x002EC3E4, 3576, UiSubsystem::Items, UiSemanticMechanic::ItemSelection,
         UiSeamPhase::Helper, UiNativeDisposition::Replaceable, R(UiResponsibility::Mechanics)},
        {0x004338EC, 432, UiSubsystem::Items, UiSemanticMechanic::ItemSelection,
         UiSeamPhase::Submit, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x00433AB4, 4396, UiSubsystem::Items, UiSemanticMechanic::ItemSelection,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable, R(UiResponsibility::Mechanics)},
        {0x00434C80, 232, UiSubsystem::Items, UiSemanticMechanic::ItemSelection,
         UiSeamPhase::Draw, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x004456A8, 4928, UiSubsystem::Items, UiSemanticMechanic::ArrowTypeSelection,
         UiSeamPhase::Helper, UiNativeDisposition::Replaceable, R(UiResponsibility::Mechanics)},
        {0x0046B554, 1020, UiSubsystem::Items, UiSemanticMechanic::ItemSelection,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x002E9B4C, 528, UiSubsystem::Equipment, UiSemanticMechanic::InventoryEquipment,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x0042401C, 752, UiSubsystem::Equipment, UiSemanticMechanic::EquipmentSelection,
         UiSeamPhase::Submit, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x00424324, 2152, UiSubsystem::Equipment, UiSemanticMechanic::EquipmentSelection,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable, R(UiResponsibility::Mechanics)},
        {0x00424C20, 172, UiSubsystem::Equipment, UiSemanticMechanic::EquipmentSelection,
         UiSeamPhase::Draw, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x00438740, 560, UiSubsystem::Equipment, UiSemanticMechanic::EquipmentSelection,
         UiSeamPhase::Input, UiNativeDisposition::Replaceable, R(UiResponsibility::Input)},
        {0x00449A38, 824, UiSubsystem::Equipment, UiSemanticMechanic::EquipmentSelection,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x0041DEF0, 916, UiSubsystem::QuestStatus, UiSemanticMechanic::QuestStatus,
         UiSeamPhase::Composite, UiNativeDisposition::Replaceable,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Presentation)},
        {0x0042B9F4, 1280, UiSubsystem::QuestStatus, UiSemanticMechanic::QuestStatus,
         UiSeamPhase::Draw, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x0045554C, 884, UiSubsystem::QuestStatus, UiSemanticMechanic::QuestStatus,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x0042D0E4, 2200, UiSubsystem::Map, UiSemanticMechanic::MapNavigation,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable, R(UiResponsibility::Mechanics)},
        {0x0042DA14, 188, UiSubsystem::Map, UiSemanticMechanic::MapNavigation,
         UiSeamPhase::Draw, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x00480BD8, 668, UiSubsystem::Map, UiSemanticMechanic::MapNavigation,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x00424F20, 560, UiSubsystem::SystemMenu, UiSemanticMechanic::SystemSave,
         UiSeamPhase::Submit, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x00425164, 324, UiSubsystem::SystemMenu, UiSemanticMechanic::SystemSave,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable, R(UiResponsibility::Mechanics)},
        {0x004252BC, 100, UiSubsystem::SystemMenu, UiSemanticMechanic::SystemSave,
         UiSeamPhase::Draw, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x004392C8, 6420, UiSubsystem::SystemMenu, UiSemanticMechanic::SystemSave,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x004676A4, 1340, UiSubsystem::SystemMenu, UiSemanticMechanic::SystemSave,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x0042DDA8, 384, UiSubsystem::TouchControls, UiSemanticMechanic::TouchControls,
         UiSeamPhase::Submit, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x0042DF3C, 5344, UiSubsystem::TouchControls, UiSemanticMechanic::TouchControls,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Input)},
        {0x0042F5A8, 436, UiSubsystem::TouchControls, UiSemanticMechanic::TouchControls,
         UiSeamPhase::Draw, UiNativeDisposition::Replaceable, R(UiResponsibility::Presentation)},
        {0x0046ACB8, 1012, UiSubsystem::TouchControls, UiSemanticMechanic::TouchControls,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x0042F9A8, 15504, UiSubsystem::FileSelect, UiSemanticMechanic::FileSelection,
         UiSeamPhase::Composite, UiNativeDisposition::Replaceable,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Input) |
             R(UiResponsibility::Presentation)},
        {0x0046B114, 976, UiSubsystem::FileSelect, UiSemanticMechanic::FileSelection,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x00427CE4, 3016, UiSubsystem::NameEntry, UiSemanticMechanic::NameEntry,
         UiSeamPhase::Composite, UiNativeDisposition::Replaceable,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Input) |
             R(UiResponsibility::Presentation)},
        {0x004682A0, 140, UiSubsystem::NameEntry, UiSemanticMechanic::NameEntry,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable, R(UiResponsibility::Lifecycle)},
        {0x00316CEC, 132, UiSubsystem::Items, UiSemanticMechanic::InventoryItems,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x0033187C, 52, UiSubsystem::Equipment, UiSemanticMechanic::InventoryEquipment,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x0033C1B8, 80, UiSubsystem::Items, UiSemanticMechanic::InventoryItems,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x0033C20C, 72, UiSubsystem::Items, UiSemanticMechanic::InventoryLookup,
         UiSeamPhase::Query, UiNativeDisposition::RetainQuery, R(UiResponsibility::Content)},
        {0x0033C25C, 1196, UiSubsystem::Items, UiSemanticMechanic::InventoryItems,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x0033C730, 40, UiSubsystem::Equipment, UiSemanticMechanic::InventoryUpgrades,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x00355830, 540, UiSubsystem::Items, UiSemanticMechanic::InventoryAmmo,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x0035D190, 180, UiSubsystem::Equipment, UiSemanticMechanic::InventoryEquipment,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x00377A04, 72, UiSubsystem::Items, UiSemanticMechanic::InventoryLookup,
         UiSeamPhase::Query, UiNativeDisposition::RetainQuery, R(UiResponsibility::Content)},
        {0x004C1044, 72, UiSubsystem::Items, UiSemanticMechanic::InventoryBottles,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x002D7974, 256, UiSubsystem::GameplayHud, UiSemanticMechanic::HudAlpha,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Presentation)},
        {0x002D7A78, 400, UiSubsystem::GameplayHud, UiSemanticMechanic::PlayerEnvironmentHazard,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x002D7C28, 1432, UiSubsystem::GameplayHud, UiSemanticMechanic::HudAlpha,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Presentation)},
        {0x002E2E60, 5372, UiSubsystem::GameplayHud, UiSemanticMechanic::GameplayFrame,
         UiSeamPhase::Update, UiNativeDisposition::RetainOrderingOwner,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::GameStateAction)},
        {0x00353998, 60, UiSubsystem::GameplayHud, UiSemanticMechanic::HudMagic,
         UiSeamPhase::Action, UiNativeDisposition::RetainActionSink,
         R(UiResponsibility::GameStateAction)},
        {0x0044E0D4, 452, UiSubsystem::GameplayHud, UiSemanticMechanic::GameplayFrame,
         UiSeamPhase::Initialize, UiNativeDisposition::RetainOrderingOwner,
         R(UiResponsibility::Lifecycle)},
        {0x00457318, 488, UiSubsystem::GameplayHud, UiSemanticMechanic::HudMinimap,
         UiSeamPhase::Initialize, UiNativeDisposition::RetainComposite,
         R(UiResponsibility::Lifecycle) | R(UiResponsibility::GameStateAction)},
        {0x004596D0, 692, UiSubsystem::GameplayHud, UiSemanticMechanic::HudMagic,
         UiSeamPhase::Composite, UiNativeDisposition::RetainComposite,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Presentation) |
             R(UiResponsibility::GameStateAction)},
        {0x0045ACCC, 2472, UiSubsystem::GameplayHud, UiSemanticMechanic::GameplayFrame,
         UiSeamPhase::Composite, UiNativeDisposition::RetainComposite,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::GameStateAction)},
        {0x0046345C, 188, UiSubsystem::GameplayHud, UiSemanticMechanic::HudHealth,
         UiSeamPhase::Initialize, UiNativeDisposition::Replaceable,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Lifecycle)},
        {0x00470DF0, 640, UiSubsystem::GameplayHud, UiSemanticMechanic::HudMinimap,
         UiSeamPhase::Update, UiNativeDisposition::RetainComposite,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::GameStateAction)},
        {0x004715C0, 528, UiSubsystem::GameplayHud, UiSemanticMechanic::HudHealth,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Presentation)},
        {0x00475CA8, 4236, UiSubsystem::GameplayHud, UiSemanticMechanic::HudButtons,
         UiSeamPhase::Update, UiNativeDisposition::RetainComposite,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::GameStateAction)},
        {0x00476E1C, 1528, UiSubsystem::GameplayHud, UiSemanticMechanic::HudMagic,
         UiSeamPhase::Update, UiNativeDisposition::RetainComposite,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Presentation) |
             R(UiResponsibility::GameStateAction)},
        {0x0047A940, 760, UiSubsystem::GameplayHud, UiSemanticMechanic::HudHealth,
         UiSeamPhase::Update, UiNativeDisposition::Replaceable,
         R(UiResponsibility::Mechanics) | R(UiResponsibility::Presentation)},
    }};

constexpr std::uint32_t ContractBytes() noexcept {
    std::uint32_t bytes = 0;
    for (const auto& contract : kOot3dNativeUiFunctionContracts) {
        bytes += contract.guest_size;
    }
    return bytes;
}

constexpr std::size_t ContractDispositionCount(UiNativeDisposition disposition) noexcept {
    std::size_t count = 0;
    for (const auto& contract : kOot3dNativeUiFunctionContracts) {
        if (contract.disposition == disposition) {
            ++count;
        }
    }
    return count;
}

constexpr bool ContractEntriesAreUnique() noexcept {
    for (std::size_t left = 0; left < kOot3dNativeUiFunctionContracts.size(); ++left) {
        for (std::size_t right = left + 1; right < kOot3dNativeUiFunctionContracts.size(); ++right) {
            if (kOot3dNativeUiFunctionContracts[left].guest_entry ==
                kOot3dNativeUiFunctionContracts[right].guest_entry) {
                return false;
            }
        }
    }
    return true;
}

static_assert(ContractBytes() == 88012, "native UI contract byte coverage drifted");
static_assert(ContractDispositionCount(UiNativeDisposition::Replaceable) == 40,
              "replaceable native UI seam count drifted");
static_assert(ContractDispositionCount(UiNativeDisposition::RetainActionSink) == 13,
              "native UI action-sink count drifted");
static_assert(ContractDispositionCount(UiNativeDisposition::RetainQuery) == 2,
              "native UI query count drifted");
static_assert(ContractDispositionCount(UiNativeDisposition::RetainComposite) == 6,
              "native UI retained-composite count drifted");
static_assert(ContractDispositionCount(UiNativeDisposition::RetainOrderingOwner) == 2,
              "native UI ordering-owner count drifted");
static_assert(ContractEntriesAreUnique(), "native UI contract entries must be unique");
static_assert(std::variant_size_v<UiBackendIntent> ==
              static_cast<std::size_t>(UiBackendIntentKind::Count));

} // namespace

const std::array<UiNativeFunctionContract, kUiNativeFunctionContractCount>&
Oot3dNativeUiFunctionContracts() noexcept {
    return kOot3dNativeUiFunctionContracts;
}

UiBackendProfile BuildOot3dNativeUiProfile() {
    UiBackendProfile profile;
    profile.id = "oot3d-native";
    for (std::size_t index = 0; index < profile.routes.size(); ++index) {
        profile.routes[index].subsystem = static_cast<UiSubsystem>(index);
    }
    return profile;
}

UiFramePlan BuildFramePlan(const UiBackendProfile& profile, UiSubsystem subsystem) noexcept {
    const std::size_t index = ToIndex(subsystem);
    if (index >= profile.routes.size()) {
        return {};
    }

    const UiSubsystemRoute& route = profile.routes[index];
    UiFramePlan plan;
    plan.use_guest_content = route.content == UiExecutionOwner::Oot3dGuest;
    plan.run_guest_mechanics = route.mechanics == UiExecutionOwner::Oot3dGuest;
    plan.forward_input_to_guest = route.input == UiExecutionOwner::Oot3dGuest;
    plan.run_guest_presentation = route.presentation == UiExecutionOwner::Oot3dGuest;
    plan.run_host_content = route.content == UiExecutionOwner::HostBackend;
    plan.run_host_mechanics = route.mechanics == UiExecutionOwner::HostBackend;
    plan.route_input_to_host = route.input == UiExecutionOwner::HostBackend;
    plan.run_host_presentation = route.presentation == UiExecutionOwner::HostBackend;
    return plan;
}

bool ValidateUiBackendProfile(const UiBackendProfile& profile, std::string* error) {
    if (profile.id.empty()) {
        if (error != nullptr) {
            *error = "UI backend profile id is required";
        }
        return false;
    }

    for (std::size_t index = 0; index < profile.routes.size(); ++index) {
        const UiSubsystemRoute& route = profile.routes[index];
        if (ToIndex(route.subsystem) != index) {
            if (error != nullptr) {
                *error = "UI backend routes must contain every subsystem in canonical order";
            }
            return false;
        }
        if (!IsOwnerValid(route.content) || !IsOwnerValid(route.mechanics) ||
            !IsOwnerValid(route.input) || !IsOwnerValid(route.presentation)) {
            if (error != nullptr) {
                *error = "UI backend route has an invalid execution owner";
            }
            return false;
        }
    }
    return true;
}

bool HasUiBackendIntent(const UiBackendIntent& intent) noexcept {
    return !std::holds_alternative<std::monostate>(intent);
}

UiBackendIntentKind UiBackendIntentKindOf(const UiBackendIntent& intent) noexcept {
    return static_cast<UiBackendIntentKind>(intent.index());
}

const char* UiSubsystemName(UiSubsystem subsystem) noexcept {
    switch (subsystem) {
    case UiSubsystem::GameplayHud:
        return "gameplay_hud";
    case UiSubsystem::PauseShell:
        return "pause_shell";
    case UiSubsystem::Items:
        return "items";
    case UiSubsystem::Equipment:
        return "equipment";
    case UiSubsystem::QuestStatus:
        return "quest_status";
    case UiSubsystem::Map:
        return "map";
    case UiSubsystem::SystemMenu:
        return "system_menu";
    case UiSubsystem::FileSelect:
        return "file_select";
    case UiSubsystem::NameEntry:
        return "name_entry";
    case UiSubsystem::TouchControls:
        return "touch_controls";
    case UiSubsystem::Count:
        return "invalid";
    }
    return "invalid";
}

const char* UiSemanticMechanicName(UiSemanticMechanic mechanic) noexcept {
    switch (mechanic) {
    case UiSemanticMechanic::HudVisibility:
        return "hud_visibility";
    case UiSemanticMechanic::HudAlpha:
        return "hud_alpha";
    case UiSemanticMechanic::PauseSession:
        return "pause_session";
    case UiSemanticMechanic::ItemSelection:
        return "item_selection";
    case UiSemanticMechanic::ItemAssignment:
        return "item_assignment";
    case UiSemanticMechanic::ArrowTypeSelection:
        return "arrow_type_selection";
    case UiSemanticMechanic::EquipmentSelection:
        return "equipment_selection";
    case UiSemanticMechanic::QuestStatus:
        return "quest_status";
    case UiSemanticMechanic::MapNavigation:
        return "map_navigation";
    case UiSemanticMechanic::SystemSave:
        return "system_save";
    case UiSemanticMechanic::TouchControls:
        return "touch_controls";
    case UiSemanticMechanic::FileSelection:
        return "file_selection";
    case UiSemanticMechanic::NameEntry:
        return "name_entry";
    case UiSemanticMechanic::InventoryItems:
        return "inventory_items";
    case UiSemanticMechanic::InventoryEquipment:
        return "inventory_equipment";
    case UiSemanticMechanic::InventoryUpgrades:
        return "inventory_upgrades";
    case UiSemanticMechanic::InventoryAmmo:
        return "inventory_ammo";
    case UiSemanticMechanic::InventoryBottles:
        return "inventory_bottles";
    case UiSemanticMechanic::InventoryLookup:
        return "inventory_lookup";
    case UiSemanticMechanic::HudButtons:
        return "hud_buttons";
    case UiSemanticMechanic::HudHealth:
        return "hud_health";
    case UiSemanticMechanic::HudMagic:
        return "hud_magic";
    case UiSemanticMechanic::HudMinimap:
        return "hud_minimap";
    case UiSemanticMechanic::HudActionLabel:
        return "hud_action_label";
    case UiSemanticMechanic::HudContextPrompt:
        return "hud_context_prompt";
    case UiSemanticMechanic::PlayerEnvironmentHazard:
        return "player_environment_hazard";
    case UiSemanticMechanic::GameplayFrame:
        return "gameplay_frame";
    case UiSemanticMechanic::Count:
        return "invalid";
    }
    return "invalid";
}

const char* UiSeamPhaseName(UiSeamPhase phase) noexcept {
    switch (phase) {
    case UiSeamPhase::Update:
        return "update";
    case UiSeamPhase::Draw:
        return "draw";
    case UiSeamPhase::Submit:
        return "submit";
    case UiSeamPhase::Initialize:
        return "initialize";
    case UiSeamPhase::Reset:
        return "reset";
    case UiSeamPhase::Input:
        return "input";
    case UiSeamPhase::Composite:
        return "composite";
    case UiSeamPhase::Helper:
        return "helper";
    case UiSeamPhase::Action:
        return "action";
    case UiSeamPhase::Query:
        return "query";
    }
    return "invalid";
}

const char* UiNativeDispositionName(UiNativeDisposition disposition) noexcept {
    switch (disposition) {
    case UiNativeDisposition::Replaceable:
        return "replaceable";
    case UiNativeDisposition::RetainActionSink:
        return "retain_action_sink";
    case UiNativeDisposition::RetainQuery:
        return "retain_query";
    case UiNativeDisposition::RetainComposite:
        return "retain_composite";
    case UiNativeDisposition::RetainOrderingOwner:
        return "retain_ordering_owner";
    }
    return "invalid";
}

Oot3dNativeUiBackend::Oot3dNativeUiBackend() : profile_(BuildOot3dNativeUiProfile()) {}

const UiBackendProfile& Oot3dNativeUiBackend::Profile() const noexcept {
    return profile_;
}

UiFramePlan Oot3dNativeUiBackend::PlanFrame(UiSubsystem subsystem) const noexcept {
    return BuildFramePlan(profile_, subsystem);
}

void Oot3dNativeUiBackend::ObserveState(const UiBackendStateView&) noexcept {}

UiBackendIntent Oot3dNativeUiBackend::HandleInput(const UiInputEvent&) noexcept {
    return {};
}

void Oot3dNativeUiBackend::AppendPresentation(UiSubsystem, const UiBackendStateView&,
                                               std::vector<UiPrimitive>&) const {}

UiRuntimeRouter::UiRuntimeRouter(UiBackend& backend) noexcept : backend_(backend) {}

UiFramePlan UiRuntimeRouter::PlanFrame(UiSubsystem subsystem) const noexcept {
    return backend_.PlanFrame(subsystem);
}

void UiRuntimeRouter::ObserveState(const Oot3dUiSemanticState& state) noexcept {
    const UiBackendStateView view(state);
    backend_.ObserveState(view);
}

UiInputRouteResult UiRuntimeRouter::RouteInput(const UiInputEvent& input) noexcept {
    UiInputRouteResult result;
    const UiFramePlan plan = backend_.PlanFrame(input.subsystem);
    result.forward_to_guest = plan.forward_input_to_guest;
    if (!plan.route_input_to_host) {
        return result;
    }
    result.host_intent = backend_.HandleInput(input);
    result.has_host_intent = HasUiBackendIntent(result.host_intent);
    return result;
}

std::vector<UiPrimitive> UiRuntimeRouter::BuildPresentation(
    UiSubsystem subsystem, const Oot3dUiSemanticState& state) const {
    std::vector<UiPrimitive> output;
    const UiFramePlan plan = backend_.PlanFrame(subsystem);
    if (plan.run_host_presentation) {
        const UiBackendStateView view(state);
        backend_.AppendPresentation(subsystem, view, output);
    }
    return output;
}

} // namespace oot3d::ui
