#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

inline constexpr const char* kNativeGameLaunchProfileFormat =
    "oot3d_native_game_launch_profile_v1";

// The running executable's real path (OS query), falling back to argv[0].
std::filesystem::path ResolveNativeGameExecutablePath(
    const std::filesystem::path& argv0);

std::filesystem::path DefaultNativeGameLaunchProfilePath(
    const std::filesystem::path& executablePath);

std::vector<std::string> LoadNativeGameLaunchProfile(
    const std::filesystem::path& profilePath);

} // namespace Oot3dNativeGame
