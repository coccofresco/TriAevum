// MM3D's startup uber module list: the list format, the selection both the
// runtime and Forge's helper use, the session recorder, and the embedded
// title data (a fixture list here, CMake/oot3d/game.cmake).
#include "mm3d_startup_uber_modules.h"
#include "fast/extension/pica_uber_fragment_binaries.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "mm3d_startup_uber_modules_tests: " << message << '\n';
        ++failures;
    }
}

using Fast::Extension::PicaUberModuleKey;

void TestKeyRoundTrip() {
    const PicaUberModuleKey key{0x0123456789abcdefULL, 0xfedcba9876543210ULL, 0x1a2bULL};
    const auto text = Fast::Extension::FormatPicaUberModuleKey(key);
    Require(text == "0123456789abcdef-fedcba9876543210-1a2b", "key format");
    const auto parsed = Fast::Extension::ParsePicaUberModuleKey(text);
    Require(parsed && *parsed == key, "key round trip");
    for (const char* bad : {"", "12-34", "12-34-", "x2-34-56", "12-34-0", "0-34-56", "12-34-56-78",
                            "ABCDEF-12-34", "11112222333344445-1-1"})
        Require(!Fast::Extension::ParsePicaUberModuleKey(bad), bad);
}

void TestListParsing() {
    std::vector<PicaUberModuleKey> modules;
    std::string error;
    Require(Fast::Extension::ParseStartupUberModules(
                R"({"schema":"triaevum.startup_uber_modules.v1","modules":["2-3-4","1-2-3","2-3-4"]})", modules,
                &error),
            "valid list rejected");
    Require(modules.size() == 2 && modules[0] == PicaUberModuleKey{1, 2, 3} && modules[1] == PicaUberModuleKey{2, 3, 4},
            "list not sorted and deduplicated");
    Require(Fast::Extension::ParseStartupUberModules(
                R"({"schema":"triaevum.startup_uber_modules.v1","modules":[]})", modules, &error) &&
                modules.empty(),
            "empty list rejected");
    for (const char* bad : {"", "[]", "{}", R"({"schema":"other","modules":[]})",
                            R"({"schema":"triaevum.startup_uber_modules.v1"})",
                            R"({"schema":"triaevum.startup_uber_modules.v1","modules":[1]})",
                            R"({"schema":"triaevum.startup_uber_modules.v1","modules":["1-2"]})"}) {
        error.clear();
        Require(!Fast::Extension::ParseStartupUberModules(bad, modules, &error) && modules.empty() && !error.empty(),
                bad);
    }
    // The written form reads back.
    const std::vector<PicaUberModuleKey> written{{9, 8, 7}, {1, 2, 3}, {9, 8, 7}};
    Require(Fast::Extension::ParseStartupUberModules(Fast::Extension::FormatStartupUberModules(written), modules,
                                                     &error) &&
                modules.size() == 2 && modules[0] == PicaUberModuleKey{1, 2, 3},
            "format does not round trip");
    // The title reader: absent data is no list; malformed data throws.
    Require(Oot3dNativeGame::Mm3dStartupUberModules(std::span<const unsigned char>{}).empty(), "absent data");
    const std::string malformed = "{";
    bool threw = false;
    try {
        (void)Oot3dNativeGame::Mm3dStartupUberModules(std::span<const unsigned char>(
            reinterpret_cast<const unsigned char*>(malformed.data()), malformed.size()));
    } catch (const std::exception&) {
        threw = true;
    }
    Require(threw, "malformed title data accepted");
}

void TestSelection() {
    const auto family = Fast::Extension::StartupUberFragmentArtifacts(true, true);
    Require(family.size() == 140U, "MM3D family is not legacy 36 + native-lighting 104");
    Require(Fast::Extension::StartupUberFragmentArtifacts(false, false).size() == 36U, "legacy family changed");
    // No list: the whole family, in its order (today's startup work).
    const auto all = Fast::Extension::SelectStartupUberModules(family, {});
    Require(all.size() == family.size(), "empty list does not select the whole family");
    for (size_t i = 0; i < all.size() && i < family.size(); ++i)
        Require(all[i].Spirv.data() == family[i].Spirv.data(), "whole family reordered");
    // A list: only those modules, in family order; unknown keys select nothing.
    const std::vector<PicaUberModuleKey> listed{family[120].Source, family[3].Source, {1, 2, 3}};
    const auto some = Fast::Extension::SelectStartupUberModules(family, listed);
    Require(some.size() == 2 && some[0].Spirv.data() == family[3].Spirv.data() &&
                some[1].Spirv.data() == family[120].Spirv.data(),
            "listed selection wrong");
    // The pack key is the key the list names.
    for (const auto& artifact : family) Require(artifact.Source.Available(), "family module without a key");
}

void TestRecorder() {
    const auto directory = std::filesystem::temp_directory_path() / "mm3d_startup_uber_modules_tests";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto path = directory / "recorded.json";
    {
        Fast::Extension::PicaUberModuleRecorder disabled{std::filesystem::path{}};
        Require(!disabled.Enabled() && !disabled.Record({1, 2, 3}), "disabled recorder recorded");
    }
    {
        Fast::Extension::PicaUberModuleRecorder recorder(path);
        Require(recorder.Record({5, 6, 7}), "first module not new");
        Require(!recorder.Record({5, 6, 7}), "repeated module recorded twice");
        Require(!recorder.Record({0, 6, 7}), "unavailable key recorded");
        Require(recorder.Record({1, 2, 3}), "second module not new");
    }
    std::vector<PicaUberModuleKey> modules;
    std::string error;
    {
        std::ifstream input(path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        Require(Fast::Extension::ParseStartupUberModules(text, modules, &error) && modules.size() == 2,
                "recorded file is not a title list");
    }
    // A later session merges with the file.
    {
        Fast::Extension::PicaUberModuleRecorder recorder(path);
        Require(recorder.Modules().size() == 2, "existing record not merged");
        Require(recorder.Record({9, 9, 9}) && recorder.Modules().size() == 3, "merged record");
    }
    std::filesystem::remove_all(directory);
}

void TestEmbeddedFixture() {
#if defined(EXPECT_STARTUP_LIST) && EXPECT_STARTUP_LIST
    const auto modules = Oot3dNativeGame::Mm3dStartupUberModules();
    Require(modules.size() == 1 && modules[0] == PicaUberModuleKey{0x0123456789abcdefULL, 0xfedcba9876543210ULL, 0x1a2bULL},
            "embedded fixture list not read");
#else
    // The tree's own list, when it has one, is valid title data and names
    // modules of MM3D's family (absent: no list, the whole family).
    try {
        const auto modules = Oot3dNativeGame::Mm3dStartupUberModules();
        const auto family = Fast::Extension::StartupUberFragmentArtifacts(true, true);
        Require(Fast::Extension::SelectStartupUberModules(family, modules).size() ==
                    (modules.empty() ? family.size() : modules.size()),
                "the tree's list names modules MM3D's family does not hold");
    } catch (const std::exception& error) {
        Require(false, error.what());
    }
#endif
}
} // namespace

int main() {
    TestKeyRoundTrip();
    TestListParsing();
    TestSelection();
    TestRecorder();
    TestEmbeddedFixture();
    if (failures != 0) return 1;
    std::cout << "mm3d_startup_uber_modules_tests: ok\n";
    return 0;
}
