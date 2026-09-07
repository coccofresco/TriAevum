#include "oot3d/actor_init_context_owner.h"

#include <bit>
#include <cstdint>

namespace Oot3dSourceActorInitContext {
namespace {

using undefined1 = uint8_t;
using undefined2 = uint16_t;
using undefined4 = uint32_t;
using undefined8 = uint64_t;
using byte = uint8_t;
using uint = uint32_t;
using ulonglong = uint64_t;

class GuestAddress {
  public:
    explicit GuestAddress(uint32_t address) noexcept : mAddress(address) {
    }

    template <typename T>
    operator T*() const {
        return static_cast<T*>(ResolveGuestMemory(mAddress, sizeof(T)));
    }

  private:
    uint32_t mAddress;
};

constexpr uint32_t kAllocatorSourcePath = 0x0044EFB0U;

} // namespace

// Revision-pinned import of FUN_0044e7c0 from producer commit d5c2927.
// Mechanical guest-pointer lowering is represented by GuestAddress. The
// explicit hard-float calls below follow code.bin rather than Ghidra's
// incomplete host prototype.
void Actor_InitContext(
    uint32_t param_1, uint32_t param_2, const int16_t* param_3) {
    const auto& literal = ActiveLiterals();
#define DAT_0044ecac literal.DAT_0044ecac
#define DAT_0044ecb0 literal.DAT_0044ecb0
#define DAT_0044ecb4 literal.DAT_0044ecb4
#define DAT_0044ecc0 literal.DAT_0044ecc0
#define DAT_0044ecc4 literal.DAT_0044ecc4
#define DAT_0044ecc8 literal.DAT_0044ecc8
#define DAT_0044eccc literal.DAT_0044eccc
#define DAT_0044ecd0 literal.DAT_0044ecd0
#define DAT_0044ecd4 literal.DAT_0044ecd4
#define DAT_0044ecd8 literal.DAT_0044ecd8
#define DAT_0044ecdc literal.DAT_0044ecdc
#define DAT_0044efac literal.DAT_0044efac
#define DAT_0044efe8 literal.DAT_0044efe8
#define DAT_0044efec literal.DAT_0044efec
#define DAT_0044eff0 literal.DAT_0044eff0
#define DAT_0044eff4 literal.DAT_0044eff4
#define DAT_0044eff8 literal.DAT_0044eff8
#define DAT_0044effc literal.DAT_0044effc
#define DAT_0044f000 literal.DAT_0044f000
#define DAT_0044f004 literal.DAT_0044f004
#define DAT_0044f008 literal.DAT_0044f008
#define DAT_00463420 literal.DAT_00463420
#define DAT_00463424 literal.DAT_00463424
#define DAT_00463428 literal.DAT_00463428
#define DAT_00463434 literal.DAT_00463434
#define DAT_00463438 literal.DAT_00463438
#define DAT_0046343c literal.DAT_0046343c
#define DAT_00463440 literal.DAT_00463440
#define DAT_00463444 literal.DAT_00463444
#define DAT_00463448 literal.DAT_00463448
#define uintptr_t GuestAddress

    uint* puVar1;
    int32_t iVar2;
    int32_t iVar3;
    int32_t iVar4;
    uint uVar5;
    undefined4 uVar6;
    undefined4 uVar7;
    byte* pbVar8;
    undefined4 uVar9;
    undefined4 uVar10;
    int32_t iVar11;
    int32_t iVar12;
    undefined4* puVar13;
    undefined1* puVar14;
    uint32_t factoryAddress;
    undefined8 uVar16;
    undefined4 uVar17;
    undefined4 uVar18;
    undefined4 uVar19;
    undefined4 local_vector[4];

    puVar1 = (uint*)(uintptr_t)(DAT_0044ecb0);
    iVar2 = static_cast<int32_t>(DAT_0044ecac) +
            *(int16_t*)(uintptr_t)(param_1 + 0x104U) * 0x1c;
    if (((*puVar1 & 1U) == 0U) &&
        ((iVar3 = oot3d_static_init_guard_acquire(DAT_0044ecb0)) != 0)) {
        RendererGlobalState_Init(DAT_0044ecb4);
    }
    FUN_004644a8(DAT_0044ecc0, param_1);
    oot3d_memclear(param_2, 0x20cU);
    FUN_0045fe94();
    iVar3 = static_cast<int32_t>(DAT_0044ecc8);
    iVar11 = 0;
    iVar4 = static_cast<int32_t>(DAT_0044ecc4);
    do {
        *(undefined4*)(uintptr_t)(static_cast<uint32_t>(iVar4) + 0x10U) = 0;
        ++iVar11;
        *(undefined1*)(uintptr_t)(static_cast<uint32_t>(iVar4) + 0x1eU) = 0;
        iVar4 += 0x20;
    } while (iVar11 < iVar3);
    *(undefined4*)(uintptr_t)(param_2 + 0x1acU) =
        *(undefined4*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 0xecU);
    *(undefined4*)(uintptr_t)(param_2 + 0x19cU) =
        *(undefined4*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 0xf0U);
    *(undefined4*)(uintptr_t)(param_2 + 0x1b0U) =
        *(undefined4*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 0xf4U);
    *(undefined4*)(uintptr_t)(param_2 + 0x1b8U) =
        *(undefined4*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 0xf8U);
    *(undefined2*)(uintptr_t)(param_2 + 0x1d6U) = 0;
    *(undefined2*)(uintptr_t)(param_2 + 0x1d2U) = 0;
    *(undefined2*)(uintptr_t)(param_2 + 0x1d4U) = 0;
    *(undefined1*)(uintptr_t)(param_2 + 0x1d1U) = 0;
    *(undefined1*)(uintptr_t)(param_2 + 0x1d0U) = 0;
    *(undefined4*)(uintptr_t)(param_2 + 0x1dcU) = 0;
    Actor_Spawn(
        param_2, param_1, param_3[0], static_cast<float>(param_3[1]),
        static_cast<float>(param_3[2]), static_cast<float>(param_3[3]),
        param_3[4], param_3[5], param_3[6], param_3[7], 1);
    iVar2 = *(int32_t*)(uintptr_t)(param_2 + 0x20U);
    *(undefined4*)(uintptr_t)(param_2 + 0x114U) = 0;
    *(undefined4*)(uintptr_t)(param_2 + 0x110U) = 0;
    *(undefined4*)(uintptr_t)(param_2 + 0xa8U) = 0;
    uVar7 = DAT_0044eccc;
    *(undefined4*)(uintptr_t)(param_2 + 0xa4U) = 0;
    *(undefined1*)(uintptr_t)(param_2 + 0xbbU) = 0;
    *(undefined4*)(uintptr_t)(param_2 + 0xacU) = uVar7;
    *(undefined1*)(uintptr_t)(param_2 + 0xc4U) = 0;
    puVar13 = (undefined4*)(uintptr_t)(param_2 + 0x6cU);
    *(undefined1*)(uintptr_t)(param_2 + 0xbcU) = 1;
    uVar5 = Object_GetIndex(param_1 + 0x3a58U, 1);
    if (((uVar5 & 0xffU) < 0x13U) &&
        ((iVar3 = static_cast<int32_t>(
              param_1 + (uVar5 & 0xffU) * 0x80U)),
         *(int32_t*)(uintptr_t)(
             DAT_0044ecd0 + static_cast<uint32_t>(iVar3)) != 0)) {
        iVar3 += 0x3a5c;
    } else {
        iVar3 = 0;
    }
    iVar3 += 0x10;
    *(int32_t*)(uintptr_t)(param_2 + 0x18cU) = iVar3;
    uVar6 = ZAR_GetCMBByIndex(static_cast<uint32_t>(iVar3), 0x2dU);
    *(undefined4*)(uintptr_t)(param_2 + 0x184U) = uVar6;
    if (((*puVar1 & 1U) == 0U) &&
        ((iVar4 = oot3d_static_init_guard_acquire(DAT_0044ecb0)) != 0)) {
        RendererGlobalState_Init(DAT_0044ecb4);
    }
    uVar6 = RendererFactoryCreate(
        RendererFactoryAddress(DAT_0044ecb4),
        *(undefined4*)(uintptr_t)(param_2 + 0x184U), 0);
    *(undefined4*)(uintptr_t)(param_2 + 0x11cU) = uVar6;
    uVar6 = ZAR_GetCMBByIndex(static_cast<uint32_t>(iVar3), 0x2eU);
    if (((*puVar1 & 1U) == 0U) &&
        ((iVar4 = oot3d_static_init_guard_acquire(DAT_0044ecb0)) != 0)) {
        RendererGlobalState_Init(DAT_0044ecb4);
    }
    uVar6 = RendererFactoryCreate(
        RendererFactoryAddress(DAT_0044ecb4), uVar6, 0);
    *(undefined4*)(uintptr_t)(param_2 + 0x150U) = uVar6;
    oot3d_store_child14_byte16(
        *(undefined4*)(uintptr_t)(param_2 + 0x11cU), 2);
    *(undefined1*)(uintptr_t)(
        *(int32_t*)(uintptr_t)(
            *(int32_t*)(uintptr_t)(param_2 + 0x11cU) + 0xcU) +
        0x10U) = 1;
    oot3d_store_child14_byte16(
        *(undefined4*)(uintptr_t)(param_2 + 0x150U), 2);
    *(undefined1*)(uintptr_t)(
        *(int32_t*)(uintptr_t)(
            *(int32_t*)(uintptr_t)(param_2 + 0x150U) + 0xcU) +
        0x10U) = 1;
    uVar6 = ZAR_GetCMBByIndex(static_cast<uint32_t>(iVar3), 0x2bU);
    *(undefined4*)(uintptr_t)(param_2 + 0x188U) = uVar6;
    local_vector[3] =
        ZAR_GetCMBByIndex(static_cast<uint32_t>(iVar3), 0x2cU);
    uVar16 =
        (static_cast<uint64_t>(DAT_0044ecd4) << 32U) | uVar7;
    iVar3 = 0;
    do {
        if (((*puVar1 & 1U) == 0U) &&
            ((iVar4 = oot3d_static_init_guard_acquire(
                  DAT_0044ecb0)) != 0)) {
            RendererGlobalState_Init(DAT_0044ecb4);
        }
        uVar7 = RendererFactoryCreate(
            RendererFactoryAddress(DAT_0044ecb4),
            *(undefined4*)(uintptr_t)(param_2 + 0x188U), 0);
        puVar13[iVar3 + 0x2d] = uVar7;
        oot3d_store_child14_byte16(uVar7, 2);
        *(undefined1*)(uintptr_t)(
            *(int32_t*)(uintptr_t)(
                puVar13[iVar3 + 0x2d] + 0xcU) +
            0x10U) = 1;
        *(int32_t*)(uintptr_t)(
            *(int32_t*)(uintptr_t)(
                puVar13[iVar3 + 0x2d] + 0xcU) +
            0xcU) = static_cast<int32_t>(uVar16 >> 32U);
        if (((*puVar1 & 1U) == 0U) &&
            ((iVar4 = oot3d_static_init_guard_acquire(
                  DAT_0044ecb0)) != 0)) {
            RendererGlobalState_Init(DAT_0044ecb4);
        }
        uVar7 = RendererFactoryCreate(
            RendererFactoryAddress(DAT_0044ecb4), local_vector[3], 0);
        puVar13[iVar3 + 0x3a] = uVar7;
        oot3d_store_child14_byte16(uVar7, 2);
        iVar4 = iVar3 + 1;
        *(undefined1*)(uintptr_t)(
            *(int32_t*)(uintptr_t)(
                puVar13[iVar3 + 0x3a] + 0xcU) +
            0x10U) = 1;
        *(int32_t*)(uintptr_t)(
            *(int32_t*)(uintptr_t)(
                puVar13[iVar3 + 0x3a] + 0xcU) +
            0xcU) = static_cast<int32_t>(uVar16 >> 32U);
        iVar3 = iVar4;
    } while (iVar4 < 0xc);
    *(undefined4*)(uintptr_t)(param_2 + 400U) = 0xffffffffU;
    *(undefined4*)(uintptr_t)(param_2 + 0x194U) = 0xffffffffU;
    iVar3 = static_cast<int32_t>(DAT_0044ecd8);
    if (iVar2 != 0) {
        pbVar8 = (byte*)(uintptr_t)(
            DAT_0044ecd8 +
            static_cast<uint32_t>(
                *(byte*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 2U)) *
                8U);
        *puVar13 =
            *(undefined4*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 0x3cU);
        *(float*)(uintptr_t)(param_2 + 0x70U) =
            *(float*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 0x40U) +
            *(float*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 0x50U) *
                *(float*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 0x58U);
        *(undefined4*)(uintptr_t)(param_2 + 0x74U) =
            *(undefined4*)(uintptr_t)(
                static_cast<uint32_t>(iVar2) + 0x44U);
        for (uint32_t index = 0; index < 8U; ++index) {
            *(float*)(uintptr_t)(param_2 + 0x84U + index * 4U) =
                static_cast<float>(pbVar8[index]);
        }
    }
    puVar14 = (undefined1*)(uintptr_t)(
        static_cast<uint32_t>(iVar3) +
        static_cast<uint32_t>(
            *(byte*)(uintptr_t)(static_cast<uint32_t>(iVar2) + 2U)) *
            8U);
    uVar7 = *(undefined4*)(uintptr_t)(param_1 + 0x1bcU);
    uVar6 = *(undefined4*)(uintptr_t)(param_1 + 0x1c0U);
    *(undefined4*)(uintptr_t)(param_2 + 0x78U) =
        *(undefined4*)(uintptr_t)(param_1 + 0x1b8U);
    *(undefined4*)(uintptr_t)(param_2 + 0x7cU) = uVar7;
    *(undefined4*)(uintptr_t)(param_2 + 0x80U) = uVar6;
    iVar2 = static_cast<int32_t>(DAT_0044ecdc);
    uVar7 = *(undefined4*)(uintptr_t)(DAT_0044ecdc + 4U);
    *(undefined4*)(uintptr_t)(param_2 + 0xb0U) = uVar7;
    *(undefined4*)(uintptr_t)(param_2 + 0xb4U) = uVar7;
    *(int8_t*)(uintptr_t)(param_2 + 0xc5U) =
        static_cast<int8_t>(
            *(undefined4*)(uintptr_t)(
                static_cast<uint32_t>(iVar2) + 0x18U));
    *(undefined1*)(uintptr_t)(param_2 + 0xbdU) = 0;
    *(undefined2*)(uintptr_t)(param_2 + 0xb8U) = 0x100U;
    uVar7 = *(undefined4*)(uintptr_t)(
        static_cast<uint32_t>(iVar2) + 0x30U);
    iVar3 = static_cast<int32_t>(param_2 + 200U);
    iVar4 = 0;
    do {
        uVar9 = static_cast<undefined4>(uVar16);
        uVar6 = *(undefined4*)(uintptr_t)(param_2 + 0xb0U);
        iVar12 = iVar4 + 1;
        puVar13[iVar4 * 6 + 0x17] = uVar9;
        puVar13[iVar4 * 6 + 0x18] = uVar9;
        puVar13[iVar4 * 6 + 0x19] = uVar9;
        puVar13[iVar4 * 6 + 0x1c] = uVar7;
        puVar13[iVar4 * 6 + 0x1a] = uVar6;
        *(undefined1*)(uintptr_t)(
            static_cast<uint32_t>(iVar3) + 0x10U) = *puVar14;
        *(undefined1*)(uintptr_t)(
            static_cast<uint32_t>(iVar3) + 0x11U) = puVar14[1];
        *(undefined1*)(uintptr_t)(
            static_cast<uint32_t>(iVar3) + 0x12U) = puVar14[2];
        iVar11 = static_cast<int32_t>(DAT_0044ecac);
        iVar3 += 0x18;
        iVar4 = iVar12;
    } while (iVar12 < 3);
    if (*(int32_t*)(uintptr_t)(DAT_0044ecac + 0xe98U) == 0) {
        *(undefined1*)(uintptr_t)(DAT_0044ecac + 0x153bU) = 0;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x1528U) = uVar9;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x152cU) = uVar9;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x1530U) = uVar9;
    } else {
        *(undefined1*)(uintptr_t)(DAT_0044ecac + 0x153bU) = 0x28U;
        *(float*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x1528U) =
            static_cast<float>(
                *(int32_t*)(uintptr_t)(
                    static_cast<uint32_t>(iVar11) + 0xe7cU));
        *(float*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x152cU) =
            static_cast<float>(
                *(int32_t*)(uintptr_t)(
                    static_cast<uint32_t>(iVar11) + 0xe80U));
        *(float*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x1530U) =
            static_cast<float>(
                *(int32_t*)(uintptr_t)(
                    static_cast<uint32_t>(iVar11) + 0xe84U));
        *(int16_t*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x1534U) =
            static_cast<int16_t>(
                *(undefined4*)(uintptr_t)(
                    static_cast<uint32_t>(iVar11) + 0xe88U));
        *(int16_t*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x1536U) =
            static_cast<int16_t>(
                *(undefined4*)(uintptr_t)(
                    static_cast<uint32_t>(iVar11) + 0xe8cU));
        *(int16_t*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x1538U) =
            static_cast<int16_t>(
                *(undefined4*)(uintptr_t)(
                    static_cast<uint32_t>(iVar11) + 0xe90U));
        *(int8_t*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x153aU) =
            static_cast<int8_t>(
                *(undefined4*)(uintptr_t)(
                    static_cast<uint32_t>(iVar11) + 0xe94U));
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x153cU) =
            *(undefined4*)(uintptr_t)(
                static_cast<uint32_t>(iVar11) + 0xe9cU);
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x1540U) =
            *(undefined4*)(uintptr_t)(
                static_cast<uint32_t>(iVar11) + 0xea0U);
    }
    const float lightX = *(float*)(uintptr_t)(DAT_0044ecac + 0x1528U);
    const float lightY =
        *(float*)(uintptr_t)(DAT_0044ecac + 0x152cU) +
        std::bit_cast<float>(literal.DAT_0044efa8);
    const float lightZ = *(float*)(uintptr_t)(DAT_0044ecac + 0x1530U);
    Lights_PointNoGlowSetInfo(
        DAT_0044efac, lightX, lightY, lightZ,
        0xffU, 0xffU, 0xffU, -1, 0);
    uVar6 = static_cast<undefined4>(uVar16);
    uVar7 = LightContext_InsertLight(
        param_1, param_1 + 0xa70U, DAT_0044efac);
    *(undefined4*)(uintptr_t)(
        static_cast<uint32_t>(iVar2) + 0x90U) = uVar7;
    *(undefined4*)(uintptr_t)(
        static_cast<uint32_t>(iVar2) + 0x94U) = 0;
    *(undefined4*)(uintptr_t)(
        static_cast<uint32_t>(iVar2) + 0x98U) = uVar6;
    uVar5 = Object_GetIndex(param_1 + 0x3a58U, 1);
    if (((uVar5 & 0xffU) < 0x13U) &&
        ((iVar2 = static_cast<int32_t>(
              param_1 + (uVar5 & 0xffU) * 0x80U)),
         *(int32_t*)(uintptr_t)(
             DAT_0044ecd0 + static_cast<uint32_t>(iVar2)) != 0)) {
        iVar2 += 0x3a5c;
    } else {
        iVar2 = 0;
    }
    uVar9 = ZAR_GetCTXBByIndex(static_cast<uint32_t>(iVar2) + 0x10U, 2);
    iVar2 = static_cast<int32_t>(AllocatorAllocate(
        DAT_0044efe8, 0x1b8U, kAllocatorSourcePath, DAT_0044efec));
    uVar7 = 0;
    if (iVar2 != 0) {
        uVar7 = FUN_00348f34(
            static_cast<uint32_t>(iVar2), DAT_0044eff0);
    }
    *(undefined4*)(uintptr_t)(param_1 + 0x226cU) = uVar7;
    FUN_00348be4(uVar7);
    FUN_00348a64(
        *(undefined4*)(uintptr_t)(param_1 + 0x226cU), 0, uVar9,
        DAT_0044eff8, DAT_0044eff8, DAT_0044eff4, DAT_0044eff4);
    iVar2 = static_cast<int32_t>(DAT_0044f008);
    local_vector[0] = DAT_0044effc;
    local_vector[1] = DAT_0044f000;
    iVar3 = 0;
    local_vector[3] = DAT_0044f004;
    local_vector[2] = uVar6;
    do {
        if (((*puVar1 & 1U) == 0U) &&
            ((iVar4 = oot3d_static_init_guard_acquire(
                  DAT_0044ecb0)) != 0)) {
            RendererGlobalState_Init(DAT_0044ecb4);
        }
        iVar4 = static_cast<int32_t>(FUN_0034897c(
            *(undefined4*)(uintptr_t)(
                static_cast<uint32_t>(iVar2) + 0x47cU),
            *(undefined4*)(uintptr_t)(param_1 + 0x226cU), 0, 0));
        iVar11 = static_cast<int32_t>(param_1) + iVar3 * 4;
        *(int32_t*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x2270U) = iVar4;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x170U) = 0;
        FUN_003429c8(
            *(undefined4*)(uintptr_t)(
                static_cast<uint32_t>(iVar11) + 0x2270U),
            1, local_vector);
        ++iVar3;
    } while (iVar3 < 4);
    *(undefined4*)(uintptr_t)(param_1 + 0x2280U) = 0;

    // Target tail fragment 0x004632B4, reached by the branch at 0x0044EFA4.
    uVar5 = Object_GetIndex(param_1 + 0x3a58U, 1);
    if (((uVar5 & 0xffU) < 0x13U) &&
        ((iVar2 = static_cast<int32_t>(
              param_1 + (uVar5 & 0xffU) * 0x80U)),
         *(int32_t*)(uintptr_t)(
             DAT_00463420 + static_cast<uint32_t>(iVar2)) != 0)) {
        iVar2 += 0x3a5c;
    } else {
        iVar2 = 0;
    }
    if (((*(uint32_t*)(uintptr_t)(DAT_00463424) & 1U) == 0U) &&
        ((iVar3 = oot3d_static_init_guard_acquire(
              DAT_00463424)) != 0)) {
        RendererGlobalState_Init(DAT_00463428);
    }
    factoryAddress = RendererFactoryAddress(DAT_00463428);
    iVar3 = 0;
    uVar7 = DAT_0046343c;
    uVar6 = DAT_00463434;
    uVar9 = DAT_00463438;
    uVar17 = DAT_00463440;
    uVar18 = DAT_00463444;
    uVar19 = DAT_00463448;
    do {
        uVar10 = ZAR_GetCMBByIndex(
            static_cast<uint32_t>(iVar2) + 0x10U,
            static_cast<uint32_t>(iVar3) + 0x60U);
        iVar4 = static_cast<int32_t>(
            RendererFactoryCreate(factoryAddress, uVar10, 0));
        iVar11 = static_cast<int32_t>(
            param_1 + 0x208cU + static_cast<uint32_t>(iVar3) * 4U);
        ++iVar3;
        *(int32_t*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x200U) = iVar4;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x24U) = uVar6;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x28U) = uVar9;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x2cU) = uVar7;
        iVar4 = *(int32_t*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x200U);
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x34U) = uVar17;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x38U) = uVar17;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x3cU) = uVar18;
        iVar4 = *(int32_t*)(uintptr_t)(
            static_cast<uint32_t>(iVar11) + 0x200U);
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x40U) = uVar19;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x44U) = uVar19;
        *(undefined4*)(uintptr_t)(
            static_cast<uint32_t>(iVar4) + 0x48U) = uVar19;
    } while (iVar3 < 3);
    uVar6 = oot3d_get_indexed_field_58_entry(
        static_cast<uint32_t>(iVar2) + 0x10U, 0x31U);
    const uint32_t finalModel =
        *(undefined4*)(uintptr_t)(param_1 + 0x2294U);
    const uint32_t finalChild =
        *(undefined4*)(uintptr_t)(finalModel + 0xcU);
    FUN_00372d94(finalChild, uVar6);
    *(undefined1*)(uintptr_t)(finalChild + 0x10U) = 1;
    *(undefined4*)(uintptr_t)(finalChild + 0xcU) = uVar7;

#undef uintptr_t
#undef DAT_00463448
#undef DAT_00463444
#undef DAT_00463440
#undef DAT_0046343c
#undef DAT_00463438
#undef DAT_00463434
#undef DAT_00463428
#undef DAT_00463424
#undef DAT_00463420
#undef DAT_0044f008
#undef DAT_0044f004
#undef DAT_0044f000
#undef DAT_0044effc
#undef DAT_0044eff8
#undef DAT_0044eff4
#undef DAT_0044eff0
#undef DAT_0044efec
#undef DAT_0044efe8
#undef DAT_0044efac
#undef DAT_0044ecdc
#undef DAT_0044ecd8
#undef DAT_0044ecd4
#undef DAT_0044ecd0
#undef DAT_0044eccc
#undef DAT_0044ecc8
#undef DAT_0044ecc4
#undef DAT_0044ecc0
#undef DAT_0044ecb4
#undef DAT_0044ecb0
#undef DAT_0044ecac
}

} // namespace Oot3dSourceActorInitContext
