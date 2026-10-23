// CTR host services: the hid:USER session and the HID shared-memory samples.
#include "oot3d_native_a32_ctr_host_internal.h"

namespace Oot3dNativeGame {

// SendSyncRequest on a client session of hid:USER.
NativeA32HostResult NativeA32CtrHostServices::HandleHidRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
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

// New 3DS ir:rst: GetHandles (1), Initialize (2), Shutdown (3). The shared
// memory holds a ring of eight C-stick/ZL/ZR samples behind two tick stamps.
NativeA32HostResult NativeA32CtrHostServices::HandleIrstRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
    const uint32_t command = event.Detail >> 16U;
    bool written = false;
    if (event.Detail == 0x00010000U) {
        auto shared = std::make_shared<KernelObject>();
        shared->Type = "shared_memory:ir:rst";
        shared->FileSize = 0x1000U;
        const uint32_t sharedHandle = CreateHandle(shared);
        const uint32_t eventHandle = CreateHandle("event:ir:rst");
        written = memory.Write32(commandBuffer, 0x00010043U) &&
                  memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                  memory.Write32(commandBuffer + 8U, 0x04000000U) &&
                  memory.Write32(commandBuffer + 12U, sharedHandle) &&
                  memory.Write32(commandBuffer + 16U, eventHandle);
        if (!written) {
            CloseHandle(sharedHandle);
            CloseHandle(eventHandle);
        }
    } else if (event.Detail == 0x00020080U || event.Detail == 0x00030000U) {
        written = memory.Write32(commandBuffer, (command << 16U) | 0x40U) &&
                  memory.Write32(commandBuffer + 4U, kResultSuccess);
    }
    if (written) {
        state.r[0] = kResultSuccess;
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Wait};
}

bool NativeA32CtrHostServices::WriteIrstSample(NativeA32Memory& memory, int16_t x,
                                               int16_t y, uint32_t buttons) {
    for (const auto& handle : mHandles) {
        if (handle.Object->Type != "shared_memory:ir:rst" ||
            !handle.Object->MappedAddress.has_value()) {
            continue;
        }
        const uint32_t base = *handle.Object->MappedAddress;
        uint32_t index = 0;
        if (!memory.Read32(base + 16U, &index)) return false;
        index = (index + 1U) % 8U;
        const uint32_t entry = base + 24U + index * 16U;
        const uint32_t previous = base + 24U + ((index + 7U) % 8U) * 16U;
        uint32_t previousButtons = 0;
        if (!memory.Read32(previous, &previousButtons)) return false;
        if (!memory.WriteHost<uint64_t>(base, mSystemTicks) ||
            !memory.WriteHost<uint64_t>(base + 8U, mSystemTicks == 0 ? 1U : mSystemTicks - 1U) ||
            !memory.WriteHost<uint32_t>(entry, buttons) ||
            !memory.WriteHost<uint32_t>(entry + 4U, buttons & ~previousButtons) ||
            !memory.WriteHost<uint32_t>(entry + 8U, previousButtons & ~buttons) ||
            !memory.WriteHost<uint16_t>(entry + 12U, static_cast<uint16_t>(x)) ||
            !memory.WriteHost<uint16_t>(entry + 14U, static_cast<uint16_t>(y)) ||
            !memory.WriteHost<uint32_t>(base + 16U, index)) {
            return false;
        }
    }
    return true;
}

} // namespace Oot3dNativeGame
