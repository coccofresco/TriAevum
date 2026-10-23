#pragma once

#include "fast/oot3d/grass_types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Fast::Oot3d {

// Conservative per-cell actor lists for the grass compaction shader. A root
// can only collide with an actor whose swept XZ box contains it (the
// shader's first rejection), so listing every actor whose box, widened by one
// cell, overlaps a cell keeps every contributing actor of every root in that
// cell, in the original actor order. Skipped actors contribute exactly zero.
struct GrassActorCells {
    std::array<float, 2> Origin{};
    float InverseCellSize = 0.0F;
    // Zero width: no grid, the shader iterates every actor.
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    // Width * Height + 1 cell offsets, then the actor indices of every cell.
    std::vector<uint32_t> Words;
};

inline constexpr uint32_t kGrassActorCellsMaximumExtent = 64U;

// The swept box exactly as resolve_direct_collision rejects it.
[[nodiscard]] inline std::array<float, 4> GrassActorSweptBox(const GrassInteractor& actor,
                                                             float radiusMultiplier) noexcept {
    const float radius = std::max(0.01F, actor.Radius * radiusMultiplier);
    return {std::min(actor.PreviousPosition[0], actor.Position[0]) - radius,
            std::min(actor.PreviousPosition[2], actor.Position[2]) - radius,
            std::max(actor.PreviousPosition[0], actor.Position[0]) + radius,
            std::max(actor.PreviousPosition[2], actor.Position[2]) + radius};
}

[[nodiscard]] inline GrassActorCells BuildGrassActorCells(std::span<const GrassInteractor> actors,
                                                          float radiusMultiplier) {
    GrassActorCells cells;
    if (actors.empty() || actors.size() > UINT32_MAX / kGrassActorCellsMaximumExtent / kGrassActorCellsMaximumExtent)
        return cells;
    std::vector<std::array<float, 4>> boxes;
    boxes.reserve(actors.size());
    std::array<float, 4> bounds{INFINITY, INFINITY, -INFINITY, -INFINITY};
    const auto finite = [](float value) { return std::isfinite(value); };
    for (const auto& actor : actors) {
        // GLSL min/max of a NaN is undefined: such frames keep the full loop.
        const auto box = GrassActorSweptBox(actor, radiusMultiplier);
        if (!std::all_of(box.begin(), box.end(), finite) || !finite(actor.Radius * radiusMultiplier) ||
            !std::all_of(actor.Position.begin(), actor.Position.end(), finite) ||
            !std::all_of(actor.PreviousPosition.begin(), actor.PreviousPosition.end(), finite))
            return cells;
        bounds = {std::min(bounds[0], box[0]), std::min(bounds[1], box[1]),
                  std::max(bounds[2], box[2]), std::max(bounds[3], box[3])};
        boxes.push_back(box);
    }
    // Room for the one-cell margin on both sides and for rounding.
    const float extent = std::max(bounds[2] - bounds[0], bounds[3] - bounds[1]);
    const float cellSize = extent / static_cast<float>(kGrassActorCellsMaximumExtent - 4U);
    if (!(cellSize > 0.0F) || !std::isfinite(cellSize))
        return cells;
    const float inverse = 1.0F / cellSize;
    const std::array<float, 2> origin{bounds[0] - cellSize, bounds[1] - cellSize};
    const auto cell = [&](float value, float start) { return std::floor((value - start) * inverse); };
    const float width = cell(bounds[2], origin[0]) + 2.0F;
    const float height = cell(bounds[3], origin[1]) + 2.0F;
    if (!(width >= 1.0F && height >= 1.0F && width <= kGrassActorCellsMaximumExtent &&
          height <= kGrassActorCellsMaximumExtent))
        return cells;
    cells.Origin = origin;
    cells.InverseCellSize = inverse;
    cells.Width = static_cast<uint32_t>(width);
    cells.Height = static_cast<uint32_t>(height);
    const uint32_t cellCount = cells.Width * cells.Height;
    struct Range {
        uint32_t X0, Z0, X1, Z1;
    };
    std::vector<Range> ranges;
    ranges.reserve(boxes.size());
    const auto clampCell = [](float value, uint32_t size) {
        return static_cast<uint32_t>(std::clamp(value, 0.0F, static_cast<float>(size - 1U)));
    };
    std::vector<uint32_t> counts(cellCount + 1U);
    for (const auto& box : boxes) {
        const Range range{clampCell(cell(box[0], origin[0]) - 1.0F, cells.Width),
                          clampCell(cell(box[1], origin[1]) - 1.0F, cells.Height),
                          clampCell(cell(box[2], origin[0]) + 1.0F, cells.Width),
                          clampCell(cell(box[3], origin[1]) + 1.0F, cells.Height)};
        ranges.push_back(range);
        for (uint32_t z = range.Z0; z <= range.Z1; ++z)
            for (uint32_t x = range.X0; x <= range.X1; ++x) ++counts[z * cells.Width + x];
    }
    cells.Words.resize(cellCount + 1U);
    uint32_t total = 0U;
    for (uint32_t i = 0; i < cellCount; ++i) {
        cells.Words[i] = total;
        total += counts[i];
    }
    cells.Words[cellCount] = total;
    cells.Words.resize(cellCount + 1U + total);
    std::fill(counts.begin(), counts.end(), 0U);
    for (uint32_t actor = 0; actor < ranges.size(); ++actor) {
        const auto& range = ranges[actor];
        for (uint32_t z = range.Z0; z <= range.Z1; ++z)
            for (uint32_t x = range.X0; x <= range.X1; ++x) {
                const uint32_t index = z * cells.Width + x;
                cells.Words[cellCount + 1U + cells.Words[index] + counts[index]++] = actor;
            }
    }
    return cells;
}

} // namespace Fast::Oot3d
