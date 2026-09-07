#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Fast::Oot3d {

[[nodiscard]] std::vector<uint8_t> EncodeTexturePreviewBmp(
    uint16_t width, uint16_t height,
    std::span<const uint8_t> rgba8);

[[nodiscard]] bool WriteTexturePreviewBmp(
    const std::filesystem::path& output,
    uint16_t width, uint16_t height,
    std::span<const uint8_t> rgba8) noexcept;

// Development-only artifact export. It is a no-op unless
// OOT3D_TEXTURE_PREVIEW_DIRECTORY names an output directory.
void ExportTexturePreviewArtifact(
    uint64_t contentHash, uint16_t width, uint16_t height,
    std::span<const uint8_t> rgba8) noexcept;

} // namespace Fast::Oot3d
