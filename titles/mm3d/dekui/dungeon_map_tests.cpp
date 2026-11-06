// SPDX-License-Identifier: GPL-2.0-or-later
// S3 tests for the tip 4f2692ed dungeon map (sites 5/6/7, arrows, side panels, queue routing,
// marker style). The memory harness and the side-panel/routing/marker/description/arrow scenarios
// are adapted from mm3d-dekui 4f2692ed tools/mm3d/dungeon_side_panels_check.cpp and
// native_inventory_check.cpp (tgacquin/mm3d-dekui, GPL-2.0-or-later), with our v1.1 literals.
// usage: oot3d_mm3d_dekui_dungeon_map_tests [code.bin]
//   with code.bin: every translated guard word against original EUR v1.1, the marker-style
//   contract, relocated branch targets and the v110_address.h translation of each address.
#include "core.h"
#include "dungeon_map.h"
#include "v110_address.h"
#include "world_map_prompt.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
using namespace Mm3dDekui;
namespace D=Mm3dDekui::DungeonMap;
unsigned checks=0;
void Check(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}

struct Mem final : Mm3dDekui::Memory {
    std::unordered_map<U32,U32> words;
    U32 next=0x1000000;
    std::optional<U32> Read(U32 p) override {auto i=words.find(p);return i==words.end()?std::nullopt:std::optional<U32>(i->second);}
    void Write(U32 p,U32 v) override {Check(words.contains(p),"write to mapped memory only");words[p]=v;}
    U32 Alloc(U32 size){U32 p=next;next+=(size+3)&~3u;for(U32 i=p;i<next;i+=4)words[i]=0;return p;}
    U32 String(const char* s){
        const auto n=std::char_traits<char>::length(s)+1;U32 p=Alloc(U32(n));
        for(unsigned i=0;i<n;++i)words[p+(i&~3u)]|=U32(static_cast<unsigned char>(s[i]))<<((i&3)*8);
        return p;
    }
    void WidgetAt(U32 p,const char* name,float px=0,float py=0){
        words[p+0x10]=String(name);words[p+0x24]=0xffff;words[p+0xd4]=15;
        for(U32 i=0;i<3;++i)words[p+0xe0+i*16+i*4]=std::bit_cast<U32>(1.f);
        words[p+0xec]=std::bit_cast<U32>(px);words[p+0xfc]=std::bit_cast<U32>(py);
    }
    U32 Widget(const char* name,float px=0,float py=0){auto p=Alloc(0x140);WidgetAt(p,name,px,py);return p;}
    U32 Layout(const char* name,float px=0,float py=0,U32 size=0x200){
        auto p=Alloc(size),resource=Alloc(0x20);words[p+4]=resource;words[resource+0x18]=String(name);
        WidgetAt(p+8,"root",px,py);
        words[p+0x44]=std::bit_cast<U32>(px);words[p+0x48]=std::bit_cast<U32>(py);return p;
    }
    void Children(U32 p,const std::vector<U32>& nodes){
        auto list=Alloc(U32(nodes.size()*4));words[p+0x14]=list;words[p+0x18]=U32(nodes.size());
        for(unsigned i=0;i<nodes.size();++i)words[list+i*4]=nodes[i];
    }
    void Queue(U32 p,const std::vector<U32>& nodes){
        for(unsigned i=0;i<nodes.size();++i)words[p+4+0x80+i*4]=nodes[i];
        words[p+0x808]=U32(nodes.size());words[p+0x844]=U32(nodes.size());
    }
};

struct TestCpu final : Cpu {
    std::array<U32,16> r{};std::array<U32,64> s{};U32 flags=0;
    std::array<U32,16>& Regs() override {return r;}
    U32 Cpsr() override {return flags;}
    void SetCpsr(U32 value) override {flags=value;}
    std::array<U32,64>& ExtRegs() override {return s;}
};

// ---- Translation and code contracts -----------------------------------------------------
struct Translated {U32 ref,ours;};
// Every code address of the tip module with its reference (upstream) address.
constexpr std::array<Translated,40> Addresses{{
    {0x596c24,D::PCs[0]},{0x596cb8,D::PCs[1]},{0x596cbc,D::PCs[2]},{0x594480,D::PCs[3]},{0x596cec,D::PCs[4]},
    {0x5944d4,D::PCs[5]},{0x597604,D::PCs[6]},{0x594c28,D::PCs[7]},
    {0x1b2c48,D::SetZoom},{0x594b68,D::OverviewCursorEnd},{0x596ff4,D::WorldEnd},{0x36e2a0,D::MapStateLiteral},
    {0x5944cc,D::ZoomGuards[0].first},{0x5944d0,D::ZoomGuards[1].first},{0x5975f4,D::ZoomGuards[2].first},
    {0x597608,D::ZoomGuards[3].first},{0x1b2c48,D::ZoomGuards[4].first},{0x1b2c4c,D::ZoomGuards[5].first},
    {0x1b2c50,D::ZoomGuards[6].first},{0x1b2c54,D::ZoomGuards[7].first},{0x1b2c58,D::ZoomGuards[8].first},
    {0x1b2c5c,D::ZoomGuards[9].first},{0x1b2c60,D::ZoomGuards[10].first},{0x1b2c64,D::ZoomGuards[11].first},
    {0x1b2c68,D::ZoomGuards[12].first},{0x1b2c6c,D::ZoomGuards[13].first},
    {0x594b44,D::OverviewCursorGuards[0].first},{0x594b48,D::OverviewCursorGuards[1].first},
    {0x594b50,D::OverviewCursorGuards[2].first},{0x594c34,D::OverviewCursorGuards[3].first},
    {0x594c38,D::OverviewCursorGuards[4].first},{0x594c88,D::OverviewCursorGuards[5].first},
    {0x594b68,D::OverviewCursorGuards[6].first},{0x594b6c,D::OverviewCursorGuards[7].first},
    {0x1b274c,D::OverviewCursorGuards[8].first},{0x1b2750,D::OverviewCursorGuards[9].first},
    {0x1b2754,D::OverviewCursorGuards[10].first},{0x1b2764,D::OverviewCursorGuards[11].first},
    {0x596c20,0x00596d90},{0x175c30,0x00175ad4}
}};
// Upstream words; branch words are re-encoded by V110Branch for comparison with ours.
constexpr std::array<U32,8> UpstreamWords{0xe1a04000,0x0bf4316b,0xe5d4074c,0x15d6000c,0xe594001c,0xebf079db,0x03a00001,0xe2850fd7};

U32 BranchTarget(U32 at,U32 word){
    std::int32_t offset=std::int32_t(word&0x00ffffffu);if(offset&0x00800000)offset-=0x01000000;
    return U32(std::int64_t(at)+8+4*offset);
}

void TranslationChecks(){
    for(auto [ref,ours]:Addresses)Check(V110(ref)==ours,"v110 translation of "+std::to_string(ref));
    constexpr std::array<U32,8> refs{0x596c24,0x596cb8,0x596cbc,0x594480,0x596cec,0x5944d4,0x597604,0x594c28};
    for(unsigned i=0;i<8;++i){
        const bool branch=((UpstreamWords[i]>>25)&7)==5;
        Check(D::Words[i]==(branch?V110Branch(UpstreamWords[i],refs[i]):UpstreamWords[i]),"site word "+std::to_string(i));
    }
    Check(BranchTarget(D::PCs[5],D::Words[5])==D::SetZoom,"site 5 is the BL SetZoom");
    Check(BranchTarget(D::ZoomGuards[3].first,D::ZoomGuards[3].second)==D::SetZoom,"site 6's BLEQ calls SetZoom");
    Check(BranchTarget(D::OverviewCursorGuards[0].first,D::OverviewCursorGuards[0].second)==D::OverviewCursorGuards[8].first,
          "overview branch calls IsMapZoomed");
    Check(BranchTarget(D::OverviewCursorGuards[7].first,D::OverviewCursorGuards[7].second)==D::OverviewCursorGuards[8].first,
          "cursor end calls IsMapZoomed");
    Check(BranchTarget(D::OverviewCursorGuards[2].first,D::OverviewCursorGuards[2].second)==D::PCs[7],
          "IsMapZoomed()==0 reaches site 7");
    // Replacement SVCs keep each original condition.
    Check(D::Instruction(D::PCs[5],D::Words[5])==0xefffffa5u&&D::Instruction(D::PCs[6],D::Words[6])==0x0fffffa6u&&
          D::Instruction(D::PCs[7],D::Words[7])==0xefffffa7u,"sites 5/6/7 SVC numbers and conditions");
    Check(D::Instruction(D::PCs[6],D::Words[6]^1)==(D::Words[6]^1),"wrong word passes through");
    std::cout<<"PASS translation and branch targets\n";
}

void ImageChecks(const char* path){
    if(!path||!*path){std::cout<<"SKIP code.bin contracts: pass the EUR v1.1 code.bin\n";return;}
    std::ifstream file(path,std::ios::binary);
    if(!file){std::cout<<"SKIP code.bin contracts: file absent\n";return;}
    std::vector<unsigned char> image((std::istreambuf_iterator<char>(file)),{});
    Check(image.size()==5967872,"vanilla EUR v1.1 code.bin size");
    Mem m;
    // Only the code windows the contracts read (the whole image would be 1.5M map entries).
    for(auto [begin,end]:std::array<std::pair<U32,U32>,5>{{{0x175000,0x176000},{0x188000,0x189000},
            {0x1ae000,0x1b3000},{0x36e000,0x36f000},{0x594000,0x5a0000}}})
        for(U32 at=begin;at<end;at+=4){
            const U32 p=at-0x100000;
            m.words[at]=U32(image[p])|(U32(image[p+1])<<8)|(U32(image[p+2])<<16)|(U32(image[p+3])<<24);
        }
    m.words[D::MapState+0x520]=0;
    for(unsigned i=0;i<8;++i)Check(m.Read(D::PCs[i])==D::Words[i],"original site word "+std::to_string(i));
    Check(D::CodeMatches(m),"site contract matches original EUR v1.1");
    Check(D::MarkerStyleContract(m),"marker-style contract matches original EUR v1.1");
    // Every guard rejects a corrupted or unmapped word.
    std::vector<U32> guards;
    for(auto [at,word]:D::ZoomGuards)guards.push_back(at);
    for(auto [at,word]:D::OverviewCursorGuards)guards.push_back(at);
    for(U32 pc:D::PCs)guards.push_back(pc);
    for(U32 at:{0x00596d90u,0x00596e24u,0x005945f4u,0x00596e58u,D::WorldEnd,0x0036e25cu,0x00175ad4u})guards.push_back(at);
    for(U32 at:guards){
        const U32 old=m.words.at(at);
        m.words[at]=old^1;Check(!D::CodeMatches(m),"corrupted site guard "+std::to_string(at));
        m.words.erase(at);Check(!D::CodeMatches(m),"unmapped site guard "+std::to_string(at));
        m.words[at]=old;
    }
    for(auto [at,word]:D::MarkerStyleGuards){
        m.words[at]=word^1;Check(!D::MarkerStyleContract(m),"corrupted marker guard "+std::to_string(at));
        m.words.erase(at);Check(!D::MarkerStyleContract(m),"unmapped marker guard "+std::to_string(at));
        m.words[at]=word;
    }
    Check(D::CodeMatches(m)&&D::MarkerStyleContract(m),"contracts restored");
    // The native writer's own branches: mode 3 reaches the mov r1,#0 path, mode 2 the other.
    Check(BranchTarget(0x001aeb24,m.words.at(0x001aeb24))==0x001aeb48,"mode 3 branch");
    Check(BranchTarget(0x001aeb2c,m.words.at(0x001aeb2c))==0x001aebe0,"mode 2 branch");
    Check(BranchTarget(0x001aebb4,m.words.at(0x001aebb4))==0x001aec08,"mode 3 joins the shared store");
    Check(BranchTarget(0x001aeba8,m.words.at(0x001aeba8))==0x001882f8&&BranchTarget(0x001aec00,m.words.at(0x001aec00))==0x001882f8&&
          BranchTarget(0x001aecac,m.words.at(0x001aecac))==0x001882f8,"each store follows the Radar getter");
    std::cout<<"PASS code.bin: 8 sites, "<<guards.size()<<" site guards and "<<D::MarkerStyleGuards.size()
             <<" marker guards against original EUR v1.1\n";
}

// ---- Marker style decode ------------------------------------------------------------------
void MarkerStyleWordChecks(){
    for(U32 mode=0;mode<6;++mode)for(U32 stale:{0u,1u,0xffu}){
        const auto w=D::MarkerStyleWord((stale<<24)|0x00abcdefu,(mode<<24)|0x00123456u);
        if(mode<1||mode>3)Check(!w,"modes outside 1..3 keep native");
        else Check(w==((mode==3?0u:1u)<<24|0x00abcdefu),"style byte = (mode == 3) ? 0 : 1, low bytes kept");
    }
    std::cout<<"PASS marker-style decode\n";
}

// ---- Dispatcher sites 5/6/7 (Core::CallSvc) -------------------------------------------------
struct MapFixture {
    Mem m;U32 owner=0,menu=0,game=0;
    MapFixture(){
        menu=m.Layout("MenuMapDungeon");
        owner=m.Alloc(0x800);m.words[owner+0x18]=menu;
        game=m.Alloc(0x200);
        m.words[game+0x13c]=4u<<8;m.words[game+0x148]=0x1b;
        m.words[D::MapMode]=0x01010101u;
    }
};

void SiteChecks(){
    D::Apply(D::Options{});
    Core core;MapFixture f;TestCpu cpu;
    for(unsigned i=0;i<16;++i)cpu.r[i]=0x22220000u+i;
    for(unsigned i=0;i<64;++i)cpu.s[i]=0x33330000u+i;
    cpu.flags=0x40000010u;
    const auto vfp=cpu.s;
    // Site 5: relocated BL SetZoom, the accepted A toggle; remembers modes 1/3 in the dungeon.
    const auto site5=[&](U32 game,U32 mode){
        cpu.r[4]=f.owner;cpu.r[9]=game;cpu.r[0]=mode;cpu.r[15]=D::PCs[5]+4;
        auto expected=cpu.r;expected[14]=D::PCs[5]+4;expected[15]=D::SetZoom;
        const auto writes=f.m.words;
        core.CallSvc(D::FirstSVC+5,cpu,f.m);
        Check(cpu.r==expected&&cpu.s==vfp&&cpu.flags==0x40000010u,"site 5 sets only lr and pc");
        Check(f.m.words==writes,"site 5 writes no guest memory");
    };
    // Site 6: MOVEQ r0,#1 -> remembered mode for the same game and scene.
    const auto site6=[&](U32 game)->U32{
        cpu.r[5]=game;cpu.r[0]=0x77;cpu.r[15]=D::PCs[6]+4;
        auto expected=cpu.r;
        core.CallSvc(D::FirstSVC+6,cpu,f.m);
        expected[0]=cpu.r[0];
        Check(cpu.r==expected&&cpu.s==vfp,"site 6 sets only r0");
        return cpu.r[0];
    };
    Check(site6(f.game)==1,"first entry keeps the native mode 1");
    site5(f.game,3);Check(site6(f.game)==3,"remembered overview mode on re-entry");
    site5(f.game,1);Check(site6(f.game)==1,"remembered zoomed mode on re-entry");
    site5(f.game,3);
    site5(f.game,2);Check(site6(f.game)==3,"mode 2 (gameplay transform) is never remembered");
    f.m.words[f.game+0x148]=0x1c;Check(site6(f.game)==1,"a new scene starts at mode 1");
    f.m.words[f.game+0x148]=0x1b;Check(site6(f.game)==3,"same scene recalls again");
    const U32 other=f.m.Alloc(0x200);f.m.words[other+0x13c]=4u<<8;f.m.words[other+0x148]=0x1b;
    Check(site6(other)==1,"another game context starts at mode 1");
    f.m.words[f.game+0x13c]=3u<<8;Check(site6(f.game)==1,"not a gameplay state: native mode");
    f.m.words[f.game+0x13c]=4u<<8;
    D::enabled=false;Check(site6(f.game)==1,"disabled module: native mode");D::enabled=true;
    // Outside the dungeon page (world map) the toggle is not remembered.
    f.m.words[f.owner+0x78c]=0x100;site5(f.game,1);f.m.words[f.owner+0x78c]=0;
    Check(site6(f.game)==3,"world-map toggles are not remembered");
    // Site 7: displaced ADD r0,r5,#860, skip only the overview coordinate update in mode 3.
    for(U32 mode:{0u,1u,2u,3u,4u})for(bool dungeon:{true,false}){
        f.m.words[D::MapMode]=(mode<<24)|0x00010101u;
        f.m.words[f.owner+0x78c]=dungeon?0:0x100;
        cpu.r[4]=f.owner;cpu.r[5]=0x10000000;cpu.r[15]=D::PCs[7]+4;
        auto expected=cpu.r;expected[0]=0x1000035c;
        expected[15]=(dungeon&&mode==3)?D::OverviewCursorEnd:D::PCs[7]+4;
        core.CallSvc(D::FirstSVC+7,cpu,f.m);
        Check(cpu.r==expected&&cpu.s==vfp,"site 7 r0 and continuation");
    }
    f.m.words[f.owner+0x78c]=0;
    // Site 3: A is no longer filtered in dungeon mode; the world map still filters it.
    const U32 pad=f.m.Alloc(0x10);
    for(bool dungeon:{true,false}){
        f.m.words[f.owner+0x78c]=dungeon?0:0x100;
        f.m.words[pad+0xc]=0x0f;cpu.r[4]=f.owner;cpu.r[6]=pad;
        core.CallSvc(D::FirstSVC+3,cpu,f.m);
        Check(cpu.r[0]==(dungeon?0x0fu:0x0eu),"A passes in the dungeon map, filtered on the world map");
    }
    std::cout<<"PASS sites 5/6/7 and the A filter\n";
}

// ---- Side panels, routing, arrows, inner shadows (tip dungeon_side_panels_check) ----------
void SidePanelAndRoutingChecks(){
    Mem m;D::Hook h;
    const auto layout=m.Layout("CommonBottomBg"),all=m.Widget("all");
    m.Children(layout+8,{all});std::vector<U32> source;
    for(unsigned i=0;i<2;++i){
        auto n=m.Widget(i?"common_bottom_bg_pane1":"common_bottom_bg_pane0"),g=m.Alloc(0x7c);
        m.words[n+0x28]=g;m.words[g+4]=6;
        m.words[g+0x14]=std::bit_cast<U32>(160.f*i);
        m.words[g+0x20]=std::bit_cast<U32>(160.f);m.words[g+0x24]=std::bit_cast<U32>(240.f);
        m.words[g+0x28]=std::bit_cast<U32>(i?.953125f:.640625f);
        m.words[g+0x30]=std::bit_cast<U32>(i?-.3125f:.3125f);
        m.words[n+0x134]=std::bit_cast<U32>(.6f);
        for(unsigned c=0;c<3;++c)m.words[n+0x128+c*4]=std::bit_cast<U32>(1.f);
        source.push_back(n);
    }
    m.Children(all,source);h.valid=h.dungeon=true;h.pass=1;h.backgrounds[0]=layout;h.routed={layout};
    const auto sp=(m.Alloc(0x14000)+0x14000)&~7u;
    D::Options o;o.top_strip=false;o.panels={{{12,110,.5f},{275,128,.3f}}};D::Apply(o);
    const auto f=[&](U32 p){return std::bit_cast<float>(m.words.at(p));};
    for(float darkness:{0.f,.7f,1.f}){
        D::panel_darkness=darkness;auto before=m.words;
        auto r=h.PanelRoot(m,layout,sp);Check(r==sp-0x3000&&m.words[r+0x18]==14,"side panel root");
        for(unsigned i=0;i<8;++i){
            auto n=m.words[r+0x140+i*4],g=m.words[n+0x28];auto p=o.panels[i/4];
            Check(f(n+0xec)==p.x&&std::abs(f(n+0xfc)-(p.y-(i%4>=2?240*p.scale:0)))<.0001f,"panel pose");
            Check(f(n+0xe0)==p.scale&&f(n+0xf4)==p.scale,"panel scale");
            Check(f(g+0x20)==160&&f(g+0x24)==240&&f(g+0x30)==(i%2?-.3125f:.3125f),"full native halves");
            Check(f(n+0x128)==1-darkness&&f(n+0x134)==.6f,"darkness");
        }
        for(unsigned i=8;i<14;++i){
            auto n=m.words[r+0x140+i*4];Check(f(n+0x128)==0,"shadow colour");
            Check(std::abs(f(n+0x134)-.6f*(.32f-.09f*((i-8)%3)))<.0001f,"shadow band alpha");
        }
        Check(h.panel_scratch.End(m)==sp&&m.words==before,"panel End restores scratch");
    }
    for(unsigned mode=0;mode<5;++mode){
        h.dungeon=mode!=0;h.pass=mode==1?3:1;h.routed=mode==2?std::vector<U32>{}:std::vector<U32>{layout};
        D::side_panels=mode!=3;D::panel_darkness=mode==4?NAN:0;
        auto before=m.words;Check(h.PanelRoot(m,layout,sp)==layout+8,"declined panel root");
        Check(!h.panel_scratch.End(m)&&m.words==before,"declined panel writes nothing");
    }
    D::side_panels=true;D::panel_darkness=0;
    // Routing on the real queue layout: border inserted FIRST in layout 2 (screen 2), red arrows
    // early, native touch Zoom buttons hidden, inner shadows hidden while zoomed.
    auto map=m.Layout("BackGround"),bg_group=m.Widget("backGround"),map_pane=m.Widget("map_base"),map_geo=m.Alloc(0x7c);
    m.Children(map+8,{bg_group});m.Children(bg_group,{map_pane});m.words[map_pane+0x28]=map_geo;
    m.words[map_geo+4]=6;m.words[map_geo+0x20]=std::bit_cast<U32>(320.f);
    m.words[map_geo+0x24]=std::bit_cast<U32>(240.f);m.words[map_pane+0x134]=std::bit_cast<U32>(.6f);
    auto menu=m.Layout("MenuMapDungeon");
    o.side_panels=true;o.top_strip=true;o.panels={{{-20,120,.44f},{288,120,.44f}}};o.top_panel={-45,0,.529f};
    auto owner=m.Alloc(0x800),menu_all=m.Widget("all");std::vector<U32> groups;
    for(const char* name:{"icon_touch_direct","cursor_loupe_g","disp_no_map_g","disp_wing_g","item_set_base_g","slide_gauge_g",
                          "btn_place_name_g","btn_expansion_g","btn_reduction_g","btn_return_l","bb_map_area_g"})
        groups.push_back(m.Widget(name,80,60));
    m.Children(menu+8,{menu_all});m.Children(menu_all,groups);
    m.words[owner+0x18]=menu;m.words[owner+0x10]=layout;m.words[owner+0x14]=map;
    auto manager=m.Alloc(0x30);std::array<U32,6> queues{};
    for(unsigned i=0;i<6;++i){queues[i]=m.Alloc(0x848);m.words[manager+8+i*4]=queues[i];}
    auto banner=m.Layout("SubMenuTitle"),labels=m.Layout("Labels"),controls=m.Layout("Controls");
    m.words[queues[2]+4]=banner;m.words[queues[2]+0x804]=1;m.words[queues[2]+0x844]=1;
    m.words[queues[2]+4+0x80]=labels;m.words[queues[2]+0x808]=1;m.words[queues[2]+0x844]=2;
    m.Queue(queues[3],{controls});m.Queue(queues[4],{layout,map});m.Queue(queues[5],{menu});
    D::Apply(o);h.Capture(m,owner);Check(h.valid&&h.dungeon,"capture dungeon page");
    auto native=m.words;h.Pass(m,manager,4);
    Check(h.border_overlay,"border overlay when both backgrounds move");
    Check(m.words[queues[2]+4]==layout&&m.words[queues[2]+8]==map&&m.words[queues[2]+12]==menu&&
          m.words[queues[2]+16]==banner,"border, map backdrop and arrows first in layout 2");
    Check(m.words[queues[2]+0x804]==4&&m.words[queues[2]+0x844]==5&&m.words[queues[2]+4+0x80]==labels,"layout 2 queue kept");
    Check(m.words[queues[3]+4+0x80]==controls&&m.words[queues[3]+8+0x80]==menu,"menu routed to the late pass");
    for(auto node:groups)for(U32 off=0;off<0x140;off+=4)Check(m.words[node+off]==native[node+off],"Pass changes no widget");
    Check(h.arrows_early,"arrows drawn early");
    const auto routed_state=m.words;
    for(unsigned pass:{2u,3u}){
        h.Pass(m,manager,pass);h.Begin(m,menu);
        for(unsigned i=0;i<groups.size();++i){
            const bool visible=m.words[groups[i]+0xd4]&1;
            Check(visible==(i!=7&&i!=8&&((pass==2)==(i==0))),"early arrows / late controls / zoom buttons hidden");
        }
        h.End(m);Check(m.words==routed_state,"Begin/End restore");
    }
    for(unsigned zoom:{1u,3u,2u,3u,1u,0u,4u})for(bool visible:{false,true}){
        m.words[D::MapStateLiteral]=D::MapState;m.words[D::MapMode]=(zoom<<24)|0x123456;
        m.words[groups[10]+0xd4]=visible?15:14;m.words[groups[10]+0x134]=std::bit_cast<U32>(.4f);
        const auto before_zoom=m.words;
        for(unsigned pass:{2u,3u}){
            h.Pass(m,manager,pass);h.Begin(m,menu);
            for(unsigned i=0;i<groups.size();++i){
                const bool expected=i!=7&&i!=8&&((pass==2)==(i==0))&&(i!=10||(visible&&zoom!=1&&zoom!=2));
                Check(bool(m.words[groups[i]+0xd4]&1)==expected,"inner shadows hidden only while zoomed");
            }
            Check(m.words[groups[10]+0x134]==std::bit_cast<U32>(.4f),"inner shadow fade kept");
            h.End(m);Check(m.words==before_zoom,"zoom draw restores");
        }
    }
    // Without the literal guard the zoom state is not trusted: inner shadows keep native flags.
    m.words[D::MapStateLiteral]=0;m.words[D::MapMode]=1u<<24;m.words[groups[10]+0xd4]=15;
    h.Pass(m,manager,3);h.Begin(m,menu);Check(m.words[groups[10]+0xd4]&1,"unguarded mode keeps native visibility");h.End(m);
    m.words=routed_state;
    m.words[groups[0]+0xd4]&=~1u;const auto hidden=m.words;
    for(unsigned pass:{2u,3u}){
        h.Pass(m,manager,pass);h.Begin(m,menu);
        Check(!(m.words[groups[0]+0xd4]&1),"hidden arrows stay hidden");h.End(m);Check(m.words==hidden,"restore");
    }
    // Queue saturation cannot partially route a page.
    m.words=native;m.words[queues[2]+0x804]=30;m.words[queues[2]+0x844]=31;
    for(unsigned i=0;i<30;++i)m.words[queues[2]+4+i*4]=banner;
    h.Capture(m,owner);auto full=m.words;h.Pass(m,manager,4);
    Check(!h.border_overlay&&!h.arrows_early&&m.words==full,"saturated layout 2: nothing routed");
    D::Apply(D::Options{});
    std::cout<<"PASS side panels, queue scan with screen 2, early arrows, hidden zoom buttons and inner shadows\n";
}

// ---- Marker style around the map draw ------------------------------------------------------
void MarkerStyleChecks(){
    Mem draw;D::Hook map_hook;map_hook.valid=true;
    draw.words[NativeMinimap::Count(6)]=0;
    for(auto [at,word]:D::MarkerStyleGuards)draw.words[at]=word;
    for(bool enabled:{false,true}){
        D::marker_style_write=enabled;
        for(bool dungeon:{false,true})for(U32 zoom:{0u,1u,2u,3u,4u})for(U32 stale:{0u,1u}){
            map_hook.dungeon=dungeon;
            draw.words[D::MapMode]=(zoom<<24)|0x456789;draw.words[D::MarkerStyle]=(stale<<24)|0x123456;
            const auto initial=draw.words;
            map_hook.BeforeMap(draw);
            const U32 expected=enabled&&dungeon&&zoom>=1&&zoom<=3?U32(zoom!=3):stale;
            Check(draw.words[D::MarkerStyle]==((expected<<24)|0x123456),"marker style for the current mode");
            Check(draw.words[D::MapMode]==initial.at(D::MapMode),"never changes the zoom");
            map_hook.AfterMap(draw);Check(draw.words==initial,"AfterMap restores");
            map_hook.AfterMap(draw);Check(draw.words==initial,"second AfterMap inert");
        }
    }
    // A failing contract word leaves the cached byte native.
    D::marker_style_write=true;map_hook.dungeon=true;
    for(auto [at,word]:D::MarkerStyleGuards){
        draw.words[at]=word^1;draw.words[D::MapMode]=3u<<24;draw.words[D::MarkerStyle]=1u<<24;
        const auto initial=draw.words;map_hook.BeforeMap(draw);
        Check(draw.words==initial,"contract mismatch: no write");map_hook.AfterMap(draw);draw.words[at]=word;
    }
    // Early exits restore: routing rejected after BeforeMap, and the next BeforeMap.
    draw.words[D::MapMode]=0x01000000;draw.words[D::MarkerStyle]=0x123456;
    auto initial=draw.words;map_hook.BeforeMap(draw);map_hook.valid=false;
    map_hook.AfterMap(draw);Check(draw.words==initial,"restored even when routing rejects");
    map_hook.valid=true;map_hook.BeforeMap(draw);map_hook.BeforeMap(draw);map_hook.AfterMap(draw);
    Check(draw.words==initial,"a repeated BeforeMap restores first");
    draw.words.erase(D::MarkerStyle);const auto incomplete=draw.words;
    map_hook.BeforeMap(draw);map_hook.AfterMap(draw);Check(draw.words==incomplete,"unmapped style: no write");
    D::marker_style_write=D::MarkerStyleConfirmed;
    std::cout<<"PASS marker style (gated, contract, restore)\n";
}

// ---- Description plate excluded from side shadows ------------------------------------------
void DescriptionChecks(){
    Mem mem;D::Hook hook;
    hook.valid=hook.dungeon=hook.border_overlay=true;hook.pass=2;
    hook.owner=mem.Alloc(0x800);hook.panel=mem.Layout("MenuMapDungeon");
    mem.words[hook.owner+0x18]=hook.panel;
    hook.controller=mem.Alloc(0x14);auto context=mem.Alloc(0x170),state=mem.Alloc(4);
    mem.words[hook.controller+8]=hook.owner;mem.words[hook.controller+4]=context;mem.words[context+0x16c]=state;
    auto desc=mem.Layout("MessageWindow",0,0,0x448),plate=mem.Widget("plate",20,150),geo=mem.Alloc(0x7c);
    mem.words[0x006b3700+0x2c]=desc;mem.words[desc+0x444]=state;mem.words[desc+0x3f4]=0x17ac;
    mem.Children(desc+8,{plate});mem.words[plate+0x28]=geo;mem.words[plate+0x134]=std::bit_cast<U32>(1.f);
    mem.words[geo+4]=6;mem.words[geo+0x20]=std::bit_cast<U32>(360.f);mem.words[geo+0x24]=std::bit_cast<U32>(70.f);
    auto backdrop=mem.Layout("BackGround"),group=mem.Widget("backGround"),pane=mem.Widget("map_base",40,0),pg=mem.Alloc(0x7c);
    mem.Children(backdrop+8,{group});mem.Children(group,{pane});mem.words[pane+0x28]=pg;
    mem.words[pane+0x134]=std::bit_cast<U32>(1.f);mem.words[pg+4]=6;
    mem.words[pg+0x20]=std::bit_cast<U32>(320.f);mem.words[pg+0x24]=std::bit_cast<U32>(240.f);
    hook.backgrounds[1]=backdrop;hook.routed={backdrop};
    const auto stack=(mem.Alloc(0x20000)+0x20000)&~7u;
    const auto f=[&](U32 at){return std::bit_cast<float>(mem.words.at(at));};
    for(float dx:{0.f,20.f,350.f})for(float factor:{.5f,1.f,1.4f}){
        mem.words[plate+0xec]=std::bit_cast<U32>(20.f+dx);mem.words[plate+0xe0]=std::bit_cast<U32>(factor);
        const auto cover=hook.DescriptionBounds(mem);
        Check(cover&&(*cover)[0]==20+dx&&(*cover)[2]==20+dx+360*factor,"description bounds");
        auto original=mem.words;const auto root=hook.ShadowRoot(mem,backdrop,stack);Check(root==stack-0xc400,"shadow scratch");
        const auto count=mem.words[root+0x18],children=mem.words[root+0x14];Check(count>0&&count<=96,"piece count");
        float area=0;
        for(U32 i=0;i<count;++i){
            const auto n=mem.words[children+i*4],g=mem.words[n+0x28];
            const float x=f(n+0xec)+f(g+0x14)*f(n+0xe0),y=f(n+0xfc)+f(g+0x18)*f(n+0xf4),w=f(g+0x20)*f(n+0xe0),h=f(g+0x24)*f(n+0xf4);
            Check(x+w<=(*cover)[0]+.001f||x>=(*cover)[2]-.001f||y+h<=(*cover)[1]+.001f||y>=(*cover)[3]-.001f,"no shadow under the plate");
            area+=w*h;
        }
        float expected=0;
        for(const D::Hook::Rectangle side:{D::Hook::Rectangle{24,0,40,240},D::Hook::Rectangle{360,0,376,240}})
            for(auto piece:D::Hook::Outside(side,cover))expected+=(piece[2]-piece[0])*(piece[3]-piece[1]);
        Check(std::abs(area-expected)<.1f,"no missing or overlapping strips");
        Check(hook.shadow_scratch.End(mem)==stack&&mem.words==original,"shadow End restores");
    }
    hook.RememberDescription(mem,desc);Check(hook.description_layout==desc,"remember calculated description");
    mem.words[0x006b3700+0x2c]=mem.Layout("MessageWindow",0,0,0x448);Check(hook.DescriptionBounds(mem).has_value(),"remembered wins");
    mem.words[plate+0x134]=0;Check(!hook.DescriptionBounds(mem),"invisible plate");
    mem.words[plate+0x134]=std::bit_cast<U32>(1.f);mem.words[desc+0x3f4]=0x1700;
    Check(!hook.DescriptionBounds(mem),"other messages cannot clip shadows");
    std::cout<<"PASS side shadows exclude the description plate\n";
}

// ---- Arrows: scale and per-arrow offsets about each centre ----------------------------------
void ArrowChecks(){
    Mem mem;D::Hook hook;auto root=mem.Widget("icon_touch_direct");
    std::array<U32,4> panes{};std::vector<U32> children;
    for(unsigned i=0;i<4;++i){
        const auto name=std::string("icon_")+D::ArrowNames[i]+"_direct";
        auto group=mem.Widget(name.c_str()),pane=mem.Widget((name+"_pane").c_str(),20.f+50*i,30.f+10*i);
        panes[i]=pane;children.push_back(group);mem.Children(group,{pane});
        auto geo=mem.Alloc(0x7c);mem.words[pane+0x28]=geo;mem.words[geo+4]=6;
        mem.words[geo+0x14]=std::bit_cast<U32>(-4.f);mem.words[geo+0x18]=std::bit_cast<U32>(-6.f);
        mem.words[geo+0x20]=std::bit_cast<U32>(8.f);mem.words[geo+0x24]=std::bit_cast<U32>(12.f);
        mem.words[pane+0x134]=std::bit_cast<U32>(.65f);
    }
    auto touch=mem.Widget("map_touch_icon_rect_n",100,120);children.push_back(touch);mem.Children(root,children);
    const auto original=mem.words;
    const auto f=[&](U32 at){return std::bit_cast<float>(mem.words.at(at));};
    D::Options arrows;arrows.poses[D::Map]={41,-9,1.5f};hook.frame=arrows.poses;
    for(float scale:{1.f,.5f,2.f}){
        arrows.arrow_scale=scale;arrows.arrow_offsets={{{7,-4},{-9,6},{15,11},{-12,-3}}};D::Apply(arrows);
        std::vector<D::Hook::Write> writes;std::vector<U32> seen;
        Check(hook.helper.Transform(mem,root,writes,seen,{1.5f,0,0,1.5f,41,-9}),"map transform");
        Check(hook.AdjustArrows(mem,root,writes),"arrow adjustment");
        for(auto w:writes)mem.Write(w.address,w.after);
        for(unsigned i=0;i<4;++i){
            Check(std::abs(f(panes[i]+0xec)-(41+1.5f*(20+50*i)+arrows.arrow_offsets[i][0]))<.0001f,"arrow x about its centre");
            Check(std::abs(f(panes[i]+0xfc)-(-9+1.5f*(30+10*i)+arrows.arrow_offsets[i][1]))<.0001f,"arrow y about its centre");
            Check(f(panes[i]+0xe0)==1.5f*scale&&f(panes[i]+0xf4)==1.5f*scale,"arrow scale");
            Check(f(panes[i]+0x134)==.65f,"native fade untouched");
        }
        Check(f(touch+0xec)==191&&f(touch+0xfc)==171&&f(touch+0xe0)==1.5f,"touch rectangle keeps the map pose");
        for(auto it=writes.rbegin();it!=writes.rend();++it)mem.Write(it->address,it->before);
        Check(mem.words==original,"arrow writes restore");
    }
    arrows.arrow_scale=1;arrows.arrow_offsets={};D::Apply(arrows);
    std::vector<D::Hook::Write> writes;Check(hook.AdjustArrows(mem,root,writes)&&writes.empty(),"default arrows are exact no-ops");
    arrows.arrow_scale=NAN;D::Apply(arrows);Check(!hook.AdjustArrows(mem,root,writes),"invalid arrow scale rejected");
    D::Apply(D::Options{});
    std::cout<<"PASS arrows scale and offsets\n";
}

// ---- Dungeon zoom prompt follows the accepted mode byte -----------------------------------
void ZoomFadeChecks(){
    WorldMapPrompt::ZoomFade fade;
    Check(fade.Update(1,true,0)==0,"first sample starts hidden");
    Check(std::abs(fade.Update(1,true,75000)-.5f)<1e-6f,"150 ms ramp, half way");
    Check(fade.Update(1,true,150000)==1,"fully shown");
    Check(std::abs(fade.Update(1,false,225000)-1)<1e-6f,"reversal keeps the current opacity for the step");
    Check(std::abs(fade.Update(1,false,300000)-.5f)<1e-6f,"then fades out");
    Check(fade.Update(2,true,310000)==0,"another owner restarts");
    Check(fade.Update(2,true,900000)==0,"a long gap restarts");
    std::cout<<"PASS zoom prompt fade\n";
}
} // namespace

int main(int argc,char** argv){
    try{
        TranslationChecks();ImageChecks(argc>1?argv[1]:std::getenv("MM3D_CODE_BIN"));MarkerStyleWordChecks();
        SiteChecks();SidePanelAndRoutingChecks();MarkerStyleChecks();DescriptionChecks();ArrowChecks();ZoomFadeChecks();
        std::cout<<"PASS "<<checks<<" dungeon map checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
}
