#include "oot3d_native_a32_light_selection.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Oot3dNativeGame {
namespace {

using Fast::Renderer3ds::PicaScenePointLight;

float Luminance(const std::array<float, 3>& c) noexcept {
    return 0.2126F * c[0] + 0.7152F * c[1] + 0.0722F * c[2];
}

float Distance(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

float ScenePointLightAt(const PicaScenePointLight& light, const std::array<float, 3>& at) noexcept {
    if (!(light.Radius > 0.0F)) return 0.0F;
    const float d = Distance(light.PositionWorld, at);
    const float falloff = std::max(0.0F, 1.0F - (d * d) / (light.Radius * light.Radius));
    return Luminance(light.Color) * falloff * falloff;
}

void NativeSceneLightSelector::Reset() {
    mStates.clear();
    mExposure = 1.0F;
}

void NativeSceneLightSelector::Select(std::span<const PicaScenePointLight> candidates,
                                      const std::optional<std::array<float, 3>>& player, bool discontinuity,
                                      Fast::Renderer3ds::PicaScenePointLights& snapshot) {
    if (discontinuity) Reset();
    ++mFrame;
    const auto& s = Settings;
    snapshot.Count = 0;
    mStats = {};
    mStats.Candidates = static_cast<uint32_t>(candidates.size());

    // Distance level of an added light: 1 near Link, 0 beyond the fade,
    // measured from its reach (a wide light counts from its edge).
    const auto level = [&](const PicaScenePointLight& light) {
        if (!player) return 1.0F;
        const float surface = std::max(0.0F, Distance(light.PositionWorld, *player) - light.Radius);
        const float full = s.FullDistance, fade = std::max(1.0F, s.FadeDistance);
        return std::clamp(1.0F - (surface - full) / fade, 0.0F, 1.0F);
    };
    struct Entry {
        const PicaScenePointLight* Light;
        float Level;
        float Score;
    };
    std::vector<Entry> added;
    std::vector<const PicaScenePointLight*> fixed;
    for (const auto& light : candidates) {
        const auto same = [&](const auto* other) { return other->SourceIdentity == light.SourceIdentity; };
        if (light.Group < 2U) {
            if (std::none_of(fixed.begin(), fixed.end(), same)) fixed.push_back(&light);
            continue;
        }
        if (std::any_of(added.begin(), added.end(), [&](const Entry& e) { return same(e.Light); })) continue;
        const float l = level(light);
        if (!(l > 0.0F) || !(light.Radius > 0.0F)) continue;
        // Its light at Link (falling with distance over its reach), so a wide
        // dim light and a near bright one compare by what they give.
        float score = Luminance(light.Color) * l;
        if (player) {
            const float d = Distance(light.PositionWorld, *player);
            score *= light.Radius / (light.Radius + d);
        }
        const auto state = mStates.find(light.SourceIdentity);
        if (state != mStates.end() && state->second.Selected) score *= s.Hysteresis;
        added.push_back({&light, l, score});
    }
    std::stable_sort(added.begin(), added.end(), [](const Entry& a, const Entry& b) {
        if (a.Score != b.Score) return a.Score > b.Score;
        return a.Light->SourceIdentity < b.Light->SourceIdentity;
    });

    for (const auto* light : fixed) {
        if (snapshot.Count == snapshot.Lights.size()) break;
        snapshot.Lights[snapshot.Count++] = *light;
    }
    const uint32_t budget = std::min<uint32_t>(s.MaximumAddedLights,
                                               static_cast<uint32_t>(snapshot.Lights.size()) - snapshot.Count);
    // Selected lights fade in; lights that lost their slot fade out while a
    // slot is free for them (retiring before new admissions beyond budget).
    std::vector<std::pair<const Entry*, float>> emitted;
    for (size_t i = 0; i < added.size(); ++i) {
        auto& state = mStates[added[i].Light->SourceIdentity];
        state.SeenFrame = mFrame;
        state.Selected = i < budget;
        state.Weight = std::clamp(state.Weight + (state.Selected ? s.FadeStep : -s.FadeStep), 0.0F, 1.0F);
        if (state.Weight > 0.0F) emitted.push_back({&added[i], state.Weight});
    }
    // Lights gone from the candidates disappear at once (their source ended).
    std::erase_if(mStates, [&](const auto& entry) { return entry.second.SeenFrame != mFrame; });

    // Exposure: the added lights' summed light at Link above the ceiling dims
    // them together, quickly, and recovers slowly.
    float sum = 0.0F;
    if (player)
        for (const auto& [entry, weight] : emitted)
            sum += ScenePointLightAt(*entry->Light, *player) * weight * entry->Level;
    const float target = sum > s.Ceiling ? s.Ceiling / sum : 1.0F;
    mExposure = target < mExposure ? std::max(target, mExposure - s.Attack)
                                   : std::min(target, mExposure + s.Release);
    const uint32_t room = static_cast<uint32_t>(snapshot.Lights.size()) - snapshot.Count;
    std::stable_sort(emitted.begin(), emitted.end(), [](const auto& a, const auto& b) {
        return a.first->Score > b.first->Score;
    });
    for (size_t i = 0; i < emitted.size() && i < room; ++i) {
        const auto& [entry, weight] = emitted[i];
        auto light = *entry->Light;
        const float gain = weight * entry->Level * mExposure;
        for (auto& channel : light.Color) channel = std::clamp(channel * gain, 0.0F, 1.0F);
        snapshot.Lights[snapshot.Count++] = light;
        if (weight >= 1.0F) ++mStats.Selected;
        else ++mStats.Fading;
    }
    mStats.SummedLight = sum;
    mStats.Exposure = mExposure;
}

} // namespace Oot3dNativeGame
