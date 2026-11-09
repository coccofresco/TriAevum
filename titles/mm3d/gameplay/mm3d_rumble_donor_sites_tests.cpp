#include "mm3d_rumble_donor_sites.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
using namespace Oot3dNativeGame::Mm3dRumble;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++fails; } }while(0)
// Re-scan mode (auditor F09 action): given the 2Ship mm/src root, walk every
// .c file, collect each call-site line of the three rumble entry points
// (definitions "void Name(" and the Player_RequestRumble wrapper body excluded) and require an exact match with the
// committed table: same (file,line) set and the same entry point (CallKind).
static std::string Trim(const std::string& t){
  size_t a=t.find_first_not_of(" \t\r"), b=t.find_last_not_of(" \t\r");
  return a==std::string::npos?std::string():t.substr(a,b-a+1);
}
static int Rescan(const std::filesystem::path& root){
  namespace fs=std::filesystem;
  std::map<std::pair<std::string,uint32_t>,CallKind> found;
  for(const auto& e:fs::recursive_directory_iterator(root)){
    if(!e.is_regular_file()||e.path().extension()!=".c") continue;
    std::ifstream f(e.path()); std::string line; uint32_t n=0;
    std::string rel=fs::relative(e.path(),root).generic_string();
    bool inWrapper=false; // body of the Player_RequestRumble forwarding wrapper
    while(std::getline(f,line)){
      ++n; std::string t=Trim(line);
      if(t.rfind("void Player_RequestRumble(",0)==0) inWrapper=true;
      if(inWrapper){ if(t=="}") inWrapper=false; continue; }
      int hit=-1;
      if(t.find("Player_RequestRumble(")!=std::string::npos) hit=int(CallKind::Player);
      else if(t.find("Rumble_Override(")!=std::string::npos) hit=int(CallKind::Override);
      else if(t.find("Rumble_Request(")!=std::string::npos) hit=int(CallKind::Request);
      if(hit<0||t.rfind("void ",0)==0) continue;
      found[{rel,n}]=CallKind(hit);
    }
  }
  CHECK(found.size()==DonorRumbleSites.size());
  for(const auto& s:DonorRumbleSites){
    auto it=found.find({std::string(s.File),s.Line});
    CHECK(it!=found.end());
    if(it!=found.end()&&it->second!=s.Kind){ std::printf("DRIFT %s:%u\n",it->first.first.c_str(),s.Line); ++fails; }
  }
  std::printf("%s rescan donor_sites=%zu table=%zu\n",fails?"FAILED":"PASS",found.size(),DonorRumbleSites.size());
  return fails?1:0;
}
int main(int argc,char** argv){
  if(argc>1) return Rescan(argv[1]);
  // 2Ship census at W/external/2ship2harkinian: 102 call sites (excl. wrappers).
  CHECK(DonorRumbleSites.size()==102);
  std::set<std::pair<std::string,uint32_t>> seen;
  size_t damage=0,landing=0,menu=0;
  for(const auto& s:DonorRumbleSites){
    CHECK(seen.insert({std::string(s.File),s.Line}).second);
    CHECK(s.Function!="?");
    CHECK(s.Kind==CallKind::Request||s.Kind==CallKind::Override||s.Kind==CallKind::Player);
    if(s.Disp==Disposition::SampledDamage){ ++damage; CHECK(s.Cat==Category::PlayerDamage); }
    if(s.Disp==Disposition::SampledHardLanding){ ++landing; CHECK(s.Function=="func_80836F10"); }
    if(s.Disp==Disposition::ExcludedMenuNoGameRumble){ ++menu; CHECK(s.File.find("gamestates")!=std::string_view::npos); }
  }
  CHECK(damage==7); CHECK(landing==2); CHECK(menu==4);
  std::printf("%s rows=%zu damage=%zu landing=%zu menu=%zu open=%zu\n",fails?"FAILED":"PASS",
              DonorRumbleSites.size(),damage,landing,menu,DonorRumbleSites.size()-damage-landing-menu);
  return fails?1:0;
}
