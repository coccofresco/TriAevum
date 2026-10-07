#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

namespace Oot3dNativeGame {

// Which title adapter a process image runs with. The OoT3D adapter (hooks
// and replacements at its code addresses, typed gameplay, TopScreen,
// composition and camera metadata) belongs to the exact code.bin it was
// written for. MM3D's adapter so far only reads its RomFS (languages); its
// code still runs with no guest hook. Any other title runs on the
// title-neutral CTR host, A32 runtime and canonical renderer alone.
enum class TitleAdapter { Oot3dEur, Mm3d, Generic };

// SHA-256 of OoT3D EUR's decompressed code.bin.
inline constexpr std::string_view kOot3dEurCodeBinSha256 =
    "16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220";
// SHA-256 of MM3D's decompressed code.bin (v1.0). The EUR and USA cards carry
// the same bytes; the region lives in the program ID and RomFS.
inline constexpr std::string_view kMm3dCodeBinSha256 =
    "6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d";

[[nodiscard]] inline bool SameCodeBinSha256(std::string_view actual, std::string_view expected) noexcept {
    return actual.size() == expected.size() &&
           std::equal(actual.begin(), actual.end(), expected.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == b;
           });
}

[[nodiscard]] inline TitleAdapter ResolveTitleAdapter(std::string_view codeBinSha256) noexcept {
    if (SameCodeBinSha256(codeBinSha256, kOot3dEurCodeBinSha256))
        return TitleAdapter::Oot3dEur;
    if (SameCodeBinSha256(codeBinSha256, kMm3dCodeBinSha256))
        return TitleAdapter::Mm3d;
    return TitleAdapter::Generic;
}

// The product name a title runs under (window title, log name), or nullptr to
// keep the launch's own name. UTF-8.
[[nodiscard]] inline const char* TitleProductName(TitleAdapter adapter) noexcept {
    return adapter == TitleAdapter::Mm3d ? "TriAevum \xE2\x80\x94 Moonfall Curse" : nullptr;
}

// The F1 graphics window's title, or nullptr for the renderer's default.
[[nodiscard]] inline const char* TitleGraphicsWindowTitle(TitleAdapter adapter) noexcept {
    switch (adapter) {
    case TitleAdapter::Mm3d:
        return "Moonfall Curse Graphics";
    case TitleAdapter::Generic:
        return "Graphics";
    case TitleAdapter::Oot3dEur:
        break;
    }
    return nullptr;
}

// The scene view (frustum, and for OoT3D also camera and scene) and the
// native composition passes come from the title adapter's hooks; both
// adapters hook the perspective frustum builder, which widens the scene to
// the output aspect and applies the scene FOV.
[[nodiscard]] inline bool TitlePublishesSceneView(TitleAdapter adapter) noexcept {
    return adapter == TitleAdapter::Oot3dEur || adapter == TitleAdapter::Mm3d;
}
[[nodiscard]] inline bool TitleAppliesSceneFov(TitleAdapter adapter) noexcept {
    return adapter == TitleAdapter::Oot3dEur || adapter == TitleAdapter::Mm3d;
}

[[nodiscard]] inline const char* TitleAdapterName(TitleAdapter adapter) noexcept {
    switch (adapter) {
    case TitleAdapter::Oot3dEur:
        return "oot3d_eur";
    case TitleAdapter::Mm3d:
        return "mm3d";
    case TitleAdapter::Generic:
        break;
    }
    return "generic";
}

} // namespace Oot3dNativeGame
