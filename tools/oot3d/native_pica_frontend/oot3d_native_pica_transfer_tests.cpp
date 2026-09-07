#include "oot3d_native_pica_transfer.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_pica_transfer_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

} // namespace

int main() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Require(memory.MapRegion({"source", 0x10000000U, 0x1000U, true,
                              false, {}}, &error) &&
                memory.MapRegion({"target", 0x10001000U, 0x1000U, true,
                                  false, {}}, &error),
            error);
    constexpr std::array<uint8_t, 2> redRgba4{0x0F, 0xF0};
    Require(memory.WriteBytes(0x10000000U, redRgba4),
            "cannot seed tiled RGBA4 source");
    const Oot3dNativeGame::Oot3dPicaDisplayTransfer transfer{
        0x10000000U, 0x10001000U, 0x00080008U, 0x00080008U,
        0x00004400U};
    Require(Oot3dNativeGame::ExecuteOot3dPicaDisplayTransfer(
                transfer, memory, &error),
            error);
    std::array<uint8_t, 2> output{};
    Require(memory.ReadBytes(0x10001000U, output) && output == redRgba4,
            "tiled-to-linear RGBA4 transfer changed the first texel");

    Require(Oot3dNativeGame::ExecuteOot3dPicaMemoryFill(
                {0x10001100U, 0x1000110CU, 0x00332211U, 0x0101U},
                memory, &error),
            error);
    std::array<uint8_t, 12> fill24{};
    Require(memory.ReadBytes(0x10001100U, fill24),
            "cannot read 24-bit memory fill result");
    for (size_t index = 0; index < fill24.size(); ++index) {
        Require(fill24[index] == std::array<uint8_t, 3>{0x11U, 0x22U, 0x33U}
                                     [index % 3U],
                "24-bit memory fill did not repeat its native byte pattern");
    }

    std::cout << "oot3d_native_pica_transfer_tests: ok\n";
    return 0;
}
