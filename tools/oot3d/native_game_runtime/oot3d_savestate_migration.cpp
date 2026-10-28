#include "oot3d_savestate_migration.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "fast/extension/portable_path_setting.h"

namespace Oot3dNativeGame {
namespace {

namespace fs = std::filesystem;

std::string Utf8(const fs::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::string GenericUtf8(const fs::path &path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

bool SameFolder(const fs::path &left, const fs::path &right) {
    std::error_code error;
    if (fs::exists(left, error) && fs::exists(right, error) && fs::equivalent(left, right, error))
        return true;
    return left.lexically_normal() == right.lexically_normal();
}

// The relative path of `path` inside `folder`, or empty when outside.
fs::path Inside(const fs::path &path, const fs::path &folder) {
    const auto relative = path.lexically_normal().lexically_relative(folder.lexically_normal());
    if (relative.empty() || relative.native().empty())
        return {};
    const auto first = *relative.begin();
    if (first == ".." || first == ".")
        return {};
    return relative;
}

// Byte-for-byte comparison; the FNV-1a hash of `source` goes in the record.
bool SameContent(const fs::path &source, const fs::path &copy, uint64_t *sourceHash) {
    std::error_code error;
    const auto sourceSize = fs::file_size(source, error);
    if (error)
        return false;
    const auto copySize = fs::file_size(copy, error);
    if (error || sourceSize != copySize)
        return false;
    std::ifstream left(source, std::ios::binary), right(copy, std::ios::binary);
    if (!left || !right)
        return false;
    uint64_t hash = 0xCBF29CE484222325ULL;
    std::array<char, 1 << 16> a{}, b{};
    for (;;) {
        left.read(a.data(), static_cast<std::streamsize>(a.size()));
        right.read(b.data(), static_cast<std::streamsize>(b.size()));
        const auto count = left.gcount();
        if (count != right.gcount())
            return false;
        if (count == 0)
            break;
        if (!std::equal(a.begin(), a.begin() + count, b.begin()))
            return false;
        for (std::streamsize index = 0; index < count; ++index) {
            hash ^= static_cast<uint8_t>(a[static_cast<size_t>(index)]);
            hash *= 0x100000001B3ULL;
        }
    }
    if (left.bad() || right.bad())
        return false;
    if (sourceHash != nullptr)
        *sourceHash = hash;
    return true;
}

std::string Hex64(uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

std::string UtcNow() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

// A JSON object file: nullopt when absent, an error when unreadable.
std::optional<nlohmann::json> ReadObject(const fs::path &path, std::string *error) {
    std::error_code status;
    if (!fs::exists(path, status))
        return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    const auto document = input ? nlohmann::json::parse(input, nullptr, false)
                                : nlohmann::json(nlohmann::json::value_t::discarded);
    if (!document.is_object()) {
        *error = Utf8(path) + " is unreadable";
        return std::optional<nlohmann::json>(std::in_place, nlohmann::json::value_t::discarded);
    }
    return std::optional<nlohmann::json>(std::in_place, document);
}

bool WriteAtomically(const fs::path &path, const nlohmann::json &document, std::string *error) {
    std::error_code status;
    fs::create_directories(path.parent_path(), status);
    const auto temporary = fs::path(path).concat(".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << document.dump(4) << '\n';
        output.flush();
        if (!output) {
            *error = "could not write " + Utf8(temporary);
            if (fs::is_regular_file(temporary, status))
                fs::remove(temporary, status);
            return false;
        }
    }
    fs::rename(temporary, path, status);
    if (status) {
        *error = "could not write " + Utf8(path) + ": " + status.message();
        if (fs::is_regular_file(temporary, status))
            fs::remove(temporary, status);
        return false;
    }
    return true;
}

// Copies one file to a destination that does not exist, through a verified
// temporary file. Never replaces a file.
bool CopyVerified(const fs::path &source, const fs::path &destination, uint64_t *hash, std::string *error) {
    std::error_code status;
    fs::create_directories(destination.parent_path(), status);
    if (status) {
        *error = "could not create " + Utf8(destination.parent_path()) + ": " + status.message();
        return false;
    }
    const auto temporary = fs::path(destination).concat(".migrating");
    fs::remove(temporary, status);
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, status);
    if (status || !SameContent(source, temporary, hash)) {
        *error = "could not copy " + Utf8(source) + (status ? ": " + status.message() : " (verification failed)");
        fs::remove(temporary, status);
        return false;
    }
    if (fs::exists(destination, status)) {
        fs::remove(temporary, status);
        *error = Utf8(destination) + " appeared during the copy";
        return false;
    }
    fs::rename(temporary, destination, status);
    if (status) {
        *error = "could not name " + Utf8(destination) + ": " + status.message();
        fs::remove(temporary, status);
        return false;
    }
    return true;
}

void WriteReadme(const fs::path &legacy, const fs::path &managed, const std::vector<fs::path> &conflicts) {
    std::ofstream readme(legacy / kSavestateMovedReadme, std::ios::binary | std::ios::trunc);
    readme << "TriAevum now keeps save states in the installation's data folder:\n"
           << "  " << Utf8(managed) << "\n\n"
           << "The save states in this folder were copied there and checked. These files\n"
           << "were left here as they were, so you can delete this folder once you have\n"
           << "checked your save states in the game (F1 > Save states).\n";
    if (!conflicts.empty()) {
        readme << "\nThese files were not copied because a different file with the same name\n"
               << "already exists in the new folder; they are only here:\n";
        for (const auto &conflict : conflicts)
            readme << "  " << GenericUtf8(conflict) << '\n';
    }
}

SavestateMigrationResult Fail(SavestateMigrationResult result, const SavestateMigrationRequest &request,
                              const std::string &reason) {
    result.Status = SavestateMigrationStatus::Failed;
    result.DefaultDirectory = request.LegacyDirectory;
    result.Message = "save states stay in " + Utf8(request.LegacyDirectory) + " this session: " + reason;
    return result;
}

} // namespace

const char *SavestateMigrationStatusName(SavestateMigrationStatus status) noexcept {
    switch (status) {
    case SavestateMigrationStatus::NotNeeded:
        return "not_needed";
    case SavestateMigrationStatus::Completed:
        return "completed";
    case SavestateMigrationStatus::Failed:
        return "failed";
    }
    return "unknown";
}

SavestateMigrationResult MigrateLegacySavestates(const SavestateMigrationRequest &request) {
    SavestateMigrationResult result;
    result.DefaultDirectory = request.ManagedDirectory;
    const auto notNeeded = [&](std::string reason) {
        result.Message = std::move(reason);
        return result;
    };
    std::error_code status;
    if (request.FixedSlot)
        return notNeeded("--quick-state fixes the save-state file");
    if (request.LegacyDirectory.empty() || request.ManagedDirectory.empty() ||
        SameFolder(request.LegacyDirectory, request.ManagedDirectory))
        return notNeeded("the save-state folder is unchanged");
    if (!fs::is_directory(request.LegacyDirectory, status))
        return notNeeded("no earlier save-state folder");

    std::string error;
    auto preferences = ReadObject(request.PreferencesPath, &error);
    if (!error.empty())
        return Fail(result, request, error);
    if (preferences.has_value()) {
        const auto directory = preferences->find("Directory");
        if (directory != preferences->end() && !directory->is_null()) {
            if (!directory->is_string())
                return Fail(result, request, Utf8(request.PreferencesPath) + " has an invalid Directory");
            if (!directory->get_ref<const std::string &>().empty())
                return notNeeded("a save-state folder was chosen");
        }
    }
    auto migrations = ReadObject(request.MigrationsPath, &error);
    if (!error.empty())
        return Fail(result, request, error);
    nlohmann::json records = nlohmann::json::array();
    if (migrations.has_value()) {
        if (migrations->contains("migrations") && (*migrations)["migrations"].is_array())
            records = (*migrations)["migrations"];
        // The record's folders are relative to its file inside the
        // installation, so a moved installation still recognises it.
        const auto recordFolder = request.MigrationsPath.parent_path();
        for (const auto &record : records) {
            if (record.is_object() && record.value("id", std::string{}) == kSavestateMigrationId &&
                record.value("status", std::string{}) == "completed" &&
                SameFolder(Fast::Extension::LoadPathSetting(record.value("source", std::string{}), recordFolder),
                           request.LegacyDirectory) &&
                SameFolder(Fast::Extension::LoadPathSetting(record.value("destination", std::string{}), recordFolder),
                           request.ManagedDirectory))
                return notNeeded("save states were already moved");
        }
    }

    // Every regular file below the earlier folder, in a stable order.
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(request.LegacyDirectory, status), end; !status && it != end;
         it.increment(status)) {
        std::error_code entryStatus;
        if (it->is_symlink(entryStatus) || !it->is_regular_file(entryStatus))
            continue;
        const auto relative = it->path().lexically_relative(request.LegacyDirectory);
        if (relative == fs::path(kSavestateMovedReadme))
            continue;
        files.push_back(relative);
    }
    if (status)
        return Fail(result, request, "could not list " + Utf8(request.LegacyDirectory) + ": " + status.message());
    if (files.empty())
        return notNeeded("the earlier save-state folder is empty");
    std::sort(files.begin(), files.end());

    std::set<fs::path> verified;
    nlohmann::json copied = nlohmann::json::array();
    for (const auto &relative : files) {
        const auto source = request.LegacyDirectory / relative;
        const auto destination = request.ManagedDirectory / relative;
        uint64_t hash = 0;
        if (fs::exists(destination, status)) {
            if (SameContent(source, destination, &hash)) {
                ++result.PresentFiles;
                verified.insert(relative);
            } else {
                result.Conflicts.push_back(relative);
            }
            continue;
        }
        if (!CopyVerified(source, destination, &hash, &error))
            return Fail(result, request, error);
        const auto bytes = fs::file_size(destination, status);
        ++result.CopiedFiles;
        result.CopiedBytes += bytes;
        verified.insert(relative);
        copied.push_back({{"path", GenericUtf8(relative)}, {"bytes", bytes}, {"fnv1a64", Hex64(hash)}});
    }

    WriteReadme(request.LegacyDirectory, request.ManagedDirectory, result.Conflicts);

    // LastState follows its verified copy; the chosen Directory ("" or absent)
    // and anything else in the file stay as they are.
    if (preferences.has_value()) {
        const auto last = preferences->find("LastState");
        if (last != preferences->end() && last->is_string()) {
            const auto preferencesFolder = request.PreferencesPath.parent_path();
            const auto relative = Inside(
                Fast::Extension::LoadPathSetting(last->get<std::string>(), preferencesFolder), request.LegacyDirectory);
            if (!relative.empty() && verified.contains(relative.lexically_normal())) {
                (*preferences)["LastState"] =
                    Fast::Extension::StorePathSetting(request.ManagedDirectory / relative, preferencesFolder);
                if (!WriteAtomically(request.PreferencesPath, *preferences, &error))
                    return Fail(result, request, error);
                result.LastStateRewritten = true;
            }
        }
    }

    nlohmann::json conflicts = nlohmann::json::array();
    for (const auto &conflict : result.Conflicts)
        conflicts.push_back(GenericUtf8(conflict));
    nlohmann::json record{
        {"id", kSavestateMigrationId},
        {"status", "completed"},
        {"source", Fast::Extension::StorePathSetting(request.LegacyDirectory, request.MigrationsPath.parent_path())},
        {"destination",
         Fast::Extension::StorePathSetting(request.ManagedDirectory, request.MigrationsPath.parent_path())},
        {"copied_files", result.CopiedFiles},
        {"present_files", result.PresentFiles},
        {"copied_bytes", result.CopiedBytes},
        {"files", copied},
        {"conflicts", conflicts},
        {"last_state_rewritten", result.LastStateRewritten},
        {"completed_at", UtcNow()},
    };
    nlohmann::json kept = nlohmann::json::array();
    for (const auto &existing : records)
        if (!existing.is_object() || existing.value("id", std::string{}) != kSavestateMigrationId)
            kept.push_back(existing);
    kept.push_back(std::move(record));
    nlohmann::json document = migrations.has_value() ? *migrations : nlohmann::json::object();
    document["format"] = kMigrationsFormat;
    document["migrations"] = std::move(kept);
    if (!WriteAtomically(request.MigrationsPath, document, &error))
        return Fail(result, request, error);

    result.Status = SavestateMigrationStatus::Completed;
    std::ostringstream message;
    message << "save states moved to " << Utf8(request.ManagedDirectory) << ": " << result.CopiedFiles
            << " copied, " << result.PresentFiles << " already there, " << result.Conflicts.size()
            << " left in " << Utf8(request.LegacyDirectory);
    result.Message = message.str();
    return result;
}

} // namespace Oot3dNativeGame
