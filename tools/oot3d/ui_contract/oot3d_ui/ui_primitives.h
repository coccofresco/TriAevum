#pragma once

#include "oot3d_ui/ui_backend_fwd.h"

#include <array>
#include <cstdint>
#include <optional>
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

// One free corner: canvas position (top-left origin) and normalized UV.
struct UiVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
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
    // Optional non-rectangular quad, in strip order: triangles (0,1,2) and
    // (2,1,3). When present it replaces the destination/uv corners, and the
    // destination only bounds the quad.
    std::optional<std::array<UiVertex, 4>> corners;
    // The texture supplies coverage only: RGB comes from `color` and alpha
    // is the texel alpha times color.alpha (a combiner that takes the vertex
    // colour and the texture alpha).
    bool texture_alpha_only = false;
    // Post-filter texture alpha scale (a PICA TEV alpha scale): alpha is
    // clamp(scale x texel alpha x color.alpha, 0, 1). 1 for ordinary draws.
    float texture_alpha_scale = 1.0F;
};

} // namespace oot3d::ui
