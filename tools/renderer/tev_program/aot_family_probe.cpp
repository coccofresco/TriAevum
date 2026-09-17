// Developer-only whole-family experiment. Generated title code remains private.
#define TRIAEVUM_GEOMETRY_KERNEL_TEST
#include "oot3d_cpu_geometry_kernel_probe.cpp"
#include <atomic>

namespace {
uint64_t familyHits=0;
Oot3dWholeAotFlow FamilyTriangle(Oot3dWholeAotFrame& frame,Oot3dWholeAotContext& context,
    Oot3dAotArchitecturalState& state,uint32_t pc) {
    state.Flush(0x7FFFU,true);
    const auto returnPc=frame.Guest.r[14];
    a32::ExecutionResult result{};Oot3dWholeAotStats stats{};uint32_t used=0;
    bool ok=Triangle(pc,frame.Guest,context.Memory,&result,&stats,context.ExternalCall,
        context.BlocksRemaining,&used,nullptr,nullptr,nullptr,0,nullptr,true,returnPc);
    state.Reload(0x7FFFU,true);
    context.BlocksConsumed+=used;context.BlocksRemaining-=used;
    context.Stats.MemoryFaults+=stats.MemoryFaults;
    context.Stats.BlockLimitExits+=stats.BlockLimitExits;
    if (!ok) return Oot3dAotBlockLimit(context,pc);
    switch(result.kind) {
    case a32::ExitKind::Branch: return result.pc==returnPc?Oot3dAotReturned(result.pc):Oot3dAotBranch(result.pc);
    case a32::ExitKind::MemoryFault: return {Oot3dWholeAotFlowKind::MemoryFault,result.pc,result.detail};
    case a32::ExitKind::BlockLimit: return {Oot3dWholeAotFlowKind::BlockLimit,result.pc,0};
    default: return {Oot3dWholeAotFlowKind::Unsupported,result.pc,result.detail};
    }
}
#include "family_generated.inc"

bool FamilyExecute(uint32_t pc,a32::GuestState& guest,NativeA32Memory& memory,
    a32::ExecutionResult* result,Oot3dWholeAotStats* stats,
    Oot3dWholeAotExternalCall external,uint32_t budget,uint32_t* consumed,
    a32::BlockEntryCallback callback,void* user,const uint32_t* pcs,size_t count,
    const Oot3dAotBlockEntryFilter* filter,bool skipFirst,uint32_t stop) {
    if (!FamilySupportsRoot(pc) || stop!=guest.r[14] || !result || !stats || !budget ||
        FamilyObserved(pc,&stop,1) ||
        (callback && (!count || FamilyObserved(pc,pcs,count))))
        return original->Execute(pc,guest,memory,result,stats,external,budget,consumed,
            callback,user,pcs,count,filter,skipFirst,stop);
    Oot3dWholeAotFrame frame(guest);Oot3dAotArchitecturalState state(guest);
    Oot3dWholeAotContext context{memory,memory,*stats,external,
        kFamilyOptimized?nullptr:callback,user,pcs,count,filter,skipFirst,budget,0};
    Oot3dWholeAotFlow flow;
    try {
        Oot3dWholeAotExecutionScope scope;
        flow=RunFamily(frame,context,state,pc);
    } catch(const Oot3dWholeAotObservableExit& exit) {
        state.Flush(0x7FFFU,true);
        Oot3dWholeAotTakeObservableExitSnapshot(exit.Pc,&guest);
        state.Reload(0x7FFFU,true);
        flow=Oot3dAotBranch(exit.Pc);
    }
    state.Flush(0x7FFFU,true);guest.r[15]=flow.Pc;
    ++familyHits;++stats->Calls;
    if(consumed) *consumed=context.BlocksConsumed?context.BlocksConsumed:1;
    switch(flow.Kind) {
    case Oot3dWholeAotFlowKind::Returned:
    case Oot3dWholeAotFlowKind::Branch:
        *result={a32::ExitKind::Branch,flow.Pc,a32::FallbackReason::None,0};break;
    case Oot3dWholeAotFlowKind::MemoryFault:
        *result={a32::ExitKind::MemoryFault,flow.Pc,a32::FallbackReason::None,flow.Detail};break;
    case Oot3dWholeAotFlowKind::BlockLimit:
        *result={a32::ExitKind::BlockLimit,flow.Pc,a32::FallbackReason::None,context.BlocksConsumed};break;
    case Oot3dWholeAotFlowKind::Svc:
        *result={a32::ExitKind::Svc,flow.Pc,a32::FallbackReason::None,flow.Detail};break;
    default:
        ++stats->UnsupportedExits;
        *result={a32::ExitKind::Unsupported,flow.Pc,a32::FallbackReason::Unsupported,flow.Detail};break;
    }
    return true;
}
}
extern "C" __declspec(dllexport) uint64_t triaevum_invocation_candidate_hits() noexcept { return familyHits; }
extern "C" __declspec(dllexport) uint64_t triaevum_invocation_native_leaf_hits() noexcept { return acceptedInvocations; }
extern "C" __declspec(dllexport) uint32_t triaevum_invocation_coverage_entry(uint32_t index) noexcept {
    return index<std::size(kFamilyEntries)?kFamilyEntries[index]:0;
}
extern "C" __declspec(dllexport) uint64_t triaevum_invocation_coverage_hits(uint32_t index) noexcept {
    return index<std::size(familyVisits)?familyVisits[index]:0;
}
extern "C" __declspec(dllexport) const Oot3dWholeAotProgramV2*
triaevum_title_whole_aot_query(uint32_t abi) noexcept {
    if(abi!=kOot3dWholeAotPluginAbiV2) return nullptr;
    static Oot3dWholeAotProgramV2 program{};
    if(!program.Execute) {
        auto path=std::getenv("TRIAEVUM_FAMILY_ORACLE");
        if(!path) path=std::getenv("TRIAEVUM_INVOCATION_BASELINE");
        if(!path)return nullptr;
        auto module=LoadLibraryExA(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!module)return nullptr;
        HMODULE self=nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(FamilyExecute),&self);
        if(module==self) return nullptr; // A control module needs a separate frozen oracle.
        using Query=const Oot3dWholeAotProgramV2* (*)(uint32_t) noexcept;
        auto query=reinterpret_cast<Query>(GetProcAddress(module,"triaevum_title_whole_aot_query"));
        original=query?query(abi):nullptr;if(!original || original->StructSize!=sizeof(program))return nullptr;
        program=*original;program.Execute=FamilyExecute;
    }
    return &program;
}
