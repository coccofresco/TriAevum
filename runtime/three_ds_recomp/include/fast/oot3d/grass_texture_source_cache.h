#pragma once

#include "fast/oot3d/grass_types.h"

#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Fast::Oot3d {

struct GrassScalarMask {
    uint16_t Width = 0;
    uint16_t Height = 0;
    GrassSampleChannel Channel = GrassSampleChannel::Luminance;
    std::vector<uint8_t> Samples;
};

struct GrassTexturePreview {
    uint16_t Width = 0;
    uint16_t Height = 0;
    std::vector<uint8_t> Rgba8;
};

struct GrassTextureColorGrid {
    static constexpr uint32_t Side = 4U;
    std::array<std::array<float, 3>, Side * Side> Rgb{};
};

struct GrassTextureColorSource {
    std::shared_ptr<const GrassTextureColorGrid> Grid;

    [[nodiscard]] uint64_t ContentVersion() const noexcept;

    // Inverse-distance RGB from the two nearest grid points. High byte is validity.
    [[nodiscard]] uint32_t SamplePacked(float u, float v,
        GrassTextureWrap wrapS, GrassTextureWrap wrapT) const noexcept;
};

class GrassTextureSourceCache final {
  public:
    static GrassTextureSourceCache& Instance();
    void ObserveDecoded(uint64_t rgba8Hash, uint16_t width, uint16_t height,
                        std::span<const uint8_t> rgba8);
    // The PICA catalog identifies the encoded texture payload, while decoded
    // room models identify its RGBA8 payload. Resolve the latter back to the
    // catalog key so placement rules remain stable across both producers.
    [[nodiscard]] uint64_t ResolveObservedHash(
        uint64_t decodedRgba8Hash, uint16_t width, uint16_t height) const;
    [[nodiscard]] GrassScalarMask AcquireMask(
        uint64_t rgba8Hash, GrassSampleChannel channel) const;
    [[nodiscard]] std::shared_ptr<const GrassScalarMask>
    AcquireMaskShared(
        uint64_t rgba8Hash, GrassSampleChannel channel) const;
    [[nodiscard]] GrassTexturePreview AcquirePreview(uint64_t rgba8Hash) const;
    [[nodiscard]] GrassTextureColorSource AcquireColorSource(uint64_t rgba8Hash) const;
    // Alpha-weighted encoded RGB average. This intentionally stays in the
    // texture's color domain because the PICA color target is UNORM and the
    // grass color controls operate in that same domain.
    [[nodiscard]] std::optional<std::array<float, 3>>
    AcquireAverageColor(uint64_t rgba8Hash) const;
    void Clear();

  private:
    struct Source {
        uint16_t Width = 0;
        uint16_t Height = 0;
        std::shared_ptr<const std::vector<uint8_t>> Rgba8;
        mutable std::array<
            std::shared_ptr<const GrassScalarMask>, 5U>
            Masks;
        mutable std::optional<std::array<float, 3>> AverageColor;
        mutable bool AverageColorComputed = false;
        mutable std::shared_ptr<const GrassTextureColorGrid> ColorGrid;
    };
    mutable std::mutex mMutex;
    std::unordered_map<uint64_t, Source> mSources;
    std::unordered_map<uint64_t, uint64_t> mDecodedToObserved;
};

} // namespace Fast::Oot3d
