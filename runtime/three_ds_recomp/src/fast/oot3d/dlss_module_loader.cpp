#include "fast/oot3d/dlss_module_loader.h"

#include <filesystem>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Fast::Oot3d {
namespace {
struct LoadedModule {
    const TriaevumDlssModule* Module = nullptr;
    std::string Reason;
};

std::filesystem::path ExecutableDirectory() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size())
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
#else
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : executable.parent_path();
#endif
}

LoadedModule Load() {
    LoadedModule loaded;
    const auto directory = ExecutableDirectory();
    if (directory.empty()) {
        loaded.Reason = "the runtime directory is unknown";
        return loaded;
    }
    std::vector<std::filesystem::path> candidates{ directory / TRIAEVUM_DLSS_LIBRARY };
#if !defined(_WIN32)
    candidates.push_back(directory / "lib" / TRIAEVUM_DLSS_LIBRARY);
#endif
    for (const auto& path : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
            continue;
#if defined(_WIN32)
        HMODULE library = LoadLibraryExW(path.c_str(), nullptr,
                                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        const auto entry = library ? reinterpret_cast<TriaevumDlssEntryPoint>(
                                         GetProcAddress(library, TRIAEVUM_DLSS_ENTRY_POINT))
                                   : nullptr;
#else
        void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        const auto entry = library ? reinterpret_cast<TriaevumDlssEntryPoint>(
                                         dlsym(library, TRIAEVUM_DLSS_ENTRY_POINT))
                                   : nullptr;
#endif
        const TriaevumDlssModule* module = entry ? entry(TRIAEVUM_DLSS_ABI_VERSION) : nullptr;
        if (module != nullptr && module->AbiVersion == TRIAEVUM_DLSS_ABI_VERSION) {
            // Kept loaded: NGX and its features live for the process lifetime.
            loaded.Module = module;
            return loaded;
        }
        loaded.Reason = library ? "the DLSS module does not implement this interface version"
                                : "the DLSS module could not be loaded";
#if defined(_WIN32)
        if (library)
            FreeLibrary(library);
#else
        if (library)
            dlclose(library);
#endif
        return loaded;
    }
    loaded.Reason = "the DLSS module (" TRIAEVUM_DLSS_LIBRARY ") is not installed";
    return loaded;
}

const LoadedModule& Loaded() {
    static const LoadedModule loaded = Load();
    return loaded;
}
} // namespace

const TriaevumDlssModule* DlssModule() {
    return Loaded().Module;
}

const std::string& DlssModuleUnavailableReason() {
    return Loaded().Reason;
}

} // namespace Fast::Oot3d
