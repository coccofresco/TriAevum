#include "oot3d_ctr_gsp_service.h"

#include <cstring>
#include <vector>

namespace Oot3dSourceRuntime {
namespace {
constexpr std::uint32_t kCurrentProcessHandle = 0xffff8001U;
constexpr std::uint32_t kAcquireRightRequest = 0x00160042U;
constexpr std::uint32_t kAcquireRightResponse = 0x00160040U;
constexpr std::uint32_t kRegisterRelayRequest = 0x00130042U;
constexpr std::uint32_t kRegisterRelayResponse = 0x00130082U;
constexpr std::uint32_t kFirstInitialization = 0x00002a07U;
constexpr std::uint32_t kWriteRegistersRequest = 0x00010082U;
constexpr std::uint32_t kWriteRegistersResponse = 0x00010040U;
constexpr std::uint32_t kWriteRegistersMaskedRequest = 0x00020084U;
constexpr std::uint32_t kWriteRegistersMaskedResponse = 0x00020040U;
constexpr std::uint32_t kSetBufferSwapRequest = 0x00050200U;
constexpr std::uint32_t kSetBufferSwapResponse = 0x00050040U;
constexpr std::uint32_t kTriggerQueueRequest = 0x000c0000U;
constexpr std::uint32_t kTriggerQueueResponse = 0x000c0040U;
constexpr std::uint32_t kSetPrioritiesRequest = 0x001e0080U;
constexpr std::uint32_t kSetPrioritiesResponse = 0x001e0040U;
constexpr std::uint32_t kFlushCacheRequest = 0x00080082U;
constexpr std::uint32_t kFlushCacheResponse = 0x00080040U;
constexpr std::uint32_t kInvalidateCacheRequest = 0x00090082U;
constexpr std::uint32_t kInvalidateCacheResponse = 0x00090040U;
constexpr std::uint32_t kSetLcdForceBlackRequest = 0x000b0040U;
constexpr std::uint32_t kSetLcdForceBlackResponse = 0x000b0040U;

std::uint32_t ReadWord(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void WriteWord(std::span<std::byte> bytes, std::size_t offset,
               std::uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
}

CtrGspService::CtrGspService(GuestAddressSpace& memory, CtrIpcRouter& router,
                             CtrGpuBackend* backend)
    : mMemory(memory), mRouter(router), mBackend(backend) {}

CtrResult CtrGspService::Dispatch(std::span<std::uint32_t> commandBuffer) {
    if (commandBuffer.empty()) return CtrIpcRouter::UnhandledResult;
    const std::uint32_t request = commandBuffer[0];
    if ((request == kFlushCacheRequest || request == kInvalidateCacheRequest) &&
        commandBuffer.size() >= 5 && commandBuffer[3] == 0 &&
        commandBuffer[4] == kCurrentProcessHandle &&
        (commandBuffer[2] == 0 ||
         mMemory.ResolveRead(commandBuffer[1], commandBuffer[2]).size() ==
             commandBuffer[2])) {
        commandBuffer[0] = request == kFlushCacheRequest
                               ? kFlushCacheResponse
                               : kInvalidateCacheResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (request == kAcquireRightRequest && commandBuffer.size() >= 4 &&
        commandBuffer[2] == 0 && commandBuffer[3] == kCurrentProcessHandle) {
        commandBuffer[0] = kAcquireRightResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (request == kRegisterRelayRequest && commandBuffer.size() >= 5 &&
        commandBuffer[2] == 0) {
        const auto event = mRouter.KernelObject(commandBuffer[3]);
        if (!event || event->Kind != CtrKernelObjectKind::Event ||
            mInterruptEvent) return CtrIpcRouter::UnhandledResult;
        if (!mSharedMemory) {
            mSharedMemory = std::make_shared<CtrKernelObject>();
            mSharedMemory->Kind = CtrKernelObjectKind::SharedMemory;
            mSharedMemory->Name = "gsp:shared-memory";
            mSharedMemory->SharedMemory.resize(0x1000);
        }
        CtrHandle handle = 0;
        if (mRouter.OpenKernelObject(mSharedMemory, handle) < 0)
            return CtrIpcRouter::UnhandledResult;
        mInterruptEvent = event;
        commandBuffer[0] = kRegisterRelayResponse;
        commandBuffer[1] = kFirstInitialization;
        commandBuffer[2] = 0;
        commandBuffer[3] = 0;
        commandBuffer[4] = handle;
        return 0;
    }
    if (request == kSetPrioritiesRequest && commandBuffer.size() >= 3) {
        mPriority = commandBuffer[1];
        mPriorityWithRights = commandBuffer[2];
        commandBuffer[0] = kSetPrioritiesResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (request == kSetLcdForceBlackRequest && commandBuffer.size() >= 2 &&
        mBackend) {
        mBackend->SetLcdForceBlack(commandBuffer[1] != 0);
        commandBuffer[0] = kSetLcdForceBlackResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (request == kSetBufferSwapRequest && commandBuffer.size() >= 9 &&
        mBackend) {
        const CtrGspFramebuffer framebuffer{
            commandBuffer[1], commandBuffer[2], commandBuffer[3],
            commandBuffer[4], commandBuffer[5], commandBuffer[6],
            commandBuffer[7]};
        if (framebuffer.Screen >= 2 || framebuffer.ActiveBuffer >= 2 ||
            framebuffer.ShownBuffer >= 2 || framebuffer.Stride == 0 ||
            (framebuffer.Format & 7U) > 4U ||
            !mBackend->SetFramebuffer(framebuffer))
            return CtrIpcRouter::UnhandledResult;
        commandBuffer[0] = kSetBufferSwapResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (request == kWriteRegistersRequest &&
        DispatchRegisterWrite(commandBuffer, false)) return 0;
    if (request == kWriteRegistersMaskedRequest &&
        DispatchRegisterWrite(commandBuffer, true)) return 0;
    if (request == kTriggerQueueRequest && commandBuffer.size() >= 2 &&
        TriggerCommandQueue()) {
        commandBuffer[0] = kTriggerQueueResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    return CtrIpcRouter::UnhandledResult;
}

bool CtrGspService::DispatchRegisterWrite(
    std::span<std::uint32_t> commandBuffer, bool masked) {
    if (!mBackend || commandBuffer.size() < (masked ? 7U : 5U)) return false;
    const std::uint32_t size = commandBuffer[2];
    if ((size & 3U) != 0 || (commandBuffer[3] & 0xfU) != 2U ||
        ((commandBuffer[3] >> 10U) & 0xfU) != 0U ||
        (commandBuffer[3] >> 14U) < size) return false;
    const auto valuesBytes = mMemory.ResolveRead(commandBuffer[4], size);
    if (valuesBytes.size() != size) return false;
    std::vector<std::uint32_t> values(size / 4U);
    std::memcpy(values.data(), valuesBytes.data(), size);
    std::vector<std::uint32_t> masks;
    if (masked) {
        if ((commandBuffer[5] & 0xfU) != 2U ||
            ((commandBuffer[5] >> 10U) & 0xfU) != 1U ||
            (commandBuffer[5] >> 14U) < size) return false;
        const auto maskBytes = mMemory.ResolveRead(commandBuffer[6], size);
        if (maskBytes.size() != size) return false;
        masks.resize(size / 4U);
        std::memcpy(masks.data(), maskBytes.data(), size);
    }
    if (!mBackend->WriteRegisters(commandBuffer[1], values, masks)) return false;
    commandBuffer[0] = masked ? kWriteRegistersMaskedResponse
                              : kWriteRegistersResponse;
    commandBuffer[1] = 0;
    return true;
}

bool CtrGspService::TriggerCommandQueue() {
    if (!mBackend || !mSharedMemory || !mSharedMemory->MappedMemory)
        return false;
    auto queue = mSharedMemory->MappedMemory->ResolveWrite(
        mSharedMemory->MappedAddress + 0x800U, 0x200U);
    if (queue.size() != 0x200U) return false;
    std::uint32_t header = ReadWord(queue, 0);
    std::uint32_t index = header & 0xffU;
    std::uint32_t count = (header >> 8U) & 0xffU;
    std::uint32_t status = (header >> 16U) & 0xffU;
    const std::uint32_t stop = header >> 24U;
    if (index >= 15 || count > 15) return false;
    while (count != 0 && status == 0 && stop == 0) {
        const std::size_t offset = 0x20U + index * 0x20U;
        CtrGspCommand command;
        command.Control = ReadWord(queue, offset);
        for (std::size_t word = 0; word < command.Parameters.size(); ++word)
            command.Parameters[word] = ReadWord(queue, offset + 4U + word * 4U);
        std::vector<std::uint32_t> list;
        if ((command.Control & 0xffU) == 1U) {
            const std::uint32_t size = command.Parameters[1];
            if ((size & 3U) != 0 || size > 0x01000000U) return false;
            const auto bytes = mMemory.ResolveRead(command.Parameters[0], size);
            if (bytes.size() != size) return false;
            list.resize(size / 4U);
            std::memcpy(list.data(), bytes.data(), size);
        }
        if (!mBackend->SubmitCommand(command, list)) return false;
        for (const std::uint8_t interrupt : mBackend->TakeInterrupts())
            if (!QueueInterrupt(interrupt)) return false;
        --count;
        index = (index + 1U) % 15U;
        if (((command.Control >> 16U) & 0xffU) != 0) status = 1;
    }
    WriteWord(queue, 0, index | (count << 8U) | (status << 16U) |
                          (stop << 24U));
    return true;
}

bool CtrGspService::QueueInterrupt(std::uint8_t interrupt) {
    if (!mSharedMemory || !mSharedMemory->MappedMemory || !mInterruptEvent)
        return false;
    auto relay = mSharedMemory->MappedMemory->ResolveWrite(
        mSharedMemory->MappedAddress, 0x40);
    if (relay.size() != 0x40) return false;
    const std::uint8_t index = std::to_integer<std::uint8_t>(relay[0]);
    const std::uint8_t count = std::to_integer<std::uint8_t>(relay[1]);
    if (count >= 0x34) {
        relay[2] = std::byte{1};
        return true;
    }
    const std::uint8_t slot = static_cast<std::uint8_t>((index + count) % 0x34);
    relay[0x0c + slot] = static_cast<std::byte>(interrupt);
    relay[1] = static_cast<std::byte>(count + 1U);
    mInterruptEvent->AvailableCount = 1;
    return true;
}

std::shared_ptr<CtrKernelObject> CtrGspService::SharedMemory() const {
    return mSharedMemory;
}

} // namespace Oot3dSourceRuntime
