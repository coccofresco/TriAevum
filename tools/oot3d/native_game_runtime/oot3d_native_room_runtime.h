#pragma once

#include <cstdint>
#include <vector>

#include "oot3d_room_compilation_unit.h"

namespace Oot3dNativeGame {

class NativeRoomRuntime {
  public:
    explicit NativeRoomRuntime(const Oot3d::RoomCompilationUnit& unit);

    bool RequestRoom(int32_t roomIndex, int32_t* droppedPreviousRoom = nullptr);
    bool CompleteRoomRequest();
    bool AdvanceResourceCleanup();

    bool IsRoomResident(int32_t roomIndex) const;
    bool IsRoomLoaded(int32_t roomIndex) const;
    bool ShouldRetainRoomActor(int32_t roomIndex) const;
    bool ShouldSpawnTransitionActor(const Oot3d::RoomCompilationActorEntry& actor) const;
    int16_t TransitionSpawnActorId(const Oot3d::RoomCompilationActorEntry& actor) const;
    int16_t TransitionSpawnParams(const Oot3d::RoomCompilationActorEntry& actor) const;
    std::vector<int32_t> ResidentRooms() const;

    int32_t CurrentRoom() const;
    int32_t PreviousRoom() const;
    int32_t PendingRoom() const;
    uint8_t LoadState() const;
    bool ResourceCleanupPending() const;
    uint32_t CleanupTicksRemaining() const;
    uint64_t RequestCount() const;
    uint64_t CompletionCount() const;
    uint64_t ResourceCleanupCompletionCount() const;

  private:
    bool MatchesResidentRoom(int32_t roomIndex) const;

    const Oot3d::RoomCompilationUnit& mUnit;
    const Oot3d::RoomCompilationLifecycleContract& mContract;
    int32_t mCurrentRoom = -1;
    int32_t mPreviousRoom = -1;
    int32_t mPendingRoom = -1;
    uint8_t mLoadState = 0;
    bool mResourceCleanupPending = false;
    uint32_t mCleanupTicksRemaining = 0;
    uint64_t mRequestCount = 0;
    uint64_t mCompletionCount = 0;
    uint64_t mResourceCleanupCompletionCount = 0;
};

} // namespace Oot3dNativeGame
