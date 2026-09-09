#include "oot3d_ui/ui_pause_gear_semantics.h"

namespace oot3d::ui {

namespace {

constexpr std::array<Oot3dPauseGearFunctionDescriptor,
                     kOot3dPauseGearFunctionCount> kFunctions{{
    {0x002E9B4C, 528, "PauseGearPage_TryEquipCategorySlot", "Validate ownership and age, change the selected sword, shield, or tunic, refresh Player equipment, and seed the native equip animation.", "N64 corroborates equipment categories and values; validation and animation remain OoT3D-native."},
    {0x002E9D78, 552, "PauseGearSlot_ResolveContentSelector", "Resolve the polymorphic native value for all twenty-seven Gear slots: availability, dynamic ItemId, or packed count according to slot role.", "Shared gameplay meanings only; the twenty-seven-slot selector contract is OoT3D-native."},
    {0x002F0444, 412, "PauseGearPage_ApplyTransitionMode", "Select the observed native Gear transition direction and seed its frame and controller state.", "No N64 state numbering or transition ownership is imported."},
    {0x002F7B44, 1516, "PauseGearPage_RefreshSlots", "Refresh every Gear icon, ownership state, age gate, equipped highlight, and upgrade quantity from native save data.", "N64 names corroborate shared content meanings only."},
    {0x002F88E0, 216, "PauseGearPage_AnimateClose", "Advance one native Gear transition formula and complete at its exact frame boundary.", "Maintained helper name retained; replacement must preserve the observed formula rather than infer N64 motion."},
    {0x002F8AF4, 116, "PauseGearPage_ResetInteractionState", "Reset Gear focus, touch ownership, animation values, and native interaction latches.", "Split OoT3D page owner has no binary-layout counterpart on N64."},
    {0x00424324, 2152, "PauseGearPage_Update", "Dispatch all eleven Gear states, including browse, equip animation, page transitions, and touch-owned requests.", "The complete OoT3D state machine is authoritative."},
    {0x00438600, 196, "PauseGearPage_AnimateOpen", "Advance the opposite native Gear transition formula and complete its interaction reset.", "Maintained helper name retained; no N64 direction or frame count is substituted."},
    {0x00438740, 560, "PauseGearPage_HandleCursorInput", "Navigate the exact Gear neighbour graph and confirm only the first nine equipment slots.", "N64 equipment meanings correspond; navigation graph and input gates are OoT3D-native."},
    {0x00438984, 672, "PauseGearPage_UpdateSelectedDetail", "Resolve the focused slot and update its native detail message and contextual icon.", "Detail ownership remains native 3DS."},
    {0x00438C58, 644, "PauseGearPage_UpdateSelectionHighlights", "Update slot scale, visibility, equipped markers, and selection geometry for the current Gear focus.", "Presentation geometry remains native 3DS."},
    {0x00438F20, 504, "PauseGearPage_UpdateTouchSlot", "Hit-test all twenty-seven rectangles and confirm only an owned, age-valid slot among the first nine.", "Touch workflow is an OoT3D extension."},
    {0x00449A38, 824, "PauseGearPage_Init", "Initialize Gear resources and slot arrays, repair initial equipment state, synchronize inventory, and apply the pending native swap.", "Initialization order and repair behavior remain OoT3D-native."},
}};

constexpr std::array<Oot3dPauseGearPageStateDescriptor,
                     kOot3dPauseGearPageStateCount> kStates{{
    {Oot3dPauseGearPageState::Inactive, 0x00424AD0, "inactive", "Keep input inactive while native slot and presentation state can still refresh.", "Do not synthesize selection or equipment mutations."},
    {Oot3dPauseGearPageState::AwaitTouchRelease, 0x0042438C, "await_touch_release", "Wait for the native touch-release gate before browsing.", "Preserve the release gate and next-state order."},
    {Oot3dPauseGearPageState::BrowseSlots, 0x004243A0, "browse_slots", "Drive the twenty-seven-slot controller through touch and physical input.", "Preserve both input paths, the exact neighbour graph, and nine-slot equip boundary."},
    {Oot3dPauseGearPageState::PrepareEquipAnimation, 0x004244F0, "prepare_equip_animation", "Prepare the auxiliary icon and four-update pre-commit movement.", "Do not commit or destroy animation ownership early."},
    {Oot3dPauseGearPageState::AnimateEquipCommit, 0x00424680, "animate_equip_commit", "Advance the six-update equip animation, release its auxiliary icon, and restore the focused slot.", "Preserve completion timing, cleanup, and focused-icon restoration."},
    {Oot3dPauseGearPageState::TouchOpenRequestA, 0x00424994, "touch_open_request_a", "Process the first observed touch-owned shell request.", "Retain native widget latch and touch disabling."},
    {Oot3dPauseGearPageState::TouchItemsPageTransition, 0x004248D8, "touch_items_page_transition", "Reset Gear and hand the observed transition to the Items page.", "Keep the native cross-page call, audio, and reset ordering."},
    {Oot3dPauseGearPageState::TouchPauseShellTransition, 0x00424A4C, "touch_pause_shell_transition", "Reset Gear through the shared pause-shell interaction path.", "Keep the shared reset, audio, and native offset clearing."},
    {Oot3dPauseGearPageState::TouchOpenRequestB, 0x0042495C, "touch_open_request_b", "Process the second observed touch-owned shell request.", "Retain native widget latch and touch disabling."},
    {Oot3dPauseGearPageState::AnimateTransitionFromHidden, 0x004249F4, "animate_transition_from_hidden", "Advance the transition entered with native mode one and frame five.", "Preserve the exact PauseGearPage_AnimateClose formula and completion state without renaming its direction from N64."},
    {Oot3dPauseGearPageState::AnimateTransitionToHidden, 0x00424A1C, "animate_transition_to_hidden", "Advance the transition entered by native back or touch requests with frame zero.", "Preserve the exact PauseGearPage_AnimateOpen formula, reset, and touch-button mode."},
}};

constexpr std::array<Oot3dPauseGearSlotDescriptor,
                     kOot3dPauseGearSlotCount> kSlots{{
    {Oot3dPauseGearSlot::KokiriSword, Oot3dPauseGearSlotRole::Equipment, ItemId::ITEM_SWORD_KOKIRI, 0, 1, -1, -1, Oot3dPauseGearAgeRequirement::Child, true, "kokiri_sword", "Child sword; ownership is equipment sword bit zero and equipped value is one.", "ITEM_SWORD_KOKIRI and sword equipment category agree."},
    {Oot3dPauseGearSlot::MasterSword, Oot3dPauseGearSlotRole::Equipment, ItemId::ITEM_SWORD_MASTER, 0, 2, -1, -1, Oot3dPauseGearAgeRequirement::Adult, true, "master_sword", "Adult sword; ownership is equipment sword bit one and equipped value is two.", "ITEM_SWORD_MASTER and sword equipment category agree."},
    {Oot3dPauseGearSlot::BiggoronSword, Oot3dPauseGearSlotRole::Equipment, ItemId::ITEM_SWORD_BIGGORON, 0, 3, -1, -1, Oot3dPauseGearAgeRequirement::Adult, true, "biggoron_sword", "Adult third sword tier; native presentation may select the Giant's Knife variant from sword durability and Biggoron state.", "Shared third sword tier; OoT3D variant selection remains authoritative."},
    {Oot3dPauseGearSlot::DekuShield, Oot3dPauseGearSlotRole::Equipment, ItemId::ITEM_SHIELD_DEKU, 1, 1, -1, -1, Oot3dPauseGearAgeRequirement::Child, true, "deku_shield", "Child shield; ownership is shield bit zero and equipped value is one.", "ITEM_SHIELD_DEKU and shield category agree."},
    {Oot3dPauseGearSlot::HylianShield, Oot3dPauseGearSlotRole::Equipment, ItemId::ITEM_SHIELD_HYLIAN, 1, 2, -1, -1, Oot3dPauseGearAgeRequirement::Any, true, "hylian_shield", "Age-independent shield; ownership is shield bit one and equipped value is two.", "ITEM_SHIELD_HYLIAN and shield category agree."},
    {Oot3dPauseGearSlot::MirrorShield, Oot3dPauseGearSlotRole::Equipment, ItemId::ITEM_SHIELD_MIRROR, 1, 3, -1, -1, Oot3dPauseGearAgeRequirement::Adult, true, "mirror_shield", "Adult shield; ownership is shield bit two and equipped value is three.", "ITEM_SHIELD_MIRROR and shield category agree."},
    {Oot3dPauseGearSlot::KokiriTunic, Oot3dPauseGearSlotRole::Equipment, ItemId::ITEM_TUNIC_KOKIRI, 2, 1, -1, -1, Oot3dPauseGearAgeRequirement::Any, true, "kokiri_tunic", "Age-independent default tunic; ownership is tunic bit zero and equipped value is one.", "ITEM_TUNIC_KOKIRI and tunic category agree."},
    {Oot3dPauseGearSlot::GoronTunic, Oot3dPauseGearSlotRole::Equipment, ItemId::ITEM_TUNIC_GORON, 2, 2, -1, -1, Oot3dPauseGearAgeRequirement::Adult, true, "goron_tunic", "Adult heat-resistant tunic; ownership is tunic bit one and equipped value is two.", "ITEM_TUNIC_GORON and tunic category agree."},
    {Oot3dPauseGearSlot::ZoraTunic, Oot3dPauseGearSlotRole::Equipment, ItemId::ITEM_TUNIC_ZORA, 2, 3, -1, -1, Oot3dPauseGearAgeRequirement::Adult, true, "zora_tunic", "Adult underwater tunic; ownership is tunic bit two and equipped value is three.", "ITEM_TUNIC_ZORA and tunic category agree."},
    {Oot3dPauseGearSlot::ForestMedallion, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_MEDALLION_FOREST, -1, 0, 0, -1, Oot3dPauseGearAgeRequirement::Any, false, "forest_medallion", "Owned by quest bit zero.", "Medallion identity and quest bit agree."},
    {Oot3dPauseGearSlot::FireMedallion, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_MEDALLION_FIRE, -1, 0, 1, -1, Oot3dPauseGearAgeRequirement::Any, false, "fire_medallion", "Owned by quest bit one.", "Medallion identity and quest bit agree."},
    {Oot3dPauseGearSlot::WaterMedallion, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_MEDALLION_WATER, -1, 0, 2, -1, Oot3dPauseGearAgeRequirement::Any, false, "water_medallion", "Owned by quest bit two.", "Medallion identity and quest bit agree."},
    {Oot3dPauseGearSlot::SpiritMedallion, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_MEDALLION_SPIRIT, -1, 0, 3, -1, Oot3dPauseGearAgeRequirement::Any, false, "spirit_medallion", "Owned by quest bit three.", "Medallion identity and quest bit agree."},
    {Oot3dPauseGearSlot::ShadowMedallion, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_MEDALLION_SHADOW, -1, 0, 4, -1, Oot3dPauseGearAgeRequirement::Any, false, "shadow_medallion", "Owned by quest bit four.", "Medallion identity and quest bit agree."},
    {Oot3dPauseGearSlot::LightMedallion, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_MEDALLION_LIGHT, -1, 0, 5, -1, Oot3dPauseGearAgeRequirement::Any, false, "light_medallion", "Owned by quest bit five.", "Medallion identity and quest bit agree."},
    {Oot3dPauseGearSlot::KokiriEmerald, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_KOKIRI_EMERALD, -1, 0, 18, -1, Oot3dPauseGearAgeRequirement::Any, false, "kokiri_emerald", "Owned by quest bit eighteen.", "Spiritual-stone identity and quest bit agree."},
    {Oot3dPauseGearSlot::GoronRuby, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_GORON_RUBY, -1, 0, 19, -1, Oot3dPauseGearAgeRequirement::Any, false, "goron_ruby", "Owned by quest bit nineteen.", "Spiritual-stone identity and quest bit agree."},
    {Oot3dPauseGearSlot::ZoraSapphire, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_ZORA_SAPPHIRE, -1, 0, 20, -1, Oot3dPauseGearAgeRequirement::Any, false, "zora_sapphire", "Owned by quest bit twenty.", "Spiritual-stone identity and quest bit agree."},
    {Oot3dPauseGearSlot::Ocarina, Oot3dPauseGearSlotRole::Ocarina, ItemId::ITEM_OCARINA_FAIRY, -1, 0, -1, -1, Oot3dPauseGearAgeRequirement::Any, false, "ocarina", "Resolve Fairy Ocarina or Ocarina of Time directly from inventory slot seven.", "Ocarina item identities agree; slot placement is OoT3D-native."},
    {Oot3dPauseGearSlot::StoneOfAgony, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_STONE_OF_AGONY, -1, 0, 21, -1, Oot3dPauseGearAgeRequirement::Any, false, "stone_of_agony", "Owned by quest bit twenty-one.", "Quest-item identity and bit agree."},
    {Oot3dPauseGearSlot::GerudoCard, Oot3dPauseGearSlotRole::QuestItem, ItemId::ITEM_GERUDOS_CARD, -1, 0, 22, -1, Oot3dPauseGearAgeRequirement::Any, false, "gerudo_card", "Owned by quest bit twenty-two.", "Quest-item identity and bit agree."},
    {Oot3dPauseGearSlot::GoldSkulltulaTokens, Oot3dPauseGearSlotRole::TokenCount, ItemId::ITEM_SKULL_TOKEN, -1, 0, -1, -1, Oot3dPauseGearAgeRequirement::Any, false, "gold_skulltula_tokens", "Available when the native token count is non-zero and exposes that count.", "Token meaning agrees; count display is OoT3D-native."},
    {Oot3dPauseGearSlot::ProjectileCapacity, Oot3dPauseGearSlotRole::AgeDependentProjectileUpgrade, ItemId::ITEM_QUIVER_30, -1, 0, -1, -2, Oot3dPauseGearAgeRequirement::Any, false, "projectile_capacity", "Resolve quiver for adult Link or bullet bag for child Link from their distinct packed upgrade fields.", "Shared upgrades; age-dependent merged Gear slot is OoT3D-native."},
    {Oot3dPauseGearSlot::BombBag, Oot3dPauseGearSlotRole::Upgrade, ItemId::ITEM_BOMB_BAG_20, -1, 0, -1, 1, Oot3dPauseGearAgeRequirement::Any, false, "bomb_bag", "Resolve the acquired bomb-bag tier from packed upgrade field one.", "Bomb-bag tiers agree."},
    {Oot3dPauseGearSlot::Strength, Oot3dPauseGearSlotRole::Upgrade, ItemId::ITEM_STRENGTH_GORONS_BRACELET, -1, 0, -1, 2, Oot3dPauseGearAgeRequirement::Any, false, "strength", "Resolve bracelet or gauntlet tier from packed upgrade field two.", "Strength tiers agree."},
    {Oot3dPauseGearSlot::Scale, Oot3dPauseGearSlotRole::Upgrade, ItemId::ITEM_SCALE_SILVER, -1, 0, -1, 3, Oot3dPauseGearAgeRequirement::Any, false, "scale", "Resolve diving-scale tier from packed upgrade field three.", "Scale tiers agree."},
    {Oot3dPauseGearSlot::HeartPieces, Oot3dPauseGearSlotRole::HeartPieceCount, ItemId::ITEM_HEART_PIECE, -1, 0, -1, -1, Oot3dPauseGearAgeRequirement::Any, false, "heart_pieces", "Expose the native four-bit heart-piece count stored at quest bits twenty-eight through thirty-one.", "Heart-piece meaning agrees; packed count selector remains native."},
}};

constexpr std::array<Oot3dPauseGearFieldDescriptor,
                     kOot3dPauseGearFieldCount> kFields{{
    {0x0018, Oot3dPauseGearFieldEncoding::S32, "controller_state", 0x00424334, 0xE59A0018, "Selects one of eleven native Gear handlers."},
    {0x001C, Oot3dPauseGearFieldEncoding::S32, "transition_frame", 0x002F0474, 0xE58B001C, "Stores the current native page-transition frame."},
    {0x0020, Oot3dPauseGearFieldEncoding::S32, "focus_slot", 0x00424AF8, 0xE59A0020, "Stores the active 0..26 Gear focus."},
    {0x0024, Oot3dPauseGearFieldEncoding::S32, "detail_slot", 0x004389D8, 0xE5870024, "Caches the slot whose detail content was last refreshed."},
    {0x0028, Oot3dPauseGearFieldEncoding::S32, "touch_slot", 0x004244B0, 0xE59A0028, "Stores the pressed 0..26 touch slot or native negative sentinel."},
    {0x0030, Oot3dPauseGearFieldEncoding::Bool32, "touch_open_request_a_latch", 0x004243B0, 0x158A0030, "Latches the first observed touch-owned shell request."},
    {0x0034, Oot3dPauseGearFieldEncoding::Bool32, "touch_open_request_b_latch", 0x004243DC, 0xE58A0034, "Latches the second observed touch-owned shell request."},
    {0x0038, Oot3dPauseGearFieldEncoding::S32, "equip_animation_step", 0x00424500, 0xE58A0038, "Counts native equip-animation updates."},
    {0x0040, Oot3dPauseGearFieldEncoding::S32, "pending_equip_category", 0x002E9BFC, 0xE5856040, "Stores sword, shield, or tunic category zero through two."},
    {0x0044, Oot3dPauseGearFieldEncoding::S32, "pending_equip_tier", 0x00424508, 0xE59A0044, "Stores zero-based tier; committed equipment value is tier plus one."},
    {0x0048, Oot3dPauseGearFieldEncoding::S32, "equip_animation_x", 0x00424520, 0xE59A0048, "Stores the current native equip-animation x coordinate."},
    {0x004C, Oot3dPauseGearFieldEncoding::ItemIdS32, "pending_item_id", 0x002E9BF8, 0xE585004C, "Stores the selected equipment ItemId pending animation."},
    {0x0050, Oot3dPauseGearFieldEncoding::Bool32, "suppress_focused_slot_enlargement", 0x00424884, 0xE58A0050, "Suppresses ordinary focused-slot enlargement while animation owns the selected icon."},
}};

constexpr std::array<Oot3dPauseGearMutationDescriptor,
                     kOot3dPauseGearMutationCount> kMutations{{
    {0x002E9BCC, 0xEB011F2A, Oot3dPauseGearMutationKind::ChangeEquipment, "SaveContext tunic equipment", "owned, age-valid tunic tier differs from current value", "Retain Inventory_ChangeEquipment(category, tier + 1)."},
    {0x002E9BF0, 0xEB017D51, Oot3dPauseGearMutationKind::RefreshPlayerEquipment, "live Player equipment data", "after tunic equipment change when PlayState is available", "Refresh Player only after SaveContext changes."},
    {0x002E9C70, 0xEB011F01, Oot3dPauseGearMutationKind::ChangeEquipment, "SaveContext sword equipment", "owned, age-valid sword tier differs from current value", "Retain Inventory_ChangeEquipment(category, tier + 1)."},
    {0x002E9C94, 0xEB017D28, Oot3dPauseGearMutationKind::RefreshPlayerEquipment, "live Player equipment data", "after sword equipment change when PlayState is available", "Refresh Player only after SaveContext changes."},
    {0x002E9D04, 0xEB011EDC, Oot3dPauseGearMutationKind::ChangeEquipment, "SaveContext shield equipment", "owned, age-valid shield tier differs from current value", "Retain Inventory_ChangeEquipment(category, tier + 1)."},
    {0x002E9D28, 0xEB017D03, Oot3dPauseGearMutationKind::RefreshPlayerEquipment, "live Player equipment data", "after shield equipment change when PlayState is available", "Refresh Player only after SaveContext changes."},
    {0x00449B70, 0xEBFB9F41, Oot3dPauseGearMutationKind::ChangeEquipment, "initial sword equipment", "Gear initialization repairs a present sword with no selected sword", "Preserve initialization repair before inventory synchronization."},
    {0x00449B94, 0xEBFB9F38, Oot3dPauseGearMutationKind::ChangeEquipment, "initial shield equipment", "Gear initialization repairs the observed default shield case", "Preserve initialization repair before inventory synchronization."},
    {0x00449CC4, 0xEBFBC964, Oot3dPauseGearMutationKind::SynchronizePauseInventory, "pause inventory presentation", "after Gear initialization and native repair writes", "Synchronize only after native equipment repair completes."},
}};

} // namespace

const std::array<Oot3dPauseGearFunctionDescriptor, kOot3dPauseGearFunctionCount>&
Oot3dPauseGearFunctions() noexcept { return kFunctions; }

const Oot3dPauseGearFunctionDescriptor* Oot3dPauseGearFunction(
    std::uint32_t entry) noexcept {
    for (const auto& item : kFunctions) if (item.entry == entry) return &item;
    return nullptr;
}

const std::array<Oot3dPauseGearPageStateDescriptor, kOot3dPauseGearPageStateCount>&
Oot3dPauseGearPageStates() noexcept { return kStates; }

const Oot3dPauseGearPageStateDescriptor* Oot3dPauseGearPageStateInfo(
    Oot3dPauseGearPageState state) noexcept {
    for (const auto& item : kStates) if (item.state == state) return &item;
    return nullptr;
}

const std::array<Oot3dPauseGearSlotDescriptor, kOot3dPauseGearSlotCount>&
Oot3dPauseGearSlots() noexcept { return kSlots; }

const Oot3dPauseGearSlotDescriptor* Oot3dPauseGearSlotInfo(
    Oot3dPauseGearSlot slot) noexcept {
    for (const auto& item : kSlots) if (item.slot == slot) return &item;
    return nullptr;
}

const std::array<Oot3dPauseGearFieldDescriptor, kOot3dPauseGearFieldCount>&
Oot3dPauseGearFields() noexcept { return kFields; }

const Oot3dPauseGearFieldDescriptor* Oot3dPauseGearField(
    std::uint16_t offset) noexcept {
    for (const auto& item : kFields) if (item.offset == offset) return &item;
    return nullptr;
}

const std::array<Oot3dPauseGearMutationDescriptor, kOot3dPauseGearMutationCount>&
Oot3dPauseGearMutations() noexcept { return kMutations; }

} // namespace oot3d::ui
