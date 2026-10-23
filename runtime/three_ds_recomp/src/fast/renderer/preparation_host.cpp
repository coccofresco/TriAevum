#include "fast/renderer/preparation_host.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#if defined(__linux__)
#include <fstream>
#include <sys/sysinfo.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif
#endif

namespace Fast::Renderer {

unsigned PreparationWorkerCount(const PreparationWorkerPolicy& policy) {
    if (policy.Environment != nullptr && policy.Environment[0] != '\0') {
        const std::string_view text(policy.Environment);
        unsigned requested = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), requested);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || requested < 1U ||
            requested > kPreparationWorkerLimit)
            throw std::invalid_argument("TRIAEVUM_PIPELINE_PREPARE_THREADS must be 1..16");
        return requested;
    }
    const unsigned hardware = std::max(1U, policy.HardwareThreads);
    const unsigned limit = policy.DriverCompilesConcurrently ? kPreparationWorkerConcurrentDriverLimit
                                                             : kPreparationWorkerDefaultLimit;
    return std::clamp((hardware + 1U) / 2U, 1U, limit);
}

HostMemory QueryHostMemory() {
    HostMemory memory;
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) != 0) {
        memory.Known = true;
        memory.TotalBytes = status.ullTotalPhys;
        memory.AvailableBytes = status.ullAvailPhys;
    }
#elif defined(__linux__)
    // MemAvailable counts reclaimable cache; sysinfo's freeram alone would
    // report a healthy machine as exhausted.
    std::ifstream meminfo("/proc/meminfo");
    std::string key;
    uint64_t value = 0;
    std::string unit;
    bool total = false, available = false;
    while (meminfo >> key >> value >> unit) {
        if (key == "MemTotal:") { memory.TotalBytes = value * 1024ULL; total = true; }
        else if (key == "MemAvailable:") { memory.AvailableBytes = value * 1024ULL; available = true; }
        if (total && available) break;
    }
    memory.Known = total && available;
#elif defined(__APPLE__)
    uint64_t total = 0;
    size_t length = sizeof(total);
    int names[2] = {CTL_HW, HW_MEMSIZE};
    vm_statistics64_data_t statistics{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (sysctl(names, 2, &total, &length, nullptr, 0) == 0 &&
        host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count) ==
            KERN_SUCCESS) {
        const uint64_t page = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
        memory.Known = true;
        memory.TotalBytes = total;
        memory.AvailableBytes = (static_cast<uint64_t>(statistics.free_count) +
                                 static_cast<uint64_t>(statistics.inactive_count)) * page;
    }
#endif
    return memory;
}

uint64_t PreparationMemoryReserveBytes(uint64_t totalBytes) {
    constexpr uint64_t kFloor = 1024ULL * 1024ULL * 1024ULL;
    return std::max(kFloor, totalBytes / 8ULL);
}

bool ShouldRetirePreparationWorker(const HostMemory& memory, unsigned runningWorkers) {
    if (!memory.Known || runningWorkers <= 1U) return false;
    return memory.AvailableBytes < PreparationMemoryReserveBytes(memory.TotalBytes);
}

uint64_t PeakWorkingSetBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0)
        return counters.PeakWorkingSetSize;
    return 0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
#endif
}

bool LowerPreparationProcessPriority() {
#ifdef _WIN32
    return SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS) != 0;
#else
    return nice(5) != -1;
#endif
}

void LowerPreparationThreadPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

}  // namespace Fast::Renderer
