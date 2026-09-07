#include "oot3d_ctr_apt_service.h"

#include <algorithm>
#include <cstring>

namespace Oot3dSourceRuntime {
namespace {
constexpr std::uint32_t kGetLockHandleRequest = 0x00010040U;
constexpr std::uint32_t kGetLockHandleResponse = 0x000100C2U;
constexpr std::uint32_t kInitializeRequest = 0x00020080U;
constexpr std::uint32_t kInitializeResponse = 0x00020043U;
constexpr std::uint32_t kEnableRequest = 0x00030040U;
constexpr std::uint32_t kEnableResponse = 0x00030040U;
constexpr std::uint32_t kNotifyToWaitRequest = 0x00430040U;
constexpr std::uint32_t kNotifyToWaitResponse = 0x00430040U;
constexpr std::uint32_t kReceiveParameterRequest = 0x000D0080U;
constexpr std::uint32_t kReceiveParameterResponse = 0x000D0104U;
constexpr std::uint32_t kAppletUtilityRequest = 0x004B00C2U;
constexpr std::uint32_t kAppletUtilityResponse = 0x004B0082U;
constexpr std::uint32_t kCopyHandleDescriptor = 0U;
constexpr std::uint32_t kTwoCopyHandlesDescriptor = 0x04000000U;
}

CtrAptService::CtrAptService(GuestAddressSpace& memory, CtrIpcRouter& router,
                             CtrAptProfile profile)
    : mMemory(memory), mRouter(router), mProfile(profile) {}

CtrResult CtrAptService::Dispatch(std::span<std::uint32_t> commandBuffer) {
    if (commandBuffer.empty()) return CtrIpcRouter::UnhandledResult;
    if (commandBuffer[0] == kGetLockHandleRequest && commandBuffer.size() >= 6) {
        if (!mLock) {
            mLock = std::make_shared<CtrKernelObject>(
                CtrKernelObjectKind::Mutex, "apt:lock", 1, 1);
        }
        CtrHandle handle = 0;
        if (mRouter.OpenKernelObject(mLock, handle) < 0)
            return CtrIpcRouter::UnhandledResult;
        const std::uint32_t attributes = commandBuffer[1];
        commandBuffer[0] = kGetLockHandleResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = attributes;
        commandBuffer[3] = 0;
        commandBuffer[4] = kCopyHandleDescriptor;
        commandBuffer[5] = handle;
        return 0;
    }
    if (commandBuffer[0] == kInitializeRequest && commandBuffer.size() >= 5) {
        if (!mNotificationEvent) {
            mNotificationEvent = std::make_shared<CtrKernelObject>(
                CtrKernelObjectKind::Event, "apt:notification", 0, 1);
            mParameterEvent = std::make_shared<CtrKernelObject>(
                CtrKernelObjectKind::Event, "apt:parameter", 1, 1);
        }
        CtrHandle notification = 0;
        CtrHandle parameter = 0;
        if (mRouter.OpenKernelObject(mNotificationEvent, notification) < 0 ||
            mRouter.OpenKernelObject(mParameterEvent, parameter) < 0) {
            if (notification != 0) mRouter.CloseHandle(notification);
            return CtrIpcRouter::UnhandledResult;
        }
        mApplicationId = commandBuffer[1];
        mWakeupPending = true;
        commandBuffer[0] = kInitializeResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = kTwoCopyHandlesDescriptor;
        commandBuffer[3] = notification;
        commandBuffer[4] = parameter;
        return 0;
    }
    if (commandBuffer[0] == kEnableRequest && commandBuffer.size() >= 2) {
        commandBuffer[0] = kEnableResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (commandBuffer[0] == kNotifyToWaitRequest && commandBuffer.size() >= 2) {
        commandBuffer[0] = kNotifyToWaitResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (commandBuffer[0] == kReceiveParameterRequest &&
        commandBuffer.size() >= 9 && mWakeupPending &&
        commandBuffer[1] == mApplicationId && commandBuffer[2] <= 0x1000U) {
        const auto table = mMemory.ResolveRead(mProfile.StaticBufferTableAddress, 8);
        if (table.size() != 8) return CtrIpcRouter::UnhandledResult;
        std::uint32_t descriptor = 0;
        GuestAddress address = 0;
        std::memcpy(&descriptor, table.data(), 4);
        std::memcpy(&address, table.data() + 4, 4);
        const std::uint32_t size = commandBuffer[2];
        auto output = mMemory.ResolveWrite(address, size);
        if ((descriptor & 0xfU) != 2U || (descriptor >> 14U) < size ||
            output.size() != size) return CtrIpcRouter::UnhandledResult;
        std::ranges::fill(output, std::byte{0});
        commandBuffer[0] = kReceiveParameterResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = 0;
        commandBuffer[3] = 1;
        commandBuffer[4] = 0;
        commandBuffer[5] = 0x10;
        commandBuffer[6] = 0;
        commandBuffer[7] = (size << 14U) | 2U;
        commandBuffer[8] = address;
        mWakeupPending = false;
        return 0;
    }
    if (commandBuffer[0] == kAppletUtilityRequest &&
        commandBuffer.size() >= 4 && commandBuffer[3] == 1U) {
        const auto table = mMemory.ResolveRead(mProfile.StaticBufferTableAddress, 8);
        if (table.size() != 8) return CtrIpcRouter::UnhandledResult;
        std::uint32_t descriptor = 0;
        GuestAddress address = 0;
        std::memcpy(&descriptor, table.data(), 4);
        std::memcpy(&address, table.data() + 4, 4);
        auto output = mMemory.ResolveWrite(address, 1);
        if ((commandBuffer[4] & 0xfU) != 2U ||
            ((commandBuffer[4] >> 10U) & 0xfU) != 1U ||
            (descriptor & 0xfU) != 2U || output.size() != 1 ||
            (commandBuffer[1] != 4U && commandBuffer[1] != 7U))
            return CtrIpcRouter::UnhandledResult;
        output[0] = std::byte{0};
        commandBuffer[0] = kAppletUtilityResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = descriptor;
        commandBuffer[3] = address;
        return 0;
    }
    return CtrIpcRouter::UnhandledResult;
}

} // namespace Oot3dSourceRuntime
