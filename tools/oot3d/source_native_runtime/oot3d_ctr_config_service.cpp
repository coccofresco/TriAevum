#include "oot3d_ctr_config_service.h"

#include <cstring>

namespace Oot3dSourceRuntime {
namespace {

constexpr std::uint32_t kGetConfigRequest = 0x00010082U;
constexpr std::uint32_t kGetConfigResponse = 0x00010042U;
constexpr std::uint32_t kGetRegionRequest = 0x00020000U;
constexpr std::uint32_t kGetRegionResponse = 0x00020080U;
constexpr std::uint32_t kSoundOutputModeBlockId = 0x00070001U;
constexpr std::uint32_t kLanguageBlockId = 0x000A0002U;
constexpr std::uint32_t kStereoCameraSettingsBlockId = 0x00050005U;

} // namespace

CtrConfigService::CtrConfigService(GuestAddressSpace& memory,
                                   CtrConfigServiceProfile profile)
    : mMemory(memory), mProfile(profile) {}

CtrResult CtrConfigService::Dispatch(std::span<std::uint32_t> commandBuffer) {
    if (commandBuffer.empty()) {
        return CtrIpcRouter::UnhandledResult;
    }
    if (commandBuffer[0] == kGetRegionRequest && commandBuffer.size() >= 3) {
        commandBuffer[0] = kGetRegionResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = mProfile.SystemRegion;
        return 0;
    }
    if (commandBuffer[0] != kGetConfigRequest || commandBuffer.size() < 5) {
        return CtrIpcRouter::UnhandledResult;
    }

    const std::size_t size = commandBuffer[1];
    const std::uint32_t blockId = commandBuffer[2];
    const std::uint32_t descriptor = commandBuffer[3];
    const GuestAddress address = commandBuffer[4];
    const bool validDescriptor =
        (descriptor & 0x8U) != 0U && ((descriptor >> 1U) & 2U) != 0U &&
        (descriptor >> 4U) == size;
    if (!validDescriptor || !WriteConfigBlock(blockId, address, size)) {
        return CtrIpcRouter::UnhandledResult;
    }
    commandBuffer[0] = kGetConfigResponse;
    commandBuffer[1] = 0;
    commandBuffer[2] = descriptor;
    commandBuffer[3] = address;
    return 0;
}

bool CtrConfigService::WriteConfigBlock(std::uint32_t blockId,
                                        GuestAddress address,
                                        std::size_t size) {
    const void* source = nullptr;
    std::size_t sourceSize = 0;
    if (blockId == kSoundOutputModeBlockId) {
        source = &mProfile.SoundOutputMode;
        sourceSize = sizeof(mProfile.SoundOutputMode);
    } else if (blockId == kLanguageBlockId) {
        source = &mProfile.SystemLanguage;
        sourceSize = sizeof(mProfile.SystemLanguage);
    } else if (blockId == kStereoCameraSettingsBlockId) {
        source = mProfile.StereoCameraSettings.data();
        sourceSize = sizeof(mProfile.StereoCameraSettings);
    }
    if (source == nullptr || sourceSize != size) {
        return false;
    }
    auto destination = mMemory.ResolveWrite(address, size);
    if (destination.size() != size) {
        return false;
    }
    std::memcpy(destination.data(), source, size);
    return true;
}

} // namespace Oot3dSourceRuntime
