#pragma once

#include "fast/extension/scene_view_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Fast::Extension {

using SampleViewMatrix = std::array<float, 16>;

// The camera of the geometry actually rendered into one target during one
// presented frame: the captured native world-to-view of its opaque world
// draws (interpolated with the geometry for synthetic samples). The view most
// draws agree on wins, so a stray draw with its own view cannot move it.
class SampleViewAccumulator final {
  public:
    void Observe(uint64_t frame, const SampleViewMatrix& worldToView) noexcept;
    [[nodiscard]] std::optional<SampleViewMatrix> Resolve(uint64_t frame) const noexcept;
    // Distinct views observed in `frame` (diagnostics).
    [[nodiscard]] uint32_t DistinctViews(uint64_t frame) const noexcept;

  private:
    struct Candidate {
        SampleViewMatrix View{};
        uint32_t Draws = 0;
    };
    static constexpr size_t kCandidates = 4;
    uint64_t mFrame = 0;
    std::array<Candidate, kCandidates> mCandidates{};
    uint32_t mCount = 0;
};

// The x and y scales of the projection the native draws apply: OOT3D's
// programs read their projection from vertex float uniforms c0-c3, rotated
// with the 3DS framebuffer. The game adjusts the x and y rows of the matrix it
// builds from the frustum its camera requests, so that frustum alone does not
// describe the rendered depth. X scales view x (Projection[0]), Y view y
// (Projection[5]).
struct SampleProjection {
    float X = 0.0F;
    float Y = 0.0F;
    bool operator==(const SampleProjection&) const = default;
};
// The scales of a perspective program's projection, rotated or not; none for
// an orthographic or unrecognized projection.
[[nodiscard]] std::optional<SampleProjection> DecodePicaPerspectiveProjection(
    std::span<const uint8_t> packedVertexUniforms) noexcept;

// The projection most opaque world draws of one presented frame agree on.
class SampleProjectionAccumulator final {
  public:
    void Observe(uint64_t frame, const SampleProjection& projection) noexcept;
    [[nodiscard]] std::optional<SampleProjection> Resolve(uint64_t frame) const noexcept;

  private:
    struct Candidate {
        SampleProjection Projection{};
        uint32_t Draws = 0;
    };
    static constexpr size_t kCandidates = 4;
    uint64_t mFrame = 0;
    std::array<Candidate, kCandidates> mCandidates{};
    uint32_t mCount = 0;
};

// The guest projection with the native x and y scales: the x and y rows of the
// projection and of world-to-clip, and the frustum extents, follow them; depth
// and the camera are unchanged. Returns the guest for an implausible scale.
[[nodiscard]] PerspectiveViewState ApplySampleProjection(const PerspectiveViewState& guest,
                                                         const SampleProjection& native) noexcept;

// The guest projection with the rendered world-to-view. The latest guest
// camera can belong to another simulation step (it runs ahead of the replayed
// geometry) or another interpolation phase; effects that reconstruct or
// reproject positions from the rendered depth must use this view instead.
// The result carries no guest serial: its identity is its view.
[[nodiscard]] PerspectiveViewState ResolveSamplePerspective(const PerspectiveViewState& guest,
                                                            const SampleViewMatrix& worldToView) noexcept;

} // namespace Fast::Extension
