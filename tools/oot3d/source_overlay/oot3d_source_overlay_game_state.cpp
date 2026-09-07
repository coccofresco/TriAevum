#include "oot3d_source_overlay_game_state.h"

#include <array>
#include <cstddef>
#include <cstdint>

extern "C" {
std::uint32_t oot3d_game_state_read_main(const void* gameState);
std::uint32_t oot3d_game_state_increment_frames(void* gameState);
}

namespace Oot3dSourceOverlay::GameState {
namespace {

constexpr std::uint32_t kMainOffset = 0x04U;
constexpr std::uint32_t kFramesOffset = 0xF8U;
constexpr std::uint32_t kSavedFrameSize = 8U;
Stats gStats;

bool RangeFits(std::uint32_t address, std::size_t size) {
    return static_cast<std::uint64_t>(address) + size <=
           (std::uint64_t{1} << 32U);
}

bool AccessAllowed(const Services& services, std::uint32_t address,
                   std::size_t size, std::uint32_t access) {
    return services.Host != nullptr && RangeFits(address, size) &&
           (size == 0U ||
            (services.Host->ProbeMemory(
                 services.Host->Context, address, size) & access) == access);
}

bool Read(const Services& services, std::uint32_t address, void* bytes,
          std::size_t size) {
    return services.Host->ReadMemory(
               services.Host->Context, address, bytes, size) != 0;
}

bool Write(const Services& services, std::uint32_t address,
           const void* bytes, std::size_t size) {
    return services.Host->WriteMemory(
               services.Host->Context, address, bytes, size) != 0;
}

void SetBranchResult(Oot3dSourceOverlayGuestState* state,
                     Oot3dSourceOverlayExecutionResult* result,
                     std::uint32_t target, std::uint32_t detail) {
    state->Registers[15] = target;
    result->StructSize = sizeof(*result);
    result->Kind = OOT3D_SOURCE_OVERLAY_BRANCH;
    result->Pc = target;
    result->Detail = detail;
    result->BlocksConsumed = 1U;
}

int DispatchMain(Oot3dSourceOverlayGuestState* state,
                 Oot3dSourceOverlayExecutionResult* result,
                 const Services& services) {
    const std::uint32_t gameState = state->Registers[0];
    if (gameState > UINT32_MAX - kMainOffset ||
        state->Registers[13] < kSavedFrameSize) {
        return 0;
    }
    const std::uint32_t frameAddress =
        state->Registers[13] - kSavedFrameSize;
    if (!AccessAllowed(services, gameState + kMainOffset,
                       sizeof(std::uint32_t),
                       OOT3D_SOURCE_OVERLAY_MEMORY_READ) ||
        !AccessAllowed(services, frameAddress, kSavedFrameSize,
                       OOT3D_SOURCE_OVERLAY_MEMORY_WRITE)) {
        return 0;
    }

    services.ResetSourceMemoryFault();
    const std::uint32_t main = oot3d_game_state_read_main(
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(gameState)));
    if (services.SourceMemoryFaulted()) {
        return 0;
    }

    const std::array<std::uint32_t, 2> savedFrame{
        state->Registers[4], state->Registers[14]};
    if (!Write(services, frameAddress, savedFrame.data(),
               sizeof(savedFrame))) {
        return 0;
    }

    state->Registers[1] = main;
    state->Registers[4] = gameState;
    state->Registers[13] = frameAddress;
    state->Registers[14] = kMainReturn;
    SetBranchResult(state, result, main, kUpdateEntry);
    return 1;
}

int ReturnFromMain(Oot3dSourceOverlayGuestState* state,
                   Oot3dSourceOverlayExecutionResult* result,
                   const Services& services) {
    const std::uint32_t gameState = state->Registers[4];
    const std::uint32_t frameAddress = state->Registers[13];
    if (gameState > UINT32_MAX - kFramesOffset) {
        return 0;
    }
    const std::uint32_t counterAddress = gameState + kFramesOffset;
    if (!AccessAllowed(services, frameAddress, kSavedFrameSize,
                       OOT3D_SOURCE_OVERLAY_MEMORY_READ) ||
        !AccessAllowed(services, counterAddress, sizeof(std::uint32_t),
                       OOT3D_SOURCE_OVERLAY_MEMORY_READ |
                           OOT3D_SOURCE_OVERLAY_MEMORY_WRITE)) {
        return 0;
    }

    std::array<std::uint32_t, 2> savedFrame{};
    if (!Read(services, frameAddress, savedFrame.data(),
              sizeof(savedFrame))) {
        return 0;
    }

    services.ResetSourceMemoryFault();
    const std::uint32_t frameCounter = oot3d_game_state_increment_frames(
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(gameState)));
    if (services.SourceMemoryFaulted()) {
        return 0;
    }

    state->Registers[0] = frameCounter;
    state->Registers[4] = savedFrame[0];
    state->Registers[13] += kSavedFrameSize;
    SetBranchResult(state, result, savedFrame[1], kMainReturn);
    return 1;
}

} // namespace

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Services& services) {
    if (state == nullptr || result == nullptr || services.Host == nullptr ||
        services.ResetSourceMemoryFault == nullptr ||
        services.SourceMemoryFaulted == nullptr) {
        return 0;
    }
    switch (entry) {
    case kUpdateEntry: {
        ++gStats.UpdateCalls;
        const int handled = DispatchMain(state, result, services);
        gStats.UpdateHandled += handled != 0 ? 1U : 0U;
        return handled;
    }
    case kMainReturn: {
        ++gStats.MainReturnCalls;
        const int handled = ReturnFromMain(state, result, services);
        gStats.MainReturnHandled += handled != 0 ? 1U : 0U;
        return handled;
    }
    default:
        return 0;
    }
}

Stats GetStats() {
    return gStats;
}

} // namespace Oot3dSourceOverlay::GameState
