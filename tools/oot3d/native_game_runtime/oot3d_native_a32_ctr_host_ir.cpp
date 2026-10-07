// CTR host services: the ir:USER session (Circle Pad Pro / New 3DS
// ZL/ZR/C-stick over infrared, 3dbrew "IR Services", "IRUSER Shared Memory").
// By default the host behaves as a New 3DS, whose ir module presents the
// built-in C-stick and ZL/ZR as a Circle Pad Pro that is always present:
// RequireConnection(1) connects at once, and the Extra HID device answers the
// title's polling and calibration requests with packets in the receive ring.
// With the device detached, connection requests never complete and the title
// keeps using HID alone (an Old 3DS without the accessory).
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
constexpr uint32_t kIrSendIrNop = 0x000DU;
constexpr uint32_t kIrSendIrNopLarge = 0x000EU;
constexpr uint32_t kIrInitializeIrNopShared = 0x0018U;
constexpr uint32_t kIrReleaseReceivedData = 0x0019U;

// The Extra HID (Circle Pad Pro) device id of RequireConnection.
constexpr uint8_t kIrExtraHidDeviceId = 1U;
// Shared memory: a 0x10-byte status header, the receive ring information
// (begin, end, count, unused) and, from 0x20, the packet table (offset, size
// per packet) followed by the packet data.
constexpr uint32_t kIrConnectionStatusOffset = 0x08U;
constexpr uint32_t kIrConnectionRoleOffset = 0x0AU;
constexpr uint32_t kIrConnectedOffset = 0x0CU;
constexpr uint32_t kIrNetworkIdOffset = 0x0DU;
constexpr uint32_t kIrInitializedOffset = 0x0EU;
constexpr uint32_t kIrReceiveInfoOffset = 0x10U;
constexpr uint32_t kIrReceiveTableOffset = 0x20U;
constexpr uint32_t kIrPacketInfoSize = 8U;
// The connected values a New 3DS reports.
constexpr uint8_t kIrStatusConnected = 2U;
constexpr uint8_t kIrRoleConnected = 2U;
// ReleaseReceivedData beyond the queued packets.
constexpr uint32_t kIrResultInvalidReceiveCount = 0xD8A10C0DU;

// Extra HID requests and responses.
constexpr uint8_t kExtraHidConfigurePolling = 0x01U;
constexpr uint8_t kExtraHidReadCalibration = 0x02U;
constexpr uint8_t kExtraHidPollResponse = 0x10U;
constexpr uint8_t kExtraHidCalibrationResponse = 0x11U;
constexpr uint32_t kExtraHidStickCenter = 0x800U;
constexpr int32_t kExtraHidStickRadius = 0x7FF;
constexpr uint8_t kExtraHidBatteryFull = 0x1FU;
// Calibration block read from a New 3DS (the values Citra and Azahar serve).
constexpr std::array<uint8_t, 0x40> kExtraHidCalibration{
    0x00, 0x00, 0x08, 0x80, 0x85, 0xEB, 0x11, 0x3F, 0x85, 0xEB, 0x11, 0x3F, 0xFF, 0xFF, 0xFF, 0xF5,
    0xFF, 0x00, 0x08, 0x80, 0x85, 0xEB, 0x11, 0x3F, 0x85, 0xEB, 0x11, 0x3F, 0xFF, 0xFF, 0xFF, 0x65,
    0xFF, 0x00, 0x08, 0x80, 0x85, 0xEB, 0x11, 0x3F, 0x85, 0xEB, 0x11, 0x3F, 0xFF, 0xFF, 0xFF, 0x65,
    0xFF, 0x00, 0x08, 0x80, 0x85, 0xEB, 0x11, 0x3F, 0x85, 0xEB, 0x11, 0x3F, 0xFF, 0xFF, 0xFF, 0x65};
constexpr uint64_t kCtrTicksPerSecond = 268111856ULL;
// A report backlog longer than this (a stalled guest) is dropped, not burst.
constexpr uint64_t kExtraHidMaximumBacklogReports = 8U;

uint8_t IrCrc8(std::span<const uint8_t> bytes) {
    uint8_t crc = 0;
    for (const uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint8_t>((crc & 0x80U) != 0U ? (crc << 1U) ^ 0x07U : crc << 1U);
        }
    }
    return crc;
}

uint32_t ExtraHidStickValue(int16_t axis) {
    const int32_t scaled =
        static_cast<int32_t>(std::lround(static_cast<double>(axis) * kExtraHidStickRadius /
                                         ThreeDsRecomp::Input::kNativeStickMaximum));
    return static_cast<uint32_t>(
        std::clamp<int32_t>(static_cast<int32_t>(kExtraHidStickCenter) + scaled, 0, 0xFFF));
}

} // namespace

void NativeA32CtrHostServices::SetIrExtraHidAttached(bool attached) noexcept {
    mIrExtraHidAttached = attached;
}

// The connection fields of the shared header and the connection-status event.
void NativeA32CtrHostServices::SetIrConnection(NativeA32Memory& memory, bool connected) {
    mIrExtraHidConnected = connected;
    if (!connected) {
        mIrExtraHidPeriodTicks = 0;
    }
    if (mIrSharedMemory && mIrSharedMemory->MappedAddress.has_value()) {
        const uint32_t base = *mIrSharedMemory->MappedAddress;
        memory.Write8(base + kIrConnectionStatusOffset, connected ? kIrStatusConnected : 0U);
        memory.Write8(base + kIrConnectionRoleOffset, connected ? kIrRoleConnected : 0U);
        memory.Write8(base + kIrConnectedOffset, connected ? 1U : 0U);
    }
    auto& event = mIrEvents[kIrGetConnectionStatusEvent - kIrGetReceiveEvent];
    if (event) {
        event->AvailableCount = 1;
        WakeSynchronizationWaiters();
    }
}

// Frames a payload as an IR packet (0xA5, network id, 6- or 14-bit size,
// payload, CRC-8) and appends it to the receive ring.
bool NativeA32CtrHostServices::PutIrReceivePacket(NativeA32Memory& memory,
                                                  std::span<const uint8_t> payload) {
    if (!mIrSharedMemory || !mIrSharedMemory->MappedAddress.has_value() ||
        mIrReceivePacketCapacity == 0U || payload.size() >= 0x4000U) {
        return false;
    }
    const uint32_t base = *mIrSharedMemory->MappedAddress;
    uint8_t networkId = 0;
    memory.Read8(base + kIrNetworkIdOffset, &networkId);
    std::vector<uint8_t> packet{0xA5U, networkId};
    if (payload.size() < 0x40U) {
        packet.push_back(static_cast<uint8_t>(payload.size()));
    } else {
        packet.push_back(static_cast<uint8_t>(0x40U | (payload.size() >> 8U)));
        packet.push_back(static_cast<uint8_t>(payload.size()));
    }
    packet.insert(packet.end(), payload.begin(), payload.end());
    packet.push_back(IrCrc8(packet));

    const uint32_t table = base + kIrReceiveTableOffset;
    const uint32_t data = table + kIrPacketInfoSize * mIrReceivePacketCapacity;
    const uint32_t dataSize = mIrReceiveBufferSize - kIrPacketInfoSize * mIrReceivePacketCapacity;
    const auto size = static_cast<uint32_t>(packet.size());
    if (mIrReceiveCount == mIrReceivePacketCapacity || size > dataSize) {
        return false;
    }
    uint32_t writeOffset = 0;
    if (mIrReceiveCount != 0U) {
        uint32_t firstOffset = 0, lastOffset = 0, lastSize = 0;
        const uint32_t last = (mIrReceiveEnd + mIrReceivePacketCapacity - 1U) % mIrReceivePacketCapacity;
        if (!memory.Read32(table + kIrPacketInfoSize * mIrReceiveBegin, &firstOffset) ||
            !memory.Read32(table + kIrPacketInfoSize * last, &lastOffset) ||
            !memory.Read32(table + kIrPacketInfoSize * last + 4U, &lastSize)) {
            return false;
        }
        writeOffset = (lastOffset + lastSize) % dataSize;
        const uint32_t freeSpace = (firstOffset + dataSize - writeOffset) % dataSize;
        if (size > freeSpace) {
            return false;
        }
    }
    for (uint32_t index = 0; index < size; ++index) {
        if (!memory.Write8(data + (writeOffset + index) % dataSize, packet[index])) {
            return false;
        }
    }
    const uint32_t info = table + kIrPacketInfoSize * mIrReceiveEnd;
    mIrReceiveEnd = (mIrReceiveEnd + 1U) % mIrReceivePacketCapacity;
    ++mIrReceiveCount;
    return memory.Write32(info, writeOffset) && memory.Write32(info + 4U, size) &&
           memory.Write32(base + kIrReceiveInfoOffset, mIrReceiveBegin) &&
           memory.Write32(base + kIrReceiveInfoOffset + 4U, mIrReceiveEnd) &&
           memory.Write32(base + kIrReceiveInfoOffset + 8U, mIrReceiveCount);
}

// A request sent to the Circle Pad Pro (SendIrNop payload).
void NativeA32CtrHostServices::HandleExtraHidRequest(NativeA32Memory& memory,
                                                     std::span<const uint8_t> request) {
    if (request.empty()) {
        return;
    }
    if (request[0] == kExtraHidConfigurePolling && request.size() == 3U) {
        // Starts or re-times the input reports (byte 1: period in ms).
        mIrExtraHidPeriodTicks = std::max<uint64_t>(1U, kCtrTicksPerSecond * request[1] / 1000U);
        mIrNextExtraHidReportTick = mSystemTicks + mIrExtraHidPeriodTicks;
        return;
    }
    if (request[0] == kExtraHidReadCalibration && request.size() == 6U) {
        const uint16_t offset = static_cast<uint16_t>((request[2] | (request[3] << 8U)) & ~0xFU);
        const uint16_t size = static_cast<uint16_t>((request[4] | (request[5] << 8U)) & ~0xFU);
        if (static_cast<size_t>(offset) + size > kExtraHidCalibration.size()) {
            return;
        }
        std::vector<uint8_t> response{kExtraHidCalibrationResponse,
                                      static_cast<uint8_t>(offset), static_cast<uint8_t>(offset >> 8U),
                                      static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8U)};
        response.insert(response.end(), kExtraHidCalibration.begin() + offset,
                        kExtraHidCalibration.begin() + offset + size);
        if (PutIrReceivePacket(memory, response)) {
            mIrEvents[0]->AvailableCount = 1;
            WakeSynchronizationWaiters();
        }
    }
}

bool NativeA32CtrHostServices::AdvanceIrToCurrentTick(
    NativeA32Memory& memory, const ThreeDsRecomp::Input::ExtraHidState& state) {
    if (!mIrExtraHidConnected || mIrExtraHidPeriodTicks == 0U || !mIrEvents[0]) {
        return false;
    }
    if (mSystemTicks > mIrNextExtraHidReportTick +
                           kExtraHidMaximumBacklogReports * mIrExtraHidPeriodTicks) {
        mIrNextExtraHidReportTick = mSystemTicks;
    }
    const uint32_t stick =
        ExtraHidStickValue(state.CStickX) | (ExtraHidStickValue(state.CStickY) << 12U);
    const bool zl = (state.Buttons & ThreeDsRecomp::Input::ButtonMask(ThreeDsRecomp::Input::Button::Zl)) != 0U;
    const bool zr = (state.Buttons & ThreeDsRecomp::Input::ButtonMask(ThreeDsRecomp::Input::Button::Zr)) != 0U;
    // Byte 4: battery level, then "not held" bits for ZL, ZR and the
    // accessory's own R (absent on a New 3DS).
    const std::array<uint8_t, 6> report{
        kExtraHidPollResponse, static_cast<uint8_t>(stick), static_cast<uint8_t>(stick >> 8U),
        static_cast<uint8_t>(stick >> 16U),
        static_cast<uint8_t>(kExtraHidBatteryFull | (zl ? 0U : 0x20U) | (zr ? 0U : 0x40U) | 0x80U),
        0U};
    bool sent = false;
    while (mIrNextExtraHidReportTick <= mSystemTicks) {
        // A full ring drops the report, as the device does.
        if (PutIrReceivePacket(memory, report)) {
            sent = true;
            ++mIrExtraHidReportsSent;
        }
        mIrNextExtraHidReportTick += mIrExtraHidPeriodTicks;
    }
    if (sent) {
        mIrEvents[0]->AvailableCount = 1;
        WakeSynchronizationWaiters();
    }
    return sent;
}

// SendSyncRequest on a client session of ir:USER.
NativeA32HostResult NativeA32CtrHostServices::HandleIrRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext&, const std::optional<std::string>&,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
    const uint32_t command = event.Detail >> 16U;
    const auto respond = [&](const char* name, uint32_t result = kResultSuccess) {
        if (!memory.Write32(commandBuffer, (command << 16U) | 0x40U) ||
            !memory.Write32(commandBuffer + 4U, result)) {
            mSvcEvents.push_back(std::move(event));
            return NativeA32HostResult{NativeA32HostAction::Wait};
        }
        state.r[0] = kResultSuccess;
        event.Name += std::string(":") + name;
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return NativeA32HostResult{NativeA32HostAction::Resume};
    };
    std::array<uint32_t, 9> words{};
    for (size_t index = 1; index < words.size(); ++index) {
        memory.Read32(commandBuffer + 4U * static_cast<uint32_t>(index), &words[index]);
    }
    switch (command) {
    case kIrInitializeIrNopShared: {
        // (shared size, receive size, receive packets, send size, send
        // packets, baud rate, copy-handle descriptor, shared memory handle)
        const HandleRecord* record = LookupHandle(words[8]);
        const uint32_t sharedSize = words[1];
        const uint32_t receiveSize = words[2];
        const uint32_t receivePackets = words[3];
        if (record == nullptr || !record->Object->MappedAddress.has_value() ||
            receivePackets == 0U ||
            receiveSize <= kIrPacketInfoSize * receivePackets ||
            kIrReceiveTableOffset + static_cast<uint64_t>(receiveSize) > sharedSize) {
            break;
        }
        mIrSharedMemory = record->Object;
        mIrReceiveBufferSize = receiveSize;
        mIrReceivePacketCapacity = receivePackets;
        mIrReceiveBegin = mIrReceiveEnd = mIrReceiveCount = 0;
        mIrExtraHidConnected = false;
        mIrExtraHidPeriodTicks = 0;
        const uint32_t base = *mIrSharedMemory->MappedAddress;
        const std::array<uint8_t, kIrReceiveTableOffset> cleared{};
        if (!memory.WriteBytes(base, cleared) || !memory.Write8(base + kIrInitializedOffset, 1U)) {
            break;
        }
        mIrInitialized = true;
        return respond("InitializeIrNopShared");
    }
    case kIrFinalizeIrNop:
        if (mIrExtraHidConnected) {
            SetIrConnection(memory, false);
        }
        mIrSharedMemory.reset();
        mIrReceivePacketCapacity = 0;
        mIrInitialized = false;
        return respond("FinalizeIrNop");
    case kIrRequireConnection:
        // Byte parameter: the device id. Only the Extra HID device exists.
        if (mIrExtraHidAttached && (words[1] & 0xFFU) == kIrExtraHidDeviceId && mIrSharedMemory) {
            SetIrConnection(memory, true);
            return respond("RequireConnection:extra_hid");
        }
        return respond("RequireConnection");
    case kIrDisconnect:
        SetIrConnection(memory, false);
        return respond("Disconnect");
    case kIrSendIrNop:
    case kIrSendIrNopLarge: {
        // (size, static or mapped buffer descriptor, address)
        const uint32_t size = words[1];
        if (size > 0x1000U) {
            break;
        }
        std::vector<uint8_t> payload(size);
        if (!memory.ReadBytes(words[3], payload)) {
            break;
        }
        if (mIrExtraHidConnected) {
            HandleExtraHidRequest(memory, payload);
            if (mIrEvents[1]) {
                mIrEvents[1]->AvailableCount = 1;
                WakeSynchronizationWaiters();
            }
        }
        return respond(command == kIrSendIrNop ? "SendIrNop" : "SendIrNopLarge");
    }
    case kIrReleaseReceivedData: {
        const uint32_t count = words[1];
        if (count > mIrReceiveCount || mIrReceivePacketCapacity == 0U) {
            return respond("ReleaseReceivedData:invalid", kIrResultInvalidReceiveCount);
        }
        mIrReceiveCount -= count;
        mIrReceiveBegin = (mIrReceiveBegin + count) % mIrReceivePacketCapacity;
        const uint32_t base = *mIrSharedMemory->MappedAddress;
        memory.Write32(base + kIrReceiveInfoOffset, mIrReceiveBegin);
        memory.Write32(base + kIrReceiveInfoOffset + 8U, mIrReceiveCount);
        return respond("ReleaseReceivedData");
    }
    case kIrClearReceiveBuffer:
    case kIrClearSendBuffer:
        return respond(command == kIrClearReceiveBuffer ? "ClearReceiveBuffer" : "ClearSendBuffer");
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
            state.r[0] = kResultSuccess;
            event.Name += std::string(":") + names[command - kIrGetReceiveEvent];
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
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
