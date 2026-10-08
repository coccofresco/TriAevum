#include "input.h"
#include "main.h"
#include "mm3d_restoration.h"
#include "v110_address.h"
#include "sites.h"
#include "port_tests.inc"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>
#include <map>
#include <string_view>

namespace {

using namespace Oot3dNativeGame;
using namespace Mm3dRestoration;
namespace a32 = oot3d::recomp::a32;

void Require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "mm3d_restoration_tests: " << message << '\n';
        std::exit(1);
    }
}

class WordMemory final : public a32::MemoryBus {
  public:
    std::map<std::uint32_t, std::uint32_t> Words;
    bool Read32(std::uint32_t address, std::uint32_t* value) override {
        const auto found = Words.find(address);
        *value = found == Words.end() ? 0U : found->second;
        return true;
    }
    bool Write32(std::uint32_t address, std::uint32_t value) override {
        Words[address] = value;
        return true;
    }
};

// ZR+A plays the ocarina, ZR+X uses item I; ZL stays and also selects the
// Pictograph Box; unusable buttons are cleared from what the game sees.
void TestPadState() {
    WordMemory memory;
    Guest guest(memory);
    Input input;
    guest.Write32(kButtons, Button::ZR | Button::A | Button::X | Button::ZL);
    guest.Write32(kNewButtons, Button::A);
    input.UpdatePadState(guest, false);
    Require(guest.Read32(kButtons) == (Button::ZR | Button::ZL), "ZR+A/X still reached the game");
    Require(guest.Read8(kTouchscreenButtons) ==
                (TouchscreenButton::Ocarina | TouchscreenButton::I | TouchscreenButton::PictographBox),
            "the touch buttons were not set");
    Require(guest.Read8(kTouchscreenNewButtons) == TouchscreenButton::Ocarina, "the new ocarina press is missing");
    // The ocarina is unusable here: the game does not see it, the item
    // assignment state still does.
    guest.Write8(kUsableButtons + UsableButton::Ocarina, kButtonIsNotUsable);
    guest.Write32(kButtons, Button::ZR | Button::A);
    guest.Write32(kNewButtons, Button::A);
    guest.Write8(kTouchscreenButtons, 0);
    guest.Write8(kTouchscreenNewButtons, 0);
    input.UpdatePadState(guest, false);
    Require(guest.Read8(kTouchscreenButtons) == 0U && input.mButtons == TouchscreenButton::Ocarina,
            "an unusable ocarina reached the game");
    // Swimming keeps A for the game.
    guest.Write32(kButtons, Button::ZR | Button::A);
    input.UpdatePadState(guest, true);
    Require((guest.Read32(kButtons) & Button::A) != 0U, "A was taken while swimming");
}

void TestOcarinaScreen() {
    WordMemory memory;
    Guest guest(memory);
    guest.Write32(kOcarinaScreen, 0x08001000U);
    guest.Write32(kButtons, Button::ZL | Button::ZR);
    Input::UpdatePadStateForOcarina(guest);
    Require(guest.Read32(kButtons) == (Button::ZL | Button::ZR), "L/R were set outside the ocarina screen");
    guest.Write32(kScreenContext + 0x8U, 0x08001000U);
    Input::UpdatePadStateForOcarina(guest);
    Require(guest.Read32(kButtons) == (Button::ZL | Button::ZR | Button::L | Button::R),
            "ZL/ZR do not play L/R on the ocarina screen");
}

void TestItemAssign() {
    WordMemory memory;
    Guest guest(memory);
    Input input;
    Require(!input.IsItemAssignRequested(guest), "an assignment without a button");
    guest.Write32(kNewButtons, Button::Y);
    Require(input.IsItemAssignRequested(guest) && input.GetItemAssignIndex(guest) == 2, "Y assigns to slot 2");
    guest.Write32(kNewButtons, 0);
    input.mNewButtons = TouchscreenButton::II;
    Require(input.IsItemAssignRequested(guest) && input.GetItemAssignIndex(guest) == 3, "II assigns to slot 3");
}

// The hooks: guarded by their v1.1 words, the trampolines' effects.
void TestSites() {
    WordMemory memory;
    memory.Words = {{0x00116A08U, 0xE59D0014U}, {0x001168D8U, 0xE1CD70B2U}, {0x001166A0U, 0xE3100901U},
                    {0x005C1664U, 0xE3110B03U}, {0x005C178CU, 0x03A03002U}};
    Mm3dGuestCalls calls;
    Mm3dRestorationPort port(calls);
    Mm3dGuestPatches patches;
    calls.Install(patches);
    port.Register(patches);
    for(const auto& site:SourceSites)for(unsigned i=0;i<site.guard_count;++i)memory.Words[site.guards[i].first]=site.guards[i].second;
    Require(patches.Verify(memory) == SourceSites.size()+6, "the hooks do not match v1.1");
    a32::GuestState state;
    a32::ExecutionResult result;
    // The softbranch runs and leaves the guest's instruction to follow.
    Require(!Mm3dGuestPatches::Execute(0x00116A08U, state, memory, &result, &patches),
            "the softbranch replaced the guest's instruction");
    // The decoupling branch jumps over the checks (ref 0x116718 -> v1.1).
    Require(Mm3dGuestPatches::Execute(0x001166A0U, state, memory, &result, &patches) &&
                result.pc == Mm3dDekui::V110(0x00116718U),
            "the decoupling branch did not land on the translated target");
    Guest guest(memory);
    guest.Write32(kNewButtons, Button::X);
    Require(Mm3dGuestPatches::Execute(0x005C1664U, state, memory, &result, &patches) &&
                result.pc == 0x005C1668U && (state.cpsr & (1U << 30U)) == 0U,
            "a requested assignment left Z set");
    Require(Mm3dGuestPatches::Execute(0x005C178CU, state, memory, &result, &patches) && state.r[3] == 1U,
            "X does not assign to slot 1");
    // decouple_start_select.
    for(const auto& p:DataPatches)for(unsigned i=0;i<p.size;++i)guest.Write8(p.pc+i,p.original[i]);
    Require(port.ApplyDataPatches(memory) == DataPatches.size() && guest.Read8(0x006B083CU) == 1U, "the data patch was not written");
    Require(port.ApplyDataPatches(memory) == 0U, "a patched byte was patched again");
}

// rst::Calc: the first game state plays the initialisation jingle; in play,
// Start on the ocarina screen closes the ocarina layout (opening the song
// list) and plays the decide effect, then the site's instruction runs.
void TestCalc() {
    WordMemory memory;
    memory.Words[0x00106740U] = 0xE5901138U;
    for(const auto& site:SourceSites)if(site.action=="Calc")for(unsigned i=0;i<site.guard_count;++i)memory.Words[site.guards[i].first]=site.guards[i].second;
    Mm3dGuestCalls calls;
    Mm3dRestorationPort port(calls);
    Mm3dGuestPatches patches;
    calls.Install(patches);
    port.Register(patches);
    (void)patches.Verify(memory);
    Guest guest(memory);
    constexpr std::uint32_t kState = 0x08100000U;
    a32::GuestState state;
    state.thread_pointer = 0x1000U;
    state.r[0] = kState;
    state.r[13] = 0x0FFF0000U;
    a32::ExecutionResult result;
    const auto run = [&](std::uint32_t pc) {
        Require(Mm3dGuestPatches::Execute(pc, state, memory, &result, &patches), "the hook did not run");
        return result.pc;
    };
    const auto returnFromGuest = [&] { return run(state.r[14]); };
    guest.Write8(kState + kStateType, kStateFirstGame);
    Require(run(0x00106740U) == kPlayEffect && state.r[0] == NA_SE_SY_QUEST_CLEAR, "no initialisation jingle");
    Require(returnFromGuest() == 0x00106740U && state.r[0] == kState, "the state register was not restored");
    Require(!Mm3dGuestPatches::Execute(0x00106740U, state, memory, &result, &patches),
            "the displaced instruction did not follow");
    // In play, on the ocarina screen with the ocarina layout current.
    constexpr std::uint32_t kScreen = 0x08200000U;
    guest.Write8(kState + kStateType, kStatePlay);
    guest.Write32(kOcarinaScreen, kScreen);
    guest.Write32(kScreenContext + 0x8U, kScreen);
    guest.Write32(kScreen + kOcarinaLayout, 0x08300000U);
    guest.Write8(kScreen + kOcarinaIsMenu, 1U);
    guest.Write32(kState + kStateNewButtons, Button::Start);
    Require(run(0x00106740U) == kCloseOcarinaLayout && state.r[0] == 0x08300000U &&
                guest.Read8(kScreen + kOcarinaTransitioning) == 1U,
            "Start did not close the ocarina layout");
    Require(returnFromGuest() == kPlayEffect && state.r[0] == NA_SE_SY_DECIDE, "no decide effect");
    Require(returnFromGuest() == 0x00106740U, "the hook did not resume the guest at itself");
}

// The edits prepare_message_edits.py writes reach the port; their hashes
// are checked when set.
void TestReadMessageEdits() {
    const auto fnv = [](std::string_view hex) {
        std::uint64_t value = 0xcbf29ce484222325ULL;
        for (size_t at = 0; at < hex.size(); at += 2U) {
            value = (value ^ std::stoul(std::string(hex.substr(at, 2U)), nullptr, 16)) * 0x100000001b3ULL;
        }
        char text[17];
        std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
        return std::string(text);
    };
    const auto document = [&](std::string_view format, const std::string& editedHash) {
        return std::string("{\"format\":\"") + std::string(format) +
               "\",\"edits\":[{\"id\":62,\"language\":4,\"original\":\"5461700000\",\"edited\":\"5072000000\","
               "\"original_hash\":\"" + fnv("5461700000") + "\",\"edited_hash\":\"" + editedHash + "\"}]}";
    };
    std::string error;
    auto edits = Mm3dRestorationPort::ReadMessageEdits(
        document("triaevum_mm3d_message_edits_v1", fnv("5072000000")), &error);
    Require(edits.has_value() && edits->size() == 1U && (*edits)[0].id == 62U && (*edits)[0].language == 4U &&
                (*edits)[0].original.size() == 5U && (*edits)[0].edited[1] == 0x72U,
            "message edits were not read");
    Mm3dGuestCalls calls;
    Mm3dRestorationPort port(calls);
    Require(port.SetMessageEdits(*edits), "valid message edits were refused");
    auto wrong = Mm3dRestorationPort::ReadMessageEdits(
        document("triaevum_mm3d_message_edits_v1", fnv("5072000001")), &error);
    Require(wrong.has_value() && !port.SetMessageEdits(*wrong), "an edit with a wrong hash was accepted");
    Require(!Mm3dRestorationPort::ReadMessageEdits(document("other", fnv("5072000000")), &error).has_value(),
            "an unknown format was read");
}

} // namespace

int main(int argc,char** argv) {
    try {
    TestPadState();
    TestOcarinaScreen();
    TestItemAssign();
    TestSites();
    TestCalc();
    TestReadMessageEdits();
    RestorationPortTests::All(argc>1?argv[1]:nullptr);
    std::cout << "mm3d_restoration_tests: ok\n";
    return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
