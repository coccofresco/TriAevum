#include "oot3d_native_render_command_stream.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace Oot3dNativeGame {
namespace {

thread_local bool tInCommand = false;
thread_local bool tSynchronous = false;

uint64_t NanosecondsSince(std::chrono::steady_clock::time_point start) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count());
}

struct CommandScope {
    explicit CommandScope(bool synchronous) {
        tInCommand = true;
        tSynchronous = synchronous;
    }
    ~CommandScope() {
        tInCommand = false;
        tSynchronous = false;
    }
};

} // namespace

NativeRenderCommandStream::NativeRenderCommandStream(Mode mode,
                                                     size_t maxFramesInFlight)
    : mMode(mode),
      mMaxFramesInFlight(maxFramesInFlight == 0U ? 1U : maxFramesInFlight) {}

NativeRenderCommandStream::~NativeRenderCommandStream() {
    StopWorker();
}

void NativeRenderCommandStream::SetMaxFramesInFlight(size_t frames) {
    Drain();
    std::lock_guard lock(mMutex);
    mMaxFramesInFlight = frames == 0U ? 1U : frames;
}

void NativeRenderCommandStream::SetMode(Mode mode) {
    Drain();
    if (mode == mMode) return;
    if (mode == Mode::Inline) StopWorker();
    mMode = mode;
}

bool NativeRenderCommandStream::InCommand() const noexcept {
    return tInCommand;
}

bool NativeRenderCommandStream::InSynchronousCommand() const noexcept {
    return tInCommand && tSynchronous;
}

void NativeRenderCommandStream::SetWaitPump(std::function<void()> pump,
                                            std::chrono::milliseconds interval) {
    std::lock_guard lock(mMutex);
    mWaitPump = std::move(pump);
    mWaitPumpInterval = interval.count() > 0 ? interval : std::chrono::milliseconds(1);
    mWaitPumpThread = std::this_thread::get_id();
}

template <typename Done>
void NativeRenderCommandStream::WaitLocked(std::unique_lock<std::mutex>& lock, Done done) {
    if (!mWaitPump || std::this_thread::get_id() != mWaitPumpThread) {
        mProgress.wait(lock, done);
        return;
    }
    while (!mProgress.wait_for(lock, mWaitPumpInterval, done)) {
        // The pump may take its time (window messages); the worker goes on.
        const auto pump = mWaitPump;
        lock.unlock();
        pump();
        lock.lock();
        ++mWaits.WaitPumps;
    }
}

uint64_t NativeRenderCommandStream::ExecutedCommands() const noexcept {
    std::lock_guard lock(mMutex);
    return mExecuted;
}

NativeRenderCommandStream::WaitStats NativeRenderCommandStream::Waits() const noexcept {
    std::lock_guard lock(mMutex);
    return mWaits;
}

void NativeRenderCommandStream::Submit(Command command, bool endsFrame) {
    Enqueue(std::move(command), endsFrame, false);
}

void NativeRenderCommandStream::Enqueue(Command command, bool endsFrame,
                                        bool synchronous) {
    if (mMode == Mode::Inline) {
        {
            CommandScope scope(true);
            command();
        }
        std::lock_guard lock(mMutex);
        ++mSubmitted;
        ++mExecuted;
        return;
    }
    StartWorker();
    std::unique_lock lock(mMutex);
    RethrowPendingFailureLocked(lock);
    if (endsFrame) {
        const auto waitStart = std::chrono::steady_clock::now();
        WaitLocked(lock, [&] {
            return mFailure != nullptr || mFramesQueued < mMaxFramesInFlight;
        });
        ++mWaits.FrameEndSubmits;
        mWaits.FrameEndBackPressureNanoseconds += NanosecondsSince(waitStart);
        RethrowPendingFailureLocked(lock);
        ++mFramesQueued;
    }
    mQueue.push_back({std::move(command), endsFrame, synchronous});
    ++mSubmitted;
    mWork.notify_one();
}

void NativeRenderCommandStream::Run(Command command) {
    Drain();
    const auto commandStart = std::chrono::steady_clock::now();
    {
        CommandScope scope(true);
        command();
    }
    const uint64_t commandNanoseconds = NanosecondsSince(commandStart);
    std::lock_guard lock(mMutex);
    ++mSubmitted;
    ++mExecuted;
    ++mWaits.RunCommands;
    mWaits.RunCommandNanoseconds += commandNanoseconds;
}

void NativeRenderCommandStream::Drain() {
    if (mMode == Mode::Inline || !mWorker.joinable()) return;
    std::unique_lock lock(mMutex);
    const auto waitStart = std::chrono::steady_clock::now();
    WaitLocked(lock, [&] {
        return mFailure != nullptr || (mQueue.empty() && !mBusy);
    });
    ++mWaits.Drains;
    mWaits.DrainNanoseconds += NanosecondsSince(waitStart);
    RethrowPendingFailureLocked(lock);
}

void NativeRenderCommandStream::WaitForFramesInFlight(size_t frames) {
    if (mMode == Mode::Inline || !mWorker.joinable()) return;
    std::unique_lock lock(mMutex);
    const auto waitStart = std::chrono::steady_clock::now();
    WaitLocked(lock, [&] {
        return mFailure != nullptr || mFramesQueued <= frames;
    });
    ++mWaits.FramesInFlightWaits;
    mWaits.FramesInFlightNanoseconds += NanosecondsSince(waitStart);
    RethrowPendingFailureLocked(lock);
}

void NativeRenderCommandStream::RethrowPendingFailureLocked(
    std::unique_lock<std::mutex>& lock) {
    if (mFailure == nullptr) return;
    // Report the failure once; the stream is empty and usable afterwards.
    auto failure = std::exchange(mFailure, nullptr);
    mQueue.clear();
    mFramesQueued = 0U;
    mExecuted = mSubmitted;
    lock.unlock();
    std::rethrow_exception(failure);
}

void NativeRenderCommandStream::StartWorker() {
    if (mWorker.joinable()) return;
    mStop = false;
    mWorker = std::thread([this] { WorkerLoop(); });
}

void NativeRenderCommandStream::StopWorker() {
    if (!mWorker.joinable()) return;
    {
        std::lock_guard lock(mMutex);
        mStop = true;
    }
    mWork.notify_all();
    mWorker.join();
}

void NativeRenderCommandStream::WorkerLoop() {
    std::unique_lock lock(mMutex);
    for (;;) {
        mWork.wait(lock, [&] { return mStop || !mQueue.empty(); });
        if (mQueue.empty()) return;  // stopping with nothing left to run
        Entry entry = std::move(mQueue.front());
        mQueue.pop_front();
        mBusy = true;
        lock.unlock();
        std::exception_ptr failure;
        try {
            CommandScope scope(entry.Synchronous);
            entry.Work();
        } catch (...) {
            failure = std::current_exception();
        }
        lock.lock();
        mBusy = false;
        if (entry.EndsFrame && mFramesQueued != 0U) --mFramesQueued;
        if (failure != nullptr) {
            mFailure = failure;
            mQueue.clear();
            mFramesQueued = 0U;
        } else {
            ++mExecuted;
        }
        mProgress.notify_all();
    }
}

} // namespace Oot3dNativeGame
