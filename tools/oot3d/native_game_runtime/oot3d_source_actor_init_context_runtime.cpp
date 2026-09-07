#include "oot3d_source_actor_init_context_runtime.h"

#include "oot3d_native_a32_process.h"
#include "oot3d_native_owner_call_adapter.h"

#include "oot3d/actor_init_context_owner.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kGuestServiceReturn = 0x0BAD1000U;
constexpr uint32_t kOwnerCallFrameSize = 0x80U;
constexpr NativeA32OwnerGuestCallLimits kBulkInitializationCallLimits{
    1'000'000U,
    65'536U,
};

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

thread_local SourceActorInitContextBridge* gActiveBridge = nullptr;

class ActiveBridgeScope {
  public:
    explicit ActiveBridgeScope(
        SourceActorInitContextBridge& bridge) noexcept
        : mPrevious(gActiveBridge) {
        gActiveBridge = &bridge;
    }

    ActiveBridgeScope(const ActiveBridgeScope&) = delete;
    ActiveBridgeScope& operator=(const ActiveBridgeScope&) = delete;

    ~ActiveBridgeScope() {
        gActiveBridge = mPrevious;
    }

  private:
    SourceActorInitContextBridge* mPrevious = nullptr;
};

} // namespace

class SourceActorInitContextBridge {
  public:
    explicit SourceActorInitContextBridge(NativeA32Process& process)
        : Process(process), Adapter(process) {
    }

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed) {
        if (pc != kSourceActorInitContextEntry) {
            return false;
        }
        if (result == nullptr || &memory != &Process.Memory()) {
            return false;
        }
        const uint32_t playAddress = state.r[0];
        const uint32_t actorContextAddress = state.r[1];
        const uint32_t playerSpawnEntryAddress = state.r[2];
        if ((state.r[13] & 7U) != 0U ||
            !Process.Memory().IsWritable(playAddress, 0x5C78U) ||
            !Process.Memory().IsWritable(actorContextAddress, 0x20CU) ||
            !Process.Memory().IsMapped(
                playerSpawnEntryAddress, 8U * sizeof(uint16_t))) {
            return false;
        }

        ++RuntimeStats.OwnerCalls;
        Failed = false;
        Error.clear();
        CallerState = state;
        Literals = {};

        std::string adapterError;
        auto execution = Adapter.BeginExecution(
            kSourceActorInitContextEntry, state, &adapterError);
        if (!execution) {
            Fail("begin source owner: " + adapterError);
        }
        if (!Failed && !LoadLiterals()) {
            Fail("load Actor_InitContext target literals");
        }

        if (!Failed) {
            const auto* spawnEntry = reinterpret_cast<const int16_t*>(
                Adapter.ResolveRead(
                    playerSpawnEntryAddress, 8U * sizeof(int16_t)));
            if (spawnEntry == nullptr) {
                Fail("resolve Actor_InitContext spawn entry");
            } else {
                ActiveBridgeScope bridgeScope(*this);
                try {
                    Oot3dSourceActorInitContext::Actor_InitContext(
                        playAddress, actorContextAddress, spawnEntry);
                    ++RuntimeStats.TailHelperCalls;
                } catch (const std::exception& exception) {
                    Fail(std::string("source owner exception: ") +
                         exception.what());
                } catch (...) {
                    Fail("source owner unknown exception");
                }
            }
        }
        execution.Reset();

        if (Failed) {
            ++RuntimeStats.Failures;
            *result = {
                oot3d::recomp::a32::ExitKind::Unsupported,
                pc,
                oot3d::recomp::a32::FallbackReason::Unsupported,
                kSourceActorInitContextEntry,
            };
            if (blocksConsumed != nullptr) {
                *blocksConsumed = 1U;
            }
            return true;
        }

        state.r[15] = state.r[14];
        *result = {
            oot3d::recomp::a32::ExitKind::Branch,
            state.r[15],
            oot3d::recomp::a32::FallbackReason::None,
            pc,
        };
        if (blocksConsumed != nullptr) {
            *blocksConsumed = 1U;
        }
        return true;
    }

    void* Resolve(uint32_t address, size_t size) {
        if (Failed || address == 0U || size == 0U) {
            throw std::runtime_error(
                "Actor_InitContext invalid guest pointer");
        }
        auto* pointer = Adapter.ResolveWrite(address, size);
        if (pointer == nullptr) {
            Fail("Actor_InitContext guest pointer resolution failed at " +
                 std::to_string(address));
            throw std::runtime_error(Error);
        }
        return pointer;
    }

    uint32_t ReadWord(uint32_t address) {
        uint32_t value = 0U;
        if (Failed ||
            !Process.Memory().ReadFast(address, &value)) {
            Fail("Actor_InitContext guest word read failed at " +
                 std::to_string(address));
            throw std::runtime_error(Error);
        }
        return value;
    }

    uint32_t ScratchAddress(
        uint32_t frameSize, uint32_t offset, size_t size) {
        if (frameSize > CallerState.r[13] || offset > frameSize ||
            size > static_cast<size_t>(frameSize - offset)) {
            Fail("Actor_InitContext scratch frame overflow");
            throw std::runtime_error(Error);
        }
        const uint32_t address =
            CallerState.r[13] - frameSize + offset;
        if (Adapter.ResolveWrite(address, size) == nullptr) {
            Fail("Actor_InitContext scratch memory is not writable");
            throw std::runtime_error(Error);
        }
        return address;
    }

    NativeA32OwnerGuestCallResult Invoke(
        uint32_t entryAddress, std::span<const uint32_t> core = {},
        std::span<const uint32_t> vfp = {},
        std::span<const uint32_t> stack = {},
        uint32_t frameSize = kOwnerCallFrameSize,
        NativeA32OwnerGuestCallLimits limits = {}) {
        NativeA32OwnerGuestCall call;
        call.EntryAddress = entryAddress;
        call.ReturnAddress = kGuestServiceReturn;
        call.CallerFrameSize = frameSize;
        call.Limits = limits;
        call.CoreArgumentCount =
            std::min(core.size(), call.CoreArguments.size());
        call.VfpArgumentCount =
            std::min(vfp.size(), call.VfpArguments.size());
        if (call.CoreArgumentCount != core.size() ||
            call.VfpArgumentCount != vfp.size()) {
            Fail("Actor_InitContext service argument overflow");
            throw std::runtime_error(Error);
        }
        std::copy(core.begin(), core.end(), call.CoreArguments.begin());
        std::copy(vfp.begin(), vfp.end(), call.VfpArguments.begin());
        call.StackArguments = stack;

        NativeA32OwnerGuestCallResult result;
        std::string invokeError;
        if (!Adapter.Invoke(call, &result, &invokeError)) {
            Fail("Actor_InitContext guest call " +
                 std::to_string(entryAddress) +
                 " failed: " + invokeError);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.NestedGuestCalls;
        return result;
    }

    uint32_t InvokeCore(
        uint32_t entryAddress,
        std::initializer_list<uint32_t> coreArguments,
        NativeA32OwnerGuestCallLimits limits = {}) {
        const std::span<const uint32_t> core{
            coreArguments.begin(), coreArguments.size()};
        return Invoke(
            entryAddress, core, {}, {}, kOwnerCallFrameSize, limits)
            .State.r[0];
    }

    uint32_t FactoryAddress(uint32_t rendererStateAddress) {
        return ReadWord(rendererStateAddress + 0x17CU);
    }

    uint32_t FactoryCreate(
        uint32_t factoryAddress, uint32_t resourceAddress,
        uint32_t flags) {
        if (factoryAddress == 0U) {
            Fail("Actor_InitContext renderer factory is null");
            throw std::runtime_error(Error);
        }
        const uint32_t vtableAddress = ReadWord(factoryAddress);
        const uint32_t entryAddress = ReadWord(vtableAddress + 8U);
        ++RuntimeStats.DynamicFactoryCalls;
        return InvokeCore(
            entryAddress, {factoryAddress, resourceAddress, flags});
    }

    uint32_t Allocate(
        uint32_t allocatorGlobalAddress, uint32_t size,
        uint32_t sourcePathAddress, uint32_t sourceLine) {
        const uint32_t allocatorAddress =
            ReadWord(allocatorGlobalAddress);
        const uint32_t vtableAddress = ReadWord(allocatorAddress);
        const uint32_t entryAddress = ReadWord(vtableAddress + 0xCU);
        ++RuntimeStats.DynamicAllocatorCalls;
        return InvokeCore(
            entryAddress,
            {allocatorAddress, size, sourcePathAddress, sourceLine});
    }

    void Fail(std::string message) {
        if (!Failed) {
            Failed = true;
            Error = std::move(message);
        }
    }

    bool LoadLiterals() {
        return
            LoadLiteral(0x0044ECACU, Literals.DAT_0044ecac) &&
            LoadLiteral(0x0044ECB0U, Literals.DAT_0044ecb0) &&
            LoadLiteral(0x0044ECB4U, Literals.DAT_0044ecb4) &&
            LoadLiteral(0x0044ECC0U, Literals.DAT_0044ecc0) &&
            LoadLiteral(0x0044ECC4U, Literals.DAT_0044ecc4) &&
            LoadLiteral(0x0044ECC8U, Literals.DAT_0044ecc8) &&
            LoadLiteral(0x0044ECCCU, Literals.DAT_0044eccc) &&
            LoadLiteral(0x0044ECD0U, Literals.DAT_0044ecd0) &&
            LoadLiteral(0x0044ECD4U, Literals.DAT_0044ecd4) &&
            LoadLiteral(0x0044ECD8U, Literals.DAT_0044ecd8) &&
            LoadLiteral(0x0044ECDCU, Literals.DAT_0044ecdc) &&
            LoadLiteral(0x0044EFA8U, Literals.DAT_0044efa8) &&
            LoadLiteral(0x0044EFACU, Literals.DAT_0044efac) &&
            LoadLiteral(0x0044EFE8U, Literals.DAT_0044efe8) &&
            LoadLiteral(0x0044EFECU, Literals.DAT_0044efec) &&
            LoadLiteral(0x0044EFF0U, Literals.DAT_0044eff0) &&
            LoadLiteral(0x0044EFF4U, Literals.DAT_0044eff4) &&
            LoadLiteral(0x0044EFF8U, Literals.DAT_0044eff8) &&
            LoadLiteral(0x0044EFFCU, Literals.DAT_0044effc) &&
            LoadLiteral(0x0044F000U, Literals.DAT_0044f000) &&
            LoadLiteral(0x0044F004U, Literals.DAT_0044f004) &&
            LoadLiteral(0x0044F008U, Literals.DAT_0044f008) &&
            LoadLiteral(0x00463420U, Literals.DAT_00463420) &&
            LoadLiteral(0x00463424U, Literals.DAT_00463424) &&
            LoadLiteral(0x00463428U, Literals.DAT_00463428) &&
            LoadLiteral(0x00463434U, Literals.DAT_00463434) &&
            LoadLiteral(0x00463438U, Literals.DAT_00463438) &&
            LoadLiteral(0x0046343CU, Literals.DAT_0046343c) &&
            LoadLiteral(0x00463440U, Literals.DAT_00463440) &&
            LoadLiteral(0x00463444U, Literals.DAT_00463444) &&
            LoadLiteral(0x00463448U, Literals.DAT_00463448);
    }

    bool LoadLiteral(uint32_t cellAddress, uint32_t& value) {
        return Process.Memory().ReadFast(cellAddress, &value);
    }

    NativeA32Process& Process;
    NativeA32OwnerCallAdapter Adapter;
    Oot3dSourceActorInitContext::ActorInitContextLiterals Literals;
    SourceActorInitContextStats RuntimeStats;
    oot3d::recomp::a32::GuestState CallerState{};
    bool Failed = false;
    std::string Error;
};

namespace {

SourceActorInitContextBridge& ActiveBridge() {
    if (gActiveBridge == nullptr) {
        throw std::runtime_error(
            "Actor_InitContext source bridge is not active");
    }
    return *gActiveBridge;
}

} // namespace

SourceActorInitContextRuntime::SourceActorInitContextRuntime(
    NativeA32Process& process)
    : mBridge(std::make_unique<SourceActorInitContextBridge>(process)) {
}

SourceActorInitContextRuntime::~SourceActorInitContextRuntime() = default;

bool SourceActorInitContextRuntime::Execute(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t* blocksConsumed) {
    return mBridge->Execute(
        pc, state, memory, result, blocksConsumed);
}

SourceActorInitContextStats
SourceActorInitContextRuntime::Stats() const noexcept {
    return mBridge->RuntimeStats;
}

void SourceActorInitContextRuntime::ResetStats() noexcept {
    mBridge->RuntimeStats = {};
}

const std::string&
SourceActorInitContextRuntime::LastError() const noexcept {
    return mBridge->Error;
}

} // namespace Oot3dNativeGame

namespace Oot3dSourceActorInitContext {

const ActorInitContextLiterals& ActiveLiterals() {
    return Oot3dNativeGame::ActiveBridge().Literals;
}

void* ResolveGuestMemory(uint32_t address, size_t size) {
    return Oot3dNativeGame::ActiveBridge().Resolve(address, size);
}

uint32_t Actor_Spawn(
    uint32_t actorContext, uint32_t play, int32_t actorId,
    float x, float y, float z, int32_t rotX, int32_t rotY,
    int32_t rotZ, int32_t params, int32_t initializeNow) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    const std::array<uint32_t, 4> core{
        actorContext, play, static_cast<uint32_t>(actorId),
        static_cast<uint32_t>(rotX)};
    const std::array<uint32_t, 3> vfp{
        std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(y),
        std::bit_cast<uint32_t>(z)};
    const std::array<uint32_t, 4> stack{
        static_cast<uint32_t>(rotY), static_cast<uint32_t>(rotZ),
        static_cast<uint32_t>(params),
        static_cast<uint32_t>(initializeNow)};
    ++bridge.RuntimeStats.ActorSpawnCalls;
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.Invoke(
        Oot3dNativeGame::kActorSpawn, core, vfp, stack).State.r[0];
}

void FUN_004644a8(uint32_t value, uint32_t play) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    // The target routine has bounded 0x50-model and 0x40-material loops.
    // Each factory operation can re-enter the native dispatcher, so it needs
    // a bulk-call safety budget without weakening other owner calls.
    bridge.InvokeCore(
        Oot3dNativeGame::kInitializeActorRuntime, {value, play},
        Oot3dNativeGame::kBulkInitializationCallLimits);
}

uint32_t oot3d_memclear(uint32_t address, uint32_t size) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.InvokeCore(
        Oot3dNativeGame::kMemclear, {address, size});
}

void FUN_0045fe94() {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    bridge.InvokeCore(
        Oot3dNativeGame::kInitializeActorContextGlobals, {});
}

uint32_t Object_GetIndex(uint32_t objectContext, uint32_t objectId) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.InvokeCore(
        Oot3dNativeGame::kObjectGetIndex,
        {objectContext, objectId});
}

uint32_t ZAR_GetCMBByIndex(uint32_t archive, uint32_t index) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.InvokeCore(
        Oot3dNativeGame::kZarGetCmbByIndex, {archive, index});
}

uint32_t ZAR_GetCTXBByIndex(uint32_t archive, uint32_t index) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.InvokeCore(
        Oot3dNativeGame::kZarGetCtxbByIndex, {archive, index});
}

int32_t oot3d_static_init_guard_acquire(uint32_t guardAddress) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return static_cast<int32_t>(bridge.InvokeCore(
        Oot3dNativeGame::kStaticInitGuardAcquire, {guardAddress}));
}

void RendererGlobalState_Init(uint32_t rendererStateAddress) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    bridge.InvokeCore(
        Oot3dNativeGame::kRendererGlobalStateInit,
        {rendererStateAddress});
}

uint32_t RendererFactoryAddress(uint32_t rendererStateAddress) {
    return Oot3dNativeGame::ActiveBridge().FactoryAddress(
        rendererStateAddress);
}

uint32_t RendererFactoryCreate(
    uint32_t factoryAddress, uint32_t resourceAddress, uint32_t flags) {
    return Oot3dNativeGame::ActiveBridge().FactoryCreate(
        factoryAddress, resourceAddress, flags);
}

void oot3d_store_child14_byte16(
    uint32_t objectAddress, uint8_t value) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    bridge.InvokeCore(
        Oot3dNativeGame::kStoreChild14Byte16,
        {objectAddress, value});
}

void Lights_PointNoGlowSetInfo(
    uint32_t infoAddress, float x, float y, float z,
    uint32_t red, uint32_t green, uint32_t blue,
    int32_t radius, uint32_t attenuation) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    const std::array<uint32_t, 4> core{
        infoAddress, red, green, blue};
    const std::array<uint32_t, 3> vfp{
        std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(y),
        std::bit_cast<uint32_t>(z)};
    const std::array<uint32_t, 2> stack{
        static_cast<uint32_t>(radius), attenuation};
    ++bridge.RuntimeStats.LightSetupCalls;
    ++bridge.RuntimeStats.DirectDependencyCalls;
    bridge.Invoke(
        Oot3dNativeGame::kLightsPointNoGlowSetInfo,
        core, vfp, stack);
}

uint32_t LightContext_InsertLight(
    uint32_t play, uint32_t lightContext, uint32_t infoAddress) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.InvokeCore(
        Oot3dNativeGame::kLightContextInsertLight,
        {play, lightContext, infoAddress});
}

uint32_t AllocatorAllocate(
    uint32_t allocatorGlobalAddress, uint32_t size,
    uint32_t sourcePathAddress, uint32_t sourceLine) {
    return Oot3dNativeGame::ActiveBridge().Allocate(
        allocatorGlobalAddress, size, sourcePathAddress, sourceLine);
}

uint32_t FUN_00348f34(
    uint32_t objectAddress, uint32_t profileAddress) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.InvokeCore(
        Oot3dNativeGame::kInitializeModelStorage,
        {objectAddress, profileAddress});
}

uint32_t FUN_00348be4(uint32_t objectAddress) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.InvokeCore(
        Oot3dNativeGame::kFinalizeModelStorage, {objectAddress});
}

void FUN_00348a64(
    uint32_t layoutAddress, int32_t streamIndex, uint32_t sourceAddress,
    uint32_t semantic, uint32_t componentType, uint32_t inputType,
    uint32_t outputType) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    const std::array<uint32_t, 4> core{
        layoutAddress, static_cast<uint32_t>(streamIndex),
        sourceAddress, semantic};
    const std::array<uint32_t, 3> stack{
        componentType, inputType, outputType};
    ++bridge.RuntimeStats.DirectDependencyCalls;
    bridge.Invoke(
        Oot3dNativeGame::kInitializeVertexLayout, core, {}, stack);
}

uint32_t FUN_0034897c(
    uint32_t rootAddress, uint32_t textureAddress,
    uint32_t modelContextAddress, uint32_t transferStateAddress) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.InvokeCore(
        Oot3dNativeGame::kCreateMaterial,
        {rootAddress, textureAddress, modelContextAddress,
         transferStateAddress});
}

void FUN_003429c8(
    uint32_t materialAddress, uint32_t enabled,
    const uint32_t parameters[4]) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    if (parameters == nullptr) {
        bridge.Fail("Actor_InitContext material vector is null");
        throw std::runtime_error(bridge.Error);
    }
    constexpr uint32_t vectorSize = 4U * sizeof(uint32_t);
    const uint32_t scratch = bridge.ScratchAddress(
        Oot3dNativeGame::kOwnerCallFrameSize, 0U, vectorSize);
    for (uint32_t index = 0U; index < 4U; ++index) {
        if (!bridge.Process.Memory().WriteFast(
                scratch + index * sizeof(uint32_t), parameters[index])) {
            bridge.Fail("write Actor_InitContext material vector");
            throw std::runtime_error(bridge.Error);
        }
    }
    const std::array<uint32_t, 3> core{
        materialAddress, enabled, scratch};
    ++bridge.RuntimeStats.DirectDependencyCalls;
    bridge.Invoke(
        Oot3dNativeGame::kConfigureMaterial, core, {}, {},
        Oot3dNativeGame::kOwnerCallFrameSize);
}

uint32_t oot3d_get_indexed_field_58_entry(
    uint32_t archive, uint32_t index) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    return bridge.InvokeCore(
        Oot3dNativeGame::kGetIndexedField58Entry, {archive, index});
}

void FUN_00372d94(
    uint32_t objectAddress, uint32_t resourceAddress) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.DirectDependencyCalls;
    bridge.InvokeCore(
        Oot3dNativeGame::kBindModelResource,
        {objectAddress, resourceAddress});
}

} // namespace Oot3dSourceActorInitContext
