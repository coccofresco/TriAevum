#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Fast::Oot3d {

enum class GrassSampleChannel : uint8_t { Red, Green, Blue, Alpha, Luminance };
enum class GrassWrapOverride : uint8_t { Material, Clamp, Repeat, Mirror };
enum class GrassTextureWrap : uint8_t { Clamp, Repeat, Mirror };

inline constexpr float kMaximumGrassInstancesPerSquareMeter = 4096.0F;

struct GrassTextureSelector {
    std::string AssetName;
    uint64_t Rgba8Hash = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t MapperSlotMask = 0x07;
};

// A rectangle of a grass mask, in its normalized texture coordinates after
// wrap (u across, v down the rows, as the editor's preview shows it).
struct GrassMaskRect {
    float MinU = 0.0F;
    float MinV = 0.0F;
    float MaxU = 1.0F;
    float MaxV = 1.0F;
    bool operator==(const GrassMaskRect&) const = default;
};
inline constexpr size_t kMaximumGrassMaskRects = 16U;

struct GrassPlacementRule {
    uint32_t RuleId = 0;
    GrassTextureSelector Target;
    GrassSampleChannel Channel = GrassSampleChannel::Luminance;
    GrassWrapOverride Wrap = GrassWrapOverride::Material;
    bool Invert = false;
    float InputBlack = 0.35F;
    float InputWhite = 1.0F;
    float ResponseExponent = 1.0F;
    float OutputBlack = 0.0F;
    float OutputWhite = 1.0F;
    float MaximumSlopeDegrees = 48.0F;
    float NormalOffset = 0.5F;
    // Active rectangles: when any are listed, the mask applies only inside
    // them and is zero elsewhere (as a black mask); none: all of it applies.
    std::vector<GrassMaskRect> ActiveRects;
    // Inherited rules only (a twin texture's, never configured or saved):
    // its samples mapped onto the source texture's range before the levels,
    // and the configured rule they come from.
    float SampleScale = 1.0F;
    float SampleOffset = 0.0F;
    uint32_t InheritedFrom = 0U;
};

enum class GrassMaskMode : uint8_t { Automatic, Custom, Off };

// An override for a native overlay texture. Exact native identity and size:
// visual texture twins can have different alpha and must not inherit cutouts.
struct GrassMaskRule {
    GrassMaskMode Mode = GrassMaskMode::Automatic;
    GrassPlacementRule Coverage = [] {
        GrassPlacementRule rule;
        rule.Channel = GrassSampleChannel::Alpha;
        rule.InputBlack = 0.0F;
        return rule;
    }();
    float Strength = 1.0F;
};

[[nodiscard]] const GrassMaskRule* FindGrassMaskRule(const std::vector<GrassMaskRule>& rules, uint64_t hash,
                                                     uint16_t width, uint16_t height);
void NormalizeGrassMaskRule(GrassMaskRule& rule);

struct GrassGenerationSettings {
    float InstancesPerSquareMeter = 8.0F;
    float MinimumSpacing = 8.0F;
    uint32_t Seed = 1;
    float IndividualRandomness = 1.0F;
    float ClusterStrength = 0.0F;
    float ClusterScale = 300.0F;
    float ClusterCoverage = 0.65F;
    float BladeHeightMin = 18.0F;
    float BladeHeightMax = 42.0F;
    float BladeWidthMin = 1.8F;
    float BladeWidthMax = 4.5F;
};

struct GrassInteractor {
    enum class Kind : uint8_t { PlayerLink, SceneActor, Epona };
    Kind Type = Kind::PlayerLink;
    uint64_t StableId = 1;
    uint64_t ContextId = 0;
    std::array<float, 3> PreviousPosition{};
    std::array<float, 3> Position{};
    std::array<float, 3> Velocity{};
    float Radius = 0.35F;
    float HalfHeight = 0.85F;
    uint64_t FrameId = 0;
    bool Teleported = false;
};

} // namespace Fast::Oot3d
