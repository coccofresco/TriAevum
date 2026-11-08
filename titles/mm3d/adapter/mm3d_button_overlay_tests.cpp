#include "oot3d_top_screen_mod_profile.h"
#include "mm3d_button_overlay.h"
#include "../dekui/button_glyph_emission.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace Oot3dNativeGame;
using namespace Mm3dDekui;
using U32 = uint32_t;
namespace {
unsigned checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct MemoryFixture {
 std::map<U32,U32> words;unsigned reads=0;
 std::optional<U32> Read(U32 p){++reads;auto i=words.find(p);return i==words.end()?std::nullopt:std::optional<U32>(i->second);}
 void F(U32 p,float f){words[p]=std::bit_cast<U32>(f);}
 void Name(U32 p,const char* text){std::string s=text;for(unsigned i=0;i<=s.size();i+=4){U32 v=0;for(unsigned j=0;j<4&&i+j<s.size();++j)v|=U32(uint8_t(s[i+j]))<<(8*j);words[p+i]=v;}}
};
constexpr U32 layout=0x10000000,node=layout+8,geo=0x10010000;
MemoryFixture Fixture(){
 MemoryFixture m;
 m.words[Mm3dButtonOverlay::CommandCursorAddress]=0x14001000;
 for(auto [pc,word]:ButtonEmission::Guards)m.words[pc]=word;
 m.words[0x1262d0]=0xeb003ab0;m.words[0x59e564]=0xe59410c0;
 m.words[layout+4]=0x10020000;m.words[0x10020018]=0x10020100;m.Name(0x10020100,"HudItemIconSet");
 m.words[node+4]=layout;m.words[node+0x10]=0x10020200;m.Name(0x10020200,"disp_x_pane");
 m.words[node+0x28]=geo;m.words[geo+4]=6;m.words[geo+0x10]=0x10030000;
 m.words[node+0xd4]=15;m.words[node+0x20]=0;m.words[node+0x24]=65535;m.words[node+0x18]=0;m.words[node+0x14]=0;
 for(U32 off:{0xe0U,0xf4U,0x110U,0x120U,0x128U,0x12cU,0x130U,0x134U})m.F(node+off,1);
 for(U32 off:{0xe4U,0xf0U,0x114U,0x118U,0x11cU,0x124U})m.F(node+off,0);
 m.F(node+0xec,300);m.F(node+0xfc,20);
 for(U32 off:{0x14U,0x18U,0x28U,0x2cU,0x30U,0x34U})m.F(geo+off,0);
 m.F(geo+0x20,16);m.F(geo+0x24,16);
 return m;
}
std::vector<oot3d::ui::UiPrimitive> Capture(MemoryFixture& m,NativeControlConfig config=NativeControlDefaults(),NativePromptSet set=NativePromptSet::Xbox,U32 pass=3){
 Mm3dButtonOverlay overlay;overlay.Configure(config,set);overlay.Capture(m,layout,node,pass);
 overlay.SubmitCommandList(0x14001000,0x100);overlay.ObserveDraw(10);
 const uint64_t draw=10;overlay.Select(std::span(&draw,1));return overlay.Compose(3);
}
void TestCapture(){
 auto m=Fixture();const auto original=m.words;auto glyphs=Capture(m);
 Check(!glyphs.empty(),"native named HUD label captured");Check(m.words==original,"no guest writes");
 Mm3dButtonOverlay replacement;replacement.Configure(NativeControlDefaults(),NativePromptSet::Xbox);
 Check(replacement.Capture(m,layout,node,3),"pure native letter geometry replaced, not double printed");
 auto mixed=Fixture();mixed.Name(0x10020100,"AActionIconBase");mixed.Name(0x10020200,"bb00_ybtn");
 mixed.F(geo+0x20,38);mixed.F(geo+0x24,38);
 Check(!replacement.Capture(mixed,layout,node,3),"mixed ring art retained");
 Check(Capture(m,NativeControlDefaults(),NativePromptSet::Touch).empty(),"touch keeps native letters");
 auto config=NativeControlDefaults();config.NativeButtonLabels=false;m.reads=0;
 Check(Capture(m,config).empty()&&m.reads==0,"Original performs no reads");
 Check(Capture(m,NativeControlDefaults(),NativePromptSet::Xbox,5).empty(),"lower screen excluded");
 for(auto [pc,word]:ButtonEmission::Guards){auto bad=Fixture();bad.words[pc]^=1;Check(Capture(bad).empty(),"each emission guard fails closed");}
 m=Fixture();m.words[node+0xd4]&=~1U;Check(Capture(m).empty(),"hidden pane omitted");
 m=Fixture();m.F(node+0x134,0);Check(Capture(m).empty(),"zero fade omitted");
 for(float alpha:{1.000001f,1.25f,255.f}) {
  m=Fixture();m.F(node+0x134,alpha);auto saturated=Capture(m);
  Check(!saturated.empty(),"finite native alpha overshoot saturates instead of dropping labels");
  for(const auto& primitive:saturated)Check(primitive.color.alpha<=1.f,"saturated host alpha stays normalized");
 }
 m=Fixture();m.F(node+0x134,std::numeric_limits<float>::infinity());Check(Capture(m).empty(),"nonfinite alpha remains rejected");
 m=Fixture();m.F(node+0xec,std::numeric_limits<float>::quiet_NaN());Check(Capture(m).empty(),"nonfinite pose omitted");
 m=Fixture();m.Name(0x10020100,"Unrelated");Check(Capture(m).empty(),"same name outside HUD not promoted");
 m=Fixture();m.Name(0x10020200,"disp_1_pane");auto chord=Capture(m);Check(chord.size()>glyphs.size(),"Restoration I is ZR+X chord");
 config=NativeControlDefaults();config.Bindings[size_t(NativeControlAction::Zr)]={};Check(Capture(m,config).empty(),"unbound modifier leaves native I");
 for(auto set:{NativePromptSet::Keyboard,NativePromptSet::Xbox,NativePromptSet::PlayStation,NativePromptSet::Nintendo,NativePromptSet::SteamDeck,NativePromptSet::Generic}){
  m=Fixture();Check(!Capture(m,NativeControlDefaults(),set).empty(),"all controller families use same art");
 }
}
void TestPictogramAndDpad(){
 auto m=Fixture();m.Name(0x10020100,"Pictogram");m.Name(0x10020200,"pict");
 constexpr U32 reg=NotebookPrompt::Registry,groups=0x10060000,entries=groups+0x100,record=groups+0x200;
 m.words[NotebookPrompt::RegistryLiteral]=reg;m.words[reg+0x28]=groups;m.words[reg+0x2c]=entries;
 m.words[groups]=12;m.words[entries+44]=record;m.words[record+4]=11<<16;m.words[geo+0x10]=record+12;
 m.F(geo+0x28,112.f/256);m.F(geo+0x2c,0);m.F(geo+0x30,16.f/256);m.F(geo+0x34,16.f/128);
 Check(!Capture(m).empty(),"separately emitted notebook/dialogue A without a parent tree");
 Check(!Capture(m,NativeControlDefaults(),NativePromptSet::Xbox,~0u).empty(),"native-validated standalone emission without LayoutBase pass");
 const auto full=Capture(m);
 m.F(geo+0x28,96.f/256);m.F(node+0x118,16.f/256);
 Check(Capture(m).size()==full.size(),"native Pictogram animated UV resolves A from nominal x96");
 m.F(node+0x118,64.f/256);m.F(node+0x124,32.f/128);
 Check(Capture(m).size()>full.size(),"animated Restoration I cell resolves ZR+X chord");
 m.Name(0x10020100,"ItemIconSet2");m.Name(0x10020200,"disp_x_pane");
 Check(Capture(m).size()>full.size(),"native inventory animated I overrides template X pane name");
 m.Name(0x10020100,"Pictogram");m.Name(0x10020200,"pict");
 m.F(node+0x118,112.f/256);
 Check(Capture(m).size()>full.size(),"animated Restoration II cell resolves ZR+Y chord");
 auto unbound=NativeControlDefaults();unbound.Bindings[size_t(NativeControlAction::Zr)]={};
 Check(Capture(m,unbound).empty(),"unbound animated chord retains native art");
 m.F(node+0x114,.1f);Check(Capture(m).empty(),"rotated atlas sampling not guessed as a button");
 m.F(node+0x114,0);
 m.words[record+4]=12<<16;Check(Capture(m).empty(),"foreign atlas stays native");
 m=Fixture();m.Name(0x10020100,"HudDpad");m.Name(0x10020200,"pane_dpad");
 auto out=Capture(m);Check(out.size()>=4,"four D-pad binding glyphs");
 auto controls=NativeControlDefaults();controls.Bindings[size_t(NativeControlAction::DpadLeft)]={};
 Check(Capture(m,controls).size()<out.size(),"unbound D-pad direction not invented");
}
void TestText(){
 auto m=Fixture();m.Name(0x10020100,"MenuItem");m.words[geo+4]=3;
 constexpr U32 font=0x10040000,header=font+0x100,pages=font+0x200,chars=font+0x300,records=font+0x400;
 m.words[geo+0x28]=font;m.words[geo+0x2fc]=records;m.words[geo+0x308]=4;
 m.words[font+4]=header;m.words[font+0xc]=pages;m.words[font+0x10]=chars;
 m.words[header+8]=8<<16;m.words[header+12]=12;m.words[header+0x10]=1;m.words[header+0x14]=4;
 m.words[header+0x1c]=0;m.words[header+0x24]=16<<16;m.words[header+0x28]=16;
 m.words[pages]=0x1000;m.words[pages+4]=256|(128<<16);
 unsigned i=0;for(char c:std::string_view(" +RZ")){m.words[chars+i*12]=c;m.words[chars+i*12+4]=8;m.words[chars+i*12+8]=i<<16;++i;}
 float pen=0;i=0;for(char c:std::string_view("ZR +")){
  const auto g=NotebookFont::Lookup(m,font,c,pen);Check(bool(g),"font fixture lookup");
  for(unsigned j=0;j<15;++j)m.words[records+i*60+j*4]=g->words[j];pen+=g->advance;++i;
 }
 const auto original=m.words;Check(!Capture(m).empty(),"Restoration ZR legend font records recognized");
 Check(m.words==original,"legend recognition read-only");
 m.words[records+40]^=1;Check(Capture(m).empty(),"unrelated glyph records remain native");
}
void TestFrames(){
 NativeButtonOverlayFrames frames;NativeButtonOverlaySlot a;a.X=1;NativeButtonOverlaySlot b;b.X=2;
 frames.Record(10,{a});frames.Select(10,true);Check(frames.Presented().empty(),"no invented predecessor");
 frames.Record(20,{b});frames.Select(20,true);Check(frames.Presented().at(0).X==1,"presented frame uses its materialization");
 frames.Record(30,{});frames.Select(30,true);Check(frames.Presented().at(0).X==2,"next materialization paired");
 frames.Record(40,{});frames.Select(40,true);Check(frames.Presented().empty(),"hidden label never retained");
 frames.Record(1,{a});frames.Select(1,true);Check(frames.Presented().empty(),"save restore clears history");
 frames.Reset();frames.Select(100,true);Check(frames.Presented().empty(),"reset clears presented labels");
 auto original=NativeControlDefaults();original.NativeButtonLabels=false;
 frames.Record(10,{a},NativePromptSet::Xbox,NativeControlDefaults());
 frames.Record(20,{},NativePromptSet::Touch,original);frames.Select(20,true);
 Check(frames.Prompts()==NativePromptSet::Xbox&&frames.Controls().NativeButtonLabels,"already suppressed native frame retains its binding snapshot during a switch");
 frames.Record(30,{},NativePromptSet::Touch,original);frames.Select(30,true);
 Check(frames.Presented().empty()&&!frames.Controls().NativeButtonLabels,"Original/touch native frame clears replacements at presentation, not early");
}
void TestCaptureAccounting(){
 auto m=Fixture();Mm3dButtonOverlay overlay;
 overlay.Configure(NativeControlDefaults(),NativePromptSet::Keyboard);
 Check(overlay.Capture(m,layout,node,3),"first pane captured for accounting");
 Check(overlay.Capture(m,layout,node,3),"duplicate pure pane still suppressed");
 Check(overlay.Stats().Candidates==2 && overlay.Stats().Labels==1 &&
       overlay.Stats().DuplicateCandidates==1 && overlay.PendingLabels()==1 && overlay.PendingPanes()==1,
       "duplicate candidate distinguished from missing binding and empty pending data");
 overlay.SubmitCommandList(0x14001000,0x100);
 Check(overlay.PendingLabels()==0 && overlay.PendingPanes()==0,"record drains labels and dedup keys together");
 Check(overlay.Capture(m,layout,node,3) && overlay.Stats().Labels==2,"next recording admits the same pane");
 overlay.SubmitCommandList(0x14001000,0x100);overlay.SubmitCommandList(0x14001000,0x100);
 Check(overlay.Stats().EmptyRecords==1,"empty record distinguished from duplicate emission");
 auto unbound=NativeControlDefaults();unbound.Bindings[size_t(NativeControlAction::X)]={};
 overlay.Configure(unbound,NativePromptSet::Keyboard);
 Check(!overlay.Capture(m,layout,node,3) && overlay.Stats().UnboundCandidates==1,"unbound candidate accounted");
 overlay.Configure(NativeControlDefaults(),NativePromptSet::Keyboard);
 auto ring=Fixture();ring.Name(0x10020100,"AActionIconBase");ring.Name(0x10020200,"bb00_ybtn");
 Check(!overlay.Capture(ring,layout,node,3) && overlay.Stats().RingGeometryRejected==1,"wrong ring geometry accounted");
 for(unsigned i=0;i<1020;++i){m.F(node+0xec,float(i%500));m.words[Mm3dButtonOverlay::CommandCursorAddress]=0x14001000+i*8;overlay.Capture(m,layout,node,3);}
 Check(overlay.PendingLabels()==1020,"pending cap fixture filled");
 m.F(node+0xec,300);
 Check(!overlay.Capture(m,layout,node,3) && overlay.Stats().CapacityRejected==1,"capacity rejection accounted without suppression");
 overlay.Reset();Check(overlay.PendingLabels()==0 && overlay.PendingPanes()==0,"reset drains accounting state");
}
void TestSubmissionOwnership(){
 auto m=Fixture();Mm3dButtonOverlay overlay;auto controls=NativeControlDefaults();
 overlay.Configure(controls,NativePromptSet::Keyboard);
 overlay.Capture(m,layout,node,3);
 // Same pane/position, different native destination, before any host drain.
 m.words[Mm3dButtonOverlay::CommandCursorAddress]=0x14002000;
 overlay.Capture(m,layout,node,3);
 Check(overlay.PendingLabels()==2,"different native buffers cannot deduplicate one another");
 overlay.SubmitCommandList(0x14003000,0x100);overlay.ObserveDraw(10);
 Check(overlay.PendingLabels()==2,"unrelated command submission cannot drain captures");
 overlay.SubmitCommandList(0x14001000,0x100);overlay.ObserveDraw(20);overlay.ObserveDraw(21);
 Check(overlay.PendingLabels()==1,"only submitted command range consumes captures");
 const uint64_t a[]{20,21};overlay.Select(a);auto first=overlay.Compose(3);
 Check(!first.empty(),"exact rendered draw selects its materialization without host-refresh lag");
 const uint64_t unrelated[]{10};overlay.Select(unrelated);
 Check(overlay.Compose(3).empty(),"world-only unrelated frame does not borrow HUD");
 controls.NativeButtonLabels=false;overlay.Configure(controls,NativePromptSet::Touch);
 overlay.SubmitCommandList(0x14002000,0x100);overlay.ObserveDraw(30);
 const uint64_t b[]{30};overlay.Select(b);
 Check(overlay.Compose(3).size()==first.size(),"pending suppressed frame retains captured binding/family despite later Original switch");
 overlay.Select(a);Check(overlay.Compose(3).size()==first.size(),"delayed presentation retains immutable earlier source snapshot");
 const uint64_t both[]{20,21,30};overlay.Select(both);
 Check(overlay.Compose(3).size()==2*first.size(),"split lists compose once each, not once per draw");
 overlay.SubmitCommandList(0x14001000,0x100);overlay.ObserveDraw(40);
 const uint64_t empty[]{40};overlay.Select(empty);
 Check(overlay.Compose(3).empty(),"reused command address with no labels clears instead of retaining stale HUD");
 overlay.Select(a);Check(!overlay.Compose(3).empty(),"address reuse does not mutate older draw snapshot");
 overlay.Reset();overlay.Select(a);Check(overlay.Compose(3).empty(),"reset removes draw and capture ownership");
 overlay.Configure(NativeControlDefaults(),NativePromptSet::Keyboard);
 m.words[Mm3dButtonOverlay::CommandCursorAddress]=0x14001008;overlay.Capture(m,layout,node,3);
 overlay.SubmitCommandList(0x14001000,8);
 Check(overlay.PendingLabels()==1,"command range end is exclusive");
 overlay.SubmitCommandList(0xfffffff8,32);
 Check(overlay.PendingLabels()==1,"overflowing range cannot consume captures");
 m.words.erase(Mm3dButtonOverlay::CommandCursorAddress);
 Check(!overlay.Capture(m,layout,node,3),"missing command destination leaves native art intact");
}
void TestInlineAndChordBounds(){
 auto hotbar=Fixture();hotbar.Name(0x10020200,"disp_1_pane");hotbar.F(node+0xec,-12);
 const auto hotbarGlyphs=Capture(hotbar,NativeControlDefaults(),NativePromptSet::Keyboard);
 Check(!hotbarGlyphs.empty(),"left-edge hotbar chord exists");
 float tallest=0;for(const auto& p:hotbarGlyphs){Check(p.destination.x>=-.01f,"hotbar modifier stays on canvas");tallest=std::max(tallest,p.destination.height);}
 Check(tallest>=16,"hotbar chord retains readable native height instead of shrinking to a single letter cell");
 NativeButtonOverlaySlot anchor;anchor.Action=NativeControlAction::X;anchor.X=100;anchor.Y=20;anchor.Height=8;
 std::vector<oot3d::ui::UiPrimitive> mainOnly,anchored;
 AppendNativeButtonOverlay(mainOnly,{anchor},NativePromptSet::Keyboard,NativeControlDefaults(),3);
 anchor.Modifier=NativeControlAction::Zr;anchor.ChordMainAnchor=true;
 AppendNativeButtonOverlay(anchored,{anchor},NativePromptSet::Keyboard,NativeControlDefaults(),3);
 Check(!mainOnly.empty() && anchored.size()>mainOnly.size() &&
       mainOnly.front().destination.x==anchored.front().destination.x &&
       mainOnly.front().destination.width==anchored.front().destination.width,
       "HUD main key stays over mixed-art native letter when adding modifier");
 auto m=Fixture();constexpr U32 groups=0x10060000,entries=groups+0x100,resource=groups+0x200,
     descriptor=groups+0x400,pixels=0x11000000,record=groups+0x500;
 m.words[NotebookPrompt::RegistryLiteral]=NotebookPrompt::Registry;
 m.words[NotebookPrompt::Registry+0x28]=groups;m.words[NotebookPrompt::Registry+0x2c]=entries;
 m.words[groups]=12;m.words[entries+44]=resource;m.words[resource+4]=11<<16;
 m.words[resource+12+0x4c]=descriptor;m.words[descriptor+0x20]=pixels;
 m.words[record]=pixels;m.words[record+0xc]=256;m.words[record+0x10]=128;m.words[record+0x38]=~0u;
 m.F(record+0x14,0);m.F(record+0x18,0);m.F(record+0x20,16);m.F(record+0x24,16);
 m.F(record+0x28,144.f/256);m.F(record+0x2c,0);m.F(record+0x30,16.f/256);m.F(record+0x34,16.f/128);
 const auto original=m.words;Mm3dButtonOverlay overlay;
 overlay.Configure(NativeControlDefaults(),NativePromptSet::Keyboard);
 Check(overlay.CaptureInline(m,node,record,true)&&overlay.PendingLabels()==0,"proven inline shadow omitted without duplicate host glyph");
 Check(overlay.CaptureInline(m,node,record,false)&&overlay.PendingLabels()==1,"inline X record replaced independently of prose");
 Check(m.words==original,"inline capture writes no guest bytes");
 overlay.SubmitCommandList(0x14001000,0x100);overlay.ObserveDraw(1);const uint64_t draw=1;overlay.Select(std::span(&draw,1));
 Check(!overlay.Compose(3).empty(),"inline glyph uses exact native submitted draw");
 m.words[record]=pixels+8;Check(!overlay.CaptureInline(m,node,record,false),"unrelated font pixels never recognized by UV alone");
 m.words[record]=pixels;m.F(record+0x28,0);Check(!overlay.CaptureInline(m,node,record,false),"ordinary atlas art stays native");
 m.F(record+0x28,160.f/256);m.F(record+0x2c,32.f/128);
 Check(overlay.CaptureInline(m,node,record,false),"inline Restoration I cell recognized");
 {
  struct Extent{float Tallest=0,Left=1e9f,Right=-1e9f;size_t Count=0;};
  auto run=[&](Mm3dButtonOverlay::InlineChordStyle style){
   Mm3dButtonOverlay o;o.Configure(NativeControlDefaults(),NativePromptSet::Keyboard);o.SetInlineChordStyle(style);
   Check(o.CaptureInline(m,node,record,false)&&o.PendingLabels()==1,"inline I captured for style");
   o.SubmitCommandList(0x14001000,0x100);o.ObserveDraw(7);const uint64_t d=7;o.Select(std::span(&d,1));
   Extent e;for(const auto& p:o.Compose(3)){++e.Count;e.Tallest=std::max(e.Tallest,p.destination.height);
    e.Left=std::min(e.Left,p.destination.x);e.Right=std::max(e.Right,p.destination.x+p.destination.width);}
   return e;};
  Mm3dButtonOverlay defaults;Check(defaults.GetInlineChordStyle()==Mm3dButtonOverlay::InlineChordStyle::Abbreviated,"abbreviation is the default inline chord look");
  const auto glyph=run(Mm3dButtonOverlay::InlineChordStyle::Abbreviated),chord=run(Mm3dButtonOverlay::InlineChordStyle::Chord);
  Check(glyph.Count>0&&chord.Count>0,"both inline chord looks render");
  Check(glyph.Tallest>=1.8f*chord.Tallest,"abbreviated inline I main key is readable, not squeezed beside its modifier");
  Check(glyph.Right-glyph.Left<=chord.Right-chord.Left+.01f,"abbreviated inline I stays within the same native text cell");
  Check(Mm3dButtonOverlay::ParseInlineChordStyle("chord")==Mm3dButtonOverlay::InlineChordStyle::Chord&&
        Mm3dButtonOverlay::ParseInlineChordStyle("abbreviated")==Mm3dButtonOverlay::InlineChordStyle::Abbreviated&&
        !Mm3dButtonOverlay::ParseInlineChordStyle("big"),"inline chord flag parsing fails closed");
 }
 auto config=NativeControlDefaults();config.Bindings[size_t(NativeControlAction::Zr)]={};overlay.Configure(config,NativePromptSet::Keyboard);
 Check(!overlay.CaptureInline(m,node,record,false),"unbound inline modifier preserves native glyph");
 NativeButtonOverlaySlot slot;slot.Action=NativeControlAction::X;slot.Modifier=NativeControlAction::Zr;
 slot.X=8;slot.Y=8;slot.Height=16;slot.Width=16;
 std::vector<oot3d::ui::UiPrimitive> primitives;
 AppendNativeButtonOverlay(primitives,{slot},NativePromptSet::Keyboard,NativeControlDefaults(),3);
 Check(!primitives.empty(),"left-edge chord rendered");
 for(const auto& p:primitives)Check(p.destination.x>=-.01f && p.destination.x+p.destination.width<=16.01f,
     "whole chord, including modifier, fits native cell at left screen edge");
}
void TestSettings(){
 auto config=NativeControlDefaults();std::string text,error;NativeControlConfig parsed;
 Check(SerializeNativeControlConfigText(config,&text,&error),"default serialize");
 Check(text.find("native_button_labels")==std::string::npos,"default legacy JSON stays unchanged");
 auto before=NativeButtonOverlayGlyph(NativeControlAction::A,NativePromptSet::Keyboard,config);
 config.MouseEnabled=false;config.Bindings[size_t(NativeControlAction::A)].KeyboardPrimary=NativeKeyboardKey::Enter;
 auto rebound=NativeButtonOverlayGlyph(NativeControlAction::A,NativePromptSet::Keyboard,config);
 Check(before&&rebound&&*before!=*rebound,"keyboard remap and mouse disable resolved live");
 Check(rebound&&rebound->Icon==Menu::ButtonGlyphIcon::KeyEnter,"conventional Enter art");
 config.NativeButtonLabels=false;Check(SerializeNativeControlConfigText(config,&text,&error),"original serialize");
 Check(ParseNativeControlConfigText(text,&parsed,&error)&&!parsed.NativeButtonLabels,"original roundtrip");
 auto j=nlohmann::json::parse(text);j["native_button_labels"]="false";
 Check(!ParseNativeControlConfigText(j.dump(),&parsed,&error),"invalid type rejected");
 for(unsigned i=0;i<7;++i)Check(Mm3dButtonOverlay::PartsButton(float(112+i*16)/256,0,16.f/256,16.f/128)!=NativeControlAction::Count,"parts button crop");
 Check(Mm3dButtonOverlay::PartsButton(112.f/256,16.f/128,16.f/256,16.f/128)==NativeControlAction::Count,"other atlas row native");
 Check(Mm3dButtonOverlay::PartsButton(0,0,16.f/256,16.f/128)==NativeControlAction::Count,"unknown icon native");
}
void TestTouchHud(){
 using ThreeDsRecomp::Input::TouchAnchor;
 auto m=Fixture();const auto original=m.words;
 Mm3dButtonOverlay touch;touch.Configure(NativeControlDefaults(),NativePromptSet::Touch);
 Check(!touch.Capture(m,layout,node,3),"touch observation never suppresses native HUD art");
 Check(touch.PendingLabels()==0&&m.words==original,"touch observation adds no labels and writes nothing");
 const auto& x=touch.TouchGroups()[size_t(Mm3dTouchHudElement::ItemX)];
 Check(x.Known&&std::abs(x.X-(308.f+40.f/3.f))<.01f&&x.Y==28.f&&x.Radius==8.f,"live X item pane centre and radius");
 auto ring=Fixture();ring.Name(0x10020100,"AActionIconBase");ring.Name(0x10020200,"bb00_ybtn");
 ring.F(geo+0x20,38);ring.F(geo+0x24,38);(void)touch.Capture(ring,layout,node,3);
 const auto& a=touch.TouchGroups()[size_t(Mm3dTouchHudElement::ActionA)];
 Check(a.Known&&std::abs(a.X-(319.f+40.f/3.f))<.01f&&a.Y==39.f&&a.Radius==19.f,"whole A ring, not its letter crop");
 auto one=Fixture();one.Name(0x10020200,"disp_1_pane");(void)touch.Capture(one,layout,node,3);
 Check(touch.TouchGroups()[size_t(Mm3dTouchHudElement::ItemI)].Known,"Restoration I pane recorded");
 Mm3dButtonOverlay pad;pad.Configure(NativeControlDefaults(),NativePromptSet::Xbox);(void)pad.Capture(m,layout,node,3);
 Check(!pad.TouchGroups()[size_t(Mm3dTouchHudElement::ItemX)].Known,"controller prompts record no touch groups");
 auto menu=Fixture();menu.Name(0x10020100,"MenuItem");Mm3dButtonOverlay other;other.Configure(NativeControlDefaults(),NativePromptSet::Touch);
 (void)other.Capture(menu,layout,node,3);
 for(const auto& g:other.TouchGroups())Check(!g.Known,"non-HUD layouts are not touch groups");
 ThreeDsRecomp::Input::TouchAnchorSet anchors{};const float canvas=240.f*16.f/9.f;
 anchors[size_t(TouchAnchor::X)]={true,.80f,.40f,.88f,.54f};anchors[size_t(TouchAnchor::A)]={true,.88f,.55f,.98f,.72f};
 const auto plan=PlanMm3dTouchHud(touch.TouchGroups(),anchors,canvas,Oot3dNativeGame::ResolveTopScreenTouchControlShape);
 const auto shape=Oot3dNativeGame::ResolveTopScreenTouchControlShape(TouchAnchor::X,anchors[size_t(TouchAnchor::X)],canvas);
 const auto& px=plan[size_t(Mm3dTouchHudElement::ItemX)];
 Check(px.Active&&px.ToX==shape.ContentX&&px.ToY==shape.ContentY&&std::abs(px.Scale-shape.ContentHalf*.9f/8.f)<1e-4f,"X item to its anchor, OoT3D fill");
 float cx=x.X,cy=x.Y;px.Apply(cx,cy);Check(cx==shape.ContentX&&cy==shape.ContentY,"group centre lands on control content centre");
 float ex=x.X+8,ey=x.Y;px.Apply(ex,ey);Check(std::abs(ex-cx-shape.ContentHalf*.9f)<1e-3f,"group edge lands at 0.9 of content half");
 Check(plan[size_t(Mm3dTouchHudElement::ActionA)].Active,"A ring anchored");
 Check(!plan[size_t(Mm3dTouchHudElement::ItemI)].Active,"no ZL anchor leaves I native");
 Check(!plan[size_t(Mm3dTouchHudElement::ItemY)].Active,"unseen Y group never invented");
 Check(!PlanMm3dTouchHud(touch.TouchGroups(),anchors,0.f,Oot3dNativeGame::ResolveTopScreenTouchControlShape)[0].Active,"degenerate canvas plans nothing");
 Check(Mm3dTouchHudAnchor(Mm3dTouchHudElement::ItemII)==TouchAnchor::Zr&&Mm3dTouchHudAnchor(Mm3dTouchHudElement::Dpad)==TouchAnchor::Dpad,"element anchor facts");
}
void TestDeviceSwitch(){
 // Mirrors oot3d_native_a32_input_tests' prompt transitions, then feeds the
 // runtime's ActivePromptSet into the MM3D overlay as the frame loop does.
 using ThreeDsRecomp::Input::InputActivity;
 NativeControlConfigRuntime runtime({},NativeControlDefaults());
 NativeControlDeviceDescriptor xbox;xbox.InstanceId=1;xbox.Prompts=NativePromptSet::Xbox;
 NativeControlDeviceDescriptor dualSense;dualSense.InstanceId=2;dualSense.Prompts=NativePromptSet::PlayStation;
 const auto pads=[&](std::initializer_list<NativeControlDeviceDescriptor> devices,int32_t selected){
  std::vector<NativeControlDeviceDescriptor> list(devices);for(auto& d:list)d.Selected=d.InstanceId==selected;runtime.ObserveDevices(std::move(list));};
 auto m=Fixture();Mm3dButtonOverlay overlay;
 const auto frame=[&](uint64_t draw){
  overlay.Configure(runtime.Snapshot().Config,runtime.ActivePromptSet());overlay.Capture(m,layout,node,3);
  overlay.SubmitCommandList(0x14001000,0x100);overlay.ObserveDraw(draw);overlay.Select(std::span(&draw,1));
  return overlay.Compose(3);};
 const auto glyph=[&]{return NativeButtonOverlayGlyph(NativeControlAction::X,runtime.ActivePromptSet(),runtime.Snapshot().Config);};
 Check(runtime.ActivePromptSet()==NativePromptSet::Keyboard&&!frame(1).empty(),"no pad: keyboard labels on the MM HUD");
 const auto keyboard=glyph();
 pads({xbox,dualSense},1);Check(runtime.ActivePromptSet()==NativePromptSet::Xbox&&!frame(2).empty(),"pad hot-plugged: Xbox labels");
 const auto xboxGlyph=glyph();Check(keyboard&&xboxGlyph&&*keyboard!=*xboxGlyph,"hot-plug changes the MM X label art");
 pads({xbox,dualSense},2);Check(runtime.ActivePromptSet()==NativePromptSet::PlayStation&&!frame(3).empty(),"the selected pad of two");
 const auto ps=glyph();Check(ps&&*ps!=*xboxGlyph,"selection switch changes the MM label family");
 pads({},-1);Check(runtime.ActivePromptSet()==NativePromptSet::Keyboard&&!frame(4).empty(),"pad unplugged: keyboard again");
 pads({dualSense},2);Check(runtime.ActivePromptSet()==NativePromptSet::PlayStation&&!frame(5).empty(),"pad reconnected without a press");
 runtime.ObserveInputActivity(InputActivity::Touch,6.0);
 Check(runtime.ActivePromptSet()==NativePromptSet::Touch&&frame(6).empty(),"touch: native MM letters, no host labels");
 Check(overlay.TouchGroups()[size_t(Mm3dTouchHudElement::ItemX)].Known,"touch: HUD groups recorded for anchors");
 runtime.ObserveInputActivity(InputActivity::Controller,7.0);
 Check(!frame(7).empty()&&!overlay.TouchGroups()[size_t(Mm3dTouchHudElement::ItemX)].Known,"back to the pad: labels return, touch groups dropped");
}
}

struct WritableFixture:MemoryFixture{void Write(U32 p,U32 v){words[p]=v;}};
// A HudItemIconSet tree as dekui HudButtons::Hook::Begin walks it.
WritableFixture HudTree(){
 WritableFixture m;constexpr U32 lay=0x11000000,res=0x11001000,all=0x11002000,list=0x11003000,names=0x11004000,rootList=0x11005000;
 m.words[lay+4]=res;m.words[res+0x18]=names;m.Name(names,"HudItemIconSet");
 m.words[lay+8+0x14]=rootList;m.words[lay+8+0x18]=1;m.words[rootList]=all;
 m.words[all+0x10]=names+0x40;m.Name(names+0x40,"all");m.words[all+0x14]=list;
 const char* nodes[]{"icon_item_x_g","icon_item_y_g","icon_item_1_g","icon_item_2_g","btn_x_g","btn_y_g","btn_corner01_g","btn_corner03_g"};
 m.words[all+0x18]=8;
 for(U32 i=0;i<8;++i){const U32 n=0x11010000+i*0x200;m.words[list+i*4]=n;m.words[n+0x10]=names+0x80+i*0x20;m.Name(names+0x80+i*0x20,nodes[i]);
  for(U32 c=0;c<4;++c){m.F(n+0xe0+c*4,c==0?1.f:0.f);m.F(n+0xf0+c*4,c==1?1.f:0.f);}
  m.F(n+0xec,300.f+i);m.F(n+0xfc,20.f);m.words[n+0x20]=0;m.words[n+0x24]=0xffff;m.words[n+0x18]=0;m.words[n+0x14]=0;}
 return m;
}
void TestTouchHudConsumer(){
 using ThreeDsRecomp::Input::TouchAnchor;
 namespace HB=Mm3dDekui::HudButtons;
 HB::Apply(HB::Options{});
 auto m=Fixture();Mm3dButtonOverlay touch;touch.Configure(NativeControlDefaults(),NativePromptSet::Touch);
 (void)touch.Capture(m,layout,node,3);
 ThreeDsRecomp::Input::TouchAnchorSet anchors{};const float canvas=240.f*16.f/9.f;
 anchors[size_t(TouchAnchor::X)]={true,.80f,.40f,.88f,.54f};
 touch.ApplyTouchHud(anchors,canvas,Oot3dNativeGame::ResolveTopScreenTouchControlShape);
 const auto applied=touch.AppliedTouchHud()[size_t(Mm3dTouchHudElement::ItemX)];
 Check(applied.Active&&HB::touch[1].active&&HB::touch[1].scale==applied.Scale&&!HB::touch[2].active,"X plan handed to dekui HudButtons slot 1 only");
 // The real dekui HudButtons transform moves the native X group's world matrix.
 auto tree=HudTree();constexpr U32 xg=0x11010000;HB::Hook hook;
 hook.Begin(tree,0x11000000);
 const float tx=std::bit_cast<float>(tree.words[xg+0xec]),xx=std::bit_cast<float>(tree.words[xg+0xe0]);
 Check(std::abs(xx-applied.Scale)<1e-5f&&std::abs(tx-(applied.Scale*300.f+applied.X))<1e-3f,"native X draw relocated by touch placement");
 const float ytx=std::bit_cast<float>(tree.words[xg+0x200+0xec]);
 Check(ytx==301.f,"unanchored Y group stays at its dekui pose");
 hook.End(tree);Check(std::bit_cast<float>(tree.words[xg+0xec])==300.f,"End restores native matrix");
 // Observing through the applied placement gives back the un-moved group: no feedback.
 const auto before=touch.TouchGroups()[size_t(Mm3dTouchHudElement::ItemX)];
 auto moved=Fixture();moved.F(node+0xe0,applied.Scale);moved.F(node+0xf4,applied.Scale);
 moved.F(node+0xec,applied.Scale*300.f+applied.X);moved.F(node+0xfc,applied.Scale*20.f+applied.Y);
 (void)touch.Capture(moved,layout,node,3);
 const auto after=touch.TouchGroups()[size_t(Mm3dTouchHudElement::ItemX)];
 Check(std::abs(after.X-before.X)<1e-3f&&std::abs(after.Y-before.Y)<1e-3f&&std::abs(after.Radius-before.Radius)<1e-3f,"observed group inverted, plan stable");
 touch.Configure(NativeControlDefaults(),NativePromptSet::Xbox);
 Check(!HB::touch[1].active,"leaving touch prompts returns the HUD to dekui's layout");
 auto plain=HudTree();hook.Begin(plain,0x11000000);Check(std::bit_cast<float>(plain.words[xg+0xec])==300.f,"controller HUD unmoved");hook.End(plain);
}
int main(int argc,char** argv){try{
 TestCapture();TestPictogramAndDpad();TestText();TestFrames();TestCaptureAccounting();TestSubmissionOwnership();TestInlineAndChordBounds();TestSettings();TestTouchHud();TestTouchHudConsumer();TestDeviceSwitch();
 if(argc==2){std::ifstream in(argv[1],std::ios::binary);Check(bool(in),"code image opens");
  for(auto [pc,word]:ButtonEmission::Guards){U32 w=0;in.seekg(pc-0x100000);in.read(reinterpret_cast<char*>(&w),4);Check(w==word,"EUR emission guard agrees");}
  for(auto [pc,word]:ButtonGlyphEmission::Guards){U32 w=0;in.seekg(pc-0x100000);in.read(reinterpret_cast<char*>(&w),4);Check(w==word,"EUR glyph renderer guard agrees");}}
 std::cout<<checks<<" overlay checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
