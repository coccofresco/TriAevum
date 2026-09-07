#include "oot3d_ctr_hid_service.h"

#include <cstring>
#include <utility>

namespace Oot3dSourceRuntime {
namespace {
constexpr std::uint32_t kGetIpcHandlesRequest = 0x000A0000U;
constexpr std::uint32_t kGetIpcHandlesResponse = 0x000A0047U;
constexpr std::uint32_t kSixCopyHandlesDescriptor = 0x14000000U;
constexpr std::uint32_t kEnableAccelerometerRequest = 0x00110000U;
constexpr std::uint32_t kEnableAccelerometerResponse = 0x00110040U;
constexpr std::uint32_t kDisableAccelerometerRequest = 0x00120000U;
constexpr std::uint32_t kDisableAccelerometerResponse = 0x00120040U;
constexpr std::uint32_t kEnableGyroscopeRequest = 0x00130000U;
constexpr std::uint32_t kEnableGyroscopeResponse = 0x00130040U;
constexpr std::uint32_t kDisableGyroscopeRequest = 0x00140000U;
constexpr std::uint32_t kDisableGyroscopeResponse = 0x00140040U;
constexpr std::uint32_t kGetGyroscopeCoefficientRequest = 0x00150000U;
constexpr std::uint32_t kGetGyroscopeCoefficientResponse = 0x00150080U;
constexpr std::uint32_t kGetGyroscopeCalibrationRequest = 0x00160000U;
constexpr std::uint32_t kGetGyroscopeCalibrationResponse = 0x00160180U;
}

CtrHidService::CtrHidService(CtrIpcRouter& router, CtrHidProfile profile)
    : mRouter(router), mProfile(std::move(profile)) {}

void CtrHidService::EnsureObjects() {
    if (mSharedMemory) return;
    mSharedMemory = std::make_shared<CtrKernelObject>();
    mSharedMemory->Kind = CtrKernelObjectKind::SharedMemory;
    mSharedMemory->Name = "hid:shared-memory";
    mSharedMemory->SharedMemory.resize(0x1000);
    constexpr std::array names{
        "hid:pad-touch-1", "hid:pad-touch-2", "hid:accelerometer",
        "hid:gyroscope", "hid:debug-pad"};
    for (std::size_t index = 0; index < mEvents.size(); ++index) {
        mEvents[index] = std::make_shared<CtrKernelObject>(
            CtrKernelObjectKind::Event, names[index], 0, 1);
    }
}

CtrResult CtrHidService::Dispatch(std::span<std::uint32_t> commandBuffer) {
    if (commandBuffer.empty()) return CtrIpcRouter::UnhandledResult;
    if (commandBuffer[0] == kGetIpcHandlesRequest && commandBuffer.size() >= 9) {
        EnsureObjects();
        std::array<CtrHandle, 6> handles{};
        if (mRouter.OpenKernelObject(mSharedMemory, handles[0]) < 0)
            return CtrIpcRouter::UnhandledResult;
        for (std::size_t index = 0; index < mEvents.size(); ++index) {
            if (mRouter.OpenKernelObject(mEvents[index], handles[index + 1]) < 0) {
                for (const CtrHandle handle : handles)
                    if (handle != 0) mRouter.CloseHandle(handle);
                return CtrIpcRouter::UnhandledResult;
            }
        }
        commandBuffer[0] = kGetIpcHandlesResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = kSixCopyHandlesDescriptor;
        for (std::size_t index = 0; index < handles.size(); ++index)
            commandBuffer[index + 3] = handles[index];
        return 0;
    }

    std::uint32_t* count = nullptr;
    std::uint32_t response = 0;
    bool enable = false;
    switch (commandBuffer[0]) {
    case kEnableAccelerometerRequest:
        count = &mAccelerometerEnableCount;
        response = kEnableAccelerometerResponse;
        enable = true;
        break;
    case kDisableAccelerometerRequest:
        count = &mAccelerometerEnableCount;
        response = kDisableAccelerometerResponse;
        break;
    case kEnableGyroscopeRequest:
        count = &mGyroscopeEnableCount;
        response = kEnableGyroscopeResponse;
        enable = true;
        break;
    case kDisableGyroscopeRequest:
        count = &mGyroscopeEnableCount;
        response = kDisableGyroscopeResponse;
        break;
    default:
        break;
    }
    if (count != nullptr && commandBuffer.size() >= 2) {
        if (!enable && *count == 0) return CtrIpcRouter::UnhandledResult;
        enable ? ++*count : --*count;
        commandBuffer[0] = response;
        commandBuffer[1] = 0;
        return 0;
    }
    if (commandBuffer[0] == kGetGyroscopeCoefficientRequest &&
        commandBuffer.size() >= 3) {
        commandBuffer[0] = kGetGyroscopeCoefficientResponse;
        commandBuffer[1] = 0;
        std::memcpy(&commandBuffer[2], &mProfile.GyroscopeRawToDpsCoefficient,
                    sizeof(float));
        return 0;
    }
    if (commandBuffer[0] == kGetGyroscopeCalibrationRequest &&
        commandBuffer.size() >= 7) {
        commandBuffer[0] = kGetGyroscopeCalibrationResponse;
        commandBuffer[1] = 0;
        std::memset(&commandBuffer[2], 0, 5 * sizeof(std::uint32_t));
        std::memcpy(&commandBuffer[2], mProfile.GyroscopeCalibration.data(),
                    sizeof(mProfile.GyroscopeCalibration));
        return 0;
    }
    return CtrIpcRouter::UnhandledResult;
}

std::shared_ptr<CtrKernelObject> CtrHidService::SharedMemory() const {
    return mSharedMemory;
}

std::span<std::byte> CtrHidService::SharedMemoryBytes() {
    EnsureObjects();
    if (mSharedMemory->MappedMemory != nullptr) {
        return mSharedMemory->MappedMemory->ResolveWrite(
            mSharedMemory->MappedAddress, mSharedMemory->SharedMemory.size());
    }
    return mSharedMemory->SharedMemory;
}

bool CtrHidService::AccelerometerEnabled() const {
    return mAccelerometerEnableCount != 0;
}

bool CtrHidService::GyroscopeEnabled() const {
    return mGyroscopeEnableCount != 0;
}

void CtrHidService::SignalPadEvents() {
    EnsureObjects();
    mEvents[0]->AvailableCount = 1;
    mEvents[1]->AvailableCount = 1;
}

} // namespace Oot3dSourceRuntime
