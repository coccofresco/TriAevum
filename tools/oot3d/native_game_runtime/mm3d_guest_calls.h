#pragma once

#include "mm3d_guest_patches.h"
#include "recomp/a32_runtime.h"

#include <array>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

// Host code that calls the guest's own functions, for the source ports of
// MM3D mods (Project Restoration calls dozens of game routines). A guest
// patch site starts a coroutine on the guest thread's state; each
// `co_await context.Call(...)` branches to the guest function with the
// return address set to a sentinel PC, the A32 runtime runs the function as
// ordinary guest code (SVCs, waits and other threads included), and its
// return to the sentinel resumes the coroutine with the result. When the
// coroutine finishes, the guest continues with the registers it left in
// Saved (the state at the site, which the calls do not disturb) at Resume.
struct Mm3dGuestCallResult {
    std::uint32_t R0 = 0;
    std::uint32_t R1 = 0;
    std::uint32_t S0 = 0; // hard-float return
};

struct Mm3dGuestCallArguments {
    std::array<std::uint32_t, 4> R{};                    // r0..r3
    std::array<std::optional<std::uint32_t>, 16> S{};   // s0..s15 when set
    std::array<std::uint32_t, 8> Stack{};               // further words at sp
    std::size_t StackWords = 0;
};

class Mm3dGuestCallContext {
  public:
    using Result = Mm3dGuestCallResult;
    using Arguments = Mm3dGuestCallArguments;

    // The registers at the site; edit them to change what the guest sees.
    oot3d::recomp::a32::GuestState Saved;
    // Where the guest continues (the site's next instruction by default).
    std::uint32_t Resume = 0;
    oot3d::recomp::a32::MemoryBus* Memory = nullptr;

    class CallAwaiter {
      public:
        CallAwaiter(Mm3dGuestCallContext& context, std::uint32_t entry, Arguments arguments)
            : mContext(context), mEntry(entry), mArguments(std::move(arguments)) {}
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) noexcept;
        Result await_resume() const noexcept { return mContext.mLastResult; }

      private:
        Mm3dGuestCallContext& mContext;
        std::uint32_t mEntry;
        Arguments mArguments;
    };
    CallAwaiter Call(std::uint32_t entry, Arguments arguments = {}) {
        return CallAwaiter(*this, entry, std::move(arguments));
    }

  private:
    friend class Mm3dGuestCalls;
    struct PendingCall {
        std::uint32_t Entry = 0;
        Arguments Args;
    };
    std::optional<PendingCall> mPending;
    Result mLastResult;
};

// The coroutine type a guest-calling site body returns.
class Mm3dGuestTask {
  public:
    struct promise_type {
        Mm3dGuestTask get_return_object() {
            return Mm3dGuestTask(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { Failure = std::current_exception(); }
        std::exception_ptr Failure;
    };
    Mm3dGuestTask() = default;
    explicit Mm3dGuestTask(std::coroutine_handle<promise_type> handle) : mHandle(handle) {}
    Mm3dGuestTask(Mm3dGuestTask&& other) noexcept : mHandle(std::exchange(other.mHandle, {})) {}
    Mm3dGuestTask& operator=(Mm3dGuestTask&& other) noexcept {
        if (this != &other) {
            Destroy();
            mHandle = std::exchange(other.mHandle, {});
        }
        return *this;
    }
    Mm3dGuestTask(const Mm3dGuestTask&) = delete;
    Mm3dGuestTask& operator=(const Mm3dGuestTask&) = delete;
    ~Mm3dGuestTask() { Destroy(); }
    // Runs until the next guest call or the end; rethrows a failure.
    bool Step();
    bool Done() const noexcept { return !mHandle || mHandle.done(); }

  private:
    void Destroy() noexcept {
        if (mHandle) {
            mHandle.destroy();
            mHandle = {};
        }
    }
    std::coroutine_handle<promise_type> mHandle;
};

class Mm3dGuestCalls {
  public:
    // The address guest calls return to: never guest code (below the
    // process image), registered as a site by Install.
    static constexpr std::uint32_t kReturnSentinel = 0x000FFFF0U;
    using Body = std::function<Mm3dGuestTask(Mm3dGuestCallContext&)>;

    // Adds the return sentinel to patches. Call once.
    void Install(Mm3dGuestPatches& patches);
    // A site handler body for Mm3dGuestPatchSite::Handler: starts body at
    // the site (pc) with the guest continuing at resume afterwards.
    std::optional<std::uint32_t> Start(std::uint32_t pc, std::uint32_t resume, const Body& body,
                                       oot3d::recomp::a32::GuestState& state,
                                       oot3d::recomp::a32::MemoryBus& memory);
    std::size_t Active() const noexcept { return mRunning.size(); }

  private:
    struct Running {
        Mm3dGuestCallContext Context;
        Mm3dGuestTask Task;
        std::uint32_t CallerSp = 0;
    };
    std::optional<std::uint32_t> Advance(std::uint32_t thread, Running& running,
                                         oot3d::recomp::a32::GuestState& state);
    std::optional<std::uint32_t> Returned(oot3d::recomp::a32::GuestState& state,
                                          oot3d::recomp::a32::MemoryBus& memory);
    // One suspended body per guest thread (its TLS pointer), nested calls
    // on a thread form a stack.
    std::map<std::uint32_t, std::vector<std::unique_ptr<Running>>> mRunning;
};

} // namespace Oot3dNativeGame
