#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace Oot3dNativeGame {

// Android Dynamic Performance Framework session (APerformanceHint, API 33+):
// the threads report how long each presentation's work took against the
// presentation period, so the CPU governor raises clocks before frames are
// late instead of lowering them during the pacing waits. Elsewhere, or on
// older Android releases, every call is a no-op and Active() stays false.
class NativePerformanceHintSession final {
  public:
    NativePerformanceHintSession() = default;
    ~NativePerformanceHintSession();
    NativePerformanceHintSession(const NativePerformanceHintSession&) = delete;
    NativePerformanceHintSession& operator=(const NativePerformanceHintSession&) = delete;

    // Opens (or retargets) the session for the calling thread set.
    bool Open(const std::vector<int32_t>& threadIds, std::chrono::nanoseconds target);
    void UpdateTarget(std::chrono::nanoseconds target);
    void ReportActual(std::chrono::nanoseconds actual);
    void Close();

    [[nodiscard]] bool Active() const noexcept { return mSession != nullptr; }
    [[nodiscard]] uint64_t Reports() const noexcept { return mReports; }
    [[nodiscard]] uint64_t ReportsOverTarget() const noexcept { return mReportsOverTarget; }
    [[nodiscard]] int64_t TargetNanoseconds() const noexcept { return mTarget; }

    [[nodiscard]] static bool Available() noexcept;
    [[nodiscard]] static int32_t CurrentThreadId() noexcept;

  private:
    void* mSession = nullptr;
    int64_t mTarget = 0;
    uint64_t mReports = 0;
    uint64_t mReportsOverTarget = 0;
};

} // namespace Oot3dNativeGame
