// Ported from mm3d-dekui src/mm3d/price_text_data.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
// dekui's table carries original US English message text, which is game
// material and stays out of the repository (W/analysis/dekui/withheld/).
// Empty until the edits are applied to the user's own messages, checked by
// hash; the module stays inert on vanilla v1.1 until then anyway (its guard
// needs Project Restoration's message getter).
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace Mm3dDekui::Prices {
struct MessageEdit {std::uint16_t id;std::string_view original,text;std::uint32_t metadata,updated;std::uint64_t hash,edited_hash;};
inline constexpr std::array<MessageEdit,0> Messages{};
}
