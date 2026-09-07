#include "oot3d_native_a32_execution.h"

#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

constexpr uint32_t kCodeBase = 0x00100000U;
constexpr uint32_t kRandS16Offset = 0x00368B68U;
constexpr uint32_t kRandomStateAddress = 0x0050C0C4U;
constexpr uint32_t kMultiplier = 0x0019660DU;
constexpr uint32_t kIncrement = 0x3C6EF35FU;

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void WriteWord(std::vector<uint8_t>& image, uint32_t address, uint32_t value) {
    const size_t offset = static_cast<size_t>(address - kCodeBase);
    for (size_t byte = 0; byte < sizeof(value); ++byte) {
        image.at(offset + byte) = static_cast<uint8_t>(value >> (byte * 8));
    }
}

} // namespace

int main() {
    try {
        std::vector<uint8_t> image(
            static_cast<size_t>(kRandomStateAddress + 8U - kCodeBase), 0);
        WriteWord(image, 0x00375A08U, kRandomStateAddress);
        WriteWord(image, 0x00375A0CU, kMultiplier);
        WriteWord(image, 0x00375A10U, kIncrement);
        WriteWord(image, 0x00375A14U, 0x3F800000U);

        Oot3dNativeGame::NativeA32ExecutionRuntime runtime(image, kCodeBase);
        Expect(runtime.Available(), "A32 AOT registry is unavailable");

        constexpr uint32_t busProbe = kRandomStateAddress - 0x20U;
        Expect(runtime.Write64(busProbe, 0x8877665544332211ULL, nullptr),
               "guest 64-bit write");
        uint8_t byte = 0;
        uint16_t half = 0;
        uint32_t word = 0;
        uint64_t wide = 0;
        Expect(runtime.Read8(busProbe + 1U, &byte) && byte == 0x22U,
               "guest byte read");
        Expect(runtime.Read16(busProbe + 2U, &half) && half == 0x4433U,
               "guest halfword read");
        Expect(runtime.Read32(busProbe + 4U, &word) && word == 0x88776655U,
               "guest word read");
        Expect(runtime.Read64(busProbe, &wide, nullptr) &&
                   wide == 0x8877665544332211ULL,
               "guest 64-bit read");
        Expect(!runtime.Read32(0x60000000U, &word),
               "unmapped guest read must fail");

        const auto scratch = runtime.AllocateScratch(24, 16);
        const auto scratchNext = runtime.AllocateScratch(8, 32);
        Expect(scratch.has_value() && scratchNext.has_value() &&
                   (*scratch & 15U) == 0 && (*scratchNext & 31U) == 0 &&
                   *scratchNext > *scratch,
               "guest scratch allocation and alignment");
        Expect(runtime.Write32(*scratch, 0xA5A55A5AU),
               "guest scratch write");
        runtime.ResetScratch();
        const auto scratchReused = runtime.AllocateScratch(24, 16);
        Expect(scratchReused == scratch && runtime.Read32(*scratchReused, &word) &&
                   word == 0,
               "guest scratch reset and zero initialization");

        constexpr uint32_t initialState = 1U;
        constexpr uint32_t expectedState = initialState * kMultiplier + kIncrement;
        constexpr uint32_t expectedFloatBits =
            (expectedState >> 9U) | 0x3F800000U;
        Expect(runtime.Write32(kRandomStateAddress, initialState),
               "initialize native RNG state");
        oot3d::recomp::a32::GuestState guestState;
        guestState.r[0] = 0;
        guestState.r[1] = 100;
        const auto call = runtime.Call(kRandS16Offset, guestState);
        Expect(call.Completed && guestState.r[0] == 23U,
               "native Rand_S16Offset return value");
        Expect(runtime.Read32(kRandomStateAddress, &word) && word == expectedState,
               "native Rand_ZeroOne state update");
        Expect(runtime.Read32(kRandomStateAddress + 4U, &word) &&
                   word == expectedFloatBits,
               "native Rand_ZeroOne generated float word");

        Expect(runtime.Write32(kRandomStateAddress, initialState),
               "reset native RNG state");
        oot3d::recomp::a32::GuestState floatState;
        const auto floatCall = runtime.Call(0x003759D0U, floatState);
        const float expectedFloat =
            std::bit_cast<float>(expectedFloatBits) - 1.0f;
        Expect(floatCall.Completed &&
                   floatState.vfp[0] == std::bit_cast<uint32_t>(expectedFloat),
               "native Rand_ZeroOne VFP return value");
        Expect(runtime.Read32(kRandomStateAddress, &word) && word == expectedState,
               "direct native Rand_ZeroOne state update");
        Expect(runtime.SuccessfulCallCount() == 2 && runtime.FailedCallCount() == 0,
               "native A32 successful call diagnostics");

        oot3d::recomp::a32::GuestState missingState;
        const auto missing = runtime.Call(0x60000000U, missingState, 8);
        Expect(!missing.Completed && runtime.FailedCallCount() == 1,
               "missing A32 entrypoint must fail closed");

        std::cout << "oot3d_native_a32_execution_tests: ok\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "oot3d_native_a32_execution_tests: " << ex.what() << '\n';
        return 1;
    }
}
