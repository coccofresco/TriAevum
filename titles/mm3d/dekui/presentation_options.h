// SPDX-License-Identifier: GPL-2.0-or-later
// Configuration schema from mm3d-dekui 6e36dfb4 depth_of_field.h and
// ambient_occlusion.h. Renderer implementation belongs to the host.
#pragma once
#include <algorithm>
#include <cmath>
namespace Mm3dDekui::DepthOfField {
struct Options {bool enabled=false;float focus=600.f,transition=1200.f,strength=1.5f;};
inline float Bounded(float x,float lo,float hi,float fallback) {
    return std::isfinite(x)?std::clamp(x,lo,hi):fallback;
}
inline Options Sanitize(Options o) {
    o.focus=Bounded(o.focus,50.f,4000.f,600.f);
    o.transition=Bounded(o.transition,100.f,6000.f,1200.f);
    o.strength=Bounded(o.strength,0.f,4.f,1.5f);return o;
}
}
namespace Mm3dDekui::AmbientOcclusion {
struct Options {bool enabled=false;float strength=.6f,radius=40.f;};
inline Options Sanitize(Options o) {
    o.strength=DepthOfField::Bounded(o.strength,0.f,1.5f,.6f);
    o.radius=DepthOfField::Bounded(o.radius,5.f,150.f,40.f);return o;
}
}
