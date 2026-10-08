#include "mm3d_restoration.h"

#include "input.h"
#include "main.h"
#include "mm3d_a32_substitute.h"
#include "v110_address.h"

namespace Oot3dNativeGame {

namespace a32 = oot3d::recomp::a32;
using Mm3dRestoration::Guest;

namespace {

constexpr std::uint32_t kN = 1U << 31U;
constexpr std::uint32_t kZ = 1U << 30U;
constexpr std::uint32_t kC = 1U << 29U;
constexpr std::uint32_t kV = 1U << 28U;

// The flags of CMP r0, #0 (TRAMPOLINE_CMP_RESULT ... #0).
std::uint32_t CompareWithZero(std::uint32_t cpsr, std::uint32_t value) {
    cpsr &= ~(kN | kZ | kC | kV);
    cpsr |= value & kN;
    cpsr |= value == 0U ? kZ : 0U;
    cpsr |= kC; // no borrow
    return cpsr;
}

// hooks/main.hks, v1.0 reference -> v1.1.
// decouple_trigger_btns: B over the ZL/ZR checks (ref 0x1166C8, data
// "12 00 00 EA"), re-encoded for the v1.1 position.
constexpr std::uint32_t kDecoupleTriggers = 0x001166A0U;
constexpr std::uint32_t kDecoupleTriggersWord = Mm3dDekui::V110Branch(0xEA000012U, 0x001166C8U);
static_assert(Mm3dDekui::V110(0x001166C8U) == kDecoupleTriggers);

} // namespace

Mm3dRestorationPort::Mm3dRestorationPort(Mm3dGuestCalls& calls)
    : mCalls(calls), mInput(std::make_unique<Mm3dRestoration::Input>()) {}

Mm3dRestorationPort::~Mm3dRestorationPort() = default;

void Mm3dRestorationPort::Register(Mm3dGuestPatches& patches) {
    patches.AddTargets({Mm3dDekui::V110(0x00116718U)}); // decouple_trigger_btns' target
    // pre_calc_hook (softbranch, ref 0x10676C; LDR r1, [r0, #0x138]).
    patches.Add({0x00106740U, {{0x00106740U, 0xE5901138U}}, "rst.calc",
                 [this](a32::GuestState& state, a32::MemoryBus& memory) {
                     return mCalls.Start(0x00106740U, 0x00106740U,
                                         [this](Mm3dGuestCallContext& context) { return Calc(context); },
                                         state, memory);
                 }});
    // pad_update_hook (softbranch, ref 0x116A30; LDR r0, [sp, #0x14]).
    patches.Add({0x00116A08U, {{0x00116A08U, 0xE59D0014U}}, "rst.pad_update",
                 [this](a32::GuestState&, a32::MemoryBus& memory) -> std::optional<std::uint32_t> {
                     Guest guest(memory);
                     mInput->UpdatePadState(guest, mIsSwimming);
                     return std::nullopt;
                 }});
    // pad_update_hook for the ocarina (softbranch, ref 0x116900; STRH r7, [sp, #2]).
    patches.Add({0x001168D8U, {{0x001168D8U, 0xE1CD70B2U}}, "rst.pad_update_ocarina",
                 [](a32::GuestState&, a32::MemoryBus& memory) -> std::optional<std::uint32_t> {
                     Guest guest(memory);
                     Mm3dRestoration::Input::UpdatePadStateForOcarina(guest);
                     return std::nullopt;
                 }});
    // decouple_trigger_btns (patch over TST r0, #0x4000).
    patches.Add({kDecoupleTriggers, {{kDecoupleTriggers, 0xE3100901U}}, "rst.decouple_trigger_btns",
                 [](a32::GuestState& state, a32::MemoryBus& memory) {
                     return ExecuteMm3dSubstitute(kDecoupleTriggersWord, kDecoupleTriggers, state, memory);
                 }});
    // ui_items_fast_assign_item (BL to TRAMPOLINE_CMP_RESULT
    // IsItemAssignRequested, #0 over TST r1, #0xC00; ref 0x5C14F4).
    patches.Add({0x005C1664U, {{0x005C1664U, 0xE3110B03U}}, "rst.ui_items_assign_requested",
                 [this](a32::GuestState& state, a32::MemoryBus& memory) -> std::optional<std::uint32_t> {
                     Guest guest(memory);
                     state.cpsr = CompareWithZero(state.cpsr, mInput->IsItemAssignRequested(guest) ? 1U : 0U);
                     return 0x005C1664U + 4U;
                 }});
    // ui_items_fast_assign_item (BL to the GetItemAssignIndex trampoline,
    // r3 = index, over MOVEQ r3, #2; ref 0x5C161C).
    patches.Add({0x005C178CU, {{0x005C178CU, 0x03A03002U}}, "rst.ui_items_assign_index",
                 [this](a32::GuestState& state, a32::MemoryBus& memory) -> std::optional<std::uint32_t> {
                     Guest guest(memory);
                     state.r[3] = static_cast<std::uint32_t>(mInput->GetItemAssignIndex(guest));
                     return 0x005C178CU + 4U;
                 }});
}

Mm3dGuestTask Mm3dRestorationPort::Calc(Mm3dGuestCallContext& context) {
    using namespace Mm3dRestoration;
    Guest guest(*context.Memory);
    const std::uint32_t state = context.Saved.r[0];
    mGctx = 0;
    const u8 type = guest.Read8(state + kStateType);
    if (!mInitialised && type == kStateFirstGame) {
        // rst::Init (link::Init comes with the Link port).
        co_await context.Call(kPlayEffect, {{NA_SE_SY_QUEST_CLEAR}});
        mInitialised = true;
    }
    if (type != kStatePlay) {
        co_return;
    }
    mGctx = state;
    // CalcCamera, link::Calc and the Fix* functions come with their ports.
    UiGearScreenUpdate(guest, mGctx);
    if (const auto close = UiOcarinaScreenUpdate(guest, mGctx)) {
        co_await context.Call(close->Function, {{close->Layout}});
        co_await context.Call(kPlayEffect, {{NA_SE_SY_DECIDE}});
    }
}

std::size_t Mm3dRestorationPort::ApplyDataPatches(a32::MemoryBus& memory) {
    std::size_t applied = 0;
    // decouple_start_select (ref 0x6B083C, data "01").
    std::uint8_t value = 0xFF;
    if (memory.Read8(0x006B083CU, &value) && value == 0U && memory.Write8(0x006B083CU, 1U)) {
        ++applied;
    }
    return applied;
}

} // namespace Oot3dNativeGame
