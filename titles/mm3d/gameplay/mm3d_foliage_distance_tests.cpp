#include "mm3d_foliage_distance.h"
#include <fstream>
#include <iostream>
#include <map>
#include <source_location>
#include <cstdlib>
#include <limits>
using namespace Oot3dNativeGame::Mm3dFoliage;
static void Check(bool v,std::source_location at=std::source_location::current()) {
    if(!v){std::cerr<<"failure at "<<at.line()<<'\n';std::exit(1);}
}
struct Memory {
    std::map<uint32_t,uint8_t> bytes;unsigned writes=0;uint32_t fail=0;
    bool Read8(uint32_t p,uint8_t*v){auto i=bytes.find(p);if(i==bytes.end())return false;*v=i->second;return true;}
    bool Read32(uint32_t p,uint32_t*v){*v=0;for(unsigned i=0;i<4;++i){uint8_t b=0;if(!Read8(p+i,&b))return false;*v|=uint32_t(b)<<(8*i);}return true;}
    bool Write8(uint32_t p,uint8_t v){++writes;if(p==fail){fail=0;return false;}if(!bytes.contains(p))return false;bytes[p]=v;return true;}
    void Word(uint32_t p,uint32_t w){for(unsigned i=0;i<4;++i)bytes[p+i]=uint8_t(w>>(i*8));}
    uint32_t Word(uint32_t p){uint32_t w=0;Check(Read32(p,&w));return w;}
};
struct Cpu {std::array<uint32_t,16> r{};std::array<uint32_t,64> vfp{};};
static void Proof(Memory&m) {
    for(const auto&s:Sites)for(unsigned i=0;i<4;++i)m.Word(s.Pc+4*i,s.Words[i]);
    for(const auto&s:Proofs)for(unsigned i=0;i<4;++i)m.Word(s.Pc+4*i,s.Words[i]);
    for(const auto&[p,w]:std::array<std::pair<uint32_t,uint32_t>,7>{{
        {0x5268F8,ClipTable},{0x545090,ClipTable},{0x54508C,0x42200000},
        {0x519C6C,0x49A17020},{0x518128,0x4A005390},{0x4A7AB8,0xEE200A00},{0x4A7ABC,0xEE000AA0}}})m.Word(p,w);
}
int main(int argc,char**argv) {
    Memory m;Cpu c;Source s;constexpr uint32_t P=0x08045000;
    s.Observe(0x526678,c,m);Check(!m.writes);
    Proof(m);const auto proof=m.bytes;Check(s.Verify(m,[](uint32_t){return true;}));
    for(const auto&site:Sites)Check(!s.Verify(m,[&](uint32_t p){return p!=site.Pc;}));
    for(const auto&[p,b]:proof)if(!(p&3)) {
        m.bytes[p]=b^1;Check(!s.Verify(m,[](uint32_t){return true;}));
        m.bytes.erase(p);Check(!s.Verify(m,[](uint32_t){return true;}));m.bytes[p]=b;
    }
    Check(s.Verify(m,[](uint32_t){return true;}));
    c.r[1]=c.r[5]=P;c.r[13]=0xFFFF000;m.Word(c.r[13],P);m.bytes[P+0x20BE]=20;
    for(unsigned i=0;i<5;++i)m.Word(ClipTable+4*i,std::bit_cast<uint32_t>(NativeClip[i]));
    s.Observe(0x526678,c,m);Check(!m.writes);
    s.SetExtended(true);s.Observe(0x526678,c,m);
    for(unsigned i=0;i<5;++i)Check(m.Word(ClipTable+4*i)==std::bit_cast<uint32_t>(3*NativeClip[i]));
    s.Observe(0x544ED8,c,m);
    for(unsigned i=0;i<5;++i)Check(std::bit_cast<float>(m.Word(ClipTable+4*i))+40==3*(NativeClip[i]+40));
    const auto active=m.bytes;s.Observe(0x526678,c,m);Check(m.bytes!=active);
    // Partial failed table write rolls back every byte, including current field.
    const auto previous=m.bytes;m.fail=ClipTable+7;s.Observe(0x544ED8,c,m);Check(m.bytes==previous&&s.Stats.Invalid==1);
    m.bytes[P+0x20BE]=165;s.Observe(0x526678,c,m);
    for(unsigned i=0;i<5;++i)Check(m.Word(ClipTable+4*i)==std::bit_cast<uint32_t>(NativeClip[i]));
    Check(s.Stats.PressureNative==1);
    m.bytes[P+0x20BE]=20;s.Observe(0x544ED8,c,m);s.SetExtended(false);s.Observe(0x544ED8,c,m);
    for(unsigned i=0;i<5;++i)Check(m.Word(ClipTable+4*i)==std::bit_cast<uint32_t>(NativeClip[i]));
    s.SetExtended(true);m.Word(ClipTable+8,123);const auto foreign=m.bytes;
    s.Observe(0x526678,c,m);Check(m.bytes==foreign&&s.Stats.Invalid==2);
    c.vfp[0]=std::bit_cast<uint32_t>(1600.f*1600.f);const auto before=c;
    s.Observe(0x519C24,c,m);Check(c.vfp[0]==std::bit_cast<uint32_t>((1600.f*1600.f)/9));
    Check(c.r==before.r);for(unsigned i=1;i<64;++i)Check(c.vfp[i]==before.vfp[i]);
    c.vfp[0]=std::bit_cast<uint32_t>(2500.f*2500.f);s.Observe(0x518078,c,m);Check(s.Stats.RadiusChanges==2);
    s.SetExtended(false);const auto off=c.vfp;s.Observe(0x518078,c,m);Check(c.vfp==off);
    s.SetExtended(true);c.vfp[0]=std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());
    const auto bad=c.vfp;s.Observe(0x519C24,c,m);Check(c.vfp==bad&&s.Stats.Invalid==3);
    // F06 census: no id proof -> no census; with it, the actor's id names the
    // row, a culled->active->culled cycle is one spawn and one retire, an
    // unknown id is counted as unexpected (negative), never as a Mure.
    {
        Memory n;Cpu k;Source t;Proof(n);Check(t.Verify(n,[](uint32_t){return true;})&&!t.IdsVerified());
        k.r[1]=P;n.bytes[P+0x20BE]=20;constexpr uint32_t A=0x08046000,B=0x08047000;
        n.Word(A,0x3F);k.r[0]=A;  // the game's id (profile 0x68B7D8)
        t.Observe(0x526678,k,n);Check(t.Stats.Calls[0]==0);
        for(const auto&[p,w]:IdProof)n.Word(p,w);
        Check(t.Verify(n,[](uint32_t){return true;})&&t.IdsVerified());
        for(const auto&[p,w]:IdProof){const auto keep=n.Word(p);n.Word(p,w^1);t.Verify(n,[](uint32_t){return true;});
            Check(!t.IdsVerified());n.Word(p,keep);}
        Check(t.Verify(n,[](uint32_t){return true;})&&t.IdsVerified());
        t.Observe(0x526678,k,n);t.Observe(0x544ED8,k,n);t.Observe(0x544ED8,k,n);t.Observe(0x526678,k,n);
        Check(t.Stats.Calls[0]==4&&t.Stats.Actors[0]==1&&t.Stats.Spawned[0]==1&&t.Stats.Retired[0]==1);
        n.Word(B,0x9D);k.r[4]=B;k.r[5]=B;k.r[13]=0xFFFF000;n.Word(k.r[13],P);
        t.Observe(0x519C24,k,n);t.Observe(0x518078,k,n);
        Check(t.Stats.Calls[2]==2&&t.Stats.Actors[2]==1&&t.Stats.Spawned[2]==1&&t.Stats.Retired[2]==0);
        n.Word(A,0x0123);k.r[0]=A;t.Observe(0x526678,k,n);Check(t.Stats.Unexpected==1&&t.Stats.Calls[0]==4);
    }
    if(argc==2) {
        Memory rom;std::ifstream f(argv[1],std::ios::binary);Check(bool(f));
        for(const auto&[p,b]:proof){char byte=0;f.seekg(p-0x100000);f.read(&byte,1);Check(bool(f));rom.bytes[p]=uint8_t(byte);}
        for(const auto&[p,w]:IdProof)for(unsigned i=0;i<4;++i){char byte=0;f.seekg(p+i-0x100000);f.read(&byte,1);Check(bool(f));rom.bytes[p+i]=uint8_t(byte);}
        Check(s.Verify(rom,[](uint32_t){return true;})&&s.IdsVerified());
    }
    std::cout<<"MM3D ObjMure / ObjMure3: PASS\n";
}
