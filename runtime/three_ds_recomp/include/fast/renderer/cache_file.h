#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace Fast::Renderer {
// Publish only a closed, complete file. Readers see either the old or new entry.
// This protects process interruptions, not power-loss durability of the filesystem.
bool WriteCacheFileAtomically(const std::filesystem::path& path,
                              std::span<const uint8_t> header,
                              std::span<const uint8_t> payload,
                              std::string* error = nullptr);
}
