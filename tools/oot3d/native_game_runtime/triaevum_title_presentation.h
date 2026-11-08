#pragma once
#include <cmath>
#include <optional>
namespace Oot3dNativeGame {
// Resolved renderer/window settings, never independent title preferences.
struct TitlePresentationSettings {
  float FovMultiplier=1.0F;
  float HorizontalExpansion=1.0F;
  float VerticalExpansion=1.0F;
  bool ExtendedDrawDistance=false;
};
struct SymmetricPresentedFrustum { float Right,Top,Near,Far; };
inline std::optional<SymmetricPresentedFrustum> DecodeSymmetricPerspective(
    float xScale,float yScale,float nearPlane,float farPlane) {
  if(!std::isfinite(xScale)||!std::isfinite(yScale)||!std::isfinite(nearPlane)||!std::isfinite(farPlane)||
     xScale<=0.f||yScale<=0.f||nearPlane<=0.f||farPlane<=nearPlane)return std::nullopt;
  const float right=nearPlane/xScale,top=nearPlane/yScale;
  if(!std::isfinite(right)||!std::isfinite(top))return std::nullopt;
  return SymmetricPresentedFrustum{right,top,nearPlane,farPlane};
}
}
