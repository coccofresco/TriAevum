#include "oot3d_native_pica_azahar_capture.h"
#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_program_descriptor.h"
#include "oot3d_native_pica_shader_gen.h"
#include "fast/renderer3ds/pica_nri_shader_contract.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <nlohmann/json.hpp>

namespace {
using Json = nlohmann::json;
using namespace Oot3dNativeGame;

Json ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return Json::parse(input);
}

using ProgramKey = std::tuple<std::string, std::string, size_t, size_t>;
ProgramKey Key(const Json& payload) {
    return {payload.at("program_hash"), payload.at("swizzle_hash"),
            payload.at("program").size(), payload.at("swizzles").size()};
}

struct Collector {
    std::map<ProgramKey, Oot3dPicaShaderState> Programs;
    std::map<std::pair<std::string, std::string>, Json> Shaders;
    std::map<uint64_t, std::string> VertexSources;
    std::map<std::pair<uint64_t, bool>, std::pair<std::string, std::string>> FragmentSources;
    std::map<std::string, Json> Recipes;
    Json Reports = Json::array();
    uint64_t Draws = 0, CompleteDraws = 0, MissingVertex = 0, Failures = 0, SeededDraws = 0;

    void IndexPrograms(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("missing corpus frame " + path.string());
        std::string line, error;
        while (std::getline(input, line)) {
            if (line.size() > 16U * 1024U * 1024U) throw std::runtime_error("oversized capture record");
            const auto event = Json::parse(line);
            if (event.value("event", "") != "shader_seed_program") continue;
            Oot3dPicaShaderState shader;
            if (!DecodeOot3dAzaharShaderSeedProgram(event, shader, &error)) throw std::runtime_error(error);
            const auto [it, added] = Programs.emplace(Key(event), shader);
            if (!added && (it->second.Program.Values() != shader.Program.Values() ||
                           it->second.Swizzles.Values() != shader.Swizzles.Values()))
                throw std::runtime_error("captured program identity collision");
        }
    }

    std::string Add(const std::string& stage, const std::string& source) {
        const auto id = Oot3d::Renderer::IdentifyPicaShaderSource(source);
        const auto name = FormatOot3dPicaCanonicalId(id.Id);
        const Json entry = {{"stage", stage}, {"source_id", name},
            {"secondary_hash", FormatOot3dPicaCanonicalId(id.SecondaryHash)},
            {"source_size", id.Size}, {"source", source}};
        const auto [it, inserted] = Shaders.emplace(std::pair{stage, name}, entry);
        if (!inserted && it->second != entry) throw std::runtime_error("source identity collision");
        return name;
    }

    void Frame(const std::filesystem::path& path, const std::string& scenario) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("missing corpus frame " + path.string());
        Oot3dPicaDrawPacket lutPacket;
        bool begun = false, ended = false, lutsAvailable = false;
        uint64_t draws = 0, complete = 0, missing = 0, failures = 0;
        Json errors = Json::array();
        std::set<std::string> framePipelines;
        std::string line, error;
        while (std::getline(input, line)) {
            if (line.size() > 16U * 1024U * 1024U) throw std::runtime_error("oversized capture record");
            const auto event = Json::parse(line);
            const std::string type = event.value("event", "");
            if (ended) throw std::runtime_error("data after capture_end");
            if (type == "capture_begin") {
                if (begun) throw std::runtime_error("nested capture_begin");
                begun = true; continue;
            }
            if (!begun) throw std::runtime_error("missing capture_begin");
            if (type == "capture_end") { ended = true; continue; }
            if (type == "shader_seed_program") {
                continue;
            }
            if (type == "shader_seed_luts") {
                if (!DecodeOot3dAzaharShaderSeedLuts(event, lutPacket, &error))
                    throw std::runtime_error(error);
                lutsAvailable = true; continue;
            }
            if (type != "draw_begin") continue;
            ++draws;
            Oot3dPicaDrawPacket packet;
            Oot3dAzaharPicaDrawMetadata metadata;
            const auto fail = [&](const std::string& message) {
                ++failures;
                if (errors.size() < 32) errors.push_back({{"draw", metadata.DrawIndex}, {"error", message}});
            };
            if (!DecodeOot3dAzaharPicaDraw(event, packet, metadata, &error)) { fail(error); continue; }
            const bool seeded = event.value("shader_seed_resources", false);
            if (seeded) ++SeededDraws;
            if (seeded && !lutsAvailable) { fail("missing referenced LUT snapshot"); continue; }
            if (seeded) {
                packet.LightingLuts = lutPacket.LightingLuts;
                packet.ProcTexLuts = lutPacket.ProcTexLuts;
                packet.FogLut = lutPacket.FogLut;
            }
            const auto vertex = Programs.find({metadata.VertexProgramHash, metadata.VertexSwizzleHash,
                                               metadata.VertexProgramWords, metadata.VertexSwizzleWords});
            const bool hasVertex = vertex != Programs.end();
            if (!hasVertex) ++missing;
            if (seeded && !hasVertex) { fail("missing referenced vertex program"); continue; }
            if (hasVertex) {
                packet.VertexShader.Program = vertex->second.Program;
                packet.VertexShader.Swizzles = vertex->second.Swizzles;
                packet.VertexShader.ProgramWordCount = vertex->second.ProgramWordCount;
                packet.VertexShader.SwizzleWordCount = vertex->second.SwizzleWordCount;
            }
            Oot3dPicaDecodedDrawState state;
            if (!DecodeOot3dPicaDrawState(packet, state, &error)) { fail(error); continue; }
            const auto fragmentKey = std::pair{ComputeOot3dPicaFragmentShaderStateKey(packet, state), seeded};
            auto fragment = FragmentSources.find(fragmentKey);
            if (fragment == FragmentSources.end()) {
                Oot3dPicaGeneratedFragmentShader shader;
                const auto purpose = seeded ? Oot3dPicaShaderBuildPurpose::RuntimeDraw
                                            : Oot3dPicaShaderBuildPurpose::OfflineSource;
                if (!GenerateOot3dPicaFragmentShader(packet, state, shader, &error, purpose)) {
                    fail(error); continue;
                }
                const auto nri = Fast::Renderer3ds::BuildPicaNriFragmentShaderVariant(shader.Source);
                if (!nri.Applied) { fail(nri.Error); continue; }
                fragment = FragmentSources.emplace(fragmentKey,
                    std::pair{Add("fragment", shader.Source), Add("nri_fragment", nri.Source)}).first;
            }
            if (!hasVertex) continue;
            if (metadata.GeometryShaderEnabled) { fail("native geometry shader seed lowering unavailable"); continue; }
            const auto vertexKey = ComputeOot3dPicaVertexShaderStateKey(packet);
            auto vertexSource = VertexSources.find(vertexKey);
            if (vertexSource == VertexSources.end()) {
                Oot3dPicaGeneratedVertexShader shader;
                if (!GenerateOot3dPicaVertexShader(packet, state, shader, &error)) { fail(error); continue; }
                vertexSource = VertexSources.emplace(vertexKey, Add("vertex", shader.Source)).first;
            }
            const auto identity = BuildOot3dPicaCanonicalDrawIdentity(packet, state);
            const std::string recipeId = FormatOot3dPicaCanonicalId(identity.PipelineId);
            const auto [it, added] = Recipes.try_emplace(recipeId, Json{
                {"native_pipeline_id", recipeId},
                {"native_raster_state_id", FormatOot3dPicaCanonicalId(identity.RasterStateId)},
                {"vertex_source_id", vertexSource->second},
                {"fragment_source_id", fragment->second.first},
                {"nri_fragment_source_id", fragment->second.second},
                {"native_register_snapshot", event.at("register_snapshot")},
                {"topology", metadata.TriangleTopology}, {"draw_mode", metadata.DrawMode},
                {"complete_capture_resources", seeded},
                {"first_scenario", scenario}, {"first_draw", metadata.DrawIndex}, {"draws", 0U}});
            if (!added && (it->second.at("vertex_source_id") != vertexSource->second ||
                           it->second.at("fragment_source_id") != fragment->second.first))
                throw std::runtime_error("native pipeline/source identity collision");
            if (seeded && !it->second.at("complete_capture_resources").get<bool>()) {
                it->second["native_register_snapshot"] = event.at("register_snapshot");
                it->second["complete_capture_resources"] = true;
                it->second["resource_scenario"] = scenario;
                it->second["resource_draw"] = metadata.DrawIndex;
            }
            it->second["draws"] = it->second.at("draws").get<uint64_t>() + 1;
            framePipelines.insert(recipeId);
            ++complete;
        }
        if (!begun || !ended) throw std::runtime_error("incomplete capture " + path.string());
        Draws += draws; CompleteDraws += complete; MissingVertex += missing; Failures += failures;
        Reports.push_back({{"scenario", scenario}, {"frame", path.generic_string()},
            {"draws", draws}, {"complete_draws", complete}, {"missing_vertex_draws", missing},
            {"failures", failures}, {"errors", errors}, {"native_pipeline_ids", framePipelines}});
    }
};
}

int main(int argc, char** argv) {
    try {
        std::filesystem::path manifest, output;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (++i >= argc) throw std::runtime_error("missing option value");
            if (arg == "--manifest") manifest = argv[i];
            else if (arg == "--output") output = argv[i];
            else throw std::runtime_error("unknown option " + arg);
        }
        if (manifest.empty() || output.empty())
            throw std::runtime_error("usage: oot3d_native_pica_capture_inventory --manifest corpus.json --output inventory.json");
        const auto corpus = ReadJson(manifest);
        if (corpus.value("format", "") != "oot3d_pica_capture_corpus_v1")
            throw std::runtime_error("unsupported capture corpus manifest");
        if (std::filesystem::weakly_canonical(manifest) == std::filesystem::weakly_canonical(output))
            throw std::runtime_error("output overlaps manifest");
        Collector collector;
        // Program bytes are immutable for their captured identity and can repair
        // hash-only historical draws. Dynamic LUT state never crosses frames.
        for (const auto& frame : corpus.at("frames"))
            collector.IndexPrograms(manifest.parent_path() / frame.at("path").get<std::string>());
        for (const auto& frame : corpus.at("frames")) {
            const auto path = manifest.parent_path() / frame.at("path").get<std::string>();
            if (std::filesystem::weakly_canonical(path) == std::filesystem::weakly_canonical(output))
                throw std::runtime_error("output overlaps capture");
            collector.Frame(path, frame.at("scenario"));
        }
        Json shaders = Json::array(), recipes = Json::array();
        for (const auto& [key, value] : collector.Shaders) shaders.push_back(value);
        for (const auto& [key, value] : collector.Recipes) recipes.push_back(value);
        const Json report = {{"frames", collector.Reports}, {"draws", collector.Draws},
            {"complete_draws", collector.CompleteDraws}, {"missing_vertex_draws", collector.MissingVertex},
            {"draws_with_complete_resource_snapshots", collector.SeededDraws},
            {"program_payload_identities", collector.Programs.size()},
            {"failures", collector.Failures}, {"observed_native_pipeline_recipes", recipes.size()},
            {"device_pipeline_prewarm", false}, {"game_coverage_proven", false},
            {"complete_import", collector.Draws > 0 && collector.CompleteDraws == collector.Draws}};
        const Json inventory = {{"format", "oot3d_pica_effective_shader_inventory_v1"},
            {"descriptor_schema_version", kOot3dPicaProgramDescriptorSchemaVersion},
            {"shader_count", shaders.size()}, {"shaders", shaders},
            {"capture_import", report}, {"native_pipeline_recipes", recipes}};
        if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
        std::ofstream out(output, std::ios::binary | std::ios::trunc);
        out << inventory.dump(2) << '\n'; out.close();
        if (!out) throw std::runtime_error("failed writing inventory");
        std::cout << "draws=" << collector.Draws << " complete=" << collector.CompleteDraws
                  << " modules=" << shaders.size() << " recipes=" << recipes.size()
                  << " failures=" << collector.Failures << '\n';
        return report.at("complete_import").get<bool>() ? 0 : 3;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n'; return 1;
    }
}
