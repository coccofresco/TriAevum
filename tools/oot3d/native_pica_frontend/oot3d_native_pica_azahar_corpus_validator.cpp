#include "oot3d_native_pica_azahar_capture.h"

#include "oot3d_native_pica_draw_state.h"
#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_program_descriptor.h"
#include "fast/oot3d/pica_fragment_lighting.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#if defined(OOT3D_NATIVE_PICA_CORPUS_SHADERC)
#include <shaderc/shaderc.hpp>
#endif

namespace {

struct FirstOccurrence {
  std::string ScenarioId;
  std::string FramePath;
  uint64_t FrameIndex = 0;
  uint64_t DrawIndex = 0;
};

struct ErrorUse {
  uint64_t DrawCount = 0;
  std::set<std::string> PipelineIds;
  FirstOccurrence First;
};

struct PipelineUse {
  uint64_t DrawCount = 0;
  uint64_t DrawStateDecodeFailures = 0;
  uint64_t FullySupportedDrawCount = 0;
  uint64_t UnsupportedDrawCount = 0;
  uint64_t FragmentGenerationSuccesses = 0;
  uint64_t FragmentGenerationFailures = 0;
  uint32_t UnsupportedFeatureMask = 0;
  std::set<std::string> FragmentConfigHashes;
  std::set<std::string> DecodeErrors;
  std::set<std::string> GenerationErrors;
  FirstOccurrence First;
};

struct GeneratedShaderUse {
  std::string Source;
  uint64_t DrawCount = 0;
  bool SourceCollision = false;
  bool CompilationAttempted = false;
  bool CompilationSucceeded = false;
  std::string CompilationError;
  std::set<std::string> PipelineIds;
  std::set<std::string> FragmentConfigHashes;
  FirstOccurrence First;
};

struct Totals {
  uint64_t Scenarios = 0;
  uint64_t Frames = 0;
  uint64_t Draws = 0;
  uint64_t CaptureDecodeFailures = 0;
  uint64_t DrawStateDecodeFailures = 0;
  uint64_t FullySupportedDraws = 0;
  uint64_t UnsupportedDraws = 0;
  uint64_t FragmentGenerationSuccesses = 0;
  uint64_t FragmentGenerationFailures = 0;
  uint64_t UnexpectedFragmentGenerationFailures = 0;
  uint64_t UnsupportedButGeneratedDraws = 0;
  uint64_t UnmappedPipelineDraws = 0;
  uint64_t FragmentStateKeyCollisions = 0;
  uint64_t FragmentCompilationSuccesses = 0;
  uint64_t FragmentCompilationFailures = 0;
};

nlohmann::json ReadJson(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("could not open JSON: " + path.string());
  }
  nlohmann::json value;
  input >> value;
  return value;
}

std::filesystem::path AbsoluteNormalized(const std::filesystem::path &path) {
  return std::filesystem::absolute(path).lexically_normal();
}

std::string Hex64(uint64_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setfill('0') << std::setw(16) << value;
  return stream.str();
}

FirstOccurrence
MakeOccurrence(std::string_view scenarioId,
               const std::filesystem::path &framePath,
               const Oot3dNativeGame::Oot3dAzaharPicaDrawMetadata &metadata) {
  return {std::string(scenarioId), framePath.generic_string(),
          metadata.FrameIndex, metadata.DrawIndex};
}

void ObserveError(std::map<std::string, ErrorUse> &errors,
                  std::string_view message, std::string_view pipelineId,
                  const FirstOccurrence &occurrence) {
  auto &use = errors[std::string(message)];
  if (use.DrawCount++ == 0U) {
    use.First = occurrence;
  }
  use.PipelineIds.insert(std::string(pipelineId));
}

nlohmann::json OccurrenceJson(const FirstOccurrence &value) {
  return {
      {"scenario_id", value.ScenarioId},
      {"frame_path", value.FramePath},
      {"frame_index", value.FrameIndex},
      {"draw_index", value.DrawIndex},
  };
}

nlohmann::json StringSet(const std::set<std::string> &values) {
  nlohmann::json result = nlohmann::json::array();
  for (const auto &value : values) {
    result.push_back(value);
  }
  return result;
}

nlohmann::json ErrorInventory(const std::map<std::string, ErrorUse> &errors) {
  nlohmann::json result = nlohmann::json::array();
  for (const auto &[message, use] : errors) {
    result.push_back({
        {"message", message},
        {"draw_count", use.DrawCount},
        {"pipeline_ids", StringSet(use.PipelineIds)},
        {"first", OccurrenceJson(use.First)},
    });
  }
  return result;
}

constexpr std::array<std::pair<std::string_view, uint32_t>, 8>
    kUnsupportedFeatures{{
        {"fragment_lighting", 1U << 0U},
        {"procedural_texture", 1U << 1U},
        {"texture_cube", 1U << 2U},
        {"shadow_2d", 1U << 3U},
        {"shadow_cube", 1U << 4U},
        {"gas", 1U << 5U},
        {"invalid_fog_mode", 1U << 6U},
        {"tev_encoding", 1U << 7U},
    }};

void ObserveFeatureCounts(
    uint32_t mask, std::map<std::string, uint64_t> &drawCounts,
    std::map<std::string, std::set<std::string>> &pipelines,
    std::string_view pipelineId) {
  for (const auto &[name, bit] : kUnsupportedFeatures) {
    if ((mask & bit) == 0U) {
      continue;
    }
    ++drawCounts[std::string(name)];
    pipelines[std::string(name)].insert(std::string(pipelineId));
  }
}

std::map<std::string, std::string>
LoadPipelineIds(const nlohmann::json &captureSummary) {
  const auto coveragePath = captureSummary.find("shader_coverage_path");
  if (coveragePath == captureSummary.end() || !coveragePath->is_string()) {
    throw std::runtime_error("capture summary lacks shader_coverage_path");
  }
  const auto coverage = ReadJson(coveragePath->get<std::string>());
  if (coverage.value("format", std::string{}) !=
      "oot3d_azahar_shader_coverage_v1") {
    throw std::runtime_error("unsupported Azahar shader coverage format");
  }
  std::map<std::string, std::string> result;
  for (const auto &pipeline : coverage.at("pipelines")) {
    const std::string key = pipeline.at("identity").dump();
    const std::string id = pipeline.at("id").get<std::string>();
    if (!result.emplace(key, id).second) {
      throw std::runtime_error("duplicate Azahar pipeline identity: " + id);
    }
  }
  return result;
}

std::string
ResolvePipelineId(const Oot3dNativeGame::Oot3dAzaharPicaDrawMetadata &metadata,
                  const std::map<std::string, std::string> &pipelineIds,
                  Totals &totals) {
  const std::string key =
      Oot3dNativeGame::BuildOot3dAzaharPipelineKey(metadata);
  const auto found = pipelineIds.find(key);
  if (found != pipelineIds.end()) {
    return found->second;
  }
  ++totals.UnmappedPipelineDraws;
  return "unmapped:" + metadata.FragmentConfigHash + ':' +
         std::to_string(metadata.TriangleTopology) + ':' + metadata.DrawMode;
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::filesystem::path matrixPath;
    std::filesystem::path outputPath;
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument = argv[index];
      if (argument == "--matrix" && index + 1 < argc) {
        matrixPath = argv[++index];
      } else if (argument == "--output" && index + 1 < argc) {
        outputPath = argv[++index];
      } else {
        throw std::runtime_error(
            "usage: oot3d_native_pica_azahar_corpus_validator "
            "--matrix <coverage_matrix.json> --output <report.json>");
      }
    }
    if (matrixPath.empty() || outputPath.empty()) {
      throw std::runtime_error("matrix and output paths are required");
    }
    matrixPath = AbsoluteNormalized(matrixPath);
    outputPath = AbsoluteNormalized(outputPath);
    const auto matrix = ReadJson(matrixPath);
    if (matrix.value("format", std::string{}) !=
        "oot3d_azahar_coverage_matrix_v1") {
      throw std::runtime_error("unsupported Azahar coverage matrix format");
    }

    Totals totals;
    std::set<std::string> observedPipelineIds;
    std::set<std::string> observedFragmentConfigs;
    std::map<std::string, PipelineUse> pipelineUses;
    std::map<std::string, uint64_t> unsupportedFeatureDrawCounts;
    std::map<std::string, std::set<std::string>> unsupportedFeaturePipelines;
    std::map<std::string, uint64_t> observedFeatureDrawCounts;
    std::map<std::string, std::set<std::string>> observedFeaturePipelines;
    std::map<std::string, ErrorUse> captureDecodeErrors;
    std::map<std::string, ErrorUse> drawStateErrors;
    std::map<std::string, ErrorUse> generationErrors;
    std::map<uint64_t, GeneratedShaderUse> generatedShaders;

    for (const auto &result : matrix.at("results")) {
      if (result.value("status", std::string{}) != "passed") {
        continue;
      }
      const std::string scenarioId =
          result.at("scenario_id").get<std::string>();
      const auto captureSummary =
          ReadJson(result.at("coverage_summary_path").get<std::string>());
      const auto picaFrames = captureSummary.find("pica_frames");
      const bool hasRetainedFrames =
          picaFrames != captureSummary.end() && picaFrames->is_array() &&
          !picaFrames->empty();
      const bool evidenceRetained =
          captureSummary.contains("pica_evidence_retained")
              ? captureSummary.value("pica_evidence_retained", false)
              : hasRetainedFrames;
      if (captureSummary.value("format", std::string{}) !=
              "oot3d_azahar_coverage_capture_v1" ||
          !evidenceRetained || !hasRetainedFrames) {
        throw std::runtime_error("scenario lacks retained PICA evidence: " +
                                 scenarioId);
      }
      const auto pipelineIds = LoadPipelineIds(captureSummary);
      ++totals.Scenarios;
      for (const auto &frameValue : captureSummary.at("pica_frames")) {
        const std::filesystem::path framePath =
            AbsoluteNormalized(frameValue.get<std::string>());
        std::ifstream frame(framePath, std::ios::binary);
        if (!frame) {
          throw std::runtime_error("could not open PICA frame: " +
                                   framePath.string());
        }
        ++totals.Frames;
        uint64_t lineNumber = 0;
        for (std::string line; std::getline(frame, line);) {
          ++lineNumber;
          if (line.find("\"draw_begin\"") == std::string::npos) {
            continue;
          }
          nlohmann::json event;
          try {
            event = nlohmann::json::parse(line);
          } catch (const std::exception &exception) {
            throw std::runtime_error(
                "invalid PICA JSON at " + framePath.string() + ':' +
                std::to_string(lineNumber) + ": " + exception.what());
          }
          if (event.value("event", std::string{}) != "draw_begin") {
            continue;
          }
          ++totals.Draws;
          Oot3dNativeGame::Oot3dPicaDrawPacket packet;
          Oot3dNativeGame::Oot3dAzaharPicaDrawMetadata metadata;
          std::string error;
          if (!Oot3dNativeGame::DecodeOot3dAzaharPicaDraw(event, packet,
                                                          metadata, &error)) {
            ++totals.CaptureDecodeFailures;
            ObserveError(captureDecodeErrors, error, "<unresolved>",
                         {scenarioId, framePath.generic_string(), 0, 0});
            continue;
          }
          const std::string pipelineId =
              ResolvePipelineId(metadata, pipelineIds, totals);
          observedPipelineIds.insert(pipelineId);
          observedFragmentConfigs.insert(metadata.FragmentConfigHash);
          const auto occurrence =
              MakeOccurrence(scenarioId, framePath, metadata);
          auto &pipeline = pipelineUses[pipelineId];
          if (pipeline.DrawCount++ == 0U) {
            pipeline.First = occurrence;
          }
          pipeline.FragmentConfigHashes.insert(metadata.FragmentConfigHash);

          Oot3dNativeGame::Oot3dPicaDecodedDrawState state;
          error.clear();
          if (!Oot3dNativeGame::DecodeOot3dPicaDrawState(packet, state,
                                                         &error)) {
            ++totals.DrawStateDecodeFailures;
            ++pipeline.DrawStateDecodeFailures;
            pipeline.DecodeErrors.insert(error);
            ObserveError(drawStateErrors, error, pipelineId, occurrence);
            continue;
          }
          const auto features =
              Oot3dNativeGame::AnalyzeOot3dPicaFragmentFeatures(packet, state);
          const auto lighting =
              Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers);
          const auto observeFeature = [&](std::string_view name,
                                          bool observed) {
            if (!observed) {
              return;
            }
            ++observedFeatureDrawCounts[std::string(name)];
            observedFeaturePipelines[std::string(name)].insert(pipelineId);
          };
          observeFeature("fragment_lighting", features.FragmentLightingEnabled);
          observeFeature("procedural_texture",
                         features.ProceduralTextureEnabled &&
                             features.ProceduralTextureReferenced);
          observeFeature("fog", features.FogEnabled);
          observeFeature("projection2d",
                         state.Textures[0].Enabled &&
                             state.Textures[0].Type == 3U &&
                             (features.ReferencedTextureMask & 1U) != 0U);
          observeFeature("shadow2d_sample",
                         state.Textures[0].Enabled &&
                             state.Textures[0].Type == 2U &&
                             (features.ReferencedTextureMask & 1U) != 0U);
          observeFeature("shadow2d_producer",
                         state.OutputMerger.FragmentOperationMode == 3U);
          observeFeature("shadow_cube_sample",
                         state.Textures[0].Enabled &&
                             state.Textures[0].Type == 4U &&
                             (features.ReferencedTextureMask & 1U) != 0U);
          observeFeature("lighting_shadow_factor",
                         lighting.Enabled && lighting.ShadowFactorEnabled);
          pipeline.UnsupportedFeatureMask |= features.UnsupportedFeatureMask;
          if (features.FullySupported()) {
            ++totals.FullySupportedDraws;
            ++pipeline.FullySupportedDrawCount;
          } else {
            ++totals.UnsupportedDraws;
            ++pipeline.UnsupportedDrawCount;
            ObserveFeatureCounts(features.UnsupportedFeatureMask,
                                 unsupportedFeatureDrawCounts,
                                 unsupportedFeaturePipelines, pipelineId);
          }

          Oot3dNativeGame::Oot3dPicaGeneratedFragmentShader shader;
          error.clear();
          const bool generated =
              Oot3dNativeGame::GenerateOot3dPicaFragmentShader(packet, state,
                                                               shader, &error);
          if (generated) {
            ++totals.FragmentGenerationSuccesses;
            ++pipeline.FragmentGenerationSuccesses;
            auto &shaderUse = generatedShaders[shader.StateKey];
            if (shaderUse.DrawCount++ == 0U) {
              shaderUse.Source = shader.Source;
              shaderUse.First = occurrence;
            } else if (shaderUse.Source != shader.Source &&
                       !shaderUse.SourceCollision) {
              shaderUse.SourceCollision = true;
              ++totals.FragmentStateKeyCollisions;
            }
            shaderUse.PipelineIds.insert(pipelineId);
            shaderUse.FragmentConfigHashes.insert(metadata.FragmentConfigHash);
            if (!features.FullySupported()) {
              ++totals.UnsupportedButGeneratedDraws;
            }
          } else {
            ++totals.FragmentGenerationFailures;
            ++pipeline.FragmentGenerationFailures;
            pipeline.GenerationErrors.insert(error);
            ObserveError(generationErrors, error, pipelineId, occurrence);
            if (features.FullySupported()) {
              ++totals.UnexpectedFragmentGenerationFailures;
            }
          }
        }
      }
    }
    if (totals.Draws == 0U) {
      throw std::runtime_error("coverage matrix contains no retained draws");
    }

    bool fragmentCompilationAvailable = false;
    std::set<std::string> compiledFragmentConfigs;
    std::set<std::string> compiledPipelineIds;
#if defined(OOT3D_NATIVE_PICA_CORPUS_SHADERC)
    fragmentCompilationAvailable = true;
    shaderc::Compiler compiler;
    shaderc::CompileOptions compileOptions;
    compileOptions.SetTargetEnvironment(shaderc_target_env_vulkan,
                                        shaderc_env_version_vulkan_1_1);
    compileOptions.SetOptimizationLevel(shaderc_optimization_level_performance);
    for (auto &[stateKey, use] : generatedShaders) {
      if (use.SourceCollision) {
        continue;
      }
      use.CompilationAttempted = true;
      const std::string sourceName =
          "native_pica_" + Hex64(stateKey) + ".frag";
      const auto result = compiler.CompileGlslToSpv(
          use.Source, shaderc_fragment_shader, sourceName.c_str(), "main",
          compileOptions);
      if (result.GetCompilationStatus() ==
          shaderc_compilation_status_success) {
        use.CompilationSucceeded = true;
        ++totals.FragmentCompilationSuccesses;
        compiledFragmentConfigs.insert(use.FragmentConfigHashes.begin(),
                                       use.FragmentConfigHashes.end());
        compiledPipelineIds.insert(use.PipelineIds.begin(),
                                   use.PipelineIds.end());
      } else {
        use.CompilationError = result.GetErrorMessage();
        ++totals.FragmentCompilationFailures;
      }
    }
#endif

    nlohmann::json featureCounts = nlohmann::json::object();
    for (const auto &[name, count] : unsupportedFeatureDrawCounts) {
      featureCounts[name] = {
          {"draw_count", count},
          {"pipeline_count", unsupportedFeaturePipelines[name].size()},
          {"pipeline_ids", StringSet(unsupportedFeaturePipelines[name])},
      };
    }
    nlohmann::json observedFeatureCounts = nlohmann::json::object();
    for (const auto &[name, count] : observedFeatureDrawCounts) {
      observedFeatureCounts[name] = {
          {"draw_count", count},
          {"pipeline_count", observedFeaturePipelines[name].size()},
          {"pipeline_ids", StringSet(observedFeaturePipelines[name])},
      };
    }
    nlohmann::json pipelines = nlohmann::json::array();
    for (const auto &[id, use] : pipelineUses) {
      pipelines.push_back({
          {"id", id},
          {"draw_count", use.DrawCount},
          {"draw_state_decode_failures", use.DrawStateDecodeFailures},
          {"fully_supported_draw_count", use.FullySupportedDrawCount},
          {"unsupported_draw_count", use.UnsupportedDrawCount},
          {"fragment_generation_successes", use.FragmentGenerationSuccesses},
          {"fragment_generation_failures", use.FragmentGenerationFailures},
          {"unsupported_feature_mask", use.UnsupportedFeatureMask},
          {"fragment_config_hashes", StringSet(use.FragmentConfigHashes)},
          {"decode_errors", StringSet(use.DecodeErrors)},
          {"generation_errors", StringSet(use.GenerationErrors)},
          {"first", OccurrenceJson(use.First)},
      });
    }

    nlohmann::json shaderCompilationFailures = nlohmann::json::array();
    nlohmann::json shaderStateKeyCollisions = nlohmann::json::array();
    for (const auto &[stateKey, use] : generatedShaders) {
      if (use.SourceCollision) {
        shaderStateKeyCollisions.push_back({
            {"state_key", Hex64(stateKey)},
            {"draw_count", use.DrawCount},
            {"pipeline_ids", StringSet(use.PipelineIds)},
            {"fragment_config_hashes", StringSet(use.FragmentConfigHashes)},
            {"first", OccurrenceJson(use.First)},
        });
      }
      if (use.CompilationAttempted && !use.CompilationSucceeded) {
        shaderCompilationFailures.push_back({
            {"state_key", Hex64(stateKey)},
            {"message", use.CompilationError},
            {"draw_count", use.DrawCount},
            {"pipeline_ids", StringSet(use.PipelineIds)},
            {"fragment_config_hashes", StringSet(use.FragmentConfigHashes)},
            {"first", OccurrenceJson(use.First)},
        });
      }
    }

    const uint64_t structuralFailures =
        totals.CaptureDecodeFailures + totals.DrawStateDecodeFailures +
        totals.UnexpectedFragmentGenerationFailures +
        totals.UnmappedPipelineDraws + totals.FragmentStateKeyCollisions +
        totals.FragmentCompilationFailures;
    const char *status =
        structuralFailures != 0U
            ? "structural_failures"
            : (totals.UnsupportedDraws != 0U ? "valid_with_feature_gaps"
                                             : "valid");
    const nlohmann::json report = {
        {"format", "oot3d_native_pica_azahar_validation_v1"},
        {"evidence_role", "validation_only_not_runtime_input"},
        {"source_matrix", matrixPath.generic_string()},
        {"scope", "register decode, raster state, fragment feature analysis, "
                  "fragment shader generation and Vulkan compilation when "
                  "shaderc is available"},
        {"status", status},
        {"counts",
         {{"scenarios", totals.Scenarios},
          {"frames", totals.Frames},
          {"draws", totals.Draws},
          {"unique_azahar_pipelines", observedPipelineIds.size()},
          {"unique_fragment_configs", observedFragmentConfigs.size()},
          {"capture_decode_failures", totals.CaptureDecodeFailures},
          {"draw_state_decode_failures", totals.DrawStateDecodeFailures},
          {"fully_supported_draws", totals.FullySupportedDraws},
          {"unsupported_draws", totals.UnsupportedDraws},
          {"fragment_generation_successes", totals.FragmentGenerationSuccesses},
          {"fragment_generation_failures", totals.FragmentGenerationFailures},
          {"unique_generated_fragment_shaders", generatedShaders.size()},
          {"fragment_shader_compilation_available",
           fragmentCompilationAvailable},
          {"fragment_compilation_successes",
           totals.FragmentCompilationSuccesses},
          {"fragment_compilation_failures",
           totals.FragmentCompilationFailures},
          {"compiled_fragment_configs", compiledFragmentConfigs.size()},
          {"compiled_pipeline_ids", compiledPipelineIds.size()},
          {"fragment_state_key_collisions",
           totals.FragmentStateKeyCollisions},
          {"unexpected_fragment_generation_failures",
           totals.UnexpectedFragmentGenerationFailures},
          {"unsupported_but_generated_draws",
           totals.UnsupportedButGeneratedDraws},
          {"unmapped_pipeline_draws", totals.UnmappedPipelineDraws},
          {"structural_failures", structuralFailures}}},
        {"unsupported_fragment_features", std::move(featureCounts)},
        {"observed_fragment_features", std::move(observedFeatureCounts)},
        {"capture_decode_errors", ErrorInventory(captureDecodeErrors)},
        {"draw_state_decode_errors", ErrorInventory(drawStateErrors)},
        {"fragment_generation_errors", ErrorInventory(generationErrors)},
        {"fragment_shader_compilation_failures",
         std::move(shaderCompilationFailures)},
        {"fragment_shader_state_key_collisions",
         std::move(shaderStateKeyCollisions)},
        {"pipelines", std::move(pipelines)},
    };

    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    output << report.dump(2) << '\n';
    if (!output) {
      throw std::runtime_error("could not write validation report: " +
                               outputPath.string());
    }
    std::cout << "Azahar PICA corpus: " << totals.Draws << " draws, "
              << observedPipelineIds.size() << " pipelines, "
              << totals.FullySupportedDraws << " fully supported, "
              << totals.UnsupportedDraws << " with declared gaps, "
              << structuralFailures << " structural failures\n";
    if (fragmentCompilationAvailable) {
      std::cout << "Vulkan fragment compilation: "
                << totals.FragmentCompilationSuccesses << "/"
                << generatedShaders.size() << " unique shaders, covering "
                << compiledFragmentConfigs.size() << " fragment configs and "
                << compiledPipelineIds.size() << " pipelines\n";
    }
    std::cout << "Report: " << outputPath.string() << '\n';
    return structuralFailures == 0U ? 0 : 2;
  } catch (const std::exception &exception) {
    std::cerr << "oot3d_native_pica_azahar_corpus_validator: "
              << exception.what() << '\n';
    return 1;
  }
}
