#include "oot3d/renderer/azahar_texture_pack.h"
#include "fast/extension/azahar_texture_pack_import.h"
#include "fast/extension/portable_path_setting.h"
#include "../src/fast/extension/azahar_pbr_materials.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

namespace {

class Oot3dAzaharTexturePackTest : public testing::Test {
  protected:
    void SetUp() override {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() / ("oot3d_azahar_texture_pack_" + std::to_string(suffix));
        std::filesystem::create_directories(Root);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(Root, ignored);
    }

    std::filesystem::path Root;
};

TEST(Oot3dAzaharTexturePack, MatchesAzaharCityHashVectors) {
    using Oot3d::Renderer::AzaharCityHash64;
    const auto bytes = [](std::string_view text) {
        return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    };
    EXPECT_EQ(AzaharCityHash64(bytes("")), 0x9AE16A3B2F90404FULL);
    EXPECT_EQ(AzaharCityHash64(bytes("a")), 0xB3454265B6DF75E3ULL);
    EXPECT_EQ(AzaharCityHash64(bytes("abc")), 0x24A5B3A074E7F369ULL);
    EXPECT_EQ(AzaharCityHash64(bytes("hello")), 0xB48BE5A931380CE8ULL);

    std::array<uint8_t, 256> sequence{};
    for (size_t index = 0; index < sequence.size(); ++index) {
        sequence[index] = static_cast<uint8_t>(index);
    }
    EXPECT_EQ(AzaharCityHash64(sequence), 0x0C693049D8C2C68DULL);
}

TEST(Oot3dAzaharTexturePack, UsesExactDumpFilenameContract) {
    EXPECT_EQ(Oot3d::Renderer::MakeAzaharTextureFilename(128U, 128U, 0x083287DDE382AE8FULL, 13U, 0U),
              "tex1_128x128_083287DDE382AE8F_13_mip0.png");
}

// Issue #122: a replacement carries its own mip chain, gamma-correct and
// alpha-weighted, and a cut-out keeps its alpha-tested coverage per level.
TEST(Oot3dAzaharTexturePack, BuildsReplacementMipChain) {
    using Oot3d::Renderer::BuildAzaharReplacementMipChain;
    // 8x4: levels 4x2, 2x1, 1x1. Black and white halves average to the
    // linear midpoint (sRGB 188), not 128.
    std::vector<uint8_t> stripes(8U * 4U * 4U, 255U);
    for (uint32_t y = 0; y < 4U; ++y)
        for (uint32_t x = 0; x < 8U; x += 2U)
            for (int channel = 0; channel < 3; ++channel) stripes[(y * 8U + x) * 4U + channel] = 0U;
    uint32_t levels = 0U;
    const auto chain = BuildAzaharReplacementMipChain(stripes, 8U, 4U, levels);
    EXPECT_EQ(levels, 4U);
    ASSERT_EQ(chain.size(), (4U * 2U + 2U * 1U + 1U) * 4U);
    EXPECT_NEAR(chain[0], 188, 1);
    EXPECT_EQ(chain[3], 255U);
    EXPECT_NEAR(chain[chain.size() - 4U], 188, 1);

    // A fully transparent texel adds no colour: red beside invisible blue.
    std::vector<uint8_t> fringe{255U, 0U, 0U, 255U, 0U, 0U, 255U, 0U};
    const auto fringeChain = BuildAzaharReplacementMipChain(fringe, 2U, 1U, levels);
    ASSERT_EQ(levels, 2U);
    EXPECT_EQ(fringeChain[0], 255U);
    EXPECT_EQ(fringeChain[2], 0U);

    // A 64x64 cut-out with sparse one-texel blades (a quarter covered): plain
    // averaging would leave every coarse texel below one half (the blades
    // vanish); the scaled alpha keeps about a quarter of each level covered.
    std::vector<uint8_t> blades(64U * 64U * 4U, 0U);
    for (uint32_t y = 0; y < 64U; ++y)
        for (uint32_t x = 0; x < 64U; ++x)
            if ((x % 2U) == 0U && (y % 2U) == 0U) blades[(y * 64U + x) * 4U + 3U] = 255U;
    const auto bladeChain = BuildAzaharReplacementMipChain(blades, 64U, 64U, levels);
    ASSERT_EQ(levels, 7U);
    size_t offset = 0U;
    for (uint32_t level = 1U; level < 4U; ++level) {
        const uint32_t size = 64U >> level;
        size_t covered = 0U;
        for (size_t texel = 0; texel < size_t{size} * size; ++texel)
            covered += bladeChain[offset + texel * 4U + 3U] >= 128U ? 1U : 0U;
        EXPECT_GT(covered, 0U) << "level " << level;
        offset += size_t{size} * size * 4U;
    }
}

TEST(Oot3dAzaharTexturePack, RepeatedWorkerStartupAndShutdownIsStable) {
    for (size_t iteration = 0; iteration < 256U; ++iteration) {
        Oot3d::Renderer::AzaharTexturePackRuntime runtime;
    }
}

TEST(Oot3dAzaharTexturePack, MatchesLegacyDetiledHash) {
    std::array<uint8_t, 256> native{};
    for (size_t index = 0; index < native.size(); ++index) {
        native[index] = static_cast<uint8_t>(index);
    }
    std::array<uint8_t, 256> rgba{};
    const Oot3d::Renderer::AzaharTextureRequest request{ 8U, 8U, 0U, 0U, native, rgba };
    std::string error;
    EXPECT_EQ(Oot3d::Renderer::ComputeAzaharTextureHash(request, false, &error), 0x34487FA49AF68E6BULL) << error;
}

TEST_F(Oot3dAzaharTexturePackTest, DumpsAndLoadsModernAzaharPngWithoutOrientationLoss) {
    constexpr uint64_t titleId = 0x0004000000000042ULL;
    std::array<uint8_t, 256> native{};
    for (size_t index = 0; index < native.size(); ++index) {
        native[index] = static_cast<uint8_t>((index * 37U) & 0xFFU);
    }
    std::vector<uint8_t> rgba(8U * 8U * 4U);
    for (uint32_t y = 0; y < 8U; ++y) {
        for (uint32_t x = 0; x < 8U; ++x) {
            const size_t offset = (static_cast<size_t>(y) * 8U + x) * 4U;
            rgba[offset] = static_cast<uint8_t>(y * 31U);
            rgba[offset + 1U] = static_cast<uint8_t>(x * 29U);
            rgba[offset + 2U] = static_cast<uint8_t>(x + y);
            rgba[offset + 3U] = 255U;
        }
    }
    const Oot3d::Renderer::AzaharTextureRequest request{ 8U, 8U, 0U, 0U, native, rgba };
    const uint64_t hash = Oot3d::Renderer::AzaharCityHash64(native);
    const auto dumpDirectory = Root / "chosen_dump";

    Oot3d::Renderer::AzaharTexturePackRuntime runtime;
    runtime.Configure({
        .DumpTextures = true,
        .LoadCustomTextures = false,
        .TitleId = titleId,
        .UserDirectory = Root,
        .DumpDirectory = dumpDirectory,
    });
    EXPECT_EQ(runtime.ResolveAndMaybeDump(request), nullptr);
    runtime.WaitForPendingDumps();

    const auto status = runtime.Snapshot();
    const auto filename = Oot3d::Renderer::MakeAzaharTextureFilename(8U, 8U, hash, 0U, 0U);
    const auto dumped = status.DumpDirectory / filename;
    EXPECT_EQ(status.DumpDirectory, dumpDirectory);
    ASSERT_TRUE(std::filesystem::exists(dumped));
    ASSERT_TRUE(std::filesystem::exists(status.DumpDirectory / "pack.json"));
    EXPECT_EQ(status.DumpedTextures, 1U);

    const auto loadDirectory = Root / "chosen_load";
    std::filesystem::create_directories(loadDirectory);
    std::filesystem::copy_file(dumped, loadDirectory / filename);
    std::filesystem::copy_file(status.DumpDirectory / "pack.json", loadDirectory / "pack.json");

    runtime.Configure({
        .DumpTextures = false,
        .LoadCustomTextures = true,
        .TitleId = titleId,
        .UserDirectory = Root,
        .LoadDirectory = loadDirectory,
    });
    const auto replacement = runtime.ResolveAndMaybeDump(request);
    ASSERT_NE(replacement, nullptr);
    ASSERT_NE(replacement->Rgba8, nullptr);
    EXPECT_EQ(replacement->Width, 8U);
    EXPECT_EQ(replacement->Height, 8U);
    EXPECT_EQ(*replacement->Rgba8, rgba);
    EXPECT_EQ(replacement->SourcePath, loadDirectory / filename);
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, loadDirectory);
    EXPECT_EQ(runtime.Snapshot().LoadedTextures, 1U);
}

TEST_F(Oot3dAzaharTexturePackTest, IndexesThreeFieldAndMixedCaseFilenames) {
    constexpr uint64_t titleId = 0x0004000000000042ULL;
    std::array<uint8_t, 256> native{};
    for (size_t index = 0; index < native.size(); ++index) {
        native[index] = static_cast<uint8_t>((index * 53U) & 0xFFU);
    }
    std::vector<uint8_t> rgba(8U * 8U * 4U, 0x7FU);
    const Oot3d::Renderer::AzaharTextureRequest request{ 8U, 8U, 0U, 0U, native, rgba };
    const uint64_t hash = Oot3d::Renderer::AzaharCityHash64(native);
    const auto dumpDirectory = Root / "tolerant_dump";

    Oot3d::Renderer::AzaharTexturePackRuntime runtime;
    runtime.Configure({
        .DumpTextures = true,
        .LoadCustomTextures = false,
        .TitleId = titleId,
        .UserDirectory = Root,
        .DumpDirectory = dumpDirectory,
    });
    EXPECT_EQ(runtime.ResolveAndMaybeDump(request), nullptr);
    runtime.WaitForPendingDumps();
    const auto dumped = dumpDirectory / Oot3d::Renderer::MakeAzaharTextureFilename(8U, 8U, hash, 0U, 0U);
    ASSERT_TRUE(std::filesystem::exists(dumped));

    // Older Citra packs: tex1_WxH_HASH without format and mip fields; hand
    // renamed files: upper-case prefix, lower-case hash. No pack.json mapping.
    std::ostringstream hex;
    hex << std::hex << std::setw(16) << std::setfill('0') << hash;
    const std::array<std::string, 2> variants{
        "tex1_8x8_" + hex.str() + ".png",
        "TEX1_8X8_" + hex.str() + "_0_MIP0.png",
    };
    for (const auto& variant : variants) {
        EXPECT_EQ(Oot3d::Renderer::ParseAzaharTextureFilenameHash(std::filesystem::path(variant)), hash) << variant;
    }
    for (size_t variant = 0; variant < variants.size(); ++variant) {
        const auto loadDirectory = Root / ("tolerant_load_" + std::to_string(variant));
        std::filesystem::create_directories(loadDirectory);
        std::filesystem::copy_file(dumped, loadDirectory / variants[variant]);
        std::filesystem::copy_file(dumpDirectory / "pack.json", loadDirectory / "pack.json");
        runtime.Configure({
            .DumpTextures = false,
            .LoadCustomTextures = true,
            .TitleId = titleId,
            .UserDirectory = Root,
            .LoadDirectory = loadDirectory,
        });
        const auto replacement = runtime.ResolveAndMaybeDump(request);
        ASSERT_NE(replacement, nullptr) << variants[variant];
        EXPECT_EQ(replacement->SourcePath, loadDirectory / variants[variant]);
    }
    EXPECT_FALSE(Oot3d::Renderer::ParseAzaharTextureFilenameHash(std::filesystem::path("tex1_8x8_zz.png")).has_value());
    EXPECT_FALSE(Oot3d::Renderer::ParseAzaharTextureFilenameHash(std::filesystem::path("texture_8x8_0000000000000001.png")).has_value());
}

TEST_F(Oot3dAzaharTexturePackTest, BaseDirectoriesLoadBeneathTheUserPackInOrder) {
    std::array<uint8_t, 256> native{};
    native.fill(37U);
    const uint64_t hash = Oot3d::Renderer::AzaharCityHash64(native);
    const auto filename = Oot3d::Renderer::MakeAzaharTextureFilename(8U, 8U, hash, 0U, 0U);
    const auto writePack = [&](const std::filesystem::path& directory, uint8_t red) {
        std::filesystem::create_directories(directory / "nested");
        std::vector<uint8_t> rgba(8U * 8U * 4U, 255U);
        for (size_t index = 0; index < rgba.size(); index += 4U) rgba[index] = red;
        Oot3d::Renderer::WriteAzaharTexturePng(directory / "nested" / filename, 8U, 8U, rgba);
    };
    const auto exact = Root / "base" / "languages" / "08_EU_ITALIAN";
    const auto common = Root / "base" / "common";
    const auto user = Root / "user_pack";
    writePack(exact, 11U);
    writePack(common, 22U);
    writePack(user, 33U);
    std::vector<uint8_t> pixels(8U * 8U * 4U, 0U);
    const Oot3d::Renderer::AzaharTextureRequest request{8U, 8U, 0U, 0U, native, pixels};
    const auto red = [&](Oot3d::Renderer::AzaharTexturePackRuntime& runtime) {
        const auto replacement = runtime.ResolveAndMaybeDump(request);
        return replacement != nullptr ? (*replacement->Rgba8)[0] : uint8_t{0};
    };

    Oot3d::Renderer::AzaharTexturePackRuntime runtime;
    runtime.SetBaseDirectories({exact, common});
    runtime.Configure({.LoadCustomTextures = false, .UserDirectory = Root, .LoadDirectory = user});
    runtime.WaitForPendingLoads();
    EXPECT_EQ(red(runtime), 11U) << "the first base directory must win, user pack off";
    runtime.SetBaseDirectories({common});
    runtime.WaitForPendingLoads();
    EXPECT_EQ(red(runtime), 22U) << "a new base list must replace the previous one";
    runtime.Configure({.LoadCustomTextures = true, .UserDirectory = Root, .LoadDirectory = user});
    runtime.WaitForPendingLoads();
    EXPECT_EQ(red(runtime), 33U) << "the user's pack must win over base packs";
    runtime.SetBaseDirectories({});
    runtime.Configure({.LoadCustomTextures = false, .UserDirectory = Root, .LoadDirectory = user});
    runtime.WaitForPendingLoads();
    EXPECT_EQ(runtime.ResolveOrQueue(request).State, Oot3d::Renderer::AzaharTextureResolveState::Disabled);

    // A user pack in the other hash mode keeps loading; the base pack is left
    // out and the status says so.
    std::ofstream(user / "pack.json") << R"({"options":{"use_new_hash":true}})";
    runtime.SetBaseDirectories({common});
    runtime.Configure({.LoadCustomTextures = true, .UserDirectory = Root, .LoadDirectory = user});
    runtime.WaitForPendingLoads();
    const auto status = runtime.Snapshot();
    EXPECT_NE(status.LastError.find("another hash mode"), std::string::npos) << status.LastError;
    EXPECT_EQ(status.IndexedTextures, 1U) << "the user's pack must stay loaded";
}

// Sets (or, with an empty value, removes) an environment variable for one
// test and restores the previous value.
class ScopedEnvironment {
  public:
    ScopedEnvironment(const char* name, const std::string& value) : mName(name) {
        if (const char* previous = std::getenv(name); previous != nullptr) mPrevious = previous;
        Set(value);
    }
    ~ScopedEnvironment() { Set(mPrevious.value_or(std::string{})); }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

  private:
    void Set(const std::string& value) {
#ifdef _WIN32
        _putenv_s(mName, value.c_str());
#else
        if (value.empty()) unsetenv(mName);
        else setenv(mName, value.c_str(), 1);
#endif
    }
    const char* mName;
    std::optional<std::string> mPrevious;
};

// UX plan WP-05C: the host's managed defaults and installed title rank below
// the environment, the F1 folders and a named user folder; an existing pack in
// Azahar's own folder keeps loading; unrelated settings changes keep the pack.
TEST_F(Oot3dAzaharTexturePackTest, HostDefaultsRankBelowEveryChoiceAndKeepAnInstalledPack) {
    constexpr uint64_t usaTitle = 0x0004000000033500ULL;
#ifdef _WIN32
    ScopedEnvironment azaharHome("APPDATA", Root.string());
    const auto azahar = Root / "Azahar";
#else
    ScopedEnvironment azaharHome("XDG_DATA_HOME", Root.string());
    const auto azahar = Root / "azahar";
#endif
    ScopedEnvironment noUser("OOT3D_AZAHAR_USER_DIRECTORY", "");
    ScopedEnvironment noLoad("OOT3D_AZAHAR_LOAD_DIRECTORY", "");
    ScopedEnvironment noDump("OOT3D_AZAHAR_DUMP_DIRECTORY", "");
    ScopedEnvironment noTitle("OOT3D_AZAHAR_TITLE_ID", "");
    const auto managedLoad = Root / "data" / "textures" / "load" / "0004000000033500";
    const auto managedDump = Root / "data" / "textures" / "dump" / "0004000000033500";

    Oot3d::Renderer::AzaharTexturePackRuntime runtime;
    // Without host defaults (an older launch profile): Azahar's folder, EUR.
    runtime.Configure({.LoadCustomTextures = true});
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, azahar / "load" / "textures" / "0004000000033600");
    EXPECT_EQ(runtime.Snapshot().DumpDirectory, azahar / "dump" / "textures" / "0004000000033600");

    // The host's defaults reach a configuration made before them, and every
    // later settings Configure keeps them.
    runtime.SetHostDefaults({.TitleId = usaTitle, .LoadDirectory = managedLoad, .DumpDirectory = managedDump});
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, managedLoad);
    EXPECT_EQ(runtime.Snapshot().DumpDirectory, managedDump);
    const auto generation = runtime.Generation();
    runtime.Configure({.LoadCustomTextures = true});
    EXPECT_EQ(runtime.Generation(), generation) << "an unrelated settings change must not reload the pack";
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, managedLoad);

    // F1 folders win over the managed defaults.
    const auto chosen = Root / "chosen";
    runtime.Configure({.LoadCustomTextures = true, .LoadDirectory = chosen});
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, chosen);
    EXPECT_EQ(runtime.Snapshot().DumpDirectory, managedDump);
    {
        // The environment wins over F1.
        ScopedEnvironment load("OOT3D_AZAHAR_LOAD_DIRECTORY", (Root / "from_environment").string());
        runtime.Configure({.LoadCustomTextures = true, .LoadDirectory = chosen, .DumpDirectory = Root / "dumps"});
        EXPECT_EQ(runtime.Snapshot().LoadDirectory, Root / "from_environment");
        EXPECT_EQ(runtime.Snapshot().DumpDirectory, Root / "dumps");
    }
    {
        // A named user folder supplies a root, below F1 and above the managed defaults.
        ScopedEnvironment user("OOT3D_AZAHAR_USER_DIRECTORY", (Root / "user_root").string());
        runtime.Configure({.LoadCustomTextures = true});
        EXPECT_EQ(runtime.Snapshot().LoadDirectory, Root / "user_root" / "load" / "textures" / "0004000000033500");
        runtime.Configure({.LoadCustomTextures = true, .LoadDirectory = chosen});
        EXPECT_EQ(runtime.Snapshot().LoadDirectory, chosen);
    }
    {
        // OOT3D_AZAHAR_TITLE_ID names the folder over the installed title.
        ScopedEnvironment title("OOT3D_AZAHAR_TITLE_ID", "0004000000033600");
        runtime.SetHostDefaults({.TitleId = usaTitle});
        runtime.Configure({.LoadCustomTextures = true});
        EXPECT_EQ(runtime.Snapshot().LoadDirectory, azahar / "load" / "textures" / "0004000000033600");
    }

    // A pack already installed in Azahar's folder under the installed title
    // keeps loading; dumps go to the managed default.
    const auto installed = azahar / "load" / "textures" / "0004000000033500";
    std::filesystem::create_directories(installed);
    runtime.SetHostDefaults({.TitleId = usaTitle, .LoadDirectory = managedLoad, .DumpDirectory = managedDump});
    runtime.Configure({.DumpTextures = false, .LoadCustomTextures = true});
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, installed);
    EXPECT_EQ(runtime.Snapshot().DumpDirectory, managedDump);
    EXPECT_EQ(runtime.Snapshot().TitleId, usaTitle);
    EXPECT_EQ(runtime.HostDefaults().LoadDirectory, managedLoad);
    EXPECT_EQ(runtime.HostDefaults().TitleId, std::optional<uint64_t>(usaTitle));

    // UX plan WP-07: a managed folder with only empty subfolders holds no
    // pack, so Azahar's keeps loading, also across a reload.
    std::filesystem::create_directories(managedLoad / "empty" / "nested");
    runtime.Reload();
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, installed);

    // Once the managed folder holds a pack it wins: a reload (F1's Import...
    // requests one) switches without a settings change, and a fresh
    // configuration resolves to it too. Azahar's folder is left alone.
    {
        std::ofstream file(managedLoad / "empty" / "nested" / "tex1_32x32_0123456789ABCDEF_13_mip0.png",
                           std::ios::binary);
        file << "x";
    }
    const auto beforeImport = runtime.Generation();
    runtime.Reload();
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, managedLoad);
    EXPECT_GT(runtime.Generation(), beforeImport);
    Oot3d::Renderer::AzaharTexturePackRuntime fresh;
    fresh.SetHostDefaults({.TitleId = usaTitle, .LoadDirectory = managedLoad, .DumpDirectory = managedDump});
    fresh.Configure({.LoadCustomTextures = true});
    EXPECT_EQ(fresh.Snapshot().LoadDirectory, managedLoad);
    EXPECT_TRUE(std::filesystem::is_directory(installed));
    // F1's explicit folder still wins over the managed pack.
    fresh.Configure({.LoadCustomTextures = true, .LoadDirectory = chosen});
    EXPECT_EQ(fresh.Snapshot().LoadDirectory, chosen);
    runtime.WaitForPendingLoads();
    fresh.WaitForPendingLoads();
}

// Issue #124: F1 folders follow a moved portable installation (<A>/data/config).
// A relative folder is read from the settings file's folder, never from the
// working folder; an absolute folder an earlier version stored inside the
// earlier location is found again; a missing folder outside stays as chosen.
TEST_F(Oot3dAzaharTexturePackTest, FolderSettingsFollowAMovedInstallation) {
    ScopedEnvironment noUser("OOT3D_AZAHAR_USER_DIRECTORY", "");
    ScopedEnvironment noLoad("OOT3D_AZAHAR_LOAD_DIRECTORY", "");
    ScopedEnvironment noDump("OOT3D_AZAHAR_DUMP_DIRECTORY", "");
    const auto installation = Root / "moved here" / "Tri Aevum";
    const auto settings = installation / "data" / "config";
    std::filesystem::create_directories(settings);
    std::filesystem::create_directories(installation / "packs" / "HD");

    EXPECT_TRUE(std::filesystem::equivalent(Fast::Extension::PortableInstallationRoot(settings), installation));
    EXPECT_TRUE(Fast::Extension::PortableInstallationRoot(Root / "elsewhere").empty())
        << "a developer settings folder has no installation folder";
    EXPECT_EQ(Fast::Extension::StorePathSetting(installation / "packs" / "HD", settings), "../../packs/HD");
    const auto outside = Root / "outside pack";
    EXPECT_EQ(Fast::Extension::StorePathSetting(outside, settings), outside.string());

    Oot3d::Renderer::AzaharTexturePackRuntime runtime;
    runtime.SetHostDefaults({.SettingsDirectory = settings});
    runtime.Configure({.LoadCustomTextures = true, .LoadDirectory = "../../packs/HD", .DumpDirectory = "../dumps"});
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, (installation / "packs" / "HD").lexically_normal());
    EXPECT_EQ(runtime.Snapshot().DumpDirectory, (installation / "data" / "dumps").lexically_normal());

    // Written by an earlier version at the earlier location (now gone).
    const auto earlier = Root / "Games" / "TriAevum";
    runtime.Configure({.LoadCustomTextures = true, .LoadDirectory = earlier / "packs" / "HD"});
    EXPECT_TRUE(std::filesystem::equivalent(runtime.Snapshot().LoadDirectory, installation / "packs" / "HD"));
    const auto unplugged = Root / "unplugged" / "packs" / "SD";
    runtime.Configure({.LoadCustomTextures = true, .LoadDirectory = unplugged});
    EXPECT_EQ(runtime.Snapshot().LoadDirectory, unplugged.lexically_normal());
    runtime.WaitForPendingLoads();
}

TEST_F(Oot3dAzaharTexturePackTest,
       QueuesReplacementDecodeWithoutBlockingTheCaller) {
    constexpr uint64_t titleId = 0x0004000000000044ULL;
    std::array<uint8_t, 256> native{};
    for (size_t index = 0; index < native.size(); ++index) {
        native[index] =
            static_cast<uint8_t>((index * 19U) & 0xFFU);
    }
    std::vector<uint8_t> rgba(8U * 8U * 4U);
    for (size_t index = 0; index < rgba.size(); index += 4U) {
        rgba[index] = static_cast<uint8_t>(index);
        rgba[index + 1U] = 91U;
        rgba[index + 2U] = 173U;
        rgba[index + 3U] = 255U;
    }
    const Oot3d::Renderer::AzaharTextureRequest request{
        8U, 8U, 0U, 0U, native, rgba};
    const uint64_t hash =
        Oot3d::Renderer::AzaharCityHash64(native);

    Oot3d::Renderer::AzaharTexturePackRuntime runtime;
    runtime.Configure({
        .DumpTextures = true,
        .TitleId = titleId,
        .UserDirectory = Root,
    });
    (void)runtime.ResolveAndMaybeDump(request);
    runtime.WaitForPendingDumps();
    const auto dumped = runtime.Snapshot().DumpDirectory /
                        Oot3d::Renderer::MakeAzaharTextureFilename(
                            8U, 8U, hash, 0U, 0U);

    const auto loadDirectory = Root / "async_load";
    std::filesystem::create_directories(loadDirectory);
    std::filesystem::copy_file(
        dumped, loadDirectory / dumped.filename());
    std::filesystem::copy_file(
        runtime.Snapshot().DumpDirectory / "pack.json",
        loadDirectory / "pack.json");
    runtime.Configure({
        .LoadCustomTextures = true,
        .TitleId = titleId,
        .UserDirectory = Root,
        .LoadDirectory = loadDirectory,
    });
    runtime.WaitForPendingLoads();

    const auto queued = runtime.ResolveOrQueue(request);
    EXPECT_EQ(
        queued.State,
        Oot3d::Renderer::AzaharTextureResolveState::Pending);
    EXPECT_EQ(queued.NativeHash, hash);
    EXPECT_TRUE(queued.NativeHashAvailable);
    EXPECT_TRUE(queued.UseNewHash);
    EXPECT_EQ(queued.Replacement, nullptr);
    runtime.WaitForPendingLoads();

    const auto ready = runtime.PollQueued(hash);
    ASSERT_EQ(
        ready.State,
        Oot3d::Renderer::AzaharTextureResolveState::Ready);
    ASSERT_NE(ready.Replacement, nullptr);
    ASSERT_NE(ready.Replacement->Rgba8, nullptr);
    EXPECT_EQ(*ready.Replacement->Rgba8, rgba);
    EXPECT_EQ(runtime.Snapshot().PendingLoads, 0U);
    EXPECT_EQ(runtime.Snapshot().FailedLoads, 0U);

    // Authoring still needs the native pack identity when replacement loading is off.
    runtime.Configure({
        .LoadCustomTextures = false,
        .TitleId = titleId,
        .UserDirectory = Root,
        .LoadDirectory = loadDirectory,
    });
    runtime.WaitForPendingLoads();
    const auto disabled = runtime.ResolveOrQueue(request);
    EXPECT_EQ(disabled.State, Oot3d::Renderer::AzaharTextureResolveState::Disabled);
    ASSERT_TRUE(disabled.NativeHashAvailable);
    EXPECT_EQ(disabled.NativeHash, hash);
    EXPECT_TRUE(disabled.UseNewHash);
}

TEST_F(Oot3dAzaharTexturePackTest, HonorsPackJsonFilenameMappingAndFlipSemantics) {
    constexpr uint64_t titleId = 0x0004000000000043ULL;
    std::array<uint8_t, 256> native{};
    for (size_t index = 0; index < native.size(); ++index) {
        native[index] = static_cast<uint8_t>((index * 13U) & 0xFFU);
    }
    std::vector<uint8_t> rgba(8U * 8U * 4U, 255U);
    for (uint32_t y = 0; y < 8U; ++y) {
        for (uint32_t x = 0; x < 8U; ++x) {
            rgba[(static_cast<size_t>(y) * 8U + x) * 4U] = static_cast<uint8_t>(y * 20U + x);
        }
    }
    const Oot3d::Renderer::AzaharTextureRequest request{ 8U, 8U, 0U, 0U, native, rgba };
    const uint64_t hash = Oot3d::Renderer::AzaharCityHash64(native);

    Oot3d::Renderer::AzaharTexturePackRuntime runtime;
    runtime.Configure({
        .DumpTextures = true,
        .TitleId = titleId,
        .UserDirectory = Root,
    });
    (void)runtime.ResolveAndMaybeDump(request);
    runtime.WaitForPendingDumps();
    const auto dumpStatus = runtime.Snapshot();
    const auto dumped = dumpStatus.DumpDirectory / Oot3d::Renderer::MakeAzaharTextureFilename(8U, 8U, hash, 0U, 0U);

    const auto loadDirectory = Root / "load" / "textures" / "0004000000000043";
    const auto customPath = loadDirectory / "nested" / "custom.png";
    std::filesystem::create_directories(customPath.parent_path());
    std::filesystem::copy_file(dumped, customPath);
    const nlohmann::json pack{
        { "options",
          {
              { "skip_mipmap", false },
              { "flip_png_files", false },
              { "use_new_hash", true },
          } },
        { "textures",
          { { [hash] {
                 std::ostringstream output;
                 output << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << hash;
                 return output.str();
             }(),
              "nested/custom.png" } } },
    };
    {
        std::ofstream output(loadDirectory / "pack.json");
        output << pack.dump(2);
    }

    runtime.Configure({
        .LoadCustomTextures = true,
        .TitleId = titleId,
        .UserDirectory = Root,
    });
    const auto replacement = runtime.ResolveAndMaybeDump(request);
    ASSERT_NE(replacement, nullptr);
    ASSERT_NE(replacement->Rgba8, nullptr);
    for (uint32_t y = 0; y < 8U; ++y) {
        for (uint32_t x = 0; x < 8U; ++x) {
            const size_t actual = (static_cast<size_t>(y) * 8U + x) * 4U;
            const size_t expected = (static_cast<size_t>(7U - y) * 8U + x) * 4U;
            EXPECT_EQ((*replacement->Rgba8)[actual], rgba[expected]);
        }
    }
}

TEST_F(Oot3dAzaharTexturePackTest, MaterialReloadIsAtomicAndExplicitNullOverridesSuffixes) {
    using namespace Oot3d::Renderer;
    AzaharTexturePackRuntime runtime;
    const auto dump = Root / "dump";
    runtime.Configure({.DumpTextures = true, .UserDirectory = Root, .DumpDirectory = dump});
    std::array<uint8_t, 256> native{};
    std::array<uint8_t, 256> pixels{};
    const auto makeImage = [&](uint8_t value) {
        native.fill(value);
        pixels.fill(value);
        (void)runtime.ResolveAndMaybeDump({8, 8, 0, 0, native, pixels});
        runtime.WaitForPendingDumps();
        return dump / MakeAzaharTextureFilename(8, 8, AzaharCityHash64(native), 0, 0);
    };
    const auto normal = makeImage(127U);
    const auto orm = makeImage(63U);
    const auto emission = makeImage(191U);
    const auto height = makeImage(223U);
    native.fill(19U);
    const auto hash = AzaharCityHash64(native);
    std::ostringstream formatted;
    formatted << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << hash;
    const auto name = formatted.str();
    const auto pack = Root / "pack";
    const auto materials = pack / "materials";
    std::filesystem::create_directories(materials);
    std::filesystem::copy_file(dump / "pack.json", pack / "pack.json");
    std::filesystem::copy_file(normal, materials / (name + "_n.png"));
    std::filesystem::copy_file(orm, materials / (name + "_orm.png"));
    std::filesystem::copy_file(emission, materials / (name + "_e.png"));
    std::filesystem::copy_file(height, materials / (name + "_h.png"));
    runtime.Configure({.LoadCustomTextures = true, .UserDirectory = Root, .LoadDirectory = pack});
    runtime.WaitForPendingLoads();
    const auto generation = runtime.Generation();
    EXPECT_EQ(runtime.ResolveOrQueue({8, 8, 0, 0, native, pixels}).State, AzaharTextureResolveState::Missing);
    EXPECT_EQ(runtime.ResolveMaterialOrQueue(hash, generation).State, AzaharTextureResolveState::Pending);
    runtime.WaitForPendingLoads();
    const auto ready = runtime.ResolveMaterialOrQueue(hash, generation);
    ASSERT_EQ(ready.State, AzaharTextureResolveState::Ready);
    ASSERT_NE(ready.Material, nullptr);
    for (const auto& texture : ready.Material->Textures) ASSERT_NE(texture, nullptr);
    EXPECT_EQ((*ready.Material->Textures[0]->Rgba8)[0], 127U);
    EXPECT_EQ((*ready.Material->Textures[1]->Rgba8)[0], 63U);
    EXPECT_EQ((*ready.Material->Textures[2]->Rgba8)[0], 191U);
    EXPECT_EQ((*ready.Material->Textures[3]->Rgba8)[0], 223U);
    EXPECT_EQ(ready.Material->Identity.NativeHash, hash);
    EXPECT_TRUE(ready.Material->Identity.UseNewHash);
    const auto jsonPath = materials / (name + ".material.json");
    nlohmann::json metadata{{"schema", "triaevum.pbr-material"}, {"version", 1},
        {"orm", {{"texture", "missing.png"}}}};
    { std::ofstream output(jsonPath); output << metadata; }
    runtime.Reload();
    runtime.WaitForPendingLoads();
    const auto failed = runtime.ResolveMaterialOrQueue(hash, runtime.Generation());
    ASSERT_NE(failed.Material, nullptr);
    EXPECT_EQ(failed.Material->Generation, ready.Material->Generation);
    EXPECT_EQ(runtime.Snapshot().FailedMaterials, 1U);
    metadata["normal"] = nullptr;
    metadata["orm"] = {{"texture", nullptr}, {"metallicFactor", 0.75F}};
    metadata["height"] = nullptr;
    { std::ofstream output(jsonPath); output << metadata; }
    const auto colorPath = pack / MakeAzaharTextureFilename(8, 8, hash, 0, 0);
    std::filesystem::copy_file(emission, colorPath);
    runtime.Reload();
    runtime.WaitForPendingLoads();
    (void)runtime.ResolveMaterialOrQueue(hash, runtime.Generation());
    runtime.WaitForPendingLoads();
    const auto changed = runtime.ResolveMaterialOrQueue(hash, runtime.Generation());
    ASSERT_NE(changed.Material, nullptr);
    EXPECT_EQ(changed.Material->Textures[0], nullptr);
    EXPECT_EQ(changed.Material->Textures[1], nullptr);
    ASSERT_NE(changed.Material->Textures[2], nullptr);
    EXPECT_EQ(changed.Material->Textures[3], nullptr);
    ASSERT_NE(changed.Material->ColorReplacement, nullptr);
    EXPECT_EQ((*changed.Material->ColorReplacement->Rgba8)[0], 191U);
    EXPECT_EQ(changed.Material->Parameters.MetallicFactor, 0.75F);
    EXPECT_GT(changed.Material->Generation, ready.Material->Generation);
    EXPECT_EQ(runtime.ResolveMaterialOrQueue(hash, generation).State, AzaharTextureResolveState::Disabled);
    { std::ofstream brokenColor(colorPath, std::ios::binary | std::ios::trunc); brokenColor << "invalid PNG"; }
    runtime.Reload();
    runtime.WaitForPendingLoads();
    (void)runtime.ResolveMaterialOrQueue(hash, runtime.Generation());
    runtime.WaitForPendingLoads();
    const auto broken = runtime.ResolveMaterialOrQueue(hash, runtime.Generation());
    ASSERT_NE(broken.Material, nullptr);
    EXPECT_EQ(broken.Material->Generation, changed.Material->Generation);
    EXPECT_EQ((*broken.Material->ColorReplacement->Rgba8)[0], 191U);
    EXPECT_EQ(runtime.Snapshot().FailedMaterials, 1U);
}

TEST_F(Oot3dAzaharTexturePackTest, RejectsEscapingMaterialPathsAndNormalAlbedoCollisions) {
    using namespace Oot3d::Renderer;
    const auto pack = Root / "pack";
    std::filesystem::create_directories(pack);
    std::array<uint8_t, 256> native{};
    const auto hash = ComputeAzaharTextureHash({8, 8, 0, 0, native, {}}, false);
    std::ostringstream formatted;
    formatted << std::hex << hash;
    const auto name = formatted.str();
    { std::ofstream image(pack / ("tex1_8x8_" + name + "_0_n.png")); image << "not albedo"; }
    { std::ofstream outside(Root / "outside.png"); outside << "private"; }
    {
        std::ofstream metadata(pack / (name + ".material.json"));
        metadata << nlohmann::json{{"schema", "triaevum.pbr-material"}, {"version", 1},
            {"normal", {{"texture", "../outside.png"}}}};
    }
    AzaharTexturePackRuntime runtime;
    runtime.Configure({.LoadCustomTextures = true, .UserDirectory = Root, .LoadDirectory = pack});
    runtime.WaitForPendingLoads();
    EXPECT_EQ(runtime.ResolveOrQueue({8, 8, 0, 0, native, {}}).State, AzaharTextureResolveState::Missing);
    EXPECT_EQ(runtime.ResolveMaterialOrQueue(hash, runtime.Generation()).State, AzaharTextureResolveState::Failed);
    EXPECT_EQ(runtime.Snapshot().LoadedMaterials, 0U);
}

// UX plan WP-07: F1's Import... (azahar_texture_pack_import.cpp) follows the
// rules Forge's import shares (TEXTURE_IMPORT_RULES.md).
class AzaharTexturePackImportTest : public Oot3dAzaharTexturePackTest {
  protected:
    static constexpr uint64_t kUsa = 0x0004000000033500ULL;
    static constexpr const char* kTexture = "tex1_32x32_0123456789ABCDEF_13_mip0.png";

    void Png(const std::filesystem::path& path, uint32_t width = 2, uint32_t height = 2) {
        std::filesystem::create_directories(path.parent_path());
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4U, 0x7FU);
        Oot3d::Renderer::WriteAzaharTexturePng(path, width, height, pixels);
    }
    void Text(const std::filesystem::path& path, std::string_view text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    Fast::Extension::AzaharTexturePackImportResult Import(const std::filesystem::path& source) {
        return Fast::Extension::ImportAzaharTexturePack(
            {.Source = source, .Destination = Destination(), .InstalledTitleId = kUsa});
    }
    std::filesystem::path Destination() const { return Root / "data" / "textures" / "load" / "0004000000033500"; }
    // Nothing but the destination is left beside it (no staging/previous).
    bool OnlyDestinationBesideIt() const {
        for (const auto& entry : std::filesystem::directory_iterator(Destination().parent_path()))
            if (entry.path().filename() != Destination().filename()) return false;
        return true;
    }
};

TEST_F(AzaharTexturePackImportTest, ImportsTheInstalledTitleFolderAndSkipsOtherFiles) {
    const auto source = Root / "download";
    Png(source / "load" / "textures" / "0004000000033500" / "sub" / kTexture);
    Png(source / "load" / "textures" / "0004000000033500" / "untitled.png");
    Text(source / "load" / "textures" / "0004000000033500" / "pack.json", R"({"options":{"use_new_hash":true}})");
    Text(source / "load" / "textures" / "0004000000033500" / "readme.txt", "hello");
    Text(source / "load" / "textures" / "0004000000033500" / "tex1_8x8_00000000000000AA_0.dds", "DDS ");
    Png(source / "load" / "textures" / "0004000000033600" / kTexture);
    Text(source / "outside.txt", "not part of the pack");

    const auto result = Import(source);
    ASSERT_TRUE(result.Imported) << result.Message;
    EXPECT_EQ(result.Textures, 1U) << "untitled.png is copied but not loadable";
    EXPECT_EQ(result.NotLoadedYet, 1U);
    EXPECT_EQ(result.Skipped, 1U);
    EXPECT_EQ(result.SourceTitleId, std::optional<uint64_t>(kUsa));
    EXPECT_TRUE(std::filesystem::is_regular_file(Destination() / "sub" / kTexture));
    EXPECT_TRUE(std::filesystem::is_regular_file(Destination() / "untitled.png"));
    EXPECT_TRUE(std::filesystem::is_regular_file(Destination() / "pack.json"));
    EXPECT_FALSE(std::filesystem::exists(Destination() / "readme.txt"));
    EXPECT_NE(result.Message.find("Imported 1 texture into"), std::string::npos) << result.Message;
    EXPECT_TRUE(OnlyDestinationBesideIt());
}

TEST_F(AzaharTexturePackImportTest, ChoosesAmongTitleFoldersAndExplainsAnotherTitle) {
    const auto several = Root / "several";
    Png(several / "textures" / "0004000000033600" / kTexture);
    Png(several / "textures" / "000400000008F900" / kTexture);
    auto result = Import(several);
    EXPECT_FALSE(result.Imported);
    EXPECT_NE(result.Message.find("several games (0004000000033600, 000400000008F900)"), std::string::npos)
        << result.Message;
    EXPECT_NE(result.Message.find("installed game 0004000000033500"), std::string::npos) << result.Message;

    const auto duplicate = Root / "duplicate";
    Png(duplicate / "a" / "0004000000033500" / kTexture);
    Png(duplicate / "b" / "0004000000033500" / kTexture);
    result = Import(duplicate);
    EXPECT_FALSE(result.Imported);
    EXPECT_NE(result.Message.find("more than one folder for 0004000000033500"), std::string::npos) << result.Message;

    // One folder of another title is used: textures match by content.
    const auto european = Root / "0004000000033600";
    Png(european / kTexture);
    result = Import(european);
    ASSERT_TRUE(result.Imported) << result.Message;
    EXPECT_EQ(result.SourceTitleId, std::optional<uint64_t>(0x0004000000033600ULL));
    EXPECT_NE(result.Message.find("made for 0004000000033600"), std::string::npos) << result.Message;
    EXPECT_TRUE(std::filesystem::is_regular_file(Destination() / kTexture));
}

TEST_F(AzaharTexturePackImportTest, FlatPackLosesItsWrapperAndPackJsonMappingsCount) {
    const auto source = Root / "flat";
    Png(source / "My Pack" / "textures" / "grass.png");
    Text(source / "My Pack" / "pack.json", R"({"textures":{"0123456789ABCDEF":["textures/grass.png"]}})");
    const auto result = Import(source);
    ASSERT_TRUE(result.Imported) << result.Message;
    EXPECT_EQ(result.Textures, 1U);
    EXPECT_FALSE(result.SourceTitleId.has_value());
    EXPECT_TRUE(std::filesystem::is_regular_file(Destination() / "pack.json"));
    EXPECT_TRUE(std::filesystem::is_regular_file(Destination() / "textures" / "grass.png"));
}

TEST_F(AzaharTexturePackImportTest, InvalidPacksAreRefusedAndTheCurrentPackStays) {
    const auto good = Root / "good" / "0004000000033500";
    Png(good / kTexture);
    ASSERT_TRUE(Import(good).Imported);
    const auto check = [&](const std::filesystem::path& source, std::string_view expected) {
        const auto result = Import(source);
        EXPECT_FALSE(result.Imported) << source;
        EXPECT_NE(result.Message.find(expected), std::string::npos) << result.Message;
        EXPECT_TRUE(std::filesystem::is_regular_file(Destination() / kTexture)) << "the current pack must stay";
        EXPECT_TRUE(OnlyDestinationBesideIt());
    };
    const auto notPng = Root / "not_png";
    Png(notPng / "tex1_32x32_1111111111111111_13_mip0.png");
    Text(notPng / kTexture, "not a png at all, just text");
    check(notPng, std::string(kTexture) + " is not a PNG image");

    const auto huge = Root / "huge";
    Png(huge / kTexture, 16385, 1);
    check(huge, "16385x1 pixels");

    const auto badJson = Root / "bad_json";
    Png(badJson / kTexture);
    Text(badJson / "pack.json", "[1, 2]");
    check(badJson, "pack.json is not a JSON object");

    const auto noTextures = Root / "no_textures";
    Png(noTextures / "picture.png");
    Text(noTextures / "readme.txt", "x");
    check(noTextures, "This does not look like a texture pack");

    const auto missing = Root / "missing";
    check(missing, "Choose a folder that exists");

    const auto deep = Root / "deep";
    auto folder = deep;
    for (int level = 0; level < 33; ++level) folder /= "d";
    Png(folder / kTexture);
    check(deep, "more than 32 folders deep");

    const auto many = Root / "many";
    Png(many / kTexture);
    Png(many / "tex1_32x32_2222222222222222_13_mip0.png");
    Png(many / "tex1_32x32_3333333333333333_13_mip0.png");
    auto limited = Fast::Extension::AzaharTexturePackImportRequest{
        .Source = many, .Destination = Destination(), .InstalledTitleId = kUsa};
    limited.Limits.MaximumFiles = 2U;
    auto result = Fast::Extension::ImportAzaharTexturePack(limited);
    EXPECT_FALSE(result.Imported);
    EXPECT_NE(result.Message.find("more than 2 files"), std::string::npos) << result.Message;
    limited.Limits = {};
    limited.Limits.MaximumFileBytes = 16U;
    result = Fast::Extension::ImportAzaharTexturePack(limited);
    EXPECT_NE(result.Message.find("is larger than 16 bytes"), std::string::npos) << result.Message;
    limited.Limits = {};
    limited.Limits.MaximumTotalBytes = 100U;
    result = Fast::Extension::ImportAzaharTexturePack(limited);
    EXPECT_NE(result.Message.find("The pack is larger than 100 bytes"), std::string::npos) << result.Message;
    EXPECT_TRUE(std::filesystem::is_regular_file(Destination() / kTexture));

    // A new valid pack replaces the previous one whole.
    const auto next = Root / "next";
    Png(next / "tex1_16x16_ABCDEF0123456789_13_mip0.png");
    ASSERT_TRUE(Import(next).Imported);
    EXPECT_FALSE(std::filesystem::exists(Destination() / kTexture));
    EXPECT_TRUE(std::filesystem::is_regular_file(Destination() / "tex1_16x16_ABCDEF0123456789_13_mip0.png"));
    EXPECT_TRUE(OnlyDestinationBesideIt());

    EXPECT_FALSE(Fast::Extension::ImportAzaharTexturePack({.Source = next, .InstalledTitleId = kUsa}).Imported)
        << "no managed folder: no import";
}

TEST_F(AzaharTexturePackImportTest, LinksRefuseTheImport) {
    const auto source = Root / "linked";
    Png(source / kTexture);
    std::filesystem::create_directories(Root / "elsewhere");
    std::error_code error;
    std::filesystem::create_directory_symlink(Root / "elsewhere", source / "link", error);
#ifdef _WIN32
    if (error) {
        // Junctions need no privilege; they refuse the import the same way.
        const auto command = "cmd /c mklink /J \"" + (source / "link").make_preferred().string() + "\" \"" +
                             (Root / "elsewhere").make_preferred().string() + "\" >nul";
        error = std::system(command.c_str()) == 0 ? std::error_code{} : std::make_error_code(std::errc::not_supported);
    }
#endif
    if (error) GTEST_SKIP() << "no symbolic link or junction could be made here: " << error.message();
    const auto result = Import(source);
    EXPECT_FALSE(result.Imported);
    EXPECT_NE(result.Message.find("link is a link or a special file"), std::string::npos) << result.Message;
}

TEST(AzaharTexturePackImport, RecognizesTitleFolderNames) {
    using Fast::Extension::ParseAzaharTitleFolderName;
    EXPECT_EQ(ParseAzaharTitleFolderName("0004000000033500"), std::optional<uint64_t>(0x0004000000033500ULL));
    EXPECT_EQ(ParseAzaharTitleFolderName("000400000008f900"), std::optional<uint64_t>(0x000400000008F900ULL));
    EXPECT_FALSE(ParseAzaharTitleFolderName("0005000000033500").has_value());
    EXPECT_FALSE(ParseAzaharTitleFolderName("00040000000335").has_value());
    EXPECT_FALSE(ParseAzaharTitleFolderName("000400000003350G").has_value());
}

} // namespace
