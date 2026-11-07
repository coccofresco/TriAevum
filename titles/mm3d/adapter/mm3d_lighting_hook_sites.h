#pragma once
#include "mm3d_guest_patches.h"
#include <array>
#include <utility>
namespace Oot3dNativeGame {
struct Mm3dLightingHookSite { uint32_t Pc; std::array<uint32_t, 4> Words; };
// code.bin SHA256 6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d
inline constexpr std::array kMm3dLightingHookSites{
    Mm3dLightingHookSite{0x00110D88U, {0xE320F000U, 0xE320F000U, 0xEB01624CU, 0xE59D2048U}},
    Mm3dLightingHookSite{0x00110DB8U, {0xE320F000U, 0xE320F000U, 0xEB016240U, 0xE59D2048U}},
    Mm3dLightingHookSite{0x00110DF8U, {0xE2850F46U, 0xE320F000U, 0xEB005145U, 0xE2850F46U}},
    Mm3dLightingHookSite{0x0011EF3CU, {0xE5940004U, 0xE58D5000U, 0xE3A02001U, 0xE5B01408U}},
    Mm3dLightingHookSite{0x0011EF58U, {0xE8BD8038U, 0x0061FD58U, 0xE92D4038U, 0xE1A04000U}},
    Mm3dLightingHookSite{0x0011EF88U, {0xE5940004U, 0xE58D5000U, 0xE3A02000U, 0xE5B01408U}},
    Mm3dLightingHookSite{0x0011EFA4U, {0xE8BD8038U, 0x0061FD84U, 0xE92D4038U, 0xE1A04000U}},
    Mm3dLightingHookSite{0x0011EFD4U, {0xE59F1020U, 0xE5940008U, 0xE3A02000U, 0xE58D1000U}},
    Mm3dLightingHookSite{0x0011EFF4U, {0xE8BD8038U, 0x0061FD58U, 0x0061FD84U, 0xE92D4030U}},
    Mm3dLightingHookSite{0x0011F000U, {0xE92D4030U, 0xE24DD00CU, 0xE1A04001U, 0xE1A05000U}},
    Mm3dLightingHookSite{0x00126294U, {0xE92D4070U, 0xE1A05000U, 0xE59F6038U, 0xE3A00000U}},
    Mm3dLightingHookSite{0x001262E0U, {0xE92D4010U, 0xE5902C3CU, 0xE59F4020U, 0xE3520000U}},
    Mm3dLightingHookSite{0x00126998U, {0xE1A0000DU, 0xE320F000U, 0xEB000078U, 0xE5951008U}},
    Mm3dLightingHookSite{0x001269DCU, {0xE320F000U, 0xE320F000U, 0xEA000043U, 0xE59F0148U}},
    Mm3dLightingHookSite{0x00126A64U, {0xE28D0010U, 0xE320F000U, 0xEB000045U, 0xE5951008U}},
    Mm3dLightingHookSite{0x00126AA8U, {0xE320F000U, 0xE320F000U, 0xEB000AD9U, 0xE28730DCU}},
    Mm3dLightingHookSite{0x0012C6A8U, {0xE92D41F0U, 0xE0812081U, 0xE0807101U, 0xE2878A06U}},
    Mm3dLightingHookSite{0x001565ACU, {0xE92D4008U, 0xE59DC008U, 0xE58DC000U, 0xEB000276U}},
    Mm3dLightingHookSite{0x001565BCU, {0xE8BD8008U, 0xE1A03002U, 0xE1A0C001U, 0xE0800103U}},
    Mm3dLightingHookSite{0x00112CA4U, {0xE3A01000U, 0xE5801008U, 0xE580100CU, 0xE5801010U}},
    Mm3dLightingHookSite{0x00179930U, {0xE3520000U, 0x03A00000U, 0x012FFF1EU, 0xE080C101U}},
    Mm3dLightingHookSite{0x00179AD8U, {0xE3A00000U, 0xE5840240U, 0xE5971014U, 0xE0890089U}},
    Mm3dLightingHookSite{0x00191CC8U, {0xE3A00000U, 0xE5840240U, 0xE5971014U, 0xE08A008AU}},
    Mm3dLightingHookSite{0x0012C71CU, {0xE0860104U, 0xE5900578U, 0xE5901000U, 0xE5911030U}},
    Mm3dLightingHookSite{0x0012C730U, {0xE5970014U, 0xE2844001U, 0xE1500004U, 0x8AFFFFF6U}},
    Mm3dLightingHookSite{0x00172D18U, {0xE92D4FF0U, 0xE1A04000U, 0xE2805902U, 0xE280A903U}},
    Mm3dLightingHookSite{0x0015ED08U, {0xE2840B02U, 0xE2800FA2U, 0xE59D1048U, 0xE3A02001U}},
    Mm3dLightingHookSite{0x0016FD3CU, {0xE92D43F0U, 0xE2806902U, 0xE1A05000U, 0xE3A0C000U}},
    Mm3dLightingHookSite{0x0015F0B4U, {0xE1DB02FAU, 0xE3500000U, 0x0A000006U, 0xE20000FFU}},
    Mm3dLightingHookSite{0x00171BE0U, {0xE92D4070U, 0xE1A04000U, 0xE5D0014AU, 0xE59F504CU}},
    Mm3dLightingHookSite{0x0015EF7CU, {0xE5D8000CU, 0xE3A09000U, 0xE3500000U, 0x9A000009U}},
    Mm3dLightingHookSite{0x00171B40U, {0xE52DE004U, 0xE24DD014U, 0xE28DE018U, 0xE89E1001U}},
    Mm3dLightingHookSite{0x0015EF58U, {0xE2841A02U, 0xE28110B0U, 0xE1A00004U, 0xEB00584AU}},
    Mm3dLightingHookSite{0x0015F070U, {0xE5D7082DU, 0xE3500001U, 0x1A000006U, 0xE5D70831U}},
    Mm3dLightingHookSite{0x0015F098U, {0xE5D70832U, 0xE3500000U, 0x0A000003U, 0xE59F041CU}},
    Mm3dLightingHookSite{0x0015F0DCU, {0xE5980000U, 0xE2800C01U, 0xE1D018BEU, 0xE3510000U}},
    Mm3dLightingHookSite{0x00110B7CU, {0xE320F000U, 0xE320F000U, 0xEB0162CFU, 0xE59D2048U}},
    Mm3dLightingHookSite{0x00110BACU, {0xE320F000U, 0xE320F000U, 0xEB00BEA1U, 0xE1A0A000U}},
    Mm3dLightingHookSite{0x00110C8CU, {0xE59F01D8U, 0xE320F000U, 0xEB00361CU, 0xE320F000U}},
    Mm3dLightingHookSite{0x00110CB0U, {0xE320F000U, 0xE320F000U, 0xEB016282U, 0xE59D2048U}},
    // Actor init dispatches (UpdateAll's two paths and spawn-time Actor_Init),
    // Actor_Destroy, and the model vtable destructor. EUR v1.1, re-derived
    // from callback loads +138/+13c and vtable 63bed4; never replace guest code.
    Mm3dLightingHookSite{0x001749C4U, {0xE5942138U, 0xE1A01007U, 0xE1A00004U, 0xE12FFF32U}},
    Mm3dLightingHookSite{0x00174EBCU, {0xE5942138U, 0xE59D1084U, 0xE1A00004U, 0xE12FFF32U}},
    Mm3dLightingHookSite{0x00191200U, {0xE5942138U, 0xE1A01006U, 0xE1A00004U, 0xE12FFF32U}},
    Mm3dLightingHookSite{0x00191238U, {0xE92D47F0U, 0xE1A04000U, 0xE1A07001U, 0xE3A06000U}},
    Mm3dLightingHookSite{0x005EC220U, {0xE92D4010U, 0xE1A04000U, 0xE59F00C0U, 0xE5840000U}},
    Mm3dLightingHookSite{0x0015E9ECU, {0xE92D4FF0U, 0xE1A04000U, 0xE2807B33U, 0xE3A00000U}},
    Mm3dLightingHookSite{0x00182C44U, {0xE12FFF32U, 0xE59400CCU, 0xE3500000U, 0x0A00004FU}},
    Mm3dLightingHookSite{0x00182C48U, {0xE59400CCU, 0xE3500000U, 0x0A00004FU, 0xE5D40004U}},
    Mm3dLightingHookSite{0x00182D90U, {0xE12FFF33U, 0xE5C48125U, 0xE28DD034U, 0xECBD8B04U}},
    Mm3dLightingHookSite{0x001BFB40U, {0xE3520000U, 0xE1A03000U, 0xE3A02000U, 0xE1A00001U}},
    Mm3dLightingHookSite{0x001E7244U, {0xE92D41F0U, 0xE1B04001U, 0xE1A08002U, 0x159F70A8U}},
    Mm3dLightingHookSite{0x001EEAA8U, {0xE92D4FF0U, 0xE3A05004U, 0xE1A07000U, 0xE1A0A002U}},
    Mm3dLightingHookSite{0x001EED64U, {0xE5D12000U, 0xE3520000U, 0x0A000035U, 0xE5D02114U}},
    Mm3dLightingHookSite{0x001EFC7CU, {0xE1A03000U, 0xE52D4004U, 0xE591004CU, 0xE3520000U}},
    Mm3dLightingHookSite{0x001F2294U, {0xE92D0030U, 0xE280C0C4U, 0xEC910A04U, 0xE28010F8U}},
    Mm3dLightingHookSite{0x001FD5E8U, {0xE3510004U, 0xAA000003U, 0xE5D0206CU, 0xE3A03001U}},
    Mm3dLightingHookSite{0x0020A898U, {0xE92D4070U, 0xE1A04001U, 0xE1A05000U, 0xEBFF9065U}},
    Mm3dLightingHookSite{0x0020A8B8U, {0xE92D4070U, 0xE1A04000U, 0xE5900004U, 0xE3520000U}},
    Mm3dLightingHookSite{0x00223EDCU, {0xE92D4010U, 0xE1A04000U, 0xE5900000U, 0xE590100CU}},
    Mm3dLightingHookSite{0x00224104U, {0xE5B02004U, 0xE2823001U, 0xE0802102U, 0xE5803000U}},
    Mm3dLightingHookSite{0x0040650CU, {0xE5D1000CU, 0xED9F2A2EU, 0xEDDF1A2EU, 0xE5C30004U}},
    Mm3dLightingHookSite{0x004648F4U, {0xE3E0C000U, 0xE5C23000U, 0xE1C2C0BAU, 0xE5D13003U}},
    Mm3dLightingHookSite{0x005B51D0U, {0xE92D41F0U, 0xE2525000U, 0xE1A07003U, 0xE1A08001U}},
    Mm3dLightingHookSite{0x005EF054U, {0xE92D4070U, 0xE3520000U, 0xE1A06001U, 0xE1A04000U}},
    Mm3dLightingHookSite{0x005EF094U, {0xE12FFF32U, 0xE2555001U, 0x5AFFFFF6U, 0xE8BD8070U}},
    Mm3dLightingHookSite{0x005EF098U, {0xE2555001U, 0x5AFFFFF6U, 0xE8BD8070U, 0xE5940004U}},
    Mm3dLightingHookSite{0x005EF0CCU, {0xE12FFF32U, 0xE5940004U, 0xE2855004U, 0xE0840100U}},
    Mm3dLightingHookSite{0x005EF0D0U, {0xE5940004U, 0xE2855004U, 0xE0840100U, 0xE2800008U}},
};
inline constexpr std::array kMm3dCompositionOwnerGuards{
    // Both native pooled producers store the exact model in the primary queue.
    std::pair{0x00179960U, 0xE5842578U}, std::pair{0x00179AF0U, 0xE5814578U},
    std::pair{0x00191CD8U, 0xE0850480U}, std::pair{0x00191CE0U, 0xE5814578U},
    // Manager reset clears all eight primary and secondary queue counts.
    std::pair{0x00112CB4U, 0xE5801014U}, std::pair{0x00112CD4U, 0xE5801030U},
    std::pair{0x00112CD8U, 0xE5821178U}, std::pair{0x00112CF4U, 0xE5821194U},
    std::pair{0x00112CF8U, 0xE12FFF1EU},
    // Primitive builder and native descriptor/record caller ABIs.
    std::pair{0x00156F98U, 0xE92D47F0U}, std::pair{0x00156F9CU, 0xE1A06000U},
    std::pair{0x00156FD0U, 0xE5960000U}, std::pair{0x00156FD4U, 0xE5900008U},
    std::pair{0x00167498U, 0xE5801008U}, std::pair{0x0016749CU, 0xE12FFF1EU},
    std::pair{0x00157080U, 0xE5960000U}, std::pair{0x00157088U, 0xEA004102U},
    std::pair{0x005E31D0U, 0xE0895186U}, std::pair{0x005E3208U, 0xEBEDCCE7U},
    std::pair{0x005E32D4U, 0xE0880185U}, std::pair{0x005E32F4U, 0xEBEDCCACU},
    std::pair{0x005E41F4U, 0xE0880185U}, std::pair{0x005E4214U, 0xEBEDC8E4U},
    std::pair{0x005E90E8U, 0xE0895186U}, std::pair{0x005E9120U, 0xEBEDB521U},
    std::pair{0x005E91ECU, 0xE0880185U}, std::pair{0x005E920CU, 0xEBEDB4E6U},
    std::pair{0x005EA1A8U, 0xE0880185U}, std::pair{0x005EA1C8U, 0xEBEDB0F7U},
    std::pair{0x001323F4U, 0xEB00906CU}, std::pair{0x001413BCU, 0xEB00547AU},
    std::pair{0x0011F034U, 0xE5950008U}, std::pair{0x0011F038U, 0xE28DD00CU},
    std::pair{0x0011F03CU, 0xE1A01004U}, std::pair{0x0011F040U, 0xE8BD4030U},
    std::pair{0x0011F044U, 0xEA04142EU},
    std::pair{0x0039F068U, 0xE92D4070U}, std::pair{0x0039F06CU, 0xE1A04000U},
    std::pair{0x004C03F4U, 0xE92D4FF0U}, std::pair{0x004C03F8U, 0xE3A09000U},
    std::pair{0x0063BED4U, 0x005EC220U},
    std::pair{0x0067C9E8U, 0x00060087U}, std::pair{0x0067CA04U, 0x0039F068U},
    std::pair{0x00689C3CU, 0x00070041U}, std::pair{0x00689C58U, 0x004C03F4U}};
// Observers decline replacement: the original instruction always executes.
// Also exported by --block-boundaries; adding a runtime site alone does not
// regenerate an already-built registry.
inline void RegisterMm3dLightingHooks(Mm3dGuestPatches& patches) {
    for (const auto& hook : kMm3dLightingHookSites) {
        Mm3dGuestPatchSite site;
        site.Pc = hook.Pc; site.Name = "lighting-observer-" + std::to_string(hook.Pc);
        for (uint32_t i = 0; i < hook.Words.size(); ++i) site.Guard.emplace_back(hook.Pc + i * 4U, hook.Words[i]);
        site.Handler = [](auto&, auto&) -> std::optional<uint32_t> { return std::nullopt; };
        patches.Add(std::move(site));
    }
}
} // namespace Oot3dNativeGame
