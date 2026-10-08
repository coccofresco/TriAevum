// Ported from mm3d-dekui src/mm3d/minigame_hud.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "song_list.h"
namespace Mm3dDekui::MinigameHud {
struct Options { float x=0,y=0,scale=1; };
inline std::atomic<float> x{0},y{0},scale{1};
inline void Apply(const Options& o){x=o.x;y=o.y;scale=o.scale;}
struct Hook {
    SongList::Hook helper;
    using Write=NativeInventory::Hook::Write;
    std::vector<std::vector<Write>> frames;
    template<class M> void Begin(M& m,NativeInventory::U32 layout){
        const bool nested=std::any_of(frames.begin(),frames.end(),[](const auto& f){return !f.empty();});
        frames.emplace_back();
        if(nested||!NativeInventory::LayoutNamed(m,layout,"Hud"))return;
        // Installed Restoration Hud.mfl: root -> all -> minigame_g.
        auto all=helper.Child(m,layout+8,"all");
        auto node=all?helper.Child(m,all,"minigame_g"):0;
        if(!node)return;
        const float dx=x.load(),dy=y.load(),s=scale.load();
        if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(s)||s<.25f||s>3)return;
        auto xb=m.Read(node+0xec),yb=m.Read(node+0xfc);if(!xb||!yb)return;
        const float cx=std::bit_cast<float>(*xb),cy=std::bit_cast<float>(*yb);
        if(!std::isfinite(cx)||!std::isfinite(cy))return;
        std::vector<Write> writes;std::vector<NativeInventory::U32> seen;
        if(!helper.helper.Transform(m,node,writes,seen,{s,0,0,s,dx+cx*(1-s),dy+cy*(1-s)}))return;
        for(auto w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
    }
    template<class M> void End(M& m){if(frames.empty())return;
        for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);
        frames.pop_back();
    }
};
}
