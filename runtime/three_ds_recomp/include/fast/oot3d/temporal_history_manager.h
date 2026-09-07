#pragma once

#include "fast/oot3d/scene_view_bridge.h"

#include <array>
#include <cstdint>
#include <unordered_map>

namespace Fast::Oot3d {

enum class TemporalResetReason : uint8_t {
    None,
    FirstFrame,
    ExplicitReset,
    ResolutionChanged,
    ProjectionChanged,
    CameraTeleport,
    NonInvertibleViewProjection,
};

struct TemporalViewInput {
    SceneViewKey Key;
    uint64_t RenderedFrameId = 0;
    uint64_t SourceResetEpoch = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::array<float, 16> Projection{};
    std::array<float, 16> WorldToClip{};
    std::array<float, 3> Eye{};
    std::array<float, 3> At{};
    std::array<float, 2> JitterUv{};
};

struct TemporalViewState {
    std::array<float, 16> CurrentWorldToClip{};
    std::array<float, 16> PreviousWorldToClip{};
    std::array<float, 16> InverseCurrentWorldToClip{};
    std::array<float, 2> CurrentJitterUv{};
    std::array<float, 2> PreviousJitterUv{};
    uint64_t HistoryEpoch = 0;
    bool HistoryValid = false;
    bool CameraCut = true;
    TemporalResetReason ResetReason = TemporalResetReason::FirstFrame;
};

// Renderer-independent owner of per-view temporal continuity. Vulkan passes
// consume its immutable result; savestate and scene transitions only need to
// advance an epoch, never reach into TAA/upscaler resources directly.
class TemporalHistoryManager final {
  public:
    [[nodiscard]] TemporalViewState Prepare(const TemporalViewInput& input);
    void Reset();
    void Invalidate(const SceneViewKey& key);
    [[nodiscard]] uint64_t Epoch() const;

  private:
    struct KeyHash {
        size_t operator()(const SceneViewKey& key) const;
    };
    struct Record {
        TemporalViewInput Input;
        TemporalViewState State;
    };
    uint64_t mEpoch = 1;
    std::unordered_map<SceneViewKey, Record, KeyHash> mRecords;
};

[[nodiscard]] bool InvertTemporalMatrix(
    const std::array<float, 16>& matrix, std::array<float, 16>& inverse);

} // namespace Fast::Oot3d
