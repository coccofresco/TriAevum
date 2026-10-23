#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace Oot3dNativeGame {

// Ordered renderer work submitted by the guest loop.
//
// Inline mode runs every command at submission: the program order of the
// serial loop, unchanged. Worker mode runs the same commands, in the same
// order, on one worker thread, so the guest can simulate the next frame while
// the previous one is presented. Commands must own copies of their inputs;
// state they touch must be touched only by commands (or after Drain).
//
// Contract in worker mode:
// - Submit queues a command; at most MaxFramesInFlight frames (commands
//   submitted with EndsFrame) are pending, beyond that Submit waits.
// - Run executes a command synchronously after everything before it, on the
//   calling thread once the worker is idle. Thread-affine work (window
//   operations, which Windows delivers to the window's thread) therefore
//   stays on the submitter, and the renderer is never used by two threads at
//   once.
// - An exception thrown by a command stops the worker from running further
//   commands and is rethrown to the submitter by the next Submit, Run or
//   Drain. Pending commands are discarded.
// - Drain waits until the stream is empty.
// - A command may touch guest-owned state (guest memory, guest interrupts)
//   only while the submitter is known to be stopped: inline, or inside Run.
//   InSynchronousCommand tells the two cases apart; commands that may reach
//   guest state check it and fail instead of racing the guest.
class NativeRenderCommandStream final {
  public:
    enum class Mode : uint8_t { Inline, Worker };
    using Command = std::function<void()>;

    explicit NativeRenderCommandStream(Mode mode = Mode::Inline,
                                       size_t maxFramesInFlight = 1U);
    ~NativeRenderCommandStream();

    NativeRenderCommandStream(const NativeRenderCommandStream&) = delete;
    NativeRenderCommandStream& operator=(const NativeRenderCommandStream&) =
        delete;

    [[nodiscard]] Mode CurrentMode() const noexcept { return mMode; }
    // Drains, then switches. Inline -> Worker starts the thread lazily.
    void SetMode(Mode mode);

    void Submit(Command command, bool endsFrame = false);
    void Run(Command command);
    void Drain();
    // Waits until at most `frames` submitted frames have not ended (their
    // EndsFrame command has not run). Inline: returns at once.
    void WaitForFramesInFlight(size_t frames);

    // True on the worker thread while it executes a command, and on the
    // submitting thread while an inline command runs.
    [[nodiscard]] bool InCommand() const noexcept;
    // True while a command runs and its submitter waits for it: every inline
    // command, and a Run command in worker mode.
    [[nodiscard]] bool InSynchronousCommand() const noexcept;
    [[nodiscard]] uint64_t ExecutedCommands() const noexcept;

  private:
    struct Entry {
        Command Work;
        bool EndsFrame = false;
        bool Synchronous = false;
    };

    void Enqueue(Command command, bool endsFrame, bool synchronous);
    void StartWorker();
    void StopWorker();
    void WorkerLoop();
    void RethrowPendingFailureLocked(std::unique_lock<std::mutex>& lock);

    Mode mMode;
    size_t mMaxFramesInFlight;
    std::thread mWorker;
    mutable std::mutex mMutex;
    std::condition_variable mWork;
    std::condition_variable mProgress;
    std::deque<Entry> mQueue;
    size_t mFramesQueued = 0U;
    bool mBusy = false;
    bool mStop = false;
    std::exception_ptr mFailure;
    uint64_t mExecuted = 0U;
    uint64_t mSubmitted = 0U;
};

} // namespace Oot3dNativeGame
