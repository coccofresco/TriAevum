#include "oot3d_native_a32_process.h"

#include "oot3d_native_compiled_functions.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <utility>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kFallbackBoundaryDetail = 0x41333248U;
using TimingClock = std::chrono::steady_clock;

uint64_t ElapsedNanoseconds(TimingClock::time_point start) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            TimingClock::now() - start).count());
}

#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
oot3d::recomp::a32::ExecutionResult DispatchWholeAotProduct(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    uint32_t blockLimit,
    oot3d::recomp::a32::BlockEntryCallback blockEntry,
    void* blockEntryUser, const uint32_t* blockEntryPcs,
    size_t blockEntryPcCount, std::optional<uint32_t> stopPc,
    oot3d::recomp::a32::NativeBlockCallback productMod, void* productModUser) {
    using oot3d::recomp::a32::ExecutionResult;
    using oot3d::recomp::a32::ExitKind;
    using oot3d::recomp::a32::FallbackReason;

    uint32_t consumedTotal = 0U;
    while (consumedTotal < blockLimit) {
        if (consumedTotal != 0U && stopPc.has_value() &&
            state.r[15] == *stopPc) {
            return {ExitKind::Branch, *stopPc, FallbackReason::None, 0U};
        }

        ExecutionResult result{};
        uint32_t consumed = 0U;
        const uint32_t remaining = blockLimit - consumedTotal;
        const bool modHandled = productMod != nullptr && productMod(
            state.r[15], state, memory, &result, remaining, &consumed, productModUser);
        const bool handled = modHandled || ExecuteOot3dCompiledFunction(
            state.r[15], state, memory, &result, remaining, &consumed,
            blockEntry, blockEntryUser, blockEntryPcs, blockEntryPcCount,
            false, true, true, stopPc.value_or(0U));
        if (!handled) {
            return {
                ExitKind::MissingBlock,
                state.r[15],
                FallbackReason::MissingBlock,
                0x57414F54U,
            };
        }

        consumed = std::clamp(consumed, 1U, remaining);
        consumedTotal += consumed;
        if (result.kind != ExitKind::Branch &&
            result.kind != ExitKind::Fallthrough) {
            if (result.kind == ExitKind::BlockLimit) {
                result.detail = consumedTotal;
            }
            return result;
        }
        state.r[15] = result.pc;
    }

    return {
        ExitKind::BlockLimit,
        state.r[15],
        FallbackReason::None,
        consumedTotal,
    };
}
#endif

bool RangeFits(uint32_t baseAddress, size_t size) {
    return size != 0 &&
           static_cast<uint64_t>(baseAddress) + static_cast<uint64_t>(size) <=
               static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U;
}

struct SynchronousInvokeCallbackContext {
    uint32_t ReturnAddress = 0U;
    oot3d::recomp::a32::NativeFunctionCallback Delegate = nullptr;
    void* DelegateUser = nullptr;
};

bool StopSynchronousInvokeAtReturn(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result, void* user) {
    auto& context =
        *static_cast<SynchronousInvokeCallbackContext*>(user);
    if (pc == context.ReturnAddress) {
        if (result == nullptr) {
            return false;
        }
        *result = {
            oot3d::recomp::a32::ExitKind::MissingBlock,
            pc,
            oot3d::recomp::a32::FallbackReason::None,
            0U,
        };
        return true;
    }
    return context.Delegate != nullptr &&
           context.Delegate(
               pc, state, memory, result, context.DelegateUser);
}

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

nlohmann::json EncodeGuestState(
    const oot3d::recomp::a32::GuestState& state) {
    return {
        {"r", state.r},
        {"cpsr", state.cpsr},
        {"fpscr", state.fpscr},
        {"thread_pointer", state.thread_pointer},
        {"vfp", state.vfp},
        {"exclusive_address", state.exclusive_address},
        {"exclusive_token", state.exclusive_token},
        {"exclusive_size", state.exclusive_size},
        {"exclusive_valid", state.exclusive_valid},
    };
}

bool DecodeGuestState(const nlohmann::json& encoded,
                      oot3d::recomp::a32::GuestState& state) {
    if (!encoded.is_object()) {
        return false;
    }
    state = {};
    state.r = encoded.at("r").get<decltype(state.r)>();
    state.cpsr = encoded.at("cpsr").get<uint32_t>();
    state.fpscr = encoded.at("fpscr").get<uint32_t>();
    state.thread_pointer = encoded.at("thread_pointer").get<uint32_t>();
    state.vfp = encoded.at("vfp").get<decltype(state.vfp)>();
    state.exclusive_address =
        encoded.at("exclusive_address").get<uint32_t>();
    state.exclusive_token =
        encoded.at("exclusive_token").get<uint64_t>();
    state.exclusive_size = encoded.at("exclusive_size").get<uint8_t>();
    state.exclusive_valid = encoded.at("exclusive_valid").get<bool>();
    return state.exclusive_size <= 8U;
}

template <typename Enum>
bool DecodeEnum(const nlohmann::json& encoded, Enum maximum, Enum& output) {
    const auto raw = encoded.get<uint32_t>();
    if (raw > static_cast<uint32_t>(maximum)) {
        return false;
    }
    output = static_cast<Enum>(raw);
    return true;
}

} // namespace

NativeA32HostResult NativeA32HostServices::HandleFallback(
    oot3d::recomp::a32::FallbackReason reason, uint32_t pc,
    const oot3d::recomp::a32::PackedOp& op,
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory) {
    static_cast<void>(reason);
    static_cast<void>(pc);
    static_cast<void>(op);
    static_cast<void>(state);
    static_cast<void>(memory);
    return {NativeA32HostAction::Fault, std::nullopt, 0,
            "unhandled native A32 fallback"};
}

bool NativeA32HostServices::CompleteSynchronousWait(
    uint32_t immediate, const NativeA32HostResult& waitResult,
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, std::string* error) {
    static_cast<void>(immediate);
    static_cast<void>(waitResult);
    static_cast<void>(state);
    static_cast<void>(memory);
    static_cast<void>(context);
    static_cast<void>(error);
    return false;
}

NativeA32Process::NativeA32Process(
    const oot3d::recomp::a32::Registry& registry,
    NativeA32HostServices& hostServices)
    : mRegistry(registry), mHostServices(hostServices) {
}

bool NativeA32Process::MapRegion(const NativeA32MemoryRegionConfig& config,
                                 std::string* error) {
    if (mPrimaryThreadStatus != NativeA32ThreadStatus::Empty) {
        SetError(error, "guest regions cannot change after thread creation");
        return false;
    }
    return mMemory.MapRegion(config, error);
}

bool NativeA32Process::CreatePrimaryThread(
    const NativeA32PrimaryThreadConfig& config, std::string* error) {
    if (mPrimaryThreadStatus != NativeA32ThreadStatus::Empty) {
        SetError(error, "native A32 primary thread already exists");
        return false;
    }
    if (config.EntryAddress == 0 ||
        !mMemory.IsExecutable(config.EntryAddress, 1)) {
        SetError(error, "native A32 entrypoint is not executable");
        return false;
    }
    if (!RangeFits(config.StackBaseAddress, config.StackSize) ||
        !RangeFits(config.TlsBaseAddress, config.TlsSize)) {
        SetError(error, "native A32 stack or TLS range is invalid");
        return false;
    }

    const uint64_t stackEnd =
        static_cast<uint64_t>(config.StackBaseAddress) + config.StackSize;
    const uint64_t tlsEnd =
        static_cast<uint64_t>(config.TlsBaseAddress) + config.TlsSize;
    if (static_cast<uint64_t>(config.StackBaseAddress) < tlsEnd &&
        static_cast<uint64_t>(config.TlsBaseAddress) < stackEnd) {
        SetError(error, "native A32 stack and TLS ranges overlap");
        return false;
    }
    if (stackEnd > std::numeric_limits<uint32_t>::max()) {
        SetError(error, "native A32 stack top is not representable");
        return false;
    }

    NativeA32Memory stagedMemory = mMemory;
    std::string mapError;
    if (!stagedMemory.MapRegion({"primary_stack", config.StackBaseAddress,
                                 config.StackSize, true, false, {}},
                                &mapError) ||
        !stagedMemory.MapRegion({"primary_tls", config.TlsBaseAddress,
                                 config.TlsSize, true, false, {}},
                                &mapError)) {
        if (error != nullptr) {
            *error = mapError;
        }
        return false;
    }
    mMemory = std::move(stagedMemory);

    mPrimaryThreadState = {};
    mPrimaryThreadState.r[0] = config.Argument0;
    mPrimaryThreadState.r[13] =
        static_cast<uint32_t>(stackEnd) & ~7U;
    mPrimaryThreadState.r[15] = config.EntryAddress;
    mPrimaryThreadState.cpsr = config.InitialCpsr;
    mPrimaryThreadState.fpscr = config.InitialFpscr;
    mPrimaryThreadState.thread_pointer =
        config.ThreadPointer != 0 ? config.ThreadPointer
                                  : config.TlsBaseAddress;
    mPrimaryThreadPriority = config.Priority;
    mPrimaryThreadEntryAddress = config.EntryAddress;
    mPrimaryThreadArgument = config.Argument0;
    mTlsBaseAddress = config.TlsBaseAddress;
    mTlsSize = config.TlsSize;
    mNextTlsOffset = 0x200U;
    mPrimaryThreadStatus = NativeA32ThreadStatus::Ready;
    return true;
}

bool NativeA32Process::ResumePrimaryThread(
    std::optional<uint32_t> resumePc, std::string* error) {
    return ResumeThread(0, resumePc, error);
}

bool NativeA32Process::ResumeThread(
    uint32_t threadId, std::optional<uint32_t> resumePc,
    std::string* error) {
    auto* state = ThreadState(threadId);
    NativeA32ThreadStatus* status = nullptr;
    if (threadId == 0) {
        status = &mPrimaryThreadStatus;
    } else {
        const size_t index = static_cast<size_t>(threadId - 1U);
        if (index < mSecondaryThreads.size()) {
            status = &mSecondaryThreads[index].Status;
        }
    }
    if (state == nullptr || status == nullptr) {
        SetError(error, "native A32 thread does not exist");
        return false;
    }
    if (*status != NativeA32ThreadStatus::Waiting) {
        SetError(error, "native A32 thread is not waiting");
        return false;
    }
    if (resumePc.has_value()) {
        if (!mMemory.IsExecutable(*resumePc, 1)) {
            SetError(error, "native A32 resume PC is not executable");
            return false;
        }
        state->r[15] = *resumePc;
    }
    *status = NativeA32ThreadStatus::Ready;
    return true;
}

std::optional<uint32_t> NativeA32Process::CreateThread(
    const NativeA32ThreadConfig& config, std::string* error) {
    if (mPrimaryThreadStatus == NativeA32ThreadStatus::Empty) {
        SetError(error, "native A32 primary thread does not exist");
        return std::nullopt;
    }
    if (config.EntryAddress == 0 ||
        !mMemory.IsExecutable(config.EntryAddress, 1)) {
        SetError(error, "native A32 thread entrypoint is not executable");
        return std::nullopt;
    }
    if (config.StackTopAddress < 8U ||
        !mMemory.IsWritable(config.StackTopAddress - 8U, 8U)) {
        SetError(error, "native A32 thread stack top is not writable");
        return std::nullopt;
    }
    constexpr size_t kTlsEntrySize = 0x200U;
    if (mNextTlsOffset > mTlsSize ||
        kTlsEntrySize > mTlsSize - mNextTlsOffset) {
        SetError(error, "native A32 process has no free TLS entry");
        return std::nullopt;
    }
    const uint32_t threadPointer =
        mTlsBaseAddress + static_cast<uint32_t>(mNextTlsOffset);
    if (!mMemory.Fill(threadPointer, kTlsEntrySize, 0)) {
        SetError(error, "native A32 TLS entry is not writable");
        return std::nullopt;
    }

    SecondaryThread thread;
    thread.Id = static_cast<uint32_t>(mSecondaryThreads.size()) + 1U;
    thread.Priority = config.Priority;
    thread.EntryAddress = config.EntryAddress;
    thread.Argument = config.Argument;
    thread.State.r[0] = config.Argument;
    thread.State.r[13] = config.StackTopAddress & ~7U;
    thread.State.r[15] = config.EntryAddress;
    thread.State.cpsr = config.InitialCpsr;
    thread.State.fpscr = config.InitialFpscr;
    thread.State.thread_pointer = threadPointer;
    thread.Status = NativeA32ThreadStatus::Ready;
    mNextTlsOffset += kTlsEntrySize;
    const uint32_t id = thread.Id;
    mSecondaryThreads.push_back(std::move(thread));
    return id;
}

oot3d::recomp::a32::GuestState& NativeA32Process::CurrentThreadState() {
    if (mCurrentThreadId == 0) {
        return mPrimaryThreadState;
    }
    return mSecondaryThreads.at(mCurrentThreadId - 1U).State;
}

NativeA32ThreadStatus& NativeA32Process::CurrentThreadStatus() {
    if (mCurrentThreadId == 0) {
        return mPrimaryThreadStatus;
    }
    return mSecondaryThreads.at(mCurrentThreadId - 1U).Status;
}

bool NativeA32Process::HasReadyThread() const {
    if (mPrimaryThreadStatus == NativeA32ThreadStatus::Ready) {
        return true;
    }
    for (const auto& thread : mSecondaryThreads) {
        if (thread.Status == NativeA32ThreadStatus::Ready) {
            return true;
        }
    }
    return false;
}

bool NativeA32Process::SelectReadyThread() {
    if (!HasReadyThread()) {
        return false;
    }
    uint32_t selectedId = 0;
    uint32_t selectedPriority = std::numeric_limits<uint32_t>::max();
    if (mPrimaryThreadStatus == NativeA32ThreadStatus::Ready) {
        selectedPriority = mPrimaryThreadPriority;
    }
    for (const auto& thread : mSecondaryThreads) {
        if (thread.Status == NativeA32ThreadStatus::Ready &&
            thread.Priority < selectedPriority) {
            selectedId = thread.Id;
            selectedPriority = thread.Priority;
        }
    }
    mCurrentThreadId = selectedId;
    CurrentThreadStatus() = NativeA32ThreadStatus::Running;
    return true;
}

bool NativeA32Process::AllThreadsTerminated() const {
    if (mPrimaryThreadStatus != NativeA32ThreadStatus::Terminated) {
        return false;
    }
    for (const auto& thread : mSecondaryThreads) {
        if (thread.Status != NativeA32ThreadStatus::Terminated) {
            return false;
        }
    }
    return true;
}

oot3d::recomp::a32::ExecutionResult NativeA32Process::DispatchFallback(
    oot3d::recomp::a32::FallbackReason reason, uint32_t pc,
    const oot3d::recomp::a32::PackedOp& op,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory, void* user) {
    auto* process = static_cast<NativeA32Process*>(user);
    if (process == nullptr) {
        return {oot3d::recomp::a32::ExitKind::Fallback, pc, reason, op.raw};
    }
    auto& nativeMemory = static_cast<NativeA32Memory&>(memory);

    const auto hostStart = process->mTimingEnabled
                               ? TimingClock::now()
                               : TimingClock::time_point{};
    NativeA32HostResult hostResult = process->mHostServices.HandleFallback(
        reason, pc, op, state, nativeMemory);
    if (process->mTimingEnabled) {
        ++process->mTimingStats.FallbackCalls;
        process->mTimingStats.FallbackNanoseconds +=
            ElapsedNanoseconds(hostStart);
    }
    if (hostResult.Action == NativeA32HostAction::Resume) {
        if (reason == oot3d::recomp::a32::FallbackReason::MissingBlock &&
            !hostResult.ResumePc.has_value()) {
            hostResult.Action = NativeA32HostAction::Fault;
            hostResult.Error = "missing-block fallback supplied no resume PC";
        } else {
            const uint32_t resumePc =
                hostResult.ResumePc.value_or(pc + 4U);
            return {oot3d::recomp::a32::ExitKind::Fallthrough, resumePc,
                    oot3d::recomp::a32::FallbackReason::None,
                    hostResult.Detail};
        }
    }

    process->mPendingFallback.Active = true;
    process->mPendingFallback.Result = std::move(hostResult);
    process->mPendingFallback.Exit = {
        oot3d::recomp::a32::ExitKind::Wait, pc, reason,
        kFallbackBoundaryDetail};
    return process->mPendingFallback.Exit;
}

NativeA32ProcessRunResult NativeA32Process::Fault(
    const oot3d::recomp::a32::ExecutionResult& exit,
    uint32_t hostTransitions, std::string error) {
    CurrentThreadStatus() = NativeA32ThreadStatus::Faulted;
    return {NativeA32ProcessRunKind::Faulted, exit, hostTransitions,
            std::move(error)};
}

NativeA32ProcessRunResult NativeA32Process::ApplyHostResult(
    NativeA32HostResult hostResult,
    const oot3d::recomp::a32::ExecutionResult& exit,
    uint32_t defaultResumePc, uint32_t hostTransitions) {
    const uint32_t resumePc = hostResult.ResumePc.value_or(defaultResumePc);
    auto& state = CurrentThreadState();
    auto& status = CurrentThreadStatus();
    switch (hostResult.Action) {
    case NativeA32HostAction::Resume:
        state.r[15] = resumePc;
        status = NativeA32ThreadStatus::Ready;
        return {NativeA32ProcessRunKind::Yielded, exit, hostTransitions, {}};
    case NativeA32HostAction::Wait:
        state.r[15] = resumePc;
        status = NativeA32ThreadStatus::Waiting;
        return {NativeA32ProcessRunKind::Waiting, exit, hostTransitions, {}};
    case NativeA32HostAction::Terminate:
        state.r[15] = resumePc;
        status = NativeA32ThreadStatus::Terminated;
        return {NativeA32ProcessRunKind::Terminated, exit, hostTransitions, {}};
    case NativeA32HostAction::Fault:
        return Fault(exit, hostTransitions,
                     hostResult.Error.empty()
                         ? "native A32 host service failed"
                         : std::move(hostResult.Error));
    }
    return Fault(exit, hostTransitions, "invalid native A32 host action");
}

NativeA32ProcessRunResult NativeA32Process::Run(
    uint32_t blockLimitPerDispatch, uint32_t maxHostTransitions) {
    if (mTimingEnabled) {
        ++mTimingStats.ProcessRunCalls;
    }
    if (mPrimaryThreadStatus == NativeA32ThreadStatus::Empty) {
        return Fault({}, 0, "native A32 primary thread does not exist");
    }
    if (mPrimaryThreadStatus == NativeA32ThreadStatus::Faulted) {
        return {NativeA32ProcessRunKind::Faulted, {}, 0,
                "native A32 primary thread is faulted"};
    }
    if (blockLimitPerDispatch == 0 || maxHostTransitions == 0) {
        return Fault({}, 0, "native A32 run limits must be nonzero");
    }
    if (AllThreadsTerminated()) {
        return {NativeA32ProcessRunKind::Terminated, {}, 0, {}};
    }
    if (!SelectReadyThread()) {
        return {NativeA32ProcessRunKind::Waiting, {}, 0, {}};
    }
    for (uint32_t transitions = 0; transitions < maxHostTransitions;
         ++transitions) {
        mPendingFallback = {};
        auto& state = CurrentThreadState();
        const auto dispatchStart = mTimingEnabled
                                       ? TimingClock::now()
                                       : TimingClock::time_point{};
        const auto exit =
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
            DispatchWholeAotProduct(
                state, mMemory, blockLimitPerDispatch,
                mBlockEntryCallback, mBlockEntryUser,
                mBlockEntryFilterPcs.data(), mBlockEntryFilterPcs.size(), std::nullopt,
                mNativeBlockCallback, mNativeBlockUser);
#else
            oot3d::recomp::a32::Dispatch(
            mRegistry, state.r[15], state, mMemory,
            &NativeA32Process::DispatchFallback, this,
            blockLimitPerDispatch, mBlockEntryCallback, mBlockEntryUser,
            mBlockEntryFilterPcs.data(), mBlockEntryFilterPcs.size(),
            mNativeFunctionCallback, mNativeFunctionUser,
            mNativeFunctionPcs.data(), mNativeFunctionPcs.size(),
            mNativeBlockCallback, mNativeBlockUser);
#endif
        if (mTimingEnabled) {
            ++mTimingStats.DispatchCalls;
            mTimingStats.DispatchNanoseconds +=
                ElapsedNanoseconds(dispatchStart);
        }

        if (mPendingFallback.Active) {
            const uint32_t defaultResumePc =
                mPendingFallback.Result.ResumePc.value_or(exit.pc);
            auto result = ApplyHostResult(std::move(mPendingFallback.Result),
                                          exit, defaultResumePc,
                                          transitions + 1U);
            if (result.Kind != NativeA32ProcessRunKind::Yielded) {
                if ((result.Kind == NativeA32ProcessRunKind::Waiting ||
                     result.Kind == NativeA32ProcessRunKind::Terminated) &&
                    SelectReadyThread()) {
                    continue;
                }
                if (AllThreadsTerminated()) {
                    result.Kind = NativeA32ProcessRunKind::Terminated;
                }
                return result;
            }
            SelectReadyThread();
            continue;
        }

        if (exit.kind == oot3d::recomp::a32::ExitKind::Svc) {
            NativeA32HostContext context{*this, mCurrentThreadId};
            const auto svcStart = mTimingEnabled
                                      ? TimingClock::now()
                                      : TimingClock::time_point{};
            auto hostResult =
                mHostServices.HandleSvc(exit.detail, state, mMemory, context);
            if (mTimingEnabled) {
                ++mTimingStats.SvcCalls;
                const uint64_t nanoseconds = ElapsedNanoseconds(svcStart);
                mTimingStats.SvcNanoseconds += nanoseconds;
                if (exit.detail < mTimingStats.SvcCallsByImmediate.size()) {
                    ++mTimingStats.SvcCallsByImmediate[exit.detail];
                    mTimingStats.SvcNanosecondsByImmediate[exit.detail] +=
                        nanoseconds;
                }
            }
            auto result = ApplyHostResult(
                std::move(hostResult), exit, exit.pc + 4U,
                transitions + 1U);
            if (result.Kind != NativeA32ProcessRunKind::Yielded) {
                if ((result.Kind == NativeA32ProcessRunKind::Waiting ||
                     result.Kind == NativeA32ProcessRunKind::Terminated) &&
                    SelectReadyThread()) {
                    continue;
                }
                if (AllThreadsTerminated()) {
                    result.Kind = NativeA32ProcessRunKind::Terminated;
                }
                return result;
            }
            SelectReadyThread();
            continue;
        }
        if (exit.kind == oot3d::recomp::a32::ExitKind::BlockLimit) {
            CurrentThreadStatus() = NativeA32ThreadStatus::Ready;
            return {NativeA32ProcessRunKind::Yielded, exit, transitions, {}};
        }
        if (exit.kind == oot3d::recomp::a32::ExitKind::Wait) {
            CurrentThreadStatus() = NativeA32ThreadStatus::Waiting;
            if (SelectReadyThread()) {
                continue;
            }
            return {NativeA32ProcessRunKind::Waiting, exit, transitions, {}};
        }
        return Fault(
            exit, transitions,
            "native A32 dispatch exited unexpectedly: kind=" +
                std::to_string(static_cast<uint32_t>(exit.kind)) +
                " pc=0x" + [&] {
                    char encoded[9]{};
                    std::snprintf(encoded, sizeof(encoded), "%08X", exit.pc);
                    return std::string(encoded);
                }() +
                " detail=" + std::to_string(exit.detail));
    }

    CurrentThreadStatus() = NativeA32ThreadStatus::Ready;
    return {NativeA32ProcessRunKind::Yielded,
            {oot3d::recomp::a32::ExitKind::BlockLimit,
             CurrentThreadState().r[15],
             oot3d::recomp::a32::FallbackReason::None,
             maxHostTransitions},
            maxHostTransitions, {}};
}

bool NativeA32Process::InvokeFunction(
    uint32_t entryAddress, std::span<const uint32_t> arguments,
    uint32_t returnAddress, std::string* error,
    uint32_t blockLimitPerDispatch, uint32_t maxHostTransitions) {
    return InvokeFunctionWithResult(
        entryAddress, arguments, returnAddress, nullptr, error,
        blockLimitPerDispatch, maxHostTransitions);
}

bool NativeA32Process::InvokeFunctionWithResult(
    uint32_t entryAddress, std::span<const uint32_t> arguments,
    uint32_t returnAddress, uint32_t* returnValue, std::string* error,
    uint32_t blockLimitPerDispatch, uint32_t maxHostTransitions) {
    if (arguments.size() > 4U) {
        SetError(error, "native A32 function accepts at most four arguments");
        return false;
    }
    auto state = mPrimaryThreadState;
    for (size_t index = 0; index < arguments.size(); ++index) {
        state.r[index] = arguments[index];
    }
    if (!InvokeFunctionWithState(
            entryAddress, state, returnAddress, error,
            blockLimitPerDispatch, maxHostTransitions)) {
        return false;
    }
    if (returnValue != nullptr) {
        *returnValue = state.r[0];
    }
    return true;
}

bool NativeA32Process::InvokeFunctionWithState(
    uint32_t entryAddress, oot3d::recomp::a32::GuestState& state,
    uint32_t returnAddress, std::string* error,
    uint32_t blockLimitPerDispatch, uint32_t maxHostTransitions) {
    if (!mMemory.IsExecutable(entryAddress, 1U)) {
        SetError(error, "native A32 function entry is not executable");
        return false;
    }
    if (blockLimitPerDispatch == 0U || maxHostTransitions == 0U) {
        SetError(error, "native A32 function limits must be nonzero");
        return false;
    }

    state.r[14] = returnAddress;
    state.r[15] = entryAddress;

    SynchronousInvokeCallbackContext invokeCallback{
        returnAddress,
        mNativeFunctionCallback,
        mNativeFunctionUser,
    };
    auto invokeCallbackPcs = mNativeFunctionPcs;
    if (mNativeFunctionCallback == nullptr ||
        !mNativeFunctionPcs.empty()) {
        invokeCallbackPcs.push_back(returnAddress);
        std::sort(invokeCallbackPcs.begin(), invokeCallbackPcs.end());
        invokeCallbackPcs.erase(
            std::unique(
                invokeCallbackPcs.begin(), invokeCallbackPcs.end()),
            invokeCallbackPcs.end());
    }

    for (uint32_t transition = 0U; transition < maxHostTransitions;
         ++transition) {
        const auto exit =
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
            DispatchWholeAotProduct(
                state, mMemory, blockLimitPerDispatch,
                mBlockEntryCallback, mBlockEntryUser,
                mBlockEntryFilterPcs.data(), mBlockEntryFilterPcs.size(),
                returnAddress, mNativeBlockCallback, mNativeBlockUser);
#else
            oot3d::recomp::a32::Dispatch(
            mRegistry, state.r[15], state, mMemory, nullptr, nullptr,
            blockLimitPerDispatch, mBlockEntryCallback, mBlockEntryUser,
            mBlockEntryFilterPcs.data(), mBlockEntryFilterPcs.size(),
            &StopSynchronousInvokeAtReturn, &invokeCallback,
            invokeCallbackPcs.data(), invokeCallbackPcs.size(),
            mNativeBlockCallback, mNativeBlockUser);
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
        if ((exit.kind == oot3d::recomp::a32::ExitKind::Branch ||
             exit.kind == oot3d::recomp::a32::ExitKind::Fallthrough) &&
            exit.pc == returnAddress) {
            return true;
        }
#else
        if (exit.kind == oot3d::recomp::a32::ExitKind::MissingBlock &&
            exit.pc == returnAddress) {
            return true;
        }
#endif
        if (exit.kind == oot3d::recomp::a32::ExitKind::Svc) {
            NativeA32HostContext context{*this, mCurrentThreadId};
            auto hostResult =
                mHostServices.HandleSvc(exit.detail, state, mMemory, context);
            if (hostResult.Action == NativeA32HostAction::Resume) {
                state.r[15] = hostResult.ResumePc.value_or(exit.pc + 4U);
                continue;
            }
            if (hostResult.Action == NativeA32HostAction::Wait) {
                std::string completionError;
                if (mHostServices.CompleteSynchronousWait(
                        exit.detail, hostResult, state, mMemory, context,
                        &completionError)) {
                    state.r[15] =
                        hostResult.ResumePc.value_or(exit.pc + 4U);
                    continue;
                }
                if (hostResult.Error.empty() &&
                    !completionError.empty()) {
                    hostResult.Error = std::move(completionError);
                }
            }
            if (error != nullptr) {
                *error = "entry=" + std::to_string(entryAddress) +
                         " svc=" + std::to_string(exit.detail) +
                         " host_action=" + std::to_string(
                             static_cast<uint32_t>(hostResult.Action)) +
                         (hostResult.Error.empty()
                              ? std::string{}
                              : " host_error=" + hostResult.Error);
            }
            return false;
        }
        if (exit.kind == oot3d::recomp::a32::ExitKind::BlockLimit) {
            state.r[15] = exit.pc;
            continue;
        }
        if (error != nullptr) {
            *error = "entry=" + std::to_string(entryAddress) +
                     " exit_kind=" +
                     std::to_string(static_cast<uint32_t>(exit.kind)) +
                     " exit_pc=" + std::to_string(exit.pc) +
                     " detail=" + std::to_string(exit.detail);
        }
        return false;
    }
    SetError(error, "native A32 function exceeded host-transition limit");
    return false;
}

bool NativeA32Process::InvokeSvcWithState(
    uint32_t immediate, oot3d::recomp::a32::GuestState& state,
    std::string* error) {
    NativeA32HostContext context{*this, mCurrentThreadId};
    const auto svcStart =
        mTimingEnabled ? TimingClock::now() : TimingClock::time_point{};
    auto hostResult =
        mHostServices.HandleSvc(immediate, state, mMemory, context);
    if (mTimingEnabled) {
        ++mTimingStats.SvcCalls;
        const uint64_t nanoseconds = ElapsedNanoseconds(svcStart);
        mTimingStats.SvcNanoseconds += nanoseconds;
        if (immediate < mTimingStats.SvcCallsByImmediate.size()) {
            ++mTimingStats.SvcCallsByImmediate[immediate];
            mTimingStats.SvcNanosecondsByImmediate[immediate] += nanoseconds;
        }
    }
    if (hostResult.Action != NativeA32HostAction::Resume) {
        if (error != nullptr) {
            *error = "svc=" + std::to_string(immediate) +
                     " host_action=" +
                     std::to_string(
                         static_cast<uint32_t>(hostResult.Action)) +
                     (hostResult.Error.empty()
                          ? std::string{}
                          : " host_error=" + hostResult.Error);
        }
        return false;
    }
    if (hostResult.ResumePc.has_value()) {
        state.r[15] = *hostResult.ResumePc;
    }
    return true;
}

void NativeA32Process::SetBlockEntryCallback(
    oot3d::recomp::a32::BlockEntryCallback callback, void* user,
    std::vector<uint32_t> filterPcs) {
    mBlockEntryCallback = callback;
    mBlockEntryUser = callback != nullptr ? user : nullptr;
    mBlockEntryFilterPcs = callback != nullptr ? std::move(filterPcs)
                                                : std::vector<uint32_t>{};
    std::sort(mBlockEntryFilterPcs.begin(), mBlockEntryFilterPcs.end());
    mBlockEntryFilterPcs.erase(
        std::unique(mBlockEntryFilterPcs.begin(),
                    mBlockEntryFilterPcs.end()),
        mBlockEntryFilterPcs.end());
}

void NativeA32Process::SetNativeFunctionCallback(
    oot3d::recomp::a32::NativeFunctionCallback callback, void* user,
    std::vector<uint32_t> functionPcs) {
    mNativeFunctionCallback = callback;
    mNativeFunctionUser = callback != nullptr ? user : nullptr;
    mNativeFunctionPcs = callback != nullptr ? std::move(functionPcs)
                                             : std::vector<uint32_t>{};
    std::sort(mNativeFunctionPcs.begin(), mNativeFunctionPcs.end());
    mNativeFunctionPcs.erase(
        std::unique(mNativeFunctionPcs.begin(), mNativeFunctionPcs.end()),
        mNativeFunctionPcs.end());
}

void NativeA32Process::SetNativeBlockCallback(
    oot3d::recomp::a32::NativeBlockCallback callback, void* user) {
    mNativeBlockCallback = callback;
    mNativeBlockUser = callback != nullptr ? user : nullptr;
}

void NativeA32Process::SetTimingEnabled(bool enabled) noexcept {
    mTimingEnabled = enabled;
    mTimingStats = {};
}

NativeA32ProcessTimingStats NativeA32Process::TimingStats() const noexcept {
    return mTimingStats;
}

uint64_t NativeA32Process::StateFingerprint() const noexcept {
    uint64_t hash = mMemory.StateFingerprint();
    const auto append = [&](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ULL;
        }
    };
    const auto appendGuestState = [&](
                                      const oot3d::recomp::a32::GuestState& state) {
        append(state.r.data(), state.r.size() * sizeof(state.r[0]));
        append(&state.cpsr, sizeof(state.cpsr));
        append(&state.fpscr, sizeof(state.fpscr));
        append(&state.thread_pointer, sizeof(state.thread_pointer));
        append(state.vfp.data(), state.vfp.size() * sizeof(state.vfp[0]));
        append(&state.exclusive_address, sizeof(state.exclusive_address));
        append(&state.exclusive_token, sizeof(state.exclusive_token));
        append(&state.exclusive_size, sizeof(state.exclusive_size));
        append(&state.exclusive_valid, sizeof(state.exclusive_valid));
    };
    appendGuestState(mPrimaryThreadState);
    append(&mPrimaryThreadStatus, sizeof(mPrimaryThreadStatus));
    append(&mPrimaryThreadPriority, sizeof(mPrimaryThreadPriority));
    append(&mPrimaryThreadEntryAddress, sizeof(mPrimaryThreadEntryAddress));
    append(&mPrimaryThreadArgument, sizeof(mPrimaryThreadArgument));
    append(&mCurrentThreadId, sizeof(mCurrentThreadId));
    append(&mTlsBaseAddress, sizeof(mTlsBaseAddress));
    append(&mTlsSize, sizeof(mTlsSize));
    append(&mNextTlsOffset, sizeof(mNextTlsOffset));
    for (const auto& thread : mSecondaryThreads) {
        append(&thread.Id, sizeof(thread.Id));
        append(&thread.Priority, sizeof(thread.Priority));
        append(&thread.EntryAddress, sizeof(thread.EntryAddress));
        append(&thread.Argument, sizeof(thread.Argument));
        appendGuestState(thread.State);
        append(&thread.Status, sizeof(thread.Status));
    }
    append(&mPendingFallback.Active, sizeof(mPendingFallback.Active));
    append(&mPendingFallback.Result.Action,
           sizeof(mPendingFallback.Result.Action));
    const bool hasResumePc = mPendingFallback.Result.ResumePc.has_value();
    append(&hasResumePc, sizeof(hasResumePc));
    if (hasResumePc) {
        const uint32_t resumePc = *mPendingFallback.Result.ResumePc;
        append(&resumePc, sizeof(resumePc));
    }
    append(&mPendingFallback.Result.Detail,
           sizeof(mPendingFallback.Result.Detail));
    append(mPendingFallback.Result.Error.data(),
           mPendingFallback.Result.Error.size());
    append(&mPendingFallback.Exit.kind, sizeof(mPendingFallback.Exit.kind));
    append(&mPendingFallback.Exit.pc, sizeof(mPendingFallback.Exit.pc));
    append(&mPendingFallback.Exit.fallback,
           sizeof(mPendingFallback.Exit.fallback));
    append(&mPendingFallback.Exit.detail,
           sizeof(mPendingFallback.Exit.detail));
    return hash;
}

nlohmann::json NativeA32Process::CaptureState() const {
    nlohmann::json secondaryThreads = nlohmann::json::array();
    for (const auto& thread : mSecondaryThreads) {
        secondaryThreads.push_back({
            {"id", thread.Id},
            {"priority", thread.Priority},
            {"entry_address", thread.EntryAddress},
            {"argument", thread.Argument},
            {"state", EncodeGuestState(thread.State)},
            {"status", static_cast<uint32_t>(thread.Status)},
        });
    }
    nlohmann::json pendingFallback{
        {"active", mPendingFallback.Active},
        {"action",
         static_cast<uint32_t>(mPendingFallback.Result.Action)},
        {"resume_pc", mPendingFallback.Result.ResumePc.has_value()
                          ? nlohmann::json(*mPendingFallback.Result.ResumePc)
                          : nlohmann::json(nullptr)},
        {"detail", mPendingFallback.Result.Detail},
        {"error", mPendingFallback.Result.Error},
        {"exit_kind",
         static_cast<uint32_t>(mPendingFallback.Exit.kind)},
        {"exit_pc", mPendingFallback.Exit.pc},
        {"exit_reason",
         static_cast<uint32_t>(mPendingFallback.Exit.fallback)},
        {"exit_detail", mPendingFallback.Exit.detail},
    };
    return {
        {"format", "oot3d_native_a32_process_state_v1"},
        {"memory", mMemory.CaptureState()},
        {"primary_thread_state", EncodeGuestState(mPrimaryThreadState)},
        {"primary_thread_status",
         static_cast<uint32_t>(mPrimaryThreadStatus)},
        {"primary_thread_priority", mPrimaryThreadPriority},
        {"primary_thread_entry_address", mPrimaryThreadEntryAddress},
        {"primary_thread_argument", mPrimaryThreadArgument},
        {"current_thread_id", mCurrentThreadId},
        {"tls_base_address", mTlsBaseAddress},
        {"tls_size", mTlsSize},
        {"next_tls_offset", mNextTlsOffset},
        {"secondary_threads", std::move(secondaryThreads)},
        {"pending_fallback", std::move(pendingFallback)},
    };
}

bool NativeA32Process::RestoreState(const nlohmann::json& state,
                                    std::string* error) {
    try {
        if (!state.is_object() ||
            state.value("format", std::string{}) !=
                "oot3d_native_a32_process_state_v1") {
            SetError(error, "native A32 process state format is invalid");
            return false;
        }
        NativeA32Memory stagedMemory;
        if (!stagedMemory.RestoreState(state.at("memory"), error)) {
            return false;
        }
        oot3d::recomp::a32::GuestState primaryState;
        if (!DecodeGuestState(state.at("primary_thread_state"),
                              primaryState)) {
            SetError(error, "native A32 primary thread state is invalid");
            return false;
        }
        NativeA32ThreadStatus primaryStatus;
        if (!DecodeEnum(state.at("primary_thread_status"),
                        NativeA32ThreadStatus::Faulted, primaryStatus)) {
            SetError(error, "native A32 primary thread status is invalid");
            return false;
        }
        std::deque<SecondaryThread> secondaryThreads;
        const auto& encodedThreads = state.at("secondary_threads");
        if (!encodedThreads.is_array()) {
            SetError(error, "native A32 secondary thread state is invalid");
            return false;
        }
        for (const auto& encoded : encodedThreads) {
            SecondaryThread thread;
            thread.Id = encoded.at("id").get<uint32_t>();
            thread.Priority = encoded.at("priority").get<uint32_t>();
            thread.EntryAddress =
                encoded.at("entry_address").get<uint32_t>();
            thread.Argument = encoded.at("argument").get<uint32_t>();
            if (thread.Id != secondaryThreads.size() + 1U ||
                !DecodeGuestState(encoded.at("state"), thread.State) ||
                !DecodeEnum(encoded.at("status"),
                            NativeA32ThreadStatus::Faulted,
                            thread.Status)) {
                SetError(error,
                         "native A32 secondary thread record is invalid");
                return false;
            }
            secondaryThreads.push_back(std::move(thread));
        }
        const uint32_t currentThreadId =
            state.at("current_thread_id").get<uint32_t>();
        if (currentThreadId > secondaryThreads.size()) {
            SetError(error, "native A32 current thread ID is invalid");
            return false;
        }
        const uint32_t tlsBaseAddress =
            state.at("tls_base_address").get<uint32_t>();
        const size_t tlsSize = state.at("tls_size").get<size_t>();
        const size_t nextTlsOffset =
            state.at("next_tls_offset").get<size_t>();
        if (nextTlsOffset > tlsSize ||
            (tlsSize != 0U &&
             !stagedMemory.IsMapped(tlsBaseAddress, tlsSize))) {
            SetError(error, "native A32 TLS state is invalid");
            return false;
        }

        PendingFallback pendingFallback;
        const auto& encodedFallback = state.at("pending_fallback");
        pendingFallback.Active = encodedFallback.at("active").get<bool>();
        if (!DecodeEnum(encodedFallback.at("action"),
                        NativeA32HostAction::Fault,
                        pendingFallback.Result.Action)) {
            SetError(error, "native A32 pending host action is invalid");
            return false;
        }
        if (!encodedFallback.at("resume_pc").is_null()) {
            pendingFallback.Result.ResumePc =
                encodedFallback.at("resume_pc").get<uint32_t>();
        }
        pendingFallback.Result.Detail =
            encodedFallback.at("detail").get<uint32_t>();
        pendingFallback.Result.Error =
            encodedFallback.at("error").get<std::string>();
        if (!DecodeEnum(encodedFallback.at("exit_kind"),
                        oot3d::recomp::a32::ExitKind::Unsupported,
                        pendingFallback.Exit.kind) ||
            !DecodeEnum(encodedFallback.at("exit_reason"),
                        oot3d::recomp::a32::FallbackReason::MissingBlock,
                        pendingFallback.Exit.fallback)) {
            SetError(error, "native A32 pending exit state is invalid");
            return false;
        }
        pendingFallback.Exit.pc =
            encodedFallback.at("exit_pc").get<uint32_t>();
        pendingFallback.Exit.detail =
            encodedFallback.at("exit_detail").get<uint32_t>();

        mMemory = std::move(stagedMemory);
        mPrimaryThreadState = primaryState;
        mPrimaryThreadStatus = primaryStatus;
        mPrimaryThreadPriority =
            state.at("primary_thread_priority").get<uint32_t>();
        mPrimaryThreadEntryAddress =
            state.at("primary_thread_entry_address").get<uint32_t>();
        mPrimaryThreadArgument =
            state.at("primary_thread_argument").get<uint32_t>();
        mCurrentThreadId = currentThreadId;
        mTlsBaseAddress = tlsBaseAddress;
        mTlsSize = tlsSize;
        mNextTlsOffset = nextTlsOffset;
        mSecondaryThreads = std::move(secondaryThreads);
        mPendingFallback = std::move(pendingFallback);
        mTimingStats = {};
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("native A32 process state decode failed: ") +
                     exception.what();
        }
        return false;
    }
}

NativeA32Memory& NativeA32Process::Memory() {
    return mMemory;
}

const NativeA32Memory& NativeA32Process::Memory() const {
    return mMemory;
}

oot3d::recomp::a32::GuestState& NativeA32Process::PrimaryThreadState() {
    return mPrimaryThreadState;
}

const oot3d::recomp::a32::GuestState&
NativeA32Process::PrimaryThreadState() const {
    return mPrimaryThreadState;
}

NativeA32ThreadStatus NativeA32Process::PrimaryThreadStatus() const {
    return mPrimaryThreadStatus;
}

size_t NativeA32Process::ThreadCount() const {
    return mPrimaryThreadStatus == NativeA32ThreadStatus::Empty
               ? 0
               : 1U + mSecondaryThreads.size();
}

const oot3d::recomp::a32::GuestState* NativeA32Process::ThreadState(
    uint32_t threadId) const {
    if (threadId == 0) {
        return mPrimaryThreadStatus == NativeA32ThreadStatus::Empty
                   ? nullptr
                   : &mPrimaryThreadState;
    }
    const size_t index = threadId - 1U;
    return index < mSecondaryThreads.size()
               ? &mSecondaryThreads[index].State
               : nullptr;
}

oot3d::recomp::a32::GuestState* NativeA32Process::ThreadState(
    uint32_t threadId) {
    return const_cast<oot3d::recomp::a32::GuestState*>(
        std::as_const(*this).ThreadState(threadId));
}

NativeA32ThreadStatus NativeA32Process::ThreadStatus(uint32_t threadId) const {
    if (threadId == 0) {
        return mPrimaryThreadStatus;
    }
    const size_t index = threadId - 1U;
    return index < mSecondaryThreads.size()
               ? mSecondaryThreads[index].Status
               : NativeA32ThreadStatus::Empty;
}

std::optional<uint32_t> NativeA32Process::ThreadPriority(
    uint32_t threadId) const {
    if (threadId == 0) {
        return mPrimaryThreadStatus == NativeA32ThreadStatus::Empty
                   ? std::nullopt
                   : std::optional<uint32_t>(mPrimaryThreadPriority);
    }
    const size_t index = static_cast<size_t>(threadId - 1U);
    return index < mSecondaryThreads.size()
               ? std::optional<uint32_t>(mSecondaryThreads[index].Priority)
               : std::nullopt;
}

std::optional<uint32_t> NativeA32Process::ThreadEntryAddress(
    uint32_t threadId) const {
    if (threadId == 0) {
        return mPrimaryThreadStatus == NativeA32ThreadStatus::Empty
                   ? std::nullopt
                   : std::optional<uint32_t>(mPrimaryThreadEntryAddress);
    }
    const size_t index = static_cast<size_t>(threadId - 1U);
    return index < mSecondaryThreads.size()
               ? std::optional<uint32_t>(mSecondaryThreads[index].EntryAddress)
               : std::nullopt;
}

std::optional<uint32_t> NativeA32Process::ThreadArgument(
    uint32_t threadId) const {
    if (threadId == 0) {
        return mPrimaryThreadStatus == NativeA32ThreadStatus::Empty
                   ? std::nullopt
                   : std::optional<uint32_t>(mPrimaryThreadArgument);
    }
    const size_t index = static_cast<size_t>(threadId - 1U);
    return index < mSecondaryThreads.size()
               ? std::optional<uint32_t>(mSecondaryThreads[index].Argument)
               : std::nullopt;
}

} // namespace Oot3dNativeGame
