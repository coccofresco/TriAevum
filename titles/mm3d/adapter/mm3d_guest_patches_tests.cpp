#include "mm3d_guest_patches.h"
#include "mm3d_lighting_hook_sites.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {

using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;

void Require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "mm3d_guest_patches_tests: " << message << '\n';
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

constexpr std::uint32_t kSite = 0x005A745CU;
constexpr std::uint32_t kCmpR1R2 = 0xE1510002U; // cmp r1, r2
constexpr std::uint32_t kBeq = 0x0A000002U;     // beq +16

Mm3dGuestPatchSite CompareSite(int* calls) {
    Mm3dGuestPatchSite site;
    site.Pc = kSite;
    site.Name = "compare";
    site.Guard = {{kSite, kCmpR1R2}, {kSite + 4U, kBeq}};
    site.Handler = [calls](a32::GuestState& state, a32::MemoryBus& memory)
        -> std::optional<std::uint32_t> {
        ++*calls;
        std::uint32_t value = 0;
        if (!memory.Read32(0x1000U, &value)) {
            return std::nullopt;
        }
        state.r[2] = value;
        return kSite + 4U;
    };
    return site;
}

// A site whose code matches replaces its instruction and continues at the
// PC its handler returns.
void TestReplacement() {
    int calls = 0;
    Mm3dGuestPatches patches;
    patches.Add(CompareSite(&calls));
    WordMemory memory;
    memory.Words = {{kSite, kCmpR1R2}, {kSite + 4U, kBeq}, {0x1000U, 7U}};
    Require(patches.Verify(memory) == 1U, "a matching site was not enabled");
    Require(patches.EnabledPcs() == std::vector<std::uint32_t>{kSite}, "the filter is wrong");
    Require(patches.BlockBoundaries() == std::vector<std::uint32_t>{kSite, kSite + 4U},
            "the site does not bound its block");
    a32::GuestState state;
    a32::ExecutionResult result;
    Require(Mm3dGuestPatches::Execute(kSite, state, memory, &result, &patches),
            "an enabled site did not replace its instruction");
    Require(calls == 1 && state.r[2] == 7U && state.r[15] == kSite + 4U &&
                result.kind == a32::ExitKind::Branch && result.pc == kSite + 4U,
            "the replacement did not continue where its handler said");
    Require(!Mm3dGuestPatches::Execute(kSite + 8U, state, memory, &result, &patches),
            "a PC without a site was replaced");
    // A handler that declines leaves the original instruction to run.
    memory.Words.erase(0x1000U);
    Require(!Mm3dGuestPatches::Execute(kSite, state, memory, &result, &patches),
            "a declined replacement was applied");
    const auto stats = patches.Statistics();
    Require(stats.size() == 1U && stats[0].Replacements == 1U && stats[0].Declined == 1U,
            "the statistics are wrong");
}

// Code that differs from what the site was written against disables it.
void TestGuard() {
    int calls = 0;
    Mm3dGuestPatches patches;
    patches.Add(CompareSite(&calls));
    WordMemory memory;
    memory.Words = {{kSite, kCmpR1R2}, {kSite + 4U, 0xE1A00000U}};
    Require(patches.Verify(memory) == 0U && patches.EnabledPcs().empty(),
            "a site over different code was enabled");
    a32::GuestState state;
    a32::ExecutionResult result;
    Require(!Mm3dGuestPatches::Execute(kSite, state, memory, &result, &patches) && calls == 0,
            "a disabled site ran");
    const auto stats = patches.Statistics();
    Require(!stats[0].Enabled && stats[0].MismatchAddress == kSite + 4U,
            "the mismatch is not reported");
}

// A site must start a registry block: inside a block it would only run
// when branched to.
void TestMissingBlock() {
    int calls = 0;
    Mm3dGuestPatches patches;
    patches.Add(CompareSite(&calls));
    WordMemory memory;
    memory.Words = {{kSite, kCmpR1R2}, {kSite + 4U, kBeq}};
    static const a32::Block kBlocks[] = {{kSite - 8U, nullptr, 4U, false}};
    static const a32::BlockShard kShards[] = {{kSite - 8U, kSite - 8U, kBlocks, 1U}};
    const a32::Registry without{kShards, 1U, nullptr, 0U};
    Require(patches.Verify(memory, &without) == 0U && patches.Statistics()[0].MissingBlock,
            "a site inside a registry block was enabled");
    static const a32::Block kSplit[] = {{kSite - 8U, nullptr, 2U, false}, {kSite, nullptr, 1U, false}};
    static const a32::BlockShard kSplitShards[] = {{kSite - 8U, kSite, kSplit, 2U}};
    const a32::Registry with{kSplitShards, 1U, nullptr, 0U};
    Require(patches.Verify(memory, &with) == 1U && !patches.Statistics()[0].MissingBlock,
            "a site starting a registry block was not enabled");
}

// A site's preparation decides, on the mounted image, whether it applies.
void TestPrepare() {
    int calls = 0;
    Mm3dGuestPatches patches;
    auto site = CompareSite(&calls);
    site.Prepare = [](a32::MemoryBus& memory) {
        std::uint32_t word = 0;
        return memory.Read32(0x2000U, &word) && word == 1U;
    };
    patches.Add(std::move(site));
    WordMemory memory;
    memory.Words = {{kSite, kCmpR1R2}, {kSite + 4U, kBeq}, {0x2000U, 0U}};
    Require(patches.Verify(memory) == 0U && !patches.Statistics()[0].Prepared,
            "a site its preparation refused was enabled");
    memory.Words[0x2000U] = 1U;
    Require(patches.Verify(memory) == 1U && patches.Statistics()[0].Prepared,
            "a prepared site was not enabled");

    // A preparation sees every guard's outcome, also a later site's.
    Mm3dGuestPatches ordered;
    auto early = CompareSite(&calls);
    early.Pc = 0x1000U;
    early.Guard.clear();
    early.Prepare = [&ordered](a32::MemoryBus&) {
        const auto enabled = ordered.EnabledPcs();
        return std::find(enabled.begin(), enabled.end(), kSite) != enabled.end();
    };
    ordered.Add(std::move(early));
    ordered.Add(CompareSite(&calls));
    Require(ordered.Verify(memory) == 2U, "a preparation ran before a later site's guard");
    memory.Words[kSite] = 0U;
    Require(ordered.Verify(memory) == 0U, "a preparation saw a disabled site as enabled");
}

void TestInvalidSites() {
    int calls = 0;
    Mm3dGuestPatches patches;
    patches.Add(CompareSite(&calls));
    bool threw = false;
    try {
        patches.Add(CompareSite(&calls));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Require(threw, "two sites at one PC were accepted");
    threw = false;
    auto misaligned = CompareSite(&calls);
    misaligned.Pc = kSite + 2U;
    try {
        patches.Add(std::move(misaligned));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Require(threw, "a misaligned site was accepted");
}

void TestCondition() {
    constexpr std::uint32_t kZero = 1U << 30U;
    Require(Mm3dConditionPassed(0x0F000000U, kZero), "EQ with Z set must pass");
    Require(!Mm3dConditionPassed(0x0F000000U, 0U), "EQ with Z clear must fail");
    Require(Mm3dConditionPassed(0x1F000000U, 0U), "NE with Z clear must pass");
    Require(Mm3dConditionPassed(0xEF000000U, 0U), "AL must pass");
}

// Whole-AOT product mode: an enabled site needs module boundaries at its PC,
// the next instruction and the declared targets; a disabled site needs none.
void TestProductBoundaries() {
    int calls = 0;
    Mm3dGuestPatches patches;
    patches.Add(CompareSite(&calls));
    patches.AddTargets({kSite + 16U});
    WordMemory memory;
    memory.Words = {{kSite, kCmpR1R2}, {kSite + 4U, kBeq}};
    Require(patches.Verify(memory) == 1U, "the site was not enabled");
    Require(patches.MissingProductBoundaries({kSite, kSite + 4U, kSite + 16U}).empty(),
            "a module exporting every boundary was refused");
    // Negative: a module without the continuation boundary is refused, and
    // the missing PC is named.
    Require(patches.MissingProductBoundaries({kSite, kSite + 16U}) ==
                std::vector<std::uint32_t>{kSite + 4U},
            "a module lacking the continuation boundary was accepted");
    Require(patches.MissingProductBoundaries({}).size() == 3U,
            "an empty module must lack all three boundaries");
    WordMemory other;
    other.Words = {{kSite, 0U}, {kSite + 4U, kBeq}};
    Require(patches.Verify(other) == 0U, "a mismatching site was enabled");
    Require(patches.MissingProductBoundaries({}).empty(),
            "a disabled site must not need module boundaries");
}

void TestReturnSiteNeedsNoContinuation() {
    // An observer on an unconditional return: Pc + 4 is literal data and is
    // neither a registry boundary nor a module entry the product requires.
    Mm3dGuestPatches patches;
    Mm3dGuestPatchSite site;
    site.Pc = kSite;
    site.Name = "epilogue-observer";
    site.Guard = {{kSite, 0xE8BD8038U}};
    site.Handler = [](auto&, auto&) -> std::optional<std::uint32_t> { return std::nullopt; };
    site.ContinuesAtNext = false;
    patches.Add(std::move(site));
    WordMemory memory;
    memory.Words = {{kSite, 0xE8BD8038U}};
    Require(patches.Verify(memory) == 1U, "the return site was not enabled");
    Require(patches.BlockBoundaries() == std::vector<std::uint32_t>{kSite},
            "a return site must not export its literal successor");
    Require(patches.MissingProductBoundaries({kSite}).empty(),
            "a module without the literal successor was refused");
    // Negative: the site itself is still required.
    Require(patches.MissingProductBoundaries({}) == std::vector<std::uint32_t>{kSite},
            "a module lacking the return site was accepted");
    Require(Mm3dIsUnconditionalReturn(0xE8BD8038U) && Mm3dIsUnconditionalReturn(0xE12FFF1EU) &&
                !Mm3dIsUnconditionalReturn(0x08BD8038U) && !Mm3dIsUnconditionalReturn(0xE8BD4038U),
            "return classification");
    // The title's epilogue observers (guard word 0 = pop {r3-r5,pc}) export
    // no literal successor; the body observers keep theirs.
    Mm3dGuestPatches lighting;
    RegisterMm3dLightingHooks(lighting);
    const auto bounds = lighting.BlockBoundaries();
    const auto has = [&](std::uint32_t pc) {
        return std::binary_search(bounds.begin(), bounds.end(), pc);
    };
    Require(has(0x0011EF58U) && !has(0x0011EF5CU) && !has(0x0011EFA8U) && !has(0x0011EFF8U),
            "epilogue observers exported their literal successors");
    Require(has(0x0011EF3CU) && has(0x0011EF40U), "a body observer lost its continuation");
}

void TestHandlerNeverReturningNext() {
    // MM3D product F07 r16 (startup self-check): the photo camera and the
    // roll-dash sites decline or continue elsewhere. The product runs a
    // declined site as the module's code at Pc, so Pc + 4 is no module
    // boundary it needs; the registry runtime keeps it.
    Mm3dGuestPatches patches;
    Mm3dGuestPatchSite site;
    site.Pc = kSite;
    site.Name = "observer";
    site.Guard = {{kSite, kCmpR1R2}};
    site.Handler = [](auto&, auto&) -> std::optional<std::uint32_t> { return std::nullopt; };
    site.HandlerReturnsNext = false;
    patches.Add(std::move(site));
    WordMemory memory;
    memory.Words = {{kSite, kCmpR1R2}};
    Require(patches.Verify(memory) == 1U, "the observer was not enabled");
    Require(patches.BlockBoundaries() == std::vector<std::uint32_t>{kSite, kSite + 4U},
            "the registry lost the observer's continuation");
    Require(patches.MissingProductBoundaries({kSite}).empty(),
            "a module without Pc + 4 was refused for a handler that never returns there");
    // Negative: the site itself is still required.
    Require(patches.MissingProductBoundaries({}) == std::vector<std::uint32_t>{kSite},
            "a module lacking the observer's own PC was accepted");
}

} // namespace

int main() {
    TestProductBoundaries();
    TestHandlerNeverReturningNext();
    TestReturnSiteNeedsNoContinuation();
    TestReplacement();
    TestGuard();
    TestMissingBlock();
    TestPrepare();
    TestInvalidSites();
    TestCondition();
    std::cout << "mm3d_guest_patches_tests: ok\n";
    return 0;
}
