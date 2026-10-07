// CTR host services of the native A32 process: SVC dispatch, handles and kernel objects, GSP
// framebuffers and interrupts, memory, timing. Services: oot3d_native_a32_ctr_host_*.cpp.
#include "oot3d_native_a32_ctr_host_internal.h"

namespace Oot3dNativeGame {

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
    const bool isPdc =
        interruptId == static_cast<uint8_t>(Oot3dPicaInterruptId::Pdc0) ||
        interruptId == static_cast<uint8_t>(Oot3dPicaInterruptId::Pdc1);
    if (!mGspSharedMemory || !mGspSharedMemory->MappedAddress.has_value() ||
        !mGspInterruptEvent) {
        // GSP relays interrupts only to a registered queue: VBlanks before a
        // title registers one are not delivered. A PICA completion without
        // a queue is an error.
        return isPdc;
    }
    const uint32_t relay =
        *mGspSharedMemory->MappedAddress + mGspThreadId * 0x40U;
    uint8_t index = 0;
    uint8_t count = 0;
    if (!memory.Read8(relay, &index) || !memory.Read8(relay + 1U, &count)) {
        return false;
    }
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

// svcQueryMemory: r2 = address; r0 = result, r1 = base, r2 = size,
// r3 = permissions, r4 = MemoryState, r5 = page flags. The kernel reports the
// memory area holding the address: touching mappings of equal state and
// permissions form one area, and the gaps between mappings are Free.
NativeA32HostResult NativeA32CtrHostServices::HandleQueryMemory(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32CtrSvcEvent event) {
    event.Name = "QueryMemory";
    const uint32_t address = state.r[2];
    event.Detail = address;
    const auto regions = memory.RegionInfos();
    const auto permissions = [](const NativeA32MemoryRegionInfo& region) {
        return 1U | (region.Writable ? 2U : 0U) | (region.Executable ? 4U : 0U);
    };
    const auto end = [](const NativeA32MemoryRegionInfo& region) {
        return uint64_t(region.BaseAddress) + region.Size;
    };
    uint64_t areaBase = 0U;
    uint64_t areaEnd = uint64_t(1) << 32U;
    uint32_t areaPermissions = 0U;
    auto areaState = NativeA32KernelMemoryState::Free;
    size_t hit = regions.size();
    for (size_t index = 0; index < regions.size(); ++index) {
        const auto& region = regions[index];
        if (address >= region.BaseAddress && address < end(region)) {
            hit = index;
            break;
        }
        if (end(region) <= address) {
            areaBase = std::max(areaBase, end(region));
        } else {
            areaEnd = std::min<uint64_t>(areaEnd, region.BaseAddress);
        }
    }
    if (hit != regions.size()) {
        const auto& region = regions[hit];
        areaPermissions = permissions(region);
        areaState = region.KernelState;
        areaBase = region.BaseAddress;
        areaEnd = end(region);
        const auto same = [&](const NativeA32MemoryRegionInfo& other) {
            return permissions(other) == areaPermissions &&
                   other.KernelState == areaState;
        };
        for (size_t index = hit; index > 0U; --index) {
            const auto& previous = regions[index - 1U];
            if (end(previous) != areaBase || !same(previous)) {
                break;
            }
            areaBase = previous.BaseAddress;
        }
        for (size_t index = hit + 1U; index < regions.size(); ++index) {
            const auto& next = regions[index];
            if (next.BaseAddress != areaEnd || !same(next)) {
                break;
            }
            areaEnd = end(next);
        }
    }
    state.r[0] = kResultSuccess;
    state.r[1] = static_cast<uint32_t>(areaBase);
    state.r[2] = static_cast<uint32_t>(
        std::min<uint64_t>(areaEnd - areaBase, 0xFFFFF000U));
    state.r[3] = areaPermissions;
    state.r[4] = static_cast<uint32_t>(areaState);
    state.r[5] = 0U;
    event.Handled = true;
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
             {},
             linear ? NativeA32KernelMemoryState::Continuous
                    : NativeA32KernelMemoryState::Private},
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
    case 0x02:
        return HandleQueryMemory(state, memory, std::move(event));
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
        NativeA32HostResult yielded{NativeA32HostAction::Resume};
        yielded.Yield = true;
        return yielded;
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
                 {},
                 NativeA32KernelMemoryState::Shared},
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
        event.Name += ":address=0x";
        AppendHex8(event.Name, address);
        event.Name += ":type=" + std::to_string(type) +
                      ":value=" + std::to_string(value);
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
    case 0x24: {
        // WaitSynchronization1: r0 = handle, r2:r3 = timeout in ns (negative
        // waits forever). Finite non-zero timeouts are not modelled yet and
        // stop as unhandled.
        event.Name = "WaitSynchronization1";
        event.Detail = state.r[0];
        const auto* record = LookupHandle(state.r[0]);
        const auto timeout = static_cast<int64_t>(
            uint64_t(state.r[2]) | uint64_t(state.r[3]) << 32U);
        if (record == nullptr) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        event.Name += ":" + record->Object->Type;
        if (IsKernelObjectReady(*record->Object, context.ThreadId,
                                context.Process)) {
            AcquireKernelObject(*record->Object, context.ThreadId);
            state.r[0] = kResultSuccess;
        } else if (timeout == 0) {
            state.r[0] = kResultTimeout;
        } else if (timeout > 0) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        } else {
            mSynchronizationWaiters.push_back(
                {{record->Object}, &context.Process, context.ThreadId, true});
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
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
        std::string detail = waitAll ? ":all" : ":any";
        for (uint32_t index = 0; index < handleCount; ++index) {
            uint32_t handle = 0;
            if (!memory.Read32(handlesAddress + index * sizeof(uint32_t),
                               &handle)) {
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Wait};
            }
            detail += index == 0 ? ":0x" : ",0x";
            AppendHex8(detail, handle);
            const auto type = LookupHandleType(handle);
            detail += "=";
            detail += type.value_or("invalid");
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
        event.Name += detail;
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
    case 0x37: {
        // GetThreadId: r1 = thread handle; r1 = kernel thread ID. Kernel IDs
        // start at 1 (titles may treat 0 as "no thread"), so host thread n
        // reports n + 1.
        event.Name = "GetThreadId";
        std::optional<uint32_t> threadId;
        if (state.r[1] == kCurrentThreadHandle) {
            threadId = context.ThreadId;
        } else if (const auto* handle = LookupHandle(state.r[1]);
                   handle != nullptr && handle->Object->Type == "thread") {
            threadId = handle->Object->ThreadId;
        }
        if (!threadId.has_value()) {
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Wait};
        }
        state.r[0] = kResultSuccess;
        state.r[1] = *threadId + 1U;
        event.Detail = *threadId;
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
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
            return HandleNdmRequest(state, memory, context, sessionType,
                                    commandBuffer, std::move(event));
        }
        if (*sessionType == "client_session:cfg:u") {
            return HandleCfgRequest(state, memory, context, sessionType,
                                    commandBuffer, std::move(event));
        }
        if (*sessionType == "client_session:hid:USER") {
            return HandleHidRequest(state, memory, context, sessionType,
                                    commandBuffer, std::move(event));
        }
        if (*sessionType == "client_session:y2r:u") {
            return HandleY2rRequest(state, memory, context, sessionType,
                                    commandBuffer, std::move(event));
        }
        if (*sessionType == "client_session:dsp::DSP") {
            return HandleDspRequest(state, memory, context, sessionType,
                                    commandBuffer, std::move(event));
        }
        if (*sessionType == "client_session:file:romfs" ||
            *sessionType == "client_session:file:savedata") {
            return HandleRomFsRequest(state, memory, context, sessionType,
                                      commandBuffer, std::move(event));
        }
        if (*sessionType == "client_session:gsp::Gpu") {
            return HandleGspRequest(state, memory, context, sessionType,
                                    commandBuffer, std::move(event));
        }
        // APT:A is the applications' APT port of later SDKs (Majora's Mask
        // 3D), with APT:U's commands.
        if (*sessionType == "client_session:APT:U" ||
            *sessionType == "client_session:APT:A") {
            return HandleAptRequest(state, memory, context, sessionType,
                                    commandBuffer, std::move(event));
        }
        if (*sessionType == "client_session:fs:USER") {
            return HandleFsRequest(state, memory, context, sessionType,
                                   commandBuffer, std::move(event));
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
                if (serviceName == "APT:U" || serviceName == "APT:A" ||
                    serviceName == "fs:USER" ||
                    serviceName == "gsp::Gpu" || serviceName == "dsp::DSP" ||
                    serviceName == "cfg:u" || serviceName == "hid:USER" ||
                    serviceName == "ndm:u" || serviceName == "y2r:u") {
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

uint32_t NativeA32CtrHostServices::NdmSuspendedDaemons() const {
    return mNdmSuspendedDaemons;
}

NativeA32CtrRuntimeProfile
NativeA32CtrHostServices::RuntimeProfile() const noexcept {
    return mRuntimeProfile;
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
