// Ported from mm3d-dekui src/mm3d/camera_live.h (tgacquin/mm3d-dekui).
// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace Mm3dDekui::CameraLive {
using U32 = std::uint32_t;
constexpr U32 CameraPointer = 0x00656b5c;
struct Snapshot {
    bool valid{}, active{};
    U32 base{};
    float fov{}, auxiliary_fov{}, near_plane{}, far_plane{}, matrix_fov{};
    std::array<float,3> eye{}, target{};
    const char* status = "Enable live camera inspection during gameplay.";
};
inline std::atomic<bool> enabled{false}, needs_service{false};
inline std::mutex mutex;
inline Snapshot published;
inline std::atomic<float> desired_fov{45.f};
// Translation-only replacement of the two verified primary projection loads.
// This does not patch the ROM or guest code/data memory.
constexpr U32 ProjectionSVC=0x00fffffe;
inline U32 ProjectionInstruction(U32 pc,U32 instruction) {
    return (pc==0x18d414 || pc==0x18d428 || pc==0x0018d414 || pc==0x0018d428) && instruction==0xed940a06 // ref 0x18d570, 0x18d584
        ? 0xef000000U|ProjectionSVC : instruction;
}
inline U32 ProjectionValue(U32 native_bits,float configured) {
    const float native=std::bit_cast<float>(native_bits);
    if(!std::isfinite(native) || native<=0.f || native>=170.f ||
       !std::isfinite(configured) || configured<=45.f || configured>70.f) return native_bits;
    constexpr float rad=3.14159265358979323846f/360.f;
    // A 45-degree gameplay camera becomes the chosen FOV. Other cameras retain
    // their animation, with the same relative widening in projection space.
    const float result=std::atan(std::tan(native*rad)*std::tan(configured*rad)/std::tan(45.f*rad))/rad;
    return std::bit_cast<U32>(result);
}
// Restoration's Play camera array: main at408, four198-byte cameras,
// active index atA7A, Camera::state at156 (DEMO0/FREE0 scripted cameras).
template<class M> bool ScriptedCamera(M& m) {
    auto context=m.Read(0x72f5c0);
    const auto pointer=[](U32 p,U32 size){return p>=0x100000&&!(p&3)&&p<0x40000000&&size<=0x40000000-p;};
    if(!context||!pointer(*context,0x170))return false;
    auto game=m.Read(*context+0x16c);if(!game||!pointer(*game,0xa7c))return false;
    auto type=m.Read(*game+0x13c),indices=m.Read(*game+0xa78);
    if(!type||((*type>>8)&255)!=4||!indices)return false;
    const unsigned index=*indices>>16;if(index>3)return false;
    const U32 camera=*game+0x408+index*0x198;
    auto owner=m.Read(camera+0xa0),state=m.Read(camera+0x154);
    if(owner!=game||!state)return false;
    const unsigned setting=*state>>16;
    return setting==9||setting==10;
}
// Restoration's verified ChangeMode trampoline, after arguments are restored.
constexpr U32 ChangeModePC=0x007d0910,ChangeModeWord=0xe2804c01,ChangeModeSVC=0xffffb3; // ref 0x7d0910 (Restoration source port)
inline U32 ChangeModeInstruction(U32 pc,U32 word) {
    return pc==ChangeModePC&&word==ChangeModeWord?0xef000000|ChangeModeSVC:word;
}
template<class M> bool ChangeModeCodeMatches(M& m) {
    constexpr std::array<U32,14> code{0xe52de004,0xed2d0b20,0xe1a04000,0xe1a05001,
        0xe1a06002,0xebfffa1b,0xe3500001,0x03a0501f,0xe1a00004,0xe1a01005,
        0xe1a02006,ChangeModeWord,0xecbd0b20,0xe49df004};
    for(unsigned i=0;i<code.size();++i)if(m.Read(0x007d08e4+4*i)!=std::optional<U32>(code[i]))return false; // ref 0x7d08e4 (Restoration source port)
    return true;
}
template<class M> float ConfiguredFov(M& m,U32 projection) {
    // Only the live top-screen projection, not menu or preview cameras.
    if(m.Read(CameraPointer)==std::optional<U32>(projection)&&
       m.Read(projection+4)==std::optional<U32>(0)&&m.Read(projection+8)==std::optional<U32>(400)&&
       m.Read(projection+12)==std::optional<U32>(0)&&m.Read(projection+16)==std::optional<U32>(240)) {
        if(ScriptedCamera(m))return 51.f;
    }
    return desired_fov.load();
}
template<class Memory> bool CodeMatches(Memory& m,U32 pc=0) {
    // Original USA Rev 1 and the supplied Restoration+HD HUD v110 build.
    for(bool restoration : {false,true}) {
        const U32 delta=0; // ref variant delta 0x15c; both variants map to v1.1
        if(pc && pc!=0x18d414+delta && pc!=0x18d428+delta) continue;
        bool matches=true;
        for(const auto& [address, expected] : std::array<std::pair<U32,U32>,6>{{
            {0x18d3cc+delta,0xe92d4010}, {0x18d414+delta,0xed940a06}, {0x18d428+delta,0xed940a06},
            {0x18d448+delta,0xed940a13}, {restoration ? 0x001dd0d8U : 0x1dd0d8U,0x00656b44}, // ref 0x1dd2e8
            {0x18d524+delta,0x3c8efa35}}})
            if(m.Read(address)!=std::optional<U32>(expected)) { matches=false; break; }
        if(matches) return true;
    }
    return false;
}

// Restoration permits modes9..11 (KEEPON, PARALLEL, TALK) plus FREECAMERA.
// Restrict that range to9..10, restoring the game's dialogue camera selection.
constexpr U32 DialogueCameraPC=0x007cf174,DialogueCameraWord=0x13530002; // ref 0x7cf174 (Restoration source port)
inline U32 DialogueCameraInstruction(U32 pc,U32 word) {
    return pc==DialogueCameraPC&&word==DialogueCameraWord?0x13530001:word;
}
template<class M> bool DialogueCameraCodeMatches(M& m) {
    for(auto [pc,word]:std::array<std::pair<U32,U32>,6>{{
        {0x007cf16c,0xe2413009},{0x007cf170,0xe351001f},{DialogueCameraPC,DialogueCameraWord}, // ref 0x7cf16c (Restoration source port), 0x7cf170 (Restoration source port)
        {0x007cf178,0x93a03001},{0x007cf180,0x9a000001},{0x007d08f8,0xebfffa1b}}}) // ref 0x7cf178 (Restoration source port), 0x7cf180 (Restoration source port), 0x7d08f8 (Restoration source port)
        if(m.Read(pc)!=std::optional<U32>(word))return false;
    return true;
}

inline Snapshot GetSnapshot() { std::scoped_lock lock{mutex}; return published; }
inline void SetFov(float degrees) {
    std::scoped_lock lock{mutex};
    desired_fov.store(std::isfinite(degrees) ? std::clamp(degrees,45.f,70.f) : 45.f);
    needs_service.store(true);
}
inline void Stop() { SetFov(45.f); }

// CPU-thread state. The adapter reads/writes guest virtual RAM, never host UI state.
class Engine {
    template<class Memory> bool ReadCamera(Memory& m, Snapshot& s) {
        auto ptr=m.Read(CameraPointer);
        // Do not follow the getter's fallback camera; require a live heap object.
        if(!ptr || *ptr<0x08000000 || *ptr>0x3ffffd00 || (*ptr&3)) return false;
        s.base=*ptr;
        auto f=[&](U32 offset,float& out) {
            auto v=m.Read(s.base+offset); if(!v) return false;
            out=std::bit_cast<float>(*v); return std::isfinite(out);
        };
        auto left=m.Read(s.base+4),right=m.Read(s.base+8),top=m.Read(s.base+12),bottom=m.Read(s.base+16);
        if(!left||!right||!top||!bottom || *left!=0 || *right!=400 || *top!=0 || *bottom!=240) return false;
        if(!f(0x18,s.fov)||!f(0x4c,s.auxiliary_fov)||!f(0x1c,s.near_plane)||!f(0x20,s.far_plane)) return false;
        if(s.fov<10 || s.fov>120 || s.auxiliary_fov<10 || s.auxiliary_fov>120 ||
           s.near_plane<=0 || s.far_plane<=s.near_plane) return false;
        for(unsigned i=0;i<3;++i)
            if(!f(0x28+4*i,s.eye[i])||!f(0x34+4*i,s.target[i]) ||
               std::abs(s.eye[i])>1e7f || std::abs(s.target[i])>1e7f) return false;
        float projection{};
        if(!f(0xd0,projection) || projection<=0) return false; // matrix at +BC, element [5]
        s.matrix_fov=2.f*std::atan(1.f/projection)*180.f/3.14159265358979323846f;
        s.valid=true; return true;
    }
public:
    template<class Memory>
    Snapshot Tick(Memory& m,float degrees,const void* =nullptr) {
        Snapshot s;
        const bool code_ok=CodeMatches(m);
        if(code_ok) ReadCamera(m,s);
        s.active=code_ok && std::isfinite(degrees) && degrees>45.f && degrees<=70.f;
        s.status=!code_ok ? "Unsupported game code; original FOV retained." :
            s.active ? "Saved FOV applied when each camera projection is built." : "Original game FOV.";
        return s;
    }
};

template<class Memory>
void Service(Memory& memory,const void* space,std::int64_t) {
    static Engine engine;
    std::scoped_lock lock{mutex};
    const float degrees=desired_fov.load();
    published=engine.Tick(memory,degrees,space);
    needs_service.store(false);
}
}
