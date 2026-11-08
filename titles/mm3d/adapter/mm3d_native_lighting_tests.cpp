#include "mm3d_native_a32_lighting.h"
#include "mm3d_pica_composition.h"
#include "mm3d_title_contracts.h"
#include "mm3d_lighting_hook_sites.h"
#include "mm3d_local_light_sources.h"
#include "mm3d_room_emitters.h"
#include "mm3d_synthetic_emitters.h"
#include <cstdio>
#include "oot3d_native_a32_memory.h"
#include "fast/renderer3ds/pica_resolved_draw_stream.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace Oot3dNativeGame;
using namespace Fast::Renderer3ds;
namespace {
void Require(bool yes, const char* message) {
    if (!yes) { std::cerr << "mm3d_native_lighting_tests: " << message << '\n'; std::exit(1); }
}
struct Fixture {
    NativeA32Memory Memory;
    oot3d::recomp::a32::GuestState State{};
    static constexpr uint32_t Play = 0x01000000U, Actor = 0x02000000U, Owner = 0x02001000U,
        Context = 0x02002000U, Renderer = 0x02003000U, Model = 0x02004000U,
        Queue = 0x02005000U, Params = 0x02006004U, Nodes = 0x02007000U;
    Fixture() {
        for (auto [address, size] : {std::pair{0x00100000U, 0x00540000U},
             std::pair{0x00650000U, 0x001B0000U}, std::pair{Play, 0x20000U}, std::pair{Actor, 0x10000U}}) {
            std::string error;
            Require(Memory.MapRegion({"mm3d-lighting-test", address, size, true, false, {}}, &error), "map fixture");
        }
        for (const auto& hook : kMm3dLightingHookSites)
            for (uint32_t i = 0; i < 4; ++i) W32(hook.Pc + 4U * i, hook.Words[i]);
        for (const auto& guard : kMm3dRoomEmitterGuards) W32(guard[0], guard[1]);
        W8(Play + 0xC2C4U, 0xFF);
        W32(kMm3dCurrentViewPointer, Play + kMm3dPlayViewOffset);
        W8(Play + 0x13DU, 4); W32(Play + 0x4A8U, Play); W16(Play + 0x148U, 0x6F); W32(Play + 0x138U, 1);
        W8(Play + 0x87FFU, 0xFF); W32(Play + 0x8824U, std::bit_cast<uint32_t>(1.0F));
        W16(0x7751F8U, 0x8000U);
        const float rows[] = {1,0,0,-10,0,1,0,-20,0,0,1,-30};
        for (uint32_t i = 0; i < 12; ++i) F32(Play + kMm3dPlayViewOffset + 0x17CU + i * 4U, rows[i]);
        W32(Renderer + 0x10U, Context); W8(Context + 0x6CU, 15);
        W32(Model, 0x0063BED4U); W32(Model + 0xA0U, Renderer);
        W16(Actor, 0); W8(Actor + 2U, 2); W32(Actor + 0x140U, 0x100100U); W32(Actor + 0x144U, 0x1F8E28U);
    }
    void W8(uint32_t p, uint8_t v) { Require(Memory.Write8(p,v), "write8 fixture"); }
    void W16(uint32_t p, uint16_t v) { Require(Memory.Write16(p,v), "write16 fixture"); }
    void W32(uint32_t p, uint32_t v) { Require(Memory.Write32(p,v), "write32 fixture"); }
    void F32(uint32_t p, float v) { W32(p,std::bit_cast<uint32_t>(v)); }
    void Point(uint32_t params, int16_t radius) {
        W8(params - 4U, 2); F32(params, 10); F32(params + 4U, 20); F32(params + 8U, 30);
        W8(params + 12U, 255); W8(params + 13U, 128); W8(params + 14U, 64);
        W8(params + 16U, 12); W16(params + 18U, std::bit_cast<uint16_t>(radius));
    }
    void Direction(uint32_t info, int8_t y, int8_t z, uint8_t color) {
        W8(info, 1); W8(info + 4U, 0); W8(info + 5U, std::bit_cast<uint8_t>(y)); W8(info + 6U, std::bit_cast<uint8_t>(z));
        for (uint32_t i=0;i<3;++i) W8(info + 7U + i, color);
    }
    void Link(uint32_t node, uint32_t info, uint32_t next) { W32(node,info); W32(node+8U,next); }
};

void TestAcceptedAndSelectedSources() {
    Fixture f; Mm3dNativeA32LightingProbe probe;
    auto& s=f.State;
    f.Point(f.Params,250);
    // An entry hit is not acceptance.
    s.r[0]=f.Owner; s.r[1]=f.Params; s.r[3]=f.Owner+12U;
    probe.ObserveBlockEntry(0x406458U,s,f.Memory);
    s.r[0]=f.Owner;s.r[1]=f.Context;
    probe.ObserveBlockEntry(0x1EEAA8U,s,f.Memory);
    s.r[0]=f.Context+0x70U;s.r[1]=f.Owner+12U;s.r[5]=0;s.r[8]=f.Context;
    probe.ObserveBlockEntry(0x1EED64U,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[0].SourceIdentity==0,"rejected bind became a source");
    s.r[0]=f.Owner;s.r[1]=f.Params;s.r[3]=f.Owner+12U;
    probe.ObserveBlockEntry(0x40650CU,s,f.Memory);
    f.W8(f.Owner+12U,1); f.W8(f.Owner+13U,128); f.W8(f.Owner+14U,64); f.W8(f.Owner+15U,32);
    for (uint32_t slot=0;slot<4;++slot) {
        s.r[0]=f.Context+0x70U+slot*0x118U;s.r[1]=f.Owner+12U;s.r[5]=slot;s.r[8]=f.Context;
        probe.ObserveBlockEntry(0x1EED64U,s,f.Memory);
    }
    const auto captured=probe.CaptureRenderer(f.Renderer,f.Memory);
    Require(captured.Sources[3].SourceIdentity==f.Params && captured.Sources[3].Radius==250,"fourth native slot lost");
    Require(captured.Sources[0].UnattenuatedColor[0]==1.0F && captured.Sources[0].ReferenceColor[0]<0.51F,"source/reference colors conflated");
    PicaResolvedDrawRecord replay; replay.NativeLightSources=captured.Sources;
    Require(replay.NativeLightSources[3].SourceIdentity==f.Params,"resolved draw lost fourth slot");
    f.W32(f.Renderer+0x100U+0x10U,f.Context+0x600U);
    Require(probe.CaptureRenderer(f.Renderer+0x100U,f.Memory).Sources[0].SourceIdentity==0,"unobserved context invented a source");
    f.F32(f.Params,99);
    Require(captured.Sources[0].PositionWorld[0]==10,"snapshot read live source");
    s.r[0]=f.Context;s.r[1]=3;
    probe.ObserveBlockEntry(0x1FD5E8U,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[3].SourceIdentity==0,"disabled slot retained source");
    s.r[0]=f.Context+0x70U;s.r[14]=0x111CC0U;
    probe.ObserveBlockEntry(0x1F2294U,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[0].SourceIdentity==0,"unrelated color writer retained source");
    f.Point(f.Params,-1); s.r[0]=f.Owner;s.r[1]=f.Params;s.r[3]=f.Owner+12U;
    probe.ObserveBlockEntry(0x40650CU,s,f.Memory);
    s.r[0]=f.Context+0x70U;s.r[1]=f.Owner+12U;s.r[5]=0;s.r[8]=f.Context;
    probe.ObserveBlockEntry(0x1EED64U,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[0].Radius==0,"negative radius converted to positive");
    probe.ObserveBlockEntry(0x15E9ECU,s,f.Memory);
    Require(probe.CaptureRenderer(f.Renderer,f.Memory).Sources[1].SourceIdentity==0,"generation reuse retained native source");
}

void TestLocalSourceGuards() {
    Fixture f;
    for (const auto& source : kMm3dLocalLightSources) {
        f.W16(f.Actor,source.Actor); f.W8(f.Actor+2,source.Category);
        f.W32(f.Actor+0x140,source.Update); f.W32(f.Actor+0x13C,source.Destroy);
        for (const auto& guard : source.Guards) f.W32(guard[0],guard[1]);
        Require(Mm3dLocalLightSourceProved(f.Memory,f.Actor,source.Info,source.Node),"proved source rejected");
        for (const auto& guard : source.Guards) {
            f.W32(guard[0],guard[1]^1U);
            Require(!Mm3dLocalLightSourceProved(f.Memory,f.Actor,source.Info,source.Node),"corrupt source guard accepted");
            f.W32(guard[0],guard[1]);
        }
        f.W32(f.Actor+0x140,source.Update+4);
        Require(!Mm3dLocalLightSourceProved(f.Memory,f.Actor,source.Info,source.Node),"reused/other actor accepted");
        f.W32(f.Actor+0x140,source.Update);
        Require(!Mm3dLocalLightSourceProved(f.Memory,f.Actor,source.Info+4,source.Node),"wrong source offset accepted");
    }
}

void TestRoundTwoSourcesConverted() {
    // F02a: Dno, Boss_Hakugin and Ot were native-retained; they must now be
    // selected as converted sources (fails on the 18-row table).
    for (const uint16_t id : {uint16_t(0x117),uint16_t(0x16E),uint16_t(0x194),uint16_t(0x15F),uint16_t(0x197)}) {
        bool present = false;
        for (const auto& source : kMm3dLocalLightSources) present |= source.Actor == id;
        Require(present,"round-2 native light source not converted");
    }
}

void TestEachProvenSourcePublication() {
    for (const auto& source : kMm3dLocalLightSources) {
        Fixture f; Mm3dNativeA32LightingProbe probe;
        f.W16(f.Actor,source.Actor); f.W8(f.Actor+2,source.Category);
        f.W16(f.Actor+0x1C,1);
        f.W32(f.Actor+0x140,source.Update); f.W32(f.Actor+0x13C,source.Destroy);
        for (const auto& guard : source.Guards) f.W32(guard[0],guard[1]);
        f.W32(f.Play+0x20C0+source.Category*12,1);
        f.W32(f.Play+0x20C4+source.Category*12,f.Actor);
        f.W32(f.Play+0xA88,f.Nodes); f.Link(f.Nodes,f.Actor+source.Info,0);
        f.W32(f.Actor+source.Node,f.Nodes); f.Point(f.Actor+source.Info+4,250);
        probe.CaptureSceneLights(f.Play,f.Memory);
        const auto binding=probe.CaptureRenderer(0,f.Memory);
        Require(binding.ScenePoints && binding.ScenePoints->Count==1,"proved source not selected exactly once");
        const auto& light=binding.ScenePoints->Lights[0];
        Require(light.SourceIdentity==f.Actor+source.Info+4 && light.ReplacesNativeLocal && light.Radius==250,
                "source publication changed native identity/radius");
        const float gain=source.Actor==0x10?1.0F:0.15F;
        Require(std::abs(light.Color[0]-gain)<1e-6F && std::abs(light.Color[1]-gain*128.0F/255.0F)<1e-6F,
                "selector lost native colour or added-light fade policy");
        f.W32(f.Play+0x138,2); f.W32(f.Actor+source.Node,0);
        probe.CaptureSceneLights(f.Play,f.Memory);
        Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints,"unowned native source remained selected");
    }
}

void TestRoomEmitterPublication() {
    Fixture f; Mm3dNativeA32LightingProbe probe;
    const uint32_t segment=f.Actor+0x8000;
    std::array<uint8_t,40> bytes{};
    bytes[0]='Z';bytes[1]='S';bytes[2]='I';bytes[3]=9;
    bytes[16]=1;bytes[17]=2;bytes[20]=0x48;
    bytes[24]=0xA;bytes[28]=0x60;bytes[32]=0x14;
    for(uint32_t i=0;i<bytes.size();++i)f.W8(segment+i,bytes[i]);
    f.W8(f.Play+0xC260,0);f.W32(f.Play+0xC270,segment);
    const auto hash=Mm3dRoomFingerprint(bytes);
    Require(hash.has_value(),"ROM room fingerprint rejected");
    // Exactly the native loader's observed pointer/relocation changes.
    f.W16(segment+18,1);f.W32(segment+20,0x09001234);
    f.W16(segment+26,1);f.W32(segment+28,0x09005678);
    const auto room=CaptureMm3dEmitterRoom(f.Play,f.Memory);
    Require(room && room->Fingerprint==*hash,"runtime relocation changed room identity");
    f.W8(f.Play+0xC2C4,1); f.W32(f.Play+0xC2D4,segment);
    Require(CaptureMm3dEmitterRooms(f.Play,f.Memory).size()==2,"coexisting previous room lost");
    f.W32(f.Play+0xC330,1);
    const auto pending=CaptureMm3dEmitterRooms(f.Play,f.Memory);
    Require(pending.size()==1 && pending[0].Number==1,"pending current inherited old segment");
    f.W32(f.Play+0xC330,0); f.W8(f.Play+0xC2C4,0xFF); f.W32(f.Play+0xC2D4,0);
    for (const auto& guard : kMm3dRoomEmitterGuards) {
        f.W32(guard[0],guard[1]^1U);
        Require(CaptureMm3dEmitterRooms(f.Play,f.Memory).empty(),"corrupt room writer guard admitted");
        f.W32(guard[0],guard[1]);
    }
    char fingerprint[17];std::snprintf(fingerprint,sizeof(fingerprint),"%016llx",static_cast<unsigned long long>(*hash));
    const std::string json=std::string(R"({"schema":"triaevum.scene-lights","version":1,"title":"mm3d","rooms":[{"scene":111,"room":0,"fingerprint":")")+
        fingerprint+R"(","emitters":[{"position":[10,20,30],"colour":[1,0.8,0.5],"radius":500,"gain":0.45,"category":"window-lamp","independentEmission":true,"surfaces":["0000000000001234"]}]}]})";
    const auto catalog=PicaEmitterCatalog::Parse(json);
    Require(catalog && catalog->MatchesTitle("mm3d"),"MM catalog rejected");
    std::vector<PicaScenePointLight> duplicateLights;
    const std::array duplicateRooms{*room,*room};
    CapturePicaRoomEmitters(*catalog,111,duplicateRooms,duplicateLights,nullptr);
    Require(duplicateLights.size()==1,"duplicate current/previous room emitted twice");
    auto conflicts=duplicateRooms; conflicts[1].Fingerprint^=1;
    duplicateLights.clear();CapturePicaRoomEmitters(*catalog,111,conflicts,duplicateLights,nullptr);
    Require(duplicateLights.empty(),"conflicting room identity admitted");
    probe.SetEmitterCatalog(catalog);probe.CaptureSceneLights(f.Play,f.Memory);
    auto binding=probe.CaptureRenderer(0,f.Memory);
    Require(binding.ScenePoints && binding.ScenePoints->Count==1 && binding.ScenePoints->Lights[0].Group==3 &&
            binding.ScenePoints->Lights[0].CastsShadow && binding.ScenePoints->Lights[0].IndependentEmission &&
            !binding.ScenePoints->Lights[0].ReplacesNativeLocal,
            "room emitter did not use shared supplemental policy");
    auto& emission=PicaSurfaceEmissionRegistry::Instance();emission.BeginFrame();
    Require(emission.Follows(0x1234),"MM S14 follow set not published");
    emission.Observe(0x1234,0);emission.BeginFrame();
    f.W32(f.Play+0x138,2);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints,"native-off fake-emissive surface still lit");
    emission.Observe(0x1234,0); // in-flight observation from the old room
    emission.Follow({}); emission.Follow(std::array<uint64_t,1>{0x1234});
    Require(emission.Strength(0x1234)==1,"same-frame reload inherited old fade");
    emission.BeginFrame();
    Require(emission.Strength(0x1234)==1,"in-flight old observation survived reload");
    emission.Observe(0x1234,.25F);emission.BeginFrame();emission.BeginFrame();
    Require(emission.Strength(0x1234)==.25F,"temporarily unseen loaded surface lost strength");
    // Same-frame scene reuse must invalidate the global follow history.
    f.W16(f.Play+0x148,0x61);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(emission.Strength(0x1234)==1,"scene change retained surface strength");
    f.W8(f.Play+0xC260,0xFF);
    Require(!CaptureMm3dEmitterRoom(f.Play,f.Memory),"unloaded room accepted");
    f.W8(f.Play+0xC260,0);f.W8(segment,'X');
    Require(!CaptureMm3dEmitterRoom(f.Play,f.Memory),"unknown room ABI accepted");
    probe.Reset();emission.BeginFrame();
    Require(!emission.Follows(0x1234),"reset retained MM S14 follow set");
}

void TestSyntheticFireAndFairies() {
    for(const auto& row:kMm3dSyntheticSources) {
        Fixture f;std::vector<PicaScenePointLight> lights;
        f.W16(f.Actor,row.Actor);f.W32(f.Actor+0x140,row.Update);f.W32(f.Actor+0x13C,row.Destroy);
        for(const auto& g:row.Guards)if(g[0])f.W32(g[0],g[1]);
        f.F32(f.Actor+0x24,10);f.F32(f.Actor+0x28,20);f.F32(f.Actor+0x2C,30);
        for(uint32_t n=0;n<3;++n)f.F32(f.Actor+0x58+n*4,.03F);
        uint32_t draw=0x100100;
        if(row.Actor==0xC){draw=0x457638;f.W8(f.Actor+0x768,1);}
        if(row.Actor==0x57){draw=0x44C8D0;f.W32(f.Actor+0x30C,0x25DFC4);f.W8(f.Actor+0x314,255);}
        if(row.Actor==0xB7){draw=0x47C5E4;f.F32(f.Actor+0x224,255);f.F32(f.Actor+0x228,1);f.W32(f.Actor+0x28C,f.Model);}
        if(row.Actor==0xFA){draw=0x483518;for(uint32_t n=0;n<3;++n)f.F32(f.Actor+0x2FC+n*4,255);
            for(uint32_t i=0;i<10;++i)for(uint32_t n=0;n<3;++n)f.F32(f.Actor+0x22C+i*12+n*4,float(10+n*10));}
        f.W32(f.Actor+0x144,draw);
        CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
        Require(lights.size()==1 && lights[0].IndependentEmission && !lights[0].ReplacesNativeLocal &&
                lights[0].Group==2 && lights[0].Radius>0,"proved native fire phase not published");
        lights.clear();CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,true,false,lights);
        Require(lights.empty(),"registered source gained a synthetic duplicate");
        for(const auto& g:row.Guards)if(g[0]){f.W32(g[0],g[1]^1U);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"corrupt fire phase guard accepted");f.W32(g[0],g[1]);}
        f.W32(f.Actor+0x144,0);CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
        Require(lights.empty(),"hidden fire phase emitted");
        if(row.Actor==0x57) {
            f.W32(f.Actor+0x144,draw);f.W32(f.Actor+0x30C,0x58BCEC);
            f.W32(f.Actor+0x128,f.Owner);f.W16(f.Owner,0xF);f.W32(f.Owner+0x140,1);
            f.W16(f.Actor+0x310,0);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"uncharged arrow emitted");
            f.W16(f.Actor+0x310,5);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.size()==1 && lights[0].Radius==125,"half charge lost OoT policy");
            lights.clear();f.W32(f.Actor+0x30C,0xBAD);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"unknown fire-arrow action emitted");
        }
        if(row.Actor==0xC){f.W32(f.Actor+0x144,draw);f.W8(f.Actor+0x768,2);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"ice/extinguished Keese treated as fire");
            f.W8(f.Actor+0x768,1);f.W8(f.Actor+0x76A,1);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,false,lights);
            Require(lights.empty(),"invisible Keese bypassed native draw/lens gate");
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,true,lights);
            Require(lights.empty(),"previous draw survived current lens-off");
            f.W8(f.Play+0x20B4,100);
            CaptureMm3dSyntheticEmitters(f.Play,f.Actor,row.Actor,f.Memory,false,true,lights);
            Require(lights.size()==1,"native-visible invisible Keese rejected");}
    }
    Fixture f;std::vector<PicaScenePointLight> lights;
    for(auto g:std::array{std::pair{0x397F54U,0xE5D405EEU},std::pair{0x397FD4U,0xED940A16U},
                         std::pair{0x39805CU,0xEDD50A51U},std::pair{0x3980D8U,0xE2833F56U}})f.W32(g.first,g.second);
    f.W32(f.Play+0x20DC,f.Owner);
    f.W32(f.Actor+0x144,0x397F2C);f.F32(f.Actor+0x58,.008F);f.F32(f.Actor+0x544,1);f.F32(f.Actor+0x564,255);
    for(uint32_t n=0;n<3;++n)f.F32(f.Actor+0x558+n*4,float(255-n*20));
    CaptureMm3dVisibleFairy(f.Play,f.Actor,1,f.Memory,lights);
    Require(lights.size()==1 && lights[0].Group==1 && lights[0].SourceIdentity==f.Actor+0x57C &&
            lights[0].IndependentEmission && !lights[0].ReplacesNativeLocal,"visible dormant fairy lost native identity/proof");
    lights.clear();f.W8(f.Actor+0x5EE,8);CaptureMm3dVisibleFairy(f.Play,f.Actor,1,f.Memory,lights);
    Require(lights.empty(),"hidden fairy emitted");f.W8(f.Actor+0x5EE,0);f.F32(f.Actor+0x544,0);
    CaptureMm3dVisibleFairy(f.Play,f.Actor,1,f.Memory,lights);Require(lights.empty(),"zero glow fairy emitted");
}

void TestSceneSnapshots() {
    TestSyntheticFireAndFairies();
    TestLocalSourceGuards();
    TestRoundTwoSourcesConverted();
    TestEachProvenSourcePublication();
    TestRoomEmitterPublication();
    Fixture f; Mm3dNativeA32LightingProbe probe;
    probe.SetSupplementalPublication(true, true);
    const uint32_t sun=f.Play+0x86D0U,moon=f.Play+0x86E8U,torch=f.Actor+0x2B0U;
    f.Direction(sun,120,20,220);f.Direction(moon,-120,-20,20);
    f.W32(f.Play+0xA88U,f.Nodes);f.Link(f.Nodes,moon,f.Nodes+12U);f.Link(f.Nodes+12U,sun,f.Nodes+24U);
    f.Link(f.Nodes+24U,torch,0); f.Point(torch+4U,250);
    f.W16(f.Actor,0x2E);f.W8(f.Actor+2U,6);f.W32(f.Actor+0x2ACU,f.Nodes+24U);
    f.W32(f.Actor+0x140U,0x4AA8D4); f.W32(f.Actor+0x13CU,0x467A08);
    for (const auto& source : kMm3dLocalLightSources)
        for (const auto& guard : source.Guards) f.W32(guard[0],guard[1]);
    f.W32(f.Play+0x20C0U+6U*12U,1);f.W32(f.Play+0x20C4U+6U*12U,f.Actor);
    probe.CaptureSceneLights(f.Play,f.Memory);
    const auto first=probe.CaptureRenderer(0,f.Memory);
    Require(first.SceneDirectionals && first.SceneDirectionals->Count==2,"linked environment not captured");
    Require(first.SceneDirectionals->Lights[0].SourceIdentity==sun+4U,"source identity is not params");
    Require(first.SceneDirectionals->Lights[0].SkyLight && first.SceneDirectionals->ShadowCasterIdentity==sun+4U,"clock/caster policy missing");
    Require(first.ScenePoints && first.ScenePoints->Count==1 && first.ScenePoints->Lights[0].Group==2 &&
            first.ScenePoints->Lights[0].ReplacesNativeLocal,"native torch duplicated or source proof missing");
    probe.SetSupplementalPublication(false,false);
    Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints && !probe.CaptureRenderer(0,f.Memory).Water,"review gate leaked supplemental payload");
    probe.SetSupplementalPublication(true,true);
    Require(first.WorldToView && (*first.WorldToView)[12]==-10,"view transpose wrong");
    probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.CaptureRenderer(0,f.Memory).SceneDirectionals==first.SceneDirectionals,"same guest frame advanced continuity");
    f.W16(f.Play+0x148U,0x61);
    Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints && !probe.CaptureRenderer(0,f.Memory).SceneDirectionals,
            "same-frame scene change reused a stale source/catalog snapshot");
    probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.CaptureRenderer(0,f.Memory).ScenePoints!=nullptr,"same-frame new scene was not captured");
    f.W16(f.Play+0x148U,0x6F);probe.CaptureSceneLights(f.Play,f.Memory);
    f.W32(f.Play+0x138U,2);f.Point(torch+4U,-1);probe.CaptureSceneLights(f.Play,f.Memory);
    const auto second=probe.CaptureRenderer(0,f.Memory);
    Require(!second.ScenePoints && first.ScenePoints->Lights[0].Radius==250,"dormant source selected or immutable snapshot lost");
    f.W32(f.Play+0x138U,3);f.Direction(sun,-120,-20,220);f.Direction(moon,120,20,20);
    probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.CaptureRenderer(0,f.Memory).SceneDirectionals->Lights[0].SkyLight,"nonclock direction was overwritten");
    f.W32(f.Play+0x138U,4);f.W32(f.Play+0xA88U,0);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.CaptureRenderer(0,f.Memory).ScenePoints && !probe.CaptureRenderer(0,f.Memory).SceneDirectionals,"unlinked lights persisted");
    f.W32(kMm3dCurrentViewPointer,0);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.CaptureRenderer(0,f.Memory).WorldToView,"no-view captured camera");
    probe.Reset();
}

void TestAmbiguousQueueOwnership() {
    Fixture f; Mm3dNativePicaCompositionTracker tracker;auto& s=f.State;
    Require(tracker.VerifyHooks(f.Memory),"verify ambiguous fixture");
    const auto observe=[&](uint32_t pc){tracker.ObserveBlockEntry(pc,s,f.Memory);};
    s.r[0]=f.Play;observe(0x15E9ECU);
    s.r[0]=f.Actor;s.r[2]=0x1F8E28U;observe(0x182C44U);
    s.r[0]=f.Queue;s.r[1]=f.Model;s.r[14]=0x20A8F8U;f.W32(f.Queue+4U,0);observe(0x224104U);
    f.W32(f.Queue+8U,f.Model);f.W32(f.Queue+4U,1);observe(0x182C48U);
    s.r[0]=f.Model;s.r[14]=0x21EFACU;observe(0x20A898U);
    s.r[0]=f.Queue;s.r[1]=f.Model;s.r[14]=0x20A8F8U;observe(0x224104U);
    f.W32(f.Queue+12U,f.Model);f.W32(f.Queue+4U,2);observe(0x223EDCU);
    f.W32(kMm3dCommandListCursorAddress,0x14000000U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
    s.r[0]=f.Model;s.r[4]=f.Queue;s.r[5]=f.Queue+8U;observe(0x5EF0CCU);
    f.W32(kMm3dCommandListCursorAddress,0x14000020U);observe(0x5EF0D0U);observe(0x11EF3CU);
    std::vector<Oot3dPicaCommandListCompositionSpan> spans;tracker.TakeCommandListCompositionSpans(0,0,spans);
    Require(spans.size()==1 && !spans[0].Attribution.NativeActorGeometry && !spans[0].Attribution.NativeProjectedShadow,
            "ambiguous same-model permutation transferred an owner");
}

void TestNestedAndInvalidConsumer() {
    for (uint32_t scenario : {0U,1U,2U}) {
        const bool invalidRange = scenario != 0U;
        Fixture f; Mm3dNativePicaCompositionTracker tracker;auto& s=f.State;
        Require(tracker.VerifyHooks(f.Memory),"verify nested fixture");
        const auto observe=[&](uint32_t pc){tracker.ObserveBlockEntry(pc,s,f.Memory);};
        s.r[0]=f.Play;observe(0x15E9ECU);
        s.r[0]=f.Actor;s.r[2]=0x1F8E28U;observe(0x182C44U);
        s.r[0]=f.Queue;s.r[1]=f.Model;s.r[14]=0x20A8F8U;f.W32(f.Queue+4U,0);observe(0x224104U);
        f.W32(f.Queue+8U,f.Model);f.W32(f.Queue+4U,1);observe(0x182C48U);
        f.W32(kMm3dCommandListCursorAddress,0x14000000U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
        if (scenario == 2U) f.W32(kMm3dCommandListCursorAddress,0x13FFFFF8U); // invalid child begin
        s.r[0]=f.Model;s.r[4]=f.Queue;s.r[5]=f.Queue+8U;observe(0x5EF0CCU);
        if (!invalidRange) observe(0x5EF0CCU); // unknown nested consumer
        f.W32(kMm3dCommandListCursorAddress,invalidRange?0x13FFFFF8U:0x14000010U);observe(0x5EF0D0U);
        f.W32(kMm3dCommandListCursorAddress,0x14000020U);observe(0x11EF3CU);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;tracker.TakeCommandListCompositionSpans(0,0,spans);
        if (invalidRange) Require(spans.empty() && tracker.Stats().InvalidRanges!=0,"invalid range became a tail prefix");
        else Require(spans.size()==1 && !spans[0].Attribution.NativeActorGeometry && tracker.Stats().NestedScopeEntries!=0,
                     "nested commands inherited outer actor");
    }
}

void TestClockCutsAndDayReset() {
    Fixture f; Mm3dNativeA32LightingProbe probe;
    const uint32_t sun=f.Play+0x86D0,moon=f.Play+0x86E8;
    f.W32(f.Play+0xA88,f.Nodes);f.Link(f.Nodes,sun,f.Nodes+12);f.Link(f.Nodes+12,moon,0);
    f.W32(0x7751EC,3);
    uint32_t frame=0;uint16_t clock=0;
    const auto capture=[&](uint16_t time) {
        clock=time; f.W16(0x7751F8,time);f.W32(f.Play+0x138,++frame);
        const double angle=double(std::bit_cast<int16_t>(uint16_t(time-0x8000)))*6.283185307179586/65536.0;
        const std::array<int,3> direction{int(-std::sin(angle)*120),int(std::cos(angle)*120),int(std::cos(angle)*20)};
        f.Direction(sun,0,0,uint8_t(128+direction[1]));
        f.Direction(moon,0,0,uint8_t(128-direction[1]));
        for (unsigned a=0;a<3;++a) {
            f.W8(sun+4+a,std::bit_cast<uint8_t>(int8_t(direction[a])));
            f.W8(moon+4+a,std::bit_cast<uint8_t>(int8_t(-direction[a])));
        }
        probe.CaptureSceneLights(f.Play,f.Memory);
        const auto lights=probe.CaptureRenderer(0,f.Memory).SceneDirectionals;
        Require(lights && lights->Count==2,"clock fixture lost directionals");
        Require(std::isfinite(lights->ShadowStrength) && lights->ShadowStrength>=0 && lights->ShadowStrength<=1,
                "clock/horizon shadow strength invalid");
        for (uint32_t i=0;i<lights->Count;++i)
            for (float component:lights->Lights[i].Color)
                Require(std::isfinite(component) && component>=0 && component<=1,"clock colour out of range");
        const auto* caster=FindPicaSceneMainDirectional(*lights);
        if (caster && caster->SkyLight && lights->ShadowStrength>0) {
            const auto direction=PicaSceneDirectionalDirection(*caster);
            Require(direction && PicaSceneElevationSine(*direction,lights->WorldUp)>0,
                    "clock fixture casts below horizon");
        }
        return lights;
    };
    const auto start=capture(0);const auto epoch=start->ContinuityEpoch;
    for (uint32_t tick=1;tick<=1024;++tick) {
        const auto lights=capture(uint16_t(tick*64));
        Require(lights->ContinuityEpoch==epoch && lights->Lights[0].SkyLight,
                "ordinary clock wrap/horizon broke continuity");
    }
    const auto song=capture(0x8000);
    Require(song->ContinuityEpoch>epoch,"Double Time direction cut did not start an epoch");
    const auto beforeReset=song->ContinuityEpoch;
    f.W32(0x7751EC,1);const auto reset=capture(clock);
    Require(reset->ContinuityEpoch>beforeReset,"Song of Time day rollback inherited old epoch");
    const auto beforeRestore=reset->ContinuityEpoch;frame=0;
    Require(capture(clock)->ContinuityEpoch>beforeRestore,"save restore inherited old epoch");
    f.W8(f.Play+0x8704,1);const auto fixed=capture(clock);
    Require(!fixed->Lights[0].SkyLight,"fixed-time entry acquired clock sky ownership");
    f.W16(f.Play+0x148,0x61);const auto scene=capture(clock);
    Require(scene->ContinuityEpoch>fixed->ContinuityEpoch,"scene reuse inherited old epoch");
}

void TestWaterOperandEligibility() {
    Require(PicaNativeFragmentOperandWaterEligible(true,0U,false,false,false),"null/empty-water legacy route changed");
    Require(PicaNativeFragmentOperandWaterEligible(true,2U,true,false,false),"dry geometry in a water scene lost operand");
    Require(PicaNativeFragmentOperandWaterEligible(true,2U,true,true,false),"mixed mesh lost its dry-fragment operand");
    Require(!PicaNativeFragmentOperandWaterEligible(true,kMaximumPicaSurfaceWaterBodies+1U,true,true,false),"truncated water coverage gained operand");
    Require(PicaNativeFragmentOperandWaterEligible(true,kMaximumPicaSceneWaterBodies,true,false,false),"proven dry geometry was excluded by scene count");
    Require(!PicaNativeFragmentOperandWaterEligible(true,2U,false,false,false),"unknown bounds gained operand");
    Require(!PicaNativeFragmentOperandWaterEligible(true,2U,true,false,true),"whole-water geometry gained operand");
    Require(!PicaNativeFragmentOperandWaterEligible(true,0U,true,false,false),"legacy bounds exclusion changed");
    Require(!PicaNativeFragmentOperandWaterEligible(true,kMaximumPicaSceneWaterBodies+1U,true,false,false),"oversized water metadata gained operand");
    for (uint32_t bits=0;bits<16;++bits)
        Require(!PicaNativeFragmentOperandWaterEligible(false,(bits&1)?2U:0U,(bits&2)!=0,(bits&4)!=0,(bits&8)!=0),
                "OoT capability-false route changed");
    Require(!PicaNativeFragmentOperandWaterEligible(false,kMaximumPicaSceneWaterBodies+1U,true,false,false),"OoT oversized-water route changed");
}

void TestWaterCountAndHeight() {
    Fixture f; const uint32_t header=f.Actor+0x8000U,boxes=header+0x100U;
    f.W32(f.Play+0xAB0U,header);f.W16(header+0x14U,99);f.W16(header+0x16U,1);f.W32(header+0x28U,boxes);
    const int16_t fields[]={-924,-20,-1000,160,520};
    for(uint32_t i=0;i<5;++i)f.W16(boxes+2U*i,std::bit_cast<uint16_t>(fields[i]));f.W32(boxes+12U,257);
    const auto first=CaptureMm3dWaterBodies(f.Play,f.Memory);
    Require(first.Count==1 && first.Bodies[0].SurfaceY==-20 && first.Bodies[0].MaximumXZ[0]==-764,"MM water ABI wrong");
    f.W16(boxes+2U,30);
    Require(CaptureMm3dWaterBodies(f.Play,f.Memory).Bodies[0].SurfaceY==30 && first.Bodies[0].SurfaceY==-20,"live height/snapshot wrong");
    f.W16(header+0x16U,0);
    Require(CaptureMm3dWaterBodies(f.Play,f.Memory).Count==0,"zero-count dereferenced box");
}

void TestDeferredQueueAndShadowScopes() {
    Fixture f; Mm3dNativePicaCompositionTracker tracker; auto& s=f.State;
    Require(tracker.VerifyHooks(f.Memory),"valid signatures rejected");
    const auto observe=[&](uint32_t pc){return tracker.ObserveBlockEntry(pc,s,f.Memory);};
    const auto cursor=[&](uint32_t p){f.W32(kMm3dCommandListCursorAddress,p);};
    s.r[0]=f.Play;observe(0x15E9ECU);
    s.r[0]=f.Actor;s.r[2]=0x1F8E28U;observe(0x182C44U);
    s.r[0]=f.Queue;s.r[1]=f.Model;s.r[14]=0x20A8F8U;f.W32(f.Queue+4U,0);observe(0x224104U);
    f.W32(f.Queue+8U,f.Model);f.W32(f.Queue+4U,1);observe(0x182C48U);
    const uint32_t shadowModel=f.Model+0x400U;
    f.W32(shadowModel,0x63BED4U);f.W32(shadowModel+0xA0U,f.Renderer);
    s.r[0]=shadowModel;s.r[14]=0x21EFACU;observe(0x20A898U);
    s.r[0]=f.Queue;s.r[1]=shadowModel;s.r[14]=0x20A8F8U;observe(0x224104U);
    f.W32(f.Queue+12U,shadowModel);f.W32(f.Queue+4U,2);observe(0x223EDCU);
    // Native permutation reverses the appended slots before the visitor.
    f.W32(f.Queue+8U,shadowModel);f.W32(f.Queue+12U,f.Model);
    cursor(0x14000000U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
    const auto consume=[&](uint32_t slot,uint32_t begin,uint32_t end,bool reverse){
        cursor(begin);Require(f.Memory.Read32(slot,&s.r[0]),"consumer model");s.r[4]=f.Queue;s.r[5]=reverse?(slot-f.Queue-8U)/4U:slot;
        observe(reverse?0x5EF094U:0x5EF0CCU);cursor(end);observe(reverse?0x5EF098U:0x5EF0D0U);
    };
    consume(f.Queue+12U,0x14000008U,0x14000020U,false);
    consume(f.Queue+8U,0x14000020U,0x14000038U,true);
    cursor(0x14000040U);observe(0x11EF3CU);
    std::vector<Oot3dPicaCommandListCompositionSpan> spans;
    tracker.TakeCommandListCompositionSpans(0,0,spans);
    Require(spans.size()==4,"parent coverage erased children/gaps");
    Require(spans[1].Attribution.NativeActorGeometry && spans[1].Attribution.NativeActorIdentity==f.Actor &&
            !spans[1].Attribution.NativeProjectedShadow,"main model lost actor or became blob");
    Require(spans[2].Attribution.NativeProjectedShadow && !spans[2].Attribution.NativeActorGeometry,"blob inherited actor/caster role");
    Require(!spans[0].Attribution.NativeActorGeometry && !spans[3].Attribution.NativeProjectedShadow,"pass prefix/suffix contaminated");
    s.r[0]=f.Play;observe(0x15E9ECU);cursor(0x14000008U);s.r[0]=f.Queue;s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);
    consume(f.Queue+8U,0x14000008U,0x14000020U,false);observe(0x11EF3CU);
    tracker.TakeCommandListCompositionSpans(0,0,spans);
    for(const auto& span:spans)if(span.BeginAddress==0x14000008U)Require(!span.Attribution.NativeActorGeometry,"stale queue generation reused");
    tracker.Reset();tracker.TakeCommandListCompositionSpans(0,0,spans);Require(spans.empty(),"restore retained spans");
    f.W32(kMm3dLightingHookSites[0].Pc,0);Require(!tracker.VerifyHooks(f.Memory),"wrong image enabled observer");
    cursor(0x14000100U);s.r[14]=0x11EF3CU;observe(kMm3dRenderListVisitEntry);cursor(0x14000140U);observe(0x11EF3CU);
    tracker.TakeCommandListCompositionSpans(0,0,spans);Require(spans.empty(),"disabled observer emitted metadata");
}

// P1.0/P1.1: each selected light's function, and BakedOutdoorRoom from the
// environment group, with guard words and Unknown fallback.
void TestLightFunctionsAndGroup() {
    Fixture f; Mm3dNativeA32LightingProbe probe; auto& s=f.State;
    probe.SetSupplementalPublication(true,true);
    const uint32_t sun=f.Play+0x86D0U,moon=f.Play+0x86E8U,torch=f.Actor+0x2B0U,lamp=f.Actor+0x8000U;
    f.Direction(sun,120,20,220);f.Direction(moon,-120,-20,20);
    f.W32(f.Play+0xA88U,f.Nodes);f.Link(f.Nodes,moon,f.Nodes+12U);f.Link(f.Nodes+12U,sun,f.Nodes+24U);
    f.Link(f.Nodes+24U,torch,f.Nodes+36U);f.Link(f.Nodes+36U,lamp,0);
    f.Point(torch+4U,250);f.Point(lamp+4U,300); // lamp: linked, owner not proved
    f.W16(f.Actor,0x2E);f.W8(f.Actor+2U,6);f.W32(f.Actor+0x2ACU,f.Nodes+24U);
    f.W32(f.Play+0x20C0U+6U*12U,1);f.W32(f.Play+0x20C4U+6U*12U,f.Actor);
    // Outdoor, clock running (South Clock Town decode): TimeFlows.
    f.W16(f.Play+0x86B2U,3);f.W8(f.Play+0x86C6U,0);f.W8(f.Play+0x86C7U,0);f.W8(f.Play+0x8704U,0);f.W8(f.Play+0xC528U,1);
    Require(!probe.VerifyEnvironmentGroupGuards(f.Memory),"guards held on an image without the writers");
    probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.EnvironmentGroup(f.Play,0x6F) && !probe.CaptureRenderer(0,f.Memory).SceneDirectionals->BakedOutdoorRoom,
            "unknown group baked the room");
    for (const auto& guard : Mm3dEnvironmentGroupLayout::Guards) f.W32(guard.Address,guard.Word);
    Require(probe.VerifyEnvironmentGroupGuards(f.Memory),"writer guards rejected");
    f.W32(f.Play+0x138U,2);probe.CaptureSceneLights(f.Play,f.Memory);
    const auto scene=probe.CaptureRenderer(0,f.Memory);
    Require(probe.EnvironmentGroup(f.Play,0x6F)==SceneEnvironmentGroup::TimeFlows,"outdoor clock scene not TimeFlows");
    Require(scene.SceneDirectionals->BakedOutdoorRoom && scene.SceneDirectionals->Scope==PicaNativeLightScope::Scene,
            "outdoor environment not baked/scene");
    // Bind slot 0 to the sun, 1 to the actor torch, 2 to the unproved lamp.
    const auto bind=[&](uint32_t slot,uint32_t params,bool point){
        const uint32_t record=f.Owner+12U+slot*24U;
        s.r[0]=f.Owner;s.r[1]=params;s.r[2]=record;s.r[3]=record;
        probe.ObserveBlockEntry(point?0x40650CU:0x4648F4U,s,f.Memory);
        f.W8(record,point?1:0);f.W8(record+1U,200);f.W8(record+2U,200);f.W8(record+3U,200);
    };
    s.r[0]=f.Owner;s.r[1]=f.Context;probe.ObserveBlockEntry(0x1EEAA8U,s,f.Memory);
    bind(0,sun+4U,false);bind(1,torch+4U,true);bind(2,lamp+4U,true);
    for (uint32_t slot=0;slot<3;++slot){
        s.r[0]=f.Context+0x70U+slot*0x118U;s.r[1]=f.Owner+12U+slot*24U;s.r[5]=slot;s.r[8]=f.Context;
        probe.ObserveBlockEntry(0x1EED64U,s,f.Memory);
    }
    const auto lit=probe.CaptureRenderer(f.Renderer,f.Memory);
    Require(lit.Sources[0].SourceIdentity==sun+4U && lit.Sources[0].Scope==PicaNativeLightScope::Scene &&
            lit.Sources[0].Origin==PicaNativeLightOrigin::EnvironmentDirectional,"sun slot not scene/environment");
    Require(lit.Sources[1].SourceIdentity==torch+4U && lit.Sources[1].Scope==PicaNativeLightScope::Local &&
            lit.Sources[1].Origin==PicaNativeLightOrigin::Actor,"actor torch not local/actor");
    Require(lit.Sources[2].SourceIdentity==lamp+4U && lit.Sources[2].Scope==PicaNativeLightScope::Local &&
            lit.Sources[2].Origin==PicaNativeLightOrigin::Unknown,"unproved owner not unknown/local");
    Require(lit.Sources[3].Scope==PicaNativeLightScope::Local,"empty slot not local");
    // A stale snapshot (next guest frame not captured) classifies nothing as scene.
    f.W32(f.Play+0x138U,3);
    const auto stale=probe.CaptureRenderer(f.Renderer,f.Memory);
    Require(stale.Sources[0].Scope==PicaNativeLightScope::Local && stale.Sources[0].Origin==PicaNativeLightOrigin::Unknown,
            "stale snapshot kept scene scope");
    // A room change inside the visit never flips the latched group.
    f.W8(f.Play+0x86C7U,1);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.EnvironmentGroup(f.Play,0x6F)==SceneEnvironmentGroup::TimeFlows,"latched group flipped in the visit");
    // Another scene: Stock Pot Inn decode (sunDisabled, fixed lights, no sky) = Indoor, not baked.
    f.W16(f.Play+0x148U,0x61);f.W8(f.Play+0x86C6U,1);f.W8(f.Play+0x8704U,1);f.W8(f.Play+0xC528U,0);
    f.W32(f.Play+0x138U,4);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.EnvironmentGroup(f.Play,0x61)==SceneEnvironmentGroup::Indoor && !probe.EnvironmentGroup(f.Play,0x6F),
            "new visit did not relatch");
    Require(!probe.CaptureRenderer(0,f.Memory).SceneDirectionals->BakedOutdoorRoom,"interior baked as outdoor");
    // Unknown values (lightMode 2) stay Unknown and are retried, never latched.
    f.W16(f.Play+0x148U,0x62);f.W8(f.Play+0x8704U,2);f.W32(f.Play+0x138U,5);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(!probe.EnvironmentGroup(f.Play,0x62),"unknown light mode classified");
    f.W8(f.Play+0x8704U,1);f.W8(f.Play+0x86C7U,0);f.W8(f.Play+0x86C6U,0);f.W8(f.Play+0xC528U,2);
    f.W32(f.Play+0x138U,6);probe.CaptureSceneLights(f.Play,f.Memory);
    Require(probe.EnvironmentGroup(f.Play,0x62)==SceneEnvironmentGroup::FixedTime &&
            probe.CaptureRenderer(0,f.Memory).SceneDirectionals->BakedOutdoorRoom,"readable retry not latched as fixed outdoor");
    // Guard corruption after mount: Unknown again.
    f.W32(Mm3dEnvironmentGroupLayout::Guards[4].Address,0);
    Require(!probe.VerifyEnvironmentGroupGuards(f.Memory) && !probe.EnvironmentGroup(f.Play,0x62),"corrupt guard kept group");
    probe.Reset();
}
} // namespace
int main() {
    TestAcceptedAndSelectedSources();TestSceneSnapshots();TestWaterCountAndHeight();TestDeferredQueueAndShadowScopes();
    TestAmbiguousQueueOwnership();TestNestedAndInvalidConsumer();TestWaterOperandEligibility();
    TestLightFunctionsAndGroup();TestClockCutsAndDayReset();
    std::cout << "mm3d_native_lighting_tests: ok\n";
}
