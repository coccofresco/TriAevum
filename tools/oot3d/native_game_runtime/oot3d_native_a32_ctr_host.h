#pragma once

#include "oot3d_native_a32_ctr_pica_bridge.h"
#include "oot3d_native_a32_input.h"
#include "oot3d_native_a32_process.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

class Oot3dNativePicaFrontend;
class Oot3dPicaCommandListCompositionProvider;
struct Oot3dGspCommandPacket;
enum class Oot3dPicaCompositionDomain : uint8_t;
enum class Oot3dPicaInterruptId : uint8_t;

struct NativeA32CtrSvcEvent {
    uint32_t Immediate = 0;
    uint32_t Pc = 0;
    uint32_t ThreadId = 0;
    bool Handled = false;
    std::string Name;
    uint32_t Detail = 0;
};

namespace detail {

// Keeps the diagnostic views consumed by the runtime status report without
// retaining every SVC. Full history remains available as an explicit opt-in
// for tools that serialize the complete trace.
class NativeA32CtrSvcEventHistory final {
  public:
    static constexpr size_t RecentEventCapacity = 32U;
    static constexpr size_t FilesystemEventCapacity = 64U;
    static constexpr size_t IpcEventCapacity = 128U;

    explicit NativeA32CtrSvcEventHistory(bool retainFullHistory = false,
                                         bool collectIpcNameCounts = false,
                                         size_t threadCapacityHint = 0U);

    void push_back(NativeA32CtrSvcEvent event);
    const std::vector<NativeA32CtrSvcEvent>& Events() const;
    const std::map<std::string, uint64_t>& IpcNameCounts() const noexcept;
    uint64_t TotalEventCount() const noexcept;
    bool RetainsFullHistory() const noexcept;

  private:
    struct RecordedEvent {
        uint64_t Sequence = 0;
        NativeA32CtrSvcEvent Event;
    };

    template <size_t Capacity> struct EventRing {
        std::array<RecordedEvent, Capacity> Records{};
        size_t Start = 0U;
        size_t Size = 0U;

        RecordedEvent* OldestIfFull() {
            return Size == Capacity ? &Records[Start] : nullptr;
        }

        void Push(uint64_t sequence, const NativeA32CtrSvcEvent& event) {
            const size_t slot =
                Size == Capacity ? Start : (Start + Size) % Capacity;
            if (Size == Capacity) {
                Start = (Start + 1U) % Capacity;
            } else {
                ++Size;
            }
            Records[slot].Sequence = sequence;
            Records[slot].Event = event;
        }

        void Push(uint64_t sequence, NativeA32CtrSvcEvent&& event) {
            const size_t slot =
                Size == Capacity ? Start : (Start + Size) % Capacity;
            if (Size == Capacity) {
                Start = (Start + 1U) % Capacity;
            } else {
                ++Size;
            }
            Records[slot].Sequence = sequence;
            Records[slot].Event = std::move(event);
        }
    };

    static bool IsFilesystemSummaryEvent(const NativeA32CtrSvcEvent& event);
    static bool IsIpcSummaryEvent(const NativeA32CtrSvcEvent& event);
    void EnsureThreadSlot(uint32_t threadId);

    bool mRetainFullHistory = false;
    bool mCollectIpcNameCounts = false;
    uint64_t mTotalEventCount = 0U;
    EventRing<RecentEventCapacity> mRecentEvents;
    EventRing<FilesystemEventCapacity> mFilesystemEvents;
    EventRing<IpcEventCapacity> mIpcEvents;
    std::vector<uint64_t> mLastSequenceByThread;
    std::vector<std::optional<RecordedEvent>> mArchivedLastByThread;
    std::map<std::string, uint64_t> mIpcNameCounts;
    std::vector<NativeA32CtrSvcEvent> mFullEvents;
    mutable std::vector<const RecordedEvent*> mSnapshotCandidates;
    mutable std::vector<NativeA32CtrSvcEvent> mSnapshot;
    mutable bool mSnapshotDirty = true;
};

} // namespace detail

struct NativeA32CtrFramebufferState {
    uint32_t AddressLeft = 0;
    uint32_t AddressRight = 0;
    uint32_t Stride = 0;
    uint32_t Format = 0;
    uint32_t BufferIndex = 0;
};

struct NativeA32CtrRuntimeProfile {
    uint64_t TriggerCommandQueueCalls = 0;
    uint64_t TriggerCommandQueueNanoseconds = 0;
    uint64_t CommandPackets = 0;
    uint64_t CommandListBytes = 0;
    uint64_t CommandListReadNanoseconds = 0;
    uint64_t PicaFrontendSubmitNanoseconds = 0;
    uint64_t RomFsStreamOpenCalls = 0;
    uint64_t RomFsReadCalls = 0;
    uint64_t RomFsReadBytes = 0;
    uint64_t RomFsReadNanoseconds = 0;
};

enum class NativeA32CtrHidUpdateStatus : uint8_t {
    NotReady,
    Idle,
    Updated,
    Failed,
};

struct NativeA32CtrHidUpdateResult {
    NativeA32CtrHidUpdateStatus Status = NativeA32CtrHidUpdateStatus::NotReady;
    uint32_t SamplesWritten = 0;
    uint32_t AccelerometerSamplesWritten = 0;
    uint32_t GyroscopeSamplesWritten = 0;
    bool EventsSignaled = false;
};

struct NativeA32CtrHidRuntimeProfile {
    bool SharedMemoryMapped = false;
    uint64_t SamplesWritten = 0;
    uint64_t EventBatchesSignaled = 0;
    uint64_t AccelerometerSamplesWritten = 0;
    uint64_t GyroscopeSamplesWritten = 0;
    uint64_t LastSampleTick = 0;
    uint32_t LastButtons = 0;
    uint32_t LastAdditions = 0;
    uint32_t LastRemovals = 0;
    int16_t LastCirclePadX = 0;
    int16_t LastCirclePadY = 0;
    std::array<int16_t, 3> LastAccelerometer{};
    std::array<int16_t, 3> LastGyroscope{};
    uint32_t NextPadIndex = 0;
    uint32_t NextAccelerometerIndex = 0;
    uint32_t NextGyroscopeIndex = 0;
};

enum class NativeA32CtrFilesystemRoot : uint8_t {
    Content,
    Save,
};

enum NativeA32CtrFileOpenFlags : uint32_t {
    NativeA32CtrFileOpenRead = 1U << 0U,
    NativeA32CtrFileOpenWrite = 1U << 1U,
    NativeA32CtrFileOpenCreate = 1U << 2U,
    NativeA32CtrFileOpenTruncate = 1U << 3U,
};

class NativeA32CtrFile {
  public:
    virtual ~NativeA32CtrFile() = default;

    virtual bool Read(uint64_t offset, std::span<uint8_t> destination,
                      uint32_t* returnedSize) = 0;
    virtual bool Write(uint64_t offset, std::span<const uint8_t> data,
                       uint32_t* writtenSize) = 0;
    virtual bool Resize(uint64_t size) = 0;
    virtual uint64_t Size() const noexcept = 0;
};

class NativeA32CtrFilesystem {
  public:
    virtual ~NativeA32CtrFilesystem() = default;

    virtual bool EnsureSaveRoot() = 0;
    virtual bool Open(NativeA32CtrFilesystemRoot root, std::string_view path,
                      uint32_t flags,
                      std::shared_ptr<NativeA32CtrFile>* file) = 0;
};

struct NativeA32CtrHostConfig {
    std::array<uint64_t, 10> ResourceLimitValues{};
    std::array<uint64_t, 10> ResourceCurrentValues{};
    uint32_t LinearHeapBaseAddress = 0;
    size_t LinearHeapSize = 0;
    uint32_t HeapBaseAddress = 0;
    size_t HeapSize = 0;
    std::filesystem::path RomFsImagePath;
    uint64_t RomFsImageOffset = 0;
    uint64_t RomFsImageSize = 0;
    NativeA32CtrFilesystem* Filesystem = nullptr;
    std::string RomFsContentPath = "inputs/romfs";
    Oot3dNativePicaFrontend* PicaFrontend = nullptr;
    NativeA32CtrPicaBridge* PicaBridge = nullptr;
    const Oot3dPicaCompositionDomain* PicaCompositionDomain = nullptr;
    Oot3dPicaCommandListCompositionProvider* PicaCompositionProvider = nullptr;
    std::filesystem::path SaveDataDirectory;
    bool HeadphonesConnected = false;
    uint8_t SoundOutputMode = 1;
    uint8_t SystemLanguage = 1;
    uint8_t SystemRegion = 2;
    std::array<float, 8> StereoCameraSettings{
        62.0F, 289.0F, 76.80000305175781F, 46.08000183105469F,
        10.0F, 5.0F,   55.58000183105469F, 21.56999969482422F,
    };
    float GyroscopeRawToDpsCoefficient = 14.375F;
    bool DetailedSvcDiagnostics = false;
    bool ProfileRuntime = false;
    std::array<int16_t, 9> GyroscopeCalibration{
        0, 6700, -6700, 0, 6700, -6700, 0, 6700, -6700,
    };
    bool RetainFullSvcHistory = false;
};

class NativeA32CtrHostServices final : public NativeA32HostServices {
  public:
    static constexpr uint64_t DspAudioFrameTicks = 160ULL * 4096ULL * 2ULL;
    static constexpr uint64_t HidPadUpdateTicks = 268111856ULL / 234ULL;
    static constexpr uint64_t HidAccelerometerUpdateTicks =
        268111856ULL / 104ULL;
    static constexpr uint64_t HidGyroscopeUpdateTicks = 268111856ULL / 101ULL;

    explicit NativeA32CtrHostServices(NativeA32CtrHostConfig config = {});
    NativeA32HostResult HandleSvc(uint32_t immediate,
                                  oot3d::recomp::a32::GuestState& state,
                                  NativeA32Memory& memory,
                                  NativeA32HostContext& context) override;
    NativeA32HostResult
    HandleFallback(oot3d::recomp::a32::FallbackReason reason, uint32_t pc,
                   const oot3d::recomp::a32::PackedOp& op,
                   oot3d::recomp::a32::GuestState& state,
                   NativeA32Memory& memory) override;
    bool CompleteSynchronousWait(uint32_t immediate,
                                 const NativeA32HostResult& waitResult,
                                 oot3d::recomp::a32::GuestState& state,
                                 NativeA32Memory& memory,
                                 NativeA32HostContext& context,
                                 std::string* error = nullptr) override;

    const std::vector<NativeA32CtrSvcEvent>& SvcEvents() const;
    const std::map<std::string, uint64_t>& IpcNameCounts() const noexcept;
    uint64_t SvcEventCount() const noexcept;
    void AdvanceSystemTicks(uint64_t ticks);
    std::optional<uint64_t> NextSleepWakeTick() const;
    size_t PendingSleepCount() const;
    uint32_t TakePendingDspAudioFrames();
    bool SignalDspAudioFrame();
    NativeA32CtrHidUpdateResult
    AdvanceHidToCurrentTick(NativeA32Memory& memory,
                            const NativeA32HidState& state);
    NativeA32CtrHidRuntimeProfile HidRuntimeProfile() const noexcept;
    uint64_t SystemTicks() const;
    uint32_t FallbackPc() const;
    oot3d::recomp::a32::FallbackReason FallbackReason() const;
    uint32_t FallbackRawOperation() const;
    const std::vector<uint8_t>& DspComponent() const;
    uint16_t DspProgramMask() const;
    uint16_t DspDataMask() const;
    uint16_t DspSemaphoreMask() const;
    uint16_t DspSemaphoreValue() const;
    bool SignalDspInterrupt(uint32_t type, uint32_t channel);
    bool SignalPicaInterrupt(NativeA32Memory& memory,
                             Oot3dPicaInterruptId interrupt);
    bool SignalVBlank(NativeA32Memory& memory);
    std::optional<NativeA32CtrFramebufferState>
    Framebuffer(uint32_t screenId) const;
    std::optional<NativeA32CtrFramebufferState> TopFramebuffer() const;
    std::optional<NativeA32CtrFramebufferState> BottomFramebuffer() const;
    uint32_t GspPriority() const;
    uint32_t GspPriorityWithRights() const;
    bool LcdForceBlack() const;
    bool HidAccelerometerEnabled() const;
    bool HidGyroscopeEnabled() const;
    bool NdmSchedulerSuspended() const;
    bool NdmSchedulerRunsInBackground() const;
    NativeA32CtrRuntimeProfile RuntimeProfile() const noexcept;
    nlohmann::json CaptureState() const;
    bool RestoreState(const nlohmann::json& state, NativeA32Process& process,
                      std::string* error = nullptr);

  private:
    struct RomFsBackingFile;

    struct KernelObject {
        std::string Type;
        std::optional<uint32_t> ThreadId;
        std::optional<uint32_t> OwnerThreadId;
        uint32_t RecursionCount = 0;
        int64_t AvailableCount = 0;
        uint32_t ResetType = 0;
        std::filesystem::path FilePath;
        uint64_t FileOffset = 0;
        uint64_t FileSize = 0;
        uint32_t FileOpenMode = 0;
        std::shared_ptr<NativeA32CtrFile> FileBackend;
        std::shared_ptr<RomFsBackingFile> RomFsBacking;
        std::optional<uint32_t> MappedAddress;
    };

    struct HandleRecord {
        uint32_t Value = 0;
        std::shared_ptr<KernelObject> Object;
    };

    struct ArbiterWaiter {
        std::shared_ptr<KernelObject> Arbiter;
        NativeA32Process* Process = nullptr;
        uint32_t ThreadId = 0;
        uint32_t Address = 0;
    };

    struct SynchronizationWaiter {
        std::vector<std::shared_ptr<KernelObject>> Objects;
        NativeA32Process* Process = nullptr;
        uint32_t ThreadId = 0;
        bool WaitAll = false;
    };

    struct SleepWaiter {
        NativeA32Process* Process = nullptr;
        uint32_t ThreadId = 0;
        uint64_t WakeTick = 0;
    };

    struct ArchiveRecord {
        uint64_t Handle = 0;
        uint32_t Id = 0;
        std::filesystem::path Root;
    };

    struct GspFramebufferSlot {
        uint32_t AddressLeft = 0;
        uint32_t AddressRight = 0;
        uint32_t Stride = 0;
        uint32_t Format = 0;
        bool Configured = false;
    };

    struct GspScreenState {
        std::array<GspFramebufferSlot, 2> Slots{};
        uint32_t ShownBuffer = 0;
    };

    uint32_t CreateHandle(std::string type,
                          std::optional<uint32_t> threadId = std::nullopt);
    uint32_t CreateHandle(const std::shared_ptr<KernelObject>& object);
    const HandleRecord* LookupHandle(uint32_t handle) const;
    bool IsHandleType(uint32_t handle, const char* type) const;
    std::optional<std::string> LookupHandleType(uint32_t handle) const;
    bool CloseHandle(uint32_t handle);
    bool IsKernelObjectReady(const KernelObject& object, uint32_t threadId,
                             const NativeA32Process& process) const;
    void AcquireKernelObject(KernelObject& object, uint32_t threadId);
    void WakeSynchronizationWaiters();
    void WakeSleepWaiters();
    bool QueueGspInterrupt(NativeA32Memory& memory, uint8_t interruptId);
    bool ApplyGspFramebufferUpdate(NativeA32Memory& memory, uint32_t screenId);
    bool SetGspFramebuffer(uint32_t screenId, uint32_t activeBuffer,
                           uint32_t addressLeft, uint32_t addressRight,
                           uint32_t stride, uint32_t format,
                           uint32_t shownBuffer);
    bool HasPicaBackend() const noexcept;
    bool WritePicaHardwareRegisters(uint32_t base,
                                    std::span<const uint32_t> values,
                                    std::span<const uint32_t> masks,
                                    std::string* error);
    bool SubmitPicaGspCommand(const Oot3dGspCommandPacket& packet,
                              std::span<const uint32_t> commandWords,
                              NativeA32CtrPicaSubmissionResult* result,
                              std::string* error);
    bool TakePicaPendingInterrupts(std::vector<uint8_t>* interrupts,
                                   std::string* error);
    NativeA32HostResult HandleResourceLimitValues(
        bool current, oot3d::recomp::a32::GuestState& state,
        NativeA32Memory& memory, NativeA32CtrSvcEvent event);
    NativeA32HostResult
    HandleControlMemory(oot3d::recomp::a32::GuestState& state,
                        NativeA32Memory& memory, NativeA32CtrSvcEvent event);
    bool WriteHidSample(NativeA32Memory& memory, const NativeA32HidState& state,
                        uint64_t sampleTick);
    bool WriteHidAccelerometerSample(NativeA32Memory& memory,
                                     const NativeA32HidState& state,
                                     uint64_t sampleTick);
    bool WriteHidGyroscopeSample(NativeA32Memory& memory,
                                 const NativeA32HidState& state,
                                 uint64_t sampleTick);

    detail::NativeA32CtrSvcEventHistory mSvcEvents;
    NativeA32CtrHostConfig mConfig;
    NativeA32CtrRuntimeProfile mRuntimeProfile;
    std::vector<HandleRecord> mHandles;
    std::vector<ArbiterWaiter> mArbiterWaiters;
    std::vector<SynchronizationWaiter> mSynchronizationWaiters;
    std::vector<SleepWaiter> mSleepWaiters;
    std::vector<ArchiveRecord> mArchives;
    uint16_t mNextSlot = 0;
    uint16_t mNextGeneration = 1;
    uint32_t mFallbackPc = 0;
    oot3d::recomp::a32::FallbackReason mFallbackReason =
        oot3d::recomp::a32::FallbackReason::None;
    uint32_t mFallbackRawOperation = 0;
    uint32_t mLinearHeapCursor = 0;
    uint32_t mAllocationIndex = 0;
    uint64_t mNextArchiveHandle = 1;
    uint64_t mSystemTicks = 0;
    std::shared_ptr<KernelObject> mAptLock;
    std::shared_ptr<KernelObject> mAptNotificationEvent;
    std::shared_ptr<KernelObject> mAptParameterEvent;
    std::shared_ptr<KernelObject> mGpuRightOwner;
    std::shared_ptr<KernelObject> mGspInterruptEvent;
    std::shared_ptr<KernelObject> mGspSharedMemory;
    uint32_t mGspThreadId = 0;
    uint32_t mGspPriority = 0;
    uint32_t mGspPriorityWithRights = 0;
    std::array<GspScreenState, 2> mGspScreens{};
    bool mLcdForceBlack = true;
    uint32_t mAptApplicationId = 0;
    bool mAptWakeupPending = false;
    std::vector<uint8_t> mDspComponent;
    uint16_t mDspProgramMask = 0;
    uint16_t mDspDataMask = 0;
    uint16_t mDspSemaphoreMask = 0;
    uint16_t mDspSemaphoreValue = 0;
    bool mDspAudioRunning = false;
    uint64_t mNextDspAudioFrameTick = 0;
    uint32_t mPendingDspAudioFrames = 0;
    std::array<std::array<std::shared_ptr<KernelObject>, 8>, 3>
        mDspInterruptEvents{};
    std::shared_ptr<KernelObject> mDspSemaphoreEvent;
    std::array<std::vector<uint8_t>, 8> mDspPipeOutput;
    std::shared_ptr<KernelObject> mHidSharedMemory;
    std::array<std::shared_ptr<KernelObject>, 5> mHidEvents{};
    NativeA32CtrHidRuntimeProfile mHidRuntimeProfile;
    uint64_t mNextHidPadTick = 0;
    uint64_t mNextHidAccelerometerTick = 0;
    uint64_t mNextHidGyroscopeTick = 0;
    uint32_t mNextHidPadIndex = 0;
    uint32_t mNextHidTouchIndex = 0;
    uint32_t mNextHidAccelerometerIndex = 0;
    uint32_t mNextHidGyroscopeIndex = 0;
    bool mHidScheduleInitialized = false;
    bool mHidAccelerometerScheduleInitialized = false;
    bool mHidGyroscopeScheduleInitialized = false;
    uint32_t mHidAccelerometerEnableCount = 0;
    uint32_t mHidGyroscopeEnableCount = 0;
    bool mNdmSchedulerSuspended = false;
    bool mNdmSchedulerRunsInBackground = false;
};

} // namespace Oot3dNativeGame
