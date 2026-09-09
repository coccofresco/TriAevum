#include "oot3d_ui/ui_pause_items_semantics.h"

namespace oot3d::ui {

namespace {

constexpr std::array<Oot3dPauseItemsFunctionDescriptor,
                     kOot3dPauseItemsFunctionCount> kFunctions{{
    {0x002EB0D8, 216, "PauseItemsPage_CanSwapSelectedSlots", "Validate a proposed movable-grid exchange against ownership, age, and native special-slot rules.", "No direct N64 equivalent: the movable age-specific 3DS grid is authoritative."},
    {0x002EB1BC, 80, "PauseItemsPage_CommitCursorSelection", "Snapshot cursor coordinates, derive the focused 6x4 position, and optionally refresh its detail content.", "Shared cursor-selection meaning only; the stored 3DS grid layout is distinct."},
    {0x002EB210, 232, "PauseItemsPage_ShouldOpenArrowTypeSelector", "Recognize a Bow/Fire/Ice/Light interaction that enters the native arrow-type selector when an elemental arrow is available for adult Link.", "N64 corroborates the four arrow meanings, not this 3DS popup or its entry rule."},
    {0x002EBA9C, 2288, "PauseItemsPage_UpdateTouchGrid", "Hit-test and drive touch-owned source, destination, cancellation, swap, and arrow-selector transitions.", "3DS-specific touch and movable-grid controller."},
    {0x002EC3E4, 3576, "PauseItemsPage_UpdateEquipGrid", "Drive physical-input navigation and the same native Items source/destination workflow.", "N64 supplies item vocabulary only; controller shape and grid remain 3DS-native."},
    {0x00433AB4, 4396, "PauseItemsPage_Update", "Dispatch all fifteen native Items states, including movable-grid commits, animations, equipment side effects, and the arrow selector.", "Only corresponding gameplay meanings are shared with N64."},
    {0x004456A8, 4928, "PauseItemsPage_UpdateArrowTypeSelector", "Run the 2x2 Bow/Fire/Ice/Light choice controller and commit the chosen variant to the Bow inventory slot and cache.", "The 2x2 popup, touch handling, and cache write are OoT3D-specific."},
    {0x00446A70, 148, "PauseItemsPage_ResolveSpecialSwapSlots", "Resolve which selected movable-grid positions occupy one of the four native special item cells.", "No fixed-grid N64 counterpart is inferred."},
}};

constexpr std::array<Oot3dPauseItemsPageStateDescriptor,
                     kOot3dPauseItemsPageStateCount> kPageStates{{
    {Oot3dPauseItemsPageState::Inactive, 0x00434B84, "inactive", "Leave the controller inactive while still refreshing presentation-owned groups.", "Do not synthesize input or inventory work."},
    {Oot3dPauseItemsPageState::AwaitTouchRelease, 0x00433B48, "await_touch_release", "Wait for the native touch-release gate before committing the current focus.", "Preserve release gating and latch reset order."},
    {Oot3dPauseItemsPageState::BrowseGrid, 0x00433B5C, "browse_grid", "Drive the ordinary 6x4 Items grid through native touch and physical-input helpers.", "Preserve both input paths and their native ownership gates."},
    {Oot3dPauseItemsPageState::TouchOpenRequestA, 0x00433D54, "touch_open_request_a", "Process the first observed touch-owned open transition branch.", "Retain the native widget result and state transition."},
    {Oot3dPauseItemsPageState::TouchGearPageTransition, 0x00433C98, "touch_gear_page_transition", "Apply the observed transition into the Gear-page workflow.", "Keep the native cross-page call and transition mode."},
    {Oot3dPauseItemsPageState::TouchPauseShellTransition, 0x00433DB4, "touch_pause_shell_transition", "Apply the observed transition through the shared pause interaction reset.", "Keep shared pause-shell ordering."},
    {Oot3dPauseItemsPageState::TouchOpenRequestB, 0x00433D1C, "touch_open_request_b", "Process the second observed touch-owned open transition branch.", "Retain the native widget result and state transition."},
    {Oot3dPauseItemsPageState::PrepareDragIcon, 0x00433E8C, "prepare_drag_icon", "Prepare the source icon and coordinates for a movable-grid interaction.", "Preserve source item lookup and icon visibility."},
    {Oot3dPauseItemsPageState::SelectDropTarget, 0x00433F44, "select_drop_target", "Animate and resolve a destination for the selected source position.", "Preserve cancellation and destination validation."},
    {Oot3dPauseItemsPageState::AnimateGridSwap, 0x004343CC, "animate_grid_swap", "Animate the ordinary movable-grid exchange toward its commit point.", "Do not commit the mapping before the native animation boundary."},
    {Oot3dPauseItemsPageState::CommitGridLayoutSwap, 0x004344E0, "commit_grid_layout_swap", "Swap two entries in the active age-specific 6x4 menu mapping and reconcile equipment side effects.", "Write the 3DS menu mapping, not Inventory.items; retain equipment refresh and inventory sync."},
    {Oot3dPauseItemsPageState::AnimateSpecialSlotSwap, 0x0043489C, "animate_special_slot_swap", "Animate the native special-cell exchange and restore focused-icon visibility afterward.", "Preserve both animation icon owners and suppression flag."},
    {Oot3dPauseItemsPageState::ArrowTypeSelector, 0x00434B7C, "arrow_type_selector", "Advance the nested Bow/Fire/Ice/Light selector.", "Use the native selector until a future backend replaces the complete mechanic."},
    {Oot3dPauseItemsPageState::AnimatePageTransition, 0x00433E34, "animate_page_transition", "Advance the native Items page transition animation.", "Preserve transition completion and next-state order."},
    {Oot3dPauseItemsPageState::AnimateItemsPageOpen, 0x00433E5C, "animate_items_page_open", "Advance the five-frame native Items opening animation.", "Preserve the native completion gate."},
}};

constexpr std::array<Oot3dArrowTypeSelectorStateDescriptor,
                     kOot3dArrowTypeSelectorStateCount> kArrowStates{{
    {Oot3dArrowTypeSelectorState::Inactive, 0x00445A08, "inactive", "Return without changing arrow selection."},
    {Oot3dArrowTypeSelectorState::Initialize, 0x00445738, "initialize", "Build four choice icons and initialize the 2x2 cursor from Inventory.items[SLOT_BOW]."},
    {Oot3dArrowTypeSelectorState::Browse, 0x00445A4C, "browse", "Handle directional and touch choice, availability gates, confirmation, and cancellation."},
    {Oot3dArrowTypeSelectorState::CommitAnimation, 0x004468BC, "commit_animation", "Interpolate for six updates, then write the chosen Bow variant to Inventory.items and its cache and synchronize pause inventory."},
    {Oot3dArrowTypeSelectorState::SettleAnimation, 0x0044699C, "settle_animation", "Run the five-update settle animation and return to browsing."},
}};

constexpr std::array<Oot3dArrowTypeChoiceDescriptor,
                     kOot3dArrowTypeChoiceCount> kArrowChoices{{
    {Oot3dArrowTypeChoice::Bow, ItemId::ITEM_BOW, 0x7A, false, 0xFFFF, 0x004460E4, 0xE3A00003, "bow", "Commit the normal Bow value; 0x7A is a native selector icon resource in this context, not a heart-piece gameplay item."},
    {Oot3dArrowTypeChoice::FireArrow, ItemId::ITEM_BOW_FIRE, 0x04, true, 0x0004, 0x00446200, 0xE3A00038, "fire_arrow", "Require the Fire Arrow availability word and commit ITEM_BOW_FIRE."},
    {Oot3dArrowTypeChoice::IceArrow, ItemId::ITEM_BOW_ICE, 0x0C, true, 0x0008, 0x00446320, 0xE3A00039, "ice_arrow", "Require the Ice Arrow availability word and commit ITEM_BOW_ICE."},
    {Oot3dArrowTypeChoice::LightArrow, ItemId::ITEM_BOW_LIGHT, 0x12, true, 0x000C, 0x00446454, 0xE3A0003A, "light_arrow", "Require the Light Arrow availability word and commit ITEM_BOW_LIGHT."},
}};

constexpr std::array<Oot3dPauseItemsFieldDescriptor,
                     kOot3dPauseItemsFieldCount> kFields{{
    {0x0034, Oot3dPauseItemsFieldEncoding::S32, "controller_state", 0x00433AFC, 0xE5940034, "Selects one of fifteen Items-page handlers."},
    {0x0038, Oot3dPauseItemsFieldEncoding::S32, "arrow_selector_state", 0x004456F4, 0xE5980038, "Selects one of five nested arrow-selector handlers."},
    {0x0040, Oot3dPauseItemsFieldEncoding::Pointer32, "arrow_choice_icons", 0x00445778, 0xE5980040, "Owns the four selector choice icons."},
    {0x0044, Oot3dPauseItemsFieldEncoding::Pointer32, "primary_animation_icon", 0x00446040, 0xE5980044, "Owns the selected or source animation icon."},
    {0x0048, Oot3dPauseItemsFieldEncoding::Pointer32, "secondary_animation_icon", 0x004346EC, 0xE5940048, "Owns the destination icon for grid-swap animation."},
    {0x0050, Oot3dPauseItemsFieldEncoding::S32, "source_grid_position", 0x00433BC4, 0xE5940050, "Stores the source position of a movable-grid interaction."},
    {0x0054, Oot3dPauseItemsFieldEncoding::S32, "destination_grid_position", 0x00434060, 0xE5940054, "Stores the destination position of a movable-grid interaction."},
    {0x0058, Oot3dPauseItemsFieldEncoding::S32, "animation_phase", 0x00433F64, 0xE5940058, "Selects the current swap-animation phase."},
    {0x005C, Oot3dPauseItemsFieldEncoding::S32, "animation_timer", 0x00433F44, 0xE594005C, "Counts native swap-animation updates."},
    {0x0060, Oot3dPauseItemsFieldEncoding::S32, "page_transition_mode", 0x00433D8C, 0xE5840060, "Stores the pending native page-transition mode."},
    {0x0064, Oot3dPauseItemsFieldEncoding::S32, "focused_grid_position", 0x00433BCC, 0xE5840064, "Stores the active 0..23 Items grid focus."},
    {0x0070, Oot3dPauseItemsFieldEncoding::S32, "arrow_choice_index", 0x00445A4C, 0xE5980070, "Stores the 0..3 arrow choice or the native negative sentinel."},
    {0x0078, Oot3dPauseItemsFieldEncoding::S32, "grid_cursor_column", 0x00445764, 0xE5980078, "Stores the ordinary Items grid column."},
    {0x007C, Oot3dPauseItemsFieldEncoding::S32, "grid_cursor_row", 0x00445758, 0xE598007C, "Stores the ordinary Items grid row."},
    {0x0080, Oot3dPauseItemsFieldEncoding::S32, "animation_step", 0x004460EC, 0xE5887080, "Counts arrow-selector or special-swap animation updates."},
    {0x0088, Oot3dPauseItemsFieldEncoding::S32, "animation_x", 0x0043409C, 0xE5840088, "Stores the current animation x coordinate."},
    {0x008C, Oot3dPauseItemsFieldEncoding::S32, "animation_y", 0x004340A0, 0xE584008C, "Stores the current animation y coordinate."},
    {0x0090, Oot3dPauseItemsFieldEncoding::ItemIdS32, "pending_arrow_item_id", 0x004460E8, 0xE5880090, "Stores ITEM_BOW or one of the three combined Bow-arrow values pending commit."},
    {0x0094, Oot3dPauseItemsFieldEncoding::S32, "snapshotted_grid_column", 0x00434268, 0xE5840094, "Snapshots the ordinary grid column for transition geometry."},
    {0x0098, Oot3dPauseItemsFieldEncoding::S32, "snapshotted_grid_row", 0x00434270, 0xE5840098, "Snapshots the ordinary grid row for transition geometry."},
    {0x009C, Oot3dPauseItemsFieldEncoding::S32, "arrow_choice_column", 0x00445998, 0x0588709C, "Stores the 0..1 selector column."},
    {0x00A0, Oot3dPauseItemsFieldEncoding::S32, "arrow_choice_row", 0x00445994, 0x058870A0, "Stores the 0..1 selector row."},
    {0x00A4, Oot3dPauseItemsFieldEncoding::S32, "horizontal_input_latch", 0x00433B98, 0xE58400A4, "Latches horizontal native Items input."},
    {0x00A8, Oot3dPauseItemsFieldEncoding::S32, "vertical_input_latch", 0x00433BAC, 0x158400A8, "Latches vertical native Items input."},
    {0x00AC, Oot3dPauseItemsFieldEncoding::S32, "suppress_focused_grid_icon", 0x00434894, 0xE58400AC, "Suppresses the ordinary focused icon while swap animation owns it."},
}};

constexpr std::array<Oot3dPauseItemsMutationDescriptor,
                     kOot3dPauseItemsMutationCount> kMutations{{
    {0x00434578, 0xEBFBF4BF, Oot3dPauseItemsMutationKind::ChangeEquipment, "SaveContext equipment", "source special cell resolves the first boot-equipment case", "Retain Inventory_ChangeEquipment before Player refresh."},
    {0x00434590, 0xEBFC52E9, Oot3dPauseItemsMutationKind::RefreshPlayerEquipment, "live Player equipment data", "after the first conditional equipment change", "Retain live Player reconciliation."},
    {0x004345AC, 0xEBFBF4B2, Oot3dPauseItemsMutationKind::ChangeEquipment, "SaveContext equipment", "source special cell resolves the second boot-equipment case", "Retain Inventory_ChangeEquipment before Player refresh."},
    {0x004345C4, 0xEBFC52DC, Oot3dPauseItemsMutationKind::RefreshPlayerEquipment, "live Player equipment data", "after the second conditional equipment change", "Retain live Player reconciliation."},
    {0x00434608, 0xEBFBF49B, Oot3dPauseItemsMutationKind::ChangeEquipment, "SaveContext equipment", "destination special cell resolves the first boot-equipment case", "Retain Inventory_ChangeEquipment before Player refresh."},
    {0x00434620, 0xEBFC52C5, Oot3dPauseItemsMutationKind::RefreshPlayerEquipment, "live Player equipment data", "after the third conditional equipment change", "Retain live Player reconciliation."},
    {0x0043463C, 0xEBFBF48E, Oot3dPauseItemsMutationKind::ChangeEquipment, "SaveContext equipment", "destination special cell resolves the second boot-equipment case", "Retain Inventory_ChangeEquipment before Player refresh."},
    {0x00434654, 0xEBFC52B8, Oot3dPauseItemsMutationKind::RefreshPlayerEquipment, "live Player equipment data", "after the fourth conditional equipment change", "Retain live Player reconciliation."},
    {0x0043466C, 0x05C073A2, Oot3dPauseItemsMutationKind::WriteChildGridMapping, "SaveContext.child_item_menu_slots[source]", "child layout active", "Swap menu mapping bytes; do not exchange Inventory.items."},
    {0x00434670, 0x15C0738A, Oot3dPauseItemsMutationKind::WriteAdultGridMapping, "SaveContext.adult_item_menu_slots[source]", "adult layout active", "Swap menu mapping bytes; do not exchange Inventory.items."},
    {0x00434688, 0x05C063A2, Oot3dPauseItemsMutationKind::WriteChildGridMapping, "SaveContext.child_item_menu_slots[destination]", "child layout active", "Swap menu mapping bytes; do not exchange Inventory.items."},
    {0x0043468C, 0x15C0638A, Oot3dPauseItemsMutationKind::WriteAdultGridMapping, "SaveContext.adult_item_menu_slots[destination]", "adult layout active", "Swap menu mapping bytes; do not exchange Inventory.items."},
    {0x00434694, 0xEBFC1EF0, Oot3dPauseItemsMutationKind::SyncPauseInventory, "pause inventory presentation", "after grid-mapping commit", "Synchronize after both mapping writes and equipment side effects."},
    {0x00446934, 0xE7C10006, Oot3dPauseItemsMutationKind::WriteBowInventoryItem, "SaveContext.inventory.items[gItemSlots[ITEM_BOW]]", "arrow commit animation reaches six updates", "Commit the chosen Bow variant through the native Bow slot."},
    {0x00446938, 0xE7C10004, Oot3dPauseItemsMutationKind::WriteBowInventoryCache, "SaveContext.cached_inventory_item_ids[gItemSlots[ITEM_BOW]]", "immediately after the live inventory write", "Keep the cache byte identical to the live Bow item."},
    {0x00446940, 0xEBFBD645, Oot3dPauseItemsMutationKind::SyncPauseInventory, "pause inventory presentation", "after Bow inventory and cache writes", "Synchronize only after both Bow values agree."},
}};

} // namespace

const std::array<Oot3dPauseItemsFunctionDescriptor, kOot3dPauseItemsFunctionCount>&
Oot3dPauseItemsFunctions() noexcept { return kFunctions; }

const Oot3dPauseItemsFunctionDescriptor* Oot3dPauseItemsFunction(std::uint32_t entry) noexcept {
    for (const auto& item : kFunctions) if (item.entry == entry) return &item;
    return nullptr;
}

const std::array<Oot3dPauseItemsPageStateDescriptor, kOot3dPauseItemsPageStateCount>&
Oot3dPauseItemsPageStates() noexcept { return kPageStates; }

const Oot3dPauseItemsPageStateDescriptor* Oot3dPauseItemsPageStateInfo(
    Oot3dPauseItemsPageState state) noexcept {
    for (const auto& item : kPageStates) if (item.state == state) return &item;
    return nullptr;
}

const std::array<Oot3dArrowTypeSelectorStateDescriptor, kOot3dArrowTypeSelectorStateCount>&
Oot3dArrowTypeSelectorStates() noexcept { return kArrowStates; }

const Oot3dArrowTypeSelectorStateDescriptor* Oot3dArrowTypeSelectorStateInfo(
    Oot3dArrowTypeSelectorState state) noexcept {
    for (const auto& item : kArrowStates) if (item.state == state) return &item;
    return nullptr;
}

const std::array<Oot3dArrowTypeChoiceDescriptor, kOot3dArrowTypeChoiceCount>&
Oot3dArrowTypeChoices() noexcept { return kArrowChoices; }

const Oot3dArrowTypeChoiceDescriptor* Oot3dArrowTypeChoiceInfo(
    Oot3dArrowTypeChoice choice) noexcept {
    for (const auto& item : kArrowChoices) if (item.choice == choice) return &item;
    return nullptr;
}

const std::array<Oot3dPauseItemsFieldDescriptor, kOot3dPauseItemsFieldCount>&
Oot3dPauseItemsFields() noexcept { return kFields; }

const Oot3dPauseItemsFieldDescriptor* Oot3dPauseItemsField(std::uint16_t offset) noexcept {
    for (const auto& item : kFields) if (item.offset == offset) return &item;
    return nullptr;
}

const std::array<Oot3dPauseItemsMutationDescriptor, kOot3dPauseItemsMutationCount>&
Oot3dPauseItemsMutations() noexcept { return kMutations; }

} // namespace oot3d::ui
