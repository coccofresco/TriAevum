#pragma once

#include "fast/renderer3ds/virtual_shadow_map.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace Fast::Renderer3ds {

// Keyframed shadow layouts for a light that moves in every frame (animated
// time of day). A target holds up to three layouts, each complete for one
// fixed light direction: the two newest are displayed and dissolve into each
// other, a third is constructed at the current light direction under a page
// budget. When the construction is complete and the dissolve has reached the
// newest layout, the layouts rotate. A still light keeps one layout (slot 0
// after a discontinuity); a discontinuity (a new continuity epoch, another
// layout geometry, a turn above the cut angle in one frame) rebuilds one
// layout at once.
//
// The scheduler only decides; the renderer owns the atlases. Each slot keeps
// its own region of the composed atlas, so a rotation moves no page.
inline constexpr int32_t kVirtualShadowKeyframeSlots = 3;

struct VirtualShadowKeyframeSettings {
    // False: one layout only, rebuilt whenever the light has turned
    // SingleLayoutStepDegrees from it (no atlas for three layouts).
    bool Keyframed = true;
    double SingleLayoutStepDegrees = 0.25;
    // A construction starts once the light has turned this far from the
    // newest layout: above float roundoff of a still light, below any motion.
    double MinimumSpacingDegrees = 0.002;
    // Keyframe spacing the construction budget keeps at the light's speed.
    double MaximumSpacingDegrees = 0.5;
    // A turn above this angle in one frame is a discontinuity.
    double CutDegrees = 8.0;
    uint32_t MinimumPageBudget = 16;
    uint32_t MaximumPageBudget = 64;
    // A dissolve lasts this many times the construction of its layout, so a
    // construction that takes slightly longer does not stall the motion.
    double DissolveStretch = 1.25;
    // Static pages whose casters change in every frame take the construction
    // budget again in every frame and can keep a construction from ever
    // completing, which would freeze the displayed light. A construction that
    // has lasted this many times the frames a whole layout needs at the
    // minimum budget completes without a page budget.
    double ConstructionStallFactor = 2.0;
};

// The construction budget of a stalled construction: every pending page.
inline constexpr uint32_t kVirtualShadowUnboundedConstructionBudget = 0xffffffffU;

struct VirtualShadowKeyframeFrame {
    // The newest layout is slot 0, rebuilt at once at the current light.
    bool Cut = false;
    // The constructed layout became the newest this frame.
    bool Rotated = false;
    bool ConstructionStarted = false;
    int32_t Newer = -1;
    // Displayed with weight 1 - NewerWeight while the dissolve runs.
    int32_t Older = -1;
    int32_t Construction = -1;
    uint32_t ConstructionBudget = 0;
    // Frames the construction has lasted, this one included.
    uint32_t ConstructionAge = 0;
    // The construction stalled: ConstructionBudget is unbounded.
    bool ConstructionUnbounded = false;
    float NewerWeight = 1.0F;

    [[nodiscard]] bool OlderDisplayed() const noexcept {
        return Older >= 0 && NewerWeight < 1.0F;
    }
};

class VirtualShadowKeyframes {
  public:
    VirtualShadowKeyframes() noexcept = default;
    explicit VirtualShadowKeyframes(const VirtualShadowKeyframeSettings& settings) noexcept : mSettings(settings) {}

    void Reset() noexcept {
        *this = VirtualShadowKeyframes(mSettings);
    }

    // Angle between two directions in degrees, accurate for small angles.
    [[nodiscard]] static double AngleDegrees(const ShadowVector& a, const ShadowVector& b) noexcept {
        const double ax = a[0], ay = a[1], az = a[2], bx = b[0], by = b[1], bz = b[2];
        const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
        return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), ax * bx + ay * by + az * bz) * 180.0 /
               std::numbers::pi;
    }

    // Plans a frame for the current light. layoutPages: the pages of a
    // complete layout, for the construction budget.
    const VirtualShadowKeyframeFrame& Begin(const ShadowVector& light, uint64_t epoch, uint64_t layoutKey,
                                            uint32_t layoutPages) noexcept {
        auto& frame = mFrame;
        frame.Cut = frame.Rotated = frame.ConstructionStarted = frame.ConstructionUnbounded = false;
        frame.ConstructionBudget = 0;
        frame.ConstructionAge = 0;
        const double step = mHasLight ? AngleDegrees(mLastLight, light) : 0.0;
        if (mHasLight) {
            mSpeed = mSpeedValid ? 0.75 * mSpeed + 0.25 * step : step;
            mSpeedValid = true;
        }
        mLastLight = light;
        mHasLight = true;
        const bool discontinuity =
            mNewer < 0 || epoch != mEpoch || layoutKey != mLayoutKey || step > mSettings.CutDegrees ||
            (!mSettings.Keyframed && AngleDegrees(mDirections[mNewer], light) >= mSettings.SingleLayoutStepDegrees);
        mEpoch = epoch;
        mLayoutKey = layoutKey;
        if (discontinuity) {
            mNewer = 0;
            mOlder = mConstruction = -1;
            mDirections[0] = light;
            mSpeed = 0.0;
            mSpeedValid = false;
            frame.Cut = true;
            return Publish();
        }
        if (mOlder >= 0 && ++mDissolveElapsed >= mDissolveFrames)
            mOlder = -1;
        if (mSettings.Keyframed && mConstruction < 0 &&
            AngleDegrees(mDirections[mNewer], light) >= mSettings.MinimumSpacingDegrees) {
            for (int32_t slot = 0; slot < kVirtualShadowKeyframeSlots; ++slot)
                if (slot != mNewer && slot != mOlder) {
                    mConstruction = slot;
                    break;
                }
            mDirections[mConstruction] = light;
            mConstructionFrames = 0;
            mConstructionWork = 0;
            frame.ConstructionStarted = true;
        }
        if (mConstruction >= 0) {
            ++mConstructionFrames;
            // Spacing = speed x stretch x pages / budget.
            const double needed = std::ceil(double(std::max<uint32_t>(layoutPages, 1U)) * mSpeed *
                                            mSettings.DissolveStretch / mSettings.MaximumSpacingDegrees);
            frame.ConstructionBudget = static_cast<uint32_t>(
                std::clamp(needed, double(mSettings.MinimumPageBudget), double(mSettings.MaximumPageBudget)));
            frame.ConstructionAge = mConstructionFrames;
            const double minimumBudgetFrames = std::ceil(double(std::max<uint32_t>(layoutPages, 1U)) /
                                                         double(std::max<uint32_t>(mSettings.MinimumPageBudget, 1U)));
            // Only when the layouts can rotate this frame: a construction
            // that waits for the dissolve keeps its budget.
            mStallFrames = static_cast<uint32_t>(
                std::clamp(mSettings.ConstructionStallFactor * minimumBudgetFrames, 0.0, 1.0e9)) + 1U;
            if (mOlder < 0 &&
                double(mConstructionFrames) > mSettings.ConstructionStallFactor * minimumBudgetFrames) {
                frame.ConstructionBudget = kVirtualShadowUnboundedConstructionBudget;
                frame.ConstructionUnbounded = true;
            }
        }
        return Publish();
    }

    // Reports the construction recorded this frame: the pages it still lacks.
    const VirtualShadowKeyframeFrame& EndConstruction(uint32_t pendingPages) noexcept {
        if (mConstruction < 0)
            return mFrame;
        if (pendingPages != 0)
            return Publish();
        // Construction work never exceeds the stall bound: frames spent
        // waiting for a dissolve, or starved by changing static pages, are
        // not construction work, and the next dissolve must not grow by them.
        if (mConstructionWork == 0)
            mConstructionWork = mStallFrames != 0 ? std::min(mConstructionFrames, mStallFrames) : mConstructionFrames;
        // The dissolve toward the newest layout has not finished: wait.
        if (mOlder >= 0)
            return Publish();
        mOlder = mNewer;
        mNewer = mConstruction;
        mConstruction = -1;
        // A whole number of frames: every frame of the dissolve advances the
        // displayed shadow by the same amount.
        mDissolveElapsed = 0;
        mDissolveFrames = static_cast<uint32_t>(
            std::max(1.0, std::ceil(mSettings.DissolveStretch * double(mConstructionWork))));
        mFrame.Rotated = true;
        return Publish();
    }

    [[nodiscard]] const ShadowVector& Direction(int32_t slot) const noexcept {
        return mDirections[static_cast<size_t>(slot)];
    }
    [[nodiscard]] const VirtualShadowKeyframeFrame& Frame() const noexcept {
        return mFrame;
    }
    [[nodiscard]] bool Keyframed() const noexcept {
        return mSettings.Keyframed;
    }
    // Light speed in degrees per frame, smoothed.
    [[nodiscard]] double Speed() const noexcept {
        return mSpeed;
    }

  private:
    const VirtualShadowKeyframeFrame& Publish() noexcept {
        mFrame.Newer = mNewer;
        mFrame.Older = mOlder;
        mFrame.Construction = mConstruction;
        mFrame.NewerWeight =
            mOlder >= 0 ? static_cast<float>(double(mDissolveElapsed) / double(mDissolveFrames)) : 1.0F;
        return mFrame;
    }

    VirtualShadowKeyframeSettings mSettings;
    VirtualShadowKeyframeFrame mFrame;
    std::array<ShadowVector, kVirtualShadowKeyframeSlots> mDirections{};
    int32_t mNewer = -1;
    int32_t mOlder = -1;
    int32_t mConstruction = -1;
    uint64_t mEpoch = 0;
    uint64_t mLayoutKey = 0;
    uint32_t mDissolveElapsed = 0;
    uint32_t mDissolveFrames = 1;
    double mSpeed = 0.0;
    bool mSpeedValid = false;
    ShadowVector mLastLight{};
    bool mHasLight = false;
    uint32_t mConstructionFrames = 0;
    uint32_t mConstructionWork = 0;
    uint32_t mStallFrames = 0;
};

} // namespace Fast::Renderer3ds
