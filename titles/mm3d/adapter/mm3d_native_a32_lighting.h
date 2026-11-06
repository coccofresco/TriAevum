#pragma once

#include "oot3d_native_a32_lighting.h"
#include "mm3d_scene_environment.h"

namespace Oot3dNativeGame {

// v1.1, code.bin SHA256 6eec27b1...; accepted writes and native selection,
// mirroring NativeA32LightingProbe. No guest selection is reproduced here.
inline constexpr std::array kMm3dNativeLightingHookPcs{
    0x0040650CU, 0x004648F4U, 0x001BFB40U, 0x001EFC7CU,
    0x001E7244U, 0x001EEAA8U, 0x001EED64U, 0x001FD5E8U,
    0x001F2294U, 0x0015E9ECU};

[[nodiscard]] Fast::Renderer3ds::PicaSceneWaterBodies CaptureMm3dWaterBodies(
    uint32_t play, oot3d::recomp::a32::MemoryBus& memory);

class Mm3dNativeA32LightingProbe {
  public:
    // Supplemental payload isolation switches. Each point still needs title
    // source proof and per-draw native-slot replacement admission.
    void SetSupplementalPublication(bool points, bool water) noexcept {
        mPublishPoints = points; mPublishWater = water;
    }
    void ObserveBlockEntry(uint32_t pc, const oot3d::recomp::a32::GuestState& state,
                           oot3d::recomp::a32::MemoryBus& memory);
    [[nodiscard]] Fast::Renderer3ds::PicaNativeLightBinding CaptureRenderer(
        uint32_t renderer, oot3d::recomp::a32::MemoryBus& memory) const;
    [[nodiscard]] static std::optional<std::array<float, 16>> CaptureWorldToView(
        oot3d::recomp::a32::MemoryBus& memory);
    void CaptureSceneLights(uint32_t play, oot3d::recomp::a32::MemoryBus& memory);
    void Reset() noexcept;
    // Once per mounted image: the environment-group writers are where the
    // layout says (mm3d_scene_environment.h); otherwise the group stays Unknown.
    bool VerifyEnvironmentGroupGuards(oot3d::recomp::a32::MemoryBus& memory);
    // The visit's latched group, for the scene environment of this play state.
    [[nodiscard]] std::optional<SceneEnvironmentGroup> EnvironmentGroup(uint32_t play, uint16_t scene) const noexcept {
        return mGroup.Current(play, scene);
    }

  private:
    struct BoundLights {
        std::array<Fast::Renderer3ds::PicaNativeLightSource, 10> Sources{};
    };
    void ObserveAcceptedLight(uint32_t owner, uint32_t params, uint32_t record,
                              bool point, oot3d::recomp::a32::MemoryBus& memory);
    void ContinueSceneLights(Fast::Renderer3ds::PicaSceneDirectionalLights& lights,
                             uint32_t play, const std::optional<NativeEnvironmentState>& environment);
    std::unordered_map<uint32_t, BoundLights> mBoundLights;
    std::unordered_map<uint32_t, Fast::Renderer3ds::PicaNativeLightBinding> mRenderStates;
    // The upload body retains the owner in neither r7 nor r6 at assignment.
    std::unordered_map<uint32_t, uint32_t> mUploadOwners;
    std::shared_ptr<const Fast::Renderer3ds::PicaScenePointLights> mScenePoints;
    std::shared_ptr<const Fast::Renderer3ds::PicaSceneDirectionalLights> mSceneDirectionals;
    std::shared_ptr<const Fast::Renderer3ds::PicaSceneWaterBodies> mWater;
    // Function of each light still linked in this frame's LightContext,
    // by source identity (LightInfo + 4); absent = Unknown, Local.
    struct LightFunction {
        Fast::Renderer3ds::PicaNativeLightOrigin Origin = Fast::Renderer3ds::PicaNativeLightOrigin::Unknown;
        Fast::Renderer3ds::PicaNativeLightScope Scope = Fast::Renderer3ds::PicaNativeLightScope::Local;
    };
    std::unordered_map<uint64_t, LightFunction> mLightFunctions;
    Mm3dEnvironmentGroupLatch mGroup;
    uint64_t mSceneLightFrames = 0;
    uint32_t mSnapshotPlay = 0, mSnapshotFrame = 0;
    bool mHaveSnapshot = false;
    bool mPublishPoints = true, mPublishWater = true;
    struct ContinuityLight {
        uint64_t Identity = 0;
        std::array<float, 3> Direction{};
        std::array<Fast::Renderer3ds::PicaSteppedSignal, 6> Signals{};
    };
    uint32_t mContinuityPlayState = 0;
    std::optional<NativeEnvironmentState> mContinuityEnvironment;
    std::array<ContinuityLight, Fast::Renderer3ds::kMaximumPicaSceneDirectionalLights> mContinuityLights{};
    uint32_t mContinuityCount = 0;
    bool mContinuityTimeDriven = false;
    uint64_t mContinuityEpoch = 0;
    Fast::Renderer3ds::PicaSceneShadowCasterTracker mShadowCaster;
};
} // namespace Oot3dNativeGame
