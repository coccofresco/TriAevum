// Offline replay of recorded PICA variant requests (issue #76 strategy).
//
// A session pairs the raw PICA state of each canonical pipeline
// (OOT3D_PICA_RAW_DRAWS) with the distinct variant requests the renderer
// resolved (OOT3D_PICA_VARIANT_REQUESTS). This tool rebuilds each canonical
// plan with this build's native generators, binds the regenerated sources,
// hooks and typed vertex program to the recorded request and calls the real
// PicaShaderPipelineCache::Resolve with the recorded effects. It writes the
// effective shader inventory the AOT compiler consumes and, given the
// session's pipeline inventories, rewrites their recipes to the regenerated
// sources. No game, GPU or shader compiler runs.
//
// With an unchanged generator every regenerated identity must equal the
// recorded witness (--require-witness). With a changed generator the
// witnesses only link recipes; every request must still resolve.
//
// --infer-report is a dry run of inferred coverage (strategy §4): each
// recorded request is resolved again with one unresolved per-draw semantic
// changed at a time (surface availability, receivers, isolation, temporal
// motion, relief, reflection), and the report counts the modules that are not
// already observed. Nothing it finds reaches the inventory: widening the corpus
// is a budgeted decision taken on this report.
//
// Alpha-tested shadow casters run their draw's fragment program up to the
// native alpha test. A draw with a parametric (always-available) form derives
// its caster from that parametric base, which the built-in caster family holds
// for the bases it enumerates; any other derives its caster from the canonical
// program. The replay adds every caster the family does not hold as an NRI
// fragment of the corpus.

#include "oot3d_native_pica_draw_state.h"
#include "oot3d_native_pica_raw_draw_record.h"
#include "oot3d_native_pica_vulkan_bridge.h"
#include "oot3d_native_pica_vulkan_plan.h"

#include "fast/oot3d/pica_caster_fragment_binaries.h"
#include "fast/oot3d/pica_directional_shadow_caster.h"
#include "fast/renderer/pass_shader_artifact.h"

#include "fast/oot3d/pica_aot_shader_pack.h"
#include "fast/oot3d/pica_nri_shader_contract.h"
#include "fast/oot3d/pica_pipeline_manifest.h"
#include "fast/oot3d/pica_shader_pipeline_cache.h"
#include "fast/oot3d/pica_variant_request_record.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {
using Json = nlohmann::json;
using namespace Oot3dNativeGame;
using Fast::Oot3d::PicaShaderPipelineRequest;
using Fast::Oot3d::PicaVariantShaderIdentity;

// PicaCompareFunction::Always in the alpha test register.
constexpr uint32_t kPicaCompareAlways = 1U;

std::string Hex64(uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

PicaVariantShaderIdentity Identity(const Oot3d::Renderer::PicaShaderSourceIdentity& identity) {
    return {identity.Id, identity.SecondaryHash, identity.Size};
}

PicaVariantShaderIdentity Identity(const Fast::Oot3d::PicaAotShaderSourceIdentity& identity) {
    return {identity.Id, identity.SecondaryHash, identity.Size};
}

// A regenerated canonical plan, owning what a request view borrows.
struct CanonicalPlan {
    uint32_t DescriptorSchemaVersion = 0;
    uint64_t VertexKey = 0;
    uint64_t FragmentKey = 0;
    std::string VertexSource;
    std::string FragmentSource;
    Oot3d::Renderer::PicaShaderSourceIdentity VertexIdentity;
    Oot3d::Renderer::PicaShaderSourceIdentity FragmentIdentity;
    std::shared_ptr<const Oot3dPicaTemporalVertexProgram> Temporal;
    Oot3d::Renderer::PicaShaderHookLayout FragmentHooks;
};

// A program as a pipeline recipe names it: output source identities and the
// effective shader keys. One source pair can carry several keys.
using ProgramKey = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;

struct Regenerated {
    PicaVariantShaderIdentity Vertex;
    PicaVariantShaderIdentity Fragment;
    PicaVariantShaderIdentity NriFragment;
    bool NriAvailable = false;
    uint64_t VertexKey = 0;
    uint64_t FragmentKey = 0;
    Fast::Oot3d::PicaShaderInstrumentationFeature Requested{};
    Fast::Oot3d::PicaShaderInstrumentationFeature Applied{};
    Fast::Oot3d::PicaFragmentInstrumentationOutputLayout Outputs;
    bool Canonical = true;
};

struct Options {
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> Sessions;
    std::vector<std::filesystem::path> Pipelines;
    std::filesystem::path ShaderInventory;
    std::filesystem::path PipelineManifest;
    std::filesystem::path Report;
    std::filesystem::path InferReport;
    bool RequireWitness = false;
};

// One unresolved per-draw semantic of a variant request, changed in place.
struct InferredSemantic {
    const char* Name;
    void (*Apply)(PicaShaderPipelineRequest& request);
};

constexpr InferredSemantic kInferredSemantics[] = {
    {"surface_lighting", [](PicaShaderPipelineRequest& r) { r.SurfaceLightingAvailable = !r.SurfaceLightingAvailable; }},
    {"directional_shadow_receiver",
     [](PicaShaderPipelineRequest& r) { r.Draw.DirectionalShadowReceiver = !r.Draw.DirectionalShadowReceiver; }},
    {"point_shadow_receiver",
     [](PicaShaderPipelineRequest& r) { r.Draw.PointShadowReceiver = !r.Draw.PointShadowReceiver; }},
    {"isolated_lighting", [](PicaShaderPipelineRequest& r) { r.Draw.IsolatedLighting = !r.Draw.IsolatedLighting; }},
    {"temporal_motion", [](PicaShaderPipelineRequest& r) { r.TemporalMotionEnabled = !r.TemporalMotionEnabled; }},
    {"relief_texture", [](PicaShaderPipelineRequest& r) { r.ReliefTexture = r.ReliefTexture == 0xffU ? 0U : 0xffU; }},
    {"reflection",
     [](PicaShaderPipelineRequest& r) {
         if (r.ReflectionProfile.has_value())
             r.ReflectionProfile.reset();
         else
             r.ReflectionProfile = Fast::Oot3d::ReflectionMaterialParameters{};
     }},
};

int Usage() {
    std::cerr << "usage: oot3d_native_pica_variant_inventory --session RAW_DRAWS VARIANT_REQUESTS [...]\n"
                 "       [--pipelines SESSION_PIPELINE_INVENTORY ...] --shader-inventory OUT\n"
                 "       [--pipeline-manifest OUT] --report OUT [--require-witness] [--infer-report OUT]\n";
    return 64;
}

int Run(const Options& options) {
    if (Fast::Oot3d::PicaFaceNormalGuideDiagnosticRequested()) {
        std::cerr << "OOT3D_GRAPHICS_FACE_NORMAL_GUIDE is set: it changes generated variants; unset it\n";
        return 2;
    }
    Fast::Oot3d::PicaEffectiveShaderInventory inventory;
    std::string error;
    if (!inventory.Configure(options.ShaderInventory, &error)) throw std::runtime_error(error);

    Json sessions = Json::array();
    // Recorded programs (as the session's pipeline inventories name them)
    // -> regenerated programs, for recipe rewriting.
    std::map<ProgramKey, std::set<ProgramKey>> recipeLinks;
    std::map<ProgramKey, Regenerated> regeneratedByIdentity;
    uint64_t totalFailures = 0, totalWitnessMismatches = 0;
    // Inferred coverage dry run: (stage, source id) observed vs. reachable.
    struct InferredEntry {
        uint64_t Candidates = 0, Rejected = 0, SourceBytes = 0;
        std::set<std::pair<uint32_t, uint64_t>> Modules;
    };
    std::set<std::pair<uint32_t, uint64_t>> observedModules, inferredModules;
    uint64_t observedBytes = 0, inferredBytes = 0;
    std::map<std::string, InferredEntry> inferred;

    for (const auto& [rawPath, requestPath] : options.Sessions) {
        Oot3dPicaRawDrawSession raw;
        if (!LoadOot3dPicaRawDraws(rawPath, raw, &error)) throw std::runtime_error(error);
        Fast::Oot3d::PicaVariantRequestSession requests;
        if (!Fast::Oot3d::LoadPicaVariantRequests(requestPath, requests, &error)) throw std::runtime_error(error);
        if (requests.FaceNormalGuideDiagnostic)
            throw std::runtime_error(requestPath.string() +
                                     ": recorded with OOT3D_GRAPHICS_FACE_NORMAL_GUIDE; record it again without");

        Oot3dPicaVulkanShaderSourceCache shaderCache;
        shaderCache.TevMode = raw.ParametricTev ? Oot3dPicaTevMode::Parametric : Oot3dPicaTevMode::Specialized;
        shaderCache.UberFragments = raw.UberFragments;
        std::map<uint64_t, CanonicalPlan> plans;
        Json planFailures = Json::array();
        uint64_t canonicalWitnessMismatches = 0, casterFragments = 0, alphaTested = 0, alphaTestedParametric = 0,
                 parametricCastersOutsideFamily = 0;
        for (const auto& draw : raw.Draws) {
            Oot3dPicaDrawSubmission submission;
            submission.Packet = draw.Packet;
            Oot3dPicaVulkanDrawPlan plan;
            if (!DecodeOot3dPicaDrawState(submission.Packet, submission.State, &error) ||
                !BuildOot3dPicaVulkanPipelinePlan(submission, plan, &error, &shaderCache)) {
                if (planFailures.size() < 32U)
                    planFailures.push_back({{"pipeline_id", Hex64(draw.CanonicalPipelineId)}, {"error", error}});
                ++totalFailures;
                continue;
            }
            CanonicalPlan canonical;
            canonical.DescriptorSchemaVersion = plan.CanonicalIdentity.SchemaVersion;
            canonical.VertexKey = plan.VertexShader.StateKey;
            canonical.FragmentKey = plan.FragmentShader.StateKey;
            canonical.VertexSource = std::string(plan.ResolvedVertexShaderSource());
            canonical.FragmentSource = std::string(plan.ResolvedFragmentShaderSource());
            canonical.VertexIdentity = plan.VertexShader.SourceIdentity;
            canonical.FragmentIdentity = plan.FragmentShader.SourceIdentity;
            canonical.Temporal = plan.VertexShader.TemporalProgram;
            canonical.FragmentHooks = plan.FragmentShader.Hooks;
            const auto& witness = draw.Witness;
            if (canonical.VertexKey != witness.VertexShaderKey ||
                canonical.FragmentKey != witness.FragmentShaderKey ||
                canonical.VertexIdentity.Id != witness.VertexSourceId ||
                canonical.FragmentIdentity.Id != witness.FragmentSourceId)
                ++canonicalWitnessMismatches;
            // Canonical recipes link through their canonical identities.
            regeneratedByIdentity.try_emplace(
                ProgramKey{witness.VertexSourceId, witness.FragmentSourceId, witness.VertexShaderKey,
                           witness.FragmentShaderKey},
                Regenerated{Identity(canonical.VertexIdentity), Identity(canonical.FragmentIdentity), {}, false,
                            canonical.VertexKey, canonical.FragmentKey});
            // The caster the shadow passes derive (gfx_vulkan_pica_draw.cpp:
            // alpha-tested, not Always): from the parametric base when the draw
            // has a valid one, else from the canonical program.
            const auto alphaTest = DecodeOot3dPicaAlphaTest(plan.FragmentShader.Uniforms.FragmentControl[1]);
            const bool parametric = plan.SharedUberFragmentShaderSource != nullptr &&
                                    plan.UberFragmentShader.Hooks.ValidFor(*plan.SharedUberFragmentShaderSource);
            if (alphaTest.Enabled && alphaTest.Compare != kPicaCompareAlways) {
                ++alphaTested;
                if (parametric) ++alphaTestedParametric;
                const auto caster = parametric
                    ? Fast::Oot3d::BuildPicaShadowCasterAlphaFragmentShader(*plan.SharedUberFragmentShaderSource,
                                                                            plan.UberFragmentShader.StateKey,
                                                                            plan.UberFragmentShader.Hooks)
                    : Fast::Oot3d::BuildPicaShadowCasterAlphaFragmentShader(
                          canonical.FragmentSource, canonical.FragmentKey, canonical.FragmentHooks);
                // The runtime resolves a caster from the built-in family first;
                // any other compiles unless the corpus carries it.
                const bool builtIn = parametric && caster.Applied() &&
                    !Fast::Renderer::FindPassShaderArtifact(Fast::Oot3d::CasterFragmentArtifacts(), caster.Source,
                                                            Fast::Renderer::SpirvStage::Fragment, {}).empty();
                if (caster.Applied() && !builtIn) {
                    inventory.Observe(Fast::Oot3d::PicaAotShaderStage::NriFragment, caster.Source,
                                      canonical.DescriptorSchemaVersion, draw.CanonicalPipelineId,
                                      parametric ? plan.UberFragmentShader.StateKey : canonical.FragmentKey, 0U);
                    ++casterFragments;
                    if (parametric) ++parametricCastersOutsideFamily;
                }
            }
            plans.emplace(draw.CanonicalPipelineId, std::move(canonical));
        }

        // Variants depend on the effects: resolve one effects value at a time,
        // as the runtime cache does per settings revision.
        std::vector<const Fast::Oot3d::PicaVariantRequestRecord*> ordered;
        for (const auto& request : requests.Requests) ordered.push_back(&request);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const auto* left, const auto* right) { return left->EffectsId < right->EffectsId; });
        Fast::Oot3d::PicaShaderPipelineCache cache;
        uint64_t unresolvedRaw = 0, resolveFailures = 0, outputWitnessMismatches = 0, instrumented = 0;
        Json failures = Json::array();
        // The recorded request bound to this build's regenerated canonical plan.
        const auto bind = [](const Fast::Oot3d::PicaVariantRequestRecord& record, const CanonicalPlan& canonical) {
            PicaShaderPipelineRequest request = record.Request;
            request.VertexShaderSource = canonical.VertexSource;
            request.FragmentShaderSource = canonical.FragmentSource;
            request.VertexShaderKey = canonical.VertexKey;
            request.FragmentShaderKey = canonical.FragmentKey;
            request.VertexShaderSourceIdentity = canonical.VertexIdentity;
            request.FragmentShaderSourceIdentity = canonical.FragmentIdentity;
            if (canonical.Temporal != nullptr)
                request.TemporalVertexProgram = {canonical.Temporal->Hooks,
                                                 canonical.Temporal->PreviousRegisterState,
                                                 canonical.Temporal->PreviousMainBody};
            request.FragmentShaderHooks = canonical.FragmentHooks;
            return request;
        };
        for (const auto* record : ordered) {
            const auto plan = plans.find(record->CanonicalPipelineId);
            if (plan == plans.end()) {
                ++unresolvedRaw;
                continue;
            }
            const auto& canonical = plan->second;
            const PicaShaderPipelineRequest request = bind(*record, canonical);
            const auto& effects = requests.Effects.at(record->EffectsId);
            cache.BeginFrame(record->EffectsId);
            const Fast::Oot3d::PicaShaderPipelineResult* result = nullptr;
            try {
                result = &cache.Resolve(request, effects);
            } catch (const std::exception& exception) {
                ++resolveFailures;
                if (failures.size() < 32U)
                    failures.push_back({{"pipeline_id", Hex64(record->CanonicalPipelineId)},
                                        {"error", exception.what()}});
                continue;
            }
            const auto witness = Fast::Oot3d::WitnessPicaVariantRequest(request, *result);
            if (!(witness == record->Witness)) ++outputWitnessMismatches;
            if (!result->IsCanonical()) ++instrumented;
            const auto schema = canonical.DescriptorSchemaVersion;
            inventory.Observe(Fast::Oot3d::PicaAotShaderStage::Vertex, result->VertexShaderSource, schema,
                              record->CanonicalPipelineId, result->VertexShaderKey, record->SettingsRevision);
            inventory.Observe(Fast::Oot3d::PicaAotShaderStage::Fragment, result->FragmentShaderSource, schema,
                              record->CanonicalPipelineId, result->FragmentShaderKey, record->SettingsRevision);
            Regenerated regenerated{Identity(result->VertexShaderSourceIdentity),
                                    Identity(result->FragmentShaderSourceIdentity),
                                    {},
                                    false,
                                    result->VertexShaderKey,
                                    result->FragmentShaderKey,
                                    result->RequestedFeatures,
                                    result->AppliedFeatures,
                                    result->FragmentOutputs,
                                    result->IsCanonical()};
            const auto nri = Fast::Oot3d::BuildPicaNriFragmentShaderVariant(result->FragmentShaderSource);
            observedModules.insert({1U, regenerated.Vertex.Id});
            observedModules.insert({2U, regenerated.Fragment.Id});
            if (nri.Applied) {
                regenerated.NriAvailable = true;
                regenerated.NriFragment = Identity(Fast::Oot3d::IdentifyPicaAotShaderSource(nri.Source));
                inventory.Observe(Fast::Oot3d::PicaAotShaderStage::NriFragment, nri.Source, schema,
                                  record->CanonicalPipelineId, result->FragmentShaderKey, record->SettingsRevision);
                observedModules.insert({3U, regenerated.NriFragment.Id});
                observedBytes += nri.Source.size();
            }
            observedBytes += result->VertexShaderSource.size() + result->FragmentShaderSource.size();
            const ProgramKey recorded{record->Witness.Vertex.Id, record->Witness.Fragment.Id,
                                      record->Witness.VertexKey, record->Witness.FragmentKey};
            const ProgramKey current{regenerated.Vertex.Id, regenerated.Fragment.Id, regenerated.VertexKey,
                                     regenerated.FragmentKey};
            recipeLinks[recorded].insert(current);
            regeneratedByIdentity.insert_or_assign(current, regenerated);
        }
        if (!options.InferReport.empty()) {
            for (const auto& semantic : kInferredSemantics) {
                auto& entry = inferred[semantic.Name];
                for (const auto* record : ordered) {
                    const auto plan = plans.find(record->CanonicalPipelineId);
                    if (plan == plans.end()) continue;
                    auto request = bind(*record, plan->second);
                    semantic.Apply(request);
                    ++entry.Candidates;
                    cache.BeginFrame(record->EffectsId);
                    try {
                        const auto& result = cache.Resolve(request, requests.Effects.at(record->EffectsId));
                        const auto add = [&](uint32_t stage, std::string_view source) {
                            const auto id = Fast::Oot3d::IdentifyPicaAotShaderSource(source).Id;
                            if (observedModules.contains({stage, id})) return;
                            if (entry.Modules.insert({stage, id}).second) entry.SourceBytes += source.size();
                            if (inferredModules.insert({stage, id}).second) inferredBytes += source.size();
                        };
                        add(1U, result.VertexShaderSource);
                        add(2U, result.FragmentShaderSource);
                        if (const auto nri = Fast::Oot3d::BuildPicaNriFragmentShaderVariant(result.FragmentShaderSource);
                            nri.Applied)
                            add(3U, nri.Source);
                    } catch (const std::exception&) {
                        ++entry.Rejected;
                    }
                }
            }
        }
        totalFailures += unresolvedRaw + resolveFailures;
        totalWitnessMismatches += canonicalWitnessMismatches + outputWitnessMismatches;
        sessions.push_back({{"raw_draws", rawPath.string()},
                            {"variant_requests", requestPath.string()},
                            {"parametric_tev", raw.ParametricTev},
                            {"uber_fragments", raw.UberFragments},
                            {"canonical_plans", plans.size()},
                            {"plan_failures", planFailures},
                            {"canonical_witness_mismatches", canonicalWitnessMismatches},
                            {"shadow_caster_fragments", casterFragments},
                            {"alpha_tested_draws", alphaTested},
                            {"alpha_tested_parametric", alphaTestedParametric},
                            {"parametric_casters_outside_family", parametricCastersOutsideFamily},
                            {"effects", requests.Effects.size()},
                            {"requests", requests.Requests.size()},
                            {"instrumented_requests", instrumented},
                            {"requests_without_raw_draw", unresolvedRaw},
                            {"resolve_failures", failures},
                            {"output_witness_mismatches", outputWitnessMismatches}});
    }
    if (!inventory.Finish(&error)) throw std::runtime_error(error);

    Json recipes = nullptr;
    if (!options.Pipelines.empty()) {
        if (options.PipelineManifest.empty()) throw std::runtime_error("--pipelines needs --pipeline-manifest");
        std::map<uint64_t, Fast::Oot3d::PicaGraphicsPipelineManifestEntry> rewritten;
        uint32_t schema = 0;
        uint64_t input = 0, unlinked = 0, contractChanged = 0, ambiguous = 0;
        for (const auto& path : options.Pipelines) {
            Fast::Oot3d::PicaGraphicsPipelineManifest manifest;
            if (!manifest.Load(path, &error)) throw std::runtime_error(error);
            schema = manifest.DescriptorSchemaVersion();
            for (auto entry : manifest.Entries()) {
                ++input;
                const ProgramKey recorded{entry.VertexSource.Id, entry.FragmentSource.Id, entry.VertexShaderKey,
                                          entry.FragmentShaderKey};
                std::set<ProgramKey> targets;
                if (const auto link = recipeLinks.find(recorded); link != recipeLinks.end()) targets = link->second;
                else if (regeneratedByIdentity.contains(recorded)) targets.insert(recorded);
                if (targets.empty()) {
                    ++unlinked;
                    continue;
                }
                ambiguous += targets.size() > 1U ? 1U : 0U;
                for (const auto& target : targets) {
                    auto copy = entry;
                    const auto found = regeneratedByIdentity.find(target);
                    if (found == regeneratedByIdentity.end()) continue;
                    const auto& program = found->second;
                    copy.VertexShaderKey = program.VertexKey;
                    copy.FragmentShaderKey = program.FragmentKey;
                    copy.VertexSource = {program.Vertex.Id, program.Vertex.SecondaryHash, program.Vertex.Size};
                    copy.FragmentSource = {program.Fragment.Id, program.Fragment.SecondaryHash,
                                           program.Fragment.Size};
                    if (!program.Canonical || program.NriAvailable) {
                        copy.NriFragmentSource = {program.NriFragment.Id, program.NriFragment.SecondaryHash,
                                                  program.NriFragment.Size};
                        copy.NriFragmentAvailable = program.NriAvailable;
                    }
                    if (!program.Canonical) {
                        copy.RequestedFeatures = program.Requested;
                        copy.AppliedFeatures = program.Applied;
                        if (!(copy.ShaderOutputs == program.Outputs)) ++contractChanged;
                        copy.ShaderOutputs = program.Outputs;
                    }
                    rewritten.insert_or_assign(copy.StructuralId(), copy);
                }
            }
        }
        std::vector<Fast::Oot3d::PicaGraphicsPipelineManifestEntry> entries;
        for (auto& [id, entry] : rewritten) entries.push_back(std::move(entry));
        if (!Fast::Oot3d::WritePicaGraphicsPipelineManifest(options.PipelineManifest, schema, entries, &error))
            throw std::runtime_error(error);
        totalFailures += unlinked;
        recipes = {{"input", input},
                   {"written", entries.size()},
                   {"unlinked", unlinked},
                   {"ambiguous", ambiguous},
                   {"output_contract_changed", contractChanged}};
    }

    const Json report = {{"format", "oot3d_pica_variant_replay_v1"},
                         {"sessions", sessions},
                         {"effective_shaders", inventory.EntryCount()},
                         {"recipes", recipes},
                         {"failures", totalFailures},
                         {"witness_mismatches", totalWitnessMismatches},
                         {"require_witness", options.RequireWitness}};
    std::ofstream(options.Report) << report.dump(2) << '\n';
    if (!options.InferReport.empty()) {
        Json semantics = Json::object();
        for (const auto& [name, entry] : inferred) {
            Json byStage = {{"vertex", 0}, {"fragment", 0}, {"nri_fragment", 0}};
            for (const auto& [stage, id] : entry.Modules)
                byStage[stage == 1U ? "vertex" : stage == 2U ? "fragment" : "nri_fragment"] =
                    byStage[stage == 1U ? "vertex" : stage == 2U ? "fragment" : "nri_fragment"].get<uint64_t>() + 1U;
            semantics[name] = {{"candidates", entry.Candidates}, {"rejected", entry.Rejected},
                               {"new_modules", byStage}, {"new_source_bytes", entry.SourceBytes}};
        }
        const Json inferredReport = {
            {"format", "oot3d_pica_inferred_coverage_dry_run_v1"},
            {"observed_modules", observedModules.size()},
            {"observed_source_bytes", observedBytes},
            {"inferred_new_modules", inferredModules.size()},
            {"inferred_new_source_bytes", inferredBytes},
            {"semantics", semantics}};
        std::ofstream(options.InferReport) << inferredReport.dump(2) << '\n';
    }
    std::cout << report.dump(2) << '\n';
    if (totalFailures != 0U) return 3;
    if (options.RequireWitness && totalWitnessMismatches != 0U) return 4;
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto next = [&]() -> std::filesystem::path {
            if (index + 1 >= argc) throw std::runtime_error(argument + " needs a value");
            return argv[++index];
        };
        if (argument == "--session") {
            auto raw = next();
            options.Sessions.emplace_back(raw, next());
        } else if (argument == "--pipelines") {
            options.Pipelines.push_back(next());
        } else if (argument == "--shader-inventory") {
            options.ShaderInventory = next();
        } else if (argument == "--pipeline-manifest") {
            options.PipelineManifest = next();
        } else if (argument == "--report") {
            options.Report = next();
        } else if (argument == "--require-witness") {
            options.RequireWitness = true;
        } else if (argument == "--infer-report") {
            options.InferReport = next();
        } else {
            return Usage();
        }
    }
    if (options.Sessions.empty() || options.ShaderInventory.empty() || options.Report.empty()) return Usage();
    try {
        return Run(options);
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_pica_variant_inventory: " << exception.what() << '\n';
        return 1;
    }
}
