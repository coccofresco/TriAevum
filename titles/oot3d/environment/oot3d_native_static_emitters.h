#pragma once

#include "fast/renderer3ds/pica_emitter_catalog.h"
namespace oot3d::recomp::a32 {
class MemoryBus;
}
namespace Oot3dNativeGame {
using NativeStaticEmitterCategory = Fast::Renderer3ds::PicaEmitterCategory;
using NativeStaticEmitter = Fast::Renderer3ds::PicaRoomEmitter;
using NativeStaticEmitterCatalog = Fast::Renderer3ds::PicaEmitterCatalog;
inline constexpr uint32_t kNativeRoomFingerprintBytes = Fast::Renderer3ds::kPicaEmitterRoomFingerprintBytes;
inline uint64_t NativeRoomFingerprint(std::span<const uint8_t> bytes) noexcept {
    return Fast::Renderer3ds::PicaEmitterRoomFingerprint(bytes);
}
struct NativeLoadedRoom {
    int Number = -1;
    uint64_t Fingerprint = 0;
    uint32_t CommandsAddress = 0; // adapter-only, not a renderer payload
    bool operator==(const NativeLoadedRoom &) const = default;
};
[[nodiscard]] std::vector<NativeLoadedRoom> NativeLoadedRooms(uint32_t play,
                                                              oot3d::recomp::a32::MemoryBus &memory);
inline void CaptureStaticEmitters(const NativeStaticEmitterCatalog &catalog, uint16_t scene,
                                  std::span<const NativeLoadedRoom> rooms,
                                  std::vector<Fast::Renderer3ds::PicaScenePointLight> &lights,
                                  Fast::Renderer3ds::PicaSurfaceEmissionRegistry *emission = nullptr) {
    std::vector<Fast::Renderer3ds::PicaEmitterRoom> identities;
    for (const auto &room : rooms)
        identities.push_back({room.Number, room.Fingerprint});
    Fast::Renderer3ds::CapturePicaRoomEmitters(catalog, scene, identities, lights, emission);
}
} // namespace Oot3dNativeGame
