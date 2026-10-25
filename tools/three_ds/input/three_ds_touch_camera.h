#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ThreeDsRecomp::Input {

// One finger on the game surface, in window units.
struct SurfaceFinger {
    std::int64_t Device = 0;
    std::int64_t Id = 0;
    float X = 0.0F;
    float Y = 0.0F;
};

// Fingers that land on the game surface during gameplay drag the camera, like
// a mouse look, once they have moved past `slop` (window units) from where
// they landed. A finger keeps the role it landed with until it lifts: one
// that landed on a menu or a page never turns into a drag when gameplay
// resumes under it, and a drag stops when gameplay pauses. Two quick taps
// (each lifted within kTapSeconds without moving past the slop, the second
// within kDoubleTapSeconds of the first and near it) are a double tap.
class TouchCameraTracker {
  public:
    static constexpr double kTapSeconds = 0.25;
    static constexpr double kDoubleTapSeconds = 0.35;

    struct Sample {
        float DeltaX = 0.0F;
        float DeltaY = 0.0F;
        bool Active = false;
        bool DoubleTap = false;
    };

    Sample Update(bool gameplay, std::span<const SurfaceFinger> fingers, double nowSeconds = 0.0,
                  float slop = 0.0F) {
        Sample sample;
        for (auto& tracked : mFingers) tracked.Seen = false;
        for (const auto& finger : fingers) {
            auto found = std::find_if(mFingers.begin(), mFingers.end(), [&](const Tracked& tracked) {
                return tracked.Device == finger.Device && tracked.Id == finger.Id;
            });
            if (found == mFingers.end()) {
                mFingers.push_back({finger.Device, finger.Id, gameplay, finger.X, finger.Y, true, finger.X, finger.Y,
                                    nowSeconds, false});
                continue;
            }
            found->Seen = true;
            found->Drags = found->Drags && gameplay;
            if (!found->Moved && std::hypot(finger.X - found->StartX, finger.Y - found->StartY) > slop)
                found->Moved = true;
            if (found->Drags && found->Moved) {
                sample.DeltaX += finger.X - found->X;
                sample.DeltaY += finger.Y - found->Y;
                sample.Active = true;
            }
            found->X = finger.X;
            found->Y = finger.Y;
        }
        for (const auto& tracked : mFingers) {
            if (tracked.Seen || !tracked.Drags || tracked.Moved || nowSeconds - tracked.StartTime > kTapSeconds)
                continue;
            if (mLastTap && nowSeconds - mLastTap->Time <= kDoubleTapSeconds &&
                std::hypot(tracked.X - mLastTap->X, tracked.Y - mLastTap->Y) <= std::max(slop * 3.0F, 1.0F)) {
                sample.DoubleTap = true;
                mLastTap.reset();
            } else {
                mLastTap = Tap{tracked.X, tracked.Y, nowSeconds};
            }
        }
        std::erase_if(mFingers, [](const Tracked& tracked) { return !tracked.Seen; });
        return sample;
    }

  private:
    struct Tracked {
        std::int64_t Device = 0;
        std::int64_t Id = 0;
        bool Drags = false;
        float X = 0.0F;
        float Y = 0.0F;
        bool Seen = true;
        float StartX = 0.0F;
        float StartY = 0.0F;
        double StartTime = 0.0;
        bool Moved = false;
    };
    struct Tap {
        float X = 0.0F;
        float Y = 0.0F;
        double Time = 0.0;
    };
    std::vector<Tracked> mFingers;
    std::optional<Tap> mLastTap;
};

} // namespace ThreeDsRecomp::Input
