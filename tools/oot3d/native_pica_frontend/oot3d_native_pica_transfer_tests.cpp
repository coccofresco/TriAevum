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

    using namespace Oot3dNativeGame;
    Oot3dPicaTextureCopyPlan plan;
    Oot3dGspCommandPacket copy{4U, {0x10000000U, 0x10001000U, 79U,
                                  0x00010002U, 0x00020001U, 12U, 0U}};
    Require(BuildOot3dPicaTextureCopyPlan(copy, plan, &error), error);
    Require(plan.Size == 64U && plan.Spans.size() == 4U,
            "texture copy must align size down and split both strides");
    std::array<uint8_t, 128> source{}, target{};
    for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i);
    target.fill(0xDDU);
    Require(memory.WriteBytes(0x10000000U, source) &&
                memory.WriteBytes(0x10001000U, target), "cannot seed raw copy");
    Require(ExecuteOot3dPicaTextureCopy(plan, memory, &error), error);
    std::array<uint8_t, 160> actual{};
    Require(memory.ReadBytes(0x10001000U, actual), "cannot read raw copy");
    for (size_t row = 0; row < 4; ++row) {
        for (size_t x = 0; x < 16; ++x) {
            const size_t src = (row / 2) * 48 + (row % 2) * 16 + x;
            Require(actual[row * 48 + x] == source[src], "raw copy stride mismatch");
        }
    }
    for (size_t x = 16; x < 48; ++x)
        Require(actual[x] == 0xDDU, "raw copy overwrote destination gap");

    copy.Parameters[3] = copy.Parameters[4] = 0U;
    Require(BuildOot3dPicaTextureCopyPlan(copy, plan, &error) &&
                plan.Spans.size() == 1U && plan.Spans[0].Size == 64U,
            "zero gaps must ignore zero widths");
    copy.Parameters[3] = 0x00010000U;
    Require(!BuildOot3dPicaTextureCopyPlan(copy, plan, &error) && plan.Spans.empty(),
            "zero width with nonzero gap must fail without stale plan");
    copy.Parameters[3] = 0;
    copy.Parameters[0] = 0xFFFFFFF0U;
    Require(!BuildOot3dPicaTextureCopyPlan(copy, plan, &error), "address wrap accepted");

    // Captured native packet from #45: raw data, not a scene-specific path.
    copy = {0x01000104U, {0x1F2447C0U, 0x1F3E3300U, 0xBB800U,
                          0x3C0U, 0x4003C0U, 12U, 0U}};
    Require(BuildOot3dPicaTextureCopyPlan(copy, plan, &error), error);
    Require(plan.Spans.size() == 50U && plan.Size == 768000U &&
                plan.Spans[1].InputAddress == 0x1F2483C0U &&
                plan.Spans[1].OutputAddress == 0x1F3E7300U,
            "captured TextureCopy row spans decoded incorrectly");

    // A bad later row must not leave a partially copied destination.
    plan = {32U, {{0x10000000U, 0x10001000U, 16U},
                  {0xDEAD0000U, 0x10001010U, 16U}}};
    Require(memory.WriteBytes(0x10001000U, target), "cannot reset raw copy target");
    Require(!ExecuteOot3dPicaTextureCopy(plan, memory, &error), "unmapped row accepted");
    Require(memory.ReadBytes(0x10001000U, source) && source == target,
            "invalid copy partially changed destination");

    // Independent byte-index oracle: row/column mapping, not a second copy
    // of the planner's cursor algorithm. Exercise mismatched partial rows.
    for (uint32_t iw = 0; iw <= 4; ++iw) {
        for (uint32_t ow = 0; ow <= 4; ++ow) {
            for (uint32_t ig = 0; ig <= 3; ++ig) {
                for (uint32_t og = 0; og <= 3; ++og) {
                    for (uint32_t blocks = 1; blocks <= 9; ++blocks) {
                        copy = {4U, {0x10000000U, 0x10001000U, blocks * 16U + 7U,
                                      iw | (ig << 16U), ow | (og << 16U), 12U, 0U}};
                        const bool valid = (iw != 0 || ig == 0) && (ow != 0 || og == 0);
                        Require(BuildOot3dPicaTextureCopyPlan(copy, plan, &error) == valid,
                                "raw copy validity disagrees with width/gap contract");
                        if (!valid) continue;
                        uint32_t copied = 0;
                        for (const auto& span : plan.Spans) {
                            for (uint32_t b = 0; b < span.Size; ++b) {
                                const uint32_t k = copied + b;
                                const uint32_t input = k + (ig == 0 ? 0 : k / (iw * 16U) * ig * 16U);
                                const uint32_t output = k + (og == 0 ? 0 : k / (ow * 16U) * og * 16U);
                                Require(span.InputAddress + b == 0x10000000U + input &&
                                            span.OutputAddress + b == 0x10001000U + output,
                                        "raw copy plan disagrees with byte-index oracle");
                            }
                            copied += span.Size;
                        }
                        Require(copied == blocks * 16U, "raw copy lost or duplicated bytes");
                    }
                }
            }
        }
    }

    std::cout << "oot3d_native_pica_transfer_tests: ok\n";
    return 0;
}
