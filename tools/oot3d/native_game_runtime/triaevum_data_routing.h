#pragma once

// The managed data tree's routing contract (UX plan WP-05C): optional launch
// options with which Forge names the folders the game writes in an
// installation's data folder D, so they do not depend on the working
// directory or the package folder (read-only for AppImage, Flatpak and
// Android):
//
//   --log-directory <D/logs>             the runtime log, last-recovery.json
//   --crash-directory <D/logs/crashes>   Windows crash dumps
//   --texture-load-directory <D/textures/load/<title id>>
//   --texture-dump-directory <D/textures/dump/<title id>>
//                                        managed defaults of the custom
//                                        texture folders (the environment and
//                                        the F1 folders still win)
//   --savestate-directory <D/savestates> the default save-state folder (a
//                                        folder chosen in F1 still wins)
//   --title-id <16 hex digits>           the installed game's program ID
//                                        (Azahar's folder names; a USA
//                                        installation is 0004000000033500)
//
// Forge emits them only for runtimes whose product information advertises
// the `managed_data_paths` capability. Without them the runtime keeps its
// earlier locations, so launch profiles written before them still launch.
// A later occurrence wins (command-line arguments follow the profile's).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Oot3dNativeGame {

struct DataRouting {
    std::filesystem::path LogDirectory;
    std::filesystem::path CrashDirectory;
    std::filesystem::path TextureLoadDirectory;
    std::filesystem::path TextureDumpDirectory;
    std::filesystem::path SavestateDirectory;
    std::optional<uint64_t> TitleId;

    bool operator==(const DataRouting &) const = default;
};

// The capability name in `--product-info` (triaevum_product_info.cpp).
inline constexpr const char *kManagedDataPathsCapability = "managed_data_paths";

// Consumes the routing option at argv[index] and its value, advancing index
// past the value. Returns false (index unchanged) when argv[index] is not a
// routing option. Throws std::runtime_error for a missing or invalid value.
bool ParseDataRoutingOption(int argc, char **argv, int &index, DataRouting &routing);

// Exactly 16 hexadecimal digits (either case), or nullopt.
[[nodiscard]] std::optional<uint64_t> ParseTitleId(std::string_view text);
// 16 upper-case hexadecimal digits, as Azahar names its folders.
[[nodiscard]] std::string FormatTitleId(uint64_t titleId);

} // namespace Oot3dNativeGame
