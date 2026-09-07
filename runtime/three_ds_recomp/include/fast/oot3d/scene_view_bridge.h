#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace Fast::Oot3d {

enum class ProjectionKind : uint8_t { Unknown, Perspective, Orthographic };

struct SceneViewKey {
    uint64_t RenderTargetNamespace = 0;
    uint32_t ColorPhysicalAddress = 0;
    uint32_t DepthPhysicalAddress = 0;
    bool operator==(const SceneViewKey&) const = default;
};

struct SceneViewInfo {
    uint64_t ViewId = 0;
    SceneViewKey Target;
    ProjectionKind Projection = ProjectionKind::Unknown;
    std::array<float, 16> ProjectionMatrix{};
    std::array<float, 16> InverseProjectionMatrix{};
    std::array<float, 16> ViewProjectionMatrix{};
    std::array<float, 16> PreviousViewProjectionMatrix{};
    bool CameraCut = true;
};

class SceneViewBridge {
  public:
    void Publish(SceneViewInfo view);
    [[nodiscard]] std::optional<SceneViewInfo>
    Find(const SceneViewKey& target) const;
    void InvalidateTarget(const SceneViewKey& target);
    void ResetHistory();

  private:
    struct KeyHash {
        size_t operator()(const SceneViewKey& key) const;
    };
    std::unordered_map<SceneViewKey, SceneViewInfo, KeyHash> mViews;
    std::unordered_map<uint64_t, std::array<float, 16>> mPreviousByView;
};

} // namespace Fast::Oot3d
