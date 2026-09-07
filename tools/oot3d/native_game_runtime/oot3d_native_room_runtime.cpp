#include "oot3d_native_room_runtime.h"

#include <stdexcept>

namespace Oot3dNativeGame {

NativeRoomRuntime::NativeRoomRuntime(const Oot3d::RoomCompilationUnit& unit)
    : mUnit(unit), mContract(unit.roomLifecycle), mCurrentRoom(unit.initialRoomIndex),
      mLoadState(unit.roomLifecycle.idleState) {
    if (!mContract.available || mUnit.FindRoom(mCurrentRoom) == nullptr) {
        throw std::runtime_error("native room runtime requires a complete lifecycle contract");
    }
}

bool NativeRoomRuntime::RequestRoom(int32_t roomIndex, int32_t* droppedPreviousRoom) {
    if (mLoadState != mContract.idleState || mUnit.FindRoom(roomIndex) == nullptr) {
        return false;
    }
    if (droppedPreviousRoom != nullptr) {
        *droppedPreviousRoom = mPreviousRoom;
    }
    if (mPreviousRoom >= 0) {
        mResourceCleanupPending = true;
        mCleanupTicksRemaining = mContract.cleanupDelayTicks;
    }
    mPreviousRoom = mCurrentRoom;
    mCurrentRoom = roomIndex;
    mPendingRoom = roomIndex;
    mLoadState = mContract.loadingState;
    ++mRequestCount;
    return true;
}

bool NativeRoomRuntime::CompleteRoomRequest() {
    if (mLoadState != mContract.loadingState || mPendingRoom != mCurrentRoom) {
        return false;
    }
    mPendingRoom = -1;
    mLoadState = mContract.idleState;
    ++mCompletionCount;
    return true;
}

bool NativeRoomRuntime::AdvanceResourceCleanup() {
    if (!mResourceCleanupPending) {
        return false;
    }
    if (mCleanupTicksRemaining != 0) {
        --mCleanupTicksRemaining;
        return false;
    }
    mResourceCleanupPending = false;
    ++mResourceCleanupCompletionCount;
    return true;
}

bool NativeRoomRuntime::MatchesResidentRoom(int32_t roomIndex) const {
    return roomIndex >= 0 && (roomIndex == mCurrentRoom || roomIndex == mPreviousRoom);
}

bool NativeRoomRuntime::IsRoomResident(int32_t roomIndex) const {
    return MatchesResidentRoom(roomIndex);
}

bool NativeRoomRuntime::IsRoomLoaded(int32_t roomIndex) const {
    if (roomIndex == mPreviousRoom && roomIndex >= 0) {
        return true;
    }
    return roomIndex == mCurrentRoom && mLoadState == mContract.idleState;
}

bool NativeRoomRuntime::ShouldRetainRoomActor(int32_t roomIndex) const {
    return roomIndex < 0 || MatchesResidentRoom(roomIndex);
}

bool NativeRoomRuntime::ShouldSpawnTransitionActor(
    const Oot3d::RoomCompilationActorEntry& actor) const {
    if (!actor.transition) {
        return false;
    }
    return MatchesResidentRoom(actor.frontRoomIndex) ||
           MatchesResidentRoom(actor.backRoomIndex);
}

int16_t NativeRoomRuntime::TransitionSpawnActorId(
    const Oot3d::RoomCompilationActorEntry& actor) const {
    if (!actor.transition || actor.actorId < 0) {
        throw std::invalid_argument("transition actor identity is invalid");
    }
    return static_cast<int16_t>(
        static_cast<uint32_t>(actor.actorId) & mContract.transitionActorIdSpawnMask);
}

int16_t NativeRoomRuntime::TransitionSpawnParams(
    const Oot3d::RoomCompilationActorEntry& actor) const {
    if (!actor.transition || actor.sourceIndex < 0) {
        throw std::invalid_argument("transition actor source index is invalid");
    }
    const uint32_t base = static_cast<uint16_t>(actor.params);
    const uint32_t index = static_cast<uint32_t>(actor.sourceIndex);
    const uint32_t value = base + index * mContract.transitionActorParamsIndexStride;
    return static_cast<int16_t>(static_cast<uint16_t>(value));
}

std::vector<int32_t> NativeRoomRuntime::ResidentRooms() const {
    std::vector<int32_t> result;
    if (mCurrentRoom >= 0) {
        result.push_back(mCurrentRoom);
    }
    if (mPreviousRoom >= 0 && mPreviousRoom != mCurrentRoom) {
        result.push_back(mPreviousRoom);
    }
    return result;
}

int32_t NativeRoomRuntime::CurrentRoom() const {
    return mCurrentRoom;
}

int32_t NativeRoomRuntime::PreviousRoom() const {
    return mPreviousRoom;
}

int32_t NativeRoomRuntime::PendingRoom() const {
    return mPendingRoom;
}

uint8_t NativeRoomRuntime::LoadState() const {
    return mLoadState;
}

bool NativeRoomRuntime::ResourceCleanupPending() const {
    return mResourceCleanupPending;
}

uint32_t NativeRoomRuntime::CleanupTicksRemaining() const {
    return mCleanupTicksRemaining;
}

uint64_t NativeRoomRuntime::RequestCount() const {
    return mRequestCount;
}

uint64_t NativeRoomRuntime::CompletionCount() const {
    return mCompletionCount;
}

uint64_t NativeRoomRuntime::ResourceCleanupCompletionCount() const {
    return mResourceCleanupCompletionCount;
}

} // namespace Oot3dNativeGame
