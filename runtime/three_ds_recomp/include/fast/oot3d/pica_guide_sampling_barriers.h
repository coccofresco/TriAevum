#pragma once

#include "fast/oot3d/display_effect_resources.h"
#include "fast/oot3d/effect_graph.h"

#include <vulkan/vulkan.h>

#include <array>

namespace Fast::Oot3d {

struct PicaGuideSamplingBarrierBatch {
    std::array<VkImageMemoryBarrier, 7> Images{};
    uint32_t Count = 0;
    uint32_t DeclaredCount = 0;
    uint32_t MissingCount = 0;
    uint32_t ResourceMask = 0;
    VkPipelineStageFlags SourceStages = 0;
    VkPipelineStageFlags DestinationStages = 0;

    [[nodiscard]] bool Complete() const noexcept {
        return MissingCount == 0U && Count == DeclaredCount;
    }
};

// Transitions only the native depth/guide resources read by this compiled
// pass and not already represented by sampledResourceMask.
[[nodiscard]] PicaGuideSamplingBarrierBatch
BuildPicaGuideSamplingBarriers(
    const CompiledEffectPass& pass,
    const DisplayEffectResourceTable& resources,
    uint32_t sampledResourceMask) noexcept;

// Restores exactly the resources transitioned during pass execution. The
// reverse dependency covers compute and fragment reads.
[[nodiscard]] PicaGuideSamplingBarrierBatch
BuildPicaGuideAttachmentBarriers(
    const DisplayEffectResourceTable& resources,
    uint32_t sampledResourceMask) noexcept;

} // namespace Fast::Oot3d
