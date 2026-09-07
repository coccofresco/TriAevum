#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

#include <nlohmann/json.hpp>

namespace {

struct ProgramUse {
    uint64_t DrawCount = 0;
    uint64_t FirstEvent = 0;
    uint64_t LastEvent = 0;
    uint64_t FirstGuestFrame = 0;
    uint64_t LastGuestFrame = 0;
    std::set<std::string> SourceIds;
    std::set<std::string> LegacyKeys;
    std::set<std::string> PipelineIds;
};

struct PipelineUse {
    uint64_t DrawCount = 0;
    uint64_t FirstEvent = 0;
    uint64_t LastEvent = 0;
    uint64_t FirstGuestFrame = 0;
    uint64_t LastGuestFrame = 0;
    std::string VertexProgramId;
    std::string FragmentProgramId;
    std::string RasterStateId;
    std::string VertexSourceId;
    std::string FragmentSourceId;
};

struct ShaderSource {
    std::string Stage;
    std::string Id;
    std::string Source;
    uint64_t DrawCount = 0;
    std::set<std::string> ProgramIds;
};

void Observe(uint64_t eventId, uint64_t guestFrame,
             ProgramUse& use) {
    if (use.DrawCount++ == 0U) {
        use.FirstEvent = eventId;
        use.FirstGuestFrame = guestFrame;
    }
    use.LastEvent = eventId;
    use.LastGuestFrame = guestFrame;
}

void Observe(uint64_t eventId, uint64_t guestFrame,
             PipelineUse& use) {
    if (use.DrawCount++ == 0U) {
        use.FirstEvent = eventId;
        use.FirstGuestFrame = guestFrame;
    }
    use.LastEvent = eventId;
    use.LastGuestFrame = guestFrame;
}

std::string SourceKey(std::string_view stage, std::string_view id) {
    return std::string(stage) + ':' + std::string(id);
}

std::string FileId(std::string id) {
    if (id.starts_with("0x"))
        id.erase(0, 2);
    return id;
}

nlohmann::json StringSet(const std::set<std::string>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& value : values)
        result.push_back(value);
    return result;
}

nlohmann::json ProgramInventory(
    const std::map<std::string, ProgramUse>& programs) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [id, use] : programs) {
        result.push_back({
            {"id", id},
            {"draw_count", use.DrawCount},
            {"first_event", use.FirstEvent},
            {"last_event", use.LastEvent},
            {"first_guest_frame", use.FirstGuestFrame},
            {"last_guest_frame", use.LastGuestFrame},
            {"source_ids", StringSet(use.SourceIds)},
            {"legacy_state_keys", StringSet(use.LegacyKeys)},
            {"pipeline_ids", StringSet(use.PipelineIds)},
        });
    }
    return result;
}

std::filesystem::path AbsoluteNormalized(
    const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal();
}

void ObserveBooleanFeature(const nlohmann::json& features,
                           std::string_view key,
                           std::map<std::string, uint64_t>& counts) {
    if (features.value(std::string(key), false)) {
        ++counts[std::string(key)];
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path tracePath;
        std::filesystem::path outputPath;
        std::filesystem::path sourceDirectory;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--trace" && index + 1 < argc) {
                tracePath = argv[++index];
            } else if (argument == "--output" && index + 1 < argc) {
                outputPath = argv[++index];
            } else if (argument == "--source-dir" && index + 1 < argc) {
                sourceDirectory = argv[++index];
            } else {
                throw std::runtime_error(
                    "usage: oot3d_native_pica_trace_inventory --trace "
                    "<trace.jsonl> --output <inventory.json> "
                    "[--source-dir <directory>]");
            }
        }
        if (tracePath.empty() || outputPath.empty()) {
            throw std::runtime_error(
                "trace and output paths are required");
        }
        tracePath = AbsoluteNormalized(tracePath);
        outputPath = AbsoluteNormalized(outputPath);
        if (sourceDirectory.empty())
            sourceDirectory = outputPath.parent_path() / "shader_sources";
        sourceDirectory = AbsoluteNormalized(sourceDirectory);

        std::ifstream trace(tracePath, std::ios::binary);
        if (!trace)
            throw std::runtime_error("could not open trace: " +
                                     tracePath.string());

        std::map<std::string, ProgramUse> vertexPrograms;
        std::map<std::string, ProgramUse> fragmentPrograms;
        std::map<std::string, PipelineUse> pipelines;
        std::map<std::string, ShaderSource> sources;
        std::map<std::string, uint64_t> eventCounts;
        std::map<std::string, uint64_t> fragmentFeatureDrawCounts;
        std::map<std::string, uint64_t> unsupportedFeatureDrawCounts;
        std::set<std::string> unsupportedFragmentPrograms;
        uint64_t unsupportedFragmentDrawCount = 0;
        uint64_t descriptorSchemaVersion = 0;
        uint64_t lineNumber = 0;
        uint64_t drawCount = 0;
        for (std::string line; std::getline(trace, line);) {
            ++lineNumber;
            if (line.empty())
                continue;
            nlohmann::json event;
            try {
                event = nlohmann::json::parse(line);
            } catch (const std::exception& exception) {
                throw std::runtime_error(
                    "invalid trace JSON at line " +
                    std::to_string(lineNumber) + ": " + exception.what());
            }
            if (event.value("format", std::string{}) !=
                "oot3d_pica_semantic_trace_v1") {
                throw std::runtime_error(
                    "unsupported semantic trace format at line " +
                    std::to_string(lineNumber));
            }
            const std::string type = event.at("event").get<std::string>();
            ++eventCounts[type];
            if (type == "session_begin") {
                descriptorSchemaVersion =
                    event.value("descriptor_schema_version", 0U);
                continue;
            }
            if (type == "shader_source") {
                ShaderSource source;
                source.Stage = event.at("stage").get<std::string>();
                source.Id = event.at("source_id").get<std::string>();
                source.Source = event.at("source").get<std::string>();
                source.ProgramIds.insert(
                    event.at("canonical_program_id").get<std::string>());
                const std::string key = SourceKey(source.Stage, source.Id);
                const auto [found, inserted] =
                    sources.emplace(key, std::move(source));
                if (!inserted &&
                    found->second.Source !=
                        event.at("source").get<std::string>()) {
                    throw std::runtime_error(
                        "shader source ID collision: " + key);
                }
                continue;
            }
            if (type != "draw")
                continue;

            ++drawCount;
            const uint64_t eventId = event.at("event_id").get<uint64_t>();
            const uint64_t guestFrame =
                event.at("guest_frame").get<uint64_t>();
            const auto& identity = event.at("identity");
            const auto& generated = event.at("generated_source_ids");
            const auto& legacy = event.at("legacy_shader_keys");
            const std::string vertexProgram =
                identity.at("vertex_program").get<std::string>();
            const std::string fragmentProgram =
                identity.at("fragment_program").get<std::string>();
            const std::string pipelineId =
                identity.at("pipeline").get<std::string>();
            const std::string rasterStateId =
                identity.at("raster_state").get<std::string>();
            const std::string vertexSource =
                generated.at("vertex").get<std::string>();
            const std::string fragmentSource =
                generated.at("fragment").get<std::string>();

            if (event.contains("fragment_features")) {
                const auto& features = event.at("fragment_features");
                for (const std::string_view key : {
                         "fragment_lighting", "procedural_texture_enabled",
                         "procedural_texture_referenced", "fog", "gas"}) {
                    ObserveBooleanFeature(features, key,
                                          fragmentFeatureDrawCounts);
                }
                const uint32_t unsupportedMask =
                    features.value("unsupported_feature_mask", 0U);
                if (unsupportedMask != 0U) {
                    ++unsupportedFragmentDrawCount;
                    unsupportedFragmentPrograms.insert(fragmentProgram);
                }
                for (const auto& [name, bit] :
                     std::array<std::pair<std::string_view, uint32_t>, 8>{
                         {{"fragment_lighting", 1U << 0U},
                          {"procedural_texture", 1U << 1U},
                          {"texture_cube", 1U << 2U},
                          {"shadow_2d", 1U << 3U},
                          {"shadow_cube", 1U << 4U},
                          {"gas", 1U << 5U},
                          {"invalid_fog_mode", 1U << 6U},
                          {"tev_encoding", 1U << 7U}}}) {
                    if ((unsupportedMask & bit) != 0U) {
                        ++unsupportedFeatureDrawCounts[std::string(name)];
                    }
                }
            }

            auto& vertex = vertexPrograms[vertexProgram];
            Observe(eventId, guestFrame, vertex);
            vertex.SourceIds.insert(vertexSource);
            vertex.LegacyKeys.insert(
                legacy.at("vertex").get<std::string>());
            vertex.PipelineIds.insert(pipelineId);

            auto& fragment = fragmentPrograms[fragmentProgram];
            Observe(eventId, guestFrame, fragment);
            fragment.SourceIds.insert(fragmentSource);
            fragment.LegacyKeys.insert(
                legacy.at("fragment").get<std::string>());
            fragment.PipelineIds.insert(pipelineId);

            auto& pipeline = pipelines[pipelineId];
            Observe(eventId, guestFrame, pipeline);
            if (pipeline.VertexProgramId.empty()) {
                pipeline.VertexProgramId = vertexProgram;
                pipeline.FragmentProgramId = fragmentProgram;
                pipeline.RasterStateId = rasterStateId;
                pipeline.VertexSourceId = vertexSource;
                pipeline.FragmentSourceId = fragmentSource;
            } else if (pipeline.VertexProgramId != vertexProgram ||
                       pipeline.FragmentProgramId != fragmentProgram ||
                       pipeline.RasterStateId != rasterStateId ||
                       pipeline.VertexSourceId != vertexSource ||
                       pipeline.FragmentSourceId != fragmentSource) {
                throw std::runtime_error(
                    "canonical pipeline ID collision: " + pipelineId);
            }

            for (const auto& [stage, id, program] : {
                     std::tuple{"vertex", vertexSource, vertexProgram},
                     std::tuple{"fragment", fragmentSource,
                                fragmentProgram}}) {
                const auto source = sources.find(SourceKey(stage, id));
                if (source != sources.end()) {
                    ++source->second.DrawCount;
                    source->second.ProgramIds.insert(program);
                }
            }
        }
        if (descriptorSchemaVersion == 0U || drawCount == 0U)
            throw std::runtime_error(
                "trace has no canonical PICA draw inventory");

        std::filesystem::create_directories(outputPath.parent_path());
        std::filesystem::create_directories(sourceDirectory);
        nlohmann::json sourceInventory = nlohmann::json::array();
        for (const auto& [key, source] : sources) {
            const std::string extension =
                source.Stage == "vertex" ? ".vert.glsl" : ".frag.glsl";
            const std::filesystem::path file =
                sourceDirectory /
                (source.Stage + "_" + FileId(source.Id) + extension);
            std::ofstream output(file, std::ios::binary | std::ios::trunc);
            output << source.Source;
            if (!output)
                throw std::runtime_error(
                    "could not write shader source: " + file.string());
            sourceInventory.push_back({
                {"stage", source.Stage},
                {"id", source.Id},
                {"draw_count", source.DrawCount},
                {"program_ids", StringSet(source.ProgramIds)},
                {"path", file.generic_string()},
            });
        }

        nlohmann::json pipelineInventory = nlohmann::json::array();
        for (const auto& [id, pipeline] : pipelines) {
            pipelineInventory.push_back({
                {"id", id},
                {"draw_count", pipeline.DrawCount},
                {"first_event", pipeline.FirstEvent},
                {"last_event", pipeline.LastEvent},
                {"first_guest_frame", pipeline.FirstGuestFrame},
                {"last_guest_frame", pipeline.LastGuestFrame},
                {"vertex_program_id", pipeline.VertexProgramId},
                {"fragment_program_id", pipeline.FragmentProgramId},
                {"raster_state_id", pipeline.RasterStateId},
                {"vertex_source_id", pipeline.VertexSourceId},
                {"fragment_source_id", pipeline.FragmentSourceId},
            });
        }

        nlohmann::json counts = nlohmann::json::object();
        for (const auto& [name, count] : eventCounts)
            counts[name] = count;
        nlohmann::json featureCounts = nlohmann::json::object();
        for (const auto& [name, count] : fragmentFeatureDrawCounts)
            featureCounts[name] = count;
        nlohmann::json unsupportedCounts = nlohmann::json::object();
        for (const auto& [name, count] : unsupportedFeatureDrawCounts)
            unsupportedCounts[name] = count;
        const nlohmann::json inventory = {
            {"format", "oot3d_pica_shader_inventory_v1"},
            {"descriptor_schema_version", descriptorSchemaVersion},
            {"source_trace", tracePath.generic_string()},
            {"source_directory", sourceDirectory.generic_string()},
            {"event_counts", std::move(counts)},
            {"draw_count", drawCount},
            {"fragment_feature_draw_counts", std::move(featureCounts)},
            {"unsupported_fragment_draw_count",
             unsupportedFragmentDrawCount},
            {"unsupported_fragment_program_count",
             unsupportedFragmentPrograms.size()},
            {"unsupported_fragment_feature_draw_counts",
             std::move(unsupportedCounts)},
            {"unique_vertex_programs", vertexPrograms.size()},
            {"unique_fragment_programs", fragmentPrograms.size()},
            {"unique_pipelines", pipelines.size()},
            {"unique_shader_sources", sources.size()},
            {"vertex_programs", ProgramInventory(vertexPrograms)},
            {"fragment_programs", ProgramInventory(fragmentPrograms)},
            {"pipelines", std::move(pipelineInventory)},
            {"shader_sources", std::move(sourceInventory)},
        };
        std::ofstream output(outputPath,
                             std::ios::binary | std::ios::trunc);
        output << inventory.dump(2) << '\n';
        if (!output)
            throw std::runtime_error(
                "could not write shader inventory: " +
                outputPath.string());

        std::cout << "PICA inventory: " << drawCount << " draws, "
                  << vertexPrograms.size() << " vertex programs, "
                  << fragmentPrograms.size() << " fragment programs, "
                  << pipelines.size() << " pipelines, " << sources.size()
                  << " source modules\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_pica_trace_inventory: "
                  << exception.what() << '\n';
        return 1;
    }
}
