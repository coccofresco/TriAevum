#include "triaevum_game_session_lock.h"

#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Oot3dNativeGame {
namespace {

std::string Utf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

} // namespace

std::filesystem::path GameSessionLockPath(const std::filesystem::path &executableDirectory,
                                          const std::filesystem::path &profilePath) {
    if (profilePath.empty()) return {};
#if defined(_WIN32)
    const auto state = executableDirectory / "data";
#else
    (void)executableDirectory;
    const auto state = std::filesystem::absolute(profilePath).lexically_normal().parent_path();
#endif
    const auto activation = state / ".forge-activation";
    std::error_code error;
    if (!std::filesystem::is_directory(activation, error)) return {};
    return activation / "game-session.lock";
}

std::filesystem::path PendingUpdateJournal(const std::filesystem::path &executableDirectory,
                                           const std::filesystem::path &profilePath) {
    std::vector<std::filesystem::path> roots{executableDirectory, executableDirectory.parent_path()};
    if (!profilePath.empty()) {
        const auto folder = std::filesystem::absolute(profilePath).lexically_normal().parent_path();
#if defined(_WIN32)
        roots.push_back(folder.parent_path());
#else
        roots.push_back(folder);
#endif
    }
    for (const auto &root : roots) {
        if (root.empty()) continue;
        const auto journal = root / ".forge-update" / "journal.json";
        std::error_code error;
        if (std::filesystem::is_regular_file(journal, error)) return journal;
    }
    return {};
}

GameSessionLock::~GameSessionLock() {
    Release();
}

bool GameSessionLock::Held() const noexcept {
#if defined(_WIN32)
    return mHandle != nullptr;
#else
    return mDescriptor >= 0;
#endif
}

#if defined(_WIN32)

GameSessionLock::Result GameSessionLock::Acquire(const std::filesystem::path &path, std::string *error) {
    Release();
    // Shared access: Forge opens the same file to test and take the lock.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = "cannot open " + Utf8(path) + ": " + std::system_category().message(GetLastError());
        return Result::Unavailable;
    }
    // The whole file, as Rust's File::try_lock locks it.
    OVERLAPPED overlapped{};
    if (!LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD,
                    &overlapped)) {
        const DWORD code = GetLastError();
        CloseHandle(file);
        if (code == ERROR_LOCK_VIOLATION || code == ERROR_IO_PENDING) return Result::Busy;
        if (error) *error = "cannot lock " + Utf8(path) + ": " + std::system_category().message(code);
        return Result::Unavailable;
    }
    mHandle = file;
    return Result::Held;
}

void GameSessionLock::Release() noexcept {
    if (mHandle == nullptr) return;
    // Closing the handle releases the lock.
    CloseHandle(static_cast<HANDLE>(mHandle));
    mHandle = nullptr;
}

#else

GameSessionLock::Result GameSessionLock::Acquire(const std::filesystem::path &path, std::string *error) {
    Release();
    const int descriptor = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        if (error) *error = "cannot open " + Utf8(path) + ": " + std::strerror(errno);
        return Result::Unavailable;
    }
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        const int code = errno;
        close(descriptor);
        if (code == EWOULDBLOCK) return Result::Busy;
        if (error) *error = "cannot lock " + Utf8(path) + ": " + std::strerror(code);
        return Result::Unavailable;
    }
    mDescriptor = descriptor;
    return Result::Held;
}

void GameSessionLock::Release() noexcept {
    if (mDescriptor < 0) return;
    close(mDescriptor);
    mDescriptor = -1;
}

#endif

} // namespace Oot3dNativeGame
