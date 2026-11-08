// Ported from mm3d-dekui src/mm3d/native_minimap.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud_adjustments.h"
#include "host_interfaces.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace Mm3dDekui::NativeMinimap {
using U32 = std::uint32_t;
// Supplied Restoration + HD HUD v110. See MINIMAP-HANDOFF.md for binary evidence.
constexpr U32 Before = 0x0059b4b4, After = 0x0059b4bc; // ref 0x59b344, 0x59b34c
constexpr U32 BeforeSVC = 0xfffffc, AfterSVC = 0xfffffd;
constexpr U32 Manager = 0x71b000, Capacity = 0x180;
inline std::atomic<float> right_margin{8.f}, bottom_margin{8.f};
inline std::atomic<unsigned> moved_quads{0};
inline std::atomic<unsigned> status{0}; // 0 awaiting hook, 1 inactive, 2 moved, 3 rejected
inline U32 Instruction(U32 pc, U32 word) {
    if (pc == Before && word == 0xe320f000) return 0xef000000 | BeforeSVC;
    if (pc == After && word == 0xe5940038) return 0xef000000 | AfterSVC;
    return word;
}
template<class Memory> bool CodeMatches(Memory& m) {
    for (auto [at, value] : std::array<std::pair<U32,U32>,12>{{
        {Before,0xe320f000}, {0x0059b4b8,0xebf41f18}, {After,0xe5940038}, // ref 0x59b348, 0xebf41fc7
        {0x00175ad4,Manager}, {0x00188348,0x727970}, {0x0036e25c,0x6890f4}, // ref 0x175c30, 0x1884a4, 0x36e294
        {0x002a3144,0x6891ec}, {0x0036e6a4,0xebfb3b55}, // ref 0x2a3290, 0x36e6dc, 0xebfb3bba
        // S2 (tip pending-quad fix): the radar object's quad count +0x2C8 and capacity +0x2C4
        // on vanilla v1.1. The prepare/draw reads the count (0x5E8C24 ldr r0,[r5,#0x2c8]) and
        // stores it back after the draw (0x5E8F84 str r6,[r5,#0x2c8]); the reserve routine
        // stores the capacity before reading the count (0x5F0C48 str r1,[r0,#0x2c4]; 0x5F0C4C
        // ldr r6,[r0,#0x2c8]). Upstream's "retail 5E8F84" is this same store.
        {0x005e8c24,0xe59502c8}, {0x005e8f84,0xe58562c8},
        {0x005f0c48,0xe58012c4}, {0x005f0c4c,0xe59062c8}}}) {
        if (m.Read(at) != std::optional<U32>(value)) return false;
    }
    return true;
}
constexpr U32 Count(unsigned q) { return Manager + 0x14 + q*4; }
constexpr U32 Pointer(unsigned q, unsigned i) { return Manager + 0x578 + q*0x600 + i*4; }
constexpr U32 Priority(unsigned q, unsigned i) { return Manager + 0x3578 + q*0x600 + i*4; }
// Restoration Hud2 starts at 0x7f8104; heart_group is at +0x1fc.
// Verified by the heart_g lookup/store at 0x7d3e68..0x7d3e90.
// Widget::pos is +0x3c: color alpha +0x80, visibility flags +0x84.
template<class Memory> float HeartsOpacity(Memory& m) {
    std::optional<U32> widget;
    if constexpr(requires { m.RestorationHeartWidget(); }) {
        if(const auto heart=m.RestorationHeartWidget())widget=heart;
    }
    if(!widget) {
        if (m.Read(0x007cff70)!=std::optional<U32>(0x007f8100) ||
            m.Read(0x007d3e90)!=std::optional<U32>(0xe58501fc)) return 1.f;
        widget=m.Read(0x007f8300);
    }
    const auto valid=[](U32 p) { return p>=0x100000 && p<0x3fffff00 && !(p&3); };
    if (!widget || !valid(*widget)) return 1.f;
    auto name=m.Read(*widget+0x10);
    if (!name || *name<0x100000 || *name>=0x3ffffff8 || m.Read(*name)!=std::optional<U32>(0x72616568) ||
        m.Read(*name+4)!=std::optional<U32>(0x00675f74)) return 1.f;
    float opacity=1.f;
    // Include ancestor visibility/opacity so screen transition fades are respected too.
    for(unsigned depth=0;depth<32;++depth) {
        auto alpha=m.Read(*widget+0x80), flags=m.Read(*widget+0x84), parent=m.Read(*widget+8);
        if (!alpha || !flags || !parent) return 1.f;
        const float a=std::bit_cast<float>(*alpha);
        if (!std::isfinite(a) || a<0.f || a>1.f) return 1.f;
        if (!(*flags&1)) return 0.f;
        opacity*=a;
        if (!*parent) return opacity;
        if (!valid(*parent) || *parent==*widget) return 1.f;
        widget=parent;
    }
    return 1.f;
}
struct Entry { U32 object, priority, source_slot = 0; };
struct Hook {
    bool armed = false;
    bool suppress_draw = false;
    U32 owner = 0;
    std::vector<Entry> before;
    std::vector<std::pair<U32,U32>> zoom_restore;
    template<class Memory> void RestoreZoom(Memory& m){
        for(auto [at,bits]:zoom_restore)m.Write(at,bits);zoom_restore.clear();
    }
    template<class Memory> void SmallGameplayMap(Memory& m,U32 game){
        // Native map mode3 uses overview resource, overview scale+20, and
        // header's signed center at+C/+E (original1AE848). The geometry builder
        // also reads cached rotation at +28 independently of the mode byte.
        // Clear it for this draw, then restore the Maps screen's state.
        if(m.Read(0x0036e268)!=std::optional<U32>(0x7781d8))return; // ref 0x36e2a0
        constexpr U32 state=0x6890f4,mode_word=0x7781d8+0x520;
        auto mode=m.Read(mode_word),header=m.Read(state),small=m.Read(state+0x20);
        if(!mode||!header||!small||(*mode>>24)<1||(*mode>>24)>3||
           *header<0x100000||*header>=0x3fffffe0||(*header&3))return;
        auto center=m.Read(*header+0xc);if(!center)return;
        const float scale=std::bit_cast<float>(*small);
        if(!std::isfinite(scale)||scale<=0||scale>10000)return;
        // The player marker bypasses WorldToMap and reads cached +50/+52.
        // Rebuild those coordinates using the same overview transform, rather
        // than reusing the zoomed-mode anchor (160,180). Original 1AE848/1C0154.
        const auto valid=[](U32 p,U32 size){return p>=0x100000 && !(p&3) && p<0x40000000-size;};
        if(!valid(game,0xf0d8))return;
        auto actor=m.Read(game+0x20dc),rooms=m.Read(*header),count=m.Read(*header+4);
        auto floor=m.Read(state+0x54),yoffset=m.Read(state+0x5c),map_scale=m.Read(*header+0x10);
        auto special=m.Read(game+0xf0d4),marker=m.Read(0x727970+0x834);
        if(!actor||!valid(*actor,0x30)||!rooms||!count||!floor||!yoffset||!map_scale||!special||!marker)return;
        const int room=static_cast<std::int8_t>(*floor&255);
        if(room<0||unsigned(room)>=(*count&255)||!valid(*rooms,0x728*(room+1)))return;
        const auto record=*rooms+0x728*room;
        auto flags=m.Read(record),fixed=m.Read(record+4),px=m.Read(*actor+0x24),pz=m.Read(*actor+0x2c);
        if(!flags||!fixed||!px||!pz)return;
        float x=std::bit_cast<float>(*px),z=std::bit_cast<float>(*pz);
        if(*flags&8) {
            x=float(static_cast<std::int16_t>(*flags>>16));
            z=float(static_cast<std::int16_t>(*fixed&0xffff));
        } else if((*special>>16)&255) {x*=10.f;z*=10.f;}
        const float factor=std::bit_cast<float>(*map_scale)*scale;
        const float mx=160.f+(x-float(static_cast<std::int16_t>(*center&0xffff)))*factor;
        const float my=120.f+(z-float(static_cast<std::int16_t>(*center>>16)))*factor+
            float(static_cast<std::int8_t>((*yoffset>>16)&255));
        if(!std::isfinite(mx)||!std::isfinite(my)||mx< -32768.f||mx>32767.f||my< -32768.f||my>32767.f)return;
        const U32 anchor=U32(static_cast<std::uint16_t>(static_cast<std::int16_t>(mx)))|
            (U32(static_cast<std::uint16_t>(static_cast<std::int16_t>(my)))<<16);
        const std::array<std::pair<U32,U32>,7> changes{{
            {state+0x50,anchor},{0x727970+0x834,*marker&0x00ffffff},
            {mode_word,(*mode&0x00ffffff)|0x03000000}, {state+0x1c,*small},
            {state+0x28,std::bit_cast<U32>(0.f)},
            {state+0x2c,std::bit_cast<U32>(float(static_cast<std::int16_t>(*center&0xffff)))},
            {state+0x30,std::bit_cast<U32>(float(static_cast<std::int16_t>(*center>>16)))}}};
        for(auto [at,bits]:changes){auto old=m.Read(at);if(!old){zoom_restore.clear();return;}zoom_restore.push_back({at,*old});}
        for(auto [at,bits]:changes)m.Write(at,bits);
    }
    template<class Memory> static bool Queue(Memory& m, unsigned q, std::vector<Entry>& out) {
        out.clear();
        const auto count = m.Read(Count(q));
        if (!count || *count > Capacity) return false;
        for (U32 i=0; i<*count; ++i) {
            auto p=m.Read(Pointer(q,i)), rank=m.Read(Priority(q,i));
            if (!p || !rank || !*p) return false;
            out.push_back({*p,*rank,Pointer(q,i)});
        }
        return true;
    }
    template<class Memory> void Begin(Memory& m, U32 screen, bool enabled, U32 game=0,
                                     bool suppress=false) {
        RestoreZoom(m);
        armed = false;
        suppress_draw = suppress;
        moved_quads.store(0);
        status.store(1);
        owner = screen;
        if (enabled && Queue(m,6,before)) { armed = true;SmallGameplayMap(m,game); }
    }
    template<class Memory> void End(Memory& m, U32 screen, bool enabled,
                                     float scale, float opacity, float right_edge) {
        RestoreZoom(m);
        const bool run = std::exchange(armed,false) && owner == screen && enabled;
        if (!run) return;
        status.store(3);
        if (!std::isfinite(scale) || !std::isfinite(opacity)) return;
        scale=std::clamp(scale,.2f,.6f); opacity=std::clamp(opacity,0.f,1.f);
        opacity *= HeartsOpacity(m)*HudAdjustments::Opacity();
        const float right=std::clamp(right_margin.load(),-64.f,120.f);
        const float bottom_inset=std::clamp(bottom_margin.load(),-64.f,120.f)+HudAdjustments::Bottom();
        if (!std::isfinite(right) || !std::isfinite(bottom_inset)) return;
        std::vector<Entry> bottom, top, selected, retained;
        if (!Queue(m,6,bottom) || !Queue(m,4,top)) return;
        auto map0=m.Read(0x6890ec), map1=m.Read(0x6890f0), radar=m.Read(0x727970+0x828);
        if (!map0 || !map1 || !radar) return;
        // The original call priority-sorts the queue; compare entries, not array indices.
        auto unmatched=before;
        for (const auto& e:bottom) {
            auto old=std::find_if(unmatched.begin(),unmatched.end(),[&](const Entry& v) {
                return v.object==e.object && v.priority==e.priority;
            });
            if (old!=unmatched.end()) { unmatched.erase(old); retained.push_back(e); continue; }
            if (e.object!=*map0 && e.object!=*map1 && e.object!=*radar) return;
            selected.push_back(e);
        }
        if (!unmatched.empty() || selected.empty() || selected.size()>2 ||
            top.size()+selected.size()>Capacity) return;
        // Avoid preparing or transforming an object already referenced by another queue.
        for (const auto& e:selected) {
            for (unsigned q=0;q<8;++q) {
                std::vector<Entry> entries;
                if (!Queue(m,q,entries)) return;
                unsigned occurrences=0;
                for (const auto& v:entries) occurrences += v.object==e.object;
                if (occurrences != (q==6 ? 1u : 0u)) return;
            }
        }
        const auto publish_moves=[&](const std::vector<Entry>* new_top) {
            if constexpr(requires { m.NativeQuadQueueCommitted(std::span<const NativeQuadQueueMove>{}); }) {
                std::vector<NativeQuadQueueMove> moves;
                for(unsigned i=0;i<retained.size();++i)
                    moves.push_back({retained[i].source_slot,Pointer(6,i),retained[i].object,false});
                if(new_top) for(unsigned i=0;i<new_top->size();++i) {
                    const auto& entry=(*new_top)[i];
                    const bool moved=std::any_of(selected.begin(),selected.end(),[&](const Entry& e){return e.source_slot==entry.source_slot;});
                    moves.push_back({entry.source_slot,Pointer(4,i),entry.object,moved});
                }
                else for(const auto& e:selected) moves.push_back({e.source_slot,0,e.object,false});
                m.NativeQuadQueueCommitted(moves);
            }
        };
        if(HudAdjustments::MinimapHidden()||suppress_draw) {
            // A removed drawable never reaches the native prepare/draw routine
            // (retail 5E8B7C), which consumes its quad count at 5E8F84.
            // Discard those pending quads too, or the shared radar buffer keeps
            // small gameplay icons and appends them to the next full map draw.
            // Validate every selected object before changing either buffer/queue.
            for(const auto& e:selected) {
                if(e.object<0x100000 || (e.object&3) || e.object>=0x3ffffc58)return;
                const auto n=m.Read(e.object+0x2c8),cap=m.Read(e.object+0x2c4);
                if(!n||!cap||*n>*cap||*cap>4096)return;
            }
            for(const auto& e:selected)m.Write(e.object+0x2c8,0);
            // Remove only this gameplay call's map/radar entries. Full map menus
            // use their own routing; unrelated lower-screen objects remain.
            for(unsigned i=0;i<retained.size();++i) {
                m.Write(Pointer(6,i),retained[i].object);m.Write(Priority(6,i),retained[i].priority);
            }
            m.Write(Count(6),static_cast<U32>(retained.size()));
            publish_moves(nullptr);status.store(2);return;
        }
        // Stage all writes. A bad pointer/count leaves the original map untouched.
        std::vector<std::pair<U32,U32>> writes;
        unsigned total=0;
        const auto valid=[](U32 p, U32 bytes) { return p>=0x100000 && !(p&3) &&
            p<0x40000000 && bytes<=0x40000000-p; };
        const auto change=[&](U32 at, float multiplier, float offset) {
            auto bits=m.Read(at);
            if (!bits) return false;
            const float v=std::bit_cast<float>(*bits);
            const float result=v*multiplier+offset;
            if (!std::isfinite(v) || !std::isfinite(result)) return false;
            writes.emplace_back(at,std::bit_cast<U32>(result));
            return true;
        };
        // Native quad preparation transforms the template corners at +2cc by
        // size and the per-quad rotation, then adds position. Position is not
        // necessarily a top-left corner (map/radar quads can be centered).
        // Use map geometry for the pivot; moving radar markers must not move it.
        const bool has_map=std::any_of(selected.begin(),selected.end(),[&](const Entry& e){
            return e.object==*map0||e.object==*map1;
        });
        float map_right=-1e9f,map_bottom=-1e9f;
        const auto number=[&](U32 at)->std::optional<float>{
            auto bits=m.Read(at);if(!bits)return {};
            const float v=std::bit_cast<float>(*bits);
            return std::isfinite(v)?std::optional<float>(v):std::nullopt;
        };
        for (const auto& e:selected) {
            if(has_map&&e.object!=*map0&&e.object!=*map1)continue;
            if (!valid(e.object,0x3a8)) return;
            auto n=m.Read(e.object+0x2c8),pos=m.Read(e.object+0x2b8),size=m.Read(e.object+0x2c0),rotation=m.Read(e.object+0x2bc);
            if(!n||!pos||!size||!rotation||!*n||*n>4096||!valid(*pos,*n*12)||
               !valid(*size,*n*12)||!valid(*rotation,*n*48))return;
            for(U32 i=0;i<*n;++i)for(U32 vertex=0;vertex<4;++vertex) {
                std::array<float,3> point{};
                for(U32 axis=0;axis<3;++axis) {
                    auto v=number(e.object+0x2cc+vertex*12+axis*4),s=number(*size+i*12+axis*4);
                    if(!v||!s)return;point[axis]=*v * *s;
                }
                for(U32 axis=0;axis<2;++axis) {
                    auto p=number(*pos+i*12+axis*4);
                    if(!p)return;float edge=*p;
                    for(U32 component=0;component<3;++component) {
                        auto r=number(*rotation+i*48+axis*16+component*4);
                        if(!r)return;edge+=*r*point[component];
                    }
                    if(!std::isfinite(edge))return;
                    auto& maximum=axis?map_bottom:map_right;maximum=std::max(maximum,edge);
                }
            }
        }
        const float player_scale=HudAdjustments::Scale(HudAdjustments::Minimap);
        const float offset_x=right_edge-right-320.f*scale+map_right*scale*(1-player_scale)+HudAdjustments::X(HudAdjustments::Minimap)+HudAdjustments::HorizontalPadding(HudAdjustments::Minimap);
        const float offset_y=240.f-bottom_inset-240.f*scale+map_bottom*scale*(1-player_scale)+HudAdjustments::Y(HudAdjustments::Minimap);
        scale*=player_scale;
        for (const auto& e:selected) {
            if (!valid(e.object,0x3a8)) return;
            auto n=m.Read(e.object+0x2c8), cap=m.Read(e.object+0x2c4);
            auto pos=m.Read(e.object+0x2b8), size=m.Read(e.object+0x2c0), color=m.Read(e.object+0x398);
            if (!n || !cap || !pos || !size || !color || *n>*cap || *n>4096 ||
                !valid(*pos,*n*12) || !valid(*size,*n*12) ||
                (*color && !valid(*color,*n*64))) return;
            for (U32 i=0;i<*n;++i) {
                if (!change(*pos+i*12,scale,offset_x) ||
                    !change(*pos+i*12+4,scale,offset_y) ||
                    !change(*size+i*12,scale,0) || !change(*size+i*12+4,scale,0)) return;
                if (*color) for (U32 vertex=0;vertex<4;++vertex)
                    if (!change(*color+i*64+vertex*16+12,opacity,0)) return;
            }
            total += *n;
        }
        for (const auto& e:selected) top.push_back(e);
        std::stable_sort(top.begin(),top.end(),[](const Entry& a,const Entry& b) {
            return a.priority<b.priority;
        });
        // Ensure every destination is mapped before committing any geometry or queue edit.
        for (unsigned i=0;i<top.size();++i)
            if (!m.Read(Pointer(4,i)) || !m.Read(Priority(4,i))) return;
        for (auto [at,value]:writes) m.Write(at,value);
        const auto save=[&](unsigned q,const std::vector<Entry>& entries) {
            for (unsigned i=0;i<entries.size();++i) {
                m.Write(Pointer(q,i),entries[i].object); m.Write(Priority(q,i),entries[i].priority);
            }
            m.Write(Count(q),static_cast<U32>(entries.size()));
        };
        save(6,retained); save(4,top);
        publish_moves(&top);
        moved_quads.store(total); status.store(2);
    }
};
}
