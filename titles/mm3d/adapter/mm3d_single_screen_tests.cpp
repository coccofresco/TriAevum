#include "mm3d_lighting_hook_sites.h"
#include "core.h"
#include "mm3d_restoration.h"
#include "mm3d_single_screen.h"
#include "../dekui/button_glyph_emission.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
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

// State + settings: the MM settings a state carries are dekui's live
// settings (damage multiplier among them), applied through dekui's own
// parser and live apply; damaged text changes nothing.
void TestStateSettings() {
    Require(Mm3dSingleScreen::LoadSettings({}), "dekui defaults did not load");
    Mm3dSingleScreen::SetDamageMultiplier(4);
    Require(Difficulty::damage_multiplier.load() == 4, "damage multiplier not live");
    const auto captured = Mm3dSingleScreen::ActiveSettingsJson();
    Require(captured.find("\"damage_multiplier\": 4") != std::string::npos,
            "captured MM settings lost the active damage multiplier");
    Require(Mm3dSingleScreen::LoadSettings({}), "dekui defaults did not reload");
    Require(Difficulty::damage_multiplier.load() == 1, "defaults kept the old multiplier");
    std::string error;
    Require(Mm3dSingleScreen::ApplySettingsJson(captured, &error), "captured MM settings rejected: " + error);
    Require(Difficulty::damage_multiplier.load() == 4, "restored MM settings did not reach the live owner");
    Require(Mm3dSingleScreen::ActiveSettingsJson() == captured, "restored settings are not the active ones");
    for (const auto* damaged : {"", "{", "[]", "{\"version\":2}", "null"}) {
        error.clear();
        Require(!Mm3dSingleScreen::ApplySettingsJson(damaged, &error) && !error.empty(),
                "damaged MM settings accepted");
        Require(Difficulty::damage_multiplier.load() == 4 && Mm3dSingleScreen::ActiveSettingsJson() == captured,
                "damaged MM settings mutated the live owner");
    }
    Mm3dSingleScreen::SetDamageMultiplier(1);
}

// A restored state resets dekui's host state but keeps its settings.
void TestRestoreReset() {
    Mm3dSingleScreen singleScreen;
    Mm3dSingleScreen::SetDamageMultiplier(3);
    const auto settings = Mm3dSingleScreen::ActiveSettingsJson();
    const auto before = singleScreen.HostStateGeneration();
    singleScreen.ResetHostState();
    Require(singleScreen.HostStateGeneration() == before + 1, "restore did not reset dekui host state");
    Require(Mm3dSingleScreen::ActiveSettingsJson() == settings && Difficulty::damage_multiplier.load() == 3,
            "restore reset changed dekui settings");
    Mm3dSingleScreen::SetDamageMultiplier(1);
}

void TestInlineGlyphAbi() {
    namespace G=Mm3dDekui::ButtonGlyphEmission;
    WordMemory memory;
    for(auto [pc,word]:G::Guards)memory.Words[pc]=word;
    constexpr uint32_t node=0x10000000,groups=0x10060000,entries=groups+0x100,
        resource=groups+0x200,descriptor=groups+0x400,record=groups+0x500,pixels=0x11000000;
    memory.Words[Mm3dDekui::NotebookPrompt::RegistryLiteral]=Mm3dDekui::NotebookPrompt::Registry;
    memory.Words[Mm3dDekui::NotebookPrompt::Registry+0x28]=groups;
    memory.Words[Mm3dDekui::NotebookPrompt::Registry+0x2c]=entries;
    memory.Words[groups]=12;memory.Words[entries+44]=resource;memory.Words[resource+4]=11<<16;
    memory.Words[resource+12+0x4c]=descriptor;memory.Words[descriptor+0x20]=pixels;
    memory.Words[record]=pixels;memory.Words[record+0xc]=256;memory.Words[record+0x10]=128;
    memory.Words[record+0x38]=~0u;memory.Words[0x006a3a88]=0x14001000;
    const auto f=[&](uint32_t at,float value){memory.Words[at]=std::bit_cast<uint32_t>(value);};
    for(auto off:{0xe0u,0xf4u,0x110u,0x120u,0x128u,0x12cu,0x130u,0x134u})f(node+off,1);
    for(auto off:{0xe4u,0xf0u,0xecu,0xfcu,0x114u,0x118u,0x11cu,0x124u})f(node+off,0);
    f(record+0x14,0);f(record+0x18,0);f(record+0x20,16);f(record+0x24,16);
    f(record+0x28,144.f/256);f(record+0x2c,0);f(record+0x30,16.f/256);f(record+0x34,16.f/128);
    const auto original=memory.Words;
    for(auto [pc,word]:G::Guards) {
        auto bad=memory;bad.Words[pc]=word^1;
        Mm3dSingleScreen adapter;Mm3dGuestPatches patches;adapter.Register(patches);
        Require(patches.Verify(bad)==0,"each inline glyph guard must fail closed");
    }
    Mm3dSingleScreen adapter;Mm3dGuestPatches patches;adapter.Register(patches);
    Require(patches.Verify(memory)==1,"only inline glyph site enabled in isolated fixture");
    adapter.ConfigureButtonOverlay(NativeControlDefaults(),NativePromptSet::Keyboard);
    for(auto caller:{G::ShadowReturn,G::MainReturn}) {
        a32::GuestState state;for(unsigned i=0;i<16;++i)state.r[i]=100+i;
        state.r[1]=17;state.r[2]=node;state.r[3]=record;state.r[14]=caller;
        state.cpsr=0xa0000010;state.vfp.fill(0x3f800000);
        const auto before=state;a32::ExecutionResult result;
        Require(Mm3dGuestPatches::Execute(G::Pc,state,memory,&result,&patches) && result.pc==caller && state.r[0]==17,
            "inline renderer returns unchanged native command count to its exact caller");
        for(unsigned i=1;i<15;++i)Require(state.r[i]==before.r[i],"inline replacement changed a preserved register");
        Require(state.cpsr==before.cpsr && state.vfp==before.vfp && memory.Words==original,
            "inline replacement changed flags/VFP/guest bytes");
    }
    a32::GuestState prose;prose.r[14]=G::MainReturn;prose.r[2]=node;prose.r[3]=record;
    memory.Words[record]=pixels+8;a32::ExecutionResult result;
    Require(!Mm3dGuestPatches::Execute(G::Pc,prose,memory,&result,&patches),"ordinary font invokes original renderer");
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
    TestStateSettings();
    TestRestoreReset();
    TestInlineGlyphAbi();
    std::cout << "mm3d_single_screen_tests: ok\n";
    return 0;
}
