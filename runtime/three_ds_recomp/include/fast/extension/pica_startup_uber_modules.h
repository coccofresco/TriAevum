#pragma once
// The uber fragment modules a title's startup preparation creates, as title
// data, and the recorder that produces that data from real sessions.
//
// A module is named by the key its pack entry carries (the source identity of
// the composed superset program: Id, SecondaryHash, Size), which is stable
// across builds as long as the module itself is. The list is title data
// (e.g. titles/mm3d/shaders/mm3d_startup_uber_modules.json); the shared
// renderer and Forge's pipeline helper both select through
// SelectStartupUberModules, so they prepare the same parts.
//
// An empty list selects the whole family the title's lighting admits: the
// behaviour without a list. Modules not listed stay lazy (created by their
// first draw); draw-time lookup always searches the full family.
#include "fast/renderer3ds/pica_fragment_artifact.h"
#include "fast/renderer3ds/pica_shader_source_identity.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace Fast::Extension {

inline constexpr std::string_view kStartupUberModulesSchema = "triaevum.startup_uber_modules.v1";
// Opt-in: the path the session writes the uber modules its draws requested to.
inline constexpr const char* kRecordUberModulesEnvironment = "TRIAEVUM_RECORD_UBER_MODULES";

using PicaUberModuleKey = Renderer3ds::PicaShaderSourceIdentity;

[[nodiscard]] inline std::string FormatPicaUberModuleKey(const PicaUberModuleKey& key) {
    char text[64];
    std::snprintf(text, sizeof(text), "%016" PRIx64 "-%016" PRIx64 "-%" PRIx64, key.Id, key.SecondaryHash,
                  key.Size);
    return text;
}

[[nodiscard]] inline std::optional<PicaUberModuleKey> ParsePicaUberModuleKey(std::string_view text) {
    PicaUberModuleKey key;
    uint64_t* fields[] = {&key.Id, &key.SecondaryHash, &key.Size};
    size_t at = 0;
    for (size_t field = 0; field < 3; ++field) {
        const size_t end = field < 2 ? text.find('-', at) : text.size();
        if (end == std::string_view::npos || end == at || end - at > 16) return std::nullopt;
        uint64_t value = 0;
        for (size_t i = at; i < end; ++i) {
            const char c = text[i];
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (digit < 0) return std::nullopt;
            value = (value << 4U) | static_cast<uint64_t>(digit);
        }
        *fields[field] = value;
        at = end + 1;
    }
    if (!key.Available()) return std::nullopt;
    return key;
}

namespace Detail {
inline bool UberModuleKeyLess(const PicaUberModuleKey& a, const PicaUberModuleKey& b) {
    return std::tie(a.Id, a.SecondaryHash, a.Size) < std::tie(b.Id, b.SecondaryHash, b.Size);
}
} // namespace Detail

// {"schema": kStartupUberModulesSchema, "modules": ["<id>-<secondary>-<size>", ...]}
// (lowercase hex). Duplicates collapse; the result is sorted.
[[nodiscard]] inline bool ParseStartupUberModules(std::string_view text, std::vector<PicaUberModuleKey>& modules,
                                                  std::string* error) {
    modules.clear();
    const auto fail = [&](std::string why) {
        if (error != nullptr) *error = std::move(why);
        modules.clear();
        return false;
    };
    const auto json = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) return fail("startup uber module list is not a JSON object");
    const auto schema = json.find("schema");
    if (schema == json.end() || !schema->is_string() || schema->get<std::string>() != kStartupUberModulesSchema)
        return fail("startup uber module list has no schema " + std::string(kStartupUberModulesSchema));
    const auto list = json.find("modules");
    if (list == json.end() || !list->is_array()) return fail("startup uber module list has no modules array");
    for (const auto& entry : *list) {
        if (!entry.is_string()) return fail("startup uber module key is not a string");
        const auto key = ParsePicaUberModuleKey(entry.get<std::string>());
        if (!key) return fail("startup uber module key is malformed: " + entry.get<std::string>());
        modules.push_back(*key);
    }
    std::sort(modules.begin(), modules.end(), Detail::UberModuleKeyLess);
    modules.erase(std::unique(modules.begin(), modules.end()), modules.end());
    return true;
}

[[nodiscard]] inline std::string FormatStartupUberModules(std::span<const PicaUberModuleKey> modules) {
    std::vector<PicaUberModuleKey> sorted(modules.begin(), modules.end());
    std::sort(sorted.begin(), sorted.end(), Detail::UberModuleKeyLess);
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    nlohmann::json json;
    json["schema"] = kStartupUberModulesSchema;
    auto& list = json["modules"] = nlohmann::json::array();
    for (const auto& key : sorted) list.push_back(FormatPicaUberModuleKey(key));
    return json.dump(2) + "\n";
}

// The startup subset of `family`, in the family's order (so the preparation
// order of the selected parts is unchanged). Empty `listed`: the whole family.
// A listed key the family does not hold selects nothing.
[[nodiscard]] inline std::vector<Renderer3ds::PicaFragmentArtifact> SelectStartupUberModules(
    std::span<const Renderer3ds::PicaFragmentArtifact> family, std::span<const PicaUberModuleKey> listed) {
    if (listed.empty()) return {family.begin(), family.end()};
    std::vector<Renderer3ds::PicaFragmentArtifact> selected;
    for (const auto& artifact : family)
        if (std::find(listed.begin(), listed.end(), artifact.Source) != listed.end()) selected.push_back(artifact);
    return selected;
}

// Records the uber modules a session's draws request, into the list format
// above. A file already at the path is merged (sessions accumulate); the file
// is rewritten (via a temporary, then renamed) on every new module, so a
// killed session keeps what it saw.
class PicaUberModuleRecorder {
  public:
    explicit PicaUberModuleRecorder(std::filesystem::path path) : mPath(std::move(path)) {
        if (mPath.empty()) return;
        std::ifstream input(mPath, std::ios::binary);
        if (!input) return;
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::vector<PicaUberModuleKey> existing;
        std::string error;
        if (ParseStartupUberModules(text, existing, &error))
            for (const auto& key : existing) mModules.insert(Tuple(key));
        else
            std::fprintf(stderr, "TRIAEVUM_UBER_MODULE_RECORD result=ignored_existing error=%s\n", error.c_str());
    }

    // The process recorder, from kRecordUberModulesEnvironment; null when unset.
    static PicaUberModuleRecorder* FromEnvironment() {
        static PicaUberModuleRecorder* recorder = [] () -> PicaUberModuleRecorder* {
            const char* path = std::getenv(kRecordUberModulesEnvironment);
            if (path == nullptr || *path == '\0') return nullptr;
            return new PicaUberModuleRecorder(path);
        }();
        return recorder;
    }

    [[nodiscard]] bool Enabled() const noexcept { return !mPath.empty(); }

    // Thread-safe. True when the module was new (and the file written).
    bool Record(const PicaUberModuleKey& key) {
        if (!Enabled() || !key.Available()) return false;
        std::lock_guard lock(mMutex);
        if (!mModules.insert(Tuple(key)).second) return false;
        WriteLocked();
        return true;
    }

    [[nodiscard]] std::vector<PicaUberModuleKey> Modules() const {
        std::lock_guard lock(mMutex);
        std::vector<PicaUberModuleKey> modules;
        for (const auto& [id, secondary, size] : mModules) modules.push_back({id, secondary, size});
        return modules;
    }

  private:
    using KeyTuple = std::tuple<uint64_t, uint64_t, uint64_t>;
    static KeyTuple Tuple(const PicaUberModuleKey& key) { return {key.Id, key.SecondaryHash, key.Size}; }

    void WriteLocked() {
        std::vector<PicaUberModuleKey> modules;
        for (const auto& [id, secondary, size] : mModules) modules.push_back({id, secondary, size});
        const auto text = FormatStartupUberModules(modules);
        auto temporary = mPath;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << text;
            if (!output) {
                std::fprintf(stderr, "TRIAEVUM_UBER_MODULE_RECORD result=failed modules=%zu\n", modules.size());
                return;
            }
        }
        std::error_code ec;
        std::filesystem::rename(temporary, mPath, ec);
        if (ec) {
            std::filesystem::remove(mPath, ec);
            std::filesystem::rename(temporary, mPath, ec);
        }
        std::fprintf(stderr, "TRIAEVUM_UBER_MODULE_RECORD result=%s modules=%zu\n", ec ? "failed" : "stored",
                     modules.size());
    }

    std::filesystem::path mPath;
    mutable std::mutex mMutex;
    std::set<KeyTuple> mModules;
};

} // namespace Fast::Extension
