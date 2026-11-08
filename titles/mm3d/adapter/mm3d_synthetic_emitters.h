#pragma once
#include "mm3d_local_light_sources.h"
#include "fast/renderer3ds/pica_light_binding.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <span>
#include <unordered_set>
#include <vector>

namespace Oot3dNativeGame {
// EUR-only evidence: DL/{player_light_proof,final_source_semantics,
// fire_arrow_wall_proof,final_native_xrefs}.txt. Native records (including
// player/stick and lantern carriers) are captured first; these readers never
// create a second source for an actor with an active registered point.
struct Mm3dSyntheticSource {
    uint16_t Actor;
    uint32_t Update, Destroy;
    std::array<std::array<uint32_t,2>,7> Guards;
};
inline constexpr std::array kMm3dSyntheticSources{
    Mm3dSyntheticSource{0xC,0x457874,0x3F9F04,{{
        {0x3F9E40,0xE5C41768},{0x3F9DE8,0xE5C40768},
        {0x3F9E94,0xE5C40768},{0x45764C,0xE5D02769},
        {0x45789C,0xE7D0000A},{0x4578A0,0xE3500064},{0x457BC8,0x000020B4}}}},
    // Parent arrow params 3..5 index EUR table624074:57/58/59. Only57 is fire.
    Mm3dSyntheticSource{0x57,0x44CF48,0x3F0B08,{{
        {0x624074,0x00580057},{0x44C8E4,0xE5B04128},
        {0x44C914,0xE1D611B2},{0x44CEEC,0xE5D50314},
        {0x58BDC4,0x0025DBB4},{0x44CD18,0x0025DFC4},{0x3F0AF4,0x0058BCEC}}}},
    Mm3dSyntheticSource{0xB5,0x495420,0x44D44C,{{
        {0x44D328,0xED840A17},{0x495434,0xE59021F8},
        {0x44D3B0,0xE5840268},{0x44D36C,0xED840A16}}}},
    Mm3dSyntheticSource{0xB7,0x47C71C,0x4253F4,{{
        {0x47C5EC,0xED900A89},{0x47C604,0xE594028C},
        {0x47C670,0xED940A87},{0x47C678,0xEDD40A8A}}}},
    Mm3dSyntheticSource{0xFA,0x483C88,0x435704,{{
        {0x483544,0xE1D602F2},{0x483594,0xEDD40A16},
        {0x4841F8,0xE2840FBF},{0x48420C,0xE1D511F6}}}},
    // 0x61 = En_Honotrap (gameplay_dangeon_keep, init 428270 branches on params
    // 0/3/4). Flame kinds 1/2: init installs update 4E2304 / draw 4E2234
    // (literals 42878C/428790, stored at 428740/428748); FlameGrow 2D23A8
    // (init literal 428784) grows scale.x(+0x58) to 0.004 (params 1, literal
    // 2D24B0 via vldreq at 2D23C0) or 0.0048. Analysis round5/honotrap_proof.out.
    Mm3dSyntheticSource{0x61,0x4E2304,0x428794,{{
        {0x428740,0xE5860140},{0x428748,0xE5860144},
        {0x42878C,0x004E2304},{0x428790,0x004E2234},
        {0x428784,0x002D23A8},{0x2D24B0,0x3B83126F},{0x2D23C0,0x0D9F0A3A}}}},
    // 0xB = En_Dodongo (object_dodongo, update 456A10, draw 456990). Breath
    // action 55868C (actionFunc +0x9D4, literal 5589BC, compared at 558744)
    // spawns its fire while the frame (+0x240 = skelAnime+0x48, read 558794)
    // is within 29.0 (5589C0) .. 43.0 (5589C4), at bodyPartsPos[0] (+0xA20,
    // read 5587F0). Analysis round5/dodongo_proof.out.
    Mm3dSyntheticSource{0xB,0x456A10,0x3F9010,{{
        {0x558740,0xE59F1274},{0x558744,0xE59509D4},
        {0x5589BC,0x0055868C},{0x5589C0,0x41E80000},
        {0x5589C4,0x422C0000},{0x558794,0xED950A90},{0x5587F0,0xED961A88}}}},
    // 0xFD = Obj_Fireshield (MM fire wall; keep, init 4A8618 scales x by
    // 0.0348 literal 4A88A0). Update 4CE284 stores alpha = rise(+0x258)*255
    // (vldr 4CE5B4, 255.0 at 4CE6A0, strb +0x266 at 4CE5EC); draw 4CE20C reads
    // it (4CE224). Analysis round5/fireshield_proof.out.
    Mm3dSyntheticSource{0xFD,0x4CE284,0x4A88C4,{{
        {0x4CE5B4,0xED940A96},{0x4CE5B8,0xED9F1A38},
        {0x4CE6A0,0x437F0000},{0x4CE5EC,0xE5C40266},
        {0x4CE224,0xE5D40266},{0x4A88A0,0x3D0E8A72},{0,0}}}},
    // 0x145 = En_Elforg (stray fairy; keep, size 0x690, draw 424D18). Its
    // sparkle spawner 47C080 (called from the init-installed actions) reads
    // area s16 +0x674 (literal 47C1C8, ldrsh 47C170, >=5 -> 0 at 47C174/178)
    // and the authored per-area env colours at 675708 (literal 47C1CC).
    // No LightContext record: a draw-only glow, lit in the fairy group.
    Mm3dSyntheticSource{0x145,0x424D48,0x3CE078,{{
        {0x47C158,0xE59F0068},{0x47C1C8,0x00000674},
        {0x47C170,0xE19000F4},{0x47C174,0xE3500005},
        {0x47C178,0x23A00000},{0x47C1CC,0x00675708},{0x675708,0xFF0096FF}}}},
    Mm3dSyntheticSource{0x10B,0x4B4BE8,0x474070,{{
        {0x47403C,0xEBFD1750},{0x474040,0xE584026C},
        {0x474000,0xE5840264},{0x4B4C6C,0xE594220C}}}},
};
namespace Mm3dEmitterDetail {
using Memory=oot3d::recomp::a32::MemoryBus;
inline bool F(Memory& m,uint32_t a,float& out) {
    uint32_t v=0;if(!m.Read32(a,&v))return false;
    out=std::bit_cast<float>(v);return std::isfinite(out);
}
inline bool V(Memory& m,uint32_t a,std::array<float,3>& out) {
    for(uint32_t i=0;i<3;++i)if(!F(m,a+i*4,out[i]) || std::abs(out[i])>1.0e7F)return false;
    return true;
}
inline bool FloatColour(Memory& m,uint32_t a,std::array<float,3>& out) {
    if(!V(m,a,out))return false;
    for(float& c:out){if(c<0 || c>255)return false;c/=255.F;}
    return true;
}
inline bool Proved(Memory& m,uint32_t actor,const Mm3dSyntheticSource& s) {
    uint32_t u=0,d=0;
    if(!m.Read32(actor+0x140,&u)||u!=s.Update||!m.Read32(actor+0x13C,&d)||d!=s.Destroy)return false;
    for(const auto& g:s.Guards)if(g[0]){uint32_t word=0;if(!m.Read32(g[0],&word)||word!=g[1])return false;}
    return true;
}
}
inline void CaptureMm3dSyntheticEmitters(uint32_t play,uint32_t actor,uint16_t id,
    oot3d::recomp::a32::MemoryBus& memory,bool activeNativePoint,bool nativeDrawObserved,
    std::vector<Fast::Renderer3ds::PicaScenePointLight>& lights) {
    using namespace Fast::Renderer3ds;using namespace Mm3dEmitterDetail;
    if(activeNativePoint || actor>UINT32_MAX-0x1000)return;
    const auto row=std::find_if(kMm3dSyntheticSources.begin(),kMm3dSyntheticSources.end(),
        [&](const auto& r){return r.Actor==id;});
    if(row==kMm3dSyntheticSources.end() || !Proved(memory,actor,*row))return;
    uint32_t draw=0;if(!memory.Read32(actor+0x144,&draw))return;
    std::array<float,3> position{},scale{},colour{1,.6F,.25F};
    if(!V(memory,actor+0x24,position)||!V(memory,actor+0x58,scale))return;
    float level=1,radius=300;uint32_t group=2;
    if(id==0x145) {
        // Stray fairy: visible while its draw callback is installed (enemy-
        // held/collider kinds have none), in its area's authored colour,
        // fading as the native collect shrink lowers scale below 0.01.
        int16_t area=0;std::array<uint8_t,3> rgb{};
        if(draw!=0x424D18 || !memory.Read16(actor+0x674,reinterpret_cast<uint16_t*>(&area)))return;
        const uint32_t index=area<0||area>=5?0U:uint32_t(area);
        for(uint32_t c=0;c<3;++c)if(!memory.Read8(0x675708U+index*4U+c,&rgb[c]))return;
        for(uint32_t c=0;c<3;++c)colour[c]=float(rgb[c])/255.F;
        level=std::clamp(scale[0]/.01F,0.F,1.F);radius=100;group=1;
    } else if(id==0xC) {
        uint8_t aura=0,invisible=0;
        if(draw!=0x457638 || !memory.Read8(actor+0x768,&aura)||aura!=1 ||
           !memory.Read8(actor+0x76A,&invisible) || (invisible && !nativeDrawObserved))return;
        if(invisible) {
            uint8_t lens=0;
            if(!memory.Read8(play+0x20B4,&lens) || lens!=100)return;
        }
        // EUR Update reads play+20B4 ==100 for invisible visibility. Previous
        // native draw proof alone must not survive a same-frame lens-off.
        // The native aura, not spawn params: extinguished/ice Keese give none.
    } else if(id==0x57) {
        uint32_t action=0,parent=0;uint16_t rawTimer=0,rawRadius=0;uint8_t alpha=0;
        if(draw!=0x44C8D0 || !memory.Read32(actor+0x30C,&action) ||
           (action!=0x58BCEC && action!=0x25DBB4 && action!=0x25DFC4) ||
           !memory.Read16(actor+0x312,&rawTimer)||rawTimer>=255 ||
           !memory.Read16(actor+0x310,&rawRadius)||!memory.Read8(actor+0x314,&alpha)||!alpha ||
           !memory.Read32(actor+0x128,&parent))return;
        if(action!=0x25DFC4) {
            uint32_t update=0;if(!parent || parent>UINT32_MAX-0x528 ||
                !memory.Read32(parent+0x140,&update)||!update)return;
        }
        uint8_t state=0;
        if(parent && parent<=UINT32_MAX-0x528 && memory.Read8(parent+0x511,&state) && !(state&2U)) {
            uint16_t parentId=0;
            if(!memory.Read16(parent,&parentId)||parentId!=0xF || !V(memory,parent+0x24,position))return;
        }
        level=float(alpha)/255.F;
        radius=250.F;
        if(action==0x58BCEC) {
            const float charge=std::clamp(float(std::bit_cast<int16_t>(rawRadius))/10.F,0.F,1.F);
            level*=charge;radius*=charge;
        } else if(action==0x25DFC4) {
            const float progress=(float(rawTimer)-8.F)/24.F;
            radius+=350.F*std::clamp(1.F-progress*progress,0.F,1.F);
        }
    } else if(id==0xB7) {
        float opacity=0,height=0;
        uint32_t model=0;
        if(draw!=0x47C5E4 || !F(memory,actor+0x224,opacity)||opacity<=0 ||
           !F(memory,actor+0x228,height)||height<=0 ||
           !memory.Read32(actor+0x28C,&model)||!model)return;
        level=std::clamp(opacity/255.F,0.F,1.F);radius=1000;
        position[1]+=40.F*level;
    } else if(id==0xFA) {
        uint16_t first=0;
        if(draw!=0x483518 || !(scale[0]>0) || !memory.Read16(actor+0x222,&first) || first>9 ||
           !FloatColour(memory,actor+0x2FC,colour))return;
        // Live native projectile/trail positions, never particles guessed from
        // pixels. Keep the title's fire/ice RGB; one light for the whole trail.
        std::array<float,3> sum{};float total=0;
        for(uint32_t i=first;i<10;++i) {
            std::array<float,3> p;const float size=std::max(0.F,scale[0]-float(i)*0.001F);
            if(size>0) {
                if(!V(memory,actor+0x22C+i*12,p))return;
                for(uint32_t c=0;c<3;++c)sum[c]+=p[c]*size;
                total+=size;
            }
        }
        if(!(total>0))return;
        for(uint32_t c=0;c<3;++c)position[c]=sum[c]/total;
        radius=400.F*std::clamp(scale[0]/.03F,.1F,2.F);
    } else if(id==0xB) {
        // OoT3D policy (En_Dodongo breathing fire): emits while the breath
        // frame is 29..43, fading by 10 per frame, radius 400. MM's native
        // fire spawns at the mouth body part, so the light is placed there.
        uint32_t action=0;float frame=0;
        if(draw!=0x456990 || !memory.Read32(actor+0x9D4,&action) || action!=0x55868C ||
           !F(memory,actor+0x240,frame) || !(frame>=29.F) || !(frame<=43.F) ||
           !V(memory,actor+0xA20,position))return;
        level=(255.F-10.F*std::trunc(frame-29.F))/255.F;radius=400;
    } else if(id==0xFD) {
        // OoT3D fire-wall policy (Bg_Hidan_Firewall): level follows the
        // wall's rise (here its native alpha), radius 1000*sqrt(level), raised
        // 40*level; MM's wall width (scale.x / 0.0348) scales the radius.
        uint8_t alpha=0;
        if(draw!=0x4CE20C || !memory.Read8(actor+0x266,&alpha) || !alpha || !(scale[0]>0))return;
        level=float(alpha)/255.F;
        radius=1000.F*std::sqrt(level)*std::clamp(scale[0]/.0348F,.3F,2.F);position[1]+=40.F*level;
    } else if(id==0x61) {
        // OoT3D policy (oot3d_native_a32_fire_emitters.cpp, En_Honotrap): the
        // flames (params 1, 2) grow from 0.0001 to 0.004 / 0.0048 and shrink
        // as they vanish; the eyes (0, 3) and the flame group (4) are not this.
        uint16_t params=0;
        if(draw!=0x4E2234 || !memory.Read16(actor+0x1C,&params) || (params!=1 && params!=2))return;
        const float grown=params==1?.004F:.0048F;
        level=std::clamp((scale[0]-.0001F)/(grown-.0001F),0.F,1.F);
        const float size=scale[0]/.004F;
        position[1]+=20.F*size;radius=500.F*size;
    } else {
        // A wall's native phase installs its draw callback; hidden phases do
        // not emit. The spout controller can delegate the visual to child2.
        if(id==0x10B && !draw) {
            uint32_t child=0,parent=0,update=0;uint16_t childId=0;
            if(!memory.Read32(actor+0x26C,&child)||!child||child>UINT32_MAX-0x200 ||
               !memory.Read16(child,&childId)||childId!=2 ||
               !memory.Read32(child+0x128,&parent)||parent!=actor ||
               !memory.Read32(child+0x140,&update)||!update ||
               !memory.Read32(child+0x144,&draw))return;
        }
        if(!draw || !(scale[1]>.005F))return;
        level=std::clamp((scale[1]-.005F)/.095F,0.F,1.F);
        radius=1000.F*std::sqrt(level);position[1]+=40.F*level;
    }
    if(!(level>0)||!(radius>0))return;
    PicaScenePointLight light;
    light.SourceIdentity=(uint64_t{0xB7}<<56)|(uint64_t{id}<<40)|(uint64_t{actor}<<8);
    light.PositionWorld=position;light.Radius=radius;light.Group=group;light.IndependentEmission=true;
    for(uint32_t c=0;c<3;++c)light.Color[c]=colour[c]*level;
    lights.push_back(light);
}

// A visible fairy whose registered source is dormant follows its authored
// glow. The caller already proved the exact first LightInfo/node/callback.
inline void CaptureMm3dVisibleFairy(uint32_t play,uint32_t actor,uint16_t params,
    oot3d::recomp::a32::MemoryBus& memory,std::vector<Fast::Renderer3ds::PicaScenePointLight>& lights) {
    using namespace Fast::Renderer3ds;using namespace Mm3dEmitterDetail;
    constexpr std::array<std::array<uint32_t,2>,4> guards{{
        {0x397F54,0xE5D405EE},{0x397FD4,0xED940A16},
        {0x39805C,0xEDD50A51},{0x3980D8,0xE2833F56}}};
    for(const auto& g:guards){uint32_t word=0;if(!memory.Read32(g[0],&word)||word!=g[1])return;}
    uint32_t draw=0,player=0;uint8_t flags=0;float size=0,glow=0,alpha=0;
    if(!memory.Read32(actor+0x144,&draw)||draw!=0x397F2C ||
       !memory.Read8(actor+0x5EE,&flags)||(flags&8U) ||
       !F(memory,actor+0x58,size)||!(size>0) || !F(memory,actor+0x544,glow)||!(glow>0) ||
       !F(memory,actor+0x564,alpha)||!(alpha>0))return;
    uint8_t mode=0;
    if(!memory.Read32(play+0x20DC,&player) || !player || player>UINT32_MAX-0x20C ||
       !memory.Read8(player+0x20B,&mode) || mode==20)return;
    PicaScenePointLight light;
    if(!V(memory,actor+0x24,light.PositionWorld)||!FloatColour(memory,actor+0x558,light.Color))return;
    const float gain=std::clamp(glow,0.F,1.F)*std::clamp(alpha/255.F,0.F,1.F);
    for(auto& c:light.Color)c*=gain;
    // Dormant native radius means there is no slot to replace. Independent
    // glow is positively proved above; retain the identity so any unexpected
    // live native slot clashes and fails closed in the shared ledger.
    light.SourceIdentity=actor+0x57CU;light.IndependentEmission=true;
    light.Radius=100;light.Group=params?1:0;lights.push_back(light);
}
} // namespace Oot3dNativeGame
