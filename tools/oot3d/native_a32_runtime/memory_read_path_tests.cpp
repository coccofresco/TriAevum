#include "oot3d_native_a32_memory.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

using Oot3dNativeGame::NativeA32Memory;

static void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

template <typename T>
static void Compare(const NativeA32Memory& memory, uint32_t address) {
    T actual = std::numeric_limits<T>::max();
    std::array<uint8_t, sizeof(T)> expected{};
    uint32_t fault = 0xDEADBEEFU;
    const bool checked = memory.ReadBytes(address, expected);
    const bool fast = memory.ReadFast(address, &actual, &fault);
    Require(fast == checked);
    if (fast) {
        T value{};
        std::memcpy(&value, expected.data(), sizeof(T));
        Require(actual == value && fault == 0xDEADBEEFU);
    } else {
        Require(actual == std::numeric_limits<T>::max());
        Require(fault == address);
    }
}

extern "C"
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
bool ProbeMemoryRead32(
    const NativeA32Memory& memory, uint32_t address, uint32_t* result) {
    return memory.ReadFast(address, result);
}

int main() {
    NativeA32Memory memory;
    std::array<uint8_t, 0x3000> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<uint8_t>(i * 31U + 7U);
    }
    Require(memory.MapRegion({"ram", 0x10000, bytes.size(), true, false, bytes}));
    Require(memory.MapRegion({"rom", 0x20000, 0x1000, false, false,
                              std::span(bytes).first(0x1000)}));
    Require(memory.MapRegion({"partial", 0x30003, 17, true, false,
                              std::span(bytes).first(17)}));
    const auto fingerprint = memory.ContentFingerprint();
    const auto generation = memory.WriteGeneration();
    for (bool tracing : {false, true}) {
        memory.EnableWriteTraceFingerprint(tracing);
        const auto trace = memory.WriteTraceFingerprint();
        for (uint32_t base : {0x10000U, 0x11000U, 0x12000U, 0x13000U,
                              0x20000U, 0x21000U, 0x30003U, 0x30014U, 0U}) {
            for (int offset = -9; offset < 24; ++offset) {
                const uint32_t address = base + static_cast<uint32_t>(offset);
                Compare<uint8_t>(memory, address);
                Compare<uint16_t>(memory, address);
                Compare<uint32_t>(memory, address);
                Compare<uint64_t>(memory, address);
            }
        }
        uint32_t fault = 0xABCDEF01U;
        Require(!memory.ReadFast<uint32_t>(0x10000, nullptr, &fault));
        Require(fault == 0xABCDEF01U);
        Require(memory.FastReadMismatchCount() == 0);
        Require(memory.ContentFingerprint() == fingerprint);
        Require(memory.WriteGeneration() == generation);
        Require(memory.WriteTraceFingerprint() == trace);
    }
    std::puts("Memory read paths: 2376 comparisons passed");
}
