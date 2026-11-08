#include "mm3d_actor_visibility.h"
#include "../dekui/prop_draw_distance.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <source_location>
using namespace Oot3dNativeGame;
using namespace Oot3dNativeGame::Mm3dVisibility;
static void Check(bool v,std::source_location at=std::source_location::current()) {
    if(!v){std::cerr<<"failure at "<<at.line()<<'\n';std::exit(1);}
}
struct Memory {
    std::map<uint32_t,uint8_t> bytes;unsigned reads=0,writes=0;
    bool Read8(uint32_t p,uint8_t* v){++reads;auto i=bytes.find(p);if(i==bytes.end())return false;*v=i->second;return true;}
    bool Write8(uint32_t p,uint8_t v){++writes;if(!bytes.contains(p))return false;bytes[p]=v;return true;}
    bool Read32(uint32_t p,uint32_t* v){*v=0;for(unsigned i=0;i<4;++i){uint8_t b=0;if(!Read8(p+i,&b))return false;*v|=uint32_t(b)<<(8*i);}return true;}
    void Word(uint32_t p,uint32_t v){for(unsigned i=0;i<4;++i)bytes[p+i]=uint8_t(v>>(i*8));}
    void Float(uint32_t p,float v){Word(p,std::bit_cast<uint32_t>(v));}
    std::optional<uint32_t> Read(uint32_t p){uint32_t w=0;return Read32(p,&w)?std::optional(w):std::nullopt;}
    uint32_t Word(uint32_t p){uint32_t w=0;Check(Read32(p,&w));return w;}
};
struct Cpu {std::array<uint32_t,16> r{};std::array<uint32_t,64> vfp{};uint32_t cpsr=0xA0000010;};
constexpr uint32_t P=0x08045000,A=0x08100000;
static void Proof(Memory& m) {
    for(const auto& s:Sites)for(unsigned i=0;i<4;++i)m.Word(s.Pc+i*4,s.Words[i]);
    for(unsigned i=0;i<FadeCode.size();++i)m.Word(0x1754E0+4*i,FadeCode[i]);
    for(unsigned i=0;i<Mm3dDekui::ActorDrawDistance::Cull.size();++i)m.Word(0x190D48+4*i,Mm3dDekui::ActorDrawDistance::Cull[i]);
}
static void Fixture(Memory& m) {
    m.Word(P+0x138,10);m.Word(P+0x148,45);
    m.Float(P+0x210,60);m.Float(P+0x2B4,1);m.Float(P+0x2C8,1);
    for(unsigned i=0;i<8;++i)m.Float(P+0xC474+i*4,i==0||i==5?1.f:0.f);
    m.Word(A,0x040123);m.Word(A+4,0);m.Word(A+0x138,0);m.Word(A+0x140,0x200000);m.Word(A+0x144,0x300000);
    m.bytes[A+0x124]=2;m.bytes[A+0x1EE]=3;m.bytes[A+0x1F0]=1;m.bytes[P+0x20BE]=20;
    m.Float(A+0xF0,0);m.Float(A+0xF4,0);m.Float(A+0xF8,150);
}
static void Begin(Source& s,Cpu& c,Memory& m) {
    c.r[0]=P;c.r[1]=P+0x20B0;c.r[14]=0x15EF68;s.Observe(0x175094,c,m,true);
}
static void Call(Source& s,Cpu& c,Memory& m,uint32_t ret=0x1754E0,bool light=true) {
    c.r[0]=P;c.r[1]=A+0xF0;c.r[4]=A;c.r[5]=P;c.r[13]=0x0FFFF000;c.r[14]=ret;
    c.vfp[0]=std::bit_cast<uint32_t>(100.f);c.vfp[1]=c.vfp[2]=std::bit_cast<uint32_t>(10.f);c.vfp[3]=std::bit_cast<uint32_t>(1.f);
    s.Observe(0x190D48,c,m,light);
}
int main(int argc,char**argv) {
    Sample x{0,0,150,1,100,10,10,60,1,1};Check(Valid(x));
    Check(Classify(x)==2&&Classify(x,{3,1,1})==0);
    x.Z=330;Check(Classify(x,{3,1,1})==2);x.Z=-10;Check(Classify(x,{3,1,1})==1);
    x.Z=50;x.X=11;Check(Classify(x)==1&&Classify(x,{1,2,1})==0);
    x.Y=11;Check(Classify(x,{1,2,1})==1); // widescreen never widens Y
    x.X=0;x.Y=-11;Check(Classify(x)==1);x.Y=0;x.W=-4;Check(Classify(x)==0);
    x.Forward=10000;x.Z=15000;Check(Classify(x)==2&&Classify(x,{3,1,1})==0);
    x.Z=0;x.Fovy=45;x.Scale=1;x.DiagonalX=x.DiagonalY=1;x.X=3.8f;
    Check(Classify(x)==0);x.X=4;Check(Classify(x)==1); // actual 3DS constants
    x.X=std::numeric_limits<float>::quiet_NaN();Check(!Valid(x));
    Check(PresentedExtent(2,1)==2&&PresentedExtent(1,2)==1&&PresentedExtent(1.00001f,1)==1);
    Check(NativeFade(2,3)==2&&NativeFade(2,0)==0&&NativeFade(1,3)==0);
    Memory m;Cpu c;Source s;s.Observe(0x190D48,c,m,true);Check(!m.reads&&!m.writes);
    Proof(m);const auto proof=m.bytes;
    Check(s.Verify(m,[](uint32_t){return true;}));
    for(const auto& site:Sites)Check(!s.Verify(m,[&](uint32_t p){return p!=site.Pc;}));
    for(const auto& [p,b]:proof)if(!(p&3)) {
        m.bytes[p]=b^1;Check(!s.Verify(m,[](uint32_t){return true;}));
        m.bytes.erase(p);Check(!s.Verify(m,[](uint32_t){return true;}));m.bytes[p]=b;
    }
    Check(s.Verify(m,[](uint32_t){return true;}));Fixture(m);Begin(s,c,m);Call(s,c,m);
    c.r[0]=2;s.Observe(0x1754E0,c,m,true);Check(c.r[0]==2&&!m.writes); // EDD off
    s.SetExtended(true);Begin(s,c,m);Call(s,c,m);c.r[0]=2;
    const auto before=c; s.Observe(0x1754E0,c,m,true);Check(c.r[0]==0);
    for(unsigned i=1;i<16;++i)Check(c.r[i]==before.r[i]);Check(c.vfp==before.vfp&&c.cpsr==before.cpsr);
    // Execute the native store/fade/draw's effects, including an unrelated flag.
    m.bytes[A+0x124]=0;m.bytes[A+0x1EE]=4;m.bytes[A+0x1F0]=0;m.Word(A+4,0x140);
    s.Observe(0x175650,c,m,true);
    Check(m.bytes[A+0x124]==2&&m.bytes[A+0x1EE]==2&&m.bytes[A+0x1F0]==1&&m.Word(A+4)==0x140);
    Check(s.mStats.ActorGrants==1&&s.mStats.Restored==1);
    Begin(s,c,m);Call(s,c,m);c.r[0]=2;s.Observe(0x1754E0,c,m,true);
    Check(m.bytes[A+0x1EE]==4&&m.bytes[A+0x1F0]==0); // presentation fade retained
    m.bytes[A+0x1EE]=5;s.Observe(0x175650,c,m,true);Check(m.bytes[A+0x1EE]==1);
    Begin(s,c,m);Call(s,c,m);c.r[0]=1;s.Observe(0x1754E0,c,m,true);
    Check(c.r[0]==1&&s.mStats.NativeMismatch==1); // native disagreement never rewritten
    Call(s,c,m);c.r[0]=2;s.Observe(0x1754E0,c,m,false);Check(c.r[0]==2); // arena not proven
    Call(s,c,m,0x575DB8);c.r[0]=2;s.Observe(0x575DB8,c,m,true);Check(c.r[0]==2&&s.mStats.UnknownCallers==1);
    Call(s,c,m,0x529C60);c.r[0]=2;s.Observe(0x529C60,c,m,true);Check(c.r[0]==0&&s.mStats.WoodGrants==1);
    m.bytes[P+0x20BE]=165;Call(s,c,m,0x529C60);c.r[0]=2;s.Observe(0x529C60,c,m,true);Check(c.r[0]==2);
    // Grass forward distance is already owned by vegetation: do not multiply it.
    Call(s,c,m,0x436DC0);c.r[0]=2;s.Observe(0x436DC0,c,m,true);Check(c.r[0]==2);
    m.Float(A+0xF8,50);m.Float(A+0xF0,11);m.Float(P+0xC474,2);
    Call(s,c,m,0x436DC0);c.r[0]=1;s.Observe(0x436DC0,c,m,true);Check(c.r[0]==0&&s.mStats.GrassGrants==1);
    // Outside native class restores zero fade and only clears the inside bit.
    Begin(s,c,m);Call(s,c,m);c.r[0]=1;s.Observe(0x1754E0,c,m,true);Check(c.r[0]==0);
    m.bytes[A+0x124]=0;m.bytes[A+0x1EE]=10;m.Word(A+4,0x140);
    s.Observe(0x175650,c,m,true);Check(m.bytes[A+0x1EE]==0&&m.bytes[A+0x124]==1&&m.Word(A+4)==0x100);
    // Existing unconditional draw actors retain the original result.
    m.Word(A+4,0x20);Begin(s,c,m);Call(s,c,m);c.r[0]=1;s.Observe(0x1754E0,c,m,true);Check(c.r[0]==1);
    // A wrong return stack never grants; reset drops presentation history.
    m.Word(A+4,0);Call(s,c,m);++c.r[13];c.r[0]=1;s.Observe(0x1754E0,c,m,true);Check(c.r[0]==1);
    s.Reset();Begin(s,c,m);Call(s,c,m);c.r[0]=1;s.Observe(0x1754E0,c,m,true);
    m.Word(A,0x040124);const auto writes=m.writes;s.Observe(0x175650,c,m,true);Check(m.writes==writes&&s.mStats.IdentityChanges==1);
    // Product ownership neutralizes ONLY the legacy common-distance argument.
    m.Word(A,0x040123);auto bits=std::bit_cast<uint32_t>(100.f);
    Mm3dDekui::ActorDrawDistance::result_policy_owned=true;
    Check(Mm3dDekui::ActorDrawDistance::Apply(m,A,bits,3)==bits);
    m.Word(A,0x060065);Check(Mm3dDekui::PropDrawDistance::Apply(m,A,bits,3)==bits);
    Check(Mm3dDekui::PropDrawDistance::Threshold(1200,3)==std::bit_cast<uint32_t>(3600.f));
    Mm3dDekui::ActorDrawDistance::result_policy_owned=false;
    Check(Mm3dDekui::PropDrawDistance::Apply(m,A,bits,3)==std::bit_cast<uint32_t>(300.f));
    if(argc==2) {
        Memory rom;std::ifstream f(argv[1],std::ios::binary);Check(bool(f));
        for(const auto&[p,b]:proof){char byte=0;f.seekg(p-0x100000);f.read(&byte,1);Check(bool(f));rom.bytes[p]=uint8_t(byte);}
        Check(s.Verify(rom,[](uint32_t){return true;}));
    }
    std::cout<<"MM3D actor/foliage visibility contracts: PASS\n";
}
