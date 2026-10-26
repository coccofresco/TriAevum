#pragma once

#include <filesystem>
#include <string>

namespace Oot3dNativeGame {

// The game session lock of the quiescence contract (UX plan WP-05A, Forge
// `forge_core::session`): while the game runs from a Forge installation it
// holds `<state>/.forge-activation/game-session.lock`, so Forge refuses to
// back up, restore, move or delete the installation's files, and a second
// game (or a Forge holding the lock for such an operation) is refused.
//
// It is an operating-system lock (LockFileEx on Windows, flock elsewhere),
// the same Forge's kernel locks use: process exit or a crash releases it,
// and the lock file is never deleted.
//
// Only launches from a launch profile of an installation Forge activated
// take it: the `.forge-activation` folder must already exist. Developer
// launches (explicit arguments, or a profile outside an installation) take
// no lock and are unaffected.

// `<state>/.forge-activation/game-session.lock`, where state is
// `executableDirectory/data` on Windows (the installed TriAevum.exe sits in
// the activation root, whose data/ holds the state) and the profile's folder
// elsewhere (Linux activation root, AppImage/Flatpak XDG data folder,
// Android external files folder). Empty when there is no profile or the
// `.forge-activation` folder does not exist.
[[nodiscard]] std::filesystem::path GameSessionLockPath(
    const std::filesystem::path &executableDirectory, const std::filesystem::path &profilePath);

class GameSessionLock {
public:
    enum class Result {
        // The lock is held until this object is destroyed.
        Held,
        // Another game or a Forge operation holds it.
        Busy,
        // The lock file could not be opened or locked; the game runs without it.
        Unavailable,
    };

    GameSessionLock() = default;
    GameSessionLock(const GameSessionLock &) = delete;
    GameSessionLock &operator=(const GameSessionLock &) = delete;
    ~GameSessionLock();

    Result Acquire(const std::filesystem::path &path, std::string *error);
    void Release() noexcept;
    [[nodiscard]] bool Held() const noexcept;

private:
#if defined(_WIN32)
    void *mHandle = nullptr;
#else
    int mDescriptor = -1;
#endif
};

} // namespace Oot3dNativeGame
