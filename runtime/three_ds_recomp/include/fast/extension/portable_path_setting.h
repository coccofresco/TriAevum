#pragma once

// Path settings that survive moving the installation (issue #124).
//
// A portable installation is one folder the player may move or rename: the
// installation folder A holds the data folder A/data, whose settings live in
// A/data/config. A setting that names a file or folder inside A is stored
// relative to the settings file's folder (for example "../savestates/x" or
// "../../My pack"), so it follows the installation; anything outside A stays
// absolute, because a folder the player chose elsewhere is theirs and does not
// move. A relative value is always read from the settings file's folder, never
// from the working folder.
//
// Settings outside that layout (a developer --config elsewhere) have no
// installation folder and keep absolute paths, as before.

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Fast::Extension {

namespace PortablePathDetail {

inline std::filesystem::path Normalized(const std::filesystem::path& path) {
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error) {
        canonical = std::filesystem::absolute(path, error);
        if (error) canonical = path;
    }
    return canonical.lexically_normal();
}

// The path of `path` below `folder`, or empty when it is not inside it.
inline std::filesystem::path Below(const std::filesystem::path& path, const std::filesystem::path& folder) {
    const auto relative = Normalized(path).lexically_relative(Normalized(folder));
    if (relative.empty()) return {};
    const auto first = *relative.begin();
    if (first == "..") return {};
    return relative;
}

inline std::string GenericUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

inline std::filesystem::path FromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace PortablePathDetail

// The installation folder of a settings folder laid out as <A>/data/config,
// else empty.
[[nodiscard]] inline std::filesystem::path PortableInstallationRoot(const std::filesystem::path& settingsDirectory) {
    if (settingsDirectory.empty()) return {};
    const auto config = PortablePathDetail::Normalized(settingsDirectory);
    const auto data = config.parent_path();
    if (config.filename() != "config" || data.filename() != "data" || data.parent_path() == data) return {};
    return data.parent_path();
}

// The text to store for `path`: relative to `settingsDirectory` when the path
// is inside the installation folder, else the path as given (UTF-8).
[[nodiscard]] inline std::string StorePathSetting(const std::filesystem::path& path,
                                                  const std::filesystem::path& settingsDirectory) {
    if (path.empty()) return {};
    const auto root = PortableInstallationRoot(settingsDirectory);
    if (!root.empty() && !PortablePathDetail::Below(path, root).empty()) {
        const auto relative =
            PortablePathDetail::Normalized(path).lexically_relative(PortablePathDetail::Normalized(settingsDirectory));
        if (!relative.empty()) return PortablePathDetail::GenericUtf8(relative);
    }
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

// Where an absolute path an earlier version wrote into the installation went
// after the installation moved: when `path` no longer exists, the part of it
// after a folder named like the installation folder, else its longest
// trailing part of at least two names, that exists inside the installation.
// Otherwise the path itself (a missing external folder stays as chosen).
[[nodiscard]] inline std::filesystem::path RelocatedPathSetting(const std::filesystem::path& path,
                                                                const std::filesystem::path& installationRoot) {
    std::error_code error;
    if (installationRoot.empty() || !path.has_root_path() || std::filesystem::exists(path, error)) return path;
    const auto relative = path.lexically_normal().relative_path();
    std::vector<std::filesystem::path> parts(relative.begin(), relative.end());
    while (!parts.empty() && parts.back().empty()) parts.pop_back();
    const auto tail = [&](size_t first) {
        std::filesystem::path result;
        for (auto index = first; index < parts.size(); ++index) result /= parts[index];
        return (installationRoot / result).lexically_normal();
    };
    for (size_t index = parts.size(); index-- > 0;) {
        if (index + 1 < parts.size() && parts[index] == installationRoot.filename()) {
            const auto candidate = tail(index + 1);
            if (std::filesystem::exists(candidate, error)) return candidate;
        }
    }
    for (size_t first = 1; first + 2 <= parts.size(); ++first) {
        const auto candidate = tail(first);
        if (std::filesystem::exists(candidate, error)) return candidate;
    }
    return path;
}

// The path a stored value names: a relative value is read from
// `settingsDirectory` (when known); an absolute one as stored, or where it
// went when the installation moved (RelocatedPathSetting).
[[nodiscard]] inline std::filesystem::path LoadPathSetting(std::string_view value,
                                                           const std::filesystem::path& settingsDirectory) {
    if (value.empty()) return {};
    auto path = PortablePathDetail::FromUtf8(value);
    if (!path.has_root_path()) return settingsDirectory.empty() ? path : (settingsDirectory / path).lexically_normal();
    return RelocatedPathSetting(path, PortableInstallationRoot(settingsDirectory));
}

} // namespace Fast::Extension
