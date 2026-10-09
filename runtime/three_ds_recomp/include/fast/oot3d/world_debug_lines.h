#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Fast::Oot3d {

// A line in the title's world coordinates, drawn over the scene with its
// depth: the parts in front of the scene's surfaces at full colour, the parts
// behind them dashed and faint, so a line shows where it meets a surface.
struct WorldDebugLine {
    std::array<float, 3> From{};
    std::array<float, 3> To{};
    // Non-premultiplied RGBA.
    std::array<float, 4> Color{ 1.0F, 1.0F, 1.0F, 1.0F };
    bool operator==(const WorldDebugLine&) const = default;
};

struct WorldDebugLineSet {
    std::vector<WorldDebugLine> Lines;
    // At a 720-pixel-high target; scaled with the target's height.
    float WidthPixels = 2.0F;
    // The opacity of the parts behind the scene's surfaces.
    float HiddenOpacity = 0.35F;
    bool operator==(const WorldDebugLineSet&) const = default;
};

// The title's lines for the frames to come (empty: none). The renderer reads
// the latest set when it draws the scene.
class WorldDebugLineRuntime final {
  public:
    static WorldDebugLineRuntime& Instance();
    void Publish(WorldDebugLineSet lines);
    void Clear();
    [[nodiscard]] std::shared_ptr<const WorldDebugLineSet> Latest() const;

  private:
    mutable std::mutex mMutex;
    std::shared_ptr<const WorldDebugLineSet> mLatest;
};

// One corner of a line's screen-space quad: both ends in clip space (the
// target's physical orientation, jitter applied) and which corner it is.
struct WorldDebugLineVertex {
    std::array<float, 4> ClipFrom{};
    std::array<float, 4> ClipTo{};
    std::array<float, 4> Color{};
    // X: 0 at From, 1 at To; Y: -1 or +1 across the line.
    std::array<float, 2> Corner{};
    std::array<float, 2> Padding{};
};
static_assert(sizeof(WorldDebugLineVertex) == 64U);

inline constexpr uint32_t kWorldDebugLineVerticesPerLine = 6U;
inline constexpr size_t kMaximumWorldDebugLines = 16384U;

// Clip-space quads for the lines, clipped to the near plane (Vulkan depth,
// 0 at the near plane): `worldToClip` is column-major, logical (unrotated);
// the result is turned into the physical (rotated) framebuffer and offset
// by the temporal jitter (NDC).
[[nodiscard]] std::vector<WorldDebugLineVertex> BuildWorldDebugLineVertices(
    const WorldDebugLineSet& lines, const std::array<float, 16>& worldToClip,
    const std::array<float, 2>& jitterNdc = {});

} // namespace Fast::Oot3d
