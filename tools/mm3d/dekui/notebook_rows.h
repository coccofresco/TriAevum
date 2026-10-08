// Ported from mm3d-dekui src/mm3d/notebook_rows.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
#include "notebook_scrollbar.h"
#include <chrono>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
namespace Mm3dDekui::NotebookRows {
using namespace NativeInventory;
inline std::atomic<bool> enabled{true};
constexpr U32 EnterPC=0x005ab494,ExitPC=0x005ab9c0,FirstSVC=0xffffd9; // ref 0x5ab324, 0x5ab850
constexpr U32 SyncPC=0x005b3864,SyncSVC=0xffffdb; // ref 0x5b36f4
inline U32 Instruction(U32 pc,U32 word) {
    if(pc==EnterPC&&word==0xe1a04000)return 0xef000000|FirstSVC;
    if(pc==ExitPC&&word==0xe1a00004)return 0xef000000|(FirstSVC+1);
    if(pc==SyncPC&&word==0xe1a04000)return 0xef000000|SyncSVC;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    return m.Read(SyncPC-4)==std::optional<U32>(0xe92d41f0)&&
        m.Read(SyncPC)==std::optional<U32>(0xe1a04000)&&
        m.Read(SyncPC+4)==std::optional<U32>(0xe1a07001)&&
        m.Read(SyncPC+8)==std::optional<U32>(0xe5b01068)&&
        m.Read(EnterPC-8)==std::optional<U32>(0xe92d4ff7)&&
        m.Read(EnterPC-4)==std::optional<U32>(0xe24dd038)&&
        m.Read(EnterPC)==std::optional<U32>(0xe1a04000)&&
        m.Read(EnterPC+4)==std::optional<U32>(0xebef8ecc)&& // ref 0xebef8f7f
        m.Read(ExitPC-4)==std::optional<U32>(0xe28dd044)&&
        m.Read(ExitPC)==std::optional<U32>(0xe1a00004)&&
        m.Read(ExitPC+4)==std::optional<U32>(0xe8bd4ff0);
}
inline std::mutex capture_mutex;
inline std::vector<std::string> capture_lines;
inline std::atomic<long long> capture_until{0};
inline long long Now() {return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
inline void StartCapture() {
    std::scoped_lock lock(capture_mutex);capture_lines.clear();capture_until=Now()+15000;
}
struct Hook {
    using Write=NativeInventory::Hook::Write;
    U32 layout=0,observed_owner=0;
    unsigned calls=0;
    long long last_sample=0;
    unsigned padding=0;
    bool tail_window=false;
    std::array<int,9> positions{0,1,2,3,4,5,6,7,8};
    std::array<U32,9> anchors{};
    std::vector<Write> restore;
    std::unordered_map<U32,float> row_alpha;
    template<class M> void Record(M& m,const char* phase,U32 current=0) {
        const auto now=Now();if(now>=capture_until.load())return;
        if(std::string_view(phase)=="draw"&&(current!=layout||now-last_sample<10))return;
        if(std::string_view(phase)=="draw")last_sample=now;
        std::ostringstream out;out<<now<<" "<<phase<<" enabled="<<enabled.load()<<" calls="<<calls
            <<" owner="<<std::hex<<observed_owner<<" layout="<<layout<<" current="<<current
            <<std::dec<<" padding="<<padding;
        if(auto state=m.Read(observed_owner+0x254))out<<" start="<<int(static_cast<std::int8_t>(*state&255))<<" selection="<<((*state>>8)&255);
        for(unsigned i=0;i<9;++i) {
            out<<" | "<<i<<":"<<std::hex<<anchors[i];
            for(U32 offset:{0x20u,0x84u,0xd4u,0xecu,0xf4u,0xfcu,0x128u,0x12cu,0x130u,0x134u}) {
                auto value=m.Read(anchors[i]+offset);out<<",";
                if(value)out<<*value;else out<<"?";
            }
            auto child=m.Read(anchors[i]+0x20);
            if(child&&*child) {
                auto id=m.Read(*child+0x178);out<<",id="<<(id?*id:0xffffffff);
                auto flags=m.Read(*child+8+0xd4);out<<",childflags="<<(flags?*flags:0xffffffff);
            }
            out<<",position="<<std::dec<<positions[i];
            if(child&&*child) {
                std::vector<U32> pending{*child+8};unsigned inspected=0;
                while(!pending.empty()&&inspected++<48) {
                    const U32 node=pending.back();pending.pop_back();
                    out<<" node="<<std::hex<<node;
                    for(U32 off:{0x84u,0xd4u,0xecu,0x128u,0x12cu,0x130u,0x134u}) {
                        auto v=m.Read(node+off);out<<",";if(v)out<<*v;else out<<"?";
                    }
                    auto list=m.Read(node+0x14),count=m.Read(node+0x18);
                    if(list&&count&&*count<=32)for(unsigned j=0;j<*count;++j) {
                        auto next=m.Read(*list+4*j);if(next&&*next)pending.push_back(*next);
                    }
                }
            }
            out<<std::dec;
        }
        std::scoped_lock lock(capture_mutex);
        if(capture_lines.size()<2048)capture_lines.push_back(out.str());
    }
    template<class M> bool ImmediateScroll(M& m,U32 owner,U32 flags,U32 animation) {
        if(!enabled.load()||!animation||!(flags&1)||!Pointer(owner,0x258))return false;
        auto up=m.Read(owner+0x34),count=m.Read(owner+0x70);
        return up&&count&&LayoutNamed(m,*up,"UpQuestlist")&&(*count>>24)>=9&&(*count>>24)<=67;
    }
    template<class M> void Enter(M& m,U32 owner) {
        if(observed_owner!=owner)row_alpha.clear();
        ++calls;observed_owner=owner;Record(m,"enter");
        if(!restore.empty())return;
        layout=0;padding=0;
        if(!enabled.load()||!Pointer(owner,0x258))return;
        auto up=m.Read(owner+0x34),state=m.Read(owner+0x254),count=m.Read(owner+0x70);
        if(!up||!LayoutNamed(m,*up,"UpQuestlist")||!state||!count)return;
        const int start=static_cast<std::int8_t>(*state&255);
        const unsigned total=*count>>24;
        if(start< -2||total==0||total>67)return;
        constexpr std::array<unsigned,9> order{7,5,3,1,0,2,4,6,8};
        for(unsigned i=0;i<9;++i) {
            auto p=m.Read(owner+0x138+4*order[i]);
            if(!p||!Pointer(*p,0x140))return;
            anchors[i]=*p;
        }
        padding=unsigned(std::max(0,-start));layout=*up;
        const int load_start=total<9?0:std::clamp(start,0,int(total)-9);
        tail_window=total<9||load_start==int(total)-9;
        const int shift=load_start-start;
        for(int j=0;j<9;++j)positions[(j+shift+18)%9]=j+shift;
        if(!shift)return;
        restore.push_back({owner+0x254,*state,(*state&~255u)|U32(load_start)});
        for(unsigned i=0;i<9;++i)
            restore.push_back({owner+0x138+4*order[i],anchors[i],anchors[(int(i)+shift+18)%9]});
        for(auto w:restore)m.Write(w.address,w.after);
    }
    template<class M> void Leave(M& m) {
        Record(m,"loaded");
        for(auto w:restore)m.Write(w.address,w.before);
        restore.clear();
    }
    template<class M> bool Scrollbar(M& m,NativeInventory::Hook& helper,std::vector<Write>& writes,
                                    float bottom,float fraction,float visible_fraction) {
        auto layout_ptr=m.Read(observed_owner+0x78);
        if(!layout_ptr||!LayoutNamed(m,*layout_ptr,"ScrollBar"))return true;
        return NotebookScrollbar::Resize(m,*layout_ptr,helper,writes,bottom,fraction,visible_fraction);
    }
    template<class M> bool Expand(M& m,U32 current,NativeInventory::Hook& helper,std::vector<Write>& writes) {
        Record(m,"draw",current);
        const bool cursor=layout&&LayoutNamed(m,current,"CursorQuestPlate");
        if(current!=layout&&!cursor) {
            // Attached QuestPlate layouts are submitted separately too. Their
            // computed opacity can be regenerated after the parent draw ends.
            auto child=m.Read(anchors[0]+0x20),alpha=m.Read(anchors[0]+0x134);
            if(child&&*child==current&&alpha&&*alpha==0&&LayoutNamed(m,current,"QuestPlate")) {
                std::vector<U32> visited;std::vector<Write> identity;
                if(!helper.Transform(m,current+8,identity,visited,{1,0,0,1,0,0}))return false;
                for(auto node:visited)if(auto saved=row_alpha.find(node+0x134);saved!=row_alpha.end())
                    if(!helper.Stage(m,writes,node+0x134,std::bit_cast<U32>(saved->second)))return false;
            }
            return true;
        }
        // Attachment visibility cannot override a hidden animated parent group.
        if(!cursor) {
            auto children=m.Read(layout+8+0x14),count=m.Read(layout+8+0x18);
            if(children&&count&&*count==1&&Pointer(*children)) {
                auto all=m.Read(*children);
                if(all&&Named(m,*all,"all")) {
                    auto list=m.Read(*all+0x14),n=m.Read(*all+0x18);
                    if(list&&n&&*n==13&&Pointer(*list,52)) {
                        for(unsigned j=0;j<*n;++j) {
                            auto node=m.Read(*list+j*4);if(!node)return false;
                            if(Named(m,*node,"null_plate07")||Named(m,*node,"null_plate08")||
                               Named(m,*node,"null_plate05")||Named(m,*node,"null_plate06")) {
                                auto flags=m.Read(*node+0xd4);if(!flags)return false;
                                if(!helper.Stage(m,writes,*node+0xd4,*flags|1u))return false;
                                // The bound anchors are null_pointer leaves,
                                // underneath these null_plate groups. Native
                                // rendering rejects a zero-alpha group before
                                // ever reaching the repaired anchor/subtree.
                                if(Named(m,*node,"null_plate07")) {
                                    auto alpha=m.Read(*node+0x134),parent=m.Read(*all+0x134);
                                    if(alpha&&parent&&*alpha==0&&
                                       !helper.Stage(m,writes,*node+0x134,*parent))return false;
                                }
                            }
                        }
                    }
                }
            }
        }
        std::vector<U32> seen;
        auto state=m.Read(observed_owner+0x254);if(!state)return false;
        const int selected=int((*state>>8)&255)-int(static_cast<std::int8_t>(*state&255));
        int hidden=-1;
        // Fixed local origin matches the first native list position (Y=51).
        // Keep the layout's own parent transform, rather than following an
        // animated center row.
        auto root_y=m.Read(layout+8+0xfc),root_basis=m.Read(layout+8+0xf4);
        if(!root_y||!root_basis)return false;
        const float origin=std::bit_cast<float>(*root_y)+51.f*std::bit_cast<float>(*root_basis);
        const float spacing=38.f*std::bit_cast<float>(*root_basis);
        if(!std::isfinite(origin)||!std::isfinite(spacing)||spacing<=0||spacing>100)return false;
        std::vector<unsigned> ordered;
        for(unsigned i=0;i<9;++i) {
            auto child=m.Read(anchors[i]+0x20);if(!child)return false;
            if(*child)ordered.push_back(i);
        }
        std::sort(ordered.begin(),ordered.end(),[&](unsigned a,unsigned b){return positions[a]<positions[b];});
        // Headers occupy a full row too: keep every populated window within
        // eight rows, with the selected event and cursor inside the viewport.
        if(ordered.size()>8) {
            auto first_child=m.Read(anchors[ordered.front()]+0x20);
            const bool first_is_event=first_child&&LayoutNamed(m,*first_child,"QuestPlate");
            // A short list can already be the final loaded window while its
            // first category is still on screen. Keep that heading until the
            // selection actually needs the ninth row.
            const bool hide_first=positions[ordered.back()]==selected||
                (tail_window&&first_is_event&&positions[ordered.front()]!=selected);
            hidden=int(hide_first?ordered.front():ordered.back());
            ordered.erase(hide_first?ordered.begin():ordered.end()-1);
        }
        // The native animation has tracks for a full eight-row window. With
        // fewer rows (including category/banner rows), recycled slots do not
        // carry a reliable shared phase. Mirror each live slot around its
        // resting X instead; this preserves left-in/left-out motion for
        // partially populated notebooks.
        const bool partial_window=ordered.size()<8;
        std::array<float,9> targets{};
        for(unsigned rank=0;rank<ordered.size();++rank)targets[ordered[rank]]=origin+float(rank)*spacing;
        // UpQuestlist's nine null_plate anchors rest at local X=212.
        // Preserve the working entrance. For exit, derive the native clock
        // from the seven complete linear tracks, then stagger by visible rank.
        // Spare slots have no exit X track and physical slots rotate on scroll.
        auto root_x=m.Read(layout+8+0xec),root_x_basis=m.Read(layout+8+0xe0);
        if(!root_x||!root_x_basis)return false;
        auto buffer_alpha=m.Read(anchors[0]+0x134);
        const bool buffer_exit=buffer_alpha&&std::bit_cast<float>(*buffer_alpha)==0.f;
        const float rest_x=std::bit_cast<float>(*root_x)+212.f*std::bit_cast<float>(*root_x_basis);
        const float travel=388.f*std::bit_cast<float>(*root_x_basis);
        float exit_frame=2.f;
        if(buffer_exit) {
            if(!std::isfinite(travel)||travel<=0)return false;
            // Native out_up_questlist_anim: 30fps, start frames2..8,
            // three-frame linear travel212->600, last finishes at frame11.
            for(unsigned i=1;i<=7;++i) {
                auto x=m.Read(anchors[i]+0xec);if(!x)return false;
                const float progress=(std::bit_cast<float>(*x)-rest_x)/travel;
                if(!std::isfinite(progress))return false;
                if(progress>0)exit_frame=std::max(exit_frame,float(i+1)+3.f*std::min(progress,1.f));
            }
        }

        const auto slide=[&](unsigned i)->std::optional<float> {
            auto x=m.Read(anchors[i]+0xec);if(!x)return {};
            if(partial_window) {
                const float current=std::bit_cast<float>(*x);
                const float dx=2.f*rest_x-2.f*current;
                if(!std::isfinite(dx))return {};
                return dx;
            }
            if(buffer_exit) {
                auto row=std::find(ordered.begin(),ordered.end(),i);
                const float rank=float(row-ordered.begin());
                const float delay=2.f+6.f*rank/float(std::max(std::size_t(2),ordered.size())-1);
                const float dx=rest_x-travel*std::clamp((exit_frame-delay)/3.f,0.f,1.f)-std::bit_cast<float>(*x);
                if(!std::isfinite(dx))return {};
                return dx;
            }
            unsigned source=i;
            if(i==0||i==8) {
                if(positions[8]<positions[0])source=i==8?0:8;
            }
            auto phase=m.Read(anchors[source]+0xec);if(!phase)return {};
            const float dx=2.f*rest_x-std::bit_cast<float>(*phase)-std::bit_cast<float>(*x);
            if(!std::isfinite(dx))return {};
            return dx;
        };
        if(cursor) {
            for(auto i:ordered)if(positions[i]==selected) {
                auto old=m.Read(current+8+0xfc);if(!old)return false;
                auto dx=slide(i);if(!dx)return false;
                return helper.Transform(m,current+8,writes,seen,{1,0,0,1,*dx,targets[i]-std::bit_cast<float>(*old)});
            }
            return true;
        }
        auto total_bits=m.Read(observed_owner+0x70);
        if(total_bits&&!ordered.empty()) {
            const int total=int(*total_bits>>24);
            const int first=int(static_cast<std::int8_t>(*state&255))+positions[ordered.front()];
            const int range=std::max(1,total-int(ordered.size()));
            std::vector<Write> scrollbar_writes;
            // Match the native top inset: track starts 15 units above the
            // first row center, so end 15 below the eighth row center.
            if(Scrollbar(m,helper,scrollbar_writes,origin+7.f*spacing+15.f*std::bit_cast<float>(*root_basis),
                         std::clamp(float(first)/float(range),0.f,1.f),
                         float(ordered.size())/float(std::max(1,total))))
                writes.insert(writes.end(),scrollbar_writes.begin(),scrollbar_writes.end());
        }
        for(unsigned i=0;i<9;++i) {
            auto child=m.Read(anchors[i]+0x20),flags=m.Read(anchors[i]+0xd4);
            auto old=m.Read(anchors[i]+0xfc);
            if(!child||!flags||!old)return false;
            if(!*child)continue;
            const auto visited_start=seen.size();
            const float dy=int(i)==hidden?0.f:targets[i]-std::bit_cast<float>(*old);
            auto dx=slide(i);
            if(!dx||!std::isfinite(dy)||!helper.Transform(m,anchors[i],writes,seen,{1,0,0,1,*dx,dy}))return false;
            if(i==0) {
                // The native exit zeros inherited opacity throughout slot07.
                // Retain each descendant's last visible opacity (including
                // intentionally transparent children), scoped to this draw.
                for(auto j=visited_start;j<seen.size();++j) {
                    const U32 at=seen[j]+0x134;auto bits=m.Read(at);
                    if(!bits)continue;
                    const float alpha=std::bit_cast<float>(*bits);
                    if(!std::isfinite(alpha)||alpha<0||alpha>1)continue;
                    if(!buffer_exit)row_alpha[at]=alpha;
                    else if(auto old=row_alpha.find(at);old!=row_alpha.end()) {
                        if(!helper.Stage(m,writes,at,std::bit_cast<U32>(old->second)))return false;
                    }
                }
            }
            if(!helper.Stage(m,writes,anchors[i]+0xd4,int(i)==hidden?*flags&~1u:*flags|1u))return false;
        }
        return true;
    }
};
}
