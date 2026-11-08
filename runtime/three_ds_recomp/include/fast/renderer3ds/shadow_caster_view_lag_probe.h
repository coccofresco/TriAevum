#pragma once

// Developer diagnostic (F11 read timing). It answers one question: does a
// title's captured WorldToView lag or lead the view that the game folded into
// the modelview uniform? For each rigid caster draw the probe rebuilds the
// world matrix twice:
//   world_current  = inverse(WorldToView of this frame)     * postView * model
//   world_previous = inverse(WorldToView of the last frame) * postView * model
// It counts re-anchors under each, with the same tolerances as
// BuildPicaShadowCaster (1/128 translation, 1/131072 rotation). Suppose the
// uniform was formed from the previous frame's view. Then world_previous stays
// still while world_current follows every camera change, so
// re-anchors(previous) is about 0 and re-anchors(current) is above 0.
// Matching timing gives the opposite result. A one-frame mismatch under a
// constant camera step is a constant offset and does not re-anchor: only a
// change of the step (acceleration or jitter) separates the two counts. Keys are opaque, and the probe
// holds no title semantics.

#include "fast/renderer3ds/pica_vertex_geometry.h"

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace Fast::Renderer3ds {

struct ShadowCasterViewLagCounts {
    uint64_t Frames = 0;      // frames with a view
    uint64_t ViewChanges = 0; // frames whose view differs from the previous frame's
    uint64_t Pairs = 0;       // per key, observations in consecutive frames
    uint64_t ReanchorCurrent = 0;
    uint64_t ReanchorPrevious = 0;
};

class ShadowCasterViewLagProbe final {
  public:
    // Takes the view-to-world matrix of `frame`, which is the inverse of that
    // frame's captured WorldToView. Call it for every draw: only a new frame
    // id advances the probe.
    void BeginFrame(uint64_t frame, const ShadowMatrix& viewToWorld) noexcept {
        if (mHasFrame && frame == mFrame)
            return;
        mHasPrevious = mHasFrame && frame == mFrame + 1U; // a gap leaves no previous view
        if (mHasPrevious)
            mPrevious = mCurrent;
        if (mHasPrevious && mPrevious != viewToWorld)
            ++mCounts.ViewChanges;
        mCurrent = viewToWorld;
        mFrame = frame;
        mHasFrame = true;
        ++mCounts.Frames;
    }

    // Records one rigid caster draw of the current frame.
    void Observe(uint64_t key, const ShadowMatrix& postView, const ShadowMatrix& model) {
        if (!mHasFrame || !mHasPrevious)
            return;
        const auto local = VertexGeometry::Multiply(postView, model);
        const auto current = VertexGeometry::Multiply(mCurrent, local);
        const auto previous = VertexGeometry::Multiply(mPrevious, local);
        auto& anchor = mAnchors[key];
        if (anchor.Frame == mFrame && anchor.Valid)
            return; // one observation per key and frame
        if (anchor.Valid && anchor.Frame + 1U == mFrame) {
            ++mCounts.Pairs;
            if (!Inside(current, anchor.Current)) {
                ++mCounts.ReanchorCurrent;
                anchor.Current = current;
            }
            if (!Inside(previous, anchor.Previous)) {
                ++mCounts.ReanchorPrevious;
                anchor.Previous = previous;
            }
        } else {
            anchor.Current = current;
            anchor.Previous = previous;
        }
        anchor.Valid = true;
        anchor.Frame = mFrame;
    }

    [[nodiscard]] const ShadowCasterViewLagCounts& Counts() const noexcept { return mCounts; }

    [[nodiscard]] std::string Line() const {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "[shadow-caster-view-lag] v=1 frames=%" PRIu64 " view_changes=%" PRIu64 " pairs=%" PRIu64
                      " reanchor_current=%" PRIu64 " reanchor_previous=%" PRIu64,
                      mCounts.Frames, mCounts.ViewChanges, mCounts.Pairs, mCounts.ReanchorCurrent,
                      mCounts.ReanchorPrevious);
        return line;
    }

    [[nodiscard]] static bool Inside(const ShadowMatrix& world, const ShadowMatrix& anchor) noexcept {
        for (size_t index = 0; index < world.size(); ++index) {
            const bool translation = index >= 12 && index < 15;
            const bool projective = index % 4 == 3;
            const double tolerance = translation ? 1.0 / 128.0 : projective ? 0.0 : 1.0 / 131072.0;
            if (!std::isfinite(world[index]) || std::abs(double(world[index]) - double(anchor[index])) > tolerance)
                return false;
        }
        return true;
    }

  private:
    struct Anchor {
        ShadowMatrix Current{};
        ShadowMatrix Previous{};
        uint64_t Frame = 0;
        bool Valid = false;
    };
    std::unordered_map<uint64_t, Anchor> mAnchors;
    ShadowMatrix mCurrent{};
    ShadowMatrix mPrevious{};
    uint64_t mFrame = 0;
    bool mHasFrame = false;
    bool mHasPrevious = false;
    ShadowCasterViewLagCounts mCounts;
};

} // namespace Fast::Renderer3ds
