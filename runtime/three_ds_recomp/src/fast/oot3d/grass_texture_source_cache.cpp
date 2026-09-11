#include "fast/oot3d/grass_texture_source_cache.h"
#include "fast/oot3d/texture_preview_artifact.h"
#include "fast/oot3d/grass_surface_extractor.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {
namespace {

uint64_t Fnv1a64(std::span<const uint8_t> bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (const uint8_t value : bytes) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t AliasKey(uint64_t hash, uint16_t width, uint16_t height) {
    hash ^= static_cast<uint64_t>(width) << 32U;
    hash ^= static_cast<uint64_t>(height) << 48U;
    return hash;
}

} // namespace

uint32_t GrassTextureColorSource::SamplePacked(float u, float v,
    GrassTextureWrap wrapS, GrassTextureWrap wrapT) const noexcept {
    if (!Grid || !std::isfinite(u) || !std::isfinite(v)) return 0U;
    constexpr int side = GrassTextureColorGrid::Side;
    const float x = WrapGrassTextureCoordinate(u, wrapS) * side - 0.5F;
    const float y = WrapGrassTextureCoordinate(v, wrapT) * side - 0.5F;
    const int x0 = wrapS == GrassTextureWrap::Repeat ? static_cast<int>(std::floor(x))
        : std::clamp(static_cast<int>(std::floor(x)), 0, side - 2);
    const int y0 = wrapT == GrassTextureWrap::Repeat ? static_cast<int>(std::floor(y))
        : std::clamp(static_cast<int>(std::floor(y)), 0, side - 2);
    const auto index = [](int i, int size, GrassTextureWrap wrap) {
        if (wrap == GrassTextureWrap::Clamp) return std::clamp(i, 0, size - 1);
        const int period = wrap == GrassTextureWrap::Mirror ? 2 * size : size;
        i = (i % period + period) % period;
        return i < size ? i : period - 1 - i;
    };
    // Only four adjacent candidates are needed to find the nearest two in a
    // regular grid. Read RGB only for the two selected points.
    struct Candidate { float DistanceSquared; int Index; };
    std::array<Candidate, 4> candidates{};
    for (int i = 0; i < 4; ++i) {
        const int ix = x0 + (i & 1), iy = y0 + (i >> 1);
        candidates[i] = {(x - ix) * (x - ix) + (y - iy) * (y - iy),
            index(iy, side, wrapT) * side + index(ix, side, wrapS)};
    }
    std::partial_sort(candidates.begin(), candidates.begin() + 2, candidates.end(),
        [](const auto& a, const auto& b) {
            return a.DistanceSquared == b.DistanceSquared ? a.Index < b.Index
                : a.DistanceSquared < b.DistanceSquared;
        });
    const float d0 = std::sqrt(candidates[0].DistanceSquared);
    const float d1 = std::sqrt(candidates[1].DistanceSquared);
    const float weight1 = d0 == 0.0F ? 0.0F : d0 / (d0 + d1);
    uint32_t packed = 0xff000000U;
    for (size_t c = 0; c < 3; ++c) {
        const float value = std::lerp(Grid->Rgb[candidates[0].Index][c],
            Grid->Rgb[candidates[1].Index][c], weight1);
        packed |= static_cast<uint32_t>(std::clamp(std::lround(value), 0L, 255L)) << (8U * c);
    }
    return packed;
}

GrassTextureSourceCache& GrassTextureSourceCache::Instance() {
    static GrassTextureSourceCache cache;
    return cache;
}

void GrassTextureSourceCache::ObserveDecoded(
    uint64_t rgba8Hash, uint16_t width, uint16_t height,
    std::span<const uint8_t> rgba8) {
    if (rgba8Hash == 0 || rgba8.size() !=
            static_cast<size_t>(width) * height * 4U) return;
    std::scoped_lock lock(mMutex);
    if (!mSources.contains(rgba8Hash)) {
        mSources.emplace(rgba8Hash, Source{width, height,
            std::make_shared<const std::vector<uint8_t>>(rgba8.begin(), rgba8.end())});
    }
    mDecodedToObserved.insert_or_assign(
        AliasKey(Fnv1a64(rgba8), width, height), rgba8Hash);
    ExportTexturePreviewArtifact(
        rgba8Hash, width, height, rgba8);
}

uint64_t GrassTextureSourceCache::ResolveObservedHash(
    uint64_t decodedRgba8Hash, uint16_t width, uint16_t height) const {
    std::scoped_lock lock(mMutex);
    const auto found = mDecodedToObserved.find(
        AliasKey(decodedRgba8Hash, width, height));
    return found == mDecodedToObserved.end() ? decodedRgba8Hash
                                             : found->second;
}

GrassScalarMask GrassTextureSourceCache::AcquireMask(
    uint64_t rgba8Hash, GrassSampleChannel channel) const {
    const auto shared = AcquireMaskShared(rgba8Hash, channel);
    return shared != nullptr ? *shared : GrassScalarMask{};
}

std::shared_ptr<const GrassScalarMask>
GrassTextureSourceCache::AcquireMaskShared(
    uint64_t rgba8Hash, GrassSampleChannel channel) const {
    std::scoped_lock lock(mMutex);
    const auto found = mSources.find(rgba8Hash);
    if (found == mSources.end()) return nullptr;
    const size_t channelIndex = static_cast<size_t>(channel);
    if (channelIndex >= found->second.Masks.size()) {
        return nullptr;
    }
    if (found->second.Masks[channelIndex] != nullptr) {
        return found->second.Masks[channelIndex];
    }
    auto result = std::make_shared<GrassScalarMask>(
        GrassScalarMask{
            found->second.Width, found->second.Height, channel});
    result->Samples.resize(
        static_cast<size_t>(result->Width) * result->Height);
    for (size_t index = 0; index < result->Samples.size(); ++index) {
        const uint8_t* rgba = found->second.Rgba8->data() + index * 4U;
        switch (channel) {
            case GrassSampleChannel::Red: result->Samples[index] = rgba[0]; break;
            case GrassSampleChannel::Green: result->Samples[index] = rgba[1]; break;
            case GrassSampleChannel::Blue: result->Samples[index] = rgba[2]; break;
            case GrassSampleChannel::Alpha: result->Samples[index] = rgba[3]; break;
            case GrassSampleChannel::Luminance:
                result->Samples[index] = static_cast<uint8_t>(std::clamp(
                    std::lround(0.2126 * rgba[0] + 0.7152 * rgba[1] +
                                0.0722 * rgba[2]), 0L, 255L));
                break;
        }
    }
    found->second.Masks[channelIndex] = result;
    return result;
}

GrassTexturePreview GrassTextureSourceCache::AcquirePreview(
    uint64_t rgba8Hash) const {
    std::scoped_lock lock(mMutex);
    const auto found = mSources.find(rgba8Hash);
    if (found == mSources.end()) return {};
    return {found->second.Width, found->second.Height,
            *found->second.Rgba8};
}

GrassTextureColorSource GrassTextureSourceCache::AcquireColorSource(uint64_t rgba8Hash) const {
    std::scoped_lock lock(mMutex);
    const auto found = mSources.find(rgba8Hash);
    if (found == mSources.end() || found->second.Width == 0 || found->second.Height == 0) return {};
    auto& source = found->second;
    if (!source.ColorGrid) {
        auto grid = std::make_shared<GrassTextureColorGrid>();
        constexpr uint32_t side = GrassTextureColorGrid::Side;
        // Area averages retain broad texture variation without baking individual
        // texel noise into blades. The 16 reference points are cell centers.
        for (uint32_t gy = 0; gy < side; ++gy) {
            const uint32_t y0 = gy * source.Height / side;
            const uint32_t y1 = std::max(y0 + 1U, (gy + 1U) * source.Height / side);
            for (uint32_t gx = 0; gx < side; ++gx) {
                const uint32_t x0 = gx * source.Width / side;
                const uint32_t x1 = std::max(x0 + 1U, (gx + 1U) * source.Width / side);
                std::array<double, 3> sum{}, opaqueSum{};
                double weight = 0;
                for (uint32_t y = y0; y < y1; ++y) {
                    for (uint32_t x = x0; x < x1; ++x) {
                        const auto* rgba = source.Rgba8->data() + (static_cast<size_t>(y) * source.Width + x) * 4U;
                        const double alpha = rgba[3] / 255.0;
                        for (size_t c = 0; c < 3; ++c) {
                            sum[c] += rgba[c] * alpha;
                            opaqueSum[c] += rgba[c];
                        }
                        weight += alpha;
                    }
                }
                for (size_t c = 0; c < 3; ++c) {
                    grid->Rgb[gy * side + gx][c] = static_cast<float>(weight > 0
                        ? sum[c] / weight : opaqueSum[c] / ((x1 - x0) * (y1 - y0)));
                }
            }
        }
        source.ColorGrid = std::move(grid);
    }
    return {source.ColorGrid};
}

std::optional<std::array<float, 3>>
GrassTextureSourceCache::AcquireAverageColor(
    uint64_t rgba8Hash) const {
    std::scoped_lock lock(mMutex);
    const auto found = mSources.find(rgba8Hash);
    if (found == mSources.end() || found->second.Rgba8->empty()) {
        return std::nullopt;
    }
    if (found->second.AverageColorComputed) {
        return found->second.AverageColor;
    }
    std::array<double, 3> weighted{};
    double totalWeight = 0.0;
    const size_t texelCount = found->second.Rgba8->size() / 4U;
    for (size_t texel = 0U; texel < texelCount; ++texel) {
        const uint8_t* rgba =
            found->second.Rgba8->data() + texel * 4U;
        const double weight =
            static_cast<double>(rgba[3]) / 255.0;
        for (size_t channel = 0U; channel < 3U; ++channel) {
            weighted[channel] +=
                static_cast<double>(rgba[channel]) * weight;
        }
        totalWeight += weight;
    }
    if (totalWeight <= 1.0e-9) {
        totalWeight = static_cast<double>(texelCount);
        for (size_t texel = 0U; texel < texelCount; ++texel) {
            const uint8_t* rgba =
                found->second.Rgba8->data() + texel * 4U;
            for (size_t channel = 0U; channel < 3U; ++channel) {
                weighted[channel] += rgba[channel];
            }
        }
    }
    std::array<float, 3> result{};
    for (size_t channel = 0U; channel < 3U; ++channel) {
        result[channel] = static_cast<float>(
            weighted[channel] / totalWeight / 255.0);
    }
    found->second.AverageColor = result;
    found->second.AverageColorComputed = true;
    return found->second.AverageColor;
}

void GrassTextureSourceCache::Clear() {
    std::scoped_lock lock(mMutex);
    mSources.clear();
    mDecodedToObserved.clear();
}

} // namespace Fast::Oot3d
