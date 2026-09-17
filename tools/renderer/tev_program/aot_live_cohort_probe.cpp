// Developer-only actual-call-chain adapter. No private replay or guest-time changes.
#include "triaevum_title_whole_aot_abi.h"
#include "oot3d_native_a32_memory.h"
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace Oot3dNativeGame;
namespace a32=oot3d::recomp::a32;
namespace {
const Oot3dWholeAotProgramV2* original=nullptr;
const Oot3dWholeAotProgramV2* candidate=nullptr;
using Eligible=bool (*)(uint32_t,const a32::GuestState*,bool,const uint32_t*,size_t) noexcept;
Eligible eligible=nullptr;
std::vector<uint32_t> roots;
uint64_t calls=0, handoffs=0, refunds=0, candidateBlocks=0;
struct Report {
    ~Report() {
        if(const char* path=std::getenv("TRIAEVUM_LIVE_REPORT"))
            std::ofstream(path) << "{\"candidate_calls\":" << calls
                << ",\"handoffs\":" << handoffs << ",\"refunded_blocks\":" << refunds
                << ",\"candidate_blocks\":" << candidateBlocks << "}\n";
    }
} report;
struct Route {
    a32::BlockEntryCallback Callback;
    void* User;
    const uint32_t* Pcs;
    size_t Count;
    bool Intercepted=false;
};
bool CanEnter(uint32_t pc,const a32::GuestState& state,const Route& route) {
    return std::binary_search(roots.begin(),roots.end(),pc) &&
        std::binary_search(original->EntryPoints,original->EntryPoints+original->EntryPointCount,state.r[14]) &&
        eligible(pc,&state,route.Callback!=nullptr,route.Pcs,route.Count);
}
void Observe(uint32_t pc,a32::GuestState& state,a32::MemoryBus& memory,void* user) {
    auto& route=*static_cast<Route*>(user);
    if(route.Callback && (!route.Count || std::binary_search(route.Pcs,route.Pcs+route.Count,pc)))
        route.Callback(pc,state,memory,route.User);
    if(CanEnter(pc,state,route)) {
        route.Intercepted=true;
        original->ObservableExit(pc,&state);
        throw std::runtime_error("ObservableExit unexpectedly returned");
    }
}
bool Execute(uint32_t pc,a32::GuestState& state,NativeA32Memory& memory,
    a32::ExecutionResult* result,Oot3dWholeAotStats* stats,Oot3dWholeAotExternalCall external,
    uint32_t budget,uint32_t* consumed,a32::BlockEntryCallback callback,void* user,
    const uint32_t* pcs,size_t count,const Oot3dAotBlockEntryFilter* filter,bool skipFirst,uint32_t stop) {
    if(!result || !stats || !budget) return false;
    Route route{callback,user,pcs,count};
    // A full observer remains fully observed; no interception can be eligible.
    if(callback && !count) return original->Execute(pc,state,memory,result,stats,external,
        budget,consumed,callback,user,pcs,count,filter,skipFirst,stop);
    std::vector<uint32_t> hooks=roots;
    if(count) hooks.insert(hooks.end(),pcs,pcs+count);
    std::sort(hooks.begin(),hooks.end());
    hooks.erase(std::unique(hooks.begin(),hooks.end()),hooks.end());
    Oot3dAotBlockEntryFilter merged;
    for(auto hook:hooks) merged.Insert(hook);
    uint32_t total=0;
    if(consumed) *consumed=0;
    while(total<budget) {
        uint32_t used=0;
        bool executed=false;
        route.Intercepted=false;
        const bool native=CanEnter(pc,state,route);
        if(native) {
            ++calls;
            executed=candidate->Execute(pc,state,memory,result,stats,external,budget-total,&used,
                callback,user,pcs,count,filter,skipFirst,state.r[14]);
            candidateBlocks+=used;
        } else {
            executed=original->Execute(pc,state,memory,result,stats,external,budget-total,&used,
                Observe,&route,hooks.data(),hooks.size(),&merged,skipFirst,stop);
        }
        if(!executed) {
            if(total) throw std::runtime_error("Live cohort lost a registered continuation");
            return false;
        }
        if(used>budget-total) throw std::runtime_error("Live cohort exceeded block budget");
        if(route.Intercepted) {
            // EnterBlock charges before notifying; the intercepted block has not executed.
            if(!used) throw std::runtime_error("Missing intercepted block charge");
            --used; ++refunds; ++handoffs;
        }
        total+=used;
        if(consumed) *consumed=total;
        if(!native && !route.Intercepted) return true;
        if(result->kind!=a32::ExitKind::Branch || (!route.Intercepted && result->pc==stop)) return true;
        pc=result->pc;
        skipFirst=false;
    }
    return true;
}
const Oot3dWholeAotProgramV2* Load(const char* path,HMODULE* module) {
    if(!path) return nullptr;
    *module=LoadLibraryExA(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!*module) return nullptr;
    using Query=const Oot3dWholeAotProgramV2* (*)(uint32_t) noexcept;
    auto query=reinterpret_cast<Query>(GetProcAddress(*module,"triaevum_title_whole_aot_query"));
    auto p=query?query(kOot3dWholeAotPluginAbiV2):nullptr;
    return p && p->StructSize==sizeof(*p) && p->Execute && p->ObservableExit && p->EntryPoints?p:nullptr;
}
}
#ifndef TRIAEVUM_LIVE_COHORT_TEST
extern "C" __declspec(dllexport) const Oot3dWholeAotProgramV2*
triaevum_title_whole_aot_query(uint32_t abi) noexcept {
    if(abi!=kOot3dWholeAotPluginAbiV2) return nullptr;
    static Oot3dWholeAotProgramV2 proxy{};
    if(!proxy.Execute) {
        HMODULE a=nullptr,b=nullptr;
        original=Load(std::getenv("TRIAEVUM_INVOCATION_BASELINE"),&a);
        candidate=Load(std::getenv("TRIAEVUM_LIVE_CANDIDATE"),&b);
        if(!original || !candidate || a==b) return nullptr;
        eligible=reinterpret_cast<Eligible>(GetProcAddress(b,"triaevum_family_eligible"));
        const char* list=std::getenv("TRIAEVUM_LIVE_ROOTS");
        if(!eligible || !list) return nullptr;
        std::istringstream stream(list); std::string value;
        while(std::getline(stream,value,',')) {
            const uint32_t pc=static_cast<uint32_t>(std::strtoul(value.c_str(),nullptr,16));
            if(!pc || pc%4) return nullptr;
            roots.push_back(pc);
        }
        std::sort(roots.begin(),roots.end());
        roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
        if(roots.empty() || !std::is_sorted(original->EntryPoints,original->EntryPoints+original->EntryPointCount))
            return nullptr;
        proxy=*original; proxy.Execute=Execute;
    }
    return &proxy;
}
#endif
