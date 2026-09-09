#include "oot3d_native_pica_transferable_cache.h"
#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_program_descriptor.h"
#include "fast/renderer3ds/pica_nri_shader_contract.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

// Offline ingestion only: no game execution, Vulkan device, or Citra shader
// binary is used. The exact runtime frontend remains the shader authority.
int main(int argc, char** argv) {
    using namespace Oot3dNativeGame;
    try {
        std::vector<std::filesystem::path> sources;
        std::filesystem::path output;
        std::string dialectName;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (i + 1 >= argc) throw std::runtime_error("missing option value");
            if (option == "--input") sources.emplace_back(argv[++i]);
            else if (option == "--output") output = argv[++i];
            else if (option == "--dialect") dialectName = argv[++i];
            else throw std::runtime_error("unknown option: " + option);
        }
        if (sources.empty() || output.empty() ||
            (dialectName != "citra-legacy-v1" && dialectName != "azahar-v1"))
            throw std::runtime_error("usage: oot3d_native_pica_cache_inventory --input <transferable.bin> "
                "[--input <other.bin>] --dialect <citra-legacy-v1|azahar-v1> --output <inventory.json>");
        const auto dialect = dialectName == "citra-legacy-v1"
            ? PicaTransferableDialect::CitraLegacy : PicaTransferableDialect::Azahar;
        const auto outputAbsolute = std::filesystem::weakly_canonical(output);
        for (const auto& path : sources)
            if (std::filesystem::weakly_canonical(path) == outputAbsolute)
                throw std::runtime_error("output must not overwrite an input cache");
        std::map<std::pair<std::string, std::string>, nlohmann::json> shaders;
        const auto add = [&](const char* stage, const std::string& source) {
            const auto id = Oot3d::Renderer::IdentifyPicaShaderSource(source);
            const auto key = std::pair{std::string(stage), FormatOot3dPicaCanonicalId(id.Id)};
            const nlohmann::json shader = {
                {"stage", stage}, {"source_id", key.second},
                {"secondary_hash", FormatOot3dPicaCanonicalId(id.SecondaryHash)},
                {"source_size", id.Size}, {"source", source}};
            const auto [found, inserted] = shaders.emplace(key, shader);
            if (!inserted && found->second != shader)
                throw std::runtime_error("shader identity collision");
        };
        nlohmann::json reports = nlohmann::json::array();
        bool incomplete = false;
        for (const auto& path : sources) {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("cannot open cache: " + path.string());
            const auto records = ReadPicaTransferableCache(input, dialect);
            size_t generated = 0, vertex = 0, geometry = 0;
            nlohmann::json failures = nlohmann::json::array();
            for (const auto& record : records) {
                if (record.Stage == PicaTransferableStage::Vertex) { ++vertex; continue; }
                if (record.Stage == PicaTransferableStage::Geometry) { ++geometry; continue; }
                Oot3dPicaDrawPacket packet;
                packet.Registers = record.Registers;
                Oot3dPicaDecodedDrawState state;
                Oot3dPicaGeneratedFragmentShader shader;
                std::string error;
                if (!DecodeOot3dPicaDrawState(packet, state, &error) ||
                    !GenerateOot3dPicaFragmentShader(packet, state, shader, &error,
                        Oot3dPicaShaderBuildPurpose::OfflineSource)) {
                    failures.push_back({{"source_id", FormatOot3dPicaCanonicalId(record.SourceIdentifier)},
                                        {"error", error}});
                    continue;
                }
                add("fragment", shader.Source);
                const auto nri = Fast::Renderer3ds::BuildPicaNriFragmentShaderVariant(shader.Source);
                if (!nri.Applied) {
                    failures.push_back({{"source_id", FormatOot3dPicaCanonicalId(record.SourceIdentifier)},
                                        {"error", nri.Error}});
                    continue;
                }
                add("nri_fragment", nri.Source);
                ++generated;
            }
            incomplete |= !failures.empty() || vertex != 0 || geometry != 0;
            reports.push_back({{"file", path.filename().string()}, {"records", records.size()},
                {"fragment_records_generated", generated}, {"vertex_records_not_imported", vertex},
                {"geometry_records_not_imported", geometry}, {"failures", failures}});
        }
        nlohmann::json entries = nlohmann::json::array();
        for (const auto& [key, shader] : shaders) entries.push_back(shader);
        const nlohmann::json inventory = {
            {"format", "oot3d_pica_effective_shader_inventory_v1"},
            {"descriptor_schema_version", kOot3dPicaProgramDescriptorSchemaVersion},
            {"shader_count", entries.size()}, {"shaders", entries},
            {"transferable_import", {{"dialect", dialectName}, {"inputs", reports},
                {"complete_import", !incomplete}, {"scope", "canonical_fragment_modules_only"},
                {"pipeline_pairs_available", false}, {"game_coverage_proven", false}}}};
        if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        file << inventory.dump(2) << '\n';
        file.close();
        if (!file) throw std::runtime_error("could not write inventory");
        std::cout << inventory.at("transferable_import").dump(2) << '\n'
                  << "Unique modules: " << entries.size() << '\n';
        // Partial evidence is useful for diagnosis, but must not be published
        // as a successful preparation job by Forge or a package builder.
        return incomplete || entries.empty() ? 3 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
