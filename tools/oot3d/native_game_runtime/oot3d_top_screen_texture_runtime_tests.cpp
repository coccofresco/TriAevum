#include "oot3d_top_screen_texture_runtime.h"

#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_top_screen_texture_runtime_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

std::uint64_t Fnv1a64(std::span<const std::uint8_t> bytes) {
    std::uint64_t value = 0xCBF29CE484222325ULL;
    for (const auto byte : bytes) {
        value ^= byte;
        value *= 0x100000001B3ULL;
    }
    return value;
}

template <typename T>
void Append(std::vector<std::uint8_t>& bytes, T value) {
    for (std::size_t shift = 0; shift < sizeof(T) * 8U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

int main() {
    using namespace Oot3dNativeGame;

    constexpr std::uint32_t kPauseTopSource = 0x00560000U;
    constexpr std::uint32_t kGuestSurface = 0x14000000U;
    constexpr std::uint32_t kPhysicalSurface = 0x20000000U;
    constexpr std::size_t kPayloadBytes = 512U * 256U * 2U;

    NativeA32Memory memory;
    std::string error;
    Require(memory.MapRegion(
                {"ui-state", 0x004F0000U, 0x00110000U, true, false, {}},
                &error) &&
                memory.MapRegion({"linear", kGuestSurface, kPayloadBytes,
                                  true, false, {}},
                                 &error),
            "could not map TopScreen texture runtime fixture");
    Require(memory.Write32(0x0055B498U, kPauseTopSource) &&
                memory.Write32(kPauseTopSource + 0x4CU, kGuestSurface),
            "could not seed pause-top native CTXB identity");

    std::vector<std::uint8_t> original(kPayloadBytes, 0U);
    std::vector<std::uint8_t> replacement(kPayloadBytes, 0xA5U);
    std::vector<std::uint8_t> packBytes{'O', '3', 'T', 'U'};
    Append<std::uint32_t>(packBytes, 1U);
    Append<std::uint32_t>(packBytes, 1U);
    Append<std::uint64_t>(packBytes, Fnv1a64(original));
    Append<std::uint64_t>(packBytes, Fnv1a64(replacement));
    Append<std::uint32_t>(packBytes,
                          static_cast<std::uint32_t>(replacement.size()));
    packBytes.insert(packBytes.end(), replacement.begin(), replacement.end());

    TopScreenTextureOverridePack pack;
    Require(pack.LoadBytes(packBytes, &error),
            "could not load TopScreen texture runtime fixture pack");
    const Oot3dPicaPhysicalMemoryView picaMemory(
        memory, {{kPhysicalSurface, kGuestSurface, kPayloadBytes}});
    Oot3dNativeUiLifecycleBridge bridge(memory);
    TopScreenTextureOverrideRuntime runtime(pack, picaMemory);
    runtime.Observe(bridge);

    Oot3dPicaTextureState texture;
    texture.PhysicalAddress = kPhysicalSurface;
    texture.Width = 512U;
    texture.Height = 256U;
    texture.Format = 4U;
    runtime.Transform(texture, original);
    Require(original == replacement,
            "bound native PICA snapshot did not receive CTXB replacement");
    const auto& stats = runtime.Stats();
    Require(stats.observe_calls == 1U && stats.identities_observed == 1U &&
                stats.target_bindings == 1U && stats.payload_checks == 1U &&
                stats.applied == 1U && stats.already_applied == 0U &&
                stats.no_match == 0U,
            "TopScreen texture runtime diagnostics are inconsistent");
    const auto targets = runtime.TargetStats();
    Require(targets.size() == 1U &&
                targets[0].physical_address == kPhysicalSurface &&
                targets[0].guest_surface_address == kGuestSurface &&
                targets[0].byte_count == kPayloadBytes &&
                targets[0].semantic_names.size() == 1U &&
                targets[0].semantic_names[0] ==
                    "oot3d/native/pause_shared/pause_top_page" &&
                targets[0].last_width == 512U &&
                targets[0].last_height == 256U &&
                targets[0].last_format == 4U &&
                targets[0].payload_checks == 1U &&
                targets[0].applied == 1U && targets[0].no_match == 0U,
            "per-target TopScreen texture diagnostics are inconsistent");

    std::cout << "oot3d_top_screen_texture_runtime_tests: ok\n";
    return 0;
}
