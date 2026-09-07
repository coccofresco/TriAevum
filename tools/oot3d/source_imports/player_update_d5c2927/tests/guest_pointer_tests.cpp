#include "oot3d/player_update_owner.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint32_t kBaseAddress = 0x10000000U;
std::array<std::uint8_t, 128> gMemory{};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t CheckedOffset(
    std::uint32_t address, std::size_t size) {
    if (address < kBaseAddress || size > gMemory.size() ||
        address - kBaseAddress > gMemory.size() - size) {
        throw std::runtime_error(
            "test guest address is out of range");
    }
    return address - kBaseAddress;
}

} // namespace

namespace Oot3dSourcePlayerUpdate {

const PlayerUpdateLiterals& ActiveLiterals() {
    static const PlayerUpdateLiterals literals;
    return literals;
}

void ReadGuestMemory(
    std::uint32_t address, void* destination, std::size_t size) {
    std::memcpy(
        destination, gMemory.data() + CheckedOffset(address, size),
        size);
}

void WriteGuestMemory(
    std::uint32_t address, const void* source, std::size_t size) {
    std::memcpy(
        gMemory.data() + CheckedOffset(address, size), source, size);
}

} // namespace Oot3dSourcePlayerUpdate

int main() {
    using namespace Oot3dSourcePlayerUpdate;
    try {
        gMemory.fill(0xA5U);

        WriteGuestPod<std::uint32_t>(
            kBaseAddress, 0x11223344U);
        WriteGuestPod<std::int16_t>(
            kBaseAddress + 4U, -1234);
        WriteGuestPod<float>(
            kBaseAddress + 8U,
            std::bit_cast<float>(0xC1480000U));

        Expect(
            ReadGuestPod<std::uint32_t>(kBaseAddress) ==
                0x11223344U,
            "32-bit guest value did not round-trip");
        Expect(
            ReadGuestPod<std::int16_t>(kBaseAddress + 4U) ==
                -1234,
            "signed 16-bit guest value did not round-trip");
        Expect(
            std::bit_cast<std::uint32_t>(
                ReadGuestPod<float>(kBaseAddress + 8U)) ==
                0xC1480000U,
            "float guest bits did not round-trip");

        std::array<std::uint32_t, 12> expected{};
        for (std::size_t index = 0U; index < expected.size();
             ++index) {
            expected[index] =
                0x10000000U + static_cast<std::uint32_t>(index);
        }
        WriteGuestMemory(
            kBaseAddress + 32U, expected.data(), sizeof(expected));
        std::array<std::uint32_t, 12> actual{};
        ReadGuestMemory(
            kBaseAddress + 32U, actual.data(), sizeof(actual));
        Expect(
            actual == expected,
            "48-byte native input packet did not round-trip");

        std::cout << "player_update_guest_pointer_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "player_update_guest_pointer_tests: "
                  << exception.what() << '\n';
        return 1;
    }
}
