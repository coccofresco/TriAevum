#include "fast/renderer3ds/pica_shadow_geometry.h"
#include "fast/renderer3ds/pica_vertex_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Fast::Renderer3ds {
namespace {
using namespace VertexGeometry;
uint64_t Hash(uint64_t hash, uint64_t value) noexcept {
    for (size_t byte = 0; byte < sizeof(value); ++byte) {
        hash = (hash ^ uint8_t(value)) * 1099511628211ULL;
        value >>= 8;
    }
    return hash;
}
} // namespace

VirtualShadowCaster BuildPicaShadowCaster(const PicaShadowGeometryView& geometry,
                                          const PicaVertexShaderHookLayout& hooks, uint32_t booleanUniforms,
                                          std::span<const uint8_t> floatUniforms, const ShadowMatrix& viewToWorld,
                                          uint64_t geometryVersion, PicaShadowCasterCache& cache,
                                          PicaShadowCasterAnchor* anchor) noexcept {
    const auto* transform = hooks.TransformProgram();
    if (!transform || geometry.VertexOrIndexCount == 0)
        return {};
    ShadowMatrix postView, model;
    if (!UniformMatrix(floatUniforms, transform->ViewFirstUniform, postView) ||
        !UniformMatrix(floatUniforms, transform->ModelFirstUniform, model))
        return {};
    const auto worldFromModel = Multiply(viewToWorld, postView);
    const auto* skeleton = hooks.SkeletonProgram();
    const bool skinned = skeleton && (booleanUniforms & (1U << skeleton->EnableBooleanUniform)) != 0;
    const auto* surface = hooks.SurfaceProgram();
    if (skinned && (!surface || !surface->SkinnedNormalsAvailable() || !surface->SkinnedPositionScale.Available()))
        return {};
    uint64_t signature = 0;
    ShadowMatrix world{};
    const auto anchored = [&](const ShadowBounds& bounds, uint64_t pose) -> VirtualShadowCaster {
        if (anchor == nullptr || pose == 0)
            return { bounds, pose, false, 0, VirtualShadowCasterVersionState::Rigid, pose };
        // A new region: a new revision, so no earlier anchor's version can
        // stand for it even when both quantize to the same cell.
        ++anchor->Revision;
        anchor->World = world;
        anchor->Bounds = bounds;
        anchor->GeometryVersion = geometryVersion;
        anchor->Signature = Hash(pose, anchor->Revision);
        if (anchor->Signature == 0)
            anchor->Signature = 1;
        anchor->Valid = true;
        return { bounds, anchor->Signature, false, 0, VirtualShadowCasterVersionState::Rigid, pose };
    };
    if (!skinned && geometryVersion != 0) {
        world = Multiply(worldFromModel, model);
        if (anchor != nullptr && anchor->Valid && anchor->GeometryVersion == geometryVersion) {
            bool inside = true;
            for (size_t index = 0; index < world.size() && inside; ++index) {
                const bool translation = index >= 12 && index < 15;
                const bool projective = index % 4 == 3;
                const double tolerance = translation ? 1.0 / 128.0 : projective ? 0.0 : 1.0 / 131072.0;
                inside = std::isfinite(world[index]) &&
                         std::abs(double(world[index]) - double(anchor->World[index])) <= tolerance;
            }
            if (inside) {
                uint64_t pose = Hash(1469598103934665603ULL, geometryVersion);
                for (size_t index = 0; index < world.size(); ++index) {
                    const double scale = index >= 12 ? 64.0 : 65536.0;
                    pose = Hash(pose, static_cast<uint64_t>(static_cast<int64_t>(std::llround(double(world[index]) * scale))));
                }
                return { anchor->Bounds, anchor->Signature, false, 0, VirtualShadowCasterVersionState::Rigid,
                         pose == 0 ? 1 : pose };
            }
        }
        signature = Hash(1469598103934665603ULL, geometryVersion);
        for (size_t index = 0; index < world.size(); ++index) {
            // Undoing float32 view matrices introduces sub-texel roundoff.
            // The bounds guard below contains the entire quantization cell.
            const double scale = index >= 12 ? 64.0 : 65536.0;
            const double value = double(world[index]) * scale;
            if (!std::isfinite(value) || std::abs(value) > 9.0e15)
                return {};
            signature = Hash(signature, static_cast<uint64_t>(static_cast<int64_t>(std::llround(value))));
        }
        if (signature == 0)
            signature = 1;
        // The memo's bounds may come from another pose of the same cell; a
        // new anchor needs bounds of its own pose (its guard covers one
        // step from there), so it recomputes them.
        if (anchor == nullptr && cache.Signature == signature && cache.Bounds.Available)
            return { cache.Bounds, signature, false, 0, VirtualShadowCasterVersionState::Rigid, signature };
    }
    Pose pose(hooks, booleanUniforms, floatUniforms, viewToWorld);
    if (!pose.Valid) return {};
    const bool multiple = pose.Multiple;
    ShadowBounds bounds;
    bool homogeneousRigid = true;
    float maximumLocalL1 = 0;
    const auto positions = ResolveAttribute(geometry, transform->PositionInputRegister);
    const auto boneIndices = skinned ? ResolveAttribute(geometry, skeleton->BoneIndexInputRegister) : AttributeStream{};
    const auto boneWeights =
        multiple ? ResolveAttribute(geometry, skeleton->BoneWeightInputRegister) : AttributeStream{};
    for (uint32_t item = 0; item < geometry.VertexOrIndexCount; ++item) {
        uint32_t rawIndex = item;
        if (geometry.Indexed) {
            const uint64_t offset = geometry.IndexOffset + uint64_t(item) * sizeof(uint16_t);
            if (offset > geometry.Payload.size() || sizeof(uint16_t) > geometry.Payload.size() - offset)
                return {};
            uint16_t index;
            std::memcpy(&index, geometry.Payload.data() + offset, sizeof(index));
            rawIndex = index;
        }
        const int64_t adjusted = int64_t(rawIndex) + geometry.BaseVertex;
        if (adjusted < 0 || adjusted > std::numeric_limits<uint32_t>::max())
            return {};
        const auto vertex = static_cast<uint32_t>(adjusted);
        Vec4 position;
        if (!positions.Read(vertex, position))
            return {};
        maximumLocalL1 =
            std::max(maximumLocalL1, std::abs(position[0]) + std::abs(position[1]) + std::abs(position[2]));
        Vec4 indices{}, weights{};
        if (skinned && (!boneIndices.Read(vertex, indices) ||
                        (multiple && !boneWeights.Read(vertex, weights))))
            return {};
        if (!skinned) homogeneousRigid &= position[3] == 1.0F;
        ShadowVector world;
        if (!pose.Position(position, indices, weights, world)) return {};
        if (!bounds.Available) {
            bounds.Minimum = bounds.Maximum = world;
            bounds.Available = true;
        } else
            for (size_t axis = 0; axis < 3; ++axis) {
                bounds.Minimum[axis] = std::min(bounds.Minimum[axis], world[axis]);
                bounds.Maximum[axis] = std::max(bounds.Maximum[axis], world[axis]);
            }
    }
    if (!bounds.Available)
        return {};
    for (size_t axis = 0; axis < 3; ++axis) {
        // Conservative against matrix cancellation and cached rigid pose bins.
        const float guard = 0.05F + maximumLocalL1 / 65536.0F +
                            std::max(std::abs(bounds.Minimum[axis]), std::abs(bounds.Maximum[axis])) * 2.0e-5F;
        bounds.Minimum[axis] -= guard;
        bounds.Maximum[axis] += guard;
    }
    if (!homogeneousRigid)
        signature = 0;
    if (signature != 0)
        cache = { signature, bounds };
    using State = VirtualShadowCasterVersionState;
    const State state = skinned ? State::Skinned
                      : !homogeneousRigid ? State::NonHomogeneous
                      : signature == 0 ? State::Uncacheable
                                       : State::Rigid;
    if (state == State::Rigid)
        return anchored(bounds, signature);
    if (anchor != nullptr)
        anchor->Valid = false;
    return { bounds, signature, false, 0, state, signature };
}

} // namespace Fast::Renderer3ds
