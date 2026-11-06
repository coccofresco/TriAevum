#include "mm3d_lighting_hook_sites.h"
#include "core.h"
#include "mm3d_restoration.h"
#include "mm3d_single_screen.h"
#include "mm3d_layout_stack_probe.h"

#include <cstdio>
#include <string>
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
        // The health window guards two sites (damage ADD and, since 6e36dfb4,
        // the defense reduction 16 bytes before it), plus the drop spawn.
        Require(Patches.Verify(Memory) == 3U, "only the difficulty sites should apply");
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
    // Both health-window sites (damage and defense) stay off; the spawn remains.
    Require(patches.Verify(memory) == 1U, "a site over different code was enabled");
}

// The developer stack probe measures the draw's depth below the lowered SP
// and leaves every word the draw did not write as it was.
void TestStackProbe() {
    std::FILE* out = std::tmpfile();
    Require(out != nullptr, "tmpfile");
    Mm3dLayoutStackProbe probe(out);
    WordMemory memory;
    constexpr std::uint32_t kLow = 0x0FFC0000U, kHigh = 0x10000000U;
    for (std::uint32_t at = kLow; at < kHigh; at += 4U) {
        memory.Words[at] = at ^ 0x1234U;
    }
    const auto before = memory.Words;
    a32::GuestState state;
    state.r[13] = 0x0FFFE000U;
    state.r[4] = 0;
    probe.BeforeRoot(state, memory);
    state.r[13] = 0x0FFFE000U - 0x1200U; // a committed 0x1200 scratch
    probe.AfterRoot(state, memory, 0x0FFFE000U);
    const std::uint32_t lowered = state.r[13];
    Require(memory.Words[lowered - 4U] == Mm3dLayoutStackProbe::Pattern(lowered - 4U),
            "the probe did not fill below the lowered SP");
    Require(memory.Words[lowered] == before.at(lowered), "the probe touched the scratch");
    // The draw writes two words, the deeper 0x300 bytes under the lowered SP.
    memory.Words[lowered - 0x300U] = 7U;
    memory.Words[lowered - 0x10U] = 9U;
    probe.BeforeEnd(state, memory);
    for (const auto& [at, value] : before) {
        const std::uint32_t expected =
            at == lowered - 0x300U ? 7U : at == lowered - 0x10U ? 9U : value;
        Require(memory.Words.at(at) == expected, "the probe changed a word the draw did not write");
    }
    std::rewind(out);
    char line[512]{};
    std::string text;
    while (std::fgets(line, sizeof(line), out) != nullptr) {
        text += line;
    }
    Require(text.find("hib=1") != std::string::npos, "the probe did not record the commit");
    Require(text.find("stack_low=0FFC0000 stack_high=10000000") != std::string::npos,
            "the probe did not find the mapped stack range");
    Require(text.find("draw_bytes=300 ") != std::string::npos, "the probe missed the draw depth");
    Require(text.find("headroom=3CE00 ") != std::string::npos, "the probe headroom is wrong");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--block-boundaries") {
        // The A32 registry input: every candidate PC and the instruction
        // after it start a block (titles/mm3d/a32/guest_patch_entries.py).
        // dekui's sites and Project Restoration's hooks.
        Mm3dSingleScreen singleScreen;
        Mm3dGuestCalls calls;
        Mm3dRestorationPort restoration(calls);
        Mm3dGuestPatches patches;
        singleScreen.Register(patches);
        RegisterMm3dLightingHooks(patches);
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
    TestStackProbe();
    std::cout << "mm3d_single_screen_tests: ok\n";
    return 0;
}
