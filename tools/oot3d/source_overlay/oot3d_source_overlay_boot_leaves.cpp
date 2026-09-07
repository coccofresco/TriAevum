#include "oot3d_source_overlay_boot_leaves.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Oot3dSourceOverlay::BootLeaves {
namespace {

constexpr std::uint32_t kReadWrite =
    OOT3D_SOURCE_OVERLAY_MEMORY_READ | OOT3D_SOURCE_OVERLAY_MEMORY_WRITE;
Stats gStats;

bool RangeFits(std::uint32_t address, std::size_t size) {
    return static_cast<std::uint64_t>(address) + size <=
           (std::uint64_t{1} << 32U);
}

bool GuestWritable(const Oot3dSourceOverlayHostApi& host,
                   std::uint32_t address, std::size_t size) {
    return RangeFits(address, size) && host.ProbeMemory != nullptr &&
           (host.ProbeMemory(host.Context, address, size) &
            OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) != 0U;
}

void SetReturn(Oot3dSourceOverlayGuestState& state,
               Oot3dSourceOverlayExecutionResult& result,
               std::uint32_t entry) {
    state.Registers[15] = state.Registers[14];
    result.StructSize = sizeof(result);
    result.Kind = OOT3D_SOURCE_OVERLAY_BRANCH;
    result.Pc = state.Registers[15];
    result.Detail = entry;
    result.BlocksConsumed = 1U;
}

int ExecuteMemset(Oot3dSourceOverlayGuestState& state,
                  Oot3dSourceOverlayExecutionResult& result,
                  const Oot3dSourceOverlayHostApi& host) {
    const std::uint32_t destination = state.Registers[0];
    const std::size_t size = state.Registers[1];
    const std::uint8_t value = static_cast<std::uint8_t>(state.Registers[2]);

    // This is the reviewed target contract at 0x00303B14: the host C ABI is
    // intentionally not used here because the target argument order is
    // destination, size, value and the target return is the end pointer.
    if (size != 0U) {
        if (!GuestWritable(host, destination, size) ||
            host.ResolveWrite == nullptr) {
            return 0;
        }
        void* const target = host.ResolveWrite(host.Context, destination, size);
        if (target == nullptr) {
            return 0;
        }
        std::memset(target, value, size);
    } else if (!RangeFits(destination, 0U)) {
        return 0;
    }

    // r0 is the only returned value in the maintained C contract. The other
    // argument registers remain caller-clobbered; preserving them avoids
    // inventing a host-side ABI that the source does not specify.
    state.Registers[0] = destination + static_cast<std::uint32_t>(size);
    SetReturn(state, result, kMemsetEntry);
    return 1;
}

int ExecuteIdentityDestructor(Oot3dSourceOverlayGuestState& state,
                               Oot3dSourceOverlayExecutionResult& result) {
    // The reviewed body is `return object;`: it has no guest memory access,
    // no external call and no state mutation other than the return PC.
    SetReturn(state, result, kIdentityDestructorEntry);
    return 1;
}

} // namespace

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Oot3dSourceOverlayHostApi* host) {
    if (state == nullptr || result == nullptr || host == nullptr) {
        return 0;
    }
    ++gStats.Calls;
    int handled = 0;
    switch (entry) {
    case kMemsetEntry:
        handled = ExecuteMemset(*state, *result, *host);
        break;
    case kIdentityDestructorEntry:
        handled = ExecuteIdentityDestructor(*state, *result);
        break;
    default:
        return 0;
    }
    if (handled != 0) {
        ++gStats.Handled;
    } else {
        ++gStats.Rejected;
    }
    return handled;
}

Stats GetStats() {
    return gStats;
}

} // namespace Oot3dSourceOverlay::BootLeaves
