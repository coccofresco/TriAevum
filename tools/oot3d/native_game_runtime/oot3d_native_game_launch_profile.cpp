#include "oot3d_native_game_launch_profile.h"

#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Oot3dNativeGame {
namespace {

void ReplaceAll(std::string& value, std::string_view token,
                std::string_view replacement) {
    size_t offset = 0;
    while ((offset = value.find(token, offset)) != std::string::npos) {
        value.replace(offset, token.size(), replacement);
        offset += replacement.size();
    }
}

std::string ExpandVariables(
    std::string value,
    const std::unordered_map<std::string, std::string>& variables) {
    for (const auto& [name, replacement] : variables) {
        ReplaceAll(value, "${" + name + "}", replacement);
    }
    return value;
}

} // namespace

std::filesystem::path ResolveNativeGameExecutablePath(
    const std::filesystem::path& argv0) {
    // argv[0] is a bare name for PATH/.desktop launches; the profile, plugin
    // and portable data live beside the real executable, not the cwd.
#if defined(_WIN32)
    wchar_t executable[32768];
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (length != 0 && length != 32768) {
        return std::filesystem::path(executable).lexically_normal();
    }
#elif defined(__linux__)
    std::error_code error;
    auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) {
        return executable.lexically_normal();
    }
#endif
    std::filesystem::path resolved = argv0;
    if (resolved.empty()) {
        resolved = "oot3d_native_game";
    }
    if (resolved.is_relative()) {
        resolved = std::filesystem::absolute(resolved);
    }
    return resolved.lexically_normal();
}

std::filesystem::path DefaultNativeGameLaunchProfilePath(
    const std::filesystem::path& executablePath) {
    std::filesystem::path resolved = executablePath;
    if (resolved.empty()) {
        resolved = "oot3d_native_game";
    }
    if (resolved.is_relative()) {
        resolved = std::filesystem::absolute(resolved);
    }
    resolved = resolved.lexically_normal();
    auto profileName = resolved.filename();
    profileName.replace_extension(".launch.json");
    return resolved.parent_path() / profileName;
}

std::vector<std::string> LoadNativeGameLaunchProfile(
    const std::filesystem::path& profilePath) {
    const auto resolvedPath =
        std::filesystem::absolute(profilePath).lexically_normal();
    std::ifstream stream(resolvedPath);
    if (!stream) {
        throw std::runtime_error("could not open native game launch profile: " +
                                 resolvedPath.string());
    }

    nlohmann::json document;
    try {
        stream >> document;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("invalid native game launch profile JSON: " +
                                 std::string(error.what()));
    }

    if (!document.is_object() ||
        document.value("format", std::string{}) !=
            kNativeGameLaunchProfileFormat) {
        throw std::runtime_error(
            "native game launch profile has an unsupported format: " +
            resolvedPath.string());
    }

    std::unordered_map<std::string, std::string> variables{
        {"profile_dir", resolvedPath.parent_path().generic_string()},
        {"executable_dir", resolvedPath.parent_path().generic_string()},
    };
    if (const auto variablesIt = document.find("variables");
        variablesIt != document.end()) {
        if (!variablesIt->is_object()) {
            throw std::runtime_error(
                "native game launch profile variables must be an object");
        }
        for (const auto& [name, value] : variablesIt->items()) {
            if (!value.is_string()) {
                throw std::runtime_error(
                    "native game launch profile variable is not a string: " +
                    name);
            }
            variables[name] = ExpandVariables(
                value.get<std::string>(), variables);
        }
    }

    const auto argumentsIt = document.find("arguments");
    if (argumentsIt == document.end() || !argumentsIt->is_array()) {
        throw std::runtime_error(
            "native game launch profile requires an arguments array");
    }

    std::vector<std::string> arguments;
    arguments.reserve(argumentsIt->size());
    for (const auto& value : *argumentsIt) {
        if (!value.is_string()) {
            throw std::runtime_error(
                "native game launch profile arguments must be strings");
        }
        auto argument = ExpandVariables(value.get<std::string>(), variables);
        if (argument == "--launch-profile") {
            throw std::runtime_error(
                "native game launch profiles cannot include another launch "
                "profile");
        }
        arguments.push_back(std::move(argument));
    }
    if (arguments.empty()) {
        throw std::runtime_error(
            "native game launch profile contains no arguments");
    }
    return arguments;
}

} // namespace Oot3dNativeGame
