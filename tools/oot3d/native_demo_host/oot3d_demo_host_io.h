#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <nlohmann/json_fwd.hpp>

void WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& data);

// Rows of RGB bytes, top first.
void WriteBmpFromRgb8Framebuffer(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& rgb);

void WriteBmpFromRgba5551Framebuffer(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint16_t>& rgba16);

std::filesystem::path ScreenshotSequenceFramePath(const std::filesystem::path& path, uint32_t frameIndex);
