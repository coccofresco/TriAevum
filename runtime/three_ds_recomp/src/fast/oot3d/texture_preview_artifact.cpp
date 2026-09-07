#include "fast/oot3d/texture_preview_artifact.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Fast::Oot3d {
namespace {

void AppendU16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
}

void AppendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value >> 16U));
    bytes.push_back(static_cast<uint8_t>(value >> 24U));
}

} // namespace

std::vector<uint8_t> EncodeTexturePreviewBmp(
    uint16_t width, uint16_t height,
    std::span<const uint8_t> rgba8) {
    const size_t pixelBytes =
        static_cast<size_t>(width) * height * 4U;
    if (width == 0U || height == 0U ||
        rgba8.size() != pixelBytes) {
        return {};
    }

    constexpr uint32_t headerBytes = 14U + 40U;
    const uint32_t fileBytes =
        headerBytes + static_cast<uint32_t>(pixelBytes);
    std::vector<uint8_t> result;
    result.reserve(fileBytes);
    result.push_back('B');
    result.push_back('M');
    AppendU32(result, fileBytes);
    AppendU16(result, 0U);
    AppendU16(result, 0U);
    AppendU32(result, headerBytes);
    AppendU32(result, 40U);
    AppendU32(result, width);
    // Negative height stores rows in the same top-to-bottom order as RGBA8.
    AppendU32(result, static_cast<uint32_t>(
                          -static_cast<int32_t>(height)));
    AppendU16(result, 1U);
    AppendU16(result, 32U);
    AppendU32(result, 0U);
    AppendU32(result, static_cast<uint32_t>(pixelBytes));
    AppendU32(result, 2835U);
    AppendU32(result, 2835U);
    AppendU32(result, 0U);
    AppendU32(result, 0U);
    for (size_t offset = 0U; offset < rgba8.size();
         offset += 4U) {
        result.push_back(rgba8[offset + 2U]);
        result.push_back(rgba8[offset + 1U]);
        result.push_back(rgba8[offset]);
        result.push_back(rgba8[offset + 3U]);
    }
    return result;
}

bool WriteTexturePreviewBmp(
    const std::filesystem::path& output,
    uint16_t width, uint16_t height,
    std::span<const uint8_t> rgba8) noexcept {
    try {
        const auto bytes =
            EncodeTexturePreviewBmp(width, height, rgba8);
        if (bytes.empty()) {
            return false;
        }
        if (output.has_parent_path()) {
            std::filesystem::create_directories(
                output.parent_path());
        }
        std::ofstream stream(
            output, std::ios::binary | std::ios::trunc);
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        return stream.good();
    } catch (...) {
        return false;
    }
}

void ExportTexturePreviewArtifact(
    uint64_t contentHash, uint16_t width, uint16_t height,
    std::span<const uint8_t> rgba8) noexcept {
    try {
        const char* directory =
            std::getenv("OOT3D_TEXTURE_PREVIEW_DIRECTORY");
        if (directory == nullptr || *directory == '\0' ||
            contentHash == 0U) {
            return;
        }
        std::ostringstream filename;
        filename << std::hex << std::setfill('0')
                 << std::setw(16) << contentHash
                 << std::dec << '_' << width << 'x' << height
                 << ".bmp";
        const std::filesystem::path output =
            std::filesystem::path(directory) / filename.str();
        if (!std::filesystem::exists(output)) {
            static_cast<void>(
                WriteTexturePreviewBmp(
                    output, width, height, rgba8));
        }
    } catch (...) {
        // Artifact export must never affect texture upload.
    }
}

} // namespace Fast::Oot3d
