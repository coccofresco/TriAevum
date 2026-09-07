#include "oot3d_native_a32_process.h"
#include "oot3d_source_actor_init_context_runtime.h"
#include "oot3d_a32_generated.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr uint32_t kOwnerEntry = 0x0044E7C0U;
constexpr uint32_t kOwnerReturn = 0x0BADF00CU;
constexpr uint32_t kTextBase = 0x00300000U;
constexpr uint32_t kTextSize = 0x00180000U;
constexpr uint32_t kDataBase = 0x00500000U;
constexpr uint32_t kDataSize = 0x00100000U;
constexpr uint32_t kRam = 0x10000000U;
constexpr uint32_t kRamSize = 0x00080000U;
constexpr uint32_t kPlay = kRam;
constexpr uint32_t kActorContext = kPlay + 0x208CU;
constexpr uint32_t kPlayerSpawnEntry = kPlay + 0x5C0CU;
constexpr uint32_t kPlayer = kRam + 0x08000U;
constexpr uint32_t kFactoryObjectBase = kRam + 0x10000U;
constexpr uint32_t kAllocatorObject = kRam + 0x30000U;
constexpr uint32_t kMaterialObjectBase = kRam + 0x40000U;
constexpr uint32_t kLightNode = kRam + 0x70000U;
constexpr uint32_t kStack = 0x20000000U;
constexpr uint32_t kTls = 0x20010000U;
constexpr uint32_t kRendererGuard = 0x0055A21CU;
constexpr uint32_t kRendererState = 0x005BE5B8U;
constexpr uint32_t kRendererFactory = 0x00501000U;
constexpr uint32_t kRendererFactoryVtable = 0x00501100U;
constexpr uint32_t kRendererFactoryCreate = 0x00301000U;
constexpr uint32_t kAllocatorGlobal = 0x0055A1F8U;
constexpr uint32_t kAllocator = 0x00501200U;
constexpr uint32_t kAllocatorVtable = 0x00501300U;
constexpr uint32_t kAllocatorAllocate = 0x00301010U;
constexpr uint32_t kSaveContext = 0x00587958U;
constexpr uint32_t kAttentionState = 0x0050C8E8U;
constexpr uint32_t kAttentionTable = 0x0050C998U;
constexpr uint32_t kLightInfo = 0x00569890U;

constexpr uint32_t kActorSpawn = 0x003738D0U;
constexpr uint32_t kInitializeActorRuntime = 0x004644A8U;
constexpr uint32_t kMemclear = 0x00343280U;
constexpr uint32_t kInitializeActorContextGlobals = 0x0045FE94U;
constexpr uint32_t kObjectGetIndex = 0x00363C10U;
constexpr uint32_t kZarGetCmbByIndex = 0x00358EF8U;
constexpr uint32_t kZarGetCtxbByIndex = 0x00372C90U;
constexpr uint32_t kStaticInitGuardAcquire = 0x003679B4U;
constexpr uint32_t kRendererGlobalStateInit = 0x0036788CU;
constexpr uint32_t kStoreChild14Byte16 = 0x0047D568U;
constexpr uint32_t kLightsPointNoGlowSetInfo = 0x003591E4U;
constexpr uint32_t kLightContextInsertLight = 0x0034FAA8U;
constexpr uint32_t kInitializeModelStorage = 0x00348F34U;
constexpr uint32_t kFinalizeModelStorage = 0x00348BE4U;
constexpr uint32_t kInitializeVertexLayout = 0x00348A64U;
constexpr uint32_t kCreateMaterial = 0x0034897CU;
constexpr uint32_t kConfigureMaterial = 0x003429C8U;
constexpr uint32_t kGetIndexedField58Entry = 0x00372F0CU;
constexpr uint32_t kBindModelResource = 0x00372D94U;

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void WriteWord(std::vector<uint8_t>& bytes, uint32_t base,
               uint32_t address, uint32_t value) {
    Expect(address >= base &&
               static_cast<uint64_t>(address - base) + sizeof(value) <=
                   bytes.size(),
           "literal lies outside mapped owner text");
    std::memcpy(bytes.data() + (address - base), &value, sizeof(value));
}

void ReturnToLinkRegister(
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::ExecutionResult* result) {
    state.r[15] = state.r[14];
    *result = {
        oot3d::recomp::a32::ExitKind::Branch,
        state.r[15],
        oot3d::recomp::a32::FallbackReason::None,
        0U,
    };
}

struct OwnerProbe {
    uint32_t FactoryCalls = 0U;
    uint32_t MaterialCalls = 0U;
    uint32_t ActorSpawnCalls = 0U;
    uint32_t LightCalls = 0U;
    uint32_t RendererInitCalls = 0U;
    uint32_t AllocatorCalls = 0U;
    bool ActorSpawnAbiMatched = true;
    bool LightAbiMatched = true;
    bool AllocatorAbiMatched = true;
    bool FinalizeModelAbiMatched = true;
    bool CreateMaterialAbiMatched = true;
    std::string Error;

    void Reset() {
        *this = {};
    }
};

bool HandleOwnerNativeFunction(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result, void* user) {
    if (result == nullptr || user == nullptr) {
        return false;
    }
    auto& probe = *static_cast<OwnerProbe*>(user);
    const auto finish = [&]() {
        ReturnToLinkRegister(state, result);
        return true;
    };
    switch (pc) {
    case kActorSpawn: {
        uint32_t rotY = 0U;
        uint32_t rotZ = 0U;
        uint32_t params = 0U;
        uint32_t initializeNow = 0U;
        probe.ActorSpawnAbiMatched &=
            memory.Read32(state.r[13], &rotY) &&
            memory.Read32(state.r[13] + 4U, &rotZ) &&
            memory.Read32(state.r[13] + 8U, &params) &&
            memory.Read32(state.r[13] + 12U, &initializeNow) &&
            state.r[0] == kActorContext && state.r[1] == kPlay &&
            state.r[2] == 7U && state.r[3] == 4U &&
            state.vfp[0] == std::bit_cast<uint32_t>(1.0F) &&
            state.vfp[1] == std::bit_cast<uint32_t>(2.0F) &&
            state.vfp[2] == std::bit_cast<uint32_t>(3.0F) &&
            rotY == 5U && rotZ == 6U && params == 0x1234U &&
            initializeNow == 1U;
        ++probe.ActorSpawnCalls;
        if (!memory.Write32(kActorContext + 0x20U, kPlayer) ||
            !memory.Write8(kPlayer + 0x02U, 0U) ||
            !memory.Write32(kPlayer + 0x3CU, 0x11223344U) ||
            !memory.Write32(
                kPlayer + 0x40U, std::bit_cast<uint32_t>(10.0F)) ||
            !memory.Write32(kPlayer + 0x44U, 0x55667788U) ||
            !memory.Write32(
                kPlayer + 0x50U, std::bit_cast<uint32_t>(2.0F)) ||
            !memory.Write32(
                kPlayer + 0x58U, std::bit_cast<uint32_t>(3.0F))) {
            return false;
        }
        state.r[0] = kPlayer;
        return finish();
    }
    case kMemclear:
        for (uint32_t offset = 0U; offset < state.r[1]; ++offset) {
            if (!memory.Write8(state.r[0] + offset, 0U)) {
                return false;
            }
        }
        return finish();
    case kObjectGetIndex:
        state.r[0] = 0U;
        return finish();
    case kZarGetCmbByIndex:
        state.r[0] = 0x00570000U + state.r[1] * 4U;
        return finish();
    case kZarGetCtxbByIndex:
        state.r[0] = 0x00571000U + state.r[1] * 4U;
        return finish();
    case kStaticInitGuardAcquire: {
        uint32_t guard = 0U;
        if (!memory.Read32(state.r[0], &guard)) {
            return false;
        }
        state.r[0] = (guard & 1U) == 0U ? 1U : 0U;
        if (state.r[0] != 0U &&
            !memory.Write32(kRendererGuard, 1U)) {
            return false;
        }
        return finish();
    }
    case kRendererGlobalStateInit:
        ++probe.RendererInitCalls;
        return finish();
    case kRendererFactoryCreate: {
        const uint32_t object =
            kFactoryObjectBase + probe.FactoryCalls * 0x100U;
        const uint32_t child = object + 0x80U;
        const uint32_t child14 = object + 0xC0U;
        ++probe.FactoryCalls;
        if (!memory.Write32(object + 0x0CU, child) ||
            !memory.Write32(object + 0x14U, child14)) {
            return false;
        }
        state.r[0] = object;
        return finish();
    }
    case kStoreChild14Byte16: {
        uint32_t child = 0U;
        if (!memory.Read32(state.r[0] + 0x14U, &child) ||
            !memory.Write8(child + 0x16U,
                           static_cast<uint8_t>(state.r[1]))) {
            return false;
        }
        return finish();
    }
    case kLightsPointNoGlowSetInfo: {
        uint32_t radius = 0U;
        uint32_t attenuation = 0U;
        probe.LightAbiMatched &=
            state.r[0] == kLightInfo &&
            state.r[1] == 0xFFU && state.r[2] == 0xFFU &&
            state.r[3] == 0xFFU &&
            memory.Read32(state.r[13], &radius) &&
            memory.Read32(state.r[13] + 4U, &attenuation) &&
            state.vfp[0] == std::bit_cast<uint32_t>(10.0F) &&
            state.vfp[1] == std::bit_cast<uint32_t>(100.0F) &&
            state.vfp[2] == std::bit_cast<uint32_t>(30.0F) &&
            radius == 0xFFFFFFFFU && attenuation == 0U;
        ++probe.LightCalls;
        if (!memory.Write32(kLightInfo, state.vfp[0]) ||
            !memory.Write32(kLightInfo + 4U, state.vfp[1]) ||
            !memory.Write32(kLightInfo + 8U, state.vfp[2]) ||
            !memory.Write32(kLightInfo + 12U, radius)) {
            return false;
        }
        return finish();
    }
    case kLightContextInsertLight:
        state.r[0] = kLightNode;
        return finish();
    case kAllocatorAllocate:
        probe.AllocatorAbiMatched &=
            state.r[0] == kAllocator && state.r[1] == 0x1B8U &&
            state.r[2] == 0x0044EFB0U && state.r[3] == 0x1C0CU;
        ++probe.AllocatorCalls;
        state.r[0] = kAllocatorObject;
        return finish();
    case kInitializeModelStorage:
        state.r[0] = state.r[0];
        return finish();
    case kFinalizeModelStorage:
        probe.FinalizeModelAbiMatched &=
            state.r[0] == kAllocatorObject;
        return finish();
    case kInitializeVertexLayout:
        return finish();
    case kCreateMaterial:
        probe.CreateMaterialAbiMatched &=
            state.r[2] == 0U && state.r[3] == 0U;
        state.r[0] =
            kMaterialObjectBase + probe.MaterialCalls * 0x200U;
        ++probe.MaterialCalls;
        return finish();
    case kConfigureMaterial: {
        for (uint32_t index = 0U; index < 4U; ++index) {
            uint32_t value = 0U;
            if (!memory.Read32(
                    state.r[2] + index * sizeof(uint32_t), &value) ||
                !memory.Write32(
                    state.r[0] + 0x100U +
                        index * sizeof(uint32_t),
                    value)) {
                return false;
            }
        }
        return finish();
    }
    case kGetIndexedField58Entry:
        state.r[0] = 0x00572000U + state.r[1] * 4U;
        return finish();
    case kBindModelResource:
        if (!memory.Write32(state.r[0], state.r[1])) {
            return false;
        }
        return finish();
    case kInitializeActorRuntime:
    case kInitializeActorContextGlobals:
        return finish();
    default:
        return false;
    }
}

constexpr std::array<uint32_t, 20> kNativeFunctionPcs{
    kActorSpawn,
    kInitializeActorRuntime,
    kMemclear,
    kInitializeActorContextGlobals,
    kObjectGetIndex,
    kZarGetCmbByIndex,
    kZarGetCtxbByIndex,
    kStaticInitGuardAcquire,
    kRendererGlobalStateInit,
    kStoreChild14Byte16,
    kLightsPointNoGlowSetInfo,
    kLightContextInsertLight,
    kInitializeModelStorage,
    kFinalizeModelStorage,
    kInitializeVertexLayout,
    kCreateMaterial,
    kConfigureMaterial,
    kGetIndexedField58Entry,
    kBindModelResource,
    kRendererFactoryCreate,
};

class OwnerHostServices final
    : public Oot3dNativeGame::NativeA32HostServices {
  public:
    Oot3dNativeGame::NativeA32HostResult HandleSvc(
        uint32_t immediate, oot3d::recomp::a32::GuestState&,
        Oot3dNativeGame::NativeA32Memory&,
        Oot3dNativeGame::NativeA32HostContext&) override {
        return {
            Oot3dNativeGame::NativeA32HostAction::Fault,
            std::nullopt,
            immediate,
            "unexpected Actor_InitContext SVC",
        };
    }
};

struct ProcessFixture {
    ProcessFixture()
        : Process(oot3d::recomp::GetA32GeneratedRegistry(), Host),
          SourceRuntime(Process) {
    }

    OwnerHostServices Host;
    Oot3dNativeGame::NativeA32Process Process;
    Oot3dNativeGame::SourceActorInitContextRuntime SourceRuntime;
    OwnerProbe Probe;
};

void ConfigureProcess(ProcessFixture& fixture, bool initializedGuard) {
    std::vector<uint8_t> text(kTextSize);
    constexpr std::array<std::pair<uint32_t, uint32_t>, 31> literals{{
        {0x0044ECACU, 0x00587958U},
        {0x0044ECB0U, 0x0055A21CU},
        {0x0044ECB4U, 0x005BE5B8U},
        {0x0044ECC0U, 0x005C1858U},
        {0x0044ECC4U, 0x0050CD84U},
        {0x0044ECC8U, 0x000001DBU},
        {0x0044ECCCU, 0x00000000U},
        {0x0044ECD0U, 0x00003A64U},
        {0x0044ECD4U, 0x40000000U},
        {0x0044ECD8U, 0x0050C998U},
        {0x0044ECDCU, 0x0050C8E8U},
        {0x0044EFA8U, 0x42A00000U},
        {0x0044EFACU, 0x00569890U},
        {0x0044EFE8U, 0x0055A1F8U},
        {0x0044EFECU, 0x00001C0CU},
        {0x0044EFF0U, 0x0050CA8CU},
        {0x0044EFF4U, 0x0000812FU},
        {0x0044EFF8U, 0x00002601U},
        {0x0044EFFCU, 0x3EC8C8CAU},
        {0x0044F000U, 0x3F48C8CAU},
        {0x0044F004U, 0x3F800000U},
        {0x0044F008U, 0x005C05B8U},
        {0x00463420U, 0x00003A64U},
        {0x00463424U, 0x0055A21CU},
        {0x00463428U, 0x005BE5B8U},
        {0x00463434U, 0x43480000U},
        {0x00463438U, 0x42F00000U},
        {0x0046343CU, 0x40000000U},
        {0x00463440U, 0x00000000U},
        {0x00463444U, 0x43340000U},
        {0x00463448U, 0x3E800000U},
    }};
    for (const auto& [address, value] : literals) {
        WriteWord(text, kTextBase, address, value);
    }

    std::string error;
    const auto map = [&](const Oot3dNativeGame::NativeA32MemoryRegionConfig&
                             config) {
        Expect(fixture.Process.MapRegion(config, &error),
               "map " + config.Name + ": " + error);
    };
    map({"owner_text", kTextBase, text.size(), false, true, text});
    map({"owner_data", kDataBase, kDataSize, true, false, {}});
    map({"owner_ram", kRam, kRamSize, true, false, {}});
    Expect(fixture.Process.CreatePrimaryThread(
               {kOwnerEntry, kStack, 0x8000U, kTls, 0x1000U, 0U,
                0U, 0x10U, 0x03C00010U},
               &error),
           "create owner test thread: " + error);

    auto& memory = fixture.Process.Memory();
    Expect(
        memory.Write32(
            kRendererGuard, initializedGuard ? 1U : 0U) &&
            memory.Write32(
                kRendererState + 0x17CU, kRendererFactory) &&
            memory.Write32(kRendererFactory, kRendererFactoryVtable) &&
            memory.Write32(
                kRendererFactoryVtable + 8U,
                kRendererFactoryCreate) &&
            memory.Write32(kAllocatorGlobal, kAllocator) &&
            memory.Write32(kAllocator, kAllocatorVtable) &&
            memory.Write32(
                kAllocatorVtable + 0xCU, kAllocatorAllocate) &&
            memory.Write16(kPlay + 0x104U, 0U) &&
            memory.Write32(kSaveContext + 0x0E98U, 1U) &&
            memory.Write32(kSaveContext + 0x0E7CU, 10U) &&
            memory.Write32(kSaveContext + 0x0E80U, 20U) &&
            memory.Write32(kSaveContext + 0x0E84U, 30U) &&
            memory.Write32(kSaveContext + 0x0E88U, 40U) &&
            memory.Write32(kSaveContext + 0x0E8CU, 50U) &&
            memory.Write32(kSaveContext + 0x0E90U, 60U) &&
            memory.Write32(kSaveContext + 0x0E94U, 70U) &&
            memory.Write32(kSaveContext + 0x0E9CU, 80U) &&
            memory.Write32(kSaveContext + 0x0EA0U, 90U) &&
            memory.Write32(kPlay + 0x3A64U, 1U) &&
            memory.Write32(kPlay + 0x01B8U, 0x11111111U) &&
            memory.Write32(kPlay + 0x01BCU, 0x22222222U) &&
            memory.Write32(kPlay + 0x01C0U, 0x33333333U) &&
            memory.Write32(kAttentionState + 4U, 0x3F000000U) &&
            memory.Write32(kAttentionState + 0x18U, 0x44U) &&
            memory.Write32(kAttentionState + 0x30U, 0x3F800000U) &&
            memory.Write32(0x005C05B8U + 0x47CU, 0x00573000U),
        "seed Actor_InitContext globals");
    for (uint32_t index = 0U; index < 8U; ++index) {
        Expect(memory.Write8(
                   kAttentionTable + index,
                   static_cast<uint8_t>(index + 1U)),
               "seed attention table");
    }
    constexpr std::array<int16_t, 8> spawnEntry{
        7, 1, 2, 3, 4, 5, 6, 0x1234};
    Expect(memory.WriteBytes(
               kPlayerSpawnEntry,
               std::span<const uint8_t>(
                   reinterpret_cast<const uint8_t*>(spawnEntry.data()),
                   sizeof(spawnEntry))),
           "seed player spawn entry");

    std::vector<uint32_t> callbacks(
        kNativeFunctionPcs.begin(), kNativeFunctionPcs.end());
    callbacks.push_back(kAllocatorAllocate);
    std::sort(callbacks.begin(), callbacks.end());
    fixture.Process.SetNativeFunctionCallback(
        &HandleOwnerNativeFunction, &fixture.Probe,
        std::move(callbacks));
}

void CompareRange(
    const Oot3dNativeGame::NativeA32Memory& reference,
    const Oot3dNativeGame::NativeA32Memory& source,
    uint32_t address, size_t size, const char* name) {
    std::vector<uint8_t> referenceBytes(size);
    std::vector<uint8_t> sourceBytes(size);
    Expect(reference.ReadBytes(address, referenceBytes) &&
               source.ReadBytes(address, sourceBytes),
           std::string("read differential range ") + name);
    if (referenceBytes != sourceBytes) {
        size_t offset = 0U;
        while (offset < size &&
               referenceBytes[offset] == sourceBytes[offset]) {
            ++offset;
        }
        throw std::runtime_error(
            std::string(name) + " differs at guest address " +
            std::to_string(address + static_cast<uint32_t>(offset)) +
            " reference=" +
            std::to_string(referenceBytes[offset]) +
            " source=" + std::to_string(sourceBytes[offset]));
    }
}

void CompareOwnerMemory(
    const ProcessFixture& reference, const ProcessFixture& source) {
    CompareRange(
        reference.Process.Memory(), source.Process.Memory(),
        kDataBase, kDataSize, "owner data");
    CompareRange(
        reference.Process.Memory(), source.Process.Memory(),
        kRam, kRamSize, "owner ram");
}

void RunReferenceOwner(ProcessFixture& fixture) {
    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlay;
    state.r[1] = kActorContext;
    state.r[2] = kPlayerSpawnEntry;
    std::string error;
    Expect(fixture.Process.InvokeFunctionWithState(
               kOwnerEntry, state, kOwnerReturn, &error),
           "original Actor_InitContext failed: " + error);
}

Oot3dNativeGame::SourceActorInitContextStats
RunSourceOwner(ProcessFixture& fixture) {
    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlay;
    state.r[1] = kActorContext;
    state.r[2] = kPlayerSpawnEntry;
    state.r[14] = kOwnerReturn;
    oot3d::recomp::a32::ExecutionResult result;
    uint32_t blocksConsumed = 0U;
    fixture.SourceRuntime.ResetStats();
    Expect(fixture.SourceRuntime.Execute(
               kOwnerEntry, state, fixture.Process.Memory(),
               &result, &blocksConsumed),
           "source Actor_InitContext was not selected");
    Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == kOwnerReturn &&
               state.r[15] == kOwnerReturn &&
               blocksConsumed == 1U,
           "source Actor_InitContext did not return through guest LR: " +
               fixture.SourceRuntime.LastError());
    return fixture.SourceRuntime.Stats();
}

void ValidateProbe(const OwnerProbe& probe, const char* lane) {
    Expect(
        probe.ActorSpawnCalls == 1U &&
            probe.FactoryCalls == 29U &&
            probe.MaterialCalls == 4U &&
            probe.LightCalls == 1U &&
            probe.AllocatorCalls == 1U &&
            probe.ActorSpawnAbiMatched &&
            probe.LightAbiMatched &&
            probe.AllocatorAbiMatched &&
            probe.FinalizeModelAbiMatched &&
            probe.CreateMaterialAbiMatched &&
            probe.Error.empty(),
        std::string(lane) +
            " Actor_InitContext dependency ABI differs");
}

void RoundTripFinalState(ProcessFixture& fixture) {
    const nlohmann::json checkpoint = fixture.Process.CaptureState();
    const uint64_t expectedFingerprint =
        fixture.Process.Memory().ContentFingerprint();
    Expect(
        fixture.Process.Memory().Write32(
            kActorContext, 0xDEADBEEFU),
        "mutate final Actor_InitContext checkpoint");
    std::string error;
    Expect(fixture.Process.RestoreState(checkpoint, &error),
           "restore final Actor_InitContext checkpoint: " + error);
    Expect(
        fixture.Process.Memory().ContentFingerprint() ==
            expectedFingerprint,
        "final Actor_InitContext checkpoint did not round-trip");
}

void TestActorInitContextDifferential(bool initializedGuard) {
    ProcessFixture reference;
    ProcessFixture source;
    ConfigureProcess(reference, initializedGuard);
    ConfigureProcess(source, initializedGuard);
    const nlohmann::json referenceBefore =
        reference.Process.CaptureState();
    const nlohmann::json sourceBefore = source.Process.CaptureState();

    RunReferenceOwner(reference);
    const auto sourceStats = RunSourceOwner(source);
    Expect(source.SourceRuntime.LastError().empty(),
           "source Actor_InitContext bridge failed: " +
               source.SourceRuntime.LastError());
    CompareOwnerMemory(reference, source);
    ValidateProbe(reference.Probe, "reference");
    ValidateProbe(source.Probe, "source");
    Expect(
        sourceStats.OwnerCalls == 1U &&
            sourceStats.ActorSpawnCalls == 1U &&
            sourceStats.DynamicFactoryCalls == 29U &&
            sourceStats.DynamicAllocatorCalls == 1U &&
            sourceStats.LightSetupCalls == 1U &&
            sourceStats.TailHelperCalls == 1U &&
            sourceStats.Failures == 0U,
        "source Actor_InitContext statistics differ");
    Expect(
        reference.Probe.RendererInitCalls ==
                (initializedGuard ? 0U : 1U) &&
            source.Probe.RendererInitCalls ==
                reference.Probe.RendererInitCalls,
        "source and original renderer guard behavior differs");
    RoundTripFinalState(reference);
    RoundTripFinalState(source);
    CompareOwnerMemory(reference, source);

    std::string error;
    Expect(reference.Process.RestoreState(referenceBefore, &error),
           "restore reference pre-spawn checkpoint: " + error);
    Expect(source.Process.RestoreState(sourceBefore, &error),
           "restore source pre-spawn checkpoint: " + error);
    reference.Probe.Reset();
    source.Probe.Reset();
    RunReferenceOwner(reference);
    RunSourceOwner(source);
    CompareOwnerMemory(reference, source);
    ValidateProbe(reference.Probe, "restored reference");
    ValidateProbe(source.Probe, "restored source");
}

} // namespace

int main() {
    try {
        TestActorInitContextDifferential(true);
        TestActorInitContextDifferential(false);
        std::cout << "oot3d_source_actor_init_context_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_source_actor_init_context_tests: "
                  << exception.what() << '\n';
        return 1;
    }
}
