#pragma once

#include "fast/oot3d/grass_texture_twins.h"
#include "fast/oot3d/grass_types.h"

#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
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

// Shared interpolation stencil. Points are in the wrapped 4x4 grid's
// coordinate system (including repeat-border neighbours); Indices address RGB.
struct GrassTextureGridSample {
    std::array<std::array<int32_t, 2>, 2> Points{};
    std::array<uint32_t, 2> Indices{};
    float Weight = 0.0F;
    bool Valid = false;
};

[[nodiscard]] GrassTextureGridSample ResolveGrassTextureGridSample(
    float u, float v, GrassTextureWrap wrapS, GrassTextureWrap wrapT) noexcept;
[[nodiscard]] std::array<float, 2> UnwrapGrassTextureGridPoint(
    const std::array<int32_t, 2>& point, const std::array<float, 2>& uv,
    GrassTextureWrap wrapS, GrassTextureWrap wrapT) noexcept;

struct GrassTextureColorSource {
    std::shared_ptr<const GrassTextureColorGrid> Grid;

    [[nodiscard]] uint64_t ContentVersion() const noexcept;

    // Inverse-distance RGB from the two nearest grid points. High byte is validity.
    [[nodiscard]] uint32_t SamplePacked(float u, float v,
        GrassTextureWrap wrapS, GrassTextureWrap wrapT) const noexcept;
    [[nodiscard]] uint32_t SamplePacked(const GrassTextureGridSample& sample) const noexcept;
};

class GrassTextureSourceCache final : public GrassTextureSignatureSource {
  public:
    static GrassTextureSourceCache& Instance();
    void ObserveDecoded(uint64_t rgba8Hash, uint16_t width, uint16_t height,
                        std::span<const uint8_t> rgba8);
    // The PICA catalog identifies the encoded texture payload, while decoded
    // room models identify its RGBA8 payload. Resolve the latter back to the
    // catalog key so placement rules remain stable across both producers.
    // The decoded index is built on first lookup for each dimension pair;
    // native-key previews, masks and colors do not require it.
    [[nodiscard]] uint64_t ResolveObservedHash(
        uint64_t decodedRgba8Hash, uint16_t width, uint16_t height) const;
    [[nodiscard]] GrassScalarMask AcquireMask(
        uint64_t rgba8Hash, GrassSampleChannel channel) const;
    [[nodiscard]] std::shared_ptr<const GrassScalarMask>
    AcquireMaskShared(
        uint64_t rgba8Hash, GrassSampleChannel channel) const;
    [[nodiscard]] bool Contains(uint64_t hash) const;
    [[nodiscard]] GrassTexturePreview AcquirePreview(uint64_t rgba8Hash) const;
    [[nodiscard]] GrassTextureColorSource AcquireColorSource(uint64_t rgba8Hash) const;
    // Alpha-weighted encoded RGB average. This intentionally stays in the
    // texture's color domain because the PICA color target is UNORM and the
    // grass color controls operate in that same domain.
    [[nodiscard]] std::optional<std::array<float, 3>>
    AcquireAverageColor(uint64_t rgba8Hash) const;
    // Signatures of the observed textures and of those kept from earlier
    // sessions; persist keeps one in the store (rule textures).
    [[nodiscard]] std::shared_ptr<const GrassTextureSignature> Find(
        uint64_t rgba8Hash, bool persist) override;
    [[nodiscard]] uint64_t Generation() const override;
    // Loads the kept signatures; new kept ones are appended to this file.
    void SetSignatureStore(std::filesystem::path path);
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
        mutable std::optional<uint64_t> DecodedAliasKey;
        std::shared_ptr<const GrassTextureSignature> Signature;
        uint64_t LastObservation = 0;
    };
    mutable std::mutex mMutex;
    std::unordered_map<uint64_t, Source> mSources;
    struct DecodedAlias {
        uint64_t NativeHash = 0;
        uint64_t LastObservation = 0;
    };
    void RecordDecodedAlias(uint64_t alias, uint64_t nativeHash, uint64_t observation) const;
    mutable std::unordered_map<uint64_t, DecodedAlias> mDecodedToObserved;
    uint64_t mObservationSerial = 0;
    uint64_t mGeneration = 0;
    std::filesystem::path mSignatureStore;
    std::unordered_map<uint64_t, std::shared_ptr<const GrassTextureSignature>> mStoredSignatures;
    mutable std::unordered_set<uint32_t> mDecodedAliasDimensions;
};

} // namespace Fast::Oot3d
