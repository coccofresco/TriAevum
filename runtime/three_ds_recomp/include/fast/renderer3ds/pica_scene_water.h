#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace Fast::Renderer3ds {

// Gameplay water published by the title (its collision water boxes): the
// horizontal surfaces where water is, with live heights (levels that rise
// and fall). Generic data: no guest addresses or scene rules.
inline constexpr uint32_t kMaximumPicaSceneWaterBodies = 64U;

struct PicaSceneWaterBody {
    std::array<float, 2> MinimumXZ{};
    std::array<float, 2> MaximumXZ{};
    float SurfaceY = 0.0F;
    uint32_t Properties = 0;
    bool operator==(const PicaSceneWaterBody&) const = default;
};

// One immutable snapshot shared by all draws from a native frame.
struct PicaSceneWaterBodies {
    std::array<PicaSceneWaterBody, kMaximumPicaSceneWaterBodies> Bodies{};
    uint32_t Count = 0;
    bool operator==(const PicaSceneWaterBodies&) const = default;
};

// How far (world units) a drawn water surface may lie from its body's
// gameplay height, and how much of its footprint must lie over the body.
inline constexpr float kPicaWaterSurfaceHeightTolerance = 4.0F;
inline constexpr float kPicaWaterSurfaceMinimumCoverage = 0.5F;

// Whether a draw's world bounds reach a body's surface (its height range
// includes the surface, within the tolerance, over the body's footprint): the
// draw may hold water fragments.
[[nodiscard]] inline bool PicaWaterSurfaceReaches(const std::array<float, 3>& minimum,
                                                  const std::array<float, 3>& maximum,
                                                  const PicaSceneWaterBody& body) noexcept {
    return body.SurfaceY >= minimum[1] - kPicaWaterSurfaceHeightTolerance &&
           body.SurfaceY <= maximum[1] + kPicaWaterSurfaceHeightTolerance && body.MaximumXZ[0] >= minimum[0] &&
           body.MinimumXZ[0] <= maximum[0] && body.MaximumXZ[1] >= minimum[2] && body.MinimumXZ[1] <= maximum[2];
}

// The body a draw's world bounds lie on as a water surface: flat (its height
// range within the tolerance), at the body's surface height, and with most of
// its horizontal footprint over the body. Bridges, banks and the bed are not
// flat at the surface height. Of several bodies, the one covering most.
[[nodiscard]] inline std::optional<uint32_t> MatchPicaWaterSurface(const std::array<float, 3>& minimum,
                                                                   const std::array<float, 3>& maximum,
                                                                   const PicaSceneWaterBodies& bodies) noexcept {
    if (maximum[1] - minimum[1] > kPicaWaterSurfaceHeightTolerance || maximum[0] < minimum[0] ||
        maximum[2] < minimum[2])
        return std::nullopt;
    const float height = 0.5F * (minimum[1] + maximum[1]);
    // A degenerate footprint (a line) still needs a positive area to compare.
    const float width = std::max(maximum[0] - minimum[0], 1.0F);
    const float depth = std::max(maximum[2] - minimum[2], 1.0F);
    std::optional<uint32_t> best;
    float bestCoverage = kPicaWaterSurfaceMinimumCoverage;
    for (uint32_t i = 0; i < std::min(bodies.Count, kMaximumPicaSceneWaterBodies); ++i) {
        const auto& body = bodies.Bodies[i];
        if (std::abs(body.SurfaceY - height) > kPicaWaterSurfaceHeightTolerance) continue;
        const float overlapX = std::min(maximum[0], body.MaximumXZ[0]) - std::max(minimum[0], body.MinimumXZ[0]);
        const float overlapZ = std::min(maximum[2], body.MaximumXZ[1]) - std::max(minimum[2], body.MinimumXZ[1]);
        if (overlapX < 0.0F || overlapZ < 0.0F) continue;
        // A degenerate axis inside the body counts as covered.
        const float coverage = std::max(overlapX, width - (maximum[0] - minimum[0])) *
                               std::max(overlapZ, depth - (maximum[2] - minimum[2])) / (width * depth);
        if (coverage >= bestCoverage) {
            best = i;
            bestCoverage = coverage;
        }
    }
    return best;
}

// A water surface the title publishes no body for (decorative water: a
// moat, a well), from a water-textured draw's world bounds: flat within the
// tolerance, its plane at their middle height.
[[nodiscard]] inline std::optional<PicaSceneWaterBody> PicaWaterSurfaceFromBounds(
    const std::array<float, 3>& minimum, const std::array<float, 3>& maximum) noexcept {
    if (!(maximum[1] - minimum[1] <= kPicaWaterSurfaceHeightTolerance) || maximum[0] < minimum[0] ||
        maximum[2] < minimum[2])
        return std::nullopt;
    return PicaSceneWaterBody{{minimum[0], minimum[2]}, {maximum[0], maximum[2]}, 0.5F * (minimum[1] + maximum[1])};
}

// The title's water textures: a local catalog the installer generates from
// the user's RomFS (tools/triaevum_release/water_textures.py), keyed like
// texture materials by the base level's content hash and size. Text format:
// a "triaevum-water-textures 1" line, then "<hash hex> <width> <height>
// <role mask>" lines.
enum PicaWaterTextureRole : uint8_t {
    kPicaWaterTextureSurface = 1U,
    kPicaWaterTextureRiver = 2U,
    kPicaWaterTextureWaterfall = 4U,
    // A light layer on submerged ground and walls: not water itself.
    kPicaWaterTextureUnderwater = 8U,
};
// The roles that make a draw water: one material, so all of it.
inline constexpr uint8_t kPicaWaterTextureWater =
    kPicaWaterTextureSurface | kPicaWaterTextureRiver | kPicaWaterTextureWaterfall;

class PicaWaterTextureCatalog {
public:
    [[nodiscard]] static std::optional<PicaWaterTextureCatalog> Parse(std::string_view text) {
        const auto line = [&text]() {
            const size_t end = text.find('\n');
            std::string_view result = text.substr(0, end);
            text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
            if (!result.empty() && result.back() == '\r') result.remove_suffix(1);
            return result;
        };
        if (line() != "triaevum-water-textures 1") return std::nullopt;
        PicaWaterTextureCatalog catalog;
        while (!text.empty()) {
            const auto entry = line();
            if (entry.empty()) continue;
            uint64_t fields[4]{};
            size_t field = 0, position = 0;
            for (; field < 4U && position < entry.size(); ++field) {
                const int base = field == 0U ? 16 : 10;
                size_t digits = 0;
                for (; position < entry.size() && entry[position] != ' '; ++position, ++digits) {
                    const char c = entry[position];
                    const int value = c >= '0' && c <= '9' ? c - '0'
                        : base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                    if (value < 0 || digits >= (base == 16 ? 16U : 6U)) return std::nullopt;
                    fields[field] = fields[field] * static_cast<uint64_t>(base) + static_cast<uint64_t>(value);
                }
                if (digits == 0U) return std::nullopt;
                if (position < entry.size()) ++position;
            }
            if (field != 4U || position != entry.size() || fields[1] > 0xFFFFU || fields[2] > 0xFFFFU ||
                fields[3] > 0xFFU)
                return std::nullopt;
            catalog.mEntries.push_back({fields[0], static_cast<uint32_t>(fields[1] << 16U | fields[2]),
                                        static_cast<uint8_t>(fields[3])});
        }
        std::sort(catalog.mEntries.begin(), catalog.mEntries.end());
        return catalog;
    }

    [[nodiscard]] uint8_t Roles(uint64_t contentHash, uint32_t width, uint32_t height) const noexcept {
        const Entry key{contentHash, width << 16U | (height & 0xFFFFU), 0};
        const auto found = std::lower_bound(mEntries.begin(), mEntries.end(), key);
        return found != mEntries.end() && found->Hash == key.Hash && found->Size == key.Size ? found->Roles : 0U;
    }
    [[nodiscard]] size_t Size() const noexcept { return mEntries.size(); }

private:
    struct Entry {
        uint64_t Hash = 0;
        uint32_t Size = 0;
        uint8_t Roles = 0;
        bool operator<(const Entry& other) const noexcept {
            return Hash != other.Hash ? Hash < other.Hash : Size < other.Size;
        }
    };
    std::vector<Entry> mEntries;
};

// A water draw's texture scrolling (its coordinate's matrix translation)
// over native frames, in texture units per second: the title's visual flow
// signal (calm or fast water), not fluid velocity. Repeated presentations of
// one native frame add nothing; a gap of more than kPicaWaterFlowMaximumGap
// frames, or going back, restarts the track.
inline constexpr uint64_t kPicaWaterFlowMaximumGap = 4U;

struct PicaWaterFlowTrack {
    uint64_t Frame = 0;
    std::array<float, 2> Translation{};
    std::array<float, 2> Velocity{};
    uint32_t Samples = 0;

    // Two native frames give a velocity.
    [[nodiscard]] bool Valid() const noexcept { return Samples >= 2U; }
};

inline void AdvancePicaWaterFlow(PicaWaterFlowTrack& track, uint64_t frame, float rateHz,
                                 const std::array<float, 2>& translation) noexcept {
    if (track.Samples != 0U && frame == track.Frame) return;
    if (track.Samples == 0U || frame < track.Frame || frame - track.Frame > kPicaWaterFlowMaximumGap ||
        !(rateHz > 0.0F) || !std::isfinite(translation[0]) || !std::isfinite(translation[1])) {
        track = {};
        track.Frame = frame;
        track.Translation = translation;
        track.Samples = 1U;
        return;
    }
    const float frames = static_cast<float>(frame - track.Frame);
    std::array<float, 2> sample{};
    for (size_t axis = 0; axis < 2U; ++axis) {
        // Translations may wrap at whole texture repeats.
        float delta = translation[axis] - track.Translation[axis];
        delta -= std::round(delta);
        sample[axis] = delta * rateHz / frames;
        // Smoothed: animations that step every other frame read as steady.
        track.Velocity[axis] = track.Samples == 1U ? sample[axis] : 0.7F * track.Velocity[axis] + 0.3F * sample[axis];
    }
    track.Frame = frame;
    track.Translation = translation;
    ++track.Samples;
}

// Ripples of what moves through the water: rings that a mover (an actor's
// collision cylinder) emits where its cylinder crosses a body's surface. A
// ring spreads from its centre and fades (the surface shaders add its slope
// to the waves): a splash when the mover enters, a ring every few units it
// travels (a wake when swimming), a faint ring now and then at rest.
inline constexpr uint32_t kPicaWaterRings = 16U;
// Seconds a ring lives.
inline constexpr float kPicaWaterRingLifetime = 3.0F;

struct PicaWaterMover {
    uint64_t Identity = 0;
    // The cylinder's base (world) and its radius and height.
    std::array<float, 3> Base{};
    float Radius = 0.0F;
    float Height = 0.0F;
};

struct PicaWaterRing {
    std::array<float, 2> CenterXZ{};
    // Birth (water time, seconds) and strength (0: none).
    float Birth = 0.0F;
    float Strength = 0.0F;
};

class PicaWaterRippleField {
public:
    // One step at the water time (seconds; a time going back restarts).
    void Advance(float seconds, const PicaWaterMover* movers, size_t moverCount,
                 const PicaSceneWaterBodies& bodies) {
        if (seconds < mTime) Reset();
        mTime = seconds;
        for (size_t m = 0; m < moverCount; ++m) {
            const auto& mover = movers[m];
            const bool inWater = CrossesSurface(mover, bodies);
            auto track = std::find_if(mTracks.begin(), mTracks.end(),
                                      [&](const Track& item) { return item.Identity == mover.Identity; });
            const std::array<float, 2> position{mover.Base[0], mover.Base[2]};
            if (track == mTracks.end()) {
                mTracks.push_back({mover.Identity, position, seconds, seconds, inWater});
                if (inWater) Emit(position, seconds, 0.3F);
                continue;
            }
            track->Seen = seconds;
            if (inWater && !track->InWater) {
                // Entering: a splash.
                Emit(position, seconds, 1.4F);
                track->Last = position;
                track->Emitted = seconds;
            } else if (inWater) {
                const float moved = std::hypot(position[0] - track->Last[0], position[1] - track->Last[1]);
                const float spacing = std::max(0.8F * mover.Radius, 8.0F);
                const float elapsed = std::max(seconds - track->Emitted, 1.0e-3F);
                if (moved >= spacing) {
                    Emit(position, seconds, std::clamp(0.35F + moved / elapsed / 200.0F, 0.35F, 1.0F));
                    track->Last = position;
                    track->Emitted = seconds;
                } else if (elapsed >= 1.1F) {
                    Emit(position, seconds, 0.2F);
                    track->Emitted = seconds;
                }
            }
            track->InWater = inWater;
        }
        // Movers no longer seen are forgotten after a second.
        std::erase_if(mTracks, [&](const Track& item) { return seconds - item.Seen > 1.0F; });
    }

    void Reset() {
        mTracks.clear();
        mRings = {};
        mNext = 0;
        mTime = 0.0F;
    }

    // The live rings (strength 0: an empty slot).
    [[nodiscard]] std::array<PicaWaterRing, kPicaWaterRings> Rings() const noexcept {
        auto result = mRings;
        for (auto& ring : result)
            if (mTime - ring.Birth > kPicaWaterRingLifetime) ring = {};
        return result;
    }

    [[nodiscard]] static bool CrossesSurface(const PicaWaterMover& mover, const PicaSceneWaterBodies& bodies) {
        for (uint32_t i = 0; i < std::min(bodies.Count, kMaximumPicaSceneWaterBodies); ++i) {
            const auto& body = bodies.Bodies[i];
            if (mover.Base[0] >= body.MinimumXZ[0] && mover.Base[0] <= body.MaximumXZ[0] &&
                mover.Base[2] >= body.MinimumXZ[1] && mover.Base[2] <= body.MaximumXZ[1] &&
                mover.Base[1] <= body.SurfaceY + 2.0F && mover.Base[1] + mover.Height >= body.SurfaceY - 2.0F)
                return true;
        }
        return false;
    }

private:
    struct Track {
        uint64_t Identity = 0;
        std::array<float, 2> Last{};
        float Emitted = 0.0F;
        float Seen = 0.0F;
        bool InWater = false;
    };
    void Emit(const std::array<float, 2>& center, float seconds, float strength) {
        mRings[mNext] = {center, seconds, strength};
        mNext = (mNext + 1U) % kPicaWaterRings;
    }
    std::vector<Track> mTracks;
    std::array<PicaWaterRing, kPicaWaterRings> mRings{};
    uint32_t mNext = 0;
    float mTime = 0.0F;
};

} // namespace Fast::Renderer3ds
