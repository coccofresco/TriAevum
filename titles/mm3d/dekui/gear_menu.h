// Ported from mm3d-dekui src/mm3d/gear_menu.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>

namespace Mm3dDekui::GearMenu {

enum Part : unsigned { Panel, Right, Background, Buttons, Model, Description, Count };
struct Pose { float x=0,y=0,scale=1; };
struct Options {
    bool enabled=true;
    bool horizontal=true;
    std::array<Pose,Count> poses{{{0,32,.65f},{-90,32,.65f},{0,0,1},{0,0,1},{0,0,1},{0,0,1}}};
};
struct AtomicPose { std::atomic<float> x{0},y{0},scale{1}; };
inline std::atomic<bool> enabled{true};
inline std::atomic<bool> horizontal{true};
inline std::array<AtomicPose,Count> poses;
inline Pose Get(unsigned part) { return {poses[part].x.load(),poses[part].y.load(),poses[part].scale.load()}; }
inline void Apply(const Options& o) {
    enabled=o.enabled;
    horizontal=o.horizontal;
    for(unsigned i=0;i<Count;++i) {poses[i].x=o.poses[i].x;poses[i].y=o.poses[i].y;poses[i].scale=o.poses[i].scale;}
}
}
