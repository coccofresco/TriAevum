#include "oot3d_native_game_launch_profile.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("could not create test profile");
    }
    stream << text;
}

} // namespace

int main() {
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("oot3d_launch_profile_" + std::to_string(nonce));
    try {
        std::filesystem::create_directories(root);

        const auto defaultPath =
            Oot3dNativeGame::DefaultNativeGameLaunchProfilePath(
                root / "oot3d_native_game.exe");
        Expect(defaultPath == root / "oot3d_native_game.launch.json",
               "default profile path did not follow the executable");

        const auto profilePath = root / "custom.launch.json";
        WriteText(
            profilePath,
            R"json({
  "format": "oot3d_native_game_launch_profile_v1",
  "variables": {
    "data_root": "${profile_dir}/data"
  },
  "arguments": [
    "--resource-root",
    "${data_root}/resources",
    "--config",
    "${executable_dir}/oot3d_native_game.json"
  ]
})json");
        const auto arguments =
            Oot3dNativeGame::LoadNativeGameLaunchProfile(profilePath);
        Expect(arguments.size() == 4U,
               "launch profile returned the wrong argument count");
        Expect(arguments[0] == "--resource-root",
               "launch profile changed an option");
        Expect(arguments[1] ==
                   (root / "data/resources").generic_string(),
               "launch profile did not expand a declared variable");
        Expect(arguments[3] ==
                   (root / "oot3d_native_game.json").generic_string(),
               "launch profile did not expand its directory");

        const auto recursivePath = root / "recursive.launch.json";
        WriteText(
            recursivePath,
            R"json({
  "format": "oot3d_native_game_launch_profile_v1",
  "arguments": ["--launch-profile", "other.json"]
})json");
        bool rejectedRecursiveProfile = false;
        try {
            (void)Oot3dNativeGame::LoadNativeGameLaunchProfile(recursivePath);
        } catch (const std::runtime_error&) {
            rejectedRecursiveProfile = true;
        }
        Expect(rejectedRecursiveProfile,
               "recursive launch profile was not rejected");

        std::filesystem::remove_all(root);
        std::cout << "oot3d native game launch profile tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << "oot3d native game launch profile tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
