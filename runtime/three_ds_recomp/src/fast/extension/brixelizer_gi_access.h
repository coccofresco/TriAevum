#pragma once

// Provider-private bridge between the scene distance field and the GI passes
// that trace it. Included only where the FidelityFX Brixelizer SDK is built.

#include "fast/extension/brixelizer_scene.h"

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_brixelizer.h>
#include <FidelityFX/host/ffx_brixelizergi.h>

namespace Fast::Extension {

struct BrixelizerSceneGiAccess {
    // The field's FidelityFX backend, created for the field and
    // BrixelizerScene::kMaxGiSurfaces GI contexts; null before Initialize.
    static FfxInterface* Backend(BrixelizerScene& scene);
    // Fills the field resources, the environment and the noise of frame
    // `frameIndex` into `dispatch`, recording the one-time initialization of
    // the constant GI inputs into `commandBuffer`. False before the field's
    // first update (its resources hold no data yet).
    // `environment` is the linear radiance of the (uniform) environment:
    // rays that leave the field see it.
    static bool Bind(BrixelizerScene& scene, VkCommandBuffer commandBuffer, uint64_t frameIndex,
                     const std::array<float, 3>& environment,
                     FfxBrixelizerGIDispatchDescription& dispatch);
};

} // namespace Fast::Extension
