#include "oot3d_native_pica_composition.h"
#include "mm3d_pica_composition.h"
#include "mm3d_title_contracts.h"
#include "fast/renderer3ds/pica_environment_lighting.h"

#include "oot3d_native_a32_memory.h"
#include "oot3d_typed_gameplay_bridge.h"
#include "recomp/a32_runtime.h"
#include "oot3d/actor_spawn.h"
#include <cstddef>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_pica_composition_tests: " << message << '\n';
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

void TestSelectedLightSnapshot() {
    using Light = Fast::Renderer3ds::PicaNativeLightSource;
    Oot3dNativeGame::NativeA32Memory memory;
    Map(memory, 0x0054C000U, 0x1000U);
    Map(memory, 0x10000000U, 0x1000U);
    Map(memory, 0x005B5000U, 0x1000U);
    const auto writeView = [&](const std::array<float, 12>& view) {
        for (size_t i = 0; i < view.size(); ++i) {
            Require(memory.Write32(0x005B5018U + static_cast<uint32_t>(i * 4U),
                                  std::bit_cast<uint32_t>(view[i])), "could not write native view");
            const float identity = i == 0 || i == 5 || i == 10 ? 1.0F : 0.0F;
            Require(memory.Write32(0x005B5058U + static_cast<uint32_t>(i * 4U),
                                  std::bit_cast<uint32_t>(identity)), "could not write native post-view");
        }
    };
    constexpr uint32_t lights = 0x10000400U;
    constexpr uint32_t point = 0x10000500U;
    constexpr uint32_t renderer = 0x10000600U;
    constexpr uint32_t renderState = 0x10000700U;
    constexpr uint32_t record = lights + 0x60U;
    Require(memory.Write32(renderer + 0x10U, renderState) &&
                memory.Write32(0x10000010U, 0x20U) &&
                memory.Write16(point + 0x10U, 100U),
            "could not seed light ownership");
    for (uint32_t channel = 0; channel < 3U; ++channel) {
        Require(memory.Write8(point + 0xCU + channel, 200U) &&
                    memory.Write8(record + channel, 100U),
                "could not seed native light colors");
    }
    Oot3dNativeGame::Oot3dNativePicaCompositionTracker tracker;
    oot3d::recomp::a32::GuestState state{};
    const auto bindPoint = [&](float x) {
        Require(memory.Write32(point, std::bit_cast<uint32_t>(x)),
                "could not move point light");
        state.r[0] = lights;
        state.r[1] = point;
        state.r[2] = record;
        tracker.ObserveBlockEntry(0x00213DFCU, state, memory);
        state.r[0] = renderState;
        state.r[1] = 2U;
        state.r[5] = record;
        state.r[6] = lights;
        state.r[14] = 0x0035BEE8U;
        tracker.ObserveBlockEntry(0x0033D14CU, state, memory);
    };
    const auto emit = [&](uint32_t cursor) {
        Require(memory.Write32(0x0054CC4CU, cursor), "could not advance light draw cursor");
        state.r[0] = 0x10000000U;
        tracker.ObserveBlockEntry(Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry, state, memory);
    };
    bindPoint(10.0F);
    state.r[0] = renderer;
    state.r[1] = 0U;
    state.r[14] = 0x002FADDCU;
    tracker.ObserveBlockEntry(Oot3dNativeGame::kOot3dCmbRendererSubmitDrawHandleEntry, state, memory);
    writeView({1,0,0,0, 0,1,0,0, 0,0,1,-11});
    emit(0x14001000U);
    bindPoint(20.0F);
    writeView({0,0,1,0, 0,1,0,0, -1,0,0,0});
    // A non-commuting post-view rotation must use the geometry's order.
    Require(memory.Write32(0x005B5058U, std::bit_cast<uint32_t>(0.0F)) &&
                memory.Write32(0x005B505CU, std::bit_cast<uint32_t>(-1.0F)) &&
                memory.Write32(0x005B5068U, std::bit_cast<uint32_t>(1.0F)) &&
                memory.Write32(0x005B506CU, std::bit_cast<uint32_t>(0.0F)),
            "could not rotate native post-view");
    emit(0x14001020U);
    state.r[0] = renderState;
    state.r[1] = 2U;
    tracker.ObserveBlockEntry(0x0033D174U, state, memory);
    emit(0x14001040U);
    std::vector<Oot3dNativeGame::Oot3dPicaCommandListCompositionSpan> spans;
    std::string error;
    Require(tracker.TakeCommandListCompositionSpans(0x14001000U, 0x60U, spans, &error), error);
    Require(spans.size() == 3U &&
                spans[0].LightBinding.Sources[2].Kind == Light::Type::Point &&
                spans[0].LightBinding.Sources[2].PositionWorld[0] == 10.0F &&
                spans[1].LightBinding.Sources[2].PositionWorld[0] == 20.0F &&
                spans[0].LightBinding.Sources[0].Kind == Light::Type::Unavailable &&
                spans[2].LightBinding.Sources[2].Kind == Light::Type::Unavailable,
            "adjacent draws lost selected slots, immutable light state, or explicit override invalidation");
    // A later gameplay camera must not move a surface in an earlier packet.
    const std::array<float, 4> worldPoint{3,5,7,1};
    const std::array<std::array<float, 3>, 2> expectedView{{{3,5,-4}, {-5,7,-3}}};
    for (size_t draw = 0; draw < expectedView.size(); ++draw) {
        const auto& view = spans[draw].LightBinding.WorldToView;
        Require(view.has_value(), "geometry-synchronous lighting view was not captured");
        for (size_t row = 0; row < 3; ++row) {
            float actual = 0;
            for (size_t column = 0; column < 4; ++column)
                actual += (*view)[column * 4 + row] * worldPoint[column];
            Require(actual == expectedView[draw][row], "live camera moved an immutable lighting surface");
        }
    }
}

void TestProjectedShadowOwnership() {
    using namespace Oot3dNativeGame;
    NativeA32Memory memory;
    Map(memory, 0x0054C000U, 0x1000U);
    Map(memory, 0x10000000U, 0x1000U);
    constexpr uint32_t model = 0x10000400U, shadow = 0x10000600U, ordinary = 0x10000700U;
    Require(memory.Write32(model + 0x14U, shadow) &&
            memory.Write32(model + 0x18U, shadow) &&
            memory.Write32(0x10000010U, 0x20U), "could not seed shadow model");
    Oot3dNativePicaCompositionTracker tracker;
    oot3d::recomp::a32::GuestState state{};
    uint32_t cursor = 0x14001000U;
    const auto emit = [&](uint32_t renderer) {
        state.r[0] = renderer;
        state.r[1] = 1U;
        state.r[14] = 0x002FADDCU;
        tracker.ObserveBlockEntry(kOot3dCmbRendererSubmitDrawHandleEntry, state, memory);
        Require(memory.Write32(0x0054CC4CU, cursor), "could not move shadow packet cursor");
        state.r[0] = 0x10000000U;
        tracker.ObserveBlockEntry(kOot3dMeshCommandPacketSubmitEntry, state, memory);
        tracker.ObserveBlockEntry(0x002FADDCU, state, memory);
        cursor += 0x20U;
    };
    state.r[0] = model;
    state.r[14] = 0x004BFC8CU; // Player's separate mirrored-model route is not a blob shadow.
    tracker.ObserveBlockEntry(kOot3dModelQueueSubmitEntry, state, memory);
    emit(shadow);
    state.r[0] = model;
    state.r[14] = 0x001D04DCU;
    tracker.ObserveBlockEntry(kOot3dModelQueueSubmitEntry, state, memory);
    emit(shadow);
    emit(ordinary);
    tracker.ObserveBlockEntry(kOot3dGameplayDrawEntry, state, memory);
    emit(shadow); // Reusing a renderer next frame must not retain its old role.
    state.r[0] = model;
    state.r[14] = 0x0033EA3CU;
    tracker.ObserveBlockEntry(kOot3dModelQueueSubmitEntry, state, memory);
    emit(shadow);
    tracker.ObserveBlockEntry(kOot3dGameplayDrawEntry, state, memory);
    state.r[0] = model;
    state.r[14] = 0x0033E684U; // ActorShadow_DrawFeet's floor-aligned model producer.
    tracker.ObserveBlockEntry(kOot3dModelQueueSubmitEntry, state, memory);
    emit(shadow);
    emit(ordinary);
    std::vector<Oot3dPicaCommandListCompositionSpan> spans;
    std::string error;
    Require(tracker.TakeCommandListCompositionSpans(0x14001000U, 0xE0U, spans, &error), error);
    // Adjacent ordinary spans can coalesce, but preserve every command byte's owner.
    const std::array expected{false, true, false, false, true, true, false};
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto address = 0x14001000U + static_cast<uint32_t>(i) * 0x20U;
        const auto found = std::find_if(spans.begin(), spans.end(), [&](const auto& span) {
            return span.BeginAddress <= address && address < span.EndAddress;
        });
        Require(found != spans.end() && found->Attribution.NativeProjectedShadow == expected[i],
                "shadow producer ownership was lost, overextended, or retained across frames");
    }
}

void TestDeferredAtmosphereOwnership() {
    using namespace Oot3dNativeGame;
    NativeA32Memory memory;
    Map(memory, 0x0054C000U, 0x1000U);
    Map(memory, 0x10000000U, 0x1000U);
    Require(memory.Write32(0x10000010U, 0x20U) &&
            memory.Write32(0x0054CC4CU, 0x14001000U), "could not seed deferred model packets");
    Oot3dNativePicaCompositionTracker tracker;
    oot3d::recomp::a32::GuestState state{};
    constexpr uint32_t queue = 0x10000400U, renderer = 0x10000600U;
    uint32_t skyBlend = 2;
    const auto enqueueAtmosphere = [&] {
        state.r[2] = 7;
        state.r[3] = skyBlend++; // The sky blend steps over time.
        state.r[14] = 0x002E2B8CU;
        tracker.ObserveBlockEntry(kOot3dSceneRendererSubmitConfiguredModelPassEntry, state, memory);
        state.r[1] = queue;
        state.r[2] = 0x10000800U;
        tracker.ObserveBlockEntry(kOot3dModelQueueSetSlotEntry, state, memory);
        tracker.ObserveBlockEntry(0x002E2B8CU, state, memory);
    };
    uint32_t cursor = 0x14001000U;
    const auto emit = [&](uint32_t slot, uint32_t pass) {
        state.r[0] = renderer;
        state.r[1] = pass;
        state.r[4] = 1;
        state.r[8] = slot - 8U;
        state.r[14] = pass == 0 ? 0x002FAED8U : 0x002FADF4U;
        tracker.ObserveBlockEntry(kOot3dCmbRendererSubmitDrawHandleEntry, state, memory);
        Require(memory.Write32(0x0054CC4CU, cursor), "could not move deferred packet cursor");
        state.r[0] = 0x10000000U;
        tracker.ObserveBlockEntry(kOot3dMeshCommandPacketSubmitEntry, state, memory);
        tracker.ObserveBlockEntry(state.r[14], state, memory);
        cursor += 0x20U;
        Require(memory.Write32(0x0054CC4CU, cursor), "could not complete deferred packet");
    };
    enqueueAtmosphere();
    emit(queue, 0);
    emit(queue, 1);
    emit(queue + 8U, 0); // The same renderer in another slot is ordinary world.
    state.r[1] = queue;
    state.r[2] = 0x10000800U;
    tracker.ObserveBlockEntry(kOot3dModelQueueSetSlotEntry, state, memory);
    emit(queue, 0); // Replacing a slot outside its old owner removes that role.
    enqueueAtmosphere();
    tracker.ObserveBlockEntry(kOot3dGameplayDrawEntry, state, memory);
    emit(queue, 0); // Neither pointers nor queue slots retain roles next frame.
    std::vector<Oot3dPicaCommandListCompositionSpan> spans;
    std::string error;
    Require(tracker.TakeCommandListCompositionSpans(0x14001000U, 0xA0U, spans, &error), error);
    for (uint32_t i = 0; i < 5; ++i) {
        const uint32_t address = 0x14001000U + i * 0x20U;
        const auto found = std::find_if(spans.begin(), spans.end(), [&](const auto& span) {
            return span.BeginAddress <= address && address < span.EndAddress;
        });
        const auto layer = i < 2 ? Oot3dPicaCompositionLayer::Atmosphere : Oot3dPicaCompositionLayer::OpaqueWorld;
        Require(found != spans.end() && found->Attribution.Layer == layer,
                "deferred atmosphere ownership was lost or leaked across queue slots/lifetimes");
        if (i < 2)
            // The sky selector identifies the draw; the blend does not.
            Require(found->Attribution.SourcePc == kOot3dSceneRendererSubmitConfiguredModelPassEntry &&
                    found->Attribution.NativeValue == 7U,
                    "deferred atmosphere lost its native producer provenance or kept the sky blend");
    }
}

void TestDeferredActorOwnership() {
    using namespace Oot3dNativeGame;
    NativeA32Memory memory;
    Map(memory, 0x0054C000U, 0x1000U);
    Map(memory, 0x10000000U, 0x1000U);
    Require(memory.Write32(0x10000010U, 0x20U), "could not seed actor packet");
    Oot3dNativePicaCompositionTracker tracker;
    oot3d::recomp::a32::GuestState state{};
    constexpr uint32_t queue = 0x10000400U, renderer = 0x10000600U;
    constexpr uint32_t player = 0x10000900U, horse = 0x10000B00U;
    Require(memory.Write16(player, 0) && memory.Write16(horse, 0x14U), "could not seed actor identities");
    const auto enqueue = [&](uint32_t slot) {
        state.r[1] = slot;
        state.r[2] = 0x10000800U;
        tracker.ObserveBlockEntry(kOot3dModelQueueSetSlotEntry, state, memory);
    };
    uint32_t cursor = 0x14001000U;
    const auto emit = [&](uint32_t slot, uint32_t pass) {
        state.r[0] = renderer;
        state.r[1] = pass;
        state.r[4] = 1;
        state.r[8] = slot - 8U;
        state.r[14] = pass == 0 ? 0x002FAED8U : 0x002FADF4U;
        tracker.ObserveBlockEntry(kOot3dCmbRendererSubmitDrawHandleEntry, state, memory);
        Require(memory.Write32(0x0054CC4CU, cursor), "could not move actor packet cursor");
        state.r[0] = 0x10000000U;
        tracker.ObserveBlockEntry(kOot3dMeshCommandPacketSubmitEntry, state, memory);
        tracker.ObserveBlockEntry(state.r[14], state, memory);
        cursor += 0x20U;
    };
    state.r[1] = player;
    tracker.ObserveBlockEntry(kOot3dActorDrawEntry, state, memory);
    enqueue(queue);
    tracker.ObserveBlockEntry(kOot3dActorDrawCallbackReturn, state, memory);
    emit(queue, 0);
    emit(queue, 1);
    emit(queue + 8U, 0); // The same renderer is not inherently an actor.
    enqueue(queue);
    emit(queue, 0); // Slot replacement removes its prior native owner.
    state.r[1] = player;
    tracker.ObserveBlockEntry(kOot3dActorDrawEntry, state, memory);
    state.r[1] = horse;
    tracker.ObserveBlockEntry(kOot3dActorDrawEntry, state, memory);
    enqueue(queue);
    tracker.ObserveBlockEntry(kOot3dActorDrawCallbackReturn, state, memory);
    enqueue(queue + 8U); // Returning from a child preserves the parent scope.
    tracker.ObserveBlockEntry(kOot3dActorDrawCallbackReturn, state, memory);
    emit(queue, 0);
    emit(queue + 8U, 0);
    tracker.ObserveBlockEntry(kOot3dGameplayDrawEntry, state, memory);
    emit(queue, 0);
    std::vector<Oot3dPicaCommandListCompositionSpan> spans;
    std::string error;
    Require(tracker.TakeCommandListCompositionSpans(0x14001000U, 0xE0U, spans, &error), error);
    const std::array expected{true, true, false, false, true, true, false};
    const std::array<uint64_t, 7> owners{player, player, 0, 0, (uint64_t{0x14} << 32U) | horse, player, 0};
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto address = 0x14001000U + static_cast<uint32_t>(i) * 0x20U;
        const auto found = std::find_if(spans.begin(), spans.end(), [&](const auto& span) {
            return span.BeginAddress <= address && address < span.EndAddress;
        });
        const auto layer = i == 1 ? Oot3dPicaCompositionLayer::TransparentWorld
                                  : Oot3dPicaCompositionLayer::OpaqueWorld;
        Require(found != spans.end() && found->Attribution.NativeActorGeometry == expected[i] &&
                    found->Attribution.NativeActorIdentity == owners[i] &&
                    found->Attribution.Layer == layer,
                "actor ownership was lost, leaked across queue lifetimes, or replaced native pass order");
    }
}

void TestRestoredSceneLightOwnership() {
    using namespace Oot3dNativeGame;
    NativeA32Memory memory;
    constexpr uint32_t play = 0x10000000U, navi = play + 0x4000U;
    constexpr uint32_t fairy = play + 0x5000U, nodes = play + 0x8000U;
    Map(memory, play, 0x10000U);
    const uint32_t list = play + OOT3D_ACTOR_CONTEXT_PLAY_OFFSET + offsetof(Oot3dActorContext, lists);
    Require(memory.Write32(list, 2U) && memory.Write32(list + 4U, navi), "could not seed restored actor list");
    for (uint32_t actor : {navi, fairy}) {
        Require(memory.Write16(actor + offsetof(Oot3dActor, id), 0x18U) &&
                memory.Write8(actor + offsetof(Oot3dActor, category), 0U) &&
                memory.Write16(actor + offsetof(Oot3dActor, params), actor == navi ? 0U : 2U) &&
                memory.Write32(actor + offsetof(Oot3dActor, update), 0x001D732CU) &&
                memory.Write32(actor + offsetof(Oot3dActor, next), actor == navi ? fairy : 0U),
                "could not seed restored fairy ownership");
    }
    const std::array infos{navi + 0x948U, navi + 0x964U, fairy + 0x948U};
    for (uint32_t i = 0; i < infos.size(); ++i) {
        const uint32_t info = infos[i], node = nodes + i * 12U;
        Require(memory.Write32(info + 0x18U, node) && memory.Write32(node, info) &&
                memory.Write32(info + 4U, std::bit_cast<uint32_t>(10.0F + i)) &&
                memory.Write8(info + 0x10U + i, 255U) &&
                memory.Write16(info + 0x14U, i == 1 ? 0U : 80U),
                "could not seed native light records");
    }
    NativeA32LightingProbe probe;
    // No Init hook is replayed after loading a savestate.
    probe.CaptureSceneLights(play, memory);
    const auto restored = probe.CaptureRenderer(0, memory).ScenePoints;
    Require(restored && restored->Count == 3 &&
                restored->Lights[0].Group == 0 && restored->Lights[1].Radius == 0 &&
                restored->Lights[2].Group == 1 && restored->Lights[2].PositionWorld[0] == 12,
            "restored scene lost native light ownership, groups, or inactive source suppression");
    Require(memory.Write32(navi + offsetof(Oot3dActor, update), 0) &&
                memory.Write32(infos[2] + 4U, std::bit_cast<uint32_t>(20.0F)), "could not remove native Navi");
    probe.CaptureSceneLights(play, memory);
    const auto next = probe.CaptureRenderer(0, memory).ScenePoints;
    Require(next && next->Count == 1 && next->Lights[0].PositionWorld[0] == 20 &&
                restored->Count == 3 && restored->Lights[2].PositionWorld[0] == 12,
            "actor destruction retained a light or mutated an earlier geometry snapshot");
    const auto scalar = [&](uint32_t offset, float value) {
        return memory.Write32(fairy + offset, std::bit_cast<uint32_t>(value));
    };
    Require(memory.Write16(infos[2] + 0x14U, 0U) &&
                memory.Write32(fairy + offsetof(Oot3dActor, draw), 0x001D6EC4U) &&
                scalar(0x28U, 30.0F) && scalar(0x2CU, 55.0F) &&
                scalar(0x54U, 0.008F) && scalar(0x924U, 0.5F) &&
                scalar(0x928U, 255.0F) && scalar(0x92CU, 128.0F) &&
                scalar(0x930U, 64.0F) && scalar(0x934U, 255.0F),
            "could not seed a visible fairy with a dormant native light");
    probe.CaptureSceneLights(play, memory);
    const auto emitting = probe.CaptureRenderer(0, memory).ScenePoints;
    Require(emitting && emitting->Lights[0].Radius > 0 &&
                emitting->Lights[0].PositionWorld[0] == 30.0F &&
                emitting->Lights[0].PositionWorld[1] == 55.0F &&
                emitting->Lights[0].Color[0] == 0.5F,
            "fairy emission used dormant light coordinates instead of the live glow");
    Require(memory.Write16(fairy + 0x9C4U, 8U), "could not hide fairy glow");
    probe.CaptureSceneLights(play, memory);
    const auto hidden = probe.CaptureRenderer(0, memory).ScenePoints;
    Require(hidden && hidden->Lights[0].Radius == 0 &&
                hidden->Lights[0].SourceIdentity == emitting->Lights[0].SourceIdentity &&
                emitting->Lights[0].Color[0] == 0.5F,
            "a hidden fairy emitted light or lost native-slot suppression ownership");
    Require(memory.Write32(nodes + 24U, 0), "could not recycle native light node");
    probe.CaptureSceneLights(play, memory);
    Require(!probe.CaptureRenderer(0, memory).ScenePoints,
            "a recycled LightNode remained attributed to its old actor");
}

void TestRestoredNativeEnvironmentLights() {
    using namespace Oot3dNativeGame;
    NativeA32Memory memory;
    constexpr uint32_t play = 0x10000000U, nodes = play + 0x8000U;
    Map(memory, play, 0x10000U);
    Require(memory.Write32(play + 0xA70U, nodes), "could not seed LightContext");
    const std::array infos{play + 0x31BCU, play + 0x31D4U, play + 0x7000U};
    for (uint32_t i = 0; i < infos.size(); ++i) {
        Require(memory.Write32(nodes + i * 12U, infos[i]) &&
                    memory.Write32(nodes + i * 12U + 8U, i < 2 ? nodes + (i + 1U) * 12U : 0U) &&
                    memory.Write8(infos[i], 1U) &&
                    memory.Write8(infos[i] + 5U, i == 1 ? 129U : 127U) &&
                    memory.Write8(infos[i] + 7U + i, 255U),
                "could not seed native environment light");
    }
    NativeA32LightingProbe probe;
    probe.CaptureSceneLights(play, memory);
    const auto restored = probe.CaptureRenderer(0, memory).SceneDirectionals;
    Require(restored && restored->Count == 2 &&
                restored->Lights[0].SourceIdentity == infos[0] + 4U &&
                restored->Lights[0].DirectionTowardSource[1] == 1.0F &&
                restored->Lights[1].DirectionTowardSource[1] == -1.0F &&
                restored->Lights[0].Color[0] == 1.0F && restored->Lights[1].Color[1] == 1.0F,
            "restored scene environment lost ownership, signed direction or live color");
    Require(memory.Write32(nodes + 8U, nodes + 24U) &&
                memory.Write8(infos[0] + 7U, 0U), "could not update native environment");
    probe.CaptureSceneLights(play, memory);
    const auto changed = probe.CaptureRenderer(0, memory).SceneDirectionals;
    Require(changed && changed->Count == 1 && changed->Lights[0].Color[0] == 0.0F &&
                restored->Count == 2 && restored->Lights[0].Color[0] == 1.0F,
            "unlinked source survived or a live environment update mutated old geometry");
    probe.CaptureSceneLights(0, memory);
    Require(!probe.CaptureRenderer(0, memory).SceneDirectionals,
            "native environment leaked after leaving gameplay");
}

void TestTimeDrivenEnvironmentLights() {
    using namespace Oot3dNativeGame;
    NativeA32Memory memory;
    constexpr uint32_t play = 0x10000000U, nodes = play + 0x8000U, environment = play + 0x3190U;
    constexpr uint32_t dayTime = 0x00587964U;
    Map(memory, play, 0x10000U);
    Map(memory, 0x00587000U, 0x1000U);
    const std::array infos{play + 0x31BCU, play + 0x31D4U};
    Require(memory.Write32(play + 0xA70U, nodes) && memory.Write8(environment + 0x20U, 0U) &&
                memory.Write8(environment + 0xA7U, 0xFFU),
            "could not seed a time-driven environment");
    for (uint32_t i = 0; i < infos.size(); ++i)
        Require(memory.Write32(nodes + i * 12U, infos[i]) &&
                    memory.Write32(nodes + i * 12U + 8U, i == 0 ? nodes + 12U : 0U) &&
                    memory.Write8(infos[i], 1U) && memory.Write8(infos[i] + 7U, 200U) &&
                    memory.Write8(infos[i] + 8U, 180U) && memory.Write8(infos[i] + 9U, 160U),
                "could not seed native environment light");
    // The records the clock gives, as Environment_Update stores them.
    const auto seed = [&](uint16_t clock) {
        const double angle = double(std::bit_cast<int16_t>(uint16_t(clock - 0x8000U))) * (2.0 * std::numbers::pi / 65536.0);
        const std::array<int, 3> bytes{int(-std::sin(angle) * 120.0), int(std::cos(angle) * 120.0),
                                       int(std::cos(angle) * 20.0)};
        bool written = memory.Write16(dayTime, clock);
        for (uint32_t axis = 0; axis < 3U; ++axis)
            written = written && memory.Write8(infos[0] + 4U + axis, uint8_t(int8_t(bytes[axis]))) &&
                memory.Write8(infos[1] + 4U + axis, uint8_t(int8_t(-bytes[axis])));
        Require(written, "could not seed the clock and its records");
        return angle;
    };
    NativeA32LightingProbe probe;
    double angle = seed(0x5123U);
    probe.CaptureSceneLights(play, memory);
    auto lights = probe.CaptureRenderer(0, memory).SceneDirectionals;
    Require(lights && lights->Count == 2 && lights->Lights[0].SkyLight && lights->Lights[1].SkyLight,
            "time-driven environment lights were not published as sky lights");
    const auto& sun = lights->Lights[0].SourceIdentity == infos[0] + 4U ? lights->Lights[0] : lights->Lights[1];
    const auto& moon = &sun == &lights->Lights[0] ? lights->Lights[1] : lights->Lights[0];
    Require(std::abs(sun.DirectionTowardSource[0] - float(-std::sin(angle) * 120.0 / 127.0)) < 1.0e-6F &&
                std::abs(sun.DirectionTowardSource[1] - float(std::cos(angle) * 120.0 / 127.0)) < 1.0e-6F &&
                moon.DirectionTowardSource[1] == -sun.DirectionTowardSource[1],
            "the sun did not take the clock's exact direction");
    Require(lights->ShadowCasterIdentity == (sun.DirectionTowardSource[1] > 0 ? sun : moon).SourceIdentity,
            "the light above the horizon does not cast");
    const uint64_t epoch = lights->ContinuityEpoch;
    // The next update: the direction moves, in the same epoch.
    seed(0x5123U + 7U);
    probe.CaptureSceneLights(play, memory);
    auto next = probe.CaptureRenderer(0, memory).SceneDirectionals;
    const auto& nextSun = next->Lights[0].SourceIdentity == infos[0] + 4U ? next->Lights[0] : next->Lights[1];
    Require(next->ContinuityEpoch == epoch && nextSun.DirectionTowardSource != sun.DirectionTowardSource,
            "continuous clock motion did not move the sun within its epoch");
    // A set-time jump is a discontinuity.
    seed(0x5123U + 0x2000U);
    probe.CaptureSceneLights(play, memory);
    Require(probe.CaptureRenderer(0, memory).SceneDirectionals->ContinuityEpoch != epoch,
            "a jump of the clock did not start a new epoch");
    // Another writer: the records no longer match the clock; their bytes apply.
    seed(0x5123U + 0x2007U);
    Require(memory.Write8(infos[0] + 4U, 5U) && memory.Write8(infos[1] + 4U, uint8_t(int8_t(-5))),
            "could not override the sun record");
    probe.CaptureSceneLights(play, memory);
    lights = probe.CaptureRenderer(0, memory).SceneDirectionals;
    const auto& written = lights->Lights[0].SourceIdentity == infos[0] + 4U ? lights->Lights[0] : lights->Lights[1];
    Require(written.SkyLight && written.DirectionTowardSource[0] == 5.0F / 127.0F,
            "records written over the clock did not take their own bytes");
    // A light setting override: the environment's own fixed lights.
    Require(memory.Write8(environment + 0xA7U, 3U), "could not override the light setting");
    probe.CaptureSceneLights(play, memory);
    lights = probe.CaptureRenderer(0, memory).SceneDirectionals;
    Require(!lights->Lights[0].SkyLight && !lights->Lights[1].SkyLight,
            "fixed light settings were published as sky lights");
}

void TestEnvironmentKeyDirection() {
    using namespace Fast::Renderer3ds;
    const auto near = [](const std::array<float, 3>& value, const std::array<float, 3>& expected) {
        for (size_t axis = 0; axis < 3; ++axis)
            if (std::abs(value[axis] - expected[axis]) > 1.0e-5F) return false;
        return true;
    };
    PicaSceneDirectionalLights scene;
    scene.Count = 2;
    scene.Lights[0] = {2, {0.0F, -1.6F, -1.2F}, {0.2F, 0.2F, 0.2F}};
    scene.Lights[1] = {1, {0.0F, 0.6F, 0.8F}, {1.0F, 1.0F, 1.0F}};
    PicaEnvironmentLighting environment;
    environment.Lights[0].DirectionTowardSource = {1.0F, 0.0F, 0.0F};
    Require(near(ResolvePicaEnvironmentKeyDirection(environment, &scene), {1.0F, 0.0F, 0.0F}),
            "an authored key light followed the scene");
    environment.KeyDirection = PicaEnvironmentKeyDirection::SceneMain;
    Require(FindPicaSceneMainDirectional(scene) == &scene.Lights[1] &&
                near(ResolvePicaEnvironmentKeyDirection(environment, &scene), {0.0F, 0.6F, 0.8F}),
            "the key light did not take the brightest scene light");
    environment.KeyAzimuthOffset = std::numbers::pi_v<float> / 2.0F;
    Require(near(ResolvePicaEnvironmentKeyDirection(environment, &scene), {0.8F, 0.6F, 0.0F}),
            "the azimuth offset did not turn toward +X from +Z");
    environment.KeyElevationOffset = -std::asin(0.6F);
    Require(near(ResolvePicaEnvironmentKeyDirection(environment, &scene), {1.0F, 0.0F, 0.0F}),
            "the elevation offset did not lower the key light");
    environment.KeyDirection = PicaEnvironmentKeyDirection::SceneAmbient;
    Require(near(ResolvePicaEnvironmentKeyDirection(environment, &scene), {0.0F, 0.8F, 0.6F}),
            "the ambient light axis was not turned to the main light's side");
    Require(near(ResolvePicaEnvironmentKeyDirection(environment, nullptr), {1.0F, 0.0F, 0.0F}),
            "a draw without a scene snapshot lost the authored key direction");
    // The key takes the main light's colour, tinted by its own.
    environment.Lights[0].Color = {1.0F, 0.5F, 1.0F};
    Require(near(ResolvePicaEnvironmentKeyColor(environment, &scene), {1.0F, 0.5F, 1.0F}),
            "a scene-following key did not take the main light's colour");
    scene.Lights[1].Color = {0.2F, 0.3F, 0.5F};
    Require(near(ResolvePicaEnvironmentKeyColor(environment, &scene), {0.2F, 0.15F, 0.5F}),
            "the key's colour did not follow the scene light's colour");
    Require(near(ResolvePicaEnvironmentKeyColor(environment, nullptr), {1.0F, 0.5F, 1.0F}),
            "a draw without a scene snapshot lost the authored key colour");
    environment.KeyDirection = PicaEnvironmentKeyDirection::Authored;
    Require(near(ResolvePicaEnvironmentKeyColor(environment, &scene), {1.0F, 0.5F, 1.0F}),
            "an authored key took the scene's colour");
    environment.KeyDirection = PicaEnvironmentKeyDirection::SceneAmbient;
    scene.Lights[0].Color = scene.Lights[1].Color = {};
    Require(near(ResolvePicaEnvironmentKeyDirection(environment, &scene), {1.0F, 0.0F, 0.0F}),
            "a scene without an emitting light replaced the authored key direction");
    Require(near(ResolvePicaEnvironmentKeyColor(environment, &scene), {0.0F, 0.0F, 0.0F}),
            "a scene without an emitting light still lit the key");
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

    Mm3dNativePicaCompositionTracker tracker;
    oot3d::recomp::a32::GuestState state{};
    const auto pass = [&](uint32_t entry, uint32_t returnPc, uint32_t begin, uint32_t end) {
        Require(memory.Write32(kMm3dCommandListCursorAddress, begin), "could not seed the cursor");
        state.r[14] = returnPc;
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
    Require(first.SourceIdentity == light1 && first.DirectionTowardSource[1] == 1.0F &&
                first.Color[0] == 1.0F && std::abs(first.Color[1] - 128.0F / 255.0F) < 1.0e-6F,
            "light 1 was not decoded first");
    Require(second.SourceIdentity == light2 && second.DirectionTowardSource[0] == 1.0F,
            "light 2 was not decoded");
    Require(tracker.Stats().LightBindings == 1U && tracker.Stats().SceneDirectionalLights == 2U,
            "the lighting statistics are wrong");

    // Without a current View (another game state), no lighting.
    Require(memory.Write32(kMm3dCurrentViewPointer, 0U), "could not clear the current View");
    (void)pass(kMm3dRenderListVisitEntry, 0x0011EF3CU, 0x14000200U, 0x14000240U);
    Require(tracker.TakeCommandListCompositionSpans(0x14000000U, 0x300U, spans, &error), error);
    Require(spans.size() == 3U && !spans[2].LightBinding.WorldToView && !spans[2].LightBinding.SceneDirectionals,
            "a frame without a current View carried lighting");

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
    TestSelectedLightSnapshot();
    TestProjectedShadowOwnership();
    TestDeferredAtmosphereOwnership();
    TestDeferredActorOwnership();
    TestRestoredSceneLightOwnership();
    TestRestoredNativeEnvironmentLights();
    TestTimeDrivenEnvironmentLights();
    TestEnvironmentKeyDirection();
    TestMm3dSceneLighting();
    constexpr uint32_t kWriterWrapper = 0x10000200U;
    constexpr uint32_t kWriterContext = 0x10000300U;

    Oot3dNativeGame::NativeA32Memory memory;
    Map(memory, 0x0054C000U, 0x1000U);
    Map(memory, 0x10000000U, 0x1000U);
    Require(memory.Write32(0x0054CC4CU, 0x14001000U),
            "could not seed native command-list cursor");
    Require(memory.Write32(0x10000010U, 0x20U),
            "could not seed native mesh packet size");
    Require(memory.Write16(0x1000011CU, 4U),
            "could not seed ObjectKankyo selector");
    Require(memory.Write32(kWriterWrapper, kWriterContext),
            "could not seed primitive-writer indirection");

    Oot3dNativeGame::Oot3dNativePicaCompositionTracker tracker;
    oot3d::recomp::a32::GuestState state{};

    const auto emitPrimitive =
        [&](uint32_t beginAddress, uint32_t endAddress,
            uint32_t returnPc) {
            Require(memory.Write32(kWriterContext + 8U, beginAddress),
                    "could not seed primitive packet cursor");
            state.r[0] = kWriterWrapper;
            state.r[14] = returnPc;
            tracker.ObserveBlockEntry(
                Oot3dNativeGame::kOot3dPicaPrimitivePacketBuilderEntry,
                state, memory);
            Require(memory.Write32(kWriterContext + 8U, endAddress),
                    "could not advance primitive packet cursor");
            tracker.ObserveBlockEntry(returnPc, state, memory);
        };

    state.r[1] = 0U;
    state.r[14] = 0x002FADDCU;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dCmbRendererSubmitDrawHandleEntry, state,
        memory);
    emitPrimitive(0x14001000U, 0x14001008U, 0x004527BCU);
    state.r[0] = 0x10000000U;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry, state,
        memory);
    tracker.ObserveBlockEntry(0x002FADDCU, state, memory);

    emitPrimitive(0x14001020U, 0x14001030U, 0x004527BCU);

    Require(memory.Write32(0x0054CC4CU, 0x14001030U),
            "could not seed kankyo command cursor");
    state.r[14] = 0x003FBC5CU;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dKankyoEffectPrimitiveDrawEntry, state,
        memory);
    emitPrimitive(0x14001030U, 0x14001040U, 0x003FB984U);
    Require(memory.Write32(0x0054CC4CU, 0x14001040U),
            "could not close kankyo command range");
    tracker.ObserveBlockEntry(0x003FBC5CU, state, memory);

    state.r[14] = 0xDEADBEEFU;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dRendererCommandListExecuteEntry, state,
        memory);

    state.r[14] = 0x002E2C04U;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dRendererCommandListExecuteEntry, state,
        memory);
    state.r[0] = 0x10000000U;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry, state,
        memory);
    Require(memory.Write32(0x0054CC4CU, 0x14001080U),
            "could not advance through native atmosphere owner");
    tracker.ObserveBlockEntry(0x002E2C04U, state, memory);

    state.r[0] = 0x10000100U;
    state.r[14] = Oot3dNativeGame::kOot3dActorDrawCallbackReturn;
    tracker.ObserveBlockEntry(Oot3dNativeGame::kOot3dObjectKankyoDrawEntry,
                              state, memory);
    state.r[0] = 0x10000000U;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry, state,
        memory);
    Require(memory.Write32(0x0054CC4CU, 0x140010A0U),
            "could not advance past native actor atmosphere packet");
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dActorDrawCallbackReturn, state, memory);

    state.r[1] = 1U;
    state.r[14] = 0x002FADF4U;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dCmbRendererSubmitDrawHandleEntry, state,
        memory);
    state.r[0] = 0x10000000U;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry, state,
        memory);
    tracker.ObserveBlockEntry(0x002FADF4U, state, memory);

    std::vector<Oot3dNativeGame::Oot3dPicaCommandListCompositionSpan> spans;
    std::string error;
    Require(tracker.TakeCommandListCompositionSpans(
                0x14001000U, 0xC0U, spans, &error),
            error);
    Require(spans.size() == 6U &&
                spans[0].BeginAddress == 0x14001000U &&
                spans[0].EndAddress == 0x14001020U &&
                spans[0].Attribution.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld &&
                spans[0].Attribution.NativeValue == 0U &&
                spans[1].BeginAddress == 0x14001020U &&
                spans[1].EndAddress == 0x14001030U &&
                spans[1].Attribution.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::Unknown &&
                spans[1].Attribution.SourcePc ==
                    Oot3dNativeGame::kOot3dPicaPrimitivePacketBuilderEntry &&
                spans[1].Attribution.NativeValue == 0x004527BCU &&
                spans[2].BeginAddress == 0x14001030U &&
                spans[2].EndAddress == 0x14001040U &&
                spans[2].Attribution.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::Atmosphere &&
                spans[2].Attribution.SourcePc ==
                    Oot3dNativeGame::kOot3dKankyoEffectPrimitiveDrawEntry &&
                spans[3].BeginAddress == 0x14001040U &&
                spans[3].EndAddress == 0x14001080U &&
                spans[3].Attribution.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::Atmosphere &&
                spans[3].Attribution.SourcePc ==
                    Oot3dNativeGame::kOot3dRendererCommandListExecuteEntry &&
                spans[4].BeginAddress == 0x14001080U &&
                spans[4].EndAddress == 0x140010A0U &&
                spans[4].Attribution.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::Atmosphere &&
                spans[4].Attribution.SourcePc ==
                    Oot3dNativeGame::kOot3dObjectKankyoDrawEntry &&
                spans[4].Attribution.NativeValue == 4U &&
                spans[5].BeginAddress == 0x140010A0U &&
                spans[5].EndAddress == 0x140010C0U &&
                spans[5].Attribution.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::
                        TransparentWorld &&
                spans[5].Attribution.NativeValue == 1U,
            "native composition spans were not recovered exactly");
    Require(tracker.Stats().RecordedPacketSpans == 4U &&
                tracker.Stats().ConsumedPacketSpans == 6U &&
                tracker.Stats().CmbPassExits == 2U &&
                tracker.Stats().PrimitivePacketEntries == 3U &&
                tracker.Stats().PrimitivePacketExits == 3U &&
                tracker.Stats().PrimitivePacketSpans == 3U &&
                tracker.Stats().PrimitivePacketBytes == 0x28U &&
                tracker.Stats().NestedPrimitivePacketEntries == 0U &&
                tracker.Stats().InvalidPrimitivePacketReturnAddresses == 0U &&
                tracker.Stats().InvalidPrimitivePacketRanges == 0U &&
                tracker.Stats().UnmatchedPrimitivePacketExits == 0U &&
                tracker.Stats().IgnoredUnrelatedCommandListExecutions == 1U &&
                tracker.Stats().AtmosphereScopeEntries == 3U &&
                tracker.Stats().AtmosphereScopeExits == 3U &&
                tracker.Stats().AtmospherePacketSpans == 2U &&
                tracker.Stats().DirectAtmosphereSpans == 3U &&
                tracker.Stats().DirectAtmosphereBytes == 0x70U &&
                tracker.Stats().InvalidAtmosphereReturnAddresses == 0U &&
                tracker.Stats().InvalidAtmosphereCommandRanges == 0U &&
                tracker.Stats().RecoveredAtmospherePacketSpans == 0U &&
                tracker.Stats().ReadFailures == 0U,
            "native composition tracker statistics are wrong");
    Require(tracker.TakeCommandListCompositionSpans(
                0x14001000U, 0xC0U, spans, &error) && spans.empty(),
            "native composition spans were not consumed once");

    tracker.Reset();
    memory.Write32(0x0054CC4CU, 0x14001000U);
    state.r[14] = 0x00419524U;
    tracker.ObserveBlockEntry(0x00300328U, state, memory);
    Require(tracker.Stats().UiScopeEntries == 0,
            "mixed world/UI container must not become a UI owner");
    state.r[1] = 6U;
    tracker.ObserveBlockEntry(0x002FEA30U, state, memory);
    tracker.ObserveBlockEntry(0x0041AFACU, state, memory);
    Require(tracker.Stats().UiScopeEntries == 0,
            "shared queue executor or unrelated orthographic caller classified UI");
    state.r[14] = 0x12345678U;
    tracker.ObserveBlockEntry(0x0042B9F4U, state, memory);
    Require(tracker.Stats().UiScopeEntries == 0, "unrelated view caller classified UI");
    state.r[14] = 0x003004E0U;
    tracker.ObserveBlockEntry(0x0042B9F4U, state, memory);
    memory.Write32(0x0054CC4CU, 0x14001080U);
    tracker.ObserveBlockEntry(0x003004E0U, state, memory);
    Require(tracker.TakeCommandListCompositionSpans(0x14001000U, 0x100U, spans, &error) &&
                spans.size() == 1 && spans[0].BeginAddress == 0x14001000U &&
                spans[0].EndAddress == 0x14001080U &&
                spans[0].Attribution.Layer == Oot3dNativeGame::Oot3dPicaCompositionLayer::Ui &&
                tracker.Stats().UiCommandSpans == 1,
            "native UI view did not publish its exact command span");
    tracker.Reset();
    tracker.ObserveBlockEntry(0x003004E0U, state, memory);
    Require(tracker.Stats().UiScopeExits == 0, "reset retained a UI view scope");
    for (const auto [entry, returnPc] : std::array{
             std::pair{0x0041F308U, 0x0041983CU},
             std::pair{0x00425930U, 0x0041EB14U},
            std::pair{0x0041AFACU, 0x00419890U},
            std::pair{0x00471F60U, 0x0030051CU},
            std::pair{0x00477E30U, 0x0041984CU},
            std::pair{0x002FFF7CU, 0x00419824U},
            std::pair{0x0042CB54U, 0x00300530U},
            std::pair{0x0042A278U, 0x0030053CU},
            std::pair{0x00427A3CU, 0x00300548U},
            std::pair{0x0042CBCCU, 0x00300554U}}) {
        tracker.Reset();
        memory.Write32(0x0054CC4CU, 0x14001000U);
        state.r[14] = returnPc;
        tracker.ObserveBlockEntry(entry, state, memory);
        memory.Write32(0x0054CC4CU, 0x14001040U);
        tracker.ObserveBlockEntry(returnPc, state, memory);
        Require(tracker.TakeCommandListCompositionSpans(0x14001000U, 0x100U, spans, &error) &&
                    spans.size() == 1 && spans[0].Attribution.SourcePc == entry &&
                    spans[0].Attribution.Layer == Oot3dNativeGame::Oot3dPicaCompositionLayer::Ui,
                "pause backdrop/map/overlay lost UI command ownership");
    }
    tracker.Reset();
    memory.Write32(0x0054CC4CU, 0x14001000U);
    state.r[14] = 0x00300524U;
    tracker.ObserveBlockEntry(0x004228E4U, state, memory);
    state.r[14] = 0x003FBC5CU;
    tracker.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dKankyoEffectPrimitiveDrawEntry, state, memory);
    emitPrimitive(0x14002000U, 0x14002040U, 0x003FB984U);
    tracker.ObserveBlockEntry(0x003FBC5CU, state, memory);
    tracker.ObserveBlockEntry(0x00300524U, state, memory);
    Require(tracker.TakeCommandListCompositionSpans(0x14002000U, 0x40U, spans, &error) &&
                spans.size() == 1 && spans[0].Attribution.SourcePc == 0x004228E4U &&
                spans[0].Attribution.Layer == Oot3dNativeGame::Oot3dPicaCompositionLayer::Ui &&
                tracker.Stats().OverlappingPacketSpans == 0,
            "deferred UI primitive lost ownership to the shared environment writer");
    std::cout << "oot3d_native_pica_composition_tests: ok\n";
    return 0;
}
