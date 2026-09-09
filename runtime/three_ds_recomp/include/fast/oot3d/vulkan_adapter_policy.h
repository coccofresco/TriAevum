#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {

enum class VulkanAdapterClass : uint8_t {
    Other,
    Integrated,
    Discrete,
    Virtual,
    Cpu,
};

struct VulkanQueueTopologyPlan {
    uint32_t GraphicsFamily = 0;
    uint32_t PresentFamily = 0;
    uint32_t GraphicsQueueCount = 0;
    uint32_t RequestedGraphicsQueueCount = 1;
    uint32_t PresentQueueIndex = 0;
    bool SharedFamily = false;
    bool AsynchronousPresent = false;
    bool NriSwapchainEligible = false;
};

enum class VulkanPresentDispatchMode : uint8_t {
    Automatic,
    Inline,
    // Share the queue only when the graphics family also supports presentation.
    GraphicsQueue,
};

[[nodiscard]] VulkanPresentDispatchMode ParseVulkanPresentDispatchMode(std::string_view value);

struct VulkanQueueFamilyCandidate {
    uint32_t EnumerationIndex = 0;
    uint32_t QueueCount = 0;
    bool SupportsGraphics = false;
    bool SupportsPresentation = false;
};

struct VulkanQueueFamilySelection {
    std::optional<uint32_t> GraphicsFamily;
    std::optional<uint32_t> PresentFamily;

    [[nodiscard]] bool Complete() const {
        return GraphicsFamily.has_value() &&
               PresentFamily.has_value();
    }
};

[[nodiscard]] VulkanQueueFamilySelection SelectVulkanQueueFamilies(
    const std::vector<VulkanQueueFamilyCandidate>& candidates);

[[nodiscard]] VulkanQueueTopologyPlan ResolveVulkanQueueTopology(
    uint32_t graphicsFamily, uint32_t presentFamily,
    uint32_t graphicsQueueCount,
    VulkanPresentDispatchMode mode = VulkanPresentDispatchMode::Automatic);

struct VulkanAdapterIndexParseResult {
    std::optional<uint32_t> EnumerationIndex;
    std::string Reason;

    [[nodiscard]] bool Valid() const {
        return EnumerationIndex.has_value();
    }
};

[[nodiscard]] VulkanAdapterIndexParseResult ParseVulkanAdapterIndex(
    std::string_view value);

struct VulkanAdapterCandidate {
    uint32_t EnumerationIndex = 0;
    std::string Name;
    VulkanAdapterClass Class = VulkanAdapterClass::Other;
    uint32_t MaximumImageDimension2D = 0;
    bool HasGraphicsQueue = false;
    bool HasPresentQueue = false;
    bool HasSwapchainExtension = false;
    bool HasSurfaceFormats = false;
    bool HasPresentModes = false;
};

[[nodiscard]] bool IsVulkanAdapterSuitable(
    const VulkanAdapterCandidate& candidate);
[[nodiscard]] int64_t ScoreVulkanAdapter(
    const VulkanAdapterCandidate& candidate);

struct VulkanAdapterSelection {
    std::optional<size_t> CandidatePosition;
    bool Explicit = false;
    std::string Reason;
};

[[nodiscard]] VulkanAdapterSelection SelectVulkanAdapter(
    const std::vector<VulkanAdapterCandidate>& candidates,
    std::optional<uint32_t> requestedEnumerationIndex = std::nullopt);

} // namespace Fast::Oot3d
