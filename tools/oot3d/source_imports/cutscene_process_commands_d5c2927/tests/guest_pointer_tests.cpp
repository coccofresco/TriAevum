#include "oot3d/cutscene_process_commands_owner.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint32_t kBaseAddress = 0x10000000U;
std::array<std::uint8_t, 64> gMemory{};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t CheckedOffset(std::uint32_t address, std::size_t size) {
    if (address < kBaseAddress ||
        size > gMemory.size() ||
        address - kBaseAddress > gMemory.size() - size) {
        throw std::runtime_error("test guest address is out of range");
    }
    return address - kBaseAddress;
}

} // namespace

namespace Oot3dSourceCutsceneProcessCommands {

const CutsceneProcessCommandsLiterals& ActiveLiterals() {
    static const CutsceneProcessCommandsLiterals literals;
    return literals;
}

void ReadGuestMemory(
    std::uint32_t address, void* destination, std::size_t size) {
    std::memcpy(destination, gMemory.data() + CheckedOffset(address, size), size);
}

void WriteGuestMemory(
    std::uint32_t address, const void* source, std::size_t size) {
    std::memcpy(gMemory.data() + CheckedOffset(address, size), source, size);
}

std::uint32_t InvokeGuestCoreWords(
    std::uint32_t, std::span<const std::uint32_t>) {
    throw std::runtime_error("unexpected guest call");
}

std::uint32_t InvokeGuestHardFloatWords(
    std::uint32_t, std::span<const std::uint32_t>,
    std::span<const std::uint32_t>) {
    throw std::runtime_error("unexpected hard-float guest call");
}

std::uint32_t InvokeGuestCoreWordsWithScratch(
    std::uint32_t, std::span<const std::uint32_t>,
    std::span<const GuestScratchArgument>) {
    throw std::runtime_error("unexpected scratch guest call");
}

float VectorSignedToFloat(std::int32_t value, std::uint32_t) {
    return static_cast<float>(value);
}

float VectorUnsignedToFloat(std::uint32_t value, std::uint32_t) {
    return static_cast<float>(value);
}

std::uint32_t VectorFloatToUnsigned(float value, std::uint32_t) {
    return static_cast<std::uint32_t>(value);
}

} // namespace Oot3dSourceCutsceneProcessCommands

int main() {
    using namespace Oot3dSourceCutsceneProcessCommands;
    try {
        gMemory.fill(0xA5U);

        const GuestPtr<std::uint32_t> words(kBaseAddress);
        words[0] = 0x11223344U;
        words[1] = 0x55667788U;
        Expect(
            static_cast<std::uint32_t>(words[0]) == 0x11223344U &&
                static_cast<std::uint32_t>(words[1]) == 0x55667788U,
            "scalar guest references did not round-trip");
        Expect(
            (words + 3).Address() == kBaseAddress + 12U,
            "guest pointer arithmetic did not use target element size");

        const GuestPtr<GuestPtr<std::uint32_t>> pointerCell(
            kBaseAddress + 16U);
        *pointerCell = words + 1;
        Expect(
            static_cast<GuestPtr<std::uint32_t>>(*pointerCell).Address() ==
                kBaseAddress + 4U,
            "nested guest pointer did not round-trip");
        Expect(
            gMemory[20] == 0xA5U && gMemory[21] == 0xA5U &&
                gMemory[22] == 0xA5U && gMemory[23] == 0xA5U,
            "guest pointer write escaped its four-byte target field");

        const GuestPtr<float> scalar(kBaseAddress + 24U);
        *scalar = -3.5F;
        Expect(
            std::bit_cast<std::uint32_t>(static_cast<float>(*scalar)) ==
                std::bit_cast<std::uint32_t>(-3.5F),
            "float guest reference changed target bits");

        const GuestPtr<std::int16_t> signedValue(kBaseAddress + 28U);
        *signedValue = -7;
        Expect(
            static_cast<std::int16_t>(*signedValue) == -7 &&
                CoreWord(*signedValue) == 0xFFFFFFF9U,
            "signed target value did not preserve AAPCS word encoding");

        std::cout << "cutscene_process_commands_guest_pointer_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr
            << "cutscene_process_commands_guest_pointer_tests: "
            << exception.what() << '\n';
        return 1;
    }
}
