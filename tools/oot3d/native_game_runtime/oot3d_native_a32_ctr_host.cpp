#include "oot3d_native_a32_ctr_host.h"

#if !defined(OOT3D_CTR_HOST_SERVICE_PICA_ONLY)
#include "oot3d_native_pica_frontend.h"
#endif
#include "oot3d_native_pica_transfer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kResultSuccess = 0;
using RuntimeProfileClock = std::chrono::steady_clock;

uint64_t
RuntimeProfileElapsedNanoseconds(RuntimeProfileClock::time_point start) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            RuntimeProfileClock::now() - start)
            .count());
}

class RuntimeProfileScope {
  public:
    RuntimeProfileScope(bool enabled, uint64_t& accumulator)
        : mAccumulator(enabled ? &accumulator : nullptr),
          mStart(enabled ? RuntimeProfileClock::now()
                         : RuntimeProfileClock::time_point{}) {}

    ~RuntimeProfileScope() {
        if (mAccumulator != nullptr) {
            *mAccumulator += RuntimeProfileElapsedNanoseconds(mStart);
        }
    }

  private:
    uint64_t* mAccumulator;
    RuntimeProfileClock::time_point mStart;
};

constexpr uint32_t kCurrentProcessHandle = 0xFFFF8001U;
constexpr uint32_t kCurrentThreadHandle = 0xFFFF8000U;
constexpr size_t kResourceAddressArbiter = 8;
constexpr size_t kResourceCommit = 1;
constexpr size_t kResourceEvent = 3;
constexpr size_t kResourceSemaphore = 5;
constexpr size_t kResourceThread = 2;
constexpr uint32_t kMemoryOperationMask = 0xFFU;
constexpr uint32_t kMemoryOperationCommit = 3U;
constexpr uint32_t kMemoryOperationLinear = 0x10000U;
constexpr uint64_t kCtrArm11TicksPerSecond = 268111856ULL;
constexpr uint64_t kNanosecondsPerSecond = 1'000'000'000ULL;
constexpr uint32_t kSrvRegisterClientRequest = 0x00010002U;
constexpr uint32_t kSrvRegisterClientResponse = 0x00010040U;
constexpr uint32_t kSrvEnableNotificationRequest = 0x00020000U;
constexpr uint32_t kSrvEnableNotificationResponse = 0x00020042U;
constexpr uint32_t kSrvGetServiceHandleRequest = 0x00050100U;
constexpr uint32_t kSrvGetServiceHandleResponse = 0x00050042U;
constexpr uint32_t kAptGetLockHandleRequest = 0x00010040U;
constexpr uint32_t kAptGetLockHandleResponse = 0x000100C2U;
constexpr uint32_t kAptInitializeRequest = 0x00020080U;
constexpr uint32_t kAptInitializeResponse = 0x00020043U;
constexpr uint32_t kAptEnableRequest = 0x00030040U;
constexpr uint32_t kAptEnableResponse = 0x00030040U;
constexpr uint32_t kAptNotifyToWaitRequest = 0x00430040U;
constexpr uint32_t kAptNotifyToWaitResponse = 0x00430040U;
constexpr uint32_t kAptReceiveParameterRequest = 0x000D0080U;
constexpr uint32_t kAptReceiveParameterResponse = 0x000D0104U;
constexpr uint32_t kAptAppletUtilityRequest = 0x004B00C2U;
constexpr uint32_t kAptAppletUtilityResponse = 0x004B0082U;
constexpr uint32_t kAptUtilitySleepIfShellClosed = 4U;
constexpr uint32_t kAptUtilityUnlockTransition = 7U;
constexpr uint32_t kFsInitializeRequest = 0x08010002U;
constexpr uint32_t kFsInitializeResponse = 0x08010040U;
constexpr uint32_t kFsOpenFileRequest = 0x080201C2U;
constexpr uint32_t kFsOpenFileResponse = 0x08020042U;
constexpr uint32_t kFsOpenFileDirectlyRequest = 0x08030204U;
constexpr uint32_t kFsOpenFileDirectlyResponse = 0x08030042U;
constexpr uint32_t kFsCreateFileRequest = 0x08080202U;
constexpr uint32_t kFsCreateFileResponse = 0x08080040U;
constexpr uint32_t kFsOpenArchiveRequest = 0x080C00C2U;
constexpr uint32_t kFsOpenArchiveResponse = 0x080C00C0U;
constexpr uint32_t kFsControlArchiveRequest = 0x080D0144U;
constexpr uint32_t kFsControlArchiveResponse = 0x080D0040U;
constexpr uint32_t kFsCloseArchiveRequest = 0x080E0080U;
constexpr uint32_t kFsCloseArchiveResponse = 0x080E0040U;
constexpr uint32_t kFsInvalidArchiveHandle = 0xC8804465U;
constexpr uint32_t kFsFileNotFound = 0xC8804470U;
constexpr uint32_t kFsFileAlreadyExists = 0xC82044B4U;
constexpr uint32_t kFsSaveDataArchiveId = 4U;
constexpr uint32_t kFileReadRequest = 0x080200C2U;
constexpr uint32_t kFileReadResponse = 0x08020082U;
constexpr uint32_t kFileWriteRequest = 0x08030102U;
constexpr uint32_t kFileWriteResponse = 0x08030082U;
constexpr uint32_t kFileGetSizeRequest = 0x08040000U;
constexpr uint32_t kFileGetSizeResponse = 0x080400C0U;
constexpr uint32_t kFileSetSizeRequest = 0x08050080U;
constexpr uint32_t kFileSetSizeResponse = 0x08050040U;
constexpr uint32_t kFileCloseRequest = 0x08080000U;
constexpr uint32_t kFileCloseResponse = 0x08080040U;
constexpr uint32_t kFileFlushRequest = 0x08090000U;
constexpr uint32_t kFileFlushResponse = 0x08090040U;
constexpr uint32_t kGspAcquireRightRequest = 0x00160042U;
constexpr uint32_t kGspAcquireRightResponse = 0x00160040U;
constexpr uint32_t kGspWriteHwRegsRequest = 0x00010082U;
constexpr uint32_t kGspWriteHwRegsResponse = 0x00010040U;
constexpr uint32_t kGspWriteHwRegsWithMaskRequest = 0x00020084U;
constexpr uint32_t kGspWriteHwRegsWithMaskResponse = 0x00020040U;
constexpr uint32_t kGspSetBufferSwapRequest = 0x00050200U;
constexpr uint32_t kGspSetBufferSwapResponse = 0x00050040U;
constexpr uint32_t kGspTriggerCommandQueueRequest = 0x000C0000U;
constexpr uint32_t kGspTriggerCommandQueueResponse = 0x000C0040U;
constexpr uint32_t kGspRegisterInterruptRelayQueueRequest = 0x00130042U;
constexpr uint32_t kGspRegisterInterruptRelayQueueResponse = 0x00130082U;
constexpr uint32_t kGspFirstInitialization = 0x00002A07U;
constexpr uint32_t kGspSetInternalPrioritiesRequest = 0x001E0080U;
constexpr uint32_t kGspSetInternalPrioritiesResponse = 0x001E0040U;
constexpr uint32_t kGspFlushDataCacheRequest = 0x00080082U;
constexpr uint32_t kGspFlushDataCacheResponse = 0x00080040U;
constexpr uint32_t kGspInvalidateDataCacheRequest = 0x00090082U;
constexpr uint32_t kGspInvalidateDataCacheResponse = 0x00090040U;
constexpr uint32_t kGspSetLcdForceBlackRequest = 0x000B0040U;
constexpr uint32_t kGspSetLcdForceBlackResponse = 0x000B0040U;
constexpr uint32_t kDspLoadComponentRequest = 0x001100C2U;
constexpr uint32_t kDspLoadComponentResponse = 0x00110082U;
constexpr uint32_t kDspRegisterInterruptEventsRequest = 0x00150082U;
constexpr uint32_t kDspRegisterInterruptEventsResponse = 0x00150040U;
constexpr uint32_t kDspGetSemaphoreEventHandleRequest = 0x00160000U;
constexpr uint32_t kDspGetSemaphoreEventHandleResponse = 0x00160042U;
constexpr uint32_t kDspSetSemaphoreMaskRequest = 0x00170040U;
constexpr uint32_t kDspSetSemaphoreMaskResponse = 0x00170040U;
constexpr uint32_t kDspWriteProcessPipeRequest = 0x000D0082U;
constexpr uint32_t kDspWriteProcessPipeResponse = 0x000D0040U;
constexpr uint32_t kDspSetSemaphoreRequest = 0x00070040U;
constexpr uint32_t kDspSetSemaphoreResponse = 0x00070040U;
constexpr uint32_t kDspReadPipeIfPossibleRequest = 0x001000C0U;
constexpr uint32_t kDspReadPipeIfPossibleResponse = 0x00100082U;
constexpr uint32_t kDspConvertProcessAddressRequest = 0x000C0040U;
constexpr uint32_t kDspConvertProcessAddressResponse = 0x000C0080U;
constexpr uint32_t kDspGetHeadphoneStatusRequest = 0x001F0000U;
constexpr uint32_t kDspGetHeadphoneStatusResponse = 0x001F0080U;
constexpr uint32_t kDspRamVirtualAddress = 0x1FF00000U;
constexpr uint32_t kDspRamArmWindowOffset = 0x00040000U;
constexpr uint32_t kDspAudioPipe = 2U;
constexpr std::array<uint16_t, 15> kDspHleAudioStructAddresses{
    0xBFFF, 0x9E92, 0x8680, 0xA792, 0x9430, 0x8400, 0x8540, 0x9492,
    0x8710, 0x8410, 0xA912, 0xAA12, 0xAAD2, 0xAC52, 0xAC5C,
};
constexpr uint32_t kCopyHandleDescriptor = 0x00000000U;
constexpr uint32_t kTwoCopyHandlesDescriptor = 0x04000000U;
constexpr uint32_t kMoveHandleDescriptor = 0x10U;
constexpr uint32_t kCallingPidDescriptor = 0x20U;
constexpr uint32_t kCfgGetConfigRequest = 0x00010082U;
constexpr uint32_t kCfgGetConfigResponse = 0x00010042U;
constexpr uint32_t kCfgSoundOutputModeBlockId = 0x00070001U;
constexpr uint32_t kCfgLanguageBlockId = 0x000A0002U;
constexpr uint32_t kCfgStereoCameraSettingsBlockId = 0x00050005U;
constexpr uint32_t kCfgGetRegionRequest = 0x00020000U;
constexpr uint32_t kCfgGetRegionResponse = 0x00020080U;
constexpr uint32_t kHidGetIpcHandlesRequest = 0x000A0000U;
constexpr uint32_t kHidGetIpcHandlesResponse = 0x000A0047U;
constexpr uint32_t kSixCopyHandlesDescriptor = 0x14000000U;
constexpr uint32_t kHidEnableAccelerometerRequest = 0x00110000U;
constexpr uint32_t kHidEnableAccelerometerResponse = 0x00110040U;
constexpr uint32_t kHidDisableAccelerometerRequest = 0x00120000U;
constexpr uint32_t kHidDisableAccelerometerResponse = 0x00120040U;
constexpr uint32_t kHidEnableGyroscopeRequest = 0x00130000U;
constexpr uint32_t kHidEnableGyroscopeResponse = 0x00130040U;
constexpr uint32_t kHidDisableGyroscopeRequest = 0x00140000U;
constexpr uint32_t kHidDisableGyroscopeResponse = 0x00140040U;
constexpr uint32_t kHidGetGyroscopeCoefficientRequest = 0x00150000U;
constexpr uint32_t kHidGetGyroscopeCoefficientResponse = 0x00150080U;
constexpr uint32_t kHidGetGyroscopeCalibrationRequest = 0x00160000U;
constexpr uint32_t kHidGetGyroscopeCalibrationResponse = 0x00160180U;
constexpr uint32_t kHidPadIndexResetTicksOffset = 0x00U;
constexpr uint32_t kHidPadIndexResetTicksPreviousOffset = 0x08U;
constexpr uint32_t kHidPadIndexOffset = 0x10U;
constexpr uint32_t kHidPadCurrentStateOffset = 0x1CU;
constexpr uint32_t kHidPadEntriesOffset = 0x28U;
constexpr uint32_t kHidPadEntrySize = 0x10U;
constexpr uint32_t kHidTouchIndexResetTicksOffset = 0xA8U;
constexpr uint32_t kHidTouchIndexResetTicksPreviousOffset = 0xB0U;
constexpr uint32_t kHidTouchIndexOffset = 0xB8U;
constexpr uint32_t kHidTouchEntriesOffset = 0xC8U;
constexpr uint32_t kHidTouchEntrySize = 0x08U;
constexpr uint32_t kHidAccelerometerIndexResetTicksOffset = 0x108U;
constexpr uint32_t kHidAccelerometerIndexResetTicksPreviousOffset = 0x110U;
constexpr uint32_t kHidAccelerometerIndexOffset = 0x118U;
constexpr uint32_t kHidAccelerometerRawEntryOffset = 0x120U;
constexpr uint32_t kHidAccelerometerEntriesOffset = 0x128U;
constexpr uint32_t kHidAccelerometerEntrySize = 0x06U;
constexpr uint32_t kHidGyroscopeIndexResetTicksOffset = 0x154U;
constexpr uint32_t kHidGyroscopeIndexResetTicksPreviousOffset = 0x15CU;
constexpr uint32_t kHidGyroscopeIndexOffset = 0x164U;
constexpr uint32_t kHidGyroscopeRawEntryOffset = 0x16CU;
constexpr uint32_t kHidGyroscopeEntriesOffset = 0x174U;
constexpr uint32_t kHidGyroscopeEntrySize = 0x06U;
constexpr uint32_t kHidCircleRight = 1U << 28U;
constexpr uint32_t kHidCircleLeft = 1U << 29U;
constexpr uint32_t kHidCircleUp = 1U << 30U;
constexpr uint32_t kHidCircleDown = 1U << 31U;
constexpr uint32_t kNdmSuspendSchedulerRequest = 0x00080040U;
constexpr uint32_t kNdmSuspendSchedulerResponse = 0x00080040U;
constexpr uint32_t kNdmResumeSchedulerRequest = 0x00090000U;
constexpr uint32_t kNdmResumeSchedulerResponse = 0x00090040U;

uint32_t ApplyCirclePadDirections(uint32_t buttons, int16_t x, int16_t y) {
    constexpr int32_t kDirectionThresholdSquared = 40 * 40;
    constexpr float kTan30 = 0.577350269F;
    constexpr float kTan60 = 1.0F / kTan30;
    const int32_t x32 = x;
    const int32_t y32 = y;
    if (x32 * x32 + y32 * y32 <= kDirectionThresholdSquared) {
        return buttons;
    }
    const float slope =
        x == 0 ? std::numeric_limits<float>::infinity()
               : std::abs(static_cast<float>(y) / static_cast<float>(x));
    if (x != 0 && slope < kTan60) {
        buttons |= x > 0 ? kHidCircleRight : kHidCircleLeft;
    }
    if (x == 0 || slope > kTan30) {
        buttons |= y > 0 ? kHidCircleUp : kHidCircleDown;
    }
    return buttons;
}

int16_t QuantizeHidSensor(float value, float scale) {
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<int16_t>(std::lround(
        std::clamp(static_cast<double>(value) * static_cast<double>(scale),
                   static_cast<double>(std::numeric_limits<int16_t>::min()),
                   static_cast<double>(std::numeric_limits<int16_t>::max()))));
}

int16_t SaturatingSensorMultiply(int16_t value, int32_t factor) {
    return static_cast<int16_t>(
        std::clamp(static_cast<int32_t>(value) * factor,
                   static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
                   static_cast<int32_t>(std::numeric_limits<int16_t>::max())));
}

bool WriteHidSensorVector(NativeA32Memory& memory, uint32_t address,
                          const std::array<int16_t, 3>& value) {
    return memory.WriteHost<uint16_t>(address,
                                      static_cast<uint16_t>(value[0])) &&
           memory.WriteHost<uint16_t>(address + 2U,
                                      static_cast<uint16_t>(value[1])) &&
           memory.WriteHost<uint16_t>(address + 4U,
                                      static_cast<uint16_t>(value[2]));
}

std::optional<std::string> ReadGuestCString(NativeA32Memory& memory,
                                            uint32_t address,
                                            size_t maximumLength) {
    std::string result;
    for (size_t index = 0; index <= maximumLength; ++index) {
        uint8_t value = 0;
        if (!memory.Read8(address + static_cast<uint32_t>(index), &value)) {
            return std::nullopt;
        }
        if (value == 0) {
            return result;
        }
        if (index == maximumLength) {
            return std::nullopt;
        }
        result.push_back(static_cast<char>(value));
    }
    return std::nullopt;
}

std::string DescribeLowPath(NativeA32Memory& memory, uint32_t type,
                            uint32_t size, uint32_t address) {
    if (type == 1U) {
        return "empty";
    }
    if (size > 0x1000U) {
        return "oversize";
    }
    std::vector<uint8_t> bytes(size);
    if (size != 0 && !memory.ReadBytes(address, bytes)) {
        return "unmapped";
    }
    if (type == 3U) {
        std::string text;
        for (const uint8_t value : bytes) {
            if (value == 0) {
                break;
            }
            text.push_back(value >= 0x20U && value < 0x7FU
                               ? static_cast<char>(value)
                               : '?');
        }
        return "char:" + text;
    }
    if (type == 4U) {
        std::string text;
        for (size_t index = 0; index + 1U < bytes.size(); index += 2U) {
            const uint16_t value = static_cast<uint16_t>(bytes[index]) |
                                   static_cast<uint16_t>(bytes[index + 1U])
                                       << 8U;
            if (value == 0) {
                break;
            }
            text.push_back(value >= 0x20U && value < 0x7FU
                               ? static_cast<char>(value)
                               : '?');
        }
        return "wchar:" + text;
    }
    std::ostringstream result;
    result << "type" << type << ":";
    const size_t shown = std::min<size_t>(bytes.size(), 16U);
    for (size_t index = 0; index < shown; ++index) {
        result << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<uint32_t>(bytes[index]);
    }
    return result.str();
}

std::optional<std::filesystem::path>
DecodeArchiveRelativePath(NativeA32Memory& memory, uint32_t type, uint32_t size,
                          uint32_t address) {
    if ((type != 3U && type != 4U) || size == 0U || size > 0x1000U ||
        (type == 4U && (size & 1U) != 0U)) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes(size);
    if (!memory.ReadBytes(address, bytes)) {
        return std::nullopt;
    }
    std::filesystem::path path;
    if (type == 3U) {
        std::string text;
        for (const uint8_t value : bytes) {
            if (value == 0U) {
                break;
            }
            text.push_back(static_cast<char>(value));
        }
        path = std::filesystem::path(text);
    } else {
        std::u16string text;
        for (size_t index = 0; index + 1U < bytes.size(); index += 2U) {
            const char16_t value = static_cast<char16_t>(bytes[index]) |
                                   static_cast<char16_t>(bytes[index + 1U])
                                       << 8U;
            if (value == 0U) {
                break;
            }
            text.push_back(value);
        }
        path = std::filesystem::path(text);
    }
    while (path.has_root_directory()) {
        path = path.relative_path();
    }
    path = path.lexically_normal();
    if (path.empty() || path.has_root_name() || path.has_root_directory()) {
        return std::nullopt;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return std::nullopt;
        }
    }
    return path;
}

uint64_t NanosecondsToCtrTicks(uint64_t nanoseconds) {
    const uint64_t seconds = nanoseconds / kNanosecondsPerSecond;
    const uint64_t remainder = nanoseconds % kNanosecondsPerSecond;
    if (seconds >
        std::numeric_limits<uint64_t>::max() / kCtrArm11TicksPerSecond) {
        return std::numeric_limits<uint64_t>::max();
    }
    const uint64_t wholeTicks = seconds * kCtrArm11TicksPerSecond;
    const uint64_t fractionalTicks =
        (remainder * kCtrArm11TicksPerSecond + (kNanosecondsPerSecond - 1U)) /
        kNanosecondsPerSecond;
    if (fractionalTicks > std::numeric_limits<uint64_t>::max() - wholeTicks) {
        return std::numeric_limits<uint64_t>::max();
    }
    return std::max<uint64_t>(wholeTicks + fractionalTicks, 1U);
}

} // namespace

detail::NativeA32CtrSvcEventHistory::NativeA32CtrSvcEventHistory(
    bool retainFullHistory, bool collectIpcNameCounts,
    size_t threadCapacityHint)
    : mRetainFullHistory(retainFullHistory),
      mCollectIpcNameCounts(collectIpcNameCounts) {
    // The hint is only a reservation. Correctness does not depend on the
    // configured resource limit, but the normal CTR limit avoids allocations
    // when the first SVC from a newly-created thread arrives.
    constexpr size_t kMaximumReservationHint = 1024U;
    const size_t reservedThreads =
        std::min(threadCapacityHint, kMaximumReservationHint);
    mLastSequenceByThread.reserve(reservedThreads);
    mArchivedLastByThread.reserve(reservedThreads);
    const size_t snapshotCapacity = RecentEventCapacity +
                                    FilesystemEventCapacity + IpcEventCapacity +
                                    reservedThreads;
    mSnapshotCandidates.reserve(snapshotCapacity);
    mSnapshot.reserve(snapshotCapacity);
}

bool detail::NativeA32CtrSvcEventHistory::IsFilesystemSummaryEvent(
    const NativeA32CtrSvcEvent& event) {
    return event.Name.find("fs:USER") != std::string::npos ||
           event.Name.find("file:romfs") != std::string::npos ||
           event.Name.find("file:savedata") != std::string::npos;
}

bool detail::NativeA32CtrSvcEventHistory::IsIpcSummaryEvent(
    const NativeA32CtrSvcEvent& event) {
    return event.Immediate == 0x32U &&
           event.Name.find("file:romfs:Read") == std::string::npos &&
           event.Name.find("gsp::Gpu:FlushDataCache") == std::string::npos;
}

void detail::NativeA32CtrSvcEventHistory::EnsureThreadSlot(uint32_t threadId) {
    const size_t requiredSize = static_cast<size_t>(threadId) + 1U;
    if (mLastSequenceByThread.size() >= requiredSize) {
        return;
    }
    mLastSequenceByThread.resize(requiredSize, 0U);
    mArchivedLastByThread.resize(requiredSize);
}

void detail::NativeA32CtrSvcEventHistory::push_back(
    NativeA32CtrSvcEvent event) {
    const uint64_t sequence = ++mTotalEventCount;
    if (mCollectIpcNameCounts && event.Immediate == 0x32U) {
        ++mIpcNameCounts[event.Name];
    }
    if (mRetainFullHistory) {
        mFullEvents.push_back(std::move(event));
        return;
    }

    if (IsFilesystemSummaryEvent(event)) {
        mFilesystemEvents.Push(sequence, event);
    }
    if (IsIpcSummaryEvent(event)) {
        mIpcEvents.Push(sequence, event);
    }

    EnsureThreadSlot(event.ThreadId);
    if (RecordedEvent* evicted = mRecentEvents.OldestIfFull();
        evicted != nullptr && evicted->Event.ThreadId != event.ThreadId) {
        const size_t evictedThread = evicted->Event.ThreadId;
        if (evictedThread < mLastSequenceByThread.size() &&
            mLastSequenceByThread[evictedThread] == evicted->Sequence) {
            mArchivedLastByThread[evictedThread].emplace(std::move(*evicted));
        }
    }
    mLastSequenceByThread[event.ThreadId] = sequence;
    mArchivedLastByThread[event.ThreadId].reset();
    mRecentEvents.Push(sequence, std::move(event));
    mSnapshotDirty = true;
}

const std::vector<NativeA32CtrSvcEvent>&
detail::NativeA32CtrSvcEventHistory::Events() const {
    if (mRetainFullHistory) {
        return mFullEvents;
    }
    if (!mSnapshotDirty) {
        return mSnapshot;
    }

    mSnapshotCandidates.clear();
    const auto collectRing = [this](const auto& ring) {
        for (size_t offset = 0U; offset < ring.Size; ++offset) {
            const size_t index = (ring.Start + offset) % ring.Records.size();
            mSnapshotCandidates.push_back(&ring.Records[index]);
        }
    };
    collectRing(mRecentEvents);
    collectRing(mFilesystemEvents);
    collectRing(mIpcEvents);
    for (const auto& archived : mArchivedLastByThread) {
        if (archived.has_value()) {
            mSnapshotCandidates.push_back(&*archived);
        }
    }
    std::sort(mSnapshotCandidates.begin(), mSnapshotCandidates.end(),
              [](const RecordedEvent* left, const RecordedEvent* right) {
                  return left->Sequence < right->Sequence;
              });
    const auto uniqueEnd =
        std::unique(mSnapshotCandidates.begin(), mSnapshotCandidates.end(),
                    [](const RecordedEvent* left, const RecordedEvent* right) {
                        return left->Sequence == right->Sequence;
                    });

    mSnapshot.clear();
    for (auto candidate = mSnapshotCandidates.begin(); candidate != uniqueEnd;
         ++candidate) {
        mSnapshot.push_back((*candidate)->Event);
    }
    mSnapshotDirty = false;
    return mSnapshot;
}

const std::map<std::string, uint64_t>&
detail::NativeA32CtrSvcEventHistory::IpcNameCounts() const noexcept {
    return mIpcNameCounts;
}

uint64_t detail::NativeA32CtrSvcEventHistory::TotalEventCount() const noexcept {
    return mTotalEventCount;
}

bool detail::NativeA32CtrSvcEventHistory::RetainsFullHistory() const noexcept {
    return mRetainFullHistory;
}

struct NativeA32CtrHostServices::RomFsBackingFile {
    explicit RomFsBackingFile(std::filesystem::path path)
        : Path(std::move(path)) {}

    bool Read(uint64_t offset, std::span<uint8_t> bytes, bool* openedStream) {
        if (openedStream != nullptr) {
            *openedStream = false;
        }
        if (offset > static_cast<uint64_t>(
                         std::numeric_limits<std::streamoff>::max()) ||
            bytes.size() > static_cast<size_t>(
                               std::numeric_limits<std::streamsize>::max())) {
            return false;
        }

        std::scoped_lock lock(Mutex);
        if (!Stream.is_open()) {
            Stream.clear();
            Stream.open(Path, std::ios::binary);
            if (!Stream.is_open()) {
                return false;
            }
            if (openedStream != nullptr) {
                *openedStream = true;
            }
        }

        // A previous short read leaves fail/eof set on the persistent stream.
        // Clear it before each independent FSFILE offset request.
        Stream.clear();
        Stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!Stream || bytes.empty()) {
            return static_cast<bool>(Stream);
        }
        Stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(Stream);
    }

    std::filesystem::path Path;
    std::ifstream Stream;
    std::mutex Mutex;
};

NativeA32CtrHostServices::NativeA32CtrHostServices(
    NativeA32CtrHostConfig config)
    : mSvcEvents(config.RetainFullSvcHistory, config.ProfileRuntime,
                 static_cast<size_t>(config.ResourceLimitValues[2])),
      mConfig(std::move(config)),
      mLinearHeapCursor(mConfig.LinearHeapBaseAddress) {}

bool NativeA32CtrHostServices::HasPicaBackend() const noexcept {
#if defined(OOT3D_CTR_HOST_SERVICE_PICA_ONLY)
    return mConfig.PicaBridge != nullptr;
#else
    return mConfig.PicaFrontend != nullptr || mConfig.PicaBridge != nullptr;
#endif
}

bool NativeA32CtrHostServices::WritePicaHardwareRegisters(
    uint32_t base, std::span<const uint32_t> values,
    std::span<const uint32_t> masks, std::string* error) {
#if !defined(OOT3D_CTR_HOST_SERVICE_PICA_ONLY)
    if (mConfig.PicaFrontend != nullptr) {
        return masks.empty()
                   ? mConfig.PicaFrontend->WriteHardwareRegisters(base, values,
                                                                  error)
                   : mConfig.PicaFrontend->WriteHardwareRegistersWithMask(
                         base, values, masks, error);
    }
#endif
    if (mConfig.PicaBridge != nullptr) {
        return mConfig.PicaBridge->WriteHardwareRegisters(base, values, masks,
                                                          error);
    }
    if (error != nullptr) {
        *error = "no PICA backend is configured";
    }
    return false;
}

bool NativeA32CtrHostServices::SubmitPicaGspCommand(
    const Oot3dGspCommandPacket& packet, std::span<const uint32_t> commandWords,
    NativeA32CtrPicaSubmissionResult* result, std::string* error) {
    if (result == nullptr) {
        if (error != nullptr) {
            *error = "PICA submission result is null";
        }
        return false;
    }
    *result = {};
#if !defined(OOT3D_CTR_HOST_SERVICE_PICA_ONLY)
    if (mConfig.PicaFrontend != nullptr) {
        return mConfig.PicaFrontend->SubmitGspCommand(
            packet, commandWords, error, &result->DisplayTransferDeferredToGpu,
            &result->MemoryFillDeferredToGpu,
            &result->DisplayTransferCpuCopySuppressed);
    }
#endif
    if (mConfig.PicaBridge != nullptr) {
        return mConfig.PicaBridge->SubmitGspCommand(
            packet.Control, packet.Parameters, commandWords, result, error);
    }
    if (error != nullptr) {
        *error = "no PICA backend is configured";
    }
    return false;
}

bool NativeA32CtrHostServices::TakePicaPendingInterrupts(
    std::vector<uint8_t>* interrupts, std::string* error) {
    if (interrupts == nullptr) {
        if (error != nullptr) {
            *error = "PICA interrupt destination is null";
        }
        return false;
    }
    interrupts->clear();
#if !defined(OOT3D_CTR_HOST_SERVICE_PICA_ONLY)
    if (mConfig.PicaFrontend != nullptr) {
        for (const auto interrupt :
             mConfig.PicaFrontend->TakePendingInterrupts()) {
            interrupts->push_back(static_cast<uint8_t>(interrupt));
        }
        return true;
    }
#endif
    if (mConfig.PicaBridge != nullptr) {
        return mConfig.PicaBridge->TakePendingInterrupts(interrupts, error);
    }
    if (error != nullptr) {
        *error = "no PICA backend is configured";
    }
    return false;
}

uint32_t
NativeA32CtrHostServices::CreateHandle(std::string type,
                                       std::optional<uint32_t> threadId) {
    auto object = std::make_shared<KernelObject>();
    object->Type = std::move(type);
    object->ThreadId = threadId;
    if (object->Type.starts_with("mutex:")) {
        object->AvailableCount = 1;
    }
    return CreateHandle(object);
}

uint32_t NativeA32CtrHostServices::CreateHandle(
    const std::shared_ptr<KernelObject>& object) {
    const uint32_t value = static_cast<uint32_t>(mNextGeneration) |
                           (static_cast<uint32_t>(mNextSlot) << 15U);
    ++mNextSlot;
    ++mNextGeneration;
    if (mNextGeneration >= (1U << 15U)) {
        mNextGeneration = 1;
    }
    mHandles.push_back({value, object});
    return value;
}

const NativeA32CtrHostServices::HandleRecord*
NativeA32CtrHostServices::LookupHandle(uint32_t handle) const {
    for (const auto& record : mHandles) {
        if (record.Value == handle) {
            return &record;
        }
    }
    return nullptr;
}

bool NativeA32CtrHostServices::IsHandleType(uint32_t handle,
                                            const char* type) const {
    for (const auto& record : mHandles) {
        if (record.Value == handle && record.Object->Type == type) {
            return true;
        }
    }
    return false;
}

std::optional<std::string>
NativeA32CtrHostServices::LookupHandleType(uint32_t handle) const {
    for (const auto& record : mHandles) {
        if (record.Value == handle) {
            return record.Object->Type;
        }
    }
    return std::nullopt;
}

bool NativeA32CtrHostServices::IsKernelObjectReady(
    const KernelObject& object, uint32_t threadId,
    const NativeA32Process& process) const {
    if (object.Type.starts_with("mutex:")) {
        return object.AvailableCount > 0 || object.OwnerThreadId == threadId;
    }
    if (object.Type.starts_with("semaphore:") ||
        object.Type.starts_with("event:")) {
        return object.AvailableCount > 0;
    }
    if (object.Type == "thread" && object.ThreadId.has_value()) {
        return process.ThreadStatus(*object.ThreadId) ==
               NativeA32ThreadStatus::Terminated;
    }
    return false;
}

void NativeA32CtrHostServices::AcquireKernelObject(KernelObject& object,
                                                   uint32_t threadId) {
    if (object.Type.starts_with("mutex:")) {
        if (!object.OwnerThreadId.has_value()) {
            object.AvailableCount = 0;
            object.OwnerThreadId = threadId;
        }
        ++object.RecursionCount;
    } else if (object.Type.starts_with("semaphore:") &&
               object.AvailableCount > 0) {
        --object.AvailableCount;
    } else if (object.Type.starts_with("event:") && object.AvailableCount > 0 &&
               object.ResetType == 0) {
        --object.AvailableCount;
    }
}

void NativeA32CtrHostServices::WakeSynchronizationWaiters() {
    while (true) {
        auto selected = mSynchronizationWaiters.end();
        uint32_t selectedIndex = 0;
        uint32_t selectedPriority = UINT32_MAX;
        for (auto waiter = mSynchronizationWaiters.begin();
             waiter != mSynchronizationWaiters.end(); ++waiter) {
            const auto isReady = [&](const auto& object) {
                return IsKernelObjectReady(*object, waiter->ThreadId,
                                           *waiter->Process);
            };
            uint32_t readyIndex = 0;
            bool ready = false;
            if (waiter->WaitAll) {
                ready = std::all_of(waiter->Objects.begin(),
                                    waiter->Objects.end(), isReady);
            } else {
                const auto found = std::find_if(waiter->Objects.begin(),
                                                waiter->Objects.end(), isReady);
                ready = found != waiter->Objects.end();
                readyIndex = static_cast<uint32_t>(
                    std::distance(waiter->Objects.begin(), found));
            }
            const uint32_t priority =
                waiter->Process->ThreadPriority(waiter->ThreadId)
                    .value_or(UINT32_MAX);
            if (ready && priority < selectedPriority) {
                selected = waiter;
                selectedIndex = readyIndex;
                selectedPriority = priority;
            }
        }
        if (selected == mSynchronizationWaiters.end()) {
            return;
        }
        auto* state = selected->Process->ThreadState(selected->ThreadId);
        if (selected->WaitAll) {
            for (const auto& object : selected->Objects) {
                AcquireKernelObject(*object, selected->ThreadId);
            }
        } else {
            AcquireKernelObject(*selected->Objects[selectedIndex],
                                selected->ThreadId);
        }
        if (state != nullptr) {
            state->r[0] = kResultSuccess;
            state->r[1] = selectedIndex;
            selected->Process->ResumeThread(selected->ThreadId);
        }
        mSynchronizationWaiters.erase(selected);
    }
}

bool NativeA32CtrHostServices::QueueGspInterrupt(NativeA32Memory& memory,
                                                 uint8_t interruptId) {
    if (!mGspSharedMemory || !mGspSharedMemory->MappedAddress.has_value() ||
        !mGspInterruptEvent) {
        return false;
    }
    const uint32_t relay =
        *mGspSharedMemory->MappedAddress + mGspThreadId * 0x40U;
    uint8_t index = 0;
    uint8_t count = 0;
    if (!memory.Read8(relay, &index) || !memory.Read8(relay + 1U, &count)) {
        return false;
    }
    const bool isPdc =
        interruptId == static_cast<uint8_t>(Oot3dPicaInterruptId::Pdc0) ||
        interruptId == static_cast<uint8_t>(Oot3dPicaInterruptId::Pdc1);
    if (isPdc) {
        uint8_t config = 0;
        if (!memory.Read8(relay + 3U, &config)) {
            return false;
        }
        if ((config & 1U) != 0U) {
            return true;
        }
        constexpr uint8_t kStopQueuingPdcThreshold = 0x20U;
        if (count >= kStopQueuingPdcThreshold) {
            const uint32_t missedOffset =
                interruptId == static_cast<uint8_t>(Oot3dPicaInterruptId::Pdc0)
                    ? 4U
                    : 8U;
            uint32_t missed = 0;
            return memory.Read32(relay + missedOffset, &missed) &&
                   memory.Write32(relay + missedOffset, missed + 1U);
        }
    }
    if (count >= 0x34U) {
        return memory.Write8(relay + 2U, 1U);
    }
    const uint8_t slot = static_cast<uint8_t>((index + count) % 0x34U);
    if (!memory.Write8(relay + 0x0CU + slot, interruptId) ||
        !memory.Write8(relay + 1U, static_cast<uint8_t>(count + 1U))) {
        return false;
    }
    mGspInterruptEvent->AvailableCount = 1;
    WakeSynchronizationWaiters();
    return true;
}

bool NativeA32CtrHostServices::SetGspFramebuffer(
    uint32_t screenId, uint32_t activeBuffer, uint32_t addressLeft,
    uint32_t addressRight, uint32_t stride, uint32_t format,
    uint32_t shownBuffer) {
    if (screenId >= mGspScreens.size() || activeBuffer >= 2U ||
        shownBuffer >= 2U || stride == 0U || (format & 7U) > 4U) {
        return false;
    }
#if defined(OOT3D_CTR_HOST_SERVICE_PICA_ONLY)
    if (mConfig.PicaBridge != nullptr) {
#else
    if (mConfig.PicaFrontend == nullptr && mConfig.PicaBridge != nullptr) {
#endif
        std::string error;
        if (!mConfig.PicaBridge->SetFramebuffer({screenId, activeBuffer,
                                                 addressLeft, addressRight,
                                                 stride, format, shownBuffer},
                                                &error)) {
            return false;
        }
    }
    auto& screen = mGspScreens[screenId];
    screen.Slots[activeBuffer] = {addressLeft, addressRight, stride, format,
                                  true};
    screen.ShownBuffer = shownBuffer;
    return true;
}

bool NativeA32CtrHostServices::ApplyGspFramebufferUpdate(
    NativeA32Memory& memory, uint32_t screenId) {
    if (!mGspSharedMemory || !mGspSharedMemory->MappedAddress.has_value() ||
        screenId >= mGspScreens.size()) {
        return true;
    }
    const uint32_t updateAddress = *mGspSharedMemory->MappedAddress + 0x200U +
                                   (2U * mGspThreadId + screenId) * 0x40U;
    uint8_t index = 0;
    uint8_t dirty = 0;
    if (!memory.Read8(updateAddress, &index) ||
        !memory.Read8(updateAddress + 1U, &dirty)) {
        return false;
    }
    if ((dirty & 1U) == 0U) {
        return true;
    }
    if (index >= 2U) {
        return false;
    }
    const uint32_t infoAddress = updateAddress + 4U + index * 0x1CU;
    std::array<uint32_t, 7> info{};
    for (size_t word = 0; word < info.size(); ++word) {
        if (!memory.Read32(infoAddress + static_cast<uint32_t>(word * 4U),
                           &info[word])) {
            return false;
        }
    }
    return SetGspFramebuffer(screenId, info[0], info[1], info[2], info[3],
                             info[4], info[5]) &&
           memory.Write8(updateAddress + 1U, static_cast<uint8_t>(dirty & ~1U));
}

bool NativeA32CtrHostServices::CloseHandle(uint32_t handle) {
    for (auto record = mHandles.begin(); record != mHandles.end(); ++record) {
        if (record->Value == handle) {
            mHandles.erase(record);
            return true;
        }
    }
    return false;
}

NativeA32HostResult NativeA32CtrHostServices::HandleResourceLimitValues(
    bool current, oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory, NativeA32CtrSvcEvent event) {
    event.Handled = true;
    event.Name = current ? "GetResourceLimitCurrentValues"
                         : "GetResourceLimitLimitValues";
    const uint32_t valuesAddress = state.r[0];
    const uint32_t handle = state.r[1];
    const uint32_t namesAddress = state.r[2];
    const uint32_t count = state.r[3];
    if (!IsHandleType(handle, "resource_limit") || count > 10) {
        event.Handled = false;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Wait};
    }
    const auto& values =
        current ? mConfig.ResourceCurrentValues : mConfig.ResourceLimitValues;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t name = 0;
        uint32_t faultAddress = 0;
        if (!memory.Read32(namesAddress + index * sizeof(uint32_t), &name) ||
            name >= values.size() ||
            !memory.Write64(valuesAddress + index * sizeof(uint64_t),
                            values[name], &faultAddress)) {
            event.Handled = false;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
    }
    state.r[0] = kResultSuccess;
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Resume};
}

NativeA32HostResult NativeA32CtrHostServices::HandleControlMemory(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32CtrSvcEvent event) {
    event.Name = "ControlMemory";
    const uint32_t operation = state.r[0];
    const uint32_t requestedAddress = state.r[1];
    const uint32_t secondAddress = state.r[2];
    const uint32_t size = state.r[3];
    const uint32_t permissions = state.r[4];
    const bool linear = (operation & kMemoryOperationLinear) != 0;
    if ((operation & kMemoryOperationMask) != kMemoryOperationCommit ||
        secondAddress != 0 || size == 0 || size % 0x1000U != 0 ||
        permissions > 3U ||
        (linear ? mConfig.LinearHeapSize : mConfig.HeapSize) == 0) {
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Wait};
    }

    const uint32_t heapBase =
        linear ? mConfig.LinearHeapBaseAddress : mConfig.HeapBaseAddress;
    const size_t heapSize = linear ? mConfig.LinearHeapSize : mConfig.HeapSize;
    const uint64_t heapEnd = static_cast<uint64_t>(heapBase) + heapSize;
    const uint32_t address =
        linear && requestedAddress == 0 ? mLinearHeapCursor : requestedAddress;
    if (address % 0x1000U != 0 || address < heapBase ||
        (!linear && requestedAddress == 0) ||
        static_cast<uint64_t>(address) + size > heapEnd ||
        mConfig.ResourceCurrentValues[kResourceCommit] + size >
            mConfig.ResourceLimitValues[kResourceCommit]) {
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Wait};
    }
    std::string mapError;
    if (!memory.MapRegion(
            {std::string(linear ? "ctr_linear_heap_" : "ctr_heap_") +
                 std::to_string(mAllocationIndex++),
             address,
             size,
             (permissions & 2U) != 0,
             false,
             {}},
            &mapError)) {
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Wait};
    }
    if (linear) {
        mLinearHeapCursor = std::max(mLinearHeapCursor, address + size);
    }
    mConfig.ResourceCurrentValues[kResourceCommit] += size;
    state.r[0] = kResultSuccess;
    state.r[1] = address;
    event.Handled = true;
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Resume};
}

NativeA32HostResult NativeA32CtrHostServices::HandleSvc(
    uint32_t immediate, oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory, NativeA32HostContext& context) {
    static_cast<void>(memory);
    NativeA32CtrSvcEvent event;
    event.Immediate = immediate;
    event.Pc = state.r[15];
    event.ThreadId = context.ThreadId;
    switch (immediate) {
    case 0x01:
        return HandleControlMemory(state, memory, std::move(event));
    case 0x08: {
        event.Name = "CreateThread";
        const uint32_t priority = state.r[0];
        const int32_t processorId = static_cast<int32_t>(state.r[4]);
        if (priority > 0x3FU || priority < mConfig.ResourceLimitValues[0] ||
            (processorId < -2 || processorId > 3) ||
            mConfig.ResourceCurrentValues[kResourceThread] >=
                mConfig.ResourceLimitValues[kResourceThread]) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        std::string threadError;
        const auto threadId = context.Process.CreateThread(
            {state.r[1], state.r[2], state.r[3], 0x10U, 0x03C00000U, priority},
            &threadError);
        if (!threadId.has_value()) {
            event.Detail = 1;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        state.r[0] = kResultSuccess;
        state.r[1] = CreateHandle("thread", *threadId);
        ++mConfig.ResourceCurrentValues[kResourceThread];
        event.Handled = true;
        event.Detail = *threadId;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    case 0x0A: {
        const uint64_t rawNanoseconds =
            static_cast<uint64_t>(state.r[0]) |
            (static_cast<uint64_t>(state.r[1]) << 32U);
        const int64_t nanoseconds = static_cast<int64_t>(rawNanoseconds);
        event.Name = "SleepThread";
        if (nanoseconds > 0) {
            const uint64_t ticks =
                NanosecondsToCtrTicks(static_cast<uint64_t>(nanoseconds));
            const uint64_t wakeTick =
                ticks > std::numeric_limits<uint64_t>::max() - mSystemTicks
                    ? std::numeric_limits<uint64_t>::max()
                    : mSystemTicks + ticks;
            std::erase_if(mSleepWaiters, [&](const SleepWaiter& waiter) {
                return waiter.Process == &context.Process &&
                       waiter.ThreadId == context.ThreadId;
            });
            mSleepWaiters.push_back(
                {&context.Process, context.ThreadId, wakeTick});
            event.Name += ":nanoseconds=" + std::to_string(nanoseconds) +
                          ":ticks=" + std::to_string(ticks) +
                          ":wake=" + std::to_string(wakeTick);
            event.Detail = static_cast<uint32_t>(ticks);
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (nanoseconds == -1) {
            event.Name += ":infinite";
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        } else {
            event.Name += ":yield=" + std::to_string(nanoseconds);
        }
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    case 0x09:
        event.Name = "ExitThread";
        event.Handled = true;
        if (mConfig.ResourceCurrentValues[kResourceThread] > 0) {
            --mConfig.ResourceCurrentValues[kResourceThread];
        }
        WakeSynchronizationWaiters();
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Terminate};
    case 0x14: {
        event.Name = "ReleaseMutex";
        const auto* record = LookupHandle(state.r[0]);
        if (record == nullptr || !record->Object->Type.starts_with("mutex:") ||
            record->Object->OwnerThreadId != context.ThreadId ||
            record->Object->RecursionCount == 0) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        --record->Object->RecursionCount;
        if (record->Object->RecursionCount == 0) {
            record->Object->OwnerThreadId.reset();
            record->Object->AvailableCount = 1;
            WakeSynchronizationWaiters();
        }
        state.r[0] = kResultSuccess;
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    case 0x17: {
        event.Name = "CreateEvent";
        const uint32_t resetType = state.r[1];
        if (resetType > 2U || mConfig.ResourceCurrentValues[kResourceEvent] >=
                                  mConfig.ResourceLimitValues[kResourceEvent]) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        auto object = std::make_shared<KernelObject>();
        object->Type = "event:guest";
        object->ResetType = resetType;
        state.r[0] = kResultSuccess;
        state.r[1] = CreateHandle(object);
        ++mConfig.ResourceCurrentValues[kResourceEvent];
        event.Handled = true;
        event.Detail = resetType;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    case 0x18:
    case 0x19: {
        const bool signal = immediate == 0x18;
        event.Name = signal ? "SignalEvent" : "ClearEvent";
        const auto* record = LookupHandle(state.r[0]);
        if (record == nullptr || !record->Object->Type.starts_with("event:")) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        record->Object->AvailableCount = signal ? 1 : 0;
        if (signal) {
            WakeSynchronizationWaiters();
        }
        state.r[0] = kResultSuccess;
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    case 0x1F: {
        event.Name = "MapMemoryBlock";
        const HandleRecord* memoryBlock = LookupHandle(state.r[0]);
        const uint32_t address = state.r[1];
        const uint32_t permissions = state.r[2];
        const uint32_t otherPermissions = state.r[3];
        if (memoryBlock == nullptr ||
            !memoryBlock->Object->Type.starts_with("shared_memory:") ||
            memoryBlock->Object->FileSize == 0 ||
            memoryBlock->Object->FileSize > SIZE_MAX ||
            memoryBlock->Object->MappedAddress.has_value() ||
            (address & 0xFFFU) != 0 || (permissions & ~7U) != 0 ||
            permissions == 0 || otherPermissions != 0x10000000U) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        std::string mapError;
        if (!memory.MapRegion(
                {memoryBlock->Object->Type,
                 address,
                 static_cast<size_t>(memoryBlock->Object->FileSize),
                 (permissions & 2U) != 0,
                 (permissions & 4U) != 0,
                 {}},
                &mapError)) {
            event.Name += ":" + mapError;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        memoryBlock->Object->MappedAddress = address;
        state.r[0] = kResultSuccess;
        event.Detail = address;
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    case 0x21:
        event.Handled = true;
        event.Name = "CreateAddressArbiter";
        state.r[0] = kResultSuccess;
        state.r[1] = CreateHandle("address_arbiter");
        ++mConfig.ResourceCurrentValues[kResourceAddressArbiter];
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    case 0x22: {
        event.Name = "ArbitrateAddress";
        const auto* record = LookupHandle(state.r[0]);
        const uint32_t address = state.r[1];
        const uint32_t type = state.r[2];
        const int32_t value = static_cast<int32_t>(state.r[3]);
        event.Detail = type;
        {
            std::ostringstream detail;
            detail << ":address=0x" << std::hex << std::setw(8)
                   << std::setfill('0') << address << ":type=" << std::dec
                   << type << ":value=" << value;
            event.Name += detail.str();
        }
        if (record == nullptr || record->Object->Type != "address_arbiter" ||
            type > 4U || !memory.IsMapped(address, sizeof(uint32_t))) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (type == 0U) {
            std::vector<size_t> matches;
            for (size_t index = 0; index < mArbiterWaiters.size(); ++index) {
                const auto& waiter = mArbiterWaiters[index];
                if (waiter.Arbiter == record->Object &&
                    waiter.Address == address) {
                    matches.push_back(index);
                }
            }
            const size_t wakeCount =
                value < 0
                    ? matches.size()
                    : std::min(matches.size(), static_cast<size_t>(value));
            for (size_t count = 0; count < wakeCount; ++count) {
                auto selected = std::min_element(
                    matches.begin(), matches.end(),
                    [&](size_t lhs, size_t rhs) {
                        const auto lhsPriority =
                            mArbiterWaiters[lhs]
                                .Process
                                ->ThreadPriority(mArbiterWaiters[lhs].ThreadId)
                                .value_or(UINT32_MAX);
                        const auto rhsPriority =
                            mArbiterWaiters[rhs]
                                .Process
                                ->ThreadPriority(mArbiterWaiters[rhs].ThreadId)
                                .value_or(UINT32_MAX);
                        return lhsPriority < rhsPriority;
                    });
                const size_t index = *selected;
                auto waiter = mArbiterWaiters[index];
                if (auto* waiterState =
                        waiter.Process->ThreadState(waiter.ThreadId)) {
                    waiterState->r[0] = kResultSuccess;
                    waiter.Process->ResumeThread(waiter.ThreadId);
                }
                matches.erase(selected);
                mArbiterWaiters.erase(mArbiterWaiters.begin() + index);
                for (auto& remaining : matches) {
                    if (remaining > index) {
                        --remaining;
                    }
                }
            }
            state.r[0] = kResultSuccess;
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }

        uint32_t memoryValueRaw = 0;
        if (!memory.Read32(address, &memoryValueRaw)) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        const int32_t memoryValue = static_cast<int32_t>(memoryValueRaw);
        if (memoryValue < value) {
            if ((type == 2U || type == 4U) &&
                !memory.Write32(address,
                                static_cast<uint32_t>(memoryValue - 1))) {
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Wait};
            }
            mArbiterWaiters.push_back(
                {record->Object, &context.Process, context.ThreadId, address});
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        state.r[0] = kResultSuccess;
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    case 0x23:
        event.Name = "CloseHandle";
        event.Handled = CloseHandle(state.r[0]);
        if (!event.Handled) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        state.r[0] = kResultSuccess;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    case 0x25: {
        event.Name = "WaitSynchronizationN";
        const uint32_t handlesAddress = state.r[1];
        const uint32_t handleCount = state.r[2];
        const bool waitAll = state.r[3] != 0;
        if (handleCount > 0x40U) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        std::vector<const HandleRecord*> handles;
        handles.reserve(handleCount);
        std::ostringstream detail;
        detail << (waitAll ? ":all" : ":any");
        for (uint32_t index = 0; index < handleCount; ++index) {
            uint32_t handle = 0;
            if (!memory.Read32(handlesAddress + index * sizeof(uint32_t),
                               &handle)) {
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Wait};
            }
            detail << (index == 0 ? ":" : ",") << "0x" << std::hex
                   << std::setw(8) << std::setfill('0') << handle;
            const auto type = LookupHandleType(handle);
            detail << "=" << type.value_or("invalid");
            const auto* record = LookupHandle(handle);
            if (record == nullptr) {
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Wait};
            }
            handles.push_back(record);
            if (index == 0) {
                event.Detail = handle;
            }
        }
        event.Name += detail.str();
        const auto isReady = [&](const HandleRecord* record) {
            return IsKernelObjectReady(*record->Object, context.ThreadId,
                                       context.Process);
        };
        if (waitAll) {
            if (std::all_of(handles.begin(), handles.end(), isReady)) {
                for (const auto* record : handles) {
                    AcquireKernelObject(*record->Object, context.ThreadId);
                }
                state.r[0] = kResultSuccess;
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
        } else {
            const auto ready =
                std::find_if(handles.begin(), handles.end(), isReady);
            if (ready != handles.end()) {
                AcquireKernelObject(*(*ready)->Object, context.ThreadId);
                state.r[0] = kResultSuccess;
                state.r[1] = static_cast<uint32_t>(
                    std::distance(handles.begin(), ready));
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
        }
        SynchronizationWaiter waiter;
        waiter.Process = &context.Process;
        waiter.ThreadId = context.ThreadId;
        waiter.WaitAll = waitAll;
        for (const auto* handle : handles) {
            waiter.Objects.push_back(handle->Object);
        }
        mSynchronizationWaiters.push_back(std::move(waiter));
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Wait};
    }
    case 0x27:
        event.Name = "DuplicateHandle";
        if (state.r[1] != kCurrentThreadHandle) {
            event.Handled = false;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        state.r[0] = kResultSuccess;
        state.r[1] = CreateHandle("thread", context.ThreadId);
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    case 0x28:
        event.Name = "GetSystemTick";
        event.Handled = true;
        state.r[0] = static_cast<uint32_t>(mSystemTicks);
        state.r[1] = static_cast<uint32_t>(mSystemTicks >> 32U);
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    case 0x2D: {
        const auto portName = ReadGuestCString(memory, state.r[1], 11);
        event.Name = portName.has_value() ? "ConnectToPort:" + *portName
                                          : "ConnectToPort";
        if (!portName.has_value() || *portName != "srv:") {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        state.r[0] = kResultSuccess;
        state.r[1] = CreateHandle("client_session:srv:");
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    case 0x32: {
        const auto sessionType = LookupHandleType(state.r[0]);
        event.Name = "SendSyncRequest";
        if (sessionType.has_value() &&
            sessionType->starts_with("client_session:")) {
            event.Name += ":" + sessionType->substr(15U);
        }
        if (!sessionType.has_value() ||
            !sessionType->starts_with("client_session:") ||
            !memory.Read32(state.thread_pointer + 0x80U, &event.Detail)) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        const uint32_t commandBuffer = state.thread_pointer + 0x80U;
        if (*sessionType == "client_session:ndm:u") {
            if (event.Detail == kNdmSuspendSchedulerRequest) {
                uint32_t performInBackground = 0;
                if (memory.Read32(commandBuffer + 4U, &performInBackground) &&
                    performInBackground <= 1U &&
                    memory.Write32(commandBuffer,
                                   kNdmSuspendSchedulerResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                    mNdmSchedulerSuspended = true;
                    mNdmSchedulerRunsInBackground = performInBackground != 0U;
                    state.r[0] = kResultSuccess;
                    event.Name += ":SuspendScheduler:background=" +
                                  std::to_string(performInBackground);
                    event.Detail = performInBackground;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kNdmResumeSchedulerRequest &&
                memory.Write32(commandBuffer, kNdmResumeSchedulerResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                mNdmSchedulerSuspended = false;
                mNdmSchedulerRunsInBackground = false;
                state.r[0] = kResultSuccess;
                event.Name += ":ResumeScheduler";
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (*sessionType == "client_session:cfg:u") {
            if (event.Detail == kCfgGetConfigRequest) {
                uint32_t size = 0;
                uint32_t blockId = 0;
                uint32_t descriptor = 0;
                uint32_t address = 0;
                if (memory.Read32(commandBuffer + 4U, &size) &&
                    memory.Read32(commandBuffer + 8U, &blockId) &&
                    memory.Read32(commandBuffer + 12U, &descriptor) &&
                    memory.Read32(commandBuffer + 16U, &address) &&
                    (descriptor & 0x8U) != 0U &&
                    ((descriptor >> 1U) & 2U) != 0U &&
                    (descriptor >> 4U) == size) {
                    std::vector<uint8_t> value;
                    if (blockId == kCfgSoundOutputModeBlockId && size == 1U) {
                        value.push_back(mConfig.SoundOutputMode);
                    } else if (blockId == kCfgLanguageBlockId && size == 1U) {
                        value.push_back(mConfig.SystemLanguage);
                    } else if (blockId == kCfgStereoCameraSettingsBlockId &&
                               size == sizeof(mConfig.StereoCameraSettings)) {
                        value.resize(size);
                        std::memcpy(value.data(),
                                    mConfig.StereoCameraSettings.data(),
                                    value.size());
                    }
                    if (value.size() == size &&
                        memory.WriteBytes(address, value) &&
                        memory.Write32(commandBuffer, kCfgGetConfigResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 8U, descriptor) &&
                        memory.Write32(commandBuffer + 12U, address)) {
                        state.r[0] = kResultSuccess;
                        std::ostringstream configDetail;
                        configDetail << ":GetConfig:block=0x" << std::hex
                                     << blockId << ":size=0x" << size;
                        event.Name += configDetail.str();
                        event.Detail = blockId;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kCfgGetRegionRequest &&
                memory.Write32(commandBuffer, kCfgGetRegionResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                memory.Write32(commandBuffer + 8U, mConfig.SystemRegion)) {
                state.r[0] = kResultSuccess;
                event.Name +=
                    ":GetRegion:" + std::to_string(mConfig.SystemRegion);
                event.Detail = mConfig.SystemRegion;
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (*sessionType == "client_session:hid:USER") {
            if (event.Detail == kHidGetIpcHandlesRequest) {
                if (!mHidSharedMemory) {
                    mHidSharedMemory = std::make_shared<KernelObject>();
                    mHidSharedMemory->Type = "shared_memory:hid";
                    mHidSharedMemory->FileSize = 0x1000U;
                    constexpr std::array<const char*, 5> eventNames{
                        "event:hid_pad_touch_1",   "event:hid_pad_touch_2",
                        "event:hid_accelerometer", "event:hid_gyroscope",
                        "event:hid_debug_pad",
                    };
                    for (size_t index = 0; index < mHidEvents.size(); ++index) {
                        mHidEvents[index] = std::make_shared<KernelObject>();
                        mHidEvents[index]->Type = eventNames[index];
                        mHidEvents[index]->ResetType = 0U;
                    }
                }
                std::array<uint32_t, 6> handles{};
                handles[0] = CreateHandle(mHidSharedMemory);
                for (size_t index = 0; index < mHidEvents.size(); ++index) {
                    handles[index + 1U] = CreateHandle(mHidEvents[index]);
                }
                bool written =
                    memory.Write32(commandBuffer, kHidGetIpcHandlesResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                    memory.Write32(commandBuffer + 8U,
                                   kSixCopyHandlesDescriptor);
                for (size_t index = 0; index < handles.size(); ++index) {
                    written =
                        written &&
                        memory.Write32(commandBuffer + 12U +
                                           static_cast<uint32_t>(index * 4U),
                                       handles[index]);
                }
                if (written) {
                    state.r[0] = kResultSuccess;
                    event.Name += ":GetIPCHandles";
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
                for (const uint32_t handle : handles) {
                    CloseHandle(handle);
                }
            }
            const bool enableAccelerometer =
                event.Detail == kHidEnableAccelerometerRequest;
            const bool disableAccelerometer =
                event.Detail == kHidDisableAccelerometerRequest;
            const bool enableGyroscope =
                event.Detail == kHidEnableGyroscopeRequest;
            const bool disableGyroscope =
                event.Detail == kHidDisableGyroscopeRequest;
            if (enableAccelerometer || disableAccelerometer ||
                enableGyroscope || disableGyroscope) {
                uint32_t* enableCount =
                    (enableAccelerometer || disableAccelerometer)
                        ? &mHidAccelerometerEnableCount
                        : &mHidGyroscopeEnableCount;
                if ((enableAccelerometer || enableGyroscope) ||
                    *enableCount != 0U) {
                    if (enableAccelerometer || enableGyroscope) {
                        ++*enableCount;
                    } else {
                        --*enableCount;
                    }
                    const uint32_t response =
                        enableAccelerometer ? kHidEnableAccelerometerResponse
                        : disableAccelerometer
                            ? kHidDisableAccelerometerResponse
                        : enableGyroscope ? kHidEnableGyroscopeResponse
                                          : kHidDisableGyroscopeResponse;
                    if (memory.Write32(commandBuffer, response) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                        state.r[0] = kResultSuccess;
                        event.Name +=
                            enableAccelerometer    ? ":EnableAccelerometer"
                            : disableAccelerometer ? ":DisableAccelerometer"
                            : enableGyroscope      ? ":EnableGyroscopeLow"
                                                   : ":DisableGyroscopeLow";
                        event.Detail = *enableCount;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                    if (enableAccelerometer || enableGyroscope) {
                        --*enableCount;
                    } else {
                        ++*enableCount;
                    }
                }
            }
            if (event.Detail == kHidGetGyroscopeCoefficientRequest) {
                uint32_t coefficient = 0;
                static_assert(sizeof(coefficient) ==
                              sizeof(mConfig.GyroscopeRawToDpsCoefficient));
                std::memcpy(&coefficient, &mConfig.GyroscopeRawToDpsCoefficient,
                            sizeof(coefficient));
                if (memory.Write32(commandBuffer,
                                   kHidGetGyroscopeCoefficientResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                    memory.Write32(commandBuffer + 8U, coefficient)) {
                    state.r[0] = kResultSuccess;
                    event.Name += ":GetGyroscopeLowRawToDpsCoefficient";
                    event.Detail = coefficient;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kHidGetGyroscopeCalibrationRequest) {
                std::array<uint8_t, sizeof(mConfig.GyroscopeCalibration)>
                    calibration{};
                std::memcpy(calibration.data(),
                            mConfig.GyroscopeCalibration.data(),
                            calibration.size());
                if (memory.Write32(commandBuffer,
                                   kHidGetGyroscopeCalibrationResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                    memory.WriteBytes(commandBuffer + 8U, calibration)) {
                    state.r[0] = kResultSuccess;
                    event.Name += ":GetGyroscopeLowCalibrateParam";
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (*sessionType == "client_session:dsp::DSP") {
            if (event.Detail == kDspLoadComponentRequest) {
                uint32_t size = 0;
                uint32_t programMask = 0;
                uint32_t dataMask = 0;
                uint32_t descriptor = 0;
                uint32_t address = 0;
                if (memory.Read32(commandBuffer + 4U, &size) &&
                    memory.Read32(commandBuffer + 8U, &programMask) &&
                    memory.Read32(commandBuffer + 12U, &dataMask) &&
                    memory.Read32(commandBuffer + 16U, &descriptor) &&
                    memory.Read32(commandBuffer + 20U, &address) &&
                    (descriptor & 0x8U) != 0U &&
                    ((descriptor >> 1U) & 1U) != 0U &&
                    (descriptor >> 4U) == size &&
                    memory.IsMapped(address, size)) {
                    std::vector<uint8_t> component(size);
                    if (memory.ReadBytes(address, component) &&
                        memory.Write32(commandBuffer,
                                       kDspLoadComponentResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 8U, 1U) &&
                        memory.Write32(commandBuffer + 12U, descriptor) &&
                        memory.Write32(commandBuffer + 16U, address)) {
                        mDspComponent = std::move(component);
                        mDspProgramMask = static_cast<uint16_t>(programMask);
                        mDspDataMask = static_cast<uint16_t>(dataMask);
                        state.r[0] = kResultSuccess;
                        std::ostringstream loadDetail;
                        loadDetail << ":LoadComponent:size=0x" << std::hex
                                   << size << ":program_mask=0x"
                                   << mDspProgramMask << ":data_mask=0x"
                                   << mDspDataMask;
                        event.Name += loadDetail.str();
                        event.Detail = size;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kDspRegisterInterruptEventsRequest) {
                uint32_t interruptType = 0;
                uint32_t channel = 0;
                uint32_t descriptor = 0;
                uint32_t eventHandle = 0;
                if (memory.Read32(commandBuffer + 4U, &interruptType) &&
                    memory.Read32(commandBuffer + 8U, &channel) &&
                    memory.Read32(commandBuffer + 12U, &descriptor) &&
                    memory.Read32(commandBuffer + 16U, &eventHandle) &&
                    interruptType < mDspInterruptEvents.size() &&
                    channel < mDspInterruptEvents[interruptType].size() &&
                    descriptor == kCopyHandleDescriptor) {
                    std::shared_ptr<KernelObject> interruptEvent;
                    if (eventHandle != 0U) {
                        const auto* record = LookupHandle(eventHandle);
                        if (record == nullptr ||
                            !record->Object->Type.starts_with("event:")) {
                            mSvcEvents.push_back(std::move(event));
                            return {NativeA32HostAction::Wait};
                        }
                        interruptEvent = record->Object;
                    }
                    if (memory.Write32(commandBuffer,
                                       kDspRegisterInterruptEventsResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                        mDspInterruptEvents[interruptType][channel] =
                            std::move(interruptEvent);
                        state.r[0] = kResultSuccess;
                        event.Name += ":RegisterInterruptEvents:type=" +
                                      std::to_string(interruptType) +
                                      ":channel=" + std::to_string(channel);
                        event.Detail = (interruptType << 16U) | channel;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kDspGetSemaphoreEventHandleRequest) {
                if (!mDspSemaphoreEvent) {
                    mDspSemaphoreEvent = std::make_shared<KernelObject>();
                    mDspSemaphoreEvent->Type = "event:dsp_semaphore";
                    mDspSemaphoreEvent->ResetType = 1U;
                }
                const uint32_t semaphoreEventHandle =
                    CreateHandle(mDspSemaphoreEvent);
                if (memory.Write32(commandBuffer,
                                   kDspGetSemaphoreEventHandleResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                    memory.Write32(commandBuffer + 8U, kCopyHandleDescriptor) &&
                    memory.Write32(commandBuffer + 12U, semaphoreEventHandle)) {
                    state.r[0] = kResultSuccess;
                    event.Name += ":GetSemaphoreEventHandle";
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
                CloseHandle(semaphoreEventHandle);
            }
            if (event.Detail == kDspSetSemaphoreMaskRequest) {
                uint32_t mask = 0;
                if (memory.Read32(commandBuffer + 4U, &mask) &&
                    memory.Write32(commandBuffer,
                                   kDspSetSemaphoreMaskResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                    mDspSemaphoreMask = static_cast<uint16_t>(mask);
                    state.r[0] = kResultSuccess;
                    event.Name += ":SetSemaphoreMask:mask=0x";
                    std::ostringstream maskDetail;
                    maskDetail << std::hex << mDspSemaphoreMask;
                    event.Name += maskDetail.str();
                    event.Detail = mDspSemaphoreMask;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kDspWriteProcessPipeRequest) {
                uint32_t channel = 0;
                uint32_t size = 0;
                uint32_t descriptor = 0;
                uint32_t address = 0;
                if (memory.Read32(commandBuffer + 4U, &channel) &&
                    memory.Read32(commandBuffer + 8U, &size) &&
                    memory.Read32(commandBuffer + 12U, &descriptor) &&
                    memory.Read32(commandBuffer + 16U, &address) &&
                    channel < mDspPipeOutput.size() &&
                    (descriptor & 0xFU) == 2U && (descriptor >> 14U) >= size &&
                    memory.IsMapped(address, size)) {
                    std::vector<uint8_t> payload(size);
                    if (!memory.ReadBytes(address, payload)) {
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Wait};
                    }
                    bool validPayload = true;
                    if (channel == kDspAudioPipe) {
                        validPayload = payload.size() >= 4U;
                        if (validPayload) {
                            payload[2] = 0;
                            payload[3] = 0;
                            const uint8_t stateChange = payload[0];
                            if (stateChange == 0U || stateChange == 2U ||
                                stateChange == 3U) {
                                auto& output = mDspPipeOutput[channel];
                                output.clear();
                                output.reserve(
                                    2U + kDspHleAudioStructAddresses.size() *
                                             sizeof(uint16_t));
                                const auto appendU16 =
                                    [&output](uint16_t value) {
                                        output.push_back(
                                            static_cast<uint8_t>(value));
                                        output.push_back(
                                            static_cast<uint8_t>(value >> 8U));
                                    };
                                appendU16(static_cast<uint16_t>(
                                    kDspHleAudioStructAddresses.size()));
                                for (const uint16_t structAddress :
                                     kDspHleAudioStructAddresses) {
                                    appendU16(structAddress);
                                }
                                SignalDspInterrupt(2U, channel);
                                mDspAudioRunning = stateChange != 3U;
                                mNextDspAudioFrameTick =
                                    mDspAudioRunning
                                        ? mSystemTicks + DspAudioFrameTicks
                                        : 0U;
                                mPendingDspAudioFrames = 0U;
                            } else if (stateChange == 1U) {
                                mDspAudioRunning = false;
                                mNextDspAudioFrameTick = 0U;
                                mPendingDspAudioFrames = 0U;
                            } else if (stateChange != 1U) {
                                validPayload = false;
                            }
                        }
                    }
                    if (validPayload &&
                        memory.Write32(commandBuffer,
                                       kDspWriteProcessPipeResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                        state.r[0] = kResultSuccess;
                        event.Name += ":WriteProcessPipe:channel=" +
                                      std::to_string(channel) +
                                      ":size=" + std::to_string(size);
                        event.Detail = size;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kDspSetSemaphoreRequest) {
                uint32_t value = 0;
                if (memory.Read32(commandBuffer + 4U, &value) &&
                    memory.Write32(commandBuffer, kDspSetSemaphoreResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                    mDspSemaphoreValue = static_cast<uint16_t>(value);
                    state.r[0] = kResultSuccess;
                    event.Name += ":SetSemaphore:value=0x";
                    std::ostringstream valueDetail;
                    valueDetail << std::hex << mDspSemaphoreValue;
                    event.Name += valueDetail.str();
                    event.Detail = mDspSemaphoreValue;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kDspReadPipeIfPossibleRequest) {
                uint32_t channel = 0;
                uint32_t peer = 0;
                uint32_t requestedSize = 0;
                uint32_t targetDescriptor = 0;
                uint32_t targetAddress = 0;
                const uint32_t staticBufferTable =
                    state.thread_pointer + 0x180U;
                if (memory.Read32(commandBuffer + 4U, &channel) &&
                    memory.Read32(commandBuffer + 8U, &peer) &&
                    memory.Read32(commandBuffer + 12U, &requestedSize) &&
                    memory.Read32(staticBufferTable, &targetDescriptor) &&
                    memory.Read32(staticBufferTable + 4U, &targetAddress) &&
                    channel < mDspPipeOutput.size() &&
                    (targetDescriptor & 0xFU) == 2U &&
                    ((targetDescriptor >> 10U) & 0xFU) == 0U) {
                    requestedSize &= 0xFFFFU;
                    const uint32_t targetCapacity = targetDescriptor >> 14U;
                    auto& pipe = mDspPipeOutput[channel];
                    const uint32_t returnedSize =
                        pipe.size() >= requestedSize ? requestedSize : 0U;
                    std::vector<uint8_t> returned(pipe.begin(),
                                                  pipe.begin() + returnedSize);
                    const uint32_t responseDescriptor =
                        (returnedSize << 14U) | 2U;
                    if (returnedSize <= targetCapacity &&
                        (returned.empty() ||
                         memory.WriteBytes(targetAddress, returned)) &&
                        memory.Write32(commandBuffer,
                                       kDspReadPipeIfPossibleResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 8U, returnedSize) &&
                        memory.Write32(commandBuffer + 12U,
                                       responseDescriptor) &&
                        memory.Write32(commandBuffer + 16U, targetAddress)) {
                        pipe.erase(pipe.begin(), pipe.begin() + returnedSize);
                        state.r[0] = kResultSuccess;
                        event.Name +=
                            ":ReadPipeIfPossible:channel=" +
                            std::to_string(channel) +
                            ":peer=" + std::to_string(peer) +
                            ":requested=" + std::to_string(requestedSize) +
                            ":returned=" + std::to_string(returnedSize);
                        event.Detail = returnedSize;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kDspConvertProcessAddressRequest) {
                uint32_t dspWordAddress = 0;
                if (memory.Read32(commandBuffer + 4U, &dspWordAddress) &&
                    dspWordAddress <= 0x1FFFFU) {
                    const uint32_t processAddress = (dspWordAddress << 1U) +
                                                    kDspRamVirtualAddress +
                                                    kDspRamArmWindowOffset;
                    if (memory.IsMapped(processAddress, 1U) &&
                        memory.Write32(commandBuffer,
                                       kDspConvertProcessAddressResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 8U, processAddress)) {
                        state.r[0] = kResultSuccess;
                        std::ostringstream conversion;
                        conversion << ":ConvertProcessAddressFromDspDram:dsp=0x"
                                   << std::hex << dspWordAddress << ":arm=0x"
                                   << processAddress;
                        event.Name += conversion.str();
                        event.Detail = processAddress;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kDspGetHeadphoneStatusRequest &&
                memory.Write32(commandBuffer, kDspGetHeadphoneStatusResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                memory.Write32(commandBuffer + 8U,
                               mConfig.HeadphonesConnected ? 1U : 0U)) {
                state.r[0] = kResultSuccess;
                event.Name +=
                    ":GetHeadphoneStatus:" +
                    std::string(mConfig.HeadphonesConnected ? "connected"
                                                            : "disconnected");
                event.Detail = mConfig.HeadphonesConnected ? 1U : 0U;
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (*sessionType == "client_session:file:romfs" ||
            *sessionType == "client_session:file:savedata") {
            const auto* fileHandle = LookupHandle(state.r[0]);
            if (fileHandle == nullptr || fileHandle->Object->FilePath.empty()) {
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Wait};
            }
            if (*sessionType == "client_session:file:savedata") {
                if (fileHandle->Object->FileBackend) {
                    fileHandle->Object->FileSize =
                        fileHandle->Object->FileBackend->Size();
                } else {
                    std::error_code sizeError;
                    const uint64_t fileSize = std::filesystem::file_size(
                        fileHandle->Object->FilePath, sizeError);
                    if (!sizeError) {
                        fileHandle->Object->FileSize = fileSize;
                    }
                }
            }
            if (event.Detail == kFileGetSizeRequest &&
                memory.Write32(commandBuffer, kFileGetSizeResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                memory.Write64(commandBuffer + 8U, fileHandle->Object->FileSize,
                               nullptr)) {
                state.r[0] = kResultSuccess;
                event.Name += ":GetSize";
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
            if (event.Detail == kFileReadRequest) {
                uint64_t offset = 0;
                uint32_t length = 0;
                uint32_t descriptor = 0;
                uint32_t targetAddress = 0;
                uint32_t faultAddress = 0;
                if (memory.Read64(commandBuffer + 4U, &offset, &faultAddress) &&
                    memory.Read32(commandBuffer + 12U, &length) &&
                    memory.Read32(commandBuffer + 16U, &descriptor) &&
                    memory.Read32(commandBuffer + 20U, &targetAddress) &&
                    (descriptor & 0xFU) == 0xCU &&
                    (descriptor >> 4U) >= length &&
                    offset <= fileHandle->Object->FileSize &&
                    length <= fileHandle->Object->FileSize - offset) {
                    std::vector<uint8_t> bytes(length);
                    bool readSucceeded = false;
                    if (fileHandle->Object->FileBackend) {
                        const bool isRomFs =
                            *sessionType == "client_session:file:romfs";
                        const auto readStart =
                            mConfig.ProfileRuntime && isRomFs
                                ? RuntimeProfileClock::now()
                                : RuntimeProfileClock::time_point{};
                        if (mConfig.ProfileRuntime && isRomFs) {
                            ++mRuntimeProfile.RomFsReadCalls;
                        }
                        uint32_t returnedSize = 0U;
                        if (fileHandle->Object->FileOffset <=
                            std::numeric_limits<uint64_t>::max() - offset) {
                            readSucceeded =
                                fileHandle->Object->FileBackend->Read(
                                    fileHandle->Object->FileOffset + offset,
                                    bytes, &returnedSize) &&
                                returnedSize == length;
                        }
                        if (mConfig.ProfileRuntime && isRomFs) {
                            mRuntimeProfile.RomFsReadBytes +=
                                readSucceeded ? length : 0U;
                            mRuntimeProfile.RomFsReadNanoseconds +=
                                RuntimeProfileElapsedNanoseconds(readStart);
                        }
                    } else if (*sessionType == "client_session:file:romfs") {
                        if (!fileHandle->Object->RomFsBacking) {
                            fileHandle->Object->RomFsBacking =
                                std::make_shared<RomFsBackingFile>(
                                    fileHandle->Object->FilePath);
                        }
                        const auto readStart =
                            mConfig.ProfileRuntime
                                ? RuntimeProfileClock::now()
                                : RuntimeProfileClock::time_point{};
                        bool openedStream = false;
                        if (mConfig.ProfileRuntime) {
                            ++mRuntimeProfile.RomFsReadCalls;
                        }
                        if (fileHandle->Object->FileOffset <=
                            std::numeric_limits<uint64_t>::max() - offset) {
                            readSucceeded =
                                fileHandle->Object->RomFsBacking->Read(
                                    fileHandle->Object->FileOffset + offset,
                                    bytes, &openedStream);
                        }
                        if (mConfig.ProfileRuntime) {
                            mRuntimeProfile.RomFsStreamOpenCalls +=
                                openedStream ? 1U : 0U;
                            mRuntimeProfile.RomFsReadBytes +=
                                readSucceeded ? length : 0U;
                            mRuntimeProfile.RomFsReadNanoseconds +=
                                RuntimeProfileElapsedNanoseconds(readStart);
                        }
                    } else {
                        std::ifstream stream(fileHandle->Object->FilePath,
                                             std::ios::binary);
                        stream.seekg(
                            static_cast<std::streamoff>(
                                fileHandle->Object->FileOffset + offset),
                            std::ios::beg);
                        readSucceeded =
                            stream &&
                            (bytes.empty() ||
                             static_cast<bool>(stream.read(
                                 reinterpret_cast<char*>(bytes.data()),
                                 static_cast<std::streamsize>(bytes.size()))));
                    }
                    if (readSucceeded &&
                        memory.WriteBytes(targetAddress, bytes) &&
                        memory.Write32(commandBuffer, kFileReadResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 8U, length) &&
                        memory.Write32(commandBuffer + 12U, descriptor) &&
                        memory.Write32(commandBuffer + 16U, targetAddress)) {
                        state.r[0] = kResultSuccess;
                        std::ostringstream readDetail;
                        readDetail << ":Read:offset=0x" << std::hex << offset
                                   << ":length=0x" << length << ":target=0x"
                                   << targetAddress;
                        event.Name += readDetail.str();
                        event.Handled = true;
                        event.Detail = length;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kFileWriteRequest &&
                (fileHandle->Object->FileOpenMode & 2U) != 0U) {
                uint64_t offset = 0;
                uint32_t length = 0;
                uint32_t flags = 0;
                uint32_t descriptor = 0;
                uint32_t sourceAddress = 0;
                uint32_t faultAddress = 0;
                const bool validRange =
                    memory.Read64(commandBuffer + 4U, &offset, &faultAddress) &&
                    memory.Read32(commandBuffer + 12U, &length) &&
                    memory.Read32(commandBuffer + 16U, &flags) &&
                    memory.Read32(commandBuffer + 20U, &descriptor) &&
                    memory.Read32(commandBuffer + 24U, &sourceAddress) &&
                    ((descriptor & 0xFU) == 0xAU ||
                     (descriptor & 0xFU) == 0xEU) &&
                    (descriptor >> 4U) >= length &&
                    offset <= static_cast<uint64_t>(
                                  std::numeric_limits<std::int64_t>::max()) &&
                    offset <= std::numeric_limits<uint64_t>::max() - length;
                std::vector<uint8_t> bytes(length);
                if (validRange &&
                    (length == 0U || memory.ReadBytes(sourceAddress, bytes))) {
                    bool writeSucceeded = false;
                    if (fileHandle->Object->FileBackend) {
                        uint32_t writtenSize = 0U;
                        writeSucceeded = fileHandle->Object->FileBackend->Write(
                                             offset, bytes, &writtenSize) &&
                                         writtenSize == length;
                        if (writeSucceeded) {
                            fileHandle->Object->FileSize =
                                fileHandle->Object->FileBackend->Size();
                        }
                    } else {
                        std::fstream stream(fileHandle->Object->FilePath,
                                            std::ios::binary | std::ios::in |
                                                std::ios::out);
                        stream.seekp(static_cast<std::streamoff>(offset),
                                     std::ios::beg);
                        writeSucceeded =
                            stream &&
                            (bytes.empty() ||
                             stream.write(
                                 reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<std::streamsize>(bytes.size()))) &&
                            ((flags & 0xFFU) == 0U ||
                             static_cast<bool>(stream.flush()));
                        stream.close();
                        std::error_code sizeError;
                        fileHandle->Object->FileSize =
                            std::filesystem::file_size(
                                fileHandle->Object->FilePath, sizeError);
                        writeSucceeded = writeSucceeded && !sizeError;
                    }
                    if (writeSucceeded) {
                        if (memory.Write32(commandBuffer, kFileWriteResponse) &&
                            memory.Write32(commandBuffer + 4U,
                                           kResultSuccess) &&
                            memory.Write32(commandBuffer + 8U, length) &&
                            memory.Write32(commandBuffer + 12U, descriptor) &&
                            memory.Write32(commandBuffer + 16U,
                                           sourceAddress)) {
                            state.r[0] = kResultSuccess;
                            std::ostringstream writeDetail;
                            writeDetail << ":Write:offset=0x" << std::hex
                                        << offset << ":length=0x" << length
                                        << ":flags=0x" << flags;
                            event.Name += writeDetail.str();
                            event.Handled = true;
                            event.Detail = length;
                            mSvcEvents.push_back(std::move(event));
                            return {NativeA32HostAction::Resume};
                        }
                    }
                }
            }
            if (event.Detail == kFileSetSizeRequest &&
                (fileHandle->Object->FileOpenMode & 2U) != 0U) {
                uint64_t size = 0;
                uint32_t faultAddress = 0;
                const bool readable =
                    memory.Read64(commandBuffer + 4U, &size, &faultAddress) &&
                    size <= static_cast<uint64_t>(
                                std::numeric_limits<std::int64_t>::max());
                if (readable) {
                    bool resized = false;
                    if (fileHandle->Object->FileBackend) {
                        resized = fileHandle->Object->FileBackend->Resize(size);
                    } else {
                        std::error_code resizeError;
                        std::filesystem::resize_file(
                            fileHandle->Object->FilePath, size, resizeError);
                        resized = !resizeError;
                    }
                    if (resized &&
                        memory.Write32(commandBuffer, kFileSetSizeResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                        fileHandle->Object->FileSize = size;
                        state.r[0] = kResultSuccess;
                        event.Name += ":SetSize:" + std::to_string(size);
                        event.Handled = true;
                        event.Detail = static_cast<uint32_t>(size);
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kFileFlushRequest &&
                memory.Write32(commandBuffer, kFileFlushResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                state.r[0] = kResultSuccess;
                event.Name += ":Flush";
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
            if (event.Detail == kFileCloseRequest &&
                memory.Write32(commandBuffer, kFileCloseResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                state.r[0] = kResultSuccess;
                event.Name += ":Close";
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (*sessionType == "client_session:gsp::Gpu") {
            const auto* session = LookupHandle(state.r[0]);
            if (event.Detail == kGspFlushDataCacheRequest ||
                event.Detail == kGspInvalidateDataCacheRequest) {
                uint32_t address = 0;
                uint32_t size = 0;
                uint32_t descriptor = 0;
                uint32_t processHandle = 0;
                if (memory.Read32(commandBuffer + 4U, &address) &&
                    memory.Read32(commandBuffer + 8U, &size) &&
                    memory.Read32(commandBuffer + 12U, &descriptor) &&
                    memory.Read32(commandBuffer + 16U, &processHandle) &&
                    descriptor == kCopyHandleDescriptor &&
                    processHandle == kCurrentProcessHandle &&
                    (size == 0U || memory.IsMapped(address, size)) &&
                    memory.Write32(commandBuffer,
                                   event.Detail == kGspFlushDataCacheRequest
                                       ? kGspFlushDataCacheResponse
                                       : kGspInvalidateDataCacheResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                    state.r[0] = kResultSuccess;
                    event.Name += event.Detail == kGspFlushDataCacheRequest
                                      ? ":FlushDataCache"
                                      : ":InvalidateDataCache";
                    if (mConfig.DetailedSvcDiagnostics) {
                        std::ostringstream cacheRange;
                        cacheRange << ":address=0x" << std::hex << address
                                   << ":size=0x" << size;
                        event.Name += cacheRange.str();
                    }
                    event.Detail = size;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kGspSetInternalPrioritiesRequest) {
                uint32_t priority = 0;
                uint32_t priorityWithRights = 0;
                if (memory.Read32(commandBuffer + 4U, &priority) &&
                    memory.Read32(commandBuffer + 8U, &priorityWithRights) &&
                    memory.Write32(commandBuffer,
                                   kGspSetInternalPrioritiesResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                    mGspPriority = priority;
                    mGspPriorityWithRights = priorityWithRights;
                    state.r[0] = kResultSuccess;
                    event.Name += ":SetInternalPriorities:normal=" +
                                  std::to_string(priority) + ":rights=" +
                                  std::to_string(priorityWithRights);
                    event.Detail = priority;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kGspSetBufferSwapRequest) {
                std::array<uint32_t, 8> words{};
                bool readable = true;
                for (size_t word = 0; word < words.size(); ++word) {
                    readable =
                        readable &&
                        memory.Read32(commandBuffer + 4U +
                                          static_cast<uint32_t>(word * 4U),
                                      &words[word]);
                }
                if (readable &&
                    SetGspFramebuffer(words[0], words[1], words[2], words[3],
                                      words[4], words[5], words[6]) &&
                    memory.Write32(commandBuffer, kGspSetBufferSwapResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                    state.r[0] = kResultSuccess;
                    event.Name +=
                        ":SetBufferSwap:screen=" + std::to_string(words[0]) +
                        ":shown=" + std::to_string(words[6]);
                    event.Detail = words[2];
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kGspSetLcdForceBlackRequest) {
                uint32_t forceBlack = 0;
                std::string picaError;
                const bool readable =
                    memory.Read32(commandBuffer + 4U, &forceBlack);
                const bool notified =
                    readable && (
#if !defined(OOT3D_CTR_HOST_SERVICE_PICA_ONLY)
                                    mConfig.PicaFrontend != nullptr ||
#endif
                                    mConfig.PicaBridge == nullptr ||
                                    mConfig.PicaBridge->SetLcdForceBlack(
                                        forceBlack != 0U, &picaError));
                if (notified &&
                    memory.Write32(commandBuffer,
                                   kGspSetLcdForceBlackResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                    mLcdForceBlack = forceBlack != 0U;
                    state.r[0] = kResultSuccess;
                    event.Name += mLcdForceBlack ? ":SetLcdForceBlack:on"
                                                 : ":SetLcdForceBlack:off";
                    event.Detail = forceBlack;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
                if (!picaError.empty()) {
                    event.Name += ":SetLcdForceBlack:" + picaError;
                }
            }
            if (event.Detail == kGspTriggerCommandQueueRequest &&
                HasPicaBackend() && mGspSharedMemory &&
                mGspSharedMemory->MappedAddress.has_value()) {
                RuntimeProfileScope triggerProfile(
                    mConfig.ProfileRuntime,
                    mRuntimeProfile.TriggerCommandQueueNanoseconds);
                if (mConfig.ProfileRuntime) {
                    ++mRuntimeProfile.TriggerCommandQueueCalls;
                }
                const uint32_t queueAddress =
                    *mGspSharedMemory->MappedAddress + 0x800U;
                uint32_t queueHeader = 0;
                if (memory.Read32(queueAddress, &queueHeader)) {
                    const uint32_t initialQueueHeader = queueHeader;
                    uint32_t index = queueHeader & 0xFFU;
                    uint32_t commandCount = (queueHeader >> 8U) & 0xFFU;
                    uint32_t status = (queueHeader >> 16U) & 0xFFU;
                    const uint32_t shouldStop = queueHeader >> 24U;
                    bool submitted = index < 15U && commandCount <= 15U;
                    uint32_t processed = 0;
                    std::ostringstream commandDetail;
                    if (mConfig.DetailedSvcDiagnostics) {
                        commandDetail << ":queue_before=0x" << std::hex
                                      << std::setw(8) << std::setfill('0')
                                      << initialQueueHeader;
                    }
                    while (submitted && commandCount != 0U && status == 0U &&
                           shouldStop == 0U) {
                        const uint32_t commandAddress =
                            queueAddress + 0x20U + index * 0x20U;
                        std::array<uint32_t, 8> words{};
                        for (size_t wordIndex = 0; wordIndex < words.size();
                             ++wordIndex) {
                            submitted = submitted &&
                                        memory.Read32(commandAddress +
                                                          static_cast<uint32_t>(
                                                              wordIndex * 4U),
                                                      &words[wordIndex]);
                        }
                        Oot3dGspCommandPacket packet{};
                        packet.Control = words[0];
                        std::copy_n(words.begin() + 1, packet.Parameters.size(),
                                    packet.Parameters.begin());
                        if (mConfig.ProfileRuntime) {
                            ++mRuntimeProfile.CommandPackets;
                        }
                        if (mConfig.DetailedSvcDiagnostics) {
                            commandDetail
                                << ":command=" << std::dec << processed
                                << ",control=0x" << std::hex << std::setw(8)
                                << packet.Control << ",id=" << std::dec
                                << (packet.Control & 0xFFU) << ",p0=0x"
                                << std::hex << std::setw(8)
                                << packet.Parameters[0] << ",p1=0x"
                                << std::setw(8) << packet.Parameters[1]
                                << ",p2=0x" << std::setw(8)
                                << packet.Parameters[2] << ",p3=0x"
                                << std::setw(8) << packet.Parameters[3]
                                << ",p4=0x" << std::setw(8)
                                << packet.Parameters[4] << ",p5=0x"
                                << std::setw(8) << packet.Parameters[5]
                                << ",p6=0x" << std::setw(8)
                                << packet.Parameters[6];
                        }
                        std::vector<uint32_t> commandList;
                        if ((packet.Control & 0xFFU) ==
                            static_cast<uint32_t>(
                                Oot3dGspCommandId::SubmitCommandList)) {
                            const uint32_t listAddress = packet.Parameters[0];
                            const uint32_t listSize = packet.Parameters[1];
                            if ((listSize & 3U) != 0 ||
                                listSize > 0x01000000U) {
                                submitted = false;
                            } else {
                                commandList.resize(listSize / 4U);
                                const auto readStart =
                                    mConfig.ProfileRuntime
                                        ? RuntimeProfileClock::now()
                                        : RuntimeProfileClock::time_point{};
                                submitted = submitted &&
                                            memory.ReadBytes(
                                                listAddress,
                                                std::span<uint8_t>(
                                                    reinterpret_cast<uint8_t*>(
                                                        commandList.data()),
                                                    commandList.size() *
                                                        sizeof(uint32_t)));
                                if (mConfig.ProfileRuntime) {
                                    mRuntimeProfile.CommandListBytes +=
                                        submitted ? listSize : 0U;
                                    mRuntimeProfile
                                        .CommandListReadNanoseconds +=
                                        RuntimeProfileElapsedNanoseconds(
                                            readStart);
                                }
                            }
                        }
                        std::string picaError;
                        NativeA32CtrPicaSubmissionResult submissionResult;
#if !defined(OOT3D_CTR_HOST_SERVICE_PICA_ONLY)
                        if (submitted && mConfig.PicaFrontend != nullptr) {
                            if (mConfig.PicaCompositionDomain != nullptr) {
                                mConfig.PicaFrontend
                                    ->SetCommandListCompositionDomain(
                                        *mConfig.PicaCompositionDomain);
                            }
                            if ((packet.Control & 0xFFU) ==
                                    static_cast<uint32_t>(
                                        Oot3dGspCommandId::SubmitCommandList) &&
                                mConfig.PicaCompositionProvider != nullptr) {
                                std::vector<Oot3dPicaCommandListCompositionSpan>
                                    compositionSpans;
                                submitted =
                                    mConfig.PicaCompositionProvider
                                        ->TakeCommandListCompositionSpans(
                                            packet.Parameters[0],
                                            packet.Parameters[1],
                                            compositionSpans, &picaError) &&
                                    mConfig.PicaFrontend
                                        ->SetNextCommandListCompositionSpans(
                                            packet.Parameters[0],
                                            packet.Parameters[1],
                                            compositionSpans, &picaError);
                            }
                        }
#endif
                        if (submitted) {
                            const auto submitStart =
                                mConfig.ProfileRuntime
                                    ? RuntimeProfileClock::now()
                                    : RuntimeProfileClock::time_point{};
                            submitted = SubmitPicaGspCommand(
                                packet, commandList, &submissionResult,
                                &picaError);
                            if (mConfig.ProfileRuntime) {
                                mRuntimeProfile.PicaFrontendSubmitNanoseconds +=
                                    RuntimeProfileElapsedNanoseconds(
                                        submitStart);
                            }
                        }
                        if (submitted &&
                            (packet.Control & 0xFFU) ==
                                static_cast<uint32_t>(
                                    Oot3dGspCommandId::MemoryFill)) {
                            Oot3dPicaMemoryFillCommand fillCommand;
                            fillCommand.Fills[0] = {
                                packet.Parameters[0], packet.Parameters[2],
                                packet.Parameters[1],
                                static_cast<uint16_t>(packet.Parameters[6])};
                            fillCommand.Fills[1] = {
                                packet.Parameters[3], packet.Parameters[5],
                                packet.Parameters[4],
                                static_cast<uint16_t>(packet.Parameters[6] >>
                                                      16U)};
                            const bool hasBothBuffers =
                                fillCommand.Fills[0].StartAddress != 0U &&
                                fillCommand.Fills[1].StartAddress != 0U;
                            for (size_t fillIndex = 0;
                                 fillIndex < fillCommand.Fills.size() &&
                                 submitted;
                                 ++fillIndex) {
                                const auto& fill = fillCommand.Fills[fillIndex];
                                if (fill.StartAddress == 0U ||
                                    (fill.Control & 1U) == 0U) {
                                    continue;
                                }
                                submitted = ExecuteOot3dPicaMemoryFill(
                                    fill, memory, &picaError);
                                if (!submitted ||
                                    submissionResult.MemoryFillDeferredToGpu) {
                                    continue;
                                }
                                if (fillIndex == 0U && !hasBothBuffers) {
                                    submitted = QueueGspInterrupt(
                                        memory,
                                        static_cast<uint8_t>(
                                            Oot3dPicaInterruptId::Psc0));
                                } else if (fillIndex == 1U) {
                                    submitted = QueueGspInterrupt(
                                        memory,
                                        static_cast<uint8_t>(
                                            hasBothBuffers
                                                ? Oot3dPicaInterruptId::Psc0
                                                : Oot3dPicaInterruptId::Psc1));
                                }
                            }
                        }
                        if (submitted &&
                            (packet.Control & 0xFFU) ==
                                static_cast<uint32_t>(
                                    Oot3dGspCommandId::RequestDma)) {
                            const uint32_t source = packet.Parameters[0];
                            const uint32_t destination = packet.Parameters[1];
                            const uint32_t size = packet.Parameters[2];
                            std::vector<uint8_t> dmaBytes(size);
                            submitted =
                                memory.ReadBytes(source, dmaBytes) &&
                                memory.WriteBytes(destination, dmaBytes);
                            if (submitted) {
                                submitted = QueueGspInterrupt(
                                    memory, static_cast<uint8_t>(
                                                Oot3dPicaInterruptId::Dma));
                            } else {
                                picaError = "GSP DMA range is not mapped";
                            }
                        }
                        if (submitted &&
                            (packet.Control & 0xFFU) ==
                                static_cast<uint32_t>(
                                    Oot3dGspCommandId::DisplayTransfer) &&
                            !submissionResult.DisplayTransferDeferredToGpu) {
                            if (!submissionResult
                                     .DisplayTransferCpuCopySuppressed) {
                                submitted = ExecuteOot3dPicaDisplayTransfer(
                                    {packet.Parameters[0], packet.Parameters[1],
                                     packet.Parameters[2], packet.Parameters[3],
                                     packet.Parameters[4]},
                                    memory, &picaError);
                            }
                            if (submitted) {
                                submitted = QueueGspInterrupt(
                                    memory, static_cast<uint8_t>(
                                                Oot3dPicaInterruptId::Ppf));
                            }
                        }
                        if (submitted) {
                            std::vector<uint8_t> interrupts;
                            submitted = TakePicaPendingInterrupts(&interrupts,
                                                                  &picaError);
                            for (const uint8_t interrupt : interrupts) {
                                submitted =
                                    QueueGspInterrupt(memory, interrupt);
                                if (!submitted) {
                                    break;
                                }
                            }
                        }
                        if (!submitted) {
                            if (!picaError.empty()) {
                                event.Name +=
                                    ":TriggerCmdReqQueue:" + picaError;
                            }
                            break;
                        }
                        --commandCount;
                        index = (index + 1U) % 15U;
                        ++processed;
                        if (((packet.Control >> 16U) & 0xFFU) != 0U) {
                            status = 1U;
                        }
                    }
                    queueHeader = index | (commandCount << 8U) |
                                  (status << 16U) | (shouldStop << 24U);
                    if (mConfig.DetailedSvcDiagnostics) {
                        commandDetail << ":queue_after=0x" << std::hex
                                      << std::setw(8) << queueHeader;
                    }
                    if (submitted &&
                        memory.Write32(queueAddress, queueHeader) &&
                        memory.Write32(commandBuffer,
                                       kGspTriggerCommandQueueResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                        state.r[0] = kResultSuccess;
                        event.Name += ":TriggerCmdReqQueue";
                        if (mConfig.DetailedSvcDiagnostics) {
                            event.Name += commandDetail.str();
                        }
                        event.Detail = processed;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kGspWriteHwRegsWithMaskRequest &&
                HasPicaBackend()) {
                uint32_t baseOffset = 0;
                uint32_t size = 0;
                uint32_t valueDescriptor = 0;
                uint32_t valueAddress = 0;
                uint32_t maskDescriptor = 0;
                uint32_t maskAddress = 0;
                if (memory.Read32(commandBuffer + 4U, &baseOffset) &&
                    memory.Read32(commandBuffer + 8U, &size) &&
                    memory.Read32(commandBuffer + 12U, &valueDescriptor) &&
                    memory.Read32(commandBuffer + 16U, &valueAddress) &&
                    memory.Read32(commandBuffer + 20U, &maskDescriptor) &&
                    memory.Read32(commandBuffer + 24U, &maskAddress) &&
                    (valueDescriptor & 0xFU) == 2U &&
                    ((valueDescriptor >> 10U) & 0xFU) == 0U &&
                    (valueDescriptor >> 14U) >= size &&
                    (maskDescriptor & 0xFU) == 2U &&
                    ((maskDescriptor >> 10U) & 0xFU) == 1U &&
                    (maskDescriptor >> 14U) >= size && (size & 3U) == 0U) {
                    std::vector<uint32_t> values(size / sizeof(uint32_t));
                    std::vector<uint32_t> masks(values.size());
                    bool readable = true;
                    for (size_t index = 0; index < values.size(); ++index) {
                        const uint32_t byteOffset =
                            static_cast<uint32_t>(index * sizeof(uint32_t));
                        readable = readable &&
                                   memory.Read32(valueAddress + byteOffset,
                                                 &values[index]) &&
                                   memory.Read32(maskAddress + byteOffset,
                                                 &masks[index]);
                    }
                    std::string picaError;
                    if (readable &&
                        WritePicaHardwareRegisters(baseOffset, values, masks,
                                                   &picaError) &&
                        memory.Write32(commandBuffer,
                                       kGspWriteHwRegsWithMaskResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                        state.r[0] = kResultSuccess;
                        std::ostringstream detail;
                        detail << ":WriteHWRegsWithMask:base=0x" << std::hex
                               << baseOffset << ":size=0x" << size;
                        event.Name += detail.str();
                        event.Detail = size;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                    if (!picaError.empty()) {
                        event.Name += ":WriteHWRegsWithMask:" + picaError;
                    }
                }
            }
            if (event.Detail == kGspWriteHwRegsRequest && HasPicaBackend()) {
                uint32_t baseOffset = 0;
                uint32_t size = 0;
                uint32_t descriptor = 0;
                uint32_t sourceAddress = 0;
                if (memory.Read32(commandBuffer + 4U, &baseOffset) &&
                    memory.Read32(commandBuffer + 8U, &size) &&
                    memory.Read32(commandBuffer + 12U, &descriptor) &&
                    memory.Read32(commandBuffer + 16U, &sourceAddress) &&
                    (descriptor & 0xFU) == 2U &&
                    ((descriptor >> 10U) & 0xFU) == 0U &&
                    (descriptor >> 14U) >= size && (size & 3U) == 0U) {
                    std::vector<uint32_t> values(size / sizeof(uint32_t));
                    bool readable = true;
                    for (size_t index = 0; index < values.size(); ++index) {
                        readable =
                            readable &&
                            memory.Read32(sourceAddress +
                                              static_cast<uint32_t>(
                                                  index * sizeof(uint32_t)),
                                          &values[index]);
                    }
                    std::string picaError;
                    if (readable &&
                        WritePicaHardwareRegisters(baseOffset, values, {},
                                                   &picaError) &&
                        memory.Write32(commandBuffer,
                                       kGspWriteHwRegsResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                        state.r[0] = kResultSuccess;
                        std::ostringstream detail;
                        detail << ":WriteHWRegs:base=0x" << std::hex
                               << baseOffset << ":size=0x" << size;
                        event.Name += detail.str();
                        event.Detail = size;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                    if (!picaError.empty()) {
                        event.Name += ":WriteHWRegs:" + picaError;
                    }
                }
            }
            if (event.Detail == kGspAcquireRightRequest && session != nullptr) {
                uint32_t flags = 0;
                uint32_t descriptor = 0;
                uint32_t processHandle = 0;
                if (memory.Read32(commandBuffer + 4U, &flags) &&
                    memory.Read32(commandBuffer + 8U, &descriptor) &&
                    memory.Read32(commandBuffer + 12U, &processHandle) &&
                    descriptor == kCopyHandleDescriptor &&
                    processHandle == kCurrentProcessHandle &&
                    (!mGpuRightOwner || mGpuRightOwner == session->Object) &&
                    memory.Write32(commandBuffer, kGspAcquireRightResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                    mGpuRightOwner = session->Object;
                    state.r[0] = kResultSuccess;
                    event.Name += ":AcquireRight";
                    event.Detail = flags;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kGspRegisterInterruptRelayQueueRequest &&
                session != nullptr) {
                uint32_t flags = 0;
                uint32_t descriptor = 0;
                uint32_t interruptEventHandle = 0;
                const HandleRecord* interruptEvent = nullptr;
                if (memory.Read32(commandBuffer + 4U, &flags) &&
                    memory.Read32(commandBuffer + 8U, &descriptor) &&
                    memory.Read32(commandBuffer + 12U, &interruptEventHandle) &&
                    descriptor == kCopyHandleDescriptor &&
                    (interruptEvent = LookupHandle(interruptEventHandle)) !=
                        nullptr &&
                    interruptEvent->Object->Type.starts_with("event:") &&
                    !mGspInterruptEvent) {
                    if (!mGspSharedMemory) {
                        mGspSharedMemory = std::make_shared<KernelObject>();
                        mGspSharedMemory->Type = "shared_memory:gsp";
                        mGspSharedMemory->FileSize = 0x1000U;
                    }
                    const uint32_t sharedMemory =
                        CreateHandle(mGspSharedMemory);
                    if (memory.Write32(
                            commandBuffer,
                            kGspRegisterInterruptRelayQueueResponse) &&
                        memory.Write32(commandBuffer + 4U,
                                       kGspFirstInitialization) &&
                        memory.Write32(commandBuffer + 8U, 0U) &&
                        memory.Write32(commandBuffer + 12U,
                                       kCopyHandleDescriptor) &&
                        memory.Write32(commandBuffer + 16U, sharedMemory)) {
                        mGspInterruptEvent = interruptEvent->Object;
                        mGspThreadId = 0U;
                        state.r[0] = kResultSuccess;
                        event.Name += ":RegisterInterruptRelayQueue";
                        event.Detail = flags;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                    CloseHandle(sharedMemory);
                }
            }
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (*sessionType == "client_session:APT:U") {
            if (event.Detail == kAptGetLockHandleRequest) {
                uint32_t attributes = 0;
                if (memory.Read32(commandBuffer + 4U, &attributes)) {
                    if (!mAptLock) {
                        mAptLock = std::make_shared<KernelObject>();
                        mAptLock->Type = "mutex:apt_lock";
                        mAptLock->AvailableCount = 1;
                    }
                    const uint32_t lock = CreateHandle(mAptLock);
                    if (memory.Write32(commandBuffer,
                                       kAptGetLockHandleResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 8U, attributes) &&
                        memory.Write32(commandBuffer + 12U, 0U) &&
                        memory.Write32(commandBuffer + 16U,
                                       kCopyHandleDescriptor) &&
                        memory.Write32(commandBuffer + 20U, lock)) {
                        state.r[0] = kResultSuccess;
                        event.Name += ":GetLockHandle";
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                    CloseHandle(lock);
                }
            }
            if (event.Detail == kAptInitializeRequest) {
                uint32_t appletId = 0;
                uint32_t attributes = 0;
                if (memory.Read32(commandBuffer + 4U, &appletId) &&
                    memory.Read32(commandBuffer + 8U, &attributes)) {
                    if (!mAptNotificationEvent) {
                        mAptNotificationEvent =
                            std::make_shared<KernelObject>();
                        mAptNotificationEvent->Type = "event:apt_notification";
                    }
                    if (!mAptParameterEvent) {
                        mAptParameterEvent = std::make_shared<KernelObject>();
                        mAptParameterEvent->Type = "event:apt_parameter";
                        // The first initialized application receives Wakeup.
                        mAptParameterEvent->AvailableCount = 1;
                    }
                    const uint32_t notification =
                        CreateHandle(mAptNotificationEvent);
                    const uint32_t parameter = CreateHandle(mAptParameterEvent);
                    if (memory.Write32(commandBuffer, kAptInitializeResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 8U,
                                       kTwoCopyHandlesDescriptor) &&
                        memory.Write32(commandBuffer + 12U, notification) &&
                        memory.Write32(commandBuffer + 16U, parameter)) {
                        state.r[0] = kResultSuccess;
                        event.Name += ":Initialize";
                        event.Handled = true;
                        event.Detail = appletId;
                        mAptApplicationId = appletId;
                        mAptWakeupPending = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                    CloseHandle(notification);
                    CloseHandle(parameter);
                }
            }
            if (event.Detail == kAptEnableRequest &&
                memory.Write32(commandBuffer, kAptEnableResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                state.r[0] = kResultSuccess;
                event.Name += ":Enable";
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
            if (event.Detail == kAptNotifyToWaitRequest &&
                memory.Write32(commandBuffer, kAptNotifyToWaitResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                state.r[0] = kResultSuccess;
                event.Name += ":NotifyToWait";
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
            if (event.Detail == kAptReceiveParameterRequest &&
                mAptWakeupPending) {
                uint32_t appletId = 0;
                uint32_t bufferSize = 0;
                uint32_t staticDescriptor = 0;
                uint32_t staticAddress = 0;
                const uint32_t staticBufferTable =
                    state.thread_pointer + 0x180U;
                if (memory.Read32(commandBuffer + 4U, &appletId) &&
                    memory.Read32(commandBuffer + 8U, &bufferSize) &&
                    appletId == mAptApplicationId && bufferSize <= 0x1000U &&
                    memory.Read32(staticBufferTable, &staticDescriptor) &&
                    memory.Read32(staticBufferTable + 4U, &staticAddress) &&
                    (staticDescriptor & 0xFU) == 2U &&
                    (staticDescriptor >> 14U) >= bufferSize &&
                    memory.Fill(staticAddress, bufferSize, 0) &&
                    memory.Write32(commandBuffer,
                                   kAptReceiveParameterResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                    memory.Write32(commandBuffer + 8U, 0U) &&
                    memory.Write32(commandBuffer + 12U, 1U) &&
                    memory.Write32(commandBuffer + 16U, 0U) &&
                    memory.Write32(commandBuffer + 20U,
                                   kMoveHandleDescriptor) &&
                    memory.Write32(commandBuffer + 24U, 0U) &&
                    memory.Write32(commandBuffer + 28U,
                                   (bufferSize << 14U) | 2U) &&
                    memory.Write32(commandBuffer + 32U, staticAddress)) {
                    mAptWakeupPending = false;
                    state.r[0] = kResultSuccess;
                    event.Name += ":ReceiveParameter:Wakeup";
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kAptAppletUtilityRequest) {
                uint32_t utilityCommand = 0;
                uint32_t inputSize = 0;
                uint32_t outputSize = 0;
                uint32_t inputDescriptor = 0;
                uint32_t inputAddress = 0;
                uint32_t outputDescriptor = 0;
                uint32_t outputAddress = 0;
                uint32_t transition = 0;
                uint8_t shellState = 0;
                const uint32_t staticBufferTable =
                    state.thread_pointer + 0x180U;
                if (memory.Read32(commandBuffer + 4U, &utilityCommand) &&
                    memory.Read32(commandBuffer + 8U, &inputSize) &&
                    memory.Read32(commandBuffer + 12U, &outputSize) &&
                    memory.Read32(commandBuffer + 16U, &inputDescriptor) &&
                    memory.Read32(commandBuffer + 20U, &inputAddress) &&
                    memory.Read32(staticBufferTable, &outputDescriptor) &&
                    memory.Read32(staticBufferTable + 4U, &outputAddress)) {
                    event.Name += ":AppletUtility:command=" +
                                  std::to_string(utilityCommand) +
                                  ":input=" + std::to_string(inputSize) +
                                  ":output=" + std::to_string(outputSize);
                    const bool validStaticBuffers =
                        (inputDescriptor & 0xFU) == 2U &&
                        ((inputDescriptor >> 10U) & 0xFU) == 1U &&
                        (inputDescriptor >> 14U) >= inputSize &&
                        (outputDescriptor & 0xFU) == 2U &&
                        ((outputDescriptor >> 10U) & 0xFU) == 0U &&
                        (outputDescriptor >> 14U) >= outputSize;
                    const bool unlockTransition =
                        utilityCommand == kAptUtilityUnlockTransition &&
                        inputSize == sizeof(uint32_t) && outputSize == 1U &&
                        memory.Read32(inputAddress, &transition);
                    const bool sleepIfShellClosed =
                        utilityCommand == kAptUtilitySleepIfShellClosed &&
                        inputSize == sizeof(uint8_t) && outputSize == 1U &&
                        memory.Read8(inputAddress, &shellState);
                    if (validStaticBuffers &&
                        (unlockTransition || sleepIfShellClosed) &&
                        memory.Fill(outputAddress, outputSize, 0U) &&
                        memory.Write32(commandBuffer,
                                       kAptAppletUtilityResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 8U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 12U,
                                       (outputSize << 14U) | 2U) &&
                        memory.Write32(commandBuffer + 16U, outputAddress)) {
                        state.r[0] = kResultSuccess;
                        if (unlockTransition) {
                            event.Name += ":UnlockTransition:" +
                                          std::to_string(transition);
                            event.Detail = transition;
                        } else {
                            event.Name += ":SleepIfShellClosed:open";
                            event.Detail = shellState;
                        }
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (*sessionType == "client_session:fs:USER") {
            if (event.Detail == kFsInitializeRequest) {
                uint32_t descriptor = 0;
                if (memory.Read32(commandBuffer + 4U, &descriptor) &&
                    descriptor == kCallingPidDescriptor &&
                    memory.Write32(commandBuffer, kFsInitializeResponse) &&
                    memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                    state.r[0] = kResultSuccess;
                    event.Name += ":Initialize";
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
            if (event.Detail == kFsOpenFileDirectlyRequest) {
                std::array<uint32_t, 12> words{};
                bool readable = true;
                for (size_t index = 0; index < words.size(); ++index) {
                    readable =
                        readable &&
                        memory.Read32(commandBuffer + 4U +
                                          static_cast<uint32_t>(index * 4U),
                                      &words[index]);
                }
                if (readable) {
                    event.Name += ":OpenFileDirectly:archive=0x";
                    std::ostringstream request;
                    request
                        << std::hex << std::setw(8) << std::setfill('0')
                        << words[1] << ":archive_path="
                        << DescribeLowPath(memory, words[2], words[3], words[9])
                        << "(type=" << words[2] << ",size=" << words[3]
                        << ",desc=0x" << words[8] << ")"
                        << ":file_path="
                        << DescribeLowPath(memory, words[4], words[5],
                                           words[11])
                        << "(type=" << words[4] << ",size=" << words[5]
                        << ",desc=0x" << words[10] << ")"
                        << ":mode=0x" << words[6] << ":attrs=0x" << words[7];
                    event.Name += request.str();
                    uint32_t pathType = 0;
                    uint32_t pathReserved0 = 0;
                    uint32_t pathReserved1 = 0;
                    const bool requestsRomFs =
                        words[1] == 3U && words[2] == 1U && words[3] == 1U &&
                        words[4] == 2U && words[5] == 12U && words[6] == 1U &&
                        (words[8] & 0xFU) == 2U &&
                        (words[8] >> 14U) == words[3] &&
                        (words[10] & 0xFU) == 2U &&
                        (words[10] >> 14U) == words[5] &&
                        memory.Read32(words[11], &pathType) &&
                        memory.Read32(words[11] + 4U, &pathReserved0) &&
                        memory.Read32(words[11] + 8U, &pathReserved1) &&
                        pathType == 0U && pathReserved0 == 0U &&
                        pathReserved1 == 0U;
                    const bool opensRomFs = requestsRomFs &&
                                            mConfig.RomFsImageSize != 0U &&
                                            (mConfig.Filesystem != nullptr ||
                                             !mConfig.RomFsImagePath.empty());
                    if (opensRomFs) {
                        auto file = std::make_shared<KernelObject>();
                        file->Type = "client_session:file:romfs";
                        file->FileOffset = mConfig.RomFsImageOffset;
                        file->FileSize = mConfig.RomFsImageSize;
                        bool opened = false;
                        if (mConfig.Filesystem != nullptr) {
                            file->FilePath = mConfig.RomFsContentPath;
                            opened =
                                mConfig.Filesystem->Open(
                                    NativeA32CtrFilesystemRoot::Content,
                                    mConfig.RomFsContentPath,
                                    NativeA32CtrFileOpenRead,
                                    &file->FileBackend) &&
                                file->FileBackend != nullptr &&
                                file->FileOffset <= file->FileBackend->Size() &&
                                file->FileSize <= file->FileBackend->Size() -
                                                      file->FileOffset;
                        } else {
                            file->FilePath = mConfig.RomFsImagePath;
                            file->RomFsBacking =
                                std::make_shared<RomFsBackingFile>(
                                    file->FilePath);
                            opened = true;
                        }
                        const uint32_t handle =
                            opened ? CreateHandle(file) : 0U;
                        if (handle != 0U &&
                            memory.Write32(commandBuffer,
                                           kFsOpenFileDirectlyResponse) &&
                            memory.Write32(commandBuffer + 4U,
                                           kResultSuccess) &&
                            memory.Write32(commandBuffer + 8U,
                                           kMoveHandleDescriptor) &&
                            memory.Write32(commandBuffer + 12U, handle)) {
                            state.r[0] = kResultSuccess;
                            event.Handled = true;
                            mSvcEvents.push_back(std::move(event));
                            return {NativeA32HostAction::Resume};
                        }
                        if (handle != 0U) {
                            CloseHandle(handle);
                        }
                    }
                }
            }
            if (event.Detail == kFsCreateFileRequest) {
                uint64_t archiveHandle = 0;
                uint32_t pathType = 0;
                uint32_t pathSize = 0;
                uint32_t attributes = 0;
                uint64_t fileSize = 0;
                uint32_t descriptor = 0;
                uint32_t pathAddress = 0;
                uint32_t faultAddress = 0;
                const bool readable =
                    memory.Read64(commandBuffer + 8U, &archiveHandle,
                                  &faultAddress) &&
                    memory.Read32(commandBuffer + 16U, &pathType) &&
                    memory.Read32(commandBuffer + 20U, &pathSize) &&
                    memory.Read32(commandBuffer + 24U, &attributes) &&
                    memory.Read64(commandBuffer + 28U, &fileSize,
                                  &faultAddress) &&
                    memory.Read32(commandBuffer + 36U, &descriptor) &&
                    memory.Read32(commandBuffer + 40U, &pathAddress) &&
                    (descriptor & 0x3FFU) == 2U &&
                    (descriptor >> 14U) == pathSize;
                if (readable) {
                    const auto archive = std::find_if(
                        mArchives.begin(), mArchives.end(),
                        [archiveHandle](const ArchiveRecord& record) {
                            return record.Handle == archiveHandle;
                        });
                    const auto relativePath = DecodeArchiveRelativePath(
                        memory, pathType, pathSize, pathAddress);
                    uint32_t result = kFsInvalidArchiveHandle;
                    if (archive != mArchives.end() &&
                        relativePath.has_value()) {
                        if (mConfig.Filesystem != nullptr) {
                            const std::string logicalPath =
                                relativePath->generic_string();
                            std::shared_ptr<NativeA32CtrFile> existing;
                            if (mConfig.Filesystem->Open(
                                    NativeA32CtrFilesystemRoot::Save,
                                    logicalPath, NativeA32CtrFileOpenRead,
                                    &existing)) {
                                result = kFsFileAlreadyExists;
                            } else if (fileSize <=
                                       static_cast<uint64_t>(
                                           std::numeric_limits<
                                               std::int64_t>::max())) {
                                std::shared_ptr<NativeA32CtrFile> created;
                                if (mConfig.Filesystem->Open(
                                        NativeA32CtrFilesystemRoot::Save,
                                        logicalPath,
                                        NativeA32CtrFileOpenRead |
                                            NativeA32CtrFileOpenWrite |
                                            NativeA32CtrFileOpenCreate |
                                            NativeA32CtrFileOpenTruncate,
                                        &created) &&
                                    created != nullptr &&
                                    created->Resize(fileSize)) {
                                    result = kResultSuccess;
                                }
                            }
                        } else {
                            const auto filePath = archive->Root / *relativePath;
                            std::error_code fileError;
                            if (std::filesystem::exists(filePath, fileError)) {
                                result = kFsFileAlreadyExists;
                            } else if (!fileError &&
                                       fileSize <=
                                           static_cast<uint64_t>(
                                               std::numeric_limits<
                                                   std::int64_t>::max())) {
                                std::ofstream create(filePath,
                                                     std::ios::binary |
                                                         std::ios::trunc);
                                if (create.good()) {
                                    create.close();
                                    std::filesystem::resize_file(
                                        filePath, fileSize, fileError);
                                    if (!fileError) {
                                        result = kResultSuccess;
                                    }
                                }
                            }
                        }
                    }
                    if (memory.Write32(commandBuffer, kFsCreateFileResponse) &&
                        memory.Write32(commandBuffer + 4U, result)) {
                        state.r[0] = kResultSuccess;
                        event.Name += ":CreateFile:" +
                                      DescribeLowPath(memory, pathType,
                                                      pathSize, pathAddress) +
                                      ":size=" + std::to_string(fileSize) +
                                      ":attrs=" + std::to_string(attributes);
                        event.Detail = result;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kFsOpenFileRequest) {
                uint32_t transaction = 0;
                uint64_t archiveHandle = 0;
                uint32_t pathType = 0;
                uint32_t pathSize = 0;
                uint32_t openMode = 0;
                uint32_t attributes = 0;
                uint32_t descriptor = 0;
                uint32_t pathAddress = 0;
                uint32_t faultAddress = 0;
                const bool readable =
                    memory.Read32(commandBuffer + 4U, &transaction) &&
                    memory.Read64(commandBuffer + 8U, &archiveHandle,
                                  &faultAddress) &&
                    memory.Read32(commandBuffer + 16U, &pathType) &&
                    memory.Read32(commandBuffer + 20U, &pathSize) &&
                    memory.Read32(commandBuffer + 24U, &openMode) &&
                    memory.Read32(commandBuffer + 28U, &attributes) &&
                    memory.Read32(commandBuffer + 32U, &descriptor) &&
                    memory.Read32(commandBuffer + 36U, &pathAddress) &&
                    (descriptor & 0x3FFU) == 2U &&
                    (descriptor >> 14U) == pathSize;
                if (readable) {
                    const auto archive = std::find_if(
                        mArchives.begin(), mArchives.end(),
                        [archiveHandle](const ArchiveRecord& record) {
                            return record.Handle == archiveHandle;
                        });
                    const auto relativePath = DecodeArchiveRelativePath(
                        memory, pathType, pathSize, pathAddress);
                    uint32_t result = kFsInvalidArchiveHandle;
                    uint32_t fileSession = 0;
                    if (archive != mArchives.end() &&
                        relativePath.has_value()) {
                        const bool establishWritableSaveFile =
                            archive->Id == kFsSaveDataArchiveId &&
                            (openMode & 2U) != 0U;
                        if (mConfig.Filesystem != nullptr) {
                            uint32_t backendFlags = 0U;
                            backendFlags |= (openMode & 1U) != 0U
                                                ? NativeA32CtrFileOpenRead
                                                : 0U;
                            backendFlags |= (openMode & 2U) != 0U
                                                ? NativeA32CtrFileOpenWrite
                                                : 0U;
                            backendFlags |= ((openMode & 4U) != 0U ||
                                             establishWritableSaveFile)
                                                ? NativeA32CtrFileOpenCreate
                                                : 0U;
                            auto file = std::make_shared<KernelObject>();
                            file->Type = "client_session:file:savedata";
                            file->FilePath = *relativePath;
                            file->FileOffset = 0;
                            file->FileOpenMode = openMode;
                            if (mConfig.Filesystem->Open(
                                    NativeA32CtrFilesystemRoot::Save,
                                    relativePath->generic_string(),
                                    backendFlags, &file->FileBackend) &&
                                file->FileBackend != nullptr) {
                                file->FileSize = file->FileBackend->Size();
                                fileSession = CreateHandle(file);
                                result = kResultSuccess;
                            }
                        } else {
                            const auto filePath = archive->Root / *relativePath;
                            std::error_code fileError;
                            bool exists = std::filesystem::is_regular_file(
                                filePath, fileError);
                            if (!exists &&
                                fileError ==
                                    std::errc::no_such_file_or_directory) {
                                fileError.clear();
                            }
                            if (!exists && !fileError &&
                                ((openMode & 4U) != 0U ||
                                 establishWritableSaveFile)) {
                                std::ofstream create(
                                    filePath, std::ios::binary | std::ios::app);
                                exists = create.good();
                            }
                            if (exists && !fileError) {
                                auto file = std::make_shared<KernelObject>();
                                file->Type = "client_session:file:savedata";
                                file->FilePath = filePath;
                                file->FileOffset = 0;
                                file->FileSize = std::filesystem::file_size(
                                    filePath, fileError);
                                file->FileOpenMode = openMode;
                                if (!fileError) {
                                    fileSession = CreateHandle(file);
                                    result = kResultSuccess;
                                }
                            }
                        }
                        if (fileSession == 0U &&
                            result == kFsInvalidArchiveHandle) {
                            result = kFsFileNotFound;
                        }
                    }
                    if (memory.Write32(commandBuffer, kFsOpenFileResponse) &&
                        memory.Write32(commandBuffer + 4U, result) &&
                        memory.Write32(commandBuffer + 8U,
                                       kMoveHandleDescriptor) &&
                        memory.Write32(commandBuffer + 12U, fileSession)) {
                        state.r[0] = kResultSuccess;
                        event.Name += ":OpenFile:" +
                                      DescribeLowPath(memory, pathType,
                                                      pathSize, pathAddress);
                        std::ostringstream openDetail;
                        openDetail << ":mode=0x" << std::hex << openMode
                                   << ":attrs=0x" << attributes;
                        event.Name += openDetail.str();
                        event.Detail = result;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                    if (fileSession != 0U) {
                        CloseHandle(fileSession);
                    }
                }
            }
            if (event.Detail == kFsOpenArchiveRequest) {
                uint32_t archiveId = 0;
                uint32_t pathType = 0;
                uint32_t pathSize = 0;
                uint32_t descriptor = 0;
                uint32_t pathAddress = 0;
                const bool validStaticBuffer =
                    memory.Read32(commandBuffer + 4U, &archiveId) &&
                    memory.Read32(commandBuffer + 8U, &pathType) &&
                    memory.Read32(commandBuffer + 12U, &pathSize) &&
                    memory.Read32(commandBuffer + 16U, &descriptor) &&
                    memory.Read32(commandBuffer + 20U, &pathAddress) &&
                    (descriptor & 0x3FFU) == 2U &&
                    (descriptor >> 14U) == pathSize &&
                    memory.IsMapped(pathAddress, pathSize);
                if (validStaticBuffer && archiveId == kFsSaveDataArchiveId &&
                    pathType == 1U && pathSize == 1U &&
                    (mConfig.Filesystem != nullptr ||
                     !mConfig.SaveDataDirectory.empty())) {
                    bool openedSaveRoot = false;
                    if (mConfig.Filesystem != nullptr) {
                        openedSaveRoot = mConfig.Filesystem->EnsureSaveRoot();
                    } else {
                        std::error_code directoryError;
                        std::filesystem::create_directories(
                            mConfig.SaveDataDirectory, directoryError);
                        openedSaveRoot = !directoryError;
                    }
                    if (openedSaveRoot) {
                        const uint64_t archiveHandle = mNextArchiveHandle++;
                        mArchives.push_back({archiveHandle, archiveId,
                                             mConfig.Filesystem != nullptr
                                                 ? std::filesystem::path("save")
                                                 : mConfig.SaveDataDirectory});
                        uint32_t faultAddress = 0;
                        if (memory.Write32(commandBuffer,
                                           kFsOpenArchiveResponse) &&
                            memory.Write32(commandBuffer + 4U,
                                           kResultSuccess) &&
                            memory.Write64(commandBuffer + 8U, archiveHandle,
                                           &faultAddress)) {
                            state.r[0] = kResultSuccess;
                            event.Name += ":OpenArchive:SaveData";
                            event.Detail = archiveId;
                            event.Handled = true;
                            mSvcEvents.push_back(std::move(event));
                            return {NativeA32HostAction::Resume};
                        }
                        mArchives.pop_back();
                    }
                }
            }
            if (event.Detail == kFsControlArchiveRequest) {
                uint64_t archiveHandle = 0;
                uint32_t action = 0;
                uint32_t inputSize = 0;
                uint32_t outputSize = 0;
                uint32_t inputDescriptor = 0;
                uint32_t inputAddress = 0;
                uint32_t outputDescriptor = 0;
                uint32_t outputAddress = 0;
                uint32_t faultAddress = 0;
                const bool readable =
                    memory.Read64(commandBuffer + 4U, &archiveHandle,
                                  &faultAddress) &&
                    memory.Read32(commandBuffer + 12U, &action) &&
                    memory.Read32(commandBuffer + 16U, &inputSize) &&
                    memory.Read32(commandBuffer + 20U, &outputSize) &&
                    memory.Read32(commandBuffer + 24U, &inputDescriptor) &&
                    memory.Read32(commandBuffer + 28U, &inputAddress) &&
                    memory.Read32(commandBuffer + 32U, &outputDescriptor) &&
                    memory.Read32(commandBuffer + 36U, &outputAddress) &&
                    inputDescriptor == ((inputSize << 4U) | 0xAU) &&
                    outputDescriptor == ((outputSize << 4U) | 0xCU) &&
                    memory.IsMapped(inputAddress, inputSize) &&
                    memory.IsMapped(outputAddress, outputSize);
                if (readable) {
                    const auto archive = std::find_if(
                        mArchives.begin(), mArchives.end(),
                        [archiveHandle](const ArchiveRecord& record) {
                            return record.Handle == archiveHandle;
                        });
                    const uint32_t result = archive == mArchives.end()
                                                ? kFsInvalidArchiveHandle
                                                : kResultSuccess;
                    const bool outputInitialized =
                        result != kResultSuccess || outputSize == 0U ||
                        memory.Fill(outputAddress, outputSize, 0U);
                    if (outputInitialized &&
                        memory.Write32(commandBuffer,
                                       kFsControlArchiveResponse) &&
                        memory.Write32(commandBuffer + 4U, result)) {
                        state.r[0] = kResultSuccess;
                        event.Name += ":ControlArchive:handle=0x";
                        std::ostringstream controlDetail;
                        controlDetail
                            << std::hex << archiveHandle << ":action=0x"
                            << action << std::dec << ":input=" << inputSize
                            << ":output=" << outputSize << ":result=0x"
                            << std::hex << result;
                        event.Name += controlDetail.str();
                        event.Detail = result;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            if (event.Detail == kFsCloseArchiveRequest) {
                uint64_t archiveHandle = 0;
                uint32_t faultAddress = 0;
                if (memory.Read64(commandBuffer + 4U, &archiveHandle,
                                  &faultAddress)) {
                    const auto archive = std::find_if(
                        mArchives.begin(), mArchives.end(),
                        [archiveHandle](const ArchiveRecord& record) {
                            return record.Handle == archiveHandle;
                        });
                    const uint32_t result = archive == mArchives.end()
                                                ? kFsInvalidArchiveHandle
                                                : kResultSuccess;
                    if (memory.Write32(commandBuffer,
                                       kFsCloseArchiveResponse) &&
                        memory.Write32(commandBuffer + 4U, result)) {
                        if (archive != mArchives.end()) {
                            mArchives.erase(archive);
                        }
                        state.r[0] = kResultSuccess;
                        event.Name += ":CloseArchive:handle=0x";
                        std::ostringstream handleDetail;
                        handleDetail << std::hex << archiveHandle
                                     << ":result=0x" << result;
                        event.Name += handleDetail.str();
                        event.Detail = result;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (*sessionType != "client_session:srv:") {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        if (event.Detail == kSrvRegisterClientRequest &&
            memory.Write32(commandBuffer, kSrvRegisterClientResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            state.r[0] = kResultSuccess;
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
        if (event.Detail == kSrvEnableNotificationRequest &&
            mConfig.ResourceCurrentValues[kResourceSemaphore] <
                mConfig.ResourceLimitValues[kResourceSemaphore]) {
            const uint32_t semaphore =
                CreateHandle("semaphore:srv_notification");
            if (memory.Write32(commandBuffer, kSrvEnableNotificationResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                memory.Write32(commandBuffer + 8U, kCopyHandleDescriptor) &&
                memory.Write32(commandBuffer + 12U, semaphore)) {
                ++mConfig.ResourceCurrentValues[kResourceSemaphore];
                state.r[0] = kResultSuccess;
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
        }
        if (event.Detail == kSrvGetServiceHandleRequest) {
            uint32_t nameLow = 0;
            uint32_t nameHigh = 0;
            uint32_t nameLength = 0;
            if (memory.Read32(commandBuffer + 4U, &nameLow) &&
                memory.Read32(commandBuffer + 8U, &nameHigh) &&
                memory.Read32(commandBuffer + 12U, &nameLength) &&
                nameLength <= 8U) {
                std::array<char, 8> bytes{};
                for (size_t index = 0; index < 4; ++index) {
                    bytes[index] = static_cast<char>(nameLow >> (index * 8U));
                    bytes[index + 4U] =
                        static_cast<char>(nameHigh >> (index * 8U));
                }
                event.Name += ":GetServiceHandle:" +
                              std::string(bytes.data(), nameLength);
                const std::string serviceName(bytes.data(), nameLength);
                if (serviceName == "APT:U" || serviceName == "fs:USER" ||
                    serviceName == "gsp::Gpu" || serviceName == "dsp::DSP" ||
                    serviceName == "cfg:u" || serviceName == "hid:USER" ||
                    serviceName == "ndm:u") {
                    const uint32_t session =
                        CreateHandle("client_session:" + serviceName);
                    if (memory.Write32(commandBuffer,
                                       kSrvGetServiceHandleResponse) &&
                        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                        memory.Write32(commandBuffer + 8U,
                                       kMoveHandleDescriptor) &&
                        memory.Write32(commandBuffer + 12U, session)) {
                        state.r[0] = kResultSuccess;
                        event.Handled = true;
                        mSvcEvents.push_back(std::move(event));
                        return {NativeA32HostAction::Resume};
                    }
                }
            }
        }
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Wait};
    }
    case 0x39:
        return HandleResourceLimitValues(false, state, memory,
                                         std::move(event));
    case 0x3A:
        return HandleResourceLimitValues(true, state, memory, std::move(event));
    case 0x38:
        event.Handled = true;
        event.Name = "GetResourceLimit";
        if (state.r[1] != kCurrentProcessHandle) {
            event.Handled = false;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        state.r[0] = kResultSuccess;
        state.r[1] = CreateHandle("resource_limit");
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    default:
        event.Name = "unimplemented";
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Wait};
    }
}

NativeA32HostResult NativeA32CtrHostServices::HandleFallback(
    oot3d::recomp::a32::FallbackReason reason, uint32_t pc,
    const oot3d::recomp::a32::PackedOp& op,
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory) {
    static_cast<void>(state);
    static_cast<void>(memory);
    mFallbackPc = pc;
    mFallbackReason = reason;
    mFallbackRawOperation = op.raw;
    std::ostringstream detail;
    detail << "boot reached an unimplemented A32 fallback at pc=0x" << std::hex
           << pc << ", raw=0x" << op.raw << std::dec
           << ", reason=" << static_cast<uint32_t>(reason);
    return {NativeA32HostAction::Fault, std::nullopt, 0, detail.str()};
}

bool NativeA32CtrHostServices::CompleteSynchronousWait(
    uint32_t immediate, const NativeA32HostResult& waitResult,
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, std::string* error) {
    static_cast<void>(memory);
    if (immediate != 0x0AU || waitResult.Action != NativeA32HostAction::Wait) {
        return false;
    }

    const uint64_t rawNanoseconds = static_cast<uint64_t>(state.r[0]) |
                                    (static_cast<uint64_t>(state.r[1]) << 32U);
    const int64_t nanoseconds = static_cast<int64_t>(rawNanoseconds);
    if (nanoseconds <= 0) {
        if (error != nullptr) {
            *error = "synchronous SleepThread requires a finite positive "
                     "duration";
        }
        return false;
    }

    const auto waiter =
        std::find_if(mSleepWaiters.begin(), mSleepWaiters.end(),
                     [&](const SleepWaiter& candidate) {
                         return candidate.Process == &context.Process &&
                                candidate.ThreadId == context.ThreadId;
                     });
    if (waiter == mSleepWaiters.end()) {
        if (error != nullptr) {
            *error = "synchronous SleepThread has no matching CTR timer";
        }
        return false;
    }

    const uint64_t wakeTick = waiter->WakeTick;
    mSleepWaiters.erase(waiter);
    if (wakeTick > mSystemTicks) {
        AdvanceSystemTicks(wakeTick - mSystemTicks);
    } else {
        WakeSleepWaiters();
    }
    return true;
}

const std::vector<NativeA32CtrSvcEvent>&
NativeA32CtrHostServices::SvcEvents() const {
    return mSvcEvents.Events();
}

const std::map<std::string, uint64_t>&
NativeA32CtrHostServices::IpcNameCounts() const noexcept {
    return mSvcEvents.IpcNameCounts();
}

uint64_t NativeA32CtrHostServices::SvcEventCount() const noexcept {
    return mSvcEvents.TotalEventCount();
}

void NativeA32CtrHostServices::WakeSleepWaiters() {
    for (auto waiter = mSleepWaiters.begin(); waiter != mSleepWaiters.end();) {
        if (waiter->WakeTick > mSystemTicks) {
            ++waiter;
            continue;
        }
        if (waiter->Process != nullptr &&
            waiter->Process->ThreadStatus(waiter->ThreadId) ==
                NativeA32ThreadStatus::Waiting) {
            waiter->Process->ResumeThread(waiter->ThreadId);
        }
        waiter = mSleepWaiters.erase(waiter);
    }
}

void NativeA32CtrHostServices::AdvanceSystemTicks(uint64_t ticks) {
    mSystemTicks = ticks > std::numeric_limits<uint64_t>::max() - mSystemTicks
                       ? std::numeric_limits<uint64_t>::max()
                       : mSystemTicks + ticks;
    WakeSleepWaiters();
    if (!mDspAudioRunning || mNextDspAudioFrameTick == 0U ||
        mSystemTicks < mNextDspAudioFrameTick) {
        return;
    }
    const uint64_t elapsedFrames =
        1U + (mSystemTicks - mNextDspAudioFrameTick) / DspAudioFrameTicks;
    const uint64_t pendingFrames =
        static_cast<uint64_t>(mPendingDspAudioFrames) + elapsedFrames;
    mPendingDspAudioFrames = static_cast<uint32_t>(std::min<uint64_t>(
        pendingFrames, std::numeric_limits<uint32_t>::max()));
    mNextDspAudioFrameTick += elapsedFrames * DspAudioFrameTicks;
}

std::optional<uint64_t> NativeA32CtrHostServices::NextSleepWakeTick() const {
    if (mSleepWaiters.empty()) {
        return std::nullopt;
    }
    return std::min_element(mSleepWaiters.begin(), mSleepWaiters.end(),
                            [](const SleepWaiter& lhs, const SleepWaiter& rhs) {
                                return lhs.WakeTick < rhs.WakeTick;
                            })
        ->WakeTick;
}

size_t NativeA32CtrHostServices::PendingSleepCount() const {
    return mSleepWaiters.size();
}

uint32_t NativeA32CtrHostServices::TakePendingDspAudioFrames() {
    const uint32_t frames = mPendingDspAudioFrames;
    mPendingDspAudioFrames = 0U;
    return frames;
}

bool NativeA32CtrHostServices::SignalDspAudioFrame() {
    return mDspAudioRunning && SignalDspInterrupt(2U, kDspAudioPipe);
}

bool NativeA32CtrHostServices::WriteHidSample(NativeA32Memory& memory,
                                              const NativeA32HidState& state,
                                              uint64_t sampleTick) {
    if (!mHidSharedMemory || !mHidSharedMemory->MappedAddress.has_value()) {
        return false;
    }
    const uint32_t base = *mHidSharedMemory->MappedAddress;
    const uint32_t index = mNextHidPadIndex;
    const uint32_t previousIndex = (index + 7U) % 8U;
    const uint32_t entry =
        base + kHidPadEntriesOffset + index * kHidPadEntrySize;
    const uint32_t previousEntry =
        base + kHidPadEntriesOffset + previousIndex * kHidPadEntrySize;
    uint32_t previousButtons = 0;
    if (!memory.Read32(previousEntry, &previousButtons)) {
        return false;
    }
    const uint32_t buttons = ApplyCirclePadDirections(
        state.Buttons & ThreeDsRecomp::Input::kStandardHidButtonMask,
        state.CirclePadX, state.CirclePadY);
    const uint32_t changed = buttons ^ previousButtons;
    const uint32_t additions = changed & buttons;
    const uint32_t removals = changed & previousButtons;

    bool written =
        memory.WriteHost<uint32_t>(base + kHidPadCurrentStateOffset, buttons) &&
        memory.WriteHost<uint32_t>(base + kHidPadIndexOffset, index) &&
        memory.WriteHost<uint32_t>(entry, buttons) &&
        memory.WriteHost<uint32_t>(entry + 4U, additions) &&
        memory.WriteHost<uint32_t>(entry + 8U, removals) &&
        memory.WriteHost<uint16_t>(entry + 12U,
                                   static_cast<uint16_t>(state.CirclePadX)) &&
        memory.WriteHost<uint16_t>(entry + 14U,
                                   static_cast<uint16_t>(state.CirclePadY));
    if (index == 0U) {
        uint64_t previousResetTick = 0;
        written = written &&
                  memory.Read64(base + kHidPadIndexResetTicksOffset,
                                &previousResetTick, nullptr) &&
                  memory.WriteHost<uint64_t>(
                      base + kHidPadIndexResetTicksPreviousOffset,
                      previousResetTick) &&
                  memory.WriteHost<uint64_t>(
                      base + kHidPadIndexResetTicksOffset, sampleTick);
    }

    const uint32_t touchIndex = mNextHidTouchIndex;
    const uint32_t touchEntry =
        base + kHidTouchEntriesOffset + touchIndex * kHidTouchEntrySize;
    written =
        written &&
        memory.WriteHost<uint32_t>(base + kHidTouchIndexOffset, touchIndex) &&
        memory.WriteHost<uint16_t>(touchEntry, state.TouchX) &&
        memory.WriteHost<uint16_t>(touchEntry + 2U, state.TouchY) &&
        memory.WriteHost<uint32_t>(touchEntry + 4U,
                                   state.TouchPressed ? 1U : 0U);
    if (touchIndex == 0U) {
        uint64_t previousResetTick = 0;
        written = written &&
                  memory.Read64(base + kHidTouchIndexResetTicksOffset,
                                &previousResetTick, nullptr) &&
                  memory.WriteHost<uint64_t>(
                      base + kHidTouchIndexResetTicksPreviousOffset,
                      previousResetTick) &&
                  memory.WriteHost<uint64_t>(
                      base + kHidTouchIndexResetTicksOffset, sampleTick);
    }
    if (!written) {
        return false;
    }

    mNextHidPadIndex = (index + 1U) % 8U;
    mNextHidTouchIndex = (touchIndex + 1U) % 8U;
    ++mHidRuntimeProfile.SamplesWritten;
    mHidRuntimeProfile.LastSampleTick = sampleTick;
    mHidRuntimeProfile.LastButtons = buttons;
    mHidRuntimeProfile.LastAdditions = additions;
    mHidRuntimeProfile.LastRemovals = removals;
    mHidRuntimeProfile.LastCirclePadX = state.CirclePadX;
    mHidRuntimeProfile.LastCirclePadY = state.CirclePadY;
    mHidRuntimeProfile.NextPadIndex = mNextHidPadIndex;
    return true;
}

bool NativeA32CtrHostServices::WriteHidAccelerometerSample(
    NativeA32Memory& memory, const NativeA32HidState& state,
    uint64_t sampleTick) {
    if (!mHidSharedMemory || !mHidSharedMemory->MappedAddress.has_value()) {
        return false;
    }
    const uint32_t base = *mHidSharedMemory->MappedAddress;
    const std::array<int16_t, 3> entry{
        QuantizeHidSensor(state.Accelerometer[0], 512.0F),
        QuantizeHidSensor(state.Accelerometer[1], 512.0F),
        QuantizeHidSensor(state.Accelerometer[2], 512.0F),
    };
    // CTR raw axes are wired differently from the calibrated ring entries.
    const std::array<int16_t, 3> raw{
        SaturatingSensorMultiply(entry[0], -2),
        SaturatingSensorMultiply(entry[2], -2),
        SaturatingSensorMultiply(entry[1], 2),
    };
    const uint32_t index = mNextHidAccelerometerIndex;
    const uint32_t address = base + kHidAccelerometerEntriesOffset +
                             index * kHidAccelerometerEntrySize;
    bool written = memory.WriteHost<uint32_t>(
                       base + kHidAccelerometerIndexOffset, index) &&
                   WriteHidSensorVector(
                       memory, base + kHidAccelerometerRawEntryOffset, raw) &&
                   WriteHidSensorVector(memory, address, entry);
    if (index == 0U) {
        uint64_t previousResetTick = 0;
        written =
            written &&
            memory.Read64(base + kHidAccelerometerIndexResetTicksOffset,
                          &previousResetTick, nullptr) &&
            memory.WriteHost<uint64_t>(
                base + kHidAccelerometerIndexResetTicksPreviousOffset,
                previousResetTick) &&
            memory.WriteHost<uint64_t>(
                base + kHidAccelerometerIndexResetTicksOffset, sampleTick);
    }
    if (!written) {
        return false;
    }
    mNextHidAccelerometerIndex = (index + 1U) % 8U;
    ++mHidRuntimeProfile.AccelerometerSamplesWritten;
    mHidRuntimeProfile.LastAccelerometer = entry;
    mHidRuntimeProfile.NextAccelerometerIndex = mNextHidAccelerometerIndex;
    return true;
}

bool NativeA32CtrHostServices::WriteHidGyroscopeSample(
    NativeA32Memory& memory, const NativeA32HidState& state,
    uint64_t sampleTick) {
    if (!mHidSharedMemory || !mHidSharedMemory->MappedAddress.has_value()) {
        return false;
    }
    const uint32_t base = *mHidSharedMemory->MappedAddress;
    const std::array<int16_t, 3> entry{
        QuantizeHidSensor(state.GyroscopeDegreesPerSecond[0],
                          mConfig.GyroscopeRawToDpsCoefficient),
        QuantizeHidSensor(state.GyroscopeDegreesPerSecond[1],
                          mConfig.GyroscopeRawToDpsCoefficient),
        QuantizeHidSensor(state.GyroscopeDegreesPerSecond[2],
                          mConfig.GyroscopeRawToDpsCoefficient),
    };
    const std::array<int16_t, 3> raw{
        entry[0],
        entry[2],
        SaturatingSensorMultiply(entry[1], -1),
    };
    const uint32_t index = mNextHidGyroscopeIndex;
    const uint32_t address =
        base + kHidGyroscopeEntriesOffset + index * kHidGyroscopeEntrySize;
    bool written =
        memory.WriteHost<uint32_t>(base + kHidGyroscopeIndexOffset, index) &&
        WriteHidSensorVector(memory, base + kHidGyroscopeRawEntryOffset, raw) &&
        WriteHidSensorVector(memory, address, entry);
    if (index == 0U) {
        uint64_t previousResetTick = 0;
        written = written &&
                  memory.Read64(base + kHidGyroscopeIndexResetTicksOffset,
                                &previousResetTick, nullptr) &&
                  memory.WriteHost<uint64_t>(
                      base + kHidGyroscopeIndexResetTicksPreviousOffset,
                      previousResetTick) &&
                  memory.WriteHost<uint64_t>(
                      base + kHidGyroscopeIndexResetTicksOffset, sampleTick);
    }
    if (!written) {
        return false;
    }
    mNextHidGyroscopeIndex = (index + 1U) % 32U;
    ++mHidRuntimeProfile.GyroscopeSamplesWritten;
    mHidRuntimeProfile.LastGyroscope = entry;
    mHidRuntimeProfile.NextGyroscopeIndex = mNextHidGyroscopeIndex;
    return true;
}

NativeA32CtrHidUpdateResult NativeA32CtrHostServices::AdvanceHidToCurrentTick(
    NativeA32Memory& memory, const NativeA32HidState& state) {
    mHidRuntimeProfile.SharedMemoryMapped =
        mHidSharedMemory && mHidSharedMemory->MappedAddress.has_value();
    if (!mHidRuntimeProfile.SharedMemoryMapped || !mHidEvents[0] ||
        !mHidEvents[1]) {
        return {};
    }
    if (!mHidScheduleInitialized) {
        mNextHidPadTick = mSystemTicks;
        mHidScheduleInitialized = true;
    }

    NativeA32CtrHidUpdateResult result;
    result.Status = NativeA32CtrHidUpdateStatus::Idle;
    while (mNextHidPadTick <= mSystemTicks) {
        if (!WriteHidSample(memory, state, mNextHidPadTick)) {
            result.Status = NativeA32CtrHidUpdateStatus::Failed;
            return result;
        }
        ++result.SamplesWritten;
        if (mNextHidPadTick >
            std::numeric_limits<uint64_t>::max() - HidPadUpdateTicks) {
            mNextHidPadTick = std::numeric_limits<uint64_t>::max();
            break;
        }
        mNextHidPadTick += HidPadUpdateTicks;
    }
    if (HidAccelerometerEnabled()) {
        if (!mHidAccelerometerScheduleInitialized) {
            mNextHidAccelerometerTick = mSystemTicks;
            mHidAccelerometerScheduleInitialized = true;
        }
        while (mNextHidAccelerometerTick <= mSystemTicks) {
            if (!WriteHidAccelerometerSample(memory, state,
                                             mNextHidAccelerometerTick)) {
                result.Status = NativeA32CtrHidUpdateStatus::Failed;
                return result;
            }
            ++result.AccelerometerSamplesWritten;
            if (mNextHidAccelerometerTick >
                std::numeric_limits<uint64_t>::max() -
                    HidAccelerometerUpdateTicks) {
                mNextHidAccelerometerTick =
                    std::numeric_limits<uint64_t>::max();
                break;
            }
            mNextHidAccelerometerTick += HidAccelerometerUpdateTicks;
        }
    } else {
        mHidAccelerometerScheduleInitialized = false;
    }
    if (HidGyroscopeEnabled()) {
        if (!mHidGyroscopeScheduleInitialized) {
            mNextHidGyroscopeTick = mSystemTicks;
            mHidGyroscopeScheduleInitialized = true;
        }
        while (mNextHidGyroscopeTick <= mSystemTicks) {
            if (!WriteHidGyroscopeSample(memory, state,
                                         mNextHidGyroscopeTick)) {
                result.Status = NativeA32CtrHidUpdateStatus::Failed;
                return result;
            }
            ++result.GyroscopeSamplesWritten;
            if (mNextHidGyroscopeTick > std::numeric_limits<uint64_t>::max() -
                                            HidGyroscopeUpdateTicks) {
                mNextHidGyroscopeTick = std::numeric_limits<uint64_t>::max();
                break;
            }
            mNextHidGyroscopeTick += HidGyroscopeUpdateTicks;
        }
    }
    const bool padUpdated = result.SamplesWritten != 0U;
    const bool accelerometerUpdated = result.AccelerometerSamplesWritten != 0U;
    const bool gyroscopeUpdated = result.GyroscopeSamplesWritten != 0U;
    if (!padUpdated && !accelerometerUpdated && !gyroscopeUpdated) {
        return result;
    }
    if (padUpdated) {
        mHidEvents[0]->AvailableCount = 1;
        mHidEvents[1]->AvailableCount = 1;
    }
    if (accelerometerUpdated && mHidEvents[2]) {
        mHidEvents[2]->AvailableCount = 1;
    }
    if (gyroscopeUpdated && mHidEvents[3]) {
        mHidEvents[3]->AvailableCount = 1;
    }
    WakeSynchronizationWaiters();
    ++mHidRuntimeProfile.EventBatchesSignaled;
    result.Status = NativeA32CtrHidUpdateStatus::Updated;
    result.EventsSignaled = true;
    return result;
}

NativeA32CtrHidRuntimeProfile
NativeA32CtrHostServices::HidRuntimeProfile() const noexcept {
    return mHidRuntimeProfile;
}

uint64_t NativeA32CtrHostServices::SystemTicks() const { return mSystemTicks; }

uint32_t NativeA32CtrHostServices::FallbackPc() const { return mFallbackPc; }

oot3d::recomp::a32::FallbackReason
NativeA32CtrHostServices::FallbackReason() const {
    return mFallbackReason;
}

uint32_t NativeA32CtrHostServices::FallbackRawOperation() const {
    return mFallbackRawOperation;
}

const std::vector<uint8_t>& NativeA32CtrHostServices::DspComponent() const {
    return mDspComponent;
}

uint16_t NativeA32CtrHostServices::DspProgramMask() const {
    return mDspProgramMask;
}

uint16_t NativeA32CtrHostServices::DspDataMask() const { return mDspDataMask; }

uint16_t NativeA32CtrHostServices::DspSemaphoreMask() const {
    return mDspSemaphoreMask;
}

uint16_t NativeA32CtrHostServices::DspSemaphoreValue() const {
    return mDspSemaphoreValue;
}

uint32_t NativeA32CtrHostServices::GspPriority() const { return mGspPriority; }

uint32_t NativeA32CtrHostServices::GspPriorityWithRights() const {
    return mGspPriorityWithRights;
}

bool NativeA32CtrHostServices::LcdForceBlack() const { return mLcdForceBlack; }

bool NativeA32CtrHostServices::HidAccelerometerEnabled() const {
    return mHidAccelerometerEnableCount != 0U;
}

bool NativeA32CtrHostServices::HidGyroscopeEnabled() const {
    return mHidGyroscopeEnableCount != 0U;
}

bool NativeA32CtrHostServices::NdmSchedulerSuspended() const {
    return mNdmSchedulerSuspended;
}

bool NativeA32CtrHostServices::NdmSchedulerRunsInBackground() const {
    return mNdmSchedulerRunsInBackground;
}

NativeA32CtrRuntimeProfile
NativeA32CtrHostServices::RuntimeProfile() const noexcept {
    return mRuntimeProfile;
}

nlohmann::json NativeA32CtrHostServices::CaptureState() const {
    std::unordered_map<const KernelObject*, uint32_t> objectIds;
    std::vector<std::shared_ptr<KernelObject>> objects;
    const auto registerObject =
        [&](const std::shared_ptr<KernelObject>& object) {
            if (!object) {
                return 0U;
            }
            const auto found = objectIds.find(object.get());
            if (found != objectIds.end()) {
                return found->second;
            }
            const uint32_t id = static_cast<uint32_t>(objects.size()) + 1U;
            objectIds.emplace(object.get(), id);
            objects.push_back(object);
            return id;
        };
    for (const auto& handle : mHandles) {
        registerObject(handle.Object);
    }
    for (const auto& waiter : mArbiterWaiters) {
        registerObject(waiter.Arbiter);
    }
    for (const auto& waiter : mSynchronizationWaiters) {
        for (const auto& object : waiter.Objects) {
            registerObject(object);
        }
    }
    for (const auto& object :
         {mAptLock, mAptNotificationEvent, mAptParameterEvent, mGpuRightOwner,
          mGspInterruptEvent, mGspSharedMemory, mDspSemaphoreEvent,
          mHidSharedMemory}) {
        registerObject(object);
    }
    for (const auto& type : mDspInterruptEvents) {
        for (const auto& object : type) {
            registerObject(object);
        }
    }
    for (const auto& object : mHidEvents) {
        registerObject(object);
    }
    const auto objectId = [&](const std::shared_ptr<KernelObject>& object) {
        return object ? objectIds.at(object.get()) : 0U;
    };

    nlohmann::json encodedObjects = nlohmann::json::array();
    for (size_t index = 0; index < objects.size(); ++index) {
        const auto& object = *objects[index];
        encodedObjects.push_back({
            {"id", index + 1U},
            {"type", object.Type},
            {"thread_id", object.ThreadId.has_value()
                              ? nlohmann::json(*object.ThreadId)
                              : nlohmann::json(nullptr)},
            {"owner_thread_id", object.OwnerThreadId.has_value()
                                    ? nlohmann::json(*object.OwnerThreadId)
                                    : nlohmann::json(nullptr)},
            {"recursion_count", object.RecursionCount},
            {"available_count", object.AvailableCount},
            {"reset_type", object.ResetType},
            {"file_path", object.FilePath.string()},
            {"file_offset", object.FileOffset},
            {"file_size", object.FileSize},
            {"file_open_mode", object.FileOpenMode},
            {"mapped_address", object.MappedAddress.has_value()
                                   ? nlohmann::json(*object.MappedAddress)
                                   : nlohmann::json(nullptr)},
        });
    }
    nlohmann::json handles = nlohmann::json::array();
    for (const auto& handle : mHandles) {
        handles.push_back(
            {{"value", handle.Value}, {"object", objectId(handle.Object)}});
    }
    nlohmann::json arbiterWaiters = nlohmann::json::array();
    for (const auto& waiter : mArbiterWaiters) {
        arbiterWaiters.push_back({
            {"arbiter", objectId(waiter.Arbiter)},
            {"thread_id", waiter.ThreadId},
            {"address", waiter.Address},
        });
    }
    nlohmann::json synchronizationWaiters = nlohmann::json::array();
    for (const auto& waiter : mSynchronizationWaiters) {
        nlohmann::json waiterObjects = nlohmann::json::array();
        for (const auto& object : waiter.Objects) {
            waiterObjects.push_back(objectId(object));
        }
        synchronizationWaiters.push_back({
            {"objects", std::move(waiterObjects)},
            {"thread_id", waiter.ThreadId},
            {"wait_all", waiter.WaitAll},
        });
    }
    nlohmann::json sleepWaiters = nlohmann::json::array();
    for (const auto& waiter : mSleepWaiters) {
        sleepWaiters.push_back({
            {"thread_id", waiter.ThreadId},
            {"wake_tick", waiter.WakeTick},
        });
    }
    nlohmann::json archives = nlohmann::json::array();
    for (const auto& archive : mArchives) {
        archives.push_back({
            {"handle", archive.Handle},
            {"id", archive.Id},
            {"root", archive.Root.string()},
        });
    }
    nlohmann::json screens = nlohmann::json::array();
    for (const auto& screen : mGspScreens) {
        nlohmann::json slots = nlohmann::json::array();
        for (const auto& slot : screen.Slots) {
            slots.push_back({
                {"address_left", slot.AddressLeft},
                {"address_right", slot.AddressRight},
                {"stride", slot.Stride},
                {"format", slot.Format},
                {"configured", slot.Configured},
            });
        }
        screens.push_back({{"slots", std::move(slots)},
                           {"shown_buffer", screen.ShownBuffer}});
    }
    nlohmann::json dspInterruptEvents = nlohmann::json::array();
    for (const auto& type : mDspInterruptEvents) {
        nlohmann::json channels = nlohmann::json::array();
        for (const auto& object : type) {
            channels.push_back(objectId(object));
        }
        dspInterruptEvents.push_back(std::move(channels));
    }
    nlohmann::json dspPipeOutput = nlohmann::json::array();
    for (const auto& pipe : mDspPipeOutput) {
        dspPipeOutput.push_back(nlohmann::json::binary(pipe));
    }
    nlohmann::json hidEvents = nlohmann::json::array();
    for (const auto& object : mHidEvents) {
        hidEvents.push_back(objectId(object));
    }

    return {
        {"format", "oot3d_native_a32_ctr_host_state_v1"},
        {"kernel_objects", std::move(encodedObjects)},
        {"handles", std::move(handles)},
        {"arbiter_waiters", std::move(arbiterWaiters)},
        {"synchronization_waiters", std::move(synchronizationWaiters)},
        {"sleep_waiters", std::move(sleepWaiters)},
        {"archives", std::move(archives)},
        {"next_slot", mNextSlot},
        {"next_generation", mNextGeneration},
        {"fallback_pc", mFallbackPc},
        {"fallback_reason", static_cast<uint32_t>(mFallbackReason)},
        {"fallback_raw_operation", mFallbackRawOperation},
        {"linear_heap_cursor", mLinearHeapCursor},
        {"allocation_index", mAllocationIndex},
        {"next_archive_handle", mNextArchiveHandle},
        {"system_ticks", mSystemTicks},
        {"apt_lock", objectId(mAptLock)},
        {"apt_notification_event", objectId(mAptNotificationEvent)},
        {"apt_parameter_event", objectId(mAptParameterEvent)},
        {"gpu_right_owner", objectId(mGpuRightOwner)},
        {"gsp_interrupt_event", objectId(mGspInterruptEvent)},
        {"gsp_shared_memory", objectId(mGspSharedMemory)},
        {"gsp_thread_id", mGspThreadId},
        {"gsp_priority", mGspPriority},
        {"gsp_priority_with_rights", mGspPriorityWithRights},
        {"gsp_screens", std::move(screens)},
        {"lcd_force_black", mLcdForceBlack},
        {"apt_application_id", mAptApplicationId},
        {"apt_wakeup_pending", mAptWakeupPending},
        {"dsp_component", nlohmann::json::binary(mDspComponent)},
        {"dsp_program_mask", mDspProgramMask},
        {"dsp_data_mask", mDspDataMask},
        {"dsp_semaphore_mask", mDspSemaphoreMask},
        {"dsp_semaphore_value", mDspSemaphoreValue},
        {"dsp_audio_running", mDspAudioRunning},
        {"next_dsp_audio_frame_tick", mNextDspAudioFrameTick},
        {"pending_dsp_audio_frames", mPendingDspAudioFrames},
        {"dsp_interrupt_events", std::move(dspInterruptEvents)},
        {"dsp_semaphore_event", objectId(mDspSemaphoreEvent)},
        {"dsp_pipe_output", std::move(dspPipeOutput)},
        {"hid_shared_memory", objectId(mHidSharedMemory)},
        {"hid_events", std::move(hidEvents)},
        {"hid_runtime_profile",
         {{"shared_memory_mapped", mHidRuntimeProfile.SharedMemoryMapped},
          {"samples_written", mHidRuntimeProfile.SamplesWritten},
          {"accelerometer_samples_written",
           mHidRuntimeProfile.AccelerometerSamplesWritten},
          {"gyroscope_samples_written",
           mHidRuntimeProfile.GyroscopeSamplesWritten},
          {"event_batches_signaled", mHidRuntimeProfile.EventBatchesSignaled},
          {"last_sample_tick", mHidRuntimeProfile.LastSampleTick},
          {"last_buttons", mHidRuntimeProfile.LastButtons},
          {"last_additions", mHidRuntimeProfile.LastAdditions},
          {"last_removals", mHidRuntimeProfile.LastRemovals},
          {"last_circle_pad_x", mHidRuntimeProfile.LastCirclePadX},
          {"last_circle_pad_y", mHidRuntimeProfile.LastCirclePadY},
          {"last_accelerometer", mHidRuntimeProfile.LastAccelerometer},
          {"last_gyroscope", mHidRuntimeProfile.LastGyroscope},
          {"next_pad_index", mHidRuntimeProfile.NextPadIndex},
          {"next_accelerometer_index",
           mHidRuntimeProfile.NextAccelerometerIndex},
          {"next_gyroscope_index", mHidRuntimeProfile.NextGyroscopeIndex}}},
        {"next_hid_pad_tick", mNextHidPadTick},
        {"next_hid_accelerometer_tick", mNextHidAccelerometerTick},
        {"next_hid_gyroscope_tick", mNextHidGyroscopeTick},
        {"next_hid_pad_index", mNextHidPadIndex},
        {"next_hid_touch_index", mNextHidTouchIndex},
        {"next_hid_accelerometer_index", mNextHidAccelerometerIndex},
        {"next_hid_gyroscope_index", mNextHidGyroscopeIndex},
        {"hid_schedule_initialized", mHidScheduleInitialized},
        {"hid_accelerometer_schedule_initialized",
         mHidAccelerometerScheduleInitialized},
        {"hid_gyroscope_schedule_initialized",
         mHidGyroscopeScheduleInitialized},
        {"hid_accelerometer_enable_count", mHidAccelerometerEnableCount},
        {"hid_gyroscope_enable_count", mHidGyroscopeEnableCount},
        {"ndm_scheduler_suspended", mNdmSchedulerSuspended},
        {"ndm_scheduler_runs_in_background", mNdmSchedulerRunsInBackground},
    };
}

bool NativeA32CtrHostServices::RestoreState(const nlohmann::json& state,
                                            NativeA32Process& process,
                                            std::string* error) {
    try {
        if (!state.is_object() || state.value("format", std::string{}) !=
                                      "oot3d_native_a32_ctr_host_state_v1") {
            if (error != nullptr) {
                *error = "native CTR host state format is invalid";
            }
            return false;
        }
        NativeA32CtrHostServices staged(mConfig);
        const auto normalizedAbsolute = [](const std::filesystem::path& path) {
            return std::filesystem::absolute(path).lexically_normal();
        };
        const auto pathIsWithin = [&](const std::filesystem::path& root,
                                      const std::filesystem::path& candidate) {
            if (root.empty() || candidate.empty()) {
                return false;
            }
            const auto normalizedRoot = normalizedAbsolute(root);
            const auto normalizedCandidate = normalizedAbsolute(candidate);
            if (normalizedRoot == normalizedCandidate) {
                return true;
            }
            const auto relative =
                normalizedCandidate.lexically_relative(normalizedRoot);
            return !relative.empty() && !relative.is_absolute() &&
                   *relative.begin() != "..";
        };
        const auto validFilePath = [&](const std::filesystem::path& path) {
            if (path.empty()) {
                return true;
            }
            return (!mConfig.RomFsImagePath.empty() &&
                    normalizedAbsolute(path) ==
                        normalizedAbsolute(mConfig.RomFsImagePath)) ||
                   pathIsWithin(mConfig.SaveDataDirectory, path);
        };
        std::vector<std::shared_ptr<KernelObject>> objects;
        const auto& encodedObjects = state.at("kernel_objects");
        objects.reserve(encodedObjects.size());
        for (size_t index = 0; index < encodedObjects.size(); ++index) {
            const auto& encoded = encodedObjects[index];
            if (encoded.at("id").get<size_t>() != index + 1U) {
                throw std::runtime_error("kernel object ID is invalid");
            }
            auto object = std::make_shared<KernelObject>();
            object->Type = encoded.at("type").get<std::string>();
            if (object->Type.empty()) {
                throw std::runtime_error("kernel object type is empty");
            }
            if (!encoded.at("thread_id").is_null()) {
                object->ThreadId = encoded.at("thread_id").get<uint32_t>();
            }
            if (!encoded.at("owner_thread_id").is_null()) {
                object->OwnerThreadId =
                    encoded.at("owner_thread_id").get<uint32_t>();
            }
            object->RecursionCount =
                encoded.at("recursion_count").get<uint32_t>();
            object->AvailableCount =
                encoded.at("available_count").get<int64_t>();
            object->ResetType = encoded.at("reset_type").get<uint32_t>();
            object->FilePath = std::filesystem::path(
                encoded.at("file_path").get<std::string>());
            const bool isRomFsFile =
                object->Type == "client_session:file:romfs";
            const bool isSaveFile =
                object->Type == "client_session:file:savedata";
            if (isRomFsFile) {
                if (mConfig.Filesystem == nullptr &&
                    mConfig.RomFsImagePath.empty()) {
                    throw std::runtime_error(
                        "RomFS kernel object has no configured image");
                }
                // Savestates may originate on another host OS. The serialized
                // RomFS path is an identity hint, not authority to open an
                // arbitrary host file; bind this object to the configured
                // process image before applying the normal path validation.
                object->FilePath =
                    mConfig.Filesystem != nullptr
                        ? std::filesystem::path(mConfig.RomFsContentPath)
                        : mConfig.RomFsImagePath;
            }
            if (mConfig.Filesystem == nullptr &&
                !validFilePath(object->FilePath)) {
                throw std::runtime_error(
                    "kernel object file path is incompatible");
            }
            object->FileOffset = encoded.at("file_offset").get<uint64_t>();
            object->FileSize = encoded.at("file_size").get<uint64_t>();
            object->FileOpenMode = encoded.at("file_open_mode").get<uint32_t>();
            if (mConfig.Filesystem != nullptr && (isRomFsFile || isSaveFile)) {
                uint32_t openFlags = 0U;
                if (isRomFsFile || (object->FileOpenMode & 1U) != 0U) {
                    openFlags |= NativeA32CtrFileOpenRead;
                }
                if (isSaveFile && (object->FileOpenMode & 2U) != 0U) {
                    openFlags |= NativeA32CtrFileOpenWrite;
                }
                if (!mConfig.Filesystem->Open(
                        isRomFsFile ? NativeA32CtrFilesystemRoot::Content
                                    : NativeA32CtrFilesystemRoot::Save,
                        object->FilePath.generic_string(), openFlags,
                        &object->FileBackend) ||
                    object->FileBackend == nullptr) {
                    throw std::runtime_error(
                        "kernel object backing file could not be restored");
                }
                if (isRomFsFile &&
                    (object->FileOffset > object->FileBackend->Size() ||
                     object->FileSize >
                         object->FileBackend->Size() - object->FileOffset)) {
                    throw std::runtime_error(
                        "restored RomFS range exceeds its backing file");
                }
            } else if (isRomFsFile) {
                object->RomFsBacking =
                    std::make_shared<RomFsBackingFile>(object->FilePath);
            }
            if (!encoded.at("mapped_address").is_null()) {
                object->MappedAddress =
                    encoded.at("mapped_address").get<uint32_t>();
            }
            objects.push_back(std::move(object));
        }
        const auto object = [&](uint32_t id) -> std::shared_ptr<KernelObject> {
            if (id == 0U) {
                return {};
            }
            if (id > objects.size()) {
                throw std::runtime_error("kernel object reference is invalid");
            }
            return objects[id - 1U];
        };
        const auto validThread = [&](uint32_t threadId) {
            return process.ThreadState(threadId) != nullptr;
        };
        for (const auto& encoded : state.at("handles")) {
            auto referenced = object(encoded.at("object").get<uint32_t>());
            if (!referenced) {
                throw std::runtime_error("handle has no kernel object");
            }
            staged.mHandles.push_back(
                {encoded.at("value").get<uint32_t>(), std::move(referenced)});
        }
        for (const auto& encoded : state.at("arbiter_waiters")) {
            const uint32_t threadId = encoded.at("thread_id").get<uint32_t>();
            if (!validThread(threadId)) {
                throw std::runtime_error("arbiter waiter thread is invalid");
            }
            staged.mArbiterWaiters.push_back(
                {object(encoded.at("arbiter").get<uint32_t>()), &process,
                 threadId, encoded.at("address").get<uint32_t>()});
        }
        for (const auto& encoded : state.at("synchronization_waiters")) {
            SynchronizationWaiter waiter;
            waiter.Process = &process;
            waiter.ThreadId = encoded.at("thread_id").get<uint32_t>();
            waiter.WaitAll = encoded.at("wait_all").get<bool>();
            if (!validThread(waiter.ThreadId)) {
                throw std::runtime_error(
                    "synchronization waiter thread is invalid");
            }
            for (const auto& id : encoded.at("objects")) {
                auto referenced = object(id.get<uint32_t>());
                if (!referenced) {
                    throw std::runtime_error(
                        "synchronization waiter object is invalid");
                }
                waiter.Objects.push_back(std::move(referenced));
            }
            staged.mSynchronizationWaiters.push_back(std::move(waiter));
        }
        for (const auto& encoded : state.at("sleep_waiters")) {
            const uint32_t threadId = encoded.at("thread_id").get<uint32_t>();
            if (!validThread(threadId)) {
                throw std::runtime_error("sleep waiter thread is invalid");
            }
            staged.mSleepWaiters.push_back(
                {&process, threadId, encoded.at("wake_tick").get<uint64_t>()});
        }
        for (const auto& encoded : state.at("archives")) {
            ArchiveRecord archive{
                encoded.at("handle").get<uint64_t>(),
                encoded.at("id").get<uint32_t>(),
                std::filesystem::path(encoded.at("root").get<std::string>())};
            const auto expectedRoot = mConfig.Filesystem != nullptr
                                          ? std::filesystem::path("save")
                                          : mConfig.SaveDataDirectory;
            if (archive.Root.lexically_normal() !=
                expectedRoot.lexically_normal()) {
                throw std::runtime_error("archive root is incompatible");
            }
            staged.mArchives.push_back(std::move(archive));
        }
        staged.mNextSlot = state.at("next_slot").get<uint16_t>();
        staged.mNextGeneration = state.at("next_generation").get<uint16_t>();
        if (staged.mNextGeneration == 0U ||
            staged.mNextGeneration >= (1U << 15U)) {
            throw std::runtime_error("handle generation is invalid");
        }
        staged.mFallbackPc = state.at("fallback_pc").get<uint32_t>();
        const uint32_t fallbackReason =
            state.at("fallback_reason").get<uint32_t>();
        if (fallbackReason >
            static_cast<uint32_t>(
                oot3d::recomp::a32::FallbackReason::MissingBlock)) {
            throw std::runtime_error("fallback reason is invalid");
        }
        staged.mFallbackReason =
            static_cast<oot3d::recomp::a32::FallbackReason>(fallbackReason);
        staged.mFallbackRawOperation =
            state.at("fallback_raw_operation").get<uint32_t>();
        staged.mLinearHeapCursor =
            state.at("linear_heap_cursor").get<uint32_t>();
        staged.mAllocationIndex = state.at("allocation_index").get<uint32_t>();
        staged.mNextArchiveHandle =
            state.at("next_archive_handle").get<uint64_t>();
        staged.mSystemTicks = state.at("system_ticks").get<uint64_t>();
        staged.mAptLock = object(state.at("apt_lock").get<uint32_t>());
        staged.mAptNotificationEvent =
            object(state.at("apt_notification_event").get<uint32_t>());
        staged.mAptParameterEvent =
            object(state.at("apt_parameter_event").get<uint32_t>());
        staged.mGpuRightOwner =
            object(state.at("gpu_right_owner").get<uint32_t>());
        staged.mGspInterruptEvent =
            object(state.at("gsp_interrupt_event").get<uint32_t>());
        staged.mGspSharedMemory =
            object(state.at("gsp_shared_memory").get<uint32_t>());
        staged.mGspThreadId = state.at("gsp_thread_id").get<uint32_t>();
        staged.mGspPriority = state.at("gsp_priority").get<uint32_t>();
        staged.mGspPriorityWithRights =
            state.at("gsp_priority_with_rights").get<uint32_t>();
        const auto& screens = state.at("gsp_screens");
        if (screens.size() != staged.mGspScreens.size()) {
            throw std::runtime_error("GSP screen count is invalid");
        }
        for (size_t screenIndex = 0; screenIndex < staged.mGspScreens.size();
             ++screenIndex) {
            const auto& encodedScreen = screens[screenIndex];
            const auto& slots = encodedScreen.at("slots");
            if (slots.size() != staged.mGspScreens[screenIndex].Slots.size()) {
                throw std::runtime_error(
                    "GSP framebuffer slot count is invalid");
            }
            for (size_t slotIndex = 0;
                 slotIndex < staged.mGspScreens[screenIndex].Slots.size();
                 ++slotIndex) {
                const auto& encodedSlot = slots[slotIndex];
                auto& slot = staged.mGspScreens[screenIndex].Slots[slotIndex];
                slot.AddressLeft =
                    encodedSlot.at("address_left").get<uint32_t>();
                slot.AddressRight =
                    encodedSlot.at("address_right").get<uint32_t>();
                slot.Stride = encodedSlot.at("stride").get<uint32_t>();
                slot.Format = encodedSlot.at("format").get<uint32_t>();
                slot.Configured = encodedSlot.at("configured").get<bool>();
            }
            staged.mGspScreens[screenIndex].ShownBuffer =
                encodedScreen.at("shown_buffer").get<uint32_t>();
            if (staged.mGspScreens[screenIndex].ShownBuffer >=
                staged.mGspScreens[screenIndex].Slots.size()) {
                throw std::runtime_error("GSP shown framebuffer is invalid");
            }
        }
        staged.mLcdForceBlack = state.at("lcd_force_black").get<bool>();
        staged.mAptApplicationId =
            state.at("apt_application_id").get<uint32_t>();
        staged.mAptWakeupPending = state.at("apt_wakeup_pending").get<bool>();
        const auto& dspComponent = state.at("dsp_component");
        if (!dspComponent.is_binary()) {
            throw std::runtime_error("DSP component state is invalid");
        }
        staged.mDspComponent.assign(dspComponent.get_binary().begin(),
                                    dspComponent.get_binary().end());
        staged.mDspProgramMask = state.at("dsp_program_mask").get<uint16_t>();
        staged.mDspDataMask = state.at("dsp_data_mask").get<uint16_t>();
        staged.mDspSemaphoreMask =
            state.at("dsp_semaphore_mask").get<uint16_t>();
        staged.mDspSemaphoreValue =
            state.at("dsp_semaphore_value").get<uint16_t>();
        staged.mDspAudioRunning = state.at("dsp_audio_running").get<bool>();
        staged.mNextDspAudioFrameTick =
            state.at("next_dsp_audio_frame_tick").get<uint64_t>();
        staged.mPendingDspAudioFrames =
            state.at("pending_dsp_audio_frames").get<uint32_t>();
        const auto& dspInterrupts = state.at("dsp_interrupt_events");
        if (dspInterrupts.size() != staged.mDspInterruptEvents.size()) {
            throw std::runtime_error("DSP interrupt type count is invalid");
        }
        for (size_t type = 0; type < staged.mDspInterruptEvents.size();
             ++type) {
            if (dspInterrupts[type].size() !=
                staged.mDspInterruptEvents[type].size()) {
                throw std::runtime_error(
                    "DSP interrupt channel count is invalid");
            }
            for (size_t channel = 0;
                 channel < staged.mDspInterruptEvents[type].size(); ++channel) {
                staged.mDspInterruptEvents[type][channel] =
                    object(dspInterrupts[type][channel].get<uint32_t>());
            }
        }
        staged.mDspSemaphoreEvent =
            object(state.at("dsp_semaphore_event").get<uint32_t>());
        const auto& pipes = state.at("dsp_pipe_output");
        if (pipes.size() != staged.mDspPipeOutput.size()) {
            throw std::runtime_error("DSP pipe count is invalid");
        }
        for (size_t index = 0; index < staged.mDspPipeOutput.size(); ++index) {
            if (!pipes[index].is_binary()) {
                throw std::runtime_error("DSP pipe state is invalid");
            }
            staged.mDspPipeOutput[index].assign(
                pipes[index].get_binary().begin(),
                pipes[index].get_binary().end());
        }
        staged.mHidSharedMemory =
            object(state.at("hid_shared_memory").get<uint32_t>());
        const auto& hidEvents = state.at("hid_events");
        if (hidEvents.size() != staged.mHidEvents.size()) {
            throw std::runtime_error("HID event count is invalid");
        }
        for (size_t index = 0; index < staged.mHidEvents.size(); ++index) {
            staged.mHidEvents[index] = object(hidEvents[index].get<uint32_t>());
        }
        const auto& hid = state.at("hid_runtime_profile");
        staged.mHidRuntimeProfile.SharedMemoryMapped =
            hid.at("shared_memory_mapped").get<bool>();
        staged.mHidRuntimeProfile.SamplesWritten =
            hid.at("samples_written").get<uint64_t>();
        staged.mHidRuntimeProfile.AccelerometerSamplesWritten =
            hid.value("accelerometer_samples_written", 0ULL);
        staged.mHidRuntimeProfile.GyroscopeSamplesWritten =
            hid.value("gyroscope_samples_written", 0ULL);
        staged.mHidRuntimeProfile.EventBatchesSignaled =
            hid.at("event_batches_signaled").get<uint64_t>();
        staged.mHidRuntimeProfile.LastSampleTick =
            hid.at("last_sample_tick").get<uint64_t>();
        staged.mHidRuntimeProfile.LastButtons =
            hid.at("last_buttons").get<uint32_t>();
        staged.mHidRuntimeProfile.LastAdditions =
            hid.at("last_additions").get<uint32_t>();
        staged.mHidRuntimeProfile.LastRemovals =
            hid.at("last_removals").get<uint32_t>();
        staged.mHidRuntimeProfile.LastCirclePadX =
            hid.at("last_circle_pad_x").get<int16_t>();
        staged.mHidRuntimeProfile.LastCirclePadY =
            hid.at("last_circle_pad_y").get<int16_t>();
        if (hid.contains("last_accelerometer")) {
            staged.mHidRuntimeProfile.LastAccelerometer =
                hid.at("last_accelerometer").get<std::array<int16_t, 3>>();
        }
        if (hid.contains("last_gyroscope")) {
            staged.mHidRuntimeProfile.LastGyroscope =
                hid.at("last_gyroscope").get<std::array<int16_t, 3>>();
        }
        staged.mHidRuntimeProfile.NextPadIndex =
            hid.at("next_pad_index").get<uint32_t>();
        staged.mHidRuntimeProfile.NextAccelerometerIndex =
            hid.value("next_accelerometer_index", 0U);
        staged.mHidRuntimeProfile.NextGyroscopeIndex =
            hid.value("next_gyroscope_index", 0U);
        staged.mNextHidPadTick = state.at("next_hid_pad_tick").get<uint64_t>();
        staged.mNextHidAccelerometerTick =
            state.value("next_hid_accelerometer_tick", 0ULL);
        staged.mNextHidGyroscopeTick =
            state.value("next_hid_gyroscope_tick", 0ULL);
        staged.mNextHidPadIndex =
            state.at("next_hid_pad_index").get<uint32_t>();
        staged.mNextHidTouchIndex =
            state.at("next_hid_touch_index").get<uint32_t>();
        staged.mNextHidAccelerometerIndex =
            state.value("next_hid_accelerometer_index", 0U);
        staged.mNextHidGyroscopeIndex =
            state.value("next_hid_gyroscope_index", 0U);
        staged.mHidScheduleInitialized =
            state.at("hid_schedule_initialized").get<bool>();
        staged.mHidAccelerometerScheduleInitialized =
            state.value("hid_accelerometer_schedule_initialized", false);
        staged.mHidGyroscopeScheduleInitialized =
            state.value("hid_gyroscope_schedule_initialized", false);
        staged.mHidAccelerometerEnableCount =
            state.at("hid_accelerometer_enable_count").get<uint32_t>();
        staged.mHidGyroscopeEnableCount =
            state.at("hid_gyroscope_enable_count").get<uint32_t>();
        staged.mNdmSchedulerSuspended =
            state.at("ndm_scheduler_suspended").get<bool>();
        staged.mNdmSchedulerRunsInBackground =
            state.at("ndm_scheduler_runs_in_background").get<bool>();

        *this = std::move(staged);
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("native CTR host state decode failed: ") +
                     exception.what();
        }
        return false;
    }
}

bool NativeA32CtrHostServices::SignalDspInterrupt(uint32_t type,
                                                  uint32_t channel) {
    if (type >= mDspInterruptEvents.size() ||
        channel >= mDspInterruptEvents[type].size()) {
        return false;
    }
    const auto& event = mDspInterruptEvents[type][channel];
    if (!event) {
        return false;
    }
    event->AvailableCount = 1;
    WakeSynchronizationWaiters();
    return true;
}

bool NativeA32CtrHostServices::SignalVBlank(NativeA32Memory& memory) {
    return SignalPicaInterrupt(memory, Oot3dPicaInterruptId::Pdc0) &&
           SignalPicaInterrupt(memory, Oot3dPicaInterruptId::Pdc1);
}

std::optional<NativeA32CtrFramebufferState>
NativeA32CtrHostServices::Framebuffer(uint32_t screenId) const {
    if (screenId >= mGspScreens.size()) {
        return std::nullopt;
    }
    const auto& screen = mGspScreens[screenId];
    if (screen.ShownBuffer >= screen.Slots.size()) {
        return std::nullopt;
    }
    const auto& slot = screen.Slots[screen.ShownBuffer];
    if (!slot.Configured) {
        return std::nullopt;
    }
    return NativeA32CtrFramebufferState{slot.AddressLeft, slot.AddressRight,
                                        slot.Stride, slot.Format,
                                        screen.ShownBuffer};
}

std::optional<NativeA32CtrFramebufferState>
NativeA32CtrHostServices::TopFramebuffer() const {
    return Framebuffer(0U);
}

std::optional<NativeA32CtrFramebufferState>
NativeA32CtrHostServices::BottomFramebuffer() const {
    return Framebuffer(1U);
}

bool NativeA32CtrHostServices::SignalPicaInterrupt(
    NativeA32Memory& memory, Oot3dPicaInterruptId interrupt) {
    if (interrupt > Oot3dPicaInterruptId::Dma) {
        return false;
    }
    if (interrupt == Oot3dPicaInterruptId::Pdc0 &&
        !ApplyGspFramebufferUpdate(memory, 0U)) {
        return false;
    }
    if (interrupt == Oot3dPicaInterruptId::Pdc1 &&
        !ApplyGspFramebufferUpdate(memory, 1U)) {
        return false;
    }
    return QueueGspInterrupt(memory, static_cast<uint8_t>(interrupt));
}

} // namespace Oot3dNativeGame
