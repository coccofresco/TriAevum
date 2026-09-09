#pragma once

#include "oot3d_ui/ui_value_domains.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

enum class Oot3dPauseGearPageState : std::int32_t {
    Inactive = 0,
    AwaitTouchRelease,
    BrowseSlots,
    PrepareEquipAnimation,
    AnimateEquipCommit,
    TouchOpenRequestA,
    TouchItemsPageTransition,
    TouchPauseShellTransition,
    TouchOpenRequestB,
    AnimateTransitionFromHidden,
    AnimateTransitionToHidden,
};

enum class Oot3dPauseGearSlot : std::int32_t {
    KokiriSword = 0,
    MasterSword,
    BiggoronSword,
    DekuShield,
    HylianShield,
    MirrorShield,
    KokiriTunic,
    GoronTunic,
    ZoraTunic,
    ForestMedallion,
    FireMedallion,
    WaterMedallion,
    SpiritMedallion,
    ShadowMedallion,
    LightMedallion,
    KokiriEmerald,
    GoronRuby,
    ZoraSapphire,
    Ocarina,
    StoneOfAgony,
    GerudoCard,
    GoldSkulltulaTokens,
    ProjectileCapacity,
    BombBag,
    Strength,
    Scale,
    HeartPieces,
};

enum class Oot3dPauseGearSlotRole : std::uint8_t {
    Equipment = 0,
    QuestItem,
    Ocarina,
    TokenCount,
    AgeDependentProjectileUpgrade,
    Upgrade,
    HeartPieceCount,
};

enum class Oot3dPauseGearAgeRequirement : std::int8_t {
    Any = -1,
    Adult = 0,
    Child = 1,
};

enum class Oot3dPauseGearFieldEncoding : std::uint8_t {
    S32 = 0,
    ItemIdS32,
    Bool32,
};

enum class Oot3dPauseGearMutationKind : std::uint8_t {
    ChangeEquipment = 0,
    RefreshPlayerEquipment,
    SynchronizePauseInventory,
};

struct Oot3dPauseGearFunctionDescriptor {
    std::uint32_t entry = 0;
    std::uint32_t body_size = 0;
    const char* name = nullptr;
    const char* responsibility = nullptr;
    const char* n64_correspondence = nullptr;
};

struct Oot3dPauseGearPageStateDescriptor {
    Oot3dPauseGearPageState state = Oot3dPauseGearPageState::Inactive;
    std::uint32_t handler_address = 0;
    const char* name = nullptr;
    const char* semantic_effect = nullptr;
    const char* replacement_obligation = nullptr;
};

struct Oot3dPauseGearSlotDescriptor {
    Oot3dPauseGearSlot slot = Oot3dPauseGearSlot::KokiriSword;
    Oot3dPauseGearSlotRole role = Oot3dPauseGearSlotRole::Equipment;
    ItemId base_item = ItemId::ITEM_SWORD_KOKIRI;
    std::int8_t equipment_category = -1;
    std::uint8_t equipment_value = 0;
    std::int8_t quest_bit = -1;
    std::int8_t upgrade_index = -1;
    Oot3dPauseGearAgeRequirement age_requirement = Oot3dPauseGearAgeRequirement::Any;
    bool directly_equippable = false;
    const char* name = nullptr;
    const char* semantic_effect = nullptr;
    const char* n64_correspondence = nullptr;
};

struct Oot3dPauseGearFieldDescriptor {
    std::uint16_t offset = 0;
    Oot3dPauseGearFieldEncoding encoding = Oot3dPauseGearFieldEncoding::S32;
    const char* name = nullptr;
    std::uint32_t proof_site = 0;
    std::uint32_t proof_word = 0;
    const char* semantic_effect = nullptr;
};

struct Oot3dPauseGearMutationDescriptor {
    std::uint32_t site = 0;
    std::uint32_t instruction_word = 0;
    Oot3dPauseGearMutationKind kind = Oot3dPauseGearMutationKind::ChangeEquipment;
    const char* semantic_target = nullptr;
    const char* condition = nullptr;
    const char* replacement_obligation = nullptr;
};

inline constexpr std::uint32_t kOot3dPauseGearStateAddress = 0x0050446C;
inline constexpr std::size_t kOot3dPauseGearFunctionCount = 13;
inline constexpr std::size_t kOot3dPauseGearPageStateCount = 11;
inline constexpr std::size_t kOot3dPauseGearSlotCount = 27;
inline constexpr std::size_t kOot3dPauseGearFieldCount = 13;
inline constexpr std::size_t kOot3dPauseGearMutationCount = 9;

const std::array<Oot3dPauseGearFunctionDescriptor, kOot3dPauseGearFunctionCount>&
Oot3dPauseGearFunctions() noexcept;
const Oot3dPauseGearFunctionDescriptor* Oot3dPauseGearFunction(
    std::uint32_t entry) noexcept;
const std::array<Oot3dPauseGearPageStateDescriptor, kOot3dPauseGearPageStateCount>&
Oot3dPauseGearPageStates() noexcept;
const Oot3dPauseGearPageStateDescriptor* Oot3dPauseGearPageStateInfo(
    Oot3dPauseGearPageState state) noexcept;
const std::array<Oot3dPauseGearSlotDescriptor, kOot3dPauseGearSlotCount>&
Oot3dPauseGearSlots() noexcept;
const Oot3dPauseGearSlotDescriptor* Oot3dPauseGearSlotInfo(
    Oot3dPauseGearSlot slot) noexcept;
const std::array<Oot3dPauseGearFieldDescriptor, kOot3dPauseGearFieldCount>&
Oot3dPauseGearFields() noexcept;
const Oot3dPauseGearFieldDescriptor* Oot3dPauseGearField(
    std::uint16_t offset) noexcept;
const std::array<Oot3dPauseGearMutationDescriptor, kOot3dPauseGearMutationCount>&
Oot3dPauseGearMutations() noexcept;

} // namespace oot3d::ui
