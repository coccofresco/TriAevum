#pragma once

// Chooses, once per captured native frame, which of the candidate scene point
// lights the renderer receives, and how strongly: the economy of the Modern
// scene lights (designed with Codex). Groups 0 and 1 (Navi and the fairies,
// the title's own lights) always pass. The added lights (flames, architecture)
// are ranked by their light at Link, fade out with distance, keep their slot
// against a slightly stronger challenger, fade in and out instead of popping,
// and are dimmed together when their sum at Link would be excessive.

#include "fast/renderer3ds/pica_light_binding.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

namespace Oot3dNativeGame {

struct NativeSceneLightSelectionSettings {
    // Added lights per frame (of the renderer's 32 slots).
    uint32_t MaximumAddedLights = 16U;
    // Full light within this distance of Link (from the light's reach), none
    // beyond the far distance; both grow with the reach.
    float FullDistance = 1200.0F;
    float FadeDistance = 800.0F;
    // An incumbent keeps its slot unless a challenger scores this much more.
    float Hysteresis = 1.2F;
    // Fade in / out per captured frame (30 per second).
    float FadeStep = 0.15F;
    // The added lights' summed luminance at Link above which they are dimmed
    // together; attack (dimming) and release (recovering) per frame.
    float Ceiling = 3.0F;
    float Attack = 0.3F;
    float Release = 0.06F;
};

struct NativeSceneLightSelectionStats {
    uint32_t Candidates = 0;
    uint32_t Selected = 0;
    uint32_t Fading = 0;
    float SummedLight = 0.0F;
    float Exposure = 1.0F;
};

class NativeSceneLightSelector {
public:
    // Fills the snapshot. A discontinuity (scene change, load) restarts the
    // fades instead of fading across it.
    void Select(std::span<const Fast::Renderer3ds::PicaScenePointLight> candidates,
                const std::optional<std::array<float, 3>>& player, bool discontinuity,
                Fast::Renderer3ds::PicaScenePointLights& snapshot);
    void Reset();
    [[nodiscard]] const NativeSceneLightSelectionStats& Stats() const noexcept { return mStats; }
    NativeSceneLightSelectionSettings Settings{};

private:
    struct State {
        float Weight = 0.0F;
        bool Selected = false;
        uint64_t SeenFrame = 0;
    };
    std::unordered_map<uint64_t, State> mStates;
    uint64_t mFrame = 0;
    float mExposure = 1.0F;
    NativeSceneLightSelectionStats mStats{};
};

// The light a scene point gives at a point for the brightness ceiling: its
// colour's luminance times the square of the renderer's 1 - d^2 / r^2, so a
// light whose edge just reaches Link counts little.
[[nodiscard]] float ScenePointLightAt(const Fast::Renderer3ds::PicaScenePointLight& light,
                                      const std::array<float, 3>& at) noexcept;

} // namespace Oot3dNativeGame
