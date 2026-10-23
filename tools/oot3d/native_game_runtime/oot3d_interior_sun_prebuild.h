#pragma once
#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {
// Offline only: no guest process, title plugin, window, or renderer device.
// A mask table supplies the masks the release computed for identical room
// content; other rooms are built here. The export writes this run's table.
[[nodiscard]] nlohmann::json
PrebuildInteriorSun(const std::filesystem::path &romfs,
                    const std::filesystem::path &cacheDirectory,
                    const std::filesystem::path &maskTable = {},
                    const std::filesystem::path &exportTable = {});
[[nodiscard]] nlohmann::json ListNativeEnvironmentGroups(const std::filesystem::path& romfs);
int InteriorSunToolMain(int argc, char **argv);
} // namespace Oot3dNativeGame
