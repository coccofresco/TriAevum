// Ported from mm3d-dekui src/mm3d/prompt_text.h (tgacquin/mm3d-dekui 4f2692ed), contract part.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Prompt labels drawn by the dekui prompts with the game's own font. The language is the
// loaded message archive's (MessageArchiveLanguage), normalized to PromptLanguage; no integer
// language code crosses this interface, so `Language(m) == 2` does not compile.
//
// Columns:
// - english: the key, exactly the label the consumer draws today.
// - french, spanish: upstream 4f2692ed's strings for its 21 entries. Labels upstream does not
//   have (marked "TriAevum") carry our own French and Spanish.
// - german, italian: written for TriAevum (not taken from dekui-pal), using the terms of the
//   game's own German and Italian text (Inventar/Strumenti, Karte/Mappa, Zeitplan/programma,
//   Fall/evento, Wecker/Sveglia, Melodie, Zoom +/-, Abbrechen/Annulla).
// An empty cell falls back to English. Length budgets (glyphs) of the consumers that draw them:
// notebook A label <= 16, notebook map/alarm <= 9, notebook schedule/events <= 18,
// notebook back <= 8, menu help label <= 16, ocarina/pair prompt label <= 16.
#pragma once
#include <array>
#include <string>
#include <string_view>
#include "message_archive_language.h"

namespace Mm3dDekui::PromptText {
using MessageArchiveLanguage::PromptLanguage;
struct Entry { std::string_view english, french, spanish, german, italian; };
inline constexpr std::array Entries{
    Entry{"Change tab", "Changer d'onglet", "Cambiar pestaña", "Seite wechseln", "Cambia schermo"},
    Entry{"Select", "Choisir", "Elegir", "Wählen", "Seleziona"},
    Entry{"Choose", "Choisir", "Elegir", "Wählen", "Scegli"},
    Entry{"Return", "Retour", "Volver", "Zurück", "Indietro"},
    Entry{"Equip to I", "Assigner I", "Asignar a I", "Auf I legen", "Assegna a I"},
    Entry{"Equip to II", "Assigner II", "Asignar a II", "Auf II legen", "Assegna a II"},
    Entry{"Items", "Inventaire", "Inventario", "Inventar", "Strumenti"},
    Entry{"Map", "Carte", "Mapa", "Karte", "Mappa"},
    Entry{"Zoom in", "Zoom +", "Acercar", "Zoom +", "Zoom +"},
    Entry{"Zoom out", "Zoom -", "Alejar", "Zoom -", "Zoom -"},
    Entry{"View schedule", "Voir horaires", "Ver horarios", "Zeitplan ansehen", "Mostra programma"},
    Entry{"View events", "Voir l'historique", "Ver eventos", "Fälle ansehen", "Mostra eventi"},
    Entry{"View map", "Carte", "Mapa", "Karte", "Mappa"},
    Entry{"Set alarm", "Alarme", "Alarma", "Wecker", "Sveglia"},
    Entry{"Schedule details", "Détails horaires", "Detalles horario", "Zeitplan-Details", "Dettagli orari"},
    Entry{"Event details", "Détails", "Detalles suceso", "Fall-Details", "Dettagli evento"},
    Entry{"Songs", "Chants", "Canciones", "Melodien", "Melodie"},
    Entry{"Cancel", "Annuler", "Cancelar", "Abbrechen", "Annulla"},
    Entry{"Move map", "Déplacer la carte", "Mover mapa", "Karte bewegen", "Sposta mappa"},
    Entry{"Back", "Retour", "Volver", "Zurück", "Indietro"},
    Entry{"Move cursor", "Déplacer", "Mover cursor", "Cursor bewegen", "Sposta cursore"},
    // Labels the current (pre-tip) consumers draw; upstream renamed them later. TriAevum.
    Entry{"Song list", "Liste des chants", "Canciones", "Melodienliste", "Elenco melodie"},
    Entry{"Zoom In", "Zoom +", "Acercar", "Zoom +", "Zoom +"},
    Entry{"Zoom Out", "Zoom -", "Alejar", "Zoom -", "Zoom -"},
};

constexpr std::string_view Column(const Entry& e, PromptLanguage language) {
    switch (language) {
    case PromptLanguage::French: return e.french;
    case PromptLanguage::Spanish: return e.spanish;
    case PromptLanguage::German: return e.german;
    case PromptLanguage::Italian: return e.italian;
    case PromptLanguage::English: break;
    }
    return e.english;
}

// The label for `language`; English for unknown labels and empty cells.
constexpr std::string_view Translate(PromptLanguage language, std::string_view english) {
    for (const auto& e : Entries)
        if (e.english == english) {
            const auto text = Column(e, language);
            return text.empty() ? e.english : text;
        }
    return english;
}

// Active message archive (not a requested language awaiting restart).
template<class M> PromptLanguage Language(M& m) {
    return MessageArchiveLanguage::ReadArchiveLanguage(m).language;
}
template<class M> std::string_view Get(M& m, std::string_view english) {
    return Translate(Language(m), english);
}

// Prompt literals are UTF-8; font lookup takes Unicode code points. Invalid UTF-8 -> empty.
inline std::u32string Codes(std::string_view text) {
    std::u32string out;
    for (size_t i = 0; i < text.size();) {
        unsigned c = static_cast<unsigned char>(text[i++]), n = 0, min = 0;
        if (c < 0x80) {
        } else if ((c & 0xe0) == 0xc0) { c &= 31; n = 1; min = 0x80; }
        else if ((c & 0xf0) == 0xe0) { c &= 15; n = 2; min = 0x800; }
        else if ((c & 0xf8) == 0xf0) { c &= 7; n = 3; min = 0x10000; }
        else return {};
        if (i + n > text.size()) return {};
        while (n--) {
            unsigned b = static_cast<unsigned char>(text[i++]);
            if ((b & 0xc0) != 0x80) return {};
            c = (c << 6) | (b & 63);
        }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return {};
        out.push_back(c);
    }
    return out;
}
} // namespace Mm3dDekui::PromptText
