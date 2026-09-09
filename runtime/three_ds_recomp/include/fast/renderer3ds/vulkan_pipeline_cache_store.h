#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Fast::Renderer3ds {

inline constexpr uint64_t kMaximumPipelineCacheBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr uint32_t kPicaPipelineCacheAbi = 1U;
inline constexpr char kNriPipelineCacheFilename[] = "nri_pipeline_cache.bin";

struct VulkanPipelineCacheHeader {
    uint32_t RendererAbi = kPicaPipelineCacheAbi;
    uint32_t VendorId = 0;
    uint32_t DeviceId = 0;
    uint32_t DriverVersion = 0;
    std::array<uint8_t, VK_UUID_SIZE> Uuid{};
    bool operator==(const VulkanPipelineCacheHeader&) const = default;
};

VulkanPipelineCacheHeader MakePipelineCacheHeader(const VkPhysicalDeviceProperties& properties);
bool LoadPipelineCacheData(const std::filesystem::path& path,
                           const VulkanPipelineCacheHeader& expected,
                           std::vector<uint8_t>& data);
// Failure never replaces the last valid file. The host owns the cache directory.
bool StorePipelineCacheData(const std::filesystem::path& path,
                            const VulkanPipelineCacheHeader& identity,
                            std::span<const uint8_t> data,
                            std::string* error = nullptr);

} // namespace Fast::Renderer3ds
