#include "oot3d_source_overlay_loader.h"

#include <algorithm>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Oot3dNativeGame {
namespace {

void* OpenLibrary(const std::filesystem::path& path, std::string* error) {
#if defined(_WIN32)
    HMODULE library = LoadLibraryW(path.c_str());
    if (library == nullptr && error != nullptr) {
        *error = "LoadLibraryW failed with error " +
                 std::to_string(GetLastError());
    }
    return library;
#else
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr && error != nullptr) {
        const char* detail = dlerror();
        *error = detail == nullptr ? "dlopen failed" : detail;
    }
    return library;
#endif
}

void CloseLibrary(void* library) noexcept {
    if (library == nullptr) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

void* FindSymbol(void* library, const char* name) noexcept {
#if defined(_WIN32)
    return reinterpret_cast<void*>(
        GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

} // namespace

Oot3dSourceOverlayModule::~Oot3dSourceOverlayModule() {
    Unload();
}

bool Oot3dSourceOverlayModule::Load(
    const std::filesystem::path& path,
    const Oot3dSourceOverlayHostApi& hostApi,
    std::string* error) {
    Unload();
    if (hostApi.StructSize < sizeof(Oot3dSourceOverlayHostApi) ||
        hostApi.AbiVersion != OOT3D_SOURCE_OVERLAY_ABI_VERSION) {
        SetError(error, "source-overlay host ABI is invalid");
        return false;
    }

    std::string platformError;
    void* library = OpenLibrary(path, &platformError);
    if (library == nullptr) {
        SetError(error, "cannot load source overlay '" + path.string() +
                            "': " + platformError);
        return false;
    }
    auto* query = reinterpret_cast<Oot3dSourceOverlayQueryFn>(
        FindSymbol(library, OOT3D_SOURCE_OVERLAY_QUERY_SYMBOL));
    if (query == nullptr) {
        CloseLibrary(library);
        SetError(error, "source overlay does not export "
                        OOT3D_SOURCE_OVERLAY_QUERY_SYMBOL);
        return false;
    }

    const Oot3dSourceOverlayPluginApi* api =
        query(OOT3D_SOURCE_OVERLAY_ABI_VERSION, &hostApi);
    if (api == nullptr ||
        api->StructSize < sizeof(Oot3dSourceOverlayPluginApi) ||
        api->AbiVersion != OOT3D_SOURCE_OVERLAY_ABI_VERSION ||
        api->Execute == nullptr ||
        (api->EntryCount != 0U && api->Entries == nullptr)) {
        CloseLibrary(library);
        SetError(error, "source overlay returned an incompatible plugin ABI");
        return false;
    }

    std::vector<uint32_t> entries;
    entries.reserve(api->EntryCount);
    for (size_t index = 0; index < api->EntryCount; ++index) {
        const uint32_t address = api->Entries[index].Address;
        if (address == 0U || (address & 3U) != 0U) {
            if (api->Shutdown != nullptr) {
                api->Shutdown(api->Context);
            }
            CloseLibrary(library);
            SetError(error, "source overlay contains an invalid entry point");
            return false;
        }
        entries.push_back(address);
    }
    std::sort(entries.begin(), entries.end());
    if (std::adjacent_find(entries.begin(), entries.end()) != entries.end()) {
        if (api->Shutdown != nullptr) {
            api->Shutdown(api->Context);
        }
        CloseLibrary(library);
        SetError(error, "source overlay contains duplicate entry points");
        return false;
    }

    mLibrary = library;
    mApi = api;
    mBuildId = api->BuildId == nullptr ? "unidentified" : api->BuildId;
    mEntryPoints = std::move(entries);
    return true;
}

void Oot3dSourceOverlayModule::Unload() noexcept {
    if (mApi != nullptr && mApi->Shutdown != nullptr) {
        mApi->Shutdown(mApi->Context);
    }
    mApi = nullptr;
    mEntryPoints.clear();
    mBuildId.clear();
    CloseLibrary(mLibrary);
    mLibrary = nullptr;
}

bool Oot3dSourceOverlayModule::IsLoaded() const noexcept {
    return mApi != nullptr;
}

std::string_view Oot3dSourceOverlayModule::BuildId() const noexcept {
    return mBuildId;
}

std::span<const uint32_t>
Oot3dSourceOverlayModule::EntryPoints() const noexcept {
    return mEntryPoints;
}

bool Oot3dSourceOverlayModule::Contains(uint32_t entry) const noexcept {
    return std::binary_search(mEntryPoints.begin(), mEntryPoints.end(), entry);
}

int Oot3dSourceOverlayModule::Execute(
    uint32_t entry, Oot3dSourceOverlayGuestState* state,
    Oot3dSourceOverlayExecutionResult* result,
    uint32_t blockBudget) const {
    if (mApi == nullptr || state == nullptr || result == nullptr ||
        !Contains(entry)) {
        return 0;
    }
    return mApi->Execute(mApi->Context, entry, state, result, blockBudget);
}

void Oot3dSourceOverlayModule::SetError(
    std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

} // namespace Oot3dNativeGame

