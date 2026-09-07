#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

inline constexpr const char* kNativeGameLaunchProfileFormat =
    "oot3d_native_game_launch_profile_v1";

std::filesystem::path DefaultNativeGameLaunchProfilePath(
    const std::filesystem::path& executablePath);

std::vector<std::string> LoadNativeGameLaunchProfile(
    const std::filesystem::path& profilePath);

} // namespace Oot3dNativeGame
