#pragma once

#include "mm3d_scene_environment.h"
#include "fast/extension/grass_types.h"
#include <unordered_set>

namespace Oot3dNativeGame {
namespace Mm3dActorInteractionLayout {
// EUR code.bin 6eec27b1...; re-found by semantics, not N64 addresses.
// zeldaret-mm / 2S2H: actorLists[12], Actor.world/next/update/draw,
// CollisionCheckInfo cylinder; Player_Update's form-dependent body cylinder.
// MM3D uses FLOAT Cylinder16 dimensions/position; the generic colChkInfo
// dimensions remain s16. Player colChkInfo is only 10x10 for every form:
// use the live body cylinder, never an invented per-form radius/height table.
inline constexpr uint32_t Next = 0x134, Radius = 0xB4, Height = 0xB6, YShift = 0xB8;
inline constexpr uint32_t PlayerForm = 0x1FF, PlayerCylinder = 0x9BC;
inline constexpr uint32_t HorseType = 0x204;
inline constexpr uint8_t EponaType = 2; // MM's child horse, not bandit/donkey.
inline constexpr uint32_t CylinderOwner = 0, CylinderShape = 0x15, CylinderDimensions = 0x40;
struct Guard { uint32_t Address, Word; };
inline constexpr std::array<Guard, 19> Guards{{
    // Actor_RemoveFromCategory: list stride 12, count +10/head +14 in ActorContext.
    {0x001724AC, 0xE5D40002}, {0x001724B0, 0xE0800080},
    {0x001724C0, 0xE5801010}, {0x001724DC, 0xE5940134}, {0x001724E8, 0xE5810014},
    // CollisionCheck_SetInfo2 copies radius/height/YShift (+10/12/14).
    {0x004D6464, 0xE1C011B0}, {0x004D646C, 0xE1C011B2}, {0x004D6474, 0xE1C011B4},
    // Player init: body cylinder = player+800+1bc, owner = player.
    {0x001F5FC8, 0xE5D401FF},
    {0x00204318, 0xE2841B02}, {0x0020431C, 0xE2811F6F}, {0x00204324, 0xEB0024B0},
    // Player update stores native height / yShift at +A00/+A04.
    {0x00207254, 0xED8B0A80}, {0x00207328, 0xEDCB0A81},
    // Collider_UpdateCylinder copies Actor.world to float cylinder position +4C.
    {0x002183FC, 0xE5902024},
    // En_Horse update reads its type byte; init maps PARAM_4000 to child Epona.
    {0x002B5D04, 0xE5D00204}, {0x002B5D14, 0xE3500002},
    {0x0039B508, 0xE3A00002}, {0x0039B50C, 0xE5C40204},
}};
}

template<class Memory>
[[nodiscard]] bool Mm3dActorInteractionGuardsHold(Memory& memory) {
    for (const auto& g : Mm3dActorInteractionLayout::Guards) {
        uint32_t word = 0;
        if (!memory.Read32(g.Address, &word) || word != g.Word) return false;
    }
    return true;
}

template<class Memory>
[[nodiscard]] std::vector<Fast::Extension::GrassInteractor> CollectMm3dActorInteractions(
    Memory& memory, uint64_t sourceFrame, bool guardsHold) {
    using namespace Mm3dSceneEnvironmentLayout;
    using namespace Mm3dActorInteractionLayout;
    std::vector<Fast::Extension::GrassInteractor> out;
    if (!guardsHold) return out;
    const auto scene = CaptureMm3dSceneEnvironment(memory);
    if (!scene.State) return out;
    const auto play = scene.Play;
    const auto object = [](uint32_t a, uint32_t size) {
        return a != 0 && (a & 3) == 0 && a <= UINT32_MAX - size;
    };
    const auto f = [&](uint32_t a, float& value) {
        uint32_t bits = 0;
        if (!memory.Read32(a, &bits)) return false;
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    };
    std::unordered_set<uint32_t> seen;
    for (uint32_t category = 0; category < 12; ++category) {
        uint32_t count = 0, actor = 0;
        const auto list = play + ActorLists + category * ActorListSize;
        if (!memory.Read32(list, &count) || count > 255 || !memory.Read32(list + First, &actor)) continue;
        for (uint32_t i = 0; i < count && actor != 0; ++i) {
            uint32_t next = 0, calc = 0, draw = 0;
            uint16_t id = 0;
            uint8_t type = 0;
            if (!object(actor, ActorSize) || !seen.insert(actor).second ||
                !memory.Read16(actor, &id) || !memory.Read8(actor + ActorCategory, &type) || type != category ||
                !memory.Read32(actor + Next, &next) || !memory.Read32(actor + ActorCalc, &calc) ||
                !memory.Read32(actor + ActorDraw, &draw)) break;
            Fast::Extension::GrassInteractor contact;
            bool valid = calc != 0 && draw != 0;
            for (uint32_t axis = 0; axis < 3; ++axis)
                valid = f(actor + ActorPosition + axis * 4, contact.Position[axis]) && valid;
            float radius = 0, height = 0, shift = 0;
            if (id == 0) {
                uint32_t owner = 0;
                uint8_t form = 0, shape = 0;
                const auto cylinder = actor + PlayerCylinder;
                valid = valid && type == 2 && object(actor, PlayerCylinder + 0x58) &&
                    memory.Read8(actor + PlayerForm, &form) && form < 5 &&
                    memory.Read32(cylinder + CylinderOwner, &owner) && owner == actor &&
                    memory.Read8(cylinder + CylinderShape, &shape) && shape == 1 &&
                    f(cylinder + CylinderDimensions, radius) && f(cylinder + CylinderDimensions + 4, height) &&
                    f(cylinder + CylinderDimensions + 8, shift);
            } else {
                uint16_t r = 0, h = 0, y = 0;
                valid = valid && memory.Read16(actor + Radius, &r) && memory.Read16(actor + Height, &h) &&
                    memory.Read16(actor + YShift, &y);
                radius = float(std::bit_cast<int16_t>(r));
                height = float(std::bit_cast<int16_t>(h));
                shift = float(std::bit_cast<int16_t>(y));
            }
            if (valid && radius > 0 && height > 0 && std::isfinite(contact.Position[1] + shift)) {
                using Kind = Fast::Extension::GrassInteractor::Kind;
                uint8_t horseType = 0xFF;
                const bool epona = id == 0x000D && object(actor, HorseType + 1) &&
                    memory.Read8(actor + HorseType, &horseType) && horseType == EponaType;
                contact.Type = id == 0 ? Kind::PlayerLink : epona ? Kind::Epona : Kind::SceneActor;
                contact.StableId = (uint64_t{id} << 32) | actor;
                contact.ContextId = play;
                contact.Position[1] += shift;
                contact.PreviousPosition = contact.Position;
                contact.Radius = radius;
                contact.HalfHeight = height * 0.5F;
                contact.FrameId = sourceFrame;
                out.push_back(contact);
            }
            actor = next;
        }
    }
    return out;
}
} // namespace Oot3dNativeGame
