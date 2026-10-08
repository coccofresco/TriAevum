#include "mm3d_guest_calls.h"

#include <stdexcept>

namespace Oot3dNativeGame {

namespace a32 = oot3d::recomp::a32;

void Mm3dGuestCallContext::CallAwaiter::await_suspend(std::coroutine_handle<>) noexcept {
    mContext.mPending = PendingCall{mEntry, std::move(mArguments)};
}

bool Mm3dGuestTask::Step() {
    if (Done()) {
        return false;
    }
    mHandle.resume();
    if (mHandle.promise().Failure) {
        std::rethrow_exception(mHandle.promise().Failure);
    }
    return !mHandle.done();
}

void Mm3dGuestCalls::Install(Mm3dGuestPatches& patches) {
    mPatches = &patches;
    Mm3dGuestPatchSite sentinel;
    sentinel.Pc = kReturnSentinel;
    sentinel.Name = "guest_call.return";
    sentinel.InGuestCode = false;
    sentinel.Handler = [this](a32::GuestState& state, a32::MemoryBus& memory) {
        return Returned(state, memory);
    };
    patches.Add(std::move(sentinel));
}

std::optional<std::uint32_t> Mm3dGuestCalls::Start(std::uint32_t pc, std::uint32_t resume,
                                                   const Body& body, a32::GuestState& state,
                                                   a32::MemoryBus& memory) {
    auto running = std::make_unique<Running>();
    running->Context.Saved = state;
    running->Context.Resume = resume;
    running->Context.Memory = &memory;
    running->CallerSp = state.r[13];
    running->Site = pc;
    running->Task = body(running->Context);
    const std::uint32_t thread = state.thread_pointer;
    auto& stack = mRunning[thread];
    stack.push_back(std::move(running));
    return Advance(thread, *stack.back(), state);
}

std::optional<std::uint32_t> Mm3dGuestCalls::Advance(std::uint32_t thread, Running& running,
                                                     a32::GuestState& state) {
    auto& context = running.Context;
    context.mPending.reset();
    running.Task.Step();
    if (context.mPending.has_value()) {
        // The call runs on the site's registers (callee-saved ones included)
        // below the site's stack pointer.
        const auto call = std::move(*context.mPending);
        context.mPending.reset();
        state = context.Saved;
        for (std::size_t i = 0; i < call.Args.R.size(); ++i) {
            state.r[i] = call.Args.R[i];
        }
        for (std::size_t i = 0; i < call.Args.S.size(); ++i) {
            if (call.Args.S[i].has_value()) {
                state.vfp[i] = *call.Args.S[i];
            }
        }
        std::uint32_t sp = running.CallerSp;
        if (call.Args.StackWords != 0U) {
            sp = (sp - 4U * static_cast<std::uint32_t>(call.Args.StackWords)) & ~7U;
            for (std::size_t i = 0; i < call.Args.StackWords; ++i) {
                if (!context.Memory->Write32(sp + 4U * static_cast<std::uint32_t>(i),
                                             call.Args.Stack[i])) {
                    throw std::runtime_error("MM3D guest call: stack arguments are not writable");
                }
            }
        }
        state.r[13] = sp;
        state.r[14] = kReturnSentinel;
        state.r[15] = call.Entry;
        return call.Entry;
    }
    // Finished: the guest resumes with the registers the body left.
    state = context.Saved;
    const std::uint32_t resume = context.Resume;
    if (resume == running.Site && mPatches != nullptr) {
        // Back at the site (a softbranch): its instruction runs now.
        mPatches->SkipOnce(resume, thread);
    }
    auto& stack = mRunning[thread];
    stack.pop_back();
    if (stack.empty()) {
        mRunning.erase(thread);
    }
    return resume;
}

std::optional<std::uint32_t> Mm3dGuestCalls::Returned(a32::GuestState& state, a32::MemoryBus&) {
    const std::uint32_t thread = state.thread_pointer;
    const auto found = mRunning.find(thread);
    if (found == mRunning.end() || found->second.empty()) {
        return std::nullopt; // not a call of ours: the guest faults as before
    }
    auto& running = *found->second.back();
    running.Context.mLastResult = {state.r[0], state.r[1], state.vfp[0]};
    return Advance(thread, running, state);
}

} // namespace Oot3dNativeGame
