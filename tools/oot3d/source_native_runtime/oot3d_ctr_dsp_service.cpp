#include "oot3d_ctr_dsp_service.h"

#include <algorithm>
#include <cstring>

namespace Oot3dSourceRuntime {
namespace {
constexpr std::uint32_t kLoadComponentRequest = 0x001100c2U;
constexpr std::uint32_t kLoadComponentResponse = 0x00110082U;
constexpr std::uint32_t kRegisterInterruptRequest = 0x00150082U;
constexpr std::uint32_t kRegisterInterruptResponse = 0x00150040U;
constexpr std::uint32_t kGetSemaphoreEventRequest = 0x00160000U;
constexpr std::uint32_t kGetSemaphoreEventResponse = 0x00160042U;
constexpr std::uint32_t kSetSemaphoreMaskRequest = 0x00170040U;
constexpr std::uint32_t kSetSemaphoreMaskResponse = 0x00170040U;
constexpr std::uint32_t kWritePipeRequest = 0x000d0082U;
constexpr std::uint32_t kWritePipeResponse = 0x000d0040U;
constexpr std::uint32_t kSetSemaphoreRequest = 0x00070040U;
constexpr std::uint32_t kSetSemaphoreResponse = 0x00070040U;
constexpr std::uint32_t kReadPipeRequest = 0x001000c0U;
constexpr std::uint32_t kReadPipeResponse = 0x00100082U;
constexpr std::uint32_t kConvertAddressRequest = 0x000c0040U;
constexpr std::uint32_t kConvertAddressResponse = 0x000c0080U;
constexpr std::uint32_t kHeadphoneStatusRequest = 0x001f0000U;
constexpr std::uint32_t kHeadphoneStatusResponse = 0x001f0080U;
constexpr std::uint32_t kDspRamAddress = 0x1ff40000U;
constexpr std::uint32_t kAudioPipe = 2U;
constexpr std::array<std::uint16_t, 15> kAudioStructures{
    0xbfff, 0x9e92, 0x8680, 0xa792, 0x9430, 0x8400, 0x8540, 0x9492,
    0x8710, 0x8410, 0xa912, 0xaa12, 0xaad2, 0xac52, 0xac5c};

std::pair<std::uint32_t, GuestAddress> ReadStaticBuffer(
    GuestAddressSpace& memory, GuestAddress table) {
    const auto bytes = memory.ResolveRead(table, 8);
    std::pair<std::uint32_t, GuestAddress> result{};
    if (bytes.size() == 8) {
        std::memcpy(&result.first, bytes.data(), 4);
        std::memcpy(&result.second, bytes.data() + 4, 4);
    }
    return result;
}
}

CtrDspService::CtrDspService(GuestAddressSpace& memory, CtrIpcRouter& router,
                             CtrDspProfile profile)
    : mMemory(memory), mRouter(router), mProfile(profile) {}

CtrResult CtrDspService::Dispatch(std::span<std::uint32_t> commandBuffer) {
    if (commandBuffer.empty()) return CtrIpcRouter::UnhandledResult;
    const std::uint32_t request = commandBuffer[0];
    if (request == kLoadComponentRequest && commandBuffer.size() >= 6) {
        const std::uint32_t size = commandBuffer[1];
        const std::uint32_t descriptor = commandBuffer[4];
        const auto bytes = mMemory.ResolveRead(commandBuffer[5], size);
        if ((descriptor & 0x8U) == 0 || ((descriptor >> 1U) & 1U) == 0 ||
            (descriptor >> 4U) != size || bytes.size() != size)
            return CtrIpcRouter::UnhandledResult;
        mComponent.assign(bytes.begin(), bytes.end());
        mProgramMask = static_cast<std::uint16_t>(commandBuffer[2]);
        mDataMask = static_cast<std::uint16_t>(commandBuffer[3]);
        commandBuffer[0] = kLoadComponentResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = 1;
        commandBuffer[3] = descriptor;
        commandBuffer[4] = commandBuffer[5];
        return 0;
    }
    if (request == kRegisterInterruptRequest && commandBuffer.size() >= 5 &&
        commandBuffer[1] < mInterrupts.size() &&
        commandBuffer[2] < mInterrupts[0].size() && commandBuffer[3] == 0) {
        std::shared_ptr<CtrKernelObject> event;
        if (commandBuffer[4] != 0) {
            event = mRouter.KernelObject(commandBuffer[4]);
            if (!event || event->Kind != CtrKernelObjectKind::Event)
                return CtrIpcRouter::UnhandledResult;
        }
        mInterrupts[commandBuffer[1]][commandBuffer[2]] = std::move(event);
        commandBuffer[0] = kRegisterInterruptResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (request == kGetSemaphoreEventRequest && commandBuffer.size() >= 4) {
        if (!mSemaphoreEvent) {
            mSemaphoreEvent = std::make_shared<CtrKernelObject>(
                CtrKernelObjectKind::Event, "dsp:semaphore-event", 0, 1);
            mSemaphoreEvent->AutoReset = false;
        }
        CtrHandle handle = 0;
        if (mRouter.OpenKernelObject(mSemaphoreEvent, handle) < 0)
            return CtrIpcRouter::UnhandledResult;
        commandBuffer[0] = kGetSemaphoreEventResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = 0;
        commandBuffer[3] = handle;
        return 0;
    }
    if (request == kSetSemaphoreMaskRequest && commandBuffer.size() >= 2) {
        mSemaphoreMask = static_cast<std::uint16_t>(commandBuffer[1]);
        commandBuffer[0] = kSetSemaphoreMaskResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (request == kSetSemaphoreRequest && commandBuffer.size() >= 2) {
        mSemaphoreValue = static_cast<std::uint16_t>(commandBuffer[1]);
        commandBuffer[0] = kSetSemaphoreResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (request == kWritePipeRequest && commandBuffer.size() >= 5 &&
        commandBuffer[1] < mPipeOutput.size() &&
        (commandBuffer[3] & 0xfU) == 2U &&
        (commandBuffer[3] >> 14U) >= commandBuffer[2]) {
        const auto bytes = mMemory.ResolveRead(commandBuffer[4], commandBuffer[2]);
        if (bytes.size() != commandBuffer[2]) return CtrIpcRouter::UnhandledResult;
        if (commandBuffer[1] == kAudioPipe) {
            if (bytes.size() < 4) return CtrIpcRouter::UnhandledResult;
            const std::uint8_t state = std::to_integer<std::uint8_t>(bytes[0]);
            if (state > 3) return CtrIpcRouter::UnhandledResult;
            mAudioRunning = state != 1 && state != 3;
            if (state == 0 || state == 2 || state == 3) {
                auto& output = mPipeOutput[kAudioPipe];
                output.resize(2 + kAudioStructures.size() * 2);
                const std::uint16_t count = kAudioStructures.size();
                std::memcpy(output.data(), &count, 2);
                std::memcpy(output.data() + 2, kAudioStructures.data(),
                            kAudioStructures.size() * 2);
                if (mInterrupts[2][kAudioPipe])
                    mInterrupts[2][kAudioPipe]->AvailableCount = 1;
            }
        }
        commandBuffer[0] = kWritePipeResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (request == kReadPipeRequest && commandBuffer.size() >= 5 &&
        commandBuffer[1] < mPipeOutput.size()) {
        const auto [descriptor, address] = ReadStaticBuffer(
            mMemory, mProfile.StaticBufferTableAddress);
        const std::uint32_t requested = commandBuffer[3] & 0xffffU;
        auto& pipe = mPipeOutput[commandBuffer[1]];
        const std::uint32_t returned = pipe.size() >= requested ? requested : 0;
        auto output = mMemory.ResolveWrite(address, returned);
        if ((descriptor & 0xfU) != 2U ||
            ((descriptor >> 10U) & 0xfU) != 0U ||
            (descriptor >> 14U) < returned || output.size() != returned)
            return CtrIpcRouter::UnhandledResult;
        std::copy_n(pipe.begin(), returned, output.begin());
        pipe.erase(pipe.begin(), pipe.begin() + returned);
        commandBuffer[0] = kReadPipeResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = returned;
        commandBuffer[3] = (returned << 14U) | 2U;
        commandBuffer[4] = address;
        return 0;
    }
    if (request == kConvertAddressRequest && commandBuffer.size() >= 3 &&
        commandBuffer[1] <= 0x1ffffU) {
        const GuestAddress address = (commandBuffer[1] << 1U) + kDspRamAddress;
        if (mMemory.ResolveRead(address, 1).empty())
            return CtrIpcRouter::UnhandledResult;
        commandBuffer[0] = kConvertAddressResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = address;
        return 0;
    }
    if (request == kHeadphoneStatusRequest && commandBuffer.size() >= 3) {
        commandBuffer[0] = kHeadphoneStatusResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = mProfile.HeadphonesConnected ? 1U : 0U;
        return 0;
    }
    return CtrIpcRouter::UnhandledResult;
}

std::span<const std::byte> CtrDspService::Component() const { return mComponent; }
bool CtrDspService::AudioRunning() const { return mAudioRunning; }
std::uint16_t CtrDspService::SemaphoreValue() const { return mSemaphoreValue; }

} // namespace Oot3dSourceRuntime
