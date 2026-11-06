#pragma once
#include "fast/renderer3ds/pica_light_binding.h"
#include "fast/renderer3ds/pica_surface_emission.h"
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace Fast::Renderer3ds {
enum class PicaEmitterCategory : uint8_t { FireLava, WindowLamp, LightShaft, MagicGlow, Lamp, Doorway };
struct PicaRoomEmitter {
    std::array<float, 3> Position{}, Colour{};
    float Radius = 0, Gain = 0;
    PicaEmitterCategory Category = PicaEmitterCategory::FireLava;
    std::array<uint64_t, 2> Surfaces{};
    bool IndependentEmission = false;
};
// No guest pointers or title layout in this payload.
struct PicaEmitterRoom {
    int Number = -1;
    uint64_t Fingerprint = 0;
};
inline constexpr uint32_t kPicaEmitterRoomFingerprintBytes = 128;
[[nodiscard]] uint64_t PicaEmitterRoomFingerprint(std::span<const uint8_t> bytes) noexcept;
class PicaEmitterCatalog {
  public:
    [[nodiscard]] static std::shared_ptr<const PicaEmitterCatalog> Parse(std::string_view text,
                                                                         std::string *error = nullptr);
    [[nodiscard]] static std::shared_ptr<const PicaEmitterCatalog> LoadFile(const std::filesystem::path &path,
                                                                            std::string *error = nullptr);
    // Untitled legacy catalogs remain valid for legacy callers. A title which
    // requests a namespace requires it, so another title's local catalog fails closed.
    [[nodiscard]] static std::shared_ptr<const PicaEmitterCatalog>
    FromEnvironment(std::string_view requiredTitle = {}, bool allowUntitled = false);
    [[nodiscard]] std::span<const PicaRoomEmitter> Room(uint16_t scene, int room, uint64_t fingerprint) const;
    [[nodiscard]] bool MatchesTitle(std::string_view title) const noexcept { return mTitle == title; }
    [[nodiscard]] size_t Count() const noexcept { return mCount; }

  private:
    std::string mTitle;
    std::map<std::tuple<uint16_t, int, uint64_t>, std::vector<PicaRoomEmitter>> mRooms;
    size_t mCount = 0;
};
// Same category, identity and native TEV-strength policy as the original
// room-light implementation. The title supplies file identity and semantic
// material facts; this mechanism knows neither scene lists nor texture names.
void CapturePicaRoomEmitters(const PicaEmitterCatalog &catalog, uint16_t scene,
                             std::span<const PicaEmitterRoom> rooms, std::vector<PicaScenePointLight> &lights,
                             PicaSurfaceEmissionRegistry *emission = nullptr);
} // namespace Fast::Renderer3ds
