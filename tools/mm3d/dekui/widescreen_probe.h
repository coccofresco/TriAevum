// Ported from mm3d-dekui src/mm3d/widescreen_probe.h (tgacquin/mm3d-dekui).
// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace Mm3dDekui::WidescreenProbe {
struct Draw {
    std::uint64_t shader{},swizzle{};
    unsigned vertices{},width{},height{},textures{},blend{},depth{};
    bool operator==(const Draw&) const = default;
};
struct Candidate {
    Draw draw;
    unsigned observations{};
    std::array<int,4> scissor{};
};
// MM3D transition/title overlay, recovered from the user's tested capture.
// Observe is called only for widescreen top-screen draws without depth writes.
inline constexpr Draw TransitionOverlay{0x89b1ab98da3d9241ULL,0x6f0b398b1fc186a9ULL,
    4,480,400,0,65654,3856};
inline std::mutex mutex;
inline std::vector<Candidate> candidates;
inline std::vector<Draw> saved_draws;
inline int selected=-1;
inline unsigned bypassed=0;
inline std::int64_t capture_until=0,test_until=0;
inline auto Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void Capture() {
    std::scoped_lock lock(mutex);
    candidates.clear(); selected=-1; bypassed=0; test_until=0; capture_until=Now()+30000;
}
inline void Stop() {
    std::scoped_lock lock(mutex);
    capture_until=test_until=0; selected=-1;
}
inline bool Observe(const Draw& draw,const std::array<int,4>& scissor) {
    std::scoped_lock lock(mutex);
    const auto now=Now();
    if(now<capture_until) {
        auto it=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.draw==draw;});
        if(it!=candidates.end()) { ++it->observations; it->scissor=scissor; }
        else if(candidates.size()<128) candidates.push_back({draw,1,scissor});
    }
    const bool bypass=draw==TransitionOverlay || (std::find(saved_draws.begin(),saved_draws.end(),draw)!=saved_draws.end()) ||
        (now<test_until && selected>=0 && selected<int(candidates.size()) &&
        candidates[selected].draw==draw);
    if(bypass) ++bypassed;
    return bypass;
}
}
