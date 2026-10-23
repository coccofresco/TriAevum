#include "oot3d_native_performance_hint.h"

#if defined(__ANDROID__)
#include <cstdlib>
#include <string_view>
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace Oot3dNativeGame {

#if defined(__ANDROID__)
namespace {

// Resolved at run time: the app supports API 29, the NDK entry points exist
// from API 33.
struct PerformanceHintApi {
    using GetManager = void* (*)();
    using CreateSession = void* (*)(void*, const int32_t*, size_t, int64_t);
    using UpdateTarget = int (*)(void*, int64_t);
    using ReportActual = int (*)(void*, int64_t);
    using CloseSession = void (*)(void*);

    GetManager getManager = nullptr;
    CreateSession createSession = nullptr;
    UpdateTarget updateTarget = nullptr;
    ReportActual reportActual = nullptr;
    CloseSession closeSession = nullptr;
    void* manager = nullptr;

    PerformanceHintApi() {
        void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) return;
        getManager = reinterpret_cast<GetManager>(dlsym(library, "APerformanceHint_getManager"));
        createSession = reinterpret_cast<CreateSession>(
            dlsym(library, "APerformanceHint_createSession"));
        updateTarget = reinterpret_cast<UpdateTarget>(
            dlsym(library, "APerformanceHint_updateTargetWorkDuration"));
        reportActual = reinterpret_cast<ReportActual>(
            dlsym(library, "APerformanceHint_reportActualWorkDuration"));
        closeSession = reinterpret_cast<CloseSession>(
            dlsym(library, "APerformanceHint_closeSession"));
        if (getManager && createSession && updateTarget && reportActual && closeSession) {
            manager = getManager();
        }
    }

    [[nodiscard]] bool Usable() const noexcept { return manager != nullptr; }
};

const PerformanceHintApi& Api() {
    static const PerformanceHintApi api;
    return api;
}

// TRIAEVUM_PERFORMANCE_HINT=0 disables the sessions for A/B comparisons.
bool AllowedByEnvironment() {
    static const bool allowed = [] {
        const char* value = std::getenv("TRIAEVUM_PERFORMANCE_HINT");
        return value == nullptr || std::string_view(value) != "0";
    }();
    return allowed;
}

} // namespace

NativePerformanceHintSession::~NativePerformanceHintSession() { Close(); }

bool NativePerformanceHintSession::Available() noexcept {
    return AllowedByEnvironment() && Api().Usable();
}

int32_t NativePerformanceHintSession::CurrentThreadId() noexcept {
    return static_cast<int32_t>(gettid());
}

bool NativePerformanceHintSession::Open(const std::vector<int32_t>& threadIds,
                                        std::chrono::nanoseconds target) {
    Close();
    const auto& api = Api();
    if (!Available() || threadIds.empty() || target.count() <= 0) return false;
    mSession = api.createSession(api.manager, threadIds.data(), threadIds.size(),
                                 target.count());
    mTarget = mSession != nullptr ? target.count() : 0;
    return mSession != nullptr;
}

void NativePerformanceHintSession::UpdateTarget(std::chrono::nanoseconds target) {
    if (mSession == nullptr || target.count() <= 0 || target.count() == mTarget) return;
    if (Api().updateTarget(mSession, target.count()) == 0) mTarget = target.count();
}

void NativePerformanceHintSession::ReportActual(std::chrono::nanoseconds actual) {
    if (mSession == nullptr || actual.count() <= 0) return;
    if (Api().reportActual(mSession, actual.count()) == 0) {
        ++mReports;
        if (actual.count() > mTarget) ++mReportsOverTarget;
    }
}

void NativePerformanceHintSession::Close() {
    if (mSession != nullptr) Api().closeSession(mSession);
    mSession = nullptr;
    mTarget = 0;
}

#else

NativePerformanceHintSession::~NativePerformanceHintSession() = default;
bool NativePerformanceHintSession::Available() noexcept { return false; }
int32_t NativePerformanceHintSession::CurrentThreadId() noexcept { return 0; }
bool NativePerformanceHintSession::Open(const std::vector<int32_t>&, std::chrono::nanoseconds) {
    return false;
}
void NativePerformanceHintSession::UpdateTarget(std::chrono::nanoseconds) {}
void NativePerformanceHintSession::ReportActual(std::chrono::nanoseconds) {}
void NativePerformanceHintSession::Close() {}

#endif

} // namespace Oot3dNativeGame
