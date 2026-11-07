#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace Oot3dNativeGame {
// MAP_5 G6.01: resource names, not translated UI text. Unknown owners and
// mixed quad queues remain visible; never infer HUD from all queue-4 draws.
enum class Mm3dUiSubsystem:uint32_t { Unknown,GameplayHud,Pause,Message,FileSelect,Notebook,Ocarina,Startup };
inline constexpr std::array<const char*,4> kMm3dGameplayHudLayouts{
    "Hud", "Hud2", "HudItemIconSet", "Telop"};
inline Mm3dUiSubsystem ClassifyMm3dLayout(std::string_view name) {
  for(auto key:kMm3dGameplayHudLayouts)
    if(name==key)return Mm3dUiSubsystem::GameplayHud;
  if(name=="MessageWindow")return Mm3dUiSubsystem::Message;
  if(name=="FileSelect"||name=="FilePlate"||name=="PopupWindow"||name=="UpPlateDetail")return Mm3dUiSubsystem::FileSelect;
  if(name=="MenuSchedule"||name=="MenuQuestlist"||name=="UpSchedule"||name=="UpQuestlist"||name=="UpEventMap")return Mm3dUiSubsystem::Notebook;
  if(name=="MenuOcarina"||name=="MenuMusicList")return Mm3dUiSubsystem::Ocarina;
  if(name=="MenuStartup"||name=="Throbber")return Mm3dUiSubsystem::Startup;
  for(auto key:{"UpCommonBG","CommonBottomBg","SubMenuTitle","BackGround","MenuItem","ItemIconButton","CursorItemCollect","MenuMask","MenuCollect","Map","MenuOption"})
    if(name==key)return Mm3dUiSubsystem::Pause;
  return Mm3dUiSubsystem::Unknown;
}
template<class M> Mm3dUiSubsystem ReadMm3dUiSubsystem(M& memory,uint32_t layout) {
  const auto pointer=[](uint32_t p,uint32_t size){return p>=0x100000&&!(p&3U)&&p<0x40000000&&size<=0x40000000-p;};
  uint32_t resource=0,name=0;
  if(!pointer(layout,8)||!memory.Read32(layout+4,&resource)||!pointer(resource,0x1C)||
     !memory.Read32(resource+0x18,&name)||name<0x100000||name>0x3FFFFFC0)return Mm3dUiSubsystem::Unknown;
  std::array<char,64> text{};
  for(uint32_t i=0;i<text.size();++i){
    uint8_t b=0;if(!memory.Read8(name+i,&b))return Mm3dUiSubsystem::Unknown;
    if(!b)return ClassifyMm3dLayout(std::string_view(text.data(),i));
    if(b<32||b>126)return Mm3dUiSubsystem::Unknown;text[i]=char(b);
  }
  return Mm3dUiSubsystem::Unknown;
}
inline constexpr uint32_t kMm3dUiTag=0x4D4D0000U;
inline bool IsMm3dGameplayHud(uint32_t sourcePc,uint32_t value) {
  return sourcePc==0x126294 && value==(kMm3dUiTag|uint32_t(Mm3dUiSubsystem::GameplayHud));
}
}
