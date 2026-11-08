#include "mm3d_native_a32_lighting.h"
#include "mm3d_pica_composition.h"
#include "mm3d_title_contracts.h"
#include "mm3d_lighting_hook_sites.h"
#include "mm3d_local_light_sources.h"
#include "mm3d_room_emitters.h"
#include "mm3d_synthetic_emitters.h"
#include <cstdio>
#include "oot3d_native_a32_memory.h"
#include "fast/renderer3ds/pica_resolved_draw_stream.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <span>

using namespace Oot3dNativeGame;
using namespace Fast::Renderer3ds;
namespace {
void Require(bool yes, const char* message) {
    if (!yes) { std::cerr << "mm3d_native_lighting_tests: " << message << '\n'; std::exit(1); }
}
struct Fixture {
    NativeA32Memory Memory;
    oot3d::recomp::a32::GuestState State{};
    static constexpr uint32_t Play = 0x01000000U, Actor = 0x02000000U, Owner = 0x02001000U,
        Context = 0x02002000U, Renderer = 0x02003000U, Model = 0x02004000U,
        Queue = 0x02005000U, Params = 0x02006004U, Nodes = 0x02007000U;
    Fixture() {
        for (auto [address, size] : {std::pair{0x00100000U, 0x00540000U},
             std::pair{0x00650000U, 0x001B0000U}, std::pair{Play, 0x20000U}, std::pair{Actor, 0x10000U}}) {
            std::string error;
            Require(Memory.MapRegion({"mm3d-lighting-test", address, size, true, false, {}}, &error), "map fixture");
        }
        for (const auto& hook : kMm3dLightingHookSites)
            for (uint32_t i = 0; i < 4; ++i) W32(hook.Pc + 4U * i, hook.Words[i]);
        for (const auto [pc, word] : kMm3dCompositionOwnerGuards) W32(pc, word);
        W32(kMm3dCurrentViewPointer, Play + kMm3dPlayViewOffset);
        W8(Play + 0x13DU, 4); W32(Play + 0x4A8U, Play); W16(Play + 0x148U, 0x6F); W32(Play + 0x138U, 1);
        W8(Play + 0x87FFU, 0xFF); W32(Play + 0x8824U, std::bit_cast<uint32_t>(1.0F));
        W16(0x7751F8U, 0x8000U);
        const float rows[] = {1,0,0,-10,0,1,0,-20,0,0,1,-30};
        for (uint32_t i = 0; i < 12; ++i) F32(Play + kMm3dPlayViewOffset + 0x17CU + i * 4U, rows[i]);
        W32(Renderer + 0x10U, Context); W8(Context + 0x6CU, 15);
        W32(Model, 0x0063BED4U); W32(Model + 0xA0U, Renderer);
        W16(Actor, 0); W8(Actor + 2U, 2); W32(Actor + 0x140U, 0x100100U); W32(Actor + 0x144U, 0x1F8E28U);
    }
    void W8(uint32_t p, uint8_t v) { Require(Memory.Write8(p,v), "write8 fixture"); }
    void W16(uint32_t p, uint16_t v) { Require(Memory.Write16(p,v), "write16 fixture"); }
    void W32(uint32_t p, uint32_t v) { Require(Memory.Write32(p,v), "write32 fixture"); }
    void F32(uint32_t p, float v) { W32(p,std::bit_cast<uint32_t>(v)); }
    void Point(uint32_t params, int16_t radius) {
        W8(params - 4U, 2); F32(params, 10); F32(params + 4U, 20); F32(params + 8U, 30);
        W8(params + 12U, 255); W8(params + 13U, 128); W8(params + 14U, 64);
        W8(params + 16U, 12); W16(params + 18U, std::bit_cast<uint16_t>(radius));
    }
    void Direction(uint32_t info, int8_t y, int8_t z, uint8_t color) {
        W8(info, 1); W8(info + 4U, 0); W8(info + 5U, std::bit_cast<uint8_t>(y)); W8(info + 6U, std::bit_cast<uint8_t>(z));
        for (uint32_t i=0;i<3;++i) W8(info + 7U + i, color);
    }
    void Link(uint32_t node, uint32_t info, uint32_t next) { W32(node,info); W32(node+8U,next); }
};

void TestCompositionTemporalLifetimes() {
    Fixture f;
    Mm3dNativePicaCompositionTracker tracker;
    auto& s = f.State;
    Require(tracker.VerifyHooks(f.Memory), "temporal hooks verified");
    const auto observe = [&](uint32_t pc) { tracker.ObserveBlockEntry(pc, s, f.Memory); };
    uint32_t frame = 1;
    const auto beginFrame = [&] {
        f.W32(f.Play + 0x138U, frame++);
        s.r[0] = f.Play; observe(0x15E9ECU);
    };
    const auto enqueue = [&](uint16_t id = 0, uint32_t callback = 0x1F8E28U, bool overlay = false, bool wrongReturn = false) {
        f.W16(f.Actor, id); f.W32(f.Actor + 0x144U, callback);
        s.r[0] = f.Actor; s.r[2] = callback; observe(0x182C44U);
        f.W32(f.Queue + 4U, 0);
        if (overlay) {
            f.W32(f.Play + 8U, f.Queue);
            s.r[0] = f.Play; s.r[1] = f.Model; s.r[14] = 0x39F100U;
            observe(0x11F000U);
        }
        s.r[0] = f.Queue; s.r[1] = f.Model;
        s.r[14] = overlay ? (wrongReturn ? 0x39F104U : 0x39F100U) : 0x20A8F8U;
        observe(0x224104U);
        f.W32(f.Queue + 8U, f.Model); f.W32(f.Queue + 4U, 1);
        observe(0x182C48U);
    };
    const auto consume = [&](uint32_t pass = 0x11EF3CU) {
        f.W32(kMm3dCommandListCursorAddress, 0x14000000U);
        s.r[0] = f.Queue; s.r[14] = pass; observe(kMm3dRenderListVisitEntry);
        s.r[0] = f.Model; s.r[5] = f.Queue + 8U; observe(0x5EF0CCU);
        f.W32(kMm3dCommandListCursorAddress, 0x14000020U);
        observe(0x5EF0D0U); observe(pass);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;
        tracker.TakeCommandListCompositionSpans(0, 0, spans);
        Require(spans.size() == 1, "one precise model range");
        return spans.front().Attribution;
    };
    beginFrame(); enqueue();
    const auto first = consume();
    Require(first.TemporalIdentity.Available() && first.TemporalCommandBufferIdentity &&
            first.TemporalIdentity.Owner == f.Actor && first.TemporalIdentity.Instance == f.Model &&
            !first.ExcludeFromPresentationFxaa,
            "guarded actor/model did not publish a temporal source-buffer contract");
    beginFrame(); enqueue();
    Require(consume().TemporalIdentity == first.TemporalIdentity, "continuous lifetime changed each frame");
    for (uint32_t site : {0x191238U, 0x1749C4U, 0x174EBCU, 0x191200U, 0x5EC220U}) {
        beginFrame(); enqueue();
        s.r[0] = site == 0x5EC220U ? f.Model : f.Actor; s.r[4] = f.Actor; observe(site);
        const auto dead = consume();
        Require(!dead.TemporalIdentity.Available() && !dead.NativeActorGeometry && !dead.IsolatedLighting,
                "queued ownership survived native lifetime end");
        beginFrame(); enqueue();
        const auto replacement = consume();
        Require(replacement.TemporalIdentity.Available() &&
                replacement.TemporalIdentity.Lifetime != first.TemporalIdentity.Lifetime,
                "recycled actor/model matched the former lifetime");
    }
    beginFrame();
    f.W32(f.Queue + 4U, 0);
    s.r[0] = f.Queue; s.r[1] = f.Model; s.r[14] = 0x20A8F8U; observe(0x224104U);
    f.W32(f.Queue + 8U, f.Model); f.W32(f.Queue + 4U, 1);
    const auto background = consume();
    Require(background.TemporalIdentity.Available() && !background.NativeActorGeometry &&
            (background.TemporalIdentity.Owner & (1ULL << 63U)), "scene-owned model lost native temporal identity");
    tracker.Reset(); beginFrame(); enqueue();
    Require(consume().TemporalIdentity.Lifetime != first.TemporalIdentity.Lifetime,
            "restore rewound temporal lifetime allocator");
    beginFrame(); enqueue(); const auto beforeScene = consume().TemporalIdentity;
    f.W16(f.Play + 0x148U, 0x61); beginFrame(); enqueue();
    Require(consume().TemporalIdentity.Lifetime != beforeScene.Lifetime, "scene change retained lifetime");
    // Room change: scene-owned room model gets a new generation; a live
    // actor's own lifetime is untouched (its destroy hooks own that boundary).
    {
        const auto bgEnqueue = [&] {
            beginFrame(); f.W32(f.Queue + 4U, 0);
            s.r[0] = f.Queue; s.r[1] = f.Model; s.r[14] = 0x20A8F8U; observe(0x224104U);
            f.W32(f.Queue + 8U, f.Model); f.W32(f.Queue + 4U, 1);
            return consume().TemporalIdentity;
        };
        const auto rooms = [&](uint8_t cur, uint8_t prev) { f.W8(f.Play + 0xC260U, cur); f.W8(f.Play + 0xC2C4U, prev); };
        rooms(0, 0xFF);
        const auto roomA = bgEnqueue();
        Require(bgEnqueue().Lifetime == roomA.Lifetime, "room model lifetime unstable within a room");
        beginFrame(); enqueue(); const auto actorA = consume().TemporalIdentity;
        rooms(1, 0xFF);
        beginFrame(); enqueue();
        Require(consume().TemporalIdentity.Lifetime == actorA.Lifetime, "room change reset a live actor lifetime");
        rooms(0, 0xFF); bgEnqueue(); const auto roomA2 = bgEnqueue();
        rooms(1, 0xFF);
        const auto roomB = bgEnqueue();
        Require(roomB.Available() && roomB.Lifetime != roomA2.Lifetime, "room change kept room model lifetime");
        rooms(0, 0xFF);
        Require(bgEnqueue().Lifetime != roomA.Lifetime, "returning room reused stale lifetime");
        // Two-room transition: room 0 stays loaded in prevRoom while room 1
        // loads into curRoom; both are drawn, so history must survive.
        const auto before = bgEnqueue();
        const auto changes0 = tracker.Stats().RoomSlotChanges, ends0 = tracker.Stats().RoomLifetimeEnds;
        rooms(1, 0);
        Require(bgEnqueue().Lifetime == before.Lifetime, "two-room transition dropped loaded room history");
        Require(tracker.Stats().RoomSlotChanges == changes0 + 1U && tracker.Stats().RoomLifetimeEnds == ends0 &&
                tracker.Stats().LastCurrentRoom == 1 && tracker.Stats().LastPreviousRoom == 0,
                "two-room transition miscounted room slot statistics");
        rooms(0, 1);
        Require(bgEnqueue().Lifetime == before.Lifetime, "slot swap of loaded rooms dropped history");
        // Transition end: room 1 leaves both slots.
        rooms(0, 0xFF);
        const auto afterUnload = bgEnqueue();
        Require(afterUnload.Lifetime != before.Lifetime, "room unload kept room model lifetime");
        Require(tracker.Stats().RoomLifetimeEnds == ends0 + 1U && tracker.Stats().RoomSlotChanges == changes0 + 3U &&
                tracker.Stats().LastPreviousRoom == -1, "room unload miscounted room slot statistics");
        // Invalid room number (>127, not the empty marker) is unknown.
        rooms(0x80, 0xFF);
        const auto invalid = bgEnqueue();
        Require(invalid.Lifetime != afterUnload.Lifetime, "invalid room value kept room model lifetime");
        rooms(0, 0x90);
        Require(bgEnqueue().Lifetime == invalid.Lifetime, "unknown room state was not stable");
        rooms(0, 0xFF);
        const auto recovered = bgEnqueue();
        Require(recovered.Lifetime != invalid.Lifetime, "recovery from invalid room reused unknown lifetime");
        Require(bgEnqueue().Lifetime == recovered.Lifetime, "room lifetime unstable after recovery");
        // A failed slot read is unknown, never a match.
        // Leave a hole over the RoomContext page so the slot read fails.
        std::vector<uint8_t> saved(0x20000U);
        std::string error;
        Require(f.Memory.ReadBytes(f.Play, saved), "save play");
        const auto remap = [&](bool hole) {
            Require(f.Memory.UnmapRegion(f.Play, hole ? 0x20000U : 0xC000U, &error) &&
                    (hole || f.Memory.UnmapRegion(f.Play + 0xD000U, 0x13000U, &error)), "unmap play");
            const auto map = [&](uint32_t offset, uint32_t size) {
                Require(f.Memory.MapRegion({"mm3d-lighting-test", f.Play + offset, size, true, false, {}}, &error) &&
                        f.Memory.WriteBytes(f.Play + offset, std::span<const uint8_t>(saved).subspan(offset, size)), "map play");
            };
            if (hole) { map(0, 0xC000U); map(0xD000U, 0x13000U); } else map(0, 0x20000U);
        };
        remap(true);
        const auto unreadable = bgEnqueue();
        remap(false);
        Require(unreadable.Lifetime != recovered.Lifetime, "failed room read kept room model lifetime");
    }
    const auto beforeRollback = consume().TemporalIdentity;
    frame = 1; beginFrame(); enqueue();
    Require(consume().TemporalIdentity.Lifetime != beforeRollback.Lifetime, "frame rollback retained lifetime");
    beginFrame(); enqueue(0x87, 0x39F068U);
    const auto logo = consume(0x11EFD4U);
    Require(logo.IsolatedLighting && logo.ExcludeFromPresentationFxaa &&
            logo.Layer == Oot3dPicaCompositionLayer::Ui &&
            !logo.TemporalIdentity.Available(), "title logo lost isolated native UI ownership");
    beginFrame(); enqueue(0x87, 0x39F068U);
    const auto worldLogo = consume();
    Require(worldLogo.IsolatedLighting && worldLogo.ExcludeFromPresentationFxaa &&
            !worldLogo.TemporalIdentity.Available(), "world-queued logo acquired scene temporal history");
    beginFrame(); enqueue(0x41, 0x4C03F4U);
    Require(consume().Layer == Oot3dPicaCompositionLayer::Atmosphere, "kankyo model became relightable world");
    // Non-CMB renderables retain callback-proved semantics, but never gain
    // a temporal contract for an unknown model ABI.
    f.W32(f.Model, 0U);
    beginFrame(); enqueue(0x41, 0x4C03F4U);
    const auto effect = consume();
    Require(effect.Layer == Oot3dPicaCompositionLayer::Atmosphere && !effect.TemporalIdentity.Available(),
            "non-CMB kankyo renderable lost atmosphere ownership");
    beginFrame(); enqueue(0x87, 0x39F068U, true);
    Require(consume(0x11EFD4U).IsolatedLighting, "native 2D helper lost logo isolation");
    beginFrame(); enqueue(0x87, 0x39F068U, true, true);
    Require(!consume(0x11EFD4U).IsolatedLighting, "unmatched 2D helper return acquired logo ownership");
    f.W32(f.Model, 0x63BED4U);
    beginFrame(); enqueue(0x41, 0x1F8E28U);
    Require(consume().Layer == Oot3dPicaCompositionLayer::OpaqueWorld, "actor id alone classified atmosphere");
    beginFrame(); enqueue(); f.W32(f.Model, 0U);
    // A new unproved model cannot acquire temporal ownership.
    s.r[0] = f.Model; observe(0x5EC220U);
    beginFrame(); enqueue();
    Require(!consume().TemporalIdentity.Available(), "unproved model ABI got identity");
    for (const auto [pc, word] : kMm3dCompositionOwnerGuards) {
        f.W32(pc, word ^ 1U);
        Require(!tracker.VerifyHooks(f.Memory), "corrupt owner guard accepted");
        f.W32(pc, word);
    }
}

void TestDeferredModelResourceLifetime() {
    Fixture f;
    Mm3dNativePicaCompositionTracker tracker;
    auto& s = f.State;
    Require(tracker.VerifyHooks(f.Memory), "resource lifetime guards");
    const auto observe = [&](uint32_t pc) { tracker.ObserveBlockEntry(pc, s, f.Memory); };
    uint32_t frame = 1;
    const auto enqueue = [&] {
        f.W32(f.Play + 0x138U, frame++); s.r[0] = f.Play; observe(0x15E9ECU);
        s.r[0] = f.Actor; s.r[2] = 0x1F8E28U; observe(0x182C44U);
        f.W32(f.Queue + 4U, 0);
        s.r[0] = f.Queue; s.r[1] = f.Model; s.r[14] = 0x20A8F8U; observe(0x224104U);
        f.W32(f.Queue + 4U, 1); f.W32(f.Queue + 8U, f.Model); observe(0x182C48U);
    };
    const auto begin = [&] {
        f.W32(kMm3dCommandListCursorAddress, 0x14000000U);
        s.r[0] = f.Queue; s.r[14] = 0x11EF3CU; observe(kMm3dRenderListVisitEntry);
        s.r[0] = f.Model; s.r[5] = f.Queue + 8U; observe(0x5EF0CCU);
    };
    const auto finish = [&] {
        f.W32(kMm3dCommandListCursorAddress, 0x14000020U);
        s.r[5] = f.Queue + 8U; observe(0x5EF0D0U); observe(0x11EF3CU);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;
        tracker.TakeCommandListCompositionSpans(0, 0, spans);
        Require(spans.size() == 1, "resource lifetime keeps native model range");
        return spans.front().Attribution;
    };
    enqueue(); begin(); const auto original = finish();
    Require(original.TemporalIdentity.Available(), "resource fixture baseline identity");
    // Primary renderer first: this already belongs to the admitted ABI.
    // The remaining handles are owned/released by the same guarded destructor.
    for (uint32_t offset : {0xA0U, 0xA4U, 0x90U, 0x94U, 0x98U, 0x9CU, 0xA8U, 0U}) {
        uint32_t saved = 0; Require(f.Memory.Read32(f.Model + offset, &saved), "read original handle");
        for (bool duringDraw : {false, true}) {
            enqueue();
            if (duringDraw) begin();
            f.W32(f.Model + offset, saved ^ 0x100U);
            if (!duringDraw) begin();
            const auto rebound = finish();
            Require(!rebound.TemporalIdentity.Available() && !rebound.TemporalCommandBufferIdentity,
                    "deferred rebind retained stale temporal identity");
            Require(rebound.NativeActorGeometry && rebound.NativeActorIdentity == f.Actor,
                    "resource rejection erased independent native actor ownership");
            f.W32(f.Model + offset, saved);
            enqueue(); begin(); const auto restored = finish();
            Require(restored.TemporalIdentity.Available() &&
                    restored.TemporalIdentity.Lifetime != original.TemporalIdentity.Lifetime,
                    "observed resource ABA revived old epoch");
        }
    }
    // Rebind before enqueue also rotates the epoch, even with stable renderers.
    enqueue(); begin(); const auto before = finish();
    f.W32(f.Model + 0x90U, f.Owner);
    enqueue(); begin(); const auto after = finish();
    Require(after.TemporalIdentity.Available() && after.TemporalIdentity.Lifetime != before.TemporalIdentity.Lifetime,
            "resource-only rebind at enqueue retained old epoch");
    enqueue(); begin(); Require(finish().TemporalIdentity == after.TemporalIdentity,
                               "unchanged resources churned temporal identity");
    // Destruction while a scope is open revokes unsubmitted history as well.
    for (uint32_t pc : {0x5EC220U, 0x191238U}) {
        enqueue(); begin(); s.r[0] = pc == 0x5EC220U ? f.Model : f.Actor; observe(pc);
        Require(!finish().TemporalIdentity.Available(), "active scope survived native destruction");
    }
    Require(original.TemporalIdentity.Available(), "immutable historical attribution was mutated");
}

// F03: in-place recycling the native destroy hooks did not observe. The
// actor identity is (id << 32 | address); the model lifetime is keyed by its
// temporal owner, owner epoch and resource handles. A different actor at the
// same address, or a model handed to another owner, must never match history.
void TestUnobservedInPlaceRecycling() {
    Fixture f;
    Mm3dNativePicaCompositionTracker tracker;
    auto& s = f.State;
    Require(tracker.VerifyHooks(f.Memory), "recycling hooks verified");
    const auto observe = [&](uint32_t pc) { tracker.ObserveBlockEntry(pc, s, f.Memory); };
    uint32_t frame = 1;
    const auto enqueue = [&](uint16_t id, bool actor) {
        f.W32(f.Play + 0x138U, frame++); s.r[0] = f.Play; observe(0x15E9ECU);
        if (actor) { f.W16(f.Actor, id); f.W32(f.Actor + 0x144U, 0x1F8E28U);
            s.r[0] = f.Actor; s.r[2] = 0x1F8E28U; observe(0x182C44U); }
        f.W32(f.Queue + 4U, 0);
        s.r[0] = f.Queue; s.r[1] = f.Model; s.r[14] = 0x20A8F8U; observe(0x224104U);
        f.W32(f.Queue + 4U, 1); f.W32(f.Queue + 8U, f.Model);
        if (actor) observe(0x182C48U);
        f.W32(kMm3dCommandListCursorAddress, 0x14000000U);
        s.r[0] = f.Queue; s.r[14] = 0x11EF3CU; observe(kMm3dRenderListVisitEntry);
        s.r[0] = f.Model; s.r[5] = f.Queue + 8U; observe(0x5EF0CCU);
        f.W32(kMm3dCommandListCursorAddress, 0x14000020U);
        s.r[5] = f.Queue + 8U; observe(0x5EF0D0U); observe(0x11EF3CU);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;
        tracker.TakeCommandListCompositionSpans(0, 0, spans);
        Require(spans.size() == 1, "recycling fixture keeps one model range");
        return spans.front().Attribution.TemporalIdentity;
    };
    const auto a = enqueue(0x10, true);
    Require(a.Available() && enqueue(0x10, true) == a, "recycling baseline identity unstable");
    // Same address, different actor id, no destroy site observed.
    const auto b = enqueue(0x11, true);
    Require(b.Available() && b.Owner != a.Owner && b.Lifetime != a.Lifetime,
            "unobserved actor recycling at the same address matched the former lifetime");
    // The former id returning at that address is a new instance, not a revival.
    const auto a2 = enqueue(0x10, true);
    Require(a2.Available() && a2.Lifetime != a.Lifetime && a2.Lifetime != b.Lifetime,
            "actor ABA at the same address revived an old lifetime");
    // Same model address handed from the actor to a scene owner and back.
    const auto scene = enqueue(0, false);
    Require(scene.Available() && (scene.Owner & (1ULL << 63U)) && scene.Lifetime != a2.Lifetime,
            "model recycled to a scene owner kept the actor lifetime");
    const auto back = enqueue(0x10, true);
    Require(back.Available() && back.Lifetime != scene.Lifetime && back.Lifetime != a2.Lifetime,
            "model returned to an actor revived a former lifetime");
    Require(enqueue(0x10, true) == back, "recycled identity not stable afterwards");
}

void TestActorEpochRenewedOnIdChange() {
    Fixture f;
    Mm3dNativePicaCompositionTracker tracker;
    auto& s = f.State;
    Require(tracker.VerifyHooks(f.Memory), "recycling hooks verified");
    const auto observe = [&](uint32_t pc) { tracker.ObserveBlockEntry(pc, s, f.Memory); };
    uint32_t frame = 1;
    const auto enqueue = [&](uint16_t id, bool actor, uint32_t model) {
        f.W32(f.Play + 0x138U, frame++); s.r[0] = f.Play; observe(0x15E9ECU);
        if (actor) { f.W16(f.Actor, id); f.W32(f.Actor + 0x144U, 0x1F8E28U);
            s.r[0] = f.Actor; s.r[2] = 0x1F8E28U; observe(0x182C44U); }
        f.W32(f.Queue + 4U, 0);
        s.r[0] = f.Queue; s.r[1] = model; s.r[14] = 0x20A8F8U; observe(0x224104U);
        f.W32(f.Queue + 4U, 1); f.W32(f.Queue + 8U, model);
        if (actor) observe(0x182C48U);
        f.W32(kMm3dCommandListCursorAddress, 0x14000000U);
        s.r[0] = f.Queue; s.r[14] = 0x11EF3CU; observe(kMm3dRenderListVisitEntry);
        s.r[0] = model; s.r[5] = f.Queue + 8U; observe(0x5EF0CCU);
        f.W32(kMm3dCommandListCursorAddress, 0x14000020U);
        s.r[5] = f.Queue + 8U; observe(0x5EF0D0U); observe(0x11EF3CU);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;
        tracker.TakeCommandListCompositionSpans(0, 0, spans);
        Require(spans.size() == 1, "recycling fixture keeps one model range");
        return spans.front().Attribution.TemporalIdentity;
    };
    // A second model owned only by the original actor instance keeps a record
    // (owner identity, owner epoch). When the slot is recycled to another id
    // and the former id returns, the owner identity matches again, so only a
    // renewed actor epoch can prevent the parked model from reviving.
    const uint32_t parked = f.Model + 0x800U;
    f.W32(parked, 0x0063BED4U); f.W32(parked + 0xA0U, f.Renderer);
    const auto p1 = enqueue(0x10, true, parked);
    Require(p1.Available() && enqueue(0x10, true, parked) == p1, "parked model baseline unstable");
    const auto other = enqueue(0x11, true, f.Model);
    Require(other.Available(), "recycled actor id lost identity");
    const auto p2 = enqueue(0x10, true, parked);
    Require(p2.Available() && p2.Owner == p1.Owner && p2.Lifetime != p1.Lifetime,
            "actor lifetime kept across an unobserved id change revived a parked model");
    Require(enqueue(0x10, true, parked) == p2, "renewed parked identity not stable");
}

void TestPrimitiveOwnership() {
    Fixture f;
    Mm3dNativePicaCompositionTracker tracker;
    auto& s = f.State;
    Require(tracker.VerifyHooks(f.Memory), "primitive hook guards");
    const auto observe = [&](uint32_t pc) { tracker.ObserveBlockEntry(pc, s, f.Memory); };
    const auto run = [&](bool validRenderer, uint32_t index, uint32_t size, uint32_t rebindPhase = 0U) {
        tracker.Reset();
        f.W32(f.Model + 0x90U, f.Context);
        s.r[0] = f.Play; observe(0x15E9ECU);
        s.r[0] = f.Actor; s.r[2] = 0x1F8E28U; observe(0x182C44U);
        f.W32(f.Queue + 4U, 0);
        s.r[0] = f.Queue; s.r[1] = f.Model; s.r[14] = 0x20A8F8U; observe(0x224104U);
        f.W32(f.Queue + 4U, 1); f.W32(f.Queue + 8U, f.Model); observe(0x182C48U);
        f.W32(kMm3dCommandListCursorAddress, 0x14000000U);
        s.r[0] = f.Queue; s.r[14] = 0x11EF3CU; observe(kMm3dRenderListVisitEntry);
        s.r[0] = f.Model; s.r[5] = f.Queue + 8U; observe(0x5EF0CCU);
        f.W32(f.Renderer + 0x14U, f.Owner); f.W32(f.Owner, f.Owner + 0x100U);
        f.W32(f.Owner + 0x108U, 0x15000000U);
        f.W32(f.Nodes + 8U, 2); f.W16(f.Nodes + 0x1CU, 12);
        s.r[0] = f.Owner; s.r[2] = 12; s.r[4] = validRenderer ? f.Renderer : 0U;
        s.r[7] = f.Nodes; s.r[5] = index; s.r[14] = 0x5E32F8U;
        if (rebindPhase == 1U) f.W32(f.Model + 0x90U, f.Context + 0x100U);
        observe(0x1565ACU);
        if (rebindPhase == 2U) f.W32(f.Model + 0x90U, f.Context + 0x100U);
        f.W32(f.Owner + 0x108U, 0x15000000U + size); observe(0x1565BCU);
        if (rebindPhase == 3U) f.W32(f.Model + 0x90U, f.Context + 0x100U);
        if (rebindPhase == 4U) { s.r[0] = f.Model; observe(0x5EC220U); }
        f.W32(kMm3dCommandListCursorAddress, 0x14000040U);
        s.r[5] = f.Queue + 8U; observe(0x5EF0D0U); observe(0x11EF3CU);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;
        tracker.TakeCommandListCompositionSpans(0, 0, spans);
        return spans;
    };
    auto spans = run(true, 1U, 0x80U);
    Require(spans.size() == 2 && spans.back().BeginAddress == 0x15000000U,
            "builder-owned primitive buffer lost to closing coarse scope");
    const auto& a = spans.back().Attribution;
    Require(a.NativeActorGeometry && a.TemporalPacketSourceRelative &&
            !a.TemporalCommandBufferIdentity && a.TemporalIdentity.Part == f.Nodes &&
            a.TemporalIdentity.Instance == f.Model && a.TemporalIdentity.Owner == f.Actor &&
            a.TemporalIdentity.Slot == ((uint64_t{0x11EF3CU} << 32U) | (2U << 16U)),
            "primitive did not use guarded native group/record identity");
    const auto lifetime = a.TemporalIdentity.Lifetime;
    spans = run(true, 1U, 0x80U);
    Require(spans.back().Attribution.TemporalIdentity.Lifetime != lifetime, "restore reused primitive epoch");
    for (bool renderer : {false, true}) {
        spans = run(renderer, renderer ? 3U : 1U, 0x80U);
        Require(spans.back().Attribution.NativeActorGeometry &&
                !spans.back().Attribution.TemporalIdentity.Available(),
                "unproved renderer/index acquired temporal primitive identity");
    }
    for (uint32_t phase = 1; phase <= 4; ++phase) {
        spans = run(true, 1U, 0x80U, phase);
        Require(spans.size() == 2, "rebind discarded native primitive coverage");
        for (const auto& span : spans)
            Require(!span.Attribution.TemporalIdentity.Available() &&
                    !span.Attribution.TemporalCommandBufferIdentity &&
                    !span.Attribution.TemporalPacketSourceRelative &&
                    span.Attribution.TemporalPacketBeginAddress == 0,
                    "primitive/pending/model span survived resource invalidation");
    }
    spans = run(true, 1U, 3U);
    Require(spans.size() == 1 && tracker.Stats().InvalidPrimitives == 1,
            "invalid primitive cursor range was published");
}

void TestQuadEntryOwnership() {
    Fixture f;
    Mm3dNativePicaCompositionTracker tracker;
    auto& s = f.State;
    Require(tracker.VerifyHooks(f.Memory), "quad hook guards");
    const auto observe = [&](uint32_t pc) { tracker.ObserveBlockEntry(pc, s, f.Memory); };
    constexpr uint32_t manager = f.Actor + 0x8000U;
    constexpr std::array returns{0x110B7CU, 0x110BACU, 0x110C8CU, 0x110CB0U, 0x110D88U, 0x110DB8U};
    for (bool reordered : {false, true}) for (uint32_t q = 0; q < returns.size(); ++q) {
        tracker.Reset(); s.r[0] = f.Play; observe(0x15E9ECU);
        f.W32(manager + 0x14U + q * 4U, 0);
        s.r[14] = 0x15EF58U; observe(0x171B40U);
        s.r[0] = manager; s.r[1] = q; s.r[2] = f.Model; observe(0x179930U);
        const uint32_t slot = manager + 0x578U + q * 0x600U;
        f.W32(slot, f.Model); f.W32(manager + 0x14U + q * 4U, 1);
        observe(0x15EF58U);
        // Native UI update can enqueue before Play_Draw. That entry must
        // survive the model/actor frame reset; the manager owns its lifetime.
        s.r[0] = f.Play; observe(0x15E9ECU);
        // Same model handle, different native slot/producer. Never transfer
        // the atmosphere role by a per-model lookup. The textured quad pool
        // has its own guarded producer, separate from the flat-color pool.
        s.r[5] = manager; s.r[10] = q; s.r[4] = f.Model; observe(0x191CC8U);
        f.W32(slot + 4U, f.Model); f.W32(manager + 0x14U + q * 4U, 2);
        if (reordered) {
            // Native insertion moves the final entry ahead of the first.
            // Identical model handles must retain their different semantics.
            s.r[0] = 2; s.r[1] = 0; s.r[2] = slot; s.r[6] = manager + q * 4U;
            observe(0x16160CU); observe(0x161664U);
        }
        f.W32(kMm3dCommandListCursorAddress, 0x14000000U);
        s.r[0] = manager; s.r[1] = q; s.r[14] = returns[q]; observe(kMm3dQuadQueueDrawEntry);
        s.r[6] = manager + q * 0x600U;
        for (uint32_t index = 0; index < 2; ++index) {
            s.r[4] = index; observe(0x12C71CU);
            f.W32(kMm3dCommandListCursorAddress, 0x14000000U + (index + 1) * 0x20U);
            observe(0x12C730U);
        }
        observe(returns[q]);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;
        tracker.TakeCommandListCompositionSpans(0, 0, spans);
        const size_t atmosphere = reordered ? 1 : 0, other = reordered ? 0 : 1;
        Require(spans.size() == 2 && spans[atmosphere].Attribution.Layer == Oot3dPicaCompositionLayer::Atmosphere &&
                !spans[atmosphere].Attribution.ExcludeFromPresentationFxaa && !spans[atmosphere].Attribution.TemporalIdentity.Available(),
                "environment quad lost exact producer ownership");
        Require(spans[other].Attribution.Layer == (q >= 4 ? Oot3dPicaCompositionLayer::Ui : Oot3dPicaCompositionLayer::Unknown),
                "queue occurrence fabricated a role for a different producer");
        Require(tracker.Stats().AtmosphereQuadSpans == 1 && tracker.Stats().InvalidQuadOwners == 0,
                "quad entry scope imbalance");
    }
    tracker.Reset();
    const uint32_t source = manager + 0x578U + 6U * 0x600U;
    const uint32_t destination = manager + 0x578U + 4U * 0x600U;
    f.W32(destination, f.Model);
    const std::array moves{Mm3dDekui::NativeQuadQueueMove{source, destination, f.Model, true}};
    tracker.CommitQuadQueueMoves(moves, f.Memory);
    const auto drawMoved = [&](uint32_t begin) {
        f.W32(kMm3dCommandListCursorAddress, begin);
        s.r[0] = manager; s.r[1] = 4; s.r[14] = returns[4]; observe(kMm3dQuadQueueDrawEntry);
        s.r[6] = manager + 4U * 0x600U; s.r[4] = 0; observe(0x12C71CU);
        f.W32(kMm3dCommandListCursorAddress, begin + 0x20U);
        observe(0x12C730U); observe(returns[4]);
    };
    drawMoved(0x14000100U);
    std::vector<Oot3dPicaCommandListCompositionSpan> moved;
    tracker.TakeCommandListCompositionSpans(0, 0, moved);
    Require(moved.size() == 1 && moved[0].Attribution.Layer == Oot3dPicaCompositionLayer::Ui &&
            moved[0].Attribution.PreserveNativeUiSampling && tracker.Stats().NativeUiQuadMoves == 1,
            "producer-proved minimap relocation lost UI ownership");
    // A stale destination must invalidate the sidecar, not confer the role on
    // the new object now occupying that native queue slot.
    f.W32(destination, f.Model + 4U);
    tracker.CommitQuadQueueMoves(moves, f.Memory);
    drawMoved(0x14000200U);
    tracker.TakeCommandListCompositionSpans(0, 0, moved);
    Require(moved.back().Attribution.Layer == Oot3dPicaCompositionLayer::Unknown &&
            tracker.Stats().InvalidQuadOwners == 1, "stale relocation granted UI ownership");
}

void TestAcceptedAndSelectedSources() {
    Fixture f; Mm3dNativeA32LightingProbe probe;
    auto& s=f.State;
    f.Point(f.Params,250);
    // An entry hit is not acceptance.
    s.r[0]=f.Owner; s.r[1]=f.Params; s.r[3]=f.Owner+12U;
    probe.ObserveBlockEntry(0x406458U,s,f.Memory);
    s.r[0]=f.Owner;s.r[1]=f.Context;
    probe.ObserveBlockEntry(0x1EEAA8U,s,f.Memory);
    s.r[0]=f.Context+0x70U;s.r[1]=f.Owner+12U;s.r[5]=0;s.r[8]=f.Context;
    probe.ObserveBlockEntry(0x1EED64U,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[0].SourceIdentity==0,"rejected bind became a source");
    s.r[0]=f.Owner;s.r[1]=f.Params;s.r[3]=f.Owner+12U;
    probe.ObserveBlockEntry(0x40650CU,s,f.Memory);
    f.W8(f.Owner+12U,1); f.W8(f.Owner+13U,128); f.W8(f.Owner+14U,64); f.W8(f.Owner+15U,32);
    for (uint32_t slot=0;slot<4;++slot) {
        s.r[0]=f.Context+0x70U+slot*0x118U;s.r[1]=f.Owner+12U;s.r[5]=slot;s.r[8]=f.Context;
        probe.ObserveBlockEntry(0x1EED64U,s,f.Memory);
    }
    const auto captured=probe.CaptureRenderer(f.Renderer,f.Memory);
    Require(captured.Sources[3].SourceIdentity==f.Params && captured.Sources[3].Radius==250,"fourth native slot lost");
    Require(captured.Sources[0].UnattenuatedColor[0]==1.0F && captured.Sources[0].ReferenceColor[0]<0.51F,"source/reference colors conflated");
    PicaResolvedDrawRecord replay; replay.NativeLightSources=captured.Sources;
    Require(replay.NativeLightSources[3].SourceIdentity==f.Params,"resolved draw lost fourth slot");
    f.W32(f.Renderer+0x100U+0x10U,f.Context+0x600U);
    Require(probe.CaptureRenderer(f.Renderer+0x100U,f.Memory).Sources[0].SourceIdentity==0,"unobserved context invented a source");
    f.F32(f.Params,99);
    Require(captured.Sources[0].PositionWorld[0]==10,"snapshot read live source");
    s.r[0]=f.Context;s.r[1]=3;
    probe.ObserveBlockEntry(0x1FD5E8U,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[3].SourceIdentity==0,"disabled slot retained source");
    s.r[0]=f.Context+0x70U;s.r[14]=0x111CC0U;
    probe.ObserveBlockEntry(0x1F2294U,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[0].SourceIdentity==0,"unrelated color writer retained source");
    f.Point(f.Params,-1); s.r[0]=f.Owner;s.r[1]=f.Params;s.r[3]=f.Owner+12U;
    probe.ObserveBlockEntry(0x40650CU,s,f.Memory);
    s.r[0]=f.Context+0x70U;s.r[1]=f.Owner+12U;s.r[5]=0;s.r[8]=f.Context;
    probe.ObserveBlockEntry(0x1EED64U,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[0].Radius==0,"negative radius converted to positive");
    probe.ObserveBlockEntry(0x15E9ECU,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[1].SourceIdentity==0,"generation reuse retained native source");
}

void TestLocalSourceGuards() {
    Fixture f;
    for (const auto& source : kMm3dLocalLightSources) {
        f.W16(f.Actor,source.Actor); f.W8(f.Actor+2,source.Category);
        f.W32(f.Actor+0x140,source.Update); f.W32(f.Actor+0x13C,source.Destroy);
        for (const auto& guard : source.Guards) f.W32(guard[0],guard[1]);
        Require(Mm3dLocalLightSourceProved(f.Memory,f.Actor,source.Info,source.Node),"proved source rejected");
        for (const auto& guard : source.Guards) {
            f.W32(guard[0],guard[1]^1U);
            Require(!Mm3dLocalLightSourceProved(f.Memory,f.Actor,source.Info,source.Node),"corrupt source guard accepted");
            f.W32(guard[0],guard[1]);
        }
        f.W32(f.Actor+0x140,source.Update+4);
        Require(!Mm3dLocalLightSourceProved(f.Memory,f.Actor,source.Info,source.Node),"reused/other actor accepted");
        f.W32(f.Actor+0x140,source.Update);
        Require(!Mm3dLocalLightSourceProved(f.Memory,f.Actor,source.Info+4,source.Node),"wrong source offset accepted");
    }
}

void TestEachProvenSourcePublication() {
    for (const auto& source : kMm3dLocalLightSources) {
        Fixture f; Mm3dNativeA32LightingProbe probe;
        f.W16(f.Actor,source.Actor); f.W8(f.Actor+2,source.Category);
        f.W16(f.Actor+0x1C,1);
        f.W32(f.Actor+0x140,source.Update); f.W32(f.Actor+0x13C,source.Destroy);
        for (const auto& guard : source.Guards) f.W32(guard[0],guard[1]);
        f.W32(f.Play+0x20C0+source.Category*12,1);
        f.W32(f.Play+0x20C4+source.Category*12,f.Actor);
        f.W32(f.Play+0xA88,f.Nodes); f.Link(f.Nodes,f.Actor+source.Info,0);
        f.W32(f.Actor+source.Node,f.Nodes); f.Point(f.Actor+source.Info+4,250);
        probe.CaptureSceneLights(f.Play,f.Memory);
        const auto binding=probe.CaptureRenderer(0,f.Memory);
        Require(binding.ScenePoints && binding.ScenePoints->Count==1,"proved source not selected exactly once");
        const auto& light=binding.ScenePoints->Lights[0];
        Require(light.SourceIdentity==f.Actor+source.Info+4 && light.ReplacesNativeLocal && light.Radius==250,
                "source publication changed native identity/radius");
        const float gain=source.Actor==0x10?1.0F:0.15F;
        Require(std::abs(light.Color[0]-gain)<1e-6F && std::abs(light.Color[1]-gain*128.0F/255.0F)<1e-6F,
                "selector lost native colour or added-light fade policy");
        f.W32(f.Play+0x138,2); f.W32(f.Actor+source.Node,0);
        probe.CaptureSceneLights(f.Play,f.Memory);
        Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints,"unowned native source remained selected");
    }
}

void TestRoomEmitterPublication() {
    Fixture f; Mm3dNativeA32LightingProbe probe;
    const uint32_t segment=f.Actor+0x8000;
    std::array<uint8_t,40> bytes{};
    bytes[0]='Z';bytes[1]='S';bytes[2]='I';bytes[3]=9;
    bytes[16]=1;bytes[17]=2;bytes[20]=0x48;
    bytes[24]=0xA;bytes[28]=0x60;bytes[32]=0x14;
    for(uint32_t i=0;i<bytes.size();++i)f.W8(segment+i,bytes[i]);
    f.W8(f.Play+0xC260,0);f.W32(f.Play+0xC270,segment);
    const auto hash=Mm3dRoomFingerprint(bytes);
    Require(hash.has_value(),"ROM room fingerprint rejected");
    // Exactly the native loader's observed pointer/relocation changes.
    f.W16(segment+18,1);f.W32(segment+20,0x09001234);
    f.W16(segment+26,1);f.W32(segment+28,0x09005678);
    const auto room=CaptureMm3dEmitterRoom(f.Play,f.Memory);
    Require(room && room->Fingerprint==*hash,"runtime relocation changed room identity");
    char fingerprint[17];std::snprintf(fingerprint,sizeof(fingerprint),"%016llx",static_cast<unsigned long long>(*hash));
    const std::string json=std::string(R"({"schema":"triaevum.scene-lights","version":1,"title":"mm3d","rooms":[{"scene":111,"room":0,"fingerprint":")")+
        fingerprint+R"(","emitters":[{"position":[10,20,30],"colour":[1,0.8,0.5],"radius":500,"gain":0.45,"category":"window-lamp","independentEmission":true,"surfaces":["0000000000001234"]}]}]})";
    const auto catalog=PicaEmitterCatalog::Parse(json);
    Require(catalog && catalog->MatchesTitle("mm3d"),"MM catalog rejected");
    probe.SetEmitterCatalog(catalog);probe.CaptureSceneLights(f.Play,f.Memory);
    auto binding=probe.CaptureRenderer(0,f.Memory);
    Require(binding.ScenePoints && binding.ScenePoints->Count==1 && binding.ScenePoints->Lights[0].Group==3 &&
            binding.ScenePoints->Lights[0].CastsShadow && binding.ScenePoints->Lights[0].IndependentEmission &&
            !binding.ScenePoints->Lights[0].ReplacesNativeLocal,
            "room emitter did not use shared supplemental policy");
    auto& emission=PicaSurfaceEmissionRegistry::Instance();emission.BeginFrame();
    Require(emission.Follows(0x1234),"MM S14 follow set not published");
    emission.Observe(0x1234,0);emission.BeginFrame();
    f.W32(f.Play+0x138,2);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints,"native-off fake-emissive surface still lit");
    f.W8(f.Play+0xC260,0xFF);
    Require(!CaptureMm3dEmitterRoom(f.Play,f.Memory),"unloaded room accepted");
    f.W8(f.Play+0xC260,0);f.W8(segment,'X');
    Require(!CaptureMm3dEmitterRoom(f.Play,f.Memory),"unknown room ABI accepted");
    probe.Reset();emission.BeginFrame();
    Require(!emission.Follows(0x1234),"reset retained MM S14 follow set");
}

void TestSyntheticFireAndFairies() {
    for(const auto& row:kMm3dSyntheticSources) {
        Fixture f;std::vector<PicaScenePointLight> lights;
        f.W16(f.Actor,row.Actor);f.W32(f.Actor+0x140,row.Update);f.W32(f.Actor+0x13C,row.Destroy);
        for(const auto& g:row.Guards)if(g[0])f.W32(g[0],g[1]);
        f.F32(f.Actor+0x24,10);f.F32(f.Actor+0x28,20);f.F32(f.Actor+0x2C,30);
        for(uint32_t n=0;n<3;++n)f.F32(f.Actor+0x58+n*4,.03F);
        uint32_t draw=0x100100;
        if(row.Actor==0xC){draw=0x457638;f.W8(f.Actor+0x768,1);}
        if(row.Actor==0x57){draw=0x44C8D0;f.W32(f.Actor+0x30C,0x25DFC4);f.W8(f.Actor+0x314,255);}
        if(row.Actor==0xB7){draw=0x47C5E4;f.F32(f.Actor+0x224,255);f.F32(f.Actor+0x228,1);f.W32(f.Actor+0x28C,f.Model);}
        if(row.Actor==0xFA){draw=0x483518;for(uint32_t n=0;n<3;++n)f.F32(f.Actor+0x2FC+n*4,255);
            for(uint32_t i=0;i<10;++i)for(uint32_t n=0;n<3;++n)f.F32(f.Actor+0x22C+i*12+n*4,float(10+n*10));}
        f.W32(f.Actor+0x144,draw);
        CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
        Require(lights.size()==1 && lights[0].IndependentEmission && !lights[0].ReplacesNativeLocal &&
                lights[0].Group==2 && lights[0].Radius>0,"proved native fire phase not published");
        lights.clear();CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,true,false,lights);
        Require(lights.empty(),"registered source gained a synthetic duplicate");
        for(const auto& g:row.Guards)if(g[0]){f.W32(g[0],g[1]^1U);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"corrupt fire phase guard accepted");f.W32(g[0],g[1]);}
        f.W32(f.Actor+0x144,0);CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
        Require(lights.empty(),"hidden fire phase emitted");
        if(row.Actor==0x57) {
            f.W32(f.Actor+0x144,draw);f.W32(f.Actor+0x30C,0x58BCEC);
            f.W32(f.Actor+0x128,f.Owner);f.W16(f.Owner,0xF);f.W32(f.Owner+0x140,1);
            f.W16(f.Actor+0x310,0);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"uncharged arrow emitted");
            f.W16(f.Actor+0x310,5);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.size()==1 && lights[0].Radius==125,"half charge lost OoT policy");
            lights.clear();f.W32(f.Actor+0x30C,0xBAD);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"unknown fire-arrow action emitted");
        }
        if(row.Actor==0xC){f.W32(f.Actor+0x144,draw);f.W8(f.Actor+0x768,2);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"ice/extinguished Keese treated as fire");
            f.W8(f.Actor+0x768,1);f.W8(f.Actor+0x76A,1);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"invisible Keese bypassed native draw/lens gate");
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,true,lights);
            Require(lights.empty(),"previous draw survived current lens-off");
            f.W8(f.Play+0x20B4,100);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,true,lights);
            Require(lights.size()==1,"native-visible invisible Keese rejected");}
    }
    Fixture f;std::vector<PicaScenePointLight> lights;
    for(auto g:std::array{std::pair{0x397F54U,0xE5D405EEU},std::pair{0x397FD4U,0xED940A16U},
                         std::pair{0x39805CU,0xEDD50A51U},std::pair{0x3980D8U,0xE2833F56U}})f.W32(g.first,g.second);
    f.W32(f.Play+0x20DC,f.Owner);
    f.W32(f.Actor+0x144,0x397F2C);f.F32(f.Actor+0x58,.008F);f.F32(f.Actor+0x544,1);f.F32(f.Actor+0x564,255);
    for(uint32_t n=0;n<3;++n)f.F32(f.Actor+0x558+n*4,float(255-n*20));
    CaptureMm3dVisibleFairy(f.Play,f.Actor,1,f.Memory,lights);
    Require(lights.size()==1 && lights[0].Group==1 && lights[0].SourceIdentity==f.Actor+0x57C &&
            lights[0].IndependentEmission && !lights[0].ReplacesNativeLocal,"visible dormant fairy lost native identity/proof");
    lights.clear();f.W8(f.Actor+0x5EE,8);CaptureMm3dVisibleFairy(f.Play,f.Actor,1,f.Memory,lights);
    Require(lights.empty(),"hidden fairy emitted");f.W8(f.Actor+0x5EE,0);f.F32(f.Actor+0x544,0);
    CaptureMm3dVisibleFairy(f.Play,f.Actor,1,f.Memory,lights);Require(lights.empty(),"zero glow fairy emitted");
}

void TestSceneSnapshots() {
    TestSyntheticFireAndFairies();
    TestLocalSourceGuards();
    TestEachProvenSourcePublication();
    TestRoomEmitterPublication();
    Fixture f; Mm3dNativeA32LightingProbe probe;
    probe.SetSupplementalPublication(true, true);
    const uint32_t sun=f.Play+0x86D0U,moon=f.Play+0x86E8U,torch=f.Actor+0x2B0U;
    f.Direction(sun,120,20,220);f.Direction(moon,-120,-20,20);
    f.W32(f.Play+0xA88U,f.Nodes);f.Link(f.Nodes,moon,f.Nodes+12U);f.Link(f.Nodes+12U,sun,f.Nodes+24U);
    f.Link(f.Nodes+24U,torch,0); f.Point(torch+4U,250);
    f.W16(f.Actor,0x2E);f.W8(f.Actor+2U,6);f.W32(f.Actor+0x2ACU,f.Nodes+24U);
    f.W32(f.Actor+0x140U,0x4AA8D4); f.W32(f.Actor+0x13CU,0x467A08);
    for (const auto& source : kMm3dLocalLightSources)
        for (const auto& guard : source.Guards) f.W32(guard[0],guard[1]);
    f.W32(f.Play+0x20C0U+6U*12U,1);f.W32(f.Play+0x20C4U+6U*12U,f.Actor);
    probe.CaptureSceneLights(f.Play,f.Memory);
    const auto first=probe.CaptureRenderer(0,f.Memory);
    Require(first.SceneDirectionals && first.SceneDirectionals->Count==2,"linked environment not captured");
    Require(first.SceneDirectionals->Lights[0].SourceIdentity==sun+4U,"source identity is not params");
    Require(first.SceneDirectionals->Lights[0].SkyLight && first.SceneDirectionals->ShadowCasterIdentity==sun+4U,"clock/caster policy missing");
    Require(first.ScenePoints && first.ScenePoints->Count==1 && first.ScenePoints->Lights[0].Group==2 &&
            first.ScenePoints->Lights[0].ReplacesNativeLocal,"native torch duplicated or source proof missing");
    probe.SetSupplementalPublication(false,false);
    Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints && !probe.CaptureRenderer(0,f.Memory).Water,"review gate leaked supplemental payload");
    probe.SetSupplementalPublication(true,true);
    Require(first.WorldToView && (*first.WorldToView)[12]==-10,"view transpose wrong");
    probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.CaptureRenderer(0,f.Memory).SceneDirectionals==first.SceneDirectionals,"same guest frame advanced continuity");
    f.W16(f.Play+0x148U,0x61);
    Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints && !probe.CaptureRenderer(0,f.Memory).SceneDirectionals,
            "same-frame scene change reused a stale source/catalog snapshot");
    probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.CaptureRenderer(0,f.Memory).ScenePoints!=nullptr,"same-frame new scene was not captured");
    f.W16(f.Play+0x148U,0x6F);probe.CaptureSceneLights(f.Play,f.Memory);
    f.W32(f.Play+0x138U,2);f.Point(torch+4U,-1);probe.CaptureSceneLights(f.Play,f.Memory);
    const auto second=probe.CaptureRenderer(0,f.Memory);
    Require(!second.ScenePoints && first.ScenePoints->Lights[0].Radius==250,"dormant source selected or immutable snapshot lost");
    f.W32(f.Play+0x138U,3);f.Direction(sun,-120,-20,220);f.Direction(moon,120,20,20);
    probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.CaptureRenderer(0,f.Memory).SceneDirectionals->Lights[0].SkyLight,"nonclock direction was overwritten");
    f.W32(f.Play+0x138U,4);f.W32(f.Play+0xA88U,0);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints && !probe.CaptureRenderer(0,f.Memory).SceneDirectionals,"unlinked lights persisted");
    f.W32(kMm3dCurrentViewPointer,0);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.CaptureRenderer(0,f.Memory).WorldToView,"no-view captured camera");
    probe.Reset();
}

void TestAmbiguousQueueOwnership() {
    Fixture f; Mm3dNativePicaCompositionTracker tracker;auto& s=f.State;
    Require(tracker.VerifyHooks(f.Memory),"verify ambiguous fixture");
    const auto observe=[&](uint32_t pc){tracker.ObserveBlockEntry(pc,s,f.Memory);};
    s.r[0]=f.Play;observe(0x15E9ECU);
    s.r[0]=f.Actor;s.r[2]=0x1F8E28U;observe(0x182C44U);
    s.r[0]=f.Queue;s.r[1]=f.Model;s.r[14]=0x20A8F8U;f.W32(f.Queue+4U,0);observe(0x224104U);
    f.W32(f.Queue+8U,f.Model);f.W32(f.Queue+4U,1);observe(0x182C48U);
    s.r[0]=f.Model;s.r[14]=0x21EFACU;observe(0x20A898U);
    s.r[0]=f.Queue;s.r[1]=f.Model;s.r[14]=0x20A8F8U;observe(0x224104U);
    f.W32(f.Queue+12U,f.Model);f.W32(f.Queue+4U,2);observe(0x223EDCU);
    f.W32(kMm3dCommandListCursorAddress,0x14000000U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
    s.r[0]=f.Model;s.r[4]=f.Queue;s.r[5]=f.Queue+8U;observe(0x5EF0CCU);
    f.W32(kMm3dCommandListCursorAddress,0x14000020U);observe(0x5EF0D0U);observe(0x11EF3CU);
    std::vector<Oot3dPicaCommandListCompositionSpan> spans;tracker.TakeCommandListCompositionSpans(0,0,spans);
    Require(spans.size()==1 && !spans[0].Attribution.NativeActorGeometry && !spans[0].Attribution.NativeProjectedShadow,
            "ambiguous same-model permutation transferred an owner");
}

void TestNestedAndInvalidConsumer() {
    for (uint32_t scenario : {0U,1U,2U}) {
        const bool invalidRange = scenario != 0U;
        Fixture f; Mm3dNativePicaCompositionTracker tracker;auto& s=f.State;
        Require(tracker.VerifyHooks(f.Memory),"verify nested fixture");
        const auto observe=[&](uint32_t pc){tracker.ObserveBlockEntry(pc,s,f.Memory);};
        s.r[0]=f.Play;observe(0x15E9ECU);
        s.r[0]=f.Actor;s.r[2]=0x1F8E28U;observe(0x182C44U);
        s.r[0]=f.Queue;s.r[1]=f.Model;s.r[14]=0x20A8F8U;f.W32(f.Queue+4U,0);observe(0x224104U);
        f.W32(f.Queue+8U,f.Model);f.W32(f.Queue+4U,1);observe(0x182C48U);
        f.W32(kMm3dCommandListCursorAddress,0x14000000U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
        if (scenario == 2U) f.W32(kMm3dCommandListCursorAddress,0x13FFFFF8U); // invalid child begin
        s.r[0]=f.Model;s.r[4]=f.Queue;s.r[5]=f.Queue+8U;observe(0x5EF0CCU);
        if (!invalidRange) observe(0x5EF0CCU); // unknown nested consumer
        f.W32(kMm3dCommandListCursorAddress,invalidRange?0x13FFFFF8U:0x14000010U);observe(0x5EF0D0U);
        f.W32(kMm3dCommandListCursorAddress,0x14000020U);observe(0x11EF3CU);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;tracker.TakeCommandListCompositionSpans(0,0,spans);
        if (invalidRange) Require(spans.empty() && tracker.Stats().InvalidRanges!=0,"invalid range became a tail prefix");
        else Require(spans.size()==1 && !spans[0].Attribution.NativeActorGeometry && tracker.Stats().NestedScopeEntries!=0,
                     "nested commands inherited outer actor");
    }
}

void TestWaterOperandEligibility() {
    Require(PicaNativeFragmentOperandWaterEligible(true,0U,false,false,false),"null/empty-water legacy route changed");
    Require(PicaNativeFragmentOperandWaterEligible(true,2U,true,false,false),"dry geometry in a water scene lost operand");
    Require(PicaNativeFragmentOperandWaterEligible(true,2U,true,true,false),"mixed mesh lost its dry-fragment operand");
    Require(!PicaNativeFragmentOperandWaterEligible(true,kMaximumPicaSurfaceWaterBodies+1U,true,true,false),"truncated water coverage gained operand");
    Require(PicaNativeFragmentOperandWaterEligible(true,kMaximumPicaSceneWaterBodies,true,false,false),"proven dry geometry was excluded by scene count");
    Require(!PicaNativeFragmentOperandWaterEligible(true,2U,false,false,false),"unknown bounds gained operand");
    Require(!PicaNativeFragmentOperandWaterEligible(true,2U,true,false,true),"whole-water geometry gained operand");
    Require(!PicaNativeFragmentOperandWaterEligible(true,0U,true,false,false),"legacy bounds exclusion changed");
    Require(!PicaNativeFragmentOperandWaterEligible(true,kMaximumPicaSceneWaterBodies+1U,true,false,false),"oversized water metadata gained operand");
    for (uint32_t bits=0;bits<16;++bits)
        Require(!PicaNativeFragmentOperandWaterEligible(false,(bits&1)?2U:0U,(bits&2)!=0,(bits&4)!=0,(bits&8)!=0),
                "OoT capability-false route changed");
    Require(!PicaNativeFragmentOperandWaterEligible(false,kMaximumPicaSceneWaterBodies+1U,true,false,false),"OoT oversized-water route changed");
}

void TestWaterCountAndHeight() {
    Fixture f; const uint32_t header=f.Actor+0x8000U,boxes=header+0x100U;
    f.W32(f.Play+0xAB0U,header);f.W16(header+0x14U,99);f.W16(header+0x16U,1);f.W32(header+0x28U,boxes);
    const int16_t fields[]={-924,-20,-1000,160,520};
    for(uint32_t i=0;i<5;++i)f.W16(boxes+2U*i,std::bit_cast<uint16_t>(fields[i]));f.W32(boxes+12U,257);
    const auto first=CaptureMm3dWaterBodies(f.Play,f.Memory);
    Require(first.Count==1 && first.Bodies[0].SurfaceY==-20 && first.Bodies[0].MaximumXZ[0]==-764,"MM water ABI wrong");
    f.W16(boxes+2U,30);
    Require(CaptureMm3dWaterBodies(f.Play,f.Memory).Bodies[0].SurfaceY==30 && first.Bodies[0].SurfaceY==-20,"live height/snapshot wrong");
    f.W16(header+0x16U,0);
    Require(CaptureMm3dWaterBodies(f.Play,f.Memory).Count==0,"zero-count dereferenced box");
}

void TestDeferredQueueAndShadowScopes() {
    Fixture f; Mm3dNativePicaCompositionTracker tracker; auto& s=f.State;
    Require(tracker.VerifyHooks(f.Memory),"valid signatures rejected");
    const auto observe=[&](uint32_t pc){return tracker.ObserveBlockEntry(pc,s,f.Memory);};
    const auto cursor=[&](uint32_t p){f.W32(kMm3dCommandListCursorAddress,p);};
    s.r[0]=f.Play;observe(0x15E9ECU);
    s.r[0]=f.Actor;s.r[2]=0x1F8E28U;observe(0x182C44U);
    s.r[0]=f.Queue;s.r[1]=f.Model;s.r[14]=0x20A8F8U;f.W32(f.Queue+4U,0);observe(0x224104U);
    f.W32(f.Queue+8U,f.Model);f.W32(f.Queue+4U,1);observe(0x182C48U);
    const uint32_t shadowModel=f.Model+0x400U;
    f.W32(shadowModel,0x63BED4U);f.W32(shadowModel+0xA0U,f.Renderer);
    s.r[0]=shadowModel;s.r[14]=0x21EFACU;observe(0x20A898U);
    s.r[0]=f.Queue;s.r[1]=shadowModel;s.r[14]=0x20A8F8U;observe(0x224104U);
    f.W32(f.Queue+12U,shadowModel);f.W32(f.Queue+4U,2);observe(0x223EDCU);
    // Native permutation reverses the appended slots before the visitor.
    f.W32(f.Queue+8U,shadowModel);f.W32(f.Queue+12U,f.Model);
    cursor(0x14000000U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
    const auto consume=[&](uint32_t slot,uint32_t begin,uint32_t end,bool reverse){
        cursor(begin);Require(f.Memory.Read32(slot,&s.r[0]),"consumer model");s.r[4]=f.Queue;s.r[5]=reverse?(slot-f.Queue-8U)/4U:slot;
        observe(reverse?0x5EF094U:0x5EF0CCU);cursor(end);observe(reverse?0x5EF098U:0x5EF0D0U);
    };
    consume(f.Queue+12U,0x14000008U,0x14000020U,false);
    consume(f.Queue+8U,0x14000020U,0x14000038U,true);
    cursor(0x14000040U);observe(0x11EF3CU);
    std::vector<Oot3dPicaCommandListCompositionSpan> spans;
    tracker.TakeCommandListCompositionSpans(0,0,spans);
    Require(spans.size()==4,"parent coverage erased children/gaps");
    Require(spans[1].Attribution.NativeActorGeometry && spans[1].Attribution.NativeActorIdentity==f.Actor &&
            !spans[1].Attribution.NativeProjectedShadow,"main model lost actor or became blob");
    Require(spans[2].Attribution.NativeProjectedShadow && !spans[2].Attribution.NativeActorGeometry,"blob inherited actor/caster role");
    Require(!spans[0].Attribution.NativeActorGeometry && !spans[3].Attribution.NativeProjectedShadow,"pass prefix/suffix contaminated");
    s.r[0]=f.Play;observe(0x15E9ECU);cursor(0x14000008U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
    consume(f.Queue+8U,0x14000008U,0x14000020U,false);observe(0x11EF3CU);
    tracker.TakeCommandListCompositionSpans(0,0,spans);
    for(const auto& span:spans)if(span.BeginAddress==0x14000008U)Require(!span.Attribution.NativeActorGeometry,"stale queue generation reused");
    tracker.Reset();tracker.TakeCommandListCompositionSpans(0,0,spans);Require(spans.empty(),"restore retained spans");
    f.W32(kMm3dLightingHookSites[0].Pc,0);Require(!tracker.VerifyHooks(f.Memory),"wrong image enabled observer");
    cursor(0x14000100U);s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);cursor(0x14000140U);observe(0x11EF3CU);
    tracker.TakeCommandListCompositionSpans(0,0,spans);Require(spans.empty(),"disabled observer emitted metadata");
}

// Custom shadow producers (code.bin receipt COMPOSITION/SHADOW_PRODUCERS):
// actor overlays call the generic shadow routine 0x0021EC40 directly from
// their own draw callback (zeldaret-mm ActorShadow_Draw callers), so the blob
// submit (LR 0x0021EFAC) happens INSIDE the actor draw scope. The feet routine
// 0x001F2E64 is installed as a shadow function pointer and submits at
// 0x001F3898/0x001F3AB0. Each blob must be a shadow, never actor geometry.
void TestCustomShadowProducers() {
    for (const uint32_t lr : {0x0021EFACU, 0x001F3898U, 0x001F3AB0U, 0x0020A000U}) {
        Fixture f; Mm3dNativePicaCompositionTracker tracker; auto& s=f.State;
        Require(tracker.VerifyHooks(f.Memory),"valid signatures rejected");
        const auto observe=[&](uint32_t pc){return tracker.ObserveBlockEntry(pc,s,f.Memory);};
        const auto cursor=[&](uint32_t p){f.W32(kMm3dCommandListCursorAddress,p);};
        s.r[0]=f.Play;observe(0x15E9ECU);
        s.r[0]=f.Actor;s.r[2]=0x1F8E28U;observe(0x182C44U);
        s.r[0]=f.Queue;s.r[1]=f.Model;s.r[14]=0x20A8F8U;f.W32(f.Queue+4U,0);observe(0x224104U);
        f.W32(f.Queue+8U,f.Model);f.W32(f.Queue+4U,1);
        const uint32_t shadowModel=f.Model+0x400U;
        f.W32(shadowModel,0x63BED4U);f.W32(shadowModel+0xA0U,f.Renderer);
        // Still inside the actor's main draw callback.
        s.r[0]=shadowModel;s.r[14]=lr;observe(0x20A898U);
        s.r[0]=f.Queue;s.r[1]=shadowModel;s.r[14]=0x20A8F8U;observe(0x224104U);
        f.W32(f.Queue+12U,shadowModel);f.W32(f.Queue+4U,2);observe(0x223EDCU);
        observe(0x182C48U);
        cursor(0x14000000U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
        const auto consume=[&](uint32_t slot,uint32_t begin,uint32_t end){
            cursor(begin);Require(f.Memory.Read32(slot,&s.r[0]),"consumer model");s.r[4]=f.Queue;s.r[5]=slot;
            observe(0x5EF0CCU);cursor(end);observe(0x5EF0D0U);
        };
        consume(f.Queue+8U,0x14000008U,0x14000020U);
        consume(f.Queue+12U,0x14000020U,0x14000038U);
        cursor(0x14000040U);observe(0x11EF3CU);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;
        tracker.TakeCommandListCompositionSpans(0,0,spans);
        Require(spans.size()==4,"custom shadow coverage");
        Require(spans[1].Attribution.NativeActorGeometry && spans[1].Attribution.NativeActorIdentity==f.Actor &&
                !spans[1].Attribution.NativeProjectedShadow,"custom producer main model lost actor");
        if (lr==0x0020A000U) {
            // Negative: an unrelated submit inside the actor scope is the actor's geometry, not a blob.
            Require(!spans[2].Attribution.NativeProjectedShadow && spans[2].Attribution.NativeActorGeometry,
                    "non-shadow submit became a blob");
        } else {
            Require(spans[2].Attribution.NativeProjectedShadow && !spans[2].Attribution.NativeActorGeometry,
                    "custom shadow producer blob inherited actor role");
        }
    }
}

// Direct shadow submitters (COMPOSITION/DIRECT_SHADOW/scan.txt): an actor that
// submits the shared shadow CMB through 0x0020A898 itself, bypassing the shared
// routines, is a blob by the model's resource tuple learned from a proved
// routine submit. mode 0: same resources -> shadow; mode 1: other resources ->
// actor geometry; mode 2: no proved submit before -> actor geometry.
void TestDirectShadowSubmitByResource() {
    for (int mode = 0; mode < 3; ++mode) {
        Fixture f; Mm3dNativePicaCompositionTracker tracker; auto& s=f.State;
        Require(tracker.VerifyHooks(f.Memory),"valid signatures rejected");
        const auto observe=[&](uint32_t pc){return tracker.ObserveBlockEntry(pc,s,f.Memory);};
        const auto cursor=[&](uint32_t p){f.W32(kMm3dCommandListCursorAddress,p);};
        s.r[0]=f.Play;observe(0x15E9ECU);
        s.r[0]=f.Actor;s.r[2]=0x1F8E28U;observe(0x182C44U);
        const uint32_t proved=f.Model+0x400U,direct=f.Model+0x800U;
        f.W32(proved,0x63BED4U);f.W32(proved+0xA0U,f.Renderer);f.W32(proved+0x90U,0x0C000100U);
        f.W32(direct,0x63BED4U);f.W32(direct+0xA0U,f.Renderer);f.W32(direct+0x90U,mode==1?0x0C000200U:0x0C000100U);
        f.W32(f.Queue+4U,0);
        uint32_t n=0;
        const auto submit=[&](uint32_t model,uint32_t lr){
            s.r[0]=model;s.r[14]=lr;observe(0x20A898U);
            s.r[0]=f.Queue;s.r[1]=model;s.r[14]=0x20A8F8U;observe(0x224104U);
            f.W32(f.Queue+8U+n*4U,model);++n;f.W32(f.Queue+4U,n);observe(0x223EDCU);
        };
        if (mode!=2) submit(proved,0x0021EFACU);
        submit(direct,0x0020A000U);
        observe(0x182C48U);
        cursor(0x14000000U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
        uint32_t at=0x14000008U;
        for (uint32_t i=0;i<n;++i){
            const uint32_t slot=f.Queue+8U+i*4U;
            cursor(at);Require(f.Memory.Read32(slot,&s.r[0]),"consumer model");s.r[4]=f.Queue;s.r[5]=slot;
            observe(0x5EF0CCU);at+=0x18U;cursor(at);observe(0x5EF0D0U);
        }
        cursor(at+8U);observe(0x11EF3CU);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;
        tracker.TakeCommandListCompositionSpans(0,0,spans);
        const Oot3dPicaCommandListCompositionSpan* last=nullptr;
        for (const auto& span:spans) if (span.Attribution.NativeProjectedShadow || span.Attribution.NativeActorGeometry) last=&span;
        Require(last!=nullptr,"direct shadow coverage");
        if (mode==0) Require(last->Attribution.NativeProjectedShadow && !last->Attribution.NativeActorGeometry,
                             "direct shadow-CMB submit inherited actor role");
        else Require(!last->Attribution.NativeProjectedShadow && last->Attribution.NativeActorGeometry,
                     mode==1?"foreign resources became a blob":"unproved resources became a blob");
        Require((tracker.Stats().DirectShadowSubmits==1U)==(mode==0),"direct shadow counter");
    }
}

// P1.0/P1.1: each selected light's function, and BakedOutdoorRoom from the
// environment group, with guard words and Unknown fallback.
void TestLightFunctionsAndGroup() {
    Fixture f; Mm3dNativeA32LightingProbe probe; auto& s=f.State;
    probe.SetSupplementalPublication(true,true);
    const uint32_t sun=f.Play+0x86D0U,moon=f.Play+0x86E8U,torch=f.Actor+0x2B0U,lamp=f.Actor+0x8000U;
    f.Direction(sun,120,20,220);f.Direction(moon,-120,-20,20);
    f.W32(f.Play+0xA88U,f.Nodes);f.Link(f.Nodes,moon,f.Nodes+12U);f.Link(f.Nodes+12U,sun,f.Nodes+24U);
    f.Link(f.Nodes+24U,torch,f.Nodes+36U);f.Link(f.Nodes+36U,lamp,0);
    f.Point(torch+4U,250);f.Point(lamp+4U,300); // lamp: linked, owner not proved
    f.W16(f.Actor,0x2E);f.W8(f.Actor+2U,6);f.W32(f.Actor+0x2ACU,f.Nodes+24U);
    f.W32(f.Play+0x20C0U+6U*12U,1);f.W32(f.Play+0x20C4U+6U*12U,f.Actor);
    // Outdoor, clock running (South Clock Town decode): TimeFlows.
    f.W16(f.Play+0x86B2U,3);f.W8(f.Play+0x86C6U,0);f.W8(f.Play+0x86C7U,0);f.W8(f.Play+0x8704U,0);f.W8(f.Play+0xC528U,1);
    Require(!probe.VerifyEnvironmentGroupGuards(f.Memory),"guards held on an image without the writers");
    probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.EnvironmentGroup(f.Play,0x6F) && !probe.CaptureRenderer(0,f.Memory).SceneDirectionals->BakedOutdoorRoom,
            "unknown group baked the room");
    for (const auto& guard : Mm3dEnvironmentGroupLayout::Guards) f.W32(guard.Address,guard.Word);
    Require(probe.VerifyEnvironmentGroupGuards(f.Memory),"writer guards rejected");
    f.W32(f.Play+0x138U,2);probe.CaptureSceneLights(f.Play,f.Memory);
    const auto scene=probe.CaptureRenderer(0,f.Memory);
    Require(probe.EnvironmentGroup(f.Play,0x6F)==SceneEnvironmentGroup::TimeFlows,"outdoor clock scene not TimeFlows");
    Require(scene.SceneDirectionals->BakedOutdoorRoom && scene.SceneDirectionals->Scope==PicaNativeLightScope::Scene,
            "outdoor environment not baked/scene");
    // Bind slot 0 to the sun, 1 to the actor torch, 2 to the unproved lamp.
    const auto bind=[&](uint32_t slot,uint32_t params,bool point){
        const uint32_t record=f.Owner+12U+slot*24U;
        s.r[0]=f.Owner;s.r[1]=params;s.r[2]=record;s.r[3]=record;
        probe.ObserveBlockEntry(point?0x40650CU:0x4648F4U,s,f.Memory);
        f.W8(record,point?1:0);f.W8(record+1U,200);f.W8(record+2U,200);f.W8(record+3U,200);
    };
    s.r[0]=f.Owner;s.r[1]=f.Context;probe.ObserveBlockEntry(0x1EEAA8U,s,f.Memory);
    bind(0,sun+4U,false);bind(1,torch+4U,true);bind(2,lamp+4U,true);
    for (uint32_t slot=0;slot<3;++slot){
        s.r[0]=f.Context+0x70U+slot*0x118U;s.r[1]=f.Owner+12U+slot*24U;s.r[5]=slot;s.r[8]=f.Context;
        probe.ObserveBlockEntry(0x1EED64U,s,f.Memory);
    }
    const auto lit=probe.CaptureRenderer(f.Renderer,f.Memory);
    Require(lit.Sources[0].SourceIdentity==sun+4U && lit.Sources[0].Scope==PicaNativeLightScope::Scene &&
            lit.Sources[0].Origin==PicaNativeLightOrigin::EnvironmentDirectional,"sun slot not scene/environment");
    Require(lit.Sources[1].SourceIdentity==torch+4U && lit.Sources[1].Scope==PicaNativeLightScope::Local &&
            lit.Sources[1].Origin==PicaNativeLightOrigin::Actor,"actor torch not local/actor");
    Require(lit.Sources[2].SourceIdentity==lamp+4U && lit.Sources[2].Scope==PicaNativeLightScope::Local &&
            lit.Sources[2].Origin==PicaNativeLightOrigin::Unknown,"unproved owner not unknown/local");
    Require(lit.Sources[3].Scope==PicaNativeLightScope::Local,"empty slot not local");
    // A stale snapshot (next guest frame not captured) classifies nothing as scene.
    f.W32(f.Play+0x138U,3);
    const auto stale=probe.CaptureRenderer(f.Renderer,f.Memory);
    Require(stale.Sources[0].Scope==PicaNativeLightScope::Local && stale.Sources[0].Origin==PicaNativeLightOrigin::Unknown,
            "stale snapshot kept scene scope");
    // A room change inside the visit never flips the latched group.
    f.W8(f.Play+0x86C7U,1);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.EnvironmentGroup(f.Play,0x6F)==SceneEnvironmentGroup::TimeFlows,"latched group flipped in the visit");
    // Another scene: Stock Pot Inn decode (sunDisabled, fixed lights, no sky) = Indoor, not baked.
    f.W16(f.Play+0x148U,0x61);f.W8(f.Play+0x86C6U,1);f.W8(f.Play+0x8704U,1);f.W8(f.Play+0xC528U,0);
    f.W32(f.Play+0x138U,4);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.EnvironmentGroup(f.Play,0x61)==SceneEnvironmentGroup::Indoor && !probe.EnvironmentGroup(f.Play,0x6F),
            "new visit did not relatch");
    Require(!probe.CaptureRenderer(0,f.Memory).SceneDirectionals->BakedOutdoorRoom,"interior baked as outdoor");
    // Unknown values (lightMode 2) stay Unknown and are retried, never latched.
    f.W16(f.Play+0x148U,0x62);f.W8(f.Play+0x8704U,2);f.W32(f.Play+0x138U,5);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.EnvironmentGroup(f.Play,0x62),"unknown light mode classified");
    f.W8(f.Play+0x8704U,1);f.W8(f.Play+0x86C7U,0);f.W8(f.Play+0x86C6U,0);f.W8(f.Play+0xC528U,2);
    f.W32(f.Play+0x138U,6);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.EnvironmentGroup(f.Play,0x62)==SceneEnvironmentGroup::FixedTime &&
            probe.CaptureRenderer(0,f.Memory).SceneDirectionals->BakedOutdoorRoom,"readable retry not latched as fixed outdoor");
    // Guard corruption after mount: Unknown again.
    f.W32(Mm3dEnvironmentGroupLayout::Guards[4].Address,0);
    Require(!probe.VerifyEnvironmentGroupGuards(f.Memory) && !probe.EnvironmentGroup(f.Play,0x62),"corrupt guard kept group");
    probe.Reset();
}
} // namespace
int main() {
    TestCompositionTemporalLifetimes(); TestDeferredModelResourceLifetime(); TestUnobservedInPlaceRecycling(); TestActorEpochRenewedOnIdChange(); TestPrimitiveOwnership(); TestQuadEntryOwnership();
    TestAcceptedAndSelectedSources();TestSceneSnapshots();TestWaterCountAndHeight();TestDeferredQueueAndShadowScopes();TestCustomShadowProducers();TestDirectShadowSubmitByResource();
    TestAmbiguousQueueOwnership();TestNestedAndInvalidConsumer();TestWaterOperandEligibility();
    TestLightFunctionsAndGroup();
    std::cout << "mm3d_native_lighting_tests: ok\n";
}
