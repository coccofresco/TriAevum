// G4a-F3 / R1: developer overrides are inert in a product build and the
// TRIAEVUM_A32_PC_DUMP parser never throws. Built twice: as the product
// (triaevum_developer_overrides_tests) and with TRIAEVUM_DEVELOPER_TOOLS
// (triaevum_developer_overrides_dev_tests), the negative control proving the
// same variables are read when the tools are present.
#include "triaevum_developer_overrides.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Oot3dNativeGame;

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) {
        std::cerr << "triaevum_developer_overrides_tests: " << message << '\n';
        ++failures;
    }
}
void Set(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}
} // namespace

int main() {
    for (const auto name : kDeveloperOverrideNames) {
        const std::string text(name);
        Set(text.c_str(), "set-by-test");
        const char* value = DeveloperOverride(text.c_str());
        if (kDeveloperOverridesEnabled)
            Check(value != nullptr && std::string(value) == "set-by-test",
                  "a developer build does not read its override");
        else
            Check(value == nullptr, "a product build reads a developer override");
    }
    // A malformed TRIAEVUM_A32_PC_DUMP is refused without throwing.
    std::string error;
    try {
        Check(!ParseA32PcDumpSpec("bad", &error) && !error.empty(), "'bad' parsed");
        Check(!ParseA32PcDumpSpec("zz,10,dir,100:10", &error), "a non-hex PC parsed");
        Check(!ParseA32PcDumpSpec("100,ten,dir,100:10", &error), "a non-decimal frame parsed");
        Check(!ParseA32PcDumpSpec("100,10,dir,100", &error), "a range without size parsed");
        Check(!ParseA32PcDumpSpec("100,10,dir,100:1q", &error), "a non-hex size parsed");
        Check(!ParseA32PcDumpSpec("100,10,,100:10", &error), "an empty directory parsed");
        Check(!ParseA32PcDumpSpec("1FFFFFFFF,10,dir,100:10", &error), "an overflowing PC parsed");
    } catch (...) {
        Check(false, "the parser threw");
    }
    const auto spec = ParseA32PcDumpSpec("0012ab40,300,out/dump,8000:40;10000:1000");
    Check(spec.has_value(), "a valid spec was refused");
    if (spec) {
        Check(spec->Address == 0x12ab40U && spec->Frame == 300U && spec->Directory == "out/dump",
              "PC, frame or directory misread");
        Check(spec->Ranges.size() == 2U && spec->Ranges[0].first == 0x8000U && spec->Ranges[0].second == 0x40U &&
                  spec->Ranges[1].first == 0x10000U && spec->Ranges[1].second == 0x1000U,
              "ranges misread");
    }
    if (failures != 0) return 1;
    std::cout << "triaevum_developer_overrides_tests: ok (developer_overrides="
              << (kDeveloperOverridesEnabled ? "on" : "off") << ")\n";
    return 0;
}
