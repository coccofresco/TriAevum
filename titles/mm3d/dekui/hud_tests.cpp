// SPDX-License-Identifier: GPL-2.0-or-later
// S2 standalone tests for the tip 4f2692ed HUD changes: mirrored item-button backgrounds
// (HudItemBackground) and the native minimap's pending-quad discard. The memory harness and
// the minimap scenarios are adapted from mm3d-dekui 4f2692ed tools/mm3d/native_inventory_check.cpp
// and player_hud_check.cpp (tgacquin/mm3d-dekui, GPL-2.0-or-later), with our v1.1 literals.
#include "hud_item_background.h"
#include "native_minimap.h"
#include "player_hud.h"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
using namespace Mm3dDekui;
using U32=std::uint32_t;
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}

struct Memory {
    std::unordered_map<U32,U32> words;
    U32 next=0x1000000;
    std::optional<U32> Read(U32 p){auto i=words.find(p);return i==words.end()?std::nullopt:std::optional<U32>(i->second);}
    void Write(U32 p,U32 v){Check(words.contains(p),"write to mapped memory only");words[p]=v;}
    U32 Alloc(U32 size){U32 p=next;next+=(size+7)&~7u;for(U32 i=p;i<next;i+=4)words[i]=0;return p;}
    U32 String(const char* s){
        const auto n=std::char_traits<char>::length(s)+1;U32 p=Alloc(U32(n));
        for(unsigned i=0;i<n;++i)words[p+(i&~3u)]|=U32(static_cast<unsigned char>(s[i]))<<((i&3)*8);
        return p;
    }
    void WidgetAt(U32 p,const char* name){
        words[p+0x10]=String(name);words[p+0x24]=0xffff;words[p+0xd4]=15;
        for(U32 i=0;i<3;++i)words[p+0xe0+i*16+i*4]=std::bit_cast<U32>(1.f);
    }
    U32 Widget(const char* name){auto p=Alloc(0x140);WidgetAt(p,name);return p;}
    U32 Layout(const char* name){
        auto p=Alloc(0x200),resource=Alloc(0x20);words[p+4]=resource;words[resource+0x18]=String(name);
        WidgetAt(p+8,"root");return p;
    }
    void Children(U32 p,const std::vector<U32>& nodes){
        auto list=Alloc(U32(nodes.size()*4));words[p+0x14]=list;words[p+0x18]=U32(nodes.size());
        for(unsigned i=0;i<nodes.size();++i)words[list+i*4]=nodes[i];
    }
};
float F(const Memory& m,U32 at){return std::bit_cast<float>(m.words.at(at));}

struct Slot { U32 owner,pane,geometry; };
struct Fixture {
    Memory m;U32 layout=0,stack=0,sp=0;std::array<Slot,4> slots{};
    Fixture(const char* name="HudItemIconSet"){
        layout=m.Layout(name);
        auto all=m.Widget("all");m.Children(layout+8,{all});
        constexpr std::array<const char*,4> groups{"icon_item_x_g","icon_item_y_g","icon_item_1_g","icon_item_2_g"};
        constexpr std::array<const char*,4> bases{"icon_x_base","icon_y_base","icon_1_base","icon_2_base"};
        std::vector<U32> group_nodes;
        for(unsigned i=0;i<4;++i){
            auto group=m.Widget(groups[i]),instance=m.Widget(bases[i]),embedded=m.Layout("ItemIconBase");
            m.words[instance+0x20]=embedded;m.Children(group,{instance});
            const U32 owner=embedded+8;
            auto pane=m.Widget("bb00_ybtn"),geometry=m.Alloc(0x80);m.Children(owner,{pane});
            m.words[pane+0x28]=geometry;m.words[geometry+4]=6;
            m.words[geometry+0x18]=std::bit_cast<U32>(10.f+i);m.words[geometry+0x24]=std::bit_cast<U32>(32.f);
            m.words[geometry+0x2c]=std::bit_cast<U32>(.25f);m.words[geometry+0x34]=std::bit_cast<U32>(.5f);
            slots[i]={owner,pane,geometry};group_nodes.push_back(group);
        }
        m.Children(all,group_nodes);
        stack=m.Alloc(0x2800);sp=stack+0x2800;
        for(U32 at=stack;at<sp;at+=4)m.words[at]=0xa5a5a5a5u;
    }
};

void HudItemBackgroundChecks(){
    HudAdjustments::Options o;o.mirror_item_buttons=true;HudAdjustments::Apply(o);
    {
        Fixture f;HudItemBackground::Hook hook;const auto before=f.m.words;const U32 content=f.layout+8;
        const U32 base=hook.Root(f.m,f.layout,content,f.sp);
        Check(base==f.sp-0x1200,"mirrored root is the scratch copy below SP");
        Check(f.m.words.at(base+0x14)==f.m.words.at(content+0x14),"scratch root keeps the native children");
        for(unsigned i=0;i<4;++i){
            const auto& s=f.slots[i];const U32 block=base+0x180+i*0x400;
            Check(f.m.words.at(s.owner+0x14)==block&&f.m.words.at(s.owner+0x18)==2,"owner points at two scratch halves");
            for(unsigned half=0;half<2;++half){
                const U32 node=block+0x10+half*0x200,geo=node+0x140;
                Check(f.m.words.at(block+half*4)==node,"half listed under the owner");
                Check(f.m.words.at(node+0x28)==geo&&f.m.words.at(node+0x10)==f.m.words.at(s.pane+0x10),"half clones the pane");
                const float y=10.f+i;
                Check(F(f.m,geo+0x18)==y+half*16.f&&F(f.m,geo+0x24)==16.f,"half covers its exact half of the height");
                Check(F(f.m,geo+0x2c)==(half?.25f+.25f:.25f+.5f)&&F(f.m,geo+0x34)==(half?.25f:-.25f),"lower half mirrored upward");
            }
            Check(f.m.words.at(s.geometry+0x18)==before.at(s.geometry+0x18),"native geometry untouched");
        }
        Check(hook.scratch.End(f.m)==f.sp,"End returns the hook SP");
        Check(f.m.words==before,"End restores every word byte for byte");
        Check(hook.scratch.End(f.m)==0,"no frame left");
    }
    // Every failure leaves memory unchanged and returns the incoming content root.
    const auto unchanged=[](Fixture& f,U32 sp,const char* what){
        HudItemBackground::Hook hook;const auto before=f.m.words;
        Check(hook.Root(f.m,f.layout,f.layout+8,sp)==f.layout+8,what);
        Check(f.m.words==before,what);
        Check(hook.scratch.End(f.m)==0&&f.m.words==before,what);
    };
    {Fixture f;f.m.words[f.slots[3].owner+0x18]=2;unchanged(f,f.sp,"one slot with two children rejects all four");}
    {Fixture f;f.m.words[f.slots[2].geometry+4]=4;unchanged(f,f.sp,"non-picture pane rejects");}
    {Fixture f;f.m.words[f.slots[1].geometry+0x24]=std::bit_cast<U32>(0.f);unchanged(f,f.sp,"zero height rejects");}
    {Fixture f;f.m.words.erase(f.slots[0].geometry+0x34);unchanged(f,f.sp,"unreadable geometry rejects");}
    {Fixture f;unchanged(f,f.sp-4,"misaligned SP rejects");}
    {Fixture f;for(U32 at=f.sp-0x2400;at<f.sp-0x2200;at+=4)f.m.words.erase(at);unchanged(f,f.sp,"unmapped scratch rejects");}
    {Fixture f("ItemIconBase");unchanged(f,f.sp,"other layouts (the real B button) are never touched");}
    {Fixture f;HudAdjustments::Options off;HudAdjustments::Apply(off);unchanged(f,f.sp,"mirror off is inert");HudAdjustments::Apply(o);}
    {
        Fixture f;HudItemBackground::Hook hook;const auto before=f.m.words;
        const U32 first=hook.Root(f.m,f.layout,f.layout+8,f.sp);Check(first!=f.layout+8,"first Root mirrors");
        const auto mirrored=f.m.words;
        Check(hook.Root(f.m,f.layout,f.layout+8,first)==f.layout+8&&f.m.words==mirrored,"nested Root is inert");
        Check(hook.scratch.End(f.m)==0,"nested End restores nothing");
        Check(hook.scratch.End(f.m)==f.sp&&f.m.words==before,"outer End restores");
    }
    HudAdjustments::Apply({});
    std::cout<<"PASS HudItemBackground mirrored halves, all-or-nothing commit, byte-for-byte End\n";
}

void HideMinimapChecks(){
    namespace N=NativeMinimap;namespace H=HudAdjustments;
    for(bool suppress:{false,true}){
        Memory m;N::Hook hook;
        for(unsigned q=0;q<8;++q){m.words[N::Count(q)]=0;m.words[N::Pointer(q,0)]=0;m.words[N::Priority(q,0)]=0;}
        auto map=m.Alloc(0x400),radar=m.Alloc(0x400),other=m.Alloc(0x400),top=m.Alloc(0x400);
        m.words[0x6890ec]=map;m.words[0x6890f0]=0;m.words[0x727970+0x828]=radar;
        m.words[N::Count(4)]=1;m.words[N::Pointer(4,0)]=top;m.words[N::Priority(4,0)]=5;
        m.words[N::Count(6)]=1;m.words[N::Pointer(6,0)]=other;m.words[N::Priority(6,0)]=7;
        H::Options o;o.hide_minimap=!suppress;H::Apply(o);hook.Begin(m,0,true,0,suppress);
        m.words[N::Count(6)]=3;
        m.words[N::Pointer(6,0)]=map;m.words[N::Priority(6,0)]=1;
        m.words[N::Pointer(6,1)]=other;m.words[N::Priority(6,1)]=7;
        m.words[N::Pointer(6,2)]=radar;m.words[N::Priority(6,2)]=9;
        for(U32 object:{map,radar}){m.words[object+0x2c4]=64;m.words[object+0x2c8]=12;}
        auto before=m.words;hook.End(m,0,true,.36f,.85f,400);
        Check(H::hide_minimap==!suppress,"transient suppression never changes preferences");
        Check(m.words[map+0x2c8]==0&&m.words[radar+0x2c8]==0,"removed objects' pending quads discarded");
        Check(m.words[N::Count(6)]==1&&m.words[N::Pointer(6,0)]==other&&m.words[N::Priority(6,0)]==7,"unrelated lower-screen object kept");
        Check(m.words[N::Count(4)]==1&&m.words[N::Pointer(4,0)]==top,"other queues untouched");
        for(auto [address,value]:before)
            if(address!=N::Count(6)&&address!=N::Pointer(6,0)&&address!=N::Priority(6,0)&&address!=map+0x2c8&&address!=radar+0x2c8)
                Check(m.words[address]==value,"no other word changes");
        // Hidden gameplay frames must not accumulate overview geometry (the stale-icon bug).
        for(unsigned frame=0;frame<3;++frame){
            hook.Begin(m,0,true,0,suppress);
            m.words[N::Count(6)]=3;
            m.words[N::Pointer(6,1)]=map;m.words[N::Priority(6,1)]=1;
            m.words[N::Pointer(6,2)]=radar;m.words[N::Priority(6,2)]=9;
            m.words[map+0x2c8]+=4;m.words[radar+0x2c8]+=8;
            hook.End(m,0,true,.36f,.85f,400);
            Check(m.words[map+0x2c8]==0&&m.words[radar+0x2c8]==0,"no accumulation across hidden frames");
        }
        // An invalid second object (count above capacity) must not partially discard the first.
        m.words=before;m.words[radar+0x2c8]=65;const auto invalid=m.words;
        hook.armed=true;hook.End(m,0,true,.36f,.85f,400);Check(m.words==invalid,"invalid object: no partial discard");
        hook.Begin(m,0,true);Check(!hook.suppress_draw,"normal gameplay resumes immediately");
        H::Apply({});Check(!H::hide_minimap,"reset");
    }
    std::cout<<"PASS minimap pending-quad discard (hide option and transient suppression)\n";
}

void SmallGameplayMapChecks(){
    namespace N=NativeMinimap;
    Memory m;N::Hook hook;constexpr U32 state=0x6890f4,mode=0x7786f8;
    auto header=m.Alloc(0x20);m.words[0x0036e268]=0x7781d8;m.words[state]=header; // v1.1 literal (ref 0x36e2a0)
    const auto game=m.Alloc(0xf0d8),actor=m.Alloc(0x30),room=m.Alloc(0x728);
    m.words[game+0x20dc]=actor;m.words[header]=room;m.words[header+4]=1;
    m.words[header+0x10]=std::bit_cast<U32>(1.f);
    m.words[actor+0x24]=std::bit_cast<U32>(40.f);m.words[actor+0x2c]=std::bit_cast<U32>(30.f);
    m.words[state+0x54]=0;m.words[state+0x5c]=0;
    m.words[state+0x50]=160|(180u<<16); // zoomed mode's stale player anchor
    m.words[0x727970+0x834]=0x01123456; // large markers plus unrelated packed bytes
    m.words[header+0xc]=0xfff60014;      // center 20,-10
    m.words[state+0x20]=std::bit_cast<U32>(.5f);
    for(unsigned zoom_mode:{1u,2u,3u})for(float rotation:{0.f,.75f,-1.5f}){
        m.words[mode]=(zoom_mode<<24)|0x123456;
        m.words[state+0x28]=std::bit_cast<U32>(rotation);
        m.words[state+0x1c]=std::bit_cast<U32>(2.f);m.words[state+0x2c]=std::bit_cast<U32>(42.f);m.words[state+0x30]=std::bit_cast<U32>(73.f);
        auto before=m.words;hook.SmallGameplayMap(m,game);
        Check(m.words[mode]==0x03123456&&m.words[state+0x1c]==m.words[state+0x20],"overview mode for the small map");
        Check(std::bit_cast<float>(m.words[state+0x28])==0.f,"cached rotation cleared for this draw");
        Check(std::bit_cast<float>(m.words[state+0x2c])==20&&std::bit_cast<float>(m.words[state+0x30])==-10,"overview centre");
        Check(m.words[state+0x50]==(170u|(140u<<16)),"player projected onto the overview");
        Check(m.words[0x727970+0x834]==0x00123456,"small icons, neighbouring bytes preserved");
        hook.RestoreZoom(m);Check(m.words==before,"Maps screen state restored");
    }
    std::cout<<"PASS small gameplay map overview, player marker and restore\n";
}
} // namespace

int main(){
    try{
        HudItemBackgroundChecks();HideMinimapChecks();SmallGameplayMapChecks();
        std::cout<<"PASS "<<checks<<" HUD checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
}
