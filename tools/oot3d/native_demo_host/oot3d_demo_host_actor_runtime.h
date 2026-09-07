#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace ThreeDsRecomp::Oot3d {
struct Oot3dNativeDemoScene;
struct Oot3dNativeDemoRenderScene;
} // namespace ThreeDsRecomp::Oot3d

struct Oot3dDemoHostActorFrameContext {
    bool PlayerActorPositionValid = false;
    double PlayerActorX = 0.0;
    double PlayerActorY = 0.0;
    double PlayerActorZ = 0.0;
    bool ViewEyeValid = false;
    double ViewEyeX = 0.0;
    double ViewEyeY = 0.0;
    double ViewEyeZ = 0.0;
    double ViewTargetX = 0.0;
    double ViewTargetY = 0.0;
    double ViewTargetZ = -1.0;
    double ViewUpX = 0.0;
    double ViewUpY = 1.0;
    double ViewUpZ = 0.0;
    bool UseViewEyeForTransitionActors = false;
    bool NativeSpecialTransitionBypass = false;
};

class Oot3dDemoHostActorRuntime {
  public:
    virtual ~Oot3dDemoHostActorRuntime() = default;

    virtual void Initialize(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                            ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) = 0;
    virtual void SetFrameContext(const Oot3dDemoHostActorFrameContext& context) {
        (void)context;
    }
    virtual void Update(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                        double deltaSeconds) = 0;
    virtual bool MixAudio(uint32_t sampleRate, size_t frameCount,
                          std::vector<int16_t>& stereoSamples) {
        (void)sampleRate;
        (void)frameCount;
        stereoSamples.clear();
        return false;
    }
    virtual nlohmann::json Diagnostics() const = 0;
};
