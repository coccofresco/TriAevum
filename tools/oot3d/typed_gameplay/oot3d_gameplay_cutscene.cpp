#include "oot3d_gameplay_cutscene.h"

#include <cmath>

namespace oot3d::gameplay {
namespace {

double LogicalSubframe(double logicalFrame) noexcept {
  if (!std::isfinite(logicalFrame)) {
    return 0.0;
  }
  return logicalFrame - std::floor(logicalFrame);
}

} // namespace

CutsceneNormalFrameAdvance AdvanceCutsceneNormalFrame(
    std::uint16_t &legacyFrame, const TimeContext &time) noexcept {
  const std::uint16_t previousLegacyFrame = legacyFrame;
  CutsceneNormalFrameAdvance result;
  result.Cursor.PreviousFrame =
      static_cast<double>(previousLegacyFrame) +
      LogicalSubframe(time.PreviousLogicalFrame);

  if (time.CrossedLogicalFrame) {
    legacyFrame = static_cast<std::uint16_t>(legacyFrame + 1U);
    result.Cursor.CurrentFrame =
        static_cast<double>(previousLegacyFrame) + 1.0;
    result.Cursor.CrossedLegacyFrame = true;
    result.DispatchDiscreteCommands = true;
  } else {
    result.Cursor.CurrentFrame =
        static_cast<double>(previousLegacyFrame) +
        LogicalSubframe(time.CurrentLogicalFrame);
  }

  result.Cursor.LegacyVisibleFrame = legacyFrame;
  return result;
}

double ResolveCutsceneContinuousFrame(std::uint16_t legacyFrame,
                                      const TimeContext &time) noexcept {
  return static_cast<double>(legacyFrame) +
         LogicalSubframe(time.CurrentLogicalFrame);
}

float InterpolateCutsceneCueWeight(std::uint16_t startFrame,
                                   std::uint16_t endFrame,
                                   double currentFrame) noexcept {
  const std::int32_t duration = static_cast<std::int32_t>(endFrame) -
                                static_cast<std::int32_t>(startFrame);
  if (duration == 0) {
    return 1.0F;
  }

  const float current = static_cast<float>(currentFrame);
  const float remaining = static_cast<float>(endFrame) - current;
  const float weight =
      1.0F - remaining / static_cast<float>(duration);
  return weight < 1.0F ? weight : 1.0F;
}

} // namespace oot3d::gameplay
