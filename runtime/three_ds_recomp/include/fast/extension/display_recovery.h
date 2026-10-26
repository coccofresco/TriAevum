#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Fast::Extension {

// A display recovery: the host found that the previous launch ended within
// its first minute and restarted the presentation from the defaults
// (ResetDisplaySettings, TRIAEVUM_SAFE_PRESENTATION). The persistence port
// records it for diagnostics and tells the player.
struct DisplayRecovery {
    std::string Reason;
    // User labels of the settings whose stored values changed.
    std::vector<std::string> Fields;
};

inline constexpr std::string_view kDisplayRecoveryFormat = "triaevum_display_recovery_v1";

// The display settings that differ between two serialized "Graphics" blocks
// (SerializeGraphicsSettings), as user labels in a fixed order.
[[nodiscard]] std::vector<std::string> ChangedDisplayFields(const nlohmann::json& before,
                                                            const nlohmann::json& after);

// {"format", "time" (UTC, YYYY-MM-DDTHH:MM:SSZ), "reason", "fields"}.
[[nodiscard]] nlohmann::json DisplayRecoveryRecord(const DisplayRecovery& recovery,
                                                   std::chrono::system_clock::time_point when);

// Writes the record beside a temporary file and renames it into place.
bool WriteDisplayRecoveryRecord(const std::filesystem::path& path, const nlohmann::json& record,
                                std::string* error = nullptr);

// The notice the player sees on the first frame.
[[nodiscard]] std::string DisplayRecoveryNotice(const DisplayRecovery& recovery);

} // namespace Fast::Extension
