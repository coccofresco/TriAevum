// SPIR-V packs: format round trip, malformed packs, and the embedded families.
#ifdef ENABLE_OOT3D_IMMERSE
#include "fast/immerse/mxao_pass_binaries.h"
#endif
#include "fast/oot3d/pica_caster_fragment_binaries.h"
#include "fast/oot3d/pica_temporal_fragment_binaries.h"
#include "fast/oot3d/pica_toon_fragment_artifact.h"
#include "fast/oot3d/pica_uber_fragment_binaries.h"
#include "fast/renderer/builtin_pass_binaries.h"
#include "fast/renderer/pass_shader_artifact.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"
#include "fast/renderer3ds/pica_startup_programs.h"
#include "oot3d_native_vertex_binaries.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>

using namespace Fast::Renderer;

namespace {

struct AlignedPack {
    std::vector<uint32_t> Words;
    size_t Bytes = 0;
    explicit AlignedPack(const std::vector<unsigned char>& bytes) : Words((bytes.size() + 3) / 4), Bytes(bytes.size()) {
        std::memcpy(Words.data(), bytes.data(), bytes.size());
    }
    unsigned char* Data() { return reinterpret_cast<unsigned char*>(Words.data()); }
    std::span<const unsigned char> Span() const { return {reinterpret_cast<const unsigned char*>(Words.data()), Bytes}; }
};

const std::vector<uint32_t> kModuleA{0x07230203U, 0x10000U, 0U, 7U, 0U, 1U};
const std::vector<uint32_t> kModuleB{0x07230203U, 0x10300U, 0U, 9U, 0U, 2U, 3U};

std::vector<SpirvPackEntry> SampleEntries() {
    std::vector<SpirvPackEntry> entries(4);
    entries[0].Kind = SpirvPackKind::PicaFragment;
    entries[0].Flags = kSpirvPackSeparateSamplers;
    entries[0].Identity = {1U, 2U, 3U};
    entries[0].Spirv = kModuleA;
    entries[1].Kind = SpirvPackKind::Pass;
    entries[1].Stage = uint32_t(SpirvStage::Compute);
    entries[1].VulkanMinor = 2;
    entries[1].Source = "void main() {}";
    entries[1].Defines = {{"A", "1"}, {"B", ""}};
    entries[1].Spirv = kModuleB;
    entries[2].Kind = SpirvPackKind::PicaVertex;
    entries[2].Identity = {4U, 5U, 6U};
    entries[2].Spirv = kModuleA; // stored once
    entries[3].Kind = SpirvPackKind::Pass;
    entries[3].Stage = uint32_t(SpirvStage::Vertex);
    entries[3].VulkanMinor = 1;
    entries[3].Source = "void main() {}"; // stored once
    entries[3].Spirv = kModuleB;
    return entries;
}

bool Same(const SpirvPackEntry& a, const SpirvPackEntry& b) {
    return a.Kind == b.Kind && a.Stage == b.Stage && a.VulkanMinor == b.VulkanMinor && a.Flags == b.Flags &&
           a.Identity == b.Identity && a.Source == b.Source && a.Defines == b.Defines &&
           std::equal(a.Spirv.begin(), a.Spirv.end(), b.Spirv.begin(), b.Spirv.end());
}

} // namespace

TEST(SpirvPack, RoundTripsEveryEntryAndStoresDuplicatesOnce) {
    const auto entries = SampleEntries();
    const auto bytes = WriteSpirvPack(entries);
    EXPECT_EQ(bytes, WriteSpirvPack(entries)) << "packs are deterministic";
    AlignedPack pack(bytes);
    std::string error;
    const auto read = ReadSpirvPack(pack.Span(), &error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(read.size(), entries.size());
    for (size_t i = 0; i < read.size(); ++i) EXPECT_TRUE(Same(read[i], entries[i])) << i;
    EXPECT_EQ(read[0].Spirv.data(), read[2].Spirv.data()) << "identical modules are stored once";
    EXPECT_EQ(read[1].Source.data(), read[3].Source.data()) << "identical strings are stored once";

    const auto passes = PassShaderArtifactsFromPack(read);
    ASSERT_EQ(passes.size(), 2U);
    const ShaderDefines defines{{"A", "1"}, {"B", ""}};
    EXPECT_FALSE(FindPassShaderArtifact(passes, "void main() {}", SpirvStage::Compute, defines).empty());
    EXPECT_TRUE(FindPassShaderArtifact(passes, "void main() {}", SpirvStage::Compute, {}).empty());
    EXPECT_FALSE(FindPassShaderArtifact(passes, "void main() {}", SpirvStage::Vertex, {}, 1).empty());
    const auto fragments = Fast::Renderer3ds::PicaFragmentArtifactsFromPack(read);
    ASSERT_EQ(fragments.size(), 1U);
    EXPECT_TRUE(fragments[0].SeparateSamplers);
    EXPECT_EQ(fragments[0].Source.Size, 3U);
    const auto vertices = Fast::Renderer3ds::PicaVertexArtifactsFromPack(read);
    ASSERT_EQ(vertices.size(), 1U);
    EXPECT_EQ(vertices[0].Source.Id, 4U);
}

TEST(SpirvPack, RejectsMalformedPacks) {
    const auto bytes = WriteSpirvPack(SampleEntries());
    const auto rejects = [](AlignedPack& pack, size_t bytes) {
        std::string error;
        const auto read = ReadSpirvPack(pack.Span().first(bytes), &error);
        return read.empty() && !error.empty();
    };
    {
        AlignedPack pack(bytes);
        EXPECT_TRUE(rejects(pack, bytes.size() - 4)) << "size differs from the header";
        EXPECT_TRUE(rejects(pack, 8)) << "truncated header";
    }
    const auto corrupt = [&](size_t offset, uint32_t value) {
        AlignedPack pack(bytes);
        std::memcpy(pack.Data() + offset, &value, 4);
        std::string error;
        return ReadSpirvPack(pack.Span(), &error).empty() && !error.empty();
    };
    EXPECT_TRUE(corrupt(0, 0x12345678U)) << "magic";
    EXPECT_TRUE(corrupt(4, kSpirvPackVersion + 1)) << "version";
    EXPECT_TRUE(corrupt(8, 1000U)) << "entry count";
    EXPECT_TRUE(corrupt(16, 9U)) << "entry kind";
    EXPECT_TRUE(corrupt(16 + 44, 0x7fffffffU)) << "source extent";
    EXPECT_TRUE(corrupt(16 + 56, 2U)) << "unaligned SPIR-V";
    EXPECT_TRUE(corrupt(16 + 60, 0x7fffffffU)) << "SPIR-V extent";
    EXPECT_TRUE(corrupt(16 + 64 + 52, 0x7fffffffU)) << "define count";

    AlignedPack shifted(std::vector<unsigned char>(bytes.size() + 4));
    std::memcpy(shifted.Data() + 1, bytes.data(), bytes.size());
    std::string error;
    EXPECT_TRUE(ReadSpirvPack({shifted.Data() + 1, bytes.size()}, &error).empty()) << "unaligned pack";
}

template <typename Artifacts>
void ExpectSpirvModules(const Artifacts& artifacts, size_t count) {
    ASSERT_EQ(artifacts.size(), count);
    for (const auto& artifact : artifacts) {
        ASSERT_GE(artifact.Spirv.size(), 5U);
        EXPECT_EQ(artifact.Spirv[0], 0x07230203U);
    }
}

TEST(SpirvPack, EmbeddedFamiliesHoldTheirModules) {
    ExpectSpirvModules(Fast::Renderer3ds::NativeFragmentArtifacts(), 16U);
    ExpectSpirvModules(Fast::Oot3d::TemporalFragmentArtifacts(), 40U);
    ExpectSpirvModules(Fast::Oot3d::ToonFragmentArtifacts(), 200U);
    ExpectSpirvModules(Fast::Oot3d::UberFragmentArtifacts(), 24U);
    ExpectSpirvModules(Fast::Oot3d::CasterFragmentArtifacts(), 4U);
    ExpectSpirvModules(BuiltinPassArtifacts(), 87U); // 70 + 9 diffuse GI pass kernels + its OOT3D adapter (M5), 2 grass raster-depth fragments, 3 contact shadow lengths, 2 upscaler orientation turns
#ifdef ENABLE_OOT3D_IMMERSE
    // The iMMERSE module hands its pack to the host, which reads it.
    const auto mxaoEntries = ReadSpirvPack(Fast::Immerse::MxaoPassPack());
    ExpectSpirvModules(PassShaderArtifactsFromPack(mxaoEntries), 6U);
#endif
    ExpectSpirvModules(Oot3dNativeGame::TitleVertexArtifacts(), 8U);
    ExpectSpirvModules(Oot3dNativeGame::TitlePassArtifacts(), 6U);
    for (const auto& artifact : Oot3dNativeGame::TitleVertexArtifacts())
        EXPECT_TRUE(artifact.Source.Available());
    // Every lookup key is unique within its family.
    const auto native = Fast::Renderer3ds::NativeFragmentArtifacts();
    for (size_t i = 0; i < native.size(); ++i)
        for (size_t j = i + 1; j < native.size(); ++j)
            EXPECT_FALSE(native[i].Source == native[j].Source && native[i].SeparateSamplers == native[j].SeparateSamplers);
    // The table views the embedded pack, so repeated calls return the same storage.
    EXPECT_EQ(BuiltinPassArtifacts().data(), BuiltinPassArtifacts().data());
}

// The startup preparation (and Forge's device preparation) links each program
// part once. Every link must be valid: separate samplers, as the startup
// layout declares them, and fragment inputs the vertex program writes (the
// superset's temporal and surface programs read 7-10). Nothing is dropped.
TEST(SpirvPack, StartupProgramPartsLinkTheEmbeddedFamiliesValidly) {
    using namespace Fast::Renderer3ds;
    const auto vertices = Oot3dNativeGame::TitleVertexArtifacts();
    const auto superset = Fast::Oot3d::UberFragmentArtifacts();
    const auto canonical = NativeFragmentArtifacts();
    for (const bool geometry : {false, true}) {
        for (const auto samples : {VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_4_BIT}) {
            const PicaStartupProgramDevice device{VK_FORMAT_D32_SFLOAT, samples, geometry};
            const auto parts = PicaStartupProgramParts(vertices, superset, canonical, device);
            size_t supersetItems = 0;
            for (const auto& part : parts) {
                EXPECT_TRUE(PicaStageInterfaceLinks(part.Vertex, part.Fragment));
                const auto owner = [&](std::span<const PicaFragmentArtifact> family) {
                    return std::find_if(family.begin(), family.end(), [&](const PicaFragmentArtifact& artifact) {
                        return artifact.Spirv.data() == part.Fragment.data();
                    });
                };
                const auto inSuperset = owner(superset);
                const auto inCanonical = owner(canonical);
                ASSERT_TRUE(inSuperset != superset.end() || inCanonical != canonical.end());
                EXPECT_TRUE(inSuperset != superset.end() ? inSuperset->SeparateSamplers
                                                         : inCanonical->SeparateSamplers);
                supersetItems += inSuperset != superset.end();
            }
            size_t usable = 0;
            for (const auto& artifact : superset)
                usable += artifact.SeparateSamplers &&
                          (geometry || !DeclaresSpirvCapability(artifact.Spirv, 2U));
            EXPECT_EQ(supersetItems, usable * (samples == VK_SAMPLE_COUNT_1_BIT ? 1U : 2U));
            // Every vertex program of the family is prepared.
            for (const auto& vertex : vertices)
                EXPECT_TRUE(std::any_of(parts.begin(), parts.end(), [&](const PicaStartupProgramPart& part) {
                    return part.Vertex.data() == vertex.Spirv.data();
                }));
        }
    }
}
