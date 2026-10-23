#pragma once

// Point lights for the fire and glows the EUR title draws but never lights:
// actors with no LightContext record, lit while their flame or glow is drawn
// (traced with Codex in the EUR code). Group 2, the flames group, as En_Light
// and the torches. Actors that insert a LightContext record (the Poes'
// lanterns, blue fire, King Dodongo's breath, warps) already light natively.

#include "fast/renderer3ds/pica_light_binding.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace oot3d::recomp::a32 {
class MemoryBus;
} // namespace oot3d::recomp::a32

namespace Oot3dNativeGame {

[[nodiscard]] bool IsNativeFireEmitterActor(uint16_t id) noexcept;

// Appends the lights of one fire actor (none, one, or one per jet or wall
// segment). Each light's identity is the actor's position address, with the
// emitter slot in the upper 32 bits for actors that light several points.
void CaptureNativeFireEmitters(oot3d::recomp::a32::MemoryBus& memory, uint32_t playState, uint32_t actor,
                               uint16_t id, uint16_t params,
                               std::vector<Fast::Renderer3ds::PicaScenePointLight>& lights);


} // namespace Oot3dNativeGame
