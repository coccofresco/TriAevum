#include "fast/oot3d/grass_motion.h"

#include <cmath>

namespace Fast::Oot3d {

bool GrassMotionHistoryMatches(uint64_t previousFrame,uint64_t currentFrame,
                               size_t previousCount,size_t currentCount) {
    return previousFrame+1U==currentFrame&&previousCount==currentCount;
}

std::array<float,2> ComputeGrassMotionUv(
    const std::array<float,4>& current,const std::array<float,4>& previous,
    bool historyValid) {
    if(!historyValid||!std::isfinite(current[3])||!std::isfinite(previous[3])||
       current[3]<=1.0e-7F||previous[3]<=1.0e-7F)return {};
    return {previous[0]/previous[3]*0.5F-current[0]/current[3]*0.5F,
            previous[1]/previous[3]*0.5F-current[1]/current[3]*0.5F};
}

} // namespace Fast::Oot3d
