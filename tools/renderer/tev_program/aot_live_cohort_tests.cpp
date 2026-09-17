#define TRIAEVUM_LIVE_COHORT_TEST
#include "aot_live_cohort_probe.cpp"
#include <iostream>

namespace {
struct Exit { uint32_t Pc; a32::GuestState State; };
void ExitAt(uint32_t pc,const a32::GuestState* state) { throw Exit{pc,*state}; }
bool IsEligible(uint32_t pc,const a32::GuestState*,bool has,const uint32_t* pcs,size_t count) noexcept {
    return pc==0x2000 && (!has || (count && !std::binary_search(pcs,pcs+count,pc)));
}
bool Model(uint32_t pc,a32::GuestState& state,NativeA32Memory& memory,
    a32::ExecutionResult* result,Oot3dWholeAotStats*,Oot3dWholeAotExternalCall,
    uint32_t budget,uint32_t* consumed,a32::BlockEntryCallback callback,void* user,
    const uint32_t* pcs,size_t count,const Oot3dAotBlockEntryFilter*,bool skip,uint32_t stop) {
    uint32_t used=0;
    try {
        while(used<budget) {
            ++used;
            state.r[15]=pc;
            if(callback && !(used==1 && skip) && (!count || std::binary_search(pcs,pcs+count,pc)))
                callback(pc,state,memory,user);
            if(pc==0x1000) { ++state.r[0]; state.r[14]=0x1004; pc=0x2000; }
            else if(pc==0x2000) {
                if(state.r[3]) {
                    *consumed=used;
                    *result={a32::ExitKind::MemoryFault,pc,a32::FallbackReason::None,0xDEAD};
                    return true;
                }
                state.r[0]+=5; pc=state.r[14];
            }
            else if(pc==0x1004) { state.r[0]+=2; pc=0x8000; }
            else throw std::runtime_error("Bad test PC");
            if(pc==stop) break;
        }
    } catch(const Exit& exit) { state=exit.State; pc=exit.Pc; }
    state.r[15]=pc;
    *consumed=used;
    *result={a32::ExitKind::Branch,pc,a32::FallbackReason::None,0};
    return true;
}
void Notify(uint32_t,a32::GuestState& state,a32::MemoryBus&,void*) { ++state.r[2]; }
}
int main() {
    const uint32_t entries[]={0x1000,0x1004,0x2000,0x8000};
    Oot3dWholeAotProgramV2 ref{};ref.Execute=Model;ref.ObservableExit=ExitAt;
    ref.EntryPoints=entries;ref.EntryPointCount=std::size(entries);
    original=&ref;candidate=&ref;eligible=IsEligible;roots={0x2000};
    unsigned cases=0;
    for(uint32_t budget=1;budget<=8;++budget) for(unsigned mode=0;mode<4;++mode)
        for(bool skip:{false,true}) for(bool fault:{false,true}) {
        NativeA32Memory memory;
        a32::GuestState a{},b{};a.r[15]=b.r[15]=0x1000;
        a.r[3]=b.r[3]=fault;
        a32::ExecutionResult ra{},rb{};Oot3dWholeAotStats sa{},sb{};
        uint32_t ca=0,cb=0;
        const uint32_t hook=mode==3?0x2000:0x1004;
        const auto callback=mode?Notify:nullptr;
        const size_t count=mode==2?0:1;
        Model(0x1000,a,memory,&ra,&sa,nullptr,budget,&ca,callback,nullptr,&hook,count,nullptr,skip,0x8000);
        Execute(0x1000,b,memory,&rb,&sb,nullptr,budget,&cb,callback,nullptr,&hook,count,nullptr,skip,0x8000);
        if(a.r!=b.r || ca!=cb || ra.pc!=rb.pc || ra.kind!=rb.kind || ra.detail!=rb.detail) {
            std::cerr << "Mismatch budget=" << budget << " mode=" << mode << " skip=" << skip << '\n';
            return 1;
        }
        ++cases;
    }
    std::cout << cases << " live-routing budget/observer cases matched\n";
}
