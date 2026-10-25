#pragma once

#include <algorithm>
#include <cstdint>
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
// a mouse look. A finger keeps the role it landed with until it lifts: one
// that landed on a menu or a page never turns into a drag when gameplay
// resumes under it, and a drag stops when gameplay pauses.
class TouchCameraTracker {
  public:
    struct Sample {
        float DeltaX = 0.0F;
        float DeltaY = 0.0F;
        bool Active = false;
    };

    Sample Update(bool gameplay, std::span<const SurfaceFinger> fingers) {
        Sample sample;
        for (auto& tracked : mFingers) tracked.Seen = false;
        for (const auto& finger : fingers) {
            auto found = std::find_if(mFingers.begin(), mFingers.end(), [&](const Tracked& tracked) {
                return tracked.Device == finger.Device && tracked.Id == finger.Id;
            });
            if (found == mFingers.end()) {
                mFingers.push_back({finger.Device, finger.Id, gameplay, finger.X, finger.Y, true});
                continue;
            }
            found->Seen = true;
            found->Drags = found->Drags && gameplay;
            if (found->Drags) {
                sample.DeltaX += finger.X - found->X;
                sample.DeltaY += finger.Y - found->Y;
                sample.Active = true;
            }
            found->X = finger.X;
            found->Y = finger.Y;
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
    };
    std::vector<Tracked> mFingers;
};

} // namespace ThreeDsRecomp::Input
