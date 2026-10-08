// Ported from mm3d-dekui src/mm3d/widescreen.h (tgacquin/mm3d-dekui).
// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include "host_interfaces.h"

namespace Mm3dDekui::Widescreen {
constexpr float NativeAspect = 400.f / 240.f;
constexpr float Aspect = 16.f / 9.f;
constexpr float HorizontalScale = NativeAspect / Aspect; // 15/16
inline std::atomic<bool> enabled{false};
inline std::atomic<float> camera_zoom_out{1.f};
inline std::atomic<bool> supported_layout{false};
inline std::atomic<unsigned> corrected_draws{0};
inline unsigned frame_corrected_draws=0; // Renderer thread only.
inline bool Active() { return enabled.load() && supported_layout.load(); }
inline bool IsTopViewport(unsigned width,unsigned height) {
    // Captured MM3D top draws use 480x400, while bottom draws use 240x320.
    // Inspect guest dimensions, not cache surface offsets or resolution-scaled extents.
    return height==400 && (width==240 || width==480);
}

// Fit the wider top screen in its available slot. Keep separate neighboring
// screens outside the slot; an existing overlapping bottom-screen inset stays an inset.
inline HostRectangle<unsigned> FitTop(HostRectangle<unsigned> top,
    const HostRectangle<unsigned>& bottom, unsigned window_width, bool bottom_enabled) {
    unsigned left=0, right=window_width;
    if(bottom_enabled && bottom.top<top.bottom && bottom.bottom>top.top) {
        if(bottom.left>=top.right) right=std::min(right,bottom.left);
        if(bottom.right<=top.left) left=std::max(left,bottom.right);
    }
    if(right<=left || !top.GetHeight()) return top;
    const float h=std::min(float(top.GetHeight()),float(right-left)/Aspect);
    const unsigned height=std::max(1u,unsigned(std::lround(h)));
    const unsigned width=std::min(right-left,std::max(1u,unsigned(std::lround(h*Aspect))));
    const float cx=(float(top.left)+top.right)*.5f;
    const unsigned x=unsigned(std::clamp(cx-width*.5f,float(left),float(right-width)));
    const unsigned y=top.top+(top.GetHeight()-height)/2;
    return {x,y,x+width,y+height};
}
}
