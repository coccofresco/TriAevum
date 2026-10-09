#include "oot3d_light_gizmo.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Oot3dNativeGame {

namespace {

using Vec3 = std::array<float, 3>;
using Color = std::array<float, 4>;

Vec3 Add(const Vec3 &a, const Vec3 &b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 Scale(const Vec3 &a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
Vec3 Madd(const Vec3 &a, const Vec3 &b, float s) { return Add(a, Scale(b, s)); }
Vec3 Cross(const Vec3 &a, const Vec3 &b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float Length(const Vec3 &a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
Vec3 Normalize(const Vec3 &a, const Vec3 &fallback) {
  const float length = Length(a);
  return length > 1.0e-6F ? Scale(a, 1.0F / length) : fallback;
}
// Any unit vector perpendicular to a unit `axis`.
Vec3 Perpendicular(const Vec3 &axis) {
  const Vec3 other = std::abs(axis[1]) < 0.9F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
  return Normalize(Cross(axis, other), {1.0F, 0.0F, 0.0F});
}

constexpr Color kAxisX{1.0F, 0.3F, 0.3F, 1.0F};
constexpr Color kAxisY{0.35F, 1.0F, 0.35F, 1.0F};
constexpr Color kAxisZ{0.4F, 0.6F, 1.0F, 1.0F};
constexpr Color kPitch{1.0F, 0.6F, 0.15F, 1.0F};
constexpr Color kRoll{0.3F, 1.0F, 1.0F, 1.0F};
constexpr Color kBeam{1.0F, 1.0F, 0.6F, 1.0F};
constexpr size_t kCircleSegments = 64U;

class Builder {
public:
  explicit Builder(Fast::Oot3d::WorldDebugLineSet &set) : mSet(set) {}
  void Line(const Vec3 &from, const Vec3 &to, const Color &color) { mSet.Lines.push_back({from, to, color}); }
  // A circle of `radius` about `center` in the plane of unit `u` and `v`.
  void Circle(const Vec3 &center, const Vec3 &u, const Vec3 &v, float radius, const Color &color) {
    constexpr float kStep = 2.0F * std::numbers::pi_v<float> / static_cast<float>(kCircleSegments);
    Vec3 previous = Madd(center, u, radius);
    for (size_t index = 1; index <= kCircleSegments; ++index) {
      const float angle = kStep * static_cast<float>(index);
      const Vec3 point = Madd(Madd(center, u, radius * std::cos(angle)), v, radius * std::sin(angle));
      Line(previous, point, color);
      previous = point;
    }
  }
  void Arrow(const Vec3 &from, const Vec3 &direction, float length, float head, const Color &color) {
    const Vec3 tip = Madd(from, direction, length);
    Line(from, tip, color);
    const Vec3 side = Perpendicular(direction);
    const Vec3 other = Cross(direction, side);
    const Vec3 back = Madd(tip, direction, -head);
    for (const Vec3 &offset : {side, Scale(side, -1.0F), other, Scale(other, -1.0F)})
      Line(tip, Madd(back, offset, head * 0.4F), color);
  }

private:
  Fast::Oot3d::WorldDebugLineSet &mSet;
};

Color Faded(const Color &color, float alpha) { return {color[0], color[1], color[2], color[3] * alpha}; }

// The light's colour at full brightness, so a dim light's gizmo stays visible.
Color LightColor(const PlacedLight &light, float alpha) {
  const float peak = std::max({light.Color[0], light.Color[1], light.Color[2], 1.0e-3F});
  Color color{light.Color[0] / peak, light.Color[1] / peak, light.Color[2] / peak, alpha};
  // Near black (peak tiny): white.
  if (peak <= 1.0e-3F) color = {1.0F, 1.0F, 1.0F, alpha};
  return color;
}

void DrawShape(Builder &out, const Fast::Renderer3ds::PicaPlacedLight &resolved, float ring,
               const Color &color) {
  using Kind = Fast::Renderer3ds::PicaPlacedLightKind;
  const Vec3 &p = resolved.PositionWorld;
  const Vec3 axis = Normalize(resolved.AxisWorld, {0.0F, -1.0F, 0.0F});
  const Vec3 tangent = Normalize(resolved.TangentWorld, Perpendicular(axis));
  const Vec3 bitangent = Cross(axis, tangent);
  // The light's own point: a small cross.
  const float mark = ring * 0.3F;
  if (resolved.Kind != Kind::Sun)
    for (const Vec3 &unit : {Vec3{1.0F, 0.0F, 0.0F}, Vec3{0.0F, 1.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}})
      out.Line(Madd(p, unit, -mark), Madd(p, unit, mark), color);
  const float reach = std::max(resolved.Reach, 1.0F);
  switch (resolved.Kind) {
  case Kind::Omni:
  case Kind::Point:
    out.Circle(p, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, reach, color);
    out.Circle(p, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, reach, color);
    out.Circle(p, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, reach, color);
    break;
  case Kind::Spot: {
    const auto cone = [&](float cosine, const Color &coneColor, bool generators) {
      const float angle = std::acos(std::clamp(cosine, -1.0F, 1.0F));
      const Vec3 center = Madd(p, axis, reach * std::cos(angle));
      const float radius = reach * std::sin(angle);
      out.Circle(center, tangent, bitangent, radius, coneColor);
      if (!generators) return;
      for (size_t index = 0; index < 8U; ++index) {
        const float around = 2.0F * std::numbers::pi_v<float> * static_cast<float>(index) / 8.0F;
        out.Line(p, Madd(Madd(center, tangent, radius * std::cos(around)), bitangent, radius * std::sin(around)),
                 coneColor);
      }
    };
    cone(resolved.CosOuter, color, true);
    // The beam's hard core.
    if (resolved.CosInner > resolved.CosOuter + 1.0e-4F) cone(resolved.CosInner, Faded(color, 0.5F), false);
    break;
  }
  case Kind::Rectangle:
  case Kind::Disc: {
    if (resolved.Kind == Kind::Disc) {
      out.Circle(p, tangent, bitangent, resolved.HalfWidth, color);
    } else {
      const float w = resolved.HalfWidth, h = resolved.HalfHeight;
      const Vec3 corners[4] = {Madd(Madd(p, tangent, -w), bitangent, -h), Madd(Madd(p, tangent, w), bitangent, -h),
                               Madd(Madd(p, tangent, w), bitangent, h), Madd(Madd(p, tangent, -w), bitangent, h)};
      for (size_t index = 0; index < 4U; ++index) out.Line(corners[index], corners[(index + 1U) % 4U], color);
      out.Line(corners[0], corners[2], Faded(color, 0.4F));
      out.Line(corners[1], corners[3], Faded(color, 0.4F));
    }
    // The emitting face(s), as far as the light reaches.
    out.Arrow(p, axis, reach, ring * 0.5F, Faded(color, 0.7F));
    if (resolved.TwoSided) out.Arrow(p, Scale(axis, -1.0F), reach, ring * 0.5F, Faded(color, 0.7F));
    break;
  }
  case Kind::Sun: {
    // Parallel light: a sheaf of arrows along it.
    const float length = ring * 2.5F;
    out.Arrow(p, axis, length, ring * 0.5F, color);
    for (const Vec3 &offset : {tangent, Scale(tangent, -1.0F), bitangent, Scale(bitangent, -1.0F)})
      out.Arrow(Madd(p, offset, ring * 0.6F), axis, length, ring * 0.4F, Faded(color, 0.7F));
    break;
  }
  }
}

void DrawHandles(Builder &out, const PlacedLight &light, const Fast::Renderer3ds::PicaPlacedLight &resolved,
                 float ring) {
  const Vec3 &p = resolved.PositionWorld;
  const bool sun = light.Type == PlacedLightType::Sun;
  const bool turns = sun || light.Type == PlacedLightType::Spot || light.Type == PlacedLightType::Surface;
  const auto axisLine = [&](const Vec3 &unit, const Color &color) {
    out.Line(Madd(p, unit, -kLightGizmoAxisReach), Madd(p, unit, kLightGizmoAxisReach), color);
  };
  // Translation (a sun has no position): world X, Y and Z through the light.
  if (!sun) {
    axisLine({1.0F, 0.0F, 0.0F}, kAxisX);
    axisLine({0.0F, 1.0F, 0.0F}, kAxisY);
    axisLine({0.0F, 0.0F, 1.0F}, kAxisZ);
  }
  if (!turns) return;
  constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
  const float yaw = light.Rotation[0] * kRadians;
  const Vec3 axis = Normalize(resolved.AxisWorld, {0.0F, -1.0F, 0.0F});
  // Yaw turns about world Y.
  if (sun) axisLine({0.0F, 1.0F, 0.0F}, kAxisY);
  out.Circle(p, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, ring, kAxisY);
  // Pitch turns about the light's horizontal right axis, in the vertical
  // plane through its axis.
  const Vec3 right{std::cos(yaw), 0.0F, -std::sin(yaw)};
  axisLine(right, kPitch);
  out.Circle(p, Normalize(Cross({0.0F, 1.0F, 0.0F}, right), {0.0F, 0.0F, 1.0F}), {0.0F, 1.0F, 0.0F}, ring, kPitch);
  if (light.Type == PlacedLightType::Surface) {
    // Roll turns the surface about its own axis.
    axisLine(axis, kRoll);
    const Vec3 tangent = Normalize(resolved.TangentWorld, Perpendicular(axis));
    out.Circle(p, tangent, Cross(axis, tangent), ring * 1.2F, kRoll);
  } else {
    // Where the beam or the sunlight goes.
    out.Line(p, Madd(p, axis, kLightGizmoAxisReach), kBeam);
  }
}

} // namespace

Fast::Oot3d::WorldDebugLineSet BuildPlacedLightGizmo(const std::vector<PlacedLight> &lights,
                                                     uint32_t selectedLight, const std::array<float, 3> &eye) {
  Fast::Oot3d::WorldDebugLineSet set;
  Builder out(set);
  // As the editor: without a selection, the first light.
  if (!lights.empty() && std::none_of(lights.begin(), lights.end(),
                                      [&](const auto &light) { return light.Id == selectedLight; }))
    selectedLight = lights.front().Id;
  const PlacedLight *selected = nullptr;
  for (const auto &light : lights) {
    const auto resolved = ResolvePlacedLight(light, light.Id);
    // A constant size on screen: a fraction of the distance from the eye.
    const float distance = Length(Add(resolved.PositionWorld, Scale(eye, -1.0F)));
    const float ring = std::max(distance * 0.08F, 1.0F);
    const bool isSelected = light.Id == selectedLight;
    if (isSelected) selected = &light;
    const float alpha = isSelected ? 1.0F : light.Visible ? 0.55F : 0.3F;
    DrawShape(out, resolved, ring, LightColor(light, alpha));
  }
  if (selected != nullptr) {
    const auto resolved = ResolvePlacedLight(*selected, selected->Id);
    const float distance = Length(Add(resolved.PositionWorld, Scale(eye, -1.0F)));
    DrawHandles(out, *selected, resolved, std::max(distance * 0.08F, 1.0F));
  }
  return set;
}

} // namespace Oot3dNativeGame
