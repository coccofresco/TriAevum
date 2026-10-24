#include "oot3d_native_pica_composition.h"
#include "fast/renderer3ds/pica_environment_lighting.h"

#include "oot3d_native_a32_fire_emitters.h"
#include "oot3d_native_a32_light_selection.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d_native_static_emitters.h"
#include "oot3d_typed_gameplay_bridge.h"
#include "recomp/a32_runtime.h"
#include "oot3d/actor_spawn.h"
#include <cstddef>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
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

void TestFireEmittersAndSelection() {
    using namespace Oot3dNativeGame;
    using Fast::Renderer3ds::PicaScenePointLight;
    // Selection: Navi and fairies first, then the nearest to Link, by
    // identity on ties, whatever the actor-list order.
    std::vector<PicaScenePointLight> candidates;
    for (uint32_t i = 0; i < 40U; ++i) {
        PicaScenePointLight light;
        light.SourceIdentity = 1000U + i;
        light.Group = 2U;
        light.Radius = 100.0F;
        light.PositionWorld = {static_cast<float>(40U - i) * 100.0F, 0.0F, 0.0F};
        candidates.push_back(light);
    }
    PicaScenePointLight navi;
    navi.SourceIdentity = 7U;
    navi.PositionWorld = {9000.0F, 0.0F, 0.0F};
    candidates.push_back(navi);
    candidates.push_back(candidates.front());
    for (auto& light : candidates) {
        light.Color = {0.3F, 0.2F, 0.1F};
        light.Radius = 500.0F;
    }
    const std::optional<std::array<float, 3>> link = std::array<float, 3>{};
    Fast::Renderer3ds::PicaScenePointLights snapshot;
    NativeSceneLightSelector selector;
    selector.Settings.Ceiling = 100.0F;
    selector.Select(candidates, link, true, snapshot);
    // The first frame fades the chosen lights in.
    Require(snapshot.Count == 1U + selector.Settings.MaximumAddedLights && snapshot.Lights[0].SourceIdentity == 7U &&
                snapshot.Lights[1].SourceIdentity == 1039U &&
                std::abs(snapshot.Lights[1].Color[0] - 0.3F * selector.Settings.FadeStep) < 1.0e-4F,
            "light selection did not keep Navi and fade in the flames nearest to Link");
    for (int frame = 0; frame < 10; ++frame) selector.Select(candidates, link, false, snapshot);
    Require(std::abs(snapshot.Lights[1].Color[0] - 0.3F) < 1.0e-4F &&
                std::none_of(snapshot.Lights.begin(), snapshot.Lights.begin() + snapshot.Count,
                             [](const auto& light) { return light.SourceIdentity == 1000U; }),
            "selected lights did not reach full light, or a far light was chosen");
    // Too much light at Link dims the added lights together.
    selector.Settings.Ceiling = 0.05F;
    for (int frame = 0; frame < 20; ++frame) selector.Select(candidates, link, false, snapshot);
    Require(selector.Stats().Exposure < 1.0F && snapshot.Lights[1].Color[0] < 0.3F,
            "the brightness ceiling did not dim the added lights");

    // Bg_Hidan_Fwbig half raised (0.1 scale: 240 tall, risen 120 of it):
    // three distinct points across the exposed wall at half intensity.
    NativeA32Memory memory;
    constexpr uint32_t play = 0x10000000U, wall = play + 0x4000U;
    Map(memory, play, 0x10000U);
    const auto scalar = [&](uint32_t offset, float value) {
        return memory.Write32(wall + offset, std::bit_cast<uint32_t>(value));
    };
    Require(scalar(0x28U, 0.0F) && scalar(0x2CU, -120.0F) && scalar(0x30U, 0.0F) && scalar(0x0CU, 0.0F) &&
                scalar(0x54U, 0.1F) && scalar(0x58U, 0.1F) && scalar(0x5CU, 0.1F) && scalar(0xC4U, 0.0F) &&
                memory.Write32(wall + 0x140U, 0x0029B1C8U) && memory.Write32(wall + 0x204U, 1U) &&
                memory.Write16(wall + 0xBEU, 0x4000U),
            "could not seed the great fire wall");
    std::vector<PicaScenePointLight> lights;
    CaptureNativeFireEmitters(memory, play, wall, 0x08DU, 0U, lights);
    Require(lights.size() == 3U && lights[0].SourceIdentity != lights[1].SourceIdentity &&
                lights[0].SourceIdentity == wall + 0x28U && std::abs(lights[0].PositionWorld[1] - 60.0F) < 1.0e-3F &&
                std::abs(lights[0].PositionWorld[2] - 264.0F) < 1.0e-2F &&
                std::abs(lights[1].Color[0] - 0.5F) < 1.0e-3F,
            "the half-raised great fire wall did not light three points across it");
    Require(scalar(0x2CU, -240.0F), "could not bury the wall");
    lights.clear();
    CaptureNativeFireEmitters(memory, play, wall, 0x08DU, 0U, lights);
    Require(lights.empty(), "a buried fire wall still emitted light");

    // The drawbridge's torches burn while the bridge is up (flame scale
    // 0.004 of 0.008: half light), at the gate; down (0), or in the scene
    // layer that draws none, they give no light. The chain actors draw none.
    constexpr uint32_t bridge = play + 0x6000U, flameScale = 0x005135B4U, sceneLayer = 0x00588E40U;
    Map(memory, flameScale & ~0xFFFU, 0x1000U);
    Map(memory, sceneLayer & ~0xFFFU, 0x1000U);
    Require(memory.Write32(bridge + 0x140U, 0x0038AF04U) && memory.Write32(flameScale, std::bit_cast<uint32_t>(0.004F)) &&
                memory.Write32(sceneLayer, 1U),
            "could not seed the drawbridge");
    lights.clear();
    CaptureNativeFireEmitters(memory, play, bridge, 0x04AU, 0xFFFFU, lights);
    Require(lights.size() == 2U && std::abs(lights[0].PositionWorld[0] - 260.0F) < 1.0e-3F &&
                std::abs(lights[1].PositionWorld[0] + 260.0F) < 1.0e-3F &&
                std::abs(lights[0].Color[0] - 0.5F) < 1.0e-3F && lights[0].SourceIdentity != lights[1].SourceIdentity,
            "the raised drawbridge's torches did not light the gate");
    lights.clear();
    CaptureNativeFireEmitters(memory, play, bridge, 0x04AU, 0U, lights);
    Require(lights.empty(), "a drawbridge chain lit the torches");
    Require(memory.Write32(sceneLayer, 12U), "could not change the scene layer");
    CaptureNativeFireEmitters(memory, play, bridge, 0x04AU, 0xFFFFU, lights);
    Require(lights.empty(), "the scene layer without torches lit them");
    Require(memory.Write32(sceneLayer, 1U) && memory.Write32(flameScale, 0U), "could not lower the bridge");
    CaptureNativeFireEmitters(memory, play, bridge, 0x04AU, 0xFFFFU, lights);
    Require(lights.empty(), "the lowered drawbridge's torches still gave light");
}

void TestSceneLightCatalog() {
    using namespace Oot3dNativeGame;
    // A standard and a Master Quest room 1 of scene 10: the loaded file's
    // command list (fingerprint) chooses which.
    NativeA32Memory memory;
    constexpr uint32_t play = 0x10000000U, file = play + 0x8000U;
    Map(memory, play, 0x10000U);
    std::array<uint8_t, kNativeRoomFingerprintBytes> commands{};
    for (size_t i = 0; i < commands.size(); ++i) commands[i] = static_cast<uint8_t>(i * 7U);
    for (size_t i = 0; i < commands.size(); ++i)
        Require(memory.Write8(file + static_cast<uint32_t>(i), commands[i]), "could not seed the room file");
    Require(memory.Write8(play + 0x4C30U, 1U) && memory.Write32(play + 0x4FFCU, file) &&
                memory.Write8(play + 0x500CU, 0xFFU),
            "could not seed the loaded room");
    const uint64_t standard = NativeRoomFingerprint(commands);
    char hex[17]{};
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(standard));
    const std::string text =
        std::string(R"({"schema":"triaevum.scene-lights","version":1,"rooms":[)") +
        R"({"scene":10,"room":1,"masterQuest":false,"fingerprint":")" + hex +
        R"(","emitters":[{"category":"window-lamp","position":[1,2,3],"radius":450,"gain":1.0,"colour":[1,0.5,0.25]},)" +
        R"({"category":"fire-lava","position":[4,5,6],"radius":600,"gain":0.5,"colour":[1,0.4,0.2]},)" +
        R"({"category":"doorway","position":[0,95,820],"radius":1330,"gain":1.6,"colour":[1,0.95,0.85]}]},)" +
        R"({"scene":10,"room":1,"masterQuest":true,"fingerprint":"0123456789abcdef","emitters":[)" +
        R"({"category":"window-lamp","position":[7,8,9],"radius":300,"gain":0.9,"colour":[1,1,1]}]}]})";
    std::string error;
    const auto catalog = NativeStaticEmitterCatalog::Parse(text, &error);
    Require(catalog && catalog->Count() == 4U, "the scene light catalog was rejected: " + error);
    const auto rooms = NativeLoadedRooms(play, memory);
    Require(rooms.size() == 1U && rooms[0].Number == 1 && rooms[0].Fingerprint == standard,
            "the loaded room or its fingerprint was not read");
    std::vector<Fast::Renderer3ds::PicaScenePointLight> lights;
    CaptureStaticEmitters(*catalog, 10U, rooms, lights);
    // Windows and doorways (daylight) cast point shadows; broad sources do not.
    Require(lights.size() == 3U && lights[0].Group == 3U && lights[0].CastsShadow && !lights[1].CastsShadow &&
                lights[2].CastsShadow && std::abs(lights[2].Color[0] - 1.6F) < 1.0e-6F &&
                lights[0].PositionWorld == std::array<float, 3>{1.0F, 2.0F, 3.0F} &&
                std::abs(lights[1].Color[0] - 0.5F) < 1.0e-6F && lights[0].SourceIdentity != lights[1].SourceIdentity,
            "the standard room's lights were not published");
    // Another file loaded as room 1 (the Master Quest one here differs): only
    // its own lights, none of another variant's.
    Require(memory.Write8(file, 0xEEU), "could not change the room file");
    lights.clear();
    CaptureStaticEmitters(*catalog, 10U, NativeLoadedRooms(play, memory), lights);
    Require(lights.empty(), "an unknown room file took another variant's lights");
    lights.clear();
    CaptureStaticEmitters(*catalog, 11U, rooms, lights);
    Require(lights.empty(), "another scene took this scene's lights");
    // Lamplight through a window casts point shadows too; a shaft does not.
    {
        const auto windows = NativeStaticEmitterCatalog::Parse(
            std::string(R"({"schema":"triaevum.scene-lights","version":1,"rooms":[{"scene":82,"room":0,)") +
            R"("masterQuest":false,"fingerprint":")" + hex + R"(","emitters":[)" +
            R"({"category":"window-lamp","position":[7,8,9],"radius":300,"gain":0.45,"colour":[1,0.8,0.5],)" +
            R"("surfaces":["285c1b592e3e8731","299c35318a700c1e"]},)" +
            R"({"category":"light-shaft","position":[7,0,9],"radius":400,"gain":0.55,"colour":[1,1,1]}]}]})");
        Require(windows != nullptr, "the window catalog was rejected");
        std::vector<NativeLoadedRoom> room0{{0, standard, file}};
        lights.clear();
        CaptureStaticEmitters(*windows, 82U, room0, lights);
        Require(lights.size() == 2U && lights[0].CastsShadow && !lights[1].CastsShadow,
                "a window's lamplight does not cast point shadows");
        // The window shines as strongly as the renderer last drew its glow;
        // drawn dark (by day) it is left out. The shaft names no surface.
        Fast::Renderer3ds::PicaSurfaceEmissionRegistry emission;
        const auto capture = [&] {
            lights.clear();
            CaptureStaticEmitters(*windows, 82U, room0, lights, &emission);
        };
        capture();
        Require(lights.size() == 2U && std::abs(lights[0].Color[0] - 0.45F) < 1.0e-6F,
                "a window not drawn yet is not at full strength");
        emission.BeginFrame();
        Require(emission.Follows(0x285c1b592e3e8731ULL) && emission.Follows(0x299c35318a700c1eULL),
                "the window's surfaces are not followed");
        emission.Observe(0x285c1b592e3e8731ULL, 0.0F);
        emission.Observe(0x299c35318a700c1eULL, 0.0F);
        emission.BeginFrame();
        capture();
        Require(lights.size() == 1U && !lights[0].CastsShadow, "a window drawn dark still shines");
        emission.Observe(0x299c35318a700c1eULL, 0.5F);
        emission.BeginFrame();
        capture();
        Require(lights.size() == 2U && std::abs(lights[0].Color[0] - 0.225F) < 1.0e-6F,
                "a window does not shine as strongly as its brightest surface");
        Require(!NativeStaticEmitterCatalog::Parse(
                    std::string(R"({"schema":"triaevum.scene-lights","version":1,"rooms":[{"scene":82,"room":0,)") +
                    R"("fingerprint":")" + hex + R"(","emitters":[{"category":"lamp","position":[0,0,0],)" +
                    R"("radius":300,"gain":0.5,"colour":[1,1,1],"surfaces":["xyz"]}]}]})"),
                "malformed emitter surfaces were accepted");
    }
    // Malformed catalogs are refused whole.
    Require(!NativeStaticEmitterCatalog::Parse(R"({"schema":"triaevum.static-emitters.v1","scenes":{}})") &&
                !NativeStaticEmitterCatalog::Parse(
                    R"({"schema":"triaevum.scene-lights","version":1,"rooms":[{"scene":1,"room":0,)"
                    R"("fingerprint":"xyz","emitters":[]}]})") &&
                !NativeStaticEmitterCatalog::Parse(
                    R"({"schema":"triaevum.scene-lights","version":1,"rooms":[{"scene":1,"room":0,)"
                    R"("fingerprint":"0123456789abcdef","emitters":[{"category":"lamp","position":[0,0,0],)"
                    R"("radius":0,"gain":1,"colour":[1,1,1]}]}]})"),
            "a malformed scene light catalog was accepted");
    // A torch stand is no emitter (its flame is an actor's): a catalog that
    // still lists one predates that and is refused, to be regenerated.
    Require(!NativeStaticEmitterCatalog::Parse(
                R"({"schema":"triaevum.scene-lights","version":1,"rooms":[{"scene":1,"room":0,)"
                R"("fingerprint":"0123456789abcdef","emitters":[{"category":"torch","position":[0,0,0],)"
                R"("radius":450,"gain":1,"colour":[1,1,1]}]}]})"),
            "a catalog listing a torch stand was accepted");
    // Wrong types and unbounded values are refused, never thrown.
    Require(!NativeStaticEmitterCatalog::Parse(R"({"schema":123,"version":1,"rooms":[]})") &&
                !NativeStaticEmitterCatalog::Parse(
                    R"({"schema":"triaevum.scene-lights","version":1,"rooms":[{"scene":1,"room":0,)"
                    R"("fingerprint":"0123456789abcdef","emitters":[{"category":7,"position":[0,0,0],)"
                    R"("radius":10,"gain":1,"colour":[1,1,1]}]}]})") &&
                !NativeStaticEmitterCatalog::Parse(
                    R"({"schema":"triaevum.scene-lights","version":1,"rooms":[{"scene":1,"room":0,)"
                    R"("fingerprint":"0123456789abcdef","emitters":[{"category":"lamp","position":[0,0,0],)"
                    R"("radius":1e100,"gain":1,"colour":[1,1,1]}]}]})"),
            "a mistyped or unbounded scene light catalog was accepted");
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

int main() {
    TestSelectedLightSnapshot();
    TestProjectedShadowOwnership();
    TestDeferredAtmosphereOwnership();
    TestDeferredActorOwnership();
    TestRestoredSceneLightOwnership();
    TestFireEmittersAndSelection();
    TestSceneLightCatalog();
    TestRestoredNativeEnvironmentLights();
    TestTimeDrivenEnvironmentLights();
    TestEnvironmentKeyDirection();
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
             std::pair{0x0041EC50U, 0x0030055CU},
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
