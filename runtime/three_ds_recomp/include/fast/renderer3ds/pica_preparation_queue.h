#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Fast::Renderer3ds {

// One unit of device-program preparation (source to SPIR-V, SPIR-V to a
// driver pipeline). The work owns every input it reads; its results are
// adopted by the render thread only after Done().
class PicaPreparationTask final {
  public:
    enum class State : uint8_t { Queued, Running, Done };

    explicit PicaPreparationTask(std::function<void()> work) : mWork(std::move(work)) {}

    [[nodiscard]] bool Done() const noexcept {
        return mState.load(std::memory_order_acquire) == State::Done;
    }

  private:
    friend class PicaPreparationQueue;
    std::function<void()> mWork;
    std::atomic<State> mState{State::Queued};
};

// Preparation threads run below normal priority (the calling thread).
void LowerPicaPreparationThreadPriority();

// A small pool of below-normal-priority threads. The render thread submits
// owned-input tasks and polls them; a task it cannot draw without is run on
// the calling thread if no worker has started it yet (Complete).
class PicaPreparationQueue final {
  public:
    explicit PicaPreparationQueue(unsigned threads);
    ~PicaPreparationQueue();
    PicaPreparationQueue(const PicaPreparationQueue&) = delete;
    PicaPreparationQueue& operator=(const PicaPreparationQueue&) = delete;

    std::shared_ptr<PicaPreparationTask> Submit(std::function<void()> work);
    // Runs a still-queued task inline, otherwise waits for its worker.
    void Complete(PicaPreparationTask& task);
    // Runs or waits for every submitted task (device teardown boundary).
    void Drain();
    [[nodiscard]] size_t Pending() const;
    [[nodiscard]] unsigned Threads() const noexcept { return static_cast<unsigned>(mThreads.size()); }

  private:
    void Worker();
    static void Run(PicaPreparationTask& task);

    mutable std::mutex mMutex;
    std::condition_variable mWake;
    std::condition_variable mFinished;
    std::deque<std::shared_ptr<PicaPreparationTask>> mQueue;
    size_t mRunning = 0;
    bool mStopping = false;
    std::vector<std::thread> mThreads;
};

} // namespace Fast::Renderer3ds
