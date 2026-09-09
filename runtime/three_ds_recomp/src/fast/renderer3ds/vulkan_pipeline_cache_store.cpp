#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"

#include <algorithm>
#include <fstream>
#include <random>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Fast::Renderer3ds {
namespace {
constexpr uint32_t kMagic = 0x4f335650U;
constexpr uint32_t kVersion = 2U;
constexpr size_t kHeaderBytes = 56U;

void Append(std::vector<uint8_t>& bytes, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8U * i)));
}
uint64_t Read(std::span<const uint8_t> bytes, size_t offset, size_t count) {
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) value |= uint64_t{bytes[offset + i]} << (8U * i);
    return value;
}
uint64_t Checksum(std::span<const uint8_t> bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (auto byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
    return hash;
}
}

VulkanPipelineCacheHeader MakePipelineCacheHeader(const VkPhysicalDeviceProperties& properties) {
    VulkanPipelineCacheHeader result;
    result.VendorId = properties.vendorID;
    result.DeviceId = properties.deviceID;
    result.DriverVersion = properties.driverVersion;
    std::copy_n(properties.pipelineCacheUUID, VK_UUID_SIZE, result.Uuid.begin());
    return result;
}

bool LoadPipelineCacheData(const std::filesystem::path& path,
                           const VulkanPipelineCacheHeader& expected,
                           std::vector<uint8_t>& data) {
    data.clear();
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size <= kHeaderBytes || size > kHeaderBytes + kMaximumPipelineCacheBytes) return false;
    std::ifstream input(path, std::ios::binary);
    std::array<uint8_t, kHeaderBytes> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size())) return false;
    if (Read(header, 0, 4) != kMagic || Read(header, 4, 4) != kVersion ||
        Read(header, 8, 4) != expected.RendererAbi ||
        Read(header, 12, 4) != expected.VendorId || Read(header, 16, 4) != expected.DeviceId ||
        Read(header, 20, 4) != expected.DriverVersion ||
        !std::equal(expected.Uuid.begin(), expected.Uuid.end(), header.begin() + 24) ||
        Read(header, 40, 8) != size - kHeaderBytes) return false;
    std::vector<uint8_t> candidate(static_cast<size_t>(size - kHeaderBytes));
    if (!input.read(reinterpret_cast<char*>(candidate.data()), candidate.size()) ||
        input.peek() != std::char_traits<char>::eof() ||
        Checksum(candidate) != Read(header, 48, 8)) return false;
    data = std::move(candidate);
    return true;
}

bool StorePipelineCacheData(const std::filesystem::path& path,
                            const VulkanPipelineCacheHeader& identity,
                            std::span<const uint8_t> data, std::string* error) {
    std::filesystem::path temporary;
    try {
        if (path.empty() || data.empty() || data.size() > kMaximumPipelineCacheBytes)
            throw std::runtime_error("invalid pipeline cache path or size");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::vector<uint8_t> header;
        Append(header, kMagic, 4); Append(header, kVersion, 4);
        Append(header, identity.RendererAbi, 4); Append(header, identity.VendorId, 4);
        Append(header, identity.DeviceId, 4); Append(header, identity.DriverVersion, 4);
        header.insert(header.end(), identity.Uuid.begin(), identity.Uuid.end());
        Append(header, data.size(), 8); Append(header, Checksum(data), 8);
        std::random_device random;
        temporary = path;
        temporary += "." + std::to_string(random()) + "." + std::to_string(random()) + ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(header.data()), header.size());
        output.write(reinterpret_cast<const char*>(data.data()), data.size());
        output.flush();
        if (!output) throw std::runtime_error("pipeline cache write failed");
        output.close();
        if (!output) throw std::runtime_error("pipeline cache close failed");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("pipeline cache atomic replacement failed");
#else
        std::filesystem::rename(temporary, path);
#endif
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        std::error_code ignored;
        if (!temporary.empty()) std::filesystem::remove(temporary, ignored);
        return false;
    }
}
} // namespace Fast::Renderer3ds
