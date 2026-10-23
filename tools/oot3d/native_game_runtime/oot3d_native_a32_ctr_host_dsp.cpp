// CTR host services: the dsp::DSP session.
#include "oot3d_native_a32_ctr_host_internal.h"

namespace Oot3dNativeGame {

// SendSyncRequest on a client session of dsp::DSP.
NativeA32HostResult NativeA32CtrHostServices::HandleDspRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
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
    // Guest memory is coherent on the host, as for gsp::Gpu: validate and
    // acknowledge (the ending's audio invalidates DSP-shared buffers).
    if (event.Detail == kDspFlushDataCacheRequest ||
        event.Detail == kDspInvalidateDataCacheRequest) {
        uint32_t address = 0;
        uint32_t size = 0;
        uint32_t descriptor = 0;
        uint32_t processHandle = 0;
        const bool flush = event.Detail == kDspFlushDataCacheRequest;
        if (memory.Read32(commandBuffer + 4U, &address) &&
            memory.Read32(commandBuffer + 8U, &size) &&
            memory.Read32(commandBuffer + 12U, &descriptor) &&
            memory.Read32(commandBuffer + 16U, &processHandle) &&
            descriptor == kCopyHandleDescriptor &&
            processHandle == kCurrentProcessHandle &&
            (size == 0U || memory.IsMapped(address, size)) &&
            memory.Write32(commandBuffer,
                           flush ? kDspFlushDataCacheResponse
                                 : kDspInvalidateDataCacheResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            state.r[0] = kResultSuccess;
            event.Name += flush ? ":FlushDataCache" : ":InvalidateDataCache";
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

} // namespace Oot3dNativeGame
