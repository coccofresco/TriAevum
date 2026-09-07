#include "oot3d_native_a32_process.h"
#include "oot3d_native_compiled_functions.h"
#include "oot3d_native_owner_call_adapter.h"
#include "oot3d_native_true_aot_blocks.h"

#if defined(OOT3D_NATIVE_A32_AOT_TESTS)
#include "oot3d_a32_generated.h"
#include "oot3d_native_whole_aot_runtime.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {

constexpr uint32_t kEntry = 0x00001000U;
constexpr uint32_t kData = 0x00002000U;
constexpr uint32_t kStack = 0x00007000U;
constexpr uint32_t kTls = 0x00009000U;
constexpr uint32_t kInvokeEntry = kEntry + 0x20U;
constexpr uint32_t kInvokeReturn = 0x60000000U;

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int32_t ScaleSmallSignedSample(int32_t value, int32_t coefficient) {
    return value < 0 ? -((coefficient * -value) >> 7)
                     : (coefficient * value) >> 7;
}

class TestHostServices final
    : public Oot3dNativeGame::NativeA32HostServices {
  public:
    Oot3dNativeGame::NativeA32HostResult HandleSvc(
        uint32_t immediate, oot3d::recomp::a32::GuestState& state,
        Oot3dNativeGame::NativeA32Memory& memory,
        Oot3dNativeGame::NativeA32HostContext& context) override {
        static_cast<void>(context);
        ++SvcCount;
        if (FirstSvc == 0) {
            FirstSvc = immediate;
        }
        if (immediate == 1U) {
            state.r[0] = 0x12345678U;
            if (!memory.Write32(kData, 0xA5A55A5AU)) {
                return {Oot3dNativeGame::NativeA32HostAction::Fault,
                        std::nullopt, 0, "test SVC could not write guest data"};
            }
            return {Oot3dNativeGame::NativeA32HostAction::Wait};
        }
        if (immediate == 2U) {
            uint32_t value = 0;
            StatePersisted = state.r[0] == 0x12345678U &&
                             memory.Read32(kData, &value) &&
                             value == 0xA5A55A5AU;
            return {Oot3dNativeGame::NativeA32HostAction::Terminate};
        }
        if (immediate == 3U) {
            return {Oot3dNativeGame::NativeA32HostAction::Terminate};
        }
        if (immediate == 4U) {
            state.r[0] ^= 0x00FF00FFU;
            if (WaitOnSvc4) {
                return {Oot3dNativeGame::NativeA32HostAction::Wait};
            }
            return {Oot3dNativeGame::NativeA32HostAction::Resume};
        }
        return {Oot3dNativeGame::NativeA32HostAction::Fault, std::nullopt, 0,
                "unexpected test SVC"};
    }

    bool CompleteSynchronousWait(
        uint32_t immediate,
        const Oot3dNativeGame::NativeA32HostResult& waitResult,
        oot3d::recomp::a32::GuestState& state,
        Oot3dNativeGame::NativeA32Memory& memory,
        Oot3dNativeGame::NativeA32HostContext& context,
        std::string* error) override {
        static_cast<void>(state);
        static_cast<void>(memory);
        static_cast<void>(context);
        ++SynchronousWaitCompletionAttempts;
        if (immediate == 4U &&
            waitResult.Action ==
                Oot3dNativeGame::NativeA32HostAction::Wait &&
            CompleteSvc4Wait) {
            ++SynchronousWaitCompletions;
            return true;
        }
        if (error != nullptr) {
            *error = "test synchronous wait cannot complete";
        }
        return false;
    }

    Oot3dNativeGame::NativeA32HostResult HandleFallback(
        oot3d::recomp::a32::FallbackReason reason, uint32_t pc,
        const oot3d::recomp::a32::PackedOp& op,
        oot3d::recomp::a32::GuestState& state,
        Oot3dNativeGame::NativeA32Memory& memory) override {
        static_cast<void>(reason);
        static_cast<void>(pc);
        static_cast<void>(op);
        static_cast<void>(state);
        static_cast<void>(memory);
        ++FallbackCount;
        return {Oot3dNativeGame::NativeA32HostAction::Fault, std::nullopt, 0,
                "expected test fallback fault"};
    }

    uint32_t SvcCount = 0;
    uint32_t FallbackCount = 0;
    uint32_t FirstSvc = 0;
    bool StatePersisted = false;
    bool WaitOnSvc4 = false;
    bool CompleteSvc4Wait = false;
    uint32_t SynchronousWaitCompletionAttempts = 0;
    uint32_t SynchronousWaitCompletions = 0;
};

class OwnerTransitionBudgetHostServices final
    : public Oot3dNativeGame::NativeA32HostServices {
  public:
    Oot3dNativeGame::NativeA32HostResult HandleSvc(
        uint32_t immediate, oot3d::recomp::a32::GuestState&,
        Oot3dNativeGame::NativeA32Memory&,
        Oot3dNativeGame::NativeA32HostContext&) override {
        if (immediate != 4U) {
            return {
                Oot3dNativeGame::NativeA32HostAction::Fault,
                std::nullopt,
                immediate,
                "unexpected owner-budget SVC",
            };
        }
        ++Calls;
        return {
            Oot3dNativeGame::NativeA32HostAction::Resume,
            Calls < CallsBeforeReturn
                ? std::optional<uint32_t>{kInvokeEntry}
                : std::optional<uint32_t>{kInvokeReturn},
        };
    }

    uint32_t Calls = 0U;
    uint32_t CallsBeforeReturn = 1025U;
};

oot3d::recomp::a32::Registry MakeTestRegistry() {
    static constexpr std::array<oot3d::recomp::a32::PackedOp, 5> operations{{
        {0xEF000001U,
         oot3d::recomp::a32::EncodeMetadata(
             oot3d::recomp::a32::Opcode::Svc,
             oot3d::recomp::a32::Condition::Al)},
        {0xEF000002U,
         oot3d::recomp::a32::EncodeMetadata(
             oot3d::recomp::a32::Opcode::Svc,
             oot3d::recomp::a32::Condition::Al)},
        {0xEF000003U,
         oot3d::recomp::a32::EncodeMetadata(
             oot3d::recomp::a32::Opcode::Svc,
             oot3d::recomp::a32::Condition::Al)},
        {0xEF000004U,
         oot3d::recomp::a32::EncodeMetadata(
             oot3d::recomp::a32::Opcode::Svc,
             oot3d::recomp::a32::Condition::Al)},
        {0xEF000003U,
         oot3d::recomp::a32::EncodeMetadata(
             oot3d::recomp::a32::Opcode::Svc,
             oot3d::recomp::a32::Condition::Al)},
    }};
    static const std::array<oot3d::recomp::a32::Block, 5> blocks{{
        {kEntry, &operations[0], 1},
        {kEntry + 4U, &operations[1], 1},
        {kEntry + 0x10U, &operations[2], 1},
        {kInvokeEntry, &operations[3], 1},
        {kInvokeEntry + 4U, &operations[4], 1},
    }};
    static const oot3d::recomp::a32::BlockShard shard{
        kEntry, kInvokeEntry + 4U, blocks.data(),
        static_cast<uint32_t>(blocks.size())};
    return {&shard, 1, nullptr, 0};
}

struct NativeEntryProbe {
    uint32_t Calls = 0U;
};

struct SynchronousReturnProbe {
    uint32_t Calls = 0U;
    uint32_t ObservedArgument = 0U;
};

struct OwnerCallProbe {
    Oot3dNativeGame::NativeA32Process* Process = nullptr;
    Oot3dNativeGame::NativeA32OwnerCallAdapter* Adapter = nullptr;
    uint32_t OuterCalls = 0U;
    uint32_t NestedCalls = 0U;
    bool NestedScopeRejected = false;
    bool ArgumentsMatched = false;
    std::string Error;
};

bool ExecuteSynchronousReturn(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus&,
    oot3d::recomp::a32::ExecutionResult* result, void* user) {
    auto& probe = *static_cast<SynchronousReturnProbe*>(user);
    if (pc != kInvokeEntry + 4U || result == nullptr) {
        return false;
    }
    ++probe.Calls;
    probe.ObservedArgument = state.r[0];
    state.r[15] = state.r[14];
    *result = {oot3d::recomp::a32::ExitKind::Branch, state.r[15],
               oot3d::recomp::a32::FallbackReason::None, 0U};
    return true;
}

bool ExecuteOwnerCallProbe(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result, void* user) {
    auto& probe = *static_cast<OwnerCallProbe*>(user);
    if (result == nullptr || probe.Process == nullptr ||
        probe.Adapter == nullptr) {
        return false;
    }
    if (pc == kInvokeEntry) {
        ++probe.NestedCalls;
        uint32_t stack0 = 0U;
        uint32_t stack1 = 0U;
        probe.ArgumentsMatched =
            state.r[0] == 0x11111111U &&
            state.r[1] == 0x22222222U &&
            state.r[2] == 0x33333333U &&
            state.r[3] == 0x44444444U &&
            state.vfp[0] == 0x3F800000U &&
            state.vfp[1] == 0x40000000U &&
            memory.Read32(state.r[13], &stack0) &&
            memory.Read32(state.r[13] + 4U, &stack1) &&
            stack0 == 0x55555555U && stack1 == 0x66666666U;
        state.r[0] = 0xC001C0DEU;
        state.vfp[0] = 0x40400000U;
        state.r[15] = state.r[14];
        *result = {oot3d::recomp::a32::ExitKind::Branch, state.r[15],
                   oot3d::recomp::a32::FallbackReason::None, pc};
        return true;
    }
    if (pc != kEntry) {
        return false;
    }

    ++probe.OuterCalls;
    auto scope = probe.Adapter->BeginExecution(kEntry, state, &probe.Error);
    if (!scope) {
        return false;
    }
    auto nestedScope =
        probe.Adapter->BeginExecution(kInvokeEntry, state, nullptr);
    probe.NestedScopeRejected = !nestedScope;

    constexpr std::array<uint32_t, 2> stackArguments{
        0x55555555U,
        0x66666666U,
    };
    Oot3dNativeGame::NativeA32OwnerGuestCall call;
    call.EntryAddress = kInvokeEntry;
    call.ReturnAddress = kInvokeReturn;
    call.CallerFrameSize = 0x40U;
    call.CoreArguments = {
        0x11111111U,
        0x22222222U,
        0x33333333U,
        0x44444444U,
    };
    call.CoreArgumentCount = call.CoreArguments.size();
    call.VfpArguments[0] = 0x3F800000U;
    call.VfpArguments[1] = 0x40000000U;
    call.VfpArgumentCount = 2U;
    call.StackArguments = stackArguments;
    Oot3dNativeGame::NativeA32OwnerGuestCallResult callResult;
    if (!probe.Adapter->Invoke(call, &callResult, &probe.Error)) {
        return false;
    }
    state.r[0] = callResult.State.r[0];
    state.vfp[0] = callResult.State.vfp[0];
    scope.Reset();
    state.r[15] = kEntry + 0x10U;
    *result = {oot3d::recomp::a32::ExitKind::Branch, state.r[15],
               oot3d::recomp::a32::FallbackReason::None, pc};
    return true;
}

void TestSynchronousOriginalFunctionInvocation() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 0x30> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map synchronous-call text");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0, 0, 0, 0},
               &error),
           "create synchronous-call thread");
    SynchronousReturnProbe probe;
    process.SetNativeFunctionCallback(&ExecuteSynchronousReturn, &probe,
                                      {kInvokeEntry + 4U});
    constexpr std::array<uint32_t, 1> arguments{0xA5A50000U};
    uint32_t returnValue = 0U;

    Expect(process.InvokeFunctionWithResult(
               kInvokeEntry, arguments, kInvokeReturn, &returnValue, &error),
           "synchronous original function did not return");
    Expect(probe.Calls == 1U &&
               probe.ObservedArgument == (arguments[0] ^ 0x00FF00FFU) &&
               returnValue == (arguments[0] ^ 0x00FF00FFU) &&
               process.PrimaryThreadState().r[0] == 0U &&
               process.PrimaryThreadState().r[15] == kEntry,
           "synchronous original function did not isolate register state");
}

void TestSynchronousFiniteWaitCompletion() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    services.WaitOnSvc4 = true;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 0x30> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map synchronous-wait text");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0, 0, 0, 0},
               &error),
           "create synchronous-wait thread");
    SynchronousReturnProbe probe;
    process.SetNativeFunctionCallback(&ExecuteSynchronousReturn, &probe,
                                      {kInvokeEntry + 4U});
    constexpr std::array<uint32_t, 1> arguments{0xA5A50000U};
    uint32_t returnValue = 0U;

    Expect(!process.InvokeFunctionWithResult(
               kInvokeEntry, arguments, kInvokeReturn, &returnValue,
               &error) &&
               error.find("test synchronous wait cannot complete") !=
                   std::string::npos &&
               services.SynchronousWaitCompletionAttempts == 1U &&
               services.SynchronousWaitCompletions == 0U &&
               probe.Calls == 0U,
           "unresolved synchronous wait did not fail closed");

    services.CompleteSvc4Wait = true;
    error.clear();
    Expect(process.InvokeFunctionWithResult(
               kInvokeEntry, arguments, kInvokeReturn, &returnValue,
               &error),
           "finite synchronous wait did not resume guest execution");
    Expect(
        error.empty() && probe.Calls == 1U &&
            returnValue == (arguments[0] ^ 0x00FF00FFU) &&
            services.SynchronousWaitCompletionAttempts == 2U &&
            services.SynchronousWaitCompletions == 1U &&
            process.PrimaryThreadState().r[15] == kEntry,
        "finite synchronous wait did not preserve invocation state");
}

void TestSynchronousReturnAtRegisteredBlock() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 0x30> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map registered-return text");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0, 0, 0, 0},
               &error),
           "create registered-return thread");
    SynchronousReturnProbe probe;
    process.SetNativeFunctionCallback(&ExecuteSynchronousReturn, &probe,
                                      {kInvokeEntry + 4U});
    constexpr std::array<uint32_t, 1> arguments{0x01020304U};
    uint32_t returnValue = 0U;

    Expect(process.InvokeFunctionWithResult(
               kInvokeEntry, arguments, kEntry + 0x10U, &returnValue,
               &error),
           "synchronous invocation ran through a registered return block");
    Expect(probe.Calls == 1U && services.SvcCount == 1U &&
               services.FirstSvc == 4U &&
               returnValue == (arguments[0] ^ 0x00FF00FFU),
           "registered return stop did not preserve callback composition");
}

void TestSourceOwnerCallAdapterReentrantDispatch() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 0x30> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map source-owner text");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0, 0, 0, 0},
               &error),
           "create source-owner thread");

    Oot3dNativeGame::NativeA32OwnerCallAdapter adapter(process);
    OwnerCallProbe probe{&process, &adapter};
    process.SetNativeFunctionCallback(
        &ExecuteOwnerCallProbe, &probe, {kEntry, kInvokeEntry});

    const auto run = process.Run();
    Expect(run.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Terminated,
           "source-owner outer dispatch did not terminate");
    Expect(probe.Error.empty() && probe.OuterCalls == 1U &&
               probe.NestedCalls == 1U && probe.NestedScopeRejected &&
               probe.ArgumentsMatched && !adapter.IsActive(),
           "source-owner nested dispatch contract mismatch");
    Expect(process.PrimaryThreadState().r[0] == 0xC001C0DEU &&
               process.PrimaryThreadState().vfp[0] == 0x40400000U,
           "source-owner nested return values were not propagated");

    const auto* stackPointer = process.Memory().GetReadPointer(
        process.PrimaryThreadState().r[13] - 0x40U, 8U);
    Expect(stackPointer != nullptr &&
               adapter.ResolveRead(kEntry) == nullptr &&
               !adapter.ResolveGuestAddress(stackPointer).has_value(),
           "inactive source-owner adapter exposed guest memory");
}

void TestSourceOwnerCallAdapterPerCallBudget() {
    const auto registry = MakeTestRegistry();
    OwnerTransitionBudgetHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 0x30> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map owner-budget text");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0, 0, 0, 0},
               &error),
           "create owner-budget thread");

    Oot3dNativeGame::NativeA32OwnerCallAdapter adapter(process);
    auto scope = adapter.BeginExecution(
        kEntry, process.PrimaryThreadState(), &error);
    Expect(static_cast<bool>(scope), "begin owner-budget execution");

    Oot3dNativeGame::NativeA32OwnerGuestCall call;
    call.EntryAddress = kInvokeEntry;
    call.ReturnAddress = kInvokeReturn;
    Oot3dNativeGame::NativeA32OwnerGuestCallResult result;
    Expect(!adapter.Invoke(call, &result, &error) &&
               error == "native A32 function exceeded host-transition limit",
           "default owner-call budget did not stop a long dispatch");

    services.Calls = 0U;
    call.Limits.MaxHostTransitions = 2048U;
    error.clear();
    Expect(adapter.Invoke(call, &result, &error) &&
               services.Calls == services.CallsBeforeReturn,
           "explicit owner-call transition budget was not honored");

    call.Limits.MaxHostTransitions = 0U;
    error.clear();
    Expect(!adapter.Invoke(call, &result, &error) &&
               error == "native A32 owner call limits must be nonzero",
           "owner-call adapter accepted a zero transition budget");
}

bool ExecuteNativeEntryProbe(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus&,
    oot3d::recomp::a32::ExecutionResult* result, void* user) {
    auto& probe = *static_cast<NativeEntryProbe*>(user);
    if (pc != kEntry || result == nullptr) {
        return false;
    }
    ++probe.Calls;
    state.r[0] = 0xC001C0DEU;
    *result = {oot3d::recomp::a32::ExitKind::Branch, kEntry + 0x10U,
               oot3d::recomp::a32::FallbackReason::None, 0U};
    return true;
}

void TestNativeFunctionAtDispatchEntry() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 0x20> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map native-entry text");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0, 0, 0, 0},
               &error),
           "create native-entry thread");
    NativeEntryProbe probe;
    process.SetNativeFunctionCallback(
        &ExecuteNativeEntryProbe, &probe, {kEntry});

    const auto run = process.Run();
    Expect(run.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Terminated &&
               probe.Calls == 1U && services.SvcCount == 1U &&
               services.FirstSvc == 3U &&
               process.PrimaryThreadState().r[0] == 0xC001C0DEU,
           "dispatch-entry native function did not bypass packed execution");
}

void TestPrioritySchedulerAndTls() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 0x20> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map scheduler text");
    Expect(process.MapRegion({"data", kData, 0x100U, true, false, {}},
                             &error),
           "map scheduler data");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x1000U, 0,
                0, 0x10U, 0x03C00010U, 48U},
               &error),
           "create scheduler primary thread");
    const auto worker = process.CreateThread(
        {kEntry + 0x10U, 0x11223344U, kData + 0x100U,
         0x10U, 0x03C00000U, 24U},
        &error);
    Expect(worker == 1U && process.ThreadCount() == 2U,
           "create secondary guest thread");
    const auto* workerState = process.ThreadState(*worker);
    Expect(workerState != nullptr && workerState->r[0] == 0x11223344U &&
               workerState->r[13] == kData + 0x100U &&
               workerState->thread_pointer == kTls + 0x200U,
           "secondary registers, stack and TLS initialization");

    const auto result = process.Run();
    Expect(services.FirstSvc == 3U &&
               process.ThreadStatus(*worker) ==
                   Oot3dNativeGame::NativeA32ThreadStatus::Terminated &&
               result.Kind ==
                   Oot3dNativeGame::NativeA32ProcessRunKind::Waiting,
           "higher-priority worker must run before the primary thread");
}

void TestProcessStateRoundTrip() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 0x20> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error) &&
               process.MapRegion(
                   {"data", kData, 0x100U, true, false, {}}, &error) &&
               process.CreatePrimaryThread(
                   {kEntry, kStack, 0x1000U, kTls, 0x1000U, 0,
                    0, 0x10U, 0x03C00010U, 48U},
                   &error),
           "create state round-trip process");
    const auto waiting = process.Run();
    Expect(waiting.Kind ==
               Oot3dNativeGame::NativeA32ProcessRunKind::Waiting,
           "state round-trip process did not reach wait boundary");

    const auto encoded = process.CaptureState();
    const auto bytes = nlohmann::json::to_msgpack(encoded);
    const auto decoded = nlohmann::json::from_msgpack(bytes);
    process.PrimaryThreadState().r[0] = 0U;
    Expect(process.Memory().Write32(kData, 0U) &&
               process.ResumePrimaryThread(std::nullopt, &error) &&
               process.Run().Kind ==
                   Oot3dNativeGame::NativeA32ProcessRunKind::Terminated,
           "mutate state round-trip process");
    Expect(process.RestoreState(decoded, &error), error.c_str());
    uint32_t restoredData = 0U;
    Expect(process.PrimaryThreadStatus() ==
               Oot3dNativeGame::NativeA32ThreadStatus::Waiting &&
               process.PrimaryThreadState().r[0] == 0x12345678U &&
               process.Memory().Read32(kData, &restoredData) &&
               restoredData == 0xA5A55A5AU,
           "process state did not restore registers, scheduler and memory");
    Expect(process.ResumePrimaryThread(std::nullopt, &error) &&
               process.Run().Kind ==
                   Oot3dNativeGame::NativeA32ProcessRunKind::Terminated &&
               services.StatePersisted,
           "restored process did not resume deterministically");
}

void TestMemoryContract() {
    Oot3dNativeGame::NativeA32Memory memory;
    const std::array<uint8_t, 4> initial{1, 2, 3, 4};
    std::string error;
    Expect(memory.MapRegion(
               {"readonly", 0x1000U, 0x100U, false, true, initial}, &error),
           "map read-only executable region");
    Expect(memory.MapRegion({"data", 0x2000U, 0x100U, true, false, {}},
                            &error),
           "map writable data region");
    Expect(memory.MapRegion({"fast", 0x4000U, 0x2000U, true, false, {}},
                            &error),
           "map page-aligned fast memory region");
    Expect(!memory.MapRegion({"overlap", 0x2080U, 0x100U, true, false, {}},
                             &error),
           "overlapping map must fail");
    uint32_t value = 0;
    Expect(memory.Read32(0x1000U, &value) && value == 0x04030201U,
           "initial bytes must be copied little-endian");
    Expect(!memory.Write32(0x1000U, 7U),
           "read-only guest memory must reject writes");
    Expect(memory.WriteHost<uint32_t>(0x1000U, 0x55667788U) &&
               memory.Read32(0x1000U, &value) && value == 0x55667788U &&
               !memory.Write32(0x1000U, 7U),
           "host-owned shared memory must remain writable without relaxing guest protection");
    Expect(memory.Read32(0x2000U, &value) && value == 0,
           "uninitialized guest data must be zero-filled");

    const auto* readOnlyPointer = memory.GetReadPointer(0x1002U, 2U);
    const auto* writablePointer = memory.GetReadPointer(0x20FFU, 1U);
    const uint32_t unrelatedStorage = 0U;
    Expect(readOnlyPointer != nullptr &&
               memory.GetGuestAddress(readOnlyPointer, 2U) == 0x1002U &&
               writablePointer != nullptr &&
               memory.GetGuestAddress(writablePointer, 1U) == 0x20FFU &&
               !memory.GetGuestAddress(writablePointer, 2U).has_value() &&
               !memory.GetGuestAddress(nullptr).has_value() &&
               !memory.GetGuestAddress(readOnlyPointer, 0U).has_value() &&
               !memory.GetGuestAddress(&unrelatedStorage).has_value(),
           "host-to-guest pointer mapping must preserve region bounds");

    Expect(memory.WriteFast<uint32_t>(0x4123U, 0xA1B2C3D4U) &&
               memory.ReadFast<uint32_t>(0x4123U, &value) &&
               value == 0xA1B2C3D4U,
           "non-virtual fast memory must preserve unaligned little-endian IO");
    uint16_t crossPage = 0;
    Expect(memory.WriteFast<uint16_t>(0x4FFFU, 0x7788U) &&
               memory.ReadFast<uint16_t>(0x4FFFU, &crossPage) &&
               crossPage == 0x7788U,
           "fast memory must preserve checked cross-page IO");
    Expect(!memory.WriteFast<uint32_t>(0x1000U, 7U),
           "fast memory must preserve read-only protection");

    Oot3dNativeGame::NativeA32Memory copiedMemory = memory;
    Expect(copiedMemory.WriteFast<uint32_t>(0x4123U, 0x11223344U) &&
               copiedMemory.ReadFast<uint32_t>(0x4123U, &value) &&
               value == 0x11223344U,
           "copied fast memory must rebuild its host page view");
    Expect(memory.ReadFast<uint32_t>(0x4123U, &value) &&
               value == 0xA1B2C3D4U,
           "copied fast memory must not alias its source storage");
    const auto* copiedPointer = copiedMemory.GetReadPointer(0x4123U, 4U);
    Expect(copiedPointer != nullptr &&
               copiedMemory.GetGuestAddress(copiedPointer, 4U) == 0x4123U &&
               !memory.GetGuestAddress(copiedPointer, 4U).has_value(),
           "copied memory must reverse-map only its own backing storage");

    Expect(memory.Write32(0x2000U, 11U), "write exclusive seed");
    uint64_t loaded = 0;
    uint64_t token = 0;
    Expect(memory.LoadExclusive(0x2000U, 4, &loaded, &token, nullptr) &&
               loaded == 11U,
           "load-exclusive contract");
    Expect(memory.Write32(0x2004U, 12U), "intervening guest write");
    Expect(memory.StoreExclusive(0x2000U, 4, 13U, token, nullptr) ==
               oot3d::recomp::a32::ExclusiveStoreResult::ReservationLost,
           "intervening write must invalidate exclusive token");
}

#if defined(OOT3D_NATIVE_A32_AOT_TESTS)
void TestWholeAotShiftAndFlagSemantics() {
    using Oot3dNativeGame::Oot3dAotShiftImmediate;
    using Oot3dNativeGame::Oot3dAotShiftRegister;
    const auto lslZero = Oot3dAotShiftImmediate(0x80000001U, 0U, 0U, true);
    const auto lsr32 = Oot3dAotShiftImmediate(0x80000001U, 1U, 0U, false);
    const auto asr32 = Oot3dAotShiftImmediate(0x80000001U, 2U, 0U, false);
    const auto rrx = Oot3dAotShiftImmediate(0x00000003U, 3U, 0U, true);
    const auto lslRegister32 =
        Oot3dAotShiftRegister(0x80000001U, 0U, 32U, false);
    const auto rorRegister32 =
        Oot3dAotShiftRegister(0x80000001U, 3U, 32U, false);
    Expect(lslZero.Value == 0x80000001U && lslZero.Carry &&
               lsr32.Value == 0U && lsr32.Carry &&
               asr32.Value == 0xFFFFFFFFU && asr32.Carry &&
               rrx.Value == 0x80000001U && rrx.Carry &&
               lslRegister32.Value == 0U && lslRegister32.Carry &&
               rorRegister32.Value == 0x80000001U && rorRegister32.Carry,
           "whole-AOT ARM shifter edge semantics differ");

    oot3d::recomp::a32::GuestState guestState{};
    Oot3dNativeGame::Oot3dWholeAotFrame frame(guestState);
    frame.Guest.cpsr = oot3d::recomp::a32::kFlagV;
    Oot3dNativeGame::Oot3dAotSetLogicalFlags(frame, 0U, true);
    Expect((frame.Guest.cpsr & oot3d::recomp::a32::kFlagZ) != 0U &&
               (frame.Guest.cpsr & oot3d::recomp::a32::kFlagC) != 0U &&
               (frame.Guest.cpsr & oot3d::recomp::a32::kFlagV) != 0U,
           "whole-AOT logical flags did not preserve V");
    Expect(guestState.cpsr == frame.Guest.cpsr,
           "whole-AOT frame must update canonical guest state directly");
    const uint32_t sum = Oot3dNativeGame::Oot3dAotAddWithCarry(
        frame, 0x7FFFFFFFU, 0U, true, true);
    Expect(sum == 0x80000000U &&
               (frame.Guest.cpsr & oot3d::recomp::a32::kFlagN) != 0U &&
               (frame.Guest.cpsr & oot3d::recomp::a32::kFlagV) != 0U &&
               (frame.Guest.cpsr & oot3d::recomp::a32::kFlagC) == 0U,
           "whole-AOT add-with-carry flags differ");
}

#if defined(OOT3D_NATIVE_WHOLE_AOT_TESTS)
void TestWholeAotNativeCurveDifferential() {
    using oot3d::recomp::a32::ExitKind;
    constexpr uint32_t kEntry = 0x003087A4U;
    constexpr uint32_t kReturn = 0x60000000U;
    constexpr uint32_t kCurveReference = 0x00010000U;
    constexpr uint32_t kCurve = 0x00011000U;
    constexpr uint32_t kStackTop = 0x00021000U;

    Oot3dNativeGame::NativeA32Memory armMemory;
    std::string error;
    Expect(armMemory.MapRegion(
               {"curve", 0x00010000U, 0x2000U, true, false, {}}, &error) &&
               armMemory.MapRegion(
                   {"stack", 0x00020000U, 0x2000U, true, false, {}}, &error),
           "map whole-AOT curve differential memory");
    Expect(armMemory.Write32(kCurveReference, kCurve) &&
               armMemory.Write32(kCurveReference + 4U, 0U) &&
               armMemory.Write8(kCurve, 1U) &&
               armMemory.Write32(kCurve + 4U, 2U) &&
               armMemory.Write32(kCurve + 0x10U, 0U) &&
               armMemory.Write32(kCurve + 0x14U, 0x41200000U) &&
               armMemory.Write32(kCurve + 0x18U, 10U) &&
               armMemory.Write32(kCurve + 0x1CU, 0x41A00000U),
           "seed whole-AOT native linear curve");
    auto aotMemory = armMemory;

    oot3d::recomp::a32::GuestState armState{};
    armState.r[0] = kCurveReference;
    armState.r[4] = 0x44444444U;
    armState.r[5] = 0x55555555U;
    armState.r[6] = 0x66666666U;
    armState.r[13] = kStackTop;
    armState.r[14] = kReturn;
    armState.vfp[0] = 0x40A00000U;
    auto aotState = armState;

    const auto armResult = oot3d::recomp::a32::Dispatch(
        oot3d::recomp::GetA32GeneratedRegistry(), kEntry, armState,
        armMemory, nullptr, nullptr, 1000U);
    Expect(armResult.kind == ExitKind::MissingBlock &&
               armResult.pc == kReturn,
           "ARM native curve must return to the differential sentinel");

    oot3d::recomp::a32::ExecutionResult aotResult{};
    Oot3dNativeGame::ResetOot3dCompiledFunctionStats();
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               kEntry, aotState, aotMemory, &aotResult, nullptr) &&
               aotResult.kind == ExitKind::Branch &&
               aotResult.pc == kReturn,
           "whole-function AOT native curve must return directly");
    Expect(aotState.r == armState.r && aotState.cpsr == armState.cpsr &&
               aotState.fpscr == armState.fpscr &&
               aotState.vfp == armState.vfp &&
               aotState.thread_pointer == armState.thread_pointer &&
               aotState.exclusive_address == armState.exclusive_address &&
               aotState.exclusive_token == armState.exclusive_token &&
               aotState.exclusive_size == armState.exclusive_size &&
               aotState.exclusive_valid == armState.exclusive_valid &&
               aotState.vfp[0] == 0x41700000U,
           "whole-function AOT native curve state must match original ARM");

    std::array<uint8_t, 12> armStack{};
    std::array<uint8_t, 12> aotStack{};
    Expect(armMemory.ReadBytes(kStackTop - 12U, armStack) &&
               aotMemory.ReadBytes(kStackTop - 12U, aotStack) &&
               armStack == aotStack,
           "whole-function AOT stack effects must match original ARM");
    const auto stats = Oot3dNativeGame::GetOot3dCompiledFunctionStats();
    Expect(stats.WholeAotCalls == 1U &&
               stats.WholeAotMemoryFaults == 0U &&
               stats.WholeAotUnsupportedExits == 0U,
           "whole-function AOT differential must use the new backend");
}

void TestWholeAotAllocatorLeafDifferential() {
    using oot3d::recomp::a32::ExitKind;
    constexpr uint32_t kEntry = 0x00477D30U;
    constexpr uint32_t kReturn = 0x60000000U;
    constexpr uint32_t kOwner = 0x00010000U;
    constexpr uint32_t kNode = 0x00012000U;
    constexpr uint32_t kNext = 0x00013000U;
    constexpr uint32_t kStackTop = 0x00021000U;
    constexpr std::array<uint8_t, 4> kLiteral{0x73U, 0x73U, 0U, 0U};

    Oot3dNativeGame::NativeA32Memory armMemory;
    std::string error;
    Expect(armMemory.MapRegion(
               {"allocator", 0x00010000U, 0x4000U, true, false, {}},
               &error) &&
               armMemory.MapRegion(
                   {"stack", 0x00020000U, 0x2000U, true, false, {}},
                   &error) &&
               armMemory.MapRegion(
                   {"allocator_literal", 0x00477E2CU, kLiteral.size(),
                    false, false, kLiteral},
                   &error),
           "map whole-AOT allocator differential memory");
    Expect(armMemory.Write32(kOwner, kNode) &&
               armMemory.Write16(kNode + 2U, 1U) &&
               armMemory.Write32(kNode + 4U, 0x100U) &&
               armMemory.Write32(kNode + 8U, kNext) &&
               armMemory.Write16(kNext, 0x7373U),
           "seed whole-AOT allocator free list");
    auto aotMemory = armMemory;

    oot3d::recomp::a32::GuestState armState{};
    armState.r[0] = kOwner;
    armState.r[1] = 0x20U;
    armState.r[4] = 0x44444444U;
    armState.r[5] = 0x55555555U;
    armState.r[13] = kStackTop;
    armState.r[14] = kReturn;
    auto aotState = armState;

    const auto armResult = oot3d::recomp::a32::Dispatch(
        oot3d::recomp::GetA32GeneratedRegistry(), kEntry, armState,
        armMemory, nullptr, nullptr, 1000U);
    Expect(armResult.kind == ExitKind::MissingBlock &&
               armResult.pc == kReturn,
           "ARM allocator leaf must return to the differential sentinel");

    oot3d::recomp::a32::ExecutionResult aotResult{};
    Oot3dNativeGame::ResetOot3dCompiledFunctionStats();
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               kEntry, aotState, aotMemory, &aotResult, nullptr) &&
               aotResult.kind == ExitKind::Branch &&
               aotResult.pc == kReturn,
           "whole-function AOT allocator leaf must return directly");
    Expect(aotState.r == armState.r && aotState.cpsr == armState.cpsr &&
               aotState.fpscr == armState.fpscr &&
               aotState.vfp == armState.vfp &&
               aotState.r[0] == kNode + 0x10U,
           "whole-function AOT allocator state must match original ARM");

    std::array<uint8_t, 0x1100> armData{};
    std::array<uint8_t, 0x1100> aotData{};
    std::array<uint8_t, 8> armStack{};
    std::array<uint8_t, 8> aotStack{};
    Expect(armMemory.ReadBytes(kNode, armData) &&
               aotMemory.ReadBytes(kNode, aotData) &&
               armData == aotData &&
               armMemory.ReadBytes(kStackTop - armStack.size(), armStack) &&
               aotMemory.ReadBytes(kStackTop - aotStack.size(), aotStack) &&
               armStack == aotStack,
           "whole-function AOT allocator memory effects must match ARM");
    const auto stats = Oot3dNativeGame::GetOot3dCompiledFunctionStats();
    Expect(stats.WholeAotCalls == 1U &&
               stats.WholeAotMemoryFaults == 0U &&
               stats.WholeAotUnsupportedExits == 0U,
           "allocator differential must use the whole-function backend");
}
#endif
#endif

void TestPersistentProcess() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 8> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map process text");
    Expect(process.MapRegion({"data", kData, 0x100U, true, false, {}},
                             &error),
           "map process data");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0,
                0x55U, 0x10U, 0x03C00010U}, &error),
           "create persistent primary thread");
    Expect(process.PrimaryThreadState().r[13] == kStack + 0x1000U &&
               process.PrimaryThreadState().thread_pointer == kTls &&
               process.PrimaryThreadState().r[0] == 0x55U &&
               process.PrimaryThreadState().cpsr == 0x10U &&
               process.PrimaryThreadState().fpscr == 0x03C00010U,
           "register, stack and TLS initialization");

    const auto first = process.Run();
    Expect(first.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Waiting &&
               process.PrimaryThreadStatus() ==
                   Oot3dNativeGame::NativeA32ThreadStatus::Waiting &&
               process.PrimaryThreadState().r[15] == kEntry + 4U,
           "first SVC must suspend at its continuation PC");
    Expect(process.ResumePrimaryThread(std::nullopt, &error),
           "resume persistent primary thread");
    const auto second = process.Run();
    Expect(second.Kind ==
               Oot3dNativeGame::NativeA32ProcessRunKind::Terminated &&
               services.SvcCount == 2 && services.StatePersisted,
           "register and memory state must persist across wait/resume");
}

void TestFallbackBoundary() {
    const oot3d::recomp::a32::Registry emptyRegistry{};
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(emptyRegistry, services);
    const std::array<uint8_t, 4> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map fallback text");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0, 0, 0, 0},
               &error),
           "create fallback thread");
    const auto result = process.Run();
    Expect(result.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Faulted &&
               services.FallbackCount == 1 &&
               result.Error == "expected test fallback fault",
           "missing block must cross the typed host boundary and fail closed");
}

struct BlockEntryProbe {
    uint32_t Calls = 0;
};

void CountBlockEntry(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                     oot3d::recomp::a32::MemoryBus& memory, void* user) {
    static_cast<void>(state);
    static_cast<void>(memory);
    auto& probe = *static_cast<BlockEntryProbe*>(user);
    if (pc == kEntry) {
        ++probe.Calls;
    }
}

void TestBlockEntryCallback() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 8> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map block-entry text");
    Expect(process.MapRegion({"data", kData, 0x100U, true, false, {}},
                             &error),
           "map block-entry data");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0, 0, 0, 0},
               &error),
           "create block-entry thread");
    BlockEntryProbe probe;
    process.SetBlockEntryCallback(&CountBlockEntry, &probe, {kEntry});
    const auto result = process.Run();
    Expect(result.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Waiting &&
               probe.Calls == 1U,
           "block-entry callback must observe the dispatched guest block");
}

void TestBlockEntryCallbackFilter() {
    const auto registry = MakeTestRegistry();
    TestHostServices services;
    Oot3dNativeGame::NativeA32Process process(registry, services);
    const std::array<uint8_t, 8> code{};
    std::string error;
    Expect(process.MapRegion(
               {"text", kEntry, code.size(), false, true, code}, &error),
           "map filtered block-entry text");
    Expect(process.MapRegion({"data", kData, 0x100U, true, false, {}},
                             &error),
           "map filtered block-entry data");
    Expect(process.CreatePrimaryThread(
               {kEntry, kStack, 0x1000U, kTls, 0x100U, 0, 0, 0, 0},
               &error),
           "create filtered block-entry thread");
    BlockEntryProbe probe;
    process.SetBlockEntryCallback(&CountBlockEntry, &probe, {kEntry + 4U});
    const auto result = process.Run();
    Expect(result.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Waiting &&
               probe.Calls == 0U,
           "block-entry callback must ignore blocks outside its filter");
}

void TestCompiledRuntimeMemcpy() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Expect(memory.MapRegion(
               {"copy", 0x1000U, 0x200U, true, false, {}}, &error),
           error.c_str());
    const std::array<uint8_t, 16> source{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    Expect(memory.WriteBytes(0x1000U, source),
           "compiled memcpy source write failed");
    oot3d::recomp::a32::GuestState state{};
    state.r[0] = 0x1080U;
    state.r[1] = 0x1000U;
    state.r[2] = static_cast<uint32_t>(source.size());
    state.r[14] = 0x2000U;
    oot3d::recomp::a32::ExecutionResult result{};
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x00371738U, state, memory, &result, nullptr),
           "compiled memcpy was not handled");
    std::array<uint8_t, 16> copied{};
    Expect(memory.ReadBytes(0x1080U, copied),
           "compiled memcpy destination read failed");
    Expect(copied == source, "compiled memcpy payload differs");
    Expect(state.r[0] == 0x1090U && state.r[1] == 0x1010U &&
               result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == 0x2000U,
           "compiled memcpy ABI result differs");

    std::array<uint8_t, 16> aliasBefore{};
    Expect(memory.ReadBytes(0x1000U, aliasBefore),
           "compiled memcpy alias source read failed");
    state.r[0] = 0x1000U;
    state.r[1] = 0x1000U;
    state.r[2] = 16U;
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x00371738U, state, memory, &result, nullptr),
           "same-address memcpy was not handled");
    std::array<uint8_t, 16> aliasAfter{};
    Expect(memory.ReadBytes(0x1000U, aliasAfter) && aliasAfter == aliasBefore &&
               state.r[0] == 0x1010U && state.r[1] == 0x1010U &&
               result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == 0x2000U,
           "same-address memcpy ABI result differs");

    state.r[0] = 0x1004U;
    state.r[1] = 0x1000U;
    state.r[2] = 16U;
    Expect(!Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x00371738U, state, memory, &result, nullptr),
           "overlapping memcpy must retain the ARM fallback");
}

void TestCompiledPicaRegisterRangeWriter() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    const std::array<uint8_t, 4> headerLiteral{0x00, 0x00, 0x0F, 0x00};
    Expect(memory.MapRegion(
               {"literal", 0x00307C7CU, headerLiteral.size(), false, true,
                headerLiteral},
               &error),
           error.c_str());
    Expect(memory.MapRegion(
               {"data", 0x1000U, 0x1000U, true, false, {}}, &error),
           error.c_str());
    Expect(memory.Write32(0x1008U, 0x1400U), "seed PICA writer cursor");
    Expect(memory.Write32(0x1100U, 0x01020304U) &&
               memory.Write32(0x1104U, 0x11121314U) &&
               memory.Write32(0x1108U, 0x21222324U),
           "seed PICA register values");
    Expect(memory.Write32(0x1200U, 0x5U) &&
               memory.Write32(0x1204U, 0x1100U),
           "seed stacked PICA writer arguments");

    oot3d::recomp::a32::GuestState state{};
    state.r[0] = 0x1000U;
    state.r[1] = 0x82U;
    state.r[2] = 3U;
    state.r[3] = 1U;
    state.r[13] = 0x1200U;
    state.r[14] = 0x2000U;
    oot3d::recomp::a32::ExecutionResult result{};
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x00307BD8U, state, memory, &result, nullptr),
           "compiled PICA register writer was not handled");
    std::array<uint32_t, 4> words{};
    for (size_t index = 0; index < words.size(); ++index) {
        Expect(memory.Read32(0x1400U + static_cast<uint32_t>(index * 4U),
                             &words[index]),
               "read compiled PICA command word");
    }
    const uint32_t expectedHeader =
        0x000F0000U + (3U << 20U) | 0x82U | (5U << 16U) |
        0x80000000U;
    Expect(words == std::array<uint32_t, 4>{
                        0x01020304U, expectedHeader,
                        0x11121314U, 0x21222324U},
           "compiled PICA command payload differs");
    uint32_t cursor = 0U;
    Expect(memory.Read32(0x1008U, &cursor) && cursor == 0x1410U &&
               result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == 0x2000U,
           "compiled PICA writer cursor or ABI result differs");
}

void TestCompiledPicaVertexFloatUniformWriter() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    const std::array<uint8_t, 4> configLiteral{0x21, 0x43, 0x65, 0x87};
    const std::array<uint8_t, 4> dataLiteral{0xBA, 0x0C, 0x00, 0x00};
    Expect(memory.MapRegion(
               {"config_literal", 0x00307D84U, configLiteral.size(), false,
                true, configLiteral},
               &error),
           error.c_str());
    Expect(memory.MapRegion(
               {"data_literal", 0x00307D88U, dataLiteral.size(), false, true,
                dataLiteral},
               &error),
           error.c_str());
    Expect(memory.MapRegion(
               {"data", 0x1000U, 0x1000U, true, false, {}}, &error),
           error.c_str());
    Expect(memory.Write32(0x1008U, 0x1400U),
           "seed float-uniform writer cursor");
    for (uint32_t index = 0U; index < 8U; ++index) {
        Expect(memory.Write32(0x1100U + index * 4U, 0x10000000U + index),
               "seed float-uniform input");
    }

    oot3d::recomp::a32::GuestState state{};
    state.r[0] = 0x1000U;
    state.r[1] = 0x2C0U;
    state.r[2] = 2U;
    state.r[3] = 0x1100U;
    state.r[14] = 0x2000U;
    oot3d::recomp::a32::ExecutionResult result{};
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x00307C94U, state, memory, &result, nullptr),
           "compiled float-uniform writer was not handled");

    const std::array<uint32_t, 12> expected{
        0x800002C0U, 0x87654321U, 0x10000003U,
        (7U << 20U) | 0x00000CBAU, 0x10000002U, 0x10000001U,
        0x10000000U, 0x10000007U, 0x10000006U, 0x10000005U,
        0x10000004U, 0U,
    };
    std::array<uint32_t, expected.size()> words{};
    for (size_t index = 0; index < words.size(); ++index) {
        Expect(memory.Read32(0x1400U + static_cast<uint32_t>(index * 4U),
                             &words[index]),
               "read compiled float-uniform command word");
    }
    uint32_t cursor = 0U;
    Expect(words == expected && memory.Read32(0x1008U, &cursor) &&
               cursor == 0x1430U &&
               result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == 0x2000U,
           "compiled float-uniform command or ABI result differs");
}

void TestCompiledPicaMaterialFramebufferAccess() {
    struct TestCase {
        uint16_t Type;
        uint16_t Flags;
        uint8_t AccessMode;
        uint8_t AccessOverride;
        uint8_t ColorAccess;
        uint8_t DepthAccess;
        std::array<uint32_t, 6> Expected;
    };
    const std::array cases{
        TestCase{0x7000U, 0U, 0U, 0U, 0U, 0U,
                 {0xFU, 0x803F0112U, 0xFU, 0U, 0U, 0U}},
        TestCase{0x6030U, 0U, 0U, 0U, 0U, 0U,
                 {0U, 0x803F0112U, 0U, 0U, 0U, 0U}},
        TestCase{0x6030U, 0xFU, 0U, 0U, 1U, 0U,
                 {0U, 0x803F0112U, 0xFU, 0x2U, 0U, 0U}},
        TestCase{0x6030U, 0x1U, 0U, 0U, 0U, 0U,
                 {0xFU, 0x803F0112U, 0xFU, 0U, 0U, 0U}},
        TestCase{0x6030U, 0xFU, 0U, 1U, 0U, 0U,
                 {0xFU, 0x803F0112U, 0xFU, 0U, 0U, 0U}},
        TestCase{0x6030U, 0x1U, 1U, 0U, 1U, 1U,
                 {0xFU, 0x803F0112U, 0xFU, 0x2U, 0x2U, 0U}},
        TestCase{0x6051U, 0U, 0U, 0U, 0U, 0U,
                 {0xFU, 0x803F0112U, 0xFU, 0x2U, 0U, 0U}},
    };

    for (size_t caseIndex = 0U; caseIndex < cases.size(); ++caseIndex) {
        constexpr uint32_t materialAddress = 0x1000U;
        constexpr uint32_t ownerAddress = 0x1100U;
        const uint32_t commandAddress =
            0x1200U + static_cast<uint32_t>(caseIndex * 0x40U);
        Oot3dNativeGame::NativeA32Memory memory;
        std::string error;
        Expect(memory.MapRegion(
                   {"data", 0x1000U, 0x1000U, true, false, {}}, &error),
               error.c_str());
        const auto& test = cases[caseIndex];
        Expect(memory.Write32(materialAddress, ownerAddress) &&
                   memory.Write16(materialAddress + 0x0CU, test.Type) &&
                   memory.Write16(materialAddress + 0x0EU, test.Flags) &&
                   memory.Write8(materialAddress + 0x10U, test.AccessMode) &&
                   memory.Write8(materialAddress + 0x11U,
                                 test.AccessOverride) &&
                   memory.Write8(materialAddress + 0x12U,
                                 test.ColorAccess) &&
                   memory.Write8(materialAddress + 0x13U,
                                 test.DepthAccess) &&
                   memory.Write32(ownerAddress + 8U, commandAddress),
               "seed framebuffer-access material state");

        oot3d::recomp::a32::GuestState state{};
        state.r[0] = materialAddress;
        state.r[14] = 0x3000U;
        oot3d::recomp::a32::ExecutionResult result{};
        Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
                   0x00313D6CU, state, memory, &result, nullptr),
               "compiled framebuffer-access emitter was not handled");
        std::array<uint32_t, 6> actual{};
        for (size_t word = 0U; word < actual.size(); ++word) {
            Expect(memory.Read32(commandAddress +
                                     static_cast<uint32_t>(word * 4U),
                                 &actual[word]),
                   "read framebuffer-access command");
        }
        uint32_t cursor = 0U;
        Expect(actual == test.Expected &&
                   memory.Read32(ownerAddress + 8U, &cursor) &&
                   cursor == commandAddress + 24U &&
                   state.r[0] == ownerAddress &&
                   state.r[1] == commandAddress + 24U &&
                   state.r[2] == 0x6030U &&
                   result.kind == oot3d::recomp::a32::ExitKind::Branch &&
                   result.pc == 0x3000U,
               "compiled framebuffer-access packet or ABI differs");
    }
}

void TestCompiledMtx3x4CopyIfDistinct() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Expect(memory.MapRegion(
               {"data", 0x1000U, 0x1000U, true, false, {}}, &error),
           error.c_str());
    std::array<uint32_t, 12> expected{};
    for (uint32_t index = 0U; index < expected.size(); ++index) {
        expected[index] = 0x3F000000U + index;
        Expect(memory.Write32(0x1100U + index * 4U, expected[index]),
               "seed matrix source");
    }

    oot3d::recomp::a32::GuestState state{};
    state.r[0] = 0x1104U;
    state.r[1] = 0x1100U;
    state.r[14] = 0x2000U;
    oot3d::recomp::a32::ExecutionResult result{};
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x00372224U, state, memory, &result, nullptr),
           "compiled matrix copy was not handled");
    for (size_t index = 0U; index < expected.size(); ++index) {
        uint32_t word = 0U;
        Expect(memory.Read32(0x1104U + static_cast<uint32_t>(index * 4U),
                             &word) &&
                   word == expected[index],
               "compiled overlapping matrix copy differs");
    }
    Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == 0x2000U,
           "compiled matrix-copy ABI result differs");
}

void TestCompiledMtx3x4Multiply() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Expect(memory.MapRegion(
               {"data", 0x1000U, 0x1000U, true, false, {}}, &error),
           error.c_str());
    const std::array<uint32_t, 12> left{
        0x3F800000U, 0x40000000U, 0x40400000U, 0x40800000U,
        0x40A00000U, 0x40C00000U, 0x40E00000U, 0x41000000U,
        0x41100000U, 0x41200000U, 0x41300000U, 0x41400000U,
    };
    const std::array<uint32_t, 12> identity{
        0x3F800000U, 0U, 0U, 0U,
        0U, 0x3F800000U, 0U, 0U,
        0U, 0U, 0x3F800000U, 0U,
    };
    Expect(memory.WriteBytes(
               0x1100U,
               std::span<const uint8_t>(
                   reinterpret_cast<const uint8_t*>(left.data()),
                   left.size() * sizeof(uint32_t))) &&
               memory.WriteBytes(
                   0x1200U,
                   std::span<const uint8_t>(
                       reinterpret_cast<const uint8_t*>(identity.data()),
                       identity.size() * sizeof(uint32_t))),
           "seed matrix-multiply inputs");

    oot3d::recomp::a32::GuestState state{};
    state.r[0] = 0x1104U;
    state.r[1] = 0x1100U;
    state.r[2] = 0x1200U;
    state.r[14] = 0x2000U;
    state.fpscr = 0x03C00010U;
    oot3d::recomp::a32::ExecutionResult result{};
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x0036C174U, state, memory, &result, nullptr),
           "compiled matrix multiply was not handled");
    std::array<uint32_t, left.size()> output{};
    Expect(memory.ReadBytes(
               0x1104U,
               std::span<uint8_t>(reinterpret_cast<uint8_t*>(output.data()),
                                  output.size() * sizeof(uint32_t))) &&
               output == left,
           "compiled matrix multiply differs from identity product");
    Expect(std::equal(left.begin(), left.end(), state.vfp.begin()) &&
               std::equal(identity.begin() + 8, identity.end(),
                          state.vfp.begin() + 12) &&
               state.r[1] == 0x1114U && state.r[2] == 0x1220U &&
               result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == 0x2000U,
           "compiled matrix-multiply ABI result differs");
}

void TestCompiledMeshCommandPacketSubmit() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    const std::array<uint8_t, 4> currentPointerLiteral{
        0x00, 0x20, 0x00, 0x00};
    const std::array<uint8_t, 4> statsPointerLiteral{
        0x00, 0x20, 0x00, 0x00};
    Expect(memory.MapRegion(
               {"current_pointer_literal", 0x002F9E60U,
                currentPointerLiteral.size(), false, true,
                currentPointerLiteral},
               &error),
           error.c_str());
    Expect(memory.MapRegion(
               {"stats_pointer_literal", 0x002F9C9CU,
                statsPointerLiteral.size(), false, true,
                statsPointerLiteral},
               &error),
           error.c_str());
    Expect(memory.MapRegion(
               {"data", 0x1000U, 0x2000U, true, false, {}}, &error),
           error.c_str());
    Expect(memory.Write32(0x1008U, 0x1100U) &&
               memory.Write32(0x1010U, 16U) &&
               memory.Write32(0x1014U, 0U) &&
               memory.Write32(0x2000U, 0x1400U),
           "seed mesh packet metadata");
    for (uint32_t index = 0U; index < 5U; ++index) {
        Expect(memory.Write32(0x1100U + index * 4U,
                              0xA0B00000U + index),
               "seed mesh packet words");
    }

    oot3d::recomp::a32::GuestState state{};
    state.r[0] = 0x1000U;
    state.r[1] = 0x11111111U;
    state.r[2] = 0x22222222U;
    state.r[3] = 0x33333333U;
    state.r[14] = 0x3000U;
    oot3d::recomp::a32::ExecutionResult result{};
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x00466E2CU, state, memory, &result, nullptr),
           "compiled mesh-packet submit was not handled");
    for (uint32_t index = 0U; index < 4U; ++index) {
        uint32_t word = 0U;
        Expect(memory.Read32(0x1400U + index * 4U, &word) &&
                   word == 0xA0B00000U + index,
               "compiled mesh packet payload differs");
    }
    uint32_t commandPointer = 0U;
    Expect(memory.Read32(0x2000U, &commandPointer) &&
               commandPointer == 0x1410U && state.r[0] == 0x1410U &&
               state.r[1] == 0x2000U && state.r[2] == 0x1400U &&
               state.r[3] == 0x33333333U &&
               result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == 0x3000U,
           "compiled mesh-packet cursor or ABI result differs");
}

void TestCompiledAudioFourChannelDelay() {
    constexpr uint32_t stateAddress = 0x1000U;
    constexpr uint32_t channelsAddress = 0x1200U;
    constexpr uint32_t inputAddress = 0x2000U;
    constexpr uint32_t delayAddress = 0x3000U;
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Expect(memory.MapRegion(
               {"audio", stateAddress, 0x4000U, true, false, {}}, &error),
           error.c_str());
    Expect(memory.Write8(stateAddress + 0x56U, 1U) &&
               memory.Write8(stateAddress + 0x55U, 1U) &&
               memory.Write32(stateAddress + 0x1CU, delayAddress) &&
               memory.Write32(stateAddress + 0x2CU, 32U) &&
               memory.Write32(stateAddress + 0x3CU, 2U) &&
               memory.Write32(stateAddress + 0x40U, 0U) &&
               memory.Write32(stateAddress + 0x44U, 128U) &&
               memory.Write32(stateAddress + 0x48U, 64U) &&
               memory.Write32(stateAddress + 0x4CU, 64U) &&
               memory.Write32(channelsAddress, inputAddress),
           "seed four-channel delay metadata");

    std::array<uint32_t, 160> expectedOutput{};
    std::array<uint32_t, 160> expectedDelay{};
    int32_t expectedState = 32;
    for (uint32_t sample = 0U; sample < 160U; ++sample) {
        const int32_t input = 1000 + static_cast<int32_t>(sample);
        const int32_t delayed = 200 + static_cast<int32_t>(sample);
        Expect(memory.Write32(inputAddress + sample * 4U,
                              static_cast<uint32_t>(input)) &&
                   memory.Write32(delayAddress + sample * 4U,
                                  static_cast<uint32_t>(delayed)),
               "seed four-channel delay samples");
        expectedOutput[sample] = static_cast<uint32_t>(delayed);
        expectedState =
            (64 * (input - ScaleSmallSignedSample(delayed, 128)) +
             64 * expectedState) >>
            7;
        expectedDelay[sample] = static_cast<uint32_t>(expectedState);
    }

    oot3d::recomp::a32::GuestState state{};
    state.r[0] = stateAddress;
    state.r[1] = channelsAddress;
    state.r[14] = 0x5000U;
    oot3d::recomp::a32::ExecutionResult result{};
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x004A022CU, state, memory, &result, nullptr),
           "compiled four-channel delay was not handled");
    for (uint32_t sample = 0U; sample < 160U; ++sample) {
        uint32_t output = 0U;
        uint32_t delayed = 0U;
        Expect(memory.Read32(inputAddress + sample * 4U, &output) &&
                   memory.Read32(delayAddress + sample * 4U, &delayed) &&
                   output == expectedOutput[sample] &&
                   delayed == expectedDelay[sample],
               "compiled four-channel delay samples differ");
    }
    uint32_t feedback = 0U;
    uint32_t cursor = 0U;
    Expect(memory.Read32(stateAddress + 0x2CU, &feedback) &&
               memory.Read32(stateAddress + 0x40U, &cursor) &&
               feedback == static_cast<uint32_t>(expectedState) &&
               cursor == 1U && result.pc == 0x5000U,
           "compiled four-channel delay state differs");
}

void TestCompiledAudioStereoReverb() {
    constexpr uint32_t stateAddress = 0x1000U;
    constexpr uint32_t channelsAddress = 0x1200U;
    const std::array<uint32_t, 2> inputAddresses{0x2000U, 0x3000U};
    const std::array<uint32_t, 2> primaryAddresses{0x4000U, 0x5000U};
    const std::array<uint32_t, 2> secondaryAddresses{0x6000U, 0x7000U};
    const std::array<uint32_t, 2> delayAAddresses{0x8000U, 0x9000U};
    const std::array<uint32_t, 2> delayBAddresses{0xA000U, 0xB000U};
    const std::array<uint32_t, 2> feedbackAddresses{0xC000U, 0xD000U};
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Expect(memory.MapRegion(
               {"audio", stateAddress, 0xE000U, true, false, {}}, &error),
           error.c_str());
    Expect(memory.Write8(stateAddress + 0x100U, 1U) &&
               memory.Write32(stateAddress + 0x98U, 320U) &&
               memory.Write32(stateAddress + 0xA0U, 320U) &&
               memory.Write32(stateAddress + 0xA8U, 320U) &&
               memory.Write32(stateAddress + 0xACU, 320U) &&
               memory.Write32(stateAddress + 0xC0U, 320U) &&
               memory.Write32(stateAddress + 0xB8U, 64U) &&
               memory.Write32(stateAddress + 0xBCU, 64U) &&
               memory.Write32(stateAddress + 0xC8U, 64U) &&
               memory.Write32(stateAddress + 0xDCU, 64U) &&
               memory.Write32(stateAddress + 0xE0U, 64U) &&
               memory.Write32(stateAddress + 0xE8U, 16U),
           "seed stereo reverb metadata");

    std::array<std::array<uint32_t, 160>, 2> expectedOutputs{};
    std::array<std::array<uint32_t, 160>, 2> expectedDelayA{};
    std::array<std::array<uint32_t, 160>, 2> expectedDelayB{};
    std::array<std::array<uint32_t, 160>, 2> expectedFeedback{};
    std::array<int32_t, 2> expectedStates{10, 20};
    for (uint32_t channel = 0U; channel < 2U; ++channel) {
        Expect(memory.Write32(channelsAddress + channel * 4U,
                              inputAddresses[channel]) &&
                   memory.Write32(stateAddress + 0x38U + channel * 4U,
                                  primaryAddresses[channel]) &&
                   memory.Write32(stateAddress + 0x48U + channel * 4U,
                                  secondaryAddresses[channel]) &&
                   memory.Write32(stateAddress + 0x58U + channel * 8U,
                                  delayAAddresses[channel]) &&
                   memory.Write32(stateAddress + 0x5CU + channel * 8U,
                                  delayBAddresses[channel]) &&
                   memory.Write32(stateAddress + 0x78U + channel * 4U,
                                  feedbackAddresses[channel]) &&
                   memory.Write32(stateAddress + 0xCCU + channel * 4U,
                                  static_cast<uint32_t>(expectedStates[channel])),
               "seed stereo reverb channel metadata");
        for (uint32_t sample = 0U; sample < 160U; ++sample) {
            const int32_t input = 1000 + static_cast<int32_t>(channel * 100U + sample);
            const int32_t primary = 20 + static_cast<int32_t>(sample);
            const int32_t secondary = 30 + static_cast<int32_t>(sample);
            const int32_t delayA = 300 + static_cast<int32_t>(sample);
            const int32_t delayB = 100 + static_cast<int32_t>(sample);
            const int32_t feedback = 50 + static_cast<int32_t>(sample);
            Expect(memory.Write32(inputAddresses[channel] + sample * 4U,
                                  static_cast<uint32_t>(input)) &&
                       memory.Write32(primaryAddresses[channel] + sample * 4U,
                                      static_cast<uint32_t>(primary)) &&
                       memory.Write32(secondaryAddresses[channel] + sample * 4U,
                                      static_cast<uint32_t>(secondary)) &&
                       memory.Write32(delayAAddresses[channel] + sample * 4U,
                                      static_cast<uint32_t>(delayA)) &&
                       memory.Write32(delayBAddresses[channel] + sample * 4U,
                                      static_cast<uint32_t>(delayB)) &&
                       memory.Write32(feedbackAddresses[channel] + sample * 4U,
                                      static_cast<uint32_t>(feedback)),
                   "seed stereo reverb samples");
            expectedDelayA[channel][sample] = static_cast<uint32_t>(
                ScaleSmallSignedSample(delayA, 64) + secondary);
            expectedDelayB[channel][sample] = static_cast<uint32_t>(
                secondary + ScaleSmallSignedSample(delayB, 64));
            const int32_t mixedFeedback =
                delayA - delayB + ScaleSmallSignedSample(feedback, 64);
            expectedFeedback[channel][sample] =
                static_cast<uint32_t>(mixedFeedback);
            const int32_t feedbackDifference =
                feedback - ScaleSmallSignedSample(mixedFeedback, 64);
            const int32_t filtered =
                feedbackDifference -
                ((16 * (expectedStates[channel] + feedbackDifference)) >> 7);
            expectedStates[channel] = filtered;
            expectedOutputs[channel][sample] = static_cast<uint32_t>(
                (filtered * 64 + primary * 64) >> 7);
        }
    }

    oot3d::recomp::a32::GuestState state{};
    state.r[0] = stateAddress;
    state.r[1] = channelsAddress;
    state.r[14] = 0xF000U;
    oot3d::recomp::a32::ExecutionResult result{};
    Expect(Oot3dNativeGame::ExecuteOot3dCompiledFunction(
               0x004A0338U, state, memory, &result, nullptr),
           "compiled stereo reverb was not handled");
    for (uint32_t channel = 0U; channel < 2U; ++channel) {
        for (uint32_t sample = 0U; sample < 160U; ++sample) {
            uint32_t output = 0U;
            uint32_t delayA = 0U;
            uint32_t delayB = 0U;
            uint32_t feedback = 0U;
            Expect(memory.Read32(inputAddresses[channel] + sample * 4U,
                                 &output) &&
                       memory.Read32(delayAAddresses[channel] + sample * 4U,
                                     &delayA) &&
                       memory.Read32(delayBAddresses[channel] + sample * 4U,
                                     &delayB) &&
                       memory.Read32(feedbackAddresses[channel] + sample * 4U,
                                     &feedback) &&
                       output == expectedOutputs[channel][sample] &&
                       delayA == expectedDelayA[channel][sample] &&
                       delayB == expectedDelayB[channel][sample] &&
                       feedback == expectedFeedback[channel][sample],
                   "compiled stereo reverb samples differ");
        }
        uint32_t channelState = 0U;
        Expect(memory.Read32(stateAddress + 0xCCU + channel * 4U,
                             &channelState) &&
                   channelState == static_cast<uint32_t>(expectedStates[channel]),
               "compiled stereo reverb channel state differs");
    }
    for (const uint32_t offset :
         std::array<uint32_t, 5>{0x9CU, 0xA4U, 0xB0U, 0xB4U, 0xC4U}) {
        uint32_t cursor = 0U;
        Expect(memory.Read32(stateAddress + offset, &cursor) && cursor == 160U,
               "compiled stereo reverb cursor differs");
    }
    Expect(result.pc == 0xF000U,
           "compiled stereo reverb ABI result differs");
}

void TestTrueAotBgCheckClearBlock() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Expect(memory.MapRegion(
               {"data", 0x1000U, 0x1000U, true, false, {}}, &error),
           error.c_str());
    Expect(memory.Write32(0x104CU, 0x1100U),
           "seed true-AOT clear-block base");
    oot3d::recomp::a32::GuestState state{};
    state.r[0] = 0x1100U;
    state.r[1] = 2U;
    state.r[5] = 0xABU;
    state.r[8] = 0x1000U;
    state.cpsr = oot3d::recomp::a32::kFlagV;
    oot3d::recomp::a32::ExecutionResult result{};
    Oot3dNativeGame::ResetOot3dTrueAotBlockStats();
    Expect(Oot3dNativeGame::ExecuteOot3dTrueAotBlock(
               0x003247B0U, state, memory, &result, nullptr) &&
               result.kind == oot3d::recomp::a32::ExitKind::Fallthrough &&
               result.pc == 0x003247C4U && state.r[0] == 0x1102U,
           "true-AOT clear loop differs");
    uint8_t first = 0U;
    uint8_t second = 0U;
    const auto stats = Oot3dNativeGame::GetOot3dTrueAotBlockStats();
    Expect(memory.Read8(0x1100U, &first) && memory.Read8(0x1101U, &second) &&
               first == 0xABU && second == 0xABU && stats.Calls == 1U &&
               stats.Iterations == 2U && stats.MemoryFaults == 0U &&
               (state.cpsr & oot3d::recomp::a32::kFlagZ) != 0U &&
               (state.cpsr & oot3d::recomp::a32::kFlagC) != 0U &&
               (state.cpsr & oot3d::recomp::a32::kFlagV) == 0U,
           "true-AOT clear block memory, flags, or stats differ");
}

void TestTrueAotCopyU32x16Loop() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Expect(memory.MapRegion(
               {"data", 0x1000U, 0x1000U, true, false, {}}, &error),
           error.c_str());
    std::array<uint32_t, 16> expected{};
    for (uint32_t index = 0U; index < expected.size(); ++index) {
        expected[index] = 0xA0B00000U + index;
        Expect(memory.Write32(0x1100U + index * 4U, expected[index]),
               "seed true-AOT copy-loop source");
    }

    oot3d::recomp::a32::GuestState state{};
    state.r[4] = 0x1200U;
    state.r[5] = 0x1100U;
    state.r[7] = 0U;
    state.r[8] = 0U;
    state.r[10] = 0xFFFFFFFFU;
    state.cpsr = oot3d::recomp::a32::kFlagV;
    oot3d::recomp::a32::ExecutionResult result{};
    Oot3dNativeGame::ResetOot3dTrueAotBlockStats();
    Expect(Oot3dNativeGame::ExecuteOot3dTrueAotBlock(
               0x00303C70U, state, memory, &result, nullptr) &&
               result.kind == oot3d::recomp::a32::ExitKind::Fallthrough &&
               result.pc == 0x00303CB8U,
           "true-AOT copy region was not handled");
    for (uint32_t index = 0U; index < expected.size(); ++index) {
        uint32_t actual = 0U;
        Expect(memory.Read32(0x1200U + index * 4U, &actual) &&
                   actual == expected[index],
               "true-AOT copy-loop payload differs");
    }
    const auto stats = Oot3dNativeGame::GetOot3dTrueAotBlockStats();
    Expect(state.r[0] == 0x113CU && state.r[1] == 0x123CU &&
               state.r[2] == 0U && state.r[4] == 0x1240U &&
               state.r[7] == 16U && state.r[8] == 1U &&
               state.r[9] == 1U && stats.Calls == 1U &&
               stats.Iterations == 1U && stats.MemoryFaults == 0U &&
               (state.cpsr & oot3d::recomp::a32::kFlagZ) != 0U &&
               (state.cpsr & oot3d::recomp::a32::kFlagC) != 0U &&
               (state.cpsr & oot3d::recomp::a32::kFlagV) == 0U,
           "true-AOT copy-loop registers, flags, or stats differ");
}

void TestTrueAotNativeCurveType2Scan() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Expect(memory.MapRegion(
               {"curve", 0x1100U, 0x100U, true, false, {}}, &error),
           error.c_str());
    Expect(memory.Write32(0x1110U, 0U) &&
               memory.Write32(0x1120U, 10U) &&
               memory.Write32(0x1130U, 20U),
           "seed true-AOT native-curve key frames");

    oot3d::recomp::a32::GuestState state{};
    state.r[1] = 3U;
    state.r[2] = 0x1100U;
    state.vfp[0] = 0x41700000U;
    state.fpscr = 0x03C00010U;
    oot3d::recomp::a32::ExecutionResult result{};
    Oot3dNativeGame::ResetOot3dTrueAotBlockStats();
    Expect(Oot3dNativeGame::ExecuteOot3dTrueAotBlock(
               0x00308910U, state, memory, &result, nullptr) &&
               result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == 0x0030894CU,
           "true-AOT native-curve scan was not handled");
    const auto stats = Oot3dNativeGame::GetOot3dTrueAotBlockStats();
    Expect(state.r[0] == 2U && state.r[2] == 0x1130U &&
               state.r[6] == 20U && state.vfp[1] == state.vfp[0] &&
               state.vfp[2] == 0x41A00000U &&
               (state.cpsr & 0xF0000000U) == 0x20000000U &&
               (state.fpscr & 0xF0000000U) == 0x20000000U &&
               stats.Calls == 1U && stats.Iterations == 1U &&
               stats.MemoryFaults == 0U,
           "true-AOT native-curve scan state differs");
}

} // namespace

int main() {
    try {
        TestMemoryContract();
#if defined(OOT3D_NATIVE_A32_AOT_TESTS)
        TestWholeAotShiftAndFlagSemantics();
#if defined(OOT3D_NATIVE_WHOLE_AOT_TESTS)
        TestWholeAotNativeCurveDifferential();
        TestWholeAotAllocatorLeafDifferential();
#endif
#endif
        TestNativeFunctionAtDispatchEntry();
        TestSynchronousOriginalFunctionInvocation();
        TestSynchronousFiniteWaitCompletion();
        TestSynchronousReturnAtRegisteredBlock();
        TestSourceOwnerCallAdapterReentrantDispatch();
        TestSourceOwnerCallAdapterPerCallBudget();
        TestPersistentProcess();
        TestPrioritySchedulerAndTls();
        TestProcessStateRoundTrip();
        TestFallbackBoundary();
        TestBlockEntryCallback();
        TestBlockEntryCallbackFilter();
        TestCompiledRuntimeMemcpy();
        TestCompiledPicaRegisterRangeWriter();
        TestCompiledPicaVertexFloatUniformWriter();
        TestCompiledPicaMaterialFramebufferAccess();
        TestCompiledMtx3x4Multiply();
        TestCompiledMtx3x4CopyIfDistinct();
        TestCompiledMeshCommandPacketSubmit();
        TestCompiledAudioFourChannelDelay();
        TestCompiledAudioStereoReverb();
        TestTrueAotBgCheckClearBlock();
        TestTrueAotCopyU32x16Loop();
        TestTrueAotNativeCurveType2Scan();
        std::cout << "oot3d_native_a32_process_tests: ok\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "oot3d_native_a32_process_tests: " << ex.what() << '\n';
        return 1;
    }
}
