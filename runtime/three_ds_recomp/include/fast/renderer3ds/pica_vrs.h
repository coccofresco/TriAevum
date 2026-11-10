#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Variable-rate shading, phase A: a per-draw-class pipeline fragment shading
// rate set as dynamic state (VK_KHR_fragment_shading_rate, combiners
// KEEP/KEEP, no rate attachment). Default (unset) = clouds: the depth-off
// Atmosphere-layer draws at 2x2. TRIAEVUM_VRS_SKY=0 turns it off, =1 coarsens
// every Atmosphere-layer draw (sky quad/dome, cloud layers); every other draw
// stays 1x1. TRIAEVUM_VRS_SKY_RATE=2x2 (default) | 4x4. Without device
// support the policy is ignored and no pipeline or device state changes.
namespace Fast::Renderer3ds::PicaVrs {

enum class SkyPolicy : uint8_t { Off, Atmosphere, DepthOffAtmosphere };

// Rate codes carried by a draw: 0 = 1x1, 1 = 2x2, 2 = 4x4.
inline constexpr uint8_t kRate1x1 = 0U;
inline constexpr uint8_t kRate2x2 = 1U;
inline constexpr uint8_t kRate4x4 = 2U;

inline SkyPolicy RequestedPolicy() noexcept {
    const char* value = std::getenv("TRIAEVUM_VRS_SKY");
    if (value == nullptr || *value == '\0' || std::strcmp(value, "clouds") == 0)
        return SkyPolicy::DepthOffAtmosphere;
    if (std::strcmp(value, "0") == 0) return SkyPolicy::Off;
    return SkyPolicy::Atmosphere;
}

inline uint8_t RequestedSkyRate() noexcept {
    const char* value = std::getenv("TRIAEVUM_VRS_SKY_RATE");
    return value != nullptr && std::strcmp(value, "4x4") == 0 ? kRate4x4 : kRate2x2;
}

// Set once at device creation: true only when the switch is on and the device
// enabled pipelineFragmentShadingRate (every PICA pipeline then declares the
// rate as dynamic state and every PICA draw sets it).
inline std::atomic<bool> gDynamic{false};
inline std::atomic<uint8_t> gSkyRate{kRate2x2};
inline std::atomic<uint8_t> gPolicy{0};
inline PFN_vkCmdSetFragmentShadingRateKHR gCmdSetRate = nullptr;

inline bool Dynamic() noexcept { return gDynamic.load(std::memory_order_relaxed); }

inline VkExtent2D Extent(uint8_t rate) noexcept {
    return rate == kRate4x4 ? VkExtent2D{4U, 4U} : rate == kRate2x2 ? VkExtent2D{2U, 2U} : VkExtent2D{1U, 1U};
}

// The rate of one draw. Draws writing gl_FragDepth or discarding (alpha test,
// discard) stay 1x1: a discard would kill the whole coarse fragment.
inline uint8_t DrawRate(bool atmosphereLayer, bool depthTestOrWrite, bool writesFragDepth, bool discards) noexcept {
    if (!Dynamic() || !atmosphereLayer || writesFragDepth || discards) return kRate1x1;
    const auto policy = static_cast<SkyPolicy>(gPolicy.load(std::memory_order_relaxed));
    if (policy == SkyPolicy::Off) return kRate1x1;
    if (policy == SkyPolicy::DepthOffAtmosphere && depthTestOrWrite) return kRate1x1;
    return gSkyRate.load(std::memory_order_relaxed);
}

inline void CmdSetRate(VkCommandBuffer command, uint8_t rate) noexcept {
    if (!Dynamic() || gCmdSetRate == nullptr) return;
    const VkExtent2D size = Extent(rate);
    const VkFragmentShadingRateCombinerOpKHR combiners[2]{VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
                                                          VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR};
    gCmdSetRate(command, &size, combiners);
}

} // namespace Fast::Renderer3ds::PicaVrs
