#pragma once

#include "oot3d_ui/ui_semantics.h"

#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

class GuestMemoryReader {
public:
    virtual ~GuestMemoryReader() = default;
    virtual bool Read(std::uint32_t address, void* destination, std::size_t size) const noexcept = 0;
};

struct UiStateCaptureResult {
    Oot3dUiSemanticState state;
    std::size_t fields_read = 0;
    std::size_t fields_missing = 0;
};

struct Oot3dUiGuestRoots {
    std::uint32_t save_context = 0;
    std::uint32_t pause_root = 0;
    std::uint32_t pause_items = 0;
    std::uint32_t pause_equipment = 0;
    std::uint32_t pause_quest = 0;
    std::uint32_t pause_world_map = 0;
    std::uint32_t pause_dungeon_map = 0;
    std::uint32_t pause_system_menu = 0;
    std::uint32_t pause_repeat_input = 0;
    std::uint32_t pause_touch_buttons = 0;
    std::uint32_t pause_touch_input = 0;
    std::uint32_t file_select = 0;
    std::uint32_t file_select_slot_valid = 0;
    std::uint32_t file_select_slot_buffers = 0;
    std::uint32_t name_entry = 0;
    std::uint32_t name_entry_buffer = 0;
};

// Builds the exact root set for the pinned code.bin.  Keeping addresses in the
// OoT3D adapter prevents any future N64 backend from learning guest layouts.
Oot3dUiGuestRoots BuildVerifiedOot3dUiGuestRoots(
    std::uint32_t save_context_address) noexcept;

// Captures only UI-relevant semantics. It never writes guest memory and does
// not participate in the native OoT3D update or render path.
UiStateCaptureResult CaptureOot3dUiState(const GuestMemoryReader& memory,
                                         std::uint32_t save_context_address) noexcept;

// Extended capture for live pause, file-select, and name-entry mechanics. The
// one-address overload above remains the SaveContext-only compatibility path.
UiStateCaptureResult CaptureOot3dUiState(
    const GuestMemoryReader& memory,
    const Oot3dUiGuestRoots& roots) noexcept;

} // namespace oot3d::ui
