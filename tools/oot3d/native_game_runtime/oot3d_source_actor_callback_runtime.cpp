#include "oot3d_source_actor_callback_runtime.h"

#include "oot3d/obj_hana_init_owner.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kObjHanaFrameSize = 0x18U;
constexpr uint32_t kActorProcessInitChain = 0x003510B0U;
constexpr uint32_t kActorSetScale = 0x0037572CU;
constexpr uint32_t kColliderInitCylinder = 0x00353DD0U;
constexpr uint32_t kColliderSetCylinder = 0x00353D24U;
constexpr uint32_t kColliderUpdateCylinder = 0x0037632CU;
constexpr uint32_t kCollisionCheckSetInfo = 0x00350D20U;
constexpr uint32_t kActorKill = 0x00374428U;
constexpr uint32_t kZarGetCmbByIndex = 0x00358EF8U;
constexpr uint32_t kStaticInitGuardAcquire = 0x003679B4U;
constexpr uint32_t kRendererGlobalStateInit = 0x0036788CU;

struct ActiveSourceActorCallback {
    SourceActorCallbackHost* Host = nullptr;
    SourceActorCallbackInvocation Invocation;
    SourceActorCallbackStats* Stats = nullptr;
    std::string* Error = nullptr;
    Oot3dSourceObjHanaInit::ObjHanaInitLiterals ObjHanaLiterals;
};

thread_local ActiveSourceActorCallback* gActiveCallback = nullptr;

class ActiveCallbackScope {
  public:
    explicit ActiveCallbackScope(
        ActiveSourceActorCallback& callback) noexcept
        : mPrevious(gActiveCallback) {
        gActiveCallback = &callback;
    }

    ActiveCallbackScope(const ActiveCallbackScope&) = delete;
    ActiveCallbackScope& operator=(const ActiveCallbackScope&) = delete;

    ~ActiveCallbackScope() {
        gActiveCallback = mPrevious;
    }

  private:
    ActiveSourceActorCallback* mPrevious = nullptr;
};

class SourceActorCallbackAbort final : public std::runtime_error {
  public:
    explicit SourceActorCallbackAbort(const std::string& message)
        : std::runtime_error(message) {
    }
};

ActiveSourceActorCallback& ActiveCallback() {
    if (gActiveCallback == nullptr) {
        throw SourceActorCallbackAbort(
            "source actor callback bridge is not active");
    }
    return *gActiveCallback;
}

[[noreturn]] void Abort(std::string message) {
    auto& callback = ActiveCallback();
    if (callback.Error != nullptr && callback.Error->empty()) {
        *callback.Error = message;
    }
    throw SourceActorCallbackAbort(message);
}

uint32_t CallbackFrameSize() {
    const auto& invocation = ActiveCallback().Invocation;
    if (invocation.ParentFrameSize >
        std::numeric_limits<uint32_t>::max() - kObjHanaFrameSize) {
        Abort("source actor callback frame size overflow");
    }
    const uint32_t frameSize =
        invocation.ParentFrameSize + kObjHanaFrameSize;
    if ((frameSize & 7U) != 0U) {
        Abort("source actor callback frame is not AAPCS aligned");
    }
    return frameSize;
}

uint32_t InvokeGuest(
    uint32_t entryAddress, std::span<const uint32_t> core = {},
    std::span<const uint32_t> vfp = {},
    std::span<const uint32_t> stack = {}) {
    auto& callback = ActiveCallback();
    uint32_t result = 0U;
    std::string error;
    if (!callback.Host->InvokeGuest(
            entryAddress, core, vfp, stack, CallbackFrameSize(),
            &result, &error)) {
        Abort(
            "source actor callback guest call " +
            std::to_string(entryAddress) + " failed: " + error);
    }
    ++callback.Stats->NestedGuestCalls;
    return result;
}

bool LoadLiteral(
    SourceActorCallbackHost& host, uint32_t cellAddress,
    uint32_t* value, std::string* error) {
    std::string resolveError;
    const auto* pointer =
        host.ResolveGuestRead(cellAddress, sizeof(*value), &resolveError);
    if (pointer == nullptr) {
        if (error != nullptr) {
            *error =
                "source actor callback literal read at " +
                std::to_string(cellAddress) +
                " failed: " + resolveError;
        }
        return false;
    }
    std::memcpy(value, pointer, sizeof(*value));
    return true;
}

} // namespace

bool SourceActorCallbackRuntime::CanDispatch(
    uint32_t entryAddress) const noexcept {
    return entryAddress == kSourceObjHanaInitCallbackEntry;
}

SourceActorCallbackDispatchResult SourceActorCallbackRuntime::Dispatch(
    const SourceActorCallbackInvocation& invocation,
    SourceActorCallbackHost& host) {
    if (!CanDispatch(invocation.EntryAddress)) {
        return SourceActorCallbackDispatchResult::NotHandled;
    }

    ++mStats.Dispatches;
    mLastError.clear();
    if (invocation.ActorAddress == 0U ||
        invocation.PlayAddress == 0U) {
        ++mStats.Failures;
        mLastError = "source actor callback received a null argument";
        return SourceActorCallbackDispatchResult::Failed;
    }

    ActiveSourceActorCallback active{
        &host, invocation, &mStats, &mLastError, {}};
    if (!LoadLiteral(
            host, 0x001E1818U,
            &active.ObjHanaLiterals.ModelRecords, &mLastError) ||
        !LoadLiteral(
            host, 0x001E181CU,
            &active.ObjHanaLiterals.InitChain, &mLastError) ||
        !LoadLiteral(
            host, 0x001E1820U,
            &active.ObjHanaLiterals.CylinderInit, &mLastError) ||
        !LoadLiteral(
            host, 0x001E1824U,
            &active.ObjHanaLiterals.CollisionInfoInit, &mLastError) ||
        !LoadLiteral(
            host, 0x001E1828U,
            &active.ObjHanaLiterals.SaveContext, &mLastError) ||
        !LoadLiteral(
            host, 0x0037305CU,
            &active.ObjHanaLiterals.ObjectReadyOffset, &mLastError) ||
        !LoadLiteral(
            host, 0x00373060U,
            &active.ObjHanaLiterals.RendererInitGuard, &mLastError) ||
        !LoadLiteral(
            host, 0x00373064U,
            &active.ObjHanaLiterals.RendererGlobalState, &mLastError) ||
        !LoadLiteral(
            host, 0x00373070U,
            &active.ObjHanaLiterals.ModelLoadStats, &mLastError)) {
        ++mStats.Failures;
        return SourceActorCallbackDispatchResult::Failed;
    }
    ActiveCallbackScope scope(active);
    try {
        switch (invocation.EntryAddress) {
        case kSourceObjHanaInitCallbackEntry:
            Oot3dSourceObjHanaInit::ObjHana_Init(
                invocation.ActorAddress, invocation.PlayAddress);
            ++mStats.ObjHanaInitCalls;
            break;
        default:
            return SourceActorCallbackDispatchResult::NotHandled;
        }
    } catch (const std::exception& exception) {
        if (mLastError.empty()) {
            mLastError = exception.what();
        }
        ++mStats.Failures;
        return SourceActorCallbackDispatchResult::Failed;
    } catch (...) {
        if (mLastError.empty()) {
            mLastError = "source actor callback raised an unknown exception";
        }
        ++mStats.Failures;
        return SourceActorCallbackDispatchResult::Failed;
    }
    return SourceActorCallbackDispatchResult::Completed;
}

SourceActorCallbackStats
SourceActorCallbackRuntime::Stats() const noexcept {
    return mStats;
}

void SourceActorCallbackRuntime::ResetStats() noexcept {
    mStats = {};
}

const std::string& SourceActorCallbackRuntime::LastError() const noexcept {
    return mLastError;
}

} // namespace Oot3dNativeGame

namespace Oot3dSourceObjHanaInit {

const ObjHanaInitLiterals& ActiveLiterals() {
    return Oot3dNativeGame::ActiveCallback().ObjHanaLiterals;
}

const void* ResolveGuestRead(uint32_t address, size_t size) {
    auto& callback = Oot3dNativeGame::ActiveCallback();
    std::string error;
    const auto* pointer =
        callback.Host->ResolveGuestRead(address, size, &error);
    if (pointer == nullptr) {
        Oot3dNativeGame::Abort(
            "source actor callback read at " +
            std::to_string(address) + " failed: " + error);
    }
    return pointer;
}

void* ResolveGuestWrite(uint32_t address, size_t size) {
    auto& callback = Oot3dNativeGame::ActiveCallback();
    std::string error;
    auto* pointer =
        callback.Host->ResolveGuestWrite(address, size, &error);
    if (pointer == nullptr) {
        Oot3dNativeGame::Abort(
            "source actor callback write at " +
            std::to_string(address) + " failed: " + error);
    }
    return pointer;
}

[[noreturn]] void FailSourceCallback(const char* message) {
    Oot3dNativeGame::Abort(message);
}

int32_t StaticInitGuardAcquire(uint32_t guardAddress) {
    const std::array<uint32_t, 1> core{guardAddress};
    return static_cast<int32_t>(Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kStaticInitGuardAcquire, core));
}

void RendererGlobalState_Init(uint32_t rendererStateAddress) {
    const std::array<uint32_t, 1> core{rendererStateAddress};
    Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kRendererGlobalStateInit, core);
}

uint32_t ZAR_GetCMBByIndex(
    uint32_t archiveAddress, uint32_t modelIndex) {
    const std::array<uint32_t, 2> core{
        archiveAddress, modelIndex};
    return Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kZarGetCmbByIndex, core);
}

uint32_t RendererFactoryCreate(
    uint32_t factoryAddress, uint32_t resourceAddress,
    uint32_t flags) {
    const auto* factory = static_cast<const uint8_t*>(
        ResolveGuestRead(factoryAddress, sizeof(uint32_t)));
    uint32_t vtableAddress = 0U;
    std::memcpy(&vtableAddress, factory, sizeof(vtableAddress));
    const auto* vtable = static_cast<const uint8_t*>(
        ResolveGuestRead(vtableAddress + 8U, sizeof(uint32_t)));
    uint32_t entryAddress = 0U;
    std::memcpy(&entryAddress, vtable, sizeof(entryAddress));
    const std::array<uint32_t, 3> core{
        factoryAddress, resourceAddress, flags};
    return Oot3dNativeGame::InvokeGuest(entryAddress, core);
}

void Actor_ProcessInitChain(
    uint32_t actorAddress, uint32_t initChainAddress) {
    const std::array<uint32_t, 2> core{
        actorAddress, initChainAddress};
    Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kActorProcessInitChain, core);
}

void Actor_SetScale(uint32_t actorAddress, float scale) {
    const std::array<uint32_t, 1> core{actorAddress};
    const std::array<uint32_t, 1> vfp{
        std::bit_cast<uint32_t>(scale)};
    Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kActorSetScale, core, vfp);
}

void Collider_InitCylinder(
    uint32_t playAddress, uint32_t colliderAddress) {
    const std::array<uint32_t, 2> core{
        playAddress, colliderAddress};
    Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kColliderInitCylinder, core);
}

void Collider_SetCylinder(
    uint32_t playAddress, uint32_t colliderAddress,
    uint32_t actorAddress, uint32_t initAddress) {
    const std::array<uint32_t, 4> core{
        playAddress, colliderAddress, actorAddress, initAddress};
    Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kColliderSetCylinder, core);
}

void Collider_UpdateCylinder(
    uint32_t actorAddress, uint32_t colliderAddress) {
    const std::array<uint32_t, 2> core{
        actorAddress, colliderAddress};
    Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kColliderUpdateCylinder, core);
}

void CollisionCheck_SetInfo(
    uint32_t collisionInfoAddress, uint32_t damageTableAddress,
    uint32_t initAddress) {
    const std::array<uint32_t, 3> core{
        collisionInfoAddress, damageTableAddress, initAddress};
    Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kCollisionCheckSetInfo, core);
}

void Actor_Kill(uint32_t actorAddress) {
    const std::array<uint32_t, 1> core{actorAddress};
    Oot3dNativeGame::InvokeGuest(
        Oot3dNativeGame::kActorKill, core);
}

} // namespace Oot3dSourceObjHanaInit
