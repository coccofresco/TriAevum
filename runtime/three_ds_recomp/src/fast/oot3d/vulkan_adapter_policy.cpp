#include "fast/oot3d/vulkan_adapter_policy.h"

#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace Fast::Oot3d {

VulkanQueueFamilySelection SelectVulkanQueueFamilies(
    const std::vector<VulkanQueueFamilyCandidate>& candidates) {
    VulkanQueueFamilySelection selection;
    for (const auto& candidate : candidates) {
        if (candidate.QueueCount == 0U) {
            continue;
        }
        if (!selection.GraphicsFamily.has_value() &&
            candidate.SupportsGraphics) {
            selection.GraphicsFamily = candidate.EnumerationIndex;
        }
        if (!selection.PresentFamily.has_value() &&
            candidate.SupportsPresentation) {
            selection.PresentFamily = candidate.EnumerationIndex;
        }
        if (candidate.SupportsGraphics &&
            candidate.SupportsPresentation) {
            selection.GraphicsFamily = candidate.EnumerationIndex;
            selection.PresentFamily = candidate.EnumerationIndex;
            return selection;
        }
    }
    return selection;
}

VulkanPresentDispatchMode ParseVulkanPresentDispatchMode(std::string_view value) {
    if (value.empty() || value == "auto") return VulkanPresentDispatchMode::Automatic;
    if (value == "inline") return VulkanPresentDispatchMode::Inline;
    if (value == "graphics") return VulkanPresentDispatchMode::GraphicsQueue;
    throw std::invalid_argument("TRIAEVUM_VULKAN_PRESENT_DISPATCH must be auto, inline or graphics");
}

VulkanQueueTopologyPlan ResolveVulkanQueueTopology(
    uint32_t graphicsFamily, uint32_t presentFamily,
    uint32_t graphicsQueueCount, VulkanPresentDispatchMode mode) {
    VulkanQueueTopologyPlan plan;
    plan.GraphicsFamily = graphicsFamily;
    plan.PresentFamily = presentFamily;
    plan.GraphicsQueueCount = graphicsQueueCount;
    plan.SharedFamily = graphicsFamily == presentFamily;
    plan.NriSwapchainEligible = plan.SharedFamily;
    if (plan.SharedFamily && graphicsQueueCount >= 2U &&
        mode != VulkanPresentDispatchMode::GraphicsQueue) {
        plan.RequestedGraphicsQueueCount = 2U;
        plan.PresentQueueIndex = 1U;
        plan.AsynchronousPresent = true;
    } else {
        plan.RequestedGraphicsQueueCount = 1U;
        plan.PresentQueueIndex = 0U;
        plan.AsynchronousPresent = !plan.SharedFamily;
    }
    if (mode != VulkanPresentDispatchMode::Automatic) plan.AsynchronousPresent = false;
    return plan;
}

VulkanAdapterIndexParseResult ParseVulkanAdapterIndex(
    std::string_view value) {
    VulkanAdapterIndexParseResult result;
    if (value.empty()) {
        result.Reason =
            "OOT3D_GRAPHICS_VULKAN_ADAPTER must be an enumerated "
            "numeric adapter index";
        return result;
    }
    uint32_t parsed = 0;
    const auto conversion = std::from_chars(
        value.data(), value.data() + value.size(), parsed, 10);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != value.data() + value.size()) {
        result.Reason =
            "OOT3D_GRAPHICS_VULKAN_ADAPTER must be an enumerated "
            "numeric adapter index";
        return result;
    }
    result.EnumerationIndex = parsed;
    return result;
}

bool IsVulkanAdapterSuitable(
    const VulkanAdapterCandidate& candidate) {
    return candidate.HasGraphicsQueue &&
           candidate.HasPresentQueue &&
           candidate.HasSwapchainExtension &&
           candidate.HasSurfaceFormats &&
           candidate.HasPresentModes;
}

int64_t ScoreVulkanAdapter(
    const VulkanAdapterCandidate& candidate) {
    if (!IsVulkanAdapterSuitable(candidate)) {
        return std::numeric_limits<int64_t>::min();
    }
    return static_cast<int64_t>(
               candidate.MaximumImageDimension2D) +
        (candidate.Class == VulkanAdapterClass::Discrete
             ? 100000LL
             : 0LL);
}

VulkanAdapterSelection SelectVulkanAdapter(
    const std::vector<VulkanAdapterCandidate>& candidates,
    std::optional<uint32_t> requestedEnumerationIndex) {
    VulkanAdapterSelection selection;
    selection.Explicit = requestedEnumerationIndex.has_value();
    if (requestedEnumerationIndex.has_value()) {
        for (size_t index = 0; index < candidates.size(); ++index) {
            if (candidates[index].EnumerationIndex !=
                *requestedEnumerationIndex) {
                continue;
            }
            if (!IsVulkanAdapterSuitable(candidates[index])) {
                selection.Reason =
                    "requested Vulkan adapter is not presentation-capable";
                return selection;
            }
            selection.CandidatePosition = index;
            return selection;
        }
        selection.Reason =
            "requested Vulkan adapter index was not enumerated";
        return selection;
    }

    int64_t bestScore = std::numeric_limits<int64_t>::min();
    for (size_t index = 0; index < candidates.size(); ++index) {
        const int64_t score = ScoreVulkanAdapter(candidates[index]);
        if (score > bestScore) {
            bestScore = score;
            selection.CandidatePosition = index;
        }
    }
    if (!selection.CandidatePosition.has_value()) {
        selection.Reason =
            "no Vulkan adapter supports graphics, presentation and swapchain";
    }
    return selection;
}

} // namespace Fast::Oot3d
