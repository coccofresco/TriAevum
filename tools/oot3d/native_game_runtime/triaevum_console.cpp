#include "triaevum_console.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace TriAevum {

bool TakeConsoleOption(int& argc, char** argv) {
    if (argv == nullptr) return false;
    bool found = false;
    int kept = argc > 0 ? 1 : 0;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr && std::string_view(argv[index]) == "--console") {
            found = true;
            continue;
        }
        argv[kept++] = argv[index];
    }
    if (found) {
        argc = kept;
        argv[argc] = nullptr;
    }
    return found;
}

#ifdef _WIN32
namespace {

// A handle the process inherited (a file, pipe or console).
bool Inherited(DWORD which) {
    const HANDLE handle = GetStdHandle(which);
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

} // namespace

void AttachDeveloperConsole(int& argc, char** argv) {
    const bool option = TakeConsoleOption(argc, argv);
    const char* environment = std::getenv("TRIAEVUM_CONSOLE");
    const bool requested = option || (environment != nullptr && *environment != '\0' &&
                                      std::string_view(environment) != "0");
    if (!requested) return;
    const bool output = Inherited(STD_OUTPUT_HANDLE);
    const bool error = Inherited(STD_ERROR_HANDLE);
    const bool input = Inherited(STD_INPUT_HANDLE);
    if (output && error) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole()) return;
    SetConsoleOutputCP(CP_UTF8);
    FILE* stream = nullptr;
    if (!output && freopen_s(&stream, "CONOUT$", "w", stdout) == 0) std::cout.clear();
    if (!error && freopen_s(&stream, "CONOUT$", "w", stderr) == 0) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        std::cerr.clear();
    }
    if (!input && freopen_s(&stream, "CONIN$", "r", stdin) == 0) std::cin.clear();
}
#else
void AttachDeveloperConsole(int& argc, char** argv) {
    (void)TakeConsoleOption(argc, argv);
}
#endif

} // namespace TriAevum
