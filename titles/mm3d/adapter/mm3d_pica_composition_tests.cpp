// MM3D composition: the scene lighting its tracker binds to the opaque pass.
#include "mm3d_lighting_hook_sites.h"
#include "mm3d_pica_composition.h"
#include "mm3d_title_contracts.h"
#include "mm3d_ui_subsystems.h"

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "mm3d_pica_composition_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

void Map(Oot3dNativeGame::NativeA32Memory& memory, uint32_t address,
         size_t size) {
    std::string error;
    Require(memory.MapRegion(
                {"composition-test", address, size, true, false, {}},
                &error),
            error);
}

} // namespace

// MM3D: the opaque pass's spans carry the current View's world-to-view and
// the environment's directional lights linked in the play state's
// LightContext; the sky's span carries no lighting, and a light record that
// is not linked is not a scene light.
void TestMm3dSceneLighting() {
    using namespace Oot3dNativeGame;
    NativeA32Memory memory;
    Map(memory, 0x00656000U, 0x1000U);   // current View pointer, command cursor page
    Map(memory, 0x006A3000U, 0x1000U);
    Map(memory, 0x00790000U, 0x10000U);  // LightNode pool
    Map(memory, 0x08040000U, 0x10000U);  // play state
    constexpr uint32_t kPlay = 0x08045A70U;
    constexpr uint32_t kView = kPlay + kMm3dPlayViewOffset;
    Require(memory.Write32(kMm3dCurrentViewPointer, kView), "could not seed the current View");
    // World-to-view rows: a camera at (10, 20, 30) looking down -Z.
    const std::array<float, 12> rows{1, 0, 0, -10, 0, 1, 0, -20, 0, 0, 1, -30};
    for (uint32_t i = 0; i < rows.size(); ++i)
        Require(memory.Write32(kView + kMm3dViewWorldToViewOffset + i * 4U, std::bit_cast<uint32_t>(rows[i])),
                "could not seed the world-to-view");
    // Light 1: toward (0, 127, 0), color (255, 128, 0); light 2: (127, 0, 0).
    const auto writeLight = [&](uint32_t info, int8_t x, int8_t y, int8_t z, uint8_t r, uint8_t g, uint8_t b) {
        const std::array<uint8_t, 10> bytes{1, 0, 0, 0, std::bit_cast<uint8_t>(x), std::bit_cast<uint8_t>(y),
                                            std::bit_cast<uint8_t>(z), r, g, b};
        for (uint32_t i = 0; i < bytes.size(); ++i)
            Require(memory.Write8(info + i, bytes[i]), "could not seed a light record");
    };
    const uint32_t light1 = kPlay + kMm3dPlayEnvironmentLight1Offset;
    const uint32_t light2 = kPlay + kMm3dPlayEnvironmentLight2Offset;
    writeLight(light1, 0, 127, 0, 255, 128, 0);
    writeLight(light2, 127, 0, 0, 51, 51, 51);
    // LightContext: head -> node A (light 2) -> node B (light 1); a record
    // at another address in the list is not an environment light.
    constexpr uint32_t kNodeA = 0x0079C700U, kNodeB = 0x0079C70CU, kNodeC = 0x0079C718U;
    Require(memory.Write32(kPlay + kMm3dPlayLightListHeadOffset, kNodeC) &&
                memory.Write32(kNodeC, 0x08049000U) && memory.Write32(kNodeC + 8U, kNodeA) &&
                memory.Write32(kNodeA, light2) && memory.Write32(kNodeA + 8U, kNodeB) &&
                memory.Write32(kNodeB, light1) && memory.Write32(kNodeB + 8U, 0U),
            "could not seed the LightContext");
    writeLight(0x08049000U, 0, 0, 127, 255, 255, 255);

    Map(memory, 0x00100000U, 0x00540000U);
    Map(memory, 0x0067C000U, 0x1000U);
    Map(memory, 0x00689000U, 0x1000U);
    for (const auto& hook : kMm3dLightingHookSites)
        for (uint32_t i = 0; i < 4U; ++i)
            Require(memory.Write32(hook.Pc + i * 4U, hook.Words[i]), "seed MM hook guard");
    for (const auto [pc, word] : kMm3dCompositionOwnerGuards)
        Require(memory.Write32(pc, word), "seed composition owner guard");
    Require(memory.Write8(kPlay + 0x13DU, 4U) && memory.Write32(kPlay + 0x4A8U, kPlay) &&
            memory.Write32(kPlay + 0x138U, 1U), "seed verified MM play owner");
    Mm3dNativePicaCompositionTracker tracker;
    Require(tracker.VerifyHooks(memory), "MM hook signatures");
    oot3d::recomp::a32::GuestState state{};
    const auto pass = [&](uint32_t entry, uint32_t returnPc, uint32_t begin, uint32_t end) {
        Require(memory.Write32(kMm3dCommandListCursorAddress, begin), "could not seed the cursor");
        state.r[14] = returnPc;
        if (entry == kMm3dQuadQueueDrawEntry) state.r[1] = returnPc == 0x00110D88U ? 4U : 5U;
        const auto event = tracker.ObserveBlockEntry(entry, state, memory);
        Require(memory.Write32(kMm3dCommandListCursorAddress, end), "could not advance the cursor");
        (void)tracker.ObserveBlockEntry(returnPc, state, memory);
        return event;
    };
    Require(pass(kMm3dSkyboxDrawEntry, 0x00110DF8U, 0x14000000U, 0x14000040U) ==
                Mm3dNativePicaCompositionTracker::Event::None,
            "the sky is not the opaque pass");
    Require(pass(kMm3dRenderListVisitEntry, 0x0011EF3CU, 0x14000100U, 0x14000180U) ==
                Mm3dNativePicaCompositionTracker::Event::OpaqueWorldBegins,
            "the opaque pass did not begin the scene frame");
    std::vector<Oot3dPicaCommandListCompositionSpan> spans;
    std::string error;
    Require(tracker.TakeCommandListCompositionSpans(0x14000000U, 0x200U, spans, &error), error);
    Require(spans.size() == 2U, "expected the sky and opaque spans");
    const auto& sky = spans[0];
    const auto& opaque = spans[1];
    Require(sky.Attribution.Layer == Oot3dPicaCompositionLayer::Atmosphere && !sky.LightBinding.WorldToView &&
                !sky.LightBinding.SceneDirectionals,
            "the sky span carried scene lighting");
    Require(opaque.Attribution.Layer == Oot3dPicaCompositionLayer::OpaqueWorld && opaque.LightBinding.WorldToView,
            "the opaque span has no world-to-view");
    const auto& worldToView = *opaque.LightBinding.WorldToView;
    Require(worldToView[0] == 1.0F && worldToView[12] == -10.0F && worldToView[13] == -20.0F &&
                worldToView[14] == -30.0F && worldToView[15] == 1.0F,
            "the world-to-view is not the View's matrix, column-major");
    Require(opaque.LightBinding.SceneDirectionals && opaque.LightBinding.SceneDirectionals->Count == 2U,
            "expected the two linked environment lights");
    const auto& first = opaque.LightBinding.SceneDirectionals->Lights[0];
    const auto& second = opaque.LightBinding.SceneDirectionals->Lights[1];
    Require(first.SourceIdentity == light1 + 4U && first.DirectionTowardSource[1] == 1.0F &&
                first.Color[0] == 1.0F && std::abs(first.Color[1] - 128.0F / 255.0F) < 1.0e-6F,
            "light 1 was not decoded first");
    Require(second.SourceIdentity == light2 + 4U && second.DirectionTowardSource[0] == 1.0F,
            "light 2 was not decoded");
    Require(tracker.Stats().LightBindings == 1U && tracker.Stats().SceneDirectionalLights == 2U,
            "the lighting statistics are wrong");

    // Without a current View (another game state), no lighting.
    Require(memory.Write32(kMm3dCurrentViewPointer, 0U), "could not clear the current View");
    (void)pass(kMm3dRenderListVisitEntry, 0x0011EF3CU, 0x14000200U, 0x14000240U);
    Require(tracker.TakeCommandListCompositionSpans(0x14000000U, 0x300U, spans, &error), error);
    Require(spans.size() == 3U && !spans[2].LightBinding.WorldToView && !spans[2].LightBinding.SceneDirectionals,
            "a frame without a current View carried lighting");

    // Bounded, exact resource-name classification: only known HUD owners.
    constexpr uint32_t layout=0x08040000,resource=0x08040100,nameAddress=0x08040200;
    Require(memory.Write32(layout+4,resource)&&memory.Write32(resource+0x18,nameAddress),"layout fixture");
    uint32_t begin=0x15000000;
    for(std::string_view name:{"Hud","Hud2","HudItemIconSet","Telop","MessageWindow","MenuItem","MenuMask","MenuCollect","FileSelect","MenuSchedule","MenuOcarina","MenuStartup","HudUnknown"}) {
        for(uint32_t i=0;i<name.size();++i)Require(memory.Write8(nameAddress+i,uint8_t(name[i])),"layout name");
        Require(memory.Write8(nameAddress+uint32_t(name.size()),0),"name terminator");
        state.r[1]=layout;
        (void)pass(kMm3dLayoutOwnerDrawEntry,0x00126998U,begin,begin+0x40);
        Require(tracker.TakeCommandListCompositionSpans(begin,0x40,spans,&error),error);
        const auto& attribution=spans.back().Attribution;
        const bool hud=name=="Hud"||name=="Hud2"||name=="HudItemIconSet"||name=="Telop";
        Require(IsMm3dGameplayHud(attribution.SourcePc,attribution.NativeValue)==hud,"HUD-only attribution");
        begin+=0x40;
    }
    // P17 owner dump: every owner draw's resource name is counted, known or not.
    Require(tracker.Stats().LayoutOwners.size()==13&&tracker.Stats().LayoutOwners.at("HudUnknown")==1&&
            tracker.Stats().LayoutOwners.at("Hud")==1&&tracker.Stats().LayoutOwnersDropped==0,"layout owner dump");
    Require(!IsMm3dGameplayHud(kMm3dQuadQueueDrawEntry,kMm3dUiTag|1U),"mixed sprites never blanket HUD");
    Require(memory.Write32(resource+0x18,0xFFFFFFF0U),"bad name pointer");
    Require(ReadMm3dUiSubsystem(memory,layout)==Mm3dUiSubsystem::Unknown,"bad name bounded");
    // Clear test spans by reinitializing; legacy scope assertions below stand.
    tracker.Reset();
    state.r[1]=0;
    (void)pass(kMm3dSkyboxDrawEntry,0x00110DF8U,0x14000000U,0x14000040U);
    (void)pass(kMm3dRenderListVisitEntry,0x0011EF3CU,0x14000100U,0x14000180U);
    (void)pass(kMm3dRenderListVisitEntry,0x0011EF3CU,0x14000200U,0x14000240U);

    // A 2D layout queue (its owners, from either loop, and its finishers)
    // and the top screen's sprite queues are the interface.
    (void)pass(kMm3dLayoutOwnerDrawEntry, 0x00126998U, 0x14000300U, 0x14000340U);
    (void)pass(kMm3dLayoutOwnerDrawEntry, 0x00126A64U, 0x14000340U, 0x14000380U);
    (void)pass(kMm3dLayoutDrawEntry, 0x001269DCU, 0x14000380U, 0x140003C0U);
    (void)pass(kMm3dLayoutDrawEntry, 0x00126AA8U, 0x140003C0U, 0x14000400U);
    // The frame draw's top-screen sprite queues.
    (void)pass(kMm3dQuadQueueDrawEntry, 0x00110D88U, 0x14000400U, 0x14000440U);
    (void)pass(kMm3dQuadQueueDrawEntry, 0x00110DB8U, 0x14000440U, 0x14000480U);
    Require(tracker.TakeCommandListCompositionSpans(0x14000000U, 0x480U, spans, &error), error);
    Require(spans.size() == 9U, "expected the four layout and two sprite spans");
    for (size_t index = 3; index < spans.size(); ++index) {
        Require(spans[index].Attribution.Layer == Oot3dPicaCompositionLayer::Ui &&
                    spans[index].Attribution.Provenance == Oot3dPicaCompositionProvenance::NativeUiLifecycle &&
                    !spans[index].LightBinding.WorldToView,
                "a layout span is not the interface");
    }
}

int main() {
    TestMm3dSceneLighting();
    return 0;
}
