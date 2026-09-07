#include "oot3d/cutscene_update_frame_owner.h"

#include <cstdint>

namespace Oot3dSourceCutsceneUpdateFrame {
namespace {

class GuestAddress {
  public:
    explicit GuestAddress(uint32_t address) noexcept : mAddress(address) {
    }

    template <typename T>
    operator T*() const {
        return static_cast<T*>(ResolveGuestMemory(mAddress, sizeof(T)));
    }

  private:
    uint32_t mAddress;
};

} // namespace

// Revision-pinned import of FUN_00321f50 from producer commit d5c2927.
// GuestAddress is the only mechanical lowering applied to target pointers.
void Cutscene_UpdateFrame(
    uint32_t playAddress, uint32_t cutsceneContextAddress) {
    const auto& literal = ActiveLiterals();

    if (*static_cast<int32_t*>(
            GuestAddress(literal.SchedulerStateAddress + 8U)) <
        static_cast<int32_t>(literal.SchedulerThreshold)) {
        return;
    }
    if (*static_cast<uint8_t*>(
            GuestAddress(playAddress + 0x6028U)) != 0U) {
        if (*static_cast<uint8_t*>(
                GuestAddress(playAddress + 0x101U)) != 2U) {
            return;
        }
        if (*static_cast<uint8_t*>(
                GuestAddress(playAddress + 0x6029U)) == 0U) {
            if (QueryBackendClock() == 0) {
                *static_cast<uint16_t*>(
                    GuestAddress(cutsceneContextAddress + 0x20U)) = 0U;
                return;
            }
            CommitBackendClock(playAddress + 0x601CU);
            *static_cast<uint16_t*>(
                GuestAddress(cutsceneContextAddress + 0x20U)) = 0U;
            return;
        }
    }

    auto* const frame = static_cast<uint16_t*>(
        GuestAddress(cutsceneContextAddress + 0x20U));
    if (*frame == 0U) {
        *static_cast<uint32_t*>(
            GuestAddress(playAddress + 0x21A0U)) = 0U;
        *static_cast<uint8_t*>(
            GuestAddress(cutsceneContextAddress + 0x27AU)) = 0U;
    }

    if (*static_cast<uint8_t*>(
            GuestAddress(playAddress + 0x6028U)) != 0U) {
        const int32_t ticks = *static_cast<int32_t*>(GuestAddress(
            playAddress + literal.BackendClockTicksOffset));
        if (ticks >= 0) {
            const int32_t targetFrame =
                ConvertBackendTicksToFrame(
                    ticks, literal.TickScaleBits,
                    literal.FrameRateBits);
            if (targetFrame <= static_cast<int32_t>(*frame)) {
                return;
            }
            do {
                *frame = static_cast<uint16_t>(*frame + 1U);
                Cutscene_ProcessCommands(
                    playAddress, cutsceneContextAddress,
                    *static_cast<uint32_t*>(
                        GuestAddress(playAddress + 0x229CU)));
            } while (
                static_cast<int32_t>(*frame) < targetFrame);
            return;
        }
    }

    *frame = static_cast<uint16_t>(*frame + 1U);
    Cutscene_ProcessCommands(
        playAddress, cutsceneContextAddress,
        *static_cast<uint32_t*>(
            GuestAddress(playAddress + 0x229CU)));
}

} // namespace Oot3dSourceCutsceneUpdateFrame
