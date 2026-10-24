#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace Fast::Extension {

// Below this many items a grass placement step runs on the calling thread.
inline constexpr uint64_t kGrassParallelWorkItems = 65536U;

[[nodiscard]] inline size_t GrassParallelWorkerCount(uint64_t items) {
    if (items < kGrassParallelWorkItems) return 1U;
    const unsigned hardware = std::thread::hardware_concurrency();
    return std::clamp<size_t>(hardware == 0U ? 1U : hardware, 1U, 16U);
}

// Runs function(worker) for every worker in [0, count): the calling thread is
// worker 0. A worker that cannot get a thread runs on the calling thread. The
// first exception is rethrown after every worker has finished.
template <typename Function>
void RunGrassParallelWork(size_t count, Function&& function) {
    if (count <= 1U) {
        if (count == 1U) function(size_t{0});
        return;
    }
    std::exception_ptr failure;
    std::mutex failureMutex;
    const auto run = [&](size_t worker) {
        try {
            function(worker);
        } catch (...) {
            std::scoped_lock lock(failureMutex);
            if (!failure) failure = std::current_exception();
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(count - 1U);
    for (size_t worker = 1U; worker < count; ++worker) {
        try {
            threads.emplace_back(run, worker);
        } catch (...) {
            run(worker);
        }
    }
    run(0U);
    for (auto& thread : threads) thread.join();
    if (failure) std::rethrow_exception(failure);
}

} // namespace Fast::Extension
