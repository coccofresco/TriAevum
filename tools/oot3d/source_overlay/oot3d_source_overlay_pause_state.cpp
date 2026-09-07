#include "oot3d_source_overlay_pause_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Oot3dSourceOverlay::PauseState {
namespace {

constexpr std::size_t kRecordSize = 0x118U;
constexpr std::uint32_t kPrimarySharedRecord = 0x00435D18U;
constexpr std::uint32_t kSecondarySharedRecord = 0x00435D1CU;
Stats gStats;

bool RangeFits(std::uint32_t address, std::size_t size) {
    return static_cast<std::uint64_t>(address) + size <=
           (std::uint64_t{1} << 32U);
}

bool RangesOverlap(std::uint32_t left, std::size_t leftSize,
                   std::uint32_t right, std::size_t rightSize) {
    const std::uint64_t leftEnd =
        static_cast<std::uint64_t>(left) + leftSize;
    const std::uint64_t rightEnd =
        static_cast<std::uint64_t>(right) + rightSize;
    return leftSize != 0U && rightSize != 0U &&
           static_cast<std::uint64_t>(left) < rightEnd &&
           static_cast<std::uint64_t>(right) < leftEnd;
}

bool AccessAllowed(const Oot3dSourceOverlayHostApi& host,
                   std::uint32_t address, std::size_t size,
                   std::uint32_t access) {
    return RangeFits(address, size) && host.ProbeMemory != nullptr &&
           (host.ProbeMemory(host.Context, address, size) & access) == access;
}

bool Read(const Oot3dSourceOverlayHostApi& host, std::uint32_t address,
          void* bytes, std::size_t size) {
    return host.ReadMemory != nullptr &&
           host.ReadMemory(host.Context, address, bytes, size) != 0;
}

void WriteWord(std::array<std::uint8_t, kRecordSize>& record,
               std::size_t index, std::uint32_t value) {
    std::memcpy(record.data() + index * sizeof(value), &value,
                sizeof(value));
}

void SetBranchResult(Oot3dSourceOverlayGuestState& state,
                     Oot3dSourceOverlayExecutionResult& result) {
    state.Registers[15] = state.Registers[14];
    result.StructSize = sizeof(result);
    result.Kind = OOT3D_SOURCE_OVERLAY_BRANCH;
    result.Pc = state.Registers[15];
    result.Detail = kInitializeEntry;
    result.BlocksConsumed = 1U;
}

int Initialize(Oot3dSourceOverlayGuestState& state,
               Oot3dSourceOverlayExecutionResult& result,
               const Oot3dSourceOverlayHostApi& host) {
    const std::uint32_t outputRecord = state.Registers[0];
    if (state.Registers[13] < sizeof(std::uint32_t)) {
        return 0;
    }
    const std::uint32_t spillAddress =
        state.Registers[13] - sizeof(std::uint32_t);
    constexpr std::uint32_t kReadWrite =
        OOT3D_SOURCE_OVERLAY_MEMORY_READ |
        OOT3D_SOURCE_OVERLAY_MEMORY_WRITE;

    // Aliasing changes when the original A32 sequence observes its globals or
    // stack spill. Keep those unusual calls on the exact whole-AOT fallback.
    if (!AccessAllowed(host, outputRecord, kRecordSize, kReadWrite) ||
        !AccessAllowed(host, spillAddress, sizeof(std::uint32_t),
                       OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) ||
        !AccessAllowed(host, kPrimarySharedRecord, sizeof(std::uint32_t),
                       OOT3D_SOURCE_OVERLAY_MEMORY_READ) ||
        !AccessAllowed(host, kSecondarySharedRecord, sizeof(std::uint32_t),
                       OOT3D_SOURCE_OVERLAY_MEMORY_READ) ||
        RangesOverlap(outputRecord, kRecordSize, spillAddress,
                      sizeof(std::uint32_t)) ||
        RangesOverlap(outputRecord, kRecordSize, kPrimarySharedRecord,
                      sizeof(std::uint32_t)) ||
        RangesOverlap(outputRecord, kRecordSize, kSecondarySharedRecord,
                      sizeof(std::uint32_t)) ||
        host.ResolveWrite == nullptr) {
        return 0;
    }

    std::array<std::uint8_t, kRecordSize> record{};
    std::uint32_t primary = 0U;
    std::uint32_t secondary = 0U;
    if (!Read(host, outputRecord, record.data(), record.size()) ||
        !Read(host, kPrimarySharedRecord, &primary, sizeof(primary)) ||
        !Read(host, kSecondarySharedRecord, &secondary, sizeof(secondary))) {
        return 0;
    }

    void* const spillDestination = host.ResolveWrite(
        host.Context, spillAddress, sizeof(std::uint32_t));
    void* const recordDestination =
        host.ResolveWrite(host.Context, outputRecord, record.size());
    if (spillDestination == nullptr || recordDestination == nullptr) {
        return 0;
    }

    for (std::size_t index = 1U; index <= 0x15U; ++index) {
        WriteWord(record, index, 0U);
    }
    for (std::size_t index = 0x29U; index <= 0x2DU; ++index) {
        WriteWord(record, index, 0U);
    }
    WriteWord(record, 0x31U, 0U);
    WriteWord(record, 0x34U, 0U);
    WriteWord(record, 0x35U, 0U);
    WriteWord(record, 0x36U, UINT32_MAX);
    WriteWord(record, 0x28U, primary);
    for (std::size_t index = 0x38U; index <= 0x3CU; ++index) {
        WriteWord(record, index, 0U);
    }
    WriteWord(record, 0x40U, 0U);
    WriteWord(record, 0x43U, 0U);
    WriteWord(record, 0x44U, 0U);
    WriteWord(record, 0x45U, UINT32_MAX);
    WriteWord(record, 0U, secondary);
    WriteWord(record, 0x37U, primary);

    const std::uint32_t savedR4 = state.Registers[4];
    std::memcpy(spillDestination, &savedR4, sizeof(savedR4));
    std::memcpy(recordDestination, record.data(), record.size());

    state.Registers[1] = secondary;
    state.Registers[12] = primary;
    SetBranchResult(state, result);
    return 1;
}

} // namespace

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Oot3dSourceOverlayHostApi* host) {
    if (entry != kInitializeEntry || state == nullptr || result == nullptr ||
        host == nullptr) {
        return 0;
    }
    ++gStats.Calls;
    const int handled = Initialize(*state, *result, *host);
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

} // namespace Oot3dSourceOverlay::PauseState
