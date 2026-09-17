// Developer-only, title-derived CPU projection replacement experiment.
// Evidence: code.bin 0033B11C..0033B1C0 and decomp z_genuine_cohort_20.c.
// This is ARM binary32 math, NOT PICA vertex arithmetic. No release registration.
#include "triaevum_title_whole_aot_abi.h"
#include "oot3d_native_a32_memory.h"
#include <windows.h>
#include <immintrin.h>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>

#pragma float_control(precise, on)
#pragma float_control(except, on)
#pragma fp_contract(off)

using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;
namespace {
const Oot3dWholeAotProgramV2* original;
uint64_t acceptedInvocations = 0;
constexpr uint32_t kEntry = 0x0033B11C;
// Normal/zero inputs only; unsupported arithmetic falls back before guest writes.
bool NormalOrZero(uint32_t bits) {
    auto exponent = bits & 0x7F800000U;
    return exponent != 0x7F800000U && (exponent || !(bits & 0x7FFFFFFFU));
}
bool Project(const std::array<uint32_t, 16>& matrix,
             const std::array<uint32_t, 4>& point, uint32_t fpscr,
             std::array<uint32_t, 4>& output, uint32_t& flags) {
    if (fpscr & 0x00F79F00U) return false; // scalar, traps off, round-to-nearest
    for (auto x : matrix) if (!NormalOrZero(x)) return false;
    for (auto x : point) if (!NormalOrZero(x)) return false;
    const unsigned saved = _mm_getcsr();
    _mm_setcsr(0x1F80U); // isolated nearest/even, masked exceptions, no DAZ/FTZ
    auto column = [&](unsigned i) {
        return _mm_set_ps(std::bit_cast<float>(matrix[12+i]),
                          std::bit_cast<float>(matrix[8+i]),
                          std::bit_cast<float>(matrix[4+i]),
                          std::bit_cast<float>(matrix[i]));
    };
    __m128 sum = _mm_mul_ps(column(0), _mm_set1_ps(std::bit_cast<float>(point[0])));
    for (unsigned i = 1; i < 4; ++i) {
        __m128 product = _mm_mul_ps(column(i), _mm_set1_ps(std::bit_cast<float>(point[i])));
        sum = _mm_add_ps(sum, product); // VMLA rounds product then addition; no FMA.
    }
    _mm_storeu_si128(reinterpret_cast<__m128i*>(output.data()), _mm_castps_si128(sum));
    const unsigned raised = _mm_getcsr() & 0x3FU;
    _mm_setcsr(saved);
    if (raised & 0x1FU) return false; // preserve soft path for invalid/denormal/overflow/underflow
    for (auto x : output) if (!NormalOrZero(x)) return false;
    flags = (raised & 0x20U) ? 0x10U : 0U; // SSE precision -> ARM cumulative IXC
    return true;
}

bool Execute(uint32_t pc, a32::GuestState& state, NativeA32Memory& memory,
    a32::ExecutionResult* result, Oot3dWholeAotStats* stats,
    Oot3dWholeAotExternalCall external, uint32_t budget, uint32_t* consumed,
    a32::BlockEntryCallback callback, void* user, const uint32_t* pcs, size_t count,
    const Oot3dAotBlockEntryFilter* filter, bool skipFirst, uint32_t stop) {
    auto fallback = [&] { return original->Execute(pc,state,memory,result,stats,
        external,budget,consumed,callback,user,pcs,count,filter,skipFirst,stop); };
    // Diagnostic replacement currently qualifies isolated invocations only.
    if (pc != kEntry || stop != state.r[14] || !budget || !result || !stats ||
        (callback && !skipFirst)) return fallback();
    const uint8_t* m = memory.GetReadPointer(state.r[0], 64);
    const uint8_t* v = memory.GetReadPointer(state.r[1], 12);
    const uint8_t* w = memory.GetReadPointer(0x0033B1C4U, 4);
    if (!m || !v || !w || !memory.IsWritable(state.r[2], 12) ||
        !memory.IsWritable(state.r[3], 4)) return fallback();
    std::array<uint32_t,16> matrix;
    std::array<uint32_t,4> point, projected;
    std::memcpy(matrix.data(),m,64);
    std::memcpy(point.data(),v,12);
    std::memcpy(&point[3],w,4); // Native literal, never assumed to equal one.
    uint32_t raised = 0;
    if (!Project(matrix,point,state.fpscr,projected,raised)) return fallback();
    ++acceptedInvocations;
    // Original loads complete before its stores, so aliases retain native order.
    memory.WriteFast<uint32_t>(state.r[2], projected[0]);
    memory.WriteFast<uint32_t>(state.r[2]+4, projected[1]);
    memory.WriteFast<uint32_t>(state.r[2]+8, projected[2] ^ 0x80000000U);
    memory.WriteFast<uint32_t>(state.r[3], projected[3]);
    state.vfp[0]=projected[3]; state.vfp[1]=projected[2]^0x80000000U;
    state.vfp[2]=point[2]; state.vfp[3]=point[3]; state.vfp[4]=projected[2];
    state.vfp[5]=projected[0]; state.vfp[6]=projected[1]; state.vfp[7]=matrix[13];
    state.vfp[8]=matrix[14]; state.vfp[9]=matrix[15]; state.fpscr |= raised;
    state.r[15]=state.r[14];
    *result={a32::ExitKind::Branch,state.r[14],a32::FallbackReason::None,0};
    ++stats->Calls;
    if (consumed) *consumed=1;
    return true;
}
}
#if !defined(TRIAEVUM_PROJECTION_KERNEL_TEST)
extern "C" __declspec(dllexport) uint64_t triaevum_invocation_candidate_hits() noexcept {
    return acceptedInvocations;
}
extern "C" __declspec(dllexport) const Oot3dWholeAotProgramV2*
triaevum_title_whole_aot_query(uint32_t abi) noexcept {
    if (abi != kOot3dWholeAotPluginAbiV2) return nullptr;
    static Oot3dWholeAotProgramV2 program{};
    if (!program.Execute) {
        const char* path=std::getenv("TRIAEVUM_INVOCATION_BASELINE");
        if (!path) return nullptr;
        auto module=LoadLibraryExA(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) return nullptr;
        using Query=const Oot3dWholeAotProgramV2* (*)(uint32_t) noexcept;
        auto query=reinterpret_cast<Query>(GetProcAddress(module,"triaevum_title_whole_aot_query"));
        original=query ? query(abi) : nullptr;
        if (!original || original->StructSize != sizeof(program)) return nullptr;
        program=*original; program.Execute=Execute;
    }
    return &program;
}
#endif
