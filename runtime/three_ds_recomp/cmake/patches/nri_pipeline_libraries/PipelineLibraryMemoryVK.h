#pragma once
#include <cstdint>
#include <cstdio>
#if defined(_WIN32)
#pragma push_macro("ERROR")
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma pop_macro("ERROR")
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace triaevum_nri {
// RSS, not Vulkan heap budget: compiler IR is host memory. Zero means unknown.
// The guard cannot interrupt one driver call and is not a hard process limit.
inline uint64_t PipelineLibraryResidentBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<uint64_t>(counters.WorkingSetSize);
#elif defined(__linux__)
    if (FILE* file = std::fopen("/proc/self/statm", "r")) {
        unsigned long long total = 0, resident = 0;
        const int fields = std::fscanf(file, "%llu %llu", &total, &resident);
        std::fclose(file);
        const long page = sysconf(_SC_PAGESIZE);
        if (fields == 2 && page > 0) return resident * static_cast<uint64_t>(page);
    }
#endif
    return 0;
}
} // namespace triaevum_nri
