// Ported from mm3d-dekui src/mm3d/message_layout.h (tgacquin/mm3d-dekui 4f2692ed).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "dialogue_scale.h"
#include "native_inventory.h"

namespace Mm3dDekui::MessageLayout {
using namespace NativeInventory;
// Upstream takes Panel from message_interpolation.h, whose snapshots feed the
// renderer's 60 Hz message interpolation (class b, not ported). The value type
// is kept here unchanged; S3's DungeonMap side shadows only read the bounds.
using Source = std::array<std::uint32_t, 4>; // layout, pane, geometry, message ID
struct Panel {
    Source source{};
    std::array<std::array<float,4>,3> transform{};
    std::array<std::array<float,4>,2> uv{};
    bool operator==(const Panel&) const = default;
};
// Capture after native layout calculation and all host placement adjustments,
// immediately before Widget rendering. No animation clock or guest data changes.
template<class M> std::vector<Panel> ReadBranches(M& m,U32 layout,
        const std::vector<U32>& roots,U32 tag,unsigned max_nodes=64,unsigned max_panels=16,bool* valid=nullptr) {
    if(valid)*valid=false;
    std::vector<Panel> panels;
    std::vector<U32> seen;
    const auto number=[&](U32 at)->std::optional<float> {
        const auto bits=m.Read(at);if(!bits)return {};
        const float f=std::bit_cast<float>(*bits);
        return std::isfinite(f)?std::optional<float>{f}:std::nullopt;
    };
    const auto visit=[&](auto&& self,U32 node,unsigned depth)->bool {
        if(!Pointer(node,0x140)||depth>32||seen.size()>=max_nodes||
           std::find(seen.begin(),seen.end(),node)!=seen.end())return false;
        seen.push_back(node);
        const auto flags=m.Read(node+0xd4);const auto alpha=number(node+0x134);
        if(!flags||!alpha)return false;
        if(!(*flags&1)||*alpha<=0)return true;
        const auto geometry=m.Read(node+0x28);if(!geometry)return false;
        if(*geometry) {
            const auto kind=m.Read(*geometry+4);if(!kind)return false;
            // Textured pane's shared base geometry. This includes the plate,
            // waitIcon and branchArrow; glyphs use another type and remain
            // native so text reveal timing is unchanged.
            if((*kind&65535)==6) {
                Panel p;p.source={layout,node,*geometry,tag};
                std::array<float,5> box{};
                for(unsigned i=0;i<5;++i) {
                    auto v=number(*geometry+0x14+4*i);if(!v)return false;box[i]=*v;
                }
                if(box[3]<=0||box[4]<=0||box[3]>2048||box[4]>2048)return false;
                // Native pane renderer multiplies its world matrix by its
                // rectangle. PICA uploads each row in reversed component order.
                for(unsigned r=0;r<3;++r) {
                    std::array<float,4> row{};
                    for(unsigned c=0;c<4;++c) {
                        auto v=number(node+0xe0+16*r+4*c);if(!v)return false;row[c]=*v;
                    }
                    p.transform[r]={row[3]+row[0]*box[0]+row[1]*box[1]+row[2]*box[2],
                        row[2],row[1]*box[4],row[0]*box[3]};
                }
                std::array<float,4> cell{};
                for(unsigned i=0;i<4;++i) {
                    auto v=number(*geometry+0x28+4*i);if(!v)return false;cell[i]=*v;
                }
                for(unsigned r=0;r<2;++r) {
                    std::array<float,3> row{};
                    for(unsigned c=0;c<3;++c) {
                        auto v=number(node+0x110+12*r+4*c);if(!v)return false;row[c]=*v;
                    }
                    p.uv[r]={0,row[0]*cell[0]+row[1]*cell[1]+row[2],row[1]*cell[3],row[0]*cell[2]};
                }
                for(const auto& r:p.transform)for(float v:r)if(!std::isfinite(v))return false;
                for(const auto& r:p.uv)for(float v:r)if(!std::isfinite(v))return false;
                if(panels.size()==max_panels)return false;
                panels.push_back(p);
            }
        }
        const auto embedded=m.Read(node+0x20),group=m.Read(node+0x24);
        if(!embedded||!group)return false;
        if(*embedded)return Pointer(*embedded,0x148)&&self(self,*embedded+8,depth+1);
        // Indirect layout groups follow the same branch as the native renderer.
        if((*group&65535)!=65535) {
            const auto owner=m.Read(node+4);if(!owner||!Pointer(*owner,0x14c))return false;
            const auto count=m.Read(*owner+0x148),list=m.Read(*owner+0x144);
            if(!count||*count>1024||(*group&65535)>=*count||!list||!Pointer(*list,*count*4))return false;
            const auto target=m.Read(*list+(*group&65535)*4);
            return target&&(!*target||(Pointer(*target,0x144)&&self(self,*target+4,depth+1)));
        }
        const auto count=m.Read(node+0x18),list=m.Read(node+0x14);
        if(!count||*count>64||!list||(*count&&!Pointer(*list,*count*4)))return false;
        for(unsigned i=0;i<*count;++i) {
            const auto child=m.Read(*list+4*i);
            if(!child||(*child&&!self(self,*child,depth+1)))return false;
        }
        return true;
    };
    for(U32 root:roots) {
        if(std::find(seen.begin(),seen.end(),root)!=seen.end())continue;
        if(!visit(visit,root,0))return {};
    }
    if(valid)*valid=true;
    return panels;
}
template<class M> std::vector<Panel> Read(M& m,U32 layout,bool indicators=true) {
    if(!LayoutNamed(m,layout,"MessageWindow"))return {};
    DialogueScale::Hook finder;std::array<U32,64> searched{};unsigned used=0;
    // The native composition puts plate directly under all. Avoid exhausting
    // the general search limit on the localized text/front branches before it.
    const auto child=[&](U32 parent,const char* name) -> U32 {
        const auto count=m.Read(parent+0x18),list=m.Read(parent+0x14);
        if(!count||*count>64||!list||!Pointer(*list,*count*4))return 0;
        for(unsigned i=0;i<*count;++i) {
            const auto node=m.Read(*list+4*i);
            if(node&&Named(m,*node,name))return *node;
        }
        return 0;
    };
    const U32 all=child(layout+8,"all");
    U32 plate=all?child(all,"plate"):0;
    if(!plate)plate=finder.Find(m,layout+8,"plate",searched,used);
    const auto message=m.Read(layout+0x3f4);
    if(!plate||!message)return {};
    bool valid=false;
    auto panels=ReadBranches(m,layout,{plate},*message,64,16,&valid);
    if(!valid||!indicators)return panels;
    for(const char* name:{"waitIcon","branchArrow"}) {
        searched.fill(0);used=0;
        const U32 icon=finder.Find(m,layout+8,name,searched,used);
        if(!icon)continue;
        for(const auto& p:ReadBranches(m,layout,{icon},*message)) {
            if(panels.size()<16&&std::none_of(panels.begin(),panels.end(),[&](const auto& old){
                return old.source==p.source;
            }))panels.push_back(p);
        }
    }
    return panels;
}
// Only native selection-frame branches are eligible. Atlas sharing alone is
// insufficient: banners and other artwork may share the cursor texture.
template<class M> std::vector<Panel> ReadCursors(M& m,U32 layout) {
    if(LayoutNamed(m,layout,"CursorItemCollect"))
        return ReadBranches(m,layout,{layout+8},0,256,64);
    if(!LayoutNamed(m,layout,"FileSelect"))return {};
    DialogueScale::Hook finder;std::array<U32,64> searched{};unsigned used=0;
    const U32 cursor=finder.Find(m,layout+8,"cursor_g",searched,used);
    return cursor?ReadBranches(m,layout,{cursor},0,256,64):std::vector<Panel>{};
}
}
