#include "mm3d_single_screen.h"

#include "dekui_guest.h"

namespace Oot3dNativeGame {

namespace a32 = oot3d::recomp::a32;

namespace {

template <std::size_t N>
std::vector<std::pair<std::uint32_t, std::uint32_t>> Code(std::uint32_t address,
                                                          const std::array<std::uint32_t, N>& words) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> guard;
    guard.reserve(N);
    for (std::size_t index = 0; index < N; ++index) {
        guard.emplace_back(address + static_cast<std::uint32_t>(4U * index), words[index]);
    }
    return guard;
}

void RegisterDifficulty(Mm3dGuestPatches& patches, const Mm3dSingleScreenSettings& settings) {
    namespace Difficulty = Mm3dDekui::Difficulty;
    // The damage multiplier replays ADD r2, r2, r4 in the health-change
    // routine with the loss multiplied.
    patches.Add({Difficulty::kDamagePc,
                 Code(Difficulty::kHealthCodeAddress, Difficulty::kHealthCode),
                 "dekui.difficulty.damage",
                 [&settings](a32::GuestState& state, a32::MemoryBus&) -> std::optional<std::uint32_t> {
                     state.r[2] = Difficulty::ApplyDamage(state.r[2], state.r[4],
                                                          settings.Difficulty.DamageMultiplier);
                     return Difficulty::kDamagePc + 4U;
                 }});
    // Disabled drops: the Actor_Spawn wrapper returns no actor for them.
    patches.Add({Difficulty::kSpawnPc,
                 Code(Difficulty::kSpawnPc, Difficulty::kSpawnCode),
                 "dekui.difficulty.spawn",
                 [&settings](a32::GuestState& state, a32::MemoryBus& memory) -> std::optional<std::uint32_t> {
                     Mm3dDekui::GuestMemory guest(memory);
                     return Difficulty::Spawn(guest, state.r, settings.Difficulty);
                 }});
}

} // namespace

void RegisterMm3dSingleScreenSites(Mm3dGuestPatches& patches,
                                   const Mm3dSingleScreenSettings& settings) {
    RegisterDifficulty(patches, settings);
}

} // namespace Oot3dNativeGame
