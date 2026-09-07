#include "oot3d_source_overlay_loader.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "oot3d_source_overlay_boot_leaves.h"
#include "oot3d_source_overlay_csab_curves.h"
#include "oot3d/csab_curve_eval.h"

namespace {

constexpr uint32_t kBaseAddress = 0x00001000U;
constexpr uint32_t kCutsceneLiteralAddress = 0x00322074U;
constexpr uint32_t kCutsceneCommandLiteralAddress = 0x002C5FF0U;
constexpr uint32_t kCutsceneSchedulerAddress = 0x00587958U;
constexpr uint32_t kCutscenePlayAddress = 0x08000000U;
constexpr uint32_t kCutsceneContextAddress = 0x08010000U;
constexpr uint32_t kCutsceneScriptAddress = 0x08020000U;
constexpr uint32_t kPauseRecordAddress = 0x08030000U;
constexpr uint32_t kPauseGlobalsAddress = 0x00435D18U;
constexpr uint32_t kCsabStateAddress = 0x08040000U;
constexpr uint32_t kCsabS16CurveAddress = 0x08040100U;
constexpr uint32_t kCsabF32CurveAddress = 0x08040200U;
constexpr uint32_t kCsabStackAddress = 0x08041000U;

struct TestMemory {
    std::array<uint8_t, 256> Bytes{};
    std::vector<uint8_t> CutsceneLiterals = std::vector<uint8_t>(0x14U);
    std::vector<uint8_t> CutsceneCommandLiterals =
        std::vector<uint8_t>(0x2138U);
    std::vector<uint8_t> CutsceneScheduler = std::vector<uint8_t>(0x10U);
    std::vector<uint8_t> CutscenePlay = std::vector<uint8_t>(0x7C64U);
    std::vector<uint8_t> CutsceneContext = std::vector<uint8_t>(0x280U);
    std::vector<uint8_t> CutsceneScript = std::vector<uint8_t>(0x100U);
    std::vector<uint8_t> PauseRecord = std::vector<uint8_t>(0x118U);
    std::array<uint8_t, 8> PauseGlobals{};
    std::array<uint8_t, 8> CsabState{};
    std::vector<uint8_t> CsabS16Curve = std::vector<uint8_t>(0x30U);
    std::vector<uint8_t> CsabF32Curve = std::vector<uint8_t>(0x40U);
    std::vector<uint8_t> CsabStack = std::vector<uint8_t>(0x100U);
    uint32_t GuestCalls = 0U;
    uint32_t LastGuestEntry = 0U;
    std::array<uint32_t, 4> LastGuestArguments{};
    uint32_t LastGuestStack = 0U;
    uint32_t LastGuestLink = 0U;
};

uint8_t* ResolvePointer(TestMemory& memory, uint32_t address, size_t size) {
    const auto resolve = [address, size](uint32_t base, auto& bytes)
        -> uint8_t* {
        if (address < base) {
            return nullptr;
        }
        const uint64_t offset = static_cast<uint64_t>(address) - base;
        if (offset + size > bytes.size()) {
            return nullptr;
        }
        return bytes.data() + static_cast<size_t>(offset);
    };
    if (auto* pointer = resolve(kBaseAddress, memory.Bytes)) {
        return pointer;
    }
    if (auto* pointer =
            resolve(kCutsceneLiteralAddress, memory.CutsceneLiterals)) {
        return pointer;
    }
    if (auto* pointer = resolve(kCutsceneCommandLiteralAddress,
                                memory.CutsceneCommandLiterals)) {
        return pointer;
    }
    if (auto* pointer =
            resolve(kCutsceneSchedulerAddress, memory.CutsceneScheduler)) {
        return pointer;
    }
    if (auto* pointer = resolve(kCutscenePlayAddress, memory.CutscenePlay)) {
        return pointer;
    }
    if (auto* pointer =
            resolve(kCutsceneContextAddress, memory.CutsceneContext)) {
        return pointer;
    }
    if (auto* pointer =
            resolve(kCutsceneScriptAddress, memory.CutsceneScript)) {
        return pointer;
    }
    if (auto* pointer = resolve(kPauseRecordAddress, memory.PauseRecord)) {
        return pointer;
    }
    if (auto* pointer = resolve(kPauseGlobalsAddress, memory.PauseGlobals)) {
        return pointer;
    }
    if (auto* pointer = resolve(kCsabStateAddress, memory.CsabState)) {
        return pointer;
    }
    if (auto* pointer =
            resolve(kCsabS16CurveAddress, memory.CsabS16Curve)) {
        return pointer;
    }
    if (auto* pointer =
            resolve(kCsabF32CurveAddress, memory.CsabF32Curve)) {
        return pointer;
    }
    return resolve(kCsabStackAddress, memory.CsabStack);
}

uint32_t ProbeMemory(void* context, uint32_t address, size_t size) {
    auto& memory = *static_cast<TestMemory*>(context);
    return ResolvePointer(memory, address, size) != nullptr
               ? OOT3D_SOURCE_OVERLAY_MEMORY_READ |
                     OOT3D_SOURCE_OVERLAY_MEMORY_WRITE
               : 0U;
}

int ReadMemory(void* context, uint32_t address, void* bytes, size_t size) {
    auto& memory = *static_cast<TestMemory*>(context);
    uint8_t* source = ResolvePointer(memory, address, size);
    if (bytes == nullptr || source == nullptr) {
        return 0;
    }
    std::memcpy(bytes, source, size);
    return 1;
}

int WriteMemory(void* context, uint32_t address, const void* bytes,
                size_t size) {
    auto& memory = *static_cast<TestMemory*>(context);
    uint8_t* destination = ResolvePointer(memory, address, size);
    if (bytes == nullptr || destination == nullptr) {
        return 0;
    }
    std::memcpy(destination, bytes, size);
    return 1;
}

const void* ResolveRead(void* context, uint32_t address, size_t size) {
    auto& memory = *static_cast<TestMemory*>(context);
    return ResolvePointer(memory, address, size);
}

void* ResolveWrite(void* context, uint32_t address, size_t size) {
    return const_cast<void*>(ResolveRead(context, address, size));
}

int CallGuest(void* context, uint32_t entry,
              Oot3dSourceOverlayGuestState* state,
              Oot3dSourceOverlayExecutionResult* result, uint32_t) {
    auto& memory = *static_cast<TestMemory*>(context);
    ++memory.GuestCalls;
    memory.LastGuestEntry = entry;
    std::copy_n(state->Registers, memory.LastGuestArguments.size(),
                memory.LastGuestArguments.begin());
    memory.LastGuestStack = state->Registers[13];
    memory.LastGuestLink = state->Registers[14];
    state->Registers[15] = state->Registers[14];
    result->Kind = OOT3D_SOURCE_OVERLAY_BRANCH;
    result->Pc = state->Registers[15];
    result->Detail = entry;
    result->BlocksConsumed = 1U;
    return 1;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T, size_t N>
std::vector<uint8_t> BuildCurve(
    uint8_t interpolationType, int32_t duration,
    const std::array<T, N>& keys) {
    Oot3dSourceCsabCurve::Oot3dAnimationCurveHeader header{};
    header.interpolationType = interpolationType;
    header.keyCount = static_cast<int32_t>(N);
    header.duration = duration;
    std::vector<uint8_t> result(sizeof(header) + sizeof(T) * N);
    std::memcpy(result.data(), &header, sizeof(header));
    std::memcpy(result.data() + sizeof(header), keys.data(),
                sizeof(T) * N);
    return result;
}

void TestCsabCurves(
    Oot3dNativeGame::Oot3dSourceOverlayModule& module,
    TestMemory& memory) {
    using F32Key = Oot3dSourceCsabCurve::Oot3dAnimationCurveF32LinearKey;
    const auto f32Curve = BuildCurve<F32Key>(
        1U, 10, std::array<F32Key, 2>{{{0, 2.0F}, {10, 4.0F}}});
    std::copy(f32Curve.begin(), f32Curve.end(), memory.CsabF32Curve.begin());
    const uint32_t f32Address = kCsabF32CurveAddress;
    std::memcpy(memory.CsabState.data(), &f32Address, sizeof(f32Address));
    memory.CsabState[4] = 0U;

    Oot3dSourceOverlayGuestState state{};
    state.Registers[0] = kCsabStateAddress;
    state.Registers[4] = 0x44444444U;
    state.Registers[5] = 0x55555555U;
    state.Registers[6] = 0x66666666U;
    state.Registers[13] = kCsabStackAddress + 0x80U;
    state.Registers[14] = 0x00123458U;
    state.Cpsr = 0x28000010U;
    state.Vfp[0] = std::bit_cast<uint32_t>(5.0F);
    state.Fpscr = 0x03C00010U;
    Oot3dSourceOverlayGuestState initial = state;
    Oot3dSourceOverlayExecutionResult result{sizeof(result)};
    Require(module.Execute(
                Oot3dSourceOverlay::CsabCurves::kF32Entry, &state,
                &result, 100U) != 0,
            "source CSAB F32 execution");
    Require(result.Kind == OOT3D_SOURCE_OVERLAY_BRANCH &&
                result.Pc == initial.Registers[14] &&
                result.BlocksConsumed == 1U,
            "source CSAB F32 result");
    Require(state.Vfp[0] == std::bit_cast<uint32_t>(3.0F),
            "source CSAB F32 interpolation");
    Require(state.Registers[15] == initial.Registers[14],
            "source CSAB F32 return");
    uint32_t stackValue = 0U;
    std::memcpy(&stackValue,
                memory.CsabStack.data() + 0x80U - 12U, sizeof(stackValue));
    Require(stackValue == initial.Registers[4],
            "source CSAB F32 stack scratch");

    using S16Key = Oot3dSourceCsabCurve::Oot3dAnimationCurveS16Key;
    const auto s16Curve = BuildCurve<S16Key>(
        1U, 0, std::array<S16Key, 1>{{{0, 0x2000, 0, 0}}});
    std::copy(s16Curve.begin(), s16Curve.end(), memory.CsabS16Curve.begin());
    const uint32_t s16Address = kCsabS16CurveAddress;
    std::memcpy(memory.CsabState.data(), &s16Address, sizeof(s16Address));
    state = initial;
    state.Vfp[0] = std::bit_cast<uint32_t>(0.0F);
    Require(module.Execute(
                Oot3dSourceOverlay::CsabCurves::kS16Entry, &state,
                &result, 100U) != 0,
            "source CSAB S16 execution");
    Require(state.Vfp[0] != 0U && state.Registers[15] == initial.Registers[14],
            "source CSAB S16 value and return");

    const auto s16HermiteCurve = BuildCurve<S16Key>(
        2U, 10, std::array<S16Key, 2>{{
            {0, 0x2000, -2, -2}, {10, 0x4000, -2, -2}}});
    std::copy(s16HermiteCurve.begin(), s16HermiteCurve.end(),
              memory.CsabS16Curve.begin());
    state = initial;
    state.Vfp[0] = std::bit_cast<uint32_t>(5.0F);
    Require(module.Execute(
                Oot3dSourceOverlay::CsabCurves::kS16Entry, &state,
                &result, 100U) != 0,
            "source CSAB S16 Hermite execution");
    Require(result.Pc == initial.Registers[14] &&
                state.Registers[15] == initial.Registers[14],
            "source CSAB S16 Hermite function return");
    Require(state.Registers[0] == 5U &&
                state.Registers[1] == 0x8E000000U &&
                state.Registers[2] == 0xFF000000U &&
                state.Registers[3] == kCsabS16CurveAddress +
                                           sizeof(Oot3dSourceCsabCurve::
                                                      Oot3dAnimationCurveHeader) &&
                state.Registers[12] == kCsabS16CurveAddress +
                                            sizeof(Oot3dSourceCsabCurve::
                                                       Oot3dAnimationCurveHeader) &&
                state.Registers[14] == 0x0035569CU,
            "source CSAB S16 Math_TanF classified ABI");
    Require(state.Cpsr == 0x28000010U,
            "source CSAB S16 Hermite Math_TanF CPSR");

    state = initial;
    state.Cpsr = 0x28000010U;
    state.Vfp[0] = std::bit_cast<uint32_t>(10.0F);
    Require(module.Execute(
                Oot3dSourceOverlay::CsabCurves::kS16Entry, &state,
                &result, 100U) != 0 && state.Cpsr == 0x68000010U,
            "source CSAB S16 terminal key-search CPSR");

    state = initial;
    state.Registers[0] = 0xDEAD0000U;
    const auto rejectedState = state;
    Require(module.Execute(
                Oot3dSourceOverlay::CsabCurves::kF32Entry, &state,
                &result, 100U) == 0,
            "malformed CSAB must fall back");
    Require(std::memcmp(&state, &rejectedState, sizeof(state)) == 0,
            "malformed CSAB mutates guest state");
}

} // namespace

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "expected source-overlay DLL path");
        TestMemory memory;
        for (size_t index = 0; index < 16U; ++index) {
            memory.Bytes[0x10U + index] = static_cast<uint8_t>(0x80U + index);
        }
        const Oot3dSourceOverlayHostApi host{
            sizeof(Oot3dSourceOverlayHostApi),
            OOT3D_SOURCE_OVERLAY_ABI_VERSION,
            &memory,
            ProbeMemory,
            ReadMemory,
            WriteMemory,
            ResolveRead,
            ResolveWrite,
            CallGuest,
            nullptr,
        };
        Oot3dNativeGame::Oot3dSourceOverlayModule module;
        std::string error;
        Require(module.Load(std::filesystem::path(argv[1]), host, &error),
                error.c_str());
        Require(module.IsLoaded(), "module loaded state");
        Require(module.EntryPoints().size() == 15U, "module entry count");
        Require(module.Contains(0x0034322CU), "memset entry registration");
        Require(module.Contains(0x0034325CU), "clear-ten entry registration");
        Require(module.Contains(0x00343270U), "counted clear entry registration");
        Require(module.Contains(0x00343280U), "memclear entry registration");
        Require(module.Contains(0x0034338CU), "memcpy entry registration");
        Require(module.Contains(0x00371738U), "memcpy entry registration");
        Require(module.Contains(0x00417014U),
                "GameState_Update entry registration");
        Require(module.Contains(0x00417024U),
                "GameState_Update continuation registration");
        Require(module.Contains(0x00321F50U),
                "Cutscene_UpdateFrame entry registration");
        Require(module.Contains(0x002C5BA0U),
                "Cutscene_ProcessCommands entry registration");
        Require(module.Contains(0x00435C54U),
                "PauseItemsPage_InitializeState entry registration");
        Require(module.Contains(0x00303B14U),
                "target memset entry registration");
        Require(module.Contains(0x003FFB7CU),
                "identity destructor entry registration");
        Require(module.Contains(Oot3dSourceOverlay::CsabCurves::kS16Entry),
                "CSAB S16 entry registration");
        Require(module.Contains(Oot3dSourceOverlay::CsabCurves::kF32Entry),
                "CSAB F32 entry registration");

        TestCsabCurves(module, memory);

        Oot3dSourceOverlayGuestState state{};
        state.Registers[0] = kBaseAddress + 0x40U;
        state.Registers[1] = kBaseAddress + 0x10U;
        state.Registers[2] = 16U;
        for (uint32_t index = 4U; index <= 10U; ++index) {
            state.Registers[index] = 0x11000000U + index;
        }
        state.Registers[13] = kBaseAddress + 0xF0U;
        state.Registers[14] = 0x00123458U;
        Oot3dSourceOverlayExecutionResult result{sizeof(result)};
        Require(module.Execute(0x00371738U, &state, &result, 100U) != 0,
                "memcpy overlay execution");
        Require(result.Kind == OOT3D_SOURCE_OVERLAY_BRANCH,
                "memcpy overlay result kind");
        Require(result.Pc == state.Registers[14], "memcpy return address");
        Require(state.Registers[0] == kBaseAddress + 0x50U,
                "memcpy destination advance");
        Require(state.Registers[1] == kBaseAddress + 0x20U &&
                    state.Registers[2] == 0xFFFFFFF0U &&
                    state.Registers[3] == 0x83828180U &&
                    state.Registers[12] == 0U,
                "memcpy ARM scratch-register result");
        Require((state.Cpsr & 0xF0000000U) == 0x40000000U,
                "memcpy ARM condition flags");
        for (uint32_t index = 4U; index <= 10U; ++index) {
            Require(state.Registers[index] == 0x11000000U + index,
                    "memcpy callee-saved register preservation");
        }
        Require(std::equal(memory.Bytes.begin() + 0x10U,
                           memory.Bytes.begin() + 0x20U,
                           memory.Bytes.begin() + 0x40U),
                "memcpy payload");
        const std::array<uint32_t, 8> expectedStack{
            0x11000004U, 0x11000005U, 0x11000006U, 0x11000007U,
            0x11000008U, 0x11000009U, 0x1100000AU, 0x00123458U,
        };
        Require(std::memcmp(memory.Bytes.data() + 0xD0U,
                            expectedStack.data(), sizeof(expectedStack)) == 0,
                "memcpy ARM stack spill");

        Oot3dSourceOverlayGuestState ownerState{};
        ownerState.Registers[13] = kBaseAddress + 0xF0U;
        ownerState.Registers[14] = 0x00123458U;
        ownerState.Registers[0] = kBaseAddress + 0x20U;
        ownerState.Registers[1] = 7U;
        ownerState.Registers[2] = 0xA5U;
        Oot3dSourceOverlayExecutionResult ownerResult{sizeof(ownerResult)};
        Require(module.Execute(0x0034322CU, &ownerState, &ownerResult, 100U) !=
                    0,
                "source memset execution");
        Require(std::all_of(memory.Bytes.begin() + 0x20U,
                            memory.Bytes.begin() + 0x27U,
                            [](uint8_t value) { return value == 0xA5U; }),
                "source memset payload");
        Require(ownerState.Registers[0] == kBaseAddress + 0x26U &&
                    ownerState.Registers[1] == 0U,
                "source memset ARM register result");
        Require((ownerState.Cpsr & 0xF0000000U) == 0x60000000U,
                "source memset ARM flags");

        std::fill(memory.Bytes.begin() + 0x30U,
                  memory.Bytes.begin() + 0x50U, 0xCCU);
        ownerState.Registers[0] = kBaseAddress + 0x30U;
        ownerState.Registers[1] = 19U;
        Require(module.Execute(0x00343280U, &ownerState, &ownerResult, 100U) !=
                    0,
                "source memclear execution");
        Require(std::all_of(memory.Bytes.begin() + 0x30U,
                            memory.Bytes.begin() + 0x43U,
                            [](uint8_t value) { return value == 0U; }),
                "source memclear payload");
        Require(memory.Bytes[0x43U] == 0xCCU,
                "source memclear range boundary");
        Require(ownerState.Registers[0] == kBaseAddress + 0x43U &&
                    ownerState.Registers[1] == 0xC0000000U &&
                    ownerState.Registers[2] == 0U &&
                    ownerState.Registers[3] == 0U &&
                    ownerState.Registers[12] == 0U &&
                    ownerState.Registers[13] == kBaseAddress + 0xF0U &&
                    ownerState.Registers[14] == 0x00123458U,
                "source memclear ARM register result");
        Require((ownerState.Cpsr & 0xF0000000U) == 0U,
                "source memclear ARM flags");

        std::fill(memory.Bytes.begin() + 0x50U,
                  memory.Bytes.begin() + 0x5AU, 0x7EU);
        ownerState.Registers[0] = kBaseAddress + 0x50U;
        Require(module.Execute(0x0034325CU, &ownerState, &ownerResult, 100U) !=
                    0,
                "source clear-ten execution");
        Require(std::all_of(memory.Bytes.begin() + 0x50U,
                            memory.Bytes.begin() + 0x5AU,
                            [](uint8_t value) { return value == 0U; }),
                "source clear-ten payload");
        Require(ownerState.Registers[1] == 0U,
                "source clear-ten ARM register result");

        const uint32_t countedCount = 2U;
        const uint32_t countedEntries = kBaseAddress + 0x80U;
        std::memcpy(memory.Bytes.data() + 0x60U, &countedCount,
                    sizeof(countedCount));
        std::memcpy(memory.Bytes.data() + 0x7CU, &countedEntries,
                    sizeof(countedEntries));
        std::fill(memory.Bytes.begin() + 0x80U,
                  memory.Bytes.begin() + 0x90U, 0xEFU);
        ownerState.Registers[0] = kBaseAddress + 0x60U;
        Require(module.Execute(0x00343270U, &ownerState, &ownerResult, 100U) !=
                    0,
                "source counted memclear execution");
        Require(std::all_of(memory.Bytes.begin() + 0x80U,
                            memory.Bytes.begin() + 0x90U,
                            [](uint8_t value) { return value == 0U; }),
                "source counted memclear payload");
        Require(ownerState.Registers[0] == kBaseAddress + 0x90U &&
                    ownerState.Registers[1] == 0U &&
                    ownerState.Registers[2] == 0U &&
                    ownerState.Registers[3] == 0U &&
                    ownerState.Registers[12] == 0U,
                "source counted memclear ARM register result");
        Require((ownerState.Cpsr & 0xF0000000U) == 0x40000000U,
                "source counted memclear ARM flags");

        for (size_t index = 0; index < 13U; ++index) {
            memory.Bytes[0xA3U + index] = static_cast<uint8_t>(0x30U + index);
        }
        ownerState.Registers[0] = kBaseAddress + 0xB1U;
        ownerState.Registers[1] = kBaseAddress + 0xA3U;
        ownerState.Registers[2] = 13U;
        Require(module.Execute(0x0034338CU, &ownerState, &ownerResult, 100U) !=
                    0,
                "source general memcpy execution");
        Require(std::equal(memory.Bytes.begin() + 0xA3U,
                           memory.Bytes.begin() + 0xB0U,
                           memory.Bytes.begin() + 0xB1U),
                "source general memcpy payload");
        Require(ownerState.Registers[0] == kBaseAddress + 0xBEU &&
                    ownerState.Registers[1] == kBaseAddress + 0xB0U &&
                    ownerState.Registers[2] == 0U &&
                    ownerState.Registers[3] == 0x3BU &&
                    ownerState.Registers[12] == 0x3CU,
                "source general memcpy ARM register result");
        Require((ownerState.Cpsr & 0xF0000000U) == 0x60000000U,
                "source general memcpy ARM flags");
        Require(memory.GuestCalls == 0U,
                "runtime memory owner is independent from the ABI oracle");

        std::fill(memory.Bytes.begin() + 0xC0U,
                  memory.Bytes.begin() + 0xD0U, 0xCCU);
        Oot3dSourceOverlayGuestState bootLeafState{};
        for (uint32_t index = 0U; index < 16U; ++index) {
            bootLeafState.Registers[index] = 0x71000000U + index;
        }
        bootLeafState.Registers[0] = kBaseAddress + 0xC3U;
        bootLeafState.Registers[1] = 11U;
        bootLeafState.Registers[2] = 0x000001A5U;
        bootLeafState.Registers[13] = kBaseAddress + 0xF0U;
        bootLeafState.Registers[14] = 0x00440000U;
        bootLeafState.Registers[15] = 0x00303B14U;
        bootLeafState.Cpsr = 0xA0000010U;
        bootLeafState.Fpscr = 0x03C00010U;
        const Oot3dSourceOverlayGuestState initialBootLeafState =
            bootLeafState;
        Oot3dSourceOverlayExecutionResult bootLeafResult{sizeof(bootLeafResult)};
        Require(module.Execute(0x00303B14U, &bootLeafState, &bootLeafResult,
                                100U) != 0,
                "source target memset execution");
        Require(std::all_of(memory.Bytes.begin() + 0xC3U,
                            memory.Bytes.begin() + 0xCEU,
                            [](uint8_t value) { return value == 0xA5U; }),
                "source target memset payload");
        Require(bootLeafState.Registers[0] == kBaseAddress + 0xCEU &&
                    bootLeafState.Registers[1] == initialBootLeafState.Registers[1] &&
                    bootLeafState.Registers[2] == initialBootLeafState.Registers[2] &&
                    bootLeafState.Registers[14] == initialBootLeafState.Registers[14] &&
                    bootLeafState.Registers[15] == initialBootLeafState.Registers[14] &&
                    bootLeafState.Cpsr == initialBootLeafState.Cpsr,
                "source target memset return ABI");
        Require(memory.Bytes[0xC2U] == 0xCCU &&
                    memory.Bytes[0xCEU] == 0xCCU,
                "source target memset range boundary");

        // Exercise every target-size branch and all four destination
        // alignments against the maintained byte-fill contract.
        for (uint32_t alignment = 0U; alignment < 4U; ++alignment) {
            for (uint32_t size = 0U; size <= 32U; ++size) {
                std::fill(memory.Bytes.begin() + 0xC0U,
                          memory.Bytes.begin() + 0xF0U, 0xCDU);
                Oot3dSourceOverlayGuestState matrixState =
                    initialBootLeafState;
                const uint32_t destination =
                    kBaseAddress + 0xC0U + alignment;
                matrixState.Registers[0] = destination;
                matrixState.Registers[1] = size;
                matrixState.Registers[2] = 0x0000005AU;
                Require(module.Execute(0x00303B14U, &matrixState,
                                        &bootLeafResult, 100U) != 0,
                        "target memset size/alignment matrix execution");
                Require(matrixState.Registers[0] == destination + size,
                        "target memset size/alignment return pointer");
                for (uint32_t offset = 0U; offset < size; ++offset) {
                    Require(memory.Bytes[0xC0U + alignment + offset] == 0x5AU,
                            "target memset size/alignment payload");
                }
                if (size == 0U) {
                    Require(memory.Bytes[0xC0U + alignment] == 0xCDU,
                            "target memset zero-size no-write");
                }
            }
        }

        Oot3dSourceOverlayGuestState invalidBootLeafState = bootLeafState;
        invalidBootLeafState.Registers[0] = 0xFFFFFFF0U;
        invalidBootLeafState.Registers[1] = 0x40U;
        const Oot3dSourceOverlayGuestState expectedInvalidBootLeafState =
            invalidBootLeafState;
        Require(module.Execute(0x00303B14U, &invalidBootLeafState,
                                &bootLeafResult, 100U) == 0,
                "invalid target memset falls through to whole-AOT");
        Require(std::memcmp(&invalidBootLeafState,
                            &expectedInvalidBootLeafState,
                            sizeof(invalidBootLeafState)) == 0,
                "invalid target memset is transactional");

        Oot3dSourceOverlayGuestState identityState = initialBootLeafState;
        identityState.Registers[0] = kBaseAddress + 0x20U;
        identityState.Registers[15] =
            Oot3dSourceOverlay::BootLeaves::kIdentityDestructorEntry;
        const Oot3dSourceOverlayGuestState expectedIdentityState = [&] {
            Oot3dSourceOverlayGuestState expected = identityState;
            expected.Registers[15] = expected.Registers[14];
            return expected;
        }();
        Require(module.Execute(
                    Oot3dSourceOverlay::BootLeaves::kIdentityDestructorEntry,
                    &identityState, &bootLeafResult,
                                100U) != 0,
                "source identity destructor execution");
        Require(std::memcmp(&identityState, &expectedIdentityState,
                            sizeof(identityState)) == 0 &&
                    bootLeafResult.Kind == OOT3D_SOURCE_OVERLAY_BRANCH &&
                    bootLeafResult.Pc == identityState.Registers[14],
                "source identity destructor ABI");

        constexpr uint32_t kGameState = kBaseAddress;
        constexpr uint32_t kGameStateMain = 0x00420000U;
        constexpr uint32_t kGameStateReturn = 0x00430000U;
        constexpr uint32_t kGameStateStackTop = kBaseAddress + 0xE8U;
        constexpr uint32_t kSavedR4 = 0xABCDEF01U;
        constexpr uint32_t kCallbackCpsr = 0xA0000010U;
        uint32_t frameCounter = 41U;
        std::memcpy(memory.Bytes.data() + 0x04U, &kGameStateMain,
                    sizeof(kGameStateMain));
        std::memcpy(memory.Bytes.data() + 0xF8U, &frameCounter,
                    sizeof(frameCounter));

        Oot3dSourceOverlayGuestState gameStateOwner{};
        gameStateOwner.Registers[0] = kGameState;
        gameStateOwner.Registers[4] = kSavedR4;
        gameStateOwner.Registers[13] = kGameStateStackTop;
        gameStateOwner.Registers[14] = kGameStateReturn;
        gameStateOwner.Cpsr = 0x60000010U;
        Oot3dSourceOverlayExecutionResult gameStateResult{
            sizeof(gameStateResult)};
        Require(module.Execute(0x00417014U, &gameStateOwner,
                               &gameStateResult, 100U) != 0,
                "source GameState_Update entry execution");
        Require(gameStateResult.Kind == OOT3D_SOURCE_OVERLAY_BRANCH &&
                    gameStateResult.Pc == kGameStateMain &&
                    gameStateOwner.Registers[1] == kGameStateMain &&
                    gameStateOwner.Registers[4] == kGameState &&
                    gameStateOwner.Registers[13] ==
                        kGameStateStackTop - 8U &&
                    gameStateOwner.Registers[14] == 0x00417024U,
                "source GameState_Update callback dispatch ABI");
        const std::array<uint32_t, 2> expectedGameStateFrame{
            kSavedR4, kGameStateReturn};
        Require(std::memcmp(memory.Bytes.data() + 0xE0U,
                            expectedGameStateFrame.data(),
                            sizeof(expectedGameStateFrame)) == 0,
                "source GameState_Update saved frame");

        gameStateOwner.Registers[0] = 0xDEADBEEFU;
        gameStateOwner.Registers[1] = 0xCAFEBABEU;
        gameStateOwner.Cpsr = kCallbackCpsr;
        Require(module.Execute(0x00417024U, &gameStateOwner,
                               &gameStateResult, 100U) != 0,
                "source GameState_Update continuation execution");
        std::memcpy(&frameCounter, memory.Bytes.data() + 0xF8U,
                    sizeof(frameCounter));
        Require(gameStateResult.Kind == OOT3D_SOURCE_OVERLAY_BRANCH &&
                    gameStateResult.Pc == kGameStateReturn &&
                    gameStateOwner.Registers[0] == 42U &&
                    gameStateOwner.Registers[4] == kSavedR4 &&
                    gameStateOwner.Registers[13] == kGameStateStackTop &&
                    gameStateOwner.Registers[14] == 0x00417024U &&
                    gameStateOwner.Cpsr == kCallbackCpsr &&
                    frameCounter == 42U,
                "source GameState_Update continuation ABI");
        Require(memory.GuestCalls == 0U,
                "GameState_Update owner is independent from the ABI oracle");

        constexpr uint32_t kPausePrimary = 0x0054A9F0U;
        constexpr uint32_t kPauseSecondary = 0x004EC274U;
        constexpr uint32_t kPauseSavedR4 = 0x44AA55BBU;
        constexpr uint32_t kPauseStack = kBaseAddress + 0xD0U;
        constexpr uint32_t kPauseReturn = 0x00422B28U;
        std::array<uint32_t, 0x46U> expectedPauseRecord{};
        for (size_t index = 0U; index < expectedPauseRecord.size(); ++index) {
            expectedPauseRecord[index] =
                0xA5000000U + static_cast<uint32_t>(index);
        }
        std::memcpy(memory.PauseRecord.data(), expectedPauseRecord.data(),
                    sizeof(expectedPauseRecord));
        const std::array<uint32_t, 2> pauseGlobals{
            kPausePrimary, kPauseSecondary};
        std::memcpy(memory.PauseGlobals.data(), pauseGlobals.data(),
                    sizeof(pauseGlobals));

        Oot3dSourceOverlayGuestState pauseState{};
        for (uint32_t index = 0U; index < 16U; ++index) {
            pauseState.Registers[index] = 0x33000000U + index;
        }
        pauseState.Registers[0] = kPauseRecordAddress;
        pauseState.Registers[4] = kPauseSavedR4;
        pauseState.Registers[13] = kPauseStack;
        pauseState.Registers[14] = kPauseReturn;
        pauseState.Cpsr = 0xA0000010U;
        pauseState.Fpscr = 0x03C00010U;
        pauseState.ThreadPointer = 0x12345000U;
        const Oot3dSourceOverlayGuestState initialPauseState = pauseState;
        Oot3dSourceOverlayGuestState expectedPauseState = initialPauseState;
        expectedPauseState.Registers[1] = kPauseSecondary;
        expectedPauseState.Registers[12] = kPausePrimary;
        expectedPauseState.Registers[15] = kPauseReturn;
        Oot3dSourceOverlayExecutionResult pauseResult{sizeof(pauseResult)};
        Require(module.Execute(0x00435C54U, &pauseState, &pauseResult, 100U) !=
                    0,
                "source pause state execution");
        Require(std::memcmp(&pauseState, &expectedPauseState,
                            sizeof(pauseState)) == 0,
                "source pause state ARM register ABI");
        Require(pauseResult.Kind == OOT3D_SOURCE_OVERLAY_BRANCH &&
                    pauseResult.Pc == kPauseReturn &&
                    pauseResult.Detail == 0x00435C54U &&
                    pauseResult.BlocksConsumed == 1U,
                "source pause state return flow");

        for (size_t index = 1U; index <= 0x15U; ++index) {
            expectedPauseRecord[index] = 0U;
        }
        for (size_t index = 0x29U; index <= 0x2DU; ++index) {
            expectedPauseRecord[index] = 0U;
        }
        expectedPauseRecord[0x31U] = 0U;
        expectedPauseRecord[0x34U] = 0U;
        expectedPauseRecord[0x35U] = 0U;
        expectedPauseRecord[0x36U] = UINT32_MAX;
        expectedPauseRecord[0x28U] = kPausePrimary;
        for (size_t index = 0x38U; index <= 0x3CU; ++index) {
            expectedPauseRecord[index] = 0U;
        }
        expectedPauseRecord[0x40U] = 0U;
        expectedPauseRecord[0x43U] = 0U;
        expectedPauseRecord[0x44U] = 0U;
        expectedPauseRecord[0x45U] = UINT32_MAX;
        expectedPauseRecord[0U] = kPauseSecondary;
        expectedPauseRecord[0x37U] = kPausePrimary;
        Require(std::memcmp(memory.PauseRecord.data(),
                            expectedPauseRecord.data(),
                            sizeof(expectedPauseRecord)) == 0,
                "source pause state record contents");
        uint32_t pauseSpill = 0U;
        std::memcpy(&pauseSpill,
                    memory.Bytes.data() +
                        (kPauseStack - 4U - kBaseAddress),
                    sizeof(pauseSpill));
        Require(pauseSpill == kPauseSavedR4,
                "source pause state A32 stack spill");

        const std::vector<uint8_t> pauseRecordBeforeReject =
            memory.PauseRecord;
        Oot3dSourceOverlayGuestState rejectedPauseState = initialPauseState;
        rejectedPauseState.Registers[0] = 0xFFFFFFF0U;
        const Oot3dSourceOverlayGuestState expectedRejectedPauseState =
            rejectedPauseState;
        Oot3dSourceOverlayExecutionResult rejectedPauseResult{
            sizeof(rejectedPauseResult)};
        Require(module.Execute(0x00435C54U, &rejectedPauseState,
                               &rejectedPauseResult, 100U) == 0,
                "invalid pause state falls through to whole-AOT");
        Require(std::memcmp(&rejectedPauseState,
                            &expectedRejectedPauseState,
                            sizeof(rejectedPauseState)) == 0 &&
                    memory.PauseRecord == pauseRecordBeforeReject,
                "rejected pause state is transactional");
        Require(memory.GuestCalls == 0U,
                "pause state owner is independent from the ABI oracle");

        const std::array<uint32_t, 5> cutsceneLiterals{
            kCutsceneSchedulerAddress, 0x0000FFF0U, 0x00007C60U,
            0x38000000U, 0x41F00000U};
        std::memcpy(memory.CutsceneLiterals.data(), cutsceneLiterals.data(),
                    sizeof(cutsceneLiterals));
        const uint32_t schedulerReady = 0x0000FFF0U;
        std::memcpy(memory.CutsceneScheduler.data() + 8U, &schedulerReady,
                    sizeof(schedulerReady));
        const uint32_t cutsceneData = kCutsceneScriptAddress;
        const int32_t cutsceneEndFrame = 100;
        std::memcpy(memory.CutsceneScript.data() + 0x0CU,
                    &cutsceneEndFrame, sizeof(cutsceneEndFrame));
        const uint32_t staleOwnerState = 0xFFFFFFFFU;
        std::memcpy(memory.CutscenePlay.data() + 0x229CU, &cutsceneData,
                    sizeof(cutsceneData));
        std::memcpy(memory.CutscenePlay.data() + 0x21A0U,
                    &staleOwnerState, sizeof(staleOwnerState));
        memory.CutsceneContext[0x27AU] = 0xFFU;

        constexpr uint32_t kCutsceneStack = kBaseAddress + 0xE8U;
        constexpr uint32_t kCutsceneReturn = 0x00440000U;
        Oot3dSourceOverlayGuestState cutsceneState{};
        cutsceneState.Registers[0] = kCutscenePlayAddress;
        cutsceneState.Registers[1] = kCutsceneContextAddress;
        cutsceneState.Registers[13] = kCutsceneStack;
        cutsceneState.Registers[14] = kCutsceneReturn;
        cutsceneState.Fpscr = 0x03C00010U;
        Oot3dSourceOverlayExecutionResult cutsceneResult{
            sizeof(cutsceneResult)};
        Require(module.Execute(0x00321F50U, &cutsceneState,
                               &cutsceneResult, 100U) != 0,
                "source Cutscene_UpdateFrame execution");
        uint16_t cutsceneFrame = 0U;
        uint32_t clearedOwnerState = 1U;
        int32_t parsedEndFrame = 0;
        std::memcpy(&cutsceneFrame,
                    memory.CutsceneContext.data() + 0x20U,
                    sizeof(cutsceneFrame));
        std::memcpy(&clearedOwnerState,
                    memory.CutscenePlay.data() + 0x21A0U,
                    sizeof(clearedOwnerState));
        std::memcpy(&parsedEndFrame,
                    memory.CutsceneContext.data() + 0x18U,
                    sizeof(parsedEndFrame));
        Require(cutsceneResult.Kind == OOT3D_SOURCE_OVERLAY_BRANCH &&
                    cutsceneResult.Pc == kCutsceneReturn &&
                    cutsceneState.Registers[0] == kCutscenePlayAddress &&
                    cutsceneState.Registers[1] == kCutsceneContextAddress &&
                    cutsceneState.Registers[13] == kCutsceneStack &&
                    cutsceneState.Registers[14] == kCutsceneReturn &&
                    cutsceneState.Fpscr == 0x03C00010U,
                "source Cutscene_UpdateFrame return ABI");
        Require(cutsceneFrame == 1U && parsedEndFrame == cutsceneEndFrame &&
                    clearedOwnerState == 0U &&
                    memory.CutsceneContext[0x27AU] == 0U,
                "source cutscene frame and command state update");
        Require(memory.GuestCalls == 1U &&
                    memory.LastGuestEntry == 0x00307650U &&
                    memory.LastGuestArguments[0] == kCutscenePlayAddress &&
                    memory.LastGuestArguments[1] ==
                        kCutsceneContextAddress &&
                    memory.LastGuestArguments[2] == cutsceneData &&
                    memory.LastGuestStack ==
                        kCutsceneStack - 0x18U - 0x100U &&
                    memory.LastGuestLink == 0x0BAD3000U,
                "source cutscene owner composition and residual ABI");
        std::cout << "source-overlay loader tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "source-overlay loader tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
