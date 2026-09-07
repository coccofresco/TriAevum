#include "oot3d_ctr_hid_producer.h"

#include <cstring>

namespace Oot3dSourceRuntime {
namespace {
constexpr std::uint32_t kPadResetTick = 0x00;
constexpr std::uint32_t kPadPreviousResetTick = 0x08;
constexpr std::uint32_t kPadIndex = 0x10;
constexpr std::uint32_t kPadCurrentState = 0x1c;
constexpr std::uint32_t kPadEntries = 0x28;
constexpr std::uint32_t kPadEntrySize = 0x10;
constexpr std::uint32_t kTouchResetTick = 0xa8;
constexpr std::uint32_t kTouchPreviousResetTick = 0xb0;
constexpr std::uint32_t kTouchIndex = 0xb8;
constexpr std::uint32_t kTouchEntries = 0xc8;
constexpr std::uint32_t kTouchEntrySize = 0x08;
constexpr std::uint32_t kGuestButtonMask = 0x00003fffU;
constexpr std::uint32_t kCircleRight = 1U << 28U;
constexpr std::uint32_t kCircleLeft = 1U << 29U;
constexpr std::uint32_t kCircleUp = 1U << 30U;
constexpr std::uint32_t kCircleDown = 1U << 31U;

template <typename Value>
bool Read(std::span<const std::byte> bytes, std::size_t offset,
          Value& value) {
    if (offset > bytes.size() || sizeof(Value) > bytes.size() - offset)
        return false;
    std::memcpy(&value, bytes.data() + offset, sizeof(Value));
    return true;
}

template <typename Value>
bool Write(std::span<std::byte> bytes, std::size_t offset, Value value) {
    if (offset > bytes.size() || sizeof(Value) > bytes.size() - offset)
        return false;
    std::memcpy(bytes.data() + offset, &value, sizeof(Value));
    return true;
}

std::uint32_t ApplyCirclePad(std::uint32_t buttons, std::int16_t x,
                             std::int16_t y) {
    if (x != 0) buttons |= x > 0 ? kCircleRight : kCircleLeft;
    if (y != 0) buttons |= y > 0 ? kCircleUp : kCircleDown;
    return buttons;
}
}

CtrHidProducer::CtrHidProducer(CtrHidService& service) : mService(service) {}

bool CtrHidProducer::Submit(const CtrHidState& state,
                            std::uint64_t sampleTick) {
    auto mappedMemory = mService.SharedMemoryBytes();
    if (mappedMemory.size() != 0x1000) return false;
    auto memory = mappedMemory;
    const std::uint32_t previousIndex = (mPadIndex + 7U) % 8U;
    const std::size_t entry = kPadEntries + mPadIndex * kPadEntrySize;
    const std::size_t previousEntry =
        kPadEntries + previousIndex * kPadEntrySize;
    std::uint32_t previousButtons = 0;
    if (!Read(memory, previousEntry, previousButtons)) return false;
    const std::uint32_t buttons = ApplyCirclePad(
        state.Buttons & kGuestButtonMask, state.CirclePadX, state.CirclePadY);
    const std::uint32_t changed = buttons ^ previousButtons;
    const std::uint32_t additions = changed & buttons;
    const std::uint32_t removals = changed & previousButtons;
    bool valid = Write(memory, kPadCurrentState, buttons) &&
                 Write(memory, kPadIndex, mPadIndex) &&
                 Write(memory, entry, buttons) &&
                 Write(memory, entry + 4, additions) &&
                 Write(memory, entry + 8, removals) &&
                 Write(memory, entry + 12, state.CirclePadX) &&
                 Write(memory, entry + 14, state.CirclePadY);
    if (mPadIndex == 0) {
        std::uint64_t previousTick = 0;
        valid = valid && Read(memory, kPadResetTick, previousTick) &&
                Write(memory, kPadPreviousResetTick, previousTick) &&
                Write(memory, kPadResetTick, sampleTick);
    }
    const std::size_t touchEntry =
        kTouchEntries + mTouchIndex * kTouchEntrySize;
    valid = valid && Write(memory, kTouchIndex, mTouchIndex) &&
            Write(memory, touchEntry, state.TouchX) &&
            Write(memory, touchEntry + 2, state.TouchY) &&
            Write(memory, touchEntry + 4,
                  static_cast<std::uint32_t>(state.TouchPressed));
    if (mTouchIndex == 0) {
        std::uint64_t previousTick = 0;
        valid = valid && Read(memory, kTouchResetTick, previousTick) &&
                Write(memory, kTouchPreviousResetTick, previousTick) &&
                Write(memory, kTouchResetTick, sampleTick);
    }
    if (!valid) return false;
    mPadIndex = (mPadIndex + 1U) % 8U;
    mTouchIndex = (mTouchIndex + 1U) % 8U;
    mService.SignalPadEvents();
    return true;
}

} // namespace Oot3dSourceRuntime
