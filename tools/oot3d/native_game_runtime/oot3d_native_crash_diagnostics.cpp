#include "oot3d_native_crash_diagnostics.h"

#include <system_error>

#ifdef _WIN32
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

#include <windows.h>
#include <DbgHelp.h>
#endif

namespace Oot3dNativeGame {
namespace {

#ifdef _WIN32
constexpr size_t kMaximumCrashPath = 32768;
std::array<wchar_t, kMaximumCrashPath> gCrashDirectory{};
LPTOP_LEVEL_EXCEPTION_FILTER gPreviousExceptionFilter = nullptr;
std::atomic_flag gWritingCrashDump = ATOMIC_FLAG_INIT;

std::string WindowsErrorMessage(DWORD code) {
    char* message = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<char*>(&message), 0, nullptr);
    std::string result = length == 0 || message == nullptr
                             ? "Windows error " + std::to_string(code)
                             : std::string(message, length);
    if (message != nullptr) {
        LocalFree(message);
    }
    while (!result.empty() &&
           (result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }
    return result;
}

bool WriteMinidump(const wchar_t* path, EXCEPTION_POINTERS* exception,
                   DWORD* errorCode) noexcept {
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (errorCode != nullptr) {
            *errorCode = GetLastError();
        }
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInformation{};
    MINIDUMP_EXCEPTION_INFORMATION* exceptionInformationPointer = nullptr;
    if (exception != nullptr) {
        exceptionInformation.ThreadId = GetCurrentThreadId();
        exceptionInformation.ExceptionPointers = exception;
        exceptionInformation.ClientPointers = FALSE;
        exceptionInformationPointer = &exceptionInformation;
    }
    constexpr MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithDataSegs | MiniDumpWithHandleData |
        MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules);
    const BOOL written = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file, dumpType,
        exceptionInformationPointer, nullptr, nullptr);
    const DWORD writeError = written ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!written) {
        DeleteFileW(path);
    }
    if (errorCode != nullptr) {
        *errorCode = writeError;
    }
    return written == TRUE;
}

LONG WINAPI WholeAotExceptionFilter(EXCEPTION_POINTERS* exception) noexcept {
    if (!gWritingCrashDump.test_and_set(std::memory_order_acq_rel)) {
        SYSTEMTIME time{};
        GetSystemTime(&time);
        std::array<wchar_t, kMaximumCrashPath> path{};
        const int length = swprintf_s(
            path.data(), path.size(),
            L"%ls\\oot3d_crash_%04u%02u%02u_%02u%02u%02u_%lu_%lu.dmp",
            gCrashDirectory.data(), time.wYear, time.wMonth, time.wDay,
            time.wHour, time.wMinute, time.wSecond, GetCurrentProcessId(),
            GetCurrentThreadId());
        if (length > 0 && static_cast<size_t>(length) < path.size()) {
            WriteMinidump(path.data(), exception, nullptr);
        }
    }
    if (gPreviousExceptionFilter != nullptr &&
        gPreviousExceptionFilter != WholeAotExceptionFilter) {
        return gPreviousExceptionFilter(exception);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

bool InstallWholeAotCrashDiagnostics(const std::filesystem::path& directory,
                                     std::string* error) {
#ifdef _WIN32
    std::error_code filesystemError;
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) {
        if (error != nullptr) {
            *error = "cannot create crash directory: " +
                     filesystemError.message();
        }
        return false;
    }
    const std::wstring absolute = std::filesystem::absolute(
        directory, filesystemError).lexically_normal().wstring();
    if (filesystemError || absolute.empty() ||
        absolute.size() >= gCrashDirectory.size()) {
        if (error != nullptr) {
            *error = "invalid crash directory";
        }
        return false;
    }
    std::copy(absolute.begin(), absolute.end(), gCrashDirectory.begin());
    gCrashDirectory[absolute.size()] = L'\0';
    const auto previous = SetUnhandledExceptionFilter(WholeAotExceptionFilter);
    if (previous != WholeAotExceptionFilter) {
        gPreviousExceptionFilter = previous;
    }
    return true;
#else
    (void)directory;
    if (error != nullptr) {
        *error = "whole-AOT minidumps are only available on Windows";
    }
    return false;
#endif
}

bool WriteWholeAotDiagnosticMinidump(const std::filesystem::path& path,
                                     std::string* error) {
#ifdef _WIN32
    std::error_code filesystemError;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), filesystemError);
    }
    if (filesystemError) {
        if (error != nullptr) {
            *error = "cannot create minidump directory: " +
                     filesystemError.message();
        }
        return false;
    }
    DWORD errorCode = ERROR_SUCCESS;
    if (!WriteMinidump(path.c_str(), nullptr, &errorCode)) {
        if (error != nullptr) {
            *error = "MiniDumpWriteDump failed: " +
                     WindowsErrorMessage(errorCode);
        }
        return false;
    }
    return true;
#else
    (void)path;
    if (error != nullptr) {
        *error = "whole-AOT minidumps are only available on Windows";
    }
    return false;
#endif
}

} // namespace Oot3dNativeGame
