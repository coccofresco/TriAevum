#include "mm3d_guest_patches.h"

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

} // namespace

int main() {
    TestReplacement();
    TestGuard();
    TestInvalidSites();
    TestCondition();
    std::cout << "mm3d_guest_patches_tests: ok\n";
    return 0;
}
