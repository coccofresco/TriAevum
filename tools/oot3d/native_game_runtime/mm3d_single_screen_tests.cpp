#include "core.h"
#include "mm3d_restoration.h"
#include "mm3d_single_screen.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
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

constexpr std::uint32_t kDamagePc = 0x0021E34CU;
constexpr std::uint32_t kSpawnPc = 0x0022053CU;

// The vanilla v1.1 code of dekui's two difficulty sites, nothing else.
WordMemory DifficultyCode() {
    WordMemory memory;
    for (std::size_t i = 0; i < Difficulty::HealthCode.size(); ++i) {
        memory.Words[kDamagePc - 0x38U + 4U * i] = Difficulty::HealthCode[i];
    }
    for (std::size_t i = 0; i < Difficulty::SpawnCode.size(); ++i) {
        memory.Words[kSpawnPc + 4U * i] = Difficulty::SpawnCode[i];
    }
    return memory;
}

struct Fixture {
    Mm3dSingleScreen SingleScreen;
    Mm3dGuestPatches Patches;
    WordMemory Memory = DifficultyCode();
    Fixture() {
        SingleScreen.Register(Patches);
        Require(Patches.Verify(Memory) == 2U, "only the difficulty sites should apply");
    }
};

std::uint32_t Damage(Fixture& fixture, std::uint32_t health, std::int32_t change) {
    a32::GuestState state;
    state.r[2] = health;
    state.r[4] = static_cast<std::uint32_t>(change);
    a32::ExecutionResult result;
    Require(Mm3dGuestPatches::Execute(kDamagePc, state, fixture.Memory, &result, &fixture.Patches) &&
                result.pc == kDamagePc + 4U,
            "the damage site did not replace its ADD");
    return state.r[2];
}

// Through dekui's own substitution chain and SVC dispatch.
void TestDamage() {
    Fixture fixture;
    Difficulty::damage_multiplier = 1;
    Require(Damage(fixture, 0x30U, -8) == 0x28U, "multiplier 1 changed the damage");
    Difficulty::damage_multiplier = 4;
    Require(Damage(fixture, 0x30U, -8) == 0x10U, "the loss was not multiplied");
    Require(Damage(fixture, 0x10U, -8) == 0U, "an amplified loss wrapped around");
    Require(Damage(fixture, 0x10U, 0x10) == 0x20U, "healing was multiplied");
    Difficulty::damage_multiplier = 1;
}

void TestDrops() {
    Fixture fixture;
    constexpr std::uint32_t kSp = 0x0FFFFF00U;
    const auto spawn = [&](std::uint32_t actor, std::uint32_t params, a32::GuestState* out) {
        a32::GuestState state;
        state.r[2] = actor;
        state.r[4] = 0x44U;
        state.r[13] = kSp;
        state.r[14] = 0x00123456U;
        fixture.Memory.Words[kSp + 8U] = params;
        a32::ExecutionResult result;
        Require(Mm3dGuestPatches::Execute(kSpawnPc, state, fixture.Memory, &result, &fixture.Patches),
                "the spawn site did not run");
        *out = state;
        return result.pc;
    };
    a32::GuestState state;
    Require(spawn(0xE, 3, &state) == kSpawnPc + 4U && state.r[13] == kSp - 16U &&
                fixture.Memory.Words[kSp - 16U] == 0x44U,
            "an allowed drop did not replay the native prologue");
    Difficulty::disable_heart_drops = true;
    Require(spawn(0xE, 3, &state) == 0x00123456U && state.r[0] == 0U, "a disabled heart drop was spawned");
    Difficulty::disable_heart_drops = false;
}

// A changed guard word keeps dekui from replacing the instruction.
void TestGuard() {
    Mm3dSingleScreen singleScreen;
    Mm3dGuestPatches patches;
    singleScreen.Register(patches);
    auto memory = DifficultyCode();
    memory.Words[kDamagePc - 0x38U] ^= 1U;
    Require(patches.Verify(memory) == 1U, "a site over different code was enabled");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--block-boundaries") {
        // The A32 registry input: every candidate PC and the instruction
        // after it start a block (tools/mm3d/a32/guest_patch_entries.py).
        // dekui's sites and Project Restoration's hooks.
        Mm3dSingleScreen singleScreen;
        Mm3dGuestCalls calls;
        Mm3dRestorationPort restoration(calls);
        Mm3dGuestPatches patches;
        singleScreen.Register(patches);
        restoration.Register(patches);
        // With the port's own manifest: its calls' native entries and
        // continuations.
        std::set<std::uint32_t> boundaries;
        for (const auto pc : patches.BlockBoundaries()) {
            boundaries.insert(pc);
        }
        for (const auto pc : Mm3dRestorationPort::RegistryBoundaries()) {
            boundaries.insert(pc);
        }
        for (const auto pc : boundaries) {
            std::cout << "0x" << std::hex << std::uppercase << pc << '\n';
        }
        return 0;
    }
    TestDamage();
    TestDrops();
    TestGuard();
    std::cout << "mm3d_single_screen_tests: ok\n";
    return 0;
}
