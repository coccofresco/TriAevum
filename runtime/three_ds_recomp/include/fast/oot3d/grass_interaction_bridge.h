#pragma once

#include "fast/oot3d/grass_types.h"
#include "fast/renderer3ds/pica_frame_timing.h"

#include <mutex>
#include <optional>
#include <deque>
#include <span>
#include <vector>

namespace Fast::Oot3d {

class GrassInteractionBridge final {
  public:
    static GrassInteractionBridge& Instance();
    void PublishLink(const std::array<float, 3>& position, float radius,
                     float halfHeight, uint64_t frameId);
    void PublishLink(const std::array<float, 3>& position,
                     const std::array<float, 3>& velocity, float radius,
                     float halfHeight, uint64_t frameId, uint64_t stableId,
                     uint64_t contextId);
    [[nodiscard]] std::optional<GrassInteractor> LatestLink() const;
    // Complete native-frame snapshots, selected on the same clock as geometry.
    void PublishFrame(uint64_t sourceFrame, std::span<const GrassInteractor> actors);
    void SelectSample(const Renderer3ds::PicaFrameTemporalSample& sample);
    [[nodiscard]] std::vector<GrassInteractor> LatestActors() const;
    void Reset();

  private:
    mutable std::mutex mMutex;
    std::optional<GrassInteractor> mLink;
    struct Frame {
        uint64_t Id = 0;
        std::vector<GrassInteractor> Actors;
    };
    std::deque<Frame> mFrames;
    std::vector<GrassInteractor> mActors;
    Renderer3ds::PicaFrameTemporalSample mSample{};
    uint64_t mPresentationId = 0;
};

} // namespace Fast::Oot3d
