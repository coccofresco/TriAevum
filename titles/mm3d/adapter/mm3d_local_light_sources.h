#pragma once

#include "recomp/a32_runtime.h"
#include <array>
#include <cstdint>

namespace Oot3dNativeGame {

// EUR v1.1, source/node ownership re-found from native colour/radius writers
// and destructors. N64 supplies semantic names, never ids or field offsets.
// Evidence: local analysis/parity/DL/{actor_disassembly,source_writer_guards}.txt.
struct Mm3dLocalLightSource {
    uint16_t Actor;
    uint8_t Category;
    uint32_t Info, Node, Update, Destroy;
    std::array<std::array<uint32_t,2>,4> Guards;
};
inline constexpr std::array kMm3dLocalLightSources{
    Mm3dLocalLightSource{4,7,0x200,0x1FC,0x3FE1BC,0x39E1DC,{{
        {0x39E1FC,0xE59421FC},{0x3FE270,0xE2840C02},
        {0x3FE274,0xEBF855E2},{0x213A18,0xE1C041B6}}}},
    Mm3dLocalLightSource{0x2E,6,0x2B0,0x2AC,0x4AA8D4,0x467A08,{{
        {0x467A30,0xE59422AC},{0x4AA684,0xE2840E2B},
        {0x4AA688,0xEBF5A4DD},{0x213A18,0xE1C041B6}}}},
    Mm3dLocalLightSource{0x10,7,0x578,0x590,0x47B16C,0x31F844,{{
        {0x31F854,0xE5902590},{0x31F864,0xEBFB4609},
        {0x31F868,0xE59525AC},{0x31F874,0xEBFB4605}}}},
    Mm3dLocalLightSource{0x10,7,0x594,0x5AC,0x47B16C,0x31F844,{{
        {0x31F854,0xE5902590},{0x31F864,0xEBFB4609},
        {0x31F868,0xE59525AC},{0x31F874,0xEBFB4605}}}},
};

// Called only after current-frame LightNode -> LightInfo ownership has been
// checked. A different image, reused actor, or unproved variant stays native.
[[nodiscard]] inline bool Mm3dLocalLightSourceProved(oot3d::recomp::a32::MemoryBus& memory,
    uint32_t actor, uint32_t infoOffset, uint32_t nodeOffset) {
    uint16_t id = 0;
    uint8_t category = 0;
    uint32_t update = 0, destroy = 0;
    if (!memory.Read16(actor,&id) || !memory.Read8(actor+2,&category) ||
        !memory.Read32(actor+0x140,&update) || !memory.Read32(actor+0x13C,&destroy)) return false;
    for (const auto& source : kMm3dLocalLightSources) {
        if (id != source.Actor || category != source.Category || infoOffset != source.Info ||
            nodeOffset != source.Node || update != source.Update || destroy != source.Destroy) continue;
        for (const auto& guard : source.Guards) {
            uint32_t word = 0;
            if (!memory.Read32(guard[0],&word) || word != guard[1]) return false;
        }
        return true;
    }
    return false;
}

} // namespace Oot3dNativeGame
