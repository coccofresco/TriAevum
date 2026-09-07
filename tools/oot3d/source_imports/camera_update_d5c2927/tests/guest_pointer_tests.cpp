#include "oot3d/camera_update_owner.h"

#include <array>
#include <bit>
#include <cmath>
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

std::size_t CheckedOffset(std::uint32_t address, std::size_t size) {
    if (address < kBaseAddress ||
        size > gMemory.size() ||
        address - kBaseAddress > gMemory.size() - size) {
        throw std::runtime_error("test guest address is out of range");
    }
    return address - kBaseAddress;
}

} // namespace

namespace Oot3dSourceCameraUpdate {

const CameraUpdateLiterals& ActiveLiterals() {
    static const CameraUpdateLiterals literals;
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

GuestCallResult InvokeGuestWords(
    std::uint32_t, std::span<const std::uint32_t>,
    std::span<const std::uint32_t>,
    std::span<const GuestScratchArgument>) {
    throw std::runtime_error("unexpected guest call");
}

void InvokeDynamicCameraFunction(std::uint32_t, std::uint32_t) {
    throw std::runtime_error("unexpected dynamic guest call");
}

float TargetSqrt(float value) {
    return std::sqrt(value);
}

} // namespace Oot3dSourceCameraUpdate

int main() {
    using namespace Oot3dSourceCameraUpdate;
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

        const GuestPtr<std::uint16_t> flags(kBaseAddress + 16U);
        *flags = 0x1200U;
        *flags |= 0x0040U;
        *flags &= 0x0FFFU;
        Expect(
            static_cast<std::uint16_t>(*flags) == 0x0240U,
            "compound guest reference operations lost target bits");

        const GuestPtr<std::int32_t> counter(kBaseAddress + 20U);
        *counter = 7;
        (*counter)++;
        (*counter)--;
        Expect(
            static_cast<std::int32_t>(*counter) == 7,
            "guest postfix updates did not round-trip");

        const Oot3dActorVec3f expected{1.0F, -2.0F, 3.5F};
        WriteGuestPod(kBaseAddress + 32U, expected);
        const Oot3dActorVec3f actual =
            ReadGuestPod<Oot3dActorVec3f>(kBaseAddress + 32U);
        Expect(
            std::bit_cast<std::uint32_t>(actual.x) ==
                    std::bit_cast<std::uint32_t>(expected.x) &&
                std::bit_cast<std::uint32_t>(actual.y) ==
                    std::bit_cast<std::uint32_t>(expected.y) &&
                std::bit_cast<std::uint32_t>(actual.z) ==
                    std::bit_cast<std::uint32_t>(expected.z),
            "guest pod access changed Vec3f bits");

        std::cout << "camera_update_guest_pointer_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "camera_update_guest_pointer_tests: "
                  << exception.what() << '\n';
        return 1;
    }
}
