#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

namespace Ship {

struct AtomicConfigWriteResult {
    bool Success = false;
    std::string Error;
};

// Replaces or creates a dot-separated object block without depending on the
// runtime Window/Context graph.
[[nodiscard]] bool SetNestedConfigBlock(
    nlohmann::json& root, std::string_view key,
    nlohmann::json block);

// Writes a complete document to a sibling temporary and atomically replaces
// the destination. A failed write never truncates the last valid config.
[[nodiscard]] AtomicConfigWriteResult WriteConfigFileAtomically(
    const std::filesystem::path& target,
    std::string_view contents);

} // namespace Ship
