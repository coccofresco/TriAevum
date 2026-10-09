#include "fast/oot3d/world_debug_lines.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {

WorldDebugLineRuntime& WorldDebugLineRuntime::Instance() {
    static WorldDebugLineRuntime runtime;
    return runtime;
}

void WorldDebugLineRuntime::Publish(WorldDebugLineSet lines) {
    auto published = lines.Lines.empty()
        ? nullptr
        : std::make_shared<const WorldDebugLineSet>(std::move(lines));
    std::scoped_lock lock(mMutex);
    mLatest = std::move(published);
}

void WorldDebugLineRuntime::Clear() {
    std::scoped_lock lock(mMutex);
    mLatest.reset();
}

std::shared_ptr<const WorldDebugLineSet> WorldDebugLineRuntime::Latest() const {
    std::scoped_lock lock(mMutex);
    return mLatest;
}

namespace {

std::array<float, 4> Transform(const std::array<float, 16>& matrix, const std::array<float, 3>& point) {
    std::array<float, 4> result{};
    for (size_t row = 0; row < 4U; ++row)
        result[row] = matrix[row] * point[0] + matrix[4U + row] * point[1] + matrix[8U + row] * point[2] +
                      matrix[12U + row];
    return result;
}

std::array<float, 4> Lerp(const std::array<float, 4>& from, const std::array<float, 4>& to, float t) {
    std::array<float, 4> result{};
    for (size_t index = 0; index < 4U; ++index)
        result[index] = from[index] + (to[index] - from[index]) * t;
    return result;
}

bool Finite(const std::array<float, 4>& value) {
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]) &&
           std::isfinite(value[3]);
}

} // namespace

std::vector<WorldDebugLineVertex> BuildWorldDebugLineVertices(
    const WorldDebugLineSet& lines, const std::array<float, 16>& worldToClip,
    const std::array<float, 2>& jitterNdc) {
    std::vector<WorldDebugLineVertex> vertices;
    const size_t count = std::min(lines.Lines.size(), kMaximumWorldDebugLines);
    vertices.reserve(count * kWorldDebugLineVerticesPerLine);
    // Slightly inside the near plane, so the divide stays well defined.
    constexpr float kNearDepth = 1.0e-6F;
    for (size_t index = 0; index < count; ++index) {
        const auto& line = lines.Lines[index];
        auto from = Transform(worldToClip, line.From);
        auto to = Transform(worldToClip, line.To);
        if (!Finite(from) || !Finite(to)) continue;
        // Vulkan depth: z >= 0 in front of the near plane (w > 0 there).
        const float fromDepth = from[2] - kNearDepth * from[3];
        const float toDepth = to[2] - kNearDepth * to[3];
        if (fromDepth < 0.0F && toDepth < 0.0F) continue;
        if (fromDepth < 0.0F)
            from = Lerp(from, to, fromDepth / (fromDepth - toDepth));
        else if (toDepth < 0.0F)
            to = Lerp(to, from, toDepth / (toDepth - fromDepth));
        if (from[3] <= 0.0F || to[3] <= 0.0F) continue;
        WorldDebugLineVertex vertex;
        // The physical top framebuffer is the logical view turned a quarter:
        // its X is the logical Y, its Y the logical X reversed (checked
        // against the placed lights' own lighting in Kokiri Forest).
        vertex.ClipFrom = { from[1] + jitterNdc[0] * from[3], -from[0] + jitterNdc[1] * from[3], from[2], from[3] };
        vertex.ClipTo = { to[1] + jitterNdc[0] * to[3], -to[0] + jitterNdc[1] * to[3], to[2], to[3] };
        vertex.Color = line.Color;
        // Two triangles: (0,-1) (1,-1) (1,+1) and (0,-1) (1,+1) (0,+1).
        constexpr std::array<std::array<float, 2>, kWorldDebugLineVerticesPerLine> kCorners{
            { { 0.0F, -1.0F }, { 1.0F, -1.0F }, { 1.0F, 1.0F },
              { 0.0F, -1.0F }, { 1.0F, 1.0F }, { 0.0F, 1.0F } } };
        for (const auto& corner : kCorners) {
            vertex.Corner = corner;
            vertices.push_back(vertex);
        }
    }
    return vertices;
}

} // namespace Fast::Oot3d
