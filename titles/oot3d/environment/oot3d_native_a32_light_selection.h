#pragma once

// Compatibility names; candidate facts stay in the title, the unchanged
// ranking/fading/energy economy is owned by the reusable renderer.
#include "fast/renderer3ds/pica_scene_light_selection.h"

namespace Oot3dNativeGame {
using NativeSceneLightSelectionSettings = Fast::Renderer3ds::PicaSceneLightSelectionSettings;
using NativeSceneLightSelectionStats = Fast::Renderer3ds::PicaSceneLightSelectionStats;
using NativeSceneLightSelector = Fast::Renderer3ds::PicaSceneLightSelector;
using Fast::Renderer3ds::PicaScenePointLightAt;
inline float ScenePointLightAt(const Fast::Renderer3ds::PicaScenePointLight &light,
                               const std::array<float, 3> &at) noexcept {
    return PicaScenePointLightAt(light, at);
}
} // namespace Oot3dNativeGame
