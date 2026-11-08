#include "mm3d_photo_camera.h"
#include <cstdlib>
#include <iostream>
#include <map>
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
#error This test must compile without developer tools
#endif
using namespace Oot3dNativeGame;
namespace {
void Check(bool v,const char* s){if(!v){std::cerr<<s<<'\n';std::exit(1);}}
struct Memory {
  static constexpr uint32_t P=0x08045B40,A=0x09000000,Out=0x0FFFFE00;
  std::map<uint32_t,uint8_t> bytes;
  unsigned writes=0; uint32_t failOnce=0;
  template<class T> void Put(uint32_t a,const T& v){const auto* b=reinterpret_cast<const uint8_t*>(&v);for(size_t i=0;i<sizeof(T);++i)bytes[a+uint32_t(i)]=b[i];}
  bool Read(uint32_t a,std::span<uint8_t> b){for(size_t i=0;i<b.size();++i){auto f=bytes.find(a+uint32_t(i));if(f==bytes.end())return false;b[i]=f->second;}return true;}
  bool Write(uint32_t a,std::span<const uint8_t> b){++writes;for(size_t i=0;i<b.size();++i){auto f=bytes.find(a+uint32_t(i));if(f==bytes.end())return false;f->second=b[i];if(failOnce==a){failOnce=0;return false;}}return true;}
  Memory(){
    for(uint32_t i=0;i<0x11030;++i)bytes[P+i]=0;
    for(uint32_t i=0;i<0x140F8;++i)bytes[0x7751D8+i]=0;
    for(uint32_t i=0;i<0x200;++i)bytes[A+i]=0;
    for(uint32_t i=0;i<6;++i)bytes[Out+i]=0;
    Put(0x656B5C,P+0x1F8);Put(P+0x4A8,P);Put(P+0x200,400U);Put(P+0x208,240U);
    Put(P+0x13C,uint8_t{2});Put(P+0x13D,uint8_t{4});Put(P+0x148,int16_t{0x6F});
    Put(P+0x20DC,A);Put(A+2,uint8_t{2});Put(0x7751D8+0x26,uint8_t{4});
    Put(P+0xA68,P+0x408);Put(P+0x408+0x154,uint16_t{7});Put(P+0x408+0x156,uint16_t{1});
    Put(P+0x408+0x50,std::array<float,3>{0,0,100});Put(P+0x408+0x5C,std::array<float,3>{0,0,0});
    for(const auto& site:{kMm3dCameraUpdateSite,kMm3dPlayUpdateSite,kMm3dTransitionSite})
      for(uint32_t i=0;i<site.Words.size();++i)Put(site.Pc+4*i,site.Words[i]);
  }
};
}
int main(){
  Memory g; Mm3dPhotoCamera abi; DevFreeCamera camera;
  Check(abi.OwnsInput(g),"product camera admitted without developer ABI");
  Check(!abi.Apply(g,camera,Memory::Out,Memory::P+0x408)&&g.writes==0,"inactive is inert");
  camera.SetActive(true);Check(abi.Apply(g,camera,Memory::Out,Memory::P+0x408),"product applies");
  for(uint32_t offset:{0xAACU,0xA7AU,0xCCAAU}){
    g.Put(Memory::P+offset,uint16_t{1});g.writes=0;
    Check(!abi.OwnsInput(g)&&!abi.Apply(g,camera,Memory::Out,Memory::P+0x408)&&!g.writes,"menus/transitions relinquish ownership");
    g.Put(Memory::P+offset,uint16_t{0});
  }
  for(uint16_t setting:{uint16_t{9},uint16_t{10}}){g.Put(Memory::P+0x408+0x156,setting);Check(!abi.OwnsInput(g),"scripted camera native");}
  g.Put(Memory::P+0x408+0x156,uint16_t{1});
  g.writes=0;Check(!abi.Apply(g,camera,Memory::Out,Memory::P+0x5A0)&&!g.writes,"subcamera not written");
  const auto before=g.bytes;g.failOnce=Memory::P+0x408+0x5C;
  Check(!abi.Apply(g,camera,Memory::Out,Memory::P+0x408)&&g.bytes==before,"partial camera write rolled back");
  for(const auto& site:{kMm3dCameraUpdateSite,kMm3dPlayUpdateSite,kMm3dTransitionSite})
    for(uint32_t i=0;i<site.Words.size();++i){g.Put(site.Pc+4*i,site.Words[i]^1U);Check(!abi.OwnsInput(g),"wrong image denies input");g.Put(site.Pc+4*i,site.Words[i]);}
  std::cout<<"MM3D product photo camera passed\n";
}
