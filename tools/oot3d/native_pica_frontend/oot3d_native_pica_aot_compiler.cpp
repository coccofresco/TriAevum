#include "fast/extension/pica_aot_shader_pack.h"
#include "fast/extension/pica_scanout_effects.h"
#include "fast/extension/builtin_pass_shaders.h"
#include "fast/renderer/shaderc_compiler.h"
#include "fast/renderer/spirv_cache.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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
#include <thread>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>
#include <shaderc/shaderc.hpp>

namespace {

struct Options {
    std::vector<std::filesystem::path> Inventories;
    std::filesystem::path Pack;
    std::filesystem::path Manifest;
    std::filesystem::path MergedInventory;
    std::filesystem::path RendererCache;
    // Publisher compile cache (issue #76 strategy §5): modules keyed by
    // source, stage and compiler contract survive between regenerations.
    std::filesystem::path CompileCache;
    unsigned Jobs = 0;
};

// The options CompileShader passes; part of every cache key.
constexpr std::string_view kCompileOptionsContract = "/pica-aot/vulkan1.1/performance/main/v1";

void PrintUsage() {
    std::cerr
        << "usage: oot3d_native_pica_aot_compiler "
           "--inventory <file> [--inventory <file> ...] "
           "--pack <file> --manifest <file> "
           "[--merged-inventory <file>] [--compile-cache <directory>] [--jobs <n>]\n"
        << "or: --prepare-renderer-cache <directory> --manifest <file>\n";
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
        else if (argument == "--prepare-renderer-cache")
            options.RendererCache = value;
        else if (argument == "--compile-cache")
            options.CompileCache = value;
        else if (argument == "--jobs") {
            try {
                options.Jobs = static_cast<unsigned>(std::stoul(value.string()));
            } catch (const std::exception&) {
                return false;
            }
            if (options.Jobs == 0U)
                return false;
        }
        else
            return false;
    }
    if (!options.RendererCache.empty())
        return options.Inventories.empty() && options.Pack.empty() &&
               options.MergedInventory.empty() && !options.Manifest.empty();
    return !options.Inventories.empty() && !options.Pack.empty() &&
           !options.Manifest.empty();
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
    Fast::Extension::PicaAotShaderStage stage,
    const std::string& sourceName) {
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto kind = stage == Fast::Extension::PicaAotShaderStage::Vertex
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

int TriAevumPassCompilerMain(int argc, char** argv) {
    using namespace Fast::Extension;
    try {
        Options options;
        if (!ParseOptions(argc, argv, options)) {
            PrintUsage();
            return 2;
        }
        if (!options.RendererCache.empty()) {
            Fast::Renderer::CachedPassShaderCompiler compiler;
            compiler.Configure(options.RendererCache);
            if (!compiler.Enabled())
                throw std::runtime_error("renderer shader cache has no compiler identity");
            const auto shaders = BuildBuiltinPassShaders();
            for (const auto& shader : shaders)
                compiler.Resolve(shader.Source, shader.Stage, shader.Name, shader.Defines);
            const auto stats = compiler.Stats();
            WriteJsonAtomically(options.Manifest, {
                {"format", "triaevum_renderer_shader_preparation_v1"},
                {"modules", shaders.size()},
                {"hits", stats.Hits + compiler.BuiltinArtifactHits()},
                {"builtin_hits", compiler.BuiltinArtifactHits()}, {"compiled", stats.Compilations},
                {"compile_failed", stats.CompilationFailures}, {"writes", stats.Writes},
                {"write_failed", stats.WriteFailures}, {"compile_ms", stats.CompileNanoseconds / 1e6},
                {"cache_directory", std::filesystem::absolute(options.RendererCache).string()},
                {"game_booted", false}});
            if (stats.WriteFailures)
                throw std::runtime_error("renderer shader cache could not persist every module");
            return 0;
        }
        uint32_t schema = 0U;
        // One entry per (stage, source_id), in that order (the merged
        // inventory's and the jobs' order). Inventories are parsed one shader
        // at a time: a corpus inventory can exceed the memory of its DOM.
        std::map<std::pair<std::string, std::string>, nlohmann::json> uniqueInputs;
        const auto mergeInput = [&](nlohmann::json input) {
            const std::string stage = input.at("stage");
            const std::string sourceId = input.at("source_id");
            const auto [found, inserted] =
                uniqueInputs.emplace(std::pair{stage, sourceId}, nlohmann::json());
            if (inserted) {
                found->second = std::move(input);
            } else if (found->second.at("source") != input.at("source")) {
                throw std::runtime_error(
                    "shader source identity collision while merging inventories: " +
                    stage + ":" + sourceId);
            }
        };
        for (const auto& inventoryPath : options.Inventories) {
            std::ifstream stream(inventoryPath, std::ios::binary);
            if (!stream)
                throw std::runtime_error("could not open inventory: " + inventoryPath.string());
            std::string topKey;
            const nlohmann::json::parser_callback_t onEvent =
                [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
                    using Event = nlohmann::json::parse_event_t;
                    if (depth == 1 && event == Event::key) topKey = parsed.get<std::string>();
                    if (depth == 2 && event == Event::object_end && topKey == "shaders") {
                        mergeInput(std::move(parsed));
                        return false;  // merged; not kept in the document
                    }
                    return true;
                };
            const auto inventory = nlohmann::json::parse(stream, onEvent);
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
        }
        if (uniqueInputs.empty() ||
            uniqueInputs.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error(
                "effective shader inventory merge is empty or too large");

        // Renderer-owned sources accompany every seed, without needing a game
        // capture. Explicit mode zero keeps developer diagnostics out of Forge.
        const size_t capturedSources = uniqueInputs.size();
        const auto appendRendererSource = [&](PicaAotShaderStage stage,
                                              const std::string& source) {
            const auto identity = IdentifyPicaAotShaderSource(source);
            const std::string stageName(PicaAotShaderStageName(stage));
            const auto sourceId = FormatPicaAotShaderId(identity.Id);
            const auto found = uniqueInputs.find(std::pair{stageName, sourceId});
            if (found != uniqueInputs.end()) {
                if (found->second.at("source") != source)
                    throw std::runtime_error("renderer shader source identity collision");
                return;
            }
            uniqueInputs.emplace(std::pair{stageName, sourceId},
                nlohmann::json{{"stage", stageName}, {"source_id", sourceId},
                    {"secondary_hash", FormatPicaAotShaderId(identity.SecondaryHash)},
                    {"source_size", identity.Size}, {"source", source}});
        };
        appendRendererSource(PicaAotShaderStage::Vertex, BuildPicaScanoutVertexShader());
        appendRendererSource(PicaAotShaderStage::Fragment, BuildPicaScanoutFragmentShader(false, 0));
        const size_t rendererSourcesAdded = uniqueInputs.size() - capturedSources;

        if (!options.MergedInventory.empty()) {
            // The same bytes as dump(2) of the whole document, written one
            // shader at a time (keys in nlohmann's sorted order).
            std::error_code directoryError;
            if (!options.MergedInventory.parent_path().empty())
                std::filesystem::create_directories(options.MergedInventory.parent_path(), directoryError);
            const auto temporary = options.MergedInventory.string() + ".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                output << "{\n  \"descriptor_schema_version\": " << schema
                       << ",\n  \"format\": \"oot3d_pica_effective_shader_inventory_v1\",\n  \"shader_count\": "
                       << uniqueInputs.size() << ",\n  \"shaders\": [";
                bool first = true;
                for (const auto& [key, input] : uniqueInputs) {
                    output << (first ? "\n" : ",\n");
                    first = false;
                    const std::string text = input.dump(2);
                    size_t begin = 0;
                    while (begin <= text.size()) {
                        const size_t end = std::min(text.find('\n', begin), text.size());
                        output << "    " << std::string_view(text).substr(begin, end - begin);
                        if (end == text.size()) break;
                        output << '\n';
                        begin = end + 1;
                    }
                }
                output << "\n  ]\n}\n";
                if (!output) throw std::runtime_error("could not write merged inventory");
            }
            std::error_code error;
            std::filesystem::remove(options.MergedInventory, error);
            error.clear();
            std::filesystem::rename(temporary, options.MergedInventory, error);
            if (error) throw std::runtime_error("could not publish merged inventory: " + error.message());
        }

        struct Job {
            PicaAotShaderStage Stage;
            std::string StageName;
            std::string Source;
            PicaAotShaderSourceIdentity Identity;
            std::string SourceName;
            std::vector<uint32_t> Spirv;
            std::string Error;
            size_t SpirvWords = 0;
            uint64_t SpirvHash = 0;
        };
        std::vector<Job> jobs;
        jobs.reserve(uniqueInputs.size());
        for (auto& [key, input] : uniqueInputs) {
            Job job;
            job.StageName = input.at("stage");
            if (!ParsePicaAotShaderStage(job.StageName, job.Stage))
                throw std::runtime_error("unknown shader stage: " +
                                         job.StageName);
            job.Source = std::move(input.at("source").get_ref<std::string&>());
            job.Identity = IdentifyPicaAotShaderSource(job.Source);
            if (input.value("source_id", std::string{}) !=
                    FormatPicaAotShaderId(job.Identity.Id) ||
                input.value("secondary_hash", std::string{}) !=
                    FormatPicaAotShaderId(job.Identity.SecondaryHash) ||
                input.value("source_size", uint64_t{0}) != job.Identity.Size) {
                throw std::runtime_error(
                    "shader source identity does not match inventory");
            }
            job.SourceName =
                "pica_" + job.StageName + "_" +
                FormatPicaAotShaderId(job.Identity.Id) +
                (job.Stage == PicaAotShaderStage::Vertex ? ".vert" :
                                                           ".frag");
            jobs.push_back(std::move(job));
        }

        // Bounded parallel compilation, one shaderc instance per worker. The
        // cache serves unchanged sources compiled by the same compiler with
        // the same options; anything else compiles.
        Fast::Renderer::SpirvCache cache;
        if (!options.CompileCache.empty()) {
            const auto& contract = Fast::Renderer::ShadercCompilerContract();
            if (contract.empty())
                throw std::runtime_error("compile cache requested but the compiler identity is unavailable");
            cache.Configure(options.CompileCache, contract + std::string(kCompileOptionsContract));
        }
        const unsigned workers = std::max(1U, std::min<unsigned>(
            options.Jobs != 0U ? options.Jobs : std::min(8U, std::max(1U, std::thread::hardware_concurrency())),
            static_cast<unsigned>(jobs.size())));
        uniqueInputs.clear();  // the sources live in the jobs now
        // The pack is written as it compiles, in the pack's own entry order
        // (stage, source identity; WritePicaAotShaderPack's layout, byte for
        // byte): only one batch of SPIR-V is held in memory.
        std::vector<size_t> packOrder(jobs.size());
        for (size_t index = 0; index < packOrder.size(); ++index) packOrder[index] = index;
        std::sort(packOrder.begin(), packOrder.end(), [&](size_t left, size_t right) {
            return std::tie(jobs[left].Stage, jobs[left].Identity) < std::tie(jobs[right].Stage, jobs[right].Identity);
        });
        for (size_t index = 1; index < packOrder.size(); ++index)
            if (jobs[packOrder[index - 1]].Stage == jobs[packOrder[index]].Stage &&
                jobs[packOrder[index - 1]].Identity == jobs[packOrder[index]].Identity)
                throw std::runtime_error("PICA AOT shader pack contains an invalid entry");
        constexpr size_t kHeaderBytes = 24U, kEntryBytes = 56U;
        constexpr uint64_t kMaximumPackBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
        const auto appendU32 = [](std::vector<uint8_t>& bytes, uint32_t value) {
            for (uint32_t shift = 0; shift < 32U; shift += 8U) bytes.push_back(static_cast<uint8_t>(value >> shift));
        };
        const auto appendU64 = [](std::vector<uint8_t>& bytes, uint64_t value) {
            for (uint32_t shift = 0; shift < 64U; shift += 8U) bytes.push_back(static_cast<uint8_t>(value >> shift));
        };
        if (!options.Pack.parent_path().empty()) {
            std::error_code directoryError;
            std::filesystem::create_directories(options.Pack.parent_path(), directoryError);
        }
        const auto packTemporary = options.Pack.string() + ".tmp";
        std::ofstream packFile(packTemporary, std::ios::binary | std::ios::trunc);
        std::vector<uint8_t> header{'O', '3', 'P', 'S', 'A', 'O', 'T', 0};
        appendU32(header, kPicaAotShaderPackVersion);
        appendU32(header, schema);
        appendU32(header, static_cast<uint32_t>(jobs.size()));
        appendU32(header, 0U);
        header.resize(kHeaderBytes + jobs.size() * kEntryBytes, 0U);
        packFile.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        std::vector<uint8_t> table;
        table.reserve(jobs.size() * kEntryBytes);
        uint64_t packBytes = header.size();

        const auto compileStart = std::chrono::steady_clock::now();
        const size_t batchSize = std::max<size_t>(64U, size_t{workers} * 16U);
        for (size_t batchBegin = 0; batchBegin < packOrder.size(); batchBegin += batchSize) {
        const size_t batchEnd = std::min(packOrder.size(), batchBegin + batchSize);
        std::atomic<size_t> next{batchBegin};
        std::atomic<bool> failed{false};
        std::vector<std::thread> threads;
        for (unsigned worker = 0; worker < workers; ++worker) {
            threads.emplace_back([&] {
                shaderc::Compiler compiler;
                for (size_t position = next++; position < batchEnd && !failed; position = next++) {
                    auto& job = jobs[packOrder[position]];
                    try {
                        const auto compile = [&] {
                            return CompileShader(compiler, job.Source, job.Stage, job.SourceName);
                        };
                        job.Spirv = cache.Enabled()
                            ? cache.Resolve(job.Source,
                                            job.Stage == PicaAotShaderStage::Vertex
                                                ? Fast::Renderer::SpirvStage::Vertex
                                                : Fast::Renderer::SpirvStage::Fragment,
                                            compile)
                            : compile();
                    } catch (const std::exception& exception) {
                        job.Error = exception.what();
                        failed = true;
                    }
                }
            });
        }
        for (auto& thread : threads)
            thread.join();
        for (size_t position = batchBegin; position < batchEnd; ++position) {
            auto& job = jobs[packOrder[position]];
            if (!job.Error.empty())
                throw std::runtime_error(job.Error);
            const auto& spirv = job.Spirv;
            if (spirv.empty() || spirv.size() > std::numeric_limits<uint32_t>::max() ||
                spirv.front() != 0x07230203U || job.Identity.Id == 0U ||
                job.Identity.SecondaryHash == 0U || job.Identity.Size == 0U)
                throw std::runtime_error("PICA AOT shader pack contains an invalid entry");
            const auto* data = reinterpret_cast<const uint8_t*>(spirv.data());
            const size_t size = spirv.size() * sizeof(uint32_t);
            uint64_t binaryHash = 1469598103934665603ULL;
            for (size_t byte = 0; byte < size; ++byte) binaryHash = (binaryHash ^ data[byte]) * 1099511628211ULL;
            if (binaryHash == 0U) binaryHash = 1U;
            appendU32(table, static_cast<uint32_t>(job.Stage));
            appendU32(table, 0U);
            appendU64(table, job.Identity.Id);
            appendU64(table, job.Identity.SecondaryHash);
            appendU64(table, job.Identity.Size);
            appendU32(table, static_cast<uint32_t>(spirv.size()));
            appendU32(table, 0U);
            appendU64(table, packBytes);
            appendU64(table, binaryHash);
            packFile.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
            packBytes += size;
            if (packBytes > kMaximumPackBytes)
                throw std::runtime_error("PICA AOT shader pack exceeds its size limit");
            job.SpirvWords = spirv.size();
            job.SpirvHash = HashWords(spirv);
            job.Spirv = {};
            job.Source = {};
        }
        }
        packFile.seekp(static_cast<std::streamoff>(kHeaderBytes));
        packFile.write(reinterpret_cast<const char*>(table.data()), static_cast<std::streamsize>(table.size()));
        packFile.close();
        if (!packFile) throw std::runtime_error("could not write PICA AOT shader pack");
        {
            std::error_code publishError;
            std::filesystem::remove(options.Pack, publishError);
            publishError.clear();
            std::filesystem::rename(packTemporary, options.Pack, publishError);
            if (publishError)
                throw std::runtime_error("could not publish PICA AOT shader pack: " + publishError.message());
        }
        const double compileSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - compileStart).count();

        nlohmann::json manifestEntries = nlohmann::json::array();
        for (const auto& job : jobs) {
            manifestEntries.push_back({
                {"stage", job.StageName},
                {"source_id", FormatPicaAotShaderId(job.Identity.Id)},
                {"secondary_hash", FormatPicaAotShaderId(job.Identity.SecondaryHash)},
                {"source_size", job.Identity.Size},
                {"spirv_words", job.SpirvWords},
                {"spirv_hash", FormatPicaAotShaderId(job.SpirvHash)},
            });
        }
        std::string error;
        // Verify re-reads every binary against its table hash without keeping it.
        PicaAotShaderPack verification;
        if (!verification.Load(options.Pack, &error) || !verification.Verify(&error) ||
            verification.EntryCount() != jobs.size() ||
            verification.DescriptorSchemaVersion() != schema) {
            throw std::runtime_error(
                "published AOT shader pack failed verification: " + error);
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
            {"shader_count", jobs.size()},
            {"renderer_sources_added", rendererSourcesAdded},
            {"compilation",
             {{"workers", workers}, {"seconds", compileSeconds},
              {"cache", cache.Enabled() ? nlohmann::json{
                   {"hits", cache.Stats().Hits}, {"compiled", cache.Stats().Compilations},
                   {"rejected", cache.Stats().Rejected}, {"write_failed", cache.Stats().WriteFailures}}
                                        : nlohmann::json(nullptr)}}},
            {"shaders", std::move(manifestEntries)},
        };
        WriteJsonAtomically(options.Manifest, manifest);
        std::cout << "PICA AOT shader pack: " << jobs.size()
                  << " modules, schema " << schema << ", "
                  << options.Pack.string() << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_pica_aot_compiler: "
                  << exception.what() << '\n';
        return 1;
    }
}

// The Android runtime library links this compiler and calls it in process
// (triaevum_forge_tool.cpp): the pass cache keeps the gameplay build's identity.
#ifndef TRIAEVUM_TOOL_LIBRARY
int main(int argc, char** argv) {
    return TriAevumPassCompilerMain(argc, argv);
}
#endif
