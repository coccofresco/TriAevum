#include "fast/extension/grass_shadow_refinement.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Fast::Extension {
namespace {
constexpr float kMinimumCellSize = 4.0F;
constexpr float kBoundaryFade = 8.0F;

std::array<float, 4> SurfaceRectangle(const GrassShadowRefinementPatch& patch) {
    const auto axis = std::bit_cast<uint32_t>(patch.Normal[3]);
    const auto u = (axis + 1U) % 3U, v = (axis + 2U) % 3U;
    return { std::min({ patch.A[u], patch.B[u], patch.C[u] }), std::min({ patch.A[v], patch.B[v], patch.C[v] }),
             std::max({ patch.A[u], patch.B[u], patch.C[u] }), std::max({ patch.A[v], patch.B[v], patch.C[v] }) };
}

std::array<float, 4> GridRectangle(const GrassShadowRefinementPatch& patch) {
    auto rectangle = SurfaceRectangle(patch);
    for (size_t axis = 0; axis < 2; ++axis) {
        rectangle[axis] = std::max(rectangle[axis], patch.Footprint[axis] - kBoundaryFade);
        rectangle[axis + 2] = std::min(rectangle[axis + 2], patch.Footprint[axis + 2] + kBoundaryFade);
    }
    return rectangle;
}

bool Contains(const std::array<float, 4>& outer, const std::array<float, 4>& inner) {
    return outer[0] <= inner[0] && outer[1] <= inner[1] && outer[2] >= inner[2] && outer[3] >= inner[3];
}

bool ProjectFootprint(const GrassShadowSurface& surface, const Renderer3ds::ShadowBounds& bounds,
                      const Renderer3ds::ShadowVector& light, GrassShadowRefinementPatch& patch) {
    if (!bounds.Available)
        return false;
    uint32_t axis = 0;
    float denominator = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        denominator += surface.Normal[i] * light[i];
        if (std::abs(surface.Normal[i]) > std::abs(surface.Normal[axis]))
            axis = i;
    }
    if (!std::isfinite(denominator) || std::abs(denominator) < 1.0e-6F)
        return false;
    patch.A = surface.Position[0];
    patch.B = surface.Position[1];
    patch.C = surface.Position[2];
    patch.B[3] = kBoundaryFade;
    patch.Normal = surface.Normal;
    patch.Normal[3] = std::bit_cast<float>(axis);
    const uint32_t u = (axis + 1U) % 3U, v = (axis + 2U) % 3U;
    const float infinity = std::numeric_limits<float>::infinity();
    patch.Footprint = { infinity, infinity, -infinity, -infinity };
    bool forward = false;
    for (uint32_t corner = 0; corner < 8; ++corner) {
        std::array<float, 3> point{};
        float distance = 0;
        for (uint32_t i = 0; i < 3; ++i) {
            point[i] = corner & (1U << i) ? bounds.Maximum[i] : bounds.Minimum[i];
            distance += surface.Normal[i] * (point[i] - patch.A[i]);
        }
        distance /= denominator;
        forward |= distance > 0;
        const float x = point[u] - light[u] * distance;
        const float y = point[v] - light[v] * distance;
        if (!std::isfinite(x) || !std::isfinite(y))
            return false;
        patch.Footprint[0] = std::min(patch.Footprint[0], x);
        patch.Footprint[1] = std::min(patch.Footprint[1], y);
        patch.Footprint[2] = std::max(patch.Footprint[2], x);
        patch.Footprint[3] = std::max(patch.Footprint[3], y);
    }
    if (!forward)
        return false; // A receiver above the caster cannot receive its shadow.
    const auto rectangle = SurfaceRectangle(patch);
    for (uint32_t i = 0; i < 2; ++i) {
        if (!std::isfinite(rectangle[i]) || !std::isfinite(rectangle[i + 2]) ||
            rectangle[i + 2] < patch.Footprint[i] - kBoundaryFade ||
            rectangle[i] > patch.Footprint[i + 2] + kBoundaryFade)
            return false;
        const float center = (patch.Footprint[i] + patch.Footprint[i + 2]) * 0.5F;
        const float distance = std::clamp(center, rectangle[i], rectangle[i + 2]) - center;
        patch.Grid[3] += distance * distance;
    }
    return true;
}
} // namespace

void BuildGrassShadowRefinement(std::span<const GrassShadowSurface> surfaces,
                                const std::array<Renderer3ds::ShadowBounds, 2>& focus,
                                const Renderer3ds::ShadowVector& light, GrassShadowRefinement& output) {
    output.Patches.clear();
    output.SurfacePatches.assign(surfaces.size(), {});
    output.PointCount = 0;
    output.CellSize = kMinimumCellSize;
    if (surfaces.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("grass refinement surfaces exceed uint32 addressing");
    output.Patches.reserve(kGrassShadowRefinementPatchesPerActor * focus.size());
    const auto priority = [](const auto& a, const auto& b) {
        return a.Grid[3] != b.Grid[3] ? a.Grid[3] < b.Grid[3] : a.Address[3] < b.Address[3];
    };
    for (uint32_t actor = 0; actor < focus.size(); ++actor) {
        if (!focus[actor].Available)
            continue;
        const size_t first = output.Patches.size();
        for (uint32_t surface = 0; surface < surfaces.size(); ++surface) {
            GrassShadowRefinementPatch candidate;
            if (!ProjectFootprint(surfaces[surface], focus[actor], light, candidate))
                continue;
            candidate.C[3] = static_cast<float>(actor);
            candidate.Address[3] = surface;
            if (output.Patches.size() - first < kGrassShadowRefinementPatchesPerActor) {
                output.Patches.push_back(candidate);
                std::push_heap(output.Patches.begin() + first, output.Patches.end(), priority);
            } else {
                // A highly tessellated surface cannot starve the other actor.
                // Keep nearest patches; ties retain native surface order.
                const auto begin = output.Patches.begin() + first;
                if (priority(candidate, *begin)) {
                    std::pop_heap(begin, output.Patches.end(), priority);
                    output.Patches.back() = candidate;
                    std::push_heap(begin, output.Patches.end(), priority);
                }
            }
        }
    }
    std::sort(output.Patches.begin(), output.Patches.end(), [](const auto& a, const auto& b) {
        return a.Address[3] != b.Address[3] ? a.Address[3] < b.Address[3] : a.C[3] < b.C[3];
    });
    // Power-of-two world lattices never swim with the camera. At most 256
    // patches each converge to <= 3x3 nodes, so this budget loop terminates.
    for (;;) {
        uint32_t count = 0;
        bool fits = true;
        for (size_t index = 0; index < output.Patches.size(); ++index) {
            auto& patch = output.Patches[index];
            auto rectangle = GridRectangle(patch);
            bool shared = false;
            if (index + 1 < output.Patches.size() && output.Patches[index + 1].Address[3] == patch.Address[3]) {
                const auto other = GridRectangle(output.Patches[index + 1]);
                shared = Contains(rectangle, other) || Contains(other, rectangle);
                if (shared && Contains(other, rectangle))
                    rectangle = other;
            }
            std::array<uint32_t, 2> size{};
            for (uint32_t i = 0; i < 2; ++i) {
                const double minimum = rectangle[i];
                const double maximum = rectangle[i + 2];
                const double first = std::floor(minimum / output.CellSize);
                const double columns = std::max(2.0, std::ceil(maximum / output.CellSize) - first + 1.0);
                if (columns > kGrassShadowRefinementPointBudget) {
                    fits = false;
                    break;
                }
                patch.Grid[i] = static_cast<float>(first * output.CellSize);
                size[i] = static_cast<uint32_t>(columns);
            }
            if (!fits)
                break;
            const uint64_t nodes = uint64_t{ size[0] } * size[1];
            if (nodes > kGrassShadowRefinementPointBudget - count) {
                fits = false;
                break;
            }
            patch.A[3] = output.CellSize;
            patch.Grid[2] = 1.0F / output.CellSize;
            patch.Address[0] = count;
            patch.Address[1] = size[0];
            patch.Address[2] = size[1];
            count += static_cast<uint32_t>(nodes);
            if (shared) {
                // A rider inside the horse's receiver grid needs no second
                // VSM read. Keep both original fades, but share lattice nodes.
                // Addresses remain sorted; the GPU lookup admits equal starts.
                auto& other = output.Patches[++index];
                other.A[3] = patch.A[3];
                std::copy_n(patch.Grid.begin(), 3, other.Grid.begin());
                std::copy_n(patch.Address.begin(), 3, other.Address.begin());
            }
        }
        if (fits) {
            output.PointCount = count;
            break;
        }
        output.CellSize *= 2.0F;
    }
    for (uint32_t index = 0; index < output.Patches.size(); ++index) {
        const auto& patch = output.Patches[index];
        output.SurfacePatches[patch.Address[3]][static_cast<uint32_t>(patch.C[3])] = index + 1U;
    }
}

} // namespace Fast::Extension
