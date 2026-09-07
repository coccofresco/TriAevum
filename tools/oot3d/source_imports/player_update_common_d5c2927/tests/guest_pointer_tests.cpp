#include "oot3d/player_update_common_owner.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint32_t kBaseAddress = 0x10000000U;
std::array<std::uint8_t, 256> gMemory{};

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

namespace Oot3dSourcePlayerUpdateCommon {

const PlayerUpdateCommonLiterals& ActiveLiterals() {
    static const PlayerUpdateCommonLiterals literals;
    return literals;
}

std::uint32_t OwnerLocalAddress(std::uint32_t offset) {
    return kBaseAddress + 128U + offset;
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

GuestCallResult InvokeGuestWords(
    std::uint32_t, std::span<const std::uint32_t>,
    std::span<const std::uint32_t>,
    std::span<const std::uint32_t>) {
    throw std::runtime_error("unexpected guest call");
}

void InvokeDynamicPlayerAction(
    std::uint32_t, std::uint32_t, std::uint32_t) {
    throw std::runtime_error("unexpected dynamic guest call");
}

float TargetSignedToFloat(std::int32_t value, int) {
    return static_cast<float>(value);
}

float TargetUnsignedToFloat(std::uint32_t value, int) {
    return static_cast<float>(value);
}

float TargetSqrt(float value) {
    return value;
}

} // namespace Oot3dSourcePlayerUpdateCommon

int main() {
    using namespace Oot3dSourcePlayerUpdateCommon;
    try {
        gMemory.fill(0xA5U);

        const GuestPtr<std::uint32_t> words(kBaseAddress);
        words[0] = 0x11223344U;
        words[1] = 0x55667788U;
        Expect(
            static_cast<std::uint32_t>(words[0]) == 0x11223344U &&
                static_cast<std::uint32_t>(words[1]) == 0x55667788U,
            "guest pointer values did not round-trip");
        Expect(
            (words + 3).Address() == kBaseAddress + 12U,
            "guest pointer arithmetic did not use target width");

        const GuestPtr<short> halfwords(kBaseAddress);
        Expect(
            (halfwords + 0x1C).Address() == kBaseAddress + 0x38U,
            "guest short pointer arithmetic did not preserve ARM scaling");

        words[4] = kBaseAddress + 80U;
        WriteGuestPod<std::uint32_t>(
            kBaseAddress + 80U, 0xA1B2C3D4U);
        Expect(
            static_cast<std::uint32_t>(
                GuestIndirectRef<std::uint32_t>(
                    kBaseAddress + 16U)) == 0xA1B2C3D4U,
            "same-type guest double dereference copied its proxy address");
        GuestIndirectRef<std::uint32_t>(
            kBaseAddress + 16U) = 0x10203040U;
        Expect(
            ReadGuestPod<std::uint32_t>(
                kBaseAddress + 80U) == 0x10203040U,
            "guest double dereference did not write through its pointer");

        GuestLocal<float> localFloat(OwnerLocalAddress(0x0CU));
        localFloat = std::bit_cast<float>(0xC1480000U);
        Expect(
            std::bit_cast<std::uint32_t>(
                ReadGuestPod<float>(OwnerLocalAddress(0x0CU))) ==
                0xC1480000U,
            "guest float local did not occupy the owner frame");
        Expect(
            (&localFloat).Address() == OwnerLocalAddress(0x0CU),
            "guest local address did not preserve its target offset");

        GuestLocal<GuestPtr<byte>> localPointer(
            OwnerLocalAddress(0x10U));
        localPointer = GuestPtr<byte>(kBaseAddress + 64U);
        const GuestPtr<byte> pointerValue =
            static_cast<GuestPtr<byte>>(localPointer);
        Expect(
            pointerValue.Address() == kBaseAddress + 64U &&
                static_cast<short>(localPointer) ==
                    static_cast<short>(kBaseAddress + 64U),
            "guest pointer local lost its target address bits");

        GuestCallBuilder arguments;
        arguments.Push(std::uint32_t{1U});
        arguments.Push(2.0F);
        arguments.Push(std::int16_t{-3});
        arguments.Push(4.0F);
        arguments.Push(GuestPtr<void>(0x55667788U));
        arguments.Push(std::uint32_t{5U});
        arguments.Push(std::uint32_t{6U});
        Expect(
            arguments.CoreSpan().size() == 4U &&
                arguments.CoreSpan()[0] == 1U &&
                arguments.CoreSpan()[1] == 0xFFFFFFFDU &&
                arguments.CoreSpan()[2] == 0x55667788U &&
                arguments.CoreSpan()[3] == 5U,
            "mixed AAPCS arguments did not fill r0-r3");
        Expect(
            arguments.VfpSpan().size() == 2U &&
                arguments.VfpSpan()[0] ==
                    std::bit_cast<std::uint32_t>(2.0F) &&
                arguments.VfpSpan()[1] ==
                    std::bit_cast<std::uint32_t>(4.0F),
            "hard-float arguments did not fill s0-s15 independently");
        Expect(
            arguments.StackSpan().size() == 1U &&
                arguments.StackSpan()[0] == 6U,
            "core overflow did not begin at the owner stack pointer");

        std::cout
            << "player_update_common_guest_pointer_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr
            << "player_update_common_guest_pointer_tests: "
            << exception.what() << '\n';
        return 1;
    }
}
