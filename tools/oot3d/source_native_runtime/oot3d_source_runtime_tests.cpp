#include "oot3d_guest_address_space.h"
#include "oot3d_ctr_apt_service.h"
#include "oot3d_ctr_host_services.h"
#include "oot3d_ctr_config_service.h"
#include "oot3d_ctr_dsp_service.h"
#include "oot3d_ctr_fs_service.h"
#include "oot3d_ctr_hid_service.h"
#include "oot3d_ctr_hid_producer.h"
#include "oot3d_ctr_gsp_service.h"
#include "oot3d_ctr_pica_backend.h"
#include "oot3d_ctr_ndm_service.h"
#include "oot3d_ctr_ipc_router.h"
#include "oot3d_ctr_srv_service.h"
#include "oot3d_source_data_bindings.h"
#include "oot3d_source_dsp_mixer.h"
#include "oot3d_source_execution_stack.h"
#include "oot3d_source_function_registry.h"
#include "oot3d_source_process_image.h"
#include "oot3d_source_pica_submission.h"
#include "oot3d_source_ctr_runtime.h"
#include "oot3d_source_c_runtime_shims.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

int Add(int left, int right) {
    return left + right;
}

void OriginalTargetFunction() {}
void ReplacementTargetFunction() {}
void UnrelatedHostFunction() {}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
struct SourceStackProbeContext {
    std::uintptr_t LocalAddress = 0;
    std::uintptr_t ReconstitutedAddress = 0;
    std::uint32_t FrameChecksum = 0;
    std::uintptr_t StackBase = 0;
    bool ReadResolved = false;
    bool WriteResolved = false;
    bool CrossingRejected = false;
};

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
void RunLargeSourceStackProbe(void* opaque) {
    auto& context = *static_cast<SourceStackProbeContext*>(opaque);
    volatile std::uint8_t frame[6144]{};
    std::uint32_t local = 0;
    frame[0] = 1;
    frame[sizeof(frame) - 1] = 2;
    context.LocalAddress = reinterpret_cast<std::uintptr_t>(&local);
    context.ReconstitutedAddress = static_cast<std::uintptr_t>(
        static_cast<std::uint32_t>(context.LocalAddress));
    context.FrameChecksum = frame[0] + frame[sizeof(frame) - 1];
    const auto address = static_cast<std::uint32_t>(context.LocalAddress);
    const auto readable =
        Oot3dSourceRuntime::ResolveCurrentSourceStackRead(address,
                                                          sizeof(local));
    context.ReadResolved = readable.size() == sizeof(local) &&
                           readable.data() ==
                               reinterpret_cast<const std::byte*>(&local) &&
                           Oot3dSourceRuntime::ResolveSourceRead(
                               address, sizeof(local)) == readable.data();
    auto writable = Oot3dSourceRuntime::ResolveCurrentSourceStackWrite(
        address, sizeof(local));
    const std::uint32_t replacement = 0x12345678U;
    if (writable.size() == sizeof(local)) {
        std::memcpy(writable.data(), &replacement, sizeof(replacement));
    }
    context.WriteResolved = local == replacement;
    context.CrossingRejected =
        Oot3dSourceRuntime::ResolveCurrentSourceStackRead(
            address, context.StackBase - context.LocalAddress + 1)
            .empty();
}

void ThrowFromSourceStack(void*) {
    throw std::runtime_error("source stack exception probe");
}

void ExitFromSourceStack(void* opaque) {
    auto& continued = *static_cast<bool*>(opaque);
    Oot3dSourceRuntime::ExitCurrentSourceThread();
    continued = true;
}

void TestSourceExecutionStack() {
    using namespace Oot3dSourceRuntime;
    SourceExecutionStack stack;
    std::string error;
    Require(stack.Initialize(&error), error);
    Require(stack.StackLimit() >= 0x60000000ULL &&
                stack.StackBase() < 0x80000000ULL,
            "source stack occupies the low ABI arena");

    SourceStackProbeContext probe;
    probe.StackBase = stack.StackBase();
    const auto probeResult = stack.Invoke(&RunLargeSourceStackProbe, &probe);
    Require(probeResult.Invoked && probeResult.Exception == nullptr &&
                probe.FrameChecksum == 3 &&
                probe.LocalAddress == probe.ReconstitutedAddress &&
                probe.LocalAddress >= stack.StackLimit() &&
                probe.LocalAddress < stack.StackBase() &&
                probe.ReadResolved && probe.WriteResolved &&
                probe.CrossingRejected,
            "large source frame preserves a narrowed stack pointer");
    Require(ResolveCurrentSourceStackRead(
                static_cast<std::uint32_t>(probe.LocalAddress), 1)
                .empty(),
            "source stack mapping is scoped to its active invocation");

    const auto exceptionResult =
        stack.Invoke(&ThrowFromSourceStack, nullptr);
    Require(exceptionResult.Invoked && exceptionResult.Exception != nullptr,
            "source stack contains exceptions before the assembly boundary");

    bool continuedAfterExit = false;
    const auto exitResult =
        stack.Invoke(&ExitFromSourceStack, &continuedAfterExit);
    Require(exitResult.Invoked && exitResult.ThreadExitRequested &&
                !continuedAfterExit,
            "target thread exit returns through the source trampoline");

    bool workerPassed = false;
    std::thread worker([&workerPassed] {
        SourceExecutionStack workerStack;
        SourceStackProbeContext workerProbe;
        std::string workerError;
        if (!workerStack.Initialize(&workerError)) {
            return;
        }
        workerProbe.StackBase = workerStack.StackBase();
        const auto result =
            workerStack.Invoke(&RunLargeSourceStackProbe, &workerProbe);
        workerPassed = result.Invoked && result.Exception == nullptr &&
                       workerProbe.LocalAddress ==
                           workerProbe.ReconstitutedAddress;
    });
    worker.join();
    Require(workerPassed,
            "worker source stack preserves the ARM32 pointer ABI");
}
#endif

void TestMappedAddressSpace() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x00100000, 0x1000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Execute,
                             "code"), "map code");
    Require(memory.MapZeroed(0x08000000, 0x2000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "heap"), "map heap");
    Require(!memory.MapZeroed(0x08001000, 0x1000, GuestMemoryAccess::Read,
                              "overlap"), "reject overlap");
    Require(memory.RegionCount() == 2, "region count");
    Require(memory.ResolveRead(0x00100010, 16).size() == 16, "read code");
    Require(memory.ResolveWrite(0x00100010, 16).empty(), "protect code");
    Require(memory.ResolveWrite(0x08000010, 16).size() == 16, "write heap");
    Require(memory.ResolveRead(0x08001ff8, 16).empty(), "reject cross-region read");
    Require(memory.RegionName(0x08000000).value() == "heap", "region name");
}

void TestFunctionRegistry() {
    using namespace Oot3dSourceRuntime;
    SourceFunctionRegistry functions;
    Require(functions.Register(0x00400000, &Add, "Add"), "register function");
    Require(!functions.Register(0x00400000, &Add, "duplicate"), "reject duplicate");
    Require(functions.Size() == 1, "function count");
    Require(functions.Contains(0x00400000), "contains function");
    Require(functions.Name(0x00400000) == "Add", "function name");
    auto* add = functions.Resolve<int(int, int)>(0x00400000);
    Require(add != nullptr, "resolve typed function");
    Require(add(20, 22) == 42, "invoke typed function");
    Require(functions.Resolve<void()>(0x00400000) == nullptr,
            "reject signature mismatch");
}

void TestSourceProcessImage() {
    using namespace Oot3dSourceRuntime;
    const std::array segments{
        SourceImageSegment{"text", 0x1000, 0x1000, 0, 8, false, true},
    };
    const std::array initialValues{
        SourceImageInitialValue{4, 4, 0x78563412},
    };
    const std::array regions{
        SourceImageSystemRegion{"system", 0x3000, 0x1000, false, false, 0, 1},
    };
    const std::array zeroRegions{
        SourceImageZeroRegion{"heap", 0x5000, 0x2000, true, false},
        SourceImageZeroRegion{"stack", 0x8000, 0x1000, true, false},
        SourceImageZeroRegion{"tls", 0x9000, 0x1000, true, false},
    };
    const SourcePrimaryThreadDescriptor thread{0x8000, 0x1000, 0x9000,
                                                0x9000, 0x1000, 0, 0x10,
                                                0x03c00010, 0x30};
    const SourceProcessImageDescriptor descriptor{
        "fixture", 8, 0x1000, 0x1000, segments, regions, zeroRegions,
        initialValues, thread,
    };

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "oot3d_source_process_fixture.bin";
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        const std::array<std::uint8_t, 8> bytes{0x10, 0x20, 0x30, 0x40,
                                                0x50, 0x60, 0x70, 0x80};
        stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    std::string error;
    auto mounted = LoadSourceProcessImage(descriptor, path, &error);
    std::filesystem::remove(path);
    Require(mounted.has_value(), error);
    Require(mounted->EntryAddress == 0x1000, "process entrypoint");
    Require(mounted->Memory.RegionCount() == 5, "process region count");
    Require(mounted->PrimaryThread.StackBaseAddress == 0x8000,
            "primary thread descriptor");
    const auto text = mounted->Memory.ResolveRead(0x1000, 9);
    Require(text.size() == 9, "mapped text and bss");
    Require(std::to_integer<std::uint8_t>(text[0]) == 0x10, "text file byte");
    Require(std::to_integer<std::uint8_t>(text[8]) == 0, "zero-filled segment tail");
    Require(mounted->Memory.ResolveWrite(0x1000, 1).empty(), "text protection");
    const auto system = mounted->Memory.ResolveRead(0x3004, 4);
    Require(system.size() == 4, "system initial value range");
    Require(std::to_integer<std::uint8_t>(system[0]) == 0x12 &&
                std::to_integer<std::uint8_t>(system[3]) == 0x78,
            "system initial value little endian");
    Require(mounted->Memory.ResolveWrite(0x5000, 1).size() == 1,
            "heap is writable");
    Require(mounted->Memory.ResolveWrite(0x6fff, 1).size() == 1,
            "heap final byte is writable");
    Require(mounted->Memory.ResolveRead(0x7000, 1).empty(),
            "heap boundary is exclusive");
}

void TestSourceDataBindings() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x1000, 0x1000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "data"), "map binding data");
    Require(CurrentSourceAddressSpace() == nullptr, "unbound source address space");
    {
        ScopedSourceAddressSpace binding(memory);
        Require(CurrentSourceAddressSpace() == &memory, "bind source address space");
        SourceWriteRef<std::uint32_t>(0x1010) = 0x1020;
        SourceWriteRef<std::uint32_t>(0x1020) = 0x12345678;
        Require(SourceReadRef<std::uint32_t>(0x1020) == 0x12345678,
                "source data read/write");
        Require(SourceReadPointer32<std::uint32_t>(0x1010) != nullptr,
                "resolve pointer32");
        Require(*SourceReadPointer32<std::uint32_t>(0x1010) == 0x12345678,
                "dereference pointer32");
        Require(oot3d_source_read_pointer32(0x1010) == 0x1020,
                "C pointer32 contract");
    }
    Require(CurrentSourceAddressSpace() == nullptr, "restore source address space");
}

class TestCtrServices final : public Oot3dSourceRuntime::CtrHostServices {
  public:
    Oot3dSourceRuntime::CtrResult SendSyncRequest(
        Oot3dSourceRuntime::CtrHandle handle,
        std::span<std::uint32_t> commandBuffer) override {
        LastSentHandle = handle;
        commandBuffer[1] = 0x12345678;
        return 7;
    }

    Oot3dSourceRuntime::CtrResult CloseHandle(
        Oot3dSourceRuntime::CtrHandle handle) override {
        LastClosedHandle = handle;
        return 0;
    }

    Oot3dSourceRuntime::CtrResult DuplicateHandle(
        Oot3dSourceRuntime::CtrHandle source,
        Oot3dSourceRuntime::CtrHandle& outHandle) override {
        LastDuplicatedHandle = source;
        outHandle = 0x76;
        return 4;
    }

    Oot3dSourceRuntime::CtrResult ConnectToPort(
        Oot3dSourceRuntime::CtrHandle& outHandle,
        std::string_view portName) override {
        LastPortName = portName;
        outHandle = 0x77;
        return 5;
    }

    Oot3dSourceRuntime::CtrResult SignalObject(
        Oot3dSourceRuntime::CtrHandle handle,
        std::uint32_t releaseCount) override {
        LastSignaledHandle = handle;
        LastReleaseCount = releaseCount;
        return 11;
    }

    Oot3dSourceRuntime::CtrResult ClearEvent(
        Oot3dSourceRuntime::CtrHandle handle) override {
        LastClearedHandle = handle;
        return 12;
    }

    Oot3dSourceRuntime::CtrResult ReleaseMutex(
        Oot3dSourceRuntime::CtrHandle handle,
        std::uint64_t threadId) override {
        LastReleasedMutex = handle;
        LastReleaseThread = threadId;
        return 13;
    }

    Oot3dSourceRuntime::CtrResult CreateEvent(
        Oot3dSourceRuntime::CtrHandle& outHandle,
        std::uint32_t resetType) override {
        LastResetType = resetType;
        outHandle = 0x81;
        return 14;
    }

    Oot3dSourceRuntime::CtrResult CreateAddressArbiter(
        Oot3dSourceRuntime::CtrHandle& outHandle) override {
        outHandle = 0x82;
        return 15;
    }

    Oot3dSourceRuntime::CtrResult GetResourceLimit(
        Oot3dSourceRuntime::CtrHandle& outHandle,
        Oot3dSourceRuntime::CtrHandle process) override {
        LastResourceProcess = process;
        outHandle = 0x83;
        return 16;
    }

    std::int64_t GetResourceLimitCurrentValue(
        Oot3dSourceRuntime::CtrHandle resourceLimit,
        std::uint32_t type) override {
        LastResourceLimit = resourceLimit;
        LastResourceType = type;
        return 0x12345678;
    }

    Oot3dSourceRuntime::CtrResult ArbitrateAddress(
        Oot3dSourceRuntime::CtrHandle arbiter,
        Oot3dSourceRuntime::GuestAddressSpace&,
        Oot3dSourceRuntime::GuestAddress address,
        std::uint32_t type,
        std::int32_t value) override {
        LastArbiter = arbiter;
        LastArbitrationAddress = address;
        LastArbitrationType = type;
        LastArbitrationValue = value;
        return 17;
    }

    Oot3dSourceRuntime::CtrResult ControlMemory(
        Oot3dSourceRuntime::GuestAddressSpace&,
        Oot3dSourceRuntime::GuestAddress& outAddress,
        Oot3dSourceRuntime::GuestAddress address0,
        Oot3dSourceRuntime::GuestAddress,
        std::uint32_t size, std::uint32_t operation,
        std::uint32_t permissions) override {
        outAddress = address0;
        LastMemorySize = size;
        LastMemoryOperation = operation;
        LastMemoryPermissions = permissions;
        return 18;
    }

    Oot3dSourceRuntime::CtrResult MapMemoryBlock(
        Oot3dSourceRuntime::CtrHandle memoryBlock,
        Oot3dSourceRuntime::GuestAddressSpace&,
        Oot3dSourceRuntime::GuestAddress address,
        std::uint32_t, std::uint32_t) override {
        LastMemoryBlock = memoryBlock;
        LastMemoryAddress = address;
        return 19;
    }

    Oot3dSourceRuntime::CtrResult UnmapMemoryBlock(
        Oot3dSourceRuntime::CtrHandle memoryBlock,
        Oot3dSourceRuntime::GuestAddressSpace&,
        Oot3dSourceRuntime::GuestAddress address) override {
        LastMemoryBlock = memoryBlock;
        LastMemoryAddress = address;
        return 20;
    }

    Oot3dSourceRuntime::CtrResult CreateThread(
        Oot3dSourceRuntime::CtrHandle& outHandle,
        Oot3dSourceRuntime::GuestAddress entry,
        Oot3dSourceRuntime::GuestAddress argument,
        Oot3dSourceRuntime::GuestAddress stackTop,
        std::int32_t priority, std::int32_t processorId) override {
        outHandle = 0x84;
        LastThreadEntry = entry;
        LastThreadArgument = argument;
        LastThreadStack = stackTop;
        LastThreadPriority = priority;
        LastThreadProcessor = processorId;
        return 21;
    }

    std::uint32_t LastSentHandle = 0;
    std::uint32_t LastClosedHandle = 0;
    std::uint32_t LastDuplicatedHandle = 0;
    std::uint32_t LastSignaledHandle = 0;
    std::uint32_t LastReleaseCount = 0;
    std::uint32_t LastClearedHandle = 0;
    std::uint32_t LastReleasedMutex = 0;
    std::uint64_t LastReleaseThread = 0;
    std::uint32_t LastResetType = 0;
    std::uint32_t LastResourceProcess = 0;
    std::uint32_t LastResourceLimit = 0;
    std::uint32_t LastResourceType = 0;
    std::uint32_t LastArbiter = 0;
    std::uint32_t LastArbitrationAddress = 0;
    std::uint32_t LastArbitrationType = 0;
    std::int32_t LastArbitrationValue = 0;
    std::uint32_t LastMemoryBlock = 0;
    std::uint32_t LastMemoryAddress = 0;
    std::uint32_t LastMemorySize = 0;
    std::uint32_t LastMemoryOperation = 0;
    std::uint32_t LastMemoryPermissions = 0;
    std::uint32_t LastThreadEntry = 0;
    std::uint32_t LastThreadArgument = 0;
    std::uint32_t LastThreadStack = 0;
    std::int32_t LastThreadPriority = 0;
    std::int32_t LastThreadProcessor = 0;
    std::string LastPortName;
};

void TestCtrHostServices() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x9000, 0x1000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "tls"), "map host-service TLS");
    const SourcePrimaryThreadDescriptor thread{0, 0, 0x9000, 0x9000,
                                                0x1000, 0, 0, 0, 0};
    TestCtrServices services;
    {
        ScopedCtrHostServices binding(memory, thread, services);
        std::uint32_t* commandBuffer = oot3d_host_target_leaf_command_buffer();
        Require(getThreadCommandBuffer() == commandBuffer,
                "expose native thread command buffer ABI");
        std::uint32_t mappedAddress = 0;
        Require(oot3d_host_ctr_control_memory(
                    &mappedAddress,0xA000,0,0x1000,3,3) == 18 &&
                    mappedAddress == 0xA000 && services.LastMemorySize == 0x1000,
                "forward typed ControlMemory ABI");
        Require(oot3d_host_ctr_map_memory_block(0x40,0xA000,3,0) == 19 &&
                    services.LastMemoryBlock == 0x40,
                "forward typed MapMemoryBlock ABI");
        Require(oot3d_host_ctr_unmap_memory_block(0x40,0xA000) == 20 &&
                    services.LastMemoryAddress == 0xA000,
                "forward typed UnmapMemoryBlock ABI");
        std::uint32_t threadHandle = 0;
        Require(oot3d_host_ctr_create_thread(
                    &threadHandle,0x1234,0x5678,0xA000,0x20,-2) == 21 &&
                    threadHandle == 0x84 && services.LastThreadEntry == 0x1234 &&
                    services.LastThreadProcessor == -2,
                "forward typed CreateThread ABI");
        commandBuffer[0] = 0x00140000;
        Require(oot3d_host_target_leaf_send_sync_request(0x42) == 7,
                "forward sync request result");
        Require(services.LastSentHandle == 0x42, "forward sync request handle");
        Require(commandBuffer[1] == 0x12345678, "share TLS command buffer");
        oot3d_host_target_leaf_close_handle(0x99);
        Require(services.LastClosedHandle == 0x99, "forward close handle");
        Require(svcSendSyncRequest(0x43) == 7 &&
                    services.LastSentHandle == 0x43,
                "forward native sync-request ABI");
        Require(svcCloseHandle(0x9a) == 0 &&
                    services.LastClosedHandle == 0x9a,
                "forward native close-handle ABI");
        std::uint32_t duplicateHandle = 0;
        Require(oot3d_host_ctr_duplicate_handle(
                    &duplicateHandle, 0xffff8000U) == 4 &&
                    duplicateHandle == 0x76 &&
                    services.LastDuplicatedHandle == 0xffff8000U,
                "forward duplicate-handle host ABI");
        std::uint32_t portHandle = 0;
        Require(oot3d_host_ctr_connect_to_port(&portHandle, "srv:") == 5 &&
                    portHandle == 0x77 && services.LastPortName == "srv:",
                "forward connect-to-port host ABI");
        Require(oot3d_host_ctr_sleep_thread(0) == 0,
                "forward zero-duration thread yield ABI");
        Require(oot3d_host_ctr_signal_event(0x31) == 11 &&
                    services.LastSignaledHandle == 0x31 &&
                    services.LastReleaseCount == 1,
                "forward signal-event host ABI");
        Require(oot3d_host_ctr_clear_event(0x32) == 12 &&
                    services.LastClearedHandle == 0x32,
                "forward clear-event host ABI");
        Require(oot3d_host_ctr_release_mutex(0x33) == 13 &&
                    services.LastReleasedMutex == 0x33 &&
                    services.LastReleaseThread == 0x9000,
                "forward release-mutex host ABI");
        std::uint32_t kernelHandle = 0;
        Require(oot3d_host_ctr_create_event(&kernelHandle, 1) == 14 &&
                    kernelHandle == 0x81 && services.LastResetType == 1,
                "forward create-event host ABI");
        Require(oot3d_host_ctr_create_address_arbiter(&kernelHandle) == 15 &&
                    kernelHandle == 0x82,
                "forward create-address-arbiter host ABI");
        Require(oot3d_host_ctr_get_resource_limit(
                    &kernelHandle, 0xffff8001U) == 16 &&
                    kernelHandle == 0x83 &&
                    services.LastResourceProcess == 0xffff8001U,
                "forward get-resource-limit host ABI");
        Require(oot3d_host_ctr_arbitrate_address(0x82, 0x9040, 0, 1) == 17 &&
                    services.LastArbiter == 0x82 &&
                    services.LastArbitrationAddress == 0x9040 &&
                    services.LastArbitrationType == 0 &&
                    services.LastArbitrationValue == 1,
                "forward arbitrate-address host ABI");
    }
}

void TestSystemArenaArbiterBridge() {
    alignas(std::int32_t) std::array<std::int32_t, 3> lock{-1, 0, 0};
    std::atomic_ref<std::int32_t> state(lock[0]);
    std::atomic<bool> returned = false;
    std::thread waiter([&] {
        oot3d_host_system_arena_wait(lock.data());
        returned.store(true, std::memory_order_release);
    });

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (state.load(std::memory_order_acquire) != -2 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool decrementedBeforeWait =
        state.load(std::memory_order_acquire) == -2;
    state.store(decrementedBeforeWait ? 2 : 1, std::memory_order_release);
    oot3d_host_system_arena_wake_one(lock.data());
    waiter.join();

    Require(decrementedBeforeWait,
            "decrement LightLock state before host arbiter wait");
    Require(returned.load(std::memory_order_acquire),
            "resume LightLock waiter after native release signal");
}

class TestIpcSession final : public Oot3dSourceRuntime::CtrIpcSession {
  public:
    Oot3dSourceRuntime::CtrResult Dispatch(
        std::span<std::uint32_t> commandBuffer) override {
        LastCommand = commandBuffer[0];
        commandBuffer[1] = Response;
        return Result;
    }

    std::uint32_t LastCommand = 0;
    std::uint32_t Response = 0x55aa;
    Oot3dSourceRuntime::CtrResult Result = 3;
};

void TestCtrIpcRouter() {
    using namespace Oot3dSourceRuntime;
    CtrIpcRouter router;
    auto session = std::make_shared<TestIpcSession>();
    Require(router.RegisterPort("srv:", session), "register IPC port");
    CtrHandle connectedHandle = 0;
    Require(router.ConnectToPort(connectedHandle, "srv:") == 0 &&
                connectedHandle != 0 && router.Contains(connectedHandle),
            "connect registered IPC port");
    Require(router.ConnectToPort(connectedHandle, "missing") ==
                CtrIpcRouter::UnhandledResult,
            "reject unknown IPC port");
    Require(router.RegisterSession(0x40, "cfg:u", session),
            "register IPC session");
    Require(!router.RegisterSession(0x40, "duplicate", session),
            "reject duplicate IPC handle");
    std::array<std::uint32_t, 4> commandBuffer{0x12340000, 0, 0, 0};
    Require(router.SendSyncRequest(0x40, commandBuffer) == 3,
            "dispatch IPC request");
    Require(session->LastCommand == 0x12340000 &&
                commandBuffer[1] == 0x55aa,
            "share IPC request and response buffer");
    Require(router.Events().size() == 1 && router.Events()[0].Handled &&
                router.Events()[0].SessionName == "cfg:u",
            "record handled IPC event");
    Require(router.SendSyncRequest(0x41, commandBuffer) ==
                CtrIpcRouter::UnhandledResult,
            "reject unknown IPC handle");
    Require(router.CloseHandle(0x40) == 0 && !router.Contains(0x40),
            "close IPC handle");
    Require(router.CloseHandle(0x40) == CtrIpcRouter::UnhandledResult,
            "reject repeated IPC close");

    auto event = std::make_shared<CtrKernelObject>(
        CtrKernelObjectKind::Event, "test:event", 1, 1);
    CtrHandle eventHandle = 0;
    CtrHandle duplicate = 0;
    Require(router.OpenKernelObject(event, eventHandle) == 0 &&
                router.DuplicateHandle(eventHandle, duplicate) == 0 &&
                eventHandle != duplicate &&
                router.KernelObject(eventHandle) == router.KernelObject(duplicate),
            "duplicate shared kernel object handle");
    Require(router.CloseHandle(eventHandle) == 0 &&
                router.Contains(duplicate) && event->AvailableCount == 1,
            "close one kernel handle without destroying shared object");
    Require(router.CloseHandle(duplicate) == 0,
            "close final kernel object handle");

    auto mutex = std::make_shared<CtrKernelObject>(
        CtrKernelObjectKind::Mutex, "test:mutex", 1, 1);
    CtrHandle mutexHandle = 0;
    Require(router.OpenKernelObject(mutex, mutexHandle) == 0,
            "open waitable mutex");
    const std::array waitHandles{mutexHandle};
    auto wait = router.WaitSynchronization(waitHandles, false, 7);
    Require(wait.Ready && wait.Result == 0 && mutex->OwnerThreadId == 7 &&
                mutex->AvailableCount == 0,
            "acquire typed kernel mutex");
    Require(router.WaitSynchronization(waitHandles, false, 8).Ready == false,
            "block a second kernel thread on owned mutex");
    Require(router.ReleaseMutex(mutexHandle, 7) == 0,
            "release typed kernel mutex");
}

void TestCtrAptService() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0xa000, 0x1000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "APT static buffers"), "map APT static buffers");
    CtrIpcRouter router;
    CtrAptService apt(memory, router, {0xa180});
    std::array<std::uint32_t, 6> lock{0x00010040, 0x55, 0, 0, 0, 0};
    Require(apt.Dispatch(lock) == 0 && lock[0] == 0x000100C2 &&
                lock[2] == 0x55 && router.Contains(lock[5]),
            "APT lock handle response");
    const auto lockObject = router.KernelObject(lock[5]);
    Require(lockObject && lockObject->Kind == CtrKernelObjectKind::Mutex &&
                lockObject->AvailableCount == 1,
            "APT lock starts available");

    std::array<std::uint32_t, 5> initialize{
        0x00020080, 0x300, 0, 0, 0};
    Require(apt.Dispatch(initialize) == 0 &&
                initialize[0] == 0x00020043 &&
                initialize[2] == 0x04000000 &&
                router.Contains(initialize[3]) && router.Contains(initialize[4]),
            "APT initialize event handles");
    const auto parameter = router.KernelObject(initialize[4]);
    Require(parameter && parameter->Kind == CtrKernelObjectKind::Event &&
                parameter->AvailableCount == 1,
            "APT initial Wakeup parameter event is signaled");
    const std::array<std::uint32_t, 2> staticBuffer{0x10002, 0xa300};
    std::memcpy(memory.ResolveWrite(0xa180, sizeof(staticBuffer)).data(),
                staticBuffer.data(), sizeof(staticBuffer));
    std::array<std::uint32_t, 9> receive{
        0x000D0080, 0x300, 4, 0, 0, 0, 0, 0, 0};
    Require(apt.Dispatch(receive) == 0 && receive[0] == 0x000D0104 &&
                receive[3] == 1 && receive[8] == 0xa300,
            "APT receive initial Wakeup parameter");
    const std::array<std::uint32_t, 2> utilityBuffer{0x4002, 0xa320};
    std::memcpy(memory.ResolveWrite(0xa180, sizeof(utilityBuffer)).data(),
                utilityBuffer.data(), sizeof(utilityBuffer));
    std::uint32_t transition = 0x62;
    std::memcpy(memory.ResolveWrite(0xa310, 4).data(), &transition, 4);
    memory.ResolveWrite(0xa320, 1)[0] = std::byte{0xff};
    std::array<std::uint32_t, 6> utility{
        0x004B00C2, 7, 4, 1, 0x10402, 0xa310};
    Require(apt.Dispatch(utility) == 0 && utility[0] == 0x004B0082 &&
                memory.ResolveRead(0xa320, 1)[0] == std::byte{0},
            "APT unlock-transition utility");
    std::array<std::uint32_t, 2> enable{0x00030040, 0};
    Require(apt.Dispatch(enable) == 0 && enable[1] == 0,
            "APT enable response");
}

void TestCtrHidService() {
    using namespace Oot3dSourceRuntime;
    CtrIpcRouter router;
    CtrHidService hid(router);
    std::array<std::uint32_t, 9> handles{0x000A0000};
    Require(hid.Dispatch(handles) == 0 && handles[0] == 0x000A0047 &&
                handles[1] == 0 && handles[2] == 0x14000000,
            "HID IPC handle response");
    for (std::size_t index = 3; index < handles.size(); ++index)
        Require(router.Contains(handles[index]), "HID returned valid handle");
    const auto sharedMemory = router.KernelObject(handles[3]);
    Require(sharedMemory &&
                sharedMemory->Kind == CtrKernelObjectKind::SharedMemory &&
                sharedMemory->SharedMemory.size() == 0x1000,
            "HID native shared-memory object");
    MappedGuestAddressSpace guestMemory;
    Require(guestMemory.MapZeroed(
                0x6000, 0x1000,
                GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                "mapped HID shared memory") &&
                router.MapSharedMemory(handles[3], guestMemory, 0x6000) == 0,
            "map HID shared memory into guest process image");

    std::array<std::uint32_t, 2> enableAccelerometer{0x00110000, 0};
    std::array<std::uint32_t, 2> enableGyroscope{0x00130000, 0};
    Require(hid.Dispatch(enableAccelerometer) == 0 &&
                hid.Dispatch(enableGyroscope) == 0 &&
                hid.AccelerometerEnabled() && hid.GyroscopeEnabled(),
            "HID motion sensor reference counts");
    std::array<std::uint32_t, 3> coefficient{0x00150000, 0, 0};
    Require(hid.Dispatch(coefficient) == 0 && coefficient[0] == 0x00150080,
            "HID gyroscope coefficient response");
    float coefficientValue = 0;
    std::memcpy(&coefficientValue, &coefficient[2], sizeof(coefficientValue));
    Require(coefficientValue == 14.375F,
            "HID gyroscope coefficient value");
    std::array<std::uint32_t, 7> calibration{0x00160000};
    Require(hid.Dispatch(calibration) == 0 &&
                calibration[0] == 0x00160180,
            "HID gyroscope calibration response");
    std::array<std::int16_t, 9> calibrationValues{};
    std::memcpy(calibrationValues.data(), &calibration[2],
                sizeof(calibrationValues));
    Require(calibrationValues[1] == 6700 && calibrationValues[2] == -6700,
            "HID gyroscope calibration values");

    CtrHidProducer producer(hid);
    CtrHidState state;
    state.Buttons = (1U << 0U) | (1U << 3U);
    state.CirclePadX = 154;
    state.CirclePadY = -154;
    state.TouchX = 319;
    state.TouchY = 239;
    state.TouchPressed = true;
    Require(producer.Submit(state, 1234), "submit native HID sample");
    const auto bytes = guestMemory.ResolveRead(0x6000, 0x1000);
    std::uint32_t currentButtons = 0;
    std::uint32_t additions = 0;
    std::uint32_t touchPressed = 0;
    std::memcpy(&currentButtons, bytes.data() + 0x1c, sizeof(currentButtons));
    std::memcpy(&additions, bytes.data() + 0x2c, sizeof(additions));
    std::memcpy(&touchPressed, bytes.data() + 0xcc, sizeof(touchPressed));
    constexpr std::uint32_t expectedButtons =
        (1U << 0U) | (1U << 3U) | (1U << 28U) | (1U << 31U);
    Require(currentButtons == expectedButtons &&
                additions == expectedButtons && touchPressed == 1,
            "populate native HID pad and touch ring buffers");
    Require(currentButtons == expectedButtons,
            "write HID samples directly to mapped guest memory");
    Require(router.KernelObject(handles[4])->AvailableCount == 1 &&
                router.KernelObject(handles[5])->AvailableCount == 1,
            "signal native HID pad events");
    Require(producer.Submit({}, 2468), "submit native HID release sample");
    std::uint32_t releasedButtons = 0;
    std::memcpy(&releasedButtons, bytes.data() + 0x40,
                sizeof(releasedButtons));
    Require(releasedButtons == expectedButtons,
            "preserve native HID release edges across ring entries");
}

class TestGpuBackend final : public Oot3dSourceRuntime::CtrGpuBackend {
  public:
    bool WriteRegisters(std::uint32_t base,
                        std::span<const std::uint32_t> values,
                        std::span<const std::uint32_t> masks) override {
        RegisterBase = base;
        RegisterValues.assign(values.begin(), values.end());
        RegisterMasks.assign(masks.begin(), masks.end());
        return true;
    }
    bool SubmitCommand(const Oot3dSourceRuntime::CtrGspCommand& command,
                       std::span<const std::uint32_t> list) override {
        Command = command;
        CommandList.assign(list.begin(), list.end());
        return true;
    }
    bool SetFramebuffer(
        const Oot3dSourceRuntime::CtrGspFramebuffer& framebuffer) override {
        Framebuffer = framebuffer;
        return true;
    }
    void SetLcdForceBlack(bool value) override { ForceBlack = value; }
    std::vector<std::uint8_t> TakeInterrupts() override {
        return std::exchange(Interrupts, {});
    }

    std::uint32_t RegisterBase = 0;
    std::vector<std::uint32_t> RegisterValues;
    std::vector<std::uint32_t> RegisterMasks;
    Oot3dSourceRuntime::CtrGspCommand Command;
    std::vector<std::uint32_t> CommandList;
    Oot3dSourceRuntime::CtrGspFramebuffer Framebuffer;
    bool ForceBlack = true;
    std::vector<std::uint8_t> Interrupts;
};

void TestCtrGspService() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x7000, 0x3000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "GSP guest memory"), "map GSP guest memory");
    CtrIpcRouter router;
    TestGpuBackend backend;
    CtrGspService gsp(memory, router, &backend);
    auto interrupt = std::make_shared<CtrKernelObject>(
        CtrKernelObjectKind::Event, "gsp:test-interrupt", 0, 1);
    CtrHandle interruptHandle = 0;
    Require(router.OpenKernelObject(interrupt, interruptHandle) == 0,
            "create GSP interrupt event");
    std::array<std::uint32_t, 5> relay{
        0x00130042, 1, 0, interruptHandle, 0};
    Require(gsp.Dispatch(relay) == 0 && relay[0] == 0x00130082 &&
                relay[1] == 0x2a07 && router.Contains(relay[4]),
            "register GSP interrupt relay queue");
    Require(router.MapSharedMemory(relay[4], memory, 0x7000) == 0,
            "map GSP shared memory");

    const std::array<std::uint32_t, 4> list{0x11, 0x22, 0x33, 0x44};
    std::memcpy(memory.ResolveWrite(0x9000, sizeof(list)).data(), list.data(),
                sizeof(list));
    auto queue = memory.ResolveWrite(0x7800, 0x40);
    const std::uint32_t header = 1U << 8U;
    const std::array<std::uint32_t, 8> packet{
        1, 0x9000, sizeof(list), 0, 0, 0, 0, 0};
    std::memcpy(queue.data(), &header, sizeof(header));
    std::memcpy(queue.data() + 0x20, packet.data(), sizeof(packet));
    backend.Interrupts = {5};
    std::array<std::uint32_t, 2> trigger{0x000c0000, 0};
    Require(gsp.Dispatch(trigger) == 0 && trigger[0] == 0x000c0040 &&
                backend.Command.Control == 1 && backend.CommandList ==
                    std::vector<std::uint32_t>(list.begin(), list.end()),
            "submit native GSP command list");
    Require(memory.ResolveRead(0x7001, 1)[0] == std::byte{1} &&
                memory.ResolveRead(0x700c, 1)[0] == std::byte{5} &&
                interrupt->AvailableCount == 1,
            "queue native GSP interrupt and signal relay event");

    std::array<std::uint32_t, 9> framebuffer{
        0x00050200, 0, 0, 0x8000, 0x8100, 960, 2, 1, 0};
    Require(gsp.Dispatch(framebuffer) == 0 &&
                backend.Framebuffer.AddressLeft == 0x8000 &&
                backend.Framebuffer.Stride == 960,
            "submit native GSP framebuffer");
}

void TestCtrPicaBackend() {
    using namespace Oot3dSourceRuntime;
    Oot3dNativeGame::Oot3dNativePicaFrontend frontend;
    frontend.SetDiagnosticHistoryEnabled(false);
    CtrPicaBackend backend(frontend);
    const std::array<std::uint32_t, 2> values{0x12345678, 0xabcdef01};
    Require(backend.WriteRegisters(0x100, values, {}) &&
                frontend.HardwareWriteCount() == values.size(),
            "route GSP register writes through native PICA frontend");
    CtrGspCommand cacheFlush;
    cacheFlush.Control = 5;
    Require(backend.SubmitCommand(cacheFlush, {}),
            "route GSP command through native PICA frontend");
}

void TestCtrDspService() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x1ff40000, 0x40000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "DSP RAM") &&
                memory.MapZeroed(0xb000, 0x2000,
                                 GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                                 "DSP IPC buffers"),
            "map DSP guest memory");
    CtrIpcRouter router;
    CtrDspService dsp(memory, router, {0xb180, false});
    auto interrupt = std::make_shared<CtrKernelObject>(
        CtrKernelObjectKind::Event, "dsp:test-interrupt", 0, 1);
    CtrHandle interruptHandle = 0;
    Require(router.OpenKernelObject(interrupt, interruptHandle) == 0,
            "create DSP interrupt event");
    std::array<std::uint32_t, 5> registerInterrupt{
        0x00150082, 2, 2, 0, interruptHandle};
    Require(dsp.Dispatch(registerInterrupt) == 0,
            "register DSP audio interrupt");

    const std::array<std::byte, 4> pipeCommand{
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    std::copy(pipeCommand.begin(), pipeCommand.end(),
              memory.ResolveWrite(0xb300, pipeCommand.size()).begin());
    std::array<std::uint32_t, 5> writePipe{
        0x000d0082, 2, 4, (4U << 14U) | 2U, 0xb300};
    Require(dsp.Dispatch(writePipe) == 0 && dsp.AudioRunning() &&
                interrupt->AvailableCount == 1,
            "start native DSP audio pipe");
    const std::array<std::uint32_t, 2> staticBuffer{
        (32U << 14U) | 2U, 0xb400};
    std::memcpy(memory.ResolveWrite(0xb180, sizeof(staticBuffer)).data(),
                staticBuffer.data(), sizeof(staticBuffer));
    std::array<std::uint32_t, 5> readPipe{
        0x001000c0, 2, 0, 32, 0};
    Require(dsp.Dispatch(readPipe) == 0 && readPipe[0] == 0x00100082 &&
                readPipe[2] == 32,
            "read native DSP audio structure table");
    std::uint16_t structureCount = 0;
    std::memcpy(&structureCount, memory.ResolveRead(0xb400, 2).data(), 2);
    Require(structureCount == 15,
            "preserve native DSP audio structure count");
    std::array<std::uint32_t, 3> convert{0x000c0040, 0x100, 0};
    Require(dsp.Dispatch(convert) == 0 && convert[2] == 0x1ff40200,
            "convert native DSP word address");
}

void TestCtrNdmService() {
    using namespace Oot3dSourceRuntime;
    CtrNdmService ndm;
    std::array<std::uint32_t, 2> suspend{0x00080040, 1};
    Require(ndm.Dispatch(suspend) == 0 && ndm.SchedulerSuspended() &&
                ndm.RunsInBackground(),
            "suspend native NDM scheduler");
    std::array<std::uint32_t, 2> resume{0x00090000, 0};
    Require(ndm.Dispatch(resume) == 0 && !ndm.SchedulerSuspended() &&
                !ndm.RunsInBackground(),
            "resume native NDM scheduler");
}

void TestCtrConfigService() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x2000, 0x1000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "config output"), "map config output");
    CtrConfigServiceProfile profile;
    profile.SoundOutputMode = 2;
    profile.SystemLanguage = 4;
    profile.SystemRegion = 1;
    CtrConfigService service(memory, profile);

    std::array<std::uint32_t, 5> region{0x00020000, 0, 0, 0, 0};
    Require(service.Dispatch(region) == 0 && region[0] == 0x00020080 &&
                region[1] == 0 && region[2] == 1,
            "dispatch CFG region request");

    std::array<std::uint32_t, 5> language{
        0x00010082, 1, 0x000A0002, 0x1c, 0x2010};
    Require(service.Dispatch(language) == 0 &&
                language[0] == 0x00010042 && language[1] == 0,
            "dispatch CFG language request");
    const auto value = memory.ResolveRead(0x2010, 1);
    Require(value.size() == 1 &&
                std::to_integer<std::uint8_t>(value[0]) == 4,
            "write CFG result to guest memory");

    std::array<std::uint32_t, 5> invalid{
        0x00010082, 1, 0x000A0002, 0, 0x2010};
    Require(service.Dispatch(invalid) == CtrIpcRouter::UnhandledResult,
            "reject invalid CFG descriptor");
}

void TestCtrSrvService() {
    using namespace Oot3dSourceRuntime;
    CtrIpcRouter router;
    auto config = std::make_shared<TestIpcSession>();
    Require(router.RegisterPort("cfg:u", config), "register SRV target service");
    CtrSrvService srv(router);

    std::array<std::uint32_t, 4> registerClient{0x00010002, 0, 0, 0};
    Require(srv.Dispatch(registerClient) == 0 &&
                registerClient[0] == 0x00010040 && registerClient[1] == 0,
            "dispatch SRV register client");

    std::array<std::uint32_t, 4> getService{0x00050100, 0, 0, 5};
    std::memcpy(&getService[1], "cfg:u", 5);
    Require(srv.Dispatch(getService) == 0 &&
                getService[0] == 0x00050042 && getService[1] == 0 &&
                getService[2] == 0x10 && router.Contains(getService[3]),
            "dispatch SRV get service handle");

    std::array<std::uint32_t, 4> missing{0x00050100, 0, 0, 7};
    std::memcpy(&missing[1], "missing", 7);
    Require(srv.Dispatch(missing) == CtrIpcRouter::UnhandledResult,
            "reject unregistered SRV service");
}

void TestSourceCtrRuntime() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x2000, 0x1000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "CTR output"), "map composed CTR output");
    SourceCtrRuntime runtime(memory);
    CtrHandle srvHandle = 0;
    Require(runtime.Router().ConnectToPort(srvHandle, "srv:") == 0,
            "connect composed SRV port");

    std::array<std::uint32_t, 4> getConfig{0x00050100, 0, 0, 5};
    std::memcpy(&getConfig[1], "cfg:u", 5);
    Require(runtime.Router().SendSyncRequest(srvHandle, getConfig) == 0,
            "open CFG through composed SRV");
    const CtrHandle configHandle = getConfig[3];
    std::array<std::uint32_t, 5> getRegion{0x00020000, 0, 0, 0, 0};
    Require(runtime.Router().SendSyncRequest(configHandle, getRegion) == 0 &&
                getRegion[0] == 0x00020080,
            "query CFG through composed service handle");

    std::array<std::uint32_t, 4> getApt{0x00050100, 0, 0, 5};
    std::memcpy(&getApt[1], "APT:U", 5);
    Require(runtime.Router().SendSyncRequest(srvHandle, getApt) == 0,
            "open APT through composed SRV");
    std::array<std::uint32_t, 6> getLock{0x00010040, 0, 0, 0, 0, 0};
    Require(runtime.Router().SendSyncRequest(getApt[3], getLock) == 0 &&
                getLock[0] == 0x000100C2 &&
                runtime.Router().KernelObject(getLock[5]) != nullptr,
            "obtain APT kernel object through composed service handle");
}

void TestCtrRomFsRead() {
    using namespace Oot3dSourceRuntime;
    const auto path = std::filesystem::temp_directory_path() /
                      "oot3d_source_romfs_fixture.bin";
    const std::array<std::uint8_t, 20> fixture{
        0xaa, 0xbb, 0xcc, 0xdd, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
        0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x00};
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(fixture.data()),
                     static_cast<std::streamsize>(fixture.size()));
    }

    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x2000, 0x2000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "FS guest buffers"), "map FS guest buffers");
    CtrFsProfile fsProfile{path, 4, 16};
    SourceCtrRuntimeProfile runtimeProfile;
    runtimeProfile.Fs = fsProfile;
    SourceCtrRuntime runtime(memory, std::move(runtimeProfile));
    CtrHandle srvHandle = 0;
    Require(runtime.Router().ConnectToPort(srvHandle, "srv:") == 0,
            "connect SRV for FS");

    std::array<std::uint32_t, 4> getFs{0x00050100, 0, 0, 7};
    std::memcpy(&getFs[1], "fs:USER", 7);
    Require(runtime.Router().SendSyncRequest(srvHandle, getFs) == 0,
            "open fs:USER through SRV");
    const CtrHandle fsHandle = getFs[3];
    std::array<std::uint32_t, 2> initialize{0x08010002, 0x20};
    Require(runtime.Router().SendSyncRequest(fsHandle, initialize) == 0 &&
                initialize[0] == 0x08010040,
            "initialize fs:USER");

    std::array<std::uint32_t, 13> open{
        0x08030204, 0, 3, 1, 1, 2, 12, 1, 0,
        (1U << 14U) | 2U, 0x3000, (12U << 14U) | 2U, 0x3010};
    Require(runtime.Router().SendSyncRequest(fsHandle, open) == 0 &&
                open[0] == 0x08030042 && runtime.Router().Contains(open[3]),
            "open RomFS image through fs:USER");
    const CtrHandle fileHandle = open[3];

    std::array<std::uint32_t, 4> size{0x08040000, 0, 0, 0};
    Require(runtime.Router().SendSyncRequest(fileHandle, size) == 0 &&
                size[0] == 0x080400C0 && size[2] == 16 && size[3] == 0,
            "query RomFS image size");
    std::array<std::uint32_t, 6> read{
        0x080200C2, 3, 0, 5, (5U << 4U) | 0xCU, 0x2100};
    Require(runtime.Router().SendSyncRequest(fileHandle, read) == 0 &&
                read[0] == 0x08020082 && read[2] == 5,
            "read RomFS image into guest memory");
    const auto bytes = memory.ResolveRead(0x2100, 5);
    Require(bytes.size() == 5 &&
                std::to_integer<std::uint8_t>(bytes[0]) == 0x40 &&
                std::to_integer<std::uint8_t>(bytes[4]) == 0x80,
            "RomFS read respects image base and file offset");
    Require(runtime.Router().CloseHandle(fileHandle) == 0,
            "close RomFS file session");
    std::filesystem::remove(path);
}

void TestCtrSaveData() {
    using namespace Oot3dSourceRuntime;
    const auto directory = std::filesystem::temp_directory_path() /
                           "oot3d_source_savedata_fixture";
    std::filesystem::remove_all(directory);

    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x4000, 0x2000,
                             GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                             "SaveData guest buffers"),
            "map SaveData guest buffers");
    CtrFsProfile fsProfile;
    fsProfile.SaveDataDirectory = directory;
    SourceCtrRuntimeProfile runtimeProfile;
    runtimeProfile.Fs = fsProfile;
    SourceCtrRuntime runtime(memory, std::move(runtimeProfile));
    CtrHandle fsHandle = 0;
    Require(runtime.Router().ConnectToPort(fsHandle, "fs:USER") == 0,
            "connect fs:USER for SaveData");

    std::array<std::uint32_t, 6> openArchive{
        0x080C00C2, 4, 1, 1, 0x4002, 0x4000};
    Require(runtime.Router().SendSyncRequest(fsHandle, openArchive) == 0 &&
                openArchive[0] == 0x080C00C0 && openArchive[1] == 0,
            "open SaveData archive");
    const std::uint64_t archiveHandle =
        openArchive[2] | (static_cast<std::uint64_t>(openArchive[3]) << 32U);

    constexpr std::array<std::uint8_t, 20> pathBytes{
        '/', 0, 't', 0, 'e', 0, 's', 0, 't', 0,
        '.', 0, 'd', 0, 'a', 0, 't', 0, 0, 0};
    auto pathTarget = memory.ResolveWrite(0x4100, pathBytes.size());
    std::memcpy(pathTarget.data(), pathBytes.data(), pathBytes.size());
    std::array<std::uint32_t, 10> openFile{
        0x080201C2, 0, static_cast<std::uint32_t>(archiveHandle),
        static_cast<std::uint32_t>(archiveHandle >> 32U), 4,
        static_cast<std::uint32_t>(pathBytes.size()), 3, 0,
        (static_cast<std::uint32_t>(pathBytes.size()) << 14U) | 2U, 0x4100};
    Require(runtime.Router().SendSyncRequest(fsHandle, openFile) == 0 &&
                openFile[0] == 0x08020042 && openFile[1] == 0 &&
                runtime.Router().Contains(openFile[3]),
            "open writable SaveData file");
    const CtrHandle fileHandle = openFile[3];

    constexpr std::array<std::uint8_t, 4> payload{9, 8, 7, 6};
    auto payloadTarget = memory.ResolveWrite(0x4200, payload.size());
    std::memcpy(payloadTarget.data(), payload.data(), payload.size());
    std::array<std::uint32_t, 7> write{
        0x08030102, 0, 0, static_cast<std::uint32_t>(payload.size()), 1,
        (static_cast<std::uint32_t>(payload.size()) << 4U) | 0xAU, 0x4200};
    Require(runtime.Router().SendSyncRequest(fileHandle, write) == 0 &&
                write[0] == 0x08030082 && write[2] == payload.size(),
            "write persistent SaveData payload");
    std::array<std::uint32_t, 3> resize{0x08050080, 6, 0};
    Require(runtime.Router().SendSyncRequest(fileHandle, resize) == 0 &&
                std::filesystem::file_size(directory / "test.dat") == 6,
            "resize persistent SaveData file");
    std::array<std::uint32_t, 2> flush{0x08090000, 0};
    Require(runtime.Router().SendSyncRequest(fileHandle, flush) == 0 &&
                flush[0] == 0x08090040,
            "flush persistent SaveData file");
    std::array<std::uint32_t, 6> read{
        0x080200C2, 0, 0, 4, (4U << 4U) | 0xCU, 0x4300};
    Require(runtime.Router().SendSyncRequest(fileHandle, read) == 0,
            "read persistent SaveData payload");
    const auto persisted = memory.ResolveRead(0x4300, 4);
    Require(std::memcmp(persisted.data(), payload.data(), payload.size()) == 0,
            "SaveData bytes survive write and resize");

    memory.ResolveWrite(0x4400, 1)[0] = std::byte{0x5a};
    memory.ResolveWrite(0x4410, 1)[0] = std::byte{0xff};
    std::array<std::uint32_t, 10> control{
        0x080D0144, static_cast<std::uint32_t>(archiveHandle),
        static_cast<std::uint32_t>(archiveHandle >> 32U), 0, 1, 1,
        0x1AU, 0x4400, 0x1CU, 0x4410};
    Require(runtime.Router().SendSyncRequest(fsHandle, control) == 0 &&
                control[0] == 0x080D0040 && control[1] == 0 &&
                memory.ResolveRead(0x4410, 1)[0] == std::byte{0},
            "control SaveData archive and initialize output");

    constexpr std::array<std::uint8_t, 22> escapePath{
        '/', 0, '.', 0, '.', 0, '/', 0, 'e', 0, 's', 0,
        'c', 0, 'a', 0, 'p', 0, 'e', 0, 0, 0};
    std::memcpy(memory.ResolveWrite(0x4500, escapePath.size()).data(),
                escapePath.data(), escapePath.size());
    std::array<std::uint32_t, 10> escapeOpen{
        0x080201C2, 0, static_cast<std::uint32_t>(archiveHandle),
        static_cast<std::uint32_t>(archiveHandle >> 32U), 4,
        static_cast<std::uint32_t>(escapePath.size()), 3, 0,
        (static_cast<std::uint32_t>(escapePath.size()) << 14U) | 2U, 0x4500};
    Require(runtime.Router().SendSyncRequest(fsHandle, escapeOpen) ==
                CtrIpcRouter::UnhandledResult &&
                !std::filesystem::exists(directory.parent_path() / "escape"),
            "reject SaveData path escaping archive root");

    std::array<std::uint32_t, 3> closeArchive{
        0x080E0080, static_cast<std::uint32_t>(archiveHandle),
        static_cast<std::uint32_t>(archiveHandle >> 32U)};
    Require(runtime.Router().SendSyncRequest(fsHandle, closeArchive) == 0 &&
                closeArchive[1] == 0,
            "close SaveData archive");
    closeArchive = {0x080E0080, static_cast<std::uint32_t>(archiveHandle),
                    static_cast<std::uint32_t>(archiveHandle >> 32U)};
    Require(runtime.Router().SendSyncRequest(fsHandle, closeArchive) == 0 &&
                closeArchive[1] == 0xC8804465,
            "reject stale SaveData archive handle");
    std::filesystem::remove_all(directory);
}

void TestSourcePicaSubmissionMemory() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(0x14000000U, 0x1000U,
                             GuestMemoryAccess::Read |
                                 GuestMemoryAccess::Write,
                             "linear_heap"),
            "map source PICA linear heap");
    Require(memory.MapZeroed(0x1F000000U, 0x1000U,
                             GuestMemoryAccess::Read |
                                 GuestMemoryAccess::Write,
                             "ctr_vram"),
            "map source PICA VRAM");
    memory.ResolveWrite(0x14000020U, 4U)[0] = std::byte{0x5a};

    constexpr std::array<SourceImageSystemRegion, 1> systemRegions{{
        {"ctr_vram", 0x1F000000U, 0x1000U, true, false, 0U, 0U},
    }};
    constexpr std::array<SourceImageZeroRegion, 1> zeroRegions{{
        {"linear_heap", 0x14000000U, 0x1000U, true, false},
    }};
    const SourceProcessImageDescriptor descriptor{
        {}, 0U, 0U, 0x1000U, {}, systemRegions, zeroRegions, {}, {}};
    SourcePicaSubmission submission(memory, descriptor);
    const auto physical = submission.Queue().PendingDraws();
    Require(physical.empty(), "source PICA queue starts empty");

    Oot3dNativeGame::Oot3dPicaPhysicalMemoryView view(
        {{0x20000000U, 0x14000000U, 0x1000U}},
        [&memory](std::uint32_t address, std::size_t size) {
            const auto bytes = memory.ResolveRead(address, size);
            return std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(bytes.data()),
                bytes.size());
        });
    const auto bytes = view.View(0x20000020U, 4U);
    Require(bytes.has_value() && bytes->size() == 4U && (*bytes)[0] == 0x5a,
            "source PICA view translates physical linear memory");
    Require(!view.View(0x20001000U, 1U).has_value(),
            "source PICA view rejects an unmapped physical address");
}

void TestSourceDspMixerMemory() {
    using namespace Oot3dSourceRuntime;
    MappedGuestAddressSpace memory;
    const auto access = GuestMemoryAccess::Read | GuestMemoryAccess::Write;
    Require(memory.MapZeroed(0x14000000U, 0x1000U, access, "linear_heap") &&
                memory.MapZeroed(0x1F000000U, 0x1000U, access, "ctr_vram") &&
                memory.MapZeroed(0x1FF50000U, 0x10000U, access,
                                 "dsp_shared_0") &&
                memory.MapZeroed(0x1FF70000U, 0x10000U, access,
                                 "dsp_shared_1"),
            "map source DSP fixture memory");
    constexpr std::array<SourceImageSystemRegion, 1> systemRegions{{
        {"ctr_vram", 0x1F000000U, 0x1000U, true, false, 0U, 0U},
    }};
    constexpr std::array<SourceImageZeroRegion, 1> zeroRegions{{
        {"linear_heap", 0x14000000U, 0x1000U, true, false},
    }};
    const SourceProcessImageDescriptor descriptor{
        {}, 0U, 0U, 0x1000U, {}, systemRegions, zeroRegions, {}, {}};
    SourceDspMixer mixer(memory, descriptor);
    std::vector<std::int16_t> samples;
    std::string error;
    Require(mixer.ProcessFrame(samples, &error),
            "source DSP mixer processes native shared memory");
    Require(samples.size() ==
                Oot3dNativeGame::NativeA32DspHle::SamplesPerFrame * 2U,
            "source DSP mixer emits one native stereo frame");
}

void TestHostMemsetContract() {
    std::array<std::uint8_t, 8> bytes{
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const auto result = Oot3dHostMemsetStandard(bytes.data() + 2, 0xA5, 4);
    Require(result == bytes.data() + 2,
            "host memset returns the original destination");
    Require(bytes == std::array<std::uint8_t, 8>{
                         0x11, 0x22, 0xA5, 0xA5,
                         0xA5, 0xA5, 0x77, 0x88},
            "host memset uses the standard value-size argument order");
}

void TestTargetFunctionOverlayInterposition() {
    constexpr std::uint32_t targetAddress = 0x00123450U;
    constexpr std::uint32_t targetSlotAddress = 0x00200010U;
    oot3d_host_register_target_function_overlay(
        targetAddress, reinterpret_cast<void*>(&ReplacementTargetFunction),
        "replacement");
    oot3d_host_register_target_function(
        targetAddress, reinterpret_cast<void*>(&OriginalTargetFunction),
        "original");
    oot3d_host_finalize_target_function_registry(1);

    Require(oot3d_host_resolve_target_function(targetAddress) ==
                reinterpret_cast<void*>(&ReplacementTargetFunction),
            "guest target resolves to source overlay");
    Require(oot3d_host_resolve_target_function(
                reinterpret_cast<std::uintptr_t>(&OriginalTargetFunction)) ==
                reinterpret_cast<void*>(&ReplacementTargetFunction),
            "linked baseline symbol resolves to source overlay");
    Require(oot3d_host_resolve_target_function(
                reinterpret_cast<std::uintptr_t>(&UnrelatedHostFunction)) ==
                reinterpret_cast<void*>(&UnrelatedHostFunction),
            "unrelated host symbol bypasses source overlay");

    Oot3dSourceRuntime::MappedGuestAddressSpace memory;
    Require(memory.MapZeroed(
                0x00200000U, 0x1000U,
                Oot3dSourceRuntime::GuestMemoryAccess::Read |
                    Oot3dSourceRuntime::GuestMemoryAccess::Write,
                "target slots"),
            "map target slot storage");
    auto slot = memory.ResolveWrite(targetSlotAddress, sizeof(targetAddress));
    Require(slot.size() == sizeof(targetAddress), "resolve target slot write");
    std::memcpy(slot.data(), &targetAddress, sizeof(targetAddress));
    const Oot3dSourceRuntime::ScopedSourceAddressSpace sourceScope(memory);
    Require(oot3d_host_resolve_target_function(targetSlotAddress) ==
                reinterpret_cast<void*>(&ReplacementTargetFunction),
            "legacy guest dispatch slot resolves its registered target");
}

} // namespace

int main() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    TestSourceExecutionStack();
#endif
    TestMappedAddressSpace();
    TestFunctionRegistry();
    TestSourceProcessImage();
    TestSourceDataBindings();
    TestCtrHostServices();
    TestSystemArenaArbiterBridge();
    TestCtrIpcRouter();
    TestCtrAptService();
    TestCtrHidService();
    TestCtrGspService();
    TestCtrPicaBackend();
    TestCtrDspService();
    TestCtrNdmService();
    TestCtrConfigService();
    TestCtrSrvService();
    TestSourceCtrRuntime();
    TestCtrRomFsRead();
    TestCtrSaveData();
    TestSourcePicaSubmissionMemory();
    TestSourceDspMixerMemory();
    TestHostMemsetContract();
    TestTargetFunctionOverlayInterposition();
    return 0;
}
