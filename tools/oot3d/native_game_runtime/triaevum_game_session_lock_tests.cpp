#include "triaevum_game_session_lock.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void Expect(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

// --hold PATH: takes the lock, prints "held" and keeps it until stdin
// closes, so another process (Forge, Python) can be checked against it.
int Hold(const std::filesystem::path &path) {
    Oot3dNativeGame::GameSessionLock lock;
    std::string error;
    const auto result = lock.Acquire(path, &error);
    if (result != Oot3dNativeGame::GameSessionLock::Result::Held) {
        const bool busy = result == Oot3dNativeGame::GameSessionLock::Result::Busy;
        std::cout << (busy ? std::string("busy") : "unavailable: " + error) << std::endl;
        return 1;
    }
    std::cout << "held" << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--hold") return Hold(argv[2]);
    using Oot3dNativeGame::GameSessionLock;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("triaevum_session_lock_" + std::to_string(nonce));
    try {
        // A Linux/Android-style activation root: the profile beside the state.
        const auto activation = root / "activation";
        std::filesystem::create_directories(activation);
        const auto profile = activation / "TriAevum.launch.json";
        std::ofstream(profile) << "{}";
#if defined(_WIN32)
        const auto state = root / "package" / "data";
        const auto executableDirectory = root / "package";
        std::filesystem::create_directories(state);
        const auto windowsProfile = state / "TriAevum.launch.json";
#else
        const auto state = activation;
        const auto executableDirectory = root / "elsewhere";
        const auto windowsProfile = profile;
#endif
        Expect(Oot3dNativeGame::GameSessionLockPath(executableDirectory, {}).empty(),
               "a launch without a profile takes no lock");
        Expect(Oot3dNativeGame::GameSessionLockPath(executableDirectory, windowsProfile).empty(),
               "an installation Forge never activated takes no lock");
        std::filesystem::create_directories(state / ".forge-activation");
        const auto path = Oot3dNativeGame::GameSessionLockPath(executableDirectory, windowsProfile);
        Expect(path == state / ".forge-activation" / "game-session.lock", "the lock lives beside the journal");
        Expect(!std::filesystem::exists(path), "resolving the path creates nothing");

        GameSessionLock first;
        std::string error;
        Expect(first.Acquire(path, &error) == GameSessionLock::Result::Held, "the first game holds the lock");
        Expect(first.Held(), "the lock reports itself held");
        GameSessionLock second;
        Expect(second.Acquire(path, &error) == GameSessionLock::Result::Busy, "a second holder is refused");
        Expect(!second.Held(), "a refused lock is not held");
        first.Release();
        Expect(second.Acquire(path, &error) == GameSessionLock::Result::Held, "a released lock can be taken again");
        second.Release();
        Expect(std::filesystem::is_regular_file(path), "the lock file stays");

        GameSessionLock missing;
        Expect(missing.Acquire(root / "absent" / "game-session.lock", &error) == GameSessionLock::Result::Unavailable,
               "a lock that cannot be opened is unavailable");
        Expect(!error.empty(), "an unavailable lock says why");
    } catch (const std::exception &failure) {
        std::cerr << "triaevum_game_session_lock_tests: " << failure.what() << '\n';
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::cout << "triaevum_game_session_lock_tests: ok\n";
    return 0;
}
