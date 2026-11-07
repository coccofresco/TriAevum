#include "mm3d_rumble_events.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <limits>

using namespace Oot3dNativeGame;
namespace {
void Check(bool v,const char* message) { if (!v) { std::cerr<<message<<'\n'; std::exit(1); } }
struct Memory {
  static constexpr uint32_t P=0x08045B40, A=0x09000000;
  std::map<uint32_t,uint8_t> bytes;
  unsigned reads=0;
  template<class T> void Put(uint32_t a,T v) {
    const auto* b=reinterpret_cast<const uint8_t*>(&v);
    for (unsigned i=0;i<sizeof(T);++i) bytes[a+i]=b[i];
  }
  template<class T> bool Get(uint32_t a,T* v) {
    ++reads;
    auto* b=reinterpret_cast<uint8_t*>(v);
    for (unsigned i=0;i<sizeof(T);++i) {
      auto it=bytes.find(a+i); if(it==bytes.end()) return false; b[i]=it->second;
    } return true;
  }
  bool Read8(uint32_t a,uint8_t* v) {return Get(a,v);}
  bool Read16(uint32_t a,uint16_t* v) {return Get(a,v);}
  bool Read32(uint32_t a,uint32_t* v) {return Get(a,v);}
  Memory() {
    for(const auto& site:Mm3dRumble::Guards)
      for(uint32_t i=0;i<site.Words.size();++i) Put(site.Pc+4*i,site.Words[i]);
    Put(0x1E7464,0x68D59CU);
    Put(Mm3dRumble::ViewSlot,P+0x1F8); Put(P+0x4A8,P);
    Put(P+0x13D,uint8_t{4}); Put(P+0x13C,uint8_t{2}); Put(P+0x138,100U);
    Put(Mm3dRumble::Common+0x1361C,0U); Put(Mm3dRumble::Common+0x1C,0U);
    Put(P+0xAAC,uint16_t{0}); Put(P+0xA7A,uint16_t{0}); Put(P+0xC529,uint8_t{0});
    Put(P+0xC85A,uint8_t{0}); Put(P+0xCCAA,uint16_t{0});
    Put(P+0x148,uint16_t{0x6F}); Put(P+0x20DC,A);
    Put(A,uint16_t{0}); Put(A+2,uint8_t{2}); Put(A+0x140,0x1F9C70U);
    Put(Mm3dRumble::Common+0x26,uint8_t{4}); Put(Mm3dRumble::Common+0x13C,uint16_t{48});
    Put(A+0x68,0.0F); Put(A+0x94,uint16_t{1}); Put(A+0x11ED2,uint16_t{0});
    Put(A+0x92C,uint8_t{0}); Put(0x68D59C+0x230,0U);
  }
};
}
int main() {
  using namespace Mm3dRumble;
  Memory m;
  Source source;
  Check(source.Poll(m,true).empty() && m.reads==0,"unverified source is inert");
  source.Verify(m); Check(source.Verified(),"guards accept fixture");
  m.reads=0; Check(source.Poll(m,false).empty() && m.reads==0,"Off reads nothing");
  Check(source.Poll(m,true).empty(),"first frame is baseline");
  m.Put(Common+0x13C,uint16_t{40});
  auto pulses=source.Poll(m,true);
  Check(pulses.size()==1 && pulses[0].Intensity==0.625F,"health damage pulse");
  Check(source.Poll(m,true).empty(),"duplicate frame never repeats");
  m.Put(Common+0x13C,uint16_t{48}); Check(source.Poll(m,true).empty(),"healing silent");
  for(uint16_t distance:{uint16_t{200},uint16_t{201}}) {
    m.Put(Memory::A+0x94,uint16_t{0}); m.Put(Memory::A+0x68,-14.0F);
    Check(source.Poll(m,true).empty(),"airborne silent");
    m.Put(Memory::A+0x94,uint16_t{1}); m.Put(Memory::A+0x11ED2,distance); m.Put(Memory::A+0x68,0.0F);
    Check(source.Poll(m,true).size()==(distance>200?1:0),"native fall threshold");
    Check(source.Poll(m,true).empty(),"landing once");
  }
  for(uint32_t floor:{6U,9U}) {
    m.Put(Memory::A+0x94,uint16_t{0}); m.Put(Memory::A+0x68,-20.0F); (void)source.Poll(m,true);
    m.Put(0x68D59C+0x230,floor); m.Put(Memory::A+0x94,uint16_t{1});
    Check(source.Poll(m,true).empty(),"native soft floors silent");
  }
  m.Put(0x68D59C+0x230,0U);
  for(auto flag:{uint16_t{0x20},uint16_t{0x21}}) {
    m.Put(Memory::A+0x94,uint16_t{0}); (void)source.Poll(m,true);
    m.Put(Memory::A+0x94,flag); Check(source.Poll(m,true).empty(),"water cancels landing");
  }
  m.Put(Memory::A+0x94,uint16_t{1});
  for(uint32_t offset:{0xC529U,0xC85AU}) {
    m.Put(Memory::P+offset,uint8_t{1}); Check(!Read(m).Event.Gameplay,"fade/transition resets");
    m.Put(Memory::P+offset,uint8_t{0});
  }
  m.Put(Memory::P+0xCCAA,uint16_t{1}); Check(!Read(m).Event.Gameplay,"override transition resets");
  m.Put(Memory::P+0xCCAA,uint16_t{0});
  m.Put(Memory::P+0xAAC,uint16_t{1}); Check(source.Poll(m,true).empty(),"pause resets");
  m.Put(Common+0x13C,uint16_t{32}); m.Put(Memory::P+0xAAC,uint16_t{0});
  Check(source.Poll(m,true).empty(),"unpause does not replay damage");
  m.Put(Common+0x13C,uint16_t{24}); m.Put(Common+0x26,uint8_t{3});
  Check(source.Poll(m,true).empty(),"form change rebaselines");
  m.Put(Common+0x13C,uint16_t{16}); m.Put(Memory::P+0x138,1U);
  Check(source.Poll(m,true).empty(),"savestate rewind rebaselines");
  m.Put(Memory::P+0x20DC,0xFFFFFFFCU); Check(!Read(m).Event.Gameplay,"bad pointer denied");
  m.Put(Memory::P+0x20DC,Memory::A);
  m.Put(Memory::A+0x68,std::numeric_limits<float>::quiet_NaN()); Check(!Read(m).Event.Gameplay,"NaN denied");
  for(const auto& site:Guards) for(uint32_t i=0;i<site.Words.size();++i) {
    Memory bad; bad.Put(site.Pc+i*4,site.Words[i]^1U); Source s; s.Verify(bad);
    bad.reads=0; Check(!s.Verified() && s.Poll(bad,true).empty() && bad.reads==0,"guard fails closed");
    bad.Put(site.Pc+i*4,site.Words[i]); bad.bytes.erase(site.Pc+i*4); s.Verify(bad);
    Check(!s.Verified(),"unmapped guard fails closed");
  }
  std::cout<<"MM3D rumble events passed\n";
}
