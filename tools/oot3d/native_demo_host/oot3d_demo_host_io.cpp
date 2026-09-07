#include "oot3d_demo_host_io.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

void WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& data) {
    if (path.empty()) {
        return;
    }
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write JSON file: " + path.string());
    }
    file << data.dump(2) << "\n";
}

namespace {

void WriteLe16(std::ofstream& file, uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
    };
    file.write(bytes, sizeof(bytes));
}

void WriteLe32(std::ofstream& file, uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    file.write(bytes, sizeof(bytes));
}

uint8_t ExpandRgba5551Channel(uint16_t value) {
    const uint8_t v = static_cast<uint8_t>(value & 0x1f);
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}

} // namespace

void WriteBmpFromRgba5551Framebuffer(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint16_t>& rgba16) {
    if (path.empty() || width == 0 || height == 0 || rgba16.size() < static_cast<size_t>(width) * height) {
        return;
    }

    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    const uint32_t rowStride = ((width * 3u) + 3u) & ~3u;
    const uint32_t pixelDataSize = rowStride * height;
    const uint32_t fileHeaderSize = 14;
    const uint32_t dibHeaderSize = 40;
    const uint32_t pixelOffset = fileHeaderSize + dibHeaderSize;
    const uint32_t fileSize = pixelOffset + pixelDataSize;

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("could not write BMP screenshot: " + path.string());
    }

    file.put('B');
    file.put('M');
    WriteLe32(file, fileSize);
    WriteLe16(file, 0);
    WriteLe16(file, 0);
    WriteLe32(file, pixelOffset);
    WriteLe32(file, dibHeaderSize);
    WriteLe32(file, width);
    WriteLe32(file, height);
    WriteLe16(file, 1);
    WriteLe16(file, 24);
    WriteLe32(file, 0);
    WriteLe32(file, pixelDataSize);
    WriteLe32(file, 2835);
    WriteLe32(file, 2835);
    WriteLe32(file, 0);
    WriteLe32(file, 0);

    std::vector<uint8_t> row(rowStride, 0);
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t srcY = height - 1u - y;
        std::fill(row.begin(), row.end(), 0);
        for (uint32_t x = 0; x < width; ++x) {
            const uint16_t pixel = rgba16[static_cast<size_t>(srcY) * width + x];
            const uint8_t r = ExpandRgba5551Channel(pixel >> 11);
            const uint8_t g = ExpandRgba5551Channel(pixel >> 6);
            const uint8_t b = ExpandRgba5551Channel(pixel >> 1);
            row[x * 3u + 0u] = b;
            row[x * 3u + 1u] = g;
            row[x * 3u + 2u] = r;
        }
        file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
}

std::filesystem::path ScreenshotSequenceFramePath(const std::filesystem::path& path, uint32_t frameIndex) {
    std::ostringstream name;
    name << path.stem().string() << "_" << std::setw(6) << std::setfill('0') << frameIndex;
    const std::string extension = path.extension().empty() ? ".bmp" : path.extension().string();
    return path.parent_path() / (name.str() + extension);
}
