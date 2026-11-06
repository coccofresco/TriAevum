#include "oot3d_native_static_emitters.h"
#include "recomp/a32_runtime.h"
#include <algorithm>
#include <bit>

namespace Oot3dNativeGame {
std::vector<NativeLoadedRoom> NativeLoadedRooms(uint32_t playState, oot3d::recomp::a32::MemoryBus &memory) {
    std::vector<NativeLoadedRoom> rooms;
    if (playState == 0U)
        return rooms;
    // OoT3D EUR loaded room ABI; all catalog/strength mechanics are shared.
    for (const auto &[number, commands] : {std::pair{0x4C30U, 0x4FFCU}, std::pair{0x500CU, 0x53D8U}}) {
        uint8_t raw = 0;
        uint32_t installed = 0;
        if (!memory.Read8(playState + number, &raw) || !memory.Read32(playState + commands, &installed))
            continue;
        const int room = std::bit_cast<int8_t>(raw);
        if (room < 0 || installed == 0U ||
            std::any_of(rooms.begin(), rooms.end(), [&](const auto &r) { return r.Number == room; }))
            continue;
        std::array<uint8_t, kNativeRoomFingerprintBytes> bytes{};
        bool readable = true;
        for (uint32_t i = 0; i < bytes.size() && readable; ++i)
            readable = memory.Read8(installed + i, &bytes[i]);
        if (readable)
            rooms.push_back({room, NativeRoomFingerprint(bytes), installed});
    }
    return rooms;
}
} // namespace Oot3dNativeGame
