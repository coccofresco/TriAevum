#pragma once

#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

enum class UiSubsystem : std::uint8_t {
    GameplayHud,
    PauseShell,
    Items,
    Equipment,
    QuestStatus,
    Map,
    SystemMenu,
    FileSelect,
    NameEntry,
    TouchControls,
    Count,
};

inline constexpr std::size_t kUiSubsystemCount =
    static_cast<std::size_t>(UiSubsystem::Count);

// Backend-neutral meaning of each native OoT3D UI function. N64 source names
// stay in analysis catalogs; runtime contracts depend only on verified meaning.
enum class UiSemanticMechanic : std::uint8_t {
    HudVisibility,
    HudAlpha,
    PauseSession,
    ItemSelection,
    ItemAssignment,
    ArrowTypeSelection,
    EquipmentSelection,
    QuestStatus,
    MapNavigation,
    SystemSave,
    TouchControls,
    FileSelection,
    NameEntry,
    InventoryItems,
    InventoryEquipment,
    InventoryUpgrades,
    InventoryAmmo,
    InventoryBottles,
    InventoryLookup,
    HudButtons,
    HudHealth,
    HudMagic,
    HudMinimap,
    HudActionLabel,
    HudContextPrompt,
    PlayerEnvironmentHazard,
    GameplayFrame,
    Count,
};

} // namespace oot3d::ui
