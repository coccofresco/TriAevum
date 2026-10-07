#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

namespace Oot3dNativeGame {

// Which title adapter a process image runs with. The OoT3D adapter (hooks
// and replacements at its code addresses, typed gameplay, TopScreen,
// composition and camera metadata) belongs to the exact code.bin it was
// written for. Any other title runs on the title-neutral CTR host, A32
// runtime and canonical renderer alone, with no guest hook.
enum class TitleAdapter { Oot3dEur, Generic };

// SHA-256 of OoT3D EUR's decompressed code.bin.
inline constexpr std::string_view kOot3dEurCodeBinSha256 =
    "16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220";

[[nodiscard]] inline TitleAdapter ResolveTitleAdapter(std::string_view codeBinSha256) noexcept {
    const bool oot3d = codeBinSha256.size() == kOot3dEurCodeBinSha256.size() &&
                       std::equal(codeBinSha256.begin(), codeBinSha256.end(), kOot3dEurCodeBinSha256.begin(),
                                  [](char a, char b) {
                                      return std::tolower(static_cast<unsigned char>(a)) == b;
                                  });
    return oot3d ? TitleAdapter::Oot3dEur : TitleAdapter::Generic;
}

[[nodiscard]] inline const char* TitleAdapterName(TitleAdapter adapter) noexcept {
    return adapter == TitleAdapter::Oot3dEur ? "oot3d_eur" : "generic";
}

} // namespace Oot3dNativeGame
