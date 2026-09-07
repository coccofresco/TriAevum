#include "ship/config/ConfigPersistence.h"

#include <atomic>
#include <fstream>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Ship {
namespace {

std::vector<std::string_view> SplitKey(std::string_view key) {
    std::vector<std::string_view> components;
    while (!key.empty()) {
        const size_t separator = key.find('.');
        const std::string_view component =
            key.substr(0U, separator);
        if (component.empty()) {
            return {};
        }
        components.push_back(component);
        if (separator == std::string_view::npos) {
            break;
        }
        key.remove_prefix(separator + 1U);
    }
    return components;
}

} // namespace

bool SetNestedConfigBlock(
    nlohmann::json& root, std::string_view key,
    nlohmann::json block) {
    const auto components = SplitKey(key);
    if (components.empty()) {
        return false;
    }
    if (!root.is_object()) {
        root = nlohmann::json::object();
    }
    nlohmann::json* current = &root;
    for (size_t index = 0U; index < components.size(); ++index) {
        const std::string component(components[index]);
        if (index + 1U == components.size()) {
            (*current)[component] = std::move(block);
            return true;
        }
        auto& child = (*current)[component];
        if (!child.is_object()) {
            child = nlohmann::json::object();
        }
        current = &child;
    }
    return false;
}

AtomicConfigWriteResult WriteConfigFileAtomically(
    const std::filesystem::path& target,
    std::string_view contents) {
    if (target.empty()) {
        return {false, "empty config path"};
    }
    static std::atomic_uint64_t temporarySerial = 0U;
    std::filesystem::path temporary = target;
    temporary += ".tmp." +
        std::to_string(temporarySerial.fetch_add(
            1U, std::memory_order_relaxed));
    {
        std::ofstream file(
            temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return {
                false,
                "unable to open temporary " +
                    temporary.string(),
            };
        }
        file.write(contents.data(),
                   static_cast<std::streamsize>(
                       contents.size()));
        file.flush();
        if (!file) {
            file.close();
            std::error_code cleanupError;
            std::filesystem::remove(
                temporary, cleanupError);
            return {
                false,
                "unable to write temporary " +
                    temporary.string(),
            };
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return {
            false,
            "unable to replace " + target.string() +
                " (Win32 " + std::to_string(error) + ")",
        };
    }
#else
    std::error_code replaceError;
    std::filesystem::rename(
        temporary, target, replaceError);
    if (replaceError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return {
            false,
            "unable to replace " + target.string() +
                ": " + replaceError.message(),
        };
    }
#endif
    return {true, {}};
}

} // namespace Ship
