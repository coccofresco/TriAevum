#pragma once

#include "oot3d_ui/ui_semantics.h"

#include <cstddef>
#include <cstdint>

namespace oot3d::ui::guest_layout {

#pragma pack(push, 1)
struct ItemEquips {
    std::uint8_t button_items[kOot3dButtonItemCount];
    std::uint8_t button_slots[kOot3dAssignableButtonCount];
    std::uint8_t reserved_09;
    std::uint16_t equipment;
};

struct Inventory {
    std::uint8_t items[kOot3dInventoryItemCount];
    std::int8_t ammo[kOot3dAmmoCount];
    std::uint16_t equipment;
    std::uint32_t upgrades;
    std::uint32_t quest_items;
    std::uint8_t dungeon_items[kOot3dDungeonItemCount];
    std::int8_t dungeon_keys[kOot3dDungeonKeyCount];
    std::int8_t defense_hearts;
    std::int16_t gold_skulltula_tokens;
};
#pragma pack(pop)

static_assert(sizeof(ItemEquips) == 0x0C);
static_assert(offsetof(ItemEquips, button_items) == 0x00);
static_assert(offsetof(ItemEquips, button_slots) == 0x05);
static_assert(offsetof(ItemEquips, equipment) == 0x0A);
static_assert(sizeof(Inventory) == 0x5E);
static_assert(offsetof(Inventory, items) == 0x00);
static_assert(offsetof(Inventory, ammo) == 0x1A);
static_assert(offsetof(Inventory, equipment) == 0x2A);
static_assert(offsetof(Inventory, upgrades) == 0x2C);
static_assert(offsetof(Inventory, quest_items) == 0x30);
static_assert(offsetof(Inventory, dungeon_items) == 0x34);
static_assert(offsetof(Inventory, dungeon_keys) == 0x48);
static_assert(offsetof(Inventory, defense_hearts) == 0x5B);
static_assert(offsetof(Inventory, gold_skulltula_tokens) == 0x5C);

namespace save_context {

inline constexpr std::size_t kSize = 0x15C4;
inline constexpr std::size_t kEntranceIndex = 0x0000;
inline constexpr std::size_t kLinkAge = 0x0004;
inline constexpr std::size_t kCutsceneIndex = 0x0008;
inline constexpr std::size_t kDayTime = 0x000C;
inline constexpr std::size_t kMasterQuestFlag = 0x000E;
inline constexpr std::size_t kNightFlag = 0x0010;
inline constexpr std::size_t kPlayerName = 0x001C;
inline constexpr std::size_t kPlayerNameLength = 0x002C;
inline constexpr std::size_t kZTargetingSetting = 0x002D;
inline constexpr std::size_t kHealthCapacity = 0x0042;
inline constexpr std::size_t kHealth = 0x0044;
inline constexpr std::size_t kMagicLevel = 0x0046;
inline constexpr std::size_t kMagic = 0x0047;
inline constexpr std::size_t kRupees = 0x0048;
inline constexpr std::size_t kSwordHealth = 0x004A;
inline constexpr std::size_t kNaviTimer = 0x004C;
inline constexpr std::size_t kMagicAcquired = 0x004E;
inline constexpr std::size_t kDoubleMagicAcquired = 0x0050;
inline constexpr std::size_t kDoubleDefenseAcquired = 0x0051;
inline constexpr std::size_t kBiggoronSwordFlag = 0x0052;
inline constexpr std::size_t kChildEquips = 0x0054;
inline constexpr std::size_t kAdultEquips = 0x0060;
inline constexpr std::size_t kSavedSceneId = 0x007E;
inline constexpr std::size_t kCurrentEquips = 0x0080;
inline constexpr std::size_t kInventory = 0x008C;
inline constexpr std::size_t kGsFlags = 0x0EB4;
inline constexpr std::size_t kEventCheckInfo = 0x0EEC;
inline constexpr std::size_t kItemGetInfo = 0x0F08;
inline constexpr std::size_t kInfoTable = 0x0F10;
inline constexpr std::size_t kWorldMapAreaData = 0x0F50;
inline constexpr std::size_t kCachedInventoryItemIds = 0x1370;
inline constexpr std::size_t kChildItemMenuSlots = 0x138A;
inline constexpr std::size_t kAdultItemMenuSlots = 0x13A2;
inline constexpr std::size_t kBossBattleVictories = 0x1450;
inline constexpr std::size_t kBossBattleScores = 0x1474;
inline constexpr std::size_t kChecksum = 0x14D8;
inline constexpr std::size_t kFileNum = 0x14DC;
inline constexpr std::size_t kGameMode = 0x14E4;
inline constexpr std::size_t kRupeeAccumulator = 0x155C;
inline constexpr std::size_t kTimerState = 0x155E;
inline constexpr std::size_t kTimerSeconds = 0x1560;
inline constexpr std::size_t kSubTimerState = 0x1562;
inline constexpr std::size_t kSubTimerSeconds = 0x1564;
inline constexpr std::size_t kTimerX = 0x1566;
inline constexpr std::size_t kTimerY = 0x156A;
inline constexpr std::size_t kButtonStatus = 0x156F;
inline constexpr std::size_t kForceRisingButtonAlphas = 0x1576;
inline constexpr std::size_t kNextHudVisibilityMode = 0x1578;
inline constexpr std::size_t kHudVisibilityMode = 0x157A;
inline constexpr std::size_t kHudVisibilityModeTimer = 0x157C;
inline constexpr std::size_t kPreviousHudVisibilityMode = 0x157E;
inline constexpr std::size_t kMagicState = 0x1580;
inline constexpr std::size_t kPreviousMagicState = 0x1582;
inline constexpr std::size_t kMagicCapacity = 0x1584;
inline constexpr std::size_t kMagicFillTarget = 0x1586;
inline constexpr std::size_t kMagicTarget = 0x1588;
inline constexpr std::size_t kEventInfo = 0x158A;
inline constexpr std::size_t kMapIndex = 0x1592;
inline constexpr std::size_t kMinigameState = 0x1594;
inline constexpr std::size_t kMinigameScore = 0x1596;
inline constexpr std::size_t kNextCutsceneIndex = 0x15A0;
inline constexpr std::size_t kCutsceneTrigger = 0x15A2;
inline constexpr std::size_t kHealthAccumulator = 0x15B2;

} // namespace save_context

// The following owners and offsets are version-specific native OoT3D state.
// They are deliberately kept separate instead of projecting the monolithic
// N64 PauseContext layout onto the 3DS controllers.
namespace pause_root {
inline constexpr std::uint32_t kAddress = 0x005043D4;
inline constexpr std::size_t kSize = 0x28;
inline constexpr std::size_t kTouchPressed = 0x00;
inline constexpr std::size_t kTouchX = 0x02;
inline constexpr std::size_t kTouchY = 0x04;
inline constexpr std::size_t kLifecycleState = 0x14;
inline constexpr std::size_t kInteractionState = 0x18;
inline constexpr std::size_t kOptionalQuestPanelState = 0x1C;
inline constexpr std::size_t kOmoteUraSelectorEnabled = 0x20;
inline constexpr std::size_t kInitialized = 0x24;
} // namespace pause_root

namespace pause_equipment {
inline constexpr std::uint32_t kAddress = 0x0050446C;
inline constexpr std::size_t kSize = 0x70;
inline constexpr std::size_t kControllerState = 0x18;
inline constexpr std::size_t kTransitionFrame = 0x1C;
inline constexpr std::size_t kFocusSlot = 0x20;
inline constexpr std::size_t kDetailSlot = 0x24;
inline constexpr std::size_t kTouchSlot = 0x28;
inline constexpr std::size_t kTouchOpenRequestALatch = 0x30;
inline constexpr std::size_t kTouchOpenRequestBLatch = 0x34;
inline constexpr std::size_t kEquipAnimationStep = 0x38;
inline constexpr std::size_t kPendingEquipCategory = 0x40;
inline constexpr std::size_t kPendingEquipTier = 0x44;
inline constexpr std::size_t kEquipAnimationX = 0x48;
inline constexpr std::size_t kPendingItemId = 0x4C;
inline constexpr std::size_t kSuppressFocusedSlotEnlargement = 0x50;
} // namespace pause_equipment

namespace pause_items {
inline constexpr std::uint32_t kAddress = 0x005066F8;
inline constexpr std::size_t kSize = 0x4CC;
inline constexpr std::size_t kControllerState = 0x34;
inline constexpr std::size_t kArrowSelectorState = 0x38;
inline constexpr std::size_t kMode = 0x60;
inline constexpr std::size_t kSourceGridPosition = 0x50;
inline constexpr std::size_t kDestinationGridPosition = 0x54;
inline constexpr std::size_t kSelectedSlot = 0x64;
inline constexpr std::size_t kPreviousSlot = 0x68;
inline constexpr std::size_t kSelectedItemId = 0x6C;
inline constexpr std::size_t kArrowChoiceIndex = 0x70;
inline constexpr std::size_t kCursorColumn = 0x78;
inline constexpr std::size_t kCursorRow = 0x7C;
inline constexpr std::size_t kAnimationStep = 0x80;
inline constexpr std::size_t kSelectionMode = 0x84;
inline constexpr std::size_t kAnimationX = 0x88;
inline constexpr std::size_t kAnimationY = 0x8C;
inline constexpr std::size_t kPendingArrowItemId = 0x90;
inline constexpr std::size_t kSnapshottedGridColumn = 0x94;
inline constexpr std::size_t kSnapshottedGridRow = 0x98;
inline constexpr std::size_t kArrowChoiceColumn = 0x9C;
inline constexpr std::size_t kArrowChoiceRow = 0xA0;
inline constexpr std::size_t kHorizontalInputLatch = 0xA4;
inline constexpr std::size_t kVerticalInputLatch = 0xA8;
inline constexpr std::size_t kSuppressFocusedGridIcon = 0xAC;
} // namespace pause_items

namespace pause_dungeon_map {
inline constexpr std::uint32_t kAddress = 0x00506CB0;
inline constexpr std::size_t kSize = 0xAB0;
inline constexpr std::size_t kCursorSelection = 0x30;
inline constexpr std::size_t kControllerState = 0x38;
inline constexpr std::size_t kTransitionFrame = 0x44;
inline constexpr std::size_t kWorldDestination = 0x48;
inline constexpr std::size_t kHorizontalInputLatch = 0x54;
inline constexpr std::size_t kVerticalInputLatch = 0x58;
inline constexpr std::size_t kRestrictedScene = 0x5C;
inline constexpr std::size_t kFloorMetadata = 0x60;
} // namespace pause_dungeon_map

namespace pause_quest {
inline constexpr std::uint32_t kAddress = 0x004FC648;
inline constexpr std::size_t kSize = 0x19C;
inline constexpr std::size_t kPreviewVariantMode = 0x44;
inline constexpr std::size_t kSelectionState = 0x48;
inline constexpr std::size_t kSelectedEntry = 0x4C;
inline constexpr std::size_t kPreviousEntry = 0x50;
inline constexpr std::size_t kSelectionAnimationFrame = 0x54;
inline constexpr std::size_t kPromptMode = 0x58;
inline constexpr std::size_t kAuxiliarySelection = 0x64;
inline constexpr std::size_t kActive = 0x68;
} // namespace pause_quest

namespace pause_world_map {
inline constexpr std::uint32_t kAddress = 0x005093E4;
inline constexpr std::size_t kSize = 0xDFC;
inline constexpr std::size_t kControllerState = 0x14;
inline constexpr std::size_t kCursorColumn = 0x18;
inline constexpr std::size_t kCursorRow = 0x1C;
inline constexpr std::size_t kPreviousDestination = 0x20;
inline constexpr std::size_t kSelectionState = 0x24;
inline constexpr std::size_t kOverlayVisible = 0x28;
inline constexpr std::size_t kActiveAction = 0x2C;
inline constexpr std::size_t kMarkerAnimationFrame = 0x30;
inline constexpr std::size_t kMarkerVariant = 0x34;
inline constexpr std::size_t kTransitionFrame = 0x38;
inline constexpr std::size_t kDestinationAvailable = 0x3C;
inline constexpr std::size_t kDetailAvailable = 0x40;
inline constexpr std::size_t kEnabled = 0x44;
inline constexpr std::size_t kActiveInputMask = 0x48;
inline constexpr std::size_t kTouchOwned = 0x4C;
} // namespace pause_world_map

namespace pause_system_menu {
inline constexpr std::uint32_t kAddress = 0x0050A508;
inline constexpr std::size_t kSize = 0xA04;
inline constexpr std::size_t kSaveFlowState = 0x24;
inline constexpr std::size_t kMode = 0x28;
inline constexpr std::size_t kTransitionFrame = 0x2C;
inline constexpr std::size_t kOptionIndex = 0x30;
inline constexpr std::size_t kPrimaryChoice = 0x34;
inline constexpr std::size_t kSecondaryChoice = 0x38;
inline constexpr std::size_t kOptionsState = 0x3C;
inline constexpr std::size_t kLoadedOptionIndex = 0x40;
} // namespace pause_system_menu

namespace pause_repeat_input {
inline constexpr std::uint32_t kAddress = 0x0050AF0C;
inline constexpr std::size_t kSize = 0x28;
inline constexpr std::size_t kAnalogDirection = 0x00;
inline constexpr std::size_t kHeldButtons = 0x04;
inline constexpr std::size_t kAnalogPressed = 0x08;
inline constexpr std::size_t kButtonPressed = 0x0C;
inline constexpr std::size_t kAnalogHoldFrames = 0x10;
inline constexpr std::size_t kAnalogRepeatFrames = 0x14;
inline constexpr std::size_t kButtonHoldFrames = 0x18;
inline constexpr std::size_t kButtonRepeatFrames = 0x1C;
inline constexpr std::size_t kAnalogRepeated = 0x20;
inline constexpr std::size_t kButtonsRepeated = 0x24;
} // namespace pause_repeat_input

namespace pause_touch_buttons {
inline constexpr std::uint32_t kAddress = 0x0050AF34;
inline constexpr std::size_t kSize = 0x4D4;
inline constexpr std::size_t kPanelState = 0x34;
inline constexpr std::size_t kSelectedAction = 0x54;
inline constexpr std::size_t kPhase = 0x58;
inline constexpr std::size_t kSelection = 0x5C;
inline constexpr std::size_t kInputState = 0x60;
inline constexpr std::size_t kDisabledMask = 0x68;
inline constexpr std::size_t kDefaultItemId = 0x6C;
inline constexpr std::size_t kActionItemIds = 0x70;
inline constexpr std::size_t kFocusState = 0x78;
inline constexpr std::size_t kPlayerActionAllowed = 0x80;
} // namespace pause_touch_buttons

namespace pause_touch_input {
inline constexpr std::uint32_t kAddress = 0x0050BB38;
inline constexpr std::size_t kSize = 0x18;
inline constexpr std::size_t kTouchPressed = 0x00;
inline constexpr std::size_t kTouchX = 0x02;
inline constexpr std::size_t kTouchY = 0x04;
inline constexpr std::size_t kHeldMask = 0x06;
inline constexpr std::size_t kPressedMask = 0x08;
inline constexpr std::size_t kReleasedMask = 0x0A;
inline constexpr std::size_t kHoldFrames = 0x0C;
inline constexpr std::size_t kRepeatFrames = 0x0E;
inline constexpr std::size_t kRepeatedMask = 0x10;
} // namespace pause_touch_input

namespace file_select {
inline constexpr std::uint32_t kAddress = 0x00504FA0;
inline constexpr std::size_t kSize = 0x1758;
inline constexpr std::size_t kControllerState = 0x0010;
inline constexpr std::size_t kSelectedSlot = 0x0014;
inline constexpr std::size_t kConfirmationChoice = 0x0018;
inline constexpr std::size_t kCopySourceSlot = 0x001C;
inline constexpr std::size_t kCopyTargetSlot = 0x0020;
inline constexpr std::size_t kDeleteTargetSlot = 0x0024;
inline constexpr std::size_t kPromptTargetY = 0x0048;
inline constexpr std::size_t kPromptY = 0x004C;
inline constexpr std::size_t kPromptVelocityY = 0x0050;
inline constexpr std::uint32_t kSlotValidAddress = 0x0055BEA8;
inline constexpr std::uint32_t kSlotBuffersAddress = 0x0055BEC0;
inline constexpr std::size_t kSlotBufferStride = 0x15C4;
inline constexpr std::size_t kSlotFormatVersion = 0x002E;
inline constexpr std::size_t kSlotValidationFailed = 0x002F;
inline constexpr std::size_t kSlotInitializationMarker = 0x0036;
inline constexpr std::size_t kSlotHealthCapacity = 0x0042;
inline constexpr std::size_t kSlotHealth = 0x0044;
inline constexpr std::size_t kSlotQuestItemBits = 0x00BC;
inline constexpr std::size_t kSlotTimestampYear = 0x13BC;
inline constexpr std::size_t kSlotTimestampMonth = 0x13C0;
inline constexpr std::size_t kSlotTimestampDay = 0x13C4;
inline constexpr std::size_t kSlotTimestampHour = 0x13C8;
inline constexpr std::size_t kSlotTimestampMinute = 0x13CC;
inline constexpr std::size_t kSlotChecksum = 0x14D8;
} // namespace file_select

namespace name_entry {
inline constexpr std::uint32_t kAddress = 0x005077F0;
inline constexpr std::size_t kSize = 0x0834;
inline constexpr std::size_t kControllerState = 0x0004;
inline constexpr std::size_t kKeyboardPage = 0x0008;
inline constexpr std::size_t kPreviousKeyboardPage = 0x000C;
inline constexpr std::size_t kCursorColumn = 0x0010;
inline constexpr std::size_t kCursorRow = 0x0014;
inline constexpr std::size_t kTouchCursorX = 0x0018;
inline constexpr std::size_t kTouchCursorY = 0x001C;
inline constexpr std::size_t kLatinVariant = 0x0020;
inline constexpr std::size_t kNameLength = 0x0024;
inline constexpr std::size_t kActionChoice = 0x0028;
inline constexpr std::size_t kConfirmationChoice = 0x002C;
inline constexpr std::size_t kTouchKeyCode = 0x0030;
inline constexpr std::size_t kTouchColumn = 0x0034;
inline constexpr std::size_t kTouchRow = 0x0038;
inline constexpr std::size_t kSaveCommitTimer = 0x003C;
inline constexpr std::size_t kLanguageIndex = 0x0040;
inline constexpr std::size_t kTransitionFrame = 0x0044;
inline constexpr std::size_t kCursorBlinkTimer = 0x0048;
inline constexpr std::uint32_t kEditableNameAddress = 0x0055BE4C;
} // namespace name_entry

static_assert(pause_root::kInitialized + sizeof(std::uint32_t) == pause_root::kSize);
static_assert(pause_equipment::kSuppressFocusedSlotEnlargement +
                  sizeof(std::uint32_t) <= pause_equipment::kSize);
static_assert(pause_items::kSuppressFocusedGridIcon + sizeof(std::uint32_t) <= pause_items::kSize);
static_assert(pause_dungeon_map::kFloorMetadata + 8 <= pause_dungeon_map::kSize);
static_assert(pause_quest::kActive + sizeof(std::uint32_t) <= pause_quest::kSize);
static_assert(pause_world_map::kTouchOwned + sizeof(std::uint32_t) <= pause_world_map::kSize);
static_assert(pause_system_menu::kAddress + pause_system_menu::kSize ==
              pause_repeat_input::kAddress);
static_assert(pause_repeat_input::kAddress + pause_repeat_input::kSize ==
              pause_touch_buttons::kAddress);
static_assert(pause_touch_buttons::kActionItemIds + 6 <= pause_touch_buttons::kSize);
static_assert(pause_touch_input::kRepeatedMask + sizeof(std::uint16_t) <=
              pause_touch_input::kSize);
static_assert(file_select::kAddress + file_select::kSize == pause_items::kAddress);
static_assert(file_select::kPromptVelocityY + sizeof(float) <= file_select::kSize);
static_assert(file_select::kSlotBufferStride == save_context::kSize);
static_assert(file_select::kSlotChecksum + sizeof(std::uint16_t) <=
              file_select::kSlotBufferStride);
static_assert(name_entry::kAddress + name_entry::kSize == 0x00508024);
static_assert(name_entry::kCursorBlinkTimer + sizeof(std::int32_t) <= name_entry::kSize);

} // namespace oot3d::ui::guest_layout
