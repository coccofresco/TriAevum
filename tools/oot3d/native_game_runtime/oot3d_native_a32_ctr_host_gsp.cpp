// CTR host services: the gsp::Gpu session.
#include "oot3d_native_a32_ctr_host_internal.h"

namespace Oot3dNativeGame {

// SendSyncRequest on a client session of gsp::Gpu.
NativeA32HostResult NativeA32CtrHostServices::HandleGspRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
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
    if (event.Detail == kGspImportDisplayCaptureInfoRequest) {
        bool written = memory.Write32(commandBuffer, kGspImportDisplayCaptureInfoResponse) &&
                       memory.Write32(commandBuffer + 4U, kResultSuccess);
        for (uint32_t screen = 0; written && screen < 2U; ++screen) {
            const auto framebuffer = Framebuffer(screen).value_or(NativeA32CtrFramebufferState{});
            const uint32_t base = commandBuffer + 8U + screen * 16U;
            written = memory.Write32(base, framebuffer.AddressLeft) &&
                      memory.Write32(base + 4U, framebuffer.AddressRight) &&
                      memory.Write32(base + 8U, framebuffer.Format) &&
                      memory.Write32(base + 12U, framebuffer.Stride);
        }
        if (written) {
            state.r[0] = kResultSuccess;
            event.Name += ":ImportDisplayCaptureInfo";
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if ((event.Detail == kGspSaveVramSysAreaRequest ||
         event.Detail == kGspRestoreVramSysAreaRequest) &&
        memory.Write32(commandBuffer, event.Detail == kGspSaveVramSysAreaRequest
                                          ? kGspSaveVramSysAreaResponse
                                          : kGspRestoreVramSysAreaResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
        state.r[0] = kResultSuccess;
        event.Name += event.Detail == kGspSaveVramSysAreaRequest ? ":SaveVramSysArea"
                                                                 : ":RestoreVramSysArea";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
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
                    // A short list is usually a synchronization split
                    // (an IRQ request): its words name the interrupt the
                    // guest will wait for.
                    if (submitted && mConfig.DetailedSvcDiagnostics &&
                        commandList.size() <= 16U) {
                        commandDetail << ",words=";
                        for (const uint32_t word : commandList) {
                            commandDetail << std::hex << std::setw(8)
                                          << word << ' ';
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
                    CpuPhaseProbe::Scope cpuFrontend(CpuPhaseProbe::Phase::PicaFrontend);
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
                    // Region to region, no per-DMA buffer (sizes reach
                    // ~170 MB at title transitions).
                    submitted = memory.CopyBytes(destination, source, size);
                    if (submitted) {
                        submitted = QueueGspInterrupt(
                            memory, static_cast<uint8_t>(
                                        Oot3dPicaInterruptId::Dma));
                    } else {
                        picaError = "GSP DMA range is not mapped";
                    }
                }
                if (submitted &&
                    ((packet.Control & 0xFFU) == static_cast<uint32_t>(Oot3dGspCommandId::DisplayTransfer) ||
                     (packet.Control & 0xFFU) == static_cast<uint32_t>(Oot3dGspCommandId::TextureCopy)) &&
                    !submissionResult.DisplayTransferDeferredToGpu) {
                    if (!submissionResult
                             .DisplayTransferCpuCopySuppressed) {
                        if ((packet.Control & 0xFFU) == static_cast<uint32_t>(Oot3dGspCommandId::TextureCopy)) {
                            Oot3dPicaTextureCopyPlan copy;
                            submitted = BuildOot3dPicaTextureCopyPlan(packet, copy, &picaError) &&
                                        ExecuteOot3dPicaTextureCopy(copy, memory, &picaError);
                        } else {
                        submitted = ExecuteOot3dPicaDisplayTransfer(
                            {packet.Parameters[0], packet.Parameters[1],
                             packet.Parameters[2], packet.Parameters[3],
                             packet.Parameters[4]},
                            memory, &picaError);
                        }
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
    // Around a library applet the holder gives the GPU up and acquires it
    // again.
    if (event.Detail == kGspReleaseRightRequest && session != nullptr &&
        mGpuRightOwner == session->Object &&
        memory.Write32(commandBuffer, kGspReleaseRightResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
        mGpuRightOwner.reset();
        state.r[0] = kResultSuccess;
        event.Name += ":ReleaseRight";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
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
            // CreateHandle may reallocate mHandles, which interruptEvent
            // points into: keep the object, not the record.
            auto interruptEventObject = interruptEvent->Object;
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
                mGspInterruptEvent = std::move(interruptEventObject);
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

} // namespace Oot3dNativeGame
