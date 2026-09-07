#pragma once

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr uint32_t kSourceGameplayOwnerCount = 8U;

struct SourceGameplayOwnerRequest {
    bool EnableProfile = false;
    bool TypedGameStateUpdate = false;
    bool ActorInitContext = false;
    bool ActorUpdateAll = false;
    bool CutsceneUpdateFrame = false;
    bool CutsceneProcessCommands = false;
    bool CameraUpdate = false;
    bool PlayerUpdate = false;
    bool PlayerUpdateCommon = false;
};

struct SourceGameplayOwnerSelection {
    bool ProfileRequested = false;
    bool GameStateUpdate = false;
    bool ActorInitContext = false;
    bool ActorUpdateAll = false;
    bool CutsceneUpdateFrame = false;
    bool CutsceneProcessCommands = false;
    bool CameraUpdate = false;
    bool PlayerUpdate = false;
    bool PlayerUpdateCommon = false;

    constexpr uint32_t EnabledOwnerCount() const noexcept {
        return static_cast<uint32_t>(GameStateUpdate) +
               static_cast<uint32_t>(ActorInitContext) +
               static_cast<uint32_t>(ActorUpdateAll) +
               static_cast<uint32_t>(CutsceneUpdateFrame) +
               static_cast<uint32_t>(CutsceneProcessCommands) +
               static_cast<uint32_t>(CameraUpdate) +
               static_cast<uint32_t>(PlayerUpdate) +
               static_cast<uint32_t>(PlayerUpdateCommon);
    }

    constexpr bool Complete() const noexcept {
        return EnabledOwnerCount() == kSourceGameplayOwnerCount;
    }
};

constexpr SourceGameplayOwnerSelection ResolveSourceGameplayOwnerSelection(
    const SourceGameplayOwnerRequest& request) noexcept {
    return {
        request.EnableProfile,
        request.TypedGameStateUpdate,
        request.ActorInitContext || request.EnableProfile,
        request.ActorUpdateAll || request.EnableProfile,
        request.CutsceneUpdateFrame || request.EnableProfile,
        request.CutsceneProcessCommands || request.EnableProfile,
        request.CameraUpdate || request.EnableProfile,
        request.PlayerUpdate || request.EnableProfile,
        request.PlayerUpdateCommon || request.EnableProfile,
    };
}

} // namespace Oot3dNativeGame
