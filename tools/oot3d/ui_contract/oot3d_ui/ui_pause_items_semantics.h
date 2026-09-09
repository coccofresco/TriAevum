#pragma once

#include "oot3d_ui/ui_value_domains.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

enum class Oot3dPauseItemsPageState : std::int32_t {
    Inactive = 0,
    AwaitTouchRelease,
    BrowseGrid,
    TouchOpenRequestA,
    TouchGearPageTransition,
    TouchPauseShellTransition,
    TouchOpenRequestB,
    PrepareDragIcon,
    SelectDropTarget,
    AnimateGridSwap,
    CommitGridLayoutSwap,
    AnimateSpecialSlotSwap,
    ArrowTypeSelector,
    AnimatePageTransition,
    AnimateItemsPageOpen,
};

enum class Oot3dArrowTypeSelectorState : std::int32_t {
    Inactive = 0,
    Initialize,
    Browse,
    CommitAnimation,
    SettleAnimation,
};

enum class Oot3dArrowTypeChoice : std::int32_t {
    Bow = 0,
    FireArrow,
    IceArrow,
    LightArrow,
};

enum class Oot3dPauseItemsFieldEncoding : std::uint8_t {
    S32 = 0,
    Pointer32,
    ItemIdS32,
};

enum class Oot3dPauseItemsMutationKind : std::uint8_t {
    ChangeEquipment = 0,
    RefreshPlayerEquipment,
    WriteChildGridMapping,
    WriteAdultGridMapping,
    WriteBowInventoryItem,
    WriteBowInventoryCache,
    SyncPauseInventory,
};

struct Oot3dPauseItemsFunctionDescriptor {
    std::uint32_t entry = 0;
    std::uint32_t body_size = 0;
    const char* name = nullptr;
    const char* responsibility = nullptr;
    const char* n64_correspondence = nullptr;
};

struct Oot3dPauseItemsPageStateDescriptor {
    Oot3dPauseItemsPageState state = Oot3dPauseItemsPageState::Inactive;
    std::uint32_t handler_address = 0;
    const char* name = nullptr;
    const char* semantic_effect = nullptr;
    const char* replacement_obligation = nullptr;
};

struct Oot3dArrowTypeSelectorStateDescriptor {
    Oot3dArrowTypeSelectorState state = Oot3dArrowTypeSelectorState::Inactive;
    std::uint32_t handler_address = 0;
    const char* name = nullptr;
    const char* semantic_effect = nullptr;
};

struct Oot3dArrowTypeChoiceDescriptor {
    Oot3dArrowTypeChoice choice = Oot3dArrowTypeChoice::Bow;
    ItemId committed_item = ItemId::ITEM_BOW;
    std::uint8_t native_icon_selector = 0;
    bool icon_selector_is_item_id = false;
    std::uint16_t availability_word_offset = 0xFFFF;
    std::uint32_t value_proof_site = 0;
    std::uint32_t value_proof_word = 0;
    const char* name = nullptr;
    const char* semantic_effect = nullptr;

    constexpr bool RequiresAvailabilityWord() const noexcept {
        return availability_word_offset != 0xFFFF;
    }
};

struct Oot3dPauseItemsFieldDescriptor {
    std::uint16_t offset = 0;
    Oot3dPauseItemsFieldEncoding encoding = Oot3dPauseItemsFieldEncoding::S32;
    const char* name = nullptr;
    std::uint32_t proof_site = 0;
    std::uint32_t proof_word = 0;
    const char* semantic_effect = nullptr;
};

struct Oot3dPauseItemsMutationDescriptor {
    std::uint32_t site = 0;
    std::uint32_t instruction_word = 0;
    Oot3dPauseItemsMutationKind kind = Oot3dPauseItemsMutationKind::ChangeEquipment;
    const char* semantic_target = nullptr;
    const char* condition = nullptr;
    const char* replacement_obligation = nullptr;
};

inline constexpr std::uint32_t kOot3dPauseItemsStateAddress = 0x005066F8;
inline constexpr std::uint32_t kOot3dArrowAvailabilityStateAddress = 0x0055B628;
inline constexpr std::size_t kOot3dPauseItemsFunctionCount = 8;
inline constexpr std::size_t kOot3dPauseItemsPageStateCount = 15;
inline constexpr std::size_t kOot3dArrowTypeSelectorStateCount = 5;
inline constexpr std::size_t kOot3dArrowTypeChoiceCount = 4;
inline constexpr std::size_t kOot3dPauseItemsFieldCount = 25;
inline constexpr std::size_t kOot3dPauseItemsMutationCount = 16;

const std::array<Oot3dPauseItemsFunctionDescriptor, kOot3dPauseItemsFunctionCount>&
Oot3dPauseItemsFunctions() noexcept;
const Oot3dPauseItemsFunctionDescriptor* Oot3dPauseItemsFunction(std::uint32_t entry) noexcept;
const std::array<Oot3dPauseItemsPageStateDescriptor, kOot3dPauseItemsPageStateCount>&
Oot3dPauseItemsPageStates() noexcept;
const Oot3dPauseItemsPageStateDescriptor* Oot3dPauseItemsPageStateInfo(
    Oot3dPauseItemsPageState state) noexcept;
const std::array<Oot3dArrowTypeSelectorStateDescriptor, kOot3dArrowTypeSelectorStateCount>&
Oot3dArrowTypeSelectorStates() noexcept;
const Oot3dArrowTypeSelectorStateDescriptor* Oot3dArrowTypeSelectorStateInfo(
    Oot3dArrowTypeSelectorState state) noexcept;
const std::array<Oot3dArrowTypeChoiceDescriptor, kOot3dArrowTypeChoiceCount>&
Oot3dArrowTypeChoices() noexcept;
const Oot3dArrowTypeChoiceDescriptor* Oot3dArrowTypeChoiceInfo(
    Oot3dArrowTypeChoice choice) noexcept;
const std::array<Oot3dPauseItemsFieldDescriptor, kOot3dPauseItemsFieldCount>&
Oot3dPauseItemsFields() noexcept;
const Oot3dPauseItemsFieldDescriptor* Oot3dPauseItemsField(std::uint16_t offset) noexcept;
const std::array<Oot3dPauseItemsMutationDescriptor, kOot3dPauseItemsMutationCount>&
Oot3dPauseItemsMutations() noexcept;

} // namespace oot3d::ui
