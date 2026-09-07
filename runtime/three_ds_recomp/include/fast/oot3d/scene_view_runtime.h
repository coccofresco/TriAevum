#pragma once

#include "fast/renderer3ds/pica_scene_payloads.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace Fast::Oot3d {

using PerspectiveViewState =
    ::Fast::Renderer3ds::PicaPerspectiveCameraState;

// Thread-safe hand-off between the guest camera/frustum hook and renderer
// passes. It deliberately contains no Vulkan state.
class SceneViewRuntime final {
  public:
    static SceneViewRuntime& Instance();
    void PublishPerspective(uint32_t guestFunction, uint32_t guestReturnAddress, float left, float right, float bottom,
                            float top, float nearPlane, float farPlane);
    void PublishPerspectiveCamera(uint32_t guestFunction, uint32_t guestReturnAddress, float left, float right,
                                  float bottom, float top, float nearPlane, float farPlane,
                                  const std::array<float, 3>& eye, const std::array<float, 3>& at);
    void PublishCamera(const std::array<float, 3>& eye, const std::array<float, 3>& at);
    [[nodiscard]] std::optional<PerspectiveViewState> LatestPerspective() const;
    void Reset();

  private:
    mutable std::mutex mMutex;
    uint64_t mNextSerial = 1;
    std::optional<PerspectiveViewState> mLatest;
};

} // namespace Fast::Oot3d
