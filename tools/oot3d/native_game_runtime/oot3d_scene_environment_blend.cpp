#include "oot3d_scene_environment.h"

#include <algorithm>
#include <cmath>

namespace Oot3dNativeGame {
namespace {
float Mix(float a, float b, float w) { return std::lerp(a, b, w); }
float Angle(float a, float b, float w) {
  if (w == 0) return a;
  if (w == 1) return b;
  return std::remainder(a + std::remainder(b - a, 360.0F) * w, 360.0F);
}
std::array<float, 3> Color(const std::array<float, 3>& a, const std::array<float, 3>& b, float w) {
  if (w == 0) return a;
  if (w == 1) return b;
  std::array<float, 3> result;
  // The profile codec and F1 already store linear RGB (not sRGB).
  for (size_t i = 0; i < 3; ++i) result[i] = Mix(a[i], b[i], w);
  return result;
}
} // namespace

SceneEnvironmentProfile BlendSceneEnvironmentProfiles(const SceneEnvironmentProfile& a,
                                                        const SceneEnvironmentProfile& b, float weight) {
  const float w = std::isfinite(weight) ? std::clamp(weight, 0.0F, 1.0F) : 0.0F;
  auto out = w < 0.5F ? a : b; // Discrete policies change at the midpoint.
  out.NativeDirectStrength = Mix(a.NativeDirectStrength, b.NativeDirectStrength, w);
  out.NativeLocalStrength = Mix(a.NativeLocalStrength, b.NativeLocalStrength, w);
  out.NativeAmbientStrength = Mix(a.NativeAmbientStrength, b.NativeAmbientStrength, w);
  out.AmbientColor = Color(a.AmbientColor, b.AmbientColor, w);
  out.AmbientIntensity = Mix(a.AmbientIntensity, b.AmbientIntensity, w);
  out.FogStrength = Mix(a.FogStrength, b.FogStrength, w);
  out.KeyAzimuthOffset = Angle(a.KeyAzimuthOffset, b.KeyAzimuthOffset, w);
  out.KeyElevationOffset = Mix(a.KeyElevationOffset, b.KeyElevationOffset, w);
  for (size_t i = 0; i < out.Lights.size(); ++i) {
    auto& light = out.Lights[i];
    light.Azimuth = Angle(a.Lights[i].Azimuth, b.Lights[i].Azimuth, w);
    light.Elevation = Mix(a.Lights[i].Elevation, b.Lights[i].Elevation, w);
    light.Color = Color(a.Lights[i].Color, b.Lights[i].Color, w);
    light.Intensity = Mix(a.Lights[i].Intensity, b.Lights[i].Intensity, w);
  }
  for (size_t i = 0; i < out.PointLights.size(); ++i) {
    const auto& x = a.PointLights[i]; const auto& y = b.PointLights[i];
    auto& light = out.PointLights[i];
    light.Intensity = Mix(x.Enabled ? x.Intensity : 0, y.Enabled ? y.Intensity : 0, w);
    light.Enabled = light.Intensity > 0;
    light.ColorTint = Color(x.ColorTint, y.ColorTint, w);
    light.RadiusScale = Mix(x.RadiusScale, y.RadiusScale, w);
  }
  out.Rim.Intensity = Mix(a.Rim.Enabled ? a.Rim.Intensity : 0, b.Rim.Enabled ? b.Rim.Intensity : 0, w);
  out.Rim.Enabled = out.Rim.Intensity > 0;
  out.Rim.Color = Color(a.Rim.Color, b.Rim.Color, w);
  out.Rim.Sharpness = Mix(a.Rim.Sharpness, b.Rim.Sharpness, w);
  out.Rim.Coverage = Mix(a.Rim.Coverage, b.Rim.Coverage, w);
  out.Rim.Azimuth = Angle(a.Rim.Azimuth, b.Rim.Azimuth, w);
  out.PlacedLights.clear();
  for (size_t i = 0; i < std::max(a.PlacedLights.size(), b.PlacedLights.size()); ++i) {
    const bool hasA = i < a.PlacedLights.size(), hasB = i < b.PlacedLights.size();
    const auto& x = hasA ? a.PlacedLights[i] : b.PlacedLights[i];
    const auto& y = hasB ? b.PlacedLights[i] : a.PlacedLights[i];
    auto light = w < 0.5F ? x : y;
    light.Id = static_cast<uint32_t>(i + 1); // Same shadow identity across both endpoints.
    light.Intensity = Mix(hasA && x.Visible ? x.Intensity : 0, hasB && y.Visible ? y.Intensity : 0, w);
    light.Visible = light.Intensity > 0;
    light.Color = Color(x.Color, y.Color, w);
    for (size_t axis = 0; axis < 3; ++axis) {
      light.Position[axis] = Mix(x.Position[axis], y.Position[axis], w);
      light.Rotation[axis] = Angle(x.Rotation[axis], y.Rotation[axis], w);
    }
    light.Radius = Mix(x.Radius, y.Radius, w);
    light.Width = Mix(x.Width, y.Width, w); light.Height = Mix(x.Height, y.Height, w);
    light.Range = Mix(x.Range, y.Range, w);
    light.BeamAngle = Mix(x.BeamAngle, y.BeamAngle, w);
    light.BeamSoftness = Mix(x.BeamSoftness, y.BeamSoftness, w);
    light.Spread = Mix(x.Spread, y.Spread, w);
    out.PlacedLights.push_back(std::move(light));
  }
  return out;
}
} // namespace Oot3dNativeGame
