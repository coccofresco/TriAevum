#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdio>

// Startup checkpoints: the named steps of the renderer's startup (Vulkan
// instance, surface, physical device and driver, logical device, swapchain,
// first presented frame), for a platform host that keeps a startup report
// (Android: the app's StartupDiagnostics, ports/android/runtime/android_host.cpp).
// A crash or hang during startup then shows the last step reached. Without a
// sink (desktop) a checkpoint is one atomic load. Stage text is technical
// only: never paths, user names or messages from the user's data.
namespace Fast {

using StartupCheckpointSink = void (*)(const char* stage) noexcept;

inline std::atomic<StartupCheckpointSink>& StartupCheckpointSinkSlot() noexcept {
    static std::atomic<StartupCheckpointSink> sink{nullptr};
    return sink;
}

inline void SetStartupCheckpointSink(StartupCheckpointSink sink) noexcept {
    StartupCheckpointSinkSlot().store(sink, std::memory_order_release);
}

inline void StartupCheckpoint(const char* stage) noexcept {
    if (const auto sink = StartupCheckpointSinkSlot().load(std::memory_order_acquire)) sink(stage);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline void StartupCheckpointf(const char* format, ...) noexcept {
    const auto sink = StartupCheckpointSinkSlot().load(std::memory_order_acquire);
    if (sink == nullptr) return;
    char text[192];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    sink(text);
}

} // namespace Fast
