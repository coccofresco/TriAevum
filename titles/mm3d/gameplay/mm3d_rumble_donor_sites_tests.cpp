#include "mm3d_rumble_donor_sites.h"
#include <cstdio>
#include <set>
#include <string>
using namespace Oot3dNativeGame::Mm3dRumble;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++fails; } }while(0)
int main(){
  // 2Ship census at W/external/2ship2harkinian: 102 call sites (excl. wrappers).
  CHECK(DonorRumbleSites.size()==102);
  std::set<std::pair<std::string,uint32_t>> seen;
  size_t damage=0,landing=0,menu=0;
  for(const auto& s:DonorRumbleSites){
    CHECK(seen.insert({std::string(s.File),s.Line}).second);
    CHECK(s.Function!="?");
    CHECK(s.Call.find("Rumble")!=std::string_view::npos);
    if(s.Disp==Disposition::SampledDamage){ ++damage; CHECK(s.Cat==Category::PlayerDamage); }
    if(s.Disp==Disposition::SampledHardLanding){ ++landing; CHECK(s.Function=="func_80836F10"); }
    if(s.Disp==Disposition::ExcludedMenuNoGameRumble){ ++menu; CHECK(s.File.find("gamestates")!=std::string_view::npos); }
  }
  CHECK(damage==7); CHECK(landing==2); CHECK(menu==4);
  std::printf("%s rows=%zu damage=%zu landing=%zu menu=%zu open=%zu\n",fails?"FAILED":"PASS",
              DonorRumbleSites.size(),damage,landing,menu,DonorRumbleSites.size()-damage-landing-menu);
  return fails?1:0;
}
