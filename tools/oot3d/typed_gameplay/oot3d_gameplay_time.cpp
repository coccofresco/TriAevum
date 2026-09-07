#include "oot3d_gameplay_time.h"

#include <stdexcept>

namespace oot3d::gameplay {

TimeStep ResolveTimeStep(std::uint32_t simulationRateHz) {
  if (simulationRateHz == 0U) {
    throw std::invalid_argument("OOT3D gameplay rate must be positive");
  }
  const double seconds = 1.0 / static_cast<double>(simulationRateHz);
  return {
      seconds,
      static_cast<float>(seconds * kNativeTimeUnitsPerSecond),
  };
}

} // namespace oot3d::gameplay
