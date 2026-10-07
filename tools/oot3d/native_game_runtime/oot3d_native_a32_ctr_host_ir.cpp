// CTR host services: the ir:USER session (Circle Pad Pro / New 3DS
// ZL/ZR/C-stick over infrared, 3dbrew "IR Services"). The host presents a
// console with no infrared accessory: initialization, buffer, connection and
// disconnection requests succeed; the connection-status, receive and send
// events are created but never signalled, so the title keeps using HID.
#include "oot3d_native_a32_ctr_host_internal.h"

namespace Oot3dNativeGame {

namespace {

constexpr uint32_t kIrFinalizeIrNop = 0x0002U;
constexpr uint32_t kIrClearReceiveBuffer = 0x0003U;
constexpr uint32_t kIrClearSendBuffer = 0x0004U;
constexpr uint32_t kIrRequireConnection = 0x0006U;
constexpr uint32_t kIrDisconnect = 0x0009U;
constexpr uint32_t kIrGetReceiveEvent = 0x000AU;
constexpr uint32_t kIrGetSendEvent = 0x000BU;
constexpr uint32_t kIrGetConnectionStatusEvent = 0x000CU;
constexpr uint32_t kIrInitializeIrNopShared = 0x0018U;
constexpr uint32_t kIrReleaseReceivedData = 0x0019U;

} // namespace

// SendSyncRequest on a client session of ir:USER.
NativeA32HostResult NativeA32CtrHostServices::HandleIrRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext&, const std::optional<std::string>&,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
    const uint32_t command = event.Detail >> 16U;
    const auto respond = [&](const char* name) {
        state.r[0] = kResultSuccess;
        event.Name += std::string(":") + name;
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return NativeA32HostResult{NativeA32HostAction::Resume};
    };
    switch (command) {
    case kIrFinalizeIrNop:
    case kIrClearReceiveBuffer:
    case kIrClearSendBuffer:
    case kIrRequireConnection:
    case kIrDisconnect:
    case kIrInitializeIrNopShared:
    case kIrReleaseReceivedData:
        if (memory.Write32(commandBuffer, (command << 16U) | 0x40U) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            if (command == kIrInitializeIrNopShared) mIrInitialized = true;
            if (command == kIrFinalizeIrNop) mIrInitialized = false;
            constexpr std::array<const char*, 0x1AU> names{
                "", "", "FinalizeIrNop", "ClearReceiveBuffer", "ClearSendBuffer", "", "RequireConnection",
                "", "", "Disconnect", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
                "InitializeIrNopShared", "ReleaseReceivedData"};
            return respond(names[command]);
        }
        break;
    case kIrGetReceiveEvent:
    case kIrGetSendEvent:
    case kIrGetConnectionStatusEvent: {
        auto& object = mIrEvents[command - kIrGetReceiveEvent];
        if (!object) {
            constexpr std::array<const char*, 3> types{
                "event:ir_receive", "event:ir_send", "event:ir_connection_status"};
            object = std::make_shared<KernelObject>();
            object->Type = types[command - kIrGetReceiveEvent];
            object->ResetType = 0U;
        }
        if (memory.Write32(commandBuffer, (command << 16U) | 0x42U) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess) &&
            memory.Write32(commandBuffer + 8U, kCopyHandleDescriptor) &&
            memory.Write32(commandBuffer + 12U, CreateHandle(object))) {
            constexpr std::array<const char*, 3> names{
                "GetReceiveEvent", "GetSendEvent", "GetConnectionStatusEvent"};
            return respond(names[command - kIrGetReceiveEvent]);
        }
        break;
    }
    default:
        break;
    }
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Wait};
}

} // namespace Oot3dNativeGame
