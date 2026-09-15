#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace Oot3dNativeGame {

// Optional OS scheduling support for the calling presentation thread. No
// process-wide priority, registry or timer-resolution changes. Thread-affine.
class NativePresentationThreadScope final {
public:
    NativePresentationThreadScope() = default;
    NativePresentationThreadScope(const NativePresentationThreadScope&) = delete;
    NativePresentationThreadScope& operator=(const NativePresentationThreadScope&) = delete;
    ~NativePresentationThreadScope() {
        Configure(false);
#ifdef _WIN32
        if (mModule) FreeLibrary(mModule);
#endif
    }
    void Configure(bool enabled) noexcept {
#ifdef _WIN32
        if (!enabled) {
            if (mTask) {
                mRevert(mTask);
                mTask = nullptr;
            }
            return;
        }
        if (mTask) return;
        if (!mAttempted) {
            mAttempted = true;
            mModule = LoadLibraryExW(L"avrt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (mModule) {
                mAttach = reinterpret_cast<Attach>(GetProcAddress(mModule, "AvSetMmThreadCharacteristicsW"));
                mRevert = reinterpret_cast<Revert>(GetProcAddress(mModule, "AvRevertMmThreadCharacteristics"));
            }
        }
        if (!mAttach || !mRevert) return;
        DWORD taskIndex = 0;
        mTask = mAttach(L"Games", &taskIndex);
#else
        (void)enabled;
#endif
    }
    bool Active() const noexcept {
#ifdef _WIN32
        return mTask != nullptr;
#else
        return false;
#endif
    }
private:
#ifdef _WIN32
    using Attach = HANDLE (WINAPI*)(LPCWSTR, LPDWORD);
    using Revert = BOOL (WINAPI*)(HANDLE);
    HMODULE mModule = nullptr;
    HANDLE mTask = nullptr;
    Revert mRevert = nullptr;
    Attach mAttach = nullptr;
    bool mAttempted = false;
#endif
};

} // namespace Oot3dNativeGame
