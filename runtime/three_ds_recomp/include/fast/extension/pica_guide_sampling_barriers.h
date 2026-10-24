#pragma once

#include "fast/extension/display_effect_resources.h"
#include "fast/extension/effect_graph.h"

#include <vulkan/vulkan.h>

#include <array>

namespace Fast::Extension {

struct PicaGuideSamplingBarrierBatch {
    std::array<VkImageMemoryBarrier, 8> Images{};
    uint32_t Count = 0;
    uint32_t DeclaredCount = 0;
    uint32_t MissingCount = 0;
    uint64_t ResourceMask = 0;
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
    uint64_t sampledResourceMask) noexcept;

// Restores exactly the resources transitioned during pass execution. The
// reverse dependency covers compute and fragment reads.
[[nodiscard]] PicaGuideSamplingBarrierBatch
BuildPicaGuideAttachmentBarriers(
    const DisplayEffectResourceTable& resources,
    uint64_t sampledResourceMask) noexcept;

} // namespace Fast::Extension
