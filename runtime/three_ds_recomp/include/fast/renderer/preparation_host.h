#pragma once

// Host-load policy for background GPU preparation jobs (Forge's pipeline
// helper, shader compilers): how many workers to run, when host memory is too
// short for another concurrent driver compilation, and how to stay out of the
// desktop's way. Title-neutral; decisions are pure functions so the contract
// tests cover them without a device.

#include <cstdint>
#include <string>

namespace Fast::Renderer {

struct PreparationWorkerPolicy {
    // std::thread::hardware_concurrency(); 0 when unknown.
    unsigned HardwareThreads = 0;
    // The driver already compiles each pipeline on its own worker pool (AMD's
    // proprietary driver): fewer application threads saturate the machine.
    bool DriverCompilesConcurrently = false;
    // TRIAEVUM_PIPELINE_PREPARE_THREADS, when set: explicit 1..16.
    const char* Environment = nullptr;
};

inline constexpr unsigned kPreparationWorkerLimit = 16U;
inline constexpr unsigned kPreparationWorkerDefaultLimit = 8U;
inline constexpr unsigned kPreparationWorkerConcurrentDriverLimit = 4U;

// Default: half the logical cores (driver compilation scales sub-linearly and
// each worker keeps a compiler working set), at most 8, at most 4 when the
// driver compiles concurrently itself. Throws std::invalid_argument when the
// environment override is not an integer in 1..16.
unsigned PreparationWorkerCount(const PreparationWorkerPolicy& policy);

struct HostMemory {
    bool Known = false;
    uint64_t TotalBytes = 0;
    uint64_t AvailableBytes = 0;
};

// Physical memory the host can still hand out without paging.
HostMemory QueryHostMemory();

// Memory a preparation job keeps free for the rest of the machine: at least
// 1 GiB, or an eighth of the installed memory.
uint64_t PreparationMemoryReserveBytes(uint64_t totalBytes);

// True when a job with `runningWorkers` concurrent workers should retire one
// before starting another compilation. The last worker never retires, so the
// job still completes (sequentially) on a machine that is short of memory.
bool ShouldRetirePreparationWorker(const HostMemory& memory, unsigned runningWorkers);

// Peak resident size of this process so far; 0 when unavailable.
uint64_t PeakWorkingSetBytes();

// Below-normal process priority (nice on POSIX). Returns whether it applied.
bool LowerPreparationProcessPriority();

// Below-normal priority for the calling thread.
void LowerPreparationThreadPriority();

}  // namespace Fast::Renderer
