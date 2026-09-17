// Developer-only title-derived experiment. Not registered in the game or release.
// ARM 0040C744..0040C9CC: whole pose; 002BFCB4..002BFD74: triangle decoding.
// Compute and validate before committing guest writes; no PICA arithmetic here.
#include "triaevum_title_whole_aot_abi.h"
#include "oot3d_native_a32_memory.h"
#include <windows.h>
#include <immintrin.h>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

#pragma float_control(precise, on)
#pragma float_control(except, on)
#pragma fp_contract(off)

using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;

namespace {
const Oot3dWholeAotProgramV2* original;
uint64_t acceptedInvocations=0;
bool NormalOrZero(uint32_t bits) {
    const auto exponent=bits&0x7F800000U;
    return exponent!=0x7F800000U && (exponent || !(bits&0x7FFFFFFFU));
}
struct Span {
    uint32_t address;
    uint64_t size;
    bool Valid() const { return uint64_t(address)+size <= (uint64_t(1)<<32); }
    bool Overlaps(Span b) const {
        return uint64_t(address)<uint64_t(b.address)+b.size &&
               uint64_t(b.address)<uint64_t(address)+size;
    }
};
struct BoneResult {
    std::array<uint32_t, 15> vfp;
    std::array<uint32_t, 15> stores;
};
constexpr unsigned storeOffsets[] = {0,16,36,40,4,24,8,20,32,12,28,44,12,28,44};
float F(uint32_t v) { return std::bit_cast<float>(v); }
uint32_t U(float v) { return std::bit_cast<uint32_t>(v); }

// Keep the native evaluation order, including the non-fused VNMLS operation.
// exceptional results are rejected by the enclosing MXCSR scope before writes.
bool Bone(const uint32_t* pose, const uint8_t* table, const uint32_t* literals,
          BoneResult& out, uint32_t& fpscr, uint32_t& extraBlocks, uint32_t limit) {
    std::array<float,15> s{};
    for (unsigned i=3;i<6;++i) if (!NormalOrZero(pose[i])) return false;
    s[6]=F(literals[0]); s[4]=F(literals[1]); s[2]=F(literals[3]);
    s[3]=F(pose[3])*s[6]; s[0]=F(pose[4])*s[6]; s[1]=F(pose[5])*s[6];
    bool negative[3]={s[3]<s[4],s[0]<s[4],s[1]<s[4]};
    // Inputs are normal/zero, but overflow must not enter the reduction loops.
    for (unsigned i : {3U,0U,1U}) if (!NormalOrZero(U(s[i]))) return false;
    const auto compare=a32::VfpBinary32Compare(U(s[1]),U(s[4]),true);
    fpscr=(fpscr&0x0FFFFFFFU)|compare.value;
    uint32_t index[3];
    unsigned axis=0;
    for (unsigned i : {3U,0U,1U}) {
        s[i]=F(U(s[i])&0x7FFFFFFFU);
        while (std::bit_cast<int32_t>(U(s[i]))>=std::bit_cast<int32_t>(literals[2])) {
            if (extraBlocks>=limit) return false;
            ++extraBlocks;
            const float reduced=s[i]-s[2];
            if (!(reduced<s[i]) || !NormalOrZero(U(reduced))) return false;
            s[i]=reduced;
        }
        // Original conversion truncates to uint32 before narrowing to uint16.
        // Other conversion ranges retain the baseline implementation.
        if (!(s[i]>=0.0f && s[i]<65536.0f)) return false;
        index[axis++]=static_cast<uint32_t>(s[i]);
    }
    s[7]=static_cast<float>(index[1]); s[5]=static_cast<float>(index[0]);
    s[8]=static_cast<float>(index[2]);
    auto load=[&](unsigned axis,unsigned word) {
        uint32_t value;
        std::memcpy(&value,table+(index[axis]&255U)*16+word*4,4);
        return value;
    };
    for (unsigned a=0;a<3;++a) for (unsigned w=0;w<4;++w)
        if (!NormalOrZero(load(a,w))) return false;
    s[9]=F(load(0,3)); s[0]=s[0]-s[7]; s[7]=F(load(0,1));
    s[3]=s[3]-s[5]; s[5]=F(load(0,0)); s[1]=s[1]-s[8];
    s[8]=F(load(0,2)); s[10]=F(load(1,2));
    auto mla=[&](unsigned d,unsigned a,unsigned b) { float p=s[a]*s[b]; s[d]=s[d]+p; };
    auto nmls=[&](unsigned d,unsigned a,unsigned b) { float p=s[a]*s[b]; s[d]=p-s[d]; };
    mla(5,3,8); mla(7,3,9);
    s[3]=F(load(1,0)); s[8]=F(load(1,1)); s[9]=F(load(1,3));
    mla(3,0,10); mla(8,0,9);
    s[0]=F(load(2,0)); s[10]=F(load(2,2)); s[9]=F(load(2,1)); s[11]=F(load(2,3));
    mla(0,1,10); if (negative[0]) s[5]=F(U(s[5])^0x80000000U);
    mla(9,1,11); if (negative[1]) s[3]=F(U(s[3])^0x80000000U);
    s[13]=s[7]*s[8]; s[12]=s[5]*s[8];
    if (negative[2]) s[0]=F(U(s[0])^0x80000000U);
    s[10]=s[9]*s[8]; s[1]=s[5]*s[9]; s[11]=s[0]*s[8];
    s[8]=s[7]*s[0]; s[5]=s[5]*s[0]; s[0]=s[7]*s[9];
    out.stores[0]=U(s[10]); out.stores[1]=U(s[11]); out.stores[2]=U(s[12]);
    s[14]=s[8]; s[7]=s[5]; out.stores[3]=U(s[13]);
    nmls(14,1,3); nmls(1,8,3); mla(7,0,3); mla(0,5,3);
    out.stores[4]=U(s[14]); out.stores[5]=U(s[1]); out.stores[6]=U(s[7]);
    out.stores[7]=U(s[0]); s[0]=F(U(s[3])^0x80000000U); out.stores[8]=U(s[0]);
    out.stores[9]=out.stores[10]=out.stores[11]=0;
    for (unsigned i=0;i<3;++i) out.stores[12+i]=pose[i];
    // Translation is a bitwise VFP load/store, not a numerical operation.
    for (unsigned i=0;i<15;++i) {
        out.vfp[i]=U(s[i]);
        if (!NormalOrZero(out.vfp[i])) return false;
    }
    out.vfp[0]=pose[2];
    return true;
}

bool Skeleton(uint32_t pc,a32::GuestState& state,NativeA32Memory& memory,
    a32::ExecutionResult* result,Oot3dWholeAotStats* stats,
    Oot3dWholeAotExternalCall external,uint32_t budget,uint32_t* consumed,
    a32::BlockEntryCallback callback,void* user,const uint32_t* pcs,size_t count,
    const Oot3dAotBlockEntryFilter* filter,bool skipFirst,uint32_t stop) {
    auto fallback=[&] { return original->Execute(pc,state,memory,result,stats,external,
        budget,consumed,callback,user,pcs,count,filter,skipFirst,stop); };
    if (pc!=0x0040C744 || stop!=state.r[14] || (stop&3U) || !result || !stats ||
        (state.fpscr&0x00F79F00U) || (callback && (!skipFirst || !count))) return fallback();
    if (callback) for (size_t i=0;i<count;++i)
        if (pcs[i]>pc && pcs[i]<0x0040C9CC) return fallback();
    auto read=[&](uint32_t address,uint32_t& value) {
        auto p=memory.GetReadPointer(address,4); if (!p) return false;
        std::memcpy(&value,p,4); return true;
    };
    if (state.r[0]>0xFFFFFFEBU || state.r[13]<44) return fallback();
    uint32_t owner,header,bones,destination;
    if (!read(state.r[0]+4,owner) || !read(owner,header) || header>0xFFFFFFF7U ||
        !read(header+8,bones) || !read(state.r[0]+16,destination) ||
        !bones || bones>0x7FFFFFFFU || uint64_t(bones)*4+3>budget) return fallback();
    uint32_t literals[5];
    auto constants=memory.GetReadPointer(0x0040C9CC,20);
    if (!constants) return fallback();
    std::memcpy(literals,constants,20);
    for (auto i : {0U,1U,3U}) if (!NormalOrZero(literals[i])) return fallback();
    Span output{destination,uint64_t(bones)*48}, stack{state.r[13]-44,44};
    Span inputs[]={{state.r[0]+4,4},{state.r[0]+16,4},{owner,4},{header+8,4},
        {state.r[1],uint64_t(bones)*36},{0x0040C9CC,20},{literals[4],4096}};
    if (!output.Valid() || !stack.Valid() || output.Overlaps(stack) ||
        !memory.IsWritable(output.address,size_t(output.size)) ||
        !memory.IsWritable(stack.address,44)) return fallback();
    for (auto input:inputs) if (!input.Valid() || output.Overlaps(input) || stack.Overlaps(input))
        return fallback();
    auto poses=memory.GetReadPointer(state.r[1],size_t(bones)*36);
    auto table=memory.GetReadPointer(literals[4],4096);
    if (!poses || !table) return fallback();
    std::vector<BoneResult> computed(bones);
    uint32_t fpscr=state.fpscr, extra=0;
    unsigned saved=_mm_getcsr();
    _mm_setcsr(0x1F80U);
    bool valid=true;
    for (uint32_t i=0;i<bones && valid;++i) {
        uint32_t pose[9]; std::memcpy(pose,poses+size_t(i)*36,36);
        valid=Bone(pose,table,literals,computed[i],fpscr,extra,budget-(4*bones+3));
    }
    unsigned raised=_mm_getcsr()&0x3FU;
    _mm_setcsr(saved);
    if (!valid || (raised&0x1FU)) return fallback();
    // All validation and arithmetic finish before the first observable write.
    unsigned n=0;
    for (unsigned r : {0U,1U,4U,5U,6U,7U,8U,9U,10U,11U,14U})
        memory.WriteFast<uint32_t>(stack.address+4*n++,state.r[r]);
    for (uint32_t i=0;i<bones;++i) for (unsigned w=0;w<15;++w)
        memory.WriteFast<uint32_t>(destination+i*48+storeOffsets[w],computed[i].stores[w]);
    std::copy(computed.back().vfp.begin(),computed.back().vfp.end(),state.vfp.begin());
    state.fpscr=fpscr|((raised&0x20U)?0x10U:0U);
    state.cpsr=(state.cpsr&0x0FFFFFFFU)|0x60000000U; // final CMP count,count
    state.r[1]=state.r[2]=bones; state.r[3]=(bones-1)*48+44; state.r[12]=44;
    state.r[15]=stop; state.r[14]=literals[2];
    *result={a32::ExitKind::Branch,stop,a32::FallbackReason::None,0};
    ++stats->Calls; if (consumed) *consumed=4*bones+3+extra;
    ++acceptedInvocations;
    return true;
}

bool Triangle(uint32_t pc,a32::GuestState& state,NativeA32Memory& memory,
    a32::ExecutionResult* result,Oot3dWholeAotStats* stats,
    Oot3dWholeAotExternalCall external,uint32_t budget,uint32_t* consumed,
    a32::BlockEntryCallback callback,void* user,const uint32_t* pcs,size_t count,
    const Oot3dAotBlockEntryFilter* filter,bool skipFirst,uint32_t stop) {
    auto fallback=[&] { return original->Execute(pc,state,memory,result,stats,external,
        budget,consumed,callback,user,pcs,count,filter,skipFirst,stop); };
    // 002BFCB4..002BFD74: unpack all three indexed signed-int16 vertices.
    if (pc!=0x002BFCB4 || stop!=state.r[14] || (stop&3U) || !budget || !result || !stats ||
        (state.fpscr&0x00F79F00U) || (callback && !skipFirst)) return fallback();
    Span output{state.r[2],36}, indices{state.r[0],8};
    if (!output.Valid() || !indices.Valid() || output.Overlaps(indices) ||
        !memory.IsWritable(output.address,36)) return fallback();
    const auto* source=memory.GetReadPointer(indices.address,8);
    if (!source) return fallback();
    uint16_t index[4]; std::memcpy(index,source,8);
    int16_t xyz[9];
    for (unsigned i=0;i<3;++i) {
        // Only the first two indices contain native polygon flags.
        uint32_t v=index[i+1]&(i<2?0x1FFFU:0xFFFFU);
        Span vertex{state.r[1]+v*6,6};
        if (!vertex.Valid() || vertex.Overlaps(output)) return fallback();
        auto p=memory.GetReadPointer(vertex.address,6);
        if (!p) return fallback();
        std::memcpy(xyz+i*3,p,6);
    }
    uint32_t values[9];
    // Every signed int16 is exactly representable in binary32: no rounding/flags.
    for (unsigned i=0;i<9;++i) values[i]=U(static_cast<float>(xyz[i]));
    for (unsigned i=0;i<9;++i) memory.WriteFast<uint32_t>(output.address+i*4,values[i]);
    state.r[0]=static_cast<uint32_t>(static_cast<int32_t>(xyz[8]));
    state.r[1]=static_cast<uint32_t>(static_cast<int32_t>(xyz[7]));
    state.r[3]=static_cast<uint32_t>(static_cast<int32_t>(xyz[5]));
    state.r[12]=static_cast<uint32_t>(static_cast<int32_t>(xyz[4]));
    state.vfp[0]=values[8]; state.r[15]=stop;
    *result={a32::ExitKind::Branch,stop,a32::FallbackReason::None,0};
    ++stats->Calls; if (consumed) *consumed=1;
    ++acceptedInvocations;
    return true;
}
bool Geometry(uint32_t pc,a32::GuestState& state,NativeA32Memory& memory,
    a32::ExecutionResult* result,Oot3dWholeAotStats* stats,
    Oot3dWholeAotExternalCall external,uint32_t budget,uint32_t* consumed,
    a32::BlockEntryCallback callback,void* user,const uint32_t* pcs,size_t count,
    const Oot3dAotBlockEntryFilter* filter,bool skipFirst,uint32_t stop) {
    auto execute=pc==0x002BFCB4?Triangle:Skeleton;
    return execute(pc,state,memory,result,stats,external,budget,consumed,
        callback,user,pcs,count,filter,skipFirst,stop);
}
}
#if !defined(TRIAEVUM_GEOMETRY_KERNEL_TEST)
// Optional diagnostic selection only; never affects the running game's inputs.
extern "C" __declspec(dllexport) bool triaevum_invocation_select_input(
    const a32::GuestState* state,const NativeA32Memory* memory) noexcept {
    const char* value=std::getenv("TRIAEVUM_SKELETON_MIN_BONES");
    if (!value || state->r[15]!=0x0040C744) return true;
    uint32_t minimum=static_cast<uint32_t>(std::strtoul(value,nullptr,10));
    uint32_t p=state->r[0];
    for (uint32_t offset : {4U,0U,8U}) {
        if (p>0xFFFFFFFFU-offset) return false;
        const auto* source=memory->GetReadPointer(p+offset,4);
        if (!source) return false;
        std::memcpy(&p,source,4);
    }
    return p>=minimum && p<=0x7FFFFFFFU;
}
extern "C" __declspec(dllexport) uint64_t triaevum_invocation_candidate_hits() noexcept {
    return acceptedInvocations;
}
extern "C" __declspec(dllexport) const Oot3dWholeAotProgramV2*
triaevum_title_whole_aot_query(uint32_t abi) noexcept {
    if (abi!=kOot3dWholeAotPluginAbiV2) return nullptr;
    static Oot3dWholeAotProgramV2 program{};
    if (!program.Execute) {
        const char* path=std::getenv("TRIAEVUM_INVOCATION_BASELINE");
        if (!path) return nullptr;
        auto module=LoadLibraryExA(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) return nullptr;
        using Query=const Oot3dWholeAotProgramV2* (*)(uint32_t) noexcept;
        auto query=reinterpret_cast<Query>(GetProcAddress(module,"triaevum_title_whole_aot_query"));
        original=query?query(abi):nullptr;
        if (!original || original->StructSize!=sizeof(program)) return nullptr;
        program=*original; program.Execute=Geometry;
    }
    return &program;
}
#endif
