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
CaptureMm3dEmitterRoom(uint32_t play, oot3d::recomp::a32::MemoryBus &memory) {
    if (!play || play > UINT32_MAX - 0xC274U)
        return std::nullopt;
    uint8_t number = 0;
    uint32_t segment = 0;
    if (!memory.Read8(play + 0xC260U, &number) || number > 127 || !memory.Read32(play + 0xC270U, &segment) ||
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
} // namespace Oot3dNativeGame
