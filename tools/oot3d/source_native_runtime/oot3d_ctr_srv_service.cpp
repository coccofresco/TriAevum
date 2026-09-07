#include "oot3d_ctr_srv_service.h"

#include <array>
#include <cstring>
#include <string>

namespace Oot3dSourceRuntime {
namespace {

constexpr std::uint32_t kRegisterClientRequest = 0x00010002U;
constexpr std::uint32_t kRegisterClientResponse = 0x00010040U;
constexpr std::uint32_t kGetServiceHandleRequest = 0x00050100U;
constexpr std::uint32_t kGetServiceHandleResponse = 0x00050042U;
constexpr std::uint32_t kMoveHandleDescriptor = 0x10U;

} // namespace

CtrSrvService::CtrSrvService(CtrIpcRouter& router) : mRouter(router) {}

CtrResult CtrSrvService::Dispatch(std::span<std::uint32_t> commandBuffer) {
    if (commandBuffer.empty()) {
        return CtrIpcRouter::UnhandledResult;
    }
    if (commandBuffer[0] == kRegisterClientRequest && commandBuffer.size() >= 2) {
        commandBuffer[0] = kRegisterClientResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (commandBuffer[0] != kGetServiceHandleRequest ||
        commandBuffer.size() < 4) {
        return CtrIpcRouter::UnhandledResult;
    }

    const std::uint32_t nameLength = commandBuffer[3];
    if (nameLength == 0 || nameLength > 8) {
        return CtrIpcRouter::UnhandledResult;
    }
    std::array<char, 8> nameBytes{};
    std::memcpy(nameBytes.data(), &commandBuffer[1], nameBytes.size());
    const std::string serviceName(nameBytes.data(), nameLength);
    CtrHandle handle = 0;
    const CtrResult result = mRouter.ConnectToPort(handle, serviceName);
    if (result < 0) {
        return result;
    }
    commandBuffer[0] = kGetServiceHandleResponse;
    commandBuffer[1] = 0;
    commandBuffer[2] = kMoveHandleDescriptor;
    commandBuffer[3] = handle;
    return 0;
}

} // namespace Oot3dSourceRuntime
