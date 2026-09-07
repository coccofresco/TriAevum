#include "oot3d_native_pica_composition.h"

#include "oot3d_native_a32_memory.h"
#include "oot3d_typed_gameplay_bridge.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <cstdlib>
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

} // namespace

int main() {
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
    const auto hooks =
        Oot3dNativeGame::Oot3dNativePicaCompositionHookPcs();
    Require(hooks.size() == 41U &&
                std::find(
                    hooks.begin(), hooks.end(),
                    Oot3dNativeGame::kOot3dPicaPrimitivePacketBuilderEntry) !=
                    hooks.end() &&
                std::find(
                    hooks.begin(), hooks.end(),
                    Oot3dNativeGame::kOot3dKankyoEffectPrimitiveDrawEntry) !=
                    hooks.end(),
            "native composition hook inventory is incomplete");

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

    std::cout << "oot3d_native_pica_composition_tests: ok\n";
    return 0;
}
