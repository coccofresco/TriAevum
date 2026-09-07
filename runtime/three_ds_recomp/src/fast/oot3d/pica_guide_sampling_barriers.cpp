#include "fast/oot3d/pica_guide_sampling_barriers.h"

#include <cstddef>
#include <utility>

namespace Fast::Oot3d {
namespace {

VkImageMemoryBarrier MakeBarrier(
    VkImage image, VkImageAspectFlags aspect,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkAccessFlags sourceAccess,
    VkAccessFlags destinationAccess) noexcept {
    VkImageMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    return barrier;
}

[[nodiscard]] constexpr uint32_t ResourceBit(
    EffectResource resource) noexcept {
    return 1U << static_cast<uint32_t>(resource);
}

PicaGuideSamplingBarrierBatch BuildBarriers(
    const DisplayEffectResourceTable& resources,
    const CompiledEffectPass* pass,
    uint32_t resourceMask, bool toShaderRead) noexcept {
    PicaGuideSamplingBarrierBatch batch;
    const VkAccessFlags colorAccess =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    const VkAccessFlags depthAccess =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    const VkAccessFlags shaderAccess = VK_ACCESS_SHADER_READ_BIT;

    const VkImageLayout depthOld = toShaderRead
        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkImageLayout depthNew = toShaderRead
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkImageLayout colorOld = toShaderRead
        ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkImageLayout colorNew = toShaderRead
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    const std::array<std::pair<EffectResource, VkImageAspectFlags>, 7> candidates{ {
        { EffectResource::NativeDepth, VK_IMAGE_ASPECT_DEPTH_BIT },
        { EffectResource::NormalGuide, VK_IMAGE_ASPECT_COLOR_BIT },
        { EffectResource::MaterialGuide, VK_IMAGE_ASPECT_COLOR_BIT },
        { EffectResource::RigidMotionGuide, VK_IMAGE_ASPECT_COLOR_BIT },
        { EffectResource::AmbientGuide, VK_IMAGE_ASPECT_COLOR_BIT },
        { EffectResource::FogGuide, VK_IMAGE_ASPECT_COLOR_BIT },
        { EffectResource::OutlineGeometryGuide, VK_IMAGE_ASPECT_COLOR_BIT },
    } };
    for (const auto& [resource, aspect] : candidates) {
        const uint32_t bit = ResourceBit(resource);
        const bool requested = toShaderRead
            ? pass != nullptr && pass->ReadsResource(resource) &&
                  (resourceMask & bit) == 0U
            : (resourceMask & bit) != 0U;
        if (!requested) {
            continue;
        }
        ++batch.DeclaredCount;
        const auto* binding = resources.Find(resource);
        if (binding == nullptr ||
            binding->Kind != EffectResourceBindingKind::Image) {
            ++batch.MissingCount;
            continue;
        }
        const VkImage image = reinterpret_cast<VkImage>(
            binding->NativeImage);
        batch.ResourceMask |= bit;
        const bool depth = resource == EffectResource::NativeDepth;
        batch.Images[batch.Count++] = MakeBarrier(
            image, aspect,
            depth ? depthOld : colorOld,
            depth ? depthNew : colorNew,
            toShaderRead
                ? (depth ? depthAccess : colorAccess)
                : shaderAccess,
            toShaderRead
                ? shaderAccess
                : (depth ? depthAccess : colorAccess));
    }

    if (toShaderRead) {
        batch.SourceStages =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        batch.DestinationStages =
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        batch.SourceStages =
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        batch.DestinationStages =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    return batch;
}

} // namespace

PicaGuideSamplingBarrierBatch
BuildPicaGuideSamplingBarriers(
    const CompiledEffectPass& pass,
    const DisplayEffectResourceTable& resources,
    uint32_t sampledResourceMask) noexcept {
    return BuildBarriers(
        resources, &pass, sampledResourceMask, true);
}

PicaGuideSamplingBarrierBatch
BuildPicaGuideAttachmentBarriers(
    const DisplayEffectResourceTable& resources,
    uint32_t sampledResourceMask) noexcept {
    return BuildBarriers(
        resources, nullptr, sampledResourceMask, false);
}

} // namespace Fast::Oot3d
