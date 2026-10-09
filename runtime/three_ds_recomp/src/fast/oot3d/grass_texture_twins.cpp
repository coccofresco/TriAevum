#include "fast/oot3d/grass_texture_twins.h"
#include "fast/oot3d/grass_texture_source_cache.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <istream>
#include <ostream>

namespace Fast::Oot3d {
namespace {

constexpr uint32_t kSide = GrassTextureSignature::Side;
constexpr std::array<char, 4> kRecordTag{'G', 'T', 'S', '1'};

uint8_t Luminance(const uint8_t* rgba) noexcept {
    // As the luminance mask channel.
    return static_cast<uint8_t>(std::clamp(
        std::lround(0.2126 * rgba[0] + 0.7152 * rgba[1] + 0.0722 * rgba[2]), 0L, 255L));
}

// Wrapped box blur of radius 2 along x (step 1) or y (step kSide).
void BoxBlur(const std::array<float, kSide * kSide>& input, std::array<float, kSide * kSide>& output,
             bool vertical) noexcept {
    for (uint32_t y = 0; y < kSide; ++y)
        for (uint32_t x = 0; x < kSide; ++x) {
            float sum = 0.0F;
            for (int k = -2; k <= 2; ++k) {
                const uint32_t sx = vertical ? x : (x + kSide + k) % kSide;
                const uint32_t sy = vertical ? (y + kSide + k) % kSide : y;
                sum += input[sy * kSide + sx];
            }
            output[y * kSide + x] = sum / 5.0F;
        }
}

uint64_t Mix(uint64_t hash, uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64; shift += 8)
        hash = (hash ^ ((value >> shift) & 255U)) * 1099511628211ULL;
    return hash;
}

template <class T>
void WriteValue(std::ostream& output, const T& value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <class T>
bool ReadValue(std::istream& input, T& value) {
    return static_cast<bool>(input.read(reinterpret_cast<char*>(&value), sizeof(value)));
}

bool TargetsTexture(const GrassPlacementRule& rule, uint64_t hash, uint16_t width, uint16_t height) noexcept {
    return rule.Target.Rgba8Hash == hash && (rule.Target.Width == 0U || rule.Target.Width == width) &&
           (rule.Target.Height == 0U || rule.Target.Height == height);
}

} // namespace

GrassTextureSignature ComputeGrassTextureSignature(
    uint16_t width, uint16_t height, std::span<const uint8_t> rgba8) {
    GrassTextureSignature signature;
    signature.Width = width;
    signature.Height = height;
    const size_t pixels = static_cast<size_t>(width) * height;
    if (pixels == 0U || rgba8.size() < pixels * 4U) return signature;

    // Area averages of the luminance on a 64x64 grid.
    std::array<float, kSide * kSide> luminance{};
    for (uint32_t gy = 0; gy < kSide; ++gy) {
        const uint32_t y0 = gy * height / kSide;
        const uint32_t y1 = std::max(y0 + 1U, (gy + 1U) * height / kSide);
        for (uint32_t gx = 0; gx < kSide; ++gx) {
            const uint32_t x0 = gx * width / kSide;
            const uint32_t x1 = std::max(x0 + 1U, (gx + 1U) * width / kSide);
            float sum = 0.0F;
            for (uint32_t y = y0; y < y1; ++y)
                for (uint32_t x = x0; x < x1; ++x)
                    sum += Luminance(rgba8.data() + (static_cast<size_t>(y) * width + x) * 4U);
            luminance[gy * kSide + gx] = sum / static_cast<float>((x1 - x0) * (y1 - y0));
        }
    }
    // Detail: the luminance less its blur (twice a radius-2 box), so that tint
    // and broad shading do not count, only the pattern.
    std::array<float, kSide * kSide> blur = luminance, scratch{};
    for (int pass = 0; pass < 2; ++pass) {
        BoxBlur(blur, scratch, false);
        BoxBlur(scratch, blur, true);
    }
    float largest = 0.0F;
    for (size_t i = 0; i < luminance.size(); ++i) {
        luminance[i] -= blur[i];
        largest = std::max(largest, std::abs(luminance[i]));
    }
    const float scale = largest > 1.0e-3F ? 127.0F / largest : 0.0F;
    for (size_t i = 0; i < luminance.size(); ++i)
        signature.Detail[i] = static_cast<int8_t>(std::lround(luminance[i] * scale));

    // Channel ranges (Red, Green, Blue, Alpha, Luminance).
    std::array<std::array<uint32_t, 256>, 5> histograms{};
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t* rgba = rgba8.data() + i * 4U;
        for (size_t c = 0; c < 4U; ++c) ++histograms[c][rgba[c]];
        ++histograms[4][Luminance(rgba)];
    }
    for (size_t c = 0; c < histograms.size(); ++c) {
        const std::array<double, 2> fractions{0.2, 0.8};
        for (size_t p = 0; p < 2U; ++p) {
            const double wanted = fractions[p] * static_cast<double>(pixels);
            double cumulative = 0.0;
            uint32_t value = 0U;
            for (; value < 255U; ++value) {
                cumulative += histograms[c][value];
                if (cumulative >= wanted) break;
            }
            signature.Range[c][p] = static_cast<uint8_t>(value);
        }
    }
    return signature;
}

float GrassTextureTwinScore(
    const GrassTextureSignature& a, const GrassTextureSignature& b) noexcept {
    if (std::min({a.Width, a.Height, b.Width, b.Height}) < 16U ||
        static_cast<uint32_t>(a.Width) * b.Height != static_cast<uint32_t>(a.Height) * b.Width ||
        std::max(a.Width, b.Width) > 2U * std::min(a.Width, b.Width))
        return -1.0F;
    constexpr uint32_t tiles = 4U, tile = kSide / tiles;
    std::array<float, tiles * tiles> scores{};
    for (uint32_t ty = 0; ty < tiles; ++ty)
        for (uint32_t tx = 0; tx < tiles; ++tx) {
            float meanA = 0.0F, meanB = 0.0F;
            for (uint32_t y = 0; y < tile; ++y)
                for (uint32_t x = 0; x < tile; ++x) {
                    const size_t i = (ty * tile + y) * kSide + tx * tile + x;
                    meanA += a.Detail[i];
                    meanB += b.Detail[i];
                }
            meanA /= tile * tile;
            meanB /= tile * tile;
            float ab = 0.0F, aa = 0.0F, bb = 0.0F;
            for (uint32_t y = 0; y < tile; ++y)
                for (uint32_t x = 0; x < tile; ++x) {
                    const size_t i = (ty * tile + y) * kSide + tx * tile + x;
                    const float da = a.Detail[i] - meanA, db = b.Detail[i] - meanB;
                    ab += da * db;
                    aa += da * da;
                    bb += db * db;
                }
            // A flat tile (a patch of one colour) matches nothing.
            scores[ty * tiles + tx] = aa > 1.0F && bb > 1.0F ? ab / std::sqrt(aa * bb) : 0.0F;
        }
    std::sort(scores.begin(), scores.end());
    return 0.5F * (scores[7] + scores[8]);
}

GrassPlacementRule InheritGrassRule(
    const GrassPlacementRule& source, uint64_t twinHash, uint16_t twinWidth,
    uint16_t twinHeight, float sampleScale, float sampleOffset) {
    GrassPlacementRule rule = source;
    rule.Target.AssetName.clear();
    rule.Target.Rgba8Hash = twinHash;
    rule.Target.Width = twinWidth;
    rule.Target.Height = twinHeight;
    rule.InheritedFrom = source.RuleId;
    const uint64_t id = Mix(Mix(14695981039346656037ULL, source.RuleId), twinHash);
    rule.RuleId = kGrassInheritedRuleIdBit | (static_cast<uint32_t>(id ^ (id >> 32U)) & ~kGrassInheritedRuleIdBit);
    rule.SampleScale = sampleScale;
    rule.SampleOffset = sampleOffset;
    return rule;
}

GrassPlacementRule DeriveGrassTwinRule(
    const GrassPlacementRule& source,
    const GrassTextureSignature& sourceSignature,
    const GrassTextureSignature& twinSignature,
    uint64_t twinHash) {
    // The twin's channel range onto the source's: s = scale * t + offset.
    const size_t channel = std::min<size_t>(static_cast<size_t>(source.Channel), 4U);
    const float s0 = sourceSignature.Range[channel][0] / 255.0F, s1 = sourceSignature.Range[channel][1] / 255.0F;
    const float t0 = twinSignature.Range[channel][0] / 255.0F, t1 = twinSignature.Range[channel][1] / 255.0F;
    constexpr float flat = 2.0F / 255.0F;
    const float scale = s1 - s0 >= flat && t1 - t0 >= flat ? (s1 - s0) / (t1 - t0) : 1.0F;
    return InheritGrassRule(source, twinHash, twinSignature.Width, twinSignature.Height, scale,
                            s0 - scale * t0);
}

GrassPlacementRule OwnGrassRuleFromInherited(
    const GrassPlacementRule& inherited, uint32_t ruleId) {
    GrassPlacementRule rule = inherited;
    rule.RuleId = ruleId;
    rule.InheritedFrom = 0U;
    // Levels act on s = a t + b (then the inversion): fold the mapping into
    // them. Inverted, 1 - (a t + b) = a (1 - t) + (1 - a - b).
    const float a = std::max(inherited.SampleScale, 1.0e-3F);
    const float b = inherited.Invert ? 1.0F - a - inherited.SampleOffset : inherited.SampleOffset;
    rule.InputBlack = std::clamp((inherited.InputBlack - b) / a, 0.0F, 0.999F);
    rule.InputWhite = std::clamp((inherited.InputWhite - b) / a, rule.InputBlack + 0.001F, 1.0F);
    rule.SampleScale = 1.0F;
    rule.SampleOffset = 0.0F;
    return rule;
}

void WriteGrassTextureSignature(
    std::ostream& output, uint64_t hash, const GrassTextureSignature& signature) {
    output.write(kRecordTag.data(), kRecordTag.size());
    WriteValue(output, hash);
    WriteValue(output, signature.Width);
    WriteValue(output, signature.Height);
    WriteValue(output, signature.Detail);
    WriteValue(output, signature.Range);
}

std::vector<std::pair<uint64_t, GrassTextureSignature>>
ReadGrassTextureSignatures(std::istream& input) {
    std::vector<std::pair<uint64_t, GrassTextureSignature>> result;
    for (;;) {
        std::array<char, 4> tag{};
        uint64_t hash = 0U;
        GrassTextureSignature signature;
        // A truncated or foreign record ends the store.
        if (!ReadValue(input, tag) || tag != kRecordTag || !ReadValue(input, hash) ||
            !ReadValue(input, signature.Width) || !ReadValue(input, signature.Height) ||
            !ReadValue(input, signature.Detail) || !ReadValue(input, signature.Range))
            return result;
        result.emplace_back(hash, signature);
    }
}

GrassTwinRules& GrassTwinRules::Instance() {
    static GrassTwinRules rules;
    return rules;
}

void GrassTwinRules::SetSource(GrassTextureSignatureSource* source) {
    std::scoped_lock lock(mMutex);
    mSource = source;
    mEntries.clear();
    mRulesVersion = 0U;
    mIncomplete = false;
}

void GrassTwinRules::Clear() {
    std::scoped_lock lock(mMutex);
    mEntries.clear();
    mRulesVersion = 0U;
    mIncomplete = false;
}

uint64_t GrassTwinRules::Key(uint64_t hash, uint16_t width, uint16_t height) noexcept {
    return Mix(Mix(hash, width), height);
}

void GrassTwinRules::Sync(const std::vector<GrassPlacementRule>& rules) {
    auto& source = mSource != nullptr ? *mSource : GrassTextureSourceCache::Instance();
    // What the matching depends on: each rule's texture and mask channel.
    uint64_t version = Mix(14695981039346656037ULL, rules.size());
    for (const auto& rule : rules) {
        version = Mix(Mix(version, rule.RuleId), rule.Target.Rgba8Hash);
        version = Mix(Mix(version, rule.Target.Width), rule.Target.Height);
        version = Mix(Mix(version, static_cast<uint64_t>(rule.Channel)), rule.InheritedFrom);
    }
    if (version != mRulesVersion) {
        mRulesVersion = version;
        mEntries.clear();
        mIncomplete = false;
        mGeneration = source.Generation();
        return;
    }
    // A texture whose own or rule textures' signatures were missing is
    // resolved again once new signatures exist.
    if (mIncomplete) {
        if (const uint64_t generation = source.Generation(); generation != mGeneration) {
            mGeneration = generation;
            std::erase_if(mEntries, [](const auto& entry) { return !entry.second.Complete; });
            mIncomplete = false;
        }
    }
}

const GrassTwinRules::Entry& GrassTwinRules::Resolve(
    const std::vector<GrassPlacementRule>& rules, uint64_t hash,
    uint16_t width, uint16_t height) {
    Sync(rules);
    const uint64_t key = Key(hash, width, height);
    if (const auto found = mEntries.find(key); found != mEntries.end()) return found->second;
    Entry entry{hash, width, height};
    const bool own = std::any_of(rules.begin(), rules.end(), [&](const auto& rule) {
        return TargetsTexture(rule, hash, width, height);
    });
    auto& source = mSource != nullptr ? *mSource : GrassTextureSourceCache::Instance();
    if (!own) {
        const auto twin = source.Find(hash, false);
        entry.Complete = twin != nullptr;
        for (size_t i = 0; twin != nullptr && i < rules.size(); ++i) {
            const auto& rule = rules[i];
            // Configured rules only: an inherited rule is never inherited.
            if (rule.InheritedFrom != 0U) continue;
            const auto original = source.Find(rule.Target.Rgba8Hash, true);
            if (original == nullptr) {
                entry.Complete = false;
                continue;
            }
            const float score = GrassTextureTwinScore(*original, *twin);
            if (score >= kGrassTextureTwinThreshold && score > entry.Score) {
                const auto derived = DeriveGrassTwinRule(rule, *original, *twin, hash);
                entry.Score = score;
                entry.SourceRuleId = rule.RuleId;
                entry.SampleScale = derived.SampleScale;
                entry.SampleOffset = derived.SampleOffset;
            }
        }
    }
    mIncomplete |= !entry.Complete;
    return mEntries.emplace(key, std::move(entry)).first->second;
}

bool GrassTwinRules::Configured(
    const std::vector<GrassPlacementRule>& rules, uint64_t hash,
    uint16_t width, uint16_t height, uint8_t mapperSlot) {
    const uint32_t slot = 1U << mapperSlot;
    if (std::any_of(rules.begin(), rules.end(), [&](const auto& rule) {
            return TargetsTexture(rule, hash, width, height) && (rule.Target.MapperSlotMask & slot) != 0U;
        }))
        return true;
    std::scoped_lock lock(mMutex);
    const auto rule = RuleOf(rules, Resolve(rules, hash, width, height));
    return rule.has_value() && (rule->Target.MapperSlotMask & slot) != 0U;
}

std::optional<GrassPlacementRule> GrassTwinRules::RuleOf(
    const std::vector<GrassPlacementRule>& rules, const Entry& entry) {
    if (entry.SourceRuleId == 0U) return std::nullopt;
    // The source rule as it is now (its levels may have changed since).
    const auto source = std::find_if(rules.begin(), rules.end(), [&](const auto& rule) {
        return rule.RuleId == entry.SourceRuleId;
    });
    if (source == rules.end()) return std::nullopt;
    return InheritGrassRule(*source, entry.Hash, entry.Width, entry.Height, entry.SampleScale,
                            entry.SampleOffset);
}

std::optional<GrassPlacementRule> GrassTwinRules::Inherited(
    const std::vector<GrassPlacementRule>& rules, uint64_t hash,
    uint16_t width, uint16_t height) {
    std::scoped_lock lock(mMutex);
    return RuleOf(rules, Resolve(rules, hash, width, height));
}

std::vector<GrassPlacementRule> GrassTwinRules::AllInherited(
    const std::vector<GrassPlacementRule>& rules) {
    std::scoped_lock lock(mMutex);
    Sync(rules);
    std::vector<GrassPlacementRule> result;
    for (const auto& [key, entry] : mEntries)
        if (auto rule = RuleOf(rules, entry)) result.push_back(std::move(*rule));
    // Map order is not an identity: keep placement order stable.
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.RuleId < b.RuleId; });
    return result;
}

} // namespace Fast::Oot3d
