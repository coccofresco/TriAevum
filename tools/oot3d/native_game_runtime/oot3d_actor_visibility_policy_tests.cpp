#include "oot3d_actor_visibility_policy.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Oot3dNativeGame::ActorCullingSample Actor(float x, float y, float z, float w) {
    Oot3dNativeGame::ActorCullingSample sample;
    sample.X = x;
    sample.Y = y;
    sample.Z = z;
    sample.W = w;
    sample.Forward = 1000.0F;
    sample.Scale = 50.0F;
    sample.Downward = 100.0F;
    return sample;
}

} // namespace

int main() {
    using namespace Oot3dNativeGame;
    try {
        const ActorCullingVolume native;
        // Depth: strict inequalities at -scale and forward + scale.
        Expect(ActorInsideNativeCullingVolume(Actor(0, 0, 500, 500)), "a centred actor is visible");
        Expect(!ActorInsideNativeCullingVolume(Actor(0, 0, 1050, 1050)), "forward + scale is outside");
        Expect(ActorInsideNativeCullingVolume(Actor(0, 0, 1049, 1049)), "just inside forward + scale");
        Expect(!ActorInsideNativeCullingVolume(Actor(0, 0, -50, 1)), "-scale is outside");
        // W under 1 divides by 1.
        Expect(ActorInsideNativeCullingVolume(Actor(50.5F, 0, 10, 0.25F)), "W below 1 does not magnify");
        Expect(!ActorInsideNativeCullingVolume(Actor(51.0F, 0, 10, 0.25F)), "lateral bound with W below 1");
        // Lateral and vertical bounds grow by the scale.
        Expect(ActorInsideNativeCullingVolume(Actor(549, 0, 500, 500)), "inside the grown lateral bound");
        Expect(!ActorInsideNativeCullingVolume(Actor(550, 0, 500, 500)), "outside the lateral bound");
        Expect(!ActorInsideNativeCullingVolume(Actor(0, -600, 500, 500)), "below the vertical bound");
        Expect(!ActorInsideNativeCullingVolume(Actor(0, 550, 500, 500)), "above the vertical bound");

        // The extended volume without extensions is the native one.
        for (const auto& sample : {Actor(0, 0, 1049, 1049), Actor(0, 0, 1050, 1050), Actor(549, 0, 500, 500),
                                   Actor(550, 0, 500, 500), Actor(0, -600, 500, 500), Actor(0, 0, -50, 1)}) {
            Expect(ActorInsideExtendedCullingVolume(sample, native) == ActorInsideNativeCullingVolume(sample),
                   "an identity volume must match the native test");
        }

        // Presentation factors reveal what a wider view shows, nothing else.
        ActorCullingVolume wide;
        wide.Horizontal = 1.2F;
        wide.Vertical = 1.1F;
        Expect(ActorInsideExtendedCullingVolume(Actor(640, 0, 500, 500), wide), "16:9 margin is visible");
        Expect(!ActorInsideExtendedCullingVolume(Actor(660, 0, 500, 500), wide), "beyond the presented width");
        Expect(ActorInsideExtendedCullingVolume(Actor(0, 590, 500, 500), wide), "taller view reveals more");
        Expect(!ActorInsideExtendedCullingVolume(Actor(0, 0, 1050, 1050), wide), "aspect does not extend depth");

        // Distance: forward + scale times 5, with SoH's +1000 under 500.
        ActorCullingVolume far;
        far.DistanceMultiplier = kExtendedActorDistanceMultiplier;
        Expect(ActorInsideExtendedCullingVolume(Actor(0, 0, 5049, 5049), far), "5x forward");
        Expect(!ActorInsideExtendedCullingVolume(Actor(0, 0, 5050, 5050), far), "beyond 5x forward");
        auto shortRange = Actor(0, 0, 7544, 7544);
        shortRange.Forward = 500.0F - 1.0F;
        Expect(ActorInsideExtendedCullingVolume(shortRange, far), "short forward gains 1000 first");
        shortRange.Z = 7545.0F;
        Expect(!ActorInsideExtendedCullingVolume(shortRange, far), "beyond the short-forward extension");
        Expect(!ActorInsideExtendedCullingVolume(Actor(0, 0, -50, 1), far), "behind the camera stays culled");

        // Invalid factors never shrink the volume; non-finite samples are culled.
        ActorCullingVolume invalid;
        invalid.DistanceMultiplier = 0.5F;
        invalid.Horizontal = std::numeric_limits<float>::quiet_NaN();
        invalid.Vertical = -2.0F;
        Expect(ActorInsideExtendedCullingVolume(Actor(549, 0, 1049, 1049), invalid), "factors below 1 clamp");
        Expect(!ActorInsideExtendedCullingVolume(Actor(std::numeric_limits<float>::infinity(), 0, 10, 10), far),
               "a non-finite position is culled");

        // Failures keep the title's states: far first, then behind or aside.
        Expect(ClassifyNativeActorCulling(Actor(0, 0, 2000, 2000)) == ActorCullingClass::Far, "native far");
        Expect(ClassifyNativeActorCulling(Actor(5000, 0, 2000, 2000)) == ActorCullingClass::Far,
               "far wins over aside, as in the title");
        Expect(ClassifyNativeActorCulling(Actor(0, 0, -60, 1)) == ActorCullingClass::Outside, "behind");
        // Within the extended depth but aside: no longer far, so no far fade.
        Expect(ClassifyExtendedActorCulling(Actor(5000, 0, 2000, 2000), far) == ActorCullingClass::Outside,
               "an extended depth reclassifies a far actor aside");
        Expect(ClassifyExtendedActorCulling(Actor(0, 0, 6000, 6000), far) == ActorCullingClass::Far,
               "beyond the extended depth stays far");

        // A burst can consume every remaining reservation this epoch. No
        // previous-frame average appears in the admission calculation.
        for (const bool overflow : {false, true}) {
            const uint32_t capacity = kActorLightBudget + (overflow ? kActorLightOverflowRecords : 0U);
            for (uint32_t used = 0; used <= capacity + 30; used += 7)
                for (uint32_t remaining = 0; remaining <= 300; ++remaining)
                    Expect(ActorLightBudgetAllows(used, remaining, overflow) ==
                               (used + remaining + kActorLightAuxiliaryReserve <= capacity),
                           "exact reservation boundary");
            Expect(!ActorLightBudgetAllows(UINT32_MAX, 1, overflow), "used overflow fails closed");
            Expect(!ActorLightBudgetAllows(1, UINT32_MAX, overflow), "reservation overflow fails closed");
        }
        Expect(NativeCulledActorFade(ActorCullingClass::Far, 10) == 9, "native fade decrements");
        Expect(NativeCulledActorFade(ActorCullingClass::Far, 1) == 0, "last native fade expires");
        Expect(NativeCulledActorFade(ActorCullingClass::Outside, 10) == 0, "native aside cuts");

        std::array<ActorExtensionCandidate, 40> candidates{};
        for (size_t i = 0; i < candidates.size(); ++i)
            candidates[i] = {i + 1, static_cast<float>(i + 1), false, false};
        candidates.back().Retained = true;
        SelectActorExtensions(candidates);
        Expect(candidates.back().Selected && candidates[30].Selected && !candidates[31].Selected,
               "incumbents then nearer newcomers");
        std::reverse(candidates.begin(), candidates.end());
        SelectActorExtensions(candidates);
        for (const auto& c : candidates)
            Expect(c.Selected == (c.Identity <= 31 || c.Identity == 40), "list order does not change rank");
        candidates[0].DistanceSquared = std::numeric_limits<float>::quiet_NaN();
        SelectActorExtensions(candidates);
        Expect(!candidates[0].Selected, "NaN incumbent cannot monopolize the budget");

        // Obj_Mure2's radial scalar.
        Expect(ObjMure2DistanceScalar(1.0F, 300.0F, 400.0F, 1.0F, false) == 1.0F, "native scalar kept");
        Expect(ObjMure2DistanceScalar(4.0F, 0.0F, 0.0F, 1.0F, true) == 25.0F, "extended is at least 5x squared");
        Expect(ObjMure2DistanceScalar(100.0F, 0.0F, 0.0F, 1.0F, true) == 100.0F, "a larger native scalar stays");
        Expect(ObjMure2DistanceScalar(1.0F, 0.0F, 400.0F, 1.2F, false) == 1.0F, "straight ahead is unchanged");
        // X = 600, Z = 0, H = 1.2: the presented X is 500, so 600^2 / 500^2.
        const float aside = ObjMure2DistanceScalar(1.0F, 600.0F, 0.0F, 1.2F, false);
        Expect(aside > 1.4399F && aside < 1.4401F, "aside the test sees the presented width");
        Expect(ObjMure2DistanceScalar(1.0F, 0.0F, 0.0F, 1.2F, false) == 1.0F, "a zero position is guarded");
    } catch (const std::exception& error) {
        std::cerr << "oot3d_actor_visibility_policy_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "oot3d_actor_visibility_policy_tests: ok\n";
    return 0;
}
