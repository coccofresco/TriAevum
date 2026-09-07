#include "oot3d_gameplay_camera_animation.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace oot3d::gameplay {
namespace {

constexpr float kPositionScale = std::bit_cast<float>(0x42200000U);
constexpr float kAngleS16Scale = std::bit_cast<float>(0x4622F983U);
constexpr float kDegreeScale = std::bit_cast<float>(0x42652EE1U);

template <typename T>
bool ReadPoint(std::span<const std::byte> bytes, std::size_t index,
               T *point) noexcept {
  if (point == nullptr ||
      index > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
    return false;
  }
  const std::size_t offset = index * sizeof(T);
  if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
    return false;
  }
  std::memcpy(point, bytes.data() + offset, sizeof(T));
  return true;
}

template <typename T>
bool HasPointBytes(const CameraCurveView &curve) noexcept {
  return curve.PointCount > 0 &&
         static_cast<std::uint64_t>(curve.PointCount) <=
             std::numeric_limits<std::size_t>::max() / sizeof(T) &&
         curve.PointBytes.size() >=
             static_cast<std::size_t>(curve.PointCount) * sizeof(T);
}

CameraCurveSampleStatus SampleLinear(const CameraCurveView &curve, float frame,
                                     float *output) noexcept {
  if (!HasPointBytes<CameraLinearKeyframeWire>(curve)) {
    return curve.PointCount <= 0 ? CameraCurveSampleStatus::EmptyCurve
                                 : CameraCurveSampleStatus::TruncatedPoints;
  }

  CameraLinearKeyframeWire first;
  if (!ReadPoint(curve.PointBytes, 0U, &first)) {
    return CameraCurveSampleStatus::TruncatedPoints;
  }
  if (curve.PointCount == 1) {
    *output = first.Value;
    return CameraCurveSampleStatus::Ok;
  }

  std::size_t nextIndex = 0U;
  CameraLinearKeyframeWire next;
  for (; nextIndex < static_cast<std::size_t>(curve.PointCount); ++nextIndex) {
    if (!ReadPoint(curve.PointBytes, nextIndex, &next)) {
      return CameraCurveSampleStatus::TruncatedPoints;
    }
    if (static_cast<float>(next.Frame) > frame) {
      break;
    }
  }

  if (nextIndex == 0U) {
    *output = first.Value;
    return CameraCurveSampleStatus::Ok;
  }
  if (nextIndex == static_cast<std::size_t>(curve.PointCount)) {
    CameraLinearKeyframeWire last;
    if (!ReadPoint(curve.PointBytes, nextIndex - 1U, &last)) {
      return CameraCurveSampleStatus::TruncatedPoints;
    }
    *output = last.Value;
    return CameraCurveSampleStatus::Ok;
  }

  CameraLinearKeyframeWire previous;
  if (!ReadPoint(curve.PointBytes, nextIndex - 1U, &previous)) {
    return CameraCurveSampleStatus::TruncatedPoints;
  }
  const std::int64_t frameSpan =
      static_cast<std::int64_t>(next.Frame) - previous.Frame;
  if (frameSpan == 0) {
    return CameraCurveSampleStatus::ZeroFrameSpan;
  }
  const float t = (frame - static_cast<float>(previous.Frame)) /
                  static_cast<float>(frameSpan);
  *output = previous.Value + (next.Value - previous.Value) * t;
  return CameraCurveSampleStatus::Ok;
}

float InterpolateHermite(const CameraHermiteKeyframeWire &previous,
                         const CameraHermiteKeyframeWire &next, float frame,
                         float segmentStart, float segmentEnd) noexcept {
  const float frameDelta = frame - segmentStart;
  const float frameSpan = segmentEnd - segmentStart;
  const float t = frameDelta / frameSpan;
  const float valueTerm = (previous.Value - next.Value) * ((2.0F * t) - 3.0F);
  const float tangentBlend =
      ((t - 1.0F) * previous.TangentOut) + (t * next.TangentIn);
  return previous.Value + (valueTerm * t * t) +
         (frameDelta * (t - 1.0F) * tangentBlend);
}

CameraCurveSampleStatus SampleHermite(const CameraCurveView &curve, float frame,
                                      bool loop, float *output) noexcept {
  if (!HasPointBytes<CameraHermiteKeyframeWire>(curve)) {
    return curve.PointCount <= 0 ? CameraCurveSampleStatus::EmptyCurve
                                 : CameraCurveSampleStatus::TruncatedPoints;
  }

  CameraHermiteKeyframeWire first;
  if (!ReadPoint(curve.PointBytes, 0U, &first)) {
    return CameraCurveSampleStatus::TruncatedPoints;
  }
  if (curve.PointCount == 1) {
    *output = first.Value;
    return CameraCurveSampleStatus::Ok;
  }

  if (loop &&
      (frame < 0.0F || static_cast<float>(curve.LoopEndFrame) < frame)) {
    CameraHermiteKeyframeWire last;
    if (!ReadPoint(curve.PointBytes,
                   static_cast<std::size_t>(curve.PointCount - 1), &last)) {
      return CameraCurveSampleStatus::TruncatedPoints;
    }
    const float segmentStart = static_cast<float>(last.Frame);
    const float segmentEnd = static_cast<float>(
        static_cast<std::int64_t>(first.Frame) + curve.LoopEndFrame + 1);
    if (segmentStart == segmentEnd) {
      return CameraCurveSampleStatus::ZeroFrameSpan;
    }
    const float sampleFrame =
        frame < 0.0F
            ? frame + static_cast<float>(
                          static_cast<std::int64_t>(curve.LoopEndFrame) + 1)
            : frame;
    *output =
        InterpolateHermite(last, first, sampleFrame, segmentStart, segmentEnd);
    return CameraCurveSampleStatus::Ok;
  }

  std::size_t nextIndex = 0U;
  CameraHermiteKeyframeWire next;
  for (; nextIndex < static_cast<std::size_t>(curve.PointCount); ++nextIndex) {
    if (!ReadPoint(curve.PointBytes, nextIndex, &next)) {
      return CameraCurveSampleStatus::TruncatedPoints;
    }
    if (static_cast<float>(next.Frame) > frame) {
      break;
    }
  }

  if (nextIndex == 0U) {
    *output = 0.0F;
    return CameraCurveSampleStatus::Ok;
  }
  if (nextIndex == static_cast<std::size_t>(curve.PointCount)) {
    CameraHermiteKeyframeWire last;
    if (!ReadPoint(curve.PointBytes, nextIndex - 1U, &last)) {
      return CameraCurveSampleStatus::TruncatedPoints;
    }
    *output = last.Value;
    return CameraCurveSampleStatus::Ok;
  }

  CameraHermiteKeyframeWire previous;
  if (!ReadPoint(curve.PointBytes, nextIndex - 1U, &previous)) {
    return CameraCurveSampleStatus::TruncatedPoints;
  }
  if (previous.Frame == next.Frame) {
    return CameraCurveSampleStatus::ZeroFrameSpan;
  }
  *output = InterpolateHermite(previous, next, frame,
                               static_cast<float>(previous.Frame),
                               static_cast<float>(next.Frame));
  return CameraCurveSampleStatus::Ok;
}

CameraCurveSampleStatus SampleStep(const CameraCurveView &curve, float frame,
                                   float *output) noexcept {
  if (!HasPointBytes<CameraLinearKeyframeWire>(curve)) {
    return curve.PointCount <= 0 ? CameraCurveSampleStatus::EmptyCurve
                                 : CameraCurveSampleStatus::TruncatedPoints;
  }

  CameraLinearKeyframeWire first;
  if (!ReadPoint(curve.PointBytes, 0U, &first)) {
    return CameraCurveSampleStatus::TruncatedPoints;
  }
  if (curve.PointCount == 1) {
    *output = first.Value;
    return CameraCurveSampleStatus::Ok;
  }

  std::size_t nextIndex = 0U;
  CameraLinearKeyframeWire point;
  for (; nextIndex < static_cast<std::size_t>(curve.PointCount); ++nextIndex) {
    if (!ReadPoint(curve.PointBytes, nextIndex, &point)) {
      return CameraCurveSampleStatus::TruncatedPoints;
    }
    if (static_cast<float>(point.Frame) > frame) {
      break;
    }
  }
  if (nextIndex == 0U) {
    *output = first.Value;
    return CameraCurveSampleStatus::Ok;
  }
  if (!ReadPoint(curve.PointBytes, nextIndex - 1U, &point)) {
    return CameraCurveSampleStatus::TruncatedPoints;
  }
  *output = point.Value;
  return CameraCurveSampleStatus::Ok;
}

double LogicalSubframe(double logicalFrame) noexcept {
  if (!std::isfinite(logicalFrame)) {
    return 0.0;
  }
  return logicalFrame - std::floor(logicalFrame);
}

} // namespace

CameraCurveSampleStatus SampleCameraCurve(const CameraCurveView &curve,
                                          float frame, bool loop,
                                          float *output) noexcept {
  if (output == nullptr) {
    return CameraCurveSampleStatus::NullOutput;
  }
  *output = 0.0F;
  switch (curve.Type) {
  case CameraCurveType::Linear:
    return SampleLinear(curve, frame, output);
  case CameraCurveType::Hermite:
    return SampleHermite(curve, frame, loop, output);
  case CameraCurveType::Step:
    return SampleStep(curve, frame, output);
  default:
    return CameraCurveSampleStatus::UnsupportedType;
  }
}

void InitializeCameraAnimationSample(
    const CameraAnimationDefaultsWire &defaults,
    CameraAnimationSample *sample) noexcept {
  if (sample == nullptr) {
    return;
  }
  sample->Field80 = defaults.Field80;
  sample->Field84 = defaults.Field84;
  sample->Field88 = defaults.Field88;
  sample->Field8C = defaults.Field8C;
  sample->Field90 = defaults.Field90;
  sample->Field94 = defaults.Field94;
  sample->FieldD0 = defaults.FieldD0;
  sample->Field144 = defaults.Field144Source * kDegreeScale;
  sample->Field1A2Scaled = defaults.Field1A2Source * kAngleS16Scale;
}

std::size_t
CameraAnimationRecordChannelCount(std::uint8_t recordType) noexcept {
  switch (recordType) {
  case 1U:
  case 2U:
    return 3U;
  case 3U:
  case 7U:
  case 8U:
    return 1U;
  default:
    return 0U;
  }
}

void ApplyCameraAnimationRecord(std::uint8_t recordType,
                                const CameraAnimationChannelSamples &channels,
                                CameraAnimationSample *sample) noexcept {
  if (sample == nullptr) {
    return;
  }
  switch (recordType) {
  case 1U:
    if (channels[0].Present) {
      sample->Field8C = channels[0].Value * kPositionScale;
    }
    if (channels[1].Present) {
      sample->Field90 = channels[1].Value * kPositionScale;
    }
    if (channels[2].Present) {
      sample->Field94 = channels[2].Value * kPositionScale;
    }
    break;
  case 2U:
    if (channels[0].Present) {
      sample->Field80 = channels[0].Value * kPositionScale;
    }
    if (channels[1].Present) {
      sample->Field84 = channels[1].Value * kPositionScale;
    }
    if (channels[2].Present) {
      sample->Field88 = channels[2].Value * kPositionScale;
    }
    break;
  case 3U:
    if (channels[0].Present) {
      sample->Field1A2Scaled = channels[0].Value * kAngleS16Scale;
    }
    break;
  case 7U:
    if (channels[0].Present) {
      sample->Field144 = channels[0].Value * kDegreeScale;
    }
    break;
  case 8U:
    if (channels[0].Present) {
      sample->FieldD0 = channels[0].Value;
    }
    break;
  default:
    break;
  }
}

float ResolveCameraAnimationContinuousFrame(std::int32_t nativeFrame,
                                            const TimeContext &time) noexcept {
  return static_cast<float>(nativeFrame) +
         static_cast<float>(LogicalSubframe(time.CurrentLogicalFrame));
}

} // namespace oot3d::gameplay
