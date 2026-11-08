#pragma once
#include <cmath>
#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
namespace Oot3dNativeGame {
// The host's gameplay rows (F1 > Controls > Gameplay and Esc > Game share
// one store). A title reports which rows it implements; the host shows only
// those for it and passes their values on every change.
struct TitleGameplayOptions {
  bool QuickPutAway = false;
  bool LandingRoll = false;
  bool RollDash = false;
  bool AimingReticle = false;
};
struct TitleGameplayOptionSet {
  bool QuickPutAway = false;
  bool LandingRoll = false;
  bool RollDash = false;
  bool AimingReticle = false;
  [[nodiscard]] bool Any() const noexcept { return QuickPutAway || LandingRoll || RollDash || AimingReticle; }
};

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
// Bounded product receipt of actual published projections, not requested
// settings or developer-only guest traces. No title addresses or guest writes.
class PresentedProjectionHistory {
public:
  struct Segment {
    TitlePresentationSettings Settings;
    uint32_t FirstFrame=0,LastFrame=0;
    uint64_t Samples=0;
    float NativeFov=0,PresentedFov=0,Aspect=0;
  };
  const auto& Segments() const { return mSegments; }
  unsigned Count() const { return mCount; }
  uint64_t DiscardedSegments() const { return mDiscarded; }
  void Reset() { mSegments={};mCount=0;mDiscarded=0; }
  void Record(const TitlePresentationSettings& settings,uint32_t frame,float nativeFov,
              const SymmetricPresentedFrustum& frustum) {
    if(!std::isfinite(frustum.Near)||!std::isfinite(frustum.Far)||
       frustum.Near<=0||frustum.Far<=frustum.Near||frustum.Top<=0||frustum.Right<=0)return;
    const float fov=std::atan(frustum.Top/frustum.Near)*360.f/3.14159265358979323846f;
    const float aspect=frustum.Right/frustum.Top;
    if(!std::isfinite(nativeFov)||nativeFov<=0||!std::isfinite(fov)||fov<=0||
       !std::isfinite(aspect)||aspect<=0)return;
    const auto same=[&](const auto& s) {
      return s.FovMultiplier==settings.FovMultiplier && s.HorizontalExpansion==settings.HorizontalExpansion &&
        s.VerticalExpansion==settings.VerticalExpansion && s.ExtendedDrawDistance==settings.ExtendedDrawDistance;
    };
    if(!mCount||!same(mSegments[mCount-1].Settings)) {
      if(mCount==mSegments.size()) {
        std::move(mSegments.begin()+1,mSegments.end(),mSegments.begin());--mCount;++mDiscarded;
      }
      mSegments[mCount++]={settings,frame,frame,0,0,0,0};
    }
    auto& s=mSegments[mCount-1];++s.Samples;s.LastFrame=frame;
    s.NativeFov=nativeFov;s.PresentedFov=fov;s.Aspect=aspect;
  }
private:
  std::array<Segment,8> mSegments{};
  unsigned mCount=0;
  uint64_t mDiscarded=0;
};
}
