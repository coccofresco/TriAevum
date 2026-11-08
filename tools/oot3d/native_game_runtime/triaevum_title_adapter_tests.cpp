// R-G1a-N1 / R2: the title adapter (63152316, a3fc81ad) resolves from the
// code.bin SHA-256: OoT3D EUR, MM3D, an unknown title (generic), and the
// case of the hash; the OoT3D guest readers and developer tools are on for
// the EUR adapter only.
#include "triaevum_title_adapter.h"

#include <iostream>
#include <string>

using namespace Oot3dNativeGame;

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) {
        std::cerr << "triaevum_title_adapter_tests: " << message << '\n';
        ++failures;
    }
}
std::string Upper(std::string_view text) {
    std::string result(text);
    for (auto& c : result)
        if (c >= 'a' && c <= 'f') c = char(c - 'a' + 'A');
    return result;
}
} // namespace

int main() {
    // OoT3D EUR, any case.
    Check(ResolveTitleAdapter(kOot3dEurCodeBinSha256) == TitleAdapter::Oot3dEur, "OoT3D EUR not resolved");
    Check(ResolveTitleAdapter(Upper(kOot3dEurCodeBinSha256)) == TitleAdapter::Oot3dEur,
          "upper-case OoT3D EUR hash not resolved");
    Check(SameCodeBinSha256(Upper(kOot3dEurCodeBinSha256), kOot3dEurCodeBinSha256),
          "SameCodeBinSha256 is case-sensitive on the actual hash");
    Check(std::string(TitleAdapterName(TitleAdapter::Oot3dEur)) == "oot3d_eur", "OoT3D adapter name");
#if !defined(__APPLE__) || defined(TRIAEVUM_TITLE_MM3D)
    // MM3D, any case; never OoT3D.
    Check(ResolveTitleAdapter(kMm3dCodeBinSha256) == TitleAdapter::Mm3d, "MM3D not resolved");
    Check(ResolveTitleAdapter(Upper(kMm3dCodeBinSha256)) == TitleAdapter::Mm3d, "upper-case MM3D hash not resolved");
    Check(!SameCodeBinSha256(kMm3dCodeBinSha256, kOot3dEurCodeBinSha256), "MM3D hash matches OoT3D's");
#endif
    // Negatives: an unknown hash, a truncated or extended one, an empty one.
    const std::string unknown(64, '0');
    Check(ResolveTitleAdapter(unknown) == TitleAdapter::Generic, "an unknown hash resolved to a title");
    Check(ResolveTitleAdapter(kOot3dEurCodeBinSha256.substr(0, 63)) == TitleAdapter::Generic,
          "a truncated OoT3D hash resolved");
    Check(ResolveTitleAdapter(std::string(kOot3dEurCodeBinSha256) + "0") == TitleAdapter::Generic,
          "an extended OoT3D hash resolved");
    Check(ResolveTitleAdapter("") == TitleAdapter::Generic, "an empty hash resolved");
    std::string oneOff(kOot3dEurCodeBinSha256);
    oneOff.back() = oneOff.back() == '0' ? '1' : '0';
    Check(ResolveTitleAdapter(oneOff) == TitleAdapter::Generic, "a one-digit-off hash resolved");
    Check(std::string(TitleAdapterName(TitleAdapter::Generic)) == "generic", "generic adapter name");

    // The OoT3D gates (a3fc81ad) are on for the EUR adapter, off elsewhere.
    Check(TitleReadsOot3dSceneTime(TitleAdapter::Oot3dEur), "OoT3D night/scene time gate is off");
    Check(TitleReadsOot3dPlayerActor(TitleAdapter::Oot3dEur), "OoT3D player gate is off");
    Check(TitleHasOot3dDeveloperTools(TitleAdapter::Oot3dEur), "OoT3D developer-tools gate is off");
    for (const auto other : {TitleAdapter::Mm3d, TitleAdapter::Generic}) {
        Check(!TitleReadsOot3dSceneTime(other), "another title reads OoT3D's night/scene time");
        Check(!TitleReadsOot3dPlayerActor(other), "another title reads OoT3D's player actor");
        Check(!TitleHasOot3dDeveloperTools(other), "another title gets OoT3D's developer tools");
    }
    Check(TitlePublishesSceneView(TitleAdapter::Oot3dEur) && TitleAppliesSceneFov(TitleAdapter::Oot3dEur),
          "OoT3D scene view gates are off");
    Check(!TitlePublishesSceneView(TitleAdapter::Generic), "a generic title publishes a scene view");

    if (failures != 0) return 1;
    std::cout << "triaevum_title_adapter_tests: ok\n";
    return 0;
}
