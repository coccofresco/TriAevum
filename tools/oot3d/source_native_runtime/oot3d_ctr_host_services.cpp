#include "oot3d_ctr_host_services.h"
#include "oot3d_source_execution_stack.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace Oot3dSourceRuntime {
namespace {

// libctru exposes the IPC command buffer at TLS + 0x80.
constexpr std::size_t kCtrCommandBufferOffset = 0x80;
constexpr std::uint64_t kCtrSystemTickFrequency = 268123480ULL;
std::mutex gArenaWaitMutex;
std::condition_variable gArenaWaitChanged;

} // namespace

struct CtrHostBinding {
    GuestAddressSpace* Memory = nullptr;
    SourcePrimaryThreadDescriptor PrimaryThread;
    CtrHostServices* Services = nullptr;
};

thread_local CtrHostBinding* gCtrBinding = nullptr;

ScopedCtrHostServices::ScopedCtrHostServices(
    GuestAddressSpace& memory,
    const SourcePrimaryThreadDescriptor& primaryThread,
    CtrHostServices& services)
    : mPrevious(gCtrBinding),
      mBinding(new CtrHostBinding{&memory, primaryThread, &services}) {
    gCtrBinding = mBinding;
}

ScopedCtrHostServices::~ScopedCtrHostServices() {
    gCtrBinding = mPrevious;
    delete mBinding;
}

static std::span<std::uint32_t> CurrentCommandBuffer() {
    if (gCtrBinding == nullptr ||
        gCtrBinding->PrimaryThread.TlsSize <= kCtrCommandBufferOffset) {
        return {};
    }
    const GuestAddress address =
        gCtrBinding->PrimaryThread.TlsBaseAddress + kCtrCommandBufferOffset;
    const std::size_t byteSize =
        gCtrBinding->PrimaryThread.TlsSize - kCtrCommandBufferOffset;
    auto bytes = gCtrBinding->Memory->ResolveWrite(address, byteSize);
    if (bytes.size() != byteSize ||
        reinterpret_cast<std::uintptr_t>(bytes.data()) % alignof(std::uint32_t) != 0) {
        return {};
    }
    return {reinterpret_cast<std::uint32_t*>(bytes.data()),
            bytes.size() / sizeof(std::uint32_t)};
}

} // namespace Oot3dSourceRuntime

extern "C" std::uint32_t* oot3d_host_target_leaf_command_buffer() {
    auto commandBuffer = Oot3dSourceRuntime::CurrentCommandBuffer();
    if (commandBuffer.empty()) {
        std::abort();
    }
    return commandBuffer.data();
}

extern "C" std::uint32_t* getThreadCommandBuffer() {
    return oot3d_host_target_leaf_command_buffer();
}

extern "C" std::int32_t oot3d_host_target_leaf_send_sync_request(
    std::uint32_t handle) {
    using namespace Oot3dSourceRuntime;
    auto commandBuffer = CurrentCommandBuffer();
    if (gCtrBinding == nullptr || commandBuffer.empty()) {
        std::abort();
    }
    return gCtrBinding->Services->SendSyncRequest(handle, commandBuffer);
}

extern "C" void oot3d_host_target_leaf_close_handle(std::uint32_t handle) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) {
        std::abort();
    }
    (void)gCtrBinding->Services->CloseHandle(handle);
}

extern "C" std::int32_t svcSendSyncRequest(std::uint32_t handle) {
    return oot3d_host_target_leaf_send_sync_request(handle);
}

extern "C" std::int32_t svcCloseHandle(std::uint32_t handle) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) {
        std::abort();
    }
    return gCtrBinding->Services->CloseHandle(handle);
}

extern "C" std::int32_t oot3d_host_ctr_duplicate_handle(
    std::uint32_t* outHandle, std::uint32_t sourceHandle) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr || outHandle == nullptr) {
        std::abort();
    }
    return gCtrBinding->Services->DuplicateHandle(sourceHandle, *outHandle);
}

extern "C" std::int32_t oot3d_host_ctr_connect_to_port(
    std::uint32_t* outHandle, const char* portName) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr || outHandle == nullptr || portName == nullptr) {
        std::abort();
    }
    return gCtrBinding->Services->ConnectToPort(*outHandle, portName);
}

extern "C" std::int32_t oot3d_host_ctr_sleep_thread(
    std::int64_t nanoseconds) {
    if (nanoseconds != 0) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(nanoseconds));
    } else {
        std::this_thread::yield();
    }
    return 0;
}

extern "C" std::int32_t oot3d_host_ctr_signal_event(std::uint32_t event) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) std::abort();
    return gCtrBinding->Services->SignalObject(event);
}

extern "C" std::int32_t oot3d_host_ctr_clear_event(std::uint32_t event) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) std::abort();
    return gCtrBinding->Services->ClearEvent(event);
}

extern "C" std::int32_t oot3d_host_ctr_release_mutex(std::uint32_t mutex) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) std::abort();
    return gCtrBinding->Services->ReleaseMutex(
        mutex, gCtrBinding->PrimaryThread.ThreadPointer);
}

extern "C" std::int32_t oot3d_host_ctr_create_event(
    std::uint32_t* outHandle, std::uint32_t resetType) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr || outHandle == nullptr) std::abort();
    return gCtrBinding->Services->CreateEvent(*outHandle, resetType);
}

extern "C" std::int32_t oot3d_host_ctr_create_address_arbiter(
    std::uint32_t* outHandle) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr || outHandle == nullptr) std::abort();
    return gCtrBinding->Services->CreateAddressArbiter(*outHandle);
}

extern "C" std::int32_t oot3d_host_ctr_get_resource_limit(
    std::uint32_t* outHandle, std::uint32_t process) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr || outHandle == nullptr) std::abort();
    return gCtrBinding->Services->GetResourceLimit(*outHandle, process);
}

extern "C" std::int64_t oot3d_host_ctr_resource_limit_current_value(
    std::uint32_t resourceLimit, std::uint32_t type) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) std::abort();
    return gCtrBinding->Services->GetResourceLimitCurrentValue(resourceLimit, type);
}

extern "C" std::uint32_t oot3d_host_target_thread_pointer() {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) std::abort();
    return gCtrBinding->PrimaryThread.ThreadPointer;
}

extern "C" std::uint64_t oot3d_host_target_system_tick() {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(nanoseconds) *
         Oot3dSourceRuntime::kCtrSystemTickFrequency) / 1000000000ULL);
}

extern "C" void* oot3d_host_owner_actor_global_resolve(std::uint32_t address) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
}

extern "C" std::uint64_t oot3d_host_owner_actor_system_tick() {
    return oot3d_host_target_system_tick();
}

extern "C" std::uint32_t oot3d_host_system_arena_current_thread_id() {
    return oot3d_host_target_thread_pointer();
}

extern "C" void oot3d_host_system_arena_wait(void* lock) {
    using namespace Oot3dSourceRuntime;
    if (lock == nullptr) return;
    std::unique_lock guard(gArenaWaitMutex);
    auto& lockWord = *reinterpret_cast<std::int32_t*>(lock);
    std::atomic_ref<std::int32_t> state(lockWord);
    auto observed = state.load(std::memory_order_acquire);
    while (observed < 0) {
        const auto decremented = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(observed) - 1U);
        if (!state.compare_exchange_weak(
                observed, decremented, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            continue;
        }
        gArenaWaitChanged.wait(guard, [&state] {
            return state.load(std::memory_order_acquire) > 0;
        });
        return;
    }
}

extern "C" void oot3d_host_system_arena_wake_one(void*) {
    std::lock_guard guard(Oot3dSourceRuntime::gArenaWaitMutex);
    Oot3dSourceRuntime::gArenaWaitChanged.notify_one();
}

extern "C" std::int32_t oot3d_host_ctr_arbitrate_address(
    std::uint32_t arbiter, std::uint32_t address, std::uint32_t type,
    std::int32_t value) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) std::abort();
    return gCtrBinding->Services->ArbitrateAddress(
        arbiter,*gCtrBinding->Memory,address,type,value);
}

extern "C" std::int32_t oot3d_host_ctr_control_memory(
    std::uint32_t* outAddress, std::uint32_t address0,
    std::uint32_t address1, std::uint32_t size,
    std::uint32_t operation, std::uint32_t permissions) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr || outAddress == nullptr) std::abort();
    return gCtrBinding->Services->ControlMemory(
        *gCtrBinding->Memory,*outAddress,address0,address1,size,
        operation,permissions);
}

extern "C" std::int32_t oot3d_host_ctr_map_memory_block(
    std::uint32_t memoryBlock, std::uint32_t address,
    std::uint32_t permissions, std::uint32_t otherPermissions) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) std::abort();
    return gCtrBinding->Services->MapMemoryBlock(
        memoryBlock,*gCtrBinding->Memory,address,permissions,otherPermissions);
}

extern "C" std::int32_t oot3d_host_ctr_unmap_memory_block(
    std::uint32_t memoryBlock, std::uint32_t address) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr) std::abort();
    return gCtrBinding->Services->UnmapMemoryBlock(
        memoryBlock,*gCtrBinding->Memory,address);
}

extern "C" std::int32_t oot3d_host_ctr_create_thread(
    std::uint32_t* outHandle, std::uint32_t entry,
    std::uint32_t argument, std::uint32_t stackTop,
    std::int32_t priority, std::int32_t processorId) {
    using namespace Oot3dSourceRuntime;
    if (gCtrBinding == nullptr || outHandle == nullptr) std::abort();
    return gCtrBinding->Services->CreateThread(
        *outHandle,entry,argument,stackTop,priority,processorId);
}

extern "C" [[noreturn]] void oot3d_host_ctr_exit_process() {
    std::exit(EXIT_SUCCESS);
}

extern "C" [[noreturn]] void oot3d_host_ctr_exit_thread() {
    Oot3dSourceRuntime::ExitCurrentSourceThread();
}
