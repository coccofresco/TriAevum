#pragma once

#include "oot3d_guest_address_space.h"
#include "oot3d_source_process_image.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace Oot3dSourceRuntime {

using CtrHandle = std::uint32_t;
using CtrResult = std::int32_t;

class CtrHostServices {
  public:
    virtual ~CtrHostServices() = default;

    virtual CtrResult SendSyncRequest(
        CtrHandle handle, std::span<std::uint32_t> commandBuffer) = 0;
    virtual CtrResult CloseHandle(CtrHandle handle) = 0;
    virtual CtrResult DuplicateHandle(CtrHandle source,
                                      CtrHandle& outHandle) = 0;
    virtual CtrResult ConnectToPort(CtrHandle& outHandle,
                                    std::string_view portName) = 0;
    virtual CtrResult SignalObject(CtrHandle handle,
                                   std::uint32_t releaseCount = 1) = 0;
    virtual CtrResult ClearEvent(CtrHandle handle) = 0;
    virtual CtrResult ReleaseMutex(CtrHandle handle,
                                   std::uint64_t threadId) = 0;
    virtual CtrResult CreateEvent(CtrHandle& outHandle,
                                  std::uint32_t resetType) = 0;
    virtual CtrResult CreateAddressArbiter(CtrHandle& outHandle) = 0;
    virtual CtrResult GetResourceLimit(CtrHandle& outHandle,
                                       CtrHandle process) = 0;
    virtual std::int64_t GetResourceLimitCurrentValue(
        CtrHandle resourceLimit, std::uint32_t type) = 0;
    virtual CtrResult ArbitrateAddress(CtrHandle arbiter,
                                       GuestAddressSpace& memory,
                                       GuestAddress address,
                                       std::uint32_t type,
                                       std::int32_t value) = 0;
    virtual CtrResult ControlMemory(GuestAddressSpace& memory,
                                    GuestAddress& outAddress,
                                    GuestAddress address0,
                                    GuestAddress address1,
                                    std::uint32_t size,
                                    std::uint32_t operation,
                                    std::uint32_t permissions) = 0;
    virtual CtrResult MapMemoryBlock(CtrHandle memoryBlock,
                                     GuestAddressSpace& memory,
                                     GuestAddress address,
                                     std::uint32_t permissions,
                                     std::uint32_t otherPermissions) = 0;
    virtual CtrResult UnmapMemoryBlock(CtrHandle memoryBlock,
                                       GuestAddressSpace& memory,
                                       GuestAddress address) = 0;
    virtual CtrResult CreateThread(CtrHandle& outHandle,
                                   GuestAddress entry,
                                   GuestAddress argument,
                                   GuestAddress stackTop,
                                   std::int32_t priority,
                                   std::int32_t processorId) = 0;
};

struct CtrHostBinding;

class ScopedCtrHostServices {
  public:
    ScopedCtrHostServices(GuestAddressSpace& memory,
                          const SourcePrimaryThreadDescriptor& primaryThread,
                          CtrHostServices& services);
    ~ScopedCtrHostServices();

    ScopedCtrHostServices(const ScopedCtrHostServices&) = delete;
    ScopedCtrHostServices& operator=(const ScopedCtrHostServices&) = delete;

  private:
    CtrHostBinding* mPrevious = nullptr;
    CtrHostBinding* mBinding = nullptr;
};

} // namespace Oot3dSourceRuntime

extern "C" {
std::uint32_t* oot3d_host_target_leaf_command_buffer();
std::int32_t oot3d_host_target_leaf_send_sync_request(std::uint32_t handle);
void oot3d_host_target_leaf_close_handle(std::uint32_t handle);
std::int32_t svcSendSyncRequest(std::uint32_t handle);
std::int32_t svcCloseHandle(std::uint32_t handle);
std::int32_t oot3d_host_ctr_duplicate_handle(std::uint32_t* outHandle,
                                              std::uint32_t sourceHandle);
std::int32_t oot3d_host_ctr_connect_to_port(std::uint32_t* outHandle,
                                            const char* portName);
std::int32_t oot3d_host_ctr_sleep_thread(std::int64_t nanoseconds);
std::int32_t oot3d_host_ctr_signal_event(std::uint32_t event);
std::int32_t oot3d_host_ctr_clear_event(std::uint32_t event);
std::int32_t oot3d_host_ctr_release_mutex(std::uint32_t mutex);
std::int32_t oot3d_host_ctr_create_event(std::uint32_t* outHandle,
                                         std::uint32_t resetType);
std::int32_t oot3d_host_ctr_create_address_arbiter(std::uint32_t* outHandle);
std::int32_t oot3d_host_ctr_get_resource_limit(std::uint32_t* outHandle,
                                               std::uint32_t process);
std::int64_t oot3d_host_ctr_resource_limit_current_value(
    std::uint32_t resourceLimit, std::uint32_t type);
std::uint32_t oot3d_host_target_thread_pointer();
std::uint64_t oot3d_host_target_system_tick();
void* oot3d_host_owner_actor_global_resolve(std::uint32_t address);
std::uint64_t oot3d_host_owner_actor_system_tick();
std::uint32_t oot3d_host_system_arena_current_thread_id();
void oot3d_host_system_arena_wait(void* lock);
void oot3d_host_system_arena_wake_one(void* lock);
std::int32_t oot3d_host_ctr_arbitrate_address(std::uint32_t arbiter,
                                              std::uint32_t address,
                                              std::uint32_t type,
                                              std::int32_t value);
std::uint32_t* getThreadCommandBuffer();
std::int32_t oot3d_host_ctr_control_memory(
    std::uint32_t* outAddress, std::uint32_t address0,
    std::uint32_t address1, std::uint32_t size,
    std::uint32_t operation, std::uint32_t permissions);
std::int32_t oot3d_host_ctr_map_memory_block(
    std::uint32_t memoryBlock, std::uint32_t address,
    std::uint32_t permissions, std::uint32_t otherPermissions);
std::int32_t oot3d_host_ctr_unmap_memory_block(
    std::uint32_t memoryBlock, std::uint32_t address);
std::int32_t oot3d_host_ctr_create_thread(
    std::uint32_t* outHandle, std::uint32_t entry,
    std::uint32_t argument, std::uint32_t stackTop,
    std::int32_t priority, std::int32_t processorId);
[[noreturn]] void oot3d_host_ctr_exit_process();
[[noreturn]] void oot3d_host_ctr_exit_thread();
}
