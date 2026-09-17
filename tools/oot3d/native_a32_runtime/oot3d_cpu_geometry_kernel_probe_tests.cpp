// Private developer test: baseline module and original code are supplied by path.
#define TRIAEVUM_GEOMETRY_KERNEL_TEST
#include "oot3d_cpu_geometry_kernel_probe.cpp"
#include <fstream>
#include <iterator>
#include <cstdio>

int main(int argc,char** argv) {
    if (argc!=3) return 1;
    auto module=LoadLibraryExA(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    using Query=const Oot3dWholeAotProgramV2* (*)(uint32_t) noexcept;
    auto query=module?reinterpret_cast<Query>(GetProcAddress(module,"triaevum_title_whole_aot_query")):nullptr;
    original=query?query(kOot3dWholeAotPluginAbiV2):nullptr;
    if (!original) return 2;
    std::ifstream stream(argv[2],std::ios::binary);
    std::vector<uint8_t> code((std::istreambuf_iterator<char>(stream)),{});
    if (code.size()!=4567040) return 3;
    NativeA32Memory initial;
    if (!initial.MapRegion({"code",0x100000,code.size(),false,true,code}) ||
        !initial.MapRegion({"test",0x08000000,0x10000,true,false,{}})) return 4;
    uint32_t seed=0x73625419;
    auto random=[&] { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; };
    unsigned fast=0, rejected=0;
    for (unsigned test=0;test<512;++test) {
        const uint32_t bones=1+test%32;
        auto memory=initial;
        memory.Write32(0x08000004,0x08000040);
        memory.Write32(0x08000040,0x08000080);
        memory.Write32(0x08000088,bones);
        memory.Write32(0x08000010,0x08004000);
        for (unsigned i=0;i<bones;++i) for (unsigned j=0;j<9;++j) {
            uint32_t v=random();
            if (j>=3 && j<6) {
                float angle=(int32_t(v%100000)-50000)*0.0002f;
                // Exercise original range reduction as well as small rotations.
                if (test%7==0) angle*=1000;
                v=U(angle);
                if (test%13==0) v=(i+j)%2?0x80000000U:0;
                if (test%17==0 && i==0 && j==3) v=1; // subnormal rejection
            }
            memory.Write32(0x08001000+i*36+j*4,v);
        }
        a32::GuestState expected{}, actual{};
        for (auto& r:expected.r) r=random();
        for (auto& s:expected.vfp) s=random();
        expected.r[0]=0x08000000;expected.r[1]=0x08001000;
        expected.r[13]=0x0800F000;expected.r[14]=0x00123400;expected.r[15]=0x0040C744;
        expected.cpsr=0xA0000010;expected.fpscr=(test%4)*0x01000000U;
        if (test%19==0) expected.fpscr|=0x00400000U; // unsupported fast rounding mode
        actual=expected;
        memory.EnableWriteTraceFingerprint(true);
        auto other=memory;
        Oot3dWholeAotStats a{},b{};
        a32::ExecutionResult ar{},br{};
        uint32_t ac=0,bc=0;
        const auto hits=acceptedInvocations;
        const auto host=_mm_getcsr();
        auto okA=original->Execute(0x0040C744,expected,memory,&ar,&a,nullptr,100000,&ac,
            nullptr,nullptr,nullptr,0,nullptr,true,0x00123400);
        auto okB=Skeleton(0x0040C744,actual,other,&br,&b,nullptr,100000,&bc,
            nullptr,nullptr,nullptr,0,nullptr,true,0x00123400);
        if (_mm_getcsr()!=host) return 5;
        if (!okA || !okB || ac!=bc || ar.pc!=br.pc || ar.kind!=br.kind ||
            ar.detail!=br.detail || ar.fallback!=br.fallback ||
            expected.r!=actual.r || expected.vfp!=actual.vfp || expected.cpsr!=actual.cpsr ||
            expected.fpscr!=actual.fpscr || memory.ContentFingerprint()!=other.ContentFingerprint() ||
            memory.WriteGeneration()!=other.WriteGeneration() ||
            memory.WriteTraceFingerprint()!=other.WriteTraceFingerprint()) {
            std::printf("mismatch test=%u accepted=%llu blocks=%u/%u fpscr=%08x/%08x\n",
                test,acceptedInvocations-hits,ac,bc,expected.fpscr,actual.fpscr);
            for(unsigned i=0;i<16;++i) if(expected.r[i]!=actual.r[i])
                std::printf("r%u %08x/%08x\n",i,expected.r[i],actual.r[i]);
            for(unsigned i=0;i<32;++i) if(expected.vfp[i]!=actual.vfp[i])
                std::printf("s%u %08x/%08x\n",i,expected.vfp[i],actual.vfp[i]);
            return 6;
        }
        if (acceptedInvocations!=hits) ++fast; else ++rejected;
    }
    std::printf("skeleton differential: fast=%u fallback=%u\n",fast,rejected);
    if (fast<=400) return 7;
    fast=0; rejected=0;
    for (unsigned test=0;test<512;++test) {
        auto memory=initial;
        memory.Write16(0x08000002,0xE002); memory.Write16(0x08000004,0x6003);
        memory.Write16(0x08000006,0xF004); // third index must NOT lose the high bits
        const uint32_t base=0x09000000;
        // A separate vertex region exercises the differing native index masks.
        if (!memory.MapRegion({"vertices",base,0x60010,true,false,{}})) return 8;
        for (unsigned vertex : {2U,3U,0xF004U}) for (unsigned axis=0;axis<3;++axis)
            memory.Write16(base+vertex*6+axis*2,static_cast<uint16_t>(random()));
        a32::GuestState expected{};
        for(auto& r:expected.r) r=random();
        for(auto& s:expected.vfp) s=random();
        expected.r[0]=0x08000000;expected.r[1]=base;expected.r[2]=0x08004000;
        expected.r[14]=0x123400;expected.r[15]=0x2BFCB4;
        expected.cpsr=random();expected.fpscr=(test%4)*0x01000000U;
        if(test%19==0) expected.fpscr|=0x00400000U;
        auto actual=expected;
        memory.EnableWriteTraceFingerprint(true);
        auto other=memory;
        Oot3dWholeAotStats a{},b{};a32::ExecutionResult ar{},br{};uint32_t ac=0,bc=0;
        auto hits=acceptedInvocations;
        auto okA=original->Execute(0x2BFCB4,expected,memory,&ar,&a,nullptr,100000,&ac,
            nullptr,nullptr,nullptr,0,nullptr,true,0x123400);
        auto okB=Triangle(0x2BFCB4,actual,other,&br,&b,nullptr,100000,&bc,
            nullptr,nullptr,nullptr,0,nullptr,true,0x123400);
        if(!okA || !okB || ac!=bc || ar.pc!=br.pc || ar.kind!=br.kind ||
            expected.r!=actual.r || expected.vfp!=actual.vfp || expected.fpscr!=actual.fpscr ||
            expected.cpsr!=actual.cpsr || memory.ContentFingerprint()!=other.ContentFingerprint() ||
            memory.WriteGeneration()!=other.WriteGeneration() ||
            memory.WriteTraceFingerprint()!=other.WriteTraceFingerprint()) {
            std::printf("triangle mismatch %u\n",test);return 9;
        }
        if(hits!=acceptedInvocations) ++fast;else ++rejected;
    }
    std::printf("triangle differential: fast=%u fallback=%u\n",fast,rejected);
    return fast>400?0:10;
}
