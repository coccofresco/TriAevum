#include "fast/extension/display_recovery.h"

#include <array>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <system_error>

namespace Fast::Extension {
namespace {

struct DisplayField {
    const char* Label;
    std::initializer_list<const char*> Path;
};

// ResetDisplaySettings' fields, as SerializeGraphicsSettings names them.
const std::array<DisplayField, 8> kDisplayFields{{
    {"Window mode", {"Window", "Mode"}},
    {"Window width", {"Output", "Width"}},
    {"Window height", {"Output", "Height"}},
    {"Internal resolution", {"RenderScale"}},
    {"Anti-aliasing", {"AA", "Mode"}},
    {"MSAA samples", {"AA", "MsaaSamples"}},
    {"Upscaler", {"AA", "Upscaler", "Provider"}},
    {"Upscaler quality", {"AA", "Upscaler", "Quality"}},
}};

const nlohmann::json* Find(const nlohmann::json& root, std::initializer_list<const char*> path) {
    const nlohmann::json* node = &root;
    for (const char* key : path) {
        if (!node->is_object()) return nullptr;
        const auto found = node->find(key);
        if (found == node->end()) return nullptr;
        node = &*found;
    }
    return node;
}

std::string UtcTimestamp(std::chrono::system_clock::time_point when) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(when);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::array<char, 32> text{};
    std::strftime(text.data(), text.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text.data();
}

} // namespace

std::vector<std::string> ChangedDisplayFields(const nlohmann::json& before, const nlohmann::json& after) {
    std::vector<std::string> fields;
    for (const auto& field : kDisplayFields) {
        const auto* previous = Find(before, field.Path);
        const auto* current = Find(after, field.Path);
        const bool same = previous != nullptr && current != nullptr ? *previous == *current : previous == current;
        if (!same) fields.emplace_back(field.Label);
    }
    return fields;
}

nlohmann::json DisplayRecoveryRecord(const DisplayRecovery& recovery, std::chrono::system_clock::time_point when) {
    return {
        {"format", kDisplayRecoveryFormat},
        {"time", UtcTimestamp(when)},
        {"reason", recovery.Reason},
        {"fields", recovery.Fields},
    };
}

bool WriteDisplayRecoveryRecord(const std::filesystem::path& path, const nlohmann::json& record, std::string* error) {
    const auto fail = [error](std::string message) {
        if (error != nullptr) *error = std::move(message);
        return false;
    };
    std::error_code code;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), code);
    if (code) return fail("cannot create " + path.parent_path().string() + ": " + code.message());
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << record.dump(2) << '\n';
        if (!stream.flush()) return fail("cannot write " + temporary.string());
    }
    std::filesystem::rename(temporary, path, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        return fail("cannot replace " + path.string());
    }
    return true;
}

std::string DisplayRecoveryNotice(const DisplayRecovery& recovery) {
    std::string text = "The last launch closed within its first minute, so the display settings were reset";
    if (!recovery.Fields.empty()) {
        text += " (";
        for (size_t index = 0; index < recovery.Fields.size(); ++index) {
            if (index != 0) text += ", ";
            text += recovery.Fields[index];
        }
        text += ")";
    }
    text += ". Change them again in F1 > Display.";
    return text;
}

} // namespace Fast::Extension
