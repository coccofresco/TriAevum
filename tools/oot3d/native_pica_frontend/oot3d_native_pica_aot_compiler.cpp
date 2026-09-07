#include "fast/oot3d/pica_aot_shader_pack.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <shaderc/shaderc.hpp>

namespace {

struct Options {
    std::vector<std::filesystem::path> Inventories;
    std::filesystem::path Pack;
    std::filesystem::path Manifest;
    std::filesystem::path MergedInventory;
};

void PrintUsage() {
    std::cerr
        << "usage: oot3d_native_pica_aot_compiler "
           "--inventory <file> [--inventory <file> ...] "
           "--pack <file> --manifest <file> "
           "[--merged-inventory <file>]\n";
}

bool ParseOptions(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (index + 1 >= argc)
            return false;
        const std::filesystem::path value = argv[++index];
        if (argument == "--inventory")
            options.Inventories.push_back(value);
        else if (argument == "--pack")
            options.Pack = value;
        else if (argument == "--manifest")
            options.Manifest = value;
        else if (argument == "--merged-inventory")
            options.MergedInventory = value;
        else
            return false;
    }
    return !options.Inventories.empty() && !options.Pack.empty() &&
           !options.Manifest.empty();
}

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("could not open inventory: " +
                                 path.string());
    return nlohmann::json::parse(input);
}

uint64_t HashWords(std::span<const uint32_t> words) {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffset;
    for (const uint32_t word : words) {
        for (uint32_t shift = 0; shift < 32U; shift += 8U)
            hash = (hash ^ static_cast<uint8_t>(word >> shift)) * kPrime;
    }
    return hash == 0U ? 1U : hash;
}

std::vector<uint32_t> CompileShader(
    shaderc::Compiler& compiler, const std::string& source,
    Fast::Oot3d::PicaAotShaderStage stage,
    const std::string& sourceName) {
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto kind = stage == Fast::Oot3d::PicaAotShaderStage::Vertex
                          ? shaderc_vertex_shader
                          : shaderc_fragment_shader;
    const auto result = compiler.CompileGlslToSpv(
        source, kind, sourceName.c_str(), "main", options);
    if (result.GetCompilationStatus() !=
        shaderc_compilation_status_success) {
        throw std::runtime_error("shaderc failed for " + sourceName +
                                 ": " + result.GetErrorMessage());
    }
    return {result.cbegin(), result.cend()};
}

void WriteJsonAtomically(const std::filesystem::path& path,
                         const nlohmann::json& value) {
    std::error_code error;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        throw std::runtime_error("could not create manifest directory: " +
                                 error.message());
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary,
                             std::ios::binary | std::ios::trunc);
        output << value.dump(2) << '\n';
        if (!output)
            throw std::runtime_error("could not write AOT manifest");
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error)
        throw std::runtime_error("could not publish AOT manifest: " +
                                 error.message());
}

} // namespace

int main(int argc, char** argv) {
    using namespace Fast::Oot3d;
    try {
        Options options;
        if (!ParseOptions(argc, argv, options)) {
            PrintUsage();
            return 2;
        }
        uint32_t schema = 0U;
        nlohmann::json inputs = nlohmann::json::array();
        std::map<std::pair<std::string, std::string>, std::string>
            uniqueSources;
        for (const auto& inventoryPath : options.Inventories) {
            const auto inventory = ReadJson(inventoryPath);
            if (inventory.value("format", std::string{}) !=
                    "oot3d_pica_effective_shader_inventory_v1" ||
                !inventory.contains("shaders") ||
                !inventory.at("shaders").is_array()) {
                throw std::runtime_error(
                    "effective shader inventory format is invalid: " +
                    inventoryPath.string());
            }
            const uint32_t inventorySchema =
                inventory.value("descriptor_schema_version", 0U);
            if (inventorySchema == 0U) {
                throw std::runtime_error(
                    "effective shader inventory has no descriptor schema: " +
                    inventoryPath.string());
            }
            if (schema == 0U)
                schema = inventorySchema;
            else if (schema != inventorySchema)
                throw std::runtime_error(
                    "effective shader inventories use different descriptor schemas");

            for (const auto& input : inventory.at("shaders")) {
                const std::string stage = input.at("stage");
                const std::string sourceId = input.at("source_id");
                const std::string source = input.at("source");
                const auto key = std::pair{stage, sourceId};
                const auto [found, inserted] =
                    uniqueSources.emplace(key, source);
                if (!inserted && found->second != source) {
                    throw std::runtime_error(
                        "shader source identity collision while merging inventories: " +
                        stage + ":" + sourceId);
                }
                if (inserted)
                    inputs.push_back(input);
            }
        }
        if (inputs.empty() ||
            inputs.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error(
                "effective shader inventory merge is empty or too large");

        std::sort(inputs.begin(), inputs.end(), [](const auto& left,
                                                   const auto& right) {
            return std::tie(left.at("stage"), left.at("source_id")) <
                   std::tie(right.at("stage"), right.at("source_id"));
        });
        if (!options.MergedInventory.empty()) {
            const nlohmann::json merged = {
                {"format", "oot3d_pica_effective_shader_inventory_v1"},
                {"descriptor_schema_version", schema},
                {"shader_count", inputs.size()},
                {"shaders", inputs},
            };
            WriteJsonAtomically(options.MergedInventory, merged);
        }

        shaderc::Compiler compiler;
        std::vector<PicaAotShaderBinary> binaries;
        binaries.reserve(inputs.size());
        nlohmann::json manifestEntries = nlohmann::json::array();
        for (const auto& input : inputs) {
            PicaAotShaderStage stage;
            const std::string stageName = input.at("stage");
            if (!ParsePicaAotShaderStage(stageName, stage))
                throw std::runtime_error("unknown shader stage: " +
                                         stageName);
            const std::string source = input.at("source");
            const auto identity = IdentifyPicaAotShaderSource(source);
            if (input.value("source_id", std::string{}) !=
                    FormatPicaAotShaderId(identity.Id) ||
                input.value("secondary_hash", std::string{}) !=
                    FormatPicaAotShaderId(identity.SecondaryHash) ||
                input.value("source_size", uint64_t{0}) != identity.Size) {
                throw std::runtime_error(
                    "shader source identity does not match inventory");
            }
            const std::string sourceName =
                "pica_" + stageName + "_" +
                FormatPicaAotShaderId(identity.Id) +
                (stage == PicaAotShaderStage::Vertex ? ".vert" :
                                                       ".frag");
            auto spirv = CompileShader(compiler, source, stage, sourceName);
            const uint64_t spirvHash = HashWords(spirv);
            manifestEntries.push_back({
                {"stage", stageName},
                {"source_id", FormatPicaAotShaderId(identity.Id)},
                {"secondary_hash",
                 FormatPicaAotShaderId(identity.SecondaryHash)},
                {"source_size", identity.Size},
                {"spirv_words", spirv.size()},
                {"spirv_hash", FormatPicaAotShaderId(spirvHash)},
            });
            binaries.push_back({stage, identity, std::move(spirv)});
        }

        std::string error;
        if (!WritePicaAotShaderPack(options.Pack, schema, binaries,
                                    &error)) {
            throw std::runtime_error(error);
        }
        PicaAotShaderPack verification;
        if (!verification.Load(options.Pack, &error) ||
            verification.EntryCount() != binaries.size() ||
            verification.DescriptorSchemaVersion() != schema) {
            throw std::runtime_error(
                "published AOT shader pack failed verification: " + error);
        }
        for (const auto& binary : binaries) {
            const auto found = verification.Find(binary.Stage,
                                                 binary.Source);
            if (found.size() != binary.Spirv.size() ||
                !std::equal(found.begin(), found.end(),
                            binary.Spirv.begin())) {
                throw std::runtime_error(
                    "published AOT shader binary failed round-trip");
            }
        }

        nlohmann::json inventoryPaths = nlohmann::json::array();
        for (const auto& inventoryPath : options.Inventories)
            inventoryPaths.push_back(inventoryPath.string());
        const nlohmann::json manifest = {
            {"format", "oot3d_pica_aot_shader_pack_manifest_v1"},
            {"pack_format_version", kPicaAotShaderPackVersion},
            {"descriptor_schema_version", schema},
            {"inventories", std::move(inventoryPaths)},
            {"merged_inventory",
             options.MergedInventory.empty()
                 ? nlohmann::json(nullptr)
                 : nlohmann::json(options.MergedInventory.string())},
            {"pack", options.Pack.string()},
            {"shader_count", binaries.size()},
            {"shaders", std::move(manifestEntries)},
        };
        WriteJsonAtomically(options.Manifest, manifest);
        std::cout << "PICA AOT shader pack: " << binaries.size()
                  << " modules, schema " << schema << ", "
                  << options.Pack.string() << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_pica_aot_compiler: "
                  << exception.what() << '\n';
        return 1;
    }
}
