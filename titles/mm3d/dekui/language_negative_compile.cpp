// SPDX-License-Identifier: GPL-2.0-or-later
// S1 compile checks (DEKUI_INTEGRATION_PLAN_2026-11): no integer language code crosses the
// PromptText contract. Built by CMake try_compile and by the standalone runner:
//   TRIAEVUM_POSITIVE_CONTROL   must compile (the normalized comparison)
//   TRIAEVUM_NEGATIVE_COMPARE   must NOT compile (upstream's `Language(m) == 2`)
//   TRIAEVUM_NEGATIVE_TRANSLATE must NOT compile (upstream's `Translate(2, label)`)
#include "prompt_text.h"
#include <cstdint>
#include <optional>

namespace {
struct NoMemory {
    std::optional<std::uint32_t> Read(std::uint32_t) { return std::nullopt; }
};
} // namespace

int main() {
    NoMemory m;
    namespace T = Mm3dDekui::PromptText;
#if defined(TRIAEVUM_POSITIVE_CONTROL)
    return T::Language(m) == T::PromptLanguage::English && T::Translate(T::PromptLanguage::German, "Back") == "Zurück" ? 0 : 1;
#elif defined(TRIAEVUM_NEGATIVE_COMPARE)
    return T::Language(m) == 2 ? 0 : 1;
#elif defined(TRIAEVUM_NEGATIVE_TRANSLATE)
    return T::Translate(2, "Back").empty() ? 1 : 0;
#else
#error "define one of TRIAEVUM_POSITIVE_CONTROL, TRIAEVUM_NEGATIVE_COMPARE, TRIAEVUM_NEGATIVE_TRANSLATE"
#endif
}
