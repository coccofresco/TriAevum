// Contract of the background preparation queue: owned work runs once, a
// draw that cannot wait for a worker runs a still-queued task itself, and a
// drain boundary leaves no task behind.
#include "fast/renderer3ds/pica_preparation_queue.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

using Fast::Renderer3ds::PicaPreparationQueue;
using Fast::Renderer3ds::PicaPreparationTask;

static void Check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

int main() try {
    {
        // Without workers the submitting thread runs the work at once.
        PicaPreparationQueue inline_queue(0);
        int runs = 0;
        auto task = inline_queue.Submit([&runs] { ++runs; });
        Check(task->Done() && runs == 1, "a queue without workers runs work at submission");
    }
    {
        // A blocked worker leaves later tasks queued: Complete runs one here.
        PicaPreparationQueue queue(1);
        std::atomic<bool> release{false};
        auto blocker = queue.Submit([&release] {
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
        while (queue.Pending() != 1U || blocker->Done()) std::this_thread::yield();
        const auto caller = std::this_thread::get_id();
        std::thread::id ranOn;
        auto queued = queue.Submit([&ranOn] { ranOn = std::this_thread::get_id(); });
        queue.Complete(*queued);
        Check(queued->Done() && ranOn == caller, "Complete runs a queued task on the calling thread");
        release = true;
        queue.Complete(*blocker);
        Check(blocker->Done(), "Complete waits for a running task");
    }
    {
        // Drain finishes everything; every task runs exactly once.
        PicaPreparationQueue queue(3);
        std::atomic<int> runs{0};
        std::vector<std::shared_ptr<PicaPreparationTask>> tasks;
        for (int index = 0; index < 200; ++index)
            tasks.push_back(queue.Submit([&runs] {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                ++runs;
            }));
        for (size_t index = 0; index < tasks.size(); index += 7) queue.Complete(*tasks[index]);
        queue.Drain();
        Check(runs == 200 && queue.Pending() == 0U, "drain runs every task once");
        for (const auto& task : tasks) Check(task->Done(), "drained tasks are done");
    }
    {
        // A throwing task is contained and still completes.
        PicaPreparationQueue queue(1);
        auto task = queue.Submit([] { throw std::runtime_error("expected"); });
        queue.Complete(*task);
        Check(task->Done(), "a failing task completes");
    }
    {
        // Startup parts: without an error exit every part is claimed once.
        std::atomic<size_t> next{0};
        size_t index = 0, claimed = 0;
        while (Fast::Renderer3ds::ClaimPicaStartupPart(next, 5, index)) Check(index == claimed++, "parts are claimed in order");
        Check(claimed == 5U && !Fast::Renderer3ds::PicaStartupPreparationAbandoned(), "a normal run claims every startup part");
    }
    {
        // An error exit (process-wide, so last) abandons the parts not started:
        // workers shaped like the renderer's stop within one part, not after
        // the whole list (Q-INTEG-R10-2: the fatal teardown joins them).
        constexpr size_t kParts = 2000;
        std::atomic<size_t> next{0}, prepared{0};
        std::vector<std::thread> workers;
        for (int worker = 0; worker < 3; ++worker)
            workers.emplace_back([&] {
                for (size_t index = 0; Fast::Renderer3ds::ClaimPicaStartupPart(next, kParts, index);) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    ++prepared;
                }
            });
        while (prepared.load() < 6U) std::this_thread::yield();
        const auto abandoned = std::chrono::steady_clock::now();
        Fast::Renderer3ds::AbandonPicaStartupPreparation();
        for (auto& worker : workers) worker.join();
        const auto joinMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - abandoned).count();
        std::printf("abandon: prepared=%zu of %zu join_ms=%lld\n", prepared.load(), kParts, static_cast<long long>(joinMs));
        Check(Fast::Renderer3ds::PicaStartupPreparationAbandoned(), "the abandon is visible");
        Check(prepared.load() < 100U && joinMs < 2000, "an abandoned startup preparation stops taking parts");
        size_t index = 0;
        Check(!Fast::Renderer3ds::ClaimPicaStartupPart(next, kParts, index), "nothing is claimed after the abandon");
    }
    std::puts("preparation queue: 6 contracts passed");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "preparation queue test failed: %s\n", error.what());
    return 1;
}
