#pragma once

#include "oot3d_native_a32_memory.h"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

class NativeA32Process;

struct NativeA32HostContext {
    NativeA32Process& Process;
    uint32_t ThreadId = 0;
};

enum class NativeA32HostAction : uint8_t {
    Resume,
    Wait,
    Terminate,
    Fault,
};

struct NativeA32HostResult {
    NativeA32HostAction Action = NativeA32HostAction::Fault;
    std::optional<uint32_t> ResumePc;
    uint32_t Detail = 0;
    std::string Error;
};

class NativeA32HostServices {
  public:
    virtual ~NativeA32HostServices() = default;
    virtual NativeA32HostResult HandleSvc(
        uint32_t immediate, oot3d::recomp::a32::GuestState& state,
        NativeA32Memory& memory, NativeA32HostContext& context) = 0;
    virtual NativeA32HostResult HandleFallback(
        oot3d::recomp::a32::FallbackReason reason, uint32_t pc,
        const oot3d::recomp::a32::PackedOp& op,
        oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory);
    // Completes a finite wait that can be resolved atomically while an
    // InvokeFunctionWithState caller remains on the native C++ stack.
    // Synchronization waits requiring another guest thread must return false.
    virtual bool CompleteSynchronousWait(
        uint32_t immediate, const NativeA32HostResult& waitResult,
        oot3d::recomp::a32::GuestState& state,
        NativeA32Memory& memory, NativeA32HostContext& context,
        std::string* error = nullptr);
};

struct NativeA32PrimaryThreadConfig {
    uint32_t EntryAddress = 0;
    uint32_t StackBaseAddress = 0;
    size_t StackSize = 0;
    uint32_t TlsBaseAddress = 0;
    size_t TlsSize = 0;
    uint32_t ThreadPointer = 0;
    uint32_t Argument0 = 0;
    uint32_t InitialCpsr = 0;
    uint32_t InitialFpscr = 0;
    uint32_t Priority = 0;
};

struct NativeA32ThreadConfig {
    uint32_t EntryAddress = 0;
    uint32_t Argument = 0;
    uint32_t StackTopAddress = 0;
    uint32_t InitialCpsr = 0;
    uint32_t InitialFpscr = 0;
    uint32_t Priority = 0;
};

enum class NativeA32ThreadStatus : uint8_t {
    Empty,
    Ready,
    Running,
    Waiting,
    Terminated,
    Faulted,
};

enum class NativeA32ProcessRunKind : uint8_t {
    Yielded,
    Waiting,
    Terminated,
    Faulted,
};

struct NativeA32ProcessRunResult {
    NativeA32ProcessRunKind Kind = NativeA32ProcessRunKind::Faulted;
    oot3d::recomp::a32::ExecutionResult Exit;
    uint32_t HostTransitions = 0;
    std::string Error;
};

struct NativeA32ProcessTimingStats {
    uint64_t ProcessRunCalls = 0;
    uint64_t DispatchCalls = 0;
    uint64_t DispatchNanoseconds = 0;
    uint64_t SvcCalls = 0;
    uint64_t SvcNanoseconds = 0;
    std::array<uint64_t, 256> SvcCallsByImmediate{};
    std::array<uint64_t, 256> SvcNanosecondsByImmediate{};
    uint64_t FallbackCalls = 0;
    uint64_t FallbackNanoseconds = 0;
};

class NativeA32Process {
  public:
    NativeA32Process(const oot3d::recomp::a32::Registry& registry,
                     NativeA32HostServices& hostServices);

    bool MapRegion(const NativeA32MemoryRegionConfig& config,
                   std::string* error = nullptr);
    bool CreatePrimaryThread(const NativeA32PrimaryThreadConfig& config,
                             std::string* error = nullptr);
    bool ResumePrimaryThread(std::optional<uint32_t> resumePc = std::nullopt,
                             std::string* error = nullptr);
    bool ResumeThread(uint32_t threadId,
                      std::optional<uint32_t> resumePc = std::nullopt,
                      std::string* error = nullptr);
    std::optional<uint32_t> CreateThread(const NativeA32ThreadConfig& config,
                                         std::string* error = nullptr);
    NativeA32ProcessRunResult Run(uint32_t blockLimitPerDispatch = 1'000'000U,
                                  uint32_t maxHostTransitions = 1024U);
    // Executes an original guest routine synchronously against a copy of the
    // primary-thread registers. Guest-memory and host-service effects remain
    // visible, while scheduler ownership stays with Run(). Only finite waits
    // that the host can complete atomically are permitted.
    bool InvokeFunction(uint32_t entryAddress,
                        std::span<const uint32_t> arguments,
                        uint32_t returnAddress,
                        std::string* error = nullptr,
                        uint32_t blockLimitPerDispatch = 1'000'000U,
                        uint32_t maxHostTransitions = 1024U);
    bool InvokeFunctionWithResult(
        uint32_t entryAddress, std::span<const uint32_t> arguments,
        uint32_t returnAddress, uint32_t* returnValue,
        std::string* error = nullptr,
        uint32_t blockLimitPerDispatch = 1'000'000U,
        uint32_t maxHostTransitions = 1024U);
    // Executes a guest routine from a caller-prepared register image. This is
    // the ABI-complete path used by source-owner adapters for VFP and stack
    // arguments; scheduler-owned thread registers remain untouched.
    bool InvokeFunctionWithState(
        uint32_t entryAddress, oot3d::recomp::a32::GuestState& state,
        uint32_t returnAddress, std::string* error = nullptr,
        uint32_t blockLimitPerDispatch = 1'000'000U,
        uint32_t maxHostTransitions = 1024U);
    // Invokes one host SVC against a caller-owned register image without
    // mutating scheduler-owned thread registers.
    bool InvokeSvcWithState(
        uint32_t immediate, oot3d::recomp::a32::GuestState& state,
        std::string* error = nullptr);
    void SetBlockEntryCallback(
        oot3d::recomp::a32::BlockEntryCallback callback,
        void* user = nullptr,
        std::vector<uint32_t> filterPcs = {});
    void SetNativeFunctionCallback(
        oot3d::recomp::a32::NativeFunctionCallback callback,
        void* user = nullptr,
        std::vector<uint32_t> functionPcs = {});
    void SetNativeBlockCallback(
        oot3d::recomp::a32::NativeBlockCallback callback,
        void* user = nullptr);
    void SetTimingEnabled(bool enabled) noexcept;
    NativeA32ProcessTimingStats TimingStats() const noexcept;
    uint64_t StateFingerprint() const noexcept;
    nlohmann::json CaptureState() const;
    bool RestoreState(const nlohmann::json& state,
                      std::string* error = nullptr);

    NativeA32Memory& Memory();
    const NativeA32Memory& Memory() const;
    oot3d::recomp::a32::GuestState& PrimaryThreadState();
    const oot3d::recomp::a32::GuestState& PrimaryThreadState() const;
    NativeA32ThreadStatus PrimaryThreadStatus() const;
    size_t ThreadCount() const;
    const oot3d::recomp::a32::GuestState* ThreadState(uint32_t threadId) const;
    oot3d::recomp::a32::GuestState* ThreadState(uint32_t threadId);
    NativeA32ThreadStatus ThreadStatus(uint32_t threadId) const;
    std::optional<uint32_t> ThreadPriority(uint32_t threadId) const;
    std::optional<uint32_t> ThreadEntryAddress(uint32_t threadId) const;
    std::optional<uint32_t> ThreadArgument(uint32_t threadId) const;

  private:
    struct PendingFallback {
        bool Active = false;
        NativeA32HostResult Result;
        oot3d::recomp::a32::ExecutionResult Exit;
    };

    static oot3d::recomp::a32::ExecutionResult DispatchFallback(
        oot3d::recomp::a32::FallbackReason reason, uint32_t pc,
        const oot3d::recomp::a32::PackedOp& op,
        oot3d::recomp::a32::GuestState& state,
        oot3d::recomp::a32::MemoryBus& memory, void* user);
    NativeA32ProcessRunResult ApplyHostResult(
        NativeA32HostResult hostResult,
        const oot3d::recomp::a32::ExecutionResult& exit,
        uint32_t defaultResumePc, uint32_t hostTransitions);
    NativeA32ProcessRunResult Fault(
        const oot3d::recomp::a32::ExecutionResult& exit,
        uint32_t hostTransitions, std::string error);
    oot3d::recomp::a32::GuestState& CurrentThreadState();
    NativeA32ThreadStatus& CurrentThreadStatus();
    bool SelectReadyThread();
    bool HasReadyThread() const;
    bool AllThreadsTerminated() const;

    struct SecondaryThread {
        uint32_t Id = 0;
        uint32_t Priority = 0;
        uint32_t EntryAddress = 0;
        uint32_t Argument = 0;
        oot3d::recomp::a32::GuestState State;
        NativeA32ThreadStatus Status = NativeA32ThreadStatus::Empty;
    };

    const oot3d::recomp::a32::Registry& mRegistry;
    NativeA32HostServices& mHostServices;
    NativeA32Memory mMemory;
    oot3d::recomp::a32::GuestState mPrimaryThreadState;
    NativeA32ThreadStatus mPrimaryThreadStatus = NativeA32ThreadStatus::Empty;
    uint32_t mPrimaryThreadPriority = 0;
    uint32_t mPrimaryThreadEntryAddress = 0;
    uint32_t mPrimaryThreadArgument = 0;
    uint32_t mCurrentThreadId = 0;
    uint32_t mTlsBaseAddress = 0;
    size_t mTlsSize = 0;
    size_t mNextTlsOffset = 0;
    std::deque<SecondaryThread> mSecondaryThreads;
    PendingFallback mPendingFallback;
    oot3d::recomp::a32::BlockEntryCallback mBlockEntryCallback = nullptr;
    void* mBlockEntryUser = nullptr;
    std::vector<uint32_t> mBlockEntryFilterPcs;
    oot3d::recomp::a32::NativeFunctionCallback mNativeFunctionCallback =
        nullptr;
    void* mNativeFunctionUser = nullptr;
    std::vector<uint32_t> mNativeFunctionPcs;
    oot3d::recomp::a32::NativeBlockCallback mNativeBlockCallback = nullptr;
    void* mNativeBlockUser = nullptr;
    bool mTimingEnabled = false;
    NativeA32ProcessTimingStats mTimingStats;
};

} // namespace Oot3dNativeGame
