#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include <nlohmann/json_fwd.hpp>

// Product metadata is available without a title, window or initialized GPU.
void WriteTriAevumProductInfo(std::ostream& output);

// The configuration a new installation starts from ({"Graphics": ...}): the
// Enhanced preset with the title's grass source rules at 1280x720. Android
// fills the display (Free aspect) instead of a 16:9 pillarbox.
[[nodiscard]] nlohmann::json TriAevumProductDefaultConfig();

// Writes the product defaults when the configuration file is missing or has
// no Graphics block; every existing choice is kept. Returns false with an
// error when an existing file cannot be read or the file cannot be written.
[[nodiscard]] bool InitializeTriAevumProductConfig(
    const std::filesystem::path& path, bool* written, std::string* error);
