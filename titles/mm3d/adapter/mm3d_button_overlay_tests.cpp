#include "mm3d_button_overlay.h"
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
 for(U32 off:{0xe0U,0xf4U,0x128U,0x12cU,0x130U,0x134U})m.F(node+off,1);
 for(U32 off:{0xe4U,0xf0U})m.F(node+off,0);
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
}
int main(int argc,char** argv){try{
 TestCapture();TestPictogramAndDpad();TestText();TestFrames();TestCaptureAccounting();TestSubmissionOwnership();TestSettings();
 if(argc==2){std::ifstream in(argv[1],std::ios::binary);Check(bool(in),"code image opens");
  for(auto [pc,word]:ButtonEmission::Guards){U32 w=0;in.seekg(pc-0x100000);in.read(reinterpret_cast<char*>(&w),4);Check(w==word,"EUR emission guard agrees");}}
 std::cout<<checks<<" overlay checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
