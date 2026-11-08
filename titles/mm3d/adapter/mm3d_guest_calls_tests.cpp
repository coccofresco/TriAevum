#include "mm3d_guest_calls.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <string_view>

namespace {

using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;

void Require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "mm3d_guest_calls_tests: " << message << '\n';
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

constexpr std::uint32_t kSite = 0x00200000U;
constexpr std::uint32_t kAdd = 0x00300000U;    // a guest "int add(int, int)"
constexpr std::uint32_t kStore = 0x00300100U;  // a guest "void store(a, b, c, d, e)"

// Plays the guest: the runtime branches to a function, which "returns" by
// jumping to LR; here each call is answered by hand.
struct Guest {
    Mm3dGuestPatches Patches;
    Mm3dGuestCalls Calls;
    WordMemory Memory;
    a32::GuestState State;
    std::uint32_t Next = 0;
    bool Dispatch(std::uint32_t pc) {
        a32::ExecutionResult result;
        if (!Mm3dGuestPatches::Execute(pc, State, Memory, &result, &Patches)) {
            return false;
        }
        Next = result.pc;
        return true;
    }
    void Return(std::uint32_t r0) {
        State.r[0] = r0;
        Require(Dispatch(State.r[14]), "the return sentinel did not resume the body");
    }
};

void TestCalls() {
    Guest guest;
    guest.Calls.Install(guest.Patches);
    int finished = 0;
    std::uint32_t sum = 0;
    Mm3dGuestCalls::Body body = [&](Mm3dGuestCallContext& context) -> Mm3dGuestTask {
        const auto first = co_await context.Call(kAdd, {{2, 3}});
        sum = first.R0;
        Mm3dGuestCallContext::Arguments five;
        five.R = {1, 2, 3, 4};
        five.Stack[0] = 5;
        five.StackWords = 1;
        co_await context.Call(kStore, five);
        context.Saved.r[0] = sum * 10U; // what the guest sees after the site
        ++finished;
    };
    Mm3dGuestPatchSite site;
    site.Pc = kSite;
    site.Name = "calls";
    site.Handler = [&](a32::GuestState& state, a32::MemoryBus& memory) {
        return guest.Calls.Start(kSite, kSite + 4U, body, state, memory);
    };
    guest.Patches.Add(std::move(site));
    Require(guest.Patches.Verify(guest.Memory) == 2U, "the site and the sentinel were not enabled");
    Require(guest.Patches.BlockBoundaries() == std::vector<std::uint32_t>{kSite, kSite + 4U},
            "the sentinel became a registry boundary");

    guest.State.r[4] = 0x44U;   // callee-saved, must survive
    guest.State.r[13] = 0x8000U;
    guest.State.r[14] = 0x00123456U;
    guest.State.thread_pointer = 0x1000U;
    Require(guest.Dispatch(kSite) && guest.Next == kAdd && guest.State.r[0] == 2U &&
                guest.State.r[1] == 3U && guest.State.r[14] == Mm3dGuestCalls::kReturnSentinel,
            "the first call was not made");
    guest.State.r[4] = 0xDEADU; // the callee may use it, restores it... or not:
    guest.Return(5U);           // the body sees r0 and continues
    Require(guest.Next == kStore && guest.State.r[3] == 4U && guest.State.r[13] == 0x7FF8U &&
                guest.Memory.Words[0x7FF8U] == 5U,
            "the second call did not get its stack argument");
    guest.Return(0U);
    Require(finished == 1 && guest.Next == kSite + 4U && guest.State.r[0] == 50U &&
                guest.State.r[4] == 0x44U && guest.State.r[13] == 0x8000U &&
                guest.State.r[14] == 0x00123456U && guest.Calls.Active() == 0U,
            "the site did not resume with its own registers");
}

// A body that resumes the guest at its own site (a softbranch) lets the
// site's instruction run once, then the site applies again.
void TestResumeAtSite() {
    Guest guest;
    guest.Calls.Install(guest.Patches);
    int runs = 0;
    Mm3dGuestCalls::Body body = [&](Mm3dGuestCallContext& context) -> Mm3dGuestTask {
        ++runs;
        if (runs == 2) {
            co_await context.Call(kAdd, {{1, 1}});
        }
    };
    Mm3dGuestPatchSite site;
    site.Pc = kSite;
    site.Name = "softbranch";
    site.Handler = [&](a32::GuestState& state, a32::MemoryBus& memory) {
        return guest.Calls.Start(kSite, kSite, body, state, memory);
    };
    guest.Patches.Add(std::move(site));
    (void)guest.Patches.Verify(guest.Memory);
    guest.State.thread_pointer = 0x3000U;
    Require(guest.Dispatch(kSite) && guest.Next == kSite && runs == 1, "the body did not run");
    Require(!guest.Dispatch(kSite), "the site's own instruction did not follow");
    Require(guest.Dispatch(kSite) && guest.Next == kAdd && runs == 2, "the site did not apply again");
    guest.Return(2U);
    Require(guest.Next == kSite && !guest.Dispatch(kSite), "after a guest call the instruction did not follow");
}

// A return to the sentinel that no body is waiting for is not handled.
void TestStrayReturn() {
    Guest guest;
    guest.Calls.Install(guest.Patches);
    (void)guest.Patches.Verify(guest.Memory);
    guest.State.thread_pointer = 0x2000U;
    Require(!guest.Dispatch(Mm3dGuestCalls::kReturnSentinel), "a stray return was handled");
}

// A restored savestate abandons a body suspended in a guest call: Reset
// drops it (the old timeline's return is then a stray return, the body never
// resumes), and pending SkipOnce marks no longer let the site's instruction
// run once.
void TestResetOnRestore() {
    Guest guest;
    guest.Calls.Install(guest.Patches);
    int resumed = 0;
    bool destroyed = false;
    struct Guard {
        bool* Flag;
        ~Guard() { *Flag = true; }
    };
    Mm3dGuestCalls::Body body = [&](Mm3dGuestCallContext& context) -> Mm3dGuestTask {
        Guard guard{&destroyed};
        co_await context.Call(kAdd, {{1, 2}});
        ++resumed;
    };
    Mm3dGuestPatchSite site;
    site.Pc = kSite;
    site.Name = "restore";
    site.Handler = [&](a32::GuestState& state, a32::MemoryBus& memory) {
        return guest.Calls.Start(kSite, kSite, body, state, memory);
    };
    guest.Patches.Add(std::move(site));
    (void)guest.Patches.Verify(guest.Memory);
    guest.State.thread_pointer = 0x4000U;
    Require(guest.Dispatch(kSite) && guest.Next == kAdd && guest.Calls.Active() == 1U,
            "the call was not in flight");
    guest.Calls.Reset();
    Require(guest.Calls.Active() == 0U && destroyed, "Reset kept the suspended body");
    guest.State.r[0] = 3U;
    Require(!guest.Dispatch(Mm3dGuestCalls::kReturnSentinel) && resumed == 0,
            "the abandoned timeline's return resumed the body");

    guest.Patches.SkipOnce(kSite, 0x4000U);
    guest.Patches.ResetPending();
    Require(guest.Dispatch(kSite) && guest.Next == kAdd,
            "a SkipOnce mark from the abandoned timeline survived");
}

} // namespace

int main() {
    TestCalls();
    TestResumeAtSite();
    TestStrayReturn();
    TestResetOnRestore();
    std::cout << "mm3d_guest_calls_tests: ok\n";
    return 0;
}
