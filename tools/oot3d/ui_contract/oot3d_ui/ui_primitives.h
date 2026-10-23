#pragma once

#include "oot3d_ui/ui_backend_fwd.h"

#include <cstdint>
#include <string>

namespace oot3d::ui {

enum class UiPrimitiveRole : std::uint8_t {
    Unknown,
    Heart,
    MagicFrame,
    MagicFill,
    RupeeIcon,
    SmallKeyIcon,
    CounterDigit,
    ActionButton,
    AssignedItem,
    AmmoCounter,
    Minimap,
    Timer,
    MinigameScore,
    PauseBackground,
    PauseCursor,
    PauseItem,
    PauseEquipment,
    PauseQuestItem,
    PauseMap,
    PauseText,
    FileSlot,
    NameEntryGlyph,
    TouchControl,
    HorseStamina,
};

struct UiRect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct UiColor {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;
};

// The resolved guest resource/surface identity is primary. A texture hash may
// be useful trace evidence, but it is intentionally absent from this contract.
struct UiTextureIdentity {
    std::uint32_t guest_resource_address = 0;
    std::uint32_t guest_surface_address = 0;
    std::string semantic_name;
};

struct UiPrimitive {
    UiSubsystem subsystem{};
    UiPrimitiveRole role = UiPrimitiveRole::Unknown;
    std::uint32_t owner_address = 0;
    std::uint32_t descriptor_address = 0;
    std::uint32_t source_quad = 0;
    UiTextureIdentity texture;
    UiRect destination;
    UiRect uv;
    UiColor color;
    std::uint32_t layer = 0;
    bool visible = true;
    // The source crop is stored a quarter turn from the destination: corners
    // top-left, top-right, bottom-left, bottom-right sample (u0,v1), (u0,v0),
    // (u1,v1), (u1,v0) of the uv rectangle (top-left origin).
    bool uv_quarter_turn = false;
};

} // namespace oot3d::ui
