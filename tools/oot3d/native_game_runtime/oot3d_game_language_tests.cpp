// SPDX-License-Identifier: GPL-3.0-or-later
#include "oot3d_game_language.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Oot3dNativeGame;
namespace {
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
std::string Read(const std::filesystem::path& p){std::ifstream f(p);return {(std::istreambuf_iterator<char>(f)),{}};}
void Write(const std::filesystem::path& p,const std::string& text){std::ofstream f(p);f<<text;Check(bool(f),"fixture write failed");}
struct Fixture {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("triaevum-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){std::filesystem::create_directories(path);}
    ~Fixture(){std::error_code e;std::filesystem::remove_all(path,e);}
};
}
int main(int argc,char** argv){try{
    Fixture fixture;
    std::vector<GameLanguage> languages={{"en","English",1},{"fr","Francais",2},{"de","Deutsch",3},{"it","Italiano",4},{"es","Espanol",5}};
    if(argc==2){
        languages=DetectGameLanguages(argv[1],0,0,TitleAdapter::Mm3d);
        Check(languages.size()==5,"EUR did not expose exactly five populated languages");
        for(const auto& code:{"en","de","fr","es","it"})
            Check(std::any_of(languages.begin(),languages.end(),[&](const auto& l){return l.Code==code;}),"missing native EUR language");
    }
    for(const auto& language:languages){
        const auto path=fixture.path/(language.Code+".json");
        Write(path,GameLanguageDocument(languages,language.Code).dump());
        GameLanguageSettings settings(path,languages);
        Check(settings.Error().empty()&&settings.Selected()==language.Code&&settings.BootLanguage()==language.Code&&settings.SystemId()==language.SystemId,"native language initialization differs");
        const std::string next=language.Code=="en"?"it":"en";
        Check(settings.Select(next),"pending language selection failed");
        Check(settings.Selected()==next&&settings.BootLanguage()==language.Code&&settings.SystemId()==language.SystemId,"selection mutated the running guest language without restart");
        const auto before=Read(path);Check(!settings.Select("nl")&&Read(path)==before&&settings.Selected()==next,"unsupported language mutated preferences");
        GameLanguageSettings restarted(path,languages);Check(restarted.BootLanguage()==next,"restart did not adopt saved preference");
    }
    const auto malformed=fixture.path/"malformed.json";Write(malformed,"not JSON");
    GameLanguageSettings bad(malformed,languages);Check(!bad.Error().empty()&&Read(malformed)=="not JSON","malformed preferences were silently overwritten");
    Check(bad.Select("fr")&&bad.Error().empty(),"explicit preference repair failed");
    const auto unwritable=fixture.path/"directory-not-a-file";std::filesystem::create_directory(unwritable);
    GameLanguageSettings failed(unwritable,languages);Check(!failed.Select("de")&&!failed.Error().empty()&&std::filesystem::is_directory(unwritable),"save error was hidden or destructively repaired");
    const auto unsupported=fixture.path/"unsupported.json";Write(unsupported,R"({"format":"triaevum_game_language_v1","selected":"nl"})");
    GameLanguageSettings normalized(unsupported,languages);Check(normalized.BootLanguage()==languages.front().Code,"unsupported persisted language did not normalize to a populated language");
    Check(GameLanguageDocument(languages,"nl")["selected"]==languages.front().Code,"unsupported launch selection differs");
    std::cout<<"Five native EUR languages: populated archives, restart-only semantics, unsupported selection, malformed-file preservation, save failure and explicit repair passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
