#pragma once

// Developer environment overrides (G4a-F3, R1). A product build (no
// TRIAEVUM_DEVELOPER_TOOLS) ignores them: the variable is never read, so a
// value left in a player's environment cannot change what the installed
// product loads. Each override is named here so the release test and the
// call sites agree on the list.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

// True only in a developer build.
inline constexpr bool kDeveloperOverridesEnabled =
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
    true;
#else
    false;
#endif

// The developer overrides this build gates.
inline constexpr std::string_view kDeveloperOverrideNames[] = {
    "TRIAEVUM_ROMFS_OVERLAY",          // shared: RomFS directories over the title's
    "TRIAEVUM_MM3D_PRESENTATION",      // MM3D: dekui presentation.json
    "TRIAEVUM_MM3D_DAMAGE_MULTIPLIER", // MM3D: dekui damage multiplier
    "TRIAEVUM_A32_PC_DUMP",            // adapter bring-up memory dump
};

// The override's value in a developer build; nullptr in a product build and
// when it is unset or empty.
[[nodiscard]] inline const char* DeveloperOverride(const char* name) noexcept {
    if constexpr (!kDeveloperOverridesEnabled) {
        (void)name;
        return nullptr;
    } else {
        const char* value = std::getenv(name);
        return value != nullptr && *value != '\0' ? value : nullptr;
    }
}

// TRIAEVUM_A32_PC_DUMP=PC,FRAME,DIR,ADDR:SIZE[;ADDR:SIZE...] (hex PC, decimal
// frame, hex ranges).
struct A32PcDumpSpec {
    uint32_t Address = 0;
    uint32_t Frame = 0;
    std::string Directory;
    std::vector<std::pair<uint32_t, uint32_t>> Ranges;
};

namespace Detail {
[[nodiscard]] inline bool ParseUnsigned(std::string_view text, int base, uint32_t& value) noexcept {
    if (text.empty() || text.size() > 10U) return false;
    uint64_t result = 0;
    for (const char c : text) {
        unsigned digit;
        if (c >= '0' && c <= '9') digit = unsigned(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = unsigned(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') digit = unsigned(c - 'A' + 10);
        else return false;
        result = result * unsigned(base) + digit;
        if (result > 0xFFFFFFFFULL) return false;
    }
    value = static_cast<uint32_t>(result);
    return true;
}
} // namespace Detail

// Parses the spec without throwing: a malformed value yields nullopt and the
// reason, so a typo never stops the start-up.
[[nodiscard]] inline std::optional<A32PcDumpSpec> ParseA32PcDumpSpec(std::string_view spec,
                                                                     std::string* error = nullptr) {
    auto fail = [&](const char* reason) -> std::optional<A32PcDumpSpec> {
        if (error != nullptr) *error = reason;
        return std::nullopt;
    };
    const auto first = spec.find(',');
    const auto second = first == std::string_view::npos ? first : spec.find(',', first + 1);
    const auto third = second == std::string_view::npos ? second : spec.find(',', second + 1);
    if (third == std::string_view::npos) return fail("expected PC,FRAME,DIR,ADDR:SIZE[;...]");
    A32PcDumpSpec result;
    if (!Detail::ParseUnsigned(spec.substr(0, first), 16, result.Address)) return fail("PC is not hexadecimal");
    if (!Detail::ParseUnsigned(spec.substr(first + 1, second - first - 1), 10, result.Frame))
        return fail("FRAME is not decimal");
    result.Directory = std::string(spec.substr(second + 1, third - second - 1));
    if (result.Directory.empty()) return fail("DIR is empty");
    const auto ranges = spec.substr(third + 1);
    for (std::size_t start = 0; start <= ranges.size();) {
        const auto end = std::min(ranges.find(';', start), ranges.size());
        const auto range = ranges.substr(start, end - start);
        const auto colon = range.find(':');
        uint32_t address = 0, size = 0;
        if (colon == std::string_view::npos) return fail("range without ADDR:SIZE");
        if (!Detail::ParseUnsigned(range.substr(0, colon), 16, address) ||
            !Detail::ParseUnsigned(range.substr(colon + 1), 16, size))
            return fail("range is not hexadecimal ADDR:SIZE");
        result.Ranges.emplace_back(address, size);
        start = end + 1;
    }
    return result;
}

} // namespace Oot3dNativeGame
