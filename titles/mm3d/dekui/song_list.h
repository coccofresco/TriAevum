// Ported from mm3d-dekui src/mm3d/song_list.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include "native_inventory.h"
#include "notebook_prompt.h"
#include "song_header.h"
#include "song_panel.h"
namespace Mm3dDekui::SongList {
using namespace NativeInventory;
struct Options {bool enabled=true;float background_x=-230,spacing=16,column_spacing=62,stone_darkness=0;GearMenu::Pose songs{24,12,.35f},staff{90,0,.8f},banner{24,0,1};float banner_width=62.3f,banner_height=10.f,panel_left=20.f,panel_right=90.3f,panel_darkness=0.f;std::array<float,3> panel_color{1.f,1.f,1.f};float panel_color_opacity=0.f;};
inline std::atomic<bool> enabled{true};
inline std::atomic<float> background_x{-230},spacing{16},column_spacing{62},stone_darkness{0};
inline GearMenu::AtomicPose songs,staff;
inline std::atomic<float> panel_red{1.f},panel_green{1.f},panel_blue{1.f},panel_color_opacity{0.f};
inline std::atomic<float> panel_left{20.f},panel_right{90.3f},panel_darkness{0.f};
inline std::atomic<float> banner_x{24},banner_y{0},banner_scale{1},banner_width{62.3f},banner_height{10};
inline void Apply(const Options& o){enabled=o.enabled;background_x=o.background_x;spacing=o.spacing;column_spacing=o.column_spacing;stone_darkness=o.stone_darkness;
    songs.x=o.songs.x;songs.y=o.songs.y;songs.scale=o.songs.scale;
    staff.x=o.staff.x;staff.y=o.staff.y;staff.scale=o.staff.scale;
    banner_x=o.banner.x;banner_y=o.banner.y;banner_scale=o.banner.scale;
    banner_width=o.banner_width;banner_height=o.banner_height;panel_left=o.panel_left;panel_right=o.panel_right;panel_darkness=o.panel_darkness;
    panel_red=o.panel_color[0];panel_green=o.panel_color[1];panel_blue=o.panel_color[2];panel_color_opacity=o.panel_color_opacity;}
constexpr std::array<const char*,14> Names{"btn_song_of_time","btn_song_of_heal","btn_song_of_epona","btn_song_of_wing","btn_song_of_strom","btn_sonata","btn_lullaby","btn_lullaby_half","btn_bossanova","btn_elegy","btn_cry","btn_sakasa_uta","btn_kasane_uta","btn_kakashi"};
inline unsigned Slot(unsigned i){return i>6?i-1:i;}
inline unsigned Column(unsigned slot){return slot==12?1:slot%3;}
inline unsigned Row(unsigned slot){return slot/3;}
inline std::array<float,2> Anchor(unsigned i){return {float(i<10?22+62*(i%5):84+62*(i-10)),float(i<5?107:i<10?153:199)};}
constexpr U32 NavPC=0x005a75cc,NavWord=0xe1510002,NavSVC=0xffffb6; // ref 0x5a745c
template<class M> bool CodeMatches(M& m){
    return m.Read(NavPC)==std::optional<U32>(NavWord)&&m.Read(NavPC+4)==std::optional<U32>(0x0a000002)&&
        m.Read(NavPC+8)==std::optional<U32>(0xe1a01007)&&m.Read(NavPC+12)==std::optional<U32>(0xe1a00004);
}
inline U32 Selection(U32 current,U32 buttons,bool full){
    if(current>13)return current;
    int slot=int(Slot(current));
    if(buttons&0x40000040)slot=slot==12?10:slot<3?slot:slot-3;
    else if(buttons&0x80000080)slot=slot>=9?12:slot+3;
    else if(buttons&0x20000020){if(slot!=12&&slot%3) --slot;}
    else if(buttons&0x10000010){if(slot!=12&&slot%3<2) ++slot;}
    return slot<6?slot:slot==6?(full?6:7):slot+1;
}
struct Hook {
    NativeInventory::Hook helper;
    NotebookPrompt::Hook shadow_scratch;
    SongHeader::Hook header;
    SongPanel::Hook backdrop;
    using Write=NativeInventory::Hook::Write;
    U32 panel=0,background=0,pass=~0u;unsigned bg_draw=0;
    std::vector<U32> cursors;
    std::vector<std::vector<Write>> frames;
    GearMenu::Pose column{},caption{};float gap=16,column_gap=62,slide=0,bar_height=0,bar_alpha=0;
    template<class M> void ReadTransition(M& m) {
        slide=0;
        auto players=m.Read(panel+0x14c),count=m.Read(panel+0x150);
        if(!players||!count||*count>64||!Pointer(*players,*count*4))return;
        for(unsigned i=0;i<*count;++i) {
            auto player=m.Read(*players+4*i);if(!player||!Pointer(*player,0x18))continue;
            auto name=m.Read(*player+4),anim=m.Read(*player+0xc),frame=m.Read(*player+0x10);
            if(!name||!DialogueScale::Name(m,*name,"main")||!anim||!Pointer(*anim,0x18)||!frame)continue;
            auto animation=m.Read(*anim+8);if(!animation)return;
            const bool entering=DialogueScale::Name(m,*animation,"in_menu_music_list_anim");
            if(!entering&&!DialogueScale::Name(m,*animation,"out_menu_music_list_anim"))return;
            const float f=std::bit_cast<float>(*frame);
            if(!std::isfinite(f)||f<0||f>8)return;
            const float progress=std::clamp(f/4.f,0.f,1.f);
            const float edge=std::max(column.x+(2*column_gap+54)*column.scale,
                background_x.load()+320*background_pose.scale.load());
            if(!std::isfinite(edge))return;
            slide=-(64+std::max(0.f,edge))*(entering?1-progress:progress);
            return;
        }
    }
    template<class M> U32 Child(M& m,U32 node,const char* name){
        auto list=m.Read(node+0x14),n=m.Read(node+0x18);if(!list||!n||*n>256||!Pointer(*list,*n*4))return 0;
        for(unsigned i=0;i<*n;++i){auto p=m.Read(*list+4*i);if(p&&Named(m,*p,name))return *p;}return 0;
    }
    template<class M> bool Hide(M& m,U32 node,std::vector<Write>& w){auto f=m.Read(node+0xd4);return f&&helper.Stage(m,w,node+0xd4,*f&~1u);}
    template<class M> bool Move(M& m,U32 node,unsigned slot,std::vector<Write>& w,bool geometry=false){
        auto a=Anchor(slot);std::vector<U32> seen;
        auto wx=m.Read(node+0xec),wy=m.Read(node+0xfc);if(!wx||!wy)return false;
        // Button geometry uses absolute coordinates; anchor widgets use local
        // positions. Cancel the source's current world translation in both cases.
        float ox=std::bit_cast<float>(*wx)+(geometry?a[0]-13:0),oy=std::bit_cast<float>(*wy)+(geometry?a[1]-13:0);
        if(!std::isfinite(ox)||!std::isfinite(oy))return false;
        if(!helper.Transform(m,node,w,seen,{column.scale,0,0,column.scale,
            column.x+slide+Column(slot)*column_gap*column.scale+(geometry?0:13*column.scale)-column.scale*ox,column.y+gap*Row(slot)+(geometry?0:13*column.scale)-column.scale*oy}))return false;
        const float tint=1.f-std::clamp(stone_darkness.load(),0.f,1.f);
        if(geometry&&tint<1)for(U32 n:seen) {
            const auto geo=m.Read(n+0x28);if(!geo||!*geo)continue;
            for(unsigned c=0;c<3;++c) {
                const auto value=m.Read(n+0x128+c*4);if(!value)return false;
                const float color=std::bit_cast<float>(*value);if(!std::isfinite(color))return false;
                if(!helper.Stage(m,w,n+0x128+c*4,std::bit_cast<U32>(color*tint)))return false;
            }
        }
        return true;
    }
    template<class M> bool Prepare(M& m,U32 layout,bool top,std::vector<Write>& w){
        if(layout!=panel)return false;
        U32 all=Child(m,panel+8,"all");if(!all)return false;
        U32 music=Child(m,all,"btn_music_g"),anchors=Child(m,all,"frame_set_pos_g");
        U32 notes=Child(m,all,"notes"),bar=Child(m,all,"four_line_staff_g");
        U32 back=Child(m,all,"btn_quit_g"),instrument=Child(m,all,"btn_inst_g");
        if(!music||!anchors||!notes||!bar||!back||!instrument)return false;
        if(!top){for(U32 n:{music,anchors,notes,bar})if(!Hide(m,n,w))return false;return true;}
        if(!Hide(m,back,w)||!Hide(m,instrument,w))return false;
        for(U32 n:{notes,bar}){std::vector<U32> seen;if(!helper.Transform(m,n,w,seen,{caption.scale,0,0,caption.scale,caption.x,caption.y}))return false;}
        auto list=m.Read(music+0x14),count=m.Read(music+0x18);if(!list||!count||*count>64)return false;
        unsigned found=0;
        for(unsigned i=0;i<*count;++i){auto node=m.Read(*list+i*4);if(!node)return false;int slot=-1;
            for(unsigned j=0;j<Names.size();++j)if(Named(m,*node,Names[j])){slot=Slot(j);found|=1u<<j;break;}
            bool backing=false;for(const char* name:{"bb_music_base_pane00","bb_music_base_pane01","bb_music_base_pane02","bb_music_base_pane03","bb_music_base_pane04","bb_music_base_pane05","bb_music_base_pane06","bb_music_base_pane07"})backing|=Named(m,*node,name);
            if(backing){if(!Hide(m,*node,w))return false;continue;}
            if(slot<0){ // Locked-song artwork is a direct sibling of the buttons.
                auto geo=m.Read(*node+0x28);if(!geo||!Pointer(*geo,0x28))return false;
                auto px=m.Read(*geo+0x14),py=m.Read(*geo+0x18);if(!px||!py)return false;
                float gx=std::bit_cast<float>(*px),gy=std::bit_cast<float>(*py);if(!std::isfinite(gx)||!std::isfinite(gy))return false;
                float distance=1e30f;for(unsigned j=0;j<13;++j){auto a=Anchor(j);float d=std::abs(gx-(a[0]-13))+std::abs(gy-(a[1]-13));if(d<distance){distance=d;slot=j;}}
            }
            if(!Move(m,*node,slot,w,true))return false;
        }
        if(found!=0x3fff)return false;
        list=m.Read(anchors+0x14);count=m.Read(anchors+0x18);if(!list||count!=std::optional<U32>(13))return false;
        for(unsigned i=0;i<13;++i){auto n=m.Read(*list+4*i);if(!n||!Move(m,*n,i,w))return false;}
        return true;
    }
    template<class M> void ReadBars(M& m){
        bar_height=bar_alpha=0;
        // Native173AD0: signed animated height at *(6A0148+C)+1,
        // opacity at6A0148. Scope to the game's own letterbox enable flag.
        if(m.Read(0x00173abc)!=std::optional<U32>(0x6a0148)||m.Read(0x00173998)!=std::optional<U32>(0xe1d040d1))return; // ref 0x173c18, 0x173af4
        auto context=m.Read(0x72f5c0);if(!context||!Pointer(*context,0x170))return;
        auto game=m.Read(*context+0x16c);if(!game||!Pointer(*game,0xccb0))return;
        auto active=m.Read(*game+0xccac);if(!active||!(*active&255))return;
        auto state=m.Read(0x6a0154),alpha=m.Read(0x6a0148);if(!state||!alpha||!Pointer(*state))return;
        auto height=m.Read(*state);if(!height)return;
        const unsigned h=(*height>>8)&255;const float a=std::bit_cast<float>(*alpha);
        if(h>120||!std::isfinite(a)||a<0||a>1)return;
        bar_height=float(h);bar_alpha=a;
    }
    template<class M> bool Bar(M& m,bool bottom,std::vector<Write>& writes){
        if(bar_height<=0||bar_alpha<=0)return Hide(m,background+8,writes);
        auto all=Child(m,background+8,"all");auto left=Child(m,all,"common_bottom_bg_pane0"),right=Child(m,all,"common_bottom_bg_pane1");
        if(!left||!right||!Hide(m,left,writes))return false;
        const auto b=helper.frame_background;
        const float edge=helper.brown_shadow_steps?16*b.scale*helper.frame_shadow_width:0;
        const float width=320*b.scale+edge;std::vector<U32> seen;
        if(!helper.Transform(m,right,writes,seen,{width/160,0,0,bar_height/240,b.x-width,bottom?240-bar_height:0}))return false;
        // Match the native bar color (1/255,0,1/255) and animated opacity.
        for(U32 offset:{0x128u,0x12cu,0x130u,0x134u}){
            float v=offset==0x134?bar_alpha:offset==0x12c?0.f:1.f/255;
            if(!helper.Stage(m,writes,right+offset,std::bit_cast<U32>(v)))return false;
        }
        return true;
    }
    template<class M> void Pass(M& m,U32 manager,U32 screen){
        pass=screen;if(screen!=4)return;
        panel=background=0;bg_draw=0;cursors.clear();/* S5: OcarinaInput::Observe(false) */
        if(!enabled)return;
        auto bottom=m.Read(manager+28),top=m.Read(manager+20),bgfrom=m.Read(manager+24),bgto=m.Read(manager+12);
        NativeInventory::Hook::Buckets from,to,bf,bt;
        if(!bottom||!top||!bgfrom||!bgto||!helper.ReadQueue(m,*bottom,from)||!helper.ReadQueue(m,*top,to)||!helper.ReadQueue(m,*bgfrom,bf)||!helper.ReadQueue(m,*bgto,bt))return;
        // The native ocarina panel is retained by the music-list controller.
        // Capture its banner while it is queued, then validate its identity on draw.
        for(auto& bucket:from)for(U32 p:bucket)if(LayoutNamed(m,p,"MenuOcarina"))header.source_layout=p;
        for(auto& bucket:from)for(U32 p:bucket)if(LayoutNamed(m,p,"MenuMusicList")){if(panel){panel=0;return;}panel=p;}
        if(!panel)return;
        /* S5: OcarinaInput::Observe(true) */
        ReadBars(m);
        for(auto& bucket:bf)for(U32 p:bucket)if(LayoutNamed(m,p,"CommonBottomBg")){if(background){panel=0;return;}background=p;}
        for(auto& bucket:from)for(U32 p:bucket)if(LayoutNamed(m,p,"CursorItemCollect"))cursors.push_back(p);
        column={songs.x.load(),songs.y.load(),songs.scale.load()};caption={staff.x.load(),staff.y.load(),staff.scale.load()};gap=spacing.load();column_gap=column_spacing.load();
        for(float v:{column.x,column.y,column.scale,caption.x,caption.y,caption.scale,gap,column_gap})if(!std::isfinite(v)){panel=0;return;}
        if(column.scale<.1f||column.scale>2||caption.scale<.1f||caption.scale>2||gap<0||gap>60||column_gap<40||column_gap>180){panel=0;return;}
        ReadTransition(m);
        helper.frame_background={background_x.load()+slide,background_pose.y.load(),background_pose.scale.load()};
        auto b=helper.frame_background;
        if(!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.scale)||b.scale<.25f||b.scale>2){panel=0;return;}
        std::vector<Write> preview;if(!Prepare(m,panel,true,preview)||!Prepare(m,panel,false,preview)){panel=0;return;}
        if(to[15].size()+1+cursors.size()>32||bt[0].size()+4>32){panel=0;return;}
        for(auto& bucket:to)if(std::find(bucket.begin(),bucket.end(),panel)!=bucket.end()){panel=0;return;}
        helper.ConfigureBrownShadow(m,background,32-bt[0].size()-4,shadow_width.load(),shadow_opacity.load());
        to[15].push_back(panel);for(U32 c:cursors){to[15].push_back(c);for(auto& bucket:from)std::erase(bucket,c);}
        if(background){for(auto& bucket:bf)std::erase(bucket,background);bt[0].insert(bt[0].begin(),4+helper.brown_shadow_steps,background);}
        for(auto [q,buckets]:{std::pair{*bottom,&from},std::pair{*top,&to},std::pair{*bgfrom,&bf},std::pair{*bgto,&bt}}){
            for(unsigned p=0;p<16;++p)for(unsigned i=0;i<(*buckets)[p].size();++i)if(!m.Read(q+4+p*0x80+i*4)){panel=0;return;}
        }
        for(auto [q,buckets]:{std::pair{*bottom,&from},std::pair{*top,&to},std::pair{*bgfrom,&bf},std::pair{*bgto,&bt}}){
            unsigned total=0;for(unsigned p=0;p<16;++p){auto& bucket=(*buckets)[p];for(unsigned i=0;i<bucket.size();++i)m.Write(q+4+p*0x80+i*4,bucket[i]);m.Write(q+0x804+p*4,bucket.size());total+=bucket.size();}m.Write(q+0x844,total);
        }
    }
    template<class M> void Begin(M& m,U32 layout){
        frames.emplace_back();if(!panel)return;std::vector<Write> writes;
        if(layout==panel&&(pass==3||pass==5)){if(!Prepare(m,layout,pass==3,writes))return;}
        else if(layout==background&&pass==1){
            if(bg_draw>=helper.brown_shadow_steps+2){if(!Bar(m,(bg_draw++-helper.brown_shadow_steps-2)%2,writes))return;}
            else if(backdrop.Source(m,panel)){++bg_draw;if(!Hide(m,layout+8,writes))return;}
            else if(bg_draw<helper.brown_shadow_steps){if(!helper.PrepareBrownShadow(m,layout,bg_draw++,writes))return;}
            else{auto b=helper.frame_background;bool upper=(bg_draw++-helper.brown_shadow_steps)%2;std::vector<U32> seen;
                if(!helper.Transform(m,layout+8,writes,seen,{b.scale,0,0,b.scale,b.x,b.y-(upper?240*b.scale:0)}))return;}
        }else if(pass==3&&std::find(cursors.begin(),cursors.end(),layout)!=cursors.end()){
            auto px=m.Read(layout+8+0xec),py=m.Read(layout+8+0xfc);if(!px||!py)return;
            float cx=std::bit_cast<float>(*px),cy=std::bit_cast<float>(*py),distance=1e30f;unsigned slot=0;
            if(!std::isfinite(cx)||!std::isfinite(cy))return;
            for(unsigned i=0;i<13;++i){auto a=Anchor(i);float d=std::abs(cx-a[0])+std::abs(cy-a[1]);if(d<distance){distance=d;slot=i;}}
            auto a=Anchor(slot);std::vector<U32> seen;
            if(!helper.Transform(m,layout+8,writes,seen,{column.scale,0,0,column.scale,column.x+slide+Column(slot)*column_gap*column.scale-column.scale*(a[0]-13),column.y+gap*Row(slot)-column.scale*(a[1]-13)}))return;
        }else return;
        for(auto w:writes)m.Write(w.address,w.after);
        frames.back()=std::move(writes);
    }
    template<class M> U32 ShadowRoot(M& m,U32 layout,U32 sp) {
        shadow_scratch.frames.emplace_back();auto& frame=shadow_scratch.frames.back();
        const U32 root=layout+8;
        if(pass!=3||!panel||layout!=panel)return root;
        const U32 all=Child(m,root,"all"),music=Child(m,all,"btn_music_g");
        if(!music)return root;
        std::vector<Write> unused;std::vector<U32> nodes;
        if(!helper.Transform(m,music,unused,nodes,{1,0,0,1,0,0}))return root;
        U32 source=0;for(auto n:nodes)if(Named(m,n,"btn_song_of_time_pane")){source=n;break;}
        auto geometry=m.Read(source+0x28),alpha=m.Read(music+0x134),flags=m.Read(music+0xd4);
        if(!source||!geometry||!alpha||!flags||!(*flags&1))return root;
        auto type=m.Read(*geometry+4),px=m.Read(*geometry+0x14),py=m.Read(*geometry+0x18);
        if(!type||(*type&65535)!=6||!px||!py)return root;
        const float opacity=std::bit_cast<float>(*alpha);
        const float gx=std::bit_cast<float>(*px),gy=std::bit_cast<float>(*py);
        if(!std::isfinite(opacity)||opacity<=0||opacity>1||!std::isfinite(gx)||!std::isfinite(gy))return root;
        // Learned songs have visible button/stone subtrees; locked notes are
        // separate siblings. Follow native visibility instead of shadowing every slot.
        std::array<float,13> stone_alpha{};
        for(unsigned i=0;i<Names.size();++i) {
            const U32 button=Child(m,music,Names[i]);
            auto visible=m.Read(button+0xd4),a=m.Read(button+0x134);
            if(!button||!visible||!(*visible&1)||!a)continue;
            const float value=std::bit_cast<float>(*a);
            if(!std::isfinite(value)||value<=0||value>1)continue;
            stone_alpha[Slot(i)]=std::max(stone_alpha[Slot(i)],std::min(opacity,value));
        }
        if(std::none_of(stone_alpha.begin(),stone_alpha.end(),[](float a){return a>0;}))return root;
        constexpr U32 Size=0x5200;
        if(sp<0x10b000||(sp&7))return root;const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return root;
        std::array<U32,Size/4> words{};
        const auto put=[&](U32 at,U32 v){words[at/4]=v;};
        const auto f=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
        const auto copy=[&](U32 at,U32 from,U32 size){for(U32 j=0;j<size;j+=4){auto v=m.Read(from+j);if(!v)return false;put(at+j,*v);}return true;};
        if(!copy(0,root,0x140))return root;
        put(0x14,base+0x140);put(0x18,1);put(0x20,0);put(0x24,0xffff);put(0x28,0);put(0x140,root);
        unsigned children=1;
        for(unsigned layer=0;layer<3;++layer)for(unsigned slot=0;slot<13;++slot) {
            if(stone_alpha[slot]<=0)continue;
            const unsigned index=layer*13+slot;
            const U32 node=0x200+index*0x200,geo=node+0x140;
            if(!copy(node,source,0x140)||!copy(geo,*geometry,0x7c))return root;
            put(0x140+children++*4,base+node);put(node+0x28,base+geo);
            put(node+0x14,0);put(node+0x18,0);put(node+0x20,0);put(node+0x24,0xffff);put(node+0xd4,15);
            for(unsigned c=0;c<3;++c)f(node+0x128+c*4,0);f(node+0x134,stone_alpha[slot]*(.32f-.09f*layer));
            f(node+0xe0,column.scale);f(node+0xe4,0);f(node+0xf0,0);f(node+0xf4,column.scale);
            f(node+0xec,column.x+slide+(Column(slot)*column_gap+2-gx)*column.scale);
            f(node+0xfc,column.y+gap*Row(slot)+(4+2*layer-gy)*column.scale);
        }
        put(0x18,children);
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
        for(auto w:frame.writes)m.Write(w.address,w.after);frame.sp=sp;return base;
    }
    template<class M> U32 PanelRoot(M& m,U32 layout,U32 content,U32 sp) {
        if(!enabled||pass!=3||!panel||layout!=panel){backdrop.scratch.frames.emplace_back();return content;}
        const auto all=Child(m,panel+8,"all"),music=Child(m,all,"btn_music_g");
        const auto alpha=m.Read(music+0x134),flags=m.Read(music+0xd4);
        const float left=panel_left.load(),right=panel_right.load();
        return backdrop.Root(m,layout,content,sp,left+slide,bar_height,right-left,240.f-2*bar_height,
            alpha&&flags&&(*flags&1)?std::bit_cast<float>(*alpha):0.f,panel_darkness.load(),{panel_red.load(),panel_green.load(),panel_blue.load()},panel_color_opacity.load());
    }
    template<class M> U32 HeaderRoot(M& m,U32 layout,U32 content,U32 sp) {
        if(!enabled||pass!=3||!panel||layout!=panel) {
            header.scratch.frames.emplace_back();return content;
        }
        const U32 all=Child(m,panel+8,"all"),music=Child(m,all,"btn_music_g");
        const auto alpha=m.Read(music+0x134),flags=m.Read(music+0xd4);
        const float scale=banner_scale.load();
        return header.Root(m,layout,content,sp,banner_x.load()+slide,banner_y.load(),
            banner_width.load()*scale,banner_height.load()*scale,
            alpha&&flags&&(*flags&1)?std::bit_cast<float>(*alpha):0.f);
    }
    template<class M> void End(M& m){if(frames.empty())return;for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);frames.pop_back();}
};
}
