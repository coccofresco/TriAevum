#include "fast/oot3d/grass_texture_source_cache.h"
#include "fast/oot3d/texture_preview_artifact.h"

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
    mSources.try_emplace(rgba8Hash, Source{width, height,
        std::vector<uint8_t>(rgba8.begin(), rgba8.end())});
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
        const uint8_t* rgba = found->second.Rgba8.data() + index * 4U;
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
            found->second.Rgba8};
}

std::optional<std::array<float, 3>>
GrassTextureSourceCache::AcquireAverageColor(
    uint64_t rgba8Hash) const {
    std::scoped_lock lock(mMutex);
    const auto found = mSources.find(rgba8Hash);
    if (found == mSources.end() || found->second.Rgba8.empty()) {
        return std::nullopt;
    }
    if (found->second.AverageColorComputed) {
        return found->second.AverageColor;
    }
    std::array<double, 3> weighted{};
    double totalWeight = 0.0;
    const size_t texelCount = found->second.Rgba8.size() / 4U;
    for (size_t texel = 0U; texel < texelCount; ++texel) {
        const uint8_t* rgba =
            found->second.Rgba8.data() + texel * 4U;
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
                found->second.Rgba8.data() + texel * 4U;
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
