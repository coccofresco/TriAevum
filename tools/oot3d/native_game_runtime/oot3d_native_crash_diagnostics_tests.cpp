#include "oot3d_native_crash_diagnostics.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>

namespace {

bool IsMinidump(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::array<char, 4> signature{};
    stream.read(signature.data(), static_cast<std::streamsize>(signature.size()));
    std::error_code ignored;
    return stream &&
           signature == std::array<char, 4>{'M', 'D', 'M', 'P'} &&
           std::filesystem::file_size(path, ignored) > signature.size();
}

int RunCrashChild(const std::filesystem::path& directory) {
    std::string error;
    if (!Oot3dNativeGame::InstallWholeAotCrashDiagnostics(directory, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    RaiseException(0xE0424242U, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    return 3;
}

bool TestUnhandledCrashFilter(const std::filesystem::path& root) {
    const auto directory = root / "filter";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);

    std::array<wchar_t, 32768> executable{};
    const DWORD executableLength = GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (executableLength == 0 || executableLength >= executable.size()) {
        return false;
    }
    std::wstring command = L"\"" + std::wstring(executable.data()) +
                           L"\" --crash-child \"" + directory.wstring() +
                           L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.data(), mutableCommand.data(), nullptr,
                        nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process)) {
        return false;
    }
    const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 4);
        WaitForSingleObject(process.hProcess, 5000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (wait != WAIT_OBJECT_0) {
        return false;
    }

    std::vector<std::filesystem::path> dumps;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".dmp") {
            dumps.push_back(entry.path());
        }
    }
    const bool valid = dumps.size() == 1 && IsMinidump(dumps.front());
    std::filesystem::remove_all(directory, ignored);
    return valid;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--crash-child") {
        return RunCrashChild(argv[2]);
    }
    const auto root = std::filesystem::temp_directory_path() /
                      "oot3d_whole_aot_diagnostic_test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const auto directDump = root / "direct.dmp";
    std::string error;
    if (!Oot3dNativeGame::WriteWholeAotDiagnosticMinidump(directDump, &error) ||
        !IsMinidump(directDump)) {
        std::cerr << (error.empty() ? "invalid direct minidump" : error) << '\n';
        return 1;
    }
    if (!TestUnhandledCrashFilter(root)) {
        std::cerr << "unhandled-exception minidump test failed\n";
        return 1;
    }
    std::filesystem::remove_all(root, ignored);
    return 0;
}
#else
int main() {
    return 0;
}
#endif
