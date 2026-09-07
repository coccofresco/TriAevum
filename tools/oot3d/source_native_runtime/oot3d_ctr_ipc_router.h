#pragma once

#include "oot3d_ctr_host_services.h"
#include "oot3d_ctr_kernel_object.h"

#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dSourceRuntime {

class CtrIpcSession {
  public:
    virtual ~CtrIpcSession() = default;
    virtual CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) = 0;
};

struct CtrIpcEvent {
    enum class Kind : std::uint8_t {
        SendSyncRequest,
        CloseHandle,
    };

    Kind Operation = Kind::SendSyncRequest;
    CtrHandle Handle = 0;
    std::uint32_t CommandHeader = 0;
    CtrResult Result = 0;
    bool Handled = false;
    std::string SessionName;
};

class CtrIpcRouter final : public CtrHostServices {
  public:
    // Host-only failure used until a service-specific CTR result is known.
    static constexpr CtrResult UnhandledResult =
        static_cast<CtrResult>(0x80000000U);

    bool RegisterSession(CtrHandle handle, std::string name,
                         std::shared_ptr<CtrIpcSession> session);
    bool RegisterPort(std::string name, std::shared_ptr<CtrIpcSession> session);
    CtrResult OpenSession(std::string name,
                          std::shared_ptr<CtrIpcSession> session,
                          CtrHandle& outHandle);
    CtrResult OpenKernelObject(std::shared_ptr<CtrKernelObject> object,
                               CtrHandle& outHandle);
    CtrResult DuplicateHandle(CtrHandle source,
                              CtrHandle& outHandle) override;
    std::shared_ptr<CtrKernelObject> KernelObject(CtrHandle handle) const;
    CtrResult MapSharedMemory(CtrHandle handle, GuestAddressSpace& memory,
                              GuestAddress address);
    CtrResult SignalObject(CtrHandle handle,
                           std::uint32_t releaseCount = 1) override;
    CtrResult ClearEvent(CtrHandle handle) override;
    CtrResult ReleaseMutex(CtrHandle handle,
                           std::uint64_t threadId) override;
    CtrResult CreateEvent(CtrHandle& outHandle,
                          std::uint32_t resetType) override;
    CtrResult CreateAddressArbiter(CtrHandle& outHandle) override;
    CtrResult GetResourceLimit(CtrHandle& outHandle,
                               CtrHandle process) override;
    std::int64_t GetResourceLimitCurrentValue(
        CtrHandle resourceLimit, std::uint32_t type) override;
    CtrResult ArbitrateAddress(CtrHandle arbiter,
                               GuestAddressSpace& memory,
                               GuestAddress address,
                               std::uint32_t type,
                               std::int32_t value) override;
    CtrResult ControlMemory(GuestAddressSpace& memory,
                            GuestAddress& outAddress,
                            GuestAddress address0,
                            GuestAddress address1,
                            std::uint32_t size,
                            std::uint32_t operation,
                            std::uint32_t permissions) override;
    CtrResult MapMemoryBlock(CtrHandle memoryBlock,
                             GuestAddressSpace& memory,
                             GuestAddress address,
                             std::uint32_t permissions,
                             std::uint32_t otherPermissions) override;
    CtrResult UnmapMemoryBlock(CtrHandle memoryBlock,
                               GuestAddressSpace& memory,
                               GuestAddress address) override;
    CtrResult CreateThread(CtrHandle& outHandle,
                           GuestAddress entry,
                           GuestAddress argument,
                           GuestAddress stackTop,
                           std::int32_t priority,
                           std::int32_t processorId) override;
    void ConfigureThreadRuntime(GuestAddressSpace& memory,
                                SourcePrimaryThreadDescriptor primaryThread);

    struct WaitResult {
        CtrResult Result = UnhandledResult;
        std::uint32_t SelectedIndex = 0;
        bool Ready = false;
    };
    WaitResult WaitSynchronization(std::span<const CtrHandle> handles,
                                   bool waitAll, std::uint64_t threadId);
    bool Contains(CtrHandle handle) const;
    std::optional<std::string_view> SessionName(CtrHandle handle) const;
    std::size_t SessionCount() const;

    CtrResult SendSyncRequest(
        CtrHandle handle, std::span<std::uint32_t> commandBuffer) override;
    CtrResult CloseHandle(CtrHandle handle) override;
    CtrResult ConnectToPort(CtrHandle& outHandle,
                            std::string_view portName) override;

    const std::vector<CtrIpcEvent>& Events() const;
    void ClearEvents();

  private:
    struct SessionRecord {
        std::string Name;
        std::shared_ptr<CtrIpcSession> Session;
    };

    std::vector<std::pair<CtrHandle, SessionRecord>> mSessions;
    std::vector<std::pair<CtrHandle, std::shared_ptr<CtrKernelObject>>> mObjects;
    std::vector<std::pair<std::string, std::shared_ptr<CtrIpcSession>>> mPorts;
    std::vector<CtrIpcEvent> mEvents;
    CtrHandle mNextHandle = 0x100;
    std::mutex mArbitrationMutex;
    std::condition_variable mArbitrationChanged;
    std::vector<std::pair<GuestAddress, std::uint64_t>> mArbitrationGeneration;
    mutable std::recursive_mutex mStateMutex;
    GuestAddressSpace* mThreadMemory = nullptr;
    SourcePrimaryThreadDescriptor mPrimaryThread{};
    GuestAddress mNextThreadTls = 0;
    std::uint64_t mCommittedBytes = 0;

    std::optional<CtrHandle> AllocateHandle();
};

} // namespace Oot3dSourceRuntime
