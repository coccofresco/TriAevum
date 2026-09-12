#include "fast/renderer/cache_file.h"

#include <fstream>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Fast::Renderer {
bool WriteCacheFileAtomically(const std::filesystem::path& path,
                              std::span<const uint8_t> header,
                              std::span<const uint8_t> payload, std::string* error) {
    std::filesystem::path temporary;
    try {
        if (path.empty()) throw std::runtime_error("empty cache path");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::random_device random;
        temporary = path;
        temporary += "." + std::to_string(random()) + "." + std::to_string(random()) + ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!header.empty()) output.write(reinterpret_cast<const char*>(header.data()), header.size());
        if (!payload.empty()) output.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        output.flush();
        if (!output) throw std::runtime_error("cache write failed");
        output.close();
        if (!output) throw std::runtime_error("cache close failed");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cache atomic replacement failed");
#else
        std::filesystem::rename(temporary, path);
#endif
        if (error) error->clear();
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        std::error_code ignored;
        if (!temporary.empty()) std::filesystem::remove(temporary, ignored);
        return false;
    }
}
}
