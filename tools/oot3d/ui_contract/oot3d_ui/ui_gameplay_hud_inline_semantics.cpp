#include "oot3d_ui/ui_gameplay_hud_inline_semantics.h"

namespace oot3d::ui {

namespace {

constexpr std::array<Oot3dHudControlLaneDescriptor, kOot3dHudControlLaneCount> kLanes{{
    {Oot3dHudControlLane::B, 0x0080, 0x156F, 0x00475D6C, 0xE5D90080, 0x00475D80, 0xE5D4156F},
    {Oot3dHudControlLane::Y, 0x0081, 0x1570, 0x004761F0, 0xE5D90081, 0x004761FC, 0xE5D40570},
    {Oot3dHudControlLane::X, 0x0082, 0x1571, 0x00476220, 0xE5D90082, 0x0047622C, 0xE5D40571},
    {Oot3dHudControlLane::I, 0x0083, 0x1572, 0x00476250, 0xE5D90083, 0x0047625C, 0xE5D40572},
    {Oot3dHudControlLane::II, 0x0084, 0x1573, 0x00476280, 0xE5D90084, 0x0047628C, 0xE5D40573},
    {Oot3dHudControlLane::A, 0xFFFF, 0x1575, 0x00000000, 0x00000000, 0x004762B0, 0xE5D40575},
}};

constexpr std::array<Oot3dButtonRestrictionRuleDescriptor,
                     kOot3dButtonRestrictionRuleCount> kRules{{
    {"b_button", 0x2E43, 0x004762E4, 0xE5DB029F, Oot3dButtonRestrictionMatcher::BButtonPolicy, ItemId::ITEM_NONE, ItemId::ITEM_NONE, {ItemId::ITEM_NONE, ItemId::ITEM_NONE}, 0, 0x01, "bButton", "Reconciles B-item loadout and enable state for the native scene restriction.", "Preserve item restoration and icon reload ordering, not only the visible enabled flag."},
    {"bottles", 0x2E45, 0x00476388, 0xE5DB02A1, Oot3dButtonRestrictionMatcher::InclusiveRange, ItemId::ITEM_BOTTLE_EMPTY, ItemId::ITEM_BOTTLE_POE, {ItemId::ITEM_NONE, ItemId::ITEM_NONE}, 0, 0x1E, "bottles", "Disables or restores every bottle item on all four assignable 3DS lanes.", "Apply the same inclusive item range to Y, X, I, and II."},
    {"trade_items", 0x2E46, 0x00476490, 0xE5DB02A2, Oot3dButtonRestrictionMatcher::InclusiveRange, ItemId::ITEM_WEIRD_EGG, ItemId::ITEM_CLAIM_CHECK, {ItemId::ITEM_NONE, ItemId::ITEM_NONE}, 0, 0x1E, "tradeItems", "Disables or restores the complete child/adult trade-item range.", "Keep the original 3DS item identities and four-lane status writes."},
    {"hookshot", 0x2E47, 0x00476598, 0xE5DB02A3, Oot3dButtonRestrictionMatcher::ExactSet, ItemId::ITEM_NONE, ItemId::ITEM_NONE, {ItemId::ITEM_HOOKSHOT, ItemId::ITEM_LONGSHOT}, 2, 0x1E, "hookshot", "Disables or restores Hookshot and Longshot.", "Treat both hookshot upgrades as one restriction family."},
    {"ocarina", 0x2E48, 0x004766A4, 0xE5DB02A4, Oot3dButtonRestrictionMatcher::ExactSet, ItemId::ITEM_NONE, ItemId::ITEM_NONE, {ItemId::ITEM_OCARINA_FAIRY, ItemId::ITEM_OCARINA_OF_TIME}, 2, 0x1E, "ocarina", "Disables or restores both Ocarina variants.", "Preserve both native Ocarina identities on every assignable lane."},
    {"farores", 0x2E4B, 0x004767D0, 0xE5DB02A7, Oot3dButtonRestrictionMatcher::ExactSet, ItemId::ITEM_NONE, ItemId::ITEM_NONE, {ItemId::ITEM_FARORES_WIND, ItemId::ITEM_NONE}, 1, 0x1E, "farores", "Disables or restores Farore's Wind.", "Keep this spell independent of the paired spell rule."},
    {"dins_nayrus", 0x2E4C, 0x004768BC, 0xE5DB02A8, Oot3dButtonRestrictionMatcher::ExactSet, ItemId::ITEM_NONE, ItemId::ITEM_NONE, {ItemId::ITEM_DINS_FIRE, ItemId::ITEM_NAYRUS_LOVE}, 2, 0x1E, "dinsNayrus", "Disables or restores Din's Fire and Nayru's Love together.", "Preserve the native paired-spell category."},
    {"all", 0x2E4D, 0x004769C8, 0xE5DB02A9, Oot3dButtonRestrictionMatcher::AllOther, ItemId::ITEM_NONE, ItemId::ITEM_NONE, {ItemId::ITEM_NONE, ItemId::ITEM_NONE}, 0, 0x1E, "all", "Restricts every other assignable item while excluding specifically managed categories; native scene 0x10 retains its Lens exception and the final pass re-enables boot IDs 0x44..0x46.", "Keep the asymmetric disable/restore exclusions and the numeric 3DS scene exception exactly as observed."},
}};

constexpr std::array<Oot3dMagicStateDescriptor,
                     kOot3dMagicUpdateStateCount> kMagicStates{{
    {MagicState::MAGIC_STATE_IDLE, 0x00477430, Oot3dMagicPhase::Idle, 0, false, false, true, false, false, false, "Normalizes the state to idle and returns.", "none observed"},
    {MagicState::MAGIC_STATE_CONSUME_SETUP, 0x00476F60, Oot3dMagicPhase::Consume, 0, false, false, true, true, false, false, "Primes the border interpolation ratio to two and enters consumption.", "none observed"},
    {MagicState::MAGIC_STATE_CONSUME, 0x00476F78, Oot3dMagicPhase::Consume, -2, true, false, true, true, false, false, "Consumes two magic units toward zero or magicTarget and continues through border flashing.", "none observed"},
    {MagicState::MAGIC_STATE_METER_FLASH_1, 0x00476FE4, Oot3dMagicPhase::BorderFlash, 0, false, false, false, true, false, false, "Interpolates the magic-meter border toward the current flash color.", "none observed"},
    {MagicState::MAGIC_STATE_METER_FLASH_2, 0x00476FE4, Oot3dMagicPhase::BorderFlash, 0, false, false, false, true, false, false, "Shares the canonical four-step magic-border interpolation.", "none observed"},
    {MagicState::MAGIC_STATE_RESET, 0x0047710C, Oot3dMagicPhase::Reset, 0, false, false, true, true, false, false, "Restores the white border and returns the mechanic to idle.", "none observed"},
    {MagicState::MAGIC_STATE_METER_FLASH_3, 0x00476FE4, Oot3dMagicPhase::BorderFlash, 0, false, false, false, true, false, false, "Shares the canonical four-step magic-border interpolation.", "none observed"},
    {MagicState::MAGIC_STATE_CONSUME_LENS, 0x00477130, Oot3dMagicPhase::Lens, -1, true, false, true, true, true, true, "Applies pause/message/game-over/transition/cutscene gates, turns Lens off for zero magic, underwater hazards, missing equipped Lens, or inactive Lens state, and drains one unit on the native timer.", "Checks all four Y/X/I/II assignable lanes instead of the three N64 C-button lanes."},
    {MagicState::MAGIC_STATE_STEP_CAPACITY, 0x00476E70, Oot3dMagicPhase::Capacity, 8, false, true, true, false, false, false, "Steps capacity by eight toward magicLevel times 48, then enters fill.", "none observed"},
    {MagicState::MAGIC_STATE_FILL, 0x00476ED4, Oot3dMagicPhase::Fill, 4, true, false, true, false, true, false, "Adds four magic units to magicFillTarget, then restores prevMagicState.", "none observed"},
    {MagicState::MAGIC_STATE_ADD, 0x004773C4, Oot3dMagicPhase::Add, 4, true, false, true, false, true, false, "Adds four magic units to magicTarget, then restores prevMagicState.", "none observed"},
}};

constexpr std::array<Oot3dInlineFieldDescriptor,
                     kOot3dGameplayHudInlineFieldCount> kFields{{
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x0046, Oot3dInlineFieldEncoding::S8, "magic_level", 0x00476E78, 0xE1D004D6, "Selects the target magic-meter capacity."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x0047, Oot3dInlineFieldEncoding::U8, "magic", 0x00476F28, 0xE5D20047, "Stores the current magic amount."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x1576, Oot3dInlineFieldEncoding::U8, "force_rising_button_alphas", 0x00475D08, 0xE5C48576, "Forces restriction-aware button alpha recovery."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x1578, Oot3dInlineFieldEncoding::U16, "next_hud_visibility_mode", 0x00475E38, 0x11C607B8, "Stores the requested HUD visibility mode."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x157A, Oot3dInlineFieldEncoding::U16, "hud_visibility_mode", 0x00475E3C, 0x11C607BA, "Stores the current HUD visibility mode."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x157C, Oot3dInlineFieldEncoding::U16, "hud_visibility_mode_timer", 0x00475E40, 0x11C6B7BC, "Times native visibility reconciliation."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x1580, Oot3dInlineFieldEncoding::S16, "magic_state", 0x00476E34, 0xE1D408F0, "Selects one of the eleven native magic handlers."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x1582, Oot3dInlineFieldEncoding::S16, "previous_magic_state", 0x00476F48, 0xE1D418B2, "Restores the owner state after fill or add."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x1584, Oot3dInlineFieldEncoding::S16, "magic_capacity", 0x00476E74, 0xE1D418F4, "Stores the displayed meter capacity."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x1586, Oot3dInlineFieldEncoding::S16, "magic_fill_target", 0x00476F38, 0xE1D418F6, "Bounds the fill state."},
    {Oot3dInlineFieldOwner::SaveContext, 0x00587958, 0x1588, Oot3dInlineFieldEncoding::S16, "magic_target", 0x00476FBC, 0xE1D418F8, "Bounds consume and add states."},
    {Oot3dInlineFieldOwner::MagicBorderRuntime, 0x00539D70, 0x0008, Oot3dInlineFieldEncoding::S16, "magic_border_r", 0x00476FB4, 0xE1C010B8, "Stores the native meter-border red channel."},
    {Oot3dInlineFieldOwner::MagicBorderRuntime, 0x00539D70, 0x000A, Oot3dInlineFieldEncoding::S16, "magic_border_g", 0x00476FB0, 0xE1C010BA, "Stores the native meter-border green channel."},
    {Oot3dInlineFieldOwner::MagicBorderRuntime, 0x00539D70, 0x000C, Oot3dInlineFieldEncoding::S16, "magic_border_b", 0x00476FAC, 0xE1C010BC, "Stores the native meter-border blue channel."},
    {Oot3dInlineFieldOwner::MagicBorderRuntime, 0x00539D70, 0x0012, Oot3dInlineFieldEncoding::S16, "magic_border_ratio", 0x00476F68, 0xE1C101B2, "Controls the interpolation divisor."},
    {Oot3dInlineFieldOwner::MagicBorderRuntime, 0x00539D70, 0x0014, Oot3dInlineFieldEncoding::S16, "magic_border_step", 0x004770F4, 0xE1C101B4, "Selects one of the four border-color steps."},
    {Oot3dInlineFieldOwner::PlayState, 0x00000000, 0x2E0E, Oot3dInlineFieldEncoding::U16, "lens_magic_consumption_timer", 0x0047724C, 0xE1D016BA, "Times periodic Lens of Truth magic consumption."},
}};

} // namespace

const std::array<Oot3dHudControlLaneDescriptor, kOot3dHudControlLaneCount>&
Oot3dHudControlLanes() noexcept { return kLanes; }

const Oot3dHudControlLaneDescriptor* Oot3dHudControlLaneInfo(Oot3dHudControlLane lane) noexcept {
    for (const auto& item : kLanes) if (item.lane == lane) return &item;
    return nullptr;
}

const std::array<Oot3dButtonRestrictionRuleDescriptor, kOot3dButtonRestrictionRuleCount>&
Oot3dButtonRestrictionRules() noexcept { return kRules; }

const Oot3dButtonRestrictionRuleDescriptor* Oot3dButtonRestrictionRuleAt(std::uint16_t play_offset) noexcept {
    for (const auto& rule : kRules) if (rule.play_offset == play_offset) return &rule;
    return nullptr;
}

const std::array<Oot3dMagicStateDescriptor, kOot3dMagicUpdateStateCount>&
Oot3dMagicUpdateStates() noexcept { return kMagicStates; }

const Oot3dMagicStateDescriptor* Oot3dMagicUpdateState(MagicState state) noexcept {
    for (const auto& item : kMagicStates) if (item.state == state) return &item;
    return nullptr;
}

const std::array<Oot3dInlineFieldDescriptor, kOot3dGameplayHudInlineFieldCount>&
Oot3dGameplayHudInlineFields() noexcept { return kFields; }

const Oot3dInlineFieldDescriptor* Oot3dGameplayHudInlineField(Oot3dInlineFieldOwner owner,
                                                             std::uint16_t offset) noexcept {
    for (const auto& field : kFields) if (field.owner == owner && field.offset == offset) return &field;
    return nullptr;
}

} // namespace oot3d::ui
