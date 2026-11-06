// SPDX-License-Identifier: GPL-2.0-or-later
// Active message-archive language for the dekui prompts (DEKUI_INTEGRATION_PLAN_2026-11 S1).
//
// Upstream tgacquin/mm3d-dekui 4f2692ed reads PromptText::Language() from the raw
// Restoration-image word 0x6b373c and returns archive+0xC as an integer code that its
// consumers compare with the USA code 2. zilzo75/dekui-pal daf640f6 confirms the EUR mask
// semantics but returns 16/32 for German/Italian, which collide with the raw EUR masks
// 0x10 (English) and 0x20 (German). Neither integer is ported. This header is the one
// contract: vanilla v1.1 code words prove where the archive pointer lives, every read
// validates the GMSG v1 header, and consumers receive a normalized enum, never an integer.
//
// Proof (vanilla EUR v1.1 code.bin, W/analysis/dekui/plan_2026-11/language_xref_chain.txt):
//   getter 0x1C4FC8 (called at 0x11E704 / 0x11ED40) returns literal 0x6B3700 (0x1C5074
//   ldr r0,[pc,#4]; pool 0x1C5080);
//   callers 0x11E710 bl 0x13540C and 0x11ED48 bl 0x135868 keep it in r4 (mov r4,r0 at
//   0x13541C / 0x13586C) and pass r4+0x38 (0x1355D8 / 0x1359A4) to 0x145408 (bl at
//   0x1355DC / 0x1359A8), which keeps `this` in r4 (0x14541C) and passes r4+8 (0x14542C) to
//   the reader 0x15A0F8 (bl at 0x145430); the reader
//   stores data at this+4 (0x15A128..0x15A134 stm r4,{r1,ip}) and validates "GMSG"
//   (pool 0x15A1E8). Archive pointer: 0x6B3700+0x38+8+4 = 0x6B3744 (BSS; the -8 delta to
//   upstream's 0x6B373C is the +8 band 0x6B1E40-0x6BB364).
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace Mm3dDekui::MessageArchiveLanguage {
using U32 = std::uint32_t;

enum class PromptLanguage : std::uint8_t { English, French, Spanish, German, Italian };
enum class ArchiveRegion : std::uint8_t { Usa, Eur };

struct ArchiveLanguage {
    bool valid = false;                              // header, version, pad and mask accepted
    ArchiveRegion region = ArchiveRegion::Usa;       // Usa when !valid
    PromptLanguage language = PromptLanguage::English; // English when !valid
    U32 raw_mask = 0;                                // archive+0xC as read (0 if unreadable); identity only
    friend bool operator==(const ArchiveLanguage&, const ArchiveLanguage&) = default;
};

// The eight masks the USA and EUR v1.1 archives carry (message/us/*.gmsg, message/eu/*.gmsg).
// EUR Dutch (0x200) has no populated layout archives and is not a supported language.
struct MaskEntry { U32 mask; ArchiveRegion region; PromptLanguage language; };
inline constexpr std::array<MaskEntry, 8> KnownMasks{{
    {0x002, ArchiveRegion::Usa, PromptLanguage::English},
    {0x004, ArchiveRegion::Usa, PromptLanguage::French},
    {0x008, ArchiveRegion::Usa, PromptLanguage::Spanish},
    {0x010, ArchiveRegion::Eur, PromptLanguage::English},
    {0x020, ArchiveRegion::Eur, PromptLanguage::German},
    {0x040, ArchiveRegion::Eur, PromptLanguage::French},
    {0x080, ArchiveRegion::Eur, PromptLanguage::Spanish},
    {0x100, ArchiveRegion::Eur, PromptLanguage::Italian},
}};

constexpr ArchiveLanguage FromRawMask(U32 mask) {
    for (const auto& entry : KnownMasks)
        if (entry.mask == mask) return {true, entry.region, entry.language, mask};
    return {false, ArchiveRegion::Usa, PromptLanguage::English, mask};
}

// Text tables keyed by the USA archive codes (single_screen_text, prices, classic localized
// text). German and Italian have no USA table, so no edit applies to them.
constexpr std::optional<unsigned> UsaTableCode(PromptLanguage language) {
    switch (language) {
    case PromptLanguage::English: return 2u;
    case PromptLanguage::French: return 4u;
    case PromptLanguage::Spanish: return 8u;
    case PromptLanguage::German:
    case PromptLanguage::Italian: return std::nullopt;
    }
    return std::nullopt;
}

constexpr U32 ArchivePointer = 0x006b3744; // stored by the reader's stm at 0x15A134
constexpr U32 GmsgMagic = 0x47534d47;      // "GMSG"
constexpr U32 GmsgVersion1 = 0x00000001;   // u16 version 1, u16 0

// Original EUR v1.1 words of the chain above. All must match before the pointer is read.
inline constexpr std::array<std::pair<U32, U32>, 22> Guards{{
    {0x001c5074, 0xe59f0004}, // getter: ldr r0,[pc,#4]
    {0x001c5080, 0x006b3700}, // getter literal: message manager
    {0x0013541c, 0xe1a04000}, // first caller chain (entry 0x13540C): mov r4,r0
    {0x0013586c, 0xe1a04000}, // second caller chain (entry 0x135868, from 0x11ED48): mov r4,r0
    {0x001355d8, 0xe2840038}, // add r0,r4,#0x38 (first chain)
    {0x001359a4, 0xe2840038}, // add r0,r4,#0x38 (second chain)
    {0x0014541c, 0xe1a04000}, // 0x145408: mov r4,r0
    {0x0014542c, 0xe2840008}, // 0x145408: add r0,r4,#8 -> reader 0x6B3740
    {0x0015a128, 0xe2804004}, // reader: add r4,r0,#4
    {0x0015a12c, 0xe281c010}, // reader: add ip,r1,#0x10
    {0x0015a130, 0xe580200c}, // reader: str r2,[r0,#0xc]
    {0x0015a134, 0xe8841002}, // reader: stm r4,{r1,ip} -> data at this+4 = 0x6B3744
    {0x0015a1e8, GmsgMagic},  // reader pool: "GMSG"
    {0x001c5078, 0xe8bd8070}, // getter: pop {r4-r6,pc} (body ends before the literal)
    // Call sites that connect the chain: without them the words above could sit in code
    // that no longer calls into each other.
    {0x0011e704, 0xeb029a2f}, // first chain: bl 0x1C4FC8 (getter)
    {0x0011e710, 0xeb005b3d}, // first chain: bl 0x13540C
    {0x0011ed40, 0xeb0298a0}, // second chain: bl 0x1C4FC8 (getter)
    {0x0011ed44, 0xe1a04000}, // second chain: mov r4,r0
    {0x0011ed48, 0xeb005ac6}, // second chain: bl 0x135868
    {0x001355dc, 0xeb003f89}, // first chain: bl 0x145408
    {0x001359a8, 0xeb003e96}, // second chain: bl 0x145408
    {0x00145430, 0xeb005330}, // 0x145408: bl 0x15A0F8 (reader)
}};

template<class M> bool GuardsMatch(M& m) {
    for (const auto& [at, word] : Guards)
        if (m.Read(at) != std::optional<U32>(word)) return false;
    return true;
}

// Reads the active archive per call: the loaded archive, never a requested language
// waiting for a restart. Any failed guard, pointer or header check gives English (invalid).
template<class M> ArchiveLanguage ReadArchiveLanguage(M& m) {
    const ArchiveLanguage fallback{};
    if (!GuardsMatch(m)) return fallback;
    const auto archive = m.Read(ArchivePointer);
    if (!archive || !*archive || (*archive & 3)) return fallback;
    if (m.Read(*archive) != std::optional<U32>(GmsgMagic) ||
        m.Read(*archive + 4) != std::optional<U32>(GmsgVersion1)) return fallback;
    const auto mask = m.Read(*archive + 0xc);
    if (!mask) return fallback;
    return FromRawMask(*mask);
}
} // namespace Mm3dDekui::MessageArchiveLanguage
