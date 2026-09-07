#include "oot3d_native_root_motion.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void RunPlayerAnimationCatalogTests();
void RunPlayerClipRuntimeTests();
void RunNativePlayerActionRuntimeTests();
void RunNativePlayerCollisionTests();
void RunNativeSkelAnimeTests();
void RunNativeFrameRateTests();
void RunNativeA32TimingProbeTests();
void RunNativeA32SceneViewProbeTests();
void RunNativeTemporalEventLedgerTests();
void RunNativePlayerTemporalBridgeTests();

namespace {

using Oot3dNativeGame::ActorTransform;
using Oot3dNativeGame::AnimTransform;
using Oot3dNativeGame::RootMotionState;
using Oot3dNativeGame::Vec3f;

void ExpectNear(float actual, float expected, const std::string& label) {
    if (std::abs(actual - expected) > 0.0001f) {
        throw std::runtime_error(label + " expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

void ExpectVec(const Vec3f& actual, const Vec3f& expected, const std::string& label) {
    ExpectNear(actual.X, expected.X, label + ".x");
    ExpectNear(actual.Y, expected.Y, label + ".y");
    ExpectNear(actual.Z, expected.Z, label + ".z");
}

AnimTransform RootAt(float x, float y, float z) {
    AnimTransform transform;
    transform.Rows[0][0] = 1.0f;
    transform.Rows[1][1] = 1.0f;
    transform.Rows[2][2] = 1.0f;
    transform.Rows[0][3] = x;
    transform.Rows[1][3] = y;
    transform.Rows[2][3] = z;
    return transform;
}

void TestPlanarDeltaAndBaseRestore() {
    RootMotionState state;
    state.PreviousTranslation = { 10.0f, 2.0f, -4.0f };
    state.BaseTranslation = { 3.0f, 8.0f, 5.0f };
    std::vector<AnimTransform> joints = { RootAt(12.0f, 7.0f, 1.0f) };

    const auto delta = Oot3dNativeGame::ExtractRootMotion(state, joints, 0);
    ExpectVec(delta, { 2.0f, 0.0f, 5.0f }, "planar delta");
    ExpectVec(state.PreviousTranslation, { 12.0f, 7.0f, 1.0f }, "previous translation");
    ExpectNear(joints[0].Rows[0][3], 3.0f, "restored x");
    ExpectNear(joints[0].Rows[1][3], 7.0f, "preserved y");
    ExpectNear(joints[0].Rows[2][3], 5.0f, "restored z");
}

void TestWorldRotationAndActorScale() {
    RootMotionState state;
    state.MovementFlags = Oot3dNativeGame::kRootMotionUpdateY;
    state.PreviousTranslation = {};
    state.BaseTranslation = { 0.0f, 4.0f, 0.0f };
    std::vector<AnimTransform> joints = { RootAt(2.0f, 3.0f, 0.0f) };
    ActorTransform actor;
    actor.Position = { 10.0f, 20.0f, 30.0f };
    actor.Scale = { 2.0f, 3.0f, 4.0f };

    const auto delta = Oot3dNativeGame::ApplyRootMotionToActor(
        state, joints, 0x4000, 0.5f, actor);
    ExpectVec(delta, { 0.0f, 3.0f, -2.0f }, "rotated delta");
    ExpectVec(actor.Position, { 10.0f, 24.5f, 22.0f }, "scaled actor position");
    ExpectNear(joints[0].Rows[1][3], 4.0f, "restored y");
}

void TestPreviousRotationAndOneShotSuppression() {
    RootMotionState state;
    state.MovementFlags = static_cast<uint8_t>(
        Oot3dNativeGame::kRootMotionUpdateY | Oot3dNativeGame::kRootMotionNoMoveOnce);
    state.PreviousRotation = 0x4000;
    state.PreviousTranslation = { 7.0f, 8.0f, 9.0f };
    state.BaseTranslation = { 1.0f, 2.0f, 3.0f };
    std::vector<AnimTransform> joints = { RootAt(11.0f, 12.0f, 13.0f) };

    const auto suppressed = Oot3dNativeGame::ExtractRootMotion(state, joints, -0x4000);
    ExpectVec(suppressed, {}, "suppressed delta");
    if ((state.MovementFlags & Oot3dNativeGame::kRootMotionNoMoveOnce) != 0) {
        throw std::runtime_error("one-shot no-move flag was not consumed");
    }
    ExpectVec(state.PreviousTranslation, { 11.0f, 12.0f, 13.0f },
              "suppressed previous translation");

    joints[0] = RootAt(11.0f, 12.0f, 13.0f);
    const auto next = Oot3dNativeGame::ExtractRootMotion(state, joints, -0x4000);
    ExpectVec(next, {}, "post-suppression stable delta");
}

void TestInvalidSpecialLimbRejected() {
    RootMotionState state;
    state.SpecialLimb = 2;
    std::vector<AnimTransform> joints(1);
    try {
        (void)Oot3dNativeGame::ExtractRootMotion(state, joints, 0);
    } catch (const std::out_of_range&) {
        return;
    }
    throw std::runtime_error("invalid special limb was accepted");
}

} // namespace

int main() {
    try {
        RunPlayerAnimationCatalogTests();
        RunPlayerClipRuntimeTests();
        RunNativePlayerActionRuntimeTests();
        RunNativePlayerCollisionTests();
        RunNativeSkelAnimeTests();
        RunNativeFrameRateTests();
        RunNativeA32SceneViewProbeTests();
        RunNativeA32TimingProbeTests();
        RunNativeTemporalEventLedgerTests();
        RunNativePlayerTemporalBridgeTests();
        TestPlanarDeltaAndBaseRestore();
        TestWorldRotationAndActorScale();
        TestPreviousRotationAndOneShotSuppression();
        TestInvalidSpecialLimbRejected();
        std::cout << "oot3d native root-motion tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "oot3d native root-motion tests failed: " << ex.what() << '\n';
        return 1;
    }
}
