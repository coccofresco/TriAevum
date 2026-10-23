#include "oot3d/renderer/azahar_texture_pack.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <filesystem>
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

} // namespace
