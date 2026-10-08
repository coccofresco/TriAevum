// CTR host services: the ndm:u, cfg:u, y2r:u and APT:U sessions.
#include "oot3d_native_a32_ctr_host_internal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

namespace Oot3dNativeGame {

// SendSyncRequest on a client session of ndm:u.
NativeA32HostResult NativeA32CtrHostServices::HandleNdmRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
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
    const bool suspendDaemons = event.Detail == kNdmSuspendDaemonsRequest;
    if (suspendDaemons || event.Detail == kNdmResumeDaemonsRequest) {
        uint32_t daemons = 0;
        if (memory.Read32(commandBuffer + 4U, &daemons) &&
            (daemons & ~kNdmAllDaemons) == 0U &&
            memory.Write32(commandBuffer, suspendDaemons
                                              ? kNdmSuspendDaemonsResponse
                                              : kNdmResumeDaemonsResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            mNdmSuspendedDaemons = suspendDaemons
                                       ? mNdmSuspendedDaemons | daemons
                                       : mNdmSuspendedDaemons & ~daemons;
            state.r[0] = kResultSuccess;
            event.Name += (suspendDaemons ? ":SuspendDaemons:"
                                          : ":ResumeDaemons:") +
                          std::to_string(daemons);
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if (event.Detail == kNdmOverrideDefaultDaemonsRequest) {
        uint32_t daemons = 0;
        if (memory.Read32(commandBuffer + 4U, &daemons) &&
            (daemons & ~kNdmAllDaemons) == 0U &&
            memory.Write32(commandBuffer,
                           kNdmOverrideDefaultDaemonsResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            mNdmDefaultDaemons = daemons;
            state.r[0] = kResultSuccess;
            event.Name += ":OverrideDefaultDaemons:" + std::to_string(daemons);
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if (event.Detail == kNdmResetDefaultDaemonsRequest &&
        memory.Write32(commandBuffer, kNdmResetDefaultDaemonsResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
        mNdmDefaultDaemons = kNdmAllDaemons;
        state.r[0] = kResultSuccess;
        event.Name += ":ResetDefaultDaemons";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    if (event.Detail == kNdmGetDefaultDaemonsRequest &&
        memory.Write32(commandBuffer, kNdmGetDefaultDaemonsResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
        memory.Write32(commandBuffer + 8U, mNdmDefaultDaemons)) {
        state.r[0] = kResultSuccess;
        event.Name += ":GetDefaultDaemons";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Wait};
}

// SendSyncRequest on a client session of cfg:u.
// ptm:u (power and pedometer, 3dbrew "PTM Services"): a console on its
// charger with the shell open, a full battery and no steps walked.
NativeA32HostResult NativeA32CtrHostServices::HandlePtmRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext&, const std::optional<std::string>&,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
    struct PtmValue {
        uint32_t Command;
        const char* Name;
        uint32_t Value;
    };
    constexpr std::array<PtmValue, 6> kValues{{
        {0x0005U, "GetAdapterState", 1U},
        {0x0006U, "GetShellState", 1U},
        {0x0007U, "GetBatteryLevel", 5U},
        {0x0008U, "GetBatteryChargeState", 0U},
        {0x0009U, "GetPedometerState", 0U},
        {0x000CU, "GetTotalStepCount", 0U},
    }};
    const uint32_t command = event.Detail >> 16U;
    if (event.Detail == 0x000B00C2U) {
        // GetStepHistory(hours, start time, mapped buffer of u16 per hour).
        uint32_t descriptor = 0, address = 0;
        if (memory.Read32(commandBuffer + 16U, &descriptor) &&
            memory.Read32(commandBuffer + 20U, &address) && (descriptor & 0xEU) == 0xCU) {
            const std::vector<uint8_t> steps(descriptor >> 4U);
            if (memory.WriteBytes(address, steps) &&
                memory.Write32(commandBuffer, 0x000B0042U) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                memory.Write32(commandBuffer + 8U, descriptor) &&
                memory.Write32(commandBuffer + 12U, address)) {
                state.r[0] = kResultSuccess;
                event.Name += ":GetStepHistory";
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
        }
    }
    for (const auto& entry : kValues) {
        if (entry.Command == command &&
            memory.Write32(commandBuffer, (command << 16U) | 0x80U) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess) &&
            memory.Write32(commandBuffer + 8U, entry.Value)) {
            state.r[0] = kResultSuccess;
            event.Name += std::string(":") + entry.Name;
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Wait};
}

NativeA32HostResult NativeA32CtrHostServices::HandleCfgRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
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

// SendSyncRequest on a client session of y2r:u.
NativeA32HostResult NativeA32CtrHostServices::HandleY2rRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
    const uint16_t command = static_cast<uint16_t>(event.Detail >> 16);
    std::vector<uint32_t> arguments((event.Detail >> 6) & 63U);
    bool valid = true;
    for (size_t i = 0; i < arguments.size(); ++i)
        valid &= memory.Read32(commandBuffer + 4U + static_cast<uint32_t>(i) * 4U,
                               &arguments[i]);
    if (valid) {
        auto reply = CtrServices::DispatchY2r(mY2r, command, arguments, memory);
        if (reply.Handled) {
            if (!mY2rCompletionEvent) {
                mY2rCompletionEvent = std::make_shared<KernelObject>();
                mY2rCompletionEvent->Type = "event:y2r_completion";
                mY2rCompletionEvent->ResetType = 0U;
            }
            if (reply.Clear) mY2rCompletionEvent->AvailableCount = 0;
            if (reply.Signal) mY2rCompletionEvent->AvailableCount = 1;
            const uint32_t normalWords = static_cast<uint32_t>(reply.Words.size());
            if (reply.EventHandle) {
                reply.Words.push_back(0U); // Copy one kernel handle.
                reply.Words.push_back(CreateHandle(mY2rCompletionEvent));
            }
            valid = memory.Write32(commandBuffer, uint32_t(command) << 16 |
                normalWords << 6 | (reply.EventHandle ? 2U : 0U));
            for (size_t i = 0; i < reply.Words.size(); ++i)
                valid &= memory.Write32(commandBuffer + 4U + static_cast<uint32_t>(i) * 4U,
                                        reply.Words[i]);
            if (valid) {
                state.r[0] = kResultSuccess;
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                if (reply.Signal) WakeSynchronizationWaiters();
                return {NativeA32HostAction::Resume};
            }
        }
    }
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Wait};
}

// SendSyncRequest on a client session of APT:U.
NativeA32HostResult NativeA32CtrHostServices::HandleAptRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
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
    if (event.Detail == kAptSetScreenCapPostPermissionRequest) {
        uint32_t permission = 0;
        if (memory.Read32(commandBuffer + 4U, &permission) &&
            memory.Write32(commandBuffer,
                           kAptSetScreenCapPostPermissionResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            mAptScreenCapPostPermission = permission;
            state.r[0] = kResultSuccess;
            event.Name += ":SetScreenCapPostPermission:" +
                          std::to_string(permission);
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if (event.Detail == kAptGetScreenCapPostPermissionRequest &&
        memory.Write32(commandBuffer,
                       kAptGetScreenCapPostPermissionResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
        memory.Write32(commandBuffer + 8U, mAptScreenCapPostPermission)) {
        state.r[0] = kResultSuccess;
        event.Name += ":GetScreenCapPostPermission";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    const bool glance = event.Detail == kAptGlanceParameterRequest;
    if ((event.Detail == kAptReceiveParameterRequest || glance) &&
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
                           glance ? kAptGlanceParameterResponse
                                  : kAptReceiveParameterResponse) &&
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
            mAptWakeupPending = glance;
            state.r[0] = kResultSuccess;
            event.Name += glance ? ":GlanceParameter:Wakeup"
                                 : ":ReceiveParameter:Wakeup";
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if ((event.Detail == kAptReceiveParameterRequest || glance) &&
        mAptParameter.has_value()) {
        uint32_t appletId = 0;
        uint32_t bufferSize = 0;
        uint32_t staticDescriptor = 0;
        uint32_t staticAddress = 0;
        const uint32_t staticBufferTable = state.thread_pointer + 0x180U;
        const auto& parameter = *mAptParameter;
        const uint32_t size = static_cast<uint32_t>(
            std::min<size_t>(parameter.Buffer.size(), 0x1000U));
        if (memory.Read32(commandBuffer + 4U, &appletId) &&
            memory.Read32(commandBuffer + 8U, &bufferSize) &&
            appletId == mAptApplicationId && bufferSize >= size &&
            memory.Read32(staticBufferTable, &staticDescriptor) &&
            memory.Read32(staticBufferTable + 4U, &staticAddress) &&
            (staticDescriptor & 0xFU) == 2U &&
            (staticDescriptor >> 14U) >= size &&
            memory.WriteBytes(staticAddress,
                              std::span<const uint8_t>(parameter.Buffer.data(), size)) &&
            memory.Write32(commandBuffer,
                           glance ? kAptGlanceParameterResponse
                                  : kAptReceiveParameterResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess) &&
            memory.Write32(commandBuffer + 8U, parameter.Sender) &&
            memory.Write32(commandBuffer + 12U, parameter.Signal) &&
            memory.Write32(commandBuffer + 16U, size) &&
            memory.Write32(commandBuffer + 20U, kMoveHandleDescriptor) &&
            memory.Write32(commandBuffer + 28U, (size << 14U) | 2U) &&
            memory.Write32(commandBuffer + 32U, staticAddress)) {
            // Receiving moves the block's handle to the application.
            const uint32_t handle =
                !glance && parameter.Object ? CreateHandle(parameter.Object) : 0U;
            if (!memory.Write32(commandBuffer + 24U, handle)) {
                if (handle != 0U) {
                    CloseHandle(handle);
                }
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Wait};
            }
            state.r[0] = kResultSuccess;
            event.Name += (glance ? ":GlanceParameter:signal=" : ":ReceiveParameter:signal=") +
                          std::to_string(parameter.Signal);
            event.Detail = parameter.Signal;
            if (!glance) {
                mAptParameter.reset();
            }
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
    if (HandleAptLibraryAppletRequest(state, memory, commandBuffer, event)) {
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Wait};
}

// A parameter for the application from another applet (the HLE software
// keyboard), delivered by ReceiveParameter / GlanceParameter.
void NativeA32CtrHostServices::QueueAptParameter(uint32_t sender, uint32_t signal,
                                                 std::vector<uint8_t> buffer,
                                                 std::shared_ptr<KernelObject> object) {
    mAptParameter = AptParameter{sender, signal, std::move(buffer), std::move(object)};
    if (mAptParameterEvent) {
        mAptParameterEvent->AvailableCount = 1;
        WakeSynchronizationWaiters();
    }
}

// Library applets as an application starts them: the system software
// keyboard (applet 0x401) is answered at once with the host's text, as the
// keyboard returns it (the 0x400-byte configuration back with a
// WakeupByExit parameter, the text in the caller's shared memory). Sleep
// queries are acknowledged; other applets stay unanswered.
bool NativeA32CtrHostServices::HandleAptLibraryAppletRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    uint32_t commandBuffer, NativeA32CtrSvcEvent& event) {
    const auto reply = [&](uint32_t header, const char* name) {
        if (!memory.Write32(commandBuffer, header) ||
            !memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            return false;
        }
        state.r[0] = kResultSuccess;
        event.Name += name;
        event.Handled = true;
        return true;
    };
    uint32_t appletId = 0;
    switch (event.Detail) {
    case kAptReplySleepQueryRequest:
        return reply(kAptReplySleepQueryResponse, ":ReplySleepQuery");
    case kAptSendCaptureBufferInfoRequest:
        // Where the caller's screens were captured for the applet: the
        // keyboard draws nothing on this host.
        return reply(kAptSendCaptureBufferInfoResponse, ":SendCaptureBufferInfo");
    case kAptReplySleepNotificationCompleteRequest:
        return reply(kAptReplySleepNotificationCompleteResponse,
                     ":ReplySleepNotificationComplete");
    case kAptPrepareToStartLibraryAppletRequest:
        if (!memory.Read32(commandBuffer + 4U, &appletId) ||
            appletId != kAptSoftwareKeyboardApplet) {
            return false;
        }
        mAptLibraryApplet = appletId;
        event.Detail = appletId;
        return reply(kAptPrepareToStartLibraryAppletResponse,
                     ":PrepareToStartLibraryApplet:swkbd");
    case kAptIsRegisteredRequest: {
        if (!memory.Read32(commandBuffer + 4U, &appletId)) {
            return false;
        }
        const bool registered =
            appletId == mAptApplicationId ||
            (mAptLibraryApplet != 0U && appletId == mAptLibraryApplet);
        event.Detail = appletId;
        return reply(kAptIsRegisteredResponse, ":IsRegistered") &&
               memory.Write32(commandBuffer + 8U, registered ? 1U : 0U);
    }
    case kAptSendParameterRequest: {
        uint32_t destination = 0;
        uint32_t signal = 0;
        if (!memory.Read32(commandBuffer + 8U, &destination) ||
            !memory.Read32(commandBuffer + 12U, &signal)) {
            return false;
        }
        event.Detail = signal;
        if (destination == mAptLibraryApplet && mAptLibraryApplet != 0U &&
            signal == kAptSignalRequest) {
            // The caller asks for the memory it copies its screens into
            // (the first word of its request); the keyboard answers with a
            // block of that size, which nothing on this host reads.
            uint32_t size = 0;
            uint32_t staticDescriptor = 0;
            uint32_t requestAddress = 0;
            uint32_t captureSize = 0;
            if (!memory.Read32(commandBuffer + 16U, &size) ||
                !memory.Read32(commandBuffer + 28U, &staticDescriptor) ||
                !memory.Read32(commandBuffer + 32U, &requestAddress) || size < 4U ||
                (staticDescriptor >> 14U) < 4U ||
                !memory.Read32(requestAddress, &captureSize) || captureSize == 0U ||
                captureSize > 0x01000000U) {
                return false;
            }
            auto capture = std::make_shared<KernelObject>();
            capture->Type = "shared_memory:applet_capture";
            capture->FileSize = (captureSize + 0xFFFU) & ~0xFFFU;
            QueueAptParameter(mAptLibraryApplet, kAptSignalResponse, {}, std::move(capture));
        }
        return reply(kAptSendParameterResponse, ":SendParameter");
    }
    case kAptStartLibraryAppletRequest: {
        uint32_t size = 0;
        uint32_t handle = 0;
        uint32_t staticDescriptor = 0;
        uint32_t configAddress = 0;
        if (!memory.Read32(commandBuffer + 4U, &appletId) ||
            !memory.Read32(commandBuffer + 8U, &size) ||
            !memory.Read32(commandBuffer + 16U, &handle) ||
            !memory.Read32(commandBuffer + 20U, &staticDescriptor) ||
            !memory.Read32(commandBuffer + 24U, &configAddress) ||
            appletId != kAptSoftwareKeyboardApplet || appletId != mAptLibraryApplet ||
            size != kSoftwareKeyboardConfigSize ||
            (staticDescriptor >> 14U) < size) {
            return false;
        }
        const auto* block = LookupHandle(handle);
        std::vector<uint8_t> config(size);
        if (block == nullptr || !block->Object->MappedAddress.has_value() ||
            !memory.ReadBytes(configAddress, config)) {
            return false;
        }
        const auto field16 = [&](size_t offset) {
            return static_cast<uint16_t>(config[offset] | (config[offset + 1U] << 8U));
        };
        const auto put32 = [&](size_t offset, uint32_t value) {
            for (size_t index = 0; index < 4U; ++index) {
                config[offset + index] = static_cast<uint8_t>(value >> (8U * index));
            }
        };
        std::u16string text = mSoftwareKeyboardText;
        const uint16_t maximum = field16(kSoftwareKeyboardMaxTextLengthOffset);
        if (maximum != 0U && text.size() > maximum) {
            text.resize(maximum);
        }
        // The text and its terminator at the start of the shared memory.
        std::vector<uint8_t> encoded;
        for (const char16_t character : text) {
            encoded.push_back(static_cast<uint8_t>(character & 0xFFU));
            encoded.push_back(static_cast<uint8_t>(character >> 8U));
        }
        encoded.push_back(0U);
        encoded.push_back(0U);
        if (encoded.size() > block->Object->FileSize ||
            !memory.WriteBytes(*block->Object->MappedAddress, encoded)) {
            return false;
        }
        // The confirming button of the configured layout: the only one, the
        // right of two, the right of three.
        uint32_t buttons = 0;
        std::memcpy(&buttons, config.data() + kSoftwareKeyboardButtonsOffset, sizeof(buttons));
        constexpr std::array<int32_t, 4> kConfirm{0, 2, 5, -1};
        put32(kSoftwareKeyboardReturnCodeOffset,
              static_cast<uint32_t>(kConfirm[std::min<uint32_t>(buttons, 3U)]));
        put32(kSoftwareKeyboardTextOffsetOffset, 0U);
        config[kSoftwareKeyboardTextLengthOffset] = static_cast<uint8_t>(text.size());
        config[kSoftwareKeyboardTextLengthOffset + 1U] = static_cast<uint8_t>(text.size() >> 8U);
        if (!reply(kAptStartLibraryAppletResponse, ":StartLibraryApplet:swkbd")) {
            return false;
        }
        mAptLibraryApplet = 0U;
        QueueAptParameter(kAptSoftwareKeyboardApplet, kAptSignalWakeupByExit, std::move(config));
        return true;
    }
    default:
        return false;
    }
}

void NativeA32CtrHostServices::SetSoftwareKeyboardText(std::u16string text) {
    mSoftwareKeyboardText = std::move(text);
}

} // namespace Oot3dNativeGame
