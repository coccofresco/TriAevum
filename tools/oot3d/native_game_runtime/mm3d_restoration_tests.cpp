#include "input.h"
#include "mm3d_restoration.h"
#include "v110_address.h"

#include <cstdlib>
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
    Mm3dRestorationPort port;
    Mm3dGuestPatches patches;
    port.Register(patches);
    Require(patches.Verify(memory) == 5U, "the input hooks do not match v1.1");
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
    Require(port.ApplyDataPatches(memory) == 1U && guest.Read8(0x006B083CU) == 1U, "the data patch was not written");
    Require(port.ApplyDataPatches(memory) == 0U, "a patched byte was patched again");
}

} // namespace

int main() {
    TestPadState();
    TestOcarinaScreen();
    TestItemAssign();
    TestSites();
    std::cout << "mm3d_restoration_tests: ok\n";
    return 0;
}
