// Ported from mm3d-dekui src/mm3d/dialogue_scale.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gear_menu.h"
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <algorithm>
#include <optional>
#include <vector>
#include <cstdint>
namespace Mm3dDekui::DialogueScale {
using U32=std::uint32_t;
inline std::atomic<float> scale{1.f};
inline std::atomic<float> y_offset{0.f};
inline std::atomic<float> background_width{1.f};
inline std::atomic<float> indicator_x{0.f};
inline std::atomic<float> indicator_y{0.f};
inline std::atomic<float> item_scale{1.f}, item_x{0.f}, item_y{0.f};
constexpr U32 Before=0x00161b54,After=0x00161b5c,BeforeSVC=0xfffffa,AfterSVC=0xfffffb; // ref 0x161c9c, 0x161ca4
inline U32 Instruction(U32 pc,U32 word) {
    if(pc==Before && word==0xe2840008) return 0xef000000|BeforeSVC;
    if(pc==After && word==0xe28dd008) return 0xef000000|AfterSVC;
    return word;
}
template<class Memory> bool CodeMatches(Memory& m) {
    return m.Read(Before)==std::optional<U32>(0xe2840008) &&
        m.Read(0x00161b58)==std::optional<U32>(0xeb0060e8) && // ref 0x161ca0, 0xeb0060ed
        m.Read(After)==std::optional<U32>(0xe28dd008) &&
        m.Read(0x00161b60)==std::optional<U32>(0xecbd8b02); // ref 0x161ca8
}
template<class Memory> bool Name(Memory& m,U32 at,const char* expected) {
    if(at<0x100000 || at>0x3fffffc0) return false;
    for(unsigned i=0;i<48;++i) {
        auto word=m.Read((at+i)&~3u);if(!word) return false;
        const char c=char((*word>>(((at+i)&3)*8))&255);
        if(c!=expected[i]) return false;
        if(!c) return true;
    }
    return false;
}
struct Hook {
    struct Frame { U32 layout,widget=0,x=0,y=0,position_x=0,position_y=0,defaults=0,plate=0,plate_x=0,plate_flags=0,plate_defaults=0,indicator=0,indicator_x=0,indicator_y=0,indicator_flags=0,indicator_defaults=0; };
    std::vector<Frame> frames;
    template<class Memory> U32 Find(Memory& m,U32 node,const char* target,std::array<U32,64>& seen,unsigned& used,unsigned depth=0) {
        if(depth>32 || node<0x100000 || node>0x3ffffe00 || used>=seen.size()) return 0;
        for(unsigned i=0;i<used;++i) if(seen[i]==node) return 0;
        seen[used++]=node;
        auto name=m.Read(node+0x10);
        if(name && Name(m,*name,target)) return node;
        auto count=m.Read(node+0x18),list=m.Read(node+0x14);
        if(!count || !list || *count>32 || *list<0x100000 || *list>0x3ffffff0) return 0;
        for(U32 i=0;i<*count;++i) {
            auto child=m.Read(*list+i*4);
            if(child) { const U32 found=Find(m,*child,target,seen,used,depth+1); if(found) return found; }
        }
        return 0;
    }
    template<class Memory> void Begin(Memory& m,U32 layout,bool item_description=false,bool gear_description=false,const GearMenu::Pose* description_pose=nullptr) {
        item_description|=gear_description||description_pose!=nullptr;
        frames.push_back({layout}); auto& frame=frames.back();
        if(layout<0x100000 || layout>0x3ffffd00) return;
        auto cl=m.Read(layout+4);if(!cl || *cl<0x100000 || *cl>0x3fffffc0) return;
        auto name=m.Read(*cl+0x18);if(!name || !Name(m,*name,"MessageWindow")) return;
        // root_widget is embedded at +8; its only child is the composition's all group.
        const U32 root=layout+8;
        if(m.Read(root+0x18)!=std::optional<U32>(1)) return;
        auto list=m.Read(root+0x14);if(!list || *list<0x100000 || *list>0x3ffffff0) return;
        auto group=m.Read(*list);if(!group || *group<0x100000 || *group>0x3ffffe00) return;
        auto group_name=m.Read(*group+0x10);
        if(!group_name || !Name(m,*group_name,"all") || m.Read(*group+8)!=std::optional<U32>(root) ||
           m.Read(*group+0x18)!=std::optional<U32>(3)) return;
        auto x=m.Read(*group+0x98),y=m.Read(*group+0x9c),position_y=m.Read(*group+0x90),position_x=m.Read(*group+0x8c);
        auto flags=m.Read(*group+0xd4),defaults=m.Read(*group+0xd8);
        if(!x || !y || !position_y || !position_x || !flags || !defaults) return;
        const float sx=std::bit_cast<float>(*x),sy=std::bit_cast<float>(*y),configured=description_pose?description_pose->scale:item_description?item_scale.load():scale.load();
        if(!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(configured)) return;
        const float py=std::bit_cast<float>(*position_y),offset=description_pose?description_pose->y:item_description?item_y.load():y_offset.load();
        const float px=std::bit_cast<float>(*position_x),offset_x=description_pose?description_pose->x:item_description?item_x.load():0.f;
        if(!std::isfinite(py) || !std::isfinite(offset) || !std::isfinite(px) || !std::isfinite(offset_x)) return;
        const float translated_y=py+std::clamp(offset,item_description?-240.f:-120.f,item_description?240.f:120.f);
        const float translated_x=px+std::clamp(offset_x,-320.f,320.f);
        const float factor=std::clamp(configured,item_description?.25f:.5f,item_description?2.f:1.f),nx=sx*factor,ny=sy*factor;
        frame.widget=*group;frame.x=*x;frame.y=*y;frame.defaults=*defaults;frame.position_y=*position_y;
        frame.position_x=*position_x;
        // The shared `all` group has front, text, and plate children. Stretch only
        // the plate branch so glyphs and wrapping retain their native geometry.
        const float configured_width=background_width.load();
        const float width=std::isfinite(configured_width)?std::clamp(configured_width,1.f,2.f):1.f;
        auto children=m.Read(*group+0x14);
        if(children && *children>=0x100000 && *children<=0x3ffffff0) {
            for(U32 i=0;i<3;++i) {
                auto child=m.Read(*children+i*4);
                if(!child || *child<0x100000 || *child>0x3ffffe00) continue;
                auto child_name=m.Read(*child+0x10);
                if(!child_name || !Name(m,*child_name,"plate")) continue;
                auto px=m.Read(*child+0x98),pf=m.Read(*child+0xd4),pd=m.Read(*child+0xd8);
                if(!px || !pf || !pd) break;
                const float psx=std::bit_cast<float>(*px);
                if(!std::isfinite(psx)) break;
                frame.plate=*child;frame.plate_x=*px;frame.plate_flags=*pf;frame.plate_defaults=*pd;
                const float npx=psx*width;
                m.Write(*child+0x98,std::bit_cast<U32>(npx));
                m.Write(*child+0xd4,*pf|0x800u);
                m.Write(*child+0xd8,(*pd&~0x800u)|(npx==1.f?0x800u:0));
                break;
            }
        }
        // Locate the native advance indicator by name; this is separate from
        // branchArrow, which is used for choice selection.
        std::array<U32,64> seen{}; unsigned used=0;
        const U32 indicator=Find(m,root,"waitIcon",seen,used);
        if(indicator && !item_description) {
            auto ix=m.Read(indicator+0x8c),iy=m.Read(indicator+0x90),iflags=m.Read(indicator+0xd4),idefaults=m.Read(indicator+0xd8);
            if(ix && iy && iflags && idefaults) {
                const float ox=indicator_x.load(),oy=indicator_y.load();
                const float px=std::bit_cast<float>(*ix),py2=std::bit_cast<float>(*iy);
                if(std::isfinite(ox) && std::isfinite(oy) && std::isfinite(px) && std::isfinite(py2)) {
                    const float nx2=px+std::clamp(ox,-200.f,200.f),ny2=py2+std::clamp(oy,-64.f,64.f);
                    frame.indicator=indicator;frame.indicator_x=*ix;frame.indicator_y=*iy;frame.indicator_flags=*iflags;frame.indicator_defaults=*idefaults;
                    m.Write(indicator+0x8c,std::bit_cast<U32>(nx2));m.Write(indicator+0x90,std::bit_cast<U32>(ny2));
                    m.Write(indicator+0xd4,*iflags|0x300u);
                    m.Write(indicator+0xd8,(*idefaults&~0x300u)|(nx2==0.f?0x100u:0)|(ny2==0.f?0x200u:0));
                }
            }
        }
        // old_pos is the render pose, already copied and animated by LayoutBase::calc.
        // Apply scale and an additive Y offset after native animation; wrapping stays native.
        m.Write(*group+0x90,std::bit_cast<U32>(translated_y));
        m.Write(*group+0x8c,std::bit_cast<U32>(translated_x));
        m.Write(*group+0x98,std::bit_cast<U32>(nx));m.Write(*group+0x9c,std::bit_cast<U32>(ny));
        m.Write(*group+0xd4,*flags|0x1b00);
        m.Write(*group+0xd8,(*defaults&~0x1b00u)|(translated_x==0.f?0x100u:0)|(translated_y==0.f?0x200u:0)|(nx==1.f?0x800u:0)|(ny==1.f?0x1000u:0));
    }
    template<class Memory> void End(Memory& m,U32 layout) {
        if(frames.empty()) return;
        const auto frame=frames.back();frames.pop_back();
        if(frame.layout!=layout || !frame.widget) return;
        m.Write(frame.widget+0x98,frame.x);m.Write(frame.widget+0x9c,frame.y);
        m.Write(frame.widget+0x90,frame.position_y);
        m.Write(frame.widget+0x8c,frame.position_x);
        m.Write(frame.widget+0xd8,frame.defaults);
        if(frame.plate) {
            m.Write(frame.plate+0x98,frame.plate_x);
            m.Write(frame.plate+0xd4,frame.plate_flags);
            m.Write(frame.plate+0xd8,frame.plate_defaults);
        }
        if(frame.indicator) {
            m.Write(frame.indicator+0x8c,frame.indicator_x);m.Write(frame.indicator+0x90,frame.indicator_y);
            m.Write(frame.indicator+0xd4,frame.indicator_flags);m.Write(frame.indicator+0xd8,frame.indicator_defaults);
        }
    }
};
}
