#pragma once
#include "mm3d_guest_patches.h"
#include <array>
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
    Mm3dLightingHookSite{0x00126294U, {0xE92D4070U, 0xE1A05000U, 0xE59F6038U, 0xE3A00000U}},
    Mm3dLightingHookSite{0x001262E0U, {0xE92D4010U, 0xE5902C3CU, 0xE59F4020U, 0xE3520000U}},
    Mm3dLightingHookSite{0x00126998U, {0xE1A0000DU, 0xE320F000U, 0xEB000078U, 0xE5951008U}},
    Mm3dLightingHookSite{0x001269DCU, {0xE320F000U, 0xE320F000U, 0xEA000043U, 0xE59F0148U}},
    Mm3dLightingHookSite{0x00126A64U, {0xE28D0010U, 0xE320F000U, 0xEB000045U, 0xE5951008U}},
    Mm3dLightingHookSite{0x00126AA8U, {0xE320F000U, 0xE320F000U, 0xEB000AD9U, 0xE28730DCU}},
    // Owner setup 0x00135000 returns here from flushing the layout renderer's
    // pending quad batch (bl 0x00144B70 when +0xC3C or +0xC40 is set).
    Mm3dLightingHookSite{0x00135044U, {0xE1A01000U, 0xE5D40D46U, 0xEDDF0A1AU, 0xED9F0A1AU}},
    Mm3dLightingHookSite{0x0012C6A8U, {0xE92D41F0U, 0xE0812081U, 0xE0807101U, 0xE2878A06U}},
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
