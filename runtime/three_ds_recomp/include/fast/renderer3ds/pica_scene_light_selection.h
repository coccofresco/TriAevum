#pragma once

#include "fast/renderer3ds/pica_light_binding.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Fast::Renderer3ds {

// The existing scene-light economy, shared without changing its arithmetic:
// companion/fairy groups pass, added sources rank/fade by light at the player.
struct PicaSceneLightSelectionSettings {
    uint32_t MaximumAddedLights = 16U;
    float FullDistance = 1200.0F, FadeDistance = 800.0F, Hysteresis = 1.2F;
    float FadeStep = 0.15F, Ceiling = 3.0F, Attack = 0.3F, Release = 0.06F;
};
struct PicaSceneLightSelectionStats {
    uint32_t Candidates = 0, Selected = 0, Fading = 0;
    float SummedLight = 0.0F, Exposure = 1.0F;
};
namespace SceneLightSelectionDetail {
inline float Luminance(const std::array<float, 3> &c) noexcept {
    return 0.2126F * c[0] + 0.7152F * c[1] + 0.0722F * c[2];
}
inline float Distance(const std::array<float, 3> &a, const std::array<float, 3> &b) noexcept {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
} // namespace SceneLightSelectionDetail
inline float PicaScenePointLightAt(const PicaScenePointLight &light,
                                   const std::array<float, 3> &at) noexcept {
    if (!(light.Radius > 0.0F))
        return 0.0F;
    const float d = SceneLightSelectionDetail::Distance(light.PositionWorld, at);
    const float falloff = std::max(0.0F, 1.0F - (d * d) / (light.Radius * light.Radius));
    return SceneLightSelectionDetail::Luminance(light.Color) * falloff * falloff;
}
class PicaSceneLightSelector {
  public:
    void Reset() {
        mStates.clear();
        mExposure = 1.0F;
    }
    const PicaSceneLightSelectionStats &Stats() const noexcept { return mStats; }
    PicaSceneLightSelectionSettings Settings{};
    void Select(std::span<const PicaScenePointLight> candidates,
                const std::optional<std::array<float, 3>> &player, bool discontinuity,
                PicaScenePointLights &snapshot) {
        using namespace SceneLightSelectionDetail;
        if (discontinuity)
            Reset();
        ++mFrame;
        const auto &s = Settings;
        snapshot.Count = 0;
        mStats = {};
        mStats.Candidates = static_cast<uint32_t>(candidates.size());
        const auto level = [&](const PicaScenePointLight &light) {
            if (!player)
                return 1.0F;
            const float surface = std::max(0.0F, Distance(light.PositionWorld, *player) - light.Radius);
            const float full = s.FullDistance, fade = std::max(1.0F, s.FadeDistance);
            return std::clamp(1.0F - (surface - full) / fade, 0.0F, 1.0F);
        };
        struct Entry {
            const PicaScenePointLight *Light;
            float Level, Score;
        };
        std::vector<Entry> added;
        std::vector<const PicaScenePointLight *> fixed;
        for (const auto &light : candidates) {
            const auto same = [&](const auto *other) {
                return other->SourceIdentity == light.SourceIdentity;
            };
            if (light.Group < 2U) {
                if (std::none_of(fixed.begin(), fixed.end(), same))
                    fixed.push_back(&light);
                continue;
            }
            if (std::any_of(added.begin(), added.end(), [&](const Entry &e) { return same(e.Light); }))
                continue;
            const float l = level(light);
            if (!(l > 0.0F) || !(light.Radius > 0.0F))
                continue;
            float score = Luminance(light.Color) * l;
            if (player) {
                const float d = Distance(light.PositionWorld, *player);
                score *= light.Radius / (light.Radius + d);
            }
            const auto state = mStates.find(light.SourceIdentity);
            if (state != mStates.end() && state->second.Selected)
                score *= s.Hysteresis;
            added.push_back({&light, l, score});
        }
        std::stable_sort(added.begin(), added.end(), [](const Entry &a, const Entry &b) {
            if (a.Score != b.Score)
                return a.Score > b.Score;
            return a.Light->SourceIdentity < b.Light->SourceIdentity;
        });
        for (const auto *light : fixed) {
            if (snapshot.Count == snapshot.Lights.size())
                break;
            snapshot.Lights[snapshot.Count++] = *light;
        }
        const uint32_t budget = std::min<uint32_t>(
            s.MaximumAddedLights, static_cast<uint32_t>(snapshot.Lights.size()) - snapshot.Count);
        std::vector<std::pair<const Entry *, float>> emitted;
        for (size_t i = 0; i < added.size(); ++i) {
            auto &state = mStates[added[i].Light->SourceIdentity];
            state.SeenFrame = mFrame;
            state.Selected = i < budget;
            state.Weight = std::clamp(state.Weight + (state.Selected ? s.FadeStep : -s.FadeStep), 0.0F, 1.0F);
            if (state.Weight > 0.0F)
                emitted.push_back({&added[i], state.Weight});
        }
        std::erase_if(mStates, [&](const auto &entry) { return entry.second.SeenFrame != mFrame; });
        float sum = 0.0F;
        if (player)
            for (const auto &[entry, weight] : emitted)
                sum += PicaScenePointLightAt(*entry->Light, *player) * weight * entry->Level;
        const float target = sum > s.Ceiling ? s.Ceiling / sum : 1.0F;
        mExposure = target < mExposure ? std::max(target, mExposure - s.Attack)
                                       : std::min(target, mExposure + s.Release);
        const uint32_t room = static_cast<uint32_t>(snapshot.Lights.size()) - snapshot.Count;
        std::stable_sort(emitted.begin(), emitted.end(),
                         [](const auto &a, const auto &b) { return a.first->Score > b.first->Score; });
        for (size_t i = 0; i < emitted.size() && i < room; ++i) {
            const auto &[entry, weight] = emitted[i];
            auto light = *entry->Light;
            const float gain = weight * entry->Level * mExposure;
            for (auto &channel : light.Color)
                channel = std::clamp(channel * gain, 0.0F, 1.0F);
            snapshot.Lights[snapshot.Count++] = light;
            if (weight >= 1.0F)
                ++mStats.Selected;
            else
                ++mStats.Fading;
        }
        mStats.SummedLight = sum;
        mStats.Exposure = mExposure;
    }

  private:
    struct State {
        float Weight = 0.0F;
        bool Selected = false;
        uint64_t SeenFrame = 0;
    };
    std::unordered_map<uint64_t, State> mStates;
    uint64_t mFrame = 0;
    float mExposure = 1.0F;
    PicaSceneLightSelectionStats mStats{};
};
} // namespace Fast::Renderer3ds
