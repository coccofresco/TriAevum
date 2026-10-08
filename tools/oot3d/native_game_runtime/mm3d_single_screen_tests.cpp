#include "mm3d_single_screen.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <string_view>

namespace {

using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;
namespace Difficulty = Mm3dDekui::Difficulty;

void Require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "mm3d_single_screen_tests: " << message << '\n';
        std::exit(1);
    }
}

class WordMemory final : public a32::MemoryBus {
  public:
    std::map<std::uint32_t, std::uint32_t> Words;
    bool Read32(std::uint32_t address, std::uint32_t* value) override {
        const auto found = Words.find(address);
        if (found == Words.end()) {
            return false;
        }
        *value = found->second;
        return true;
    }
    bool Write32(std::uint32_t address, std::uint32_t value) override {
        Words[address] = value;
        return true;
    }
};

// The vanilla v1.1 code the difficulty sites were written against.
WordMemory VanillaCode() {
    WordMemory memory;
    for (std::size_t i = 0; i < Difficulty::kHealthCode.size(); ++i) {
        memory.Words[Difficulty::kHealthCodeAddress + 4U * i] = Difficulty::kHealthCode[i];
    }
    for (std::size_t i = 0; i < Difficulty::kSpawnCode.size(); ++i) {
        memory.Words[Difficulty::kSpawnPc + 4U * i] = Difficulty::kSpawnCode[i];
    }
    return memory;
}

uint32_t Damage(Mm3dGuestPatches& patches, WordMemory& memory, std::uint32_t health,
                std::int32_t change) {
    a32::GuestState state;
    state.r[2] = health;
    state.r[4] = static_cast<std::uint32_t>(change);
    a32::ExecutionResult result;
    Require(Mm3dGuestPatches::Execute(Difficulty::kDamagePc, state, memory, &result, &patches) &&
                result.pc == Difficulty::kDamagePc + 4U,
            "the damage site did not replace its ADD");
    return state.r[2];
}

void TestDamage() {
    Mm3dSingleScreenSettings settings;
    Mm3dGuestPatches patches;
    RegisterMm3dSingleScreenSites(patches, settings);
    auto memory = VanillaCode();
    Require(patches.Verify(memory) == 2U, "the difficulty sites do not match vanilla v1.1");
    // 0x30 = three hearts; a loss of half a heart (8).
    Require(Damage(patches, memory, 0x30U, -8) == 0x28U, "multiplier 1 changed the damage");
    settings.Difficulty.DamageMultiplier = 4;
    Require(Damage(patches, memory, 0x30U, -8) == 0x10U, "the loss was not multiplied");
    Require(Damage(patches, memory, 0x10U, -8) == 0U, "an amplified loss wrapped around");
    Require(Damage(patches, memory, 0x10U, 0x10) == 0x20U, "healing was multiplied");
}

void TestDrops() {
    Mm3dSingleScreenSettings settings;
    Mm3dGuestPatches patches;
    RegisterMm3dSingleScreenSites(patches, settings);
    auto memory = VanillaCode();
    (void)patches.Verify(memory);
    constexpr std::uint32_t kSp = 0x0FFFFF00U;
    const auto spawn = [&](std::uint32_t actor, std::uint32_t params, a32::GuestState* out) {
        a32::GuestState state;
        state.r[2] = actor;
        state.r[4] = 0x44U;
        state.r[5] = 0x55U;
        state.r[6] = 0x66U;
        state.r[13] = kSp;
        state.r[14] = 0x00123456U;
        memory.Words[kSp + 8U] = params;
        a32::ExecutionResult result;
        Require(Mm3dGuestPatches::Execute(Difficulty::kSpawnPc, state, memory, &result, &patches),
                "the spawn site did not run");
        *out = state;
        return result.pc;
    };
    a32::GuestState state;
    // A recovery heart with drops enabled spawns as before (STMFD replayed).
    Require(spawn(0xE, 3, &state) == Difficulty::kSpawnPc + 4U && state.r[13] == kSp - 16U &&
                memory.Words[kSp - 16U] == 0x44U && memory.Words[kSp - 4U] == 0x00123456U,
            "an allowed drop did not replay the native prologue");
    settings.Difficulty.DisableHeartDrops = true;
    Require(spawn(0xE, 3, &state) == 0x00123456U && state.r[0] == 0U && state.r[13] == kSp,
            "a disabled heart drop was spawned");
    Require(spawn(0xE, 0x13, &state) == Difficulty::kSpawnPc + 4U, "a rupee was suppressed");
    settings.Difficulty.DisableFairyDrops = true;
    Require(spawn(0x10, 0x8002, &state) == 0x00123456U, "a healing fairy drop was spawned");
    Require(spawn(0x10, 0x0001, &state) == Difficulty::kSpawnPc + 4U,
            "a fairy in another role was suppressed");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--block-boundaries") {
        // The A32 registry input: every site and the instruction after it
        // start a block (tools/mm3d/a32/guest_patch_entries.py).
        Mm3dSingleScreenSettings settings;
        Mm3dGuestPatches patches;
        RegisterMm3dSingleScreenSites(patches, settings);
        for (const auto pc : patches.BlockBoundaries()) {
            std::cout << "0x" << std::hex << std::uppercase << pc << '\n';
        }
        return 0;
    }
    TestDamage();
    TestDrops();
    std::cout << "mm3d_single_screen_tests: ok\n";
    return 0;
}
