#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Oot3dNativeGame::CpuPhaseProbe {

#if defined(TRIAEVUM_ENABLE_CPU_PHASE_PROBE) && defined(_WIN32)
inline constexpr bool BuildEnabled = true;
#else
inline constexpr bool BuildEnabled = false;
#endif

enum class Phase : size_t {
    Host, GuestScheduler, Aot, Svc, PicaFrontend, PicaQueue, PicaPlan,
    VertexCapture, TextureCapture, QueueLock, Dsp, AudioOutput,
    RendererStart, VisualPresentation, Present, Pacing, Count
};
inline constexpr std::array<const char*, static_cast<size_t>(Phase::Count)> Names{
    "host_other", "guest_scheduler", "aot", "svc_other", "pica_frontend",
    "pica_queue", "pica_plan", "vertex_capture", "texture_capture", "queue_lock",
    "dsp", "audio_output", "renderer_start_excluded", "visual_presentation_excluded",
    "present_excluded", "pacing_excluded"};

struct Stamp {
    double Wall = 0, User = 0, Kernel = 0;
    uint64_t Cycles = 0;
    bool Valid = false;
};
struct Total {
    double Wall = 0, User = 0, Kernel = 0;
    uint64_t Cycles = 0, Intervals = 0;
};
using Totals = std::array<Total, static_cast<size_t>(Phase::Count)>;

inline Stamp Read() {
    Stamp result;
#if defined(_WIN32)
    FILETIME creation{}, exit{}, kernel{}, user{};
    ULONG64 cycles = 0;
    result.Valid = GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)
        && QueryThreadCycleTime(GetCurrentThread(), &cycles);
    const auto seconds = [](FILETIME value) {
        return static_cast<double>((uint64_t{value.dwHighDateTime} << 32) |
                                   value.dwLowDateTime) / 1.0e7;
    };
    result.User = seconds(user);
    result.Kernel = seconds(kernel);
    result.Cycles = cycles;
#endif
    result.Wall = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return result;
}

// Exclusive accounting: nested work replaces its parent's phase, never adds to it.
// Thread-time charges can spill across scopes. Only the telescoping full-window
// CPU total is valid; use cycles (not seconds) for the phase breakdown.
class Ledger {
public:
    bool Active = false;
    Phase Current = Phase::Host;
    Totals Values{};
    uint64_t ReadFailures = 0;
    void StartAt(Stamp stamp) {
        Values = {};
        ReadFailures = stamp.Valid ? 0 : 1;
        Current = Phase::Host;
        Last = stamp;
        Active = stamp.Valid;
    }
    void ChangeAt(Phase next, Stamp stamp) {
        if (!Active) return;
        if (!stamp.Valid) {
            ++ReadFailures;
            Active = false;
            return;
        }
        auto& total = Values[static_cast<size_t>(Current)];
        total.Wall += stamp.Wall - Last.Wall;
        total.User += stamp.User - Last.User;
        total.Kernel += stamp.Kernel - Last.Kernel;
        total.Cycles += stamp.Cycles - Last.Cycles;
        ++total.Intervals;
        Last = stamp;
        Current = next;
    }
    void Change(Phase next) { if (Active) ChangeAt(next, Read()); }
    Totals Snapshot() { Change(Current); return Values; }
private:
    Stamp Last{};
};

inline thread_local Ledger Thread;

#if defined(TRIAEVUM_ENABLE_CPU_PHASE_PROBE) && defined(_WIN32)
class Scope {
public:
    explicit Scope(Phase phase) : Previous(Thread.Current), Armed(Thread.Active) {
        if (Armed) Thread.Change(phase);
    }
    ~Scope() { Stop(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void Stop() {
        if (Armed) { Thread.Change(Previous); Armed = false; }
    }
private:
    Phase Previous;
    bool Armed;
};
#else
class Scope {
public:
    explicit Scope(Phase) {}
    void Stop() {}
};
#endif
} // namespace Oot3dNativeGame::CpuPhaseProbe
