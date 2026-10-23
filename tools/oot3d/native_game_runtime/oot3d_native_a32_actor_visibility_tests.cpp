#include "oot3d_native_a32_actor_visibility.h"
#include "oot3d_native_a32_memory.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace Oot3dNativeGame;
using namespace oot3d::recomp::a32;
namespace {
void Expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    static constexpr uint32_t Play = 0x00800000U, Context = Play + 0x208CU;
    static constexpr uint32_t Arena = Play + 0x11000U, Actors = Play + 0x20000U;
    NativeA32Memory Memory;
    NativeActorVisibilityState Visibility;
    GuestState State{};
    uint32_t Count = 0U, DeferredCount = 0U;
    std::vector<uint32_t> Deferred;
    Fixture(uint32_t count) : Count(count) {
        Expect(Memory.MapRegion({"actors", Play, 0x100000U, true, false, {}}), "map actors");
        Expect(Memory.MapRegion({"scratch", 0x00565000U, 0x5000U, true, false, {}}), "map scratch");
        Visibility.ExtendedDrawDistance = true;
        Write(Play, Arena); Write(Arena, 0x4000U); Write(Arena + 4, 0x005656A0U);
        Write(Arena + 8, 0x005656A0U); Used(1U);
        Float(Play + 0x5BB4U, 1); Float(Play + 0x5BB4U + 20U, 1);
        Float(Play + 0x5BB4U + 40U, -1); Float(Play + 0x5BB4U + 56U, 1);
        Write(Context + 0x10U, count ? Actor(0) : 0U);
        for (uint32_t i = 0; i < count; ++i) {
            const auto actor = Actor(i);
            Expect(Memory.Write16(actor, 1), "write id");
            Write(actor + 0x130U, i + 1 < count ? Actor(i + 1) : 0U);
            Write(actor + 0x140U, 0x00123400U);
            // Nearer candidates occur later: this exercises conservative
            // admission after earlier native rejects release reservations.
            const float z = 2000.0F + static_cast<float>(count - i);
            Float(actor + 0x30U, z); Float(actor + 0xF4U, z); Float(actor + 0xF8U, z);
            Float(actor + 0xFCU, 1000); Float(actor + 0x100U, 50); Float(actor + 0x104U, 100);
        }
    }
    uint32_t Actor(uint32_t i) const { return Actors + i * 0x200U; }
    void Write(uint32_t address, uint32_t value) { Expect(Memory.Write32(address, value), "write"); }
    void Float(uint32_t address, float value) { Write(address, std::bit_cast<uint32_t>(value)); }
    uint32_t Read(uint32_t address) { uint32_t value = 0; Expect(Memory.Read32(address, &value), "read"); return value; }
    uint8_t Byte(uint32_t address) { uint8_t value = 0; Expect(Memory.Read8(address, &value), "byte"); return value; }
    void Byte(uint32_t address, uint8_t value) { Expect(Memory.Write8(address, value), "write byte"); }
    uint32_t Used() { return (0x005696A0U - Read(Arena + 12U)) / 0x80U; }
    void Used(uint32_t count) { Write(Arena + 12U, 0x005696A0U - count * 0x80U); }
    void Hook(uint32_t pc) { Expect(ApplyNativeActorVisibilityHook(pc, State, Memory, Visibility), "hook owned"); }
    void Begin() { State.r[0] = Play; State.r[1] = Context; State.r[14] = 0x002E2CE0U; DeferredCount = 0; Deferred.clear(); Hook(kActorCullingEntry); }
    void Allocate() {
        State.r[1] = Arena;
        const auto fingerprint = Memory.ContentFingerprint(); const auto before = State;
        Hook(kActorLightAllocate);
        Expect(Memory.ContentFingerprint() == fingerprint && std::memcmp(&before, &State, sizeof(State)) == 0,
               "allocator observation must be read-only");
        Used(Used() + 1U);
    }
    void Draw(uint32_t actor, bool deferred) {
        State.r[0] = Play; State.r[1] = actor;
        State.r[14] = deferred ? kActorDeferredDrawReturn : 0x00461BB8U;
        Hook(kActorDrawEntry);
        if ((Read(actor + 4U) & 0x20000000U) == 0U) Allocate();
        if (deferred) Hook(kActorDeferredDrawReturn);
        else Byte(actor + 0x121U, 1U);
    }
    // Exact relevant native tail at 461AC4..461BBC, with synthetic native
    // result input. This is a boundary test, not emulation of the game.
    bool Step(uint32_t i, bool native, bool deferred = false, uint8_t nativeClass = 2U) {
        const auto actor = Actor(i);
        const auto previous = Byte(actor + 0x120U);
        Byte(actor + 0x120U, native ? 0U : nativeClass);
        State.r[0] = native ? 1U : 0U; State.r[1] = previous;
        State.r[4] = actor; State.r[5] = DeferredCount; State.r[7] = Play + 0x2000U;
        const auto fingerprint = Memory.ContentFingerprint(); const auto before = State;
        Hook(kActorCullingResultHook);
        if (native) Expect(Memory.ContentFingerprint() == fingerprint &&
                           std::memcmp(&before, &State, sizeof(State)) == 0, "native result untouched");
        const bool extended = !native && State.r[0] != 0U;
        auto flags = Read(actor + 4U);
        if (State.r[0] != 0U) {
            flags |= 0x40U;
            const auto maximum = Byte(actor + 0x19FU) ? Byte(actor + 0x19FU) : 10U;
            Byte(actor + 0x19EU, State.r[1] == 1U ? maximum :
                 static_cast<uint8_t>(std::min<uint32_t>(Byte(actor + 0x19EU) + 1U, maximum)));
        } else {
            const auto fade = NativeCulledActorFade(static_cast<ActorCullingClass>(Byte(actor + 0x120U)),
                                                  Byte(actor + 0x19EU));
            Byte(actor + 0x19EU, fade);
            flags = (flags & ~0x40U) | (fade ? 0x40U : 0U);
        }
        Write(actor + 4U, flags); Byte(actor + 0x121U, 0U);
        if (Read(actor + 0x134U) == 0U && Read(actor + 0x140U) != 0U && (flags & 0x60U) != 0U) {
            if (deferred) { Deferred.push_back(actor); ++DeferredCount; }
            else Draw(actor, false);
        }
        State.r[4] = actor; State.r[5] = DeferredCount;
        Hook(kActorCullingNext);
        return extended;
    }
    void End(bool drawDeferred = true) {
        if (drawDeferred) for (auto actor : Deferred) Draw(actor, true);
        Hook(kActorCullingEnd);
    }
};

void TestReservationsAndBurst() {
    Fixture f(180);
    f.Begin();
    Expect(f.Visibility.BudgetValid && f.Visibility.RemainingLights == 180, "whole current list reserved");
    unsigned extended = 0;
    for (uint32_t i = 0; i < f.Count; ++i) extended += f.Step(i, i >= 170);
    f.End();
    Expect(extended == 22 && f.Used() == 33, "near extensions and sudden native suffix fit");
    for (uint32_t i = 0; i < 170; ++i) {
        Expect(f.Byte(f.Actor(i) + 0x19EU) == 0 && (f.Read(f.Actor(i) + 4U) & 0x40U) == 0,
               "extensions leave no native fade liability");
        Expect(f.Byte(f.Actor(i) + 0x121U) == 0, "extension-only drawn state restored");
    }
    // A later burst is free to use the native arena without old extensions
    // retaining alpha. Native visibility wins even above the policy budget.
    f.Used(0); f.Hook(kActorLightEpochReset); f.Begin();
    for (uint32_t i = 0; i < f.Count; ++i) f.Step(i, i >= 52);
    f.End();
    Expect(f.Used() == 128, "128 native draws never suppressed");
    Expect(f.Visibility.PreviousLightAllocations == 33 && f.Visibility.PeakLightAllocations == 128,
           "exact epoch and peak accounting");
}

void TestExactCapacityWithUnexpectedNativeSuffix() {
    Fixture f(116); f.Used(0);
    // Force an early incumbent to rank ahead of the nearer new candidates.
    f.Visibility.Retained.push_back({(static_cast<uint64_t>(f.Actor(0)) << 16U) | 1U, 0U});
    f.Begin();
    Expect(f.Step(0, false), "early incumbent at exact reservation boundary");
    for (uint32_t i = 1; i < f.Count; ++i) f.Step(i, true);
    f.End();
    for (uint32_t i = 0; i < kActorLightAuxiliaryReserve; ++i) f.Allocate();
    Expect(f.Used() == kActorLightBudget && f.Used() < kActorLightCapacity,
           "all reserved actors plus all auxiliary records preserve eight-record margin");
    // One more already-used record prevents that same extension; native
    // results still run and no last-frame telemetry supplies phantom space.
    Fixture full(116); full.Used(1); full.Visibility.PreviousLightAllocations = 0;
    full.Visibility.Retained.push_back({(static_cast<uint64_t>(full.Actor(0)) << 16U) | 1U, 0U});
    full.Begin(); Expect(!full.Step(0, false), "current cursor wins over quiet previous epoch");
    for (uint32_t i = 1; i < full.Count; ++i) full.Step(i, true);
    full.End(); Expect(full.Used() == 116, "native suffix kept at tight boundary");
}

void TestDeferredAndNativeFade() {
    Fixture f(12); f.Used(104); f.Begin();
    // Every possible actor is reserved at admission, including lens draws.
    Expect(f.Step(0, false, true), "exact budget boundary admits");
    Expect(f.Visibility.RemainingLights == 12 && f.Used() == 104, "deferred record stays reserved");
    f.Byte(f.Actor(1) + 0x19EU, 6U);
    Expect(f.Step(1, false), "native fading actor can extend");
    Expect(f.Byte(f.Actor(1) + 0x19EU) == 5 && (f.Read(f.Actor(1) + 4U) & 0x40U),
           "native fade preserved exactly");
    for (uint32_t i = 2; i < f.Count; ++i) f.Step(i, true, i % 2 == 0);
    f.End();
    Expect(f.Used() == 116 && f.Visibility.RemainingLights == 0, "deferred plus immediate bound");
    Expect(f.Byte(f.Actor(0) + 0x19EU) == 0, "deferred extension restored after draw");
    Fixture skipped(1); skipped.Begin(); skipped.Step(0, false, true); skipped.End(false);
    Expect(skipped.Byte(skipped.Actor(0) + 0x19EU) == 0, "skipped lens tail restored at walk end");
}

void TestIndependentExtensionFade() {
    Fixture f(1);
    for (uint8_t expected = 1; expected <= 10; ++expected) {
        f.Used(1); f.Begin(); Expect(f.Step(0, false, true), "extension selected while fading in");
        Expect(f.Byte(f.Actor(0) + 0x19EU) == expected, "extension alpha ramps independently");
        f.End();
        Expect(f.Byte(f.Actor(0) + 0x19EU) == 0 && f.Visibility.Retained.front().Fade == expected,
               "native fade history stays zero; visual fade retained on host");
    }
    f.Used(116); f.Begin(); Expect(!f.Step(0, false), "over budget stops extension immediately"); f.End();
    Expect(f.Byte(f.Actor(0) + 0x19EU) == 0 && f.Visibility.Retained.empty(), "denial creates no deferred fade cost");
}

void TestMultipleWalksAndColdRestore() {
    Fixture f(32);
    for (unsigned walk = 0; walk < 5; ++walk) {
        f.Begin(); for (uint32_t i = 0; i < f.Count; ++i) f.Step(i, false); f.End();
    }
    Expect(f.Used() == 116, "multiple walks share the live arena, never reset credits");
    Fixture cold(1); cold.Used(115); cold.Begin();
    Expect(cold.Step(0, false), "cold host state reads a partially used restored arena"); cold.End();
    Expect(cold.Used() == 116 && cold.Visibility.LightAllocations == 116, "cold accounting is exact");
}

void TestSavestateLifecycle() {
    Fixture f(1); f.Visibility.HostMemory = &f.Memory;
    f.Visibility.FovMultiplier = 1.1F;
    f.Begin();
    Expect(!NativeActorVisibilityHasPendingRestore(f.Visibility), "native guest state is saveable");
    Expect(f.Step(0, false, true), "deferred extension selected");
    Expect(NativeActorVisibilityHasPendingRestore(f.Visibility), "save deferred while guest fields are temporary");
    f.End();
    Expect(!NativeActorVisibilityHasPendingRestore(f.Visibility), "save resumes after restoration");
    f.Begin(); f.Step(0, false, true);
    Expect(NativeActorVisibilityHasPendingRestore(f.Visibility), "outgoing state owns a temporary mutation");
    // A successful load replaces memory before clearing the host plan.
    f.Byte(f.Actor(0) + 0x19EU, 7U); f.Used(115U);
    const auto loadedFingerprint = f.Memory.ContentFingerprint();
    ResetNativeActorVisibilityAfterStateLoad(f.Visibility);
    Expect(f.Memory.ContentFingerprint() == loadedFingerprint, "load reset never writes outgoing values into incoming memory");
    Expect(!NativeActorVisibilityHasPendingRestore(f.Visibility) && !f.Visibility.BudgetWalk &&
           !f.Visibility.BudgetValid && !f.Visibility.CullingVolumeValid &&
           f.Visibility.Reservations.empty() && f.Visibility.Retained.empty() &&
           f.Visibility.LightAllocations == 0 && f.Visibility.PreviousLightAllocations == 0,
           "load discards stale reservations, retention, projections, and epoch counters");
    Expect(f.Visibility.ExtendedDrawDistance && f.Visibility.FovMultiplier == 1.1F &&
           f.Visibility.HostMemory == &f.Memory, "load preserves settings and host ownership");
    f.Begin(); Expect(f.Step(0, false), "resumed walk reads loaded cursor"); f.End();
    Expect(f.Used() == 116U && f.Byte(f.Actor(0) + 0x19EU) == 6U,
           "incoming native fade and live budget survive load");
}

void TestPlayerAndCustomConsumers() {
    Fixture players(2);
    for (uint32_t i = 0; i < 2; ++i) {
        Expect(players.Memory.Write16(players.Actor(i), 0), "player identity");
        players.Write(players.Actor(i) + 0x140U, 0x004BF618U);
    }
    players.Begin();
    Expect(players.Visibility.RemainingLights == 4, "get-item reservation is per player instance");
    players.Step(0, false); players.Allocate();
    Expect(players.Visibility.RemainingLights == 2, "whole player cost released after draw entry");
    players.Step(1, false); players.Allocate(); players.End();
    Expect(players.Used() == 5, "two wrapper plus two item allocations counted");
    for (uint32_t callback : {0x00135384U, 0x00228A34U, 0x00278B20U, 0x0037A590U, 0x00382B1CU, 0x003B5290U}) {
        Fixture special(1); special.Write(special.Actor(0) + 0x140U, callback); special.Begin();
        Expect(!special.Visibility.BudgetValid && !special.Step(0, false), "custom consumer walk stays native");
        special.End();
    }
    Fixture invoked(1); invoked.State.r[0] = Fixture::Play; invoked.State.r[1] = Fixture::Context;
    invoked.Hook(kActorCullingEntry);
    Expect(!invoked.Visibility.BudgetValid, "independent invocation cannot use gameplay pass contract");
}

void TestInvalidAndOff() {
    Fixture f(2); f.Write(f.Actor(1) + 0x140U, 0x003B5290U); f.Begin();
    Expect(!f.Visibility.BudgetValid, "unbounded custom draw fails closed before any grant");
    Expect(!f.Step(0, false), "invalid walk adds nothing"); f.Step(1, true); f.End();
    Expect(f.Used() == 2, "invalid walk preserves native draw");
    Fixture cycle(1); cycle.Write(cycle.Actor(0) + 0x130U, cycle.Actor(0)); cycle.Begin();
    Expect(!cycle.Visibility.BudgetValid, "cyclic lists fail closed");
    Fixture corrupt(1); corrupt.Write(corrupt.Arena + 12U, 0x00565620U); corrupt.Begin();
    Expect(!corrupt.Visibility.BudgetValid && !corrupt.Step(0, false), "bad arena cannot grant"); corrupt.End();
    Fixture off(1); off.Visibility.ExtendedDrawDistance = false; off.Begin();
    Expect(!off.Step(0, false), "option off keeps native far result"); off.End();
    Expect(off.Used() == 1 && off.Visibility.BudgetLimitedWalks == 0, "off budget is inactive");
    // The pre-existing width-only policy remains active while distance is off.
    off.Visibility.CullingVolume = {1, 2, 1}; off.Visibility.CullingVolumeValid = true;
    off.Float(off.Actor(0) + 0xECU, 600); off.Float(off.Actor(0) + 0xF4U, 500);
    off.Float(off.Actor(0) + 0xF8U, 500);
    Expect(off.Step(0, false, false, 1U), "off keeps existing widened-frustum policy");
    Fixture noLight(1); noLight.Write(noLight.Actor(0) + 4U, 0x20000000U); noLight.Begin();
    Expect(noLight.Step(0, false), "no-light actor still extends"); noLight.End();
    Expect(noLight.Used() == 1, "no-light draw does not increment allocator");
}

void TestSpawnHooksAndRateLimit() {
    Fixture f(1);
    Expect(f.Memory.MapRegion({"mure table", 0x00535000U, 0x1000U, true, false, {}}), "map mure table");
    Expect(f.Memory.MapRegion({"mure margin", 0x003CC000U, 0x1000U, true, false, {}}), "map mure margin");
    f.Visibility.HostMemory = &f.Memory;
    const std::array<float, 5> distances{1600, 1600, 1000, 1000, 1000};
    for (uint32_t i = 0; i < distances.size(); ++i) f.Float(0x005354B8U + i * 4U, distances[i] * 5);
    f.Float(0x003CC430U, 200);
    f.Hook(kObjMureSpawnEntry);
    for (uint32_t i = 0; i < distances.size(); ++i)
        Expect(f.Read(0x005354B8U + i * 4U) == std::bit_cast<uint32_t>(distances[i]), "native spawn table restored");
    Expect(f.Read(0x003CC430U) == std::bit_cast<uint32_t>(40.0F), "old extended retire margin restored");
    for (bool enabled : {false, true}) {
        f.Visibility.ExtendedDrawDistance = enabled;
        f.State.r[0] = f.Actor(0); f.State.r[1] = Fixture::Play;
        f.Float(f.Actor(0) + 0x1DCU, 1); f.Hook(kObjMure2SpawnEntry);
        Expect(f.Read(f.Actor(0) + 0x1DCU) == std::bit_cast<uint32_t>(1.0F), "group scalar remains native ahead");
        f.State.r[0] = 0; f.State.r[4] = f.Actor(0); const auto regs = f.State;
        f.Hook(kEnWood02SpawnZoneResult);
        Expect(std::memcmp(&regs, &f.State, sizeof(regs)) == 0, "offspring spawn result is native");
    }
    Fixture limited(1); limited.Used(116);
    for (unsigned i = 0; i < 16; ++i) { limited.Begin(); limited.Step(0, false); limited.End(); }
    Expect(limited.Visibility.BudgetLimitedWalks == 16 && limited.Visibility.BudgetDenied == 16,
           "denial counters count every walk, independent of log sampling");
}
} // namespace

int main() {
    try {
        const auto hooks = NativeActorVisibilityHookPcs();
        Expect(std::is_sorted(hooks.begin(), hooks.end()) &&
               std::adjacent_find(hooks.begin(), hooks.end()) == hooks.end(), "sorted unique hook filter");
        TestReservationsAndBurst(); TestExactCapacityWithUnexpectedNativeSuffix(); TestDeferredAndNativeFade(); TestIndependentExtensionFade(); TestMultipleWalksAndColdRestore(); TestSavestateLifecycle(); TestPlayerAndCustomConsumers(); TestInvalidAndOff(); TestSpawnHooksAndRateLimit();
    } catch (const std::exception& error) {
        std::cerr << "oot3d_native_a32_actor_visibility_tests: " << error.what() << '\n'; return 1;
    }
    std::cout << "oot3d_native_a32_actor_visibility_tests: ok\n";
}
