// SPDX-License-Identifier: GPL-2.0-or-later
// S1 fixtures (DEKUI_INTEGRATION_PLAN_2026-11): the MessageArchiveLanguage contract, the
// PromptText columns and the language branches of the prompt consumers.
// The layout/font harness is adapted from tgacquin/mm3d-dekui 4f2692ed
// tools/mm3d/native_inventory_check.cpp and tools/mm3d/prompt_localization_check.cpp.
// usage: oot3d_mm3d_dekui_language_tests [code.bin]  (with code.bin: guard words vs original EUR v1.1)
#include "menu_help.h"
#include "notebook_prompt.h"
#include "ocarina_prompt.h"
#include "prompt_text.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Mm3dDekui;
namespace L = Mm3dDekui::MessageArchiveLanguage;
namespace T = Mm3dDekui::PromptText;
using L::PromptLanguage;
using L::ArchiveRegion;
using U32 = std::uint32_t;

namespace {
unsigned checks = 0;
void Check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) throw std::runtime_error(what);
}

struct Memory {
    std::unordered_map<U32, U32> words;
    U32 next = 0x1000000;
    std::optional<U32> Read(U32 p) {
        auto i = words.find(p);
        return i == words.end() ? std::nullopt : std::optional<U32>(i->second);
    }
    void Write(U32 p, U32 v) {
        if (!words.contains(p)) throw std::runtime_error("write to unmapped fixture word");
        words[p] = v;
    }
    U32 Alloc(U32 size) { U32 p = next; next += (size + 3) & ~3u; for (U32 i = p; i < next; i += 4) words[i] = 0; return p; }
    U32 String(const char* s) {
        const auto n = std::char_traits<char>::length(s) + 1; U32 p = Alloc(U32(n));
        for (unsigned i = 0; i < n; ++i) words[p + (i & ~3u)] |= U32(static_cast<unsigned char>(s[i])) << ((i & 3) * 8);
        return p;
    }
    void WidgetAt(U32 p, const char* name, float px = 0, float py = 0) {
        words[p + 0x10] = String(name); words[p + 0x24] = 0xffff; words[p + 0xd4] = 15;
        for (U32 i = 0; i < 3; ++i) words[p + 0xe0 + i * 16 + i * 4] = std::bit_cast<U32>(1.f);
        words[p + 0xec] = std::bit_cast<U32>(px); words[p + 0xfc] = std::bit_cast<U32>(py);
    }
    U32 Widget(const char* name, float px = 0, float py = 0) { auto p = Alloc(0x140); WidgetAt(p, name, px, py); return p; }
    U32 Layout(const char* name, float px = 0, float py = 0, U32 size = 0x200) {
        auto p = Alloc(size), resource = Alloc(0x20); words[p + 4] = resource; words[resource + 0x18] = String(name);
        WidgetAt(p + 8, "root", px, py);
        words[p + 0x44] = std::bit_cast<U32>(px); words[p + 0x48] = std::bit_cast<U32>(py); return p;
    }
    void Children(U32 p, const std::vector<U32>& nodes) {
        auto list = Alloc(U32(nodes.size()) * 4); words[p + 0x14] = list; words[p + 0x18] = U32(nodes.size());
        for (unsigned i = 0; i < nodes.size(); ++i) words[list + i * 4] = nodes[i];
    }
};

void InstallGuards(Memory& m) { for (const auto& [at, word] : L::Guards) m.words[at] = word; }
U32 InstallArchive(Memory& m, U32 mask) {
    const U32 archive = m.Alloc(16);
    m.words[L::ArchivePointer] = archive;
    m.words[archive] = L::GmsgMagic; m.words[archive + 4] = 1; m.words[archive + 8] = 0xbe3ec; m.words[archive + 12] = mask;
    return archive;
}
const L::ArchiveLanguage Fallback{};

// ---- FromRawMask / UsaTableCode -------------------------------------------------------------
struct Expected { U32 mask; ArchiveRegion region; PromptLanguage language; std::optional<unsigned> usa; };
constexpr std::array<Expected, 8> Table{{
    {0x2, ArchiveRegion::Usa, PromptLanguage::English, 2u}, {0x4, ArchiveRegion::Usa, PromptLanguage::French, 4u},
    {0x8, ArchiveRegion::Usa, PromptLanguage::Spanish, 8u}, {0x10, ArchiveRegion::Eur, PromptLanguage::English, 2u},
    {0x20, ArchiveRegion::Eur, PromptLanguage::German, std::nullopt}, {0x40, ArchiveRegion::Eur, PromptLanguage::French, 4u},
    {0x80, ArchiveRegion::Eur, PromptLanguage::Spanish, 8u}, {0x100, ArchiveRegion::Eur, PromptLanguage::Italian, std::nullopt},
}};
static_assert(L::FromRawMask(0x10).language == PromptLanguage::English);
static_assert(L::FromRawMask(0x100).language == PromptLanguage::Italian);
static_assert(!L::FromRawMask(0x30).valid);

void MaskTable() {
    for (const auto& e : Table) {
        const auto a = L::FromRawMask(e.mask);
        Check(a.valid && a.region == e.region && a.language == e.language && a.raw_mask == e.mask,
              "mask table " + std::to_string(e.mask));
        Check(L::UsaTableCode(a.language) == e.usa, "UsaTableCode for mask " + std::to_string(e.mask));
    }
    // dekui-pal Locale() returns 16 for German and 32 for Italian; as raw masks those are
    // EUR English and EUR German. The normalized value must never confuse them.
    Check(L::FromRawMask(16).language == PromptLanguage::English && L::FromRawMask(16).language != PromptLanguage::German,
          "0x10 is EUR English, not German");
    Check(L::FromRawMask(32).language == PromptLanguage::German && L::FromRawMask(32).language != PromptLanguage::Italian,
          "0x20 is EUR German, not Italian");
    for (U32 mask : {0u, 1u, 0x3u, 0x30u, 0x110u, 0x200u, 0x400u, 0x80000000u, 0xffffffffu}) {
        const auto a = L::FromRawMask(mask);
        Check(!a.valid && a.language == PromptLanguage::English && a.region == ArchiveRegion::Usa && a.raw_mask == mask,
              "invalid mask falls back to English " + std::to_string(mask));
        Check(L::UsaTableCode(a.language) == std::optional<unsigned>(2u), "invalid mask uses USA table 2");
    }
    Check(!L::UsaTableCode(PromptLanguage::German) && !L::UsaTableCode(PromptLanguage::Italian), "no De/It USA table");
}

// ---- ReadArchiveLanguage --------------------------------------------------------------------
void Reader() {
    for (const auto& e : Table) {
        Memory m; InstallGuards(m); InstallArchive(m, e.mask);
        Check(L::ReadArchiveLanguage(m) == L::FromRawMask(e.mask), "reader accepts mask " + std::to_string(e.mask));
        Check(T::Language(m) == e.language, "PromptText::Language normalized " + std::to_string(e.mask));
    }
    const auto reject = [](Memory& m, const std::string& what) { Check(L::ReadArchiveLanguage(m) == Fallback, what); };
    {
        Memory m; InstallGuards(m); const U32 a = InstallArchive(m, 0x20);
        m.words[a] = 0x47534d48; reject(m, "wrong magic");
        m.words[a] = L::GmsgMagic; m.words[a + 4] = 2; reject(m, "wrong version");
        m.words[a + 4] = 0x00010001; reject(m, "nonzero pad");
        m.words[a + 4] = 0; reject(m, "version 0");
        m.words[a + 4] = 1; m.words.erase(a + 12); reject(m, "unreadable mask");
        m.words[a + 12] = 0x20; Check(L::ReadArchiveLanguage(m).language == PromptLanguage::German, "restored archive");
        m.words[a + 12] = 0x30; Check(L::ReadArchiveLanguage(m) == L::FromRawMask(0x30) && !L::ReadArchiveLanguage(m).valid,
                                     "two-bit mask is invalid, raw kept");
        m.words[a + 12] = 0x20;
        m.words[L::ArchivePointer] = 0; reject(m, "null archive pointer");
        m.words[L::ArchivePointer] = 0x20000000; reject(m, "unmapped archive pointer");
        m.words[a + 2] = 0; m.words[L::ArchivePointer] = a + 2; reject(m, "misaligned archive pointer");
        m.words.erase(L::ArchivePointer); reject(m, "unreadable archive pointer");
    }
    {
        // Upstream's raw 0x6B373C holds 0x0063D33C on EUR v1.1 (rodata, not GMSG).
        Memory m; InstallGuards(m);
        m.words[0x0063d33c] = 0x006182f0; m.words[0x0063d340] = 0x0063d48c; m.words[0x0063d348] = 0x00616de8;
        m.words[0x006b373c] = 0x0063d33c; m.words[L::ArchivePointer] = 0x0063d33c;
        reject(m, "upstream raw 0x6B373C value falls back");
        // A valid archive only at upstream's word is not ours: 0x6B3744 decides.
        Memory n; InstallGuards(n); const U32 a = n.Alloc(16);
        n.words[a] = L::GmsgMagic; n.words[a + 4] = 1; n.words[a + 12] = 0x100; n.words[0x006b373c] = a;
        reject(n, "archive pointer is 0x6B3744, not upstream 0x6B373C");
    }
    {
        bool second_chain = false;
        for (const auto& [at, word] : L::Guards) second_chain |= at == 0x0013586c && word == 0xe1a04000;
        Check(second_chain, "second caller chain mov r4,r0 at 0x13586C is guarded");
        // The call sites that connect the chain (S1 review): each one is guarded, and the
        // loop below corrupts and removes each of them like every other guard word.
        constexpr std::array<std::pair<U32, U32>, 8> CallSites{{
            {0x0011e704, 0xeb029a2f}, {0x0011e710, 0xeb005b3d}, {0x0011ed40, 0xeb0298a0},
            {0x0011ed44, 0xe1a04000}, {0x0011ed48, 0xeb005ac6}, {0x001355dc, 0xeb003f89},
            {0x001359a8, 0xeb003e96}, {0x00145430, 0xeb005330},
        }};
        Check(L::Guards.size() == 22, "22 guard words (14 chain words + 8 call sites)");
        for (const auto& site : CallSites) {
            bool found = false;
            for (const auto& guard : L::Guards) found |= guard == site;
            Check(found, "call-site guard " + std::to_string(site.first));
            // A bl that points elsewhere (offset +1) must reject, not only a flipped low bit.
            Memory m; InstallGuards(m); InstallArchive(m, 0x100);
            m.words[site.first] = site.second + 1; reject(m, "retargeted call site " + std::to_string(site.first));
        }
        for (std::size_t i = 0; i < L::Guards.size(); ++i)
            for (std::size_t j = i + 1; j < L::Guards.size(); ++j)
                Check(L::Guards[i].first != L::Guards[j].first, "guard addresses are distinct");
        for (std::size_t i = 0; i < L::Guards.size(); ++i) {
            Memory m; InstallGuards(m); InstallArchive(m, 0x100);
            const auto [at, word] = L::Guards[i];
            m.words[at] = word ^ 1; reject(m, "corrupted guard " + std::to_string(at));
            m.words.erase(at); reject(m, "removed guard " + std::to_string(at));
            m.words[at] = word; Check(L::ReadArchiveLanguage(m).language == PromptLanguage::Italian, "guard restored");
        }
    }
}

void CodeBin(const char* path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> data((std::istreambuf_iterator<char>(file)), {});
    Check(data.size() == 5967872, "code.bin size (EUR/USA v1.1, sha256 6eec27b1...)");
    for (const auto& [at, word] : L::Guards) {
        const std::size_t off = at - 0x100000;
        U32 value = 0;
        for (unsigned b = 0; b < 4; ++b) value |= U32(static_cast<unsigned char>(data.at(off + b))) << (b * 8);
        Check(value == word, "original EUR v1.1 guard word " + std::to_string(at));
    }
    std::cout << "guard words match " << path << '\n';
}

// ---- PromptText ----------------------------------------------------------------------------
constexpr std::array<PromptLanguage, 5> Languages{PromptLanguage::English, PromptLanguage::French,
    PromptLanguage::Spanish, PromptLanguage::German, PromptLanguage::Italian};
std::size_t Glyphs(PromptLanguage l, std::string_view english) { return T::Codes(T::Translate(l, english)).size(); }

void Strings() {
    Check(T::Codes("Détails") == U"Détails" && T::Codes("pestaña") == U"pestaña", "UTF-8 decode");
    Check(T::Codes("Zurück") == U"Zurück" && T::Codes("Fälle ansehen") == U"Fälle ansehen", "German umlauts decode");
    Check(T::Codes("\xc0\x80").empty() && T::Codes("\xe2\x82").empty() && T::Codes("\xed\xa0\x80").empty(), "invalid UTF-8 rejected");
    for (const auto& e : T::Entries) {
        Check(T::Translate(PromptLanguage::English, e.english) == e.english, "English column is the key");
        for (auto l : Languages) {
            Check(!T::Codes(T::Translate(l, e.english)).empty(), "every label decodes: " + std::string(e.english));
        }
        Check(!e.german.empty() && !e.italian.empty() && !e.french.empty() && !e.spanish.empty(),
              "every column written: " + std::string(e.english));
    }
    Check(T::Translate(PromptLanguage::German, "Not a prompt label") == "Not a prompt label", "unknown label stays English");
    Check(T::Translate(PromptLanguage::Italian, "ZR +") == "ZR +", "button glyph text is not translated");
    // Our German and Italian (game terms: Inventar, Karte/Mappa, Wecker/Sveglia, Zoom +/-).
    const std::array<std::array<std::string_view, 3>, 10> pinned{{
        {"Back", "Zurück", "Indietro"}, {"Select", "Wählen", "Seleziona"}, {"Items", "Inventar", "Oggetti"},
        {"Map", "Karte", "Mappa"}, {"Set alarm", "Wecker", "Sveglia"}, {"Zoom In", "Zoom +", "Zoom +"},
        {"Songs", "Melodien", "Melodie"}, {"Cancel", "Abbrechen", "Annulla"}, {"Change tab", "Seite wechseln", "Cambia schermo"},
        {"Equip to II", "Auf II legen", "Assegna a II"}}};
    for (const auto& p : pinned)
        Check(T::Translate(PromptLanguage::German, p[0]) == p[1] && T::Translate(PromptLanguage::Italian, p[0]) == p[2],
              "pinned De/It label " + std::string(p[0]));
    Check(T::Translate(PromptLanguage::French, "Change tab") == "Changer d'onglet" &&
          T::Translate(PromptLanguage::Spanish, "Event details") == "Detalles suceso", "upstream Fr/Es columns kept");
    // Glyph budgets of the consumers that draw each label.
    for (auto l : Languages) {
        for (auto label : {"Schedule details", "Event details"}) Check(Glyphs(l, label) <= 16, "notebook A label budget");
        for (auto label : {"View map", "Set alarm"}) Check(Glyphs(l, label) <= 9, "notebook map/alarm budget");
        for (auto label : {"View schedule", "View events"}) Check(Glyphs(l, label) <= 18, "notebook page budget");
        for (auto label : {"Back", "Return"}) Check(Glyphs(l, label) <= 8, "notebook back budget");
        for (auto label : MenuHelp::Labels) Check(Glyphs(l, label) <= 16, "menu help budget");
        for (auto label : {"Choose", "Cancel"}) Check(Glyphs(l, label) <= 16, "menu help present budget");
        for (auto label : {"Select", "Back", "Items", "Map", "Move cursor", "Return", "Song list", "Zoom In", "Zoom Out"})
            Check(Glyphs(l, label) <= 16, "pair/single prompt budget");
    }
    Check(MenuHelp::EquipLabel(PromptLanguage::German, false) == "ZL - Auf I legen" &&
          MenuHelp::EquipLabel(PromptLanguage::English, true) == "ZR - Equip to II", "EquipLabel takes PromptLanguage");
}

// ---- consumer fixtures ---------------------------------------------------------------------
struct Fixture {
    Memory m;
    U32 font = 0, pages = 0, icon = 0, text = 0, ig = 0, tg = 0;
    explicit Fixture(U32 mask) {
        InstallGuards(m); InstallArchive(m, mask);
        font = m.Alloc(0x14); const U32 header = m.Alloc(0x2c); pages = m.Alloc(8);
        std::u32string chars;
        for (const auto& e : T::Entries) for (auto l : Languages) chars += T::Codes(T::Translate(l, e.english));
        chars += U"ZRL -+/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::sort(chars.begin(), chars.end()); chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
        const U32 records = m.Alloc(U32(chars.size()) * 12);
        m.words[font + 4] = header; m.words[font + 0xc] = pages; m.words[font + 0x10] = records;
        m.words[header + 8] = 8u << 16; m.words[header + 12] = 12; m.words[header + 0x10] = 1; m.words[header + 0x14] = U32(chars.size());
        m.words[header + 0x1c] = 4; m.words[header + 0x24] = 16u << 16; m.words[header + 0x28] = 16;
        m.words[pages] = 0x1000; m.words[pages + 4] = 256u | (256u << 16);
        for (unsigned i = 0; i < chars.size(); ++i) {
            m.words[records + i * 12] = chars[i]; m.words[records + i * 12 + 4] = chars[i] == ' ' ? 4 : 10;
            m.words[records + i * 12 + 8] = 1u | (((i % 16) | ((i / 16) << 8)) << 16);
        }
        icon = m.Widget("icon"); text = m.Widget("text"); ig = m.Alloc(0x7c); tg = m.Alloc(0x30c);
        m.words[icon + 0x28] = ig; m.words[text + 0x28] = tg; m.words[tg + 0x28] = font;
        m.words[ig + 4] = 6; m.words[ig + 0x10] = pages; m.words[tg + 4] = 3;
        // Main resource registry and its parts atlas (group0 resource11), as 4f2692ed's check.
        const U32 groups = m.Alloc(8), entries = m.Alloc(48), parts = m.Alloc(0x60);
        m.words[NotebookPrompt::RegistryLiteral] = NotebookPrompt::Registry;
        for (U32 at = NotebookPrompt::Registry; at < NotebookPrompt::Registry + 0x40; at += 4) m.words[at] = 0;
        m.words[NotebookPrompt::Registry + 0x28] = groups; m.words[NotebookPrompt::Registry + 0x2c] = entries;
        m.words[groups] = 12; m.words[entries + 44] = parts; m.words[parts + 4] = 11u << 16;
    }
    U32 Stack(U32 size) { return (m.Alloc(size) + size) & ~7u; }
    // Glyph records written for a text pane equal the font's glyphs for `label`.
    void ExpectGlyphs(U32 geometry, std::u32string_view label, const std::string& what) {
        Check(m.words[geometry + 0x300] == label.size(), what + ": glyph count");
        const U32 start = m.words[geometry + 0x2fc]; float pen = 0;
        for (unsigned i = 0; i < label.size(); ++i) {
            auto glyph = NotebookFont::Lookup(m, font, label[i], pen); Check(bool(glyph), what + ": glyph in font");
            for (unsigned j = 0; j < 15; ++j) Check(m.words[start + i * 60 + j * 4] == glyph->words[j], what + ": glyph record");
            pen += float(glyph->advance);
        }
    }
};
float F(U32 bits) { return std::bit_cast<float>(bits); }
std::u32string Label(PromptLanguage l, std::string_view english) { return T::Codes(T::Translate(l, english)); }

void PairAndSinglePrompts(U32 mask) {
    Fixture f(mask); auto& m = f.m;
    const auto language = L::FromRawMask(mask).language;
    const bool english = language == PromptLanguage::English;
    const U32 layout = m.Layout("MenuOcarina"), sp = f.Stack(0x6008);
    OcarinaPrompt::Hook hook; hook.pass = 3; hook.panel = layout; hook.icon = f.icon; hook.text = f.text;
    hook.texture = f.pages; hook.native_alpha = 1;
    {
        // File select's Select/Back pair.
        OcarinaPrompt::PairPrompt pair{{true, 0, 0, 1, 1, 22}, layout + 8, {"Select", "Back"}, {112, 128}};
        const auto before = m.words;
        const U32 root = hook.Root(m, layout, sp, false, nullptr, &pair);
        Check(root != layout + 8, "pair prompt built " + std::to_string(mask));
        const U32 children = m.words[root + 0x14];
        for (unsigned row = 0; row < 2; ++row) {
            const U32 node = m.words[children + row * 16 + 4];
            f.ExpectGlyphs(m.words[node + 0x28], Label(language, row ? "Back" : "Select"), "pair row " + std::to_string(row));
            if (english) Check(T::Translate(language, row ? "Back" : "Select") == (row ? "Back" : "Select"), "English pair labels");
        }
        Check(hook.End(m) == sp && m.words == before, "pair prompt restores guest memory");
    }
    for (bool reverse : {false, true}) {
        // Items/Map arrow prompt: translated labels fit the 36-pixel slot; English stays 12 px.
        OcarinaPrompt::SinglePrompt single{{0, 0, 1}, layout + 8, f.pages};
        single.reverse = reverse;
        const auto before = m.words;
        const U32 root = hook.Root(m, layout, sp, false, &single);
        Check(root != layout + 8, "single prompt built");
        const U32 node = m.words[m.words[root + 0x14] + 4];
        const auto label = Label(language, reverse ? "Items" : "Map");
        f.ExpectGlyphs(m.words[node + 0x28], label, "single prompt");
        const float scale = F(m.words[node + 0xe0]), width = 10.f * float(label.size());
        if (english) Check(scale == 12.f / 16, "English arrow label keeps the native size");
        else Check(scale <= 12.f / 16 && scale * width <= 36.01f, "translated arrow label fits its slot");
        Check(hook.End(m) == sp && m.words == before, "single prompt restores guest memory");
    }
}

void MenuHelpRow(U32 mask) {
    Fixture f(mask); auto& m = f.m;
    const auto language = L::FromRawMask(mask).language;
    const bool english = language == PromptLanguage::English;
    const U32 panel = m.Layout("MenuItem"), all = m.Widget("all");
    m.Children(panel + 8, {all}); m.Children(all, {f.icon, f.text}); m.words[all + 0x134] = std::bit_cast<U32>(1.f);
    NativeInventory::Hook inventory; inventory.panel = panel; inventory.pass = 3; inventory.routed = {panel};
    NativeInventory::enabled = true;
    MenuHelp::Options options; options.horizontal = true; options.pose = {150, 220, 1}; options.spacing = 20; options.button_gap = 4;
    MenuHelp::Apply(options);
    const U32 stack = f.Stack(0x12000);
    MenuHelp::Hook help; const auto before = m.words;
    const U32 root = help.Root(m, inventory, panel, stack, panel + 8);
    Check(root != panel + 8, "menu help built " + std::to_string(mask));
    const U32 children = m.words[root + 0x14], count = m.words[root + 0x18];
    float right = 0; std::optional<U32> text_scale; std::vector<U32> drawn;
    // Text panes in order: "/" "Change tab" "Choose" "Return" "ZR +" "Equip to I" "ZR +" "Equip to II".
    const std::array<std::string_view, 8> order{"/", "Change tab", "Choose", "Return", "ZR +", "Equip to I", "ZR +", "Equip to II"};
    for (unsigned i = 0; i + 1 < count; ++i) {
        const U32 n = m.words[children + 4 * i], g = m.words[n + 0x28];
        if ((m.words[g + 4] & 65535) != 3 || F(m.words[n + 0x128]) != 1.f) continue; // foreground text only
        if (!text_scale) text_scale = m.words[n + 0xe0];
        Check(*text_scale == m.words[n + 0xe0], "one shared text size");
        Check(drawn.size() < order.size(), "menu help label count");
        const auto label = Label(language, order[drawn.size()]);
        f.ExpectGlyphs(g, label, "menu help label " + std::string(order[drawn.size()]));
        float width = 0;
        for (char32_t c : label) width += c == U' ' ? 4.f : 10.f;
        right = std::max(right, F(m.words[n + 0xec]) + width * F(m.words[n + 0xe0]));
        drawn.push_back(m.words[g + 0x300]);
    }
    Check(drawn.size() == order.size() && text_scale.has_value(), "menu help drew every label");
    if (english) Check(F(*text_scale) == 12.f / 16, "English row is not measured (native text size)");
    else Check(right <= 396.01f && F(*text_scale) <= 12.f / 16, "translated row fits the screen");
    Check(help.scratch.End(m) == stack && m.words == before, "menu help restores guest memory");
    MenuHelp::Apply({});
}

void Notebook(U32 mask) {
    Fixture f(mask); auto& m = f.m;
    const auto language = L::FromRawMask(mask).language;
    const bool english = language == PromptLanguage::English;
    namespace P = NotebookPrompt;
    const U32 sp = f.Stack(0x8008), owner = m.Alloc(0x800), pictogram = m.Layout("Pictogram");
    const U32 all = m.Widget("all"), pict = m.Widget("pict"), anchor = m.Widget("disp_a_icon_n");
    m.Children(pictogram + 8, {all}); m.Children(all, {pict});
    m.words[owner + 0x2a8] = pictogram; m.words[owner + 0x290] = anchor; m.words[pictogram + 0x180] = anchor;
    m.words[pict + 0x28] = f.ig;
    P::enabled = P::map_enabled = P::schedule_enabled = true;
    P::pose.x = 300; P::pose.y = 0; P::pose.scale = .6f;
    P::map_pose.x = 300; P::map_pose.y = -20; P::map_pose.scale = .6f;
    for (bool schedule : {false, true}) {
        const U32 panel = m.Layout(schedule ? "MenuSchedule" : "MenuQuestlist"), a = m.Widget("all");
        const U32 detail = m.Widget(schedule ? "sche_quest_detail" : "quest_list_detail");
        m.Children(panel + 8, {a}); m.Children(a, {detail}); m.Children(detail, {f.text});
        P::Hook notebook; const auto before = m.words;
        const U32 root = notebook.Build(m, panel, owner, sp, 1);
        Check(root != panel + 8, "notebook prompt built " + std::to_string(mask));
        const U32 children = m.words[root + 0x14], count = m.words[root + 0x18];
        std::vector<std::size_t> counts;
        for (unsigned i = 0; i + 1 < count; ++i) {
            const U32 n = m.words[children + i * 4], g = m.words[n + 0x28];
            if ((m.words[g + 4] & 65535) != 3) continue;
            const U32 glyphs = m.words[g + 0x300]; counts.push_back(glyphs);
            const float end = F(m.words[n + 0xec]) + float(glyphs) * 10.f * F(m.words[n + 0xe0]);
            if (english) Check(F(m.words[n + 0xe0]) == .6f * (12.f / 16), "English notebook label keeps 12 px");
            else Check(end <= 400.01f, "translated notebook label stays on screen");
            Check(m.words[g + 0x2fc] + glyphs * 60 <= sp, "glyph records inside scratch");
        }
        const std::array<std::string_view, 4> labels = schedule
            ? std::array<std::string_view, 4>{"Event details", "Set alarm", "View events", "Back"}
            : std::array<std::string_view, 4>{"Schedule details", "View map", "View schedule", "Return"};
        Check(counts.size() == 4, "four notebook labels");
        for (unsigned i = 0; i < 4; ++i)
            Check(counts[i] == Label(language, labels[i]).size(), "notebook label " + std::string(labels[i]));
        Check(notebook.End(m) == sp && m.words == before, "notebook restores guest memory");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        MaskTable(); Reader(); Strings();
        if (argc > 1) CodeBin(argv[1]);
        else std::cout << "no code.bin given: original guard words not compared\n";
        for (U32 mask : {0x2u, 0x10u, 0x4u, 0x8u, 0x20u, 0x40u, 0x80u, 0x100u, 0x200u, 0u}) {
            PairAndSinglePrompts(mask); MenuHelpRow(mask); Notebook(mask);
        }
        std::cout << "PASS " << checks << " language contract checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
