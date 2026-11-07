#pragma once

#include "fast/renderer3ds/pica_emitter_catalog.h"
#include "recomp/a32_runtime.h"
#include <algorithm>
#include <optional>

namespace Oot3dNativeGame {
// EUR ZSI v9. The loader relocates ActorList/Mesh pointers and sets their
// relocation halfword. Hash only the command header, normalising those two
// fields; never hash the runtime-patched model/resource bytes after End.
// Offline ROM/native agreement: analysis/parity/DL/room_fingerprint_proof.txt.
[[nodiscard]] inline std::optional<uint64_t> Mm3dRoomFingerprint(std::span<const uint8_t> bytes) {
    if (bytes.size() < 24 || bytes[0] != 'Z' || bytes[1] != 'S' || bytes[2] != 'I' || bytes[3] != 9)
        return std::nullopt;
    std::vector<uint8_t> header(bytes.begin(), bytes.begin() + 16);
    for (size_t at = 16; at + 8 <= bytes.size(); at += 8) {
        const auto begin = header.size();
        header.insert(header.end(), bytes.begin() + at, bytes.begin() + at + 8);
        if (bytes[at] == 1 || bytes[at] == 0xA)
            std::fill(header.begin() + begin + 2, header.end(), 0);
        if (bytes[at] == 0x14)
            return Fast::Renderer3ds::PicaEmitterRoomFingerprint(header);
    }
    return std::nullopt;
}
[[nodiscard]] inline std::optional<Fast::Renderer3ds::PicaEmitterRoom>
CaptureMm3dEmitterRoom(uint32_t play, oot3d::recomp::a32::MemoryBus &memory,
                      uint32_t roomOffset = 0xC260U) {
    if ((roomOffset != 0xC260U && roomOffset != 0xC2C4U) ||
        !play || play > UINT32_MAX - 0xC334U)
        return std::nullopt;
    uint8_t number = 0;
    uint32_t segment = 0;
    uint32_t request = 0;
    // RequestRoom changes current.number before installing its segment. The
    // previous record remains drawable while that request is outstanding.
    if (roomOffset == 0xC260U && (!memory.Read32(play + 0xC330U, &request) || request))
        return std::nullopt;
    if (!memory.Read8(play + roomOffset, &number) || number > 127 || !memory.Read32(play + roomOffset + 0x10U, &segment) ||
        !segment || segment > UINT32_MAX - 272U)
        return std::nullopt;
    // At most 32 commands. Stop at End so unrelated, mutable model bytes
    // cannot become identity, and a truncated/unreadable header fails closed.
    std::vector<uint8_t> header;
    for (uint32_t at = 0; at < 272U; ++at) {
        uint8_t byte = 0;
        if (!memory.Read8(segment + at, &byte))
            return std::nullopt;
        header.push_back(byte);
        if (at >= 23 && at % 8 == 7 && header[at - 7] == 0x14) {
            const auto hash = Mm3dRoomFingerprint(header);
            if (hash)
                return Fast::Renderer3ds::PicaEmitterRoom{number, *hash};
            return std::nullopt;
        }
    }
    return std::nullopt;
}
// EUR RequestRoom copies all 0x64 bytes current -> previous, including number
// and segment; unload clears segment and writes number=-1. Draw submits both.
// Meaning-based disassembly: DL/wave2/{room-request,room-unload,room-writers}.txt.
inline constexpr std::array<std::array<uint32_t, 2>, 10> kMm3dRoomEmitterGuards{{
    {0x15EEF4U, 0xE3A02003U}, {0x15EEF8U, 0xE2811E26U},
    {0x15EF0CU, 0xE2811FB1U}, {0x1F5134U, 0xE59100D0U},
    {0x1F51DCU, 0xE2841064U}, {0x1F51E4U, 0xE5C40064U},
    {0x1F5234U, 0xE5943010U}, {0x1F523CU, 0xE5813010U},
    {0x22E290U, 0xE5887010U}, {0x22E294U, 0xE5C80000U}
}};
[[nodiscard]] inline std::vector<Fast::Renderer3ds::PicaEmitterRoom>
CaptureMm3dEmitterRooms(uint32_t play, oot3d::recomp::a32::MemoryBus& memory) {
    for (const auto& guard : kMm3dRoomEmitterGuards) {
        uint32_t word = 0;
        if (!memory.Read32(guard[0], &word) || word != guard[1]) return {};
    }
    std::vector<Fast::Renderer3ds::PicaEmitterRoom> rooms;
    for (uint32_t offset : {0xC260U, 0xC2C4U})
        if (auto room = CaptureMm3dEmitterRoom(play, memory, offset)) rooms.push_back(*room);
    // Shared capture owns exact duplicates and contradictory room identities.
    return rooms;
}
} // namespace Oot3dNativeGame
