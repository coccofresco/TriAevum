#pragma once

// Private to the oot3d_native_a32_ctr_host*.cpp units: their includes, the service constants and
// helpers they share (file-local to each unit) and the RomFS backing file.
#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_cpu_phase_probe.h"

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

// Same text as `std::hex << std::setw(8) << std::setfill('0') << value`,
// without constructing a stream: the event names of frequent SVCs are built
// on every call.
void AppendHex8(std::string& out, uint32_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        out.push_back(kDigits[(value >> shift) & 0xFU]);
    }
}

constexpr uint32_t kResultSuccess = 0;
// The kernel's result for a synchronization wait that timed out.
constexpr uint32_t kResultTimeout = 0x09401BFEU;
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
// GlanceParameter reads the pending parameter without consuming it.
constexpr uint32_t kAptGlanceParameterRequest = 0x000E0080U;
constexpr uint32_t kAptGlanceParameterResponse = 0x000E0104U;
// Whether the application lets the HOME Menu post its screenshots.
constexpr uint32_t kAptSetScreenCapPostPermissionRequest = 0x00550040U;
constexpr uint32_t kAptSetScreenCapPostPermissionResponse = 0x00550040U;
constexpr uint32_t kAptGetScreenCapPostPermissionRequest = 0x00560000U;
constexpr uint32_t kAptGetScreenCapPostPermissionResponse = 0x00560080U;
constexpr uint32_t kAptAppletUtilityRequest = 0x004B00C2U;
constexpr uint32_t kAptAppletUtilityResponse = 0x004B0082U;
constexpr uint32_t kAptUtilitySleepIfShellClosed = 4U;
constexpr uint32_t kAptUtilityUnlockTransition = 7U;
constexpr uint32_t kFsInitializeRequest = 0x08010002U;
constexpr uint32_t kFsInitializeResponse = 0x08010040U;
// Later SDKs pass their version before the calling-PID descriptor.
constexpr uint32_t kFsInitializeWithSdkVersionRequest = 0x08610042U;
constexpr uint32_t kFsInitializeWithSdkVersionResponse = 0x08610040U;
constexpr uint32_t kFsSetPriorityRequest = 0x08620040U;
constexpr uint32_t kFsSetPriorityResponse = 0x08620040U;
constexpr uint32_t kFsGetPriorityRequest = 0x08630000U;
constexpr uint32_t kFsGetPriorityResponse = 0x08630080U;
constexpr uint32_t kFsOpenFileRequest = 0x080201C2U;
constexpr uint32_t kFsOpenFileResponse = 0x08020042U;
constexpr uint32_t kFsOpenFileDirectlyRequest = 0x08030204U;
constexpr uint32_t kFsOpenFileDirectlyResponse = 0x08030042U;
constexpr uint32_t kFsCreateFileRequest = 0x08080202U;
constexpr uint32_t kFsDeleteFileRequest = 0x08040142U;
constexpr uint32_t kFsDeleteFileResponse = 0x08040040U;
constexpr uint32_t kFsInvalidPath = 0xE0E046BEU;
constexpr uint32_t kFsCommandNotAllowed = 0xD9004676U;
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
constexpr uint32_t kFileSetPriorityRequest = 0x080A0040U;
constexpr uint32_t kFileSetPriorityResponse = 0x080A0040U;
constexpr uint32_t kFileGetPriorityRequest = 0x080B0000U;
constexpr uint32_t kFileGetPriorityResponse = 0x080B0080U;
constexpr uint32_t kFileOpenLinkFileRequest = 0x080C0000U;
constexpr uint32_t kFileOpenLinkFileResponse = 0x080C0042U;
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
// Cache maintenance on buffers the DSP reads or writes; host memory is
// coherent with the emulated DSP.
constexpr uint32_t kDspFlushDataCacheRequest = 0x00130082U;
constexpr uint32_t kDspFlushDataCacheResponse = 0x00130040U;
constexpr uint32_t kDspInvalidateDataCacheRequest = 0x00140082U;
constexpr uint32_t kDspInvalidateDataCacheResponse = 0x00140040U;
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
// CTR/libctru gyro section begins after all eight 6-byte accel records.
// Starting four bytes early aliases the final accel sample and shifts every
// gyro field from the native consumer, even though host calibration looks OK.
constexpr uint32_t kHidGyroscopeIndexResetTicksOffset =
    kHidAccelerometerEntriesOffset + 8U * kHidAccelerometerEntrySize;
constexpr uint32_t kHidGyroscopeIndexResetTicksPreviousOffset = kHidGyroscopeIndexResetTicksOffset + 8U;
constexpr uint32_t kHidGyroscopeIndexOffset = kHidGyroscopeIndexResetTicksOffset + 0x10U;
constexpr uint32_t kHidGyroscopeRawEntryOffset = kHidGyroscopeIndexResetTicksOffset + 0x18U;
constexpr uint32_t kHidGyroscopeEntriesOffset = kHidGyroscopeIndexResetTicksOffset + 0x20U;
constexpr uint32_t kHidGyroscopeEntrySize = 0x06U;
static_assert(kHidGyroscopeIndexResetTicksOffset == 0x158U);
static_assert(kHidGyroscopeEntriesOffset == 0x178U);
constexpr uint32_t kHidCircleRight = 1U << 28U;
constexpr uint32_t kHidCircleLeft = 1U << 29U;
constexpr uint32_t kHidCircleUp = 1U << 30U;
constexpr uint32_t kHidCircleDown = 1U << 31U;
constexpr uint32_t kNdmSuspendSchedulerRequest = 0x00080040U;
constexpr uint32_t kNdmSuspendSchedulerResponse = 0x00080040U;
constexpr uint32_t kNdmResumeSchedulerRequest = 0x00090000U;
constexpr uint32_t kNdmResumeSchedulerResponse = 0x00090040U;
// The background daemons NDM keeps running: CEC, BOSS, NIM and friends.
constexpr uint32_t kNdmAllDaemons = 0xFU;
constexpr uint32_t kNdmSuspendDaemonsRequest = 0x00060040U;
constexpr uint32_t kNdmSuspendDaemonsResponse = 0x00060040U;
constexpr uint32_t kNdmResumeDaemonsRequest = 0x00070040U;
constexpr uint32_t kNdmResumeDaemonsResponse = 0x00070040U;
constexpr uint32_t kNdmOverrideDefaultDaemonsRequest = 0x00140040U;
constexpr uint32_t kNdmOverrideDefaultDaemonsResponse = 0x00140040U;
constexpr uint32_t kNdmResetDefaultDaemonsRequest = 0x00150000U;
constexpr uint32_t kNdmResetDefaultDaemonsResponse = 0x00150040U;
constexpr uint32_t kNdmGetDefaultDaemonsRequest = 0x00160000U;
constexpr uint32_t kNdmGetDefaultDaemonsResponse = 0x00160080U;

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

} // namespace Oot3dNativeGame
