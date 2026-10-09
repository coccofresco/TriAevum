#pragma once

#include "fast/oot3d/grass_types.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Fast::Oot3d {

// Twin textures: the same image stored more than once (another encoding, half
// the size, another tint, a patch painted over part of it). A texture without
// a grass rule of its own takes the rule of its twin, so every copy of an
// assigned ground grows the same grass with the same mask.

// What identifies an image: its luminance detail (high-passed, 64x64, wrapped
// as the texture tiles) and each mask channel's range (20th and 80th
// percentiles), so that a tinted twin's samples can be mapped onto the
// original's before the rule's levels.
struct GrassTextureSignature {
    static constexpr uint32_t Side = 64U;
    uint16_t Width = 0U;
    uint16_t Height = 0U;
    std::array<int8_t, Side * Side> Detail{};
    // Per GrassSampleChannel, in its enumeration order.
    std::array<std::array<uint8_t, 2>, 5> Range{};
    bool operator==(const GrassTextureSignature&) const = default;
};

[[nodiscard]] GrassTextureSignature ComputeGrassTextureSignature(
    uint16_t width, uint16_t height, std::span<const uint8_t> rgba8);

// The median over a 4x4 grid of tiles of the detail's correlation: robust to
// a patch that differs. -1 when the shapes cannot be the same image (another
// aspect ratio, more than twice the size, smaller than 16 texels).
[[nodiscard]] float GrassTextureTwinScore(
    const GrassTextureSignature& a, const GrassTextureSignature& b) noexcept;
// Same images score 0.65-1 on the title's textures; different ones <= 0.5.
inline constexpr float kGrassTextureTwinThreshold = 0.6F;

// Inherited rule ids are kept apart from configured ones (sequential).
inline constexpr uint32_t kGrassInheritedRuleIdBit = 0x80000000U;

// The rule a twin inherits: the source's, retargeted to the twin, with the
// twin's channel range mapped onto the source's.
[[nodiscard]] GrassPlacementRule InheritGrassRule(
    const GrassPlacementRule& source, uint64_t twinHash, uint16_t twinWidth,
    uint16_t twinHeight, float sampleScale, float sampleOffset);
[[nodiscard]] GrassPlacementRule DeriveGrassTwinRule(
    const GrassPlacementRule& source,
    const GrassTextureSignature& sourceSignature,
    const GrassTextureSignature& twinSignature,
    uint64_t twinHash);

// A rule of the twin's own with the inherited one's settings: the sample
// mapping folded into its input levels (clamped to their range).
[[nodiscard]] GrassPlacementRule OwnGrassRuleFromInherited(
    const GrassPlacementRule& inherited, uint32_t ruleId);

// Signatures persist (a local cache) so a rule's texture is known in scenes
// where it is not drawn. Records: hash, size, detail, ranges.
void WriteGrassTextureSignature(
    std::ostream& output, uint64_t hash, const GrassTextureSignature& signature);
[[nodiscard]] std::vector<std::pair<uint64_t, GrassTextureSignature>>
ReadGrassTextureSignatures(std::istream& input);

// Where signatures come from: the source cache in the renderer, a table in
// tests. Persist asks that a signature be kept for later sessions.
struct GrassTextureSignatureSource {
    virtual ~GrassTextureSignatureSource() = default;
    [[nodiscard]] virtual std::shared_ptr<const GrassTextureSignature> Find(
        uint64_t hash, bool persist) = 0;
    // Changes when signatures appear (new textures, the store loaded).
    [[nodiscard]] virtual uint64_t Generation() const = 0;
};

// The rules in effect for each texture: its own when it has one, else the
// rule of the rule texture it is a twin of (the best scoring). Results are
// kept per texture until the rules' textures or channels change (their other
// settings are read when asked) or a missing signature appears.
class GrassTwinRules final {
  public:
    static GrassTwinRules& Instance();
    // The renderer's source cache unless set (tests).
    void SetSource(GrassTextureSignatureSource* source);

    // Whether a texture draws grass in this mapper slot.
    [[nodiscard]] bool Configured(
        const std::vector<GrassPlacementRule>& rules, uint64_t hash,
        uint16_t width, uint16_t height, uint8_t mapperSlot);
    // The rule a texture inherits; none when it has its own or no twin.
    [[nodiscard]] std::optional<GrassPlacementRule> Inherited(
        const std::vector<GrassPlacementRule>& rules, uint64_t hash,
        uint16_t width, uint16_t height);
    // Every inherited rule resolved so far for these rules.
    [[nodiscard]] std::vector<GrassPlacementRule> AllInherited(
        const std::vector<GrassPlacementRule>& rules);
    void Clear();

  private:
    struct Entry {
        uint64_t Hash = 0U;
        uint16_t Width = 0U;
        uint16_t Height = 0U;
        uint32_t SourceRuleId = 0U;  // 0: no twin
        float Score = 0.0F;
        float SampleScale = 1.0F;
        float SampleOffset = 0.0F;
        bool Complete = true;
    };
    [[nodiscard]] static std::optional<GrassPlacementRule> RuleOf(
        const std::vector<GrassPlacementRule>& rules, const Entry& entry);
    [[nodiscard]] static uint64_t Key(uint64_t hash, uint16_t width, uint16_t height) noexcept;
    void Sync(const std::vector<GrassPlacementRule>& rules);
    [[nodiscard]] const Entry& Resolve(
        const std::vector<GrassPlacementRule>& rules, uint64_t hash,
        uint16_t width, uint16_t height);

    std::mutex mMutex;
    GrassTextureSignatureSource* mSource = nullptr;
    uint64_t mRulesVersion = 0U;
    uint64_t mGeneration = 0U;
    bool mIncomplete = false;
    std::unordered_map<uint64_t, Entry> mEntries;
};

} // namespace Fast::Oot3d
