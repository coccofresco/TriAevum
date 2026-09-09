#pragma once

#include "oot3d_ui/ui_value_domains.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

enum class Oot3dHudControlLane : std::uint8_t { B = 0, Y, X, I, II, A };
enum class Oot3dButtonRestrictionMatcher : std::uint8_t {
    BButtonPolicy = 0, InclusiveRange, ExactSet, AllOther,
};
enum class Oot3dMagicPhase : std::uint8_t {
    Idle = 0, Consume, BorderFlash, Reset, Lens, Capacity, Fill, Add,
};
enum class Oot3dInlineFieldOwner : std::uint8_t {
    SaveContext = 0, MagicBorderRuntime, PlayState,
};
enum class Oot3dInlineFieldEncoding : std::uint8_t { U8 = 0, S8, U16, S16 };

struct Oot3dHudControlLaneDescriptor {
    Oot3dHudControlLane lane = Oot3dHudControlLane::B;
    std::uint16_t item_offset = 0xFFFF;
    std::uint16_t status_offset = 0;
    std::uint32_t item_proof_site = 0;
    std::uint32_t item_proof_word = 0;
    std::uint32_t status_proof_site = 0;
    std::uint32_t status_proof_word = 0;

    constexpr bool HasAssignableItem() const noexcept { return item_offset != 0xFFFF; }
    constexpr std::uint8_t Mask() const noexcept {
        return static_cast<std::uint8_t>(1u << static_cast<std::uint8_t>(lane));
    }
};

struct Oot3dButtonRestrictionRuleDescriptor {
    const char* name = nullptr;
    std::uint16_t play_offset = 0;
    std::uint32_t proof_site = 0;
    std::uint32_t proof_word = 0;
    Oot3dButtonRestrictionMatcher matcher = Oot3dButtonRestrictionMatcher::ExactSet;
    ItemId first_item = ItemId::ITEM_NONE;
    ItemId last_item = ItemId::ITEM_NONE;
    std::array<ItemId, 2> exact_items{ItemId::ITEM_NONE, ItemId::ITEM_NONE};
    std::uint8_t exact_item_count = 0;
    std::uint8_t affected_lane_mask = 0;
    const char* n64_category = nullptr;
    const char* semantic_effect = nullptr;
    const char* replacement_obligation = nullptr;
};

struct Oot3dMagicStateDescriptor {
    MagicState state = MagicState::MAGIC_STATE_IDLE;
    std::uint32_t handler_entry = 0;
    Oot3dMagicPhase phase = Oot3dMagicPhase::Idle;
    std::int8_t step_amount = 0;
    bool writes_magic = false;
    bool writes_capacity = false;
    bool writes_state = false;
    bool writes_border = false;
    bool emits_audio = false;
    bool queries_environmental_hazard = false;
    const char* semantic_effect = nullptr;
    const char* oot3d_difference = nullptr;
};

struct Oot3dInlineFieldDescriptor {
    Oot3dInlineFieldOwner owner = Oot3dInlineFieldOwner::SaveContext;
    std::uint32_t base_address = 0;
    std::uint16_t offset = 0;
    Oot3dInlineFieldEncoding encoding = Oot3dInlineFieldEncoding::U8;
    const char* name = nullptr;
    std::uint32_t proof_site = 0;
    std::uint32_t proof_word = 0;
    const char* semantic_effect = nullptr;
};

inline constexpr std::size_t kOot3dHudControlLaneCount = 6;
inline constexpr std::size_t kOot3dButtonRestrictionRuleCount = 8;
inline constexpr std::size_t kOot3dMagicUpdateStateCount = 11;
inline constexpr std::size_t kOot3dGameplayHudInlineFieldCount = 17;
inline constexpr std::uint32_t kOot3dSaveContextAddress = 0x00587958;
inline constexpr std::uint32_t kOot3dMagicBorderRuntimeAddress = 0x00539D70;
inline constexpr std::uint8_t kOot3dAssignableHudLaneMask = 0x1E;

const std::array<Oot3dHudControlLaneDescriptor, kOot3dHudControlLaneCount>&
Oot3dHudControlLanes() noexcept;
const Oot3dHudControlLaneDescriptor* Oot3dHudControlLaneInfo(Oot3dHudControlLane lane) noexcept;
const std::array<Oot3dButtonRestrictionRuleDescriptor, kOot3dButtonRestrictionRuleCount>&
Oot3dButtonRestrictionRules() noexcept;
const Oot3dButtonRestrictionRuleDescriptor* Oot3dButtonRestrictionRuleAt(std::uint16_t play_offset) noexcept;
const std::array<Oot3dMagicStateDescriptor, kOot3dMagicUpdateStateCount>&
Oot3dMagicUpdateStates() noexcept;
const Oot3dMagicStateDescriptor* Oot3dMagicUpdateState(MagicState state) noexcept;
const std::array<Oot3dInlineFieldDescriptor, kOot3dGameplayHudInlineFieldCount>&
Oot3dGameplayHudInlineFields() noexcept;
const Oot3dInlineFieldDescriptor* Oot3dGameplayHudInlineField(Oot3dInlineFieldOwner owner,
                                                             std::uint16_t offset) noexcept;

} // namespace oot3d::ui
